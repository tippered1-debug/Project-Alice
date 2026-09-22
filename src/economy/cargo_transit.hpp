#pragma once

namespace economy::cargo_transit {

struct inputs {
	bool enabled = false;
	float opening_cargo = 0.0f;
	float dispatched_cargo = 0.0f;
	// Canonical callers provide route-derived values. average_travel_days is
	// retained only for explicit legacy callers.
	float route_distance = 0.0f;
	float route_travel_days = 0.0f;
	float route_capacity = 0.0f;
	float cargo_weight = 1.0f;
	float capacity_utilization = 0.0f;
	float average_travel_days = 1.0f;
	float daily_spoilage = 0.0f;
};

struct result {
	float delivered_cargo = 0.0f;
	float closing_cargo = 0.0f;
	float spoiled_cargo = 0.0f;
	float blocked_cargo = 0.0f;
};

result advance(inputs const& inputs) noexcept;

} // namespace economy::cargo_transit
