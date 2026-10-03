#pragma once

#include "dcon_generated.hpp"
#include "date_interface.hpp"
#include "governance/policy.hpp"
#include "persons/persons.hpp"

#include <vector>

namespace sys { class state; }

namespace governance::electorate {

// A voter is a household cohort voting as the bloc of its adult members, or a
// person with an individual budget voting with the adults of their family.
// Interests come from the canonical economy only: income per adult against the
// national median, liquid wealth, public employment, rural livelihood, and
// dependents. No population type, ideology, or legacy party enters.
struct voter {
	dcon::organization_id cohort{};
	persons::person_key person{};
	dcon::province_id province{};
	dcon::territorial_unit_id region{};
	float adults = 0.0f;
	// Daily income and liquid wealth per adult.
	float income = 0.0f;
	float wealth = 0.0f;
	bool public_employee = false;
	bool rural = false;
	bool dependents = false;
	policy::position ideal{};
	float turnout = 0.0f;
};

inline constexpr float adult_share_of_members = 0.6f;

// The nation's voters with ideal positions and turnout.
std::vector<voter> voters(sys::state const&, dcon::nation_id);
float median_income(std::vector<voter> const&);
// What a voter wants: lower taxes and less redistribution as income rises
// above the median; more of everything public for public employees; schooling
// for families; order for the wealthy; works and local government for rural
// voters. Turnout rises with income, wealth and public employment.
void assess(voter&, float median_income);
// The regional territorial unit a province votes in.
dcon::territorial_unit_id region_of(sys::state const&, dcon::province_id);

} // namespace governance::electorate
