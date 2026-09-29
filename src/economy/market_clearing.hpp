#pragma once

#include "dcon_generated_ids.hpp"
#include "system_state_forward.hpp"

#include <cstdint>

namespace economy::market_clearing {

// Demand classes remain labels at legacy API boundaries. They do not create a
// second market or determine allocation; fulfillment comes from concrete orders.
enum class demand_class : uint8_t {
	life_needs,
	everyday_needs,
	luxury_needs,
	intermediate,
	government,
	construction,
	inventory,
	trade,
	other,
	count
};

// Read-only compatibility view of the concrete market's actual buyer fill.
float fill(sys::state const& state, dcon::market_id market,
	dcon::commodity_id commodity, demand_class category) noexcept;

} // namespace economy::market_clearing
