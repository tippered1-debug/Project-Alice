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
#include "economy/physical/exact_person_goods.hpp"
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
#include <unordered_map>
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

uint64_t market_key(dcon::market_id market, dcon::commodity_id commodity) {
	return (uint64_t(market.index()) << 32) | uint64_t(commodity.index());
}

// What each market is expected to absorb and to receive: the daily quantity
// buyers bid for over the demand window, and the daily output of standing
// plants and of projects still being built. An entrant's prospects fall as
// capacity outgrows demand, so competition, not a rule, ends entry.
struct market_outlook {
	std::unordered_map<uint64_t, float> demand;
	std::unordered_map<uint64_t, float> supply;
	float demand_for(dcon::market_id market, dcon::commodity_id commodity) const {
		auto it = demand.find(market_key(market, commodity));
		return it == demand.end() ? 0.0f : it->second;
	}
	float supply_for(dcon::market_id market, dcon::commodity_id commodity) const {
		auto it = supply.find(market_key(market, commodity));
		return it == supply.end() ? 0.0f : it->second;
	}
	void add_supply(dcon::market_id market, dcon::commodity_id commodity, float quantity) {
		if(market && commodity && std::isfinite(quantity) && quantity > 0.0f) supply[market_key(market, commodity)] += quantity;
	}
};

float daily_output_of(sys::state const& state, dcon::factory_id factory) {
	auto type = state.world.factory_get_building_type(factory);
	if(!type) return 0.0f;
	if(physical::extraction::extracts_deposit(state, factory))
		return physical::extraction::daily_ceiling(state, factory, state.current_date);
	// Farms and workshops produce what their land and labor allow.
	if(physical::land::farms_land(state, factory) || households::is_craft(state, type))
		return std::max(0.0f, state.world.factory_get_output(factory));
	auto productivity = state.world.factory_get_productivity_factor(factory);
	return std::max(0.0f, state.world.factory_get_productive_capacity(factory))
		* std::max(0.0f, state.world.factory_type_get_output_amount(type)) * (productivity > 0.0f ? productivity : 1.0f);
}

float planned_output_of(sys::state const& state, dcon::capital_project_id project) {
	auto kind = capital_projects::project_kind(state.world.capital_project_get_project_kind(project));
	auto type = state.world.capital_project_get_factory_type(project);
	if(kind == capital_projects::project_kind::extraction_plant) {
		auto deposit = state.world.capital_project_get_resource_deposit_from_capital_project_target_deposit(project);
		return deposit ? std::max(0.0f, state.world.resource_deposit_get_daily_extraction_capacity(deposit)) : 0.0f;
	}
	if((kind == capital_projects::project_kind::factory || kind == capital_projects::project_kind::factory_expansion) && type)
		return std::max(0.0f, state.world.capital_project_get_planned_daily_capacity(project))
			* std::max(0.0f, state.world.factory_type_get_output_amount(type));
	return 0.0f;
}

market_outlook build_outlook(sys::state const& state) {
	market_outlook result;
	auto from = state.current_date - demand_window_days;
	state.world.for_each_concrete_market_bid([&](dcon::concrete_market_bid_id bid) {
		auto created = state.world.concrete_market_bid_get_created_on(bid);
		if(created <= from || state.current_date < created) return;
		auto market = state.world.concrete_market_bid_get_market_from_concrete_bid_market(bid);
		auto commodity = state.world.concrete_market_bid_get_commodity_from_concrete_bid_commodity(bid);
		if(market && commodity)
			result.demand[market_key(market, commodity)] += std::max(0.0f, state.world.concrete_market_bid_get_original_quantity(bid));
	});
	physical::exact_person_goods::add_submitted_demand(state, from, state.current_date, result.demand);
	for(auto& [key, quantity] : result.demand) {
		(void)key;
		quantity /= float(demand_window_days);
	}
	state.world.for_each_factory([&](dcon::factory_id factory) {
		if(state.world.factory_get_agency_lifecycle_status(factory) >= lifecycle_bankrupt) return;
		auto type = state.world.factory_get_building_type(factory);
		auto market = physical::concrete_market::market_for_site(state, world::site::site_for_factory(state, factory));
		if(type) result.add_supply(market, state.world.factory_type_get_output(type), daily_output_of(state, factory));
	});
	state.world.for_each_capital_project([&](dcon::capital_project_id project) {
		if(state.world.capital_project_get_status(project) >= uint8_t(capital_projects::status::completed)) return;
		auto type = state.world.capital_project_get_factory_type(project);
		auto market = physical::concrete_market::market_for_site(state, state.world.capital_project_get_site_from_capital_project_site(project));
		if(type) result.add_supply(market, state.world.factory_type_get_output(type), planned_output_of(state, project));
	});
	return result;
}

