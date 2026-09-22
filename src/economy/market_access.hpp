#pragma once

#include "dcon_generated_ids.hpp"
#include "system_state_forward.hpp"

namespace economy::market_access {

struct inputs {
	float distance_to_market_hub = 0.0f;
	float railway_level = 0.0f;
	float control_ratio = 1.0f;
	float transport_labor_availability = 1.0f;
	float local_capacity_utilization = 0.0f;
};

struct result {
	float access = 1.0f;
	float effective_distance = 0.0f;
	float freight_cost_multiplier = 1.0f;
	float handling_loss = 0.0f;
};

result evaluate(inputs const& inputs) noexcept;
result evaluate_province(sys::state const& state, dcon::province_id province);

} // namespace economy::market_access
