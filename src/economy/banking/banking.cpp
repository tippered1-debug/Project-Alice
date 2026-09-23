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

namespace {
dcon::obligation_id execute_loan_raw(sys::state& state, dcon::organization_id bank,
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
	// Bound total lending by the bank's actual equity capital and reserve-backed
	// deposit capacity. A loan cannot be created against an empty bank shell.
	auto sheet = bank_balance_sheet(state, bank, settlement);
	auto capital_headroom = std::max(0.0f, sheet.net_worth * 10.0f - sheet.loan_assets);
	if(principal > capital_headroom + 1.0e-5f
		|| sheet.deposit_liabilities + principal > sheet.settlement_assets * 10.0f + 1.0e-5f) return {};
	auto loan = relations::create_obligation(state, borrower, lender, principal, settlement,
		creation_date, due_date, annual_interest_rate, relations::obligation_kind::loan);
	if(!loan) return {};
	state.world.deposit_account_set_balance(borrower_account, deposit_balance(state, borrower_account) + principal);
	return loan;
}

dcon::obligation_id originate_factory_loan_to_operating_account(sys::state& state,
	dcon::organization_id bank, dcon::factory_id factory, dcon::monetary_account_id operating_account,
	float principal, float annual_interest_rate) {
	if(!valid_bank(state, bank) || !factory || !operating_account
		|| !state.world.monetary_account_is_valid(operating_account)) return {};
	auto borrower = accounts::owner_of(state, operating_account);
	auto settlement = accounts::settlement_of(state, operating_account);
	auto lender = actors::organizations::actor_for_organization(state, bank);
	auto reserve = reserve_account_for(state, bank, settlement);
	if(!borrower || borrower != actors::organizations::operator_actor_for_factory(state, factory)
		|| !settlement || !reserve) return {};
	auto sheet = bank_balance_sheet(state, bank, settlement);
	// Keep 10% of deposit liabilities liquid. These proceeds leave the bank's
	// deposit system immediately, so reserves must cover the actual payout.
	// The bank also retains 10% of its positive net worth as base liquidity.
	auto available_liquidity = std::max(0.0f, accounts::balance(state, reserve)
		- std::max(sheet.deposit_liabilities, std::max(0.0f, sheet.net_worth)) * 0.10f);
	if(principal > available_liquidity + 1.0e-5f) return {};
	auto deposit = open_deposit_account(state, bank, borrower, settlement);
	if(!deposit) return {};
	auto loan = execute_loan_raw(state, bank, deposit, principal, state.current_date,
		state.current_date + 365, annual_interest_rate);
	if(!loan) return {};
	auto issuance = relations::record_transaction(state, lender, borrower, principal, settlement,
		relations::transaction_kind::loan_issuance, state.current_date);
	if(!issuance) {
		state.world.delete_obligation(loan);
		state.world.deposit_account_set_balance(deposit, 0.0f);
		return {};
	}
	// Execute exactly one origination: principal is first booked to the bank
	// deposit by execute_loan_raw(), then paid out from reserves to operating cash.
	state.world.deposit_account_set_balance(deposit, deposit_balance(state, deposit) - principal);
	state.world.monetary_account_set_balance(reserve, accounts::balance(state, reserve) - principal);
	state.world.monetary_account_set_balance(operating_account,
		accounts::balance(state, operating_account) + principal);
	return loan;
}
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
		|| state.world.economic_proposal_get_due_date(proposal) != due_date
		|| state.world.economic_proposal_get_annual_interest_rate(proposal) != annual_interest_rate
		|| !economy::consent::proposal_fully_accepted(state, proposal, creation_date)) return {};
	auto loan = execute_loan_raw(state, bank, borrower_account, principal, creation_date, due_date, annual_interest_rate);
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

