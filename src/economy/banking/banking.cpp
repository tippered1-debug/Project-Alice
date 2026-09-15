#include "banking.hpp"

#include "actors/organizations/organizations.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/consent/consent.hpp"
#include "economy/relations/relations.hpp"
#include "system_state.hpp"

#include <algorithm>
#include <cmath>

namespace economy::banking {

namespace {

using actors::ownership::actor_kind;

bool valid_bank(sys::state const& state, dcon::organization_id bank) {
	if(!bank || !state.world.organization_is_valid(bank)) return false;
	if(state.world.organization_get_kind(bank) != uint8_t(actor_kind::bank)) return false;
	auto actor = actors::organizations::actor_for_organization(state, bank);
	return actor && state.world.economic_actor_get_kind(actor) == uint8_t(actor_kind::bank);
}

bool valid_nonnegative_amount(float amount) {
	return std::isfinite(amount) && amount >= 0.0f;
}

bool valid_positive_amount(float amount) {
	return std::isfinite(amount) && amount > 0.0f;
}

dcon::organization_id bank_for_loan(sys::state const& state, dcon::obligation_id loan) {
	if(!loan || !state.world.obligation_is_valid(loan)) return {};
	return actors::organizations::organization_for_actor(state,
		state.world.obligation_get_economic_actor_from_obligation_creditor(loan));
}

bool valid_active_loan_for_bank(sys::state const& state, dcon::obligation_id loan,
	dcon::organization_id bank) {
	return loan && state.world.obligation_is_valid(loan)
		&& state.world.obligation_get_kind(loan) == uint8_t(relations::obligation_kind::loan)
		&& state.world.obligation_get_status(loan) == uint8_t(relations::obligation_status::active)
		&& bank_for_loan(state, loan) == bank
		&& valid_bank(state, bank);
}

} // namespace

dcon::organization_id create_bank(sys::state& state) {
	return actors::organizations::create_organization(state, actor_kind::bank);
}

dcon::monetary_account_id reserve_account_for(sys::state const& state,
	dcon::organization_id bank, dcon::commodity_id settlement) {
	if(!valid_bank(state, bank) || !settlement) return {};
	dcon::monetary_account_id result{};
	bool duplicate = false;
	state.world.organization_for_each_monetary_account_reserve_bank_as_organization(bank,
		[&](dcon::monetary_account_reserve_bank_id relation) {
			auto account = state.world.monetary_account_reserve_bank_get_monetary_account(relation);
			if(!account || economy::accounts::settlement_of(state, account) != settlement) return;
			if(result) duplicate = true;
			else result = account;
		});
	return duplicate ? dcon::monetary_account_id{} : result;
}

dcon::monetary_account_id open_reserve_account(sys::state& state, dcon::organization_id bank,
	dcon::commodity_id settlement) {
	if(!valid_bank(state, bank) || !settlement || !state.world.commodity_is_valid(settlement)) return {};
	if(auto existing = reserve_account_for(state, bank, settlement)) return existing;
	uint32_t matching_reserves = 0;
	state.world.organization_for_each_monetary_account_reserve_bank_as_organization(bank,
		[&](dcon::monetary_account_reserve_bank_id relation) {
			auto account = state.world.monetary_account_reserve_bank_get_monetary_account(relation);
			if(account && economy::accounts::settlement_of(state, account) == settlement) ++matching_reserves;
		});
	if(matching_reserves != 0) return {};
	auto actor = actors::organizations::actor_for_organization(state, bank);
	auto account = economy::accounts::open_account(state, actor, settlement);
	if(!account) return {};
	state.world.force_create_monetary_account_reserve_bank(account, bank);
	return account;
}

bool bootstrap_set_reserve_balance(sys::state& state, dcon::monetary_account_id account,
	float amount) {
	if(!account || !state.world.monetary_account_is_valid(account)
		|| !valid_nonnegative_amount(amount)
		|| !state.world.monetary_account_get_organization_from_monetary_account_reserve_bank(account)
		|| !valid_bank(state, state.world.monetary_account_get_organization_from_monetary_account_reserve_bank(account))) return false;
	state.world.monetary_account_set_balance(account, amount);
	return true;
}

dcon::deposit_account_id open_deposit_account(sys::state& state, dcon::organization_id bank,
	dcon::economic_actor_id owner, dcon::commodity_id settlement) {
	if(!valid_bank(state, bank) || !owner || !state.world.economic_actor_is_valid(owner)
		|| !settlement || !state.world.commodity_is_valid(settlement)) return {};
	auto account = state.world.create_deposit_account();
	state.world.deposit_account_set_balance(account, 0.0f);
	state.world.force_create_deposit_account_bank(account, bank);
	state.world.force_create_deposit_account_owner(account, owner);
	state.world.force_create_deposit_account_settlement(account, settlement);
	return account;
}

float deposit_balance(sys::state const& state, dcon::deposit_account_id account) {
	return account && state.world.deposit_account_is_valid(account)
		? state.world.deposit_account_get_balance(account) : 0.0f;
}

bool bootstrap_set_deposit_balance(sys::state& state, dcon::deposit_account_id account,
	float amount) {
	if(!account || !state.world.deposit_account_is_valid(account)
		|| !valid_nonnegative_amount(amount)
		|| !valid_bank(state, state.world.deposit_account_get_organization_from_deposit_account_bank(account))
		|| !state.world.deposit_account_get_economic_actor_from_deposit_account_owner(account)
		|| !state.world.deposit_account_get_commodity_from_deposit_account_settlement(account)) return false;
	state.world.deposit_account_set_balance(account, amount);
	return true;
}

dcon::obligation_id originate_loan(sys::state& state, dcon::organization_id bank,
	dcon::deposit_account_id borrower_account, float principal, sys::date creation_date,
	sys::date due_date, float annual_interest_rate) {
	if(!valid_bank(state, bank) || !borrower_account
		|| !state.world.deposit_account_is_valid(borrower_account)
		|| state.world.deposit_account_get_organization_from_deposit_account_bank(borrower_account) != bank
		|| !valid_positive_amount(principal) || !std::isfinite(annual_interest_rate)
		|| annual_interest_rate < 0.0f) return {};
	auto borrower = state.world.deposit_account_get_economic_actor_from_deposit_account_owner(borrower_account);
	auto settlement = state.world.deposit_account_get_commodity_from_deposit_account_settlement(borrower_account);
	auto lender = actors::organizations::actor_for_organization(state, bank);
	if(!borrower || !settlement || !lender) return {};
	auto loan = relations::create_obligation(state, borrower, lender, principal, settlement,
		creation_date, due_date, annual_interest_rate, relations::obligation_kind::loan);
	if(!loan) return {};
	state.world.deposit_account_set_balance(borrower_account, deposit_balance(state, borrower_account) + principal);
	return loan;
}

dcon::obligation_id originate_loan_with_consent(sys::state& state, dcon::organization_id bank,
	dcon::deposit_account_id borrower_account, float principal, sys::date creation_date,
	sys::date due_date, float annual_interest_rate, dcon::economic_proposal_id proposal) {
	if(!proposal || !state.world.economic_proposal_is_valid(proposal)
		|| state.world.economic_proposal_get_kind(proposal) != uint8_t(economy::consent::proposal_kind::loan)
		|| !borrower_account || !state.world.deposit_account_is_valid(borrower_account)) return {};
	auto bank_actor = actors::organizations::actor_for_organization(state, bank);
	auto borrower = state.world.deposit_account_get_economic_actor_from_deposit_account_owner(borrower_account);
	auto settlement = state.world.deposit_account_get_commodity_from_deposit_account_settlement(borrower_account);
	if(state.world.economic_proposal_get_economic_actor_from_economic_proposal_actor_a(proposal) != bank_actor
		|| state.world.economic_proposal_get_economic_actor_from_economic_proposal_actor_b(proposal) != borrower
		|| state.world.economic_proposal_get_settlement(proposal) != settlement
		|| state.world.economic_proposal_get_amount(proposal) != principal
		|| !economy::consent::proposal_fully_accepted(state, proposal, creation_date)) return {};
	auto loan = originate_loan(state, bank, borrower_account, principal, creation_date, due_date, annual_interest_rate);
	if(!loan || !economy::consent::mark_executed(state, proposal)) return {};
	return loan;
}

bool transfer_deposit(sys::state& state, dcon::deposit_account_id source,
	dcon::deposit_account_id destination, float amount, sys::date timestamp) {
	if(!source || !destination || source == destination || !valid_positive_amount(amount)
		|| !state.world.deposit_account_is_valid(source) || !state.world.deposit_account_is_valid(destination)) return false;
	auto source_bank = state.world.deposit_account_get_organization_from_deposit_account_bank(source);
	auto destination_bank = state.world.deposit_account_get_organization_from_deposit_account_bank(destination);
	auto settlement = state.world.deposit_account_get_commodity_from_deposit_account_settlement(source);
	if(!valid_bank(state, source_bank) || !valid_bank(state, destination_bank)
		|| !settlement || state.world.deposit_account_get_commodity_from_deposit_account_settlement(destination) != settlement
		|| deposit_balance(state, source) < amount) return false;
	auto source_owner = state.world.deposit_account_get_economic_actor_from_deposit_account_owner(source);
	auto destination_owner = state.world.deposit_account_get_economic_actor_from_deposit_account_owner(destination);
	if(!source_owner || !destination_owner) return false;
	auto source_reserve = reserve_account_for(state, source_bank, settlement);
	auto destination_reserve = reserve_account_for(state, destination_bank, settlement);
	if(source_bank != destination_bank
		&& (!source_reserve || !destination_reserve || economy::accounts::balance(state, source_reserve) < amount)) return false;
	// Record the settlement only after every source, destination, currency, and reserve check passed.
	auto transaction = relations::record_transaction(state, source_owner, destination_owner, amount,
		settlement, relations::transaction_kind::transfer, timestamp);
	if(!transaction) return false;
	state.world.deposit_account_set_balance(source, deposit_balance(state, source) - amount);
	state.world.deposit_account_set_balance(destination, deposit_balance(state, destination) + amount);
	if(source_bank != destination_bank) {
		state.world.monetary_account_set_balance(source_reserve,
			economy::accounts::balance(state, source_reserve) - amount);
		state.world.monetary_account_set_balance(destination_reserve,
			economy::accounts::balance(state, destination_reserve) + amount);
	}
	return true;
}

float repay_loan(sys::state& state, dcon::obligation_id loan,
	dcon::deposit_account_id borrower_account, float amount, sys::date timestamp) {
	if(!valid_positive_amount(amount) || !borrower_account
		|| !state.world.deposit_account_is_valid(borrower_account)
		|| !valid_active_loan_for_bank(state, loan,
			state.world.deposit_account_get_organization_from_deposit_account_bank(borrower_account))) return 0.0f;
	auto borrower = state.world.deposit_account_get_economic_actor_from_deposit_account_owner(borrower_account);
	auto settlement = state.world.deposit_account_get_commodity_from_deposit_account_settlement(borrower_account);
	if(!borrower || !settlement
		|| state.world.obligation_get_economic_actor_from_obligation_debtor(loan) != borrower
		|| state.world.obligation_get_settlement_commodity(loan) != settlement) return 0.0f;
	auto accepted = std::min(amount, relations::total_due(state, loan));
	if(accepted <= 0.0f || deposit_balance(state, borrower_account) < accepted) return 0.0f;
	auto transaction = relations::record_transaction(state, borrower,
		state.world.obligation_get_economic_actor_from_obligation_creditor(loan), accepted, settlement,
		relations::transaction_kind::repayment, timestamp);
	if(!transaction) return 0.0f;
	state.world.deposit_account_set_balance(borrower_account, deposit_balance(state, borrower_account) - accepted);
	return relations::repay_obligation(state, loan, accepted);
}

float accrue_loan_interest(sys::state& state, dcon::obligation_id loan, uint32_t days) {
	if(!loan || !state.world.obligation_is_valid(loan)
		|| state.world.obligation_get_kind(loan) != uint8_t(relations::obligation_kind::loan)
		|| !valid_bank(state, bank_for_loan(state, loan))) return 0.0f;
	return relations::accrue_interest(state, loan, days);
}

bool write_off_loan(sys::state& state, dcon::obligation_id loan) {
	return loan && state.world.obligation_is_valid(loan)
		&& state.world.obligation_get_kind(loan) == uint8_t(relations::obligation_kind::loan)
		&& valid_bank(state, bank_for_loan(state, loan))
		&& relations::write_off(state, loan);
}

balance_sheet bank_balance_sheet(sys::state const& state, dcon::organization_id bank,
	dcon::commodity_id settlement) {
	balance_sheet result{};
	if(!valid_bank(state, bank) || !settlement || !state.world.commodity_is_valid(settlement)) return result;
	state.world.organization_for_each_monetary_account_reserve_bank_as_organization(bank,
		[&](dcon::monetary_account_reserve_bank_id relation) {
			auto account = state.world.monetary_account_reserve_bank_get_monetary_account(relation);
			if(account && economy::accounts::settlement_of(state, account) == settlement)
				result.settlement_assets += economy::accounts::balance(state, account);
		});
	state.world.organization_for_each_deposit_account_bank_as_organization(bank,
		[&](dcon::deposit_account_bank_id relation) {
			auto account = state.world.deposit_account_bank_get_deposit_account(relation);
			if(account && state.world.deposit_account_get_commodity_from_deposit_account_settlement(account) == settlement)
				result.deposit_liabilities += deposit_balance(state, account);
		});
	auto bank_actor = actors::organizations::actor_for_organization(state, bank);
	state.world.economic_actor_for_each_obligation_creditor_as_economic_actor(bank_actor,
		[&](dcon::obligation_creditor_id relation) {
			auto loan = state.world.obligation_creditor_get_obligation(relation);
			if(loan && state.world.obligation_get_kind(loan) == uint8_t(relations::obligation_kind::loan)
				&& state.world.obligation_get_settlement_commodity(loan) == settlement
				&& state.world.obligation_get_status(loan) != uint8_t(relations::obligation_status::paid)
				&& state.world.obligation_get_status(loan) != uint8_t(relations::obligation_status::written_off)
				&& relations::total_due(state, loan) > 0.0f)
				result.loan_assets += relations::total_due(state, loan);
			else if(loan && state.world.obligation_get_kind(loan) == uint8_t(relations::obligation_kind::public_debt)
				&& state.world.obligation_get_settlement_commodity(loan) == settlement
				&& state.world.obligation_get_status(loan) != uint8_t(relations::obligation_status::paid)
				&& state.world.obligation_get_status(loan) != uint8_t(relations::obligation_status::written_off)
				&& relations::total_due(state, loan) > 0.0f)
				result.public_debt_assets += relations::total_due(state, loan);
		});
	state.world.economic_actor_for_each_obligation_debtor_as_economic_actor(bank_actor,
		[&](dcon::obligation_debtor_id relation) {
			auto obligation = state.world.obligation_debtor_get_obligation(relation);
			if(obligation && state.world.obligation_get_settlement_commodity(obligation) == settlement
				&& state.world.obligation_get_status(obligation) != uint8_t(relations::obligation_status::paid)
				&& state.world.obligation_get_status(obligation) != uint8_t(relations::obligation_status::written_off)
				&& relations::total_due(state, obligation) > 0.0f)
				result.other_financial_liabilities += relations::total_due(state, obligation);
		});
	result.total_assets = result.settlement_assets + result.loan_assets + result.public_debt_assets;
	result.total_liabilities = result.deposit_liabilities + result.other_financial_liabilities;
	result.net_worth = result.total_assets - result.total_liabilities;
	return result;
}

} // namespace economy::banking
