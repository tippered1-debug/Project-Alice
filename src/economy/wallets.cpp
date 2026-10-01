#include "wallets.hpp"

#include "system_state.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/physical/concrete_market.hpp"
#include "economy/physical/exact_person_goods.hpp"
#include "governance/finance/finance.hpp"
#include "persons/persons.hpp"

#include <algorithm>
#include <cmath>

namespace economy::wallets {
namespace {
persons::person_key person_of(sys::state const& state, dcon::economic_actor_id actor) {
	auto person = actor ? state.world.economic_actor_get_person_from_person_actor(actor) : dcon::person_id{};
	return person ? persons::canonical_key(state, person) : persons::person_key{};
}

dcon::institution_id institution_of(sys::state const& state, dcon::economic_actor_id actor) {
	return actor ? state.world.economic_actor_get_institution_from_institution_actor(actor) : dcon::institution_id{};
}
}

account_ref account_for(sys::state const& state, dcon::economic_actor_id actor, dcon::commodity_id settlement) {
	if(!actor || !settlement) return {};
	if(auto key = person_of(state, actor); key.source_population_cell != 0)
		return economy::exact_person_economy::find_account(state, key, settlement);
	if(auto institution = institution_of(state, actor))
		return account_ref::from_dcon(governance::finance::treasury_account_for(state, institution, settlement));
	auto account = economy::accounts::find_account(state, actor, settlement);
	return account ? account_ref::from_dcon(account) : account_ref{};
}

account_ref open_for(sys::state& state, dcon::economic_actor_id actor, dcon::commodity_id settlement) {
	if(auto existing = account_for(state, actor, settlement)) return existing;
	if(!actor || !settlement) return {};
	if(auto key = person_of(state, actor); key.source_population_cell != 0)
		return economy::exact_person_economy::open_account(state, key, settlement);
	if(auto institution = institution_of(state, actor))
		return account_ref::from_dcon(governance::finance::open_treasury_account(state, institution, settlement));
	auto account = economy::accounts::open_account(state, actor, settlement);
	return account ? account_ref::from_dcon(account) : account_ref{};
}

float spendable(sys::state const& state, account_ref account) {
	if(!account) return 0.0f;
	float result = 0.0f;
	if(account.kind == economy::exact_person_economy::account_kind::exact)
		result = economy::exact_person_economy::balance(state, account)
			- economy::physical::exact_person_goods::reserved_bid_amount(state, account.exact_account_id);
	else
		result = economy::accounts::balance(state, account.dcon_account)
			- economy::physical::concrete_market::reserved_bid_amount(state, account.dcon_account);
	return std::isfinite(result) ? std::max(0.0f, result) : 0.0f;
}

bool pay(sys::state& state, account_ref source, account_ref destination, float amount, relations::transaction_kind kind) {
	if(!source || !destination || !(amount > 0.0f) || !std::isfinite(amount) || source == destination) return false;
	return economy::exact_person_economy::transfer(state, source, destination, amount, kind, state.current_date);
}

} // namespace economy::wallets