float indicative_factory_loan_rate(sys::state const& state, dcon::factory_id factory,
	dcon::commodity_id settlement, float requested_amount, float collateral_value) {
	if(!factory || !state.world.factory_is_valid(factory) || !settlement
		|| !valid_positive_amount(requested_amount) || !valid_nonnegative_amount(collateral_value)) return 0.18f;
	auto borrower = actors::organizations::operator_actor_for_factory(state, factory);
	if(!borrower) return 0.18f;
	float existing_debt = 0.0f;
	uint32_t defaulted_loans = 0;
	uint32_t repaid_loans = 0;
	state.world.economic_actor_for_each_obligation_debtor_as_economic_actor(borrower,
		[&](dcon::obligation_debtor_id relation) {
			auto loan = state.world.obligation_debtor_get_obligation(relation);
			if(!loan || state.world.obligation_get_kind(loan) != uint8_t(relations::obligation_kind::loan)
				|| state.world.obligation_get_settlement_commodity(loan) != settlement) return;
			auto linked_factory = state.world.obligation_get_factory_from_obligation_factory(loan);
			if(linked_factory && linked_factory != factory) return;
			auto status = state.world.obligation_get_status(loan);
			if(status == uint8_t(relations::obligation_status::active)
				|| status == uint8_t(relations::obligation_status::defaulted))
				existing_debt += relations::total_due(state, loan);
			if(status == uint8_t(relations::obligation_status::defaulted)) ++defaulted_loans;
			if(status == uint8_t(relations::obligation_status::paid)) ++repaid_loans;
		});
	float daily_cashflow = std::max(0.0f, std::min(state.world.factory_get_agency_cashflow(factory),
		state.world.factory_get_agency_recent_profit(factory)));
	float annual_cashflow = daily_cashflow * 365.0f;
	float distress = std::min(1.0f, float(state.world.factory_get_agency_distress_days(factory)) / 120.0f);
	float cashflow_coverage = std::clamp(annual_cashflow / std::max(1.0f, requested_amount * 1.5f), 0.0f, 1.0f);
	float collateral_coverage = std::clamp(collateral_value / std::max(1.0f, requested_amount * 1.25f), 0.0f, 1.0f);
	float repayment_history = std::clamp(0.65f + 0.05f * float(std::min<uint32_t>(repaid_loans, 4))
		- 0.35f * float(defaulted_loans), 0.0f, 1.0f);
	float score = std::clamp(0.45f * cashflow_coverage + 0.30f * collateral_coverage
		+ 0.15f * repayment_history + 0.10f * (1.0f - distress), 0.0f, 1.0f);
	return 0.04f + (1.0f - score) * 0.14f
		+ std::min(0.06f, existing_debt / std::max(1.0f, collateral_value) * 0.03f);
}

