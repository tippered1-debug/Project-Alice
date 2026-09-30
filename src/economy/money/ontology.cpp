#include "ontology.hpp"

#include "economy/accounts/accounts.hpp"
#include "economy/exact_person_economy.hpp"
#include "system_state.hpp"

#include <cmath>

namespace economy::monetary::ontology {

account_ref from_exact_economy(economy::exact_person_economy::account_ref value) {
	if(value.kind == economy::exact_person_economy::account_kind::dcon)
		return account_ref::from_monetary(value.dcon_account);
	if(value.kind == economy::exact_person_economy::account_kind::exact)
		return account_ref::from_exact_person(value.exact_account_id);
	return {};
}

bool describe(sys::state const& state, account_ref ref, account_view& result) {
	result = {};
	if(!ref) return false;
	result.ledger = ref.ledger;
	if(ref.ledger == ledger_kind::monetary_account) {
		if(!state.world.monetary_account_is_valid(ref.monetary)) return false;
		result.owner = accounts::owner_of(state, ref.monetary);
		result.settlement = accounts::settlement_of(state, ref.monetary);
		result.balance = accounts::balance(state, ref.monetary);
		result.instrument = state.world.monetary_account_get_organization_from_monetary_account_reserve_bank(ref.monetary)
			? instrument_kind::base_money_reserve : instrument_kind::operating_account;
	} else if(ref.ledger == ledger_kind::deposit_account) {
		if(!state.world.deposit_account_is_valid(ref.deposit)) return false;
		result.owner = state.world.deposit_account_get_economic_actor_from_deposit_account_owner(ref.deposit);
		result.settlement = state.world.deposit_account_get_commodity_from_deposit_account_settlement(ref.deposit);
		result.balance = state.world.deposit_account_get_balance(ref.deposit);
		result.instrument = instrument_kind::bank_deposit;
	} else if(ref.ledger == ledger_kind::exact_person_account) {
		auto exact_ref = economy::exact_person_economy::account_ref::from_exact(ref.exact_person_account_id);
		if(!economy::exact_person_economy::account_exists(state, exact_ref)) return false;
		result.exact_person_owner = economy::exact_person_economy::owner_of(state, exact_ref);
		result.settlement = economy::exact_person_economy::settlement_of(state, exact_ref);
		result.balance = economy::exact_person_economy::balance(state, exact_ref);
		result.instrument = instrument_kind::operating_account;
	} else {
		return false;
	}
	return (result.owner || result.exact_person_owner.source_population_cell != 0)
		&& result.settlement && state.world.commodity_is_valid(result.settlement)
		&& std::isfinite(result.balance) && result.balance >= 0.0f;
}

} // namespace economy::monetary::ontology
