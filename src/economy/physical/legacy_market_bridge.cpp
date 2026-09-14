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
			if(state.world.commodity_get_money_rgo(commodity)
				|| state.world.commodity_get_is_local(commodity))
				return;
			float moved_total = 0.0f;
			state.world.site_for_each_physical_stock_site_as_site(hub, [&](dcon::physical_stock_site_id relation) {
				auto stock = state.world.physical_stock_site_get_physical_stock(relation);
				if(state.world.physical_stock_get_commodity_from_physical_stock_commodity(stock) != commodity)
					return;
				auto owner = state.world.physical_stock_get_economic_actor_from_physical_stock_owner(stock);
				auto amount = inventory::quantity(state, hub, commodity, owner);
				moved_total += inventory::remove(state, hub, commodity, amount, owner);
			});
			if(moved_total > 0.0f)
				state.world.market_set_stockpile(market, commodity,
					state.world.market_get_stockpile(market, commodity) + moved_total);
		});
	});
}

} // namespace economy::physical::legacy_market_bridge
