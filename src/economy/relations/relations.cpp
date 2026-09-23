#include "relations.hpp"
#include "system_state.hpp"

#include <cmath>

namespace economy::relations {

namespace {
bool finite_nonnegative(float value) { return std::isfinite(value) && value >= 0.0f; }
}

dcon::transaction_id record_transaction(sys::state& state, dcon::economic_actor_id payer,
	dcon::economic_actor_id payee, float amount, dcon::commodity_id settlement,
	transaction_kind kind, sys::date timestamp) {
	if(!payer || !payee || !finite_nonnegative(amount)) return {};
	auto transaction = state.world.create_transaction();
	state.world.transaction_set_kind(transaction, uint8_t(kind));
	state.world.transaction_set_amount(transaction, amount);
	state.world.transaction_set_settlement_commodity(transaction, settlement);
	state.world.transaction_set_timestamp(transaction, timestamp);
	state.world.force_create_transaction_payer(transaction, payer);
	state.world.force_create_transaction_payee(transaction, payee);
	return transaction;
}

dcon::obligation_id create_obligation(sys::state& state, dcon::economic_actor_id debtor,
	dcon::economic_actor_id creditor, float principal, dcon::commodity_id settlement,
	sys::date creation_date, sys::date due_date, float annual_interest_rate, obligation_kind kind) {
	if(!debtor || !creditor || debtor == creditor || !std::isfinite(principal) || principal <= 0.0f
		|| !std::isfinite(annual_interest_rate) || annual_interest_rate < 0.0f)
		return {};
	auto obligation = state.world.create_obligation();
	state.world.obligation_set_original_principal(obligation, principal);
	state.world.obligation_set_principal_outstanding(obligation, principal);
	state.world.obligation_set_accrued_interest(obligation, 0.0f);
	state.world.obligation_set_settlement_commodity(obligation, settlement);
	state.world.obligation_set_creation_date(obligation, creation_date);
	state.world.obligation_set_due_date(obligation, due_date);
	state.world.obligation_set_annual_interest_rate(obligation, annual_interest_rate);
	state.world.obligation_set_last_interest_accrual_date(obligation, creation_date);
	state.world.obligation_set_status(obligation, uint8_t(obligation_status::active));
	state.world.obligation_set_kind(obligation, uint8_t(kind));
	state.world.force_create_obligation_debtor(obligation, debtor);
	state.world.force_create_obligation_creditor(obligation, creditor);
	return obligation;
}

float accrue_interest(sys::state& state, dcon::obligation_id obligation, uint32_t days) {
	if(!obligation || days == 0 || state.world.obligation_get_status(obligation) != uint8_t(obligation_status::active)) return 0.0f;
	auto outstanding = state.world.obligation_get_principal_outstanding(obligation);
	auto rate = state.world.obligation_get_annual_interest_rate(obligation);
	auto interest = outstanding * rate * float(days) / 365.0f;
	if(!finite_nonnegative(interest)) return 0.0f;
	state.world.obligation_set_accrued_interest(obligation,
		state.world.obligation_get_accrued_interest(obligation) + interest);
	return interest;
}

float total_due(sys::state const& state, dcon::obligation_id obligation) {
	if(!obligation) return 0.0f;
	return state.world.obligation_get_principal_outstanding(obligation)
		+ state.world.obligation_get_accrued_interest(obligation);
}

float repay_obligation(sys::state& state, dcon::obligation_id obligation, float amount) {
	if(!obligation || !finite_nonnegative(amount) || state.world.obligation_get_status(obligation) != uint8_t(obligation_status::active)) return 0.0f;
	auto interest = state.world.obligation_get_accrued_interest(obligation);
	auto principal = state.world.obligation_get_principal_outstanding(obligation);
	auto paid = std::min(amount, interest + principal);
	auto interest_paid = std::min(paid, interest);
	state.world.obligation_set_accrued_interest(obligation, interest - interest_paid);
	state.world.obligation_set_principal_outstanding(obligation, principal - (paid - interest_paid));
	if(total_due(state, obligation) <= 1.0e-6f) {
		state.world.obligation_set_accrued_interest(obligation, 0.0f);
		state.world.obligation_set_principal_outstanding(obligation, 0.0f);
		state.world.obligation_set_status(obligation, uint8_t(obligation_status::paid));
	}
	return paid;
}

bool write_off(sys::state& state, dcon::obligation_id obligation) {
	if(!obligation || state.world.obligation_get_status(obligation) != uint8_t(obligation_status::active)) return false;
	state.world.obligation_set_accrued_interest(obligation, 0.0f);
	state.world.obligation_set_principal_outstanding(obligation, 0.0f);
	state.world.obligation_set_status(obligation, uint8_t(obligation_status::written_off));
	return true;
}

float outstanding_between(sys::state const& state, dcon::economic_actor_id debtor,
	dcon::economic_actor_id creditor, dcon::commodity_id settlement) {
	float total = 0.0f;
	state.world.economic_actor_for_each_obligation_debtor_as_economic_actor(debtor, [&](dcon::obligation_debtor_id relation) {
		auto obligation = state.world.obligation_debtor_get_obligation(relation);
		if(state.world.obligation_get_economic_actor_from_obligation_debtor(obligation) == debtor
			&& state.world.obligation_get_economic_actor_from_obligation_creditor(obligation) == creditor
			&& state.world.obligation_get_settlement_commodity(obligation) == settlement
			&& state.world.obligation_get_status(obligation) == uint8_t(obligation_status::active))
			total += total_due(state, obligation);
	});
	return total;
}

} // namespace economy::relations
