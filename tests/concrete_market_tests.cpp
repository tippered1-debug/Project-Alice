#include "economy/physical/concrete_market.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/causal_order.hpp"
#include "economy/physical/inventory.hpp"
#include "economy/physical/shipments.hpp"
#include "economy/exact_person_economy.hpp"
#include "economy/physical/exact_person_goods.hpp"

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
	REQUIRE(f.state->world.concrete_market_ask_get_reserved_quantity(ask) == Approx(10.0f));
	REQUIRE(f.state->world.concrete_market_bid_get_status(bid) == uint8_t(economy::physical::concrete_market::order_status::filled));
	REQUIRE(f.state->world.concrete_market_bid_get_reserved_amount(bid) == Approx(0.0f));
	REQUIRE(f.state->world.concrete_trade_fill_get_transaction_from_concrete_fill_transaction(fills.front()));
	REQUIRE_FALSE(f.state->world.concrete_trade_fill_get_shipment_from_concrete_fill_shipment(fills.front()));
	auto request = f.state->world.concrete_trade_fill_get_freight_request_from_concrete_fill_freight_request(fills.front());
	REQUIRE(request);
	REQUIRE(f.state->world.freight_request_get_status(request) == 0);
	REQUIRE(economy::accounts::balance(*f.state, f.buyer_account) == Approx(600.0f));
	REQUIRE(economy::accounts::balance(*f.state, f.seller_account) == Approx(400.0f));
	REQUIRE(economy::physical::inventory::quantity(*f.state, f.source, f.goods, f.seller) == Approx(10.0f));
	REQUIRE(economy::physical::inventory::quantity(*f.state, f.source, f.goods, f.buyer) == Approx(40.0f));
	REQUIRE(economy::physical::inventory::quantity(*f.state, f.destination, f.goods, f.buyer) == Approx(0.0f));
	auto transaction = f.state->world.concrete_trade_fill_get_transaction_from_concrete_fill_transaction(fills.front());
	REQUIRE(f.state->world.transaction_get_monetary_account_from_transaction_source_account(transaction) == f.buyer_account);
	REQUIRE(f.state->world.transaction_get_monetary_account_from_transaction_destination_account(transaction) == f.seller_account);
	REQUIRE(economy::physical::concrete_market::observed_price(*f.state, f.market, f.goods, {}, 99.0f) == Approx(10.0f));
}

TEST_CASE("concrete market orders both representations by causal sequence", "[economy][physical][concrete_market][ordering]") {
	concrete_market_tests::fixture f;
	persons::exact_population::cell_descriptor descriptor;
	descriptor.source_population_cell = 901;
	descriptor.literal_count = 1;
	descriptor.bootstrap_base_day = 0;
	REQUIRE(persons::exact_population::register_synthetic_population_cell(*f.state, descriptor).result
		== persons::exact_population::status::created);
	economy::physical::inventory::add(*f.state, f.source, f.goods, 1.0f, f.seller);
	REQUIRE(economy::physical::concrete_market::post_ask(*f.state, f.seller, f.source, f.market,
		f.goods, 1.0f, 10.0f, {}));
	auto exact_key = persons::exact_population::person_key{901, 0};
	auto exact_account = economy::exact_person_economy::open_account(*f.state, exact_key, f.settlement);
	REQUIRE(economy::exact_person_economy::set_balance(*f.state, exact_account, 10.0f));
	REQUIRE(economy::physical::exact_person_goods::post_bid(*f.state, exact_key, exact_account,
		f.destination, f.market, f.goods, 1.0f, 10.0f));
	auto legacy_bid = economy::physical::concrete_market::post_bid(*f.state, f.buyer,
		f.buyer_account, f.destination, f.market, f.goods, 1.0f, 10.0f, {});
	REQUIRE(legacy_bid);
	(void)economy::physical::concrete_market::match(*f.state, f.market, f.goods, f.state->current_date);
	REQUIRE(economy::physical::exact_person_goods::fill_count(*f.state) == 1);
	REQUIRE(f.state->world.concrete_market_bid_get_status(legacy_bid)
		== uint8_t(economy::physical::concrete_market::order_status::active));
}

