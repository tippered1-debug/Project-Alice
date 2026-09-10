#pragma once

namespace economy::cargo_transit {

struct inputs {
	bool enabled = false;
	float opening_cargo = 0.0f;
	float dispatched_cargo = 0.0f;
	float average_travel_days = 1.0f;
	float daily_spoilage = 0.0f;
};

struct result {
	float delivered_cargo = 0.0f;
	float closing_cargo = 0.0f;
	float spoiled_cargo = 0.0f;
};

result advance(inputs const& inputs) noexcept;

} // namespace economy::cargo_transit
