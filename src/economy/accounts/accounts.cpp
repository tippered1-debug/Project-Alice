#include "accounts.hpp"
#include "system_state.hpp"

#include <cmath>

namespace economy::accounts {

namespace {
bool valid_amount(float value) { return std::isfinite(value) && value > 0.0f; }
}

dcon::monetary_account_id open_account(sys::state& state, dcon::economic_actor_id owner, dcon::commodity_id settlement) {
	if(!owner || !settlement || !state.world.commodity_is_valid(settlement)) return {};
	auto account = state.world.create_monetary_account();
	state.world.monetary_account_set_balance(account, 0.0f);
	state.world.force_create_monetary_account_owner(account, owner);
	state.world.force_create_monetary_account_settlement(account, settlement);
	return account;
}

dcon::monetary_account_id find_account(sys::state const& state, dcon::economic_actor_id owner, dcon::commodity_id settlement) {
	dcon::monetary_account_id result{};
	state.world.economic_actor_for_each_monetary_account_owner_as_economic_actor(owner, [&](dcon::monetary_account_owner_id relation) {
		auto account = state.world.monetary_account_owner_get_monetary_account(relation);
		if(!result && state.world.monetary_account_get_commodity_from_monetary_account_settlement(account) == settlement)
			result = account;
	});
	return result;
}

dcon::economic_actor_id owner_of(sys::state const& state, dcon::monetary_account_id account) {
	return account ? state.world.monetary_account_get_economic_actor_from_monetary_account_owner(account) : dcon::economic_actor_id{};
}

dcon::commodity_id settlement_of(sys::state const& state, dcon::monetary_account_id account) {
	return account ? state.world.monetary_account_get_commodity_from_monetary_account_settlement(account) : dcon::commodity_id{};
}

float balance(sys::state const& state, dcon::monetary_account_id account) {
	return account ? state.world.monetary_account_get_balance(account) : 0.0f;
}

bool bootstrap_set_balance(sys::state& state, dcon::monetary_account_id account, float amount) {
	if(!account || !std::isfinite(amount) || amount < 0.0f) return false;
	state.world.monetary_account_set_balance(account, amount);
	return true;
}

dcon::transaction_id transfer(sys::state& state, dcon::monetary_account_id source,
	dcon::monetary_account_id destination, float amount, relations::transaction_kind kind, sys::date timestamp) {
	if(!source || !destination || source == destination || !valid_amount(amount)) return {};
	auto source_owner = owner_of(state, source);
	auto destination_owner = owner_of(state, destination);
	auto settlement = settlement_of(state, source);
	if(!source_owner || !destination_owner || !settlement || !state.world.commodity_is_valid(settlement)
		|| settlement_of(state, destination) != settlement) return {};
	if(balance(state, source) < amount) return {};
	// All validation is complete before either balance or transaction is changed.
	state.world.monetary_account_set_balance(source, balance(state, source) - amount);
	state.world.monetary_account_set_balance(destination, balance(state, destination) + amount);
	auto transaction = relations::record_transaction(state, source_owner, destination_owner, amount, settlement, kind, timestamp);
	if(!transaction) {
		state.world.monetary_account_set_balance(source, balance(state, source) + amount);
		state.world.monetary_account_set_balance(destination, balance(state, destination) - amount);
		return {};
	}
	state.world.force_create_transaction_source_account(transaction, source);
	state.world.force_create_transaction_destination_account(transaction, destination);
	return transaction;
}

dcon::transaction_id settle_obligation_payment(sys::state& state, dcon::obligation_id obligation,
	dcon::monetary_account_id debtor_account, dcon::monetary_account_id creditor_account,
	float requested_amount, sys::date timestamp) {
	if(!obligation || !valid_amount(requested_amount)
		|| state.world.obligation_get_status(obligation) != uint8_t(relations::obligation_status::active)) return {};
	auto debtor = state.world.obligation_get_economic_actor_from_obligation_debtor(obligation);
	auto creditor = state.world.obligation_get_economic_actor_from_obligation_creditor(obligation);
	auto settlement = state.world.obligation_get_settlement_commodity(obligation);
	auto accepted = std::min(requested_amount, relations::total_due(state, obligation));
	if(!debtor_account || !creditor_account || owner_of(state, debtor_account) != debtor
		|| owner_of(state, creditor_account) != creditor || settlement_of(state, debtor_account) != settlement
		|| settlement_of(state, creditor_account) != settlement || balance(state, debtor_account) < accepted) return {};
	// Apply the obligation only after account validation, then emit one settlement record.
	state.world.monetary_account_set_balance(debtor_account, balance(state, debtor_account) - accepted);
	state.world.monetary_account_set_balance(creditor_account, balance(state, creditor_account) + accepted);
	auto paid = relations::repay_obligation(state, obligation, accepted);
	if(paid != accepted) return {};
	auto transaction = relations::record_transaction(state, debtor, creditor, accepted, settlement,
		relations::transaction_kind::repayment, timestamp);
	if(!transaction) return {};
	state.world.force_create_transaction_source_account(transaction, debtor_account);
	state.world.force_create_transaction_destination_account(transaction, creditor_account);
	return transaction;
}

} // namespace economy::accounts
