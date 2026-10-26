#pragma once

#include "dcon_generated.hpp"
#include "date_interface.hpp"
#include "governance/policy.hpp"
#include "governance/finance/fiscal_ledger.hpp"

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
// The governing programme is the latest issue-by-issue coalition agreement.
// Without governing parties, it follows the wealthiest tenth of the electorate.
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
// Turns a negotiated programme into a bill and submits it through the
// constitutionally deciding legislature and assent offices.
void implement(sys::state&, dcon::nation_id, sys::date);
// Lays a fiscal bill (the tax rates, disbursement and appropriations of
// `target`) before the deciding chambers. Each member votes on their own
// economic exposure and their party's platform; governing parties also on
// `whip`, the government's bill. The assenting offices must assent and a
// member (or the office holding `legislate`) enacts it. A next bill may not be
// laid for 30 days.
finance::bill_notice lay_fiscal_bill(sys::state&, dcon::nation_id, policy::position const& target,
	policy::position const& whip, sys::date);
// The government's own fiscal bill: the player's proposal of tax and budget
// priorities, laid like any other. Once enacted, the governing parties take it
// into their programme, so that their finance minister keeps it.
finance::bill_notice propose_fiscal_bill(sys::state&, dcon::nation_id, policy::position const& proposal, sys::date);
// The fiscal topics a player's proposal may change.
bool fiscal_topic(policy::topic_id);

// Founding politics: parties from the electorate, the first elections, the
// first government, the remaining offices, and the first fiscal law.
void bootstrap(sys::state&, dcon::nation_id, sys::date);
void process(sys::state&, sys::date);

} // namespace governance::government
