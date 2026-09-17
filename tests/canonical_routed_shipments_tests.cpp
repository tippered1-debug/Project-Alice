#include "catch2/catch.hpp"

#include "dcon_generated.hpp"
#include "system_state.hpp"
#include "economy/commodity_logistics.hpp"
#include "economy/physical/inventory.hpp"
#include "economy/physical/shipments.hpp"

#include <memory>

namespace {

struct routed_fixture {
	std::unique_ptr<sys::state> state = std::make_unique<sys::state>();
	dcon::market_id origin_market{};
	dcon::market_id destination_market{};
	dcon::site_id origin{};
	dcon::site_id destination{};
	dcon::commodity_id commodity{};
	dcon::trade_route_id route{};
};

dcon::site_id make_site(sys::state& state, dcon::province_id province) {
	auto site = state.world.create_site();
	state.world.force_create_site_location(site, province);
	return site;
}

routed_fixture make_routed_fixture(bool connect_markets = true, float capacity = 100.0f) {
	routed_fixture fixture{};
	auto state_a = fixture.state->world.create_state_instance();
	auto state_b = fixture.state->world.create_state_instance();
	fixture.origin_market = fixture.state->world.create_market();
	fixture.destination_market = fixture.state->world.create_market();
	auto province_a = fixture.state->world.create_province();
	auto province_b = fixture.state->world.create_province();
	fixture.state->world.province_set_mid_point_b(province_a, glm::vec3{ 0.0f, 0.0f, 0.0f });
	fixture.state->world.province_set_mid_point_b(province_b, glm::vec3{ 1.0f, 0.0f, 0.0f });
	fixture.state->world.market_set_zone_from_local_market(fixture.origin_market, state_a);
	fixture.state->world.market_set_zone_from_local_market(fixture.destination_market, state_b);
	fixture.state->world.state_instance_set_capital(state_a, province_a);
	fixture.state->world.state_instance_set_capital(state_b, province_b);
	fixture.state->world.market_set_max_throughput(fixture.origin_market, capacity);
	fixture.state->world.market_set_max_throughput(fixture.destination_market, capacity);
	auto origin_hub = make_site(*fixture.state, province_a);
	auto destination_hub = make_site(*fixture.state, province_b);
	fixture.state->world.force_create_market_hub_site(fixture.origin_market, origin_hub);
	fixture.state->world.force_create_market_hub_site(fixture.destination_market, destination_hub);
	fixture.origin = make_site(*fixture.state, province_a);
	fixture.destination = make_site(*fixture.state, province_b);
	fixture.commodity = fixture.state->world.create_commodity();
	if(connect_markets) {
		fixture.route = fixture.state->world.force_create_trade_route(
			fixture.origin_market, fixture.destination_market);
		fixture.state->world.trade_route_set_is_land_route(fixture.route, true);
		fixture.state->world.trade_route_set_land_distance(fixture.route, 150.0f);
		fixture.state->world.trade_route_set_distance(fixture.route, 150.0f);
		fixture.state->world.trade_route_resize_volume(fixture.state->world.commodity_size());
	}
	return fixture;
}

void seed_origin(routed_fixture& fixture, float quantity) {
	REQUIRE(economy::physical::inventory::add(
		*fixture.state, fixture.origin, fixture.commodity, quantity, {}) == Approx(quantity));
}

} // namespace

TEST_CASE("canonical local shipments persist and traverse explicit local legs",
	"[economy][physical][routed-shipment]") {
	auto fixture = make_routed_fixture(false);
	// Put both sites in the same market, retaining the market hub leg shape.
	auto origin_province = fixture.state->world.site_get_province_from_site_location(fixture.origin);
	auto destination_province = fixture.state->world.site_get_province_from_site_location(fixture.destination);
	fixture.state->world.province_set_state_membership(destination_province,
		fixture.state->world.province_get_state_membership(origin_province));
	fixture.state->world.market_set_zone_from_local_market(
		fixture.destination_market, fixture.state->world.province_get_state_membership(origin_province));
	seed_origin(fixture, 10.0f);
	auto shipment = economy::physical::shipments::dispatch(
		*fixture.state, fixture.origin, fixture.destination, fixture.commodity, 10.0f, {});
	REQUIRE(shipment);
	REQUIRE(fixture.state->world.shipment_get_route_leg_count(shipment) == 2);
	REQUIRE(fixture.state->world.shipment_get_lifecycle(shipment) == 0);
	economy::physical::shipments::advance(*fixture.state);
	REQUIRE(fixture.state->world.shipment_is_valid(shipment));
	REQUIRE(fixture.state->world.shipment_get_current_leg(shipment) == 1);
	economy::physical::shipments::advance(*fixture.state);
	REQUIRE_FALSE(fixture.state->world.shipment_is_valid(shipment));
	REQUIRE(economy::physical::inventory::quantity(
		*fixture.state, fixture.destination, fixture.commodity, {}) > 0.0f);
}

