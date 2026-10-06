#pragma once

#include "dcon_generated_ids.hpp"
#include "system_state_forward.hpp"

#include <array>
#include <vector>

namespace economy::world_trade {

enum class transport_mode : uint8_t { land, sea, local };

// Canonical physical capacity used by every shipment in the running economy.
struct capacity_config {
	float minimum_expansion_multiplier = 0.10f;
	// Existing roads and harbours never disappear completely when their paid
	// labour/service market clears at zero. This is the residual throughput of
	// an otherwise valid endpoint.
	float minimum_transport_availability = 0.10f;
	float maximum_transport_cost_multiplier = 3.00f;
};

struct capacity_inputs {
	float cargo = 0.0f;
	std::array<float, 2> endpoint_capacity{0.0f, 0.0f};
	std::array<float, 2> endpoint_transport_availability{1.0f, 1.0f};
};

struct capacity_result {
	float cargo = 0.0f;
	float nominal_capacity = 0.0f;
	float transport_availability = 0.0f;
	float effective_capacity = 0.0f;
	float utilization = 0.0f;
	float congestion = 0.0f;
	float headroom = 0.0f;
	float shortfall = 0.0f;
	float expansion_multiplier = 1.0f;
	float transport_cost_multiplier = 1.0f;
};

// Pure, deterministic and finite for arbitrary floating-point inputs.
capacity_result evaluate_capacity(capacity_config const& config, capacity_inputs const& inputs);

capacity_config canonical_config_for(sys::state const& state);

// Physical capacity primitive for concrete shipments. This path uses only
// explicit modal infrastructure state: railroad building levels for land and
// local movement, and naval-base/civilian-port levels for sea movement. It
// never reads max_throughput, population, route.volume, market clearing fill,
// or aggregate cargo demand.
float canonical_capacity(sys::state const& state, dcon::market_id market,
	transport_mode mode);

// One deterministic clearing result for the whole network. route.volume remains
// merchants' desired order; requested cargo is the part backed by the origin
// market's commodity allocation, and actual cargo is additionally bounded by
// shared endpoint throughput and transport availability. Keeping the allocation
// outside serialized state makes this an exact legacy no-op and avoids treating
// the same port or rail capacity as independently available to every route.
struct shipment_allocation {
	std::vector<float> requested_route_cargo;
	std::vector<float> actual_route_cargo;
	std::vector<float> route_scale;
	uint32_t commodity_count = 0;
	// Flat [route * commodity_count + commodity] arrays. Commodity-backed
	// requests stay observable instead of disappearing into one route scalar.
	std::vector<float> requested_commodity_cargo;
	std::vector<float> actual_commodity_cargo;
	std::vector<float> commodity_scale;
	std::vector<float> requested_market_capacity;
	std::vector<float> market_scale;
	std::vector<float> requested_land_capacity;
	std::vector<float> requested_sea_capacity;
	std::vector<float> land_scale;
	std::vector<float> sea_scale;

	float requested(dcon::trade_route_id route) const noexcept;
	float actual(dcon::trade_route_id route) const noexcept;
	float scale(dcon::trade_route_id route) const noexcept;
	float requested(dcon::trade_route_id route, dcon::commodity_id commodity) const noexcept;
	float actual(dcon::trade_route_id route, dcon::commodity_id commodity) const noexcept;
	float scale(dcon::trade_route_id route, dcon::commodity_id commodity) const noexcept;
	float requested_capacity(dcon::market_id market) const noexcept;
	float scale(dcon::market_id market) const noexcept;
	float requested_capacity(dcon::market_id market, transport_mode mode) const noexcept;
	float scale(dcon::market_id market, transport_mode mode) const noexcept;
};

// Clears every route together because routes compete for shared endpoint
// capacity. Commodity scarcity has already been allocated by market clearing
// and enters here through actual_probability_to_buy.
shipment_allocation clear_trade_shipments(sys::state const& state);

// Converts a network clearing result back into the existing congestion signals
// used by merchant expansion and per-unit transport pricing.
capacity_result evaluate_shipment_capacity(
	capacity_config const& config,
	float requested_cargo,
	float actual_cargo);
capacity_result evaluate_route_shipment_capacity(
	sys::state const& state,
	shipment_allocation const& allocation,
	dcon::trade_route_id route);

} // namespace economy::world_trade
