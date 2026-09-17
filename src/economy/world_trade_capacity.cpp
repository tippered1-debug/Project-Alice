#include "world_trade_capacity.hpp"

#include "advanced_province_buildings.hpp"
#include "demographics.hpp"
#include "commodity_logistics.hpp"
#include "foreign_exchange.hpp"
#include "market_clearing.hpp"
#include "money.hpp"
#include "economy_stats.hpp"
#include "gamerule.hpp"
#include "price.hpp"
#include "province.hpp"
#include "province_templates.hpp"
#include "system_state.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace economy::world_trade {
namespace {

constexpr float maximum_reported_utilization = 1000.0f;
constexpr float minimum_transport_availability = 0.000001f;

float finite_nonnegative(float value) {
	return std::isfinite(value) && value > 0.0f ? value : 0.0f;
}

float unit_interval(float value) {
	return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}

float sanitized_minimum_expansion(float value) {
	return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.10f;
}

float sanitized_minimum_transport(float value) {
	return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.10f;
}

float sanitized_maximum_cost(float value) {
	return std::isfinite(value) ? std::clamp(value, 1.0f, 10.0f) : 3.0f;
}

float state_port_availability(sys::state const& state, dcon::state_instance_id state_instance) {
	if(!state_instance || !state.world.state_instance_is_valid(state_instance)) {
		return 0.0f;
	}
	if(state.world.province_get_service_price_size()
			<= uint32_t(services::list::port_capacity)
		|| state.world.province_get_service_satisfaction_size()
			<= uint32_t(services::list::port_capacity)
		|| state.world.province_get_advanced_province_building_max_private_size_size()
			<= uint32_t(advanced_province_buildings::list::civilian_ports)) {
		return 0.0f;
	}

	double total_weight = 0.0;
	double weighted_satisfaction = 0.0;
	province::for_each_province_in_state_instance(state, state_instance, [&](dcon::province_id province_id) {
		auto const raw_price = state.world.province_get_service_price(province_id, services::list::port_capacity);
		auto const price = finite_nonnegative(raw_price);
		auto const private_size = finite_nonnegative(
			state.world.province_get_advanced_province_building_max_private_size(
				province_id, advanced_province_buildings::list::civilian_ports));
		auto const weight = (100.0 + double(private_size))
			/ double(price_properties::service::epsilon + price);
		if(!std::isfinite(weight) || weight <= 0.0) {
			return;
		}
		auto const satisfaction = unit_interval(
			state.world.province_get_service_satisfaction(province_id, services::list::port_capacity));
		total_weight += weight;
		weighted_satisfaction += weight * double(satisfaction);
	});

	if(!std::isfinite(total_weight) || !std::isfinite(weighted_satisfaction) || total_weight <= 0.0) {
		return 0.0f;
	}
	return unit_interval(float(weighted_satisfaction / total_weight));
}

float state_land_availability(sys::state const& state, dcon::state_instance_id state_instance) {
	if(!state_instance || !state.world.state_instance_is_valid(state_instance)) {
		return 0.0f;
	}
	auto const capital = state.world.state_instance_get_capital(state_instance);
	if(!capital || !state.world.province_is_valid(capital)) {
		return 0.0f;
	}
	if(state.world.province_get_labor_demand_satisfaction_size()
			<= uint32_t(labor::no_education)) {
		return 0.0f;
	}
	return unit_interval(
		state.world.province_get_labor_demand_satisfaction(capital, labor::no_education));
}

template<typename ID>
float indexed_or(std::vector<float> const& values, ID id, float fallback) noexcept {
	if(!id) {
		return fallback;
	}
	auto const index = size_t(id.index());
	return index < values.size() ? values[index] : fallback;
}

} // namespace

