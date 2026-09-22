#include "catch.hpp"

#include "economy/cargo_transit.hpp"
#include "economy/market_access.hpp"
#include "serialization.hpp"
#include "system_state.hpp"
#include "world/spatial_runtime.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <vector>

namespace spatial_runtime_tests {

struct fixture {
	std::unique_ptr<sys::state> state = std::make_unique<sys::state>();
	std::vector<dcon::province_id> provinces;
	std::vector<dcon::site_id> sites;
	std::vector<dcon::infrastructure_node_id> nodes;

	explicit fixture(uint32_t count = 4, bool shuffled = false) {
		for(uint32_t i = 0; i < count; ++i) {
			auto province = state->world.create_province();
			state->world.province_set_mid_point(province, glm::vec2{float(i * 10), float(i * 3)});
			provinces.push_back(province);
		}
		std::vector<uint32_t> order(count);
		for(uint32_t i = 0; i < count; ++i) order[i] = i;
		if(shuffled) std::reverse(order.begin(), order.end());
		for(auto index : order) {
			auto site = state->world.create_site();
			state->world.force_create_site_location(site, provinces[index]);
			state->world.site_set_position(site, state->world.province_get_mid_point(provinces[index]));
			sites.push_back(site);
			auto node = state->world.create_infrastructure_node();
			state->world.force_create_infrastructure_node_location(node, provinces[index]);
			state->world.infrastructure_node_set_position(node, state->world.province_get_mid_point(provinces[index]));
			nodes.push_back(node);
		}
		std::sort(sites.begin(), sites.end(), [&](auto left, auto right) {
			return state->world.site_get_province_from_site_location(left).index()
				< state->world.site_get_province_from_site_location(right).index();
		});
		std::sort(nodes.begin(), nodes.end(), [&](auto left, auto right) {
			return state->world.infrastructure_node_get_province_from_infrastructure_node_location(left).index()
				< state->world.infrastructure_node_get_province_from_infrastructure_node_location(right).index();
		});
	}

	dcon::infrastructure_edge_id edge(uint32_t from, uint32_t to, float distance, uint8_t quality = 0) {
		auto result = state->world.create_infrastructure_edge();
		state->world.infrastructure_edge_set_type(result, quality);
		state->world.infrastructure_edge_set_distance(result, distance);
		state->world.force_create_infrastructure_edge_from(result, nodes[from]);
		state->world.force_create_infrastructure_edge_to(result, nodes[to]);
		return result;
	}
};

}

TEST_CASE("spatial bootstrap is deterministic and maps economic endpoints", "[spatial][bootstrap]") {
	spatial_runtime_tests::fixture f(3);
	auto state_instance = f.state->world.create_state_instance();
	auto market = f.state->world.create_market();
	f.state->world.state_instance_set_capital(state_instance, f.provinces[0]);
	f.state->world.state_instance_set_market_from_local_market(state_instance, market);
	f.state->world.market_set_zone_from_local_market(market, state_instance);
	auto factory = f.state->world.create_factory();
	f.state->world.force_create_factory_location(factory, f.provinces[1]);
	auto first = world::spatial_runtime::bootstrap(*f.state);
	REQUIRE(first.status == world::spatial_runtime::bootstrap_status::created);
	REQUIRE(first.settlements_created == 3);
	REQUIRE(first.sites_created == 0);
	REQUIRE(first.nodes_created == 0);
	REQUIRE(world::spatial_runtime::site_for_province(*f.state, f.provinces[1])
		== f.state->world.factory_get_site_from_factory_site(factory));
	REQUIRE(world::spatial_runtime::site_for_market(*f.state, market));
	REQUIRE(world::spatial_runtime::settlement_for_province(*f.state, f.provinces[0]));
	for(uint32_t i = 0; i < 2; ++i) f.edge(i, i + 1, 10.0f);
	first = world::spatial_runtime::bootstrap(*f.state);
	REQUIRE(first.edges_created == 2);
	auto sites_before = f.state->world.site_size();
	auto nodes_before = f.state->world.infrastructure_node_size();
	auto settlements_before = f.state->world.settlement_size();
	auto second = world::spatial_runtime::bootstrap(*f.state);
	REQUIRE(second.settlements_created == 0);
	REQUIRE(second.sites_created == 0);
	REQUIRE(second.nodes_created == 0);
	REQUIRE(second.edges_created == 0);
	REQUIRE(f.state->world.site_size() == sites_before);
	REQUIRE(f.state->world.infrastructure_node_size() == nodes_before);
	REQUIRE(f.state->world.settlement_size() == settlements_before);
}

