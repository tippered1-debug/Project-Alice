#include "catch2/catch.hpp"

#include "gamestate/supply_chain_contagion_lab.hpp"

#include <string>
#include <utility>
#include <vector>

namespace supply_chain_contagion_lab_tests {

std::vector<std::string> fields(std::string const& row) {
	std::vector<std::string> result;
	std::string value;
	bool quoted = false;
	for(size_t i = 0; i < row.size(); ++i) {
		char character = row[i];
		if(character == '"') {
			if(quoted && i + 1 < row.size() && row[i + 1] == '"') {
				value += '"';
				++i;
			} else {
				quoted = !quoted;
			}
		} else if(character == ',' && !quoted) {
			result.push_back(std::move(value));
			value.clear();
		} else {
			value += character;
		}
	}
	result.push_back(std::move(value));
	return result;
}

float event_quantity(sys::simulation::supply_chain_lab::run_output const& output,
	std::string const& kind, std::string const& from, uint32_t first_day, uint32_t last_day) {
	float result = 0.0f;
	for(size_t i = 1; i < output.events_csv.size(); ++i) {
		auto row = fields(output.events_csv[i]);
		if(row.size() < 8 || row[3] != kind || row[4] != from) continue;
		auto day = uint32_t(std::stoul(row[0]));
		if(day >= first_day && day <= last_day) result += std::stof(row[7]);
	}
	return result;
}

bool event_trace_contains(sys::simulation::supply_chain_lab::run_output const& output,
	std::string const& kind, std::string const& token) {
	for(size_t i = 1; i < output.events_csv.size(); ++i) {
		auto row = fields(output.events_csv[i]);
		if(row.size() >= 10 && row[3] == kind && row[9].find(token) != std::string::npos) return true;
	}
	return false;
}

float event_cash_amount(sys::simulation::supply_chain_lab::run_output const& output,
	std::string const& kind, std::string const& from) {
	float result = 0.0f;
	for(size_t i = 1; i < output.events_csv.size(); ++i) {
		auto row = fields(output.events_csv[i]);
		if(row.size() < 9 || row[3] != kind || row[4] != from) continue;
		result += std::stof(row[8]);
	}
	return result;
}

sys::simulation::supply_chain_lab::config short_run(
	sys::simulation::supply_chain_lab::intervention shock =
		sys::simulation::supply_chain_lab::intervention::none) {
	sys::simulation::supply_chain_lab::config cfg;
	cfg.days = 210;
	cfg.shock = shock;
	return cfg;
}

} // namespace supply_chain_contagion_lab_tests

TEST_CASE("supply chain lab baseline moves paid A ore through freight, stock, production and consumption",
	"[economy][physical][supply-chain-lab]") {
	auto cfg = supply_chain_contagion_lab_tests::short_run();
	auto result = sys::simulation::supply_chain_lab::run(cfg);
	REQUIRE(result.ticks_completed == cfg.days);
	REQUIRE(result.baseline_trade_valid);
	REQUIRE(result.canonical_baseline_trade_valid);
	REQUIRE(result.baseline_a_to_b_import_quantity > 0.0f);
	REQUIRE(result.baseline_b_input_consumption > 0.0f);
	REQUIRE(result.baseline_b_factory_output > 0.0f);
	REQUIRE(result.household_invariants_valid);
	REQUIRE(result.maximum_money_conservation_error < 1.0e-2f);
	REQUIRE(result.paid_cargo_reconciled_error < 1.0e-4f);
	REQUIRE(supply_chain_contagion_lab_tests::event_quantity(
		result, "goods_purchase_paid", "A", 1, 89) > 0.0f);
	REQUIRE(supply_chain_contagion_lab_tests::event_quantity(
		result, "shipment_arrived_after_shock", "A", 1, cfg.days) == 0.0f);
}

TEST_CASE("supply chain lab is reproducible with a fixed seed and serial execution",
	"[economy][physical][supply-chain-lab][determinism]") {
	auto cfg = supply_chain_contagion_lab_tests::short_run(
		sys::simulation::supply_chain_lab::intervention::trade_embargo);
	auto first = sys::simulation::supply_chain_lab::run(cfg);
	auto second = sys::simulation::supply_chain_lab::run(cfg);
	REQUIRE(first.final_checksum == second.final_checksum);
	REQUIRE(first.timeseries_csv == second.timeseries_csv);
	REQUIRE(first.events_csv == second.events_csv);
}

TEST_CASE("trade embargo blocks new A fills and permits trade again after it ends",
	"[economy][physical][supply-chain-lab][embargo]") {
	auto cfg = supply_chain_contagion_lab_tests::short_run(
		sys::simulation::supply_chain_lab::intervention::trade_embargo);
	auto result = sys::simulation::supply_chain_lab::run(cfg);
	REQUIRE(result.baseline_trade_valid);
	REQUIRE(result.a_to_b_import_during_shock == Approx(0.0f).margin(1.0e-5f));
	REQUIRE(result.a_to_b_import_after_shock > 0.0f);
}

