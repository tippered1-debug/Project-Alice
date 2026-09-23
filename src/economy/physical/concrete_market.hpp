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
	float minimum_price, order_purpose, dcon::factory_id factory = {});
std::vector<dcon::concrete_trade_fill_id> match(sys::state&, dcon::market_id,
	dcon::commodity_id, sys::date);
// Canonical actor-market clearing across all reachable origin/destination
// markets. The market argument of match() remains the explicit compatibility
// scope for callers that still clear one legacy market at a time.
std::vector<dcon::concrete_trade_fill_id> match_all(sys::state&, dcon::commodity_id,
	sys::date);
dcon::market_id market_for_site(sys::state const&, dcon::site_id);
float landed_unit_cost(sys::state&, dcon::site_id origin, dcon::site_id destination,
	dcon::commodity_id, float goods_price);
float observed_price(sys::state const&, dcon::market_id, dcon::commodity_id, sys::date,
	float fallback = 0.0f);
float observed_sell_through(sys::state const&, dcon::market_id, dcon::commodity_id,
	sys::date, float fallback = -1.0f);
float canonical_reference_price(sys::state const&, dcon::market_id, dcon::commodity_id,
	sys::date, float fallback = 0.0f);
float concrete_reference_price(sys::state const&, dcon::market_id, dcon::commodity_id,
	sys::date, float fallback = 0.0f);
float legacy_compatibility_reference_price(sys::state const&, dcon::market_id,
	dcon::commodity_id, sys::date, float fallback = 0.0f);
float reserved_bid_amount(sys::state const&, dcon::monetary_account_id);
float active_factory_bid_quantity(sys::state const&, dcon::factory_id,
	dcon::site_id, dcon::commodity_id);
void expire(sys::state&, sys::date);

} // namespace economy::physical::concrete_market
