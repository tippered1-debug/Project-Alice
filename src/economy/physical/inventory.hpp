#pragma once

#include "dcon_generated.hpp"

namespace sys { class state; }

namespace economy::physical::inventory {

dcon::physical_stock_id find(sys::state const&, dcon::site_id, dcon::commodity_id);
dcon::physical_stock_id ensure(sys::state&, dcon::site_id, dcon::commodity_id);
float quantity(sys::state const&, dcon::site_id, dcon::commodity_id);
float add(sys::state&, dcon::site_id, dcon::commodity_id, float);
float remove(sys::state&, dcon::site_id, dcon::commodity_id, float);

} // namespace economy::physical::inventory
