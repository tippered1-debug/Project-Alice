#include "industrial_dynamics.hpp"

#include "actors/organizations/organizations.hpp"
#include "actors/ownership.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/banking/banking.hpp"
#include "economy/capital_projects.hpp"
#include "economy/firm_agency.hpp"
#include "economy/physical/concrete_labor.hpp"
#include "economy/physical/concrete_market.hpp"
#include "economy/physical/exchange.hpp"
#include "economy/physical/factory_inputs.hpp"
#include "economy/physical/inventory.hpp"
#include "economy/relations/relations.hpp"
#include "system_state.hpp"
#include "world/site.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
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
constexpr int32_t investor_review_days = 30;
constexpr int32_t bankruptcy_sale_window_days = 120;

bool eligible_investor(sys::state const& state, dcon::economic_actor_id actor) {
	if(!actor || !state.world.economic_actor_is_valid(actor)) return false;
	auto kind = actor_kind(state.world.economic_actor_get_kind(actor));
	return kind == actor_kind::company || kind == actor_kind::fund
		|| kind == actor_kind::cooperative || kind == actor_kind::person;
}

float unreserved_cash(sys::state const& state, dcon::monetary_account_id account) {
	if(!account) return 0.0f;
	return std::max(0.0f, accounts::balance(state, account)
		- physical::concrete_market::reserved_bid_amount(state, account));
}

dcon::monetary_account_id actor_account(sys::state& state, dcon::economic_actor_id actor,
	dcon::commodity_id settlement) {
	auto account = accounts::find_account(state, actor, settlement);
	return account ? account : accounts::open_account(state, actor, settlement);
}

float factory_value(sys::state const& state, dcon::factory_id factory) {
	if(!factory || !state.world.factory_is_valid(factory)) return 0.0f;
	auto type = state.world.factory_get_building_type(factory);
	auto site = world::site::site_for_factory(state, factory);
	auto market = physical::concrete_market::market_for_site(state, site);
	if(!type || !market) return 0.0f;
	auto output = state.world.factory_type_get_output(type);
	auto price = physical::concrete_market::canonical_reference_price(state, market, output,
		state.current_date, 0.0f);
	auto capacity = std::max(0.0f, state.world.factory_get_productive_capacity(factory));
	auto output_per_unit = std::max(0.0f, state.world.factory_type_get_output_amount(type));
	auto value = capacity * output_per_unit * std::max(0.0f, price) * 30.0f;
	return std::isfinite(value) ? value : 0.0f;
}

