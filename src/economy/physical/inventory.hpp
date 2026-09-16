#pragma once

#include "dcon_generated.hpp"

namespace sys { class state; }

namespace economy::physical::inventory {

dcon::physical_stock_id find(sys::state const&, dcon::site_id, dcon::commodity_id, dcon::economic_actor_id);
dcon::physical_stock_id ensure(sys::state&, dcon::site_id, dcon::commodity_id, dcon::economic_actor_id);
float quantity(sys::state const&, dcon::site_id, dcon::commodity_id, dcon::economic_actor_id);
float add(sys::state&, dcon::site_id, dcon::commodity_id, float, dcon::economic_actor_id);
float remove(sys::state&, dcon::site_id, dcon::commodity_id, float, dcon::economic_actor_id);
bool transfer(sys::state&, dcon::site_id, dcon::commodity_id, dcon::economic_actor_id seller,
	dcon::economic_actor_id buyer, float quantity);

} // namespace economy::physical::inventory
