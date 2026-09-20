#include "economy/firm_agency.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/physical/factory_inputs.hpp"
#include "economy/physical/inventory.hpp"
#include "economy/physical/concrete_market.hpp"
#include "economy/physical/shipments.hpp"
#include "economy/industrial_production.hpp"
#include "economy/market_clearing.hpp"
#include "actors/organizations/organizations.hpp"
#include "compat/alice/legacy_bridge.hpp"
#include "economy/relations/relations.hpp"

namespace firm_agency_tests {
struct fixture {
	std::unique_ptr<sys::state> state = std::make_unique<sys::state>();
	dcon::province_id province{};
	dcon::state_instance_id zone{};
	dcon::market_id market{};
	dcon::commodity_id settlement{};
	dcon::commodity_id input{};
	dcon::commodity_id output{};
	dcon::factory_id factory{};
	dcon::site_id site{};
	dcon::economic_actor_id owner{};
	dcon::monetary_account_id account{};

	fixture() {
		province = state->world.create_province();
		zone = state->world.create_state_instance();
		market = state->world.create_market();
		state->world.state_instance_set_market_from_local_market(zone, market);
		state->world.market_set_zone_from_local_market(market, zone);
		state->world.province_set_state_membership(province, zone);
		settlement = state->world.create_commodity();
		input = state->world.create_commodity();
		output = state->world.create_commodity();
		for(auto commodity : {input, output}) {
			state->world.commodity_set_is_local(commodity, false);
			state->world.commodity_set_money_rgo(commodity, false);
		}
		state->world.market_resize_price(state->world.commodity_size());
		state->world.commodity_set_cost(input, 2.0f);
		state->world.commodity_set_cost(output, 10.0f);
		state->world.market_set_price(market, input, 2.0f);
		state->world.market_set_price(market, output, 10.0f);
		auto type = state->world.create_factory_type();
		state->world.factory_type_set_output(type, output);
		state->world.factory_type_set_output_amount(type, 1.0f);
		state->world.factory_type_set_base_workforce(type, 10.0f);
		economy::commodity_set recipe{};
		recipe.commodity_type[0] = input;
		recipe.commodity_amounts[0] = 1.0f;
		state->world.factory_type_set_inputs(type, recipe);
		factory = state->world.create_factory();
		state->world.factory_set_building_type(factory, type);
		state->world.factory_set_size(factory, 10.0f);
		state->world.factory_set_productive_capacity(factory, 10.0f);
		state->world.factory_set_productivity_factor(factory, 1.0f);
		state->world.factory_set_unqualified_employment(factory, 10.0f);
		state->world.force_create_factory_location(factory, province);
		compat::alice::bootstrap_factory_sites(*state);
		site = world::site::site_for_factory(*state, factory);
		auto company = actors::organizations::create_company(*state);
		REQUIRE(actors::organizations::bind_factory_operator(*state, company, factory));
		owner = actors::organizations::operator_actor_for_factory(*state, factory);
		account = economy::accounts::open_account(*state, owner, settlement);
		economy::accounts::bootstrap_set_balance(*state, account, 1000.0f);
		state->world.factory_set_canonical_production(factory, 1);
		state->world.factory_set_payroll_settlement(factory, settlement);
		state->world.province_resize_labor_price(economy::labor::total);
		state->world.province_resize_labor_demand_satisfaction(economy::labor::total);
		state->world.province_set_labor_price(province, economy::labor::no_education, 1.0f);
		state->world.province_set_labor_price(province, economy::labor::basic_education, 1.0f);
		state->world.province_set_labor_price(province, economy::labor::high_education, 1.0f);
	}
};
}

TEST_CASE("firm agency chooses positive production for profitable financed factory", "[economy][firm_agency]") {
	firm_agency_tests::fixture f;
	auto decision = economy::firm_agency::decide_factory(*f.state, f.factory);
	REQUIRE(decision.desired_units > 0.05f * 10.0f);
	REQUIRE(decision.desired_utilization > 0.5f);
	REQUIRE(decision.expected_gross_margin > 0.0f);
}

TEST_CASE("firm agency reduces production at negative expected margin", "[economy][firm_agency]") {
	firm_agency_tests::fixture f;
	auto healthy = economy::firm_agency::decide_factory(*f.state, f.factory);
	f.state->world.commodity_set_cost(f.input, 20.0f);
	auto negative = economy::firm_agency::decide_factory(*f.state, f.factory);
	REQUIRE(negative.desired_units < healthy.desired_units);
}