float expected_factory_daily_profit(sys::state const& state, dcon::factory_id factory) {
	if(!factory || !state.world.factory_is_valid(factory)) return 0.0f;
	auto type = state.world.factory_get_building_type(factory);
	auto market = physical::concrete_market::market_for_site(state,
		world::site::site_for_factory(state, factory));
	if(!type || !market) return 0.0f;
	auto capacity = std::max(0.0f, state.world.factory_get_productive_capacity(factory));
	auto output = state.world.factory_type_get_output(type);
	auto price = physical::concrete_market::canonical_reference_price(state, market, output,
		state.current_date, 0.0f);
	auto output_units = std::max(0.0f, state.world.factory_type_get_output_amount(type)) * capacity;
	auto sell_through = state.world.factory_get_agency_expected_sell_through(factory);
	if(!std::isfinite(sell_through) || sell_through <= epsilon) sell_through = 0.65f;
	float materials = 0.0f;
	auto const& inputs = state.world.factory_type_get_inputs(type);
	for(uint32_t i = 0; i < economy::commodity_set::set_size; ++i) {
		auto commodity = inputs.commodity_type[i];
		if(!commodity) break;
		if(physical::factory_inputs::ordinary_physical_input(state, commodity))
			materials += std::max(0.0f, inputs.commodity_amounts[i]) * capacity
				* physical::concrete_market::canonical_reference_price(state, market, commodity,
					state.current_date, 0.0f);
	}
	auto revenue = output_units * std::max(0.0f, price) * std::clamp(sell_through, 0.05f, 1.0f);
	auto wages = physical::concrete_labor::wage_due_for_factory(state, factory);
	if(wages <= epsilon) wages = revenue * 0.25f;
	auto observed = state.world.factory_get_agency_recent_profit(factory);
	if(std::isfinite(observed) && std::abs(observed) > epsilon)
		return observed * 0.5f + (revenue - materials - wages) * 0.5f;
	return revenue - materials - wages;
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
	std::vector<dcon::ownership_stake_id> stakes;
	state.world.asset_for_each_ownership_stake_asset_as_asset(asset,
		[&](dcon::ownership_stake_asset_id relation) {
			stakes.push_back(state.world.ownership_stake_asset_get_ownership_stake(relation));
		});
	for(auto stake : stakes)
		if(stake && state.world.ownership_stake_is_valid(stake)) state.world.delete_ownership_stake(stake);
	(void)actors::ownership::create_stake(state, new_owner, asset, 1.0f, 1.0f, 1.0f);
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
	state.world.factory_set_agency_liquidation_value(factory, factory_value(state, factory));
	auto site = world::site::site_for_factory(state, factory);
	auto owner = actors::organizations::operator_actor_for_factory(state, factory);
	for(auto contract : physical::concrete_labor::active_contracts_for_factory(state, factory))
		(void)physical::concrete_labor::terminate_employment_contract(state, contract, state.current_date);
	if(site && owner) {
		std::vector<std::pair<dcon::commodity_id, float>> stocks;
		state.world.site_for_each_physical_stock_site_as_site(site, [&](dcon::physical_stock_site_id relation) {
			auto stock = state.world.physical_stock_site_get_physical_stock(relation);
			auto owner_relation = state.world.physical_stock_get_physical_stock_owner(stock);
			if(!owner_relation || state.world.physical_stock_owner_get_economic_actor(owner_relation) != owner) return;
			stocks.emplace_back(state.world.physical_stock_get_commodity_from_physical_stock_commodity(stock),
				std::max(0.0f, state.world.physical_stock_get_quantity(stock)));
		});
		for(auto const& [commodity, quantity] : stocks)
			if(commodity && quantity > epsilon) (void)physical::inventory::remove(state, site, commodity, quantity, owner);
	}
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
	state.world.factory_set_canonical_production(factory, 0);
	state.world.factory_set_productive_capacity(factory, 0.0f);
	state.world.factory_set_size(factory, 0.0f);
	state.world.factory_set_actual_utilization(factory, 0.0f);
	state.world.factory_set_target_utilization(factory, 0.0f);
	state.world.factory_set_agency_lifecycle_status(factory, lifecycle_closed);
	state.world.factory_set_agency_last_lifecycle_date(factory, state.current_date);
}

bool acquire_factory(sys::state& state, dcon::economic_actor_id buyer,
	dcon::organization_id buyer_org, dcon::factory_id factory, float price) {
	if(!buyer || !buyer_org || !factory || !state.world.factory_is_valid(factory)
		|| !state.world.factory_get_canonical_production(factory)
		|| actors::organizations::operator_actor_for_factory(state, factory) == buyer) return false;
	auto seller = actors::organizations::operator_actor_for_factory(state, factory);
	auto settlement = state.world.factory_get_payroll_settlement(factory);
	if(!settlement) settlement = physical::exchange::settlement_for_purchase(state, buyer);
	if(!seller || !settlement || !std::isfinite(price) || price <= epsilon) return false;
	auto buyer_account = accounts::find_account(state, buyer, settlement);
	if(!buyer_account || unreserved_cash(state, buyer_account) < price) return false;
	auto seller_account = actor_account(state, seller, settlement);
	if(!seller_account || !accounts::transfer(state, buyer_account, seller_account, price,
		relations::transaction_kind::purchase, state.current_date)) return false;

	transfer_factory_inventory(state, factory, seller, buyer);
	// Secured factory debt stays with the seller. The sale proceeds fund the
	// creditor waterfall; any remaining claim is recorded as a bank loss.
	(void)service_factory_claims(state, factory, seller_account, price, false, true);
	transfer_factory_title(state, factory, buyer);
	if(!actors::organizations::transfer_factory_operator(state, buyer_org, factory)) return false;
	state.world.factory_set_agency_lifecycle_status(factory, lifecycle_active);
	state.world.factory_set_agency_liquidation_value(factory, price);
	state.world.factory_set_agency_bankruptcy_date(factory, sys::date{});
	state.world.factory_set_agency_last_lifecycle_date(factory, state.current_date);
	state.world.factory_set_agency_restructuring_count(factory, 0);
	return true;
}