TEST_CASE("spatial identities and routes survive DCON save/load", "[spatial][serialization]") {
	spatial_runtime_tests::fixture f(2);
	f.edge(0, 1, 25.0f, 2);
	world::spatial_runtime::bootstrap(*f.state);
	auto before_site = world::spatial_runtime::site_for_province(*f.state, f.provinces[0]);
	auto before_node = world::spatial_runtime::node_for_province(*f.state, f.provinces[0]);
	auto before_route = world::spatial_runtime::route_for_sites(*f.state,
		world::spatial_runtime::site_for_province(*f.state, f.provinces[0]),
		world::spatial_runtime::site_for_province(*f.state, f.provinces[1]));
	auto record = f.state->world.make_serialize_record_store_save();
	std::vector<std::byte> buffer(f.state->world.serialize_size(record));
	auto write = buffer.data();
	f.state->world.serialize(write, record);
	std::unique_ptr<sys::state> loaded = std::make_unique<sys::state>();
	dcon::load_record load_record;
	std::byte const* read = buffer.data();
	loaded->world.deserialize(read, buffer.data() + buffer.size(), load_record);
	REQUIRE(world::spatial_runtime::site_for_province(*loaded, f.provinces[0]) == before_site);
	REQUIRE(world::spatial_runtime::node_for_province(*loaded, f.provinces[0]) == before_node);
	world::spatial_runtime::bootstrap(*loaded);
	auto after_route = world::spatial_runtime::route_for_sites(*loaded,
		world::spatial_runtime::site_for_province(*loaded, f.provinces[0]),
		world::spatial_runtime::site_for_province(*loaded, f.provinces[1]));
	REQUIRE(after_route.connected == before_route.connected);
	REQUIRE(after_route.distance == Approx(before_route.distance));
	REQUIRE(after_route.travel_days == Approx(before_route.travel_days));
}

TEST_CASE("spatial routes handle disconnection and deterministic equal-cost ties", "[spatial][routing]") {
	spatial_runtime_tests::fixture f(5);
	f.edge(0, 2, 10.0f);
	f.edge(2, 4, 10.0f);
	f.edge(0, 1, 10.0f);
	f.edge(1, 4, 10.0f);
	auto chosen = world::spatial_runtime::shortest_path(*f.state, f.nodes[0], f.nodes[4]);
	REQUIRE(chosen.connected);
	REQUIRE(chosen.edges.size() == 2);
	auto first_to = f.state->world.infrastructure_edge_get_node_from_infrastructure_edge_from(chosen.edges.front())
		== f.nodes[0]
		? f.state->world.infrastructure_edge_get_node_from_infrastructure_edge_to(chosen.edges.front())
		: f.state->world.infrastructure_edge_get_node_from_infrastructure_edge_from(chosen.edges.front());
	REQUIRE(first_to == f.nodes[1]);
	auto disconnected = world::spatial_runtime::shortest_path(*f.state, f.nodes[0], f.nodes[3]);
	REQUIRE_FALSE(disconnected.connected);
}

TEST_CASE("spatial route replay is deterministic", "[spatial][determinism]") {
	spatial_runtime_tests::fixture f(4);
	f.edge(0, 1, 12.0f, 1);
	f.edge(1, 3, 18.0f, 1);
	f.edge(0, 2, 12.0f, 1);
	f.edge(2, 3, 18.0f, 1);
	auto first = world::spatial_runtime::cheapest_path(*f.state, f.nodes[0], f.nodes[3]);
	REQUIRE(first.connected);
	for(uint32_t i = 0; i < 8; ++i) {
		auto replay = world::spatial_runtime::cheapest_path(*f.state, f.nodes[0], f.nodes[3]);
		REQUIRE(replay.connected == first.connected);
		REQUIRE(replay.distance == Approx(first.distance));
		REQUIRE(replay.generalized_cost == Approx(first.generalized_cost));
		REQUIRE(replay.edges == first.edges);
	}
}

TEST_CASE("infrastructure quality and capacity affect route choice", "[spatial][routing]") {
	spatial_runtime_tests::fixture f(4);
	f.edge(0, 1, 100.0f, 0);
	f.edge(1, 3, 1.0f, 0);
	f.edge(0, 2, 100.0f, 5);
	f.edge(2, 3, 1.0f, 5);
	auto quality_route = world::spatial_runtime::cheapest_path(*f.state, f.nodes[0], f.nodes[3]);
	REQUIRE(quality_route.connected);
	auto first_to = f.state->world.infrastructure_edge_get_node_from_infrastructure_edge_from(quality_route.edges.front())
		== f.nodes[0]
		? f.state->world.infrastructure_edge_get_node_from_infrastructure_edge_to(quality_route.edges.front())
		: f.state->world.infrastructure_edge_get_node_from_infrastructure_edge_from(quality_route.edges.front());
	REQUIRE(first_to == f.nodes[2]);
	spatial_runtime_tests::fixture bottleneck(3);
	bottleneck.edge(0, 1, 10.0f, 0);
	bottleneck.edge(1, 2, 10.0f, 0);
	auto route = world::spatial_runtime::shortest_path(*bottleneck.state, bottleneck.nodes[0], bottleneck.nodes[2]);
	REQUIRE(route.connected);
	REQUIRE(route.bottleneck_capacity == Approx(100.0f));
	auto overloaded = world::spatial_runtime::cheapest_path(*bottleneck.state,
		bottleneck.nodes[0], bottleneck.nodes[2], 101.0f);
	REQUIRE_FALSE(overloaded.connected);
}

