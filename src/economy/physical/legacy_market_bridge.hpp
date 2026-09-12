#pragma once

#include "dcon_generated.hpp"

namespace sys { class state; }

namespace economy::physical::legacy_market_bridge {

bool physical_path_enabled(sys::state const&) noexcept;
void handoff_arrived_stock(sys::state&);

} // namespace economy::physical::legacy_market_bridge
