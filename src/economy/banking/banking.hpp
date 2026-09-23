#pragma once

#include "dcon_generated.hpp"
#include "date_interface.hpp"

#include <cstdint>

namespace sys { class state; }

namespace economy::banking {

struct balance_sheet {
	float settlement_assets = 0.0f;
	float loan_assets = 0.0f;
	float public_debt_assets = 0.0f;
	float total_assets = 0.0f;
	float deposit_liabilities = 0.0f;
	float other_financial_liabilities = 0.0f;
	float total_liabilities = 0.0f;
	float net_worth = 0.0f;
};

struct loan_service_result {
	float interest_accrued = 0.0f;
	float amount_repaid = 0.0f;
	uint32_t defaulted_loans = 0;
};

dcon::organization_id create_bank(sys::state&);

dcon::monetary_account_id open_reserve_account(sys::state&, dcon::organization_id bank,
	dcon::commodity_id settlement);
dcon::monetary_account_id reserve_account_for(sys::state const&, dcon::organization_id bank,
	dcon::commodity_id settlement);
bool bootstrap_set_reserve_balance(sys::state&, dcon::monetary_account_id, float);

dcon::deposit_account_id open_deposit_account(sys::state&, dcon::organization_id bank,
	dcon::economic_actor_id owner, dcon::commodity_id settlement);
float deposit_balance(sys::state const&, dcon::deposit_account_id);
bool bootstrap_set_deposit_balance(sys::state&, dcon::deposit_account_id, float);

dcon::obligation_id originate_loan_with_consent(sys::state&, dcon::organization_id bank,
	dcon::deposit_account_id borrower_account, float principal, sys::date creation_date,
	sys::date due_date, float annual_interest_rate, dcon::economic_proposal_id proposal);

bool transfer_deposit(sys::state&, dcon::deposit_account_id source,
	dcon::deposit_account_id destination, float amount, sys::date timestamp);
float repay_loan(sys::state&, dcon::obligation_id, dcon::deposit_account_id borrower_account,
	float amount, sys::date timestamp);
float accrue_loan_interest(sys::state&, dcon::obligation_id, uint32_t days);
bool write_off_loan(sys::state&, dcon::obligation_id);

// Accrues interest once per elapsed day and services matured loans from the
// borrower's bank deposits and operating account. Unpaid matured loans default
// after the supplied grace period.
loan_service_result service_actor_loans(sys::state&, dcon::economic_actor_id borrower,
	sys::date today, uint32_t default_grace_days);

balance_sheet bank_balance_sheet(sys::state const&, dcon::organization_id bank,
	dcon::commodity_id settlement);

} // namespace economy::banking
