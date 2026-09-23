#include "economy/accounts/accounts.hpp"
#include "economy/physical/concrete_market.hpp"
#include "economy/physical/freight_market.hpp"
#include "economy/physical/inventory.hpp"
#include "economy/physical/shipments.hpp"
#include "system_state.hpp"
#include "world/spatial_runtime.hpp"

#include <memory>

namespace actor_market_tests {

struct fixture {
	std::unique_ptr<sys::state> state = std::make_unique<sys::state>();
	dcon::province_id origin_province = state->world.create_province();
	dcon::province_id destination_province = state->world.create_province();
	dcon::state_instance_id origin_state = state->world.create_state_instance();
	dcon::state_instance_id destination_state = state->world.create_state_instance();
	dcon::market_id origin_market = state->world.create_market();
	dcon::market_id destination_market = state->world.create_market();
	dcon::site_id origin = state->world.create_site();
	dcon::site_id destination = state->world.create_site();
	dcon::infrastructure_node_id origin_node = state->world.create_infrastructure_node();
	dcon::infrastructure_node_id destination_node = state->world.create_infrastructure_node();
	dcon::commodity_id settlement = state->world.create_commodity();
	dcon::commodity_id goods = state->world.create_commodity();
	dcon::economic_actor_id remote_seller = state->world.create_economic_actor();
	dcon::economic_actor_id local_seller = state->world.create_economic_actor();
	dcon::economic_actor_id buyer = state->world.create_economic_actor();
	dcon::economic_actor_id carrier_actor = state->world.create_economic_actor();
	dcon::monetary_account_id remote_account = {};
	dcon::monetary_account_id local_account = {};
	dcon::monetary_account_id buyer_account = {};
	dcon::monetary_account_id carrier_account = {};

	fixture() {
		state->world.province_set_state_membership(origin_province, origin_state);
		state->world.province_set_state_membership(destination_province, destination_state);
		state->world.state_instance_set_capital(origin_state, origin_province);
		state->world.state_instance_set_capital(destination_state, destination_province);
		state->world.market_set_zone_from_local_market(origin_market, origin_state);
		state->world.market_set_zone_from_local_market(destination_market, destination_state);
		state->world.force_create_site_location(origin, origin_province);
		state->world.force_create_site_location(destination, destination_province);
		state->world.force_create_infrastructure_node_location(origin_node, origin_province);
		state->world.force_create_infrastructure_node_location(destination_node, destination_province);
		auto edge = state->world.create_infrastructure_edge();
		state->world.infrastructure_edge_set_type(edge, 0);
		state->world.infrastructure_edge_set_distance(edge, 100.0f);
		state->world.force_create_infrastructure_edge_from(edge, origin_node);
		state->world.force_create_infrastructure_edge_to(edge, destination_node);
		world::spatial_runtime::bootstrap(*state);
		remote_account = economy::accounts::open_account(*state, remote_seller, settlement);
		local_account = economy::accounts::open_account(*state, local_seller, settlement);
		buyer_account = economy::accounts::open_account(*state, buyer, settlement);
		carrier_account = economy::accounts::open_account(*state, carrier_actor, settlement);
		economy::accounts::bootstrap_set_balance(*state, remote_account, 0.0f);
		economy::accounts::bootstrap_set_balance(*state, local_account, 0.0f);
		economy::accounts::bootstrap_set_balance(*state, buyer_account, 20.0f);
		economy::accounts::bootstrap_set_balance(*state, carrier_account, 0.0f);
	}
};

} // namespace actor_market_tests

