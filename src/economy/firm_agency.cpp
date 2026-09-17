#include "firm_agency.hpp"

#include "accounts/accounts.hpp"
#include "actors/organizations/organizations.hpp"
#include "compat/alice/legacy_bridge.hpp"
#include "economy/physical/concrete_market.hpp"
#include "economy/physical/deposits.hpp"
#include "economy/physical/factory_inputs.hpp"
#include "inventory.hpp"
#include "relations/relations.hpp"
#include "system_state.hpp"
#include "world/site.hpp"

#include <algorithm>
#include <cmath>

namespace economy::firm_agency {
namespace {
constexpr float epsilon = 1.0e-5f;
constexpr float probing_fraction = 0.05f;
constexpr float output_target_days = 3.0f;
constexpr float cash_safety_fraction = 0.10f;

float finite_nonnegative(float value, float fallback = 0.0f) {
	return std::isfinite(value) && value > 0.0f ? value : fallback;
}

float arrears_due(sys::state const& state, dcon::economic_actor_id debtor) {
	float result = 0.0f;
	state.world.economic_actor_for_each_obligation_debtor_as_economic_actor(debtor, [&](auto relation) {
		auto obligation = state.world.obligation_debtor_get_obligation(relation);
		if(!obligation || state.world.obligation_get_kind(obligation) != uint8_t(relations::obligation_kind::payroll)
			|| state.world.obligation_get_status(obligation) != uint8_t(relations::obligation_status::active)) return;
		result += std::max(0.0f, relations::total_due(state, obligation));
	});
	return std::isfinite(result) ? result : 0.0f;
}

float full_payroll(sys::state const& state, dcon::factory_id factory,
	dcon::province_id province, float units, float capacity) {
	if(!province || capacity <= epsilon) return 0.0f;
	auto ratio = std::clamp(units / capacity, 0.0f, 1.0f);
	return ratio * (
		finite_nonnegative(state.world.factory_get_unqualified_employment(factory))
			* finite_nonnegative(state.world.province_get_labor_price(province, economy::labor::no_education))
		+ finite_nonnegative(state.world.factory_get_primary_employment(factory))
			* finite_nonnegative(state.world.province_get_labor_price(province, economy::labor::basic_education))
		+ finite_nonnegative(state.world.factory_get_secondary_employment(factory))
			* finite_nonnegative(state.world.province_get_labor_price(province, economy::labor::high_education)));
}
}

production_decision decide_factory(sys::state const& state, dcon::factory_id factory) {
	production_decision result{};
	if(!factory || !state.world.factory_get_canonical_production(factory)) return result;
	auto type = state.world.factory_get_building_type(factory);
	auto province = compat::alice::province_for_factory(state, factory);
	auto site = world::site::site_for_factory(state, factory);
	auto owner = actors::organizations::operator_actor_for_factory(state, factory);
	auto market = province ? state.world.state_instance_get_market_from_local_market(
		state.world.province_get_state_membership(province)) : dcon::market_id{};
	if(!type || !province || !site || !owner || !market) return result;
	auto capacity = finite_nonnegative(state.world.factory_get_productive_capacity(factory));
	auto productivity = std::max(0.0f, finite_nonnegative(state.world.factory_get_productivity_factor(factory), 1.0f));
	auto output_commodity = state.world.factory_type_get_output(type);
	auto output_per_unit = finite_nonnegative(state.world.factory_type_get_output_amount(type)) * productivity;
	if(capacity <= epsilon || !output_commodity || output_per_unit <= epsilon) return result;
	auto output_price = physical::concrete_market::canonical_reference_price(state, market, output_commodity, state.current_date, 0.0f);
	result.expected_unit_revenue = finite_nonnegative(output_price) * output_per_unit;
	result.output_inventory = physical::inventory::quantity(state, site, output_commodity, owner);
	if(auto hub = physical::deposits::market_hub_for(state, market); hub)
		result.output_inventory += physical::inventory::quantity(state, hub, output_commodity, owner);

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
	result.expected_payroll_cost = full_payroll(state, factory, province, capacity, capacity);
	result.expected_gross_margin = result.expected_unit_revenue - result.expected_variable_cost
		- (capacity > epsilon ? result.expected_payroll_cost / capacity : 0.0f);

	// A healthy firm starts from capacity; negative margin leaves only a small
	// probe so reference prices can still form without forced full production.
	float desired = result.expected_gross_margin > epsilon ? capacity : capacity * probing_fraction;
	if(result.expected_gross_margin < -epsilon) desired = capacity * probing_fraction;
	if(result.output_inventory > output_per_unit * capacity * output_target_days)
		desired *= std::clamp((output_per_unit * capacity * output_target_days)
			/ std::max(result.output_inventory, epsilon), 0.0f, 1.0f);

	auto settlement = state.world.factory_get_payroll_settlement(factory);
	if(!settlement) settlement = accounts::first_settlement_for(state, owner);
	auto account = settlement ? accounts::find_account(state, owner, settlement) : dcon::monetary_account_id{};
	auto free_cash = account ? std::max(0.0f, accounts::balance(state, account)
		- physical::concrete_market::reserved_bid_amount(state, account)) : 0.0f;
	free_cash = std::max(0.0f, free_cash - arrears_due(state, owner));
	free_cash *= (1.0f - cash_safety_fraction);
	result.cash_limited_units = desired;
	float procurement_cost = 0.0f;
	for(uint32_t i = 0; i < economy::commodity_set::set_size; ++i) {
		auto commodity = inputs.commodity_type[i];
		if(!commodity) break;
		result.required_inputs.commodity_type[i] = commodity;
		result.required_inputs.commodity_amounts[i] = finite_nonnegative(inputs.commodity_amounts[i]) * desired;
		if(physical::factory_inputs::ordinary_physical_input(state, commodity)) {
			auto price = physical::concrete_market::canonical_reference_price(state, market, commodity, state.current_date, 0.0f);
			procurement_cost += physical::factory_inputs::net_demand(state, site, owner, commodity,
				result.required_inputs.commodity_amounts[i]) * finite_nonnegative(price);
		}
	}
	auto required_cash_per_unit = desired > epsilon ? procurement_cost / desired
		+ result.expected_payroll_cost / capacity : 0.0f;
	if(required_cash_per_unit > epsilon)
		result.cash_limited_units = std::min(desired, free_cash / required_cash_per_unit);
	result.desired_units = std::clamp(std::min(desired, result.cash_limited_units), 0.0f, capacity);
	result.desired_output = result.desired_units * output_per_unit;
	result.desired_utilization = capacity > epsilon ? result.desired_units / capacity : 0.0f;
	return result;
}

float desired_production(sys::state const& state, dcon::factory_id factory) {
	return decide_factory(state, factory).desired_output;
}
} // namespace economy::firm_agency