TEST_CASE("causal ordering is monotonic and date-first", "[economy][ordering][determinism]") {
	auto state = std::make_unique<sys::state>();
	state->current_date = sys::date{10};
	auto first = economy::causal_order::allocate(*state, economy::causal_order::event_kind::goods_bid);
	auto second = economy::causal_order::allocate(*state, economy::causal_order::event_kind::job_application);
	REQUIRE(first > 0);
	REQUIRE(second > first);
	REQUIRE(economy::causal_order::before({sys::date{9}, second}, {sys::date{10}, first}));
	REQUIRE_FALSE(economy::causal_order::before({sys::date{10}, second}, {sys::date{10}, first}));
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

TEST_CASE("concrete bid account must belong to its buyer", "[economy][physical][concrete_market]") {
	concrete_market_tests::fixture f;
	REQUIRE_FALSE(economy::physical::concrete_market::post_bid(*f.state, f.buyer, f.seller_account,
		f.destination, f.market, f.goods, 1.0f, 1.0f, {}));
}

TEST_CASE("concrete settlement debits the reserved account exactly", "[economy][physical][concrete_market]") {
	concrete_market_tests::fixture f;
	auto second = economy::accounts::open_account(*f.state, f.buyer, f.settlement);
	economy::accounts::bootstrap_set_balance(*f.state, second, 100.0f);
	economy::physical::inventory::add(*f.state, f.source, f.goods, 5.0f, f.seller);
	REQUIRE(economy::physical::concrete_market::post_ask(*f.state, f.seller, f.source, f.market, f.goods, 5.0f, 10.0f, {}));
	auto bid = economy::physical::concrete_market::post_bid(*f.state, f.buyer, second, f.destination,
		f.market, f.goods, 5.0f, 10.0f, {});
	REQUIRE(bid);
	REQUIRE(economy::physical::concrete_market::match(*f.state, f.market, f.goods, {}).size() == 1);
	REQUIRE(economy::accounts::balance(*f.state, second) == Approx(50.0f));
	REQUIRE(economy::accounts::balance(*f.state, f.buyer_account) == Approx(1000.0f));
	REQUIRE(economy::accounts::balance(*f.state, f.seller_account) == Approx(50.0f));
	auto fill = dcon::concrete_trade_fill_id{dcon::concrete_trade_fill_id::value_base_t(0)};
	auto transaction = f.state->world.concrete_trade_fill_get_transaction_from_concrete_fill_transaction(fill);
	REQUIRE(transaction);
	REQUIRE(f.state->world.transaction_get_monetary_account_from_transaction_source_account(transaction) == second);
}

TEST_CASE("concrete market observed price falls back without fills", "[economy][physical][concrete_market]") {
	concrete_market_tests::fixture f;
	f.state->world.market_set_price(f.market, f.goods, 2.0f);
	REQUIRE(economy::physical::concrete_market::observed_price(*f.state, f.market, f.goods, {}, 7.0f) == Approx(7.0f));
}

TEST_CASE("concrete reference price prefers prior concrete VWAP", "[economy][physical][concrete_market]") {
	concrete_market_tests::fixture f;
	f.state->world.commodity_set_cost(f.goods, 2.0f);
	f.state->world.market_set_price(f.market, f.goods, 2.0f);
	REQUIRE(economy::physical::concrete_market::canonical_reference_price(*f.state, f.market, f.goods, sys::date{1}) == Approx(2.0f));
	economy::physical::inventory::add(*f.state, f.source, f.goods, 5.0f, f.seller);
	REQUIRE(economy::physical::concrete_market::post_ask(*f.state, f.seller, f.source, f.market, f.goods, 5.0f, 10.0f, {}));
	REQUIRE(economy::physical::concrete_market::post_bid(*f.state, f.buyer, f.buyer_account, f.destination, f.market, f.goods, 5.0f, 10.0f, {}));
	REQUIRE(economy::physical::concrete_market::match(*f.state, f.market, f.goods, {}).size() == 1);
	f.state->world.market_set_price(f.market, f.goods, 999.0f);
	REQUIRE(economy::physical::concrete_market::canonical_reference_price(*f.state, f.market, f.goods, sys::date{1}) == Approx(10.0f));
}

TEST_CASE("canonical reference price ignores mutable legacy market price", "[economy][physical][concrete_market]") {
	concrete_market_tests::fixture f;
	f.state->world.commodity_set_cost(f.goods, 7.0f);
	f.state->world.market_set_price(f.market, f.goods, 1.0f);
	auto low = economy::physical::concrete_market::canonical_reference_price(*f.state, f.market, f.goods, {});
	f.state->world.market_set_price(f.market, f.goods, 100000.0f);
	auto high = economy::physical::concrete_market::canonical_reference_price(*f.state, f.market, f.goods, {});
	REQUIRE(low == Approx(7.0f));
	REQUIRE(high == Approx(7.0f));
	REQUIRE(economy::physical::concrete_market::legacy_compatibility_reference_price(
		*f.state, f.market, f.goods, {}) == Approx(100000.0f));
}

TEST_CASE("concrete market orders expire and release reservations", "[economy][physical][concrete_market]") {
	concrete_market_tests::fixture f;
	economy::physical::inventory::add(*f.state, f.source, f.goods, 20.0f, f.seller);
	auto ask = economy::physical::concrete_market::post_ask(*f.state, f.seller, f.source, f.market, f.goods, 20.0f, 10.0f, {});
	REQUIRE(ask);
	f.state->current_date = sys::date{1};
	economy::physical::concrete_market::expire(*f.state, f.state->current_date);
	REQUIRE(f.state->world.concrete_market_ask_get_status(ask) == uint8_t(economy::physical::concrete_market::order_status::canceled));
	REQUIRE(f.state->world.concrete_market_ask_get_reserved_quantity(ask) == Approx(0.0f));
	REQUIRE(economy::physical::concrete_market::post_ask(*f.state, f.seller, f.source, f.market, f.goods, 20.0f, 10.0f, {}));
}

TEST_CASE("concrete market fills create pending freight rather than synthetic delivery", "[economy][physical][concrete_market]") {
	concrete_market_tests::fixture f;
	economy::physical::inventory::add(*f.state, f.source, f.goods, 5.0f, f.seller);
	REQUIRE(economy::physical::concrete_market::post_ask(*f.state, f.seller, f.source, f.market, f.goods, 5.0f, 3.0f, {}));
	REQUIRE(economy::physical::concrete_market::post_bid(*f.state, f.buyer, f.buyer_account, f.destination, f.market, f.goods, 5.0f, 3.0f, {}));
	REQUIRE(economy::physical::concrete_market::match(*f.state, f.market, f.goods, {}).size() == 1);
	REQUIRE(f.state->world.shipment_size() == 0);
	REQUIRE(economy::physical::inventory::quantity(*f.state, f.source, f.goods, f.seller) == Approx(0.0f));
	REQUIRE(economy::physical::inventory::quantity(*f.state, f.source, f.goods, f.buyer) == Approx(5.0f));
}

TEST_CASE("concrete market records goods ownership even when delivery is disconnected",
	"[economy][physical][concrete_market]") {
	concrete_market_tests::fixture f;
	// Give the sites explicit, different state/market memberships and hubs, but
	// provide no trade route between those markets.
	auto source_state = f.state->world.create_state_instance();
	auto destination_state = f.state->world.create_state_instance();
	auto destination_market = f.state->world.create_market();
	auto source_province = f.state->world.create_province();
	auto destination_province = f.state->world.create_province();
	f.state->world.force_create_site_location(f.source, source_province);
	f.state->world.force_create_site_location(f.destination, destination_province);
	f.state->world.province_set_state_membership(source_province, source_state);
	f.state->world.province_set_state_membership(destination_province, destination_state);
	f.state->world.market_set_zone_from_local_market(f.market, source_state);
	f.state->world.market_set_zone_from_local_market(destination_market, destination_state);
	auto source_hub = f.state->world.create_site();
	auto destination_hub = f.state->world.create_site();
	f.state->world.force_create_site_location(source_hub, source_province);
	f.state->world.force_create_site_location(destination_hub, destination_province);
	f.state->world.force_create_market_hub_site(f.market, source_hub);
	f.state->world.force_create_market_hub_site(destination_market, destination_hub);

	REQUIRE(economy::physical::inventory::add(*f.state, f.source, f.goods, 20.0f, f.seller) == Approx(20.0f));
	auto ask = economy::physical::concrete_market::post_ask(
		*f.state, f.seller, f.source, f.market, f.goods, 20.0f, 10.0f, {});
	auto bid = economy::physical::concrete_market::post_bid(
		*f.state, f.buyer, f.buyer_account, f.destination, f.market, f.goods, 20.0f, 12.0f, {});
	REQUIRE(ask);
	REQUIRE(bid);
	auto transaction_count = f.state->world.transaction_size();
	auto seller_balance = economy::accounts::balance(*f.state, f.seller_account);
	auto buyer_balance = economy::accounts::balance(*f.state, f.buyer_account);

	auto fills = economy::physical::concrete_market::match(*f.state, f.market, f.goods, {});
	REQUIRE(fills.size() == 1);
	REQUIRE(f.state->world.transaction_size() == transaction_count + 1);
	REQUIRE(f.state->world.shipment_size() == 0);
	REQUIRE(economy::accounts::balance(*f.state, f.seller_account) == Approx(seller_balance + 200.0f));
	REQUIRE(economy::accounts::balance(*f.state, f.buyer_account) == Approx(buyer_balance - 200.0f));
	REQUIRE(economy::physical::inventory::quantity(*f.state, f.source, f.goods, f.seller) == Approx(0.0f));
	REQUIRE(economy::physical::inventory::quantity(*f.state, f.source, f.goods, f.buyer) == Approx(20.0f));
	REQUIRE(f.state->world.concrete_market_ask_get_remaining_quantity(ask) == Approx(0.0f));
	REQUIRE(f.state->world.concrete_market_ask_get_reserved_quantity(ask) == Approx(0.0f));
	REQUIRE(f.state->world.concrete_market_bid_get_remaining_quantity(bid) == Approx(0.0f));
	REQUIRE(f.state->world.concrete_market_bid_get_reserved_amount(bid) == Approx(0.0f));
}
