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
	bool worker = false;
	bool unemployed = false;
	bool wage_arrears = false;
	bool shareholder = false;
	bool debtor = false;
	bool depositor = false;
	policy::position ideal{};
	policy::salience issue_salience;
	float turnout = 0.0f;
};

inline constexpr float adult_share_of_members = 0.6f;

// The nation's voters with ideal positions and turnout.
std::vector<voter> voters(sys::state const&, dcon::nation_id);
float median_income(std::vector<voter> const&);
// Preferences and issue weights are derived from economic exposure. The
// sparse typed position lets each voter remain indifferent to unexposed laws.
void assess(voter&, float median_income);
// The regional territorial unit a province votes in.
dcon::territorial_unit_id region_of(sys::state const&, dcon::province_id);

} // namespace governance::electorate
