#include "catch.hpp"
#include "economy/payroll.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/relations/relations.hpp"
#include "actors/organizations/organizations.hpp"

namespace {
struct payroll_fixture {
	std::unique_ptr<sys::state> state = std::make_unique<sys::state>();
	dcon::province_id province;
	dcon::commodity_id settlement;
	dcon::factory_id factory;
	dcon::monetary_account_id operator_account;

	payroll_fixture() {
		province = state->world.create_province();
		settlement = state->world.create_commodity();
		auto type = state->world.create_factory_type();
		state->world.factory_type_set_base_workforce(type, 1.0f);
		factory = state->world.create_factory();
		state->world.factory_set_building_type(factory, type);
		state->world.force_create_factory_location(factory, province);
		state->world.factory_set_canonical_production(factory, 1);
		state->world.factory_set_payroll_settlement(factory, settlement);
		state->world.factory_set_unqualified_employment(factory, 1.0f);
		state->world.province_set_labor_price(province, economy::labor::no_education, 100.0f);
		auto company = actors::organizations::create_company(*state);
		REQUIRE(actors::organizations::bind_factory_operator(*state, company, factory));
		operator_account = economy::accounts::open_account(*state, actors::organizations::actor_for_organization(*state, company), settlement);
	}
};
}

TEST_CASE("canonical payroll pays full wages in cash", "[economy][payroll]") {
	payroll_fixture fixture;
	REQUIRE(economy::accounts::bootstrap_set_balance(*fixture.state, fixture.operator_account, 500.0f));
	economy::payroll::begin_day(*fixture.state);
	economy::payroll::settle_factory(*fixture.state, fixture.factory, 1.0f, 1.0f);
	auto operator_actor = economy::accounts::owner_of(*fixture.state, fixture.operator_account);
	auto labor_relation = fixture.state->world.province_get_province_labor_clearing(fixture.province);
	auto labor_actor = fixture.state->world.province_labor_clearing_get_economic_actor(labor_relation);
	auto labor_account = economy::accounts::find_account(*fixture.state, labor_actor, fixture.settlement);
	REQUIRE(economy::accounts::balance(*fixture.state, fixture.operator_account) == Approx(400.0f));
	REQUIRE(economy::accounts::balance(*fixture.state, labor_account) == Approx(100.0f));
	REQUIRE(fixture.state->world.transaction_size() == 1);
	REQUIRE(fixture.state->world.transaction_get_kind(dcon::transaction_id{1}) == uint8_t(economy::relations::transaction_kind::payroll));
	REQUIRE(economy::relations::outstanding_between(*fixture.state, operator_actor, labor_actor, fixture.settlement) == Approx(0.0f));
}

TEST_CASE("canonical payroll records unpaid arrears without negative cash", "[economy][payroll]") {
	payroll_fixture fixture;
	REQUIRE(economy::accounts::bootstrap_set_balance(*fixture.state, fixture.operator_account, 60.0f));
	economy::payroll::settle_factory(*fixture.state, fixture.factory, 1.0f, 1.0f);
	auto labor_relation = fixture.state->world.province_get_province_labor_clearing(fixture.province);
	auto labor_actor = fixture.state->world.province_labor_clearing_get_economic_actor(labor_relation);
	REQUIRE(economy::accounts::balance(*fixture.state, fixture.operator_account) == Approx(0.0f));
	REQUIRE(economy::relations::outstanding_between(*fixture.state,
		economy::accounts::owner_of(*fixture.state, fixture.operator_account), labor_actor, fixture.settlement) == Approx(40.0f));
}
