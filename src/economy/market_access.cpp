#include "market_access.hpp"

#include "system_state.hpp"
#include "economy_constants.hpp"
#include "economy_stats.hpp"
#include "gamerule.hpp"
#include "province.hpp"

#include <algorithm>
#include <cmath>

namespace economy::market_access {
namespace {

float nonnegative(float value) noexcept {
	return std::isfinite(value) && value > 0.0f ? value : 0.0f;
}

float unit(float value, float fallback = 0.0f) noexcept {
	return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : fallback;
}

} // namespace

result evaluate(inputs const& raw) noexcept {
	result result{};
	auto const railway = nonnegative(raw.railway_level);
	auto const distance = nonnegative(raw.distance_to_market_hub);
	result.effective_distance = distance / (1.0f + railway * 0.75f);
	auto const distance_access = 1.0f
		/ (1.0f + result.effective_distance / 150.0f);
	auto const administration = 0.20f + 0.80f * unit(raw.control_ratio);
	auto const staffed_network = 0.25f
		+ 0.75f * unit(raw.transport_labor_availability);
	auto const congestion = 1.0f
		/ (1.0f + 2.0f * nonnegative(raw.local_capacity_utilization));
	result.access = std::clamp(distance_access * administration
		* staffed_network * congestion, 0.05f, 1.0f);
	result.freight_cost_multiplier = 1.0f + (1.0f - result.access) * 3.0f;
	// Represents handling and en-route spoilage, not disappearance of all goods
	// denied immediate access. It is intentionally capped at three percent.
	result.handling_loss = (1.0f - result.access) * 0.03f;
	return result;
}

result evaluate_province(sys::state const& state, dcon::province_id province_id) {
	if(!province_id || !state.world.province_is_valid(province_id)
			|| !gamerule::age_of_transformation_enabled(state))
		return {};
	auto const state_instance =
		state.world.province_get_state_membership(province_id);
	if(!state_instance || !state.world.state_instance_is_valid(state_instance))
		return evaluate({.control_ratio = 0.0f,
			.transport_labor_availability = 0.0f});
	auto const capital = state.world.state_instance_get_capital(state_instance);
	inputs derived{};
	// direct_distance is observational but retains an old non-const signature.
	derived.distance_to_market_hub = capital
		? province::direct_distance(const_cast<sys::state&>(state), province_id, capital)
		: 1000.0f;
	derived.railway_level = float(state.world.province_get_building_level(
		province_id, uint8_t(economy::province_building_type::railroad)));
	derived.control_ratio = state.world.province_get_control_ratio(province_id);
	derived.transport_labor_availability =
		state.world.province_get_labor_demand_satisfaction_size()
			> uint32_t(economy::labor::no_education)
		? state.world.province_get_labor_demand_satisfaction(
			province_id, economy::labor::no_education)
		: 0.0f;
	return evaluate(derived);
}

} // namespace economy::market_access
