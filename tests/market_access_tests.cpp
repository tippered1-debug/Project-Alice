#include "economy/market_access.hpp"

#include "catch2/catch.hpp"

#include <cmath>
#include <limits>

namespace market_access = economy::market_access;

TEST_CASE("railways improve provincial access and lower the freight wedge",
		"[economy][logistics][market-access]") {
	market_access::inputs remote{};
	remote.distance_to_market_hub = 600.0f;
	remote.control_ratio = 1.0f;
	remote.transport_labor_availability = 1.0f;
	auto connected = remote;
	connected.railway_level = 5.0f;

	auto const remote_result = market_access::evaluate(remote);
	auto const connected_result = market_access::evaluate(connected);
	REQUIRE(connected_result.effective_distance < remote_result.effective_distance);
	REQUIRE(connected_result.access > remote_result.access);
	REQUIRE(connected_result.freight_cost_multiplier
		< remote_result.freight_cost_multiplier);
}

TEST_CASE("occupation staffing and congestion jointly constrain market access",
		"[economy][logistics][market-access]") {
	market_access::inputs healthy{};
	healthy.distance_to_market_hub = 100.0f;
	healthy.railway_level = 2.0f;
	healthy.control_ratio = 1.0f;
	healthy.transport_labor_availability = 1.0f;

	auto disrupted = healthy;
	disrupted.control_ratio = 0.2f;
	disrupted.transport_labor_availability = 0.1f;
	disrupted.local_capacity_utilization = 2.0f;

	auto const healthy_result = market_access::evaluate(healthy);
	auto const disrupted_result = market_access::evaluate(disrupted);
	REQUIRE(disrupted_result.access < healthy_result.access);
	REQUIRE(disrupted_result.handling_loss > healthy_result.handling_loss);
	REQUIRE(disrupted_result.access >= 0.05f);
	REQUIRE(disrupted_result.handling_loss <= 0.03f);
}

TEST_CASE("market access sanitizes invalid inputs", "[economy][logistics][market-access]") {
	market_access::inputs invalid{};
	invalid.distance_to_market_hub = std::numeric_limits<float>::infinity();
	invalid.control_ratio = std::numeric_limits<float>::quiet_NaN();
	invalid.transport_labor_availability = -1.0f;
	auto const result = market_access::evaluate(invalid);
	REQUIRE(std::isfinite(result.access));
	REQUIRE(std::isfinite(result.freight_cost_multiplier));
	REQUIRE(result.access >= 0.05f);
	REQUIRE(result.access <= 1.0f);
}
