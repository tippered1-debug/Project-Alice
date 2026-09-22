#include "economy/commodity_logistics.hpp"
#include "system_state.hpp"

#include "catch2/catch.hpp"

#include <cmath>
#include <limits>
#include <memory>

namespace logistics = economy::logistics;

TEST_CASE("commodity logistics distinguishes bulk raw goods and local goods",
		"[economy][logistics]") {
	auto state = std::make_unique<sys::state>();
	auto const manufactured = state->world.create_commodity();
	auto const raw = state->world.create_commodity();
	auto const local = state->world.create_commodity();
	state->world.commodity_set_rgo_amount(raw, 2.0f);
	state->world.commodity_set_is_local(local, true);

	auto const manufactured_profile = logistics::profile_for(*state, manufactured);
	auto const raw_profile = logistics::profile_for(*state, raw);
	auto const local_profile = logistics::profile_for(*state, local);
	REQUIRE(raw_profile.cargo_weight > manufactured_profile.cargo_weight);
	REQUIRE(raw_profile.daily_storage_cost > manufactured_profile.daily_storage_cost);
	REQUIRE(raw_profile.target_inventory_days < manufactured_profile.target_inventory_days);
	REQUIRE(local_profile.cargo_weight == Approx(0.0f));
}

TEST_CASE("target inventories release shortages and decay without negative stock",
		"[economy][logistics][inventory]") {
	logistics::commodity_profile profile{};
	profile.target_inventory_days = 5.0f;
	profile.maximum_daily_release = 0.25f;
	profile.daily_spoilage = 0.0f;

	REQUIRE(logistics::target_inventory(profile, 10.0f) == Approx(50.0f));
	REQUIRE(logistics::inventory_release(profile, 100.0f, 10.0f, 10.0f)
		== Approx(50.0f / 90.0f));
	REQUIRE(logistics::inventory_release(profile, 100.0f, 10.0f, 0.0f)
		== Approx(10.0f));

	profile.daily_spoilage = 0.02f;
	REQUIRE(logistics::inventory_after_storage(profile, 100.0f) == Approx(98.0f));
	REQUIRE(logistics::inventory_after_storage(profile, -5.0f) == Approx(0.0f));

	profile.daily_spoilage = std::numeric_limits<float>::quiet_NaN();
	auto const sanitized = logistics::sanitize(profile);
	REQUIRE(std::isfinite(sanitized.daily_spoilage));
	REQUIRE(sanitized.daily_spoilage >= 0.0f);
}
