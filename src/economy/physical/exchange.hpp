#pragma once

#include "dcon_generated.hpp"
#include <vector>

namespace sys { class state; }

namespace economy::physical::exchange {

std::vector<dcon::physical_stock_id> seller_stocks(sys::state const&, dcon::site_id,
	dcon::commodity_id, dcon::economic_actor_id buyer);
dcon::transaction_id purchase(sys::state&, dcon::site_id, dcon::commodity_id,
	dcon::economic_actor_id seller, dcon::economic_actor_id buyer, float quantity,
	float unit_price, dcon::commodity_id settlement, sys::date timestamp);
dcon::transaction_id purchase_with_account(sys::state&, dcon::site_id, dcon::commodity_id,
	dcon::economic_actor_id seller, dcon::economic_actor_id buyer,
	dcon::monetary_account_id buyer_account, float quantity, float unit_price,
	sys::date timestamp);
dcon::commodity_id settlement_for_purchase(sys::state const&, dcon::economic_actor_id buyer);

} // namespace economy::physical::exchange
