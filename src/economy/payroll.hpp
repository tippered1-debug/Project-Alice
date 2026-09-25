#pragma once

#include "dcon_generated.hpp"

namespace sys { class state; }

namespace economy::payroll {

struct province_payroll {
	float no_education = 0.0f;
	float basic_education = 0.0f;
	float high_education = 0.0f;
	float public_no_education_due = 0.0f;
	float public_basic_education_due = 0.0f;
	float public_high_education_due = 0.0f;
	bool canonical_factory = false;
};

void begin_day(sys::state&);
void settle_factory(sys::state&, dcon::factory_id, float actual_units, float available_units);
void record_public_payroll(sys::state&, dcon::province_id, dcon::commodity_id settlement,
	float due_no, float due_basic, float due_high,
	float paid_no, float paid_basic, float paid_high);
province_payroll for_province(sys::state const&, dcon::province_id, sys::date);

} // namespace economy::payroll
