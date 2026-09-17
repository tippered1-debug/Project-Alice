#include "catch2/catch.hpp"

#include "dcon_generated.hpp"
#include "system_state.hpp"
#include "economy/commodity_logistics.hpp"
#include "economy/physical/inventory.hpp"
#include "economy/physical/shipments.hpp"
#include "economy/world_trade_capacity.hpp"

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

routed_fixture make_routed_fixture(bool connect_markets = true, float capacity = 100.0f,
	uint8_t railroad_level = 0) {
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
	// Route planning is based on explicit province topology, never a capital
	// inference made by a test fixture.
	fixture.state->world.province_set_state_membership(province_a, state_a);
	fixture.state->world.province_set_state_membership(province_b, state_b);
	fixture.state->world.province_set_building_level(
		province_a, uint8_t(economy::province_building_type::railroad), railroad_level);
	fixture.state->world.province_set_building_level(
		province_b, uint8_t(economy::province_building_type::railroad), railroad_level);
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

dcon::shipment_route_leg_id leg_for_sequence(routed_fixture const& fixture,
	dcon::shipment_id shipment, uint8_t sequence) {
	dcon::shipment_route_leg_id result{};
	fixture.state->world.shipment_for_each_shipment_route(shipment, [&](dcon::shipment_route_id relation) {
		auto leg = fixture.state->world.shipment_route_get_shipment_route_leg(relation);
		if(leg && fixture.state->world.shipment_route_leg_get_sequence(leg) == sequence) result = leg;
	});
	return result;
}

} // namespace

TEST_CASE("canonical local shipments persist and traverse explicit local legs",
	"[economy][physical][routed-shipment]") {
	auto fixture = make_routed_fixture(false);
	// Put both sites in the same market, retaining the market hub leg shape.
	auto destination_province = fixture.state->world.site_get_province_from_site_location(fixture.destination);
	auto origin_province = fixture.state->world.site_get_province_from_site_location(fixture.origin);
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
	const auto canonical_before = economy::world_trade::canonical_capacity(*fixture.state,
		fixture.origin_market, economy::world_trade::transport_mode::land);
	fixture.state->world.market_set_max_throughput(fixture.origin_market, 0.001f);
	fixture.state->world.trade_route_set_volume(fixture.route, fixture.commodity, 1000000.0f);
	REQUIRE(economy::world_trade::canonical_capacity(*fixture.state,
		fixture.origin_market, economy::world_trade::transport_mode::land) == Approx(canonical_before));
}

TEST_CASE("increasing physical local capacity admits more concrete shipment work",
	"[economy][physical][routed-shipment]") {
	auto low_capacity = make_routed_fixture(true, 50.0f, 0);
	seed_origin(low_capacity, 120.0f);
	auto low = economy::physical::shipments::dispatch(
		*low_capacity.state, low_capacity.origin, low_capacity.destination,
		low_capacity.commodity, 120.0f, {});
	REQUIRE(low);
	economy::physical::shipments::advance(*low_capacity.state);
	REQUIRE(low_capacity.state->world.shipment_get_current_leg(low) == 0);

	auto high_capacity = make_routed_fixture(true, 100.0f, 1);
	seed_origin(high_capacity, 120.0f);
	auto high = economy::physical::shipments::dispatch(
		*high_capacity.state, high_capacity.origin, high_capacity.destination,
		high_capacity.commodity, 120.0f, {});
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
	auto normal_fixture = make_routed_fixture(true);
	seed_origin(normal_fixture, 10.0f);
	auto normal = economy::physical::shipments::dispatch(*normal_fixture.state,
		normal_fixture.origin, normal_fixture.destination, normal_fixture.commodity, 10.0f, {});
	REQUIRE(normal);
	auto normal_leg = leg_for_sequence(normal_fixture, normal, 0);

	auto bulk_fixture = make_routed_fixture(true);
	bulk_fixture.state->world.commodity_set_rgo_amount(bulk_fixture.commodity, 1.0f);
	seed_origin(bulk_fixture, 10.0f);
	auto bulk = economy::physical::shipments::dispatch(*bulk_fixture.state,
		bulk_fixture.origin, bulk_fixture.destination, bulk_fixture.commodity, 10.0f, {});
	REQUIRE(bulk);
	auto bulk_leg = leg_for_sequence(bulk_fixture, bulk, 0);
	REQUIRE(normal_leg);
	REQUIRE(bulk_leg);
	REQUIRE(normal_fixture.state->world.shipment_route_leg_get_remaining_transport_work(normal_leg)
		== Approx(10.0f));
	REQUIRE(bulk_fixture.state->world.shipment_route_leg_get_remaining_transport_work(bulk_leg)
		== Approx(13.5f));
}