// Average daily wage of the workers already hired in each province: an
// entrant competes for the same people.
std::unordered_map<uint32_t, float> prevailing_wages(sys::state const& state) {
	std::unordered_map<uint32_t, std::pair<double, double>> sums;
	state.world.for_each_factory([&](dcon::factory_id factory) {
		auto province = world::site::province_for_site(state, world::site::site_for_factory(state, factory));
		if(!province) return;
		for(auto contract_id : exact_person_economy::active_contracts_for_factory(state, factory)) {
			auto contract = exact_person_economy::contract(state, contract_id);
			if(!contract || contract->pay_period_days == 0 || !std::isfinite(contract->wage_rate)) continue;
			auto& [wages, labor] = sums[province.index()];
			wages += double(contract->wage_rate) * double(contract->labor_capacity) / double(contract->pay_period_days);
			labor += double(contract->labor_capacity);
		}
	});
	std::unordered_map<uint32_t, float> result;
	for(auto const& [province, sum] : sums)
		if(sum.second > 0.0) result[province] = float(sum.first / sum.second);
	return result;
}

// The daily wage a new plant must offer to recruit: what local plants already
// pay, more than members of the local cohorts earn working for themselves, and
// never below the bootstrap offer.
float recruitment_wage(sys::state const& state, std::unordered_map<uint32_t, float> const& prevailing,
	dcon::province_id province, float unit_revenue) {
	float reservation = 0.0f;
	for(auto cohort_role : { households::role::peasant, households::role::urban })
		reservation = std::max(reservation, households::reservation_wage(state, households::household_for(state, province, cohort_role)));
	auto local = prevailing.find(province.index());
	return std::max({ reservation * 1.1f, local == prevailing.end() ? 0.0f : local->second, unit_revenue * 0.075f });
}

float construction_cost(sys::state const& state, dcon::market_id market, dcon::factory_type_id type, float units) {
	float result = 0.0f;
	auto const& construction = state.world.factory_type_get_construction_costs(type);
	for(uint32_t i = 0; i < economy::commodity_set::set_size; ++i) {
		auto commodity = construction.commodity_type[i];
		if(!commodity) break;
		if(physical::factory_inputs::ordinary_physical_input(state, commodity))
			result += std::max(0.0f, construction.commodity_amounts[i]) * units * 0.5f
				* physical::concrete_market::canonical_reference_price(state, market, commodity, state.current_date, 0.0f);
	}
	return std::isfinite(result) ? result : 0.0f;
}

float input_unit_cost(sys::state const& state, dcon::market_id market, dcon::factory_type_id type) {
	float result = 0.0f;
	auto const& inputs = state.world.factory_type_get_inputs(type);
	for(uint32_t i = 0; i < economy::commodity_set::set_size; ++i) {
		auto commodity = inputs.commodity_type[i];
		if(!commodity) break;
		if(physical::factory_inputs::ordinary_physical_input(state, commodity))
			result += std::max(0.0f, inputs.commodity_amounts[i])
				* physical::concrete_market::canonical_reference_price(state, market, commodity, state.current_date, 0.0f);
	}
	return std::isfinite(result) ? result : 0.0f;
}

enum class opportunity_kind : uint8_t { factory, extraction };

struct project_opportunity {
	opportunity_kind kind = opportunity_kind::factory;
	dcon::province_id province{};
	dcon::nation_id nation{};
	dcon::site_id site{};
	dcon::market_id market{};
	dcon::factory_type_id type{};
	dcon::resource_deposit_id deposit{};
	dcon::commodity_id output{};
	float capacity = 0.0f;
	// At full capacity.
	float daily_output = 0.0f;
	float output_price = 0.0f;
	float daily_material_cost = 0.0f;
	float daily_wage_cost = 0.0f;
	float capital_cost = 0.0f;
	// Materials are bid up to 20% over reference and must be carried here.
	float project_budget = 0.0f;
	float lifetime_days = std::numeric_limits<float>::infinity();
};

struct appraisal {
	economy::investment::project_score score{};
	float working_capital = 0.0f;
	bool viable = false;
};

