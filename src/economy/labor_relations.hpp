#pragma once

#include "dcon_generated_ids.hpp"
#include "system_state_forward.hpp"

namespace economy::labor_relations {

enum class organization_regime : uint8_t {
	prohibited,
	recognized,
	protected_right,
};

struct membership_inputs {
	float workers = 0.f;
	float labor_movement_workers = 0.f;
	float literacy = 0.f;
	float consciousness = 0.f;
	organization_regime regime = organization_regime::prohibited;
};

struct inputs {
	bool enabled = false;
	float workers = 0.f;
	float organized_workers = 0.f;
	float literacy = 0.f;
	float consciousness = 0.f;
	float militancy = 0.f;
	float employment = 0.f;
	float life_needs_coverage = 1.f;
	float legal_leverage = 0.f;
};

struct result {
	bool enabled = false;
	float membership_share = 0.f;
	float organization = 0.f;
	float hardship = 0.f;
	float strike_participation = 0.f;
	float labor_availability = 1.f;
};

[[nodiscard]] result calculate(inputs raw_inputs);
[[nodiscard]] float estimate_membership_share(membership_inputs raw_inputs);
[[nodiscard]] result evaluate_province(sys::state const& state,
	dcon::province_id province);

} // namespace economy::labor_relations
