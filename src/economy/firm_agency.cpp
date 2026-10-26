#include "firm_agency.hpp"

#include "accounts/accounts.hpp"
#include "actors/ownership.hpp"
#include "actors/organizations/organizations.hpp"
#include "compat/alice/legacy_bridge.hpp"
#include "economy/physical/concrete_market.hpp"
#include "economy/physical/deposits.hpp"
#include "economy/physical/exact_person_goods.hpp"
#include "economy/capital_market.hpp"
#include "economy/capital_projects.hpp"
#include "economy/investment_ranking.hpp"
#include "economy/exact_person_economy.hpp"
#include "economy/collective_labor.hpp"
#include "economy/physical/deposits.hpp"
#include "economy/physical/factory_inputs.hpp"
#include "economy/physical/extraction.hpp"
#include "economy/physical/land.hpp"
#include "economy/households.hpp"
#include "economy/physical/exchange.hpp"
#include "economy/physical/inventory.hpp"
#include "economy/economy_stats.hpp"
#include "economy/payroll.hpp"
#include "economy/banking/banking.hpp"
#include "economy/wallets.hpp"
#include "governance/finance/finance.hpp"
#include "governance/policy.hpp"
#include "relations/relations.hpp"
#include "system_state.hpp"
#include "world/site.hpp"
#include "technology/technology_kernel.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <tuple>
#include <unordered_set>

namespace economy {

struct firm_agency_event_store {
	struct daily_totals {
		float offered = 0.0f;
		float sold = 0.0f;
		float revenue = 0.0f;
	};

	std::mutex mutex;
	bool initialized = false;
	std::set<std::pair<uint32_t, uint32_t>> indexed_freight_contracts;
	std::map<std::tuple<uint32_t, uint32_t, int32_t>, daily_totals> output_by_factory_product_day;
	std::map<std::pair<uint32_t, int32_t>, std::pair<float, float>> input_costs_by_factory_day;
	uint32_t indexed_ask_count = 0;
	uint32_t indexed_trade_fill_count = 0;
	size_t indexed_exact_fill_count = 0;
};

} // namespace economy

