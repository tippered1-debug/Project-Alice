#include "catch.hpp"
#include "dcon_generated.hpp"
#include "container_types.hpp"
#include "system_state.hpp"
#include "serialization.hpp"
#include "economy/physical/deposits.hpp"
#include "economy/physical/inventory.hpp"
#include "economy/physical/shipments.hpp"
#include "economy/physical/legacy_market_bridge.hpp"
#include <limits>

TEST_CASE("dl_setting", "[dcon]") {
    std::unique_ptr<sys::state> state = std::make_unique<sys::state>();

    constexpr uint32_t num_test = 2000;
    dcon::army_id aid[num_test];
    for(uint32_t i = 0; i < num_test; ++i) {
        aid[i] = state->world.create_army();
        REQUIRE(aid[i].index() == int32_t(i));
    }
    dcon::navy_id nid[num_test];
    for(uint32_t i = 0; i < num_test; ++i) {
        nid[i] = state->world.create_navy();
        REQUIRE(nid[i].index() == int32_t(i));
    }
    dcon::province_id prov[num_test];
    for(uint32_t i = 0; i < num_test; ++i) {
        prov[i] = state->world.create_province();
        REQUIRE(prov[i].index() == int32_t(i));
    }

    // Set location of army enough times to make it crash due to a bug on DataContainer
    for(uint32_t n = 0; n < 100; ++n) {
        for(uint32_t i = 0; i < num_test; ++i) {
            uint32_t idx = (i + n) % num_test;
            state->world.army_set_location_from_army_location(aid[idx], prov[idx]);
            state->world.navy_set_location_from_navy_location(nid[idx], prov[idx]);
        }
    }
}

TEST_CASE("reb_setting", "[dcon]") {
    std::unique_ptr<sys::state> state = std::make_unique<sys::state>();

    constexpr uint32_t num_test = 100;
    dcon::nation_id nid[num_test];
    for(uint32_t i = 0; i < num_test; ++i) {
        nid[i] = state->world.create_nation();
        REQUIRE(nid[i].index() == int32_t(i));
    }
    dcon::rebel_faction_id rfid[num_test];
    for(uint32_t i = 0; i < num_test; ++i) {
        rfid[i] = state->world.create_rebel_faction();
        REQUIRE(rfid[i].index() == int32_t(i));
    }
    dcon::province_id prov[num_test];
    for(uint32_t i = 0; i < num_test; ++i) {
        prov[i] = state->world.create_province();
        REQUIRE(prov[i].index() == int32_t(i));
    }
    // give provinces to nations
    for(uint32_t i = 0; i < num_test; ++i) {
        state->world.province_set_nation_from_province_ownership(prov[i], nid[i]);
        state->world.province_set_nation_from_province_control(prov[i], nid[i]);
        state->world.province_set_rebel_faction_from_province_rebel_control(prov[i], dcon::rebel_faction_id{});
    }
    // oh no rebels are attacking! ~
    for(uint32_t i = 0; i < num_test; ++i) {
        state->world.province_set_nation_from_province_control(prov[i], dcon::nation_id{});
        state->world.province_set_rebel_faction_from_province_rebel_control(prov[i], rfid[i]);
    }
    // check what the rebels did
    for(uint32_t i = 0; i < num_test; ++i) {
        REQUIRE(state->world.province_get_nation_from_province_control(prov[i]) == dcon::nation_id{});
        REQUIRE(state->world.province_get_rebel_faction_from_province_rebel_control(prov[i]) == rfid[i]);
        auto prov_fat = dcon::fatten(state->world, prov[i]);
        REQUIRE(prov_fat.get_province_rebel_control_as_province().get_rebel_faction() == rfid[i]);
    }
}