TEST_CASE("firm agency decisions ignore mutable legacy market prices", "[economy][firm_agency]") {
	firm_agency_tests::fixture f;
	auto baseline = economy::firm_agency::decide_factory(*f.state, f.factory);
	f.state->world.market_set_price(f.market, f.input, 100000.0f);
	f.state->world.market_set_price(f.market, f.output, 0.001f);
	auto mutated = economy::firm_agency::decide_factory(*f.state, f.factory);
	REQUIRE(mutated.desired_units == Approx(baseline.desired_units));
	REQUIRE(mutated.expected_gross_margin == Approx(baseline.expected_gross_margin));
	REQUIRE(mutated.expected_unit_revenue == Approx(baseline.expected_unit_revenue));
}

TEST_CASE("canonical procurement ignores mutable legacy market prices", "[economy][firm_agency][physical]") {
	auto run = [](float legacy_price) {
		firm_agency_tests::fixture f;
		auto hub = f.state->world.create_site();
		f.state->world.force_create_market_hub_site(f.market, hub);
		f.state->world.market_set_price(f.market, f.input, legacy_price);
		economy::physical::factory_inputs::begin_planning(*f.state);
		REQUIRE(economy::physical::factory_inputs::plan(*f.state, f.factory, f.site, f.owner,
			f.state->world.factory_type_get_inputs(f.state->world.factory_get_building_type(f.factory)),
			f.market, 4.0f));
		dcon::concrete_market_bid_id bid{};
		f.state->world.for_each_concrete_market_bid([&](auto candidate) { bid = candidate; });
		REQUIRE(bid);
		return std::pair{
			f.state->world.concrete_market_bid_get_remaining_quantity(bid),
			f.state->world.concrete_market_bid_get_limit_price(bid)};
	};

	auto low = run(1.0f);
	auto high = run(100000.0f);
	REQUIRE(low.first == Approx(high.first));
	REQUIRE(low.second == Approx(high.second));
	REQUIRE(low.second == Approx(2.0f));
}

TEST_CASE("canonical factory output ignores legacy intermediate clearing", "[economy][firm_agency][physical]") {
	auto run = [](float supply, bool stocked) {
		firm_agency_tests::fixture f;
		f.state->force_age_of_transformation_ruleset = true;
		for(auto labor_type : {economy::labor::no_education, economy::labor::basic_education,
			economy::labor::high_education})
			f.state->world.province_set_labor_demand_satisfaction(f.province, labor_type, 1.0f);
		if(stocked)
			REQUIRE(economy::physical::inventory::add(*f.state, f.site, f.input, 10.0f, f.owner) == Approx(10.0f));
		economy::market_clearing::begin_day(*f.state);
		economy::market_clearing::record(*f.state, f.market, f.input,
			economy::market_clearing::demand_class::intermediate, 1.0f);
		(void)economy::market_clearing::settle(*f.state, f.market, f.input, supply, 1.0f, 1.0f);
		return economy::industrial_production::produce_factory(*f.state, f.factory);
	};

	// The legacy ledger varies from empty to fully supplied, while physical
	// stock is held constant. Canonical output must not change.
	auto no_clearing = run(0.0f, true);
	auto quarter_clearing = run(0.25f, true);
	auto full_clearing = run(1.0f, true);
	REQUIRE(no_clearing == Approx(quarter_clearing));
	REQUIRE(no_clearing == Approx(full_clearing));

	// Physical inventory, unlike the legacy aggregate, remains causal.
	REQUIRE(run(0.0f, false) == Approx(0.0f));
	REQUIRE(run(0.0f, true) > 0.0f);
}

TEST_CASE("firm agency budgets production against free cash", "[economy][firm_agency]") {
	firm_agency_tests::fixture f;
	f.state->world.monetary_account_set_balance(f.account, 1.0f);
	auto decision = economy::firm_agency::decide_factory(*f.state, f.factory);
	REQUIRE(decision.desired_units < 10.0f);
	REQUIRE(decision.desired_units >= 0.0f);
	REQUIRE(economy::accounts::balance(*f.state, f.account) >= 0.0f);
}

TEST_CASE("firm agency responds to unsold output and recovers after sales", "[economy][firm_agency]") {
	firm_agency_tests::fixture f;
	auto healthy = economy::firm_agency::decide_factory(*f.state, f.factory);
	REQUIRE(economy::physical::inventory::add(*f.state, f.site, f.output, 100.0f, f.owner) == Approx(100.0f));
	auto suppressed = economy::firm_agency::decide_factory(*f.state, f.factory);
	REQUIRE(suppressed.desired_units < healthy.desired_units);
	REQUIRE(economy::physical::inventory::remove(*f.state, f.site, f.output, 100.0f, f.owner) == Approx(100.0f));
	REQUIRE(economy::firm_agency::decide_factory(*f.state, f.factory).desired_units > suppressed.desired_units);
}