namespace economy::firm_agency {
namespace {
constexpr float epsilon = 1.0e-5f;
constexpr float probing_fraction = 0.05f;
constexpr float output_target_days = 3.0f;
constexpr float cash_safety_fraction = 0.10f;
constexpr float expectation_alpha = 0.20f;
constexpr float default_sell_through = 0.75f;
constexpr float default_markup = 0.12f;

int32_t date_key(sys::date date) {
	return int32_t(date.to_raw_value());
}

void add_offer(sys::state const& state, firm_agency_event_store& index,
	dcon::concrete_market_ask_id ask) {
	if(!ask) return;
	auto factory = state.world.concrete_market_ask_get_factory_from_concrete_ask_factory(ask);
	auto commodity = state.world.concrete_market_ask_get_commodity_from_concrete_ask_commodity(ask);
	if(!factory || !commodity) return;
	auto date = date_key(state.world.concrete_market_ask_get_created_on(ask));
	index.output_by_factory_product_day[
		{factory.index(), commodity.index(), date}].offered +=
		std::max(0.0f, state.world.concrete_market_ask_get_original_quantity(ask));
}

void add_trade_fill(sys::state const& state, firm_agency_event_store& index,
	dcon::concrete_trade_fill_id fill) {
	if(!fill) return;
	auto quantity = std::max(0.0f, state.world.concrete_trade_fill_get_quantity(fill));
	auto price = std::max(0.0f, state.world.concrete_trade_fill_get_execution_price(fill));
	auto date = state.world.concrete_trade_fill_get_occurred_on(fill);
	if(auto ask = state.world.concrete_trade_fill_get_concrete_market_ask_from_concrete_fill_ask(fill)) {
		auto factory = state.world.concrete_market_ask_get_factory_from_concrete_ask_factory(ask);
		auto commodity = state.world.concrete_market_ask_get_commodity_from_concrete_ask_commodity(ask);
		if(factory && commodity) {
			auto& totals = index.output_by_factory_product_day[
				{factory.index(), commodity.index(), date_key(date)}];
			totals.sold += quantity;
			totals.revenue += quantity * price;
		}
	}
	auto bid = state.world.concrete_trade_fill_get_concrete_market_bid_from_concrete_fill_bid(fill);
	auto factory = bid ? state.world.concrete_market_bid_get_factory_from_concrete_bid_factory(bid)
		: dcon::factory_id{};
	if(!factory) return;
	index.input_costs_by_factory_day[{factory.index(), date_key(date)}].first += quantity * price;
	auto request = state.world.concrete_trade_fill_get_freight_request_from_concrete_fill_freight_request(fill);
	if(!request) return;
	state.world.freight_request_for_each_freight_contract_request_as_freight_request(request,
		[&](dcon::freight_contract_request_id relation) {
			auto contract = state.world.freight_contract_request_get_freight_contract(relation);
			if(!contract || !index.indexed_freight_contracts.emplace(factory.index(), contract.index()).second) return;
			auto contract_date = state.world.freight_contract_get_created_on(contract);
			index.input_costs_by_factory_day[{factory.index(), date_key(contract_date)}].second +=
				std::max(0.0f, state.world.freight_contract_get_agreed_freight_price(contract));
		});
}

void add_exact_fill(sys::state const& state, firm_agency_event_store& index,
	physical::exact_person_goods::fill_record const& fill) {
	if(fill.id == 0 || !fill.dcon_ask) return;
	auto factory = state.world.concrete_market_ask_get_factory_from_concrete_ask_factory(fill.dcon_ask);
	auto commodity = state.world.concrete_market_ask_get_commodity_from_concrete_ask_commodity(fill.dcon_ask);
	if(!factory || !commodity) return;
	auto& totals = index.output_by_factory_product_day[
		{factory.index(), commodity.index(), date_key(fill.occurred_on)}];
	auto quantity = std::max(0.0f, fill.quantity);
	totals.sold += quantity;
	totals.revenue += quantity * std::max(0.0f, fill.execution_price);
}

std::shared_ptr<firm_agency_event_store> ensure_event_index(sys::state const& state) {
	if(!state.firm_agency_events)
		state.firm_agency_events = std::make_shared<firm_agency_event_store>();
	auto index = state.firm_agency_events;
	std::lock_guard lock(index->mutex);
	if(!index->initialized) {
		state.world.for_each_concrete_market_ask([&](dcon::concrete_market_ask_id ask) {
			add_offer(state, *index, ask);
		});
		state.world.for_each_concrete_trade_fill([&](dcon::concrete_trade_fill_id fill) {
			add_trade_fill(state, *index, fill);
		});
		for(auto const& fill : physical::exact_person_goods::fill_records(state))
			add_exact_fill(state, *index, fill);
		index->indexed_ask_count = state.world.concrete_market_ask_size();
		index->indexed_trade_fill_count = state.world.concrete_trade_fill_size();
		index->indexed_exact_fill_count = physical::exact_person_goods::fill_records(state).size();
		index->initialized = true;
	} else {
		auto ask_count = state.world.concrete_market_ask_size();
		for(auto i = index->indexed_ask_count; i < ask_count; ++i) {
			dcon::concrete_market_ask_id ask{dcon::concrete_market_ask_id::value_base_t(i)};
			if(state.world.concrete_market_ask_is_valid(ask)) add_offer(state, *index, ask);
		}
		index->indexed_ask_count = ask_count;

		auto trade_fill_count = state.world.concrete_trade_fill_size();
		for(auto i = index->indexed_trade_fill_count; i < trade_fill_count; ++i) {
			dcon::concrete_trade_fill_id fill{dcon::concrete_trade_fill_id::value_base_t(i)};
			if(state.world.concrete_trade_fill_is_valid(fill)) add_trade_fill(state, *index, fill);
		}
		index->indexed_trade_fill_count = trade_fill_count;

		auto const& exact_fills = physical::exact_person_goods::fill_records(state);
		for(auto i = index->indexed_exact_fill_count; i < exact_fills.size(); ++i)
			add_exact_fill(state, *index, exact_fills[i]);
		index->indexed_exact_fill_count = exact_fills.size();
	}
	return index;
}

float finite_nonnegative(float value, float fallback = 0.0f) {
	return std::isfinite(value) && value > 0.0f ? value : fallback;
}

float arrears_due(sys::state const& state, dcon::economic_actor_id debtor,
	dcon::commodity_id settlement) {
	float result = 0.0f;
	state.world.economic_actor_for_each_obligation_debtor_as_economic_actor(debtor, [&](auto relation) {
		auto obligation = state.world.obligation_debtor_get_obligation(relation);
		if(!obligation || state.world.obligation_get_kind(obligation) != uint8_t(relations::obligation_kind::payroll)
			|| state.world.obligation_get_status(obligation) != uint8_t(relations::obligation_status::active)
			|| state.world.obligation_get_settlement_commodity(obligation) != settlement) return;
		result += std::max(0.0f, relations::total_due(state, obligation));
	});
	return std::isfinite(result) ? result : 0.0f;
}

float output_in_transit(sys::state const& state, dcon::economic_actor_id owner,
	dcon::commodity_id commodity, dcon::site_id production_site) {
	float result = 0.0f;
	production_site.for_each_shipment_origin_as_site([&](auto origin) {
		auto shipment = state.world.shipment_origin_get_shipment(origin);
		auto ownership = shipment ? state.world.shipment_get_shipment_owner(shipment)
			: dcon::shipment_owner_id{};
		if(shipment && state.world.shipment_get_commodity(shipment) == commodity && ownership
			&& state.world.shipment_owner_get_economic_actor(ownership) == owner)
			result += std::max(0.0f, state.world.shipment_get_remaining_quantity(shipment));
	});
	return result;
}

float full_payroll(sys::state const& state, dcon::factory_id factory, float units, float capacity) {
	if(!std::isfinite(units) || !std::isfinite(capacity) || capacity <= epsilon) return 0.0f;
	return exact_person_economy::wage_due_for_factory(state, factory)
		* std::clamp(units / capacity, 0.0f, 1.0f);
}

float factory_collateral_value(sys::state const& state, dcon::factory_id factory) {
	auto type = state.world.factory_get_building_type(factory);
	if(!type) return 0.0f;
	auto province = compat::alice::province_for_factory(state, factory);
	auto market = province ? state.world.state_instance_get_market_from_local_market(
		state.world.province_get_state_membership(province)) : dcon::market_id{};
	auto output = state.world.factory_type_get_output(type);
	if(!market || !output) return 0.0f;
	auto capacity = finite_nonnegative(state.world.factory_get_productive_capacity(factory));
	auto units = finite_nonnegative(state.world.factory_type_get_output_amount(type))
		* finite_nonnegative(state.world.factory_get_productivity_factor(factory), 1.0f);
	auto price = physical::concrete_market::canonical_reference_price(state, market, output,
		state.current_date, 0.0f);
	return std::max(0.0f, capacity * units * finite_nonnegative(price) * 30.0f);
}

// Working capital is bank credit: owners are never called for cash.
void borrow_working_capital(sys::state& state, dcon::factory_id factory,
	dcon::monetary_account_id account, dcon::commodity_id settlement, float shortfall, float expected_annual_return) {
	if(!account || !settlement || !std::isfinite(shortfall) || shortfall <= epsilon) return;
	(void)banking::underwrite_factory_credit(state, factory, account, 0, shortfall,
		expected_annual_return, factory_collateral_value(state, factory));
}

void finance_working_capital(sys::state& state, dcon::factory_id factory,
	dcon::economic_actor_id firm, production_decision const& decision) {
	auto last_request = state.world.factory_get_agency_last_funding_request_date(factory);
	if(last_request && state.current_date.to_raw_value() - last_request.to_raw_value() < 8) return;
	auto daily_profit = std::max(0.0f, state.world.factory_get_agency_recent_profit(factory));
	float expected_return = std::clamp(daily_profit * 365.0f
		/ std::max(1.0f, decision.procurement_funding_shortfall + decision.payroll_funding_shortfall), 0.0f, 1.5f);
	bool requested = false;
	auto finance_one = [&](dcon::commodity_id settlement, float shortfall) {
		if(!settlement || !std::isfinite(shortfall) || shortfall <= epsilon) return;
		auto account = accounts::find_account(state, firm, settlement);
		if(!account) account = accounts::open_account(state, firm, settlement);
		if(!account) return;
		borrow_working_capital(state, factory, account, settlement, shortfall, expected_return);
		requested = true;
	};
	finance_one(decision.procurement_settlement, decision.procurement_funding_shortfall);
	if(decision.payroll_settlement != decision.procurement_settlement)
		finance_one(decision.payroll_settlement, decision.payroll_funding_shortfall);
	if(requested) state.world.factory_set_agency_last_funding_request_date(factory, state.current_date);
}

}

realized_event_totals realized_events_between(sys::state const& state,
	dcon::factory_id factory, dcon::commodity_id output,
	sys::date from_inclusive, sys::date to_inclusive) {
	realized_event_totals result{};
	if(!factory || !output || to_inclusive < from_inclusive) return result;
	auto index = ensure_event_index(state);
	std::lock_guard lock(index->mutex);
	auto first_output = index->output_by_factory_product_day.lower_bound(
		{factory.index(), output.index(), date_key(from_inclusive)});
	auto last_output = index->output_by_factory_product_day.upper_bound(
		{factory.index(), output.index(), date_key(to_inclusive)});
	for(auto event = first_output; event != last_output; ++event) {
		result.offered += event->second.offered;
		result.sold += event->second.sold;
		result.revenue += event->second.revenue;
	}
	auto first_input = index->input_costs_by_factory_day.lower_bound(
		{factory.index(), date_key(from_inclusive)});
	auto last_input = index->input_costs_by_factory_day.upper_bound(
		{factory.index(), date_key(to_inclusive)});
	for(auto event = first_input; event != last_input; ++event) {
		result.input_cost += event->second.first + event->second.second;
	}
	return result;
}

production_decision decide_factory(sys::state const& state, dcon::factory_id factory) {
	production_decision result{};
	if(!factory || !state.world.factory_is_valid(factory)
		|| state.world.factory_get_agency_lifecycle_status(factory) >= 2) return result;
	auto type = state.world.factory_get_building_type(factory);
	auto province = compat::alice::province_for_factory(state, factory);
	auto site = world::site::site_for_factory(state, factory);
	auto owner = actors::organizations::operator_actor_for_factory(state, factory);
	auto market = province ? state.world.state_instance_get_market_from_local_market(
		state.world.province_get_state_membership(province)) : dcon::market_id{};
	if(!type || !province || !site || !owner || !market) return result;
	if(technology::kernel::canonical_runtime_active(state)
		&& technology::kernel::factory_process_has_canonical_requirement(state, type)
		&& !technology::kernel::organization_can_operate_factory_type(state,
			actors::organizations::organization_for_actor(state, owner), type)) return result;
	auto capacity = finite_nonnegative(state.world.factory_get_productive_capacity(factory));
	auto productivity = std::max(0.0f, finite_nonnegative(state.world.factory_get_productivity_factor(factory), 1.0f));
	auto output_commodity = state.world.factory_type_get_output(type);
	auto output_per_unit = finite_nonnegative(state.world.factory_type_get_output_amount(type)) * productivity
		* physical::extraction::output_grade(state, factory);
	// An extraction plant plans against what its deposit allows per day, not
	// what remains after today's output, so staffing does not swing daily.
	if(physical::extraction::extracts_deposit(state, factory))
		capacity = output_per_unit > epsilon
			? std::min(capacity, physical::extraction::daily_ceiling(state, factory, state.current_date) / output_per_unit)
			: 0.0f;
	// A farm plans with the average product of a worker at the reference ratio
	// on its land; it cannot plan at all on land it may not work.
	if(physical::land::farms_land(state, factory)) {
		auto title = physical::land::title_for_farm(state, factory);
		output_per_unit *= physical::land::suitability(state, title, output_commodity);
		if(!physical::land::may_operate(state, title, owner, state.current_date)) capacity = 0.0f;
	}
	if(capacity <= epsilon || !output_commodity || output_per_unit <= epsilon) return result;
	auto output_price = state.world.factory_get_agency_expected_selling_price(factory);
	if(!std::isfinite(output_price) || output_price <= epsilon)
		output_price = physical::concrete_market::canonical_reference_price(state, market, output_commodity, state.current_date, 0.0f);
	result.expected_unit_revenue = finite_nonnegative(output_price) * output_per_unit;
	result.output_inventory = physical::inventory::quantity(state, site, output_commodity, owner);
	if(auto hub = physical::deposits::market_hub_for(state, market); hub)
		result.output_inventory += physical::inventory::quantity(state, hub, output_commodity, owner);
	result.output_inventory += output_in_transit(state, owner, output_commodity, site);

	auto const& inputs = state.world.factory_type_get_inputs(type);
	float input_cost_per_unit = 0.0f;
	for(uint32_t i = 0; i < economy::commodity_set::set_size; ++i) {
		auto commodity = inputs.commodity_type[i];
		if(!commodity) break;
		if(!physical::factory_inputs::ordinary_physical_input(state, commodity)) continue;
		auto price = physical::concrete_market::canonical_reference_price(state, market, commodity, state.current_date, 0.0f);
		input_cost_per_unit += finite_nonnegative(inputs.commodity_amounts[i]) * finite_nonnegative(price);
	}
	result.expected_variable_cost = input_cost_per_unit;
	result.expected_payroll_cost = full_payroll(state, factory, capacity, capacity);
	result.expected_gross_margin = result.expected_unit_revenue - result.expected_variable_cost
		- (capacity > epsilon ? result.expected_payroll_cost / capacity : 0.0f);

	// Production is anchored in persistent firm expectations and moves toward a
	// stock-adjusted target with inertia. The firm does not inspect today's fill.
	float sell_through = state.world.factory_get_agency_expected_sell_through(factory);
	if(!std::isfinite(sell_through) || sell_through <= epsilon) sell_through = default_sell_through;
	float desired = state.world.factory_get_agency_planned_units(factory);
	if(!std::isfinite(desired) || desired <= epsilon) desired = capacity * sell_through;
	float procurement_reliability = state.world.factory_get_agency_expected_input_reliability(factory);
	if(!std::isfinite(procurement_reliability) || procurement_reliability <= epsilon) procurement_reliability = 0.85f;
	desired = std::min(desired, capacity * (0.5f + 0.5f * std::clamp(procurement_reliability, 0.1f, 1.0f)));
	if(result.expected_gross_margin < -epsilon) desired = std::min(desired, capacity * probing_fraction);
	if(result.output_inventory > output_per_unit * capacity * output_target_days)
		desired *= std::clamp((output_per_unit * capacity * output_target_days)
			/ std::max(result.output_inventory, epsilon), 0.0f, 1.0f);

	auto funding = physical::factory_inputs::procurement_account_for(state, owner);
	auto account = funding.account;
	result.procurement_settlement = funding.settlement;
	auto payroll_settlement = state.world.factory_get_payroll_settlement(factory);
	result.payroll_settlement = payroll_settlement;
	auto payroll_account = payroll_settlement ? accounts::find_account(state, owner, payroll_settlement) : dcon::monetary_account_id{};
	auto procurement_cash = account ? std::max(0.0f, accounts::balance(state, account)
		- physical::concrete_market::reserved_bid_amount(state, account)) : 0.0f;
	auto payroll_cash = payroll_account ? std::max(0.0f, accounts::balance(state, payroll_account)
		- physical::concrete_market::reserved_bid_amount(state, payroll_account)
		- arrears_due(state, owner, payroll_settlement)
		- collective_labor::severance_liability_for_actor(state, owner, factory)
		- exact_person_economy::unpaid_wages_for_factory(state, factory)) : 0.0f;
	if(account && payroll_account && account == payroll_account)
		procurement_cash = payroll_cash = std::max(0.0f, procurement_cash
			- arrears_due(state, owner, payroll_settlement)
			- collective_labor::severance_liability_for_actor(state, owner, factory)
			- exact_person_economy::unpaid_wages_for_factory(state, factory));
	auto raw_procurement_cash = procurement_cash;
	auto raw_payroll_cash = payroll_cash;
	procurement_cash *= (1.0f - cash_safety_fraction);
	payroll_cash *= (1.0f - cash_safety_fraction);
	result.cash_limited_units = desired;
	auto procurement_cost = [&](float units) {
		float total = 0.0f;
		auto reliability = state.world.factory_get_agency_expected_input_reliability(factory);
		if(!std::isfinite(reliability) || reliability <= epsilon) reliability = 0.85f;
		auto cover = 1.0f + 0.5f * (1.0f - std::clamp(reliability, 0.1f, 1.0f));
		auto bid_markup = 0.05f + (1.0f - std::clamp(reliability, 0.1f, 1.0f)) * 0.15f;
		for(uint32_t i = 0; i < economy::commodity_set::set_size; ++i) {
			auto commodity = inputs.commodity_type[i];
			if(!commodity) break;
			if(physical::factory_inputs::ordinary_physical_input(state, commodity)) {
				auto price = physical::concrete_market::canonical_reference_price(state, market, commodity, state.current_date, 0.0f);
				auto required = finite_nonnegative(inputs.commodity_amounts[i]) * units * cover;
				total += std::max(0.0f, physical::factory_inputs::net_demand(state, site, owner, commodity, required)
					- physical::factory_inputs::active_factory_commitment(state, factory, site, commodity))
					* finite_nonnegative(price) * (1.0f + bid_markup);
			}
		}
		return total;
	};
	auto affordable = [&](float units) {
		auto procurement = procurement_cost(units);
		auto payroll = full_payroll(state, factory, units, capacity);
		if(account && payroll_account && account == payroll_account)
			return procurement + payroll <= procurement_cash + epsilon;
		return (!account ? procurement <= epsilon : procurement <= procurement_cash + epsilon)
			&& (!payroll_account ? payroll <= epsilon : payroll <= payroll_cash + epsilon);
	};
	if(!affordable(0.0f)) result.cash_limited_units = 0.0f;
	else {
		float low = 0.0f, high = desired;
		for(int i = 0; i < 32; ++i) {
			auto middle = (low + high) * 0.5f;
			if(affordable(middle)) low = middle;
			else high = middle;
		}
		result.cash_limited_units = low;
	}
	auto required_procurement_cash = procurement_cost(desired);
	auto required_payroll_cash = full_payroll(state, factory, desired, capacity);
	if(account && payroll_account && account == payroll_account) {
		result.procurement_funding_shortfall = std::max(0.0f,
			(required_procurement_cash + required_payroll_cash) / (1.0f - cash_safety_fraction) - raw_procurement_cash);
	} else {
		result.procurement_funding_shortfall = account
			? std::max(0.0f, required_procurement_cash / (1.0f - cash_safety_fraction) - raw_procurement_cash)
			: required_procurement_cash / (1.0f - cash_safety_fraction);
		result.payroll_funding_shortfall = payroll_account
			? std::max(0.0f, required_payroll_cash / (1.0f - cash_safety_fraction) - raw_payroll_cash)
			: required_payroll_cash / (1.0f - cash_safety_fraction);
	}
	result.desired_units = std::clamp(std::min(desired, result.cash_limited_units), 0.0f, capacity);
	result.desired_output = result.desired_units * output_per_unit;
	result.desired_utilization = capacity > epsilon ? result.desired_units / capacity : 0.0f;
	for(uint32_t i = 0; i < economy::commodity_set::set_size; ++i) {
		auto commodity = inputs.commodity_type[i];
		if(!commodity) break;
		result.required_inputs.commodity_type[i] = commodity;
		result.required_inputs.commodity_amounts[i] = finite_nonnegative(inputs.commodity_amounts[i]) * result.desired_units;
	}
	return result;
}

float desired_production(sys::state const& state, dcon::factory_id factory) {
	return decide_factory(state, factory).desired_output;
}

void update_decisions(sys::state& state) {
	// The day's investors, built only if some plant offers shares.
	std::optional<capital_market::pool> investor_pool;
	auto investors = [&]() -> capital_market::pool& {
		if(!investor_pool) investor_pool = capital_market::build_pool(state);
		return *investor_pool;
	};
	std::unordered_set<uint32_t> serviced_actors;
	std::unordered_set<uint32_t> defaulted_factories;
	std::unordered_set<uint32_t> actors_with_unscoped_defaults;
	// Realized operating profit per firm and per nation where its plants produce.
	std::map<std::pair<uint32_t, uint32_t>, float> taxable_profit;
	state.world.for_each_factory([&](dcon::factory_id factory) {
		if(state.world.factory_get_agency_lifecycle_status(factory) >= 2) return;
		auto actor = actors::organizations::operator_actor_for_factory(state, factory);
		if(!actor || !serviced_actors.insert(actor.index()).second) return;
		auto result = banking::service_actor_loans(state, actor, state.current_date, 30);
		for(auto factory : result.defaulted_factories)
			if(factory) defaulted_factories.insert(factory.index());
		if(result.has_unscoped_default) actors_with_unscoped_defaults.insert(actor.index());
	});

	state.world.for_each_factory([&](dcon::factory_id factory) {
		if(state.world.factory_get_agency_lifecycle_status(factory) >= 2) return;
		// A peasant cohort's farm is not a firm: no working-capital finance or planning.
		if(households::self_working_operator(state, factory)) return;
		auto type = state.world.factory_get_building_type(factory);
		auto province = compat::alice::province_for_factory(state, factory);
		auto site = world::site::site_for_factory(state, factory);
		auto owner = actors::organizations::operator_actor_for_factory(state, factory);
		auto market = province ? state.world.state_instance_get_market_from_local_market(
			state.world.province_get_state_membership(province)) : dcon::market_id{};
		if(!type || !site || !owner || !market) return;
		if(defaulted_factories.contains(factory.index())
			|| actors_with_unscoped_defaults.contains(owner.index()))
			state.world.factory_set_agency_distress_days(factory, std::max<uint16_t>(90,
				state.world.factory_get_agency_distress_days(factory)));
		finance_working_capital(state, factory, owner, decide_factory(state, factory));
		auto output = state.world.factory_type_get_output(type);
		auto hub = physical::deposits::market_hub_for(state, market);
		auto cursor = state.world.factory_get_agency_last_observation_date(factory);
		if(!cursor) cursor = state.current_date;
		// Decisions run before today's production, payroll, asks, and purchases.
		// Treat the saved date as inclusive: today's events are observed tomorrow.
		auto observation_through = state.current_date - 1;
		auto realized_events = realized_events_between(state, factory, output, cursor, observation_through);
		float offered = realized_events.offered;
		float sold = realized_events.sold;
		float revenue = realized_events.revenue;
		float sold_quantity = sold;
		if(offered > epsilon) {
			auto expected = state.world.factory_get_agency_expected_sell_through(factory);
			if(!std::isfinite(expected) || expected <= epsilon) expected = default_sell_through;
			auto observed = std::clamp(sold / offered, 0.0f, 1.0f);
			expected += expectation_alpha * (observed - expected);
			state.world.factory_set_agency_expected_sell_through(factory, std::clamp(expected, 0.05f, 1.0f));
			state.world.factory_set_agency_recent_revenue(factory, revenue);
			state.world.factory_set_agency_recent_unsold(factory, std::max(0.0f, offered - sold));
			if(sold_quantity > epsilon) {
				auto realized_price = revenue / sold_quantity;
				auto prior_price = state.world.factory_get_agency_expected_selling_price(factory);
				state.world.factory_set_agency_expected_selling_price(factory,
					prior_price > epsilon ? prior_price + expectation_alpha * (realized_price - prior_price) : realized_price);
			}
		}
		state.world.factory_set_agency_last_observation_date(factory, state.current_date);
		float inbound_quantity = 0.0f, inbound_days = 0.0f;
		site.for_each_shipment_destination_as_site([&](auto destination) {
			auto shipment = state.world.shipment_destination_get_shipment(destination);
			auto shipment_owner = shipment ? state.world.shipment_get_shipment_owner(shipment)
				: dcon::shipment_owner_id{};
			if(!shipment_owner || state.world.shipment_owner_get_economic_actor(shipment_owner) != owner) return;
			auto quantity = std::max(0.0f, state.world.shipment_get_remaining_quantity(shipment));
			inbound_quantity += quantity;
			inbound_days += quantity * float(std::max<uint32_t>(1, state.world.shipment_get_remaining_days(shipment)));
		});
		if(inbound_quantity > epsilon) {
			auto observed_days = inbound_days / inbound_quantity;
			auto expected_days = state.world.factory_get_agency_expected_delivery_days(factory);
			if(!std::isfinite(expected_days) || expected_days <= epsilon) expected_days = observed_days;
			state.world.factory_set_agency_expected_delivery_days(factory,
				std::clamp(expected_days + expectation_alpha * (observed_days - expected_days), 1.0f, 60.0f));
		}
		float realized_input_cost = realized_events.input_cost;
		auto [realized_payroll_due, realized_payroll_paid] = payroll::factory_payroll_between(
			state, factory, cursor, observation_through);
		auto realized_costs = realized_input_cost + realized_payroll_due;
		auto realized_profit = revenue - realized_costs;
		auto realized_cashflow = revenue - realized_input_cost - realized_payroll_paid;
		auto firm_organization = actors::organizations::organization_for_actor(state, owner);
		if(firm_organization) {
			state.world.organization_set_retained_earnings(firm_organization,
				state.world.organization_get_retained_earnings(firm_organization) + realized_profit);
			auto source_nation = state.world.province_get_nation_from_province_ownership(province);
			if(source_nation && std::isfinite(realized_profit) && !households::is_household(state, firm_organization))
				taxable_profit[{ uint32_t(firm_organization.index()), uint32_t(source_nation.index()) }] += realized_profit;
			auto equity_asset = actors::organizations::equity_asset_for_organization(state, firm_organization);
			if(equity_asset)
				state.world.asset_set_appraised_value(equity_asset,
					std::max(0.0f, state.world.organization_get_paid_in_equity(firm_organization)
						+ state.world.organization_get_retained_earnings(firm_organization)));
		}
		auto prior_costs = state.world.factory_get_agency_recent_costs(factory);
		auto prior_profit = state.world.factory_get_agency_recent_profit(factory);
		state.world.factory_set_agency_recent_costs(factory,
			prior_costs > epsilon ? prior_costs + expectation_alpha * (realized_costs - prior_costs) : realized_costs);
		state.world.factory_set_agency_recent_profit(factory,
			prior_profit != 0.0f ? prior_profit + expectation_alpha * (realized_profit - prior_profit) : realized_profit);
		auto prior_cashflow = state.world.factory_get_agency_cashflow(factory);
		state.world.factory_set_agency_cashflow(factory, prior_cashflow != 0.0f
			? prior_cashflow + expectation_alpha * (realized_cashflow - prior_cashflow) : realized_cashflow);
		if(offered > epsilon) {
			auto capacity = finite_nonnegative(state.world.factory_get_productive_capacity(factory));
			auto wage_per_unit = capacity > epsilon
				? exact_person_economy::wage_due_for_factory(state, factory) / capacity : 0.0f;
			state.world.factory_set_agency_expected_wage_per_worker(factory, wage_per_unit);
		}

		auto capacity = finite_nonnegative(state.world.factory_get_productive_capacity(factory));
		auto sell_through = state.world.factory_get_agency_expected_sell_through(factory);
		if(!std::isfinite(sell_through) || sell_through <= epsilon) sell_through = default_sell_through;
		auto last_plan_date = state.world.factory_get_agency_last_planning_date(factory);
		if(last_plan_date && state.current_date.to_raw_value() - last_plan_date.to_raw_value() < 8) return;
		auto distress_days = state.world.factory_get_agency_distress_days(factory);
		auto recent_profit = state.world.factory_get_agency_recent_profit(factory);
		bool distressed = sell_through < 0.35f || (recent_profit < -epsilon && offered > epsilon);
		if(distressed) distress_days = uint16_t(std::min<uint32_t>(65535, uint32_t(distress_days) + 7));
		else distress_days = uint16_t(distress_days > 7 ? distress_days - 7 : 0);
		state.world.factory_set_agency_distress_days(factory, distress_days);
		if(distress_days >= 90 && capacity > 0.1f) {
			capacity = std::max(0.1f, capacity * 0.98f);
			state.world.factory_set_productive_capacity(factory, capacity);
		}
		auto prior_plan = state.world.factory_get_agency_planned_units(factory);
		if(!std::isfinite(prior_plan) || prior_plan <= epsilon) prior_plan = capacity * default_sell_through;
		auto output_per_unit = finite_nonnegative(state.world.factory_type_get_output_amount(type))
			* finite_nonnegative(state.world.factory_get_productivity_factor(factory), 1.0f);
		auto const& inputs = state.world.factory_type_get_inputs(type);
		float input_unit_cost = 0.0f;
		for(uint32_t i = 0; i < economy::commodity_set::set_size; ++i) {
			auto commodity = inputs.commodity_type[i];
			if(!commodity) break;
			if(physical::factory_inputs::ordinary_physical_input(state, commodity))
				input_unit_cost += finite_nonnegative(inputs.commodity_amounts[i])
					* physical::concrete_market::canonical_reference_price(state, market, commodity, state.current_date, 0.0f);
		}
		state.world.factory_set_agency_expected_input_unit_cost(factory, input_unit_cost);
		auto stock = physical::inventory::quantity(state, site, output, owner)
			+ (hub ? physical::inventory::quantity(state, hub, output, owner) : 0.0f)
			+ output_in_transit(state, owner, output, site);
		auto target_stock = capacity * output_per_unit * 2.0f * sell_through;
		state.world.factory_set_agency_target_output_inventory(factory, target_stock);
		auto reliability = state.world.factory_get_agency_expected_input_reliability(factory);
		if(!std::isfinite(reliability) || reliability <= epsilon) reliability = 0.85f;
		state.world.factory_set_agency_target_input_coverage(factory, 1.0f + 0.5f * (1.0f - reliability));
		auto stock_pressure = target_stock > epsilon ? (target_stock - stock) / target_stock : 0.0f;
		auto target = capacity * std::clamp(sell_through + 0.30f * stock_pressure, 0.05f, 1.0f);
		auto max_step = std::max(0.05f, capacity * 0.10f);
		auto planned = std::clamp(target, std::max(0.0f, prior_plan - max_step), std::min(capacity, prior_plan + max_step));
		state.world.factory_set_agency_planned_units(factory, planned);
		state.world.factory_set_agency_target_employment(factory, planned);
		if(state.world.factory_get_agency_expected_selling_price(factory) <= epsilon)
			state.world.factory_set_agency_expected_selling_price(factory,
				physical::concrete_market::canonical_reference_price(state, market, output, state.current_date, 0.0f));
		auto markup = state.world.factory_get_agency_ask_markup(factory);
		if(!std::isfinite(markup) || markup <= epsilon) markup = default_markup;
		if(sell_through > 0.90f && stock < target_stock * 0.5f) markup += 0.01f;
		else if(sell_through < 0.55f || stock > target_stock * 1.5f) markup -= 0.015f;
		auto funding = physical::factory_inputs::procurement_account_for(state, owner);
		auto cash = funding.account ? std::max(0.0f, accounts::balance(state, funding.account)
			- physical::concrete_market::reserved_bid_amount(state, funding.account)) : 0.0f;
		if(cash < std::max(0.01f, state.world.factory_get_agency_recent_costs(factory) * 0.5f)) markup -= 0.02f;
		state.world.factory_set_agency_ask_markup(factory, std::clamp(markup, 0.02f, 0.60f));
		auto existing_project = state.world.factory_get_agency_expansion_project(factory);
		if(existing_project && (!state.world.capital_project_is_valid(existing_project)
			|| state.world.capital_project_get_status(existing_project) >= uint8_t(capital_projects::status::completed))) {
			state.world.factory_set_agency_expansion_project(factory, dcon::capital_project_id{});
			existing_project = {};
		}
		if(!existing_project && capacity > epsilon && planned >= capacity * 0.85f
			&& state.world.factory_get_actual_utilization(factory) >= 0.80f
			&& sell_through >= 0.88f && recent_profit > epsilon && distress_days == 0) {
			auto added_capacity = std::max(0.10f, capacity * 0.10f);
			auto const& construction = state.world.factory_type_get_construction_costs(type);
			auto construction_scale = added_capacity * 0.5f;
			float capital_cost = 0.0f;
			for(uint32_t i = 0; i < economy::commodity_set::set_size; ++i) {
				auto commodity = construction.commodity_type[i];
				if(!commodity) break;
				if(!physical::factory_inputs::ordinary_physical_input(state, commodity)) continue;
				auto quantity = finite_nonnegative(construction.commodity_amounts[i]) * construction_scale;
				capital_cost += quantity * physical::concrete_market::canonical_reference_price(
					state, market, commodity, state.current_date, 0.0f);
			}
			auto funding = physical::factory_inputs::procurement_account_for(state, owner);
			auto project_budget = capital_cost * 1.50f;
			economy::investment::project_inputs proposal{};
			proposal.capital_cost = capital_cost;
			proposal.gross_daily_revenue = state.world.factory_get_agency_expected_selling_price(factory)
				* output_per_unit * added_capacity;
			proposal.daily_material_cost = input_unit_cost * added_capacity * sell_through;
			proposal.daily_wage_cost = state.world.factory_get_agency_expected_wage_per_worker(factory) * added_capacity;
			proposal.expected_sell_through = sell_through;
			proposal.input_reliability = reliability;
			proposal.logistics_reliability = 1.0f / (1.0f + 0.02f
				* std::max(0.0f, state.world.factory_get_agency_expected_delivery_days(factory)));
			proposal.annual_interest_rate = banking::indicative_factory_loan_rate(state, factory,
				funding.settlement, std::max(1.0f, project_budget), factory_collateral_value(state, factory));
			proposal.demand_risk = 1.0f - sell_through;
			proposal.jobs = added_capacity;
			auto score = economy::investment::evaluate(proposal);
			if(score.privately_viable && capital_cost > epsilon && funding.account && project_budget > epsilon) {
				auto available_cash = std::max(0.0f, accounts::balance(state, funding.account)
					- physical::concrete_market::reserved_bid_amount(state, funding.account));
				auto operating = decide_factory(state, factory);
				auto daily_working_cost = std::max(state.world.factory_get_agency_recent_costs(factory),
					(operating.expected_variable_cost + (capacity > epsilon
						? operating.expected_payroll_cost / capacity : 0.0f)) * operating.desired_units);
				auto working_reserve = daily_working_cost * 2.0f;
				auto own_project_cash = std::max(0.0f, available_cash - working_reserve);
				// Own cash first, then bank credit, then new shares sold to the
				// investors of the plant's country who find the expansion worth it.
				auto external_need = std::max(0.0f, project_budget - own_project_cash);
				dcon::firm_capital_request_id project_request{};
				float borrowed = 0.0f;
				if(external_need > epsilon) {
					auto credit = banking::underwrite_factory_credit(state, factory, funding.account, 1,
						external_need, score.annual_return_on_capital, factory_collateral_value(state, factory));
					project_request = credit.request;
					borrowed = credit.funded_amount;
				}
				if(external_need - borrowed > epsilon && firm_organization) {
					capital_market::offering terms{};
					terms.issuer = firm_organization;
					terms.nation = state.world.province_get_nation_from_province_ownership(province);
					terms.settlement = funding.settlement;
					terms.amount = external_need - borrowed;
					terms.pre_money = capital_market::book_value(state, firm_organization);
					terms.annual_earnings = capital_market::expected_annual_earnings(state, firm_organization)
						+ score.expected_daily_cashflow * 365.0f;
					auto raised = capital_market::raise(state, investors(), terms);
					if(raised > 0.0f)
						state.world.factory_set_agency_owner_equity_contributed(factory,
							state.world.factory_get_agency_owner_equity_contributed(factory) + raised);
				}
				auto after_funding = std::max(0.0f, accounts::balance(state, funding.account)
					- physical::concrete_market::reserved_bid_amount(state, funding.account) - working_reserve);
				auto funded_fraction = std::clamp(after_funding / project_budget, 0.0f, 1.0f);
				if(funded_fraction >= 0.10f) {
					auto funded_capacity = added_capacity * funded_fraction;
					auto project = capital_projects::create_factory_expansion(state, factory, funded_capacity, funding.settlement);
					bool requirements_added = bool(project);
					for(uint32_t i = 0; requirements_added && i < economy::commodity_set::set_size; ++i) {
						auto commodity = construction.commodity_type[i];
						if(!commodity) break;
						if(!physical::factory_inputs::ordinary_physical_input(state, commodity)) continue;
						auto quantity = finite_nonnegative(construction.commodity_amounts[i])
							* construction_scale * funded_fraction;
						if(quantity > epsilon && !capital_projects::add_requirement(state, project, commodity, quantity))
							requirements_added = false;
					}
					if(requirements_added && capital_projects::fund(state, project, funding.account,
						project_budget * funded_fraction)) {
						if(project_request) state.world.force_create_firm_capital_request_project(project_request, project);
						state.world.factory_set_agency_expansion_project(factory, project);
					} else if(project) (void)capital_projects::cancel(state, project);
				}
			}
		}
		state.world.factory_set_agency_last_planning_date(factory, state.current_date);
	});

	// The profit tax of each nation is due on a firm's net realized profit from
	// its plants there; the assessed tax is an expense against retained earnings.
	for(auto const& [key, profit] : taxable_profit)
		(void)assess_profit_tax(state, dcon::organization_id{ dcon::organization_id::value_base_t(key.first) },
			dcon::nation_id{ dcon::nation_id::value_base_t(key.second) }, profit);
}

float assess_profit_tax(sys::state& state, dcon::organization_id organization, dcon::nation_id nation, float profit) {
	if(!organization || !state.world.organization_is_valid(organization) || !nation
		|| !std::isfinite(profit) || !(profit > epsilon) || households::is_household(state, organization)) return 0.0f;
	auto actor = actors::organizations::actor_for_organization(state, organization);
	auto wallet = actor ? wallets::account_for(state, actor, accounts::first_settlement_for(state, actor))
		: wallets::account_ref{};
	if(!wallet) return 0.0f;
	auto tax = governance::finance::assess_and_collect_topic_tax(state,
		governance::policy::topic_id::corporate_profit_tax, nation, actor, profit, wallet, {}, state.current_date);
	if(!(tax.assessed > 0.0f)) return 0.0f;
	state.world.organization_set_retained_earnings(organization,
		state.world.organization_get_retained_earnings(organization) - tax.assessed);
	if(auto equity_asset = actors::organizations::equity_asset_for_organization(state, organization))
		state.world.asset_set_appraised_value(equity_asset,
			std::max(0.0f, state.world.organization_get_paid_in_equity(organization)
				+ state.world.organization_get_retained_earnings(organization)));
	return tax.assessed;
}

void post_output_asks(sys::state& state) {
	state.world.for_each_factory([&](dcon::factory_id factory) {
		// Households sell through their own daily budget.
		if(households::is_household(state, actors::organizations::operator_organization_for_factory(state, factory))) return;
		auto type = state.world.factory_get_building_type(factory);
		auto province = compat::alice::province_for_factory(state, factory);
		auto owner = actors::organizations::operator_actor_for_factory(state, factory);
		auto output = type ? state.world.factory_type_get_output(type) : dcon::commodity_id{};
		auto market = province ? state.world.state_instance_get_market_from_local_market(
			state.world.province_get_state_membership(province)) : dcon::market_id{};
		auto hub = market ? physical::deposits::market_hub_for(state, market) : dcon::site_id{};
		if(!owner || !output || !hub) return;
		auto quantity = physical::inventory::quantity(state, hub, output, owner);
		if(quantity <= epsilon) return;
		auto reference = state.world.factory_get_agency_expected_selling_price(factory);
		if(!std::isfinite(reference) || reference <= epsilon)
			reference = physical::concrete_market::canonical_reference_price(state, market, output, state.current_date, 0.0f);
		auto markup = state.world.factory_get_agency_ask_markup(factory);
		if(!std::isfinite(markup) || markup <= epsilon) markup = default_markup;
		auto unit_cost_floor = std::max(0.0f, state.world.factory_get_agency_expected_input_unit_cost(factory)
			+ state.world.factory_get_agency_expected_wage_per_worker(factory));
		auto target_stock = state.world.factory_get_agency_target_output_inventory(factory);
		bool clearance = state.world.factory_get_agency_recent_profit(factory) < 0.0f
			|| (target_stock > epsilon && state.world.factory_get_agency_recent_unsold(factory) > target_stock);
		auto cost_floor = unit_cost_floor * (clearance ? 0.85f : 1.0f);
		auto minimum_price = std::max(reference * (clearance ? 0.75f : 1.0f), cost_floor)
			* (1.0f + std::clamp(markup, 0.02f, 0.60f));
		if(minimum_price > epsilon && std::isfinite(minimum_price))
			(void)physical::concrete_market::post_ask(state, owner, hub, market, output,
				quantity, minimum_price, physical::concrete_market::order_purpose::general, factory);
	});
}

void observe_production(sys::state& state, dcon::factory_id factory, float planned_units, float realized_units) {
	if(!factory || !state.world.factory_is_valid(factory)) return;
	planned_units = finite_nonnegative(planned_units);
	realized_units = finite_nonnegative(realized_units);
	auto unmet = std::max(0.0f, planned_units - realized_units);
	state.world.factory_set_agency_recent_unmet_input(factory, unmet);
	auto reliability = state.world.factory_get_agency_expected_input_reliability(factory);
	if(!std::isfinite(reliability) || reliability <= epsilon) reliability = 0.85f;
	auto observed = planned_units > epsilon ? std::clamp(realized_units / planned_units, 0.0f, 1.0f) : reliability;
	state.world.factory_set_agency_expected_input_reliability(factory,
		std::clamp(reliability + expectation_alpha * (observed - reliability), 0.10f, 1.0f));
}
} // namespace economy::firm_agency