TEST_CASE("infrastructure_edge_connects_two_nodes", "[dcon][foundation]") {
	std::unique_ptr<sys::state> state = std::make_unique<sys::state>();

	auto const first_node = state->world.create_infrastructure_node();
	auto const second_node = state->world.create_infrastructure_node();
	auto const edge = state->world.create_infrastructure_edge();
	state->world.infrastructure_edge_set_type(edge, uint8_t(3));
	state->world.infrastructure_edge_set_distance(edge, 12.5f);
	auto const edge_from = state->world.force_create_infrastructure_edge_from(edge, first_node);
	auto const edge_to = state->world.force_create_infrastructure_edge_to(edge, second_node);

	REQUIRE(state->world.infrastructure_edge_from_get_infrastructure_edge(edge_from) == edge);
	REQUIRE(state->world.infrastructure_edge_to_get_infrastructure_edge(edge_to) == edge);
	REQUIRE(state->world.infrastructure_edge_get_node_from_infrastructure_edge_from(edge) == first_node);
	REQUIRE(state->world.infrastructure_edge_get_node_from_infrastructure_edge_to(edge) == second_node);
	auto const from_reverse = state->world.infrastructure_node_get_infrastructure_edge_from_as_node(first_node);
	auto const to_reverse = state->world.infrastructure_node_get_infrastructure_edge_to_as_node(second_node);
	REQUIRE(from_reverse.begin() != from_reverse.end());
	REQUIRE(to_reverse.begin() != to_reverse.end());
	REQUIRE(state->world.infrastructure_edge_from_get_infrastructure_edge(*from_reverse.begin()) == edge);
	REQUIRE(state->world.infrastructure_edge_to_get_infrastructure_edge(*to_reverse.begin()) == edge);
	auto incident = ::world::infrastructure::incident_edges(*state, first_node);
	REQUIRE(incident.size() == 1);
	REQUIRE(incident.front() == edge);
	REQUIRE(state->world.infrastructure_edge_get_type(edge) == uint8_t(3));
	REQUIRE(state->world.infrastructure_edge_get_distance(edge) == 12.5f);
}

TEST_CASE("factory_site_bootstrap_is_deterministic", "[world][foundation]") {
	std::unique_ptr<sys::state> state = std::make_unique<sys::state>();
	auto province = state->world.create_province();
	state->world.province_set_mid_point(province, glm::vec2{ 12.0f, 34.0f });
	auto factory = state->world.create_factory();
	state->world.force_create_factory_location(factory, province);

	::world::legacy_bridge::bootstrap_factory_sites(*state);
	auto site = state->world.factory_get_site_from_factory_site(factory);
	REQUIRE(site);
	REQUIRE(::world::site::site_for_factory(*state, factory) == site);
	REQUIRE(::world::site::province_for_site(*state, site) == province);
	REQUIRE(::world::spatial::site_position(*state, site) == glm::vec2{ 12.0f, 34.0f });

	auto site_count = state->world.site_size();
	::world::legacy_bridge::bootstrap_factory_sites(*state);
	REQUIRE(state->world.site_size() == site_count);
	REQUIRE(state->world.factory_get_site_from_factory_site(factory) == site);
}

