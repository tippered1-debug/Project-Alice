#include "political_resources.hpp"

#include "system_state.hpp"
#include "actors/organizations/organizations.hpp"
#include "actors/ownership.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/exact_person_economy.hpp"
#include "economy/money.hpp"
#include "economy/relations/relations.hpp"
#include "economy/wallets.hpp"
#include "governance/parties.hpp"
#include "governance/policy.hpp"
#include "persons/persons.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace governance::political_resources {
namespace {

using account_ref = economy::exact_person_economy::account_ref;
constexpr float supporter_contribution_rate = 0.01f;
constexpr float party_campaign_budget_share = 0.25f;
constexpr size_t campaign_staff_limit = 20;

bool valid_amount(float amount) {
	return std::isfinite(amount) && amount > 0.0f;
}

account_ref party_wallet(sys::state& state, dcon::organization_id party) {
	if(!parties::is_party(state, party)) return {};
	return economy::wallets::open_for(state,
		actors::organizations::actor_for_organization(state, party), economy::money);
}

account_ref existing_party_wallet(sys::state const& state, dcon::organization_id party) {
	if(!parties::is_party(state, party)) return {};
	return economy::wallets::account_for(state,
		actors::organizations::actor_for_organization(state, party), economy::money);
}

bool member_of(sys::state const& state, dcon::organization_id party, persons::person_key key) {
	if(!persons::exists(state, key) || !persons::alive(state, key)) return false;
	for(auto member : parties::members(state, party))
		if(persons::canonical_key(state, member) == key) return true;
	return false;
}

bool in_window(sys::date date, sys::date from, sys::date through) {
	return date && from && through && date >= from && date <= through;
}

float spendable_cash(sys::state const& state, electorate::voter const& voter,
	account_ref& account, dcon::economic_actor_id& actor) {
	if(voter.cohort) {
		actor = actors::organizations::actor_for_organization(state, voter.cohort);
		account = economy::wallets::account_for(state, actor, economy::money);
	} else if(voter.person.source_population_cell != 0) {
		account = economy::exact_person_economy::find_account(state, voter.person, economy::money);
	} else {
		return 0.0f;
	}
	return account ? economy::wallets::spendable(state, account) : 0.0f;
}

} // namespace

float party_funds(sys::state const& state, dcon::organization_id party) {
	auto wallet = existing_party_wallet(state, party);
	return wallet ? economy::wallets::spendable(state, wallet) : 0.0f;
}

bool contribute_from_actor(sys::state& state, dcon::economic_actor_id contributor,
	dcon::organization_id party, float amount) {
	if(!contributor || !state.world.economic_actor_is_valid(contributor)
		|| !parties::is_party(state, party) || !valid_amount(amount)) return false;
	auto kind = actors::ownership::actor_kind(state.world.economic_actor_get_kind(contributor));
	if(!actors::organizations::is_economic_kind(kind)
		|| state.world.economic_actor_get_institution_from_institution_actor(contributor)) return false;
	auto recipient = actors::organizations::actor_for_organization(state, party);
	if(!recipient || contributor == recipient) return false;
	auto source = economy::wallets::account_for(state, contributor, economy::money);
	if(!source || economy::wallets::spendable(state, source) < amount) return false;
	auto destination = party_wallet(state, party);
	return destination && economy::wallets::pay(state, source, destination, amount,
		economy::relations::transaction_kind::political_contribution);
}

bool contribute_from_person(sys::state& state, persons::person_key contributor,
	dcon::organization_id party, float amount) {
	if(!persons::exists(state, contributor) || !persons::alive(state, contributor)
		|| !parties::is_party(state, party) || !valid_amount(amount)) return false;
	auto source = economy::exact_person_economy::find_account(state, contributor, economy::money);
	if(!source || economy::wallets::spendable(state, source) < amount) return false;
	auto destination = party_wallet(state, party);
	return destination && economy::wallets::pay(state, source, destination, amount,
		economy::relations::transaction_kind::political_contribution);
}

bool pay_campaign_staff(sys::state& state, dcon::organization_id party,
	persons::person_key staff_member, float amount) {
	if(!parties::is_party(state, party) || !member_of(state, party, staff_member)
		|| !valid_amount(amount)) return false;
	auto source = party_wallet(state, party);
	if(!source || economy::wallets::spendable(state, source) < amount) return false;
	auto destination = economy::exact_person_economy::open_account(state, staff_member, economy::money);
	if(!destination) return false;
	return economy::wallets::pay(state, source, destination, amount,
		economy::relations::transaction_kind::campaign_expenditure);
}

float campaign_spending(sys::state const& state, dcon::organization_id party,
	sys::date from, sys::date through) {
	if(!parties::is_party(state, party)) return 0.0f;
	auto result = campaign_spending_by_party(state, std::vector{party}, from, through);
	auto found = result.find(party.index());
	return found == result.end() ? 0.0f : found->second;
}