TEST_CASE("firm agency counts owned output in transit as unsold exposure", "[economy][firm_agency]") {
	firm_agency_tests::fixture f;
	auto healthy = economy::firm_agency::decide_factory(*f.state, f.factory);
	auto transit_destination = f.state->world.create_site();
	REQUIRE(economy::physical::inventory::add(*f.state, f.site, f.output, 100.0f, f.owner) == Approx(100.0f));
	REQUIRE(economy::physical::shipments::dispatch(*f.state, f.site, transit_destination, f.output, 100.0f, f.owner));
	REQUIRE(economy::physical::inventory::quantity(*f.state, f.site, f.output, f.owner) == Approx(0.0f));
	auto pipeline = economy::firm_agency::decide_factory(*f.state, f.factory);
	REQUIRE(pipeline.output_inventory >= 100.0f);
	REQUIRE(pipeline.desired_units < healthy.desired_units);
}

TEST_CASE("firm agency attributes output transit to its factory site", "[economy][firm_agency]") {
	firm_agency_tests::fixture f;
	auto healthy = economy::firm_agency::decide_factory(*f.state, f.factory);
	auto other_source = f.state->world.create_site();
	REQUIRE(economy::physical::inventory::add(*f.state, other_source, f.output, 100.0f, f.owner) == Approx(100.0f));
	REQUIRE(economy::physical::shipments::dispatch(*f.state, other_source, f.site, f.output, 100.0f, f.owner));
	auto decision = economy::firm_agency::decide_factory(*f.state, f.factory);
	REQUIRE(decision.output_inventory == Approx(0.0f));
	REQUIRE(decision.desired_units == Approx(healthy.desired_units));
}

TEST_CASE("firm agency separates procurement and payroll accounts", "[economy][firm_agency]") {
	firm_agency_tests::fixture f;
	auto payroll_settlement = f.state->world.create_commodity();
	auto payroll_account = economy::accounts::open_account(*f.state, f.owner, payroll_settlement);
	economy::accounts::bootstrap_set_balance(*f.state, payroll_account, 1000.0f);
	f.state->world.factory_set_payroll_settlement(f.factory, payroll_settlement);
	f.state->world.monetary_account_set_balance(f.account, 0.0f);
	REQUIRE(economy::physical::inventory::add(*f.state, f.site, f.input, 100.0f, f.owner) == Approx(100.0f));
	auto decision = economy::firm_agency::decide_factory(*f.state, f.factory);
	REQUIRE(decision.desired_units > 0.0f);
}

TEST_CASE("firm agency does not use payroll-reserved bids for payroll", "[economy][firm_agency]") {
	firm_agency_tests::fixture f;
	auto payroll_settlement = f.state->world.create_commodity();
	auto payroll_account = economy::accounts::open_account(*f.state, f.owner, payroll_settlement);
	economy::accounts::bootstrap_set_balance(*f.state, payroll_account, 100.0f);
	f.state->world.factory_set_payroll_settlement(f.factory, payroll_settlement);
	REQUIRE(economy::physical::inventory::add(*f.state, f.site, f.input, 100.0f, f.owner) == Approx(100.0f));
	auto reserved = economy::physical::concrete_market::post_bid(*f.state, f.owner, payroll_account,
		f.site, f.market, f.input, 95.0f, 1.0f,
		economy::physical::concrete_market::order_purpose::general);
	REQUIRE(reserved);
	REQUIRE(economy::physical::concrete_market::reserved_bid_amount(*f.state, payroll_account) == Approx(95.0f));
	auto decision = economy::firm_agency::decide_factory(*f.state, f.factory);
	REQUIRE(decision.desired_units == Approx(0.0f));
}

TEST_CASE("firm agency gives payroll arrears conservative weight", "[economy][firm_agency]") {
	firm_agency_tests::fixture f;
	auto healthy = economy::firm_agency::decide_factory(*f.state, f.factory);
	auto labor = f.state->world.create_economic_actor();
	f.state->world.force_create_province_labor_clearing(f.province, labor);
	REQUIRE(economy::relations::create_obligation(*f.state, f.owner, labor, 950.0f, f.settlement,
		sys::date{}, sys::date{}, 0.0f, economy::relations::obligation_kind::payroll));
	auto conservative = economy::firm_agency::decide_factory(*f.state, f.factory);
	REQUIRE(conservative.desired_units < healthy.desired_units);
}

TEST_CASE("firm agency ignores legacy target utilization", "[economy][firm_agency]") {
	firm_agency_tests::fixture f;
	f.state->world.factory_set_target_utilization(f.factory, 1.0f);
	auto high = economy::firm_agency::decide_factory(*f.state, f.factory);
	f.state->world.factory_set_target_utilization(f.factory, 0.0f);
	auto low = economy::firm_agency::decide_factory(*f.state, f.factory);
	REQUIRE(low.desired_units == Approx(high.desired_units));
}