factory_credit_result underwrite_factory_credit(sys::state& state, dcon::factory_id factory,
	dcon::monetary_account_id operating_account, uint8_t request_kind, float requested_amount,
	float expected_annual_return, float collateral_value, dcon::capital_project_id project) {
	factory_credit_result result{};
	if(!factory || !state.world.factory_is_valid(factory) || !operating_account
		|| !state.world.monetary_account_is_valid(operating_account)
		|| !valid_positive_amount(requested_amount) || !std::isfinite(expected_annual_return)
		|| !valid_nonnegative_amount(collateral_value)) return result;
	auto borrower = actors::organizations::operator_actor_for_factory(state, factory);
	auto settlement = accounts::settlement_of(state, operating_account);
	if(!borrower || accounts::owner_of(state, operating_account) != borrower || !settlement) return result;

	result.requested_amount = requested_amount;
	auto request = state.world.create_firm_capital_request();
	result.request = request;
	state.world.firm_capital_request_set_request_kind(request, request_kind);
	state.world.firm_capital_request_set_status(request, 0); // pending underwriting
	state.world.firm_capital_request_set_settlement(request, settlement);
	state.world.firm_capital_request_set_requested_amount(request, requested_amount);
	state.world.firm_capital_request_set_funded_amount(request, 0.0f);
	state.world.firm_capital_request_set_annual_interest_rate(request, 0.0f);
	state.world.firm_capital_request_set_expected_annual_return(request, expected_annual_return);
	state.world.firm_capital_request_set_collateral_value(request, collateral_value);
	state.world.firm_capital_request_set_underwriting_score(request, 0.0f);
	state.world.firm_capital_request_set_requested_on(request, state.current_date);
	state.world.firm_capital_request_set_maturity_date(request, state.current_date + 365);
	state.world.force_create_firm_capital_request_factory(request, factory);
	if(project) state.world.force_create_firm_capital_request_project(request, project);

	dcon::organization_id bank{};
	state.world.for_each_organization([&](dcon::organization_id candidate) {
		if(bank || state.world.organization_get_kind(candidate) != uint8_t(actor_kind::bank)
			|| !actors::organizations::actor_for_organization(state, candidate)) return;
		auto reserve = reserve_account_for(state, candidate, settlement);
		if(!reserve) return;
		auto sheet = bank_balance_sheet(state, candidate, settlement);
		auto capital_headroom = std::max(0.0f, sheet.net_worth * 10.0f - sheet.loan_assets);
		auto liquidity = std::max(0.0f, accounts::balance(state, reserve)
			- std::max(sheet.deposit_liabilities, std::max(0.0f, sheet.net_worth)) * 0.10f);
		if(std::min(capital_headroom, liquidity) > 1.0e-5f) bank = candidate;
	});
	if(!valid_bank(state, bank)) {
		state.world.firm_capital_request_set_status(request, 3);
		return result;
	}
	state.world.force_create_firm_capital_request_bank(request, bank);

	float existing_debt = 0.0f;
	uint32_t defaulted_loans = 0;
	uint32_t repaid_loans = 0;
	state.world.economic_actor_for_each_obligation_debtor_as_economic_actor(borrower,
		[&](dcon::obligation_debtor_id relation) {
			auto loan = state.world.obligation_debtor_get_obligation(relation);
			if(!loan || state.world.obligation_get_kind(loan) != uint8_t(relations::obligation_kind::loan)
				|| state.world.obligation_get_settlement_commodity(loan) != settlement) return;
			auto linked_factory = state.world.obligation_get_factory_from_obligation_factory(loan);
			// Factory-originated credit is underwritten against that factory's
			// own debt stack. Untagged legacy actor loans remain a conservative
			// compatibility charge against each new application.
			if(linked_factory && linked_factory != factory) return;
			auto status = state.world.obligation_get_status(loan);
			if(status == uint8_t(relations::obligation_status::active)
				|| status == uint8_t(relations::obligation_status::defaulted))
				existing_debt += relations::total_due(state, loan);
			if(status == uint8_t(relations::obligation_status::defaulted)) ++defaulted_loans;
			if(status == uint8_t(relations::obligation_status::paid)) ++repaid_loans;
		});
	float daily_cashflow = std::max(0.0f, std::min(state.world.factory_get_agency_cashflow(factory),
		state.world.factory_get_agency_recent_profit(factory)));
	float annual_cashflow = daily_cashflow * 365.0f;
	float distress = std::min(1.0f, float(state.world.factory_get_agency_distress_days(factory)) / 120.0f);
	float cashflow_coverage = std::clamp(annual_cashflow / std::max(1.0f, requested_amount * 1.5f), 0.0f, 1.0f);
	float collateral_coverage = std::clamp(collateral_value / std::max(1.0f, requested_amount * 1.25f), 0.0f, 1.0f);
	float repayment_history = std::clamp(0.65f + 0.05f * float(std::min<uint32_t>(repaid_loans, 4))
		- 0.35f * float(defaulted_loans), 0.0f, 1.0f);
	float score = std::clamp(0.45f * cashflow_coverage + 0.30f * collateral_coverage
		+ 0.15f * repayment_history + 0.10f * (1.0f - distress), 0.0f, 1.0f);
	float rate = indicative_factory_loan_rate(state, factory, settlement, requested_amount, collateral_value);
	state.world.firm_capital_request_set_underwriting_score(request, score);
	state.world.firm_capital_request_set_annual_interest_rate(request, rate);
	result.underwriting_score = score;
	result.annual_interest_rate = rate;

	bool project_request = request_kind == 1;
	bool viable_return = expected_annual_return > rate + 0.015f;
	bool viable_firm = daily_cashflow > 0.0f && distress < 0.75f && defaulted_loans == 0;
	float risk_capacity = collateral_value * (0.20f + 0.35f * score) + annual_cashflow * (0.30f + 0.70f * score);
	float debt_headroom = std::max(0.0f, risk_capacity - existing_debt);
	float approved = score >= 0.20f && viable_return && viable_firm
		? std::min(requested_amount, debt_headroom) : 0.0f;
	auto bank_sheet = bank_balance_sheet(state, bank, settlement);
	auto bank_reserve = reserve_account_for(state, bank, settlement);
	float bank_capital_headroom = std::max(0.0f, bank_sheet.net_worth * 10.0f - bank_sheet.loan_assets);
	float bank_liquidity = bank_reserve ? std::max(0.0f, accounts::balance(state, bank_reserve)
		- std::max(bank_sheet.deposit_liabilities, std::max(0.0f, bank_sheet.net_worth)) * 0.10f) : 0.0f;
	approved = std::min(approved, std::min(bank_capital_headroom, bank_liquidity));
	if(project_request && expected_annual_return <= rate + 0.04f) approved = 0.0f;
	if(approved < requested_amount * 0.05f) approved = 0.0f;
	if(approved <= 1.0e-5f) {
		state.world.firm_capital_request_set_status(request, 3); // denied
		return result;
	}

	auto obligation = originate_factory_loan_to_operating_account(state, bank, factory,
		operating_account, approved, rate);
	if(!obligation) {
		state.world.firm_capital_request_set_status(request, 3);
		return result;
	}
	state.world.force_create_obligation_factory(obligation, factory);
	state.world.force_create_firm_capital_request_obligation(request, obligation);
	state.world.firm_capital_request_set_funded_amount(request, approved);
	state.world.firm_capital_request_set_status(request,
		approved + 1.0e-5f < requested_amount ? 2 : 1); // partial / full
	result.obligation = obligation;
	result.funded_amount = approved;
	return result;
}