// One evaluator for every project: the entrant's expected sell-through and
// price given the market outlook, its materials and recruitment wages, and the
// sponsor's required return as the cost of capital. A mine must outlast twice
// its payback.
appraisal appraise(project_opportunity const& opportunity, float required_return, market_outlook const& outlook) {
	appraisal result{};
	auto entry = expected_entry(outlook.demand_for(opportunity.market, opportunity.output),
		outlook.supply_for(opportunity.market, opportunity.output), opportunity.daily_output);
	economy::investment::project_inputs terms{};
	terms.capital_cost = opportunity.capital_cost;
	terms.gross_daily_revenue = opportunity.daily_output * opportunity.output_price * entry.price_factor;
	terms.expected_sell_through = entry.sell_through;
	terms.daily_material_cost = opportunity.daily_material_cost * entry.sell_through;
	terms.daily_wage_cost = opportunity.daily_wage_cost * entry.sell_through;
	terms.demand_risk = 1.0f - entry.sell_through;
	terms.jobs = opportunity.capacity;
	terms.annual_interest_rate = required_return;
	result.score = economy::investment::evaluate(terms);
	result.working_capital = std::max(1.0f, (terms.daily_material_cost + terms.daily_wage_cost) * 30.0f);
	result.viable = result.score.privately_viable && result.score.risk_adjusted_private > 0.0f
		&& opportunity.lifetime_days >= 2.0f * result.score.payback_days;
	return result;
}

std::vector<project_opportunity> factory_opportunities(sys::state const& state,
	std::unordered_map<uint32_t, float> const& wages) {
	std::vector<project_opportunity> opportunities;
	constexpr float planned_capacity = 0.5f;
	state.world.for_each_province([&](dcon::province_id province) {
		auto market_relation = state.world.province_get_state_membership(province);
		auto market = market_relation ? state.world.state_instance_get_market_from_local_market(market_relation) : dcon::market_id{};
		auto nation = nation_of_province(state, province);
		auto project_site = world::spatial_runtime::site_for_province(state, province);
		if(!market || !nation || !project_site) return;
		state.world.for_each_factory_type([&](dcon::factory_type_id type) {
			auto output = state.world.factory_type_get_output(type);
			// An extraction plant exists only on a deposit, a farm only on land,
			// and a workshop only in a cohort.
			if(!output || physical::extraction::extracts_deposit(state, type)
				|| physical::land::farms_land(state, type) || households::is_craft(state, type)) return;
			auto price = physical::concrete_market::canonical_reference_price(state, market, output, state.current_date, 0.0f);
			auto unit_revenue = std::max(0.0f, state.world.factory_type_get_output_amount(type)) * price;
			auto materials = input_unit_cost(state, market, type);
			auto wage = recruitment_wage(state, wages, province, unit_revenue);
			// A recipe without a construction bill cannot be built, and one that
			// loses money at full sales is never worth building.
			auto capital = construction_cost(state, market, type, planned_capacity);
			if(capital <= epsilon || unit_revenue <= materials + wage) return;
			project_opportunity opportunity{};
			opportunity.kind = opportunity_kind::factory;
			opportunity.province = province;
			opportunity.nation = nation;
			opportunity.site = project_site;
			opportunity.market = market;
			opportunity.type = type;
			opportunity.output = output;
			opportunity.capacity = planned_capacity;
			opportunity.daily_output = state.world.factory_type_get_output_amount(type) * planned_capacity;
			opportunity.output_price = price;
			opportunity.daily_material_cost = materials * planned_capacity;
			opportunity.daily_wage_cost = wage * planned_capacity;
			opportunity.capital_cost = capital;
			opportunity.project_budget = capital * 1.5f;
			opportunities.push_back(opportunity);
		});
	});
	return opportunities;
}

