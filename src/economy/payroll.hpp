#pragma once

#include "dcon_generated.hpp"

namespace sys { class state; }

namespace economy::payroll {

struct province_payroll {
	float no_education = 0.0f;
	float basic_education = 0.0f;
	float high_education = 0.0f;
	bool canonical_factory = false;
};

void begin_day(sys::state&);
void settle_factory(sys::state&, dcon::factory_id, float actual_units, float available_units);
province_payroll for_province(sys::state const&, dcon::province_id);

} // namespace economy::payroll