capacity_result evaluate_capacity(capacity_config const& config, capacity_inputs const& inputs) {
	capacity_result result{};
	result.enabled = config.enabled;
	result.cargo = finite_nonnegative(inputs.cargo);
	result.nominal_capacity = std::min(
		finite_nonnegative(inputs.endpoint_capacity[0]),
		finite_nonnegative(inputs.endpoint_capacity[1]));
	result.transport_availability = std::min(
		unit_interval(inputs.endpoint_transport_availability[0]),
		unit_interval(inputs.endpoint_transport_availability[1]));
	result.effective_capacity = result.nominal_capacity * result.transport_availability;
	if(!std::isfinite(result.effective_capacity)) {
		result.effective_capacity = 0.0f;
	}

	result.headroom = std::max(0.0f, result.effective_capacity - result.cargo);
	result.shortfall = std::max(0.0f, result.cargo - result.effective_capacity);

	if(result.cargo > 0.0f) {
		if(result.effective_capacity > 0.0f) {
			result.utilization = std::clamp(
				result.cargo / result.effective_capacity, 0.0f, maximum_reported_utilization);
			result.congestion = std::clamp(
				1.0f - result.effective_capacity / result.cargo, 0.0f, 1.0f);
		} else {
			result.utilization = maximum_reported_utilization;
			result.congestion = 1.0f;
		}
	}

	if(config.enabled) {
		auto const minimum_expansion = sanitized_minimum_expansion(config.minimum_expansion_multiplier);
		auto const maximum_cost = sanitized_maximum_cost(config.maximum_transport_cost_multiplier);
		result.expansion_multiplier = std::clamp(1.0f - result.congestion, minimum_expansion, 1.0f);
		result.transport_cost_multiplier = std::clamp(
			1.0f + result.congestion * (maximum_cost - 1.0f), 1.0f, maximum_cost);
	}

	return result;
}

capacity_config ruleset_config_for(sys::state const& state) {
	capacity_config config{};
	config.enabled = gamerule::age_of_transformation_enabled(state);
	return config;
}

float nominal_capacity(sys::state const& state, dcon::market_id market,
		transport_mode mode) {
	if(!market || !state.world.market_is_valid(market))
		return 0.0f;
	auto const legacy_cap = finite_nonnegative(
		state.world.market_get_max_throughput(market));
	if(!gamerule::age_of_transformation_enabled(state))
		return legacy_cap;
	auto const state_instance =
		state.world.market_get_zone_from_local_market(market);
	if(!state_instance || !state.world.state_instance_is_valid(state_instance))
		return legacy_cap;
	// The nominal path is intentionally compatibility-only. Canonical movement
	// uses physical infrastructure below and never reads these aggregates.
	auto const population = state.world.state_instance_get_demographics_size()
		> uint32_t(demographics::total.index())
		? finite_nonnegative(state.world.state_instance_get_demographics(
			state_instance, demographics::total))
		: 0.0f;
	auto derived = 100.0f;
	switch(mode) {
	case transport_mode::land:
		derived += 1000.0f * float(military::state_railroad_level(state, state_instance))
			+ population / 50.0f;
		break;
	case transport_mode::sea:
		derived += 8000.0f * float(military::state_naval_base_level(state, state_instance))
			+ population / 200.0f;
		break;
	case transport_mode::local:
		derived += 500.0f * float(military::state_railroad_level(state, state_instance))
			+ population / 100.0f;
		break;
	}
	return legacy_cap > 0.0f ? std::min(legacy_cap, derived) : derived;
}

float canonical_capacity(sys::state const& state, dcon::market_id market,
		transport_mode mode) {
	if(!market || !state.world.market_is_valid(market))
		return 0.0f;
	auto const state_instance =
		state.world.market_get_zone_from_local_market(market);
	if(!state_instance || !state.world.state_instance_is_valid(state_instance))
		return 0.0f;
	// Canonical capacity is a physical resource budget. It deliberately does
	// not read market.max_throughput, population, route volume, clearing fill,
	// or any other nominal/aggregate market value.
	double railroad = 0.0;
	double naval_base = 0.0;
	double port_size = 0.0;
	state.world.for_each_province([&](dcon::province_id province) {
		if(state.world.province_get_state_membership(province) != state_instance) return;
		railroad += state.world.province_get_building_level(
			province, uint8_t(economy::province_building_type::railroad));
		naval_base += state.world.province_get_building_level(
			province, uint8_t(economy::province_building_type::naval_base));
		if(state.world.province_get_advanced_province_building_max_private_size_size()
			> uint32_t(advanced_province_buildings::list::civilian_ports)) {
			port_size += finite_nonnegative(state.world.province_get_advanced_province_building_max_private_size(
				province, advanced_province_buildings::list::civilian_ports));
		}
	});
	if(!std::isfinite(railroad) || !std::isfinite(naval_base) || !std::isfinite(port_size)) return 0.0f;
	double derived = 0.0;
	switch(mode) {
	case transport_mode::land:
		derived = 100.0 + 1000.0 * railroad;
		break;
	case transport_mode::sea:
		// Sea movement requires a real port or naval base; there is no synthetic
		// base capacity for a state with neither.
		if(naval_base <= 0.0 && port_size <= 0.0) return 0.0f;
		derived = 8000.0 * naval_base + (port_size > 0.0 ? 100.0 + port_size : 0.0);
		break;
	case transport_mode::local:
		derived = 100.0 + 500.0 * railroad;
		break;
	}
	return std::isfinite(derived) && derived > 0.0 ? float(derived) : 0.0f;
}