loan_service_result service_actor_loans(sys::state& state, dcon::economic_actor_id borrower,
	sys::date today, uint32_t default_grace_days) {
	loan_service_result result{};
	if(!borrower || !state.world.economic_actor_is_valid(borrower)) return result;
	state.world.economic_actor_for_each_obligation_debtor_as_economic_actor(borrower,
		[&](dcon::obligation_debtor_id relation) {
			auto loan = state.world.obligation_debtor_get_obligation(relation);
			if(!loan || !state.world.obligation_is_valid(loan)
				|| state.world.obligation_get_kind(loan) != uint8_t(relations::obligation_kind::loan)
				|| state.world.obligation_get_economic_actor_from_obligation_debtor(loan) != borrower) return;
			if(state.world.obligation_get_status(loan) == uint8_t(relations::obligation_status::defaulted)) {
				++result.defaulted_loans;
				auto factory = state.world.obligation_get_factory_from_obligation_factory(loan);
				if(factory) result.defaulted_factories.push_back(factory);
				else result.has_unscoped_default = true;
				return;
			}
			if(state.world.obligation_get_status(loan) != uint8_t(relations::obligation_status::active)) return;

			auto lender = state.world.obligation_get_economic_actor_from_obligation_creditor(loan);
			auto bank = actors::organizations::organization_for_actor(state, lender);
			if(!valid_bank(state, bank)) return;

			auto last_accrual = state.world.obligation_get_last_interest_accrual_date(loan);
			if(!last_accrual) last_accrual = state.world.obligation_get_creation_date(loan);
			auto elapsed = today.to_raw_value() > last_accrual.to_raw_value()
				? uint32_t(today.to_raw_value() - last_accrual.to_raw_value()) : 0u;
			if(elapsed > 0) {
				result.interest_accrued += accrue_loan_interest(state, loan, elapsed);
				state.world.obligation_set_last_interest_accrual_date(loan, today);
			}

			auto due_date = state.world.obligation_get_due_date(loan);
			if(!due_date || today < due_date) return;
			auto settlement = state.world.obligation_get_settlement_commodity(loan);
			if(!settlement || !state.world.commodity_is_valid(settlement)) return;

			// Repay from the loan's bank deposit first, then from the firm's
			// operating account. Both balances belong to the same borrower actor.
			state.world.organization_for_each_deposit_account_bank_as_organization(bank,
				[&](dcon::deposit_account_bank_id account_relation) {
					if(relations::total_due(state, loan) <= 1.0e-5f) return;
					auto account = state.world.deposit_account_bank_get_deposit_account(account_relation);
					if(!account || state.world.deposit_account_get_economic_actor_from_deposit_account_owner(account) != borrower
						|| state.world.deposit_account_get_commodity_from_deposit_account_settlement(account) != settlement) return;
					auto payment = std::min(deposit_balance(state, account), relations::total_due(state, loan));
					if(payment > 1.0e-5f) result.amount_repaid += repay_loan(state, loan, account, payment, today);
				});

			if(relations::total_due(state, loan) > 1.0e-5f) {
				auto operating = accounts::find_account(state, borrower, settlement);
				auto reserve = reserve_account_for(state, bank, settlement);
				if(!reserve) reserve = open_reserve_account(state, bank, settlement);
				if(operating && reserve) {
					auto payment = std::min(accounts::balance(state, operating), relations::total_due(state, loan));
					if(payment > 1.0e-5f && accounts::settle_obligation_payment(state, loan,
						operating, reserve, payment, today)) result.amount_repaid += payment;
				}
			}

			if(relations::total_due(state, loan) > 1.0e-5f) {
				auto overdue_days = today.to_raw_value() - due_date.to_raw_value();
				if(overdue_days >= default_grace_days) {
					state.world.obligation_set_status(loan, uint8_t(relations::obligation_status::defaulted));
					++result.defaulted_loans;
					auto factory = state.world.obligation_get_factory_from_obligation_factory(loan);
					if(factory) result.defaulted_factories.push_back(factory);
					else result.has_unscoped_default = true;
				}
			}
		});
	return result;
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
