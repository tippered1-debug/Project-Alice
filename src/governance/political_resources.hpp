#pragma once

#include "dcon_generated.hpp"
#include "date_interface.hpp"
#include "governance/electorate.hpp"
#include "persons/persons.hpp"

#include <cstdint>
#include <map>
#include <vector>

namespace sys { class state; }

namespace governance::political_resources {

// Parties use ordinary money accounts. Contributions and campaign payroll
// are transfers between real wallets and remain visible in the transaction
// ledger.
float party_funds(sys::state const&, dcon::organization_id party);
bool contribute_from_actor(sys::state&, dcon::economic_actor_id contributor,
	dcon::organization_id party, float amount);
bool contribute_from_person(sys::state&, persons::person_key contributor,
	dcon::organization_id party, float amount);

// Pay a living party member for campaign work. The payment creates campaign
// reach only after money has actually moved.
bool pay_campaign_staff(sys::state&, dcon::organization_id party,
	persons::person_key staff_member, float amount);

// Sum recorded campaign payroll over the inclusive date interval.
float campaign_spending(sys::state const&, dcon::organization_id party,
	sys::date from, sys::date through);
std::map<uint32_t, float> campaign_spending_by_party(sys::state const&,
	std::vector<dcon::organization_id> const&, sys::date from, sys::date through);

// At a due election, supporters contribute a small share of spendable cash to
// their nearest party. Parties then pay a limited group of members for
// campaign work. Both steps move money through ordinary accounts.
void prepare_campaigns(sys::state&, dcon::nation_id,
	std::vector<electorate::voter> const&, sys::date election_date);

} // namespace governance::political_resources
