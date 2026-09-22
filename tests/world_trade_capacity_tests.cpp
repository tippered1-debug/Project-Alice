#include "economy/world_trade_capacity.hpp"
#include "economy/economy_constants.hpp"
#include "economy/economy_stats.hpp"
#include "economy/economy_trade_routes.hpp"
#include "gamerule/gamerule.hpp"
#include "system_state.hpp"

#include "catch2/catch.hpp"

#include <cmath>
#include <limits>
#include <memory>

namespace world_trade = economy::world_trade;

namespace {

struct land_trade_fixture {
	std::unique_ptr<sys::state> state;
	dcon::market_id market_a;
	dcon::market_id market_b;
	dcon::commodity_id commodity;
	dcon::trade_route_id route;
};

struct trade_buffer_snapshot {
	float payment_0 = 0.0f;
	float payment_1 = 0.0f;
	float export_0 = 0.0f;
	float import_1 = 0.0f;
};

land_trade_fixture make_land_trade_fixture() {
	land_trade_fixture fixture{};
	fixture.state = std::make_unique<sys::state>();
	auto const state_a = fixture.state->world.create_state_instance();
	auto const state_b = fixture.state->world.create_state_instance();
	fixture.market_a = fixture.state->world.create_market();
	fixture.market_b = fixture.state->world.create_market();
	auto const capital_a = fixture.state->world.create_province();
	auto const capital_b = fixture.state->world.create_province();
	fixture.commodity = fixture.state->world.create_commodity();

	fixture.state->world.market_set_zone_from_local_market(fixture.market_a, state_a);
	fixture.state->world.market_set_zone_from_local_market(fixture.market_b, state_b);
	fixture.state->world.state_instance_set_capital(state_a, capital_a);
	fixture.state->world.state_instance_set_capital(state_b, capital_b);
	fixture.state->world.market_set_max_throughput(fixture.market_a, 100.0f);
	fixture.state->world.market_set_max_throughput(fixture.market_b, 200.0f);

	fixture.state->world.market_resize_price(fixture.state->world.commodity_size());
	fixture.state->world.market_resize_actual_probability_to_buy(fixture.state->world.commodity_size());
	fixture.state->world.province_resize_labor_price(economy::labor::total);
	fixture.state->world.province_resize_labor_demand_satisfaction(economy::labor::total);
	fixture.state->world.market_set_price(fixture.market_a, fixture.commodity, 10.0f);
	fixture.state->world.market_set_price(fixture.market_b, fixture.commodity, 12.0f);
	fixture.state->world.market_set_actual_probability_to_buy(fixture.market_a, fixture.commodity, 1.0f);
	fixture.state->world.market_set_actual_probability_to_buy(fixture.market_b, fixture.commodity, 1.0f);
	fixture.state->world.province_set_labor_price(capital_a, economy::labor::no_education, 2.0f);
	fixture.state->world.province_set_labor_price(capital_b, economy::labor::no_education, 2.0f);
	fixture.state->world.province_set_labor_demand_satisfaction(
		capital_a, economy::labor::no_education, 0.5f);
	fixture.state->world.province_set_labor_demand_satisfaction(
		capital_b, economy::labor::no_education, 0.75f);

	fixture.route = fixture.state->world.force_create_trade_route(fixture.market_a, fixture.market_b);
	fixture.state->world.trade_route_set_is_land_route(fixture.route, true);
	fixture.state->world.trade_route_set_land_distance(fixture.route, 100.0f);
	fixture.state->world.trade_route_set_distance(fixture.route, 100.0f);
	fixture.state->world.trade_route_resize_volume(fixture.state->world.commodity_size());
	fixture.state->world.trade_route_set_volume(fixture.route, fixture.commodity, 80.0f);
	return fixture;
}

trade_buffer_snapshot collect_trade_payments(land_trade_fixture& fixture) {
	auto& state = *fixture.state;
	auto port_availability = state.world.market_make_vectorizable_float_buffer();
	auto port_price = state.world.market_make_vectorizable_float_buffer();
	auto export_tariff = state.world.market_make_vectorizable_float_buffer();
	auto import_tariff = state.world.market_make_vectorizable_float_buffer();
	auto payment_0 = state.world.trade_route_make_vectorizable_float_buffer();
	auto payment_1 = state.world.trade_route_make_vectorizable_float_buffer();
	auto tariff_0 = state.world.trade_route_make_vectorizable_float_buffer();
	auto tariff_1 = state.world.trade_route_make_vectorizable_float_buffer();

	for(auto market : {fixture.market_a, fixture.market_b}) {
		port_availability.set(market, 0.0f);
		port_price.set(market, 0.0f);
		export_tariff.set(market, 0.0f);
		import_tariff.set(market, 0.0f);
	}
	payment_0.set(fixture.route, 0.0f);
	payment_1.set(fixture.route, 0.0f);
	tariff_0.set(fixture.route, 0.0f);
	tariff_1.set(fixture.route, 0.0f);

	std::vector<ve::vectorizable_buffer<float, dcon::trade_route_id>> export_0;
	std::vector<ve::vectorizable_buffer<float, dcon::trade_route_id>> export_1;
	std::vector<ve::vectorizable_buffer<float, dcon::trade_route_id>> import_0;
	std::vector<ve::vectorizable_buffer<float, dcon::trade_route_id>> import_1;
	for(uint32_t index = 0; index < state.world.commodity_size(); ++index) {
		export_0.push_back(state.world.trade_route_make_vectorizable_float_buffer());
		export_1.push_back(state.world.trade_route_make_vectorizable_float_buffer());
		import_0.push_back(state.world.trade_route_make_vectorizable_float_buffer());
		import_1.push_back(state.world.trade_route_make_vectorizable_float_buffer());
		export_0.back().set(fixture.route, 0.0f);
		export_1.back().set(fixture.route, 0.0f);
		import_0.back().set(fixture.route, 0.0f);
		import_1.back().set(fixture.route, 0.0f);
	}

	economy::fill_trade_buffers(
		state,
		port_availability,
		port_price,
		export_tariff,
		import_tariff,
		payment_0,
		payment_1,
		tariff_0,
		tariff_1,
		export_0,
		export_1,
		import_0,
		import_1);

	auto const commodity_index = size_t(fixture.commodity.index());
	return {
		payment_0.get(fixture.route),
		payment_1.get(fixture.route),
		export_0[commodity_index].get(fixture.route),
		import_1[commodity_index].get(fixture.route)};
}

} // namespace

