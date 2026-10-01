#include "estates.hpp"

#include "system_state.hpp"
#include "actors/ownership.hpp"
#include "economy/banking/banking.hpp"
#include "economy/exact_person_economy.hpp"
#include "economy/households.hpp"
#include "economy/wallets.hpp"
#include "economy/physical/exact_person_goods.hpp"
#include "governance/governance.hpp"

#include <algorithm>
#include <set>
#include <vector>

namespace economy::estates {
namespace {
dcon::economic_actor_id actor_of(sys::state const& state, persons::person_key person) {
	auto profile = persons::materialized_profile(state, person);
	return profile ? persons::actor_for_person(state, profile) : dcon::economic_actor_id{};
}

// Ownership stakes and deposits held by the dead person's profile actor.
bool transfer_claims(sys::state& state, dcon::economic_actor_id dead, dcon::economic_actor_id heir) {
	if(!dead || !heir) return false;
	std::vector<dcon::ownership_stake_id> stakes;
	state.world.economic_actor_for_each_ownership_stake_owner_as_economic_actor(dead, [&](auto relation) {
		stakes.push_back(state.world.ownership_stake_owner_get_ownership_stake(relation));
	});
	for(auto stake : stakes) {
		auto asset = state.world.ownership_stake_get_asset_from_ownership_stake_asset(stake);
		dcon::ownership_stake_id existing{};
		state.world.asset_for_each_ownership_stake_asset_as_asset(asset, [&](auto relation) {
			auto other = state.world.ownership_stake_asset_get_ownership_stake(relation);
			if(other != stake && state.world.ownership_stake_get_economic_actor_from_ownership_stake_owner(other) == heir) existing = other;
		});
		if(existing) {
			// The heir already holds a stake: merge the fractions into it.
			auto ownership = state.world.ownership_stake_get_ownership_fraction(existing) + state.world.ownership_stake_get_ownership_fraction(stake);
			auto voting = state.world.ownership_stake_get_voting_fraction(existing) + state.world.ownership_stake_get_voting_fraction(stake);
			auto economic = state.world.ownership_stake_get_economic_fraction(existing) + state.world.ownership_stake_get_economic_fraction(stake);
			state.world.delete_ownership_stake(stake);
			(void)actors::ownership::set_stake_fractions(state, existing, ownership, voting, economic);
		} else {
			state.world.ownership_stake_set_economic_actor_from_ownership_stake_owner(stake, heir);
		}
	}
	std::vector<dcon::deposit_account_id> deposits;
	state.world.economic_actor_for_each_deposit_account_owner_as_economic_actor(dead, [&](auto relation) {
		deposits.push_back(state.world.deposit_account_owner_get_deposit_account(relation));
	});
	for(auto deposit : deposits) {
		auto bank = state.world.deposit_account_get_organization_from_deposit_account_bank(deposit);
		dcon::deposit_account_id heir_deposit{};
		state.world.economic_actor_for_each_deposit_account_owner_as_economic_actor(heir, [&](auto relation) {
			auto candidate = state.world.deposit_account_owner_get_deposit_account(relation);
			if(state.world.deposit_account_get_organization_from_deposit_account_bank(candidate) == bank) heir_deposit = candidate;
		});
		auto balance = economy::banking::deposit_balance(state, deposit);
		if(!heir_deposit) state.world.deposit_account_set_economic_actor_from_deposit_account_owner(deposit, heir);
		else if(balance > 0.0f) (void)economy::banking::transfer_deposit(state, deposit, heir_deposit, balance, state.current_date);
	}
	return true;
}
}

dcon::economic_actor_id heir_for(sys::state const& state, persons::person_key person) {
	auto home = persons::home_site(state, person);
	auto province = home ? state.world.site_get_province_from_site_location(home) : dcon::province_id{};
	if(!province) return {};
	auto role = economy::households::role_for_pop_type(state, persons::source_pop_type(state, person));
	if(auto cohort = economy::households::household_for(state, province, role))
		return actors::organizations::actor_for_organization(state, cohort);
	auto nation = state.world.province_get_nation_from_province_ownership(province);
	if(!nation) return {};
	for(auto institution : governance::institutions_of(state, nation))
		if(state.world.institution_get_kind(institution) == uint8_t(governance::institution_kind::central_government))
			return governance::actor_for_institution(state, institution);
	return {};
}

bool settle(sys::state& state, persons::person_key person) {
	if(!persons::exists(state, person) || persons::alive(state, person)) return false;
	auto heir = heir_for(state, person);
	if(!heir) return false;
	bool settled = true;
	for(auto account : economy::exact_person_economy::accounts_for_person(state, person)) {
		auto cash = economy::exact_person_economy::balance(state, account);
		if(!(cash > 0.0f)) continue;
		auto destination = economy::wallets::open_for(state, heir, economy::exact_person_economy::settlement_of(state, account));
		if(!economy::wallets::pay(state, account, destination, cash, relations::transaction_kind::inheritance)) settled = false;
	}
	if(!economy::physical::exact_person_goods::release_to(state, person, heir)) settled = false;
	if(auto dead = actor_of(state, person)) (void)transfer_claims(state, dead, heir);
	return settled;
}

void process(sys::state& state) {
	if(!state.exact_person_economy || !state.exact_population) return;
	std::set<std::pair<uint32_t, uint64_t>> candidates;
	for(auto owner : economy::exact_person_economy::account_owners(state))
		if(!persons::alive(state, owner)) {
			bool funded = false;
			for(auto account : economy::exact_person_economy::accounts_for_person(state, owner))
				if(economy::exact_person_economy::balance(state, account) > 0.0f) funded = true;
			if(funded) candidates.emplace(owner.source_population_cell, owner.ordinal);
		}
	if(state.exact_person_goods)
		for(auto const& need : economy::physical::exact_person_goods::need_records(state))
			if(!persons::alive(state, need.owner)) candidates.emplace(need.owner.source_population_cell, need.owner.ordinal);
	// Stakes and deposits of dead materialized persons.
	state.world.for_each_ownership_stake([&](dcon::ownership_stake_id stake) {
		auto owner = state.world.ownership_stake_get_economic_actor_from_ownership_stake_owner(stake);
		auto profile = owner ? state.world.economic_actor_get_person_from_person_actor(owner) : dcon::person_id{};
		if(!profile) return;
		auto key = persons::canonical_key(state, profile);
		if(key.source_population_cell != 0 && !persons::alive(state, key)) candidates.emplace(key.source_population_cell, key.ordinal);
	});
	state.world.for_each_deposit_account([&](dcon::deposit_account_id deposit) {
		auto owner = state.world.deposit_account_get_economic_actor_from_deposit_account_owner(deposit);
		auto profile = owner ? state.world.economic_actor_get_person_from_person_actor(owner) : dcon::person_id{};
		if(!profile) return;
		auto key = persons::canonical_key(state, profile);
		if(key.source_population_cell != 0 && !persons::alive(state, key)) candidates.emplace(key.source_population_cell, key.ordinal);
	});
	for(auto const& [cell, ordinal] : candidates) (void)settle(state, persons::person_key{ cell, ordinal });
}

} // namespace economy::estates
