#pragma once

#include "dcon_generated.hpp"
#include "date_interface.hpp"
#include "governance/electorate.hpp"
#include "governance/policy.hpp"

#include <map>
#include <vector>

namespace sys { class state; }

namespace governance::parties {

// A party is an organization of one nation with members, persons ranked on its
// list, and a platform: a position in the policy space. Parties arise from the
// interests of the electorate, not from any scripted roster.
inline constexpr size_t founding_parties = 4;
// A share of the electorate this far from every platform founds a party.
inline constexpr float representation_distance = 0.25f;
inline constexpr float entry_share = 0.15f;
// A party below this vote share in two elections in a row dissolves.
inline constexpr float exit_vote_share = 0.02f;
// After each election a party moves this far toward its own voters.
inline constexpr float platform_drift = 0.3f;
// Members a party keeps beyond the seats it could fill.
inline constexpr uint32_t list_margin = 6;

dcon::organization_id create(sys::state&, dcon::nation_id, policy::position const&, sys::date);
bool is_party(sys::state const&, dcon::organization_id);
std::vector<dcon::organization_id> parties_of(sys::state const&, dcon::nation_id);
dcon::nation_id nation_of(sys::state const&, dcon::organization_id party);
policy::position platform(sys::state const&, dcon::organization_id party);
void set_platform(sys::state&, dcon::organization_id party, policy::position const&);
dcon::organization_id party_of(sys::state const&, dcon::person_id);
// A person joins at the bottom of the list; they leave any previous party.
bool join(sys::state&, dcon::person_id, dcon::organization_id party, sys::date);
void leave(sys::state&, dcon::person_id);
// Living members in list order.
std::vector<dcon::person_id> members(sys::state const&, dcon::organization_id party);
dcon::person_id leader(sys::state const&, dcon::organization_id party);
void dissolve(sys::state&, dcon::organization_id party);

// The party whose platform is nearest the voter's ideal, if any is within reach.
dcon::organization_id nearest(sys::state const&, std::vector<dcon::organization_id> const&, policy::position const&);
// Recruits living adults who share the party's interests until it has `wanted`
// members: people with individual budgets nearest its platform first, then
// adults of the household cohorts nearest it.
uint32_t recruit(sys::state&, dcon::organization_id party, std::vector<electorate::voter> const&, uint32_t wanted, sys::date);
// Founding: the electorate's interests clustered into parties (weighted
// k-means on ideal positions), each recruiting its members.
std::vector<dcon::organization_id> found(sys::state&, dcon::nation_id, std::vector<electorate::voter> const&, uint32_t members_each, sys::date);
// Between elections: parties drift toward their voters, parties that keep
// failing dissolve, and a large unrepresented bloc founds a new party.
void evolve(sys::state&, dcon::nation_id, std::vector<electorate::voter> const&, std::map<uint32_t, float> const& last_vote_shares,
	uint32_t members_each, sys::date);

} // namespace governance::parties
