#include "factory_output.hpp"

#include "actors/organizations/organizations.hpp"
#include "economy_stats.hpp"
#include "gamerule.hpp"
#include "inventory.hpp"
#include "shipments.hpp"
#include "deposits.hpp"
#include "exchange.hpp"
#include "system_state.hpp"
#include "compat/alice/legacy_bridge.hpp"
#include "world/site.hpp"

#include <cmath>

namespace economy::physical::factory_output {

namespace {
bool legacy_fallback(sys::state& state, dcon::factory_id factory, dcon::commodity_id commodity, float amount) {
	if(!factory || !commodity || amount <= 0.0f)
		return false;
	auto province = compat::alice::province_for_factory(state, factory);
	auto local_state = province ? state.world.province_get_state_membership(province) : dcon::state_instance_id{};
	auto market = local_state ? state.world.state_instance_get_market_from_local_market(local_state) : dcon::market_id{};
	if(!market)
		return false;
	register_domestic_supply(state, market, commodity, amount, economy_reason::factory);
	return true;
}
}

bool materialize_and_dispatch(sys::state& state, dcon::factory_id factory, float produced_amount) {
	if(!factory || !std::isfinite(produced_amount) || produced_amount <= 0.0f)
		return true;
	auto factory_type = state.world.factory_get_building_type(factory);
	auto commodity = factory_type ? state.world.factory_type_get_output(factory_type) : dcon::commodity_id{};
	if(!commodity)
		return legacy_fallback(state, factory, commodity, produced_amount);
	// Local and money-like goods remain on the legacy compatibility path. This
	// v1 only physicalizes ordinary shippable factory output.
	if(state.world.commodity_get_is_local(commodity) || state.world.commodity_get_money_rgo(commodity))
		return legacy_fallback(state, factory, commodity, produced_amount);
	auto origin = world::site::site_for_factory(state, factory);
	auto operator_actor = actors::organizations::operator_actor_for_factory(state, factory);
	auto province = world::site::province_for_site(state, origin);
	auto local_state = province ? state.world.province_get_state_membership(province) : dcon::state_instance_id{};
	auto market = local_state ? state.world.state_instance_get_market_from_local_market(local_state) : dcon::market_id{};
	auto hub = market ? deposits::market_hub_for(state, market) : dcon::site_id{};
	if(!origin || !operator_actor || !market || !hub)
		return false;
	if(inventory::add(state, origin, commodity, produced_amount, operator_actor) != produced_amount)
		return false;
	// If dispatch fails, the materialized output remains at the factory site.
	if(!shipments::dispatch(state, origin, hub, commodity, produced_amount, operator_actor))
		return false;
	return true;
}

dcon::transaction_id sell_output(sys::state& state, dcon::factory_id factory,
	dcon::economic_actor_id buyer, float quantity, float unit_price,
	dcon::commodity_id settlement, sys::date timestamp) {
	if(!factory || !buyer || !std::isfinite(quantity) || quantity <= 0.0f)
		return {};
	auto operator_actor = actors::organizations::operator_actor_for_factory(state, factory);
	auto origin = world::site::site_for_factory(state, factory);
	auto type = state.world.factory_get_building_type(factory);
	auto commodity = type ? state.world.factory_type_get_output(type) : dcon::commodity_id{};
	if(!operator_actor || !origin || !commodity) return {};
	// Normal production dispatches output to the local market hub before sale;
	// retain the factory site as a useful direct-sale fallback for callers that
	// materialize output without dispatching it.
	auto province = world::site::province_for_site(state, origin);
	auto zone = province ? state.world.province_get_state_membership(province) : dcon::state_instance_id{};
	auto market = zone ? state.world.state_instance_get_market_from_local_market(zone) : dcon::market_id{};
	if(auto hub = market ? deposits::market_hub_for(state, market) : dcon::site_id{};
		hub && inventory::quantity(state, hub, commodity, operator_actor) >= quantity)
		origin = hub;
	return exchange::purchase(state, origin, commodity, operator_actor, buyer,
		quantity, unit_price, settlement, timestamp);
}

} // namespace economy::physical::factory_output