void attempt_owner_recapitalization(sys::state& state, dcon::factory_id factory) {
	auto firm = actors::organizations::operator_actor_for_factory(state, factory);
	auto settlement = state.world.factory_get_payroll_settlement(factory);
	if(!settlement) settlement = physical::exchange::settlement_for_purchase(state, firm);
	if(!firm || !settlement) return;
	auto account = actor_account(state, firm, settlement);
	if(!account) return;
	auto due = total_factory_debt(state, factory);
	auto target = std::min(due, std::max(1.0f, factory_value(state, factory) * 0.10f));
	if(target <= epsilon) return;
	auto injected = actors::ownership::contribute_equity_to_factory(state, factory, firm, account, target);
	if(injected > epsilon) {
		state.world.factory_set_agency_owner_equity_contributed(factory,
			state.world.factory_get_agency_owner_equity_contributed(factory) + injected);
		(void)service_factory_claims(state, factory, account, injected, true, false);
	}
}

void attempt_external_recapitalization(sys::state& state, dcon::factory_id factory) {
	auto firm = actors::organizations::operator_actor_for_factory(state, factory);
	auto organization = actors::organizations::operator_organization_for_factory(state, factory);
	auto settlement = state.world.factory_get_payroll_settlement(factory);
	if(!firm || !organization || !settlement || expected_factory_daily_profit(state, factory) <= 0.0f) return;
	auto need = std::min(total_factory_debt(state, factory), factory_value(state, factory) * 0.08f);
	if(need <= epsilon) return;
	auto firm_account = actor_account(state, firm, settlement);
	if(!firm_account) return;
	dcon::economic_actor_id selected{};
	dcon::monetary_account_id selected_account{};
	float selected_amount = 0.0f;
	state.world.for_each_economic_actor([&](dcon::economic_actor_id investor) {
		if(investor == firm || !eligible_investor(state, investor)) return;
		auto account = accounts::find_account(state, investor, settlement);
		auto available = unreserved_cash(state, account);
		auto amount = std::min(need, available * 0.10f);
		if(amount > selected_amount) {
			selected = investor;
			selected_account = account;
			selected_amount = amount;
		}
	});
	if(!selected || selected_amount <= epsilon
		|| !accounts::transfer(state, selected_account, firm_account, selected_amount,
			relations::transaction_kind::equity_contribution, state.current_date)) return;
	(void)actors::ownership::issue_equity(state,
		actors::organizations::equity_asset_for_organization(state, organization), selected,
		selected_amount, std::max(1.0f, factory_value(state, factory)));
	state.world.factory_set_agency_owner_equity_contributed(factory,
		state.world.factory_get_agency_owner_equity_contributed(factory) + selected_amount);
	(void)service_factory_claims(state, factory, firm_account, selected_amount, true, false);
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

void process_insolvency(sys::state& state, dcon::factory_id factory) {
	if(!factory || !state.world.factory_is_valid(factory)
		|| !state.world.factory_get_canonical_production(factory)) return;
	auto defaulted = has_defaulted_factory_loan(state, factory);
	auto status = state.world.factory_get_agency_lifecycle_status(factory);
	if(defaulted && status < lifecycle_bankrupt) {
		if(!state.world.factory_get_agency_bankruptcy_date(factory))
			state.world.factory_set_agency_bankruptcy_date(factory, state.current_date);
		auto last_recap = state.world.factory_get_agency_last_recapitalization_date(factory);
		if(!last_recap || state.current_date.to_raw_value() - last_recap.to_raw_value() >= 30) {
			attempt_owner_recapitalization(state, factory);
			attempt_external_recapitalization(state, factory);
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
	dcon::site_id site{};
	dcon::factory_type_id type{};
	dcon::commodity_id settlement{};
	float project_budget = 0.0f;
	float working_capital = 0.0f;
	float daily_profit = 0.0f;
	float score = -std::numeric_limits<float>::infinity();
};

dcon::factory_id profitable_collateral_factory(sys::state const& state,
	dcon::organization_id organization) {
	dcon::factory_id selected{};
	float selected_value = 0.0f;
	for(auto factory : actors::organizations::factories_operated_by(state, organization)) {
		if(!factory || !state.world.factory_get_canonical_production(factory)
			|| state.world.factory_get_agency_lifecycle_status(factory) >= lifecycle_bankrupt
			|| expected_factory_daily_profit(state, factory) <= epsilon) continue;
		auto value = factory_value(state, factory);
		if(value > selected_value) {
			selected = factory;
			selected_value = value;
		}
	}
	return selected;
}

void scale_greenfield_project(sys::state& state, dcon::capital_project_id project,
	float funding_fraction) {
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

std::vector<project_opportunity> greenfield_opportunities(sys::state const& state) {
	std::vector<project_opportunity> opportunities;
	std::unordered_map<uint32_t, dcon::site_id> province_sites;
	std::unordered_map<uint32_t, std::unordered_set<uint32_t>> existing_types;
	state.world.for_each_factory([&](dcon::factory_id factory) {
		auto province = state.world.factory_get_province_from_factory_location(factory);
		if(!province) return;
		if(!province_sites.contains(province.index())) {
			auto site = world::site::site_for_factory(state, factory);
			if(site) province_sites.emplace(province.index(), site);
		}
		if(state.world.factory_get_canonical_production(factory)) {
			dcon::factory_type_id type = state.world.factory_get_building_type(factory);
			if(type) existing_types[province.index()].insert(type.index());
		}
	});
	state.world.for_each_province([&](dcon::province_id province) {
		auto market_relation = state.world.province_get_state_membership(province);
		auto market = market_relation ? state.world.state_instance_get_market_from_local_market(market_relation) : dcon::market_id{};
		if(!market) return;
		auto site_entry = province_sites.find(province.index());
		if(site_entry == province_sites.end()) return;
		auto project_site = site_entry->second;
		state.world.for_each_factory_type([&](dcon::factory_type_id type) {
			auto output = state.world.factory_type_get_output(type);
			if(!output || state.world.commodity_get_is_local(output) || state.world.commodity_get_money_rgo(output)) return;
			auto type_entry = existing_types.find(province.index());
			if(type_entry != existing_types.end() && type_entry->second.contains(type.index())) return;
			auto unit_output = std::max(0.0f, state.world.factory_type_get_output_amount(type)) * 0.5f;
			auto output_price = physical::concrete_market::canonical_reference_price(state, market,
				output, state.current_date, 0.0f);
			if(unit_output <= epsilon || output_price <= epsilon) return;
			auto sell_through = physical::concrete_market::observed_sell_through(state, market,
				output, state.current_date, 0.65f);
			sell_through = std::clamp(sell_through, 0.15f, 0.95f);
			auto gross_revenue = unit_output * output_price * sell_through;
			auto const& inputs = state.world.factory_type_get_inputs(type);
			float materials = 0.0f;
			for(uint32_t i = 0; i < economy::commodity_set::set_size; ++i) {
				auto commodity = inputs.commodity_type[i];
				if(!commodity) break;
				if(physical::factory_inputs::ordinary_physical_input(state, commodity))
					materials += std::max(0.0f, inputs.commodity_amounts[i]) * 0.5f
						* physical::concrete_market::canonical_reference_price(state, market,
							commodity, state.current_date, 0.0f);
			}
			auto wages = gross_revenue * 0.25f;
			auto profit = gross_revenue - materials - wages;
			if(profit <= epsilon) return;
			float construction_cost = 0.0f;
			auto const& construction = state.world.factory_type_get_construction_costs(type);
			for(uint32_t i = 0; i < economy::commodity_set::set_size; ++i) {
				auto commodity = construction.commodity_type[i];
				if(!commodity) break;
				if(physical::factory_inputs::ordinary_physical_input(state, commodity))
					construction_cost += std::max(0.0f, construction.commodity_amounts[i]) * 0.25f
						* physical::concrete_market::canonical_reference_price(state, market,
							commodity, state.current_date, 0.0f);
			}
			if(construction_cost <= epsilon) construction_cost = profit * 45.0f;
			auto budget = construction_cost * 1.50f;
			auto working = std::max(1.0f, (materials + wages) * 30.0f);
			auto score = profit * 365.0f / std::max(1.0f, construction_cost) - 0.10f;
			project_opportunity opportunity{};
			opportunity.site = project_site;
			opportunity.type = type;
			opportunity.project_budget = budget;
			opportunity.working_capital = working;
			opportunity.daily_profit = profit;
			opportunity.score = score;
			opportunities.push_back(opportunity);
		});
	});
	return opportunities;
}

struct acquisition_opportunity {
	dcon::factory_id factory{};
	float price = 0.0f;
	float score = -std::numeric_limits<float>::infinity();
};

acquisition_opportunity best_acquisition(sys::state const& state, dcon::economic_actor_id buyer,
	dcon::commodity_id settlement, float available_cash) {
	acquisition_opportunity best{};
	state.world.for_each_factory([&](dcon::factory_id factory) {
		if(!state.world.factory_get_canonical_production(factory)
			|| actors::organizations::operator_actor_for_factory(state, factory) == buyer) return;
		if(state.world.factory_get_payroll_settlement(factory)
			&& state.world.factory_get_payroll_settlement(factory) != settlement) return;
		auto lifecycle = state.world.factory_get_agency_lifecycle_status(factory);
		auto asset_value = factory_value(state, factory);
		if(asset_value <= epsilon) return;
		auto distress = lifecycle == lifecycle_bankrupt;
		auto price = asset_value * (distress ? 0.20f : 0.65f);
		if(price <= epsilon || price > available_cash) return;
		auto profit = expected_factory_daily_profit(state, factory);
		if(profit <= epsilon) return;
		auto score = profit * 365.0f / price - (distress ? 0.08f : 0.12f);
		if(score > best.score) best = { factory, price, score };
	});
	(void)settlement;
	return best;
}

bool execute_greenfield(sys::state& state, dcon::economic_actor_id investor,
	dcon::monetary_account_id source, dcon::commodity_id settlement,
	project_opportunity const& opportunity) {
	if(!opportunity.site || !opportunity.type || !source) return false;
	auto required = opportunity.project_budget + opportunity.working_capital;
	auto available = unreserved_cash(state, source);
	auto organization = actors::organizations::organization_for_actor(state, investor);
	auto sponsor = investor;
	dcon::economic_actor_id founder{};
	if(!organization) {
		organization = actors::organizations::create_company(state);
		if(!organization) return false;
		sponsor = actors::organizations::actor_for_organization(state, organization);
		founder = investor;
	}
	if(!actors::organizations::is_economic_kind(actor_kind(state.world.organization_get_kind(organization)))) return false;
	if(founder) {
		if(available < required) return false;
		auto startup_account = actor_account(state, sponsor, settlement);
		if(!startup_account || !accounts::transfer(state, source, startup_account, required,
			relations::transaction_kind::equity_contribution, state.current_date)) return false;
		if(!actors::ownership::issue_equity(state,
			actors::organizations::equity_asset_for_organization(state, organization), founder,
			required, 0.0f)) return false;
		source = startup_account;
	}
	auto project = capital_projects::create_greenfield_factory(state, sponsor, organization,
		opportunity.site, opportunity.type, settlement, 0.5f);
	if(!project) return false;
	if(!founder && available < opportunity.working_capital + opportunity.project_budget * 0.05f) {
		(void)capital_projects::cancel(state, project);
		return false;
	}
	auto own_project_capital = founder ? opportunity.project_budget
		: std::min(opportunity.project_budget, std::max(0.0f, available - opportunity.working_capital));
	auto loan_need = std::max(0.0f, opportunity.project_budget - own_project_capital);
	economy::banking::factory_credit_result credit{};
	if(loan_need > epsilon) {
		auto collateral_factory = profitable_collateral_factory(state, organization);
		if(!collateral_factory) {
			(void)capital_projects::cancel(state, project);
			return false;
		}
		auto expected_return = opportunity.daily_profit * 365.0f
			/ std::max(1.0f, opportunity.project_budget);
		credit = economy::banking::underwrite_factory_credit(state, collateral_factory, source,
			1, loan_need, expected_return, factory_value(state, collateral_factory), project);
		if(credit.funded_amount <= epsilon) {
			(void)capital_projects::cancel(state, project);
			return false;
		}
	}
	auto total_project_funding = own_project_capital + credit.funded_amount;
	if(total_project_funding <= epsilon
		|| (own_project_capital > epsilon
			&& !capital_projects::fund(state, project, source, own_project_capital))) {
		(void)capital_projects::cancel(state, project);
		return false;
	}
	if(credit.funded_amount > epsilon
		&& !capital_projects::fund(state, project, source, credit.funded_amount)) {
		(void)capital_projects::cancel(state, project);
		return false;
	}
	if(credit.funded_amount + epsilon < loan_need)
		scale_greenfield_project(state, project,
			total_project_funding / opportunity.project_budget);
	return true;
}

bool execute_acquisition(sys::state& state, dcon::economic_actor_id investor,
	dcon::monetary_account_id account, dcon::commodity_id settlement,
	acquisition_opportunity const& opportunity) {
	auto organization = actors::organizations::organization_for_actor(state, investor);
	if(organization) return acquire_factory(state, investor, organization,
		opportunity.factory, opportunity.price);
	if(!account || unreserved_cash(state, account) < opportunity.price) return false;
	organization = actors::organizations::create_company(state);
	if(!organization) return false;
	auto company = actors::organizations::actor_for_organization(state, organization);
	auto company_account = actor_account(state, company, settlement);
	if(!company_account || !accounts::transfer(state, account, company_account,
		opportunity.price, relations::transaction_kind::equity_contribution, state.current_date)) return false;
	if(!actors::ownership::issue_equity(state,
		actors::organizations::equity_asset_for_organization(state, organization), investor,
		opportunity.price, 0.0f)) return false;
	return acquire_factory(state, company, organization, opportunity.factory, opportunity.price);
}

void process_investor(sys::state& state, dcon::economic_actor_id investor,
	std::vector<project_opportunity> const& greenfield_opportunities) {
	if(!eligible_investor(state, investor)) return;
	auto last = state.world.economic_actor_get_industrial_last_decision_date(investor);
	if(last && state.current_date.to_raw_value() - last.to_raw_value() < investor_review_days) return;
	auto settlement = physical::exchange::settlement_for_purchase(state, investor);
	if(!settlement) {
		state.world.economic_actor_set_industrial_last_decision_date(investor, state.current_date);
		return;
	}
	auto account = accounts::find_account(state, investor, settlement);
	auto available = unreserved_cash(state, account);
	if(!account || available <= 1.0f) {
		state.world.economic_actor_set_industrial_last_decision_date(investor, state.current_date);
		return;
	}
	project_opportunity greenfield{};
	auto organization = actors::organizations::organization_for_actor(state, investor);
	auto can_borrow_against_portfolio = organization
		&& profitable_collateral_factory(state, organization);
	for(auto const& candidate : greenfield_opportunities) {
		auto minimum_capital = can_borrow_against_portfolio
			? candidate.working_capital + candidate.project_budget * 0.05f
			: candidate.project_budget + candidate.working_capital;
		if(minimum_capital > available || candidate.score <= greenfield.score) continue;
		greenfield = candidate;
		greenfield.settlement = settlement;
	}
	auto acquisition = best_acquisition(state, investor, settlement, available);
	bool acted = false;
	if(acquisition.score > greenfield.score && acquisition.factory
		&& acquisition.score > 0.10f && available >= acquisition.price) {
		acted = execute_acquisition(state, investor, account, settlement, acquisition);
	} else if(greenfield.site && greenfield.score > 0.10f) {
		acted = execute_greenfield(state, investor, account, settlement, greenfield);
	}
	(void)acted;
	state.world.economic_actor_set_industrial_last_decision_date(investor, state.current_date);
}

} // namespace

void process(sys::state& state) {
	std::vector<dcon::factory_id> factories;
	state.world.for_each_factory([&](dcon::factory_id factory) { factories.push_back(factory); });
	for(auto factory : factories) process_insolvency(state, factory);
	std::vector<dcon::economic_actor_id> investors;
	state.world.for_each_economic_actor([&](dcon::economic_actor_id actor) {
		if(!eligible_investor(state, actor)) return;
		auto last = state.world.economic_actor_get_industrial_last_decision_date(actor);
		if(!last || state.current_date.to_raw_value() - last.to_raw_value() >= investor_review_days)
			investors.push_back(actor);
	});
	if(investors.empty()) return;
	auto opportunities = greenfield_opportunities(state);
	for(auto investor : investors) process_investor(state, investor, opportunities);
}

} // namespace economy::industrial_dynamics
