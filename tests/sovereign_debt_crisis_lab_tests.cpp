#include "catch2/catch.hpp"

#include "gamestate/sovereign_debt_crisis_lab.hpp"

TEST_CASE("sovereign debt baseline services its bond and conserves account cash",
	"[governance][finance][sovereign-debt-lab]") {
	sys::simulation::sovereign_debt_lab::config cfg;
	auto result = sys::simulation::sovereign_debt_lab::run(cfg);
	REQUIRE(result.ticks_completed == cfg.days);
	REQUIRE_FALSE(result.debt_reached_default);
	REQUIRE(result.final_debt_outstanding == Approx(0.0f).margin(1.0e-4f));
	REQUIRE(result.total_public_service_shortfall == Approx(0.0f).margin(1.0e-4f));
	REQUIRE(result.maximum_money_conservation_error < 1.0e-2f);
	REQUIRE(result.final_bank_defaulted_debt_assets == Approx(0.0f).margin(1.0e-4f));
}

TEST_CASE("sovereign revenue shock defaults while austerity protects scheduled debt service",
	"[governance][finance][sovereign-debt-lab][crisis]") {
	sys::simulation::sovereign_debt_lab::config shock_cfg;
	shock_cfg.experiment = sys::simulation::sovereign_debt_lab::scenario::revenue_shock;
	auto shock = sys::simulation::sovereign_debt_lab::run(shock_cfg);
	REQUIRE(shock.debt_reached_default);
	REQUIRE(shock.final_defaulted_debt > 0.0f);
	REQUIRE(shock.total_public_service_shortfall > 0.0f);
	REQUIRE(shock.final_bank_defaulted_debt_assets > 0.0f);
	REQUIRE(shock.bank_net_worth_at_0pct_recovery < shock.final_bank_net_worth);
	REQUIRE(shock.bank_net_worth_at_0pct_recovery < 0.0f);
	REQUIRE(shock.bank_net_worth_at_100pct_recovery == Approx(shock.final_bank_net_worth));

	sys::simulation::sovereign_debt_lab::config austerity_cfg;
	austerity_cfg.experiment = sys::simulation::sovereign_debt_lab::scenario::austerity;
	auto austerity = sys::simulation::sovereign_debt_lab::run(austerity_cfg);
	REQUIRE_FALSE(austerity.debt_reached_default);
	REQUIRE(austerity.final_debt_outstanding == Approx(0.0f).margin(1.0e-4f));
	REQUIRE(austerity.total_public_service_shortfall == Approx(0.0f).margin(1.0e-4f));
}

TEST_CASE("sovereign debt crisis output is reproducible for a fixed seed",
	"[governance][finance][sovereign-debt-lab][determinism]") {
	sys::simulation::sovereign_debt_lab::config cfg;
	cfg.experiment = sys::simulation::sovereign_debt_lab::scenario::revenue_shock;
	auto first = sys::simulation::sovereign_debt_lab::run(cfg);
	auto second = sys::simulation::sovereign_debt_lab::run(cfg);
	REQUIRE(first.final_checksum == second.final_checksum);
	REQUIRE(first.timeseries_csv == second.timeseries_csv);
	REQUIRE(first.events_csv == second.events_csv);
}