capacity_inputs inputs_for_route(sys::state const& state, dcon::trade_route_id route) {
	capacity_inputs inputs{};
	if(!route || !state.world.trade_route_is_valid(route)) {
		return inputs;
	}

	auto const market_a = state.world.trade_route_get_connected_markets(route, 0);
	auto const market_b = state.world.trade_route_get_connected_markets(route, 1);
	if(!market_a || !market_b || !state.world.market_is_valid(market_a) || !state.world.market_is_valid(market_b)) {
		return inputs;
	}

	auto const mode = state.world.trade_route_get_is_sea_route(route)
		? transport_mode::sea : transport_mode::land;
	inputs.endpoint_capacity = {
		nominal_capacity(state, market_a, mode),
		nominal_capacity(state, market_b, mode)};

	double cargo = 0.0;
	state.world.for_each_commodity([&](dcon::commodity_id commodity) {
		auto const volume = state.world.trade_route_get_volume(route, commodity);
		if(std::isfinite(volume)) {
			cargo += double(logistics::cargo_units(
				logistics::profile_for(state, commodity), std::abs(volume)));
		}
	});
	inputs.cargo = float(std::min(cargo, double(std::numeric_limits<float>::max())));

	auto const state_a = state.world.market_get_zone_from_local_market(market_a);
	auto const state_b = state.world.market_get_zone_from_local_market(market_b);
	if(state.world.trade_route_get_is_sea_route(route)) {
		inputs.endpoint_transport_availability = {
			state_port_availability(state, state_a),
			state_port_availability(state, state_b)};
	} else if(state.world.trade_route_get_is_land_route(route)) {
		inputs.endpoint_transport_availability = {
			state_land_availability(state, state_a),
			state_land_availability(state, state_b)};
	} else {
		inputs.endpoint_transport_availability = {0.0f, 0.0f};
	}

	return inputs;
}

capacity_result evaluate_route_capacity(sys::state const& state, dcon::trade_route_id route) {
	return evaluate_capacity(ruleset_config_for(state), inputs_for_route(state, route));
}

float shipment_allocation::requested(dcon::trade_route_id route) const noexcept {
	return indexed_or(requested_route_cargo, route, 0.0f);
}

float shipment_allocation::actual(dcon::trade_route_id route) const noexcept {
	return indexed_or(actual_route_cargo, route, 0.0f);
}

float shipment_allocation::scale(dcon::trade_route_id route) const noexcept {
	return indexed_or(route_scale, route, 1.0f);
}

float shipment_allocation::requested(dcon::trade_route_id route,
		dcon::commodity_id commodity) const noexcept {
	if(!route || !commodity || commodity_count == 0)
		return 0.f;
	auto const index = size_t(route.index()) * commodity_count + size_t(commodity.index());
	return index < requested_commodity_cargo.size()
		? requested_commodity_cargo[index] : 0.f;
}

float shipment_allocation::actual(dcon::trade_route_id route,
		dcon::commodity_id commodity) const noexcept {
	if(!route || !commodity || commodity_count == 0)
		return 0.f;
	auto const index = size_t(route.index()) * commodity_count + size_t(commodity.index());
	return index < actual_commodity_cargo.size()
		? actual_commodity_cargo[index] : 0.f;
}

float shipment_allocation::scale(dcon::trade_route_id route,
		dcon::commodity_id commodity) const noexcept {
	if(!route || !commodity || commodity_count == 0)
		return 1.f;
	auto const index = size_t(route.index()) * commodity_count + size_t(commodity.index());
	return index < commodity_scale.size() ? commodity_scale[index] : 1.f;
}

