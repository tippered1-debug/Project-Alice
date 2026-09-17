#include "economy/physical/concrete_market.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/physical/inventory.hpp"

namespace concrete_market_tests {
struct fixture {
	std::unique_ptr<sys::state> state = std::make_unique<sys::state>();
	dcon::site_id source = state->world.create_site();
	dcon::site_id destination = state->world.create_site();
	dcon::market_id market = state->world.create_market();
	dcon::commodity_id settlement = state->world.create_commodity();
	dcon::commodity_id goods = state->world.create_commodity();
	dcon::economic_actor_id seller = state->world.create_economic_actor();
	dcon::economic_actor_id buyer = state->world.create_economic_actor();
	dcon::monetary_account_id seller_account = economy::accounts::open_account(*state, seller, settlement);
	dcon::monetary_account_id buyer_account = economy::accounts::open_account(*state, buyer, settlement);
	fixture() {
		economy::accounts::bootstrap_set_balance(*state, seller_account, 0.0f);
		economy::accounts::bootstrap_set_balance(*state, buyer_account, 1000.0f);
	}
};
}

TEST_CASE("concrete market fills exact orders and records observed price", "[economy][physical][concrete_market]") {
	concrete_market_tests::fixture f;
	REQUIRE(economy::physical::inventory::add(*f.state, f.source, f.goods, 50.0f, f.seller) == Approx(50.0f));
	auto ask = economy::physical::concrete_market::post_ask(*f.state, f.seller, f.source, f.market, f.goods, 50.0f, 10.0f, {});
	auto bid = economy::physical::concrete_market::post_bid(*f.state, f.buyer, f.buyer_account, f.destination, f.market, f.goods, 40.0f, 12.0f, {});
	REQUIRE(ask);
	REQUIRE(bid);
	auto fills = economy::physical::concrete_market::match(*f.state, f.market, f.goods, {});
	REQUIRE(fills.size() == 1);
	REQUIRE(f.state->world.concrete_trade_fill_get_quantity(fills.front()) == Approx(40.0f));
	REQUIRE(f.state->world.concrete_trade_fill_get_execution_price(fills.front()) == Approx(10.0f));
	REQUIRE(f.state->world.concrete_trade_fill_get_concrete_market_bid_from_concrete_fill_bid(fills.front()) == bid);
	REQUIRE(f.state->world.concrete_trade_fill_get_concrete_market_ask_from_concrete_fill_ask(fills.front()) == ask);
	REQUIRE(f.state->world.concrete_market_ask_get_remaining_quantity(ask) == Approx(10.0f));
	REQUIRE(f.state->world.concrete_market_bid_get_status(bid) == uint8_t(economy::physical::concrete_market::order_status::filled));
	REQUIRE(f.state->world.concrete_trade_fill_get_transaction_from_concrete_fill_transaction(fills.front()));
	REQUIRE(f.state->world.concrete_trade_fill_get_shipment_from_concrete_fill_shipment(fills.front()));
	REQUIRE(economy::physical::concrete_market::observed_price(*f.state, f.market, f.goods, {}, 99.0f) == Approx(10.0f));
}

TEST_CASE("concrete market does not cross non-overlapping limits", "[economy][physical][concrete_market]") {
	concrete_market_tests::fixture f;
	economy::physical::inventory::add(*f.state, f.source, f.goods, 50.0f, f.seller);
	REQUIRE(economy::physical::concrete_market::post_ask(*f.state, f.seller, f.source, f.market, f.goods, 50.0f, 10.0f, {}));
	REQUIRE(economy::physical::concrete_market::post_bid(*f.state, f.buyer, f.buyer_account, f.destination, f.market, f.goods, 40.0f, 9.0f, {}));
	auto transactions = f.state->world.transaction_size();
	REQUIRE(economy::physical::concrete_market::match(*f.state, f.market, f.goods, {}).empty());
	REQUIRE(f.state->world.transaction_size() == transactions);
	REQUIRE(economy::physical::inventory::quantity(*f.state, f.source, f.goods, f.seller) == Approx(50.0f));
}