TEST_CASE("world trade capacity is an explicit legacy-compatible no-op", "[economy][trade][capacity]") {
	world_trade::capacity_config config{};
	world_trade::capacity_inputs inputs{};
	inputs.cargo = 200.0f;
	inputs.endpoint_capacity = {100.0f, 120.0f};
	inputs.endpoint_transport_availability = {1.0f, 1.0f};

	auto const result = world_trade::evaluate_capacity(config, inputs);
	REQUIRE_FALSE(result.enabled);
	REQUIRE(result.congestion == Approx(0.5f));
	REQUIRE(result.expansion_multiplier == Approx(1.0f));
	REQUIRE(result.transport_cost_multiplier == Approx(1.0f));
}

TEST_CASE("world trade capacity leaves uncongested routes unchanged", "[economy][trade][capacity]") {
	world_trade::capacity_config config{};
	config.enabled = true;
	world_trade::capacity_inputs inputs{};
	inputs.cargo = 40.0f;
	inputs.endpoint_capacity = {100.0f, 200.0f};
	inputs.endpoint_transport_availability = {0.8f, 0.5f};

	auto const result = world_trade::evaluate_capacity(config, inputs);
	REQUIRE(result.nominal_capacity == Approx(100.0f));
	REQUIRE(result.transport_availability == Approx(0.5f));
	REQUIRE(result.effective_capacity == Approx(50.0f));
	REQUIRE(result.utilization == Approx(0.8f));
	REQUIRE(result.congestion == Approx(0.0f));
	REQUIRE(result.headroom == Approx(10.0f));
	REQUIRE(result.shortfall == Approx(0.0f));
	REQUIRE(result.expansion_multiplier == Approx(1.0f));
	REQUIRE(result.transport_cost_multiplier == Approx(1.0f));
}

