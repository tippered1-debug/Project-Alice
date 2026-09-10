#include "economy/price_level.hpp"

#include <limits>

namespace prices = economy::price_level;

TEST_CASE("consumer price index measures a fixed basket against base cost",
		"[economy][prices]") {
	REQUIRE(prices::calculate_index(100.0f, 100.0f) == Approx(1.0f));
	REQUIRE(prices::calculate_index(125.0f, 100.0f) == Approx(1.25f));
	REQUIRE(prices::calculate_index(75.0f, 100.0f) == Approx(0.75f));
}

TEST_CASE("daily inflation is a change in the consumer price index",
		"[economy][prices]") {
	REQUIRE(prices::calculate_daily_inflation(1.0f, 1.02f) == Approx(0.02f));
	REQUIRE(prices::calculate_daily_inflation(1.25f, 1.0f) == Approx(-0.20f));
}

TEST_CASE("real wages remove the local consumer price level",
		"[economy][prices]") {
	REQUIRE(prices::calculate_real_wage(2.0f, 1.0f) == Approx(2.0f));
	REQUIRE(prices::calculate_real_wage(2.0f, 1.25f) == Approx(1.6f));
}

TEST_CASE("price-level helpers sanitize invalid and degenerate inputs",
		"[economy][prices]") {
	auto const nan = std::numeric_limits<float>::quiet_NaN();
	REQUIRE(prices::calculate_index(nan, 10.0f) > 0.0f);
	REQUIRE(prices::calculate_index(10.0f, nan) == Approx(1.0f));
	REQUIRE(prices::calculate_daily_inflation(nan, 1.0f) == Approx(0.0f));
	REQUIRE(prices::calculate_real_wage(nan, 1.0f) == Approx(0.0f));
}

TEST_CASE("market CPI follows a local POP basket and deflates local wages",
		"[economy][prices][integration]") {
	auto state = std::make_unique<sys::state>();
	state->force_age_of_transformation_ruleset = true;
	auto const money = state->world.create_commodity();
	auto const food = state->world.create_commodity();
	auto const workers = state->world.create_pop_type();
	auto const local_state = state->world.create_state_instance();
	auto const market = state->world.create_market();
	auto const province = state->world.create_province();
	state->world.state_instance_set_market_from_local_market(local_state, market);
	state->world.market_set_zone_from_local_market(market, local_state);
	state->world.province_set_state_membership(province, local_state);

	state->world.pop_type_resize_life_needs(state->world.commodity_size());
	state->world.pop_type_resize_everyday_needs(state->world.commodity_size());
	state->world.market_resize_price(state->world.commodity_size());
	state->world.market_resize_life_needs_weights(state->world.commodity_size());
	state->world.market_resize_everyday_needs_weights(state->world.commodity_size());
	state->world.market_resize_aggregated_demand_history(state->world.commodity_size());
	state->world.market_resize_aggregated_supply_history(state->world.commodity_size());
	state->world.state_instance_resize_demographics(demographics::size(*state));
	state->world.province_resize_labor_price(economy::labor::total);

	state->world.commodity_set_cost(money, 1.0f);
	state->world.commodity_set_cost(food, 2.0f);
	state->world.pop_type_set_life_needs(workers, food, 1.0f);
	state->world.market_set_life_needs_weights(market, food, 1.0f);
	state->world.state_instance_set_demographics(local_state, demographics::total, 100.0f);
	state->world.state_instance_set_demographics(
		local_state, demographics::to_key(*state, workers), 100.0f);
	state->world.market_set_aggregated_demand_history(market, food, 120.0f);
	state->world.market_set_aggregated_supply_history(market, food, 80.0f);
	state->world.market_set_price(market, food, 2.0f);
	state->world.province_set_labor_price(
		province, economy::labor::no_education, 3.0f);

	prices::begin_day(*state);
	state->world.market_set_price(market, food, 3.0f);
	prices::update(*state);

	auto const result = state->price_level_account.markets[market.index()];
	REQUIRE(result.enabled);
	REQUIRE(result.cpi == Approx(1.5f));
	REQUIRE(result.daily_inflation == Approx(0.5f));
	REQUIRE(result.demand_pressure == Approx(0.2f));
	REQUIRE(prices::real_wage(*state, province, economy::labor::no_education)
		== Approx(2.0f));
}
