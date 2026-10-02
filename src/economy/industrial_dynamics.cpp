#include "industrial_dynamics.hpp"

#include "actors/organizations/organizations.hpp"
#include "actors/ownership.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/banking/banking.hpp"
#include "economy/capital_market.hpp"
#include "economy/capital_projects.hpp"
#include "economy/exact_person_economy.hpp"
#include "economy/households.hpp"
#include "economy/investment_ranking.hpp"
#include "economy/physical/concrete_market.hpp"
#include "economy/physical/exchange.hpp"
#include "economy/physical/extraction.hpp"
#include "economy/physical/factory_inputs.hpp"
#include "economy/physical/inventory.hpp"
#include "economy/physical/land.hpp"
#include "economy/relations/relations.hpp"
#include "economy/wallets.hpp"
#include "system_state.hpp"
#include "world/site.hpp"
#include "world/spatial_runtime.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <optional>
#include <unordered_set>
#include <vector>

namespace economy::industrial_dynamics {
namespace {

using actors::ownership::actor_kind;
using economy::relations::obligation_status;

constexpr float epsilon = 1.0e-5f;
constexpr uint8_t lifecycle_active = 0;
constexpr uint8_t lifecycle_restructuring = 1;
constexpr uint8_t lifecycle_bankrupt = 2;
constexpr uint8_t lifecycle_liquidated = 3;
constexpr uint8_t lifecycle_closed = 4;

// The day's investors, built on first use: every offering of the day draws on
// the same commitments.
struct day_pool {
	std::optional<capital_market::pool> investors;
	capital_market::pool& get(sys::state const& state) {
		if(!investors) investors = capital_market::build_pool(state);
		return *investors;
	}
};

bool eligible_sponsor(sys::state const& state, dcon::economic_actor_id actor) {
	if(!actor || !state.world.economic_actor_is_valid(actor)) return false;
	auto kind = actor_kind(state.world.economic_actor_get_kind(actor));
	return kind == actor_kind::company || kind == actor_kind::fund
		|| kind == actor_kind::cooperative || kind == actor_kind::person;
}

bool review_due(sys::state const& state, dcon::economic_actor_id actor) {
	auto last = state.world.economic_actor_get_industrial_last_decision_date(actor);
	if(last) return state.current_date.to_raw_value() - last.to_raw_value() >= investor_review_days;
	// First reviews are spread over the period instead of all falling on one day.
	return (state.current_date.to_raw_value() + int32_t(actor.index())) % investor_review_days == 0;
}

dcon::monetary_account_id actor_account(sys::state& state, dcon::economic_actor_id actor,
	dcon::commodity_id settlement) {
	auto account = accounts::find_account(state, actor, settlement);
	return account ? account : accounts::open_account(state, actor, settlement);
}

dcon::nation_id nation_of_province(sys::state const& state, dcon::province_id province) {
	return province ? state.world.province_get_nation_from_province_ownership(province) : dcon::nation_id{};
}

dcon::nation_id nation_of_factory(sys::state const& state, dcon::factory_id factory) {
	return nation_of_province(state, world::site::province_for_site(state, world::site::site_for_factory(state, factory)));
}

// Moves deposit money into the actor's operating wallet until its free cash
// covers `amount`. Returns the free cash afterwards.
float cash_in_wallet(sys::state& state, dcon::economic_actor_id actor, dcon::commodity_id settlement, float amount) {
	auto wallet = wallets::open_for(state, actor, settlement);
	auto cash = wallets::spendable(state, wallet);
	if(cash + epsilon >= amount) return cash;
	if(auto deposit = economy::banking::deposit_account_for(state, actor, settlement))
		(void)economy::banking::withdraw_cash(state, deposit, wallet, amount - cash, state.current_date);
	return wallets::spendable(state, wallet);
}

// What it would cost to build the plant's capacity again at today's prices.
float replacement_value(sys::state const& state, dcon::factory_id factory) {
	if(!factory || !state.world.factory_is_valid(factory)) return 0.0f;
	auto type = state.world.factory_get_building_type(factory);
	auto market = physical::concrete_market::market_for_site(state, world::site::site_for_factory(state, factory));
	if(!type || !market) return 0.0f;
	auto capacity = std::max(0.0f, state.world.factory_get_productive_capacity(factory));
	float value = 0.0f;
	auto const& construction = state.world.factory_type_get_construction_costs(type);
	for(uint32_t i = 0; i < economy::commodity_set::set_size; ++i) {
		auto commodity = construction.commodity_type[i];
		if(!commodity) break;
		if(physical::factory_inputs::ordinary_physical_input(state, commodity))
			value += std::max(0.0f, construction.commodity_amounts[i]) * capacity * 0.5f
				* physical::concrete_market::canonical_reference_price(state, market, commodity, state.current_date, 0.0f);
	}
	return std::isfinite(value) ? value : 0.0f;
}

std::vector<dcon::obligation_id> factory_loans(sys::state const& state, dcon::factory_id factory) {
	std::vector<dcon::obligation_id> loans;
	state.world.factory_for_each_obligation_factory_as_factory(factory,
		[&](dcon::obligation_factory_id relation) {
			auto loan = state.world.obligation_factory_get_obligation(relation);
			if(loan && state.world.obligation_get_kind(loan) == uint8_t(relations::obligation_kind::loan))
				loans.push_back(loan);
		});
	return loans;
}

bool has_defaulted_factory_loan(sys::state const& state, dcon::factory_id factory) {
	for(auto loan : factory_loans(state, factory))
		if(state.world.obligation_get_status(loan) == uint8_t(obligation_status::defaulted)) return true;
	return false;
}

float total_factory_debt(sys::state const& state, dcon::factory_id factory) {
	float result = 0.0f;
	for(auto loan : factory_loans(state, factory)) {
		auto status = state.world.obligation_get_status(loan);
		if(status == uint8_t(obligation_status::active) || status == uint8_t(obligation_status::defaulted))
			result += relations::total_due(state, loan);
	}
	return result;
}

// Reopens a defaulted claim only for the settlement operation. Unpaid balances
// return to defaulted status; bankruptcy resolution writes them off explicitly.
float service_factory_claims(sys::state& state, dcon::factory_id factory,
	dcon::monetary_account_id debtor_account, float limit, bool defaulted_only, bool write_off_remainder) {
	float recovered = 0.0f;
	if(!debtor_account || limit <= epsilon) return recovered;
	auto borrower = accounts::owner_of(state, debtor_account);
	for(auto loan : factory_loans(state, factory)) {
		auto original_status = state.world.obligation_get_status(loan);
		if(original_status != uint8_t(obligation_status::active)
			&& original_status != uint8_t(obligation_status::defaulted)) continue;
		if(defaulted_only && original_status != uint8_t(obligation_status::defaulted)) continue;
		if(state.world.obligation_get_economic_actor_from_obligation_debtor(loan) != borrower) continue;
		auto creditor = state.world.obligation_get_economic_actor_from_obligation_creditor(loan);
		auto settlement = state.world.obligation_get_settlement_commodity(loan);
		auto creditor_account = accounts::find_account(state, creditor, settlement);
		if(!creditor_account) creditor_account = accounts::open_account(state, creditor, settlement);
		if(!creditor_account) continue;
		if(original_status == uint8_t(obligation_status::defaulted))
			state.world.obligation_set_status(loan, uint8_t(obligation_status::active));
		if(accounts::settlement_of(state, debtor_account) != settlement) {
			if(original_status == uint8_t(obligation_status::defaulted))
				state.world.obligation_set_status(loan, uint8_t(obligation_status::defaulted));
			continue;
		}
		auto accepted = std::min({limit - recovered, relations::total_due(state, loan),
			accounts::balance(state, debtor_account)});
		float paid = 0.0f;
		if(accepted > epsilon && accounts::settle_obligation_payment(state, loan, debtor_account,
			creditor_account, accepted, state.current_date)) paid = accepted;
		if(state.world.obligation_get_status(loan) == uint8_t(obligation_status::active)) {
			if(write_off_remainder) {
				(void)relations::write_off(state, loan);
			} else if(original_status == uint8_t(obligation_status::defaulted)) {
				state.world.obligation_set_status(loan, uint8_t(obligation_status::defaulted));
			}
		}
		recovered += paid;
		if(recovered + epsilon >= limit) break;
	}
	return recovered;
}

void liquidate_unrecovered_factory_loans(sys::state& state, dcon::factory_id factory) {
	for(auto loan : factory_loans(state, factory)) {
		auto status = state.world.obligation_get_status(loan);
		if(status == uint8_t(obligation_status::active) || status == uint8_t(obligation_status::defaulted)) {
			state.world.obligation_set_status(loan, uint8_t(obligation_status::active));
			(void)relations::write_off(state, loan);
		}
	}
}

void transfer_factory_title(sys::state& state, dcon::factory_id factory, dcon::economic_actor_id new_owner) {
	auto asset = actors::ownership::asset_for_factory(state, factory);
	if(!asset || !new_owner) return;
	actors::ownership::assign_runtime_canonical_id(state, new_owner);
	std::vector<dcon::ownership_stake_id> stakes;
	state.world.asset_for_each_ownership_stake_asset_as_asset(asset,
		[&](dcon::ownership_stake_asset_id relation) {
			stakes.push_back(state.world.ownership_stake_asset_get_ownership_stake(relation));
		});
	for(auto stake : stakes)
		if(stake && state.world.ownership_stake_is_valid(stake)) state.world.delete_ownership_stake(stake);
	auto stake = actors::ownership::create_stake(state, new_owner, asset, 1.0f, 1.0f, 1.0f);
	actors::ownership::assign_runtime_canonical_id(state, stake);
}

void transfer_factory_inventory(sys::state& state, dcon::factory_id factory,
	dcon::economic_actor_id seller, dcon::economic_actor_id buyer) {
	auto site = world::site::site_for_factory(state, factory);
	if(!site || !seller || !buyer || seller == buyer) return;
	std::vector<std::pair<dcon::commodity_id, float>> stocks;
	state.world.site_for_each_physical_stock_site_as_site(site, [&](dcon::physical_stock_site_id relation) {
		auto stock = state.world.physical_stock_site_get_physical_stock(relation);
		auto owner_relation = state.world.physical_stock_get_physical_stock_owner(stock);
		if(!owner_relation || state.world.physical_stock_owner_get_economic_actor(owner_relation) != seller) return;
		auto quantity = std::max(0.0f, state.world.physical_stock_get_quantity(stock));
		auto commodity = state.world.physical_stock_get_commodity_from_physical_stock_commodity(stock);
		if(quantity > epsilon && commodity) stocks.emplace_back(commodity, quantity);
	});
	for(auto const& [commodity, quantity] : stocks)
		(void)physical::inventory::transfer(state, site, commodity, seller, buyer, quantity);
}

void close_factory(sys::state& state, dcon::factory_id factory) {
	if(!factory || !state.world.factory_is_valid(factory)) return;
	state.world.factory_set_agency_liquidation_value(factory, replacement_value(state, factory));
	auto site = world::site::site_for_factory(state, factory);
	auto owner = actors::organizations::operator_actor_for_factory(state, factory);
	for(auto contract : exact_person_economy::active_contracts_for_factory(state, factory))
		(void)exact_person_economy::end_contract(state, contract,
			exact_person_economy::contract_status::terminated, state.current_date);
	// Goods at a closed plant stay the operator's property; closing a plant
	// destroys nothing.
	(void)site;
	auto account = owner ? accounts::find_account(state, owner,
		state.world.factory_get_payroll_settlement(factory)) : dcon::monetary_account_id{};
	if(account) (void)service_factory_claims(state, factory, account,
		std::numeric_limits<float>::max(), false, true);
	liquidate_unrecovered_factory_loans(state, factory);
	auto asset = actors::ownership::asset_for_factory(state, factory);
	if(asset) {
		std::vector<dcon::ownership_stake_id> stakes;
		state.world.asset_for_each_ownership_stake_asset_as_asset(asset,
			[&](dcon::ownership_stake_asset_id relation) {
				stakes.push_back(state.world.ownership_stake_asset_get_ownership_stake(relation));
			});
		for(auto stake : stakes)
			if(stake && state.world.ownership_stake_is_valid(stake)) state.world.delete_ownership_stake(stake);
		state.world.factory_remove_factory_asset(factory);
		state.world.delete_asset(asset);
	}
	state.world.factory_set_productive_capacity(factory, 0.0f);
	state.world.factory_set_size(factory, 0.0f);
	state.world.factory_set_actual_utilization(factory, 0.0f);
	state.world.factory_set_target_utilization(factory, 0.0f);
	state.world.factory_set_agency_lifecycle_status(factory, lifecycle_closed);
	state.world.factory_set_agency_last_lifecycle_date(factory, state.current_date);
}

bool restructure_defaulted_factory(sys::state& state, dcon::factory_id factory) {
	bool changed = false;
	for(auto loan : factory_loans(state, factory)) {
		if(state.world.obligation_get_status(loan) != uint8_t(obligation_status::defaulted)) continue;
		if(state.world.obligation_get_restructuring_count(loan) >= 1) continue;
		state.world.obligation_set_status(loan, uint8_t(obligation_status::active));
		state.world.obligation_set_due_date(loan, state.current_date + 180);
		state.world.obligation_set_last_interest_accrual_date(loan, state.current_date);
		state.world.obligation_set_annual_interest_rate(loan,
			std::max(0.01f, state.world.obligation_get_annual_interest_rate(loan) * 0.90f));
		state.world.obligation_set_restructuring_count(loan,
			uint8_t(state.world.obligation_get_restructuring_count(loan) + 1));
		changed = true;
	}
	if(changed) {
		state.world.factory_set_agency_restructuring_count(factory,
			uint8_t(state.world.factory_get_agency_restructuring_count(factory) + 1));
		state.world.factory_set_agency_lifecycle_status(factory, lifecycle_restructuring);
		state.world.factory_set_agency_last_lifecycle_date(factory, state.current_date);
	}
	return changed;
}

// A plant is for sale only when its operator puts it up: in a bankruptcy
// auction whose price falls over the sale window, or when a distressed operator
// without defaulted debt divests. A sound plant is never sold off.
float asking_price(sys::state const& state, dcon::factory_id factory) {
	if(!factory || !state.world.factory_is_valid(factory)) return 0.0f;
	// Buying the plant would not transfer control of its deposit or land, and a
	// household's livelihood is not for sale.
	if(physical::extraction::extracts_deposit(state, factory) || physical::land::farms_land(state, factory)
		|| households::is_household(state, actors::organizations::operator_organization_for_factory(state, factory)))
		return 0.0f;
	auto value = replacement_value(state, factory);
	if(value <= epsilon) return 0.0f;
	auto lifecycle = state.world.factory_get_agency_lifecycle_status(factory);
	if(lifecycle == lifecycle_bankrupt) {
		auto since = state.current_date.to_raw_value()
			- state.world.factory_get_agency_last_lifecycle_date(factory).to_raw_value();
		auto elapsed = std::clamp(float(since) / float(bankruptcy_sale_window_days), 0.0f, 1.0f);
		return value * (auction_opening_share - (auction_opening_share - auction_floor_share) * elapsed);
	}
	if(lifecycle < lifecycle_bankrupt && state.world.factory_get_agency_distress_days(factory) >= divestment_distress_days
		&& state.world.factory_get_agency_recent_profit(factory) < 0.0f && !has_defaulted_factory_loan(state, factory))
		return value * auction_opening_share;
	return 0.0f;
}

// The proceeds of a plant sale go to its creditors first, then to the owners
// of the plant in proportion to their economic stakes.
void pay_plant_owners(sys::state& state, dcon::factory_id factory, dcon::economic_actor_id seller,
	dcon::monetary_account_id seller_account, float proceeds) {
	auto asset = actors::ownership::asset_for_factory(state, factory);
	auto settlement = accounts::settlement_of(state, seller_account);
	if(!asset || !settlement || proceeds <= epsilon) return;
	struct holder { dcon::economic_actor_id owner; float fraction; };
	std::vector<holder> holders;
	float total = 0.0f;
	state.world.asset_for_each_ownership_stake_asset_as_asset(asset, [&](dcon::ownership_stake_asset_id relation) {
		auto stake = state.world.ownership_stake_asset_get_ownership_stake(relation);
		auto owner = state.world.ownership_stake_get_economic_actor_from_ownership_stake_owner(stake);
		auto fraction = state.world.ownership_stake_get_economic_fraction(stake);
		if(!owner || !(fraction > 0.0f)) return;
		holders.push_back({ owner, fraction });
		total += fraction;
	});
	if(!(total > 0.0f)) return;
	std::sort(holders.begin(), holders.end(), [](auto const& a, auto const& b) { return a.owner.index() < b.owner.index(); });
	auto available = std::min(proceeds, std::max(0.0f, accounts::balance(state, seller_account)));
	for(auto const& [owner, fraction] : holders) {
		if(owner == seller) continue; // the operator's own share stays where it is
		auto share = available * fraction / total;
		auto destination = wallets::open_for(state, owner, settlement);
		if(share > epsilon && destination)
			(void)wallets::pay(state, wallets::account_ref::from_dcon(seller_account), destination, share,
				relations::transaction_kind::purchase);
	}
}

bool buy_plant(sys::state& state, dcon::economic_actor_id buyer, dcon::organization_id buyer_org,
	dcon::factory_id factory, float price) {
	if(!buyer || !buyer_org || !factory || !state.world.factory_is_valid(factory)
		|| actors::organizations::operator_actor_for_factory(state, factory) == buyer) return false;
	auto seller = actors::organizations::operator_actor_for_factory(state, factory);
	auto settlement = state.world.factory_get_payroll_settlement(factory);
	if(!settlement) settlement = physical::exchange::settlement_for_purchase(state, buyer);
	if(!seller || !settlement || !std::isfinite(price) || price <= epsilon) return false;
	if(cash_in_wallet(state, buyer, settlement, price) + epsilon < price) return false;
	auto buyer_account = accounts::find_account(state, buyer, settlement);
	auto seller_account = actor_account(state, seller, settlement);
	if(!buyer_account || !seller_account || !accounts::transfer(state, buyer_account, seller_account, price,
		relations::transaction_kind::purchase, state.current_date)) return false;

	transfer_factory_inventory(state, factory, seller, buyer);
	// Secured factory debt stays with the seller: the proceeds fund the
	// creditor waterfall, and any unrecovered claim is a bank loss.
	auto recovered = service_factory_claims(state, factory, seller_account, price, false, true);
	pay_plant_owners(state, factory, seller, seller_account, std::max(0.0f, price - recovered));
	transfer_factory_title(state, factory, buyer);
	if(!actors::organizations::transfer_factory_operator(state, buyer_org, factory)) return false;
	state.world.factory_set_agency_lifecycle_status(factory, lifecycle_active);
	state.world.factory_set_agency_liquidation_value(factory, price);
	state.world.factory_set_agency_bankruptcy_date(factory, sys::date{});
	state.world.factory_set_agency_last_lifecycle_date(factory, state.current_date);
	state.world.factory_set_agency_restructuring_count(factory, 0);
	state.world.factory_set_agency_distress_days(factory, 0);
	return true;
}

// A defaulted firm offers new shares to cure its default. Investors buy only if
// the firm's expected earnings justify the price; existing owners are diluted.
void offer_recapitalization(sys::state& state, dcon::factory_id factory, day_pool& pool) {
	auto organization = actors::organizations::operator_organization_for_factory(state, factory);
	auto firm = actors::organizations::actor_for_organization(state, organization);
	auto settlement = state.world.factory_get_payroll_settlement(factory);
	if(!organization || !firm || !settlement) return;
	float defaulted = 0.0f;
	for(auto loan : factory_loans(state, factory))
		if(state.world.obligation_get_status(loan) == uint8_t(obligation_status::defaulted))
			defaulted += relations::total_due(state, loan);
	if(defaulted <= epsilon) return;
	capital_market::offering terms{};
	terms.issuer = organization;
	terms.nation = nation_of_factory(state, factory);
	terms.settlement = settlement;
	terms.amount = defaulted;
	terms.pre_money = capital_market::book_value(state, organization);
	terms.annual_earnings = capital_market::expected_annual_earnings(state, organization);
	auto raised = capital_market::raise(state, pool.get(state), terms);
	if(raised <= epsilon) return;
	auto account = actor_account(state, firm, settlement);
	(void)service_factory_claims(state, factory, account, raised, true, false);
}

void process_insolvency(sys::state& state, dcon::factory_id factory, day_pool& pool) {
	if(!factory || !state.world.factory_is_valid(factory)) return;
	// A household's farm is its livelihood, not a firm that can be liquidated.
	if(households::is_household(state, actors::organizations::operator_organization_for_factory(state, factory))) return;
	auto defaulted = has_defaulted_factory_loan(state, factory);
	auto status = state.world.factory_get_agency_lifecycle_status(factory);
	if(defaulted && status < lifecycle_bankrupt) {
		if(!state.world.factory_get_agency_bankruptcy_date(factory))
			state.world.factory_set_agency_bankruptcy_date(factory, state.current_date);
		auto last_recap = state.world.factory_get_agency_last_recapitalization_date(factory);
		if(!last_recap || state.current_date.to_raw_value() - last_recap.to_raw_value() >= 30) {
			offer_recapitalization(state, factory, pool);
			state.world.factory_set_agency_last_recapitalization_date(factory, state.current_date);
		}
		if(has_defaulted_factory_loan(state, factory)) (void)restructure_defaulted_factory(state, factory);
		if(has_defaulted_factory_loan(state, factory)) {
			auto since_default = state.current_date.to_raw_value()
				- state.world.factory_get_agency_last_lifecycle_date(factory).to_raw_value();
			if(status != lifecycle_restructuring || since_default >= 180
				|| state.world.factory_get_agency_distress_days(factory) >= 150) {
				state.world.factory_set_agency_lifecycle_status(factory, lifecycle_bankrupt);
				state.world.factory_set_agency_last_lifecycle_date(factory, state.current_date);
				state.world.factory_set_actual_utilization(factory, 0.0f);
				state.world.factory_set_target_utilization(factory, 0.0f);
				state.world.factory_set_agency_planned_units(factory, 0.0f);
			}
		}
	} else if(!defaulted && status == lifecycle_restructuring
		&& state.world.factory_get_agency_recent_profit(factory) > 0.0f) {
		state.world.factory_set_agency_lifecycle_status(factory, lifecycle_active);
	}

	status = state.world.factory_get_agency_lifecycle_status(factory);
	if(status == lifecycle_bankrupt) {
		auto last_change = state.world.factory_get_agency_last_lifecycle_date(factory);
		if(last_change && state.current_date.to_raw_value() - last_change.to_raw_value() >= bankruptcy_sale_window_days) {
			close_factory(state, factory);
			state.world.factory_set_agency_lifecycle_status(factory, lifecycle_liquidated);
		}
	} else if(state.world.factory_get_agency_distress_days(factory) >= 210
		&& state.world.factory_get_agency_recent_profit(factory) < 0.0f
		&& total_factory_debt(state, factory) <= epsilon) {
		state.world.factory_set_agency_lifecycle_status(factory, lifecycle_bankrupt);
		state.world.factory_set_agency_last_lifecycle_date(factory, state.current_date);
		state.world.factory_set_actual_utilization(factory, 0.0f);
		state.world.factory_set_target_utilization(factory, 0.0f);
		state.world.factory_set_agency_planned_units(factory, 0.0f);
	}
}

struct project_opportunity {
	dcon::province_id province{};
	dcon::nation_id nation{};
	dcon::site_id site{};
	dcon::factory_type_id type{};
	float project_budget = 0.0f;
	float working_capital = 0.0f;
	economy::investment::project_inputs inputs{};
};

uint64_t opportunity_key(dcon::province_id province, dcon::factory_type_id type) {
	return (uint64_t(province.index()) << 32) | uint64_t(type.index());
}

dcon::factory_id profitable_collateral_factory(sys::state const& state,
	dcon::organization_id organization) {
	dcon::factory_id selected{};
	float selected_value = 0.0f;
	for(auto factory : actors::organizations::factories_operated_by(state, organization)) {
		if(!factory || !state.world.factory_is_valid(factory)
			|| state.world.factory_get_agency_lifecycle_status(factory) >= lifecycle_bankrupt
			|| capital_market::expected_daily_profit(state, factory) <= epsilon) continue;
		auto value = replacement_value(state, factory);
		if(value > selected_value) {
			selected = factory;
			selected_value = value;
		}
	}
	return selected;
}

void scale_greenfield_project(sys::state& state, dcon::capital_project_id project, float funding_fraction) {
	auto fraction = std::clamp(funding_fraction, 0.05f, 1.0f);
	state.world.capital_project_set_planned_daily_capacity(project,
		state.world.capital_project_get_planned_daily_capacity(project) * fraction);
	state.world.capital_project_for_each_capital_project_requirement_project_as_capital_project(project,
		[&](dcon::capital_project_requirement_project_id relation) {
			auto requirement = state.world.capital_project_requirement_project_get_capital_project_requirement(relation);
			state.world.capital_project_requirement_set_required_quantity(requirement,
				state.world.capital_project_requirement_get_required_quantity(requirement) * fraction);
		});
}

// Plants already standing and plants already being built: a second investor
// does not duplicate either.
std::unordered_set<uint64_t> occupied_opportunities(sys::state const& state) {
	std::unordered_set<uint64_t> result;
	state.world.for_each_factory([&](dcon::factory_id factory) {
		auto province = world::site::province_for_site(state, world::site::site_for_factory(state, factory));
		auto type = state.world.factory_get_building_type(factory);
		if(province && type && state.world.factory_get_productive_capacity(factory) > 0.0f)
			result.insert(opportunity_key(province, type));
	});
	state.world.for_each_capital_project([&](dcon::capital_project_id project) {
		if(state.world.capital_project_get_status(project) >= uint8_t(capital_projects::status::completed)
			|| state.world.capital_project_get_project_kind(project) != uint8_t(capital_projects::project_kind::factory)) return;
		auto province = world::site::province_for_site(state, state.world.capital_project_get_site_from_capital_project_site(project));
		auto type = state.world.capital_project_get_factory_type(project);
		if(province && type) result.insert(opportunity_key(province, type));
	});
	return result;
}

// The daily wage a new plant must offer to recruit: above what members of the
// local cohorts earn working for themselves, and never below the bootstrap offer.
float recruitment_wage(sys::state const& state, dcon::province_id province, float unit_revenue) {
	float reservation = 0.0f;
	for(auto cohort_role : { households::role::peasant, households::role::urban })
		reservation = std::max(reservation, households::reservation_wage(state, households::household_for(state, province, cohort_role)));
	return std::max(reservation * 1.1f, unit_revenue * 0.075f);
}

std::vector<project_opportunity> greenfield_opportunities(sys::state const& state,
	std::unordered_set<uint64_t> const& occupied) {
	std::vector<project_opportunity> opportunities;
	state.world.for_each_province([&](dcon::province_id province) {
		auto market_relation = state.world.province_get_state_membership(province);
		auto market = market_relation ? state.world.state_instance_get_market_from_local_market(market_relation) : dcon::market_id{};
		auto nation = nation_of_province(state, province);
		auto project_site = world::spatial_runtime::site_for_province(state, province);
		if(!market || !nation || !project_site) return;
		state.world.for_each_factory_type([&](dcon::factory_type_id type) {
			auto output = state.world.factory_type_get_output(type);
			// An extraction plant exists only on a deposit its operator controls,
			// a farm only on land, and a workshop only in a cohort.
			if(!output || physical::extraction::extracts_deposit(state, type)
				|| physical::land::farms_land(state, type) || households::is_craft(state, type)) return;
			if(occupied.contains(opportunity_key(province, type))) return;
			constexpr float planned_capacity = 0.5f;
			auto output_price = physical::concrete_market::canonical_reference_price(state, market,
				output, state.current_date, 0.0f);
			auto unit_revenue = std::max(0.0f, state.world.factory_type_get_output_amount(type)) * output_price;
			if(unit_revenue <= epsilon) return;
			auto sell_through = std::clamp(physical::concrete_market::observed_sell_through(state, market,
				output, state.current_date, 0.65f), 0.15f, 0.95f);
			float material_unit_cost = 0.0f;
			auto const& inputs = state.world.factory_type_get_inputs(type);
			for(uint32_t i = 0; i < economy::commodity_set::set_size; ++i) {
				auto commodity = inputs.commodity_type[i];
				if(!commodity) break;
				if(physical::factory_inputs::ordinary_physical_input(state, commodity))
					material_unit_cost += std::max(0.0f, inputs.commodity_amounts[i])
						* physical::concrete_market::canonical_reference_price(state, market, commodity, state.current_date, 0.0f);
			}
			float construction_cost = 0.0f;
			auto const& construction = state.world.factory_type_get_construction_costs(type);
			for(uint32_t i = 0; i < economy::commodity_set::set_size; ++i) {
				auto commodity = construction.commodity_type[i];
				if(!commodity) break;
				if(physical::factory_inputs::ordinary_physical_input(state, commodity))
					construction_cost += std::max(0.0f, construction.commodity_amounts[i]) * planned_capacity * 0.5f
						* physical::concrete_market::canonical_reference_price(state, market, commodity, state.current_date, 0.0f);
			}
			// A recipe without a construction bill cannot be built.
			if(construction_cost <= epsilon) return;
			project_opportunity opportunity{};
			opportunity.province = province;
			opportunity.nation = nation;
			opportunity.site = project_site;
			opportunity.type = type;
			auto& terms = opportunity.inputs;
			terms.capital_cost = construction_cost;
			terms.gross_daily_revenue = unit_revenue * planned_capacity;
			terms.daily_material_cost = material_unit_cost * planned_capacity * sell_through;
			terms.daily_wage_cost = recruitment_wage(state, province, unit_revenue) * planned_capacity * sell_through;
			terms.expected_sell_through = sell_through;
			terms.demand_risk = 1.0f - sell_through;
			terms.jobs = planned_capacity;
			// Materials are bid up to 20% over reference and must be carried here.
			opportunity.project_budget = construction_cost * 1.5f;
			opportunity.working_capital = std::max(1.0f, (terms.daily_material_cost + terms.daily_wage_cost) * 30.0f);
			if(economy::investment::evaluate(terms).expected_daily_cashflow > 0.0f)
				opportunities.push_back(opportunity);
		});
	});
	return opportunities;
}

economy::investment::project_score evaluate_for(project_opportunity const& opportunity, float required_return) {
	auto terms = opportunity.inputs;
	terms.annual_interest_rate = required_return;
	return economy::investment::evaluate(terms);
}

struct plant_offer {
	dcon::factory_id factory{};
	float price = 0.0f;
	float excess_return = -std::numeric_limits<float>::infinity();
};

plant_offer best_plant_offer(sys::state const& state, dcon::economic_actor_id buyer, dcon::nation_id nation,
	dcon::commodity_id settlement, float available, float required_return) {
	plant_offer best{};
	state.world.for_each_factory([&](dcon::factory_id factory) {
		if(actors::organizations::operator_actor_for_factory(state, factory) == buyer
			|| nation_of_factory(state, factory) != nation) return;
		if(state.world.factory_get_payroll_settlement(factory)
			&& state.world.factory_get_payroll_settlement(factory) != settlement) return;
		auto price = asking_price(state, factory);
		if(price <= epsilon || price > available) return;
		auto excess = capital_market::expected_daily_profit(state, factory) * 365.0f / price - required_return;
		if(excess > best.excess_return) best = { factory, price, excess };
	});
	return best;
}

// The buyer's company: its own organization, or a company founded and wholly
// owned by a person buyer, capitalized with the price.
bool execute_purchase(sys::state& state, dcon::economic_actor_id investor, dcon::commodity_id settlement,
	plant_offer const& offer) {
	if(auto organization = actors::organizations::organization_for_actor(state, investor))
		return buy_plant(state, investor, organization, offer.factory, offer.price);
	auto organization = actors::organizations::create_company(state);
	if(!organization) return false;
	auto company = actors::organizations::actor_for_organization(state, organization);
	auto company_account = actor_account(state, company, settlement);
	if(!company_account || !capital_market::pay(state, investor, settlement,
		wallets::account_ref::from_dcon(company_account), offer.price, relations::transaction_kind::equity_contribution)) return false;
	actors::ownership::assign_runtime_canonical_id(state, investor);
	if(!actors::ownership::issue_equity(state,
		actors::organizations::equity_asset_for_organization(state, organization), investor, offer.price, 0.0f)) return false;
	return buy_plant(state, company, organization, offer.factory, offer.price);
}

// Finances a greenfield plant from the sponsor's own funds, then bank credit
// against a profitable plant it already runs, then new shares sold to the
// investors of its country. A person founds a company for the project.
bool execute_greenfield(sys::state& state, dcon::economic_actor_id investor, dcon::commodity_id settlement,
	float own, project_opportunity const& opportunity, day_pool& pool) {
	auto required = opportunity.project_budget + opportunity.working_capital;
	auto organization = actors::organizations::organization_for_actor(state, investor);
	auto founding = !organization;
	own = std::min(own, required);
	if(founding && own + epsilon < founder_minimum_share * required) return false;
	auto annual_earnings = evaluate_for(opportunity, 0.0f).expected_daily_cashflow * 365.0f;

	capital_market::offering terms{};
	terms.nation = opportunity.nation;
	terms.settlement = settlement;
	terms.amount = required - own;
	terms.pre_money = founding ? own : capital_market::book_value(state, organization);
	terms.annual_earnings = annual_earnings
		+ (founding ? 0.0f : capital_market::expected_annual_earnings(state, organization));
	terms.issuer = organization;
	auto& investors = pool.get(state);
	capital_market::commit(investors, investor, settlement, own);
	auto planned = terms.amount > epsilon ? capital_market::total(capital_market::subscriptions(state, investors, terms)) : 0.0f;
	auto collateral = organization ? profitable_collateral_factory(state, organization) : dcon::factory_id{};
	if(own + planned + epsilon < minimum_funded_share * required && !collateral) return false;

	dcon::economic_actor_id sponsor = investor;
	if(founding) {
		organization = actors::organizations::create_company(state);
		if(!organization) return false;
		sponsor = actors::organizations::actor_for_organization(state, organization);
		auto startup = actor_account(state, sponsor, settlement);
		if(!startup || !capital_market::pay(state, investor, settlement, wallets::account_ref::from_dcon(startup), own,
			relations::transaction_kind::equity_contribution)) return false;
		actors::ownership::assign_runtime_canonical_id(state, investor);
		if(!actors::ownership::issue_equity(state, actors::organizations::equity_asset_for_organization(state, organization),
			investor, own, 0.0f)) return false;
		terms.issuer = organization;
	}
	if(!actors::organizations::is_economic_kind(actor_kind(state.world.organization_get_kind(organization)))) return false;
	auto source = actor_account(state, sponsor, settlement);
	auto project = capital_projects::create_greenfield_factory(state, sponsor, organization,
		opportunity.site, opportunity.type, settlement, 0.5f);
	if(!project || !source) return false;

	float borrowed = 0.0f;
	if(collateral && own + planned + epsilon < required) {
		auto credit = economy::banking::underwrite_factory_credit(state, collateral, source, 1,
			std::min(opportunity.project_budget, required - own - planned),
			evaluate_for(opportunity, 0.0f).annual_return_on_capital,
			replacement_value(state, collateral), project);
		borrowed = credit.funded_amount;
	}
	float raised = 0.0f;
	if(required - own - borrowed > epsilon) {
		terms.amount = required - own - borrowed;
		raised = capital_market::raise(state, investors, terms);
	}
	auto funded = std::min(required, own + borrowed + raised);
	auto fraction = funded / required;
	if(fraction + epsilon < minimum_funded_share) {
		(void)capital_projects::cancel(state, project);
		return false;
	}
	auto budget = opportunity.project_budget * fraction;
	if(cash_in_wallet(state, sponsor, settlement, budget) + epsilon < budget
		|| !capital_projects::fund(state, project, source, std::min(budget, wallets::spendable(state, wallets::account_ref::from_dcon(source))))) {
		(void)capital_projects::cancel(state, project);
		return false;
	}
	if(fraction + epsilon < 1.0f) scale_greenfield_project(state, project, fraction);
	return true;
}

void process_investor(sys::state& state, dcon::economic_actor_id investor,
	std::vector<project_opportunity> const& opportunities, std::unordered_set<uint64_t>& occupied, day_pool& pool) {
	state.world.economic_actor_set_industrial_last_decision_date(investor, state.current_date);
	auto settlement = capital_market::settlement_of(state, investor);
	if(!settlement) return;
	auto nation = capital_market::nation_of(state, investor, settlement);
	auto own = capital_market::investable_funds(state, investor, settlement);
	if(!nation || own <= 1.0f) return;
	auto required_return = capital_market::required_return(state, investor, settlement);

	project_opportunity const* greenfield = nullptr;
	float greenfield_excess = 0.0f;
	for(auto const& candidate : opportunities) {
		if(candidate.nation != nation || occupied.contains(opportunity_key(candidate.province, candidate.type))) continue;
		auto score = evaluate_for(candidate, required_return);
		if(score.privately_viable && score.risk_adjusted_private > greenfield_excess) {
			greenfield = &candidate;
			greenfield_excess = score.risk_adjusted_private;
		}
	}
	auto purchase = best_plant_offer(state, investor, nation, settlement, own, required_return);
	if(purchase.factory && purchase.excess_return > 0.0f && purchase.excess_return >= greenfield_excess) {
		if(execute_purchase(state, investor, settlement, purchase))
			capital_market::commit(pool.get(state), investor, settlement, purchase.price);
	} else if(greenfield
		&& execute_greenfield(state, investor, settlement, own, *greenfield, pool)) {
		occupied.insert(opportunity_key(greenfield->province, greenfield->type));
	}
}

} // namespace

float asking_price_for(sys::state const& state, dcon::factory_id factory) {
	return asking_price(state, factory);
}

void process(sys::state& state) {
	day_pool pool;
	std::vector<dcon::factory_id> factories;
	state.world.for_each_factory([&](dcon::factory_id factory) { factories.push_back(factory); });
	for(auto factory : factories) process_insolvency(state, factory, pool);
	std::vector<dcon::economic_actor_id> investors;
	state.world.for_each_economic_actor([&](dcon::economic_actor_id actor) {
		if(eligible_sponsor(state, actor) && review_due(state, actor)) investors.push_back(actor);
	});
	if(investors.empty()) return;
	auto occupied = occupied_opportunities(state);
	auto opportunities = greenfield_opportunities(state, occupied);
	for(auto investor : investors) process_investor(state, investor, opportunities, occupied, pool);
}

} // namespace economy::industrial_dynamics