TEST_CASE("world trade congestion slows expansion and raises transport cost", "[economy][trade][capacity]") {
	world_trade::capacity_config config{};
	config.enabled = true;
	world_trade::capacity_inputs inputs{};
	inputs.cargo = 100.0f;
	inputs.endpoint_capacity = {100.0f, 80.0f};
	inputs.endpoint_transport_availability = {0.5f, 1.0f};

	auto const result = world_trade::evaluate_capacity(config, inputs);
	REQUIRE(result.effective_capacity == Approx(40.0f));
	REQUIRE(result.utilization == Approx(2.5f));
	REQUIRE(result.congestion == Approx(0.6f));
	REQUIRE(result.shortfall == Approx(60.0f));
	REQUIRE(result.expansion_multiplier == Approx(0.4f));
	REQUIRE(result.transport_cost_multiplier == Approx(2.2f));
}

TEST_CASE("world trade capacity sanitizes hostile inputs into bounded outputs", "[economy][trade][capacity]") {
	world_trade::capacity_config config{};
	config.enabled = true;
	config.minimum_expansion_multiplier = std::numeric_limits<float>::quiet_NaN();
	config.maximum_transport_cost_multiplier = std::numeric_limits<float>::infinity();
	world_trade::capacity_inputs inputs{};
	inputs.cargo = 25.0f;
	inputs.endpoint_capacity = {
		std::numeric_limits<float>::infinity(), -10.0f};
	inputs.endpoint_transport_availability = {
		std::numeric_limits<float>::quiet_NaN(), 5.0f};

	auto const result = world_trade::evaluate_capacity(config, inputs);
	REQUIRE(result.effective_capacity == Approx(0.0f));
	REQUIRE(result.utilization == Approx(1000.0f));
	REQUIRE(result.congestion == Approx(1.0f));
	REQUIRE(result.expansion_multiplier == Approx(0.1f));
	REQUIRE(result.transport_cost_multiplier == Approx(3.0f));
	REQUIRE(std::isfinite(result.utilization));
	REQUIRE(std::isfinite(result.expansion_multiplier));
	REQUIRE(std::isfinite(result.transport_cost_multiplier));
}

TEST_CASE("state-backed world trade capacity joins throughput cargo and labor availability",
	"[economy][trade][capacity][integration]") {
	auto state = std::make_unique<sys::state>();
	auto const rule = state->world.create_gamerule();
	state->hardcoded_gamerules.unused_gamerule = rule;
	state->world.gamerule_set_current_setting(
		rule, uint8_t(gamerule::age_of_transformation_settings::enabled));

	auto const state_a = state->world.create_state_instance();
	auto const state_b = state->world.create_state_instance();
	auto const market_a = state->world.create_market();
	auto const market_b = state->world.create_market();
	auto const capital_a = state->world.create_province();
	auto const capital_b = state->world.create_province();
	auto const commodity = state->world.create_commodity();
	state->world.market_set_zone_from_local_market(market_a, state_a);
	state->world.market_set_zone_from_local_market(market_b, state_b);
	state->world.state_instance_set_capital(state_a, capital_a);
	state->world.state_instance_set_capital(state_b, capital_b);
	state->world.market_set_max_throughput(market_a, 100.0f);
	state->world.market_set_max_throughput(market_b, 200.0f);
	state->world.province_resize_labor_demand_satisfaction(economy::labor::total);
	state->world.province_set_labor_demand_satisfaction(
		capital_a, economy::labor::no_education, 0.5f);
	state->world.province_set_labor_demand_satisfaction(
		capital_b, economy::labor::no_education, 0.75f);

	auto const route = state->world.force_create_trade_route(market_a, market_b);
	state->world.trade_route_set_is_land_route(route, true);
	state->world.trade_route_resize_volume(state->world.commodity_size());
	state->world.trade_route_set_volume(route, commodity, 80.0f);

	auto const result = world_trade::evaluate_route_capacity(*state, route);
	REQUIRE(result.enabled);
	REQUIRE(result.cargo == Approx(80.0f));
	REQUIRE(result.nominal_capacity == Approx(100.0f));
	REQUIRE(result.transport_availability == Approx(0.5f));
	REQUIRE(result.effective_capacity == Approx(50.0f));
	REQUIRE(result.utilization == Approx(1.6f));
	REQUIRE(result.congestion == Approx(0.375f));
	REQUIRE(result.expansion_multiplier == Approx(0.625f));
}