// Known deposits without a plant. Only an organization allowed to operate the
// deposit can build on it.
std::vector<project_opportunity> extraction_opportunities(sys::state const& state,
	std::unordered_map<uint32_t, float> const& wages) {
	std::unordered_map<uint32_t, dcon::factory_type_id> recipe_for;
	state.world.for_each_factory_type([&](dcon::factory_type_id type) {
		dcon::commodity_id output = state.world.factory_type_get_output(type);
		if(output && physical::extraction::extracts_deposit(state, type) && !recipe_for.contains(output.index()))
			recipe_for.emplace(output.index(), type);
	});
	std::vector<project_opportunity> opportunities;
	state.world.for_each_resource_deposit([&](dcon::resource_deposit_id deposit) {
		dcon::commodity_id commodity = state.world.resource_deposit_get_commodity(deposit);
		auto recipe = commodity ? recipe_for.find(commodity.index()) : recipe_for.end();
		auto daily = state.world.resource_deposit_get_daily_extraction_capacity(deposit);
		auto remaining = state.world.resource_deposit_get_remaining_recoverable_reserves(deposit);
		if(recipe == recipe_for.end() || physical::extraction::enterprise_for_deposit(state, deposit)
			|| !(daily > 0.0f) || !(remaining > 0.0f)) return;
		auto type = recipe->second;
		auto site = state.world.resource_deposit_get_site_from_resource_deposit_site(deposit);
		auto province = world::site::province_for_site(state, site);
		auto market = physical::concrete_market::market_for_site(state, site);
		auto nation = nation_of_province(state, province);
		auto per_unit = std::max(0.0f, state.world.factory_type_get_output_amount(type))
			* std::max(0.0f, state.world.resource_deposit_get_grade_or_quality(deposit));
		if(!market || !nation || per_unit <= epsilon) return;
		auto units = daily / per_unit;
		auto price = physical::concrete_market::canonical_reference_price(state, market, commodity, state.current_date, 0.0f);
		auto capital = construction_cost(state, market, type, units);
		if(capital <= epsilon) return;
		project_opportunity opportunity{};
		opportunity.kind = opportunity_kind::extraction;
		opportunity.province = province;
		opportunity.nation = nation;
		opportunity.site = site;
		opportunity.market = market;
		opportunity.type = type;
		opportunity.deposit = deposit;
		opportunity.output = commodity;
		opportunity.capacity = units;
		opportunity.daily_output = daily;
		opportunity.output_price = price;
		opportunity.daily_material_cost = input_unit_cost(state, market, type) * units;
		opportunity.daily_wage_cost = recruitment_wage(state, wages, province, per_unit * price) * units;
		opportunity.capital_cost = capital;
		opportunity.project_budget = capital * 1.5f;
		opportunity.lifetime_days = remaining / daily;
		opportunities.push_back(opportunity);
	});
	return opportunities;
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

// Finances a project from the sponsor's own money, then new shares sold to the
// investors of its country, then bank credit against the project itself for
// what remains. A person founds a company for a plant; only an organization
// allowed to work a deposit can build a mine on it, and a mine is built whole.
bool finance_project(sys::state& state, dcon::economic_actor_id investor, dcon::commodity_id settlement,
	float own, project_opportunity const& opportunity, appraisal const& assessed, day_pool& pool,
	market_outlook& outlook) {
	auto required = opportunity.project_budget + assessed.working_capital;
	auto organization = actors::organizations::organization_for_actor(state, investor);
	auto founding = !organization;
	auto mine = opportunity.kind == opportunity_kind::extraction;
	if(founding && mine) return false;
	own = std::min(own, required);
	if(founding && own + epsilon < founder_minimum_share * required) return false;
	auto annual_cashflow = assessed.score.expected_daily_cashflow * 365.0f;

	capital_market::offering terms{};
	terms.issuer = organization;
	terms.nation = opportunity.nation;
	terms.settlement = settlement;
	terms.amount = required - own;
	terms.pre_money = founding ? own : capital_market::book_value(state, organization);
	terms.annual_earnings = annual_cashflow + (founding ? 0.0f : capital_market::expected_annual_earnings(state, organization));
	auto& investors = pool.get(state);
	capital_market::commit(investors, investor, settlement, own);
	auto equity = terms.amount > epsilon
		? std::min(terms.amount, capital_market::total(capital_market::subscriptions(state, investors, terms))) : 0.0f;
	auto debt_need = std::min(opportunity.project_budget, required - own - equity);
	auto quoted = debt_need > epsilon ? economy::banking::quote_project_credit(state, founding ? dcon::economic_actor_id{} : investor,
		settlement, debt_need, annual_cashflow, own + equity, opportunity.capital_cost).amount : 0.0f;
	auto reachable = own + equity + quoted;
	if(reachable + epsilon < (mine ? 1.0f : minimum_funded_share) * required) return false;

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
	auto project = mine
		? capital_projects::create_extraction_plant(state, sponsor, organization, opportunity.deposit, opportunity.type, settlement)
		: capital_projects::create_greenfield_factory(state, sponsor, organization, opportunity.site, opportunity.type,
			settlement, opportunity.capacity);
	if(!project || !source) return false;

	float raised = 0.0f;
	if(equity > epsilon) {
		terms.amount = equity;
		raised = capital_market::raise(state, investors, terms);
	}
	float borrowed = 0.0f;
	auto debt = std::min(opportunity.project_budget, required - own - raised);
	if(debt > epsilon)
		borrowed = economy::banking::underwrite_project_credit(state, project, source, debt, annual_cashflow,
			own + raised, opportunity.capital_cost).funded_amount;
	auto funded = std::min(required, own + raised + borrowed);
	auto fraction = funded / required;
	if(fraction + epsilon < (mine ? 1.0f : minimum_funded_share)) {
		(void)capital_projects::cancel(state, project);
		return false;
	}
	fraction = std::min(fraction, 1.0f);
	auto budget = opportunity.project_budget * fraction;
	if(cash_in_wallet(state, sponsor, settlement, budget) + epsilon < budget
		|| !capital_projects::fund(state, project, source, std::min(budget, wallets::spendable(state, wallets::account_ref::from_dcon(source))))) {
		(void)capital_projects::cancel(state, project);
		return false;
	}
	if(fraction + epsilon < 1.0f) scale_greenfield_project(state, project, fraction);
	outlook.add_supply(opportunity.market, opportunity.output, opportunity.daily_output * fraction);
	return true;
}

void process_investor(sys::state& state, dcon::economic_actor_id investor,
	std::vector<project_opportunity> const& opportunities, market_outlook& outlook, day_pool& pool) {
	state.world.economic_actor_set_industrial_last_decision_date(investor, state.current_date);
	auto settlement = capital_market::settlement_of(state, investor);
	if(!settlement) return;
	auto nation = capital_market::nation_of(state, investor, settlement);
	auto own = capital_market::investable_funds(state, investor, settlement);
	if(!nation || own <= 1.0f) return;
	auto required_return = capital_market::required_return(state, investor, settlement);

	project_opportunity const* best = nullptr;
	appraisal best_appraisal{};
	for(auto const& candidate : opportunities) {
		if(candidate.nation != nation) continue;
		if(candidate.kind == opportunity_kind::extraction
			&& !physical::extraction::may_operate(state, candidate.deposit, investor, state.current_date)) continue;
		auto assessed = appraise(candidate, required_return, outlook);
		if(assessed.viable && (!best || assessed.score.risk_adjusted_private > best_appraisal.score.risk_adjusted_private)) {
			best = &candidate;
			best_appraisal = assessed;
		}
	}
	auto best_excess = best ? best_appraisal.score.risk_adjusted_private : 0.0f;
	auto purchase = best_plant_offer(state, investor, nation, settlement, own, required_return);
	if(purchase.factory && purchase.excess_return > 0.0f && purchase.excess_return >= best_excess) {
		if(execute_purchase(state, investor, settlement, purchase))
			capital_market::commit(pool.get(state), investor, settlement, purchase.price);
	} else if(best) {
		(void)finance_project(state, investor, settlement, own, *best, best_appraisal, pool, outlook);
	}
}

// An urban cohort takes up a craft its members do not practise yet when a
// worker would earn more at it, at the price and sales the market outlook
// allows, than the cohort's own production now gives, and it holds a month of
// the craft's inputs in savings and cash. Labor is then shared with its other
// paying crafts.
void review_workshops(sys::state& state, dcon::organization_id cohort, market_outlook& outlook) {
	if(households::role_of(state, cohort) != households::role::urban) return;
	auto site = households::home_site(state, cohort);
	auto market = physical::concrete_market::market_for_site(state, site);
	auto workers = households::workers(state, cohort);
	auto actor = actors::organizations::actor_for_organization(state, cohort);
	auto settlement = capital_market::settlement_of(state, actor);
	if(!site || !market || !settlement || workers <= epsilon) return;
	std::unordered_set<uint32_t> practised;
	uint32_t paying = 0;
	for(auto factory : actors::organizations::factories_operated_by(state, cohort)) {
		dcon::factory_type_id type = state.world.factory_get_building_type(factory);
		if(!households::is_craft(state, type)) continue;
		practised.insert(type.index());
		if(households::self_employed_labor(state, factory) > epsilon) ++paying;
	}
	auto share = workers / float(paying + 1);
	auto threshold = std::max(households::reservation_wage(state, cohort), epsilon);
	dcon::factory_type_id best{};
	float best_value = threshold;
	float best_inputs = 0.0f;
	float best_added = 0.0f;
	state.world.for_each_factory_type([&](dcon::factory_type_id type) {
		if(!households::is_craft(state, type) || practised.contains(type.index())) return;
		auto output = state.world.factory_type_get_output(type);
		auto per_worker = std::max(0.0f, state.world.factory_type_get_output_amount(type));
		auto added = share * per_worker;
		auto entry = expected_entry(outlook.demand_for(market, output), outlook.supply_for(market, output), added);
		auto price = physical::concrete_market::canonical_reference_price(state, market, output, state.current_date, 0.0f);
		auto inputs = input_unit_cost(state, market, type);
		auto value = (per_worker * price * entry.price_factor - inputs) * entry.sell_through;
		if(value > best_value) {
			best = type;
			best_value = value;
			best_inputs = inputs;
			best_added = added;
		}
	});
	if(!best || capital_market::liquid_funds(state, actor, settlement) + epsilon < best_inputs * share * 30.0f) return;
	if(households::create_workshop(state, site, best, cohort))
		outlook.add_supply(market, state.world.factory_type_get_output(best), best_added);
}

} // namespace

