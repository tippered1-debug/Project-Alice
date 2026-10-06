#pragma once

#include "dcon_generated.hpp"
#include "date_interface.hpp"
#include "governance/electorate.hpp"

#include <map>
#include <vector>

namespace sys { class state; }

namespace governance::elections {

// How a chamber's seats or an office are filled. The constitution sets it.
enum class electoral_system : uint8_t {
	none = 0,
	// Seats by highest averages (D'Hondt) over party votes.
	proportional = 1,
	// Every seat of a district goes to the party with most votes there.
	plurality = 2,
	// An office by direct vote: a runoff between the top two parties' leaders
	// when nobody wins a majority.
	direct = 3,
	// An office filled by the legislature: the leader of the largest party.
	legislative = 4,
	// A chamber's presiding office: the leader of its largest party.
	presiding = 5,
	// Elected with the institution's directly elected office: the winning
	// party's second member.
	running_mate = 6
};
// Where votes are counted: the whole nation, each region with seats
// apportioned by adults, or the office's own territory.
enum class district_rule : uint8_t { national = 0, regional = 1, own = 2 };

// Voters choose by a logit on policy distance; parties in government gain or
// lose with the change in median income since the government formed.
inline constexpr float choice_sensitivity = 12.0f;
inline constexpr float retrospective_weight = 2.0f;

void set_rule(sys::state&, dcon::institution_id chamber, electoral_system, district_rule, uint16_t term_days, sys::date first);
void set_rule(sys::state&, dcon::office_id, electoral_system, district_rule, uint16_t term_days, sys::date first);

struct tally {
	std::map<uint32_t, float> votes;
	float electorate = 0.0f;
	float cast = 0.0f;
};
// Votes for the parties among the voters inside the district (none: all).
tally count(sys::state const&, dcon::nation_id, std::vector<electorate::voter> const&, std::vector<dcon::organization_id> const& parties,
	dcon::territorial_unit_id district, float incumbent_growth, sys::date election_date = {});
std::vector<uint32_t> highest_averages(std::vector<float> const& votes, uint32_t seats);
std::vector<uint32_t> largest_remainders(std::vector<float> const& weights, uint32_t seats);
// Change in the median income since the government formed.
float incumbent_growth(sys::state const&, dcon::nation_id, float median_income);

dcon::election_id hold(sys::state&, dcon::institution_id chamber, std::vector<electorate::voter> const&, sys::date);
dcon::election_id hold(sys::state&, dcon::office_id, std::vector<electorate::voter> const&, sys::date);
// Vote shares of each party in the nation's latest chamber election.
std::map<uint32_t, float> last_shares(sys::state const&, dcon::nation_id);
// Holds the nation's due elections: chambers, then directly elected offices,
// then offices the legislature fills. Returns whether a chamber was elected.
bool process(sys::state&, dcon::nation_id, sys::date);
// A party's senior figure for an office other than its leadership: the first
// member after the leader who holds none of the excluded offices, else the
// leader. The leader is kept for the government.
dcon::person_id senior_member(sys::state const&, dcon::organization_id party, bool seated_only);
// Fills a chamber's vacant presiding office with a senior member of its largest party.
void fill_presiding(sys::state&, dcon::institution_id chamber, sys::date);

} // namespace governance::elections
