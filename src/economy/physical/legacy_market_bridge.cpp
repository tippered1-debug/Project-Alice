#include "legacy_market_bridge.hpp"
#include "deposits.hpp"
#include "inventory.hpp"
#include "system_state.hpp"
#include "gamerule.hpp"

namespace economy::physical::legacy_market_bridge {

bool physical_path_enabled(sys::state const& state) noexcept {
	return gamerule::age_of_transformation_enabled(state);
}

void handoff_arrived_stock(sys::state& state) {
	if(!physical_path_enabled(state))
		return;
	state.world.for_each_market([&](dcon::market_id market) {
		auto hub = deposits::market_hub_for(state, market);
		if(!hub)
			return;
		state.world.for_each_commodity([&](dcon::commodity_id commodity) {
			if(state.world.commodity_get_rgo_amount(commodity) <= 0.0f
				|| state.world.commodity_get_money_rgo(commodity)
				|| state.world.commodity_get_is_local(commodity))
				return;
			auto moved = inventory::quantity(state, hub, commodity);
			if(moved <= 0.0f)
				return;
			moved = inventory::remove(state, hub, commodity, moved);
			state.world.market_set_stockpile(market, commodity,
				state.world.market_get_stockpile(market, commodity) + moved);
		});
	});
}

} // namespace economy::physical::legacy_market_bridge