TEST_CASE("global shipment clearing is a legacy no-op and a hard transformed limit",
	"[economy][trade][capacity][clearing]") {
	auto fixture = make_land_trade_fixture();

	fixture.state->force_age_of_transformation_ruleset = false;
	auto const legacy = world_trade::clear_trade_shipments(*fixture.state);
	REQUIRE_FALSE(legacy.enabled);
	REQUIRE(legacy.requested(fixture.route) == Approx(80.0f));
	REQUIRE(legacy.actual(fixture.route) == Approx(80.0f));
	REQUIRE(legacy.scale(fixture.route) == Approx(1.0f));

	fixture.state->force_age_of_transformation_ruleset = true;
	auto const transformed =
		world_trade::clear_trade_shipments(*fixture.state);
	REQUIRE(transformed.enabled);
	REQUIRE(transformed.requested(fixture.route) == Approx(80.0f));
	REQUIRE(transformed.actual(fixture.route) == Approx(50.0f));
	REQUIRE(transformed.scale(fixture.route) == Approx(0.625f));
	REQUIRE(transformed.requested(fixture.route, fixture.commodity) == Approx(80.0f));
	REQUIRE(transformed.actual(fixture.route, fixture.commodity) == Approx(50.0f));
	REQUIRE(transformed.scale(fixture.route, fixture.commodity) == Approx(0.625f));
	REQUIRE(transformed.requested_capacity(fixture.market_a)
		== Approx(160.0f));
	REQUIRE(transformed.scale(fixture.market_a) == Approx(0.625f));
	auto const congestion = world_trade::evaluate_route_shipment_capacity(
		*fixture.state, transformed, fixture.route);
	REQUIRE(congestion.congestion == Approx(0.375f));
	REQUIRE(congestion.expansion_multiplier == Approx(0.625f));
	REQUIRE(congestion.transport_cost_multiplier == Approx(1.75f));
}

TEST_CASE("valid transport endpoints retain residual connectivity at zero staffing",
		"[economy][trade][capacity][clearing][connectivity]") {
	auto fixture = make_land_trade_fixture();
	fixture.state->force_age_of_transformation_ruleset = true;
	for(auto market : {fixture.market_a, fixture.market_b}) {
		auto const state_instance =
			fixture.state->world.market_get_zone_from_local_market(market);
		auto const capital =
			fixture.state->world.state_instance_get_capital(state_instance);
		fixture.state->world.province_set_labor_demand_satisfaction(
			capital, economy::labor::no_education, 0.f);
	}

	auto const allocation = world_trade::clear_trade_shipments(*fixture.state);
	// Market A has 100 nominal capacity and keeps 10% residual throughput.
	// A temporary zero in the labour market constrains the route; it no longer
	// deletes the connection and traps the market in a zero-demand feedback loop.
	REQUIRE(allocation.requested(fixture.route) == Approx(80.f));
	REQUIRE(allocation.actual(fixture.route) == Approx(10.f));
	REQUIRE(allocation.scale(fixture.route) == Approx(0.125f));
}

