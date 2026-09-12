#include "catch.hpp"
#include "dcon_generated.hpp"
#include "container_types.hpp"
#include "system_state.hpp"
#include "serialization.hpp"

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