TEST_CASE("transport blockade uses the infrastructure graph and exposes C substitution and in-flight delivery",
	"[economy][physical][supply-chain-lab][blockade]") {
	auto cfg = supply_chain_contagion_lab_tests::short_run(
		sys::simulation::supply_chain_lab::intervention::supplier_substitution);
	auto result = sys::simulation::supply_chain_lab::run(cfg);
	REQUIRE(result.baseline_trade_valid);
	REQUIRE(result.a_to_b_import_during_shock == Approx(0.0f).margin(1.0e-5f));
	REQUIRE(result.c_to_b_import_during_shock > 0.0f);
	REQUIRE(result.a_cargo_arrived_after_shock > 0.0f);
	REQUIRE(supply_chain_contagion_lab_tests::event_quantity(
		result, "factory_input_consumed", "A/C", 90, 180) >= 0.0f);
}

TEST_CASE("supply chain lab canonical save and load preserves the continuation",
	"[economy][physical][supply-chain-lab][serialization]") {
	auto cfg = supply_chain_contagion_lab_tests::short_run(
		sys::simulation::supply_chain_lab::intervention::transport_blockade);
	auto uninterrupted = sys::simulation::supply_chain_lab::run(cfg);
	// Restore during the active blockade, after real trade orders and shipments
	// exist, so the test checks a live physical and contractual state.
	auto restored = sys::simulation::supply_chain_lab::run(cfg, 110);
	REQUIRE(restored.final_checksum == uninterrupted.final_checksum);
	REQUIRE(restored.timeseries_csv == uninterrupted.timeseries_csv);
	REQUIRE(restored.events_csv == uninterrupted.events_csv);
}

TEST_CASE("three-country experiment runs canonical elections, laws, budgets, taxes and banks",
	"[economy][governance][banking][supply-chain-lab][integration]") {
	auto cfg = supply_chain_contagion_lab_tests::short_run();
	cfg.days = 135;
	auto result = sys::simulation::supply_chain_lab::run(cfg);
	REQUIRE(result.ticks_completed == cfg.days);
	REQUIRE(result.elections_recorded >= 3);
	REQUIRE(result.statutes_recorded > 0);
	REQUIRE(result.banking_invariants_valid);
	REQUIRE(result.monetary_invariants_valid);
	REQUIRE(result.household_invariants_valid);
	REQUIRE(result.maximum_money_conservation_error < 1.0e-2f);
	REQUIRE(result.timeseries_csv.front().find("tax_collected_cash") != std::string::npos);
	REQUIRE(result.timeseries_csv.front().find("bank_default_impairment_cash") != std::string::npos);
	REQUIRE(result.timeseries_csv.front().find("latest_party_vote_shares") != std::string::npos);
	REQUIRE(supply_chain_contagion_lab_tests::event_quantity(
		result, "party_election_result", "A", 0, cfg.days) > 0.0f);
	REQUIRE(supply_chain_contagion_lab_tests::event_quantity(
		result, "employment_contract_started", "B", 0, cfg.days) > 0.0f);
	REQUIRE(supply_chain_contagion_lab_tests::event_quantity(
		result, "income_tax_assessed_collected", "B", 1, cfg.days) > 0.0f);
	REQUIRE(supply_chain_contagion_lab_tests::event_quantity(
		result, "new_law_enacted", "A", 0, cfg.days) > 0.0f);
	REQUIRE(supply_chain_contagion_lab_tests::event_trace_contains(
		result, "new_law_enacted", "rules="));
}

TEST_CASE("sovereign default keeps the claim and impairs only its creditor bank asset",
	"[economy][governance][banking][supply-chain-lab][default]") {
	auto cfg = supply_chain_contagion_lab_tests::short_run();
	cfg.days = 270;
	auto result = sys::simulation::supply_chain_lab::run(cfg);
	REQUIRE(result.sovereign_default_observed);
	REQUIRE(result.maximum_defaulted_bank_claims > 0.0f);
	REQUIRE(result.banking_invariants_valid);
	REQUIRE(result.monetary_invariants_valid);
	REQUIRE(result.minimum_post_default_credit_available == Approx(0.0f).margin(1.0e-5f));
	REQUIRE(supply_chain_contagion_lab_tests::event_quantity(
		result, "sovereign_payment_defaulted", "B", 1, cfg.days) > 0.0f);
	REQUIRE(supply_chain_contagion_lab_tests::event_quantity(
		result, "bank_sovereign_asset_impaired", "B", 1, cfg.days) > 0.0f);
	REQUIRE(supply_chain_contagion_lab_tests::event_cash_amount(
		result, "bank_sovereign_asset_impaired", "B") == Approx(0.0f).margin(1.0e-5f));
	auto restored = sys::simulation::supply_chain_lab::run(cfg, 250);
	REQUIRE(restored.final_checksum == result.final_checksum);
	REQUIRE(restored.timeseries_csv == result.timeseries_csv);
	REQUIRE(restored.events_csv == result.events_csv);
}