TEST_CASE("physical_inventory_dispatch_and_arrival_conserve_stock", "[economy][physical]") {
	std::unique_ptr<sys::state> state = std::make_unique<sys::state>();
	auto first_province = state->world.create_province();
	auto second_province = state->world.create_province();
	state->world.province_set_mid_point_b(first_province, glm::vec3{ 1.0f, 0.0f, 0.0f });
	state->world.province_set_mid_point_b(second_province, glm::vec3{ 1.0f, 0.0f, 0.0f });
	auto first_site = state->world.create_site();
	auto second_site = state->world.create_site();
	state->world.force_create_site_location(first_site, first_province);
	state->world.force_create_site_location(second_site, second_province);
	auto commodity = state->world.create_commodity();

	auto canonical = ::economy::physical::inventory::ensure(*state, first_site, commodity);
	REQUIRE(::economy::physical::inventory::ensure(*state, first_site, commodity) == canonical);
	REQUIRE(state->world.physical_stock_size() == 1);
	REQUIRE(::economy::physical::inventory::add(*state, first_site, commodity, 10.0f) == 10.0f);
	REQUIRE(::economy::physical::inventory::quantity(*state, first_site, commodity) == 10.0f);
	auto shipment = ::economy::physical::shipments::dispatch(*state, first_site, second_site, commodity, 6.0f);
	REQUIRE(shipment);
	REQUIRE(::economy::physical::inventory::quantity(*state, first_site, commodity) == 4.0f);
	REQUIRE(::economy::physical::inventory::quantity(*state, second_site, commodity) == 0.0f);
	state->world.shipment_set_remaining_days(shipment, 2);
	constexpr float initial_quantity = 10.0f;
	constexpr float origin_quantity = 4.0f;
	auto spoilage = economy::logistics::profile_for(*state, commodity).daily_spoilage;
	::economy::physical::shipments::advance(*state);
	REQUIRE(state->world.shipment_is_valid(shipment));
	REQUIRE(::economy::physical::inventory::quantity(*state, second_site, commodity) == 0.0f);
	auto in_transit = state->world.shipment_get_remaining_quantity(shipment);
	auto spoiled = 6.0f - in_transit;
	REQUIRE(in_transit == Approx(6.0f * (1.0f - spoilage)).epsilon(0.00001));
	REQUIRE(origin_quantity + in_transit + spoiled == Approx(initial_quantity).epsilon(0.00001));
	::economy::physical::shipments::advance(*state);
	REQUIRE(!state->world.shipment_is_valid(shipment));
	REQUIRE(::economy::physical::inventory::quantity(*state, first_site, commodity) == 4.0f);
	auto destination_quantity = ::economy::physical::inventory::quantity(*state, second_site, commodity);
	spoiled = 6.0f - destination_quantity;
	REQUIRE(destination_quantity == Approx(6.0f * (1.0f - spoilage) * (1.0f - spoilage)).epsilon(0.00001));
	REQUIRE(origin_quantity + destination_quantity + spoiled == Approx(initial_quantity).epsilon(0.00001));
}

TEST_CASE("physical_compatibility_travel_days_use_land_speed", "[economy][physical]") {
	REQUIRE(::economy::physical::shipments::compatibility_travel_days(0.0f) == 1);
	REQUIRE(::economy::physical::shipments::compatibility_travel_days(150.0f) == 1);
	REQUIRE(::economy::physical::shipments::compatibility_travel_days(150.1f) == 2);
	REQUIRE(::economy::physical::shipments::compatibility_travel_days(std::numeric_limits<float>::quiet_NaN()) == 1);
}

TEST_CASE("physical_rgo_bootstrap_is_idempotent", "[economy][physical]") {
	std::unique_ptr<sys::state> state = std::make_unique<sys::state>();
	auto province = state->world.create_province();
	auto commodity = state->world.create_commodity();
	state->world.province_resize_rgo_size(state->world.commodity_size());
	state->world.commodity_set_rgo_amount(commodity, 1.0f);
	state->world.province_set_rgo_size(province, commodity, 1.0f);

	::economy::physical::deposits::bootstrap(*state);
	auto site = ::economy::physical::deposits::extraction_site_for(*state, province, commodity);
	REQUIRE(site);
	REQUIRE(state->world.resource_deposit_size() == 1);
	REQUIRE(state->world.site_get_province_from_site_location(site) == province);
	::economy::physical::deposits::bootstrap(*state);
	REQUIRE(state->world.resource_deposit_size() == 1);
	REQUIRE(::economy::physical::deposits::extraction_site_for(*state, province, commodity) == site);
}