float shipment_allocation::requested_capacity(dcon::market_id market) const noexcept {
	return indexed_or(requested_market_capacity, market, 0.0f);
}

float shipment_allocation::scale(dcon::market_id market) const noexcept {
	return indexed_or(market_scale, market, 1.0f);
}

float shipment_allocation::requested_capacity(dcon::market_id market,
		transport_mode mode) const noexcept {
	return mode == transport_mode::sea
		? indexed_or(requested_sea_capacity, market, 0.0f)
		: indexed_or(requested_land_capacity, market, 0.0f);
}

float shipment_allocation::scale(dcon::market_id market,
		transport_mode mode) const noexcept {
	return mode == transport_mode::sea
		? indexed_or(sea_scale, market, 1.0f)
		: indexed_or(land_scale, market, 1.0f);
}

float shipment_allocation::import_settlement(dcon::nation_id nation) const noexcept {
	return indexed_or(nation_import_settlement, nation, 1.0f);
}

float shipment_allocation::exchange_rate_multiplier(dcon::nation_id nation) const noexcept {
	return indexed_or(nation_exchange_rate_multiplier, nation, 1.0f);
}

capacity_result evaluate_shipment_capacity(
		capacity_config const& config,
		float requested_cargo,
		float actual_cargo) {
	auto const requested = finite_nonnegative(requested_cargo);
	auto const actual = std::min(
		requested, finite_nonnegative(actual_cargo));
	return evaluate_capacity(config, {
		.cargo = requested,
		.endpoint_capacity = {actual, actual},
		.endpoint_transport_availability = {1.0f, 1.0f}});
}

capacity_result evaluate_route_shipment_capacity(
		sys::state const& state,
		shipment_allocation const& allocation,
		dcon::trade_route_id route) {
	return evaluate_shipment_capacity(
		ruleset_config_for(state),
		allocation.requested(route),
		allocation.actual(route));
}

