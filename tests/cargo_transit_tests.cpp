#include "economy/cargo_transit.hpp"

#include "catch2/catch.hpp"

namespace cargo_transit = economy::cargo_transit;

TEST_CASE("cargo remains in transit and arrives over travel time",
		"[economy][trade][cargo-transit]") {
	auto const first_day = cargo_transit::advance({
		.enabled = true,
		.opening_cargo = 0.0f,
		.dispatched_cargo = 100.0f,
		.average_travel_days = 4.0f});
	REQUIRE(first_day.delivered_cargo == Approx(0.0f));
	REQUIRE(first_day.closing_cargo == Approx(100.0f));

	auto const second_day = cargo_transit::advance({
		.enabled = true,
		.opening_cargo = first_day.closing_cargo,
		.dispatched_cargo = 0.0f,
		.average_travel_days = 4.0f});
	REQUIRE(second_day.delivered_cargo == Approx(25.0f));
	REQUIRE(second_day.closing_cargo == Approx(75.0f));
}

TEST_CASE("transit spoilage conserves dispatched delivered stored and lost cargo",
		"[economy][trade][cargo-transit]") {
	auto const result = cargo_transit::advance({
		.enabled = true,
		.opening_cargo = 100.0f,
		.dispatched_cargo = 20.0f,
		.average_travel_days = 2.0f,
		.daily_spoilage = 0.10f});
	REQUIRE(result.delivered_cargo == Approx(50.0f));
	REQUIRE(result.spoiled_cargo == Approx(5.0f));
	REQUIRE(result.closing_cargo == Approx(65.0f));
	REQUIRE(result.delivered_cargo + result.spoiled_cargo
		+ result.closing_cargo == Approx(120.0f));
}

TEST_CASE("classic transit is an exact immediate-delivery no-op",
		"[economy][trade][cargo-transit]") {
	auto const result = cargo_transit::advance({
		.enabled = false,
		.opening_cargo = 7.0f,
		.dispatched_cargo = 20.0f,
		.average_travel_days = 50.0f,
		.daily_spoilage = 1.0f});
	REQUIRE(result.delivered_cargo == Approx(20.0f));
	REQUIRE(result.closing_cargo == Approx(7.0f));
	REQUIRE(result.spoiled_cargo == Approx(0.0f));
}
