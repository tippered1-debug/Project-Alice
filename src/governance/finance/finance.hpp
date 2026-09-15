#pragma once

#include "dcon_generated.hpp"
#include "date_interface.hpp"

#include <cstdint>

namespace sys { class state; }

namespace governance::finance {

enum class fiscal_action_kind : uint8_t { tax_assessment = 0, public_spending = 1, public_debt_issuance = 2 };

struct fiscal_position {
	float treasury_cash = 0.0f;
	float tax_receivables = 0.0f;
	float public_debt_outstanding = 0.0f;
	float net_financial_position = 0.0f;
};

dcon::monetary_account_id open_treasury_account(sys::state&, dcon::institution_id,
	dcon::commodity_id settlement);
dcon::monetary_account_id treasury_account_for(sys::state const&, dcon::institution_id,
	dcon::commodity_id settlement);
dcon::institution_id treasury_institution_for(sys::state const&, dcon::monetary_account_id);

dcon::fiscal_action_id authorized_assess_tax(sys::state&, dcon::person_id initiator,
	dcon::economic_actor_id taxpayer_actor, dcon::monetary_account_id treasury_account,
	float amount, sys::date due_date, sys::date date);
dcon::transaction_id pay_tax(sys::state&, dcon::obligation_id tax_obligation,
	dcon::monetary_account_id taxpayer_account, dcon::monetary_account_id treasury_account,
	float amount, sys::date date);

dcon::fiscal_action_id authorized_spend(sys::state&, dcon::person_id initiator,
	dcon::monetary_account_id treasury_account, dcon::monetary_account_id recipient_account,
	float amount, sys::date date);

dcon::fiscal_action_id authorized_issue_public_debt(sys::state&, dcon::person_id initiator,
	dcon::monetary_account_id treasury_account, dcon::monetary_account_id investor_account,
	float principal, sys::date due_date, float annual_interest_rate, sys::date date);
float accrue_public_debt_interest(sys::state&, dcon::obligation_id, uint32_t days);
dcon::transaction_id service_public_debt(sys::state&, dcon::obligation_id,
	dcon::monetary_account_id treasury_account, dcon::monetary_account_id holder_account,
	float amount, sys::date date);

fiscal_position fiscal_position_for(sys::state const&, dcon::institution_id,
	dcon::commodity_id settlement);
float public_debt_held_by(sys::state const&, dcon::economic_actor_id, dcon::commodity_id);
float national_public_debt(sys::state const&, dcon::nation_id, dcon::commodity_id);

} // namespace governance::finance
