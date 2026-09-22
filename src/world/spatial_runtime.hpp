#pragma once

#include "dcon_generated.hpp"

#include <cstdint>
#include <vector>

namespace sys { class state; }

namespace world::spatial_runtime {

enum class bootstrap_status : uint8_t {
	unchanged,
	created,
	invalid_world
};

struct bootstrap_result {
	bootstrap_status status = bootstrap_status::invalid_world;
	uint32_t settlements_created = 0;
	uint32_t sites_created = 0;
	uint32_t nodes_created = 0;
	uint32_t edges_created = 0;
};

struct route {
	bool connected = false;
	float distance = 0.0f;
	float generalized_cost = 0.0f;
	float travel_days = 0.0f;
	float bottleneck_capacity = 0.0f;
	std::vector<dcon::infrastructure_edge_id> edges;
};

bootstrap_result bootstrap(sys::state&);

dcon::settlement_id settlement_for_province(sys::state const&, dcon::province_id);
dcon::site_id site_for_province(sys::state const&, dcon::province_id);
dcon::site_id site_for_market(sys::state const&, dcon::market_id);
dcon::infrastructure_node_id node_for_province(sys::state const&, dcon::province_id);
dcon::infrastructure_node_id node_for_site(sys::state const&, dcon::site_id);

float effective_capacity(sys::state const&, dcon::infrastructure_edge_id) noexcept;
float traversal_days(sys::state const&, dcon::infrastructure_edge_id) noexcept;
float generalized_edge_cost(sys::state const&, dcon::infrastructure_edge_id,
	float cargo_units = 0.0f) noexcept;

route shortest_path(sys::state const&, dcon::infrastructure_node_id,
	dcon::infrastructure_node_id) noexcept;
route cheapest_path(sys::state const&, dcon::infrastructure_node_id,
	dcon::infrastructure_node_id, float cargo_units = 0.0f) noexcept;
route route_for_sites(sys::state const&, dcon::site_id, dcon::site_id,
	float cargo_units = 0.0f) noexcept;

} // namespace world::spatial_runtime
