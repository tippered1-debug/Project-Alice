#include "gamestate/sovereign_debt_crisis_lab.hpp"

#include <charconv>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <string>
#include <system_error>
#include <string_view>

namespace {

bool parse_unsigned(std::string_view text, uint32_t& value) {
	if(text.empty()) return false;
	uint64_t parsed = 0;
	auto const [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
	if(error != std::errc{} || end != text.data() + text.size()
		|| parsed > std::numeric_limits<uint32_t>::max())
		return false;
	value = uint32_t(parsed);
	return true;
}

void print_usage(char const* executable) {
	std::cerr << "Usage: " << executable
		<< " [--scenario baseline|revenue-shock|austerity]"
		<< " [--days N] [--seed N] [--shock-day N] [--output DIR]\n";
}

} // namespace

int main(int argc, char** argv) {
	sys::simulation::sovereign_debt_lab::config config;
	std::string output_directory = "sovereign-debt-crisis-output";

	for(int i = 1; i < argc; ++i) {
		std::string_view const argument(argv[i]);
		if(argument == "--help" || argument == "-h") {
			print_usage(argv[0]);
			return 0;
		}
		if(i + 1 >= argc) {
			print_usage(argv[0]);
			return 2;
		}
		std::string_view const value(argv[++i]);
		if(argument == "--scenario") {
			try {
				config.experiment = sys::simulation::sovereign_debt_lab::parse_scenario(std::string(value));
			} catch(std::exception const& error) {
				std::cerr << error.what() << '\n';
				return 2;
			}
		} else if(argument == "--days") {
			if(!parse_unsigned(value, config.days)) {
				std::cerr << "--days must be an unsigned 32-bit integer\n";
				return 2;
			}
		} else if(argument == "--seed") {
			if(!parse_unsigned(value, config.seed)) {
				std::cerr << "--seed must be an unsigned 32-bit integer\n";
				return 2;
			}
		} else if(argument == "--shock-day") {
			if(!parse_unsigned(value, config.shock_day)) {
				std::cerr << "--shock-day must be an unsigned 32-bit integer\n";
				return 2;
			}
		} else if(argument == "--output") {
			output_directory.assign(value);
		} else {
			std::cerr << "Unknown option: " << argument << '\n';
			print_usage(argv[0]);
			return 2;
		}
	}

	try {
		auto const result = sys::simulation::sovereign_debt_lab::run(config);
		if(!sys::simulation::sovereign_debt_lab::write_outputs(result, output_directory)) {
			std::cerr << "Could not write sovereign-debt lab output\n";
			return 1;
		}
		std::cout << "{\"scenario\":\""
			<< sys::simulation::sovereign_debt_lab::scenario_name(result.experiment)
			<< "\",\"days_completed\":" << result.ticks_completed
			<< ",\"debt_defaulted\":" << (result.debt_reached_default ? "true" : "false")
			<< ",\"defaulted_claim\":" << result.final_defaulted_debt
			<< ",\"bank_net_worth\":" << result.final_bank_net_worth
			<< ",\"money_error\":" << result.maximum_money_conservation_error
			<< ",\"checksum\":" << result.final_checksum << "}\n";
		return result.ticks_completed == config.days ? 0 : 1;
	} catch(std::exception const& error) {
		std::cerr << "Sovereign-debt lab failed: " << error.what() << '\n';
		return 1;
	}
}