entry_terms expected_entry(float demand, float supply, float added) {
	entry_terms result{};
	auto capacity = std::max(0.0f, supply) + std::max(0.0f, added);
	if(!(demand > 0.0f) || !(capacity > 0.0f)) return result;
	auto ratio = demand / capacity;
	result.sell_through = std::clamp(ratio, 0.0f, maximum_entry_sell_through);
	result.price_factor = std::clamp(std::pow(ratio, 1.0f / demand_elasticity), 0.5f, 1.25f);
	return result;
}

float asking_price_for(sys::state const& state, dcon::factory_id factory) {
	return asking_price(state, factory);
}

// Built plants wear out. Each day a plant loses the daily share of the annual
// depreciation rate of its capacity, and its operator books that share of the
// plant's replacement value as a cost. Farm land and household crafts do not
// wear; a firm replaces worn capacity by expanding when its plants run full.
void depreciate(sys::state& state) {
	auto daily = 1.0f - std::pow(1.0f - annual_depreciation_rate, 1.0f / 365.0f);
	state.world.for_each_factory([&](dcon::factory_id factory) {
		auto type = state.world.factory_get_building_type(factory);
		auto capacity = state.world.factory_get_productive_capacity(factory);
		if(!type || !(capacity > 0.0f) || state.world.factory_get_agency_lifecycle_status(factory) >= lifecycle_bankrupt
			|| physical::land::farms_land(state, factory) || households::is_craft(state, type)) return;
		auto worn = replacement_value(state, factory) * daily;
		state.world.factory_set_productive_capacity(factory, capacity * (1.0f - daily));
		state.world.factory_set_size(factory, state.world.factory_get_size(factory) * (1.0f - daily));
		auto organization = actors::organizations::operator_organization_for_factory(state, factory);
		if(organization && !households::is_household(state, organization) && std::isfinite(worn))
			state.world.organization_set_retained_earnings(organization,
				state.world.organization_get_retained_earnings(organization) - worn);
	});
}

