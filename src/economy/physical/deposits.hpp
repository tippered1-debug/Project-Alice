#pragma once

#include "dcon_generated.hpp"

namespace sys { class state; }

namespace economy::physical::deposits {

void bootstrap(sys::state& state);
dcon::resource_deposit_id create_deposit(sys::state&, dcon::site_id, dcon::commodity_id,
	float original_reserves, float remaining_reserves, float grade, float daily_capacity,
	float target_daily_extraction, uint8_t status = 0, bool legacy_compatibility = false);
bool initialize_deposit(sys::state&, dcon::resource_deposit_id, dcon::site_id, dcon::commodity_id,
	float original_reserves, float remaining_reserves, float grade, float daily_capacity,
	float target_daily_extraction, uint8_t status = 0, bool legacy_compatibility = false);
dcon::site_id extraction_site_for(sys::state const& state, dcon::province_id province, dcon::commodity_id commodity);
dcon::resource_deposit_id deposit_for(sys::state const& state, dcon::province_id province, dcon::commodity_id commodity);
dcon::site_id market_hub_for(sys::state const& state, dcon::market_id market);

} // namespace economy::physical::deposits
