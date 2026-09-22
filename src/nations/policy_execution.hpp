#pragma once

#include "dcon_generated_ids.hpp"
#include "system_state_forward.hpp"

#include <array>
#include <cstdint>

namespace nations::policy_execution {

enum class policy_kind : uint8_t {
	crime_suppression,
	education,
	social_benefits,
	reform_implementation,
	mobilization_logistics,
};

enum class capacity_factor : uint8_t {
	none,
	national_administration,
	local_control,
	funding,
	bureaucratic_labor,
	political_compliance,
};

struct inputs {
	bool enabled = false;
	float national_administration = 1.f;
	float local_control = 1.f;
	float funding = 1.f;
	float bureaucratic_labor = 1.f;
	float political_compliance = 1.f;
};

struct workload {
	// Fractions of a fully-funded budget and a fully-staffed local office needed
	// to deliver the statutory service to all eligible claimants.
	float funding_required = 1.f;
	float labor_required = 1.f;
	// Opposition creates casework, appeals and enforcement work; it does not
	// magically reduce the cash already appropriated.
	float resistance_labor = 0.f;
};

struct breakdown {
	bool enabled = false;
	policy_kind policy = policy_kind::crime_suppression;
	inputs factors{};
	workload required_work{};
	capacity_factor bottleneck = capacity_factor::none;
	float bottleneck_value = 1.f;
	float cash_coverage = 1.f;
	float staff_coverage = 1.f;
	float territorial_coverage = 1.f;
	float compliance_workload_multiplier = 1.f;
	// Legacy mode deliberately returns one, making the API safe to multiply
	// into existing effects without changing classic rules.
	float effective_execution = 1.f;
};

workload workload_for(policy_kind policy);
breakdown calculate(policy_kind policy, inputs raw_inputs);
breakdown effective_policy(sys::state const& state, dcon::nation_id nation,
	dcon::province_id province, policy_kind policy);
float average_effective_policy(sys::state const& state, dcon::nation_id nation,
	policy_kind policy);

} // namespace nations::policy_execution