std::map<uint32_t, float> campaign_spending_by_party(sys::state const& state,
	std::vector<dcon::organization_id> const& candidates, sys::date from, sys::date through) {
	std::map<uint32_t, float> result;
	if(!from || !through || from > through) return result;
	std::unordered_map<uint32_t, uint32_t> party_by_actor;
	for(auto party : candidates) {
		if(!parties::is_party(state, party)) continue;
		auto actor = actors::organizations::actor_for_organization(state, party);
		if(actor) party_by_actor.emplace(actor.index(), party.index());
	}
	std::unordered_map<uint32_t, double> totals;
	state.world.for_each_transaction([&](dcon::transaction_id transaction) {
		if(state.world.transaction_get_kind(transaction)
			!= uint8_t(economy::relations::transaction_kind::campaign_expenditure)
			|| !in_window(state.world.transaction_get_timestamp(transaction), from, through)) return;
		auto payer = state.world.transaction_get_economic_actor_from_transaction_payer(transaction);
		if(!payer) return;
		auto party = party_by_actor.find(payer.index());
		if(party != party_by_actor.end()) totals[party->second] += state.world.transaction_get_amount(transaction);
	});
	for(auto const& transaction : economy::exact_person_economy::transaction_records(state)) {
		if(transaction.kind != economy::relations::transaction_kind::campaign_expenditure
			|| !in_window(transaction.timestamp, from, through)
			|| transaction.source.kind != economy::exact_person_economy::account_kind::dcon) continue;
		auto payer = economy::accounts::owner_of(state, transaction.source.dcon_account);
		auto party = payer ? party_by_actor.find(payer.index()) : party_by_actor.end();
		if(party != party_by_actor.end()) totals[party->second] += transaction.amount;
	}
	for(auto const& [party, total] : totals)
		result[party] = std::isfinite(total) ? float(std::min(total, double(std::numeric_limits<float>::max()))) : 0.0f;
	return result;
}

void prepare_campaigns(sys::state& state, dcon::nation_id nation,
	std::vector<electorate::voter> const& voters, sys::date election_date) {
	if(!nation || !election_date) return;
	std::vector<dcon::organization_id> active_parties;
	for(auto party : parties::parties_of(state, nation))
		if(parties::leader(state, party, election_date)) active_parties.push_back(party);
	if(active_parties.empty()) return;
	std::unordered_set<uint32_t> contributors_with_dcon_accounts;
	std::unordered_set<uint64_t> contributors_with_exact_accounts;
	state.world.for_each_transaction([&](dcon::transaction_id transaction) {
		if(state.world.transaction_get_kind(transaction)
			!= uint8_t(economy::relations::transaction_kind::political_contribution)
			|| state.world.transaction_get_timestamp(transaction) != election_date) return;
		auto payer = state.world.transaction_get_economic_actor_from_transaction_payer(transaction);
		if(payer) contributors_with_dcon_accounts.insert(payer.index());
	});
	for(auto const& transaction : economy::exact_person_economy::transaction_records(state)) {
		if(transaction.kind != economy::relations::transaction_kind::political_contribution
			|| transaction.timestamp != election_date) continue;
		if(transaction.source.kind == economy::exact_person_economy::account_kind::exact)
			contributors_with_exact_accounts.insert(transaction.source.exact_account_id);
		else if(transaction.source.kind == economy::exact_person_economy::account_kind::dcon) {
			auto payer = economy::accounts::owner_of(state, transaction.source.dcon_account);
			if(payer) contributors_with_dcon_accounts.insert(payer.index());
		}
	}

	// People and household cohorts fund the party whose platform best matches
	// their interests. The transfer, rather than a wealth score, is the source
	// of later campaign reach.
	for(auto const& voter : voters) {
		if(!(voter.adults > 0.0f) || !std::isfinite(voter.adults)) continue;
		dcon::organization_id supported{};
		float best_distance = std::numeric_limits<float>::infinity();
		for(auto party : active_parties) {
			auto distance = policy::distance(voter.ideal, parties::platform(state, party), voter.issue_salience);
			if(distance < best_distance) { best_distance = distance; supported = party; }
		}
		if(!supported) continue;
		account_ref source;
		dcon::economic_actor_id contributor{};
		auto available = spendable_cash(state, voter, source, contributor);
		if(!source || !(available > 0.0f)) continue;
		if((contributor && contributors_with_dcon_accounts.contains(contributor.index()))
			|| (source.kind == economy::exact_person_economy::account_kind::exact
				&& contributors_with_exact_accounts.contains(source.exact_account_id))) continue;
		auto amount = available * supporter_contribution_rate;
		if(amount >= 0.01f) {
			if(contributor) (void)contribute_from_actor(state, contributor, supported, amount);
			else (void)contribute_from_person(state, voter.person, supported, amount);
		}
	}

	// Parties convert a bounded part of their treasury into paid organizing.
	// Only real payroll recorded inside the election window affects the count.
	auto spending_today = campaign_spending_by_party(state, active_parties, election_date, election_date);
	for(auto party : active_parties) {
		if(auto it = spending_today.find(party.index()); it != spending_today.end() && it->second > 0.0f) continue;
		auto roster = parties::members(state, party);
		if(roster.empty()) continue;
		auto staff_count = std::min(campaign_staff_limit, roster.size());
		auto budget = party_funds(state, party) * party_campaign_budget_share;
		auto wage = budget / float(staff_count);
		if(!(wage >= 0.01f) || !std::isfinite(wage)) continue;
		for(size_t i = 0; i < staff_count; ++i)
			(void)pay_campaign_staff(state, party, persons::canonical_key(state, roster[i]), wage);
	}
}

} // namespace governance::political_resources
