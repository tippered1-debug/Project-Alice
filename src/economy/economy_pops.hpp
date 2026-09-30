#pragma once

#include "system_state.hpp"
#include "persons/persons.hpp"
#include <array>
#include <vector>

namespace economy::pops {

// Read models only. None of these values allocates cash or posts an order.
struct consumption_category_projection {
	float required = 0.0f;
	float spent = 0.0f;
	float physical_consumption_ratio = 0.0f; // physical consumption / exact need
};
struct population_consumption_projection {
	consumption_category_projection life_needs{}, everyday_needs{}, luxury_needs{};
	float cash = 0.0f;
	float payroll_received = 0.0f;
	float public_transfers_received = 0.0f;
	float tax_paid = 0.0f;
	float capital_contributed = 0.0f;
	float spent_total = 0.0f;
};
// Authored category mapping is presentation metadata, never a demand multiplier.
std::array<float, 3> compatibility_category_shares(sys::state const&, persons::person_key, dcon::commodity_id);
population_consumption_projection project_consumption(sys::state const&, dcon::pop_id);
float projected_spending(sys::state const&, dcon::pop_id, dcon::commodity_id, uint8_t category);
float education_access(sys::state const&, dcon::pop_id);

struct labor_ratio_wage { int32_t labor_type; float ratio; float wage; };
// Prospective migration report based on the projected labor market; no payment.
std::vector<labor_ratio_wage> compatibility_wage_opportunities(sys::state const&, dcon::province_id, dcon::pop_type_id, bool accepted, float size);
std::vector<labor_ratio_wage> projected_payroll(sys::state const&, dcon::pop_id);
}
namespace economy {
float estimate_pops_consumption(sys::state const&, dcon::commodity_id, dcon::province_id);
}
