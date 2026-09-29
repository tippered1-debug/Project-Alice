#include "market_clearing.hpp"

#include "system_state.hpp"

#include <algorithm>
#include <cmath>

namespace economy::market_clearing {

float fill(sys::state const& state, dcon::market_id market,
	dcon::commodity_id commodity, demand_class category) noexcept {
	(void)category;
	if(!market || !commodity
			|| !state.world.market_is_valid(market)
			|| !state.world.commodity_is_valid(commodity))
		return 0.0f;
	auto const value = state.world.market_get_actual_probability_to_buy(market, commodity);
	return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}

} // namespace economy::market_clearing