TEST_CASE("firm agency does not bid for input already owned", "[economy][firm_agency]") {
	firm_agency_tests::fixture f;
	economy::commodity_set recipe{};
	recipe.commodity_type[0] = f.input;
	recipe.commodity_amounts[0] = 1.0f;
	REQUIRE(economy::physical::inventory::add(*f.state, f.site, f.input, 100.0f, f.owner) == Approx(100.0f));
	economy::physical::factory_inputs::begin_planning(*f.state);
	REQUIRE(economy::physical::factory_inputs::plan(*f.state, f.factory, f.site, f.owner, recipe, f.market, 10.0f));
	REQUIRE(f.state->world.concrete_market_bid_size() == 0);
}

TEST_CASE("firm agency treats active procurement as committed rather than duplicate demand", "[economy][firm_agency]") {
	firm_agency_tests::fixture f;
	auto bid = economy::physical::concrete_market::post_bid(*f.state, f.owner, f.account, f.site,
		f.market, f.input, 10.0f, 50.0f, economy::physical::concrete_market::order_purpose::factory_input);
	REQUIRE(bid);
	f.state->world.force_create_concrete_bid_factory(bid, f.factory);
	auto decision = economy::firm_agency::decide_factory(*f.state, f.factory);
	REQUIRE(decision.cash_limited_units > 0.0f);
	REQUIRE(economy::physical::factory_inputs::active_factory_commitment(*f.state, f.factory, f.site, f.input) == Approx(10.0f));
	economy::commodity_set recipe{};
	recipe.commodity_type[0] = f.input;
	recipe.commodity_amounts[0] = 1.0f;
	economy::physical::factory_inputs::begin_planning(*f.state);
	REQUIRE(economy::physical::factory_inputs::plan(*f.state, f.factory, f.site, f.owner, recipe, f.market, 10.0f));
	REQUIRE(economy::physical::factory_inputs::planned_quantity(*f.state, f.factory, f.input, -1.0f) == Approx(0.0f));
	REQUIRE(f.state->world.concrete_market_bid_size() == 1);
}

TEST_CASE("firm agency partial procurement commitment leaves only incremental demand", "[economy][firm_agency]") {
	firm_agency_tests::fixture f;
	auto bid = economy::physical::concrete_market::post_bid(*f.state, f.owner, f.account, f.site,
		f.market, f.input, 4.0f, 2.0f, economy::physical::concrete_market::order_purpose::factory_input);
	REQUIRE(bid);
	f.state->world.force_create_concrete_bid_factory(bid, f.factory);
	economy::commodity_set recipe{};
	recipe.commodity_type[0] = f.input;
	recipe.commodity_amounts[0] = 1.0f;
	economy::physical::factory_inputs::begin_planning(*f.state);
	REQUIRE(economy::physical::factory_inputs::plan(*f.state, f.factory, f.site, f.owner, recipe, f.market, 10.0f));
	REQUIRE(economy::physical::factory_inputs::planned_quantity(*f.state, f.factory, f.input, -1.0f) == Approx(6.0f));
}

TEST_CASE("firm agency decision is deterministic", "[economy][firm_agency]") {
	firm_agency_tests::fixture f;
	auto first = economy::firm_agency::decide_factory(*f.state, f.factory);
	auto second = economy::firm_agency::decide_factory(*f.state, f.factory);
	REQUIRE(second.desired_units == Approx(first.desired_units));
	REQUIRE(second.expected_gross_margin == Approx(first.expected_gross_margin));
}

TEST_CASE("firm agency prefers concrete output price history over legacy price", "[economy][firm_agency]") {
	firm_agency_tests::fixture f;
	f.state->current_date = sys::date{10};
	auto seller = f.state->world.create_economic_actor();
	auto seller_account = economy::accounts::open_account(*f.state, seller, f.settlement);
	economy::accounts::bootstrap_set_balance(*f.state, seller_account, 0.0f);
	auto source = f.state->world.create_site();
	REQUIRE(economy::physical::inventory::add(*f.state, source, f.output, 2.0f, seller) == Approx(2.0f));
	REQUIRE(economy::physical::concrete_market::post_ask(*f.state, seller, source, f.market, f.output, 2.0f, 40.0f, {}) );
	REQUIRE(economy::physical::concrete_market::post_bid(*f.state, f.owner, f.account, f.site, f.market, f.output, 2.0f, 40.0f, {}));
	REQUIRE(economy::physical::concrete_market::match(*f.state, f.market, f.output, sys::date{10}).size() == 1);
	f.state->current_date = sys::date{11};
	f.state->world.market_set_price(f.market, f.output, 1.0f);
	auto decision = economy::firm_agency::decide_factory(*f.state, f.factory);
	REQUIRE(decision.expected_unit_revenue == Approx(40.0f));
}
