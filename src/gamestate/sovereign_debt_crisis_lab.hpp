#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sys::simulation::sovereign_debt_lab {

enum class scenario : uint8_t { baseline, revenue_shock, austerity };

struct config {
	scenario experiment = scenario::baseline;
	uint32_t seed = 424242;
	uint32_t days = 540;
	uint32_t shock_day = 90;
};

struct run_output {
	scenario experiment = scenario::baseline;
	uint32_t seed = 424242;
	uint32_t ticks_completed = 0;
	uint64_t final_checksum = 0;
	float initial_money = 0.0f;
	float maximum_money_conservation_error = 0.0f;
	float final_debt_outstanding = 0.0f;
	float final_defaulted_debt = 0.0f;
	float total_public_service_paid = 0.0f;
	float total_public_service_shortfall = 0.0f;
	float final_bank_public_debt_assets = 0.0f;
	float final_bank_defaulted_debt_assets = 0.0f;
	float final_bank_net_worth = 0.0f;
	float bank_net_worth_at_0pct_recovery = 0.0f;
	float bank_net_worth_at_25pct_recovery = 0.0f;
	float bank_net_worth_at_50pct_recovery = 0.0f;
	float bank_net_worth_at_75pct_recovery = 0.0f;
	float bank_net_worth_at_100pct_recovery = 0.0f;
	uint8_t final_bank_status = 0;
	bool debt_reached_default = false;
	std::vector<std::string> timeseries_csv;
	std::vector<std::string> events_csv;
};

[[nodiscard]] scenario parse_scenario(std::string const&);
[[nodiscard]] std::string_view scenario_name(scenario) noexcept;
[[nodiscard]] run_output run(config const&);
bool write_outputs(run_output const&, std::string const& output_directory);

} // namespace sys::simulation::sovereign_debt_lab
