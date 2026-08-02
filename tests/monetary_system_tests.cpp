#include "economy/monetary_system.hpp"
#include "economy/advanced_province_buildings.hpp"
#include "gamerule.hpp"
#include "money.hpp"
#include "system_state.hpp"

#include <cmath>

namespace monetary = economy::monetary;

TEST_CASE("the ledger reports a residual without changing anything", "[economy][money]") {
	monetary::balance_inputs inputs;
	inputs.has_previous = true;
	inputs.previous_total = 1'000'000.0;
	inputs.observed_total = 1'400'000.0;
	inputs.gold_emission = 1'000.0;
	inputs.gross_total = 1'400'000.0;

	auto const result = monetary::calculate(inputs);
	REQUIRE(result.expected_total == Approx(1'001'000.0));
	REQUIRE(result.unaccounted == Approx(399'000.0));
	REQUIRE(result.net_to_gross == Approx(1.0));
}

TEST_CASE("the first observation establishes a baseline instead of a residual", "[economy][money]") {
	monetary::balance_inputs inputs;
	inputs.has_previous = false;
	inputs.observed_total = 5'000.0;
	inputs.gold_emission = 7.0;

	auto const result = monetary::calculate(inputs);
	REQUIRE(result.expected_total == Approx(5'000.0));
	REQUIRE(result.unaccounted == Approx(0.0));
}

TEST_CASE("gold emission is money creation, not a residual", "[economy][money]") {
	monetary::balance_inputs inputs;
	inputs.has_previous = true;
	inputs.previous_total = 100'000.0;
	inputs.observed_total = 100'500.0;
	inputs.gold_emission = 500.0;

	auto const result = monetary::calculate(inputs);
	REQUIRE(result.unaccounted == Approx(0.0));
}

TEST_CASE("money going missing is reported with the opposite sign", "[economy][money]") {
	monetary::balance_inputs inputs;
	inputs.has_previous = true;
	inputs.previous_total = 100'000.0;
	inputs.observed_total = 99'500.0;

	auto const result = monetary::calculate(inputs);
	REQUIRE(result.unaccounted == Approx(-500.0));
}

TEST_CASE("net to gross exposes offsetting claims", "[economy][money]") {
	// The regime actually observed on a real scenario: households hold millions
	// that producers owe, so the net supply is a rounding error between them.
	// This ratio is the signal that the net is not a meaningful price level.
	monetary::stocks residue;
	residue.pop_savings = 2'848'532.0;
	residue.producer_banks = -2'899'317.0;

	monetary::balance_inputs inputs;
	inputs.has_previous = true;
	inputs.previous_total = 0.0;
	inputs.observed_total = residue.total();
	inputs.gross_total = residue.gross();

	auto const result = monetary::calculate(inputs);
	REQUIRE(residue.gross() == Approx(5'747'849.0));
	REQUIRE(result.net_to_gross < 0.0);
	REQUIRE(std::abs(result.net_to_gross) < 0.01);
}

TEST_CASE("degenerate inputs stay finite", "[economy][money]") {
	monetary::balance_inputs nonfinite;
	nonfinite.has_previous = true;
	nonfinite.previous_total = std::numeric_limits<double>::infinity();
	nonfinite.observed_total = 100'000.0;
	nonfinite.gross_total = std::numeric_limits<double>::quiet_NaN();

	auto const result = monetary::calculate(nonfinite);
	REQUIRE(std::isfinite(result.previous_total));
	REQUIRE(std::isfinite(result.unaccounted));
	REQUIRE(std::isfinite(result.net_to_gross));
}

namespace {

struct monetary_fixture {
	std::unique_ptr<sys::state> state = std::make_unique<sys::state>();
	dcon::nation_id nation{};
	dcon::pop_id pop{};
	dcon::province_id province{};
	dcon::gamerule_id rule{};

	monetary_fixture() {
		state->world.create_commodity();
		nation = state->world.create_nation();
		pop = state->world.create_pop();
		state->world.nation_resize_stockpiles(state->world.commodity_size());
		state->world.market_resize_stockpile(state->world.commodity_size());
		rule = state->world.create_gamerule();
		state->hardcoded_gamerules.unused_gamerule = rule;

		province = state->world.create_province();
		state->world.province_resize_advanced_province_building_private_savings(
			advanced_province_buildings::list::total);

		state->world.pop_set_savings(pop, 1'000.f);
		state->world.nation_set_stockpiles(nation, economy::money, 2'000.f);
		state->world.nation_set_national_bank(nation, 3'000.f);
		state->world.nation_set_private_investment(nation, 3'000.f);
		state->world.province_set_rgo_bank(province, 500.f);
		state->world.province_set_factory_bank(province, 300.f);
		state->world.province_set_artisan_bank(province, 200.f);
	}

	void set_ruleset(bool enabled) {
		state->world.gamerule_set_current_setting(rule, uint8_t(enabled
			? gamerule::age_of_transformation_settings::enabled
			: gamerule::age_of_transformation_settings::disabled));
	}
};

} // namespace

TEST_CASE("measure sums every stock that can hold money", "[economy][money][integration]") {
	monetary_fixture fixture;
	auto const stocks = monetary::measure(*fixture.state);
	REQUIRE(stocks.pop_savings == Approx(1'000.0));
	REQUIRE(stocks.treasury == Approx(2'000.0));
	REQUIRE(stocks.national_bank == Approx(3'000.0));
	REQUIRE(stocks.private_investment == Approx(3'000.0));
	// Producers keep their own tills. Missing these was a real defect: a
	// transfer into one read as money vanishing from the world.
	REQUIRE(stocks.producer_banks == Approx(1'000.0));
	REQUIRE(stocks.total() == Approx(10'000.0));
}

TEST_CASE("gold emission is not treated as a leak", "[economy][money][integration]") {
	monetary_fixture fixture;
	fixture.set_ruleset(true);
	monetary::initialize(*fixture.state);

	monetary::begin_day(*fixture.state);
	// The money-RGO payout mints into a treasury and records what it minted.
	fixture.state->world.nation_set_stockpiles(fixture.nation, economy::money, 2'050.f);
	monetary::record_gold_emission(*fixture.state, 50.f);
	monetary::update(*fixture.state);

	auto const& ledger = fixture.state->monetary_account;
	REQUIRE(ledger.last_balance.gold_emission == Approx(50.0));
	REQUIRE(ledger.last_balance.unaccounted == Approx(0.0));
	// Legitimate growth of the supply is not reported as a leak.
	REQUIRE(ledger.previous_total == Approx(10'050.0));
}

TEST_CASE("the ledger never moves a balance", "[economy][money][integration]") {
	monetary_fixture fixture;
	fixture.set_ruleset(true);
	monetary::initialize(*fixture.state);

	// Money appears from nowhere. The ledger must report it and touch nothing:
	// correcting the supply was the wrong instrument, and the module no longer
	// owns one.
	fixture.state->world.nation_set_national_bank(fixture.nation, 9'000.f);
	monetary::begin_day(*fixture.state);
	monetary::update(*fixture.state);

	auto const& ledger = fixture.state->monetary_account;
	REQUIRE(ledger.last_balance.unaccounted == Approx(6'000.0));
	REQUIRE(fixture.state->world.pop_get_savings(fixture.pop) == Approx(1'000.f));
	REQUIRE(fixture.state->world.nation_get_national_bank(fixture.nation) == Approx(9'000.f));
	REQUIRE(fixture.state->world.province_get_rgo_bank(fixture.province) == Approx(500.f));
	// The legacy constant is handed back untouched, in every mode.
	REQUIRE(fixture.state->inflation == Approx(monetary::legacy_inflation));
}

TEST_CASE("a reloaded campaign resumes on the continuous run's books",
		"[economy][money][integration]") {
	monetary_fixture fixture;
	monetary::initialize(*fixture.state);
	monetary::begin_day(*fixture.state);
	monetary::update(*fixture.state);
	auto const continuous_total = fixture.state->monetary_account.previous_total;

	monetary::initialize(*fixture.state);
	REQUIRE(fixture.state->monetary_account.previous_total == Approx(continuous_total));
	REQUIRE(fixture.state->monetary_account.has_previous);
}

TEST_CASE("the phase audit attributes a change to the phase that made it",
		"[economy][money][integration]") {
	monetary_fixture fixture;
	fixture.state->money_audit.enabled = true;
	monetary::begin_day(*fixture.state);

	monetary::audit_phase(*fixture.state, "quiet phase");
	fixture.state->world.nation_set_national_bank(fixture.nation, 3'250.f);
	monetary::audit_phase(*fixture.state, "printing phase");

	auto const& phases = fixture.state->money_audit.phases;
	REQUIRE(phases.size() == 2);
	REQUIRE(phases[0].phase == "quiet phase");
	REQUIRE(phases[0].change() == Approx(0.0));
	REQUIRE(phases[1].phase == "printing phase");
	REQUIRE(phases[1].change() == Approx(250.0));
}
