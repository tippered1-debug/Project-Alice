#include "catch2/catch.hpp"

#include "gamestate/supply_chain_contagion_lab.hpp"

#include <sstream>
#include <string>
#include <vector>

namespace supply_chain_contagion_lab_tests {

std::vector<std::string> fields(std::string const& row) {
	std::vector<std::string> result;
	std::stringstream input(row);
	std::string value;
	while(std::getline(input, value, ',')) result.push_back(value);
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
	auto restored = sys::simulation::supply_chain_lab::run(cfg, 45);
	REQUIRE(restored.final_checksum == uninterrupted.final_checksum);
	REQUIRE(restored.timeseries_csv == uninterrupted.timeseries_csv);
	REQUIRE(restored.events_csv == uninterrupted.events_csv);
}