TEST_CASE("concrete market is actor-specific and deterministic", "[economy][physical][concrete_market]") {
	concrete_market_tests::fixture f;
	auto seller_b = f.state->world.create_economic_actor();
	auto seller_b_account = economy::accounts::open_account(*f.state, seller_b, f.settlement);
	economy::accounts::bootstrap_set_balance(*f.state, seller_b_account, 0.0f);
	economy::physical::inventory::add(*f.state, f.source, f.goods, 30.0f, f.seller);
	economy::physical::inventory::add(*f.state, f.source, f.goods, 30.0f, seller_b);
	auto ask_a = economy::physical::concrete_market::post_ask(*f.state, f.seller, f.source, f.market, f.goods, 30.0f, 8.0f, {});
	auto ask_b = economy::physical::concrete_market::post_ask(*f.state, seller_b, f.source, f.market, f.goods, 30.0f, 10.0f, {});
	REQUIRE(ask_a);
	REQUIRE(ask_b);
	REQUIRE(economy::physical::concrete_market::post_bid(*f.state, f.buyer, f.buyer_account, f.destination, f.market, f.goods, 50.0f, 12.0f, {}));
	auto fills = economy::physical::concrete_market::match(*f.state, f.market, f.goods, {});
	REQUIRE(fills.size() == 2);
	REQUIRE(f.state->world.concrete_trade_fill_get_concrete_market_ask_from_concrete_fill_ask(fills[0]) == ask_a);
	REQUIRE(f.state->world.concrete_trade_fill_get_concrete_market_ask_from_concrete_fill_ask(fills[1]) == ask_b);
	REQUIRE(f.state->world.concrete_trade_fill_get_quantity(fills[0]) == Approx(30.0f));
	REQUIRE(f.state->world.concrete_trade_fill_get_quantity(fills[1]) == Approx(20.0f));
}

TEST_CASE("concrete market reserves funds and inventory", "[economy][physical][concrete_market]") {
	concrete_market_tests::fixture f;
	REQUIRE_FALSE(economy::physical::concrete_market::post_bid(*f.state, f.buyer, f.buyer_account, f.destination, f.market, f.goods, 100.0f, 12.0f, {}));
	economy::physical::inventory::add(*f.state, f.source, f.goods, 50.0f, f.seller);
	REQUIRE(economy::physical::concrete_market::post_ask(*f.state, f.seller, f.source, f.market, f.goods, 40.0f, 10.0f, {}));
	REQUIRE_FALSE(economy::physical::concrete_market::post_ask(*f.state, f.seller, f.source, f.market, f.goods, 20.0f, 10.0f, {}));
}

TEST_CASE("concrete market observed price falls back without fills", "[economy][physical][concrete_market]") {
	concrete_market_tests::fixture f;
	f.state->world.market_set_price(f.market, f.goods, 2.0f);
	REQUIRE(economy::physical::concrete_market::observed_price(*f.state, f.market, f.goods, {}, 7.0f) == Approx(7.0f));
}

TEST_CASE("concrete market orders expire and release reservations", "[economy][physical][concrete_market]") {
	concrete_market_tests::fixture f;
	economy::physical::inventory::add(*f.state, f.source, f.goods, 20.0f, f.seller);
	auto ask = economy::physical::concrete_market::post_ask(*f.state, f.seller, f.source, f.market, f.goods, 20.0f, 10.0f, {});
	REQUIRE(ask);
	f.state->current_date = sys::date{1};
	economy::physical::concrete_market::expire(*f.state, f.state->current_date);
	REQUIRE(f.state->world.concrete_market_ask_get_status(ask) == uint8_t(economy::physical::concrete_market::order_status::canceled));
	REQUIRE(economy::physical::concrete_market::post_ask(*f.state, f.seller, f.source, f.market, f.goods, 20.0f, 10.0f, {}));
}

TEST_CASE("concrete market fills create physical shipment rather than synthetic goods", "[economy][physical][concrete_market]") {
	concrete_market_tests::fixture f;
	economy::physical::inventory::add(*f.state, f.source, f.goods, 5.0f, f.seller);
	REQUIRE(economy::physical::concrete_market::post_ask(*f.state, f.seller, f.source, f.market, f.goods, 5.0f, 3.0f, {}));
	REQUIRE(economy::physical::concrete_market::post_bid(*f.state, f.buyer, f.buyer_account, f.destination, f.market, f.goods, 5.0f, 3.0f, {}));
	REQUIRE(economy::physical::concrete_market::match(*f.state, f.market, f.goods, {}).size() == 1);
	REQUIRE(f.state->world.shipment_size() == 1);
	REQUIRE(economy::physical::inventory::quantity(*f.state, f.source, f.goods, f.seller) == Approx(0.0f));
}
