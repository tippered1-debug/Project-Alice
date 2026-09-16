#pragma once

#include "dcon_generated.hpp"
#include <vector>

namespace sys { class state; }

namespace economy::physical::exchange {

std::vector<dcon::physical_stock_id> seller_stocks(sys::state const&, dcon::site_id,
	dcon::commodity_id, dcon::economic_actor_id buyer);
dcon::transaction_id purchase(sys::state&, dcon::site_id, dcon::commodity_id,
	dcon::economic_actor_id seller, dcon::economic_actor_id buyer, float quantity,
	float unit_price, sys::date timestamp);

} // namespace economy::physical::exchange
