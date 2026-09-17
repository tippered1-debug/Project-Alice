#pragma once

#include "dcon_generated.hpp"
#include <vector>

namespace sys { class state; }

namespace economy::physical::concrete_market {

enum class order_status : uint8_t { active = 0, canceled = 1, filled = 2 };
enum class order_purpose : uint8_t { general = 0, factory_input = 1 };

dcon::concrete_market_bid_id post_bid(sys::state&, dcon::economic_actor_id buyer,
	dcon::monetary_account_id account, dcon::site_id destination, dcon::market_id,
	dcon::commodity_id, float quantity, float limit_price, order_purpose);
dcon::concrete_market_ask_id post_ask(sys::state&, dcon::economic_actor_id seller,
	dcon::site_id source, dcon::market_id, dcon::commodity_id, float quantity,
	float minimum_price, order_purpose);
std::vector<dcon::concrete_trade_fill_id> match(sys::state&, dcon::market_id,
	dcon::commodity_id, sys::date);
float observed_price(sys::state const&, dcon::market_id, dcon::commodity_id, sys::date,
	float fallback = 0.0f);
float canonical_reference_price(sys::state const&, dcon::market_id, dcon::commodity_id,
	sys::date, float fallback = 0.0f);
float reserved_bid_amount(sys::state const&, dcon::monetary_account_id);
float active_factory_bid_quantity(sys::state const&, dcon::factory_id,
	dcon::site_id, dcon::commodity_id);
void expire(sys::state&, sys::date);

} // namespace economy::physical::concrete_market
