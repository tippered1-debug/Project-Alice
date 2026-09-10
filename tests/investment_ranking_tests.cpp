#include "economy/investment_ranking.hpp"

#include "catch2/catch.hpp"

namespace investment = economy::investment;

TEST_CASE("private investment score prices finance supply and demand risk",
		"[economy][investment]") {
	investment::project_inputs healthy{};
	healthy.capital_cost = 1000.0f;
	healthy.gross_daily_revenue = 10.0f;
	healthy.daily_material_cost = 3.0f;
	healthy.daily_wage_cost = 2.0f;
	healthy.expected_sell_through = 0.95f;
	healthy.input_reliability = 0.95f;
	healthy.logistics_reliability = 0.90f;
	healthy.effective_tax_rate = 0.10f;
	healthy.annual_interest_rate = 0.05f;
	healthy.demand_risk = 0.05f;

	auto risky = healthy;
	risky.input_reliability = 0.40f;
	risky.logistics_reliability = 0.30f;
	risky.annual_interest_rate = 0.30f;
	risky.demand_risk = 0.70f;

	auto const healthy_score = investment::evaluate(healthy);
	auto const risky_score = investment::evaluate(risky);
	REQUIRE(healthy_score.privately_viable);
	REQUIRE(healthy_score.risk_adjusted_private > risky_score.risk_adjusted_private);
	REQUIRE(healthy_score.payback_days > 0.0f);
}

TEST_CASE("public score internalizes jobs and strategic import replacement",
		"[economy][investment]") {
	investment::project_inputs ordinary{};
	ordinary.capital_cost = 1000.0f;
	ordinary.gross_daily_revenue = 5.0f;
	ordinary.daily_material_cost = 3.0f;
	ordinary.daily_wage_cost = 2.0f;
	ordinary.expected_sell_through = 1.0f;
	ordinary.input_reliability = 1.0f;
	ordinary.logistics_reliability = 1.0f;

	auto strategic = ordinary;
	strategic.jobs = 500.0f;
	strategic.strategic_shortage = 1.0f;
	strategic.import_dependence = 1.0f;

	REQUIRE(investment::evaluate(strategic).public_value
		> investment::evaluate(ordinary).public_value);
}
