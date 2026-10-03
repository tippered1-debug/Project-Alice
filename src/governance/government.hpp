#pragma once

#include "dcon_generated.hpp"
#include "date_interface.hpp"
#include "governance/policy.hpp"

#include <vector>

namespace sys { class state; }

namespace governance::government {

// Who governs follows from seats and offices. The chief executive is the
// office that appoints the finance minister. Where a chamber confirms it, the
// leader of that chamber's largest party forms a coalition with the parties
// nearest it until it holds a majority, the chamber confirms them, and the
// appointer appoints them. Where the chief executive is elected, its party
// governs. Ministries are shared among governing parties by seats.
// Discretion is always a person's: offices are filled and policy changed by the
// holders of the offices the constitution names.

dcon::office_id chief_office(sys::state const&, dcon::nation_id);
// The chamber whose confidence the chief executive needs, if any.
dcon::institution_id confidence_chamber(sys::state const&, dcon::nation_id);
std::vector<dcon::organization_id> coalition(sys::state const&, dcon::nation_id);
uint32_t seats_of(sys::state const&, dcon::organization_id party, dcon::institution_id chamber, sys::date);
// The governing programme: the governing parties' platforms weighted by their
// seats. Without governing parties, the ideal of the wealthiest tenth of the
// electorate, on whom a government without a party base relies.
policy::position program(sys::state const&, dcon::nation_id);

// Forms the government after an election or a fall. Returns whether a chief
// executive holds office afterwards.
bool form(sys::state&, dcon::nation_id, sys::date);
// Members vote by party line: governing parties for the government's
// candidates and bills, the opposition against, and against the government in
// a motion of no confidence.
void whip_confirmation(sys::state&, dcon::office_id, dcon::person_id candidate, sys::date);
void whip_no_confidence(sys::state&, dcon::office_id, sys::date);
// A government that no longer holds a majority faces a motion of no confidence.
bool test_confidence(sys::state&, dcon::nation_id, sys::date);
// The holders of appointing offices fill vacancies: party offices with members
// of their own party or coalition, and judicial, monetary and military offices
// with persons outside party politics. A seat whose holder died passes to the
// next member on their party's list.
void fill_vacancies(sys::state&, dcon::nation_id, sys::date);
// The finance minister turns the programme into fiscal law when they differ.
void implement(sys::state&, dcon::nation_id, sys::date);

// Founding politics: parties from the electorate, the first elections, the
// first government, the remaining offices, and the first fiscal law.
void bootstrap(sys::state&, dcon::nation_id, sys::date);
void process(sys::state&, sys::date);

} // namespace governance::government
