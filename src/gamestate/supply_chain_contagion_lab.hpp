#pragma once

#include "dcon_generated.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sys { class state; }

namespace sys::simulation::supply_chain_lab {

enum class intervention : uint8_t {
	none,
	trade_embargo,
	transport_blockade,
	supplier_substitution
};

struct config {
	intervention shock = intervention::none;
	uint32_t seed = 424242;
	uint32_t days = 365;
	uint32_t shock_day = 90;
	uint32_t shock_end_day = 180;
	float a_dependency_share = 0.80f;
};

struct run_output {
	uint32_t ticks_completed = 0;
	uint64_t final_checksum = 0;
	std::vector<std::string> timeseries_csv;
	std::vector<std::string> events_csv;
	bool canonical_baseline_trade_valid = false;
	bool baseline_trade_valid = false;
	float baseline_a_to_b_import_quantity = 0.0f;
	float baseline_b_factory_output = 0.0f;
	float baseline_b_input_consumption = 0.0f;
	float a_to_b_import_during_shock = 0.0f;
	float c_to_b_import_during_shock = 0.0f;
	float a_to_b_import_after_shock = 0.0f;
	float b_factory_output_during_shock = 0.0f;
	float b_factory_output_after_shock = 0.0f;
	float a_cargo_arrived_after_shock = 0.0f;
	float paid_cargo_reconciled_error = 0.0f;
	float maximum_money_conservation_error = 0.0f;
	bool household_invariants_valid = true;
};

[[nodiscard]] intervention parse_intervention(std::string const&);
[[nodiscard]] std::string_view intervention_name(intervention) noexcept;
[[nodiscard]] run_output run(config const&, uint32_t save_restore_day = 0);
bool write_csv(run_output const&, std::string const& timeseries_path,
	std::string const& events_path);

} // namespace sys::simulation::supply_chain_lab
