#pragma once

#include "dcon_generated.hpp"
#include "container_types_dcon.hpp"

namespace sys { class state; }

namespace economy::firm_agency {

struct production_decision {
	float desired_units = 0.0f;
	float desired_output = 0.0f;
	float desired_utilization = 0.0f;
	float expected_unit_revenue = 0.0f;
	float expected_variable_cost = 0.0f;
	float expected_payroll_cost = 0.0f;
	float expected_gross_margin = 0.0f;
	float cash_limited_units = 0.0f;
	float procurement_funding_shortfall = 0.0f;
	float payroll_funding_shortfall = 0.0f;
	dcon::commodity_id procurement_settlement{};
	dcon::commodity_id payroll_settlement{};
	float output_inventory = 0.0f;
	economy::commodity_set required_inputs{};
};

production_decision decide_factory(sys::state const&, dcon::factory_id);
float desired_production(sys::state const&, dcon::factory_id);
void update_decisions(sys::state&);
void post_output_asks(sys::state&);
void observe_production(sys::state&, dcon::factory_id, float planned_units, float realized_units);

} // namespace economy::firm_agency
