#pragma once

#include "dcon_generated.hpp"
#include "date_interface.hpp"

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace sys { class state; }

namespace governance::policy {

// The policy space parties compete over. Every dimension is a lever the fiscal
// law already has, so a winning platform becomes a concrete regulation.
enum class dimension : uint8_t {
	// Base rate on wage income, from 0 to 40%.
	tax_level = 0,
	// From 0 (one rate for all) to 1 (the poor pay 40% of the base rate, the
	// rich twice it).
	progressivity = 1,
	// Relative weights of the budget shares, from 0 to 1.
	education = 2,
	policing = 3,
	public_works = 4,
	local_government = 5,
	count = 6
};
inline constexpr size_t dimension_count = size_t(dimension::count);
using position = std::array<float, dimension_count>;

inline constexpr float maximum_tax_level = 0.4f;
inline constexpr float disbursement_rate = 0.35f;

float lower_bound(dimension);
float upper_bound(dimension);
position clamp(position);
// Squared distance with every dimension scaled to its range.
float distance(position const&, position const&);

struct fiscal_rules {
	float tax_rates[3] = {};
	std::vector<std::pair<dcon::institution_id, float>> shares;
};
// The tax rates and appropriations a position means for a nation's
// institutions: education, policing and public works shares go to the
// national institutions providing those services, and the local share is split
// between the territorial governments with staff.
fiscal_rules rules_for(sys::state const&, dcon::nation_id, position const&);
// The position the fiscal law in force expresses, if there is one.
bool current(sys::state const&, dcon::nation_id, sys::date, position&);
bool law_matches(sys::state const&, dcon::nation_id, position const&, sys::date);
// The person exercising the office empowered to regulate public finance.
dcon::person_id fiscal_regulator(sys::state const&, dcon::nation_id, sys::date);
// Drafts and enacts the fiscal regulation for the position as the person and
// repeals the fiscal regulations it replaces; returns nothing when the person
// may not regulate public finance.
dcon::legal_instrument_id enact(sys::state&, dcon::nation_id, dcon::person_id, position const&, sys::date);

} // namespace governance::policy
