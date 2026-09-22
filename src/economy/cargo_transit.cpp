#include "cargo_transit.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace economy::cargo_transit {
namespace {

float nonnegative(float value) noexcept {
	return std::isfinite(value) && value > 0.0f ? value : 0.0f;
}

} // namespace

result advance(inputs const& raw) noexcept {
	result result{};
	auto const opening = nonnegative(raw.opening_cargo);
	auto const dispatched = nonnegative(raw.dispatched_cargo);
	if(!raw.enabled) {
		result.delivered_cargo = dispatched;
		result.closing_cargo = opening;
		return result;
	}
	auto const travel_days = raw.route_travel_days > 0.0f
		? std::clamp(nonnegative(raw.route_travel_days), 1.0f, 90.0f)
		: std::clamp(nonnegative(raw.average_travel_days), 1.0f, 90.0f);
	auto const cargo_weight = std::max(1.0e-6f, nonnegative(raw.cargo_weight));
	auto const available_capacity = raw.route_capacity > 0.0f
		? std::max(0.0f, raw.route_capacity
			* (1.0f - std::clamp(raw.capacity_utilization, 0.0f, 1.0f)) / cargo_weight)
		: std::numeric_limits<float>::infinity();
	auto const admitted = std::min(dispatched, available_capacity);
	result.blocked_cargo = std::max(0.0f, dispatched - admitted);
	auto const spoilage = std::isfinite(raw.daily_spoilage)
		? std::clamp(raw.daily_spoilage, 0.0f, 1.0f) : 0.0f;
	result.delivered_cargo = std::min(opening, opening / travel_days);
	auto const cargo_after_delivery = opening - result.delivered_cargo;
	result.spoiled_cargo = cargo_after_delivery * spoilage;
	result.closing_cargo = std::max(0.0f,
		cargo_after_delivery - result.spoiled_cargo + admitted);
	return result;
}

} // namespace economy::cargo_transit