TEST_CASE("canonical multi-market shipments cannot skip a bottleneck leg",
	"[economy][physical][routed-shipment]") {
	auto fixture = make_routed_fixture(true);
	seed_origin(fixture, 10.0f);
	auto shipment = economy::physical::shipments::dispatch(
		*fixture.state, fixture.origin, fixture.destination, fixture.commodity, 10.0f, {});
	REQUIRE(shipment);
	REQUIRE(fixture.state->world.shipment_get_route_leg_count(shipment) == 3);
	economy::physical::shipments::advance(*fixture.state);
	REQUIRE(fixture.state->world.shipment_is_valid(shipment));
	REQUIRE(fixture.state->world.shipment_get_current_leg(shipment) == 1);
	REQUIRE(economy::physical::inventory::quantity(
		*fixture.state, fixture.destination, fixture.commodity, {}) == Approx(0.0f));
	economy::physical::shipments::advance(*fixture.state);
	REQUIRE(fixture.state->world.shipment_is_valid(shipment));
	REQUIRE(fixture.state->world.shipment_get_current_leg(shipment) == 2);
	REQUIRE(economy::physical::shipments::dispatch(
		*fixture.state, fixture.origin, fixture.destination, fixture.commodity, 1.0f, {}) == dcon::shipment_id{});
	economy::physical::shipments::advance(*fixture.state);
	REQUIRE_FALSE(fixture.state->world.shipment_is_valid(shipment));
}

TEST_CASE("concrete shipment capacity allocation is deterministic and ignores route volume",
	"[economy][physical][routed-shipment]") {
	auto fixture = make_routed_fixture(true, 100.0f);
	fixture.state->world.trade_route_set_volume(fixture.route, fixture.commodity, 0.0f);
	seed_origin(fixture, 130.0f);
	auto first = economy::physical::shipments::dispatch(
		*fixture.state, fixture.origin, fixture.destination, fixture.commodity, 70.0f, {});
	auto second = economy::physical::shipments::dispatch(
		*fixture.state, fixture.origin, fixture.destination, fixture.commodity, 60.0f, {});
	REQUIRE(first);
	REQUIRE(second);
	economy::physical::shipments::advance(*fixture.state);
	REQUIRE(fixture.state->world.shipment_get_current_leg(first) == 1);
	REQUIRE(fixture.state->world.shipment_get_current_leg(second) == 0);
	auto second_legs = [&] {
		dcon::shipment_route_leg_id result{};
		fixture.state->world.shipment_for_each_shipment_route(second, [&](dcon::shipment_route_id relation) {
			if(fixture.state->world.shipment_route_get_shipment_route_leg(relation)) {
				auto leg = fixture.state->world.shipment_route_get_shipment_route_leg(relation);
				if(fixture.state->world.shipment_route_leg_get_sequence(leg) == 0) result = leg;
			}
		});
		return result;
	}();
	REQUIRE(second_legs);
	REQUIRE(fixture.state->world.shipment_route_leg_get_remaining_transport_work(second_legs)
		== Approx(30.0f));
}

TEST_CASE("increasing physical local capacity admits more concrete shipment work",
	"[economy][physical][routed-shipment]") {
	auto low_capacity = make_routed_fixture(true, 50.0f);
	seed_origin(low_capacity, 60.0f);
	auto low = economy::physical::shipments::dispatch(
		*low_capacity.state, low_capacity.origin, low_capacity.destination,
		low_capacity.commodity, 60.0f, {});
	REQUIRE(low);
	economy::physical::shipments::advance(*low_capacity.state);
	REQUIRE(low_capacity.state->world.shipment_get_current_leg(low) == 0);

	auto high_capacity = make_routed_fixture(true, 100.0f);
	seed_origin(high_capacity, 60.0f);
	auto high = economy::physical::shipments::dispatch(
		*high_capacity.state, high_capacity.origin, high_capacity.destination,
		high_capacity.commodity, 60.0f, {});
	REQUIRE(high);
	economy::physical::shipments::advance(*high_capacity.state);
	REQUIRE(high_capacity.state->world.shipment_get_current_leg(high) == 1);
}

TEST_CASE("disconnected canonical shipments leave source inventory untouched",
	"[economy][physical][routed-shipment]") {
	auto fixture = make_routed_fixture(false);
	seed_origin(fixture, 10.0f);
	REQUIRE(economy::physical::shipments::dispatch(
		*fixture.state, fixture.origin, fixture.destination, fixture.commodity, 10.0f, {}) == dcon::shipment_id{});
	REQUIRE(economy::physical::inventory::quantity(
		*fixture.state, fixture.origin, fixture.commodity, {}) == Approx(10.0f));
	REQUIRE(fixture.state->world.shipment_size() == 0);
}

TEST_CASE("canonical cargo demand uses commodity logistics weight",
	"[economy][physical][routed-shipment]") {
	economy::logistics::commodity_profile normal{};
	auto bulk = normal;
	bulk.cargo_weight = 1.5f;
	REQUIRE(economy::logistics::cargo_units(normal, 10.0f) == Approx(10.0f));
	REQUIRE(economy::logistics::cargo_units(bulk, 10.0f) == Approx(15.0f));
}
