#include "monetary_system.hpp"

#include "advanced_province_buildings.hpp"
#include "money.hpp"
#include "system_state.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace economy::monetary {
namespace {

constexpr double epsilon = 0.000001;

double finite_or_zero(double value) {
	return std::isfinite(value) ? value : 0.0;
}

} // namespace

double stocks::gross() const noexcept {
	return std::abs(pop_savings) + std::abs(market_cash) + std::abs(treasury)
		+ std::abs(national_bank) + std::abs(private_investment)
		+ std::abs(producer_banks) + std::abs(building_savings);
}

balance calculate(balance_inputs raw_inputs) {
	balance result;
	result.has_previous = raw_inputs.has_previous;
	result.previous_total = finite_or_zero(raw_inputs.previous_total);
	result.observed_total = finite_or_zero(raw_inputs.observed_total);
	result.gold_emission = std::max(0.0, finite_or_zero(raw_inputs.gold_emission));
	result.gross_total = std::max(0.0, finite_or_zero(raw_inputs.gross_total));
	result.net_to_gross = result.gross_total > epsilon
		? result.observed_total / result.gross_total : 1.0;
	result.expected_total = result.previous_total + result.gold_emission;

	if(!result.has_previous) {
		// The first observation of a run establishes the baseline. There is
		// nothing to compare it against yet.
		result.expected_total = result.observed_total;
		return result;
	}

	result.unaccounted = result.observed_total - result.expected_total;
	return result;
}

stocks measure(sys::state const& state) {
	stocks result;
	// Doubles throughout: a conservation check summed in float would drown its
	// own residual in accumulation error.
	state.world.for_each_pop([&](dcon::pop_id pop) {
		auto const savings = state.world.pop_get_savings(pop);
		if(std::isfinite(savings))
			result.pop_savings += double(savings);
	});
	state.world.for_each_market([&](dcon::market_id market) {
		auto const cash = state.world.market_get_stockpile(market, economy::money);
		if(std::isfinite(cash))
			result.market_cash += double(cash);
	});
	state.world.for_each_nation([&](dcon::nation_id nation) {
		auto const treasury = state.world.nation_get_stockpiles(nation, economy::money);
		if(std::isfinite(treasury))
			result.treasury += double(treasury);
		auto const bank = state.world.nation_get_national_bank(nation);
		if(std::isfinite(bank))
			result.national_bank += double(bank);
		auto const investment = state.world.nation_get_private_investment(nation);
		if(std::isfinite(investment))
			result.private_investment += double(investment);
	});
	state.world.for_each_province([&](dcon::province_id province) {
		auto const rgo = state.world.province_get_rgo_bank(province);
		if(std::isfinite(rgo))
			result.producer_banks += double(rgo);
		auto const factory = state.world.province_get_factory_bank(province);
		if(std::isfinite(factory))
			result.producer_banks += double(factory);
		auto const artisan = state.world.province_get_artisan_bank(province);
		if(std::isfinite(artisan))
			result.producer_banks += double(artisan);
		for(int32_t building = 0; building < advanced_province_buildings::list::total; ++building) {
			auto const savings =
				state.world.province_get_advanced_province_building_private_savings(province, building);
			if(std::isfinite(savings))
				result.building_savings += double(savings);
		}
	});
	return result;
}

void begin_day(sys::state& state) {
	state.monetary_account.gold_emission_today = 0.0;
	if(state.money_audit.enabled) {
		state.money_audit.phases.clear();
		state.money_audit.running_total = measure(state).total();
	}
}

void record_gold_emission(sys::state& state, float amount) {
	if(std::isfinite(amount) && amount > 0.f)
		state.monetary_account.gold_emission_today += double(amount);
}

void audit_phase(sys::state& state, std::string_view phase) {
	if(!state.money_audit.enabled)
		return;
	auto const after = measure(state).total();
	state.money_audit.phases.push_back(
		phase_delta{std::string(phase), state.money_audit.running_total, after});
	state.money_audit.running_total = after;
}

void update(sys::state& state) {
	auto& ledger = state.monetary_account;
	ledger.current = measure(state);

	balance_inputs raw;
	raw.has_previous = ledger.has_previous;
	raw.previous_total = ledger.previous_total;
	raw.observed_total = ledger.current.total();
	raw.gold_emission = ledger.gold_emission_today;
	raw.gross_total = ledger.current.gross();

	ledger.last_balance = calculate(raw);
	ledger.previous_total = ledger.last_balance.observed_total;
	ledger.has_previous = true;

	// The legacy blanket decay, unchanged in every mode. This module reports
	// the books; it does not touch them.
	state.inflation = legacy_inflation;
}

void initialize(sys::state& state) {
	auto& ledger = state.monetary_account;
	ledger.gold_emission_today = 0.0;
	ledger.current = measure(state);
	// A save is written on a day boundary, so the stocks restored here are the
	// same ones a continuous run would carry into the next day.
	ledger.previous_total = ledger.current.total();
	ledger.has_previous = true;
	ledger.last_balance = balance{};
	ledger.last_balance.has_previous = true;
	ledger.last_balance.previous_total = ledger.previous_total;
	ledger.last_balance.observed_total = ledger.previous_total;
	ledger.last_balance.expected_total = ledger.previous_total;
	ledger.last_balance.gross_total = ledger.current.gross();
	ledger.last_balance.net_to_gross = ledger.last_balance.gross_total > 0.000001
		? ledger.previous_total / ledger.last_balance.gross_total : 1.0;
}

std::string format_audit(sys::state const& state, double threshold) {
	if(!state.money_audit.enabled || state.money_audit.phases.empty())
		return {};
	std::string result = "money audit ";
	result += std::to_string(state.current_date.to_raw_value());
	result += "\n";
	auto total = 0.0;
	for(auto const& phase : state.money_audit.phases) {
		total += phase.change();
		if(std::abs(phase.change()) < threshold)
			continue;
		result += "  ";
		result += phase.phase;
		result += ": ";
		result += std::to_string(phase.change());
		result += "\n";
	}
	result += "  == day total: ";
	result += std::to_string(total);
	result += "\n";
	return result;
}

} // namespace economy::monetary
