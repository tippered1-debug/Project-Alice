#include "investment_ranking.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace economy::investment {
namespace {

float nonnegative(float value) noexcept {
	return std::isfinite(value) && value > 0.0f ? value : 0.0f;
}

float unit(float value, float fallback) noexcept {
	return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : fallback;
}

} // namespace

project_score evaluate(project_inputs const& raw) noexcept {
	project_score result{};
	auto const capital = nonnegative(raw.capital_cost);
	auto const revenue = nonnegative(raw.gross_daily_revenue)
		* unit(raw.expected_sell_through, 0.0f);
	auto const operating_cost = nonnegative(raw.daily_material_cost)
		+ nonnegative(raw.daily_wage_cost);
	auto const after_tax_margin = (revenue - operating_cost)
		* (1.0f - unit(raw.effective_tax_rate, 0.0f));
	result.expected_daily_cashflow = after_tax_margin;
	result.annual_return_on_capital = capital > 0.0f
		? after_tax_margin * 365.0f / capital : 0.0f;
	result.payback_days = after_tax_margin > 0.0f
		? capital / after_tax_margin : std::numeric_limits<float>::infinity();

	auto const financing_cost = nonnegative(raw.annual_interest_rate);
	auto const supply_risk = (1.0f - unit(raw.input_reliability, 0.0f)) * 0.35f;
	auto const logistics_risk = (1.0f - unit(raw.logistics_reliability, 0.0f)) * 0.20f;
	auto const demand_risk = unit(raw.demand_risk, 0.0f) * 0.15f;
	result.risk_adjusted_private = result.annual_return_on_capital
		- financing_cost - supply_risk - logistics_risk - demand_risk;

	// Public projects still value cash flow, but also internalize employment,
	// strategic shortages and import exposure that a private owner cannot capture.
	auto const jobs_per_capital = capital > 0.0f
		? std::min(1.0f, nonnegative(raw.jobs) / capital) : 0.0f;
	result.public_value = result.risk_adjusted_private * 0.45f
		+ jobs_per_capital * 0.20f
		+ unit(raw.strategic_shortage, 0.0f) * 0.25f
		+ unit(raw.import_dependence, 0.0f) * 0.10f;
	result.privately_viable = result.expected_daily_cashflow > 0.0f
		&& result.risk_adjusted_private > 0.0f;
	result.publicly_viable = result.expected_daily_cashflow > -operating_cost * 0.20f
		&& result.public_value > 0.0f;
	return result;
}

} // namespace economy::investment