TEST_CASE("shipment clearing only transports commodity-backed orders",
	"[economy][trade][capacity][clearing]") {
	auto fixture = make_land_trade_fixture();
	fixture.state->force_age_of_transformation_ruleset = true;
	fixture.state->world.market_set_actual_probability_to_buy(
		fixture.market_a, fixture.commodity, 0.25f);

	auto const allocation =
		world_trade::clear_trade_shipments(*fixture.state);
	REQUIRE(allocation.requested(fixture.route) == Approx(20.0f));
	REQUIRE(allocation.actual(fixture.route) == Approx(20.0f));
	REQUIRE(allocation.scale(fixture.route) == Approx(1.0f));
	REQUIRE(allocation.requested(fixture.route, fixture.commodity) == Approx(20.0f));
	REQUIRE(allocation.actual(fixture.route, fixture.commodity) == Approx(20.0f));
}

TEST_CASE("progressive clearing reuses capacity stranded behind another bottleneck",
	"[economy][trade][capacity][clearing]") {
	auto state = std::make_unique<sys::state>();
	state->force_age_of_transformation_ruleset = true;
	auto const commodity = state->world.create_commodity();

	std::array<dcon::market_id, 3> markets{};
	for(auto& market : markets) {
		auto const state_instance = state->world.create_state_instance();
		auto const capital = state->world.create_province();
		market = state->world.create_market();
		state->world.market_set_zone_from_local_market(
			market, state_instance);
		state->world.state_instance_set_capital(
			state_instance, capital);
	}
	state->world.market_set_max_throughput(markets[0], 100.0f);
	state->world.market_set_max_throughput(markets[1], 20.0f);
	state->world.market_set_max_throughput(markets[2], 100.0f);
	state->world.market_resize_actual_probability_to_buy(
		state->world.commodity_size());
	state->world.province_resize_labor_demand_satisfaction(
		economy::labor::total);
	for(auto const market : markets) {
		auto const capital = state->world.state_instance_get_capital(
			state->world.market_get_zone_from_local_market(market));
		state->world.market_set_actual_probability_to_buy(
			market, commodity, 1.0f);
		state->world.province_set_labor_demand_satisfaction(
			capital, economy::labor::no_education, 1.0f);
	}

	auto const constrained_route =
		state->world.force_create_trade_route(markets[0], markets[1]);
	auto const open_route =
		state->world.force_create_trade_route(markets[0], markets[2]);
	state->world.trade_route_resize_volume(
		state->world.commodity_size());
	for(auto const route : {constrained_route, open_route}) {
		state->world.trade_route_set_is_land_route(route, true);
		state->world.trade_route_set_volume(route, commodity, 80.0f);
	}

	auto const allocation = world_trade::clear_trade_shipments(*state);
	REQUIRE(allocation.actual(constrained_route) == Approx(20.0f));
	REQUIRE(allocation.scale(constrained_route) == Approx(0.25f));
	REQUIRE(allocation.actual(open_route) == Approx(80.0f));
	REQUIRE(allocation.scale(open_route) == Approx(1.0f));
	REQUIRE(allocation.actual(constrained_route)
			+ allocation.actual(open_route)
		== Approx(100.0f));
}

