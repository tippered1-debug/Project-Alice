#pragma once

#include "dcon_generated.hpp"
#include "economy/exact_person_economy.hpp"
#include "date_interface.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace sys { class state; }

namespace economy::banking {

struct balance_sheet {
	float settlement_assets = 0.0f;
	float loan_assets = 0.0f; // performing principal only
	float accrued_interest_receivable = 0.0f;
	float public_debt_assets = 0.0f;
	float total_assets = 0.0f;
	float deposit_liabilities = 0.0f;
	float other_financial_liabilities = 0.0f;
	float total_liabilities = 0.0f;
	float net_worth = 0.0f;
	float capital_ratio = 0.0f;
	float liquidity_ratio = 0.0f;
	float required_liquidity = 0.0f;
};

// solvent: meeting its capital and liquidity requirements. constrained: short
// of reserves or capital; it keeps operating, and an illiquid bank with good
// assets stays here however long the shortage lasts. insolvent: negative net
// worth, or a capital shortfall that outlasted its grace period; final.
enum class bank_status : uint8_t { solvent = 0, constrained = 1, insolvent = 2 };

struct bank_policy {
	dcon::nation_id jurisdiction{};
	dcon::commodity_id settlement{};
	float lending_base_rate = 0.0f;
	float minimum_capital_ratio = 0.0f;
	float liquidity_target = 0.0f;
	float risk_appetite = 0.0f;
	float lending_spread = 0.0f;
	float max_single_borrower_exposure = 0.0f;
	float reserve_requirement = 0.0f;
	uint16_t capital_breach_grace_days = 0;
};

struct bank_clearing_result {
	uint32_t settled = 0;
	uint32_t rejected = 0;
	uint32_t deferred = 0;
	float reserves_settled = 0.0f;
};

struct loan_service_result {
	float interest_accrued = 0.0f;
	float amount_repaid = 0.0f;
	uint32_t defaulted_loans = 0;
	std::vector<dcon::factory_id> defaulted_factories;
	bool has_unscoped_default = false;
};

struct factory_credit_result {
	dcon::firm_capital_request_id request{};
	dcon::obligation_id obligation{};
	float requested_amount = 0.0f;
	float funded_amount = 0.0f;
	float annual_interest_rate = 0.0f;
	float underwriting_score = 0.0f;
};

dcon::organization_id create_bank(sys::state&);
bool set_bank_lending_base_rate(sys::state&, dcon::organization_id, float annual_rate);
bool configure_bank_policy(sys::state&, dcon::organization_id, bank_policy const&);
bank_status status_of(sys::state const&, dcon::organization_id);

dcon::monetary_account_id open_reserve_account(sys::state&, dcon::organization_id bank,
	dcon::commodity_id settlement, uint64_t canonical_id = 0);
dcon::monetary_account_id reserve_account_for(sys::state const&, dcon::organization_id bank,
	dcon::commodity_id settlement);
bool bootstrap_set_reserve_balance(sys::state&, dcon::monetary_account_id, float);

dcon::deposit_account_id open_deposit_account(sys::state&, dcon::organization_id bank,
	dcon::economic_actor_id owner, dcon::commodity_id settlement, uint64_t canonical_id = 0);
float deposit_balance(sys::state const&, dcon::deposit_account_id);
bool bootstrap_set_deposit_balance(sys::state&, dcon::deposit_account_id, float);

dcon::obligation_id originate_loan_with_consent(sys::state&, dcon::organization_id bank,
	dcon::deposit_account_id borrower_account, float principal, sys::date creation_date,
	sys::date due_date, float annual_interest_rate, dcon::economic_proposal_id proposal);

bool transfer_deposit(sys::state&, dcon::deposit_account_id source,
	dcon::deposit_account_id destination, float amount, sys::date timestamp);

// The owner's deposit account in a settlement, preferring one at `bank`.
dcon::deposit_account_id deposit_account_for(sys::state const&, dcon::economic_actor_id owner,
	dcon::commodity_id settlement, dcon::organization_id bank = {});
// A deposit's owner moves operating cash into it: the cash leaves their wallet
// for the bank's reserves, and the bank owes them the same amount.
float deposit_cash(sys::state&, dcon::deposit_account_id, economy::exact_person_economy::account_ref wallet,
	float amount, sys::date timestamp);
// A deposit's owner draws cash: the bank pays it out of its reserves into the
// owner's wallet and owes that much less. A bank pays no more than its reserves
// hold; the returned amount is what was actually paid.
float withdraw_cash(sys::state&, dcon::deposit_account_id, economy::exact_person_economy::account_ref wallet,
	float amount, sys::date timestamp);
// Pays from a deposit with immediate settlement: to a deposit at the same bank
// (reserves do not move), to a deposit at another bank (reserves move from the
// payer's bank to the payee's), or to an operating wallet (reserves leave the
// payer's bank). Fails without change when the payer's balance or, for an
// external payment, the payer bank's reserves cannot cover it.
dcon::transaction_id pay_from_deposit(sys::state&, dcon::deposit_account_id source,
	dcon::deposit_account_id destination_deposit, economy::exact_person_economy::account_ref destination_wallet,
	float amount, relations::transaction_kind, sys::date timestamp);
bool queue_interbank_payment(sys::state&, dcon::deposit_account_id source,
	dcon::deposit_account_id destination, float amount, sys::date timestamp,
	uint64_t stable_transaction_key);
bank_clearing_result clear_interbank_payments(sys::state&, sys::date today);
float repay_loan(sys::state&, dcon::obligation_id, dcon::deposit_account_id borrower_account,
	float amount, sys::date timestamp);
float accrue_loan_interest(sys::state&, dcon::obligation_id, uint32_t days);
bool mark_loan_defaulted(sys::state&, dcon::obligation_id);
bool write_off_loan(sys::state&, dcon::obligation_id);
float resolve_defaulted_loan(sys::state&, dcon::obligation_id,
	dcon::monetary_account_id recovery_source, float requested_recovery, sys::date timestamp);

// Accrues interest once per elapsed day and services matured loans from the
// borrower's bank deposits and operating account. Unpaid matured loans default
// after the supplied grace period.
loan_service_result service_actor_loans(sys::state&, dcon::economic_actor_id borrower,
	sys::date today, uint32_t default_grace_days);

factory_credit_result underwrite_factory_credit(sys::state&, dcon::factory_id,
	dcon::monetary_account_id operating_account, uint8_t request_kind, float requested_amount,
	float expected_annual_return, float collateral_value, dcon::capital_project_id project = {});

// Bank credit for a capital project of a sponsor who may own no plant yet. A
// bank lends against the project's expected cash flow (repaying half the loan
// over its term), the sponsor's equity in it (at least this share of equity
// plus debt), and the plant it will become (at this share of its value). The
// loan is due at the end of the term and follows the plant once it is built.
inline constexpr int32_t project_loan_term_days = 1825;
inline constexpr float minimum_project_equity_share = 0.3f;
inline constexpr float project_collateral_share = 0.5f;
struct project_credit_quote {
	dcon::organization_id bank{};
	float amount = 0.0f;
	float annual_interest_rate = 0.0f;
	float underwriting_score = 0.0f;
};
// What the best willing bank would lend now; changes nothing. The borrower may
// be a company still to be founded.
project_credit_quote quote_project_credit(sys::state const&, dcon::economic_actor_id borrower,
	dcon::commodity_id settlement, float requested_amount, float expected_annual_cashflow,
	float sponsor_equity, float project_value);
factory_credit_result underwrite_project_credit(sys::state&, dcon::capital_project_id,
	dcon::monetary_account_id operating_account, float requested_amount, float expected_annual_cashflow,
	float sponsor_equity, float project_value);

float indicative_factory_loan_rate(sys::state const&, dcon::factory_id,
	dcon::commodity_id settlement, float requested_amount, float collateral_value);

balance_sheet bank_balance_sheet(sys::state const&, dcon::organization_id bank,
	dcon::commodity_id settlement);
void update_bank_statuses(sys::state&, sys::date today);
bool validate_canonical_banking_state(sys::state const&, std::vector<std::string>& errors);
bool canonical_banking_checksum(sys::state const&, uint64_t& checksum);

} // namespace economy::banking
