#pragma once

#include "dcon_generated.hpp"

namespace sys { class state; }

namespace economy::physical::deposits {

void bootstrap(sys::state& state);
dcon::site_id extraction_site_for(sys::state const& state, dcon::province_id province, dcon::commodity_id commodity);
dcon::site_id market_hub_for(sys::state const& state, dcon::market_id market);

} // namespace economy::physical::deposits