TEST_CASE("actor market ranks real asks by landed spatial cost and preserves partial fills",
	"[economy][actor_market][spatial]") {
	actor_market_tests::fixture f;
	REQUIRE(economy::physical::inventory::add(*f.state, f.origin, f.goods, 1.0f,
		f.remote_seller) == Approx(1.0f));
	REQUIRE(economy::physical::inventory::add(*f.state, f.destination, f.goods, 1.0f,
		f.local_seller) == Approx(1.0f));
	auto remote_ask = economy::physical::concrete_market::post_ask(*f.state,
		f.remote_seller, f.origin, f.origin_market, f.goods, 1.0f, 1.0f, {});
	auto local_ask = economy::physical::concrete_market::post_ask(*f.state,
		f.local_seller, f.destination, f.destination_market, f.goods, 1.0f, 3.0f, {});
	auto bid = economy::physical::concrete_market::post_bid(*f.state, f.buyer,
		f.buyer_account, f.destination, f.destination_market, f.goods, 1.0f, 5.0f, {});
	REQUIRE(remote_ask);
	REQUIRE(local_ask);
	REQUIRE(bid);
	REQUIRE(economy::physical::concrete_market::landed_unit_cost(*f.state,
		f.origin, f.destination, f.goods, 1.0f) > 1.0f);
	auto fills = economy::physical::concrete_market::match_all(*f.state, f.goods, {});
	REQUIRE(fills.size() == 1);
	REQUIRE(f.state->world.concrete_trade_fill_get_concrete_market_ask_from_concrete_fill_ask(fills.front())
		== remote_ask);
	REQUIRE(f.state->world.concrete_market_ask_get_remaining_quantity(local_ask) == Approx(1.0f));
	REQUIRE(f.state->world.concrete_market_bid_get_remaining_quantity(bid) == Approx(0.0f));
	REQUIRE(economy::accounts::balance(*f.state, f.remote_account) == Approx(1.0f));
	REQUIRE(economy::accounts::balance(*f.state, f.local_account) == Approx(0.0f));
	// Payment/ownership are committed, but the physical route has not delivered
	// the buyer-owned cargo yet.
	REQUIRE(economy::physical::inventory::quantity(*f.state, f.origin, f.goods, f.buyer)
		== Approx(1.0f));
	REQUIRE(economy::physical::inventory::quantity(*f.state, f.destination, f.goods, f.buyer)
		== Approx(0.0f));
}

TEST_CASE("actor market creates a routed shipment and delivers buyer inventory",
	"[economy][actor_market][shipment]") {
	actor_market_tests::fixture f;
	REQUIRE(economy::physical::inventory::add(*f.state, f.origin, f.goods, 2.0f,
		f.remote_seller) == Approx(2.0f));
	REQUIRE(economy::physical::concrete_market::post_ask(*f.state,
		f.remote_seller, f.origin, f.origin_market, f.goods, 2.0f, 1.0f, {}));
	REQUIRE(economy::physical::concrete_market::post_bid(*f.state, f.buyer,
		f.buyer_account, f.destination, f.destination_market, f.goods, 2.0f, 5.0f, {}));
	auto carrier = economy::physical::freight_market::create_carrier(*f.state,
		f.carrier_actor, f.carrier_account, 100.0f,
		economy::physical::freight_market::mode_bit(0), f.origin_market);
	REQUIRE(carrier);
	REQUIRE(economy::physical::freight_market::create_offer(*f.state, carrier,
		f.origin_market, f.destination_market,
		economy::physical::freight_market::mode_bit(0), 100.0f, 1.0f, 0.0f));
	auto fills = economy::physical::concrete_market::match_all(*f.state, f.goods, {});
	REQUIRE(fills.size() == 1);
	auto shipment = f.state->world.concrete_trade_fill_get_shipment_from_concrete_fill_shipment(fills.front());
	REQUIRE(shipment);
	REQUIRE(economy::physical::inventory::quantity(*f.state, f.origin, f.goods, f.buyer)
		== Approx(0.0f));
	REQUIRE(economy::physical::inventory::quantity(*f.state, f.destination, f.goods, f.buyer)
		== Approx(0.0f));
	for(uint32_t day = 0; day < 8 && f.state->world.shipment_is_valid(shipment); ++day)
		economy::physical::shipments::advance(*f.state);
	REQUIRE_FALSE(f.state->world.shipment_is_valid(shipment));
	REQUIRE(economy::physical::inventory::quantity(*f.state, f.destination, f.goods, f.buyer)
		== Approx(2.0f));
	REQUIRE(economy::accounts::balance(*f.state, f.remote_account) == Approx(2.0f));
	REQUIRE(economy::accounts::balance(*f.state, f.carrier_account) == Approx(1.0f));
}
