#include "catch.hpp"
#include "economy/industrial_production.hpp"

TEST_CASE("canonical factory capital API validates and preserves legacy independence", "[economy][industrial][capital]") {
	auto state = std::make_unique<sys::state>();
	auto factory = state->world.create_factory();
	REQUIRE(::economy::industrial_production::set_productive_capacity(*state, factory, 10.0f));
	REQUIRE(::economy::industrial_production::set_productivity_factor(*state, factory, 1.25f));
	state->world.factory_set_size(factory, 100000.0f);
	state->world.factory_set_technology_scale(factory, 0.05f);
	REQUIRE(state->world.factory_get_productive_capacity(factory) == Approx(10.0f));
	REQUIRE(state->world.factory_get_productivity_factor(factory) == Approx(1.25f));
	REQUIRE_FALSE(::economy::industrial_production::set_productive_capacity(*state, factory, 0.0f));
	REQUIRE_FALSE(::economy::industrial_production::set_productivity_factor(*state, factory, -1.0f));
}

TEST_CASE("canonical factory bootstrap is one-time", "[economy][industrial][capital]") {
	auto state = std::make_unique<sys::state>();
	auto commodity = state->world.create_commodity();
	auto type = state->world.create_factory_type();
	state->world.factory_type_set_base_workforce(type, 10.0f);
	state->world.factory_type_set_output(type, commodity);
	state->world.factory_type_set_output_amount(type, 2.0f);
	auto factory = state->world.create_factory();
	state->world.factory_set_building_type(factory, type);
	state->world.factory_set_size(factory, 100.0f);
	state->world.factory_set_technology_scale(factory, 1.5f);
	::economy::industrial_production::bootstrap_factory(*state, factory);
	state->world.factory_set_size(factory, 100000.0f);
	state->world.factory_set_technology_scale(factory, 0.05f);
	::economy::industrial_production::bootstrap_factory(*state, factory);
	REQUIRE(state->world.factory_get_productive_capacity(factory) == Approx(10.0f));
	REQUIRE(state->world.factory_get_productivity_factor(factory) == Approx(1.5f));
}