TEST_CASE("shorter zero-capacity sea mode cannot beat feasible land mode",
	"[economy][physical][routed-shipment]") {
	auto fixture = make_routed_fixture(true);
	fixture.state->world.trade_route_set_is_sea_route(fixture.route, true);
	fixture.state->world.trade_route_set_sea_distance(fixture.route, 1.0f);
	fixture.state->world.trade_route_set_is_land_route(fixture.route, true);
	fixture.state->world.trade_route_set_land_distance(fixture.route, 150.0f);
	seed_origin(fixture, 10.0f);
	auto shipment = economy::physical::shipments::dispatch(*fixture.state,
		fixture.origin, fixture.destination, fixture.commodity, 10.0f, {});
	REQUIRE(shipment);
	auto intermarket_leg = leg_for_sequence(fixture, shipment, 1);
	REQUIRE(intermarket_leg);
	REQUIRE(fixture.state->world.shipment_route_leg_get_mode(intermarket_leg)
		== uint8_t(economy::world_trade::transport_mode::land));
	REQUIRE(economy::world_trade::canonical_capacity(*fixture.state,
		fixture.origin_market, economy::world_trade::transport_mode::sea) == Approx(0.0f));
}

TEST_CASE("routed shipment conserves non-spoiled quantity across delivery",
	"[economy][physical][routed-shipment]") {
	auto fixture = make_routed_fixture(false);
	auto origin_province = fixture.state->world.site_get_province_from_site_location(fixture.origin);
	auto destination_province = fixture.state->world.site_get_province_from_site_location(fixture.destination);
	fixture.state->world.province_set_state_membership(destination_province,
		fixture.state->world.province_get_state_membership(origin_province));
	seed_origin(fixture, 10.0f);
	auto shipment = economy::physical::shipments::dispatch(*fixture.state,
		fixture.origin, fixture.destination, fixture.commodity, 10.0f, {});
	REQUIRE(shipment);
	const auto profile = economy::logistics::profile_for(*fixture.state, fixture.commodity);
	economy::physical::shipments::advance(*fixture.state);
	REQUIRE(fixture.state->world.shipment_is_valid(shipment));
	const auto after_one_day = fixture.state->world.shipment_get_remaining_quantity(shipment);
	REQUIRE(after_one_day == Approx(10.0f * (1.0f - profile.daily_spoilage)));
	economy::physical::shipments::advance(*fixture.state);
	REQUIRE_FALSE(fixture.state->world.shipment_is_valid(shipment));
	REQUIRE(economy::physical::inventory::quantity(*fixture.state,
		fixture.destination, fixture.commodity, {})
		== Approx(10.0f * (1.0f - profile.daily_spoilage) * (1.0f - profile.daily_spoilage)));
}

TEST_CASE("queued work spoils without refunding already consumed capacity",
	"[economy][physical][routed-shipment]") {
	auto fixture = make_routed_fixture(true);
	seed_origin(fixture, 250.0f);
	auto shipment = economy::physical::shipments::dispatch(*fixture.state,
		fixture.origin, fixture.destination, fixture.commodity, 250.0f, {});
	REQUIRE(shipment);
	auto first_leg = leg_for_sequence(fixture, shipment, 0);
	REQUIRE(first_leg);
	auto future_leg = leg_for_sequence(fixture, shipment, 1);
	REQUIRE(future_leg);
	REQUIRE(fixture.state->world.shipment_route_leg_get_remaining_transport_work(future_leg)
		== Approx(0.0f));
	const auto initial_work = fixture.state->world.shipment_route_leg_get_remaining_transport_work(first_leg);
	economy::physical::shipments::advance(*fixture.state);
	REQUIRE(fixture.state->world.shipment_is_valid(shipment));
	REQUIRE(fixture.state->world.shipment_get_current_leg(shipment) == 0);
	const auto after_one_day = fixture.state->world.shipment_route_leg_get_remaining_transport_work(first_leg);
	REQUIRE(after_one_day < initial_work);
	economy::physical::shipments::advance(*fixture.state);
	REQUIRE(fixture.state->world.shipment_is_valid(shipment));
	REQUIRE(fixture.state->world.shipment_get_current_leg(shipment) == 0);
	economy::physical::shipments::advance(*fixture.state);
	REQUIRE(fixture.state->world.shipment_is_valid(shipment));
	REQUIRE(fixture.state->world.shipment_get_current_leg(shipment) == 1);
	auto second_leg = leg_for_sequence(fixture, shipment, 1);
	REQUIRE(second_leg);
	REQUIRE(fixture.state->world.shipment_route_leg_get_remaining_transport_work(second_leg)
		== Approx(economy::logistics::cargo_units(
			economy::logistics::profile_for(*fixture.state, fixture.commodity),
			fixture.state->world.shipment_get_remaining_quantity(shipment))));
}
