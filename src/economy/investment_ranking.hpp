#pragma once

namespace economy::investment {

struct project_inputs {
	float capital_cost = 0.0f;
	float gross_daily_revenue = 0.0f;
	float daily_material_cost = 0.0f;
	float daily_wage_cost = 0.0f;
	float expected_sell_through = 1.0f;
	float input_reliability = 1.0f;
	float logistics_reliability = 1.0f;
	float effective_tax_rate = 0.0f;
	float annual_interest_rate = 0.0f;
	float demand_risk = 0.0f;
	float jobs = 0.0f;
	float strategic_shortage = 0.0f;
	float import_dependence = 0.0f;
};

struct project_score {
	float expected_daily_cashflow = 0.0f;
	float annual_return_on_capital = 0.0f;
	float risk_adjusted_private = 0.0f;
	float public_value = 0.0f;
	float payback_days = 0.0f;
	bool privately_viable = false;
	bool publicly_viable = false;
};

project_score evaluate(project_inputs const& inputs) noexcept;

} // namespace economy::investment
