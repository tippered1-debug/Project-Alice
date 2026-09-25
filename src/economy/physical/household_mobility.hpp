#pragma once

#include "dcon_generated.hpp"
#include "persons/exact_population.hpp"

namespace sys { class state; }

namespace economy::physical::household_mobility {

float commute_adjusted_daily_wage(sys::state const&, dcon::site_id home,
	dcon::job_offer_id offer);
float commute_adjusted_daily_wage(sys::state const&, dcon::site_id home,
	dcon::site_id workplace, float gross_daily_wage);
uint8_t qualification_rank(sys::state const&, dcon::pop_type_id);

// Long commutes can become household moves when the destination already has
// urban housing. Local jobs remain commutes and do not change POP locations.
bool relocate_for_job(sys::state&, dcon::person_id, dcon::site_id workplace);
bool relocate_for_job(sys::state&, persons::exact_population::person_key,
	dcon::site_id workplace);

// Convert factory payroll income into household purchases through the
// existing concrete market, then consume delivered stock.
void update_employed_households(sys::state&);

} // namespace economy::physical::household_mobility