TEST_CASE("physical_rgo_arrives_once_at_legacy_market", "[economy][physical][integration]") {
	auto state = std::make_unique<sys::state>();
	state->force_age_of_transformation_ruleset = true;
	state->world.create_province(); // Keep the test province non-null for hub bootstrap.
	state->world.create_site(); // Keep both bootstrap endpoints non-null in this minimal fixture.
	auto province = state->world.create_province();
	auto capital = state->world.create_province();
	state->world.province_set_mid_point_b(province, glm::vec3{1.0f, 0.0f, 0.0f});
	state->world.province_set_mid_point_b(capital, glm::vec3{0.0f, 1.0f, 0.0f});
	auto zone = state->world.create_state_instance();
	state->world.create_market(); // Keep the fixture market relation non-null.
	auto market = state->world.create_market();
	auto commodity = state->world.create_commodity();
	state->world.state_instance_set_capital(zone, capital);
	state->world.state_instance_set_market_from_local_market(zone, market);
	state->world.market_set_zone_from_local_market(market, zone);
	state->world.province_set_state_membership(province, zone);
	state->world.province_resize_rgo_size(state->world.commodity_size());
	state->world.province_resize_rgo_output(state->world.commodity_size());
	state->world.market_resize_supply(state->world.commodity_size());
	state->world.market_resize_stockpile(state->world.commodity_size());
	state->world.commodity_set_rgo_amount(commodity, 1.0f);
	state->world.province_set_rgo_size(province, commodity, 1.0f);
	state->world.province_set_rgo_output(province, commodity, 5.0f);
	REQUIRE(state->world.province_get_state_membership(province) == zone);
	REQUIRE(state->world.state_instance_get_market_from_local_market(zone) == market);
	REQUIRE(state->world.province_get_rgo_output(province, commodity) == 5.0f);
	REQUIRE(!state->world.commodity_get_is_local(commodity));
	REQUIRE(!state->world.commodity_get_money_rgo(commodity));

	::economy::physical::deposits::bootstrap(*state);
	REQUIRE(::economy::physical::deposits::extraction_site_for(*state, province, commodity));
	::economy::physical::shipments::process_rgo_output(*state);
	REQUIRE(state->world.shipment_size() == 1);
	REQUIRE(state->world.market_get_stockpile(market, commodity) == Approx(0.0f));
	REQUIRE(state->world.market_get_supply(market, commodity) == Approx(0.0f));

	while(state->world.shipment_size() != 0)
		::economy::physical::shipments::advance(*state);
	::economy::physical::legacy_market_bridge::handoff_arrived_stock(*state);
	REQUIRE(state->world.shipment_size() == 0);
	auto expected = 5.0f * (1.0f - economy::logistics::profile_for(*state, commodity).daily_spoilage);
	REQUIRE(state->world.market_get_stockpile(market, commodity) == Approx(expected).epsilon(0.00001));
	REQUIRE(state->world.market_get_stockpile(market, commodity) == Approx(expected).epsilon(0.00001));
}

TEST_CASE("local_rgo_keeps_legacy_supply_in_physical_mode", "[economy][physical][integration]") {
	auto state = std::make_unique<sys::state>();
	state->force_age_of_transformation_ruleset = true;
	state->world.create_province();
	auto province = state->world.create_province();
	auto zone = state->world.create_state_instance();
	state->world.create_market();
	auto market = state->world.create_market();
	auto commodity = state->world.create_commodity();
	state->world.state_instance_set_capital(zone, province);
	state->world.state_instance_set_market_from_local_market(zone, market);
	state->world.market_set_zone_from_local_market(market, zone);
	state->world.province_set_state_membership(province, zone);
	state->world.province_resize_rgo_size(state->world.commodity_size());
	state->world.province_resize_rgo_output(state->world.commodity_size());
	state->world.market_resize_supply(state->world.commodity_size());
	state->world.market_resize_stockpile(state->world.commodity_size());
	state->world.commodity_set_rgo_amount(commodity, 1.0f);
	state->world.commodity_set_is_local(commodity, true);
	state->world.province_set_rgo_size(province, commodity, 1.0f);
	state->world.province_set_rgo_output(province, commodity, 3.0f);

	::economy::physical::deposits::bootstrap(*state);
	::economy::physical::shipments::process_rgo_output(*state);
	REQUIRE(state->world.market_get_supply(market, commodity) == Approx(3.0f));
	REQUIRE(state->world.shipment_size() == 0);
	REQUIRE(state->world.market_get_stockpile(market, commodity) == Approx(0.0f));
}
