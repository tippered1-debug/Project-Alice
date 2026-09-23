#include "economy/market_clearing.hpp"
#include "system_state.hpp"

#include "catch2/catch.hpp"

#include <memory>

namespace market_clearing = economy::market_clearing;

TEST_CASE("market call auction respects reservation prices and priority", "[economy][market-clearing]") {
	std::vector<market_clearing::bid_order> bids{
		{5.0f, 20.0f, market_clearing::demand_class::life_needs, 0u},
		{10.0f, 5.0f, market_clearing::demand_class::luxury_needs, 1u}};
	std::vector<market_clearing::ask_order> asks{{8.0f, 10.0f, 0u}};

	auto const result = market_clearing::clear_call_auction(bids, asks, 9.0f);
	REQUIRE(result.quantity_traded == Approx(5.0f));
	REQUIRE(result.clearing_price == Approx(15.0f));
	REQUIRE(result.fill[size_t(market_clearing::demand_class::life_needs)] == Approx(1.0f));
	REQUIRE(result.fill[size_t(market_clearing::demand_class::luxury_needs)] == Approx(0.0f));
	REQUIRE(result.matches.size() == 1);
	REQUIRE(result.matches.front().quantity == Approx(10.0f));
	REQUIRE(result.matches.front().buyer_order == 0u);
	REQUIRE(result.matches.front().seller_order == 0u);
}

TEST_CASE("transformed market allocates scarce supply by economic purpose", "[economy][market-clearing]") {
	auto state = std::make_unique<sys::state>();
	state->force_age_of_transformation_ruleset = true;
	auto const market = state->world.create_market();
	auto const commodity = state->world.create_commodity();
	state->world.market_resize_actual_probability_to_buy(state->world.commodity_size());
	market_clearing::begin_day(*state);
	market_clearing::record(*state, market, commodity,
		market_clearing::demand_class::life_needs, 4.0f);
	market_clearing::record(*state, market, commodity,
		market_clearing::demand_class::luxury_needs, 6.0f);

	auto const result = market_clearing::settle(
		*state, market, commodity, 6.0f, 10.0f, 10.0f);
	REQUIRE(result.quantity_traded == Approx(6.0f));
	REQUIRE(result.aggregate_buy_fill == Approx(0.6f));
	REQUIRE(market_clearing::fill(*state, market, commodity,
		market_clearing::demand_class::life_needs) == Approx(1.0f));
	REQUIRE(market_clearing::fill(*state, market, commodity,
		market_clearing::demand_class::luxury_needs) == Approx(2.0f / 6.0f));
}

TEST_CASE("classic market clearing remains proportional", "[economy][market-clearing]") {
	auto state = std::make_unique<sys::state>();
	state->force_age_of_transformation_ruleset = false;
	auto const market = state->world.create_market();
	auto const commodity = state->world.create_commodity();
	market_clearing::begin_day(*state);

	auto const result = market_clearing::settle(
		*state, market, commodity, 3.0f, 5.0f, 10.0f);
	REQUIRE_FALSE(state->market_clearing_account.enabled);
	REQUIRE(result.quantity_traded == Approx(3.0f));
	REQUIRE(result.aggregate_buy_fill == Approx(0.6f));
	for(auto const class_fill : result.class_fill)
		REQUIRE(class_fill == Approx(0.6f));
}
