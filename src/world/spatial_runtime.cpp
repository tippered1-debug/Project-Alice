#include "spatial_runtime.hpp"

#include "system_state.hpp"
#include "economy/economy_constants.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <tuple>

namespace world::spatial_runtime {
namespace {

constexpr float epsilon = 1.0e-5f;

template<typename Id>
uint32_t id_key(Id id) noexcept {
	return id ? uint32_t(id.index()) : std::numeric_limits<uint32_t>::max();
}

float finite_nonnegative(float value) noexcept {
	return std::isfinite(value) && value >= 0.0f ? value : 0.0f;
}

template<typename Id>
bool id_less(Id left, Id right) noexcept {
	return id_key(left) < id_key(right);
}

dcon::settlement_id first_settlement(sys::state const& state, dcon::province_id province) {
	dcon::settlement_id result{};
	state.world.province_for_each_settlement_location_as_province(province, [&](auto relation) {
		auto settlement = state.world.settlement_location_get_settlement(relation);
		if(settlement && (!result || id_less(settlement, result))) result = settlement;
	});
	return result;
}

dcon::site_id first_site(sys::state const& state, dcon::province_id province) {
	dcon::site_id result{};
	state.world.province_for_each_site_location_as_province(province, [&](auto relation) {
		auto site = state.world.site_location_get_site(relation);
		if(site && (!result || id_less(site, result))) result = site;
	});
	return result;
}

dcon::infrastructure_node_id first_node(sys::state const& state, dcon::province_id province) {
	dcon::infrastructure_node_id result{};
	state.world.province_for_each_infrastructure_node_location_as_province(province, [&](auto relation) {
		auto node = state.world.infrastructure_node_location_get_infrastructure_node(relation);
		if(node && (!result || id_less(node, result))) result = node;
	});
	return result;
}

std::vector<dcon::province_id> provinces(sys::state const& state) {
	std::vector<dcon::province_id> result;
	state.world.for_each_province([&](auto province) { result.push_back(province); });
	std::sort(result.begin(), result.end(), id_less<dcon::province_id>);
	return result;
}

std::vector<dcon::infrastructure_edge_id> edges_between(sys::state const& state,
	dcon::infrastructure_node_id from, dcon::infrastructure_node_id to) {
	std::vector<dcon::infrastructure_edge_id> result;
	if(!from || !to) return result;
	state.world.infrastructure_node_for_each_infrastructure_edge_from_as_node(from, [&](auto relation) {
		auto edge = state.world.infrastructure_edge_from_get_infrastructure_edge(relation);
		if(edge) result.push_back(edge);
	});
	state.world.infrastructure_node_for_each_infrastructure_edge_to_as_node(from, [&](auto relation) {
		auto edge = state.world.infrastructure_edge_to_get_infrastructure_edge(relation);
		if(edge) result.push_back(edge);
	});
	std::sort(result.begin(), result.end(), id_less<dcon::infrastructure_edge_id>);
	result.erase(std::unique(result.begin(), result.end()), result.end());
	result.erase(std::remove_if(result.begin(), result.end(), [&](auto edge) {
		auto a = state.world.infrastructure_edge_get_node_from_infrastructure_edge_from(edge);
		auto b = state.world.infrastructure_edge_get_node_from_infrastructure_edge_to(edge);
		return !((a == from && b == to) || (a == to && b == from));
	}), result.end());
	return result;
}

float edge_distance(sys::state const& state, dcon::infrastructure_edge_id edge) noexcept {
	return edge && state.world.infrastructure_edge_is_valid(edge)
		? finite_nonnegative(state.world.infrastructure_edge_get_distance(edge)) : 0.0f;
}

uint8_t edge_type(sys::state const& state, dcon::infrastructure_edge_id edge) noexcept {
	return edge && state.world.infrastructure_edge_is_valid(edge)
		? state.world.infrastructure_edge_get_type(edge) : uint8_t(0);
}

uint32_t node_key(sys::state const& state, dcon::infrastructure_node_id node) noexcept {
	if(!node) return std::numeric_limits<uint32_t>::max();
	auto province = state.world.infrastructure_node_get_province_from_infrastructure_node_location(node);
	return province ? uint32_t(province.index()) : (1u << 30) + uint32_t(node.index());
}

uint64_t edge_key(sys::state const& state, dcon::infrastructure_edge_id edge) noexcept {
	if(!edge) return std::numeric_limits<uint64_t>::max();
	auto from = state.world.infrastructure_edge_get_node_from_infrastructure_edge_from(edge);
	auto to = state.world.infrastructure_edge_get_node_from_infrastructure_edge_to(edge);
	auto a = std::min(node_key(state, from), node_key(state, to));
	auto b = std::max(node_key(state, from), node_key(state, to));
	return (uint64_t(a) << 32) | uint64_t(b);
}

bool path_less(sys::state const& state, std::vector<dcon::infrastructure_edge_id> const& left,
	std::vector<dcon::infrastructure_edge_id> const& right) {
	for(size_t i = 0; i < std::min(left.size(), right.size()); ++i) {
		auto lk = edge_key(state, left[i]);
		auto rk = edge_key(state, right[i]);
		if(lk != rk) return lk < rk;
	}
	return left.size() < right.size();
}

struct path_state {
	dcon::infrastructure_node_id node{};
	float metric = std::numeric_limits<float>::infinity();
	float distance = 0.0f;
	float travel_days = 0.0f;
	float capacity = std::numeric_limits<float>::infinity();
	std::vector<dcon::infrastructure_edge_id> edges;
};

struct path_state_before {
	sys::state const* state = nullptr;
	bool operator()(path_state const& left, path_state const& right) const {
		if(std::abs(left.metric - right.metric) > epsilon) return left.metric > right.metric;
		if(left.node != right.node) return node_key(*state, left.node) > node_key(*state, right.node);
		return path_less(*state, right.edges, left.edges);
	}
};

route solve(sys::state const& state, dcon::infrastructure_node_id origin,
	dcon::infrastructure_node_id destination, bool generalized, float cargo_units) noexcept {
	route result{};
	if(!origin || !destination || !state.world.infrastructure_node_is_valid(origin)
		|| !state.world.infrastructure_node_is_valid(destination)) return result;
	if(origin == destination) {
		result.connected = true;
		result.bottleneck_capacity = std::numeric_limits<float>::infinity();
		return result;
	}
	std::vector<path_state> best(state.world.infrastructure_node_size());
	std::vector<bool> visited(state.world.infrastructure_node_size(), false);
	std::priority_queue<path_state, std::vector<path_state>, path_state_before> queue({path_state_before{&state}});
	path_state start{};
	start.node = origin;
	start.metric = 0.0f;
	start.capacity = std::numeric_limits<float>::infinity();
	best[origin.index()] = start;
	queue.push(start);
	while(!queue.empty()) {
		auto current = queue.top();
		queue.pop();
		if(current.node.index() >= visited.size() || visited[current.node.index()]) continue;
		visited[current.node.index()] = true;
		if(current.node == destination) {
			result.connected = true;
			result.distance = current.distance;
			result.generalized_cost = current.metric;
			result.travel_days = current.travel_days;
			result.bottleneck_capacity = current.capacity;
			result.edges = std::move(current.edges);
			return result;
		}
		std::vector<dcon::infrastructure_edge_id> incident;
		state.world.infrastructure_node_for_each_infrastructure_edge_from_as_node(current.node, [&](auto relation) {
			incident.push_back(state.world.infrastructure_edge_from_get_infrastructure_edge(relation));
		});
		state.world.infrastructure_node_for_each_infrastructure_edge_to_as_node(current.node, [&](auto relation) {
			incident.push_back(state.world.infrastructure_edge_to_get_infrastructure_edge(relation));
		});
		std::sort(incident.begin(), incident.end(), [&](auto left, auto right) {
			auto lk = edge_key(state, left), rk = edge_key(state, right);
			return lk == rk ? id_less(left, right) : lk < rk;
		});
		incident.erase(std::unique(incident.begin(), incident.end()), incident.end());
		for(auto edge : incident) {
			if(!edge || !state.world.infrastructure_edge_is_valid(edge)) continue;
			auto from = state.world.infrastructure_edge_get_node_from_infrastructure_edge_from(edge);
			auto to = state.world.infrastructure_edge_get_node_from_infrastructure_edge_to(edge);
			auto next = from == current.node ? to : (to == current.node ? from : dcon::infrastructure_node_id{});
			if(!next || next.index() >= best.size() || visited[next.index()]) continue;
			auto capacity = effective_capacity(state, edge);
			auto distance = edge_distance(state, edge);
			auto days = traversal_days(state, edge);
			auto metric = generalized ? generalized_edge_cost(state, edge, cargo_units) : distance;
			if(!std::isfinite(metric)) continue;
			path_state candidate = current;
			candidate.node = next;
			candidate.metric += metric;
			candidate.distance += distance;
			candidate.travel_days += days;
			candidate.capacity = std::min(candidate.capacity, capacity);
			candidate.edges.push_back(edge);
			auto const& old = best[next.index()];
			bool better = candidate.metric < old.metric - epsilon;
			if(!better && std::abs(candidate.metric - old.metric) <= epsilon)
				better = old.edges.empty() || path_less(state, candidate.edges, old.edges);
			if(better) {
				best[next.index()] = candidate;
				queue.push(candidate);
			}
		}
	}
	return result;
}

} // namespace

bootstrap_result bootstrap(sys::state& state) {
	bootstrap_result result{};
	if(state.world.province_size() == 0) return result;
	result.status = bootstrap_status::unchanged;
	for(auto province : provinces(state)) {
		auto settlement = first_settlement(state, province);
		if(!settlement) {
			settlement = state.world.create_settlement();
			if(!settlement) { result.status = bootstrap_status::invalid_world; return result; }
			state.world.force_create_settlement_location(settlement, province);
			++result.settlements_created;
		}
		state.world.settlement_set_position(settlement, state.world.province_get_mid_point(province));
		auto site = first_site(state, province);
		if(!site) {
			site = state.world.create_site();
			if(!site) { result.status = bootstrap_status::invalid_world; return result; }
			state.world.force_create_site_location(site, province);
			++result.sites_created;
		}
		state.world.site_set_position(site, state.world.province_get_mid_point(province));
		if(!state.world.site_get_settlement_from_site_settlement(site))
			state.world.force_create_site_settlement(site, settlement);
		auto node = first_node(state, province);
		if(!node) {
			node = state.world.create_infrastructure_node();
			if(!node) { result.status = bootstrap_status::invalid_world; return result; }
			state.world.force_create_infrastructure_node_location(node, province);
			++result.nodes_created;
		}
		state.world.infrastructure_node_set_position(node, state.world.province_get_mid_point(province));
	}

	std::vector<dcon::province_adjacency_id> adjacencies;
	state.world.for_each_province_adjacency([&](auto adjacency) { adjacencies.push_back(adjacency); });
	std::sort(adjacencies.begin(), adjacencies.end(), [&](auto left, auto right) {
		auto la = state.world.province_adjacency_get_connected_provinces(left, 0);
		auto lb = state.world.province_adjacency_get_connected_provinces(left, 1);
		auto ra = state.world.province_adjacency_get_connected_provinces(right, 0);
		auto rb = state.world.province_adjacency_get_connected_provinces(right, 1);
		auto lk = std::pair{std::min(id_key(la), id_key(lb)), std::max(id_key(la), id_key(lb))};
		auto rk = std::pair{std::min(id_key(ra), id_key(rb)), std::max(id_key(ra), id_key(rb))};
		return lk == rk ? id_less(left, right) : lk < rk;
	});
	for(auto adjacency : adjacencies) {
		auto first = state.world.province_adjacency_get_connected_provinces(adjacency, 0);
		auto second = state.world.province_adjacency_get_connected_provinces(adjacency, 1);
		if(!first || !second || first == second) continue;
		auto from = node_for_province(state, first);
		auto to = node_for_province(state, second);
		if(!from || !to || !edges_between(state, from, to).empty()) continue;
		auto edge = state.world.create_infrastructure_edge();
		if(!edge) { result.status = bootstrap_status::invalid_world; return result; }
		auto rail_a = state.world.province_get_building_level(first,
		uint8_t(economy::province_building_type::railroad));
		auto rail_b = state.world.province_get_building_level(second,
		uint8_t(economy::province_building_type::railroad));
		state.world.infrastructure_edge_set_type(edge, std::min(rail_a, rail_b));
		auto distance = state.world.province_adjacency_get_distance_km(adjacency);
		if(!std::isfinite(distance) || distance <= 0.0f)
			distance = state.world.province_adjacency_get_distance(adjacency);
		state.world.infrastructure_edge_set_distance(edge, finite_nonnegative(distance));
		state.world.force_create_infrastructure_edge_from(edge, from);
		state.world.force_create_infrastructure_edge_to(edge, to);
		++result.edges_created;
	}

	state.world.for_each_market([&](auto market) {
		if(state.world.market_get_site_from_market_hub_site(market)) return;
		auto zone = state.world.market_get_zone_from_local_market(market);
		auto province = zone ? state.world.state_instance_get_capital(zone) : dcon::province_id{};
		auto site = site_for_province(state, province);
		if(site) state.world.force_create_market_hub_site(market, site);
	});
	state.world.for_each_factory([&](auto factory) {
		if(state.world.factory_get_site_from_factory_site(factory)) return;
		auto province = state.world.factory_get_province_from_factory_location(factory);
		auto site = site_for_province(state, province);
		if(site) state.world.force_create_factory_site(factory, site);
	});
	if(result.settlements_created || result.sites_created || result.nodes_created || result.edges_created)
		result.status = bootstrap_status::created;
	return result;
}

dcon::settlement_id settlement_for_province(sys::state const& state, dcon::province_id province) {
	return first_settlement(state, province);
}

dcon::site_id site_for_province(sys::state const& state, dcon::province_id province) {
	return first_site(state, province);
}

dcon::site_id site_for_market(sys::state const& state, dcon::market_id market) {
	if(!market || !state.world.market_is_valid(market)) return {};
	if(auto existing = state.world.market_get_site_from_market_hub_site(market)) return existing;
	auto zone = state.world.market_get_zone_from_local_market(market);
	return zone ? site_for_province(state, state.world.state_instance_get_capital(zone)) : dcon::site_id{};
}

dcon::infrastructure_node_id node_for_province(sys::state const& state, dcon::province_id province) {
	return first_node(state, province);
}

dcon::infrastructure_node_id node_for_site(sys::state const& state, dcon::site_id site) {
	if(!site || !state.world.site_is_valid(site)) return {};
	return node_for_province(state, state.world.site_get_province_from_site_location(site));
}

float effective_capacity(sys::state const& state, dcon::infrastructure_edge_id edge) noexcept {
	if(!edge || !state.world.infrastructure_edge_is_valid(edge)) return 0.0f;
	return 100.0f * (1.0f + 0.5f * float(edge_type(state, edge)));
}

float traversal_days(sys::state const& state, dcon::infrastructure_edge_id edge) noexcept {
	if(!edge || !state.world.infrastructure_edge_is_valid(edge)) return 0.0f;
	auto quality = float(edge_type(state, edge));
	return 1.0f + edge_distance(state, edge) / (40.0f + 20.0f * quality);
}

float generalized_edge_cost(sys::state const& state, dcon::infrastructure_edge_id edge,
	float cargo_units) noexcept {
	auto distance = edge_distance(state, edge);
	auto capacity = effective_capacity(state, edge);
	if(cargo_units > capacity && capacity > 0.0f) return std::numeric_limits<float>::infinity();
	auto utilization = capacity > 0.0f ? std::clamp(finite_nonnegative(cargo_units) / capacity, 0.0f, 0.99f) : 1.0f;
	auto congestion = 1.0f + utilization * utilization * 4.0f;
	auto quality = float(edge_type(state, edge));
	auto quality_factor = 1.0f / (1.0f + 0.25f * quality);
	return distance * quality_factor * congestion + traversal_days(state, edge);
}

route shortest_path(sys::state const& state, dcon::infrastructure_node_id origin,
	dcon::infrastructure_node_id destination) noexcept {
	return solve(state, origin, destination, false, 0.0f);
}

route cheapest_path(sys::state const& state, dcon::infrastructure_node_id origin,
	dcon::infrastructure_node_id destination, float cargo_units) noexcept {
	return solve(state, origin, destination, true, cargo_units);
}

route route_for_sites(sys::state const& state, dcon::site_id origin, dcon::site_id destination,
	float cargo_units) noexcept {
	return cheapest_path(state, node_for_site(state, origin), node_for_site(state, destination), cargo_units);
}

} // namespace world::spatial_runtime
