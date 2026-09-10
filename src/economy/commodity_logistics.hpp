#pragma once

#include "dcon_generated_ids.hpp"
#include "system_state_forward.hpp"

namespace economy::logistics {

struct commodity_profile {
	float cargo_weight = 1.0f;
	float daily_spoilage = 0.0005f;
	float daily_storage_cost = 0.00025f;
	float handling_cost_multiplier = 1.0f;
	float target_inventory_days = 10.0f;
	float maximum_daily_release = 0.025f;
};

commodity_profile sanitize(commodity_profile profile) noexcept;
commodity_profile profile_for(sys::state const& state,
	dcon::commodity_id commodity) noexcept;

float cargo_units(commodity_profile profile, float quantity) noexcept;
float target_inventory(commodity_profile profile,
	float expected_daily_demand) noexcept;
float inventory_release(commodity_profile profile, float stock,
	float expected_daily_demand, float expected_daily_supply) noexcept;
float inventory_after_storage(commodity_profile profile, float stock) noexcept;

} // namespace economy::logistics
