#pragma once

#include "dcon_generated.hpp"
#include "date_interface.hpp"

namespace sys { class state; }

namespace economy::physical::concrete_labor {

enum class contract_status : uint8_t { active = 0, ended = 1, terminated = 2 };

// wage_rate is the amount due for one unit of labor for the whole pay period.
// The daily v1 amount is wage_rate * labor_capacity / pay_period_days.
dcon::employment_contract_id create_employment_contract(sys::state&, dcon::person_id,
	dcon::economic_actor_id employer, dcon::factory_id, dcon::site_id workplace,
	uint8_t occupation, float labor_capacity, float wage_rate, uint16_t pay_period_days,
	dcon::monetary_account_id payer_account, dcon::monetary_account_id worker_account,
	sys::date start_date);

bool end_employment_contract(sys::state&, dcon::employment_contract_id,
	contract_status status, sys::date end_date);
bool terminate_employment_contract(sys::state&, dcon::employment_contract_id, sys::date);

std::vector<dcon::employment_contract_id> contracts_for_factory(sys::state const&, dcon::factory_id);
std::vector<dcon::employment_contract_id> active_contracts_for_factory(sys::state const&, dcon::factory_id);
std::vector<dcon::person_id> active_workers_for_factory(sys::state const&, dcon::factory_id);
bool person_has_active_contract(sys::state const&, dcon::person_id);
bool is_unemployed(sys::state const&, dcon::person_id);
float labor_supplied_to_factory(sys::state const&, dcon::factory_id);
float wage_due(sys::state const&, dcon::employment_contract_id);
float wage_due_for_factory(sys::state const&, dcon::factory_id);
float wage_cost_for_factory(sys::state const&, dcon::factory_id, float production_units, float production_capacity);
float unpaid_wages(sys::state const&, dcon::employment_contract_id);

struct wage_settlement {
	float due = 0.0f;
	float paid = 0.0f;
	float unpaid = 0.0f;
	float current_due = 0.0f;
	float arrears_before = 0.0f;
	float current_paid = 0.0f;
	float arrears_repaid = 0.0f;
	float arrears_after = 0.0f;
	float total_transferred = 0.0f;
	dcon::obligation_id obligation{};
};

wage_settlement settle_contract_wage(sys::state&, dcon::employment_contract_id);

} // namespace economy::physical::concrete_labor
