#include "liquidity.hpp"

#include "system_state.hpp"
#include "actors/organizations/organizations.hpp"
#include "actors/ownership.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/banking/banking.hpp"
#include "economy/households.hpp"
#include "economy/monetary_policy.hpp"
#include "economy/wallets.hpp"
#include "economy/physical/concrete_market.hpp"
#include "persons/exact_population.hpp"
#include "persons/persons.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

namespace economy::liquidity {
namespace {
constexpr float epsilon = 1.0e-3f;

dcon::nation_id owner_of_province(sys::state const& state, dcon::province_id province) {
	return province ? state.world.province_get_nation_from_province_ownership(province) : dcon::nation_id{};
}

bool is_bank_actor(sys::state const& state, dcon::economic_actor_id actor) {
	return state.world.economic_actor_get_kind(actor) == uint8_t(actors::ownership::actor_kind::bank);
}

// Settlements in which an actor holds wallet cash.
std::vector<dcon::commodity_id> wallet_settlements(sys::state const& state, dcon::economic_actor_id actor) {
	std::set<uint32_t> settlements;
	auto profile = state.world.economic_actor_get_person_from_person_actor(actor);
	if(profile) {
		auto key = persons::canonical_key(state, profile);
		if(key.source_population_cell != 0)
			for(auto account : economy::exact_person_economy::accounts_for_person(state, key))
				settlements.insert(economy::exact_person_economy::settlement_of(state, account).index());
	} else {
		state.world.economic_actor_for_each_monetary_account_owner_as_economic_actor(actor, [&](auto relation) {
			auto account = state.world.monetary_account_owner_get_monetary_account(relation);
			if(!state.world.monetary_account_get_organization_from_monetary_account_reserve_bank(account))
				settlements.insert(accounts::settlement_of(state, account).index());
		});
	}
	std::vector<dcon::commodity_id> result;
	for(auto index : settlements) result.push_back(dcon::commodity_id{ dcon::commodity_id::value_base_t(index) });
	return result;
}
}

dcon::nation_id nation_for(sys::state const& state, dcon::economic_actor_id actor) {
	if(!actor) return {};
	if(auto profile = state.world.economic_actor_get_person_from_person_actor(actor)) {
		auto key = persons::canonical_key(state, profile);
		auto home = key.source_population_cell != 0 ? persons::home_site(state, key) : dcon::site_id{};
		return home ? owner_of_province(state, state.world.site_get_province_from_site_location(home)) : dcon::nation_id{};
	}
	auto organization = actors::organizations::organization_for_actor(state, actor);
	if(!organization) return {};
	if(economy::households::is_household(state, organization))
		return owner_of_province(state, economy::households::home_of(state, organization));
	dcon::nation_id result{};
	for(auto factory : actors::organizations::factories_operated_by(state, organization)) {
		auto site = state.world.factory_get_site_from_factory_site(factory);
		if(auto nation = site ? owner_of_province(state, state.world.site_get_province_from_site_location(site)) : dcon::nation_id{}) {
			result = nation;
			break;
		}
	}
	return result;
}

dcon::organization_id bank_for(sys::state const& state, dcon::nation_id nation, dcon::commodity_id settlement) {
	dcon::organization_id best{};
	float best_liquidity = -1.0f;
	if(!nation || !settlement) return best;
	state.world.for_each_organization([&](dcon::organization_id bank) {
		if(state.world.organization_get_kind(bank) != uint8_t(actors::ownership::actor_kind::bank)
			|| state.world.organization_get_bank_jurisdiction(bank) != nation
			|| state.world.organization_get_bank_settlement_currency(bank) != settlement
			|| economy::banking::status_of(state, bank) != economy::banking::bank_status::solvent
			|| !economy::banking::reserve_account_for(state, bank, settlement)) return;
		auto liquidity = economy::banking::bank_balance_sheet(state, bank, settlement).liquidity_ratio;
		if(!std::isfinite(liquidity)) return;
		if(!best || liquidity > best_liquidity
			|| (liquidity == best_liquidity && state.world.organization_get_canonical_id(bank) < state.world.organization_get_canonical_id(best))) {
			best = bank;
			best_liquidity = liquidity;
		}
	});
	return best;
}

void manage(sys::state& state, dcon::economic_actor_id actor, dcon::commodity_id settlement) {
	auto wallet = economy::wallets::account_for(state, actor, settlement);
	if(!wallet) return;
	auto cash = economy::wallets::spendable(state, wallet);
	auto deposit = economy::banking::deposit_account_for(state, actor, settlement);
	if(deposit) {
		auto bank = state.world.deposit_account_get_organization_from_deposit_account_bank(deposit);
		auto held = std::max(0.0f, economy::banking::deposit_balance(state, deposit)
			- economy::physical::concrete_market::reserved_deposit_amount(state, deposit));
		if(economy::banking::status_of(state, bank) != economy::banking::bank_status::solvent) {
			// A depositor of a troubled bank takes out everything the bank can pay.
			if(held > epsilon) (void)economy::banking::withdraw_cash(state, deposit, wallet, held, state.current_date);
			return;
		}
		auto total = cash + held;
		auto target = cash_share_target * total;
		if(cash < cash_share_floor * total && held > epsilon)
			(void)economy::banking::withdraw_cash(state, deposit, wallet, std::min(held, target - cash), state.current_date);
		else if(cash > cash_share_ceiling * total)
			(void)economy::banking::deposit_cash(state, deposit, wallet, cash - target, state.current_date);
		return;
	}
	if(cash <= minimum_idle_cash) return;
	auto bank = bank_for(state, nation_for(state, actor), settlement);
	if(!bank) return;
	actors::ownership::assign_runtime_canonical_id(state, actor);
	auto opened = economy::banking::open_deposit_account(state, bank, actor, settlement);
	if(opened) (void)economy::banking::deposit_cash(state, opened, wallet, (1.0f - cash_share_target) * cash, state.current_date);
}

void recognize_savers(sys::state& state) {
	if(!state.exact_population || !state.exact_person_economy) return;
	for(auto key : economy::exact_person_economy::account_owners(state)) {
		if(persons::exact_population::profile_for_person(state, key)
			|| !persons::exact_population::alive(state, key)) continue;
		float cash = 0.0f;
		for(auto account : economy::exact_person_economy::accounts_for_person(state, key))
			cash += economy::wallets::spendable(state, account);
		if(cash > saver_cash_threshold) (void)persons::materialize_profile(state, key);
	}
}

void process(sys::state& state) {
	recognize_savers(state);
	std::vector<dcon::economic_actor_id> actors;
	state.world.for_each_economic_actor([&](dcon::economic_actor_id actor) {
		// Banks are the depositories, and public treasuries and central banks hold base money.
		if(is_bank_actor(state, actor) || state.world.economic_actor_get_institution_from_institution_actor(actor)
			|| economy::monetary_policy::is_central_bank(state, actors::organizations::organization_for_actor(state, actor))) return;
		actors.push_back(actor);
	});
	std::sort(actors.begin(), actors.end(), [](auto left, auto right) { return left.index() < right.index(); });
	for(auto actor : actors)
		for(auto settlement : wallet_settlements(state, actor)) manage(state, actor, settlement);
}

} // namespace economy::liquidity