TEST_CASE("Age of Transformation congestion reaches scalar trade payments while legacy stays unchanged",
	"[economy][trade][capacity][integration]") {
	auto fixture = make_land_trade_fixture();
	fixture.state->force_age_of_transformation_ruleset = false;
	auto const legacy = economy::explain_trade_route_commodity(
		*fixture.state, fixture.route, fixture.commodity);
	auto const legacy_capacity = world_trade::evaluate_route_capacity(*fixture.state, fixture.route);

	REQUIRE_FALSE(legacy_capacity.enabled);
	REQUIRE(legacy_capacity.transport_cost_multiplier == Approx(1.0f));
	REQUIRE(std::isfinite(legacy.transport_cost));
	REQUIRE(legacy.transport_cost > 0.0f);

	fixture.state->force_age_of_transformation_ruleset = true;
	auto const transformed_capacity = world_trade::evaluate_route_capacity(*fixture.state, fixture.route);
	auto const transformed = economy::explain_trade_route_commodity(
		*fixture.state, fixture.route, fixture.commodity);

	REQUIRE(transformed_capacity.enabled);
	REQUIRE(transformed_capacity.congestion == Approx(0.375f));
	REQUIRE(transformed_capacity.transport_cost_multiplier == Approx(1.75f));
	auto const legacy_effect = std::max(
		economy::trade_effect_of_scale_lower_bound,
		1.0f - 80.0f * economy::effect_of_transportation_scale);
	auto const transformed_effect = std::max(
		economy::trade_effect_of_scale_lower_bound,
		1.0f - 50.0f * economy::effect_of_transportation_scale);
	REQUIRE(transformed.transport_cost
		== Approx(legacy.transport_cost
			* transformed_effect / legacy_effect
			* transformed_capacity.transport_cost_multiplier));
	REQUIRE(transformed.payment_per_unit
		== Approx(legacy.payment_per_unit + transformed.transport_cost - legacy.transport_cost));
	REQUIRE(legacy.amount_origin == Approx(80.0f));
	REQUIRE(transformed.amount_origin == Approx(50.0f));
	REQUIRE(transformed.amount_target
		== Approx(legacy.amount_target * 0.625f));

	fixture.state->force_age_of_transformation_ruleset = false;
	auto const legacy_again = economy::explain_trade_route_commodity(
		*fixture.state, fixture.route, fixture.commodity);
	REQUIRE(legacy_again.transport_cost == Approx(legacy.transport_cost));
	REQUIRE(legacy_again.payment_per_unit == Approx(legacy.payment_per_unit));
}

TEST_CASE("Age of Transformation congestion reaches production trade payment buffers",
	"[economy][trade][capacity][integration]") {
	auto fixture = make_land_trade_fixture();
	fixture.state->force_age_of_transformation_ruleset = false;
	auto const legacy = collect_trade_payments(fixture);
	auto const legacy_detail = economy::explain_trade_route_commodity(
		*fixture.state, fixture.route, fixture.commodity);

	fixture.state->force_age_of_transformation_ruleset = true;
	auto const capacity = world_trade::evaluate_route_capacity(*fixture.state, fixture.route);
	auto const transformed = collect_trade_payments(fixture);
	auto const transformed_detail = economy::explain_trade_route_commodity(
		*fixture.state, fixture.route, fixture.commodity);

	REQUIRE(capacity.enabled);
	REQUIRE(capacity.transport_cost_multiplier == Approx(1.75f));
	REQUIRE(legacy.export_0 == Approx(legacy_detail.amount_origin));
	REQUIRE(legacy.import_1 == Approx(legacy_detail.amount_target));
	REQUIRE(legacy.payment_0 == Approx(
		legacy_detail.amount_origin
			* legacy_detail.payment_received_per_unit));
	REQUIRE(legacy.payment_1 == Approx(
		-legacy_detail.amount_origin
			* legacy_detail.payment_per_unit));
	REQUIRE(transformed.export_0
		== Approx(transformed_detail.amount_origin));
	REQUIRE(transformed.import_1
		== Approx(transformed_detail.amount_target));
	REQUIRE(transformed.payment_0 == Approx(
		transformed_detail.amount_origin
			* transformed_detail.payment_received_per_unit));
	REQUIRE(transformed.payment_1 == Approx(
		-transformed_detail.amount_origin
			* transformed_detail.payment_per_unit));
	REQUIRE(transformed.export_0 == Approx(50.0f));

	fixture.state->force_age_of_transformation_ruleset = false;
	auto const legacy_again = collect_trade_payments(fixture);
	REQUIRE(legacy_again.payment_0 == Approx(legacy.payment_0));
	REQUIRE(legacy_again.payment_1 == Approx(legacy.payment_1));
}
