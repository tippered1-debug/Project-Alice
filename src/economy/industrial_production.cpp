#include "industrial_production.hpp"

#include "system_state.hpp"
#include "economy_production.hpp"
#include "economy/physical/factory_inputs.hpp"
#include "economy/physical/factory_output.hpp"
#include "actors/organizations/organizations.hpp"
#include "compat/alice/legacy_bridge.hpp"
#include "world/site.hpp"
#include "economy/payroll.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/firm_agency.hpp"
#include "economy/physical/concrete_labor.hpp"

#include <algorithm>
#include <cmath>

namespace economy::industrial_production {
namespace {
float finite_nonnegative(float value, float fallback = 0.0f) {
	return std::isfinite(value) && value >= 0.0f ? value : fallback;
}
float labor_units(sys::state const& state, dcon::factory_id factory) {
	return std::max(0.0f, physical::concrete_labor::labor_supplied_to_factory(state, factory));
}
}

bool set_productive_capacity(sys::state& state, dcon::factory_id factory, float capacity) {
	if(!factory || !state.world.factory_is_valid(factory) || !std::isfinite(capacity) || capacity <= 0.0f) return false;
	state.world.factory_set_productive_capacity(factory, capacity);
	return true;
}

bool set_productivity_factor(sys::state& state, dcon::factory_id factory, float productivity) {
	if(!factory || !state.world.factory_is_valid(factory) || !std::isfinite(productivity) || productivity <= 0.0f) return false;
	state.world.factory_set_productivity_factor(factory, productivity);
	return true;
}

void bootstrap_factory(sys::state& state, dcon::factory_id factory) {
	if(!factory || !state.world.factory_is_valid(factory)) return;
	bool already_canonical = state.world.factory_get_canonical_production(factory);
	auto type = state.world.factory_get_building_type(factory);
	auto base_workforce = type ? float(state.world.factory_type_get_base_workforce(type)) : 0.0f;
	auto output = type ? state.world.factory_type_get_output(type) : dcon::commodity_id{};
	if(!already_canonical) {
		auto size = finite_nonnegative(state.world.factory_get_size(factory));
		auto technology = finite_nonnegative(state.world.factory_get_technology_scale(factory), 1.0f);
		state.world.factory_set_productive_capacity(factory, base_workforce > 0.0f ? size / base_workforce : 0.0f);
		state.world.factory_set_productivity_factor(factory, technology > 0.0f ? technology : 1.0f);
		// Legacy/UI utilization remains available, but canonical production is
		// decided from firm economics below rather than this compatibility field.
		state.world.factory_set_target_utilization(factory, 0.0f);
		state.world.factory_set_actual_utilization(factory, 0.0f);
		state.world.factory_set_canonical_production(factory, output && !state.world.commodity_get_is_local(output)
			&& !state.world.commodity_get_money_rgo(output));
	}
	if(state.world.factory_get_canonical_production(factory) && !state.world.factory_get_payroll_settlement(factory)) {
		auto operator_actor = actors::organizations::operator_actor_for_factory(state, factory);
		dcon::commodity_id settlement{};
		bool ambiguous = false;
		if(operator_actor) state.world.economic_actor_for_each_monetary_account_owner_as_economic_actor(operator_actor, [&](auto relation) {
			auto account = state.world.monetary_account_owner_get_monetary_account(relation);
			auto candidate = economy::accounts::settlement_of(state, account);
			if(!settlement) settlement = candidate;
			else if(candidate != settlement) ambiguous = true;
		});
		if(settlement && !ambiguous) state.world.factory_set_payroll_settlement(factory, settlement);
	}
}

void bootstrap_factories(sys::state& state) {
	state.world.for_each_factory([&](dcon::factory_id factory) { bootstrap_factory(state, factory); });
}

bool plan_factory_inputs(sys::state& state, dcon::factory_id factory, dcon::province_id province, dcon::market_id market) {
	if(!factory || !province || !market) return false;
	auto type = state.world.factory_get_building_type(factory);
	auto site = world::site::site_for_factory(state, factory);
	auto owner = actors::organizations::operator_actor_for_factory(state, factory);
	if(!type || !site || !owner) return false;
	auto capacity = finite_nonnegative(state.world.factory_get_productive_capacity(factory));
	auto desired = firm_agency::decide_factory(state, factory).desired_units;
	auto planned = std::min(desired, labor_units(state, factory));
	if(!std::isfinite(planned) || planned < 0.0f) planned = 0.0f;
	return physical::factory_inputs::plan(state, factory, site, owner, state.world.factory_type_get_inputs(type), market, planned);
}

float produce_factory(sys::state& state, dcon::factory_id factory) {
	if(!factory || !state.world.factory_is_valid(factory)) return 0.0f;
	auto type = state.world.factory_get_building_type(factory);
	auto province = compat::alice::province_for_factory(state, factory);
	auto site = world::site::site_for_factory(state, factory);
	auto owner = actors::organizations::operator_actor_for_factory(state, factory);
	if(!type || !province || !site || !owner) return 0.0f;
	auto capacity = finite_nonnegative(state.world.factory_get_productive_capacity(factory));
	auto productivity = finite_nonnegative(state.world.factory_get_productivity_factor(factory), 1.0f);
	auto desired = firm_agency::decide_factory(state, factory).desired_units;
	auto planned = std::min(desired, labor_units(state, factory));
	auto market = state.world.state_instance_get_market_from_local_market(state.world.province_get_state_membership(province));
	auto available = physical::factory_inputs::evaluate(state, site, owner, state.world.factory_type_get_inputs(type), market, planned);
	auto ratio = available.active ? std::min(available.physical_ratio, available.legacy_ratio) : available.legacy_ratio;
	if(!std::isfinite(ratio)) ratio = 0.0f;
	auto actual_units = std::clamp(planned * std::clamp(ratio, 0.0f, 1.0f), 0.0f, capacity);
	auto actual_output = actual_units * std::max(0.0f, state.world.factory_type_get_output_amount(type)) * productivity;
	if(!std::isfinite(actual_units) || !std::isfinite(actual_output) || actual_units < 0.0f || actual_output < 0.0f) return 0.0f;
	if(actual_units > 0.0f && !physical::factory_inputs::consume(state, site, owner, state.world.factory_type_get_inputs(type), actual_units, 1.0f)) return 0.0f;
	state.world.factory_set_actual_utilization(factory, capacity > 0.0f ? std::clamp(actual_units / capacity, 0.0f, 1.0f) : 0.0f);
	state.world.factory_set_output(factory, actual_output);
	if(actual_output > 0.0f) physical::factory_output::materialize_and_dispatch(state, factory, actual_output);
	::economy::payroll::settle_factory(state, factory, actual_units, planned);
	return actual_output;
}

} // namespace economy::industrial_production
