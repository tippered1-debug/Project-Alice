#include "relations.hpp"
#include "system_state.hpp"

#include <cmath>
#include <limits>

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
	if(!debtor || !creditor || debtor == creditor || !std::isfinite(principal) || principal <= 0.0f || !std::isfinite(annual_interest_rate) || annual_interest_rate < 0.0f)
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
	state.world.obligation_set_collateral_value(obligation, 0.0f);
	state.world.obligation_set_recovered_amount(obligation, 0.0f);
	state.world.obligation_set_written_off_amount(obligation, 0.0f);
	state.world.force_create_obligation_debtor(obligation, debtor);
	state.world.force_create_obligation_creditor(obligation, creditor);
	return obligation;
}

float accrue_interest(sys::state& state, dcon::obligation_id obligation, uint32_t days) {
	if(!obligation || days == 0) return 0.0f;
	auto status = state.world.obligation_get_status(obligation);
	if(status != uint8_t(obligation_status::active)
		&& status != uint8_t(obligation_status::defaulted)) return 0.0f;
	auto outstanding = state.world.obligation_get_principal_outstanding(obligation);
	auto rate = state.world.obligation_get_annual_interest_rate(obligation);
	auto accrued = state.world.obligation_get_accrued_interest(obligation);
	auto exact_interest = double(outstanding) * double(rate) * double(days) / 365.0;
	auto exact_total = double(accrued) + exact_interest;
	if(!finite_nonnegative(outstanding) || !finite_nonnegative(rate) || !finite_nonnegative(accrued)
		|| !std::isfinite(exact_interest) || !std::isfinite(exact_total)
		|| exact_interest > std::numeric_limits<float>::max()
		|| exact_total > std::numeric_limits<float>::max()) return 0.0f;
	auto interest = float(exact_interest);
	state.world.obligation_set_accrued_interest(obligation, float(exact_total));
	return interest;
}

float total_due(sys::state const& state, dcon::obligation_id obligation) {
	if(!obligation) return 0.0f;
	return state.world.obligation_get_principal_outstanding(obligation)
		+ state.world.obligation_get_accrued_interest(obligation);
}

float repay_obligation(sys::state& state, dcon::obligation_id obligation, float amount, bool allow_defaulted) {
	if(!obligation || !finite_nonnegative(amount)) return 0.0f;
	auto status = state.world.obligation_get_status(obligation);
	if(status != uint8_t(obligation_status::active)
		&& !(allow_defaulted && status == uint8_t(obligation_status::defaulted))) return 0.0f;
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
		auto status = state.world.obligation_get_status(obligation);
		if(state.world.obligation_get_economic_actor_from_obligation_debtor(obligation) == debtor && state.world.obligation_get_economic_actor_from_obligation_creditor(obligation) == creditor && state.world.obligation_get_settlement_commodity(obligation) == settlement
			&& (status == uint8_t(obligation_status::active)
				|| status == uint8_t(obligation_status::defaulted)))
			total += total_due(state, obligation);
	});
	return total;
}

} // namespace economy::relations