void process(sys::state& state) {
	depreciate(state);
	day_pool pool;
	std::vector<dcon::factory_id> factories;
	state.world.for_each_factory([&](dcon::factory_id factory) { factories.push_back(factory); });
	for(auto factory : factories) process_insolvency(state, factory, pool);
	std::vector<dcon::economic_actor_id> investors;
	std::vector<dcon::organization_id> cohorts;
	state.world.for_each_economic_actor([&](dcon::economic_actor_id actor) {
		if(!review_due(state, actor)) return;
		if(eligible_sponsor(state, actor)) investors.push_back(actor);
		else if(auto organization = actors::organizations::organization_for_actor(state, actor);
			organization && households::is_household(state, organization)) cohorts.push_back(organization);
	});
	(void)capital_market::trade_shares(state, pool.get(state));
	if(investors.empty() && cohorts.empty()) return;
	auto outlook = build_outlook(state);
	for(auto cohort : cohorts) {
		review_workshops(state, cohort, outlook);
		state.world.economic_actor_set_industrial_last_decision_date(
			actors::organizations::actor_for_organization(state, cohort), state.current_date);
	}
	if(investors.empty()) return;
	auto wages = prevailing_wages(state);
	auto opportunities = factory_opportunities(state, wages);
	auto mines = extraction_opportunities(state, wages);
	opportunities.insert(opportunities.end(), mines.begin(), mines.end());
	for(auto investor : investors) process_investor(state, investor, opportunities, outlook, pool);
}

} // namespace economy::industrial_dynamics
