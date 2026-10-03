#pragma once

#include "dcon_generated.hpp"
#include "date_interface.hpp"

#include <utility>
#include <vector>

namespace sys { class state; }

namespace governance::policy_inputs {

// The legacy tax and spending sliders are policy inputs, not authority. When
// they differ from the fiscal law in force, the holder of the office empowered
// to regulate public finance (the finance minister where one holds that power)
// issues a regulation with the new tax rates and appropriations and repeals
// the fiscal regulations it replaces. With no such holder the sliders change
// nothing.
inline constexpr float disbursement_rate = 0.35f;

struct fiscal_inputs {
	float tax_rates[3] = {};
	float disbursement = disbursement_rate;
	std::vector<std::pair<dcon::institution_id, float>> shares;
};

fiscal_inputs read(sys::state const&, dcon::nation_id);
bool law_matches(sys::state const&, dcon::nation_id, fiscal_inputs const&, sys::date);
dcon::person_id fiscal_regulator(sys::state const&, dcon::nation_id, sys::date);
// Drafts and enacts the regulation as the person; returns it, or nothing when
// the person may not regulate public finance.
dcon::legal_instrument_id enact(sys::state&, dcon::nation_id, dcon::person_id, fiscal_inputs const&, sys::date);
void apply(sys::state&);

} // namespace governance::policy_inputs
