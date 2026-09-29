#include "factory_output.hpp"

#include "actors/organizations/organizations.hpp"
#include "inventory.hpp"
#include "shipments.hpp"
#include "deposits.hpp"
#include "system_state.hpp"
#include "world/site.hpp"

#include <cmath>

namespace economy::physical::factory_output {

bool materialize_and_dispatch(sys::state& state, dcon::factory_id factory, float produced_amount) {
	if(!factory || !std::isfinite(produced_amount) || produced_amount <= 0.0f)
		return true;
	auto factory_type = state.world.factory_get_building_type(factory);
	auto commodity = factory_type ? state.world.factory_type_get_output(factory_type) : dcon::commodity_id{};
	if(!commodity) {
		assert(false && "canonical factory output recipe has no commodity");
		return false;
	}
	auto origin = world::site::site_for_factory(state, factory);
	auto operator_actor = actors::organizations::operator_actor_for_factory(state, factory);
	auto province = world::site::province_for_site(state, origin);
	auto local_state = province ? state.world.province_get_state_membership(province) : dcon::state_instance_id{};
	auto market = local_state ? state.world.state_instance_get_market_from_local_market(local_state) : dcon::market_id{};
	auto hub = market ? deposits::market_hub_for(state, market) : dcon::site_id{};
	if(!origin || !operator_actor || !market || !hub) {
		assert(false && "canonical factory output requires a site, firm, and market hub");
		return false;
	}
	if(inventory::add(state, origin, commodity, produced_amount, operator_actor) != produced_amount)
		return false;
	// If dispatch fails, the materialized output remains at the factory site.
	if(!shipments::dispatch(state, origin, hub, commodity, produced_amount, operator_actor))
		return false;
	return true;
}

} // namespace economy::physical::factory_output