TEST_CASE("network-derived market access and route transit use physical paths", "[spatial][economy]") {
	spatial_runtime_tests::fixture f(2);
	f.edge(0, 1, 100.0f, 1);
	auto local = economy::market_access::evaluate_site(*f.state, f.sites[0], f.sites[0]);
	auto remote = economy::market_access::evaluate_site(*f.state, f.sites[0], f.sites[1]);
	REQUIRE(local.network_derived);
	REQUIRE(remote.network_derived);
	REQUIRE(remote.access < local.access);
	REQUIRE(remote.route_distance == Approx(100.0f));
	REQUIRE(remote.route_travel_days > 1.0f);
	auto transit = economy::cargo_transit::advance({
		.enabled = true,
		.opening_cargo = 100.0f,
		.dispatched_cargo = 100.0f,
		.route_distance = remote.route_distance,
		.route_travel_days = remote.route_travel_days,
		.route_capacity = remote.route_capacity,
		.cargo_weight = 1.0f,
		.daily_spoilage = 0.10f});
	REQUIRE(transit.delivered_cargo < 100.0f);
	REQUIRE(transit.spoiled_cargo > 0.0f);
	REQUIRE(transit.blocked_cargo == Approx(0.0f));
}

TEST_CASE("route capacity, congestion, and duration determine cargo outcome", "[spatial][cargo]") {
	auto limited = economy::cargo_transit::advance({
		.enabled = true,
		.dispatched_cargo = 100.0f,
		.route_distance = 20.0f,
		.route_travel_days = 4.0f,
		.route_capacity = 100.0f,
		.cargo_weight = 1.0f,
		.capacity_utilization = 0.50f,
		.daily_spoilage = 0.0f});
	REQUIRE(limited.blocked_cargo == Approx(50.0f));
	REQUIRE(limited.closing_cargo == Approx(50.0f));
	auto spoiled = economy::cargo_transit::advance({
		.enabled = true,
		.opening_cargo = 100.0f,
		.route_distance = 20.0f,
		.route_travel_days = 5.0f,
		.route_capacity = 1000.0f,
		.cargo_weight = 1.0f,
		.daily_spoilage = 0.10f});
	REQUIRE(spoiled.delivered_cargo == Approx(20.0f));
	REQUIRE(spoiled.spoiled_cargo == Approx(8.0f));
	REQUIRE(spoiled.closing_cargo == Approx(72.0f));
}

TEST_CASE("legacy, exact, and mixed spatial representations share one mapping", "[spatial][representation]") {
	spatial_runtime_tests::fixture legacy(2);
	legacy.edge(0, 1, 30.0f, 2);
	world::spatial_runtime::bootstrap(*legacy.state);
	auto legacy_route = world::spatial_runtime::route_for_sites(*legacy.state,
		world::spatial_runtime::site_for_province(*legacy.state, legacy.provinces[0]),
		world::spatial_runtime::site_for_province(*legacy.state, legacy.provinces[1]));
	spatial_runtime_tests::fixture exact(2, true);
	exact.edge(0, 1, 30.0f, 2);
	world::spatial_runtime::bootstrap(*exact.state);
	auto exact_route = world::spatial_runtime::route_for_sites(*exact.state, exact.sites[0], exact.sites[1]);
	REQUIRE(exact_route.distance == Approx(legacy_route.distance));
	REQUIRE(exact_route.travel_days == Approx(legacy_route.travel_days));
	REQUIRE(exact_route.bottleneck_capacity == Approx(legacy_route.bottleneck_capacity));

	spatial_runtime_tests::fixture mixed(2);
	auto preexisting_settlement = mixed.state->world.create_settlement();
	mixed.state->world.force_create_settlement_location(preexisting_settlement, mixed.provinces[0]);
	world::spatial_runtime::bootstrap(*mixed.state);
	auto sites_before = mixed.state->world.site_size();
	world::spatial_runtime::bootstrap(*mixed.state);
	REQUIRE(mixed.state->world.site_size() == sites_before);
	REQUIRE(world::spatial_runtime::settlement_for_province(*mixed.state, mixed.provinces[0])
		== preexisting_settlement);
}