shipment_allocation clear_trade_shipments(sys::state const& state) {
	shipment_allocation result{};
	auto const config = ruleset_config_for(state);
	result.enabled = config.enabled;
	result.requested_route_cargo.resize(state.world.trade_route_size(), 0.0f);
	result.actual_route_cargo.resize(state.world.trade_route_size(), 0.0f);
	result.route_scale.resize(state.world.trade_route_size(), 1.0f);
	result.commodity_count = state.world.commodity_size();
	auto const shipment_count = size_t(state.world.trade_route_size())
		* size_t(result.commodity_count);
	result.requested_commodity_cargo.resize(shipment_count, 0.0f);
	result.actual_commodity_cargo.resize(shipment_count, 0.0f);
	result.commodity_scale.resize(shipment_count, 1.0f);
	result.requested_market_capacity.resize(state.world.market_size(), 0.0f);
	result.market_scale.resize(state.world.market_size(), 1.0f);
	result.requested_land_capacity.resize(state.world.market_size(), 0.0f);
	result.requested_sea_capacity.resize(state.world.market_size(), 0.0f);
	result.land_scale.resize(state.world.market_size(), 1.0f);
	result.sea_scale.resize(state.world.market_size(), 1.0f);
	result.nation_import_settlement.resize(state.world.nation_size(), 1.0f);
	result.nation_exchange_rate_multiplier.resize(state.world.nation_size(), 1.0f);

	std::vector<float> land_availability(state.world.market_size(), 0.0f);
	std::vector<float> port_availability(state.world.market_size(), 0.0f);
	state.world.for_each_market([&](dcon::market_id market) {
		auto const index = size_t(market.index());
		if(index >= land_availability.size()) {
			return;
		}
		auto const state_instance =
			state.world.market_get_zone_from_local_market(market);
		land_availability[index] =
			state_land_availability(state, state_instance);
		port_availability[index] =
			state_port_availability(state, state_instance);
	});

	auto endpoint_availability = [&](dcon::trade_route_id route,
			dcon::market_id market) {
		auto const index = size_t(market.index());
		if(index >= land_availability.size()) {
			return 0.0f;
		}
		auto const state_instance =
			state.world.market_get_zone_from_local_market(market);
		if(!state_instance || !state.world.state_instance_is_valid(state_instance)) {
			return 0.0f;
		}
		auto const floor = sanitized_minimum_transport(
			config.minimum_transport_availability);
		// Match the live trade calculation: when both paths exist, the selected
		// sea route supplies port service and therefore owns the availability
		// signal used for this day's shipment.
		if(state.world.trade_route_get_is_sea_route(route)) {
			return std::max(floor, port_availability[index]);
		}
		if(state.world.trade_route_get_is_land_route(route)) {
			return std::max(floor, land_availability[index]);
		}
		return 0.0f;
	};

	// Pass one: turn desired orders into commodity-backed provisional cargo.
	// All demands in an origin market receive the same clearing fraction, so the
	// sum of these provisional exports cannot exceed the goods allocated to
	// trade by the commodity market.
	state.world.for_each_trade_route([&](dcon::trade_route_id route) {
		if(!route) {
			return;
		}
		auto const route_index = size_t(route.index());
		if(route_index >= result.route_scale.size()
				|| !state.world.trade_route_is_valid(route)
				|| (result.enabled
					&& state.world.trade_route_get_is_trade_forbidden(route))) {
			if(route_index < result.route_scale.size()) {
				result.route_scale[route_index] = 0.0f;
			}
			return;
		}
		auto const market_0 =
			state.world.trade_route_get_connected_markets(route, 0);
		auto const market_1 =
			state.world.trade_route_get_connected_markets(route, 1);
		if(!market_0 || !market_1
				|| !state.world.market_is_valid(market_0)
				|| !state.world.market_is_valid(market_1)) {
			result.route_scale[route_index] = 0.0f;
			return;
		}

		double requested = 0.0;
		state.world.for_each_commodity([&](dcon::commodity_id commodity) {
			if(state.world.commodity_get_money_rgo(commodity)) {
				return;
			}
			auto const volume =
				state.world.trade_route_get_volume(route, commodity);
			if(!std::isfinite(volume) || volume == 0.0f) {
				return;
			}
			auto const origin = volume > 0.0f ? market_0 : market_1;
			auto const fill = unit_interval(market_clearing::fill(
				state, origin, commodity, market_clearing::demand_class::trade));
			auto const commodity_requested =
				float(std::abs(double(volume)) * double(fill));
			requested += logistics::cargo_units(
				logistics::profile_for(state, commodity), commodity_requested);
			auto const shipment_index = route_index * result.commodity_count
				+ size_t(commodity.index());
			if(shipment_index < result.requested_commodity_cargo.size()) {
				result.requested_commodity_cargo[shipment_index] = commodity_requested;
				result.actual_commodity_cargo[shipment_index] = commodity_requested;
			}
		});

		auto const finite_requested = float(std::min(
			requested, double(std::numeric_limits<float>::max())));
		result.requested_route_cargo[route_index] = finite_requested;
		result.actual_route_cargo[route_index] = finite_requested;
	});

	if(!result.enabled) {
		return result;
	}

	// Pass two: build one capacity-constrained network. Cargo/availability is
	// the amount of nominal infrastructure consumed at an endpoint.
	auto const route_count = result.requested_route_cargo.size();
	auto const market_count = result.requested_market_capacity.size();
	auto const resource_count = market_count * 2u;
	std::vector<size_t> endpoint_resource_0(route_count, 0u);
	std::vector<size_t> endpoint_resource_1(route_count, 0u);
	std::vector<double> requirement_0(route_count, 0.0);
	std::vector<double> requirement_1(route_count, 0.0);
	std::vector<uint8_t> active(route_count, uint8_t(0));
	std::vector<std::vector<size_t>> touching_routes(resource_count);
	std::vector<double> requested_resource_capacity(resource_count, 0.0);

	state.world.for_each_trade_route([&](dcon::trade_route_id route) {
		auto const route_index = size_t(route.index());
		if(route_index >= route_count) {
			return;
		}
		auto const requested = result.requested_route_cargo[route_index];
		if(requested <= 0.0f) {
			return;
		}
		auto const market_0 =
			state.world.trade_route_get_connected_markets(route, 0);
		auto const market_1 =
			state.world.trade_route_get_connected_markets(route, 1);
		auto const availability_0 =
			endpoint_availability(route, market_0);
		auto const availability_1 =
			endpoint_availability(route, market_1);
		if(availability_0 <= minimum_transport_availability
				|| availability_1 <= minimum_transport_availability) {
			result.route_scale[route_index] = 0.0f;
			result.actual_route_cargo[route_index] = 0.0f;
			return;
		}

		auto const market_index_0 = size_t(market_0.index());
		auto const market_index_1 = size_t(market_1.index());
		if(market_index_0 >= market_count || market_index_1 >= market_count
				|| market_index_0 == market_index_1) {
			result.route_scale[route_index] = 0.0f;
			result.actual_route_cargo[route_index] = 0.0f;
			return;
		}

		auto const mode_offset = state.world.trade_route_get_is_sea_route(route)
			? market_count : 0u;
		endpoint_resource_0[route_index] = market_index_0 + mode_offset;
		endpoint_resource_1[route_index] = market_index_1 + mode_offset;
		requirement_0[route_index] =
			double(requested) / double(availability_0);
		requirement_1[route_index] =
			double(requested) / double(availability_1);
		active[route_index] = uint8_t(1);
		touching_routes[endpoint_resource_0[route_index]].push_back(route_index);
		touching_routes[endpoint_resource_1[route_index]].push_back(route_index);
		requested_resource_capacity[endpoint_resource_0[route_index]] +=
			requirement_0[route_index];
		requested_resource_capacity[endpoint_resource_1[route_index]] +=
			requirement_1[route_index];
		result.route_scale[route_index] = 0.0f;
	});

	state.world.for_each_market([&](dcon::market_id market) {
		auto const index = size_t(market.index());
		if(index >= market_count) {
			return;
		}
		auto const requested_land = requested_resource_capacity[index];
		auto const requested_sea = requested_resource_capacity[index + market_count];
		auto const requested_capacity = requested_land + requested_sea;
		auto const land_capacity = finite_nonnegative(
			nominal_capacity(state, market, transport_mode::land));
		auto const sea_capacity = finite_nonnegative(
			nominal_capacity(state, market, transport_mode::sea));
		result.requested_market_capacity[index] = float(std::min(
			requested_capacity, double(std::numeric_limits<float>::max())));
		result.requested_land_capacity[index] = float(std::min(
			requested_land, double(std::numeric_limits<float>::max())));
		result.requested_sea_capacity[index] = float(std::min(
			requested_sea, double(std::numeric_limits<float>::max())));
		result.land_scale[index] = requested_land > 0.0
			? unit_interval(float(double(land_capacity) / requested_land)) : 1.0f;
		result.sea_scale[index] = requested_sea > 0.0
			? unit_interval(float(double(sea_capacity) / requested_sea)) : 1.0f;
		result.market_scale[index] = std::min(
			result.land_scale[index], result.sea_scale[index]);
	});

	// Pass three: progressive filling (max-min fairness). Every active route
	// grows by the same fulfillment fraction until an endpoint saturates. Routes
	// touching that endpoint freeze, while unrelated routes continue and can use
	// the capacity that would otherwise be stranded behind another bottleneck.
	struct capacity_event {
		double level = 0.0;
		size_t market = 0;
		uint32_t generation = 0;
	};
	struct later_event {
		bool operator()(capacity_event const& left,
				capacity_event const& right) const noexcept {
			if(left.level != right.level) {
				return left.level > right.level;
			}
			return left.market > right.market;
		}
	};

	std::vector<double> active_load = requested_resource_capacity;
	std::vector<double> remaining_capacity(resource_count, 0.0);
	std::vector<double> last_level(resource_count, 0.0);
	std::vector<uint32_t> generation(resource_count, 0);
	std::priority_queue<capacity_event,
		std::vector<capacity_event>, later_event> events;

	for(size_t resource = 0; resource < resource_count; ++resource) {
		auto const market_index = resource % market_count;
		auto const market = dcon::market_id{
			dcon::market_id::value_base_t(market_index)};
		auto const mode = resource >= market_count
			? transport_mode::sea : transport_mode::land;
		remaining_capacity[resource] = double(finite_nonnegative(
			nominal_capacity(state, market, mode)));
		if(active_load[resource] > 0.0) {
			events.push({
				remaining_capacity[resource] / active_load[resource],
				resource,
				generation[resource]});
		}
	}

	auto advance_market = [&](size_t market_index, double level) {
		auto const delta = std::max(0.0,
			level - last_level[market_index]);
		remaining_capacity[market_index] = std::max(
			0.0,
			remaining_capacity[market_index]
				- delta * active_load[market_index]);
		last_level[market_index] = level;
	};

	auto reschedule_market = [&](size_t market_index, double level) {
		++generation[market_index];
		if(active_load[market_index] > 0.0) {
			events.push({
				level + remaining_capacity[market_index]
					/ active_load[market_index],
				market_index,
				generation[market_index]});
		}
	};

	double level = 0.0;
	while(!events.empty()) {
		auto const event = events.top();
		events.pop();
		if(event.market >= resource_count
				|| event.generation != generation[event.market]) {
			continue;
		}
		if(event.level >= 1.0) {
			break;
		}

		level = std::clamp(event.level, level, 1.0);
		advance_market(event.market, level);
		auto const& routes_at_bottleneck = touching_routes[event.market];
		for(auto const route_index : routes_at_bottleneck) {
			if(!active[route_index]) {
				continue;
			}

			active[route_index] = uint8_t(0);
			result.route_scale[route_index] = float(level);
			auto const market_index_0 = endpoint_resource_0[route_index];
			auto const market_index_1 = endpoint_resource_1[route_index];
			for(auto const endpoint : {
					std::pair{market_index_0, requirement_0[route_index]},
					std::pair{market_index_1, requirement_1[route_index]}}) {
				advance_market(endpoint.first, level);
				active_load[endpoint.first] = std::max(
					0.0, active_load[endpoint.first] - endpoint.second);
				reschedule_market(endpoint.first, level);
			}
		}
	}

	for(size_t route_index = 0; route_index < route_count; ++route_index) {
		if(active[route_index]) {
			result.route_scale[route_index] = 1.0f;
		}
		auto const requested = result.requested_route_cargo[route_index];
		auto const scale = unit_interval(result.route_scale[route_index]);
		result.route_scale[route_index] = scale;
		result.actual_route_cargo[route_index] = requested * scale;
		for(uint32_t commodity_index = 0;
				commodity_index < result.commodity_count; ++commodity_index) {
			auto const shipment_index = route_index * result.commodity_count
				+ size_t(commodity_index);
			result.commodity_scale[shipment_index] = scale;
			result.actual_commodity_cargo[shipment_index] =
				result.requested_commodity_cargo[shipment_index] * scale;
		}
		assert(std::isfinite(result.actual_route_cargo[route_index]));
		assert(result.actual_route_cargo[route_index] >= 0.0f);
		assert(result.actual_route_cargo[route_index] <= requested);
	}

	// Foreign purchases need a settlement asset. Net export receipts clear first;
	// only a bounded daily share of treasury/bank liquidity can finance the gap.
	// Domestic shipments bypass this layer entirely.
	std::vector<double> import_bills(state.world.nation_size(), 0.0);
	std::vector<double> export_receipts(state.world.nation_size(), 0.0);
	auto market_nation = [&](dcon::market_id market) {
		if(!market || !state.world.market_is_valid(market))
			return dcon::nation_id{};
		auto const state_instance =
			state.world.market_get_zone_from_local_market(market);
		if(!state_instance || !state.world.state_instance_is_valid(state_instance))
			return dcon::nation_id{};
		auto nation = state.world.state_instance_get_nation_from_state_ownership(
			state_instance);
		auto const capital = state.world.state_instance_get_capital(state_instance);
		if(capital && state.world.province_is_valid(capital)) {
			auto const controller =
				state.world.province_get_nation_from_province_control(capital);
			if(controller)
				nation = controller;
		}
		return nation;
	};

	state.world.for_each_trade_route([&](dcon::trade_route_id route) {
		auto const route_index = size_t(route.index());
		if(route_index >= route_count)
			return;
		auto const market_0 = state.world.trade_route_get_connected_markets(route, 0);
		auto const market_1 = state.world.trade_route_get_connected_markets(route, 1);
		state.world.for_each_commodity([&](dcon::commodity_id commodity) {
			auto const shipment_index = route_index * result.commodity_count
				+ size_t(commodity.index());
			if(shipment_index >= result.actual_commodity_cargo.size())
				return;
			auto const quantity = result.actual_commodity_cargo[shipment_index];
			if(quantity <= 0.0f)
				return;
			auto const volume = state.world.trade_route_get_volume(route, commodity);
			auto const origin = volume > 0.0f ? market_0 : market_1;
			auto const target = volume > 0.0f ? market_1 : market_0;
			auto const exporter = market_nation(origin);
			auto const importer = market_nation(target);
			if(!exporter || !importer || exporter == importer)
				return;
			auto const exporter_index = size_t(exporter.index());
			auto const importer_index = size_t(importer.index());
			if(exporter_index >= export_receipts.size()
					|| importer_index >= import_bills.size())
				return;
			auto const export_price = finite_nonnegative(
				state.world.market_get_price(origin, commodity));
			auto const import_price = finite_nonnegative(
				state.world.market_get_price(target, commodity));
			export_receipts[exporter_index] += double(quantity) * export_price;
			import_bills[importer_index] += double(quantity) * import_price;
		});
	});

	state.world.for_each_nation([&](dcon::nation_id nation) {
		auto const index = size_t(nation.index());
		if(index >= import_bills.size())
			return;
		auto const reserves = 0.0f;
		auto const settlement = foreign_exchange::evaluate({
			.enabled = true,
			.import_bill = float(std::min(import_bills[index],
				double(std::numeric_limits<float>::max()))),
			.export_receipts = float(std::min(export_receipts[index],
				double(std::numeric_limits<float>::max()))),
			.liquid_reserves = reserves});
		result.nation_import_settlement[index] = settlement.settlement_fraction;
		result.nation_exchange_rate_multiplier[index] =
			settlement.exchange_rate_multiplier;
	});

	state.world.for_each_trade_route([&](dcon::trade_route_id route) {
		auto const route_index = size_t(route.index());
		if(route_index >= route_count)
			return;
		auto const market_0 = state.world.trade_route_get_connected_markets(route, 0);
		auto const market_1 = state.world.trade_route_get_connected_markets(route, 1);
		double actual_cargo = 0.0;
		state.world.for_each_commodity([&](dcon::commodity_id commodity) {
			auto const shipment_index = route_index * result.commodity_count
				+ size_t(commodity.index());
			if(shipment_index >= result.actual_commodity_cargo.size())
				return;
			auto const volume = state.world.trade_route_get_volume(route, commodity);
			auto const origin = volume > 0.0f ? market_0 : market_1;
			auto const target = volume > 0.0f ? market_1 : market_0;
			auto const exporter = market_nation(origin);
			auto const importer = market_nation(target);
			auto settlement_scale = 1.0f;
			if(exporter && importer && exporter != importer)
				settlement_scale = result.import_settlement(importer);
			result.actual_commodity_cargo[shipment_index] *= settlement_scale;
			auto const requested = result.requested_commodity_cargo[shipment_index];
			result.commodity_scale[shipment_index] = requested > 0.0f
				? unit_interval(result.actual_commodity_cargo[shipment_index] / requested)
				: 1.0f;
			actual_cargo += logistics::cargo_units(
				logistics::profile_for(state, commodity),
				result.actual_commodity_cargo[shipment_index]);
		});
		result.actual_route_cargo[route_index] = float(std::min(
			actual_cargo, double(std::numeric_limits<float>::max())));
	});

#ifndef NDEBUG
	std::vector<double> used_capacity(resource_count, 0.0);
	for(size_t route_index = 0; route_index < route_count; ++route_index) {
		auto const scale = double(result.route_scale[route_index]);
		if(requirement_0[route_index] <= 0.0
				|| requirement_1[route_index] <= 0.0) {
			continue;
		}
		used_capacity[endpoint_resource_0[route_index]]
			+= requirement_0[route_index] * scale;
		used_capacity[endpoint_resource_1[route_index]]
			+= requirement_1[route_index] * scale;
	}
	for(size_t resource = 0; resource < resource_count; ++resource) {
		auto const market_index = resource % market_count;
		auto const market = dcon::market_id{
			dcon::market_id::value_base_t(market_index)};
		auto const mode = resource >= market_count
			? transport_mode::sea : transport_mode::land;
		auto const capacity = double(finite_nonnegative(
			nominal_capacity(state, market, mode)));
		auto const tolerance = std::max(0.001, capacity * 0.00001);
		assert(used_capacity[resource] <= capacity + tolerance);
	}
#endif

	return result;
}

} // namespace economy::world_trade
