#pragma once

#include "dcon_generated.hpp"
#include "date_interface.hpp"

namespace sys { class state; }

namespace economy::physical::job_market {

enum class offer_status : uint8_t { open = 0, closed = 1, expired = 2 };

dcon::job_offer_id post_job_offer(sys::state&, dcon::economic_actor_id employer,
	dcon::factory_id, dcon::site_id workplace, uint8_t occupation,
	float labor_capacity, float wage_rate, uint16_t pay_period_days,
	dcon::monetary_account_id payer_account, uint32_t openings,
	sys::date created_on, sys::date expires_on = {});

dcon::job_offer_id post_institution_job_offer(sys::state&, dcon::institution_id,
	dcon::site_id workplace, uint8_t occupation, float labor_capacity,
	float wage_rate, uint16_t pay_period_days, dcon::monetary_account_id payer_account,
	uint32_t openings, sys::date created_on, sys::date expires_on = {});

bool close_job_offer(sys::state&, dcon::job_offer_id);
bool expire_job_offer(sys::state&, dcon::job_offer_id);
bool add_job_offer_openings(sys::state&, dcon::job_offer_id, uint32_t);

std::vector<dcon::job_offer_id> open_offers_for_factory(sys::state const&, dcon::factory_id);

void process_factory_vacancies(sys::state&);
void process(sys::state&);
void project_labor_price_view(sys::state&);

} // namespace economy::physical::job_market
