#include "cargo_transit.hpp"

#include <algorithm>
#include <cmath>

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
	auto const travel_days = std::clamp(
		nonnegative(raw.average_travel_days), 1.0f, 90.0f);
	auto const spoilage = std::isfinite(raw.daily_spoilage)
		? std::clamp(raw.daily_spoilage, 0.0f, 1.0f) : 0.0f;
	result.delivered_cargo = std::min(opening, opening / travel_days);
	auto const cargo_after_delivery = opening - result.delivered_cargo;
	result.spoiled_cargo = cargo_after_delivery * spoilage;
	result.closing_cargo = std::max(0.0f,
		cargo_after_delivery - result.spoiled_cargo + dispatched);
	return result;
}

} // namespace economy::cargo_transit
