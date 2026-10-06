#pragma once

#include "dcon_generated.hpp"
#include "date_interface.hpp"
#include "economy/exact_person_economy.hpp"

#include <cstdint>

namespace sys { class state; }
namespace governance::policy { enum class topic_id : uint16_t; }

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
dcon::fiscal_action_id authorized_assess_tax_by_institution(sys::state&, dcon::institution_id authority,
	dcon::economic_actor_id taxpayer_actor, dcon::monetary_account_id treasury_account,
	float amount, sys::date due_date, sys::date date);
dcon::transaction_id pay_tax(sys::state&, dcon::obligation_id tax_obligation,
	dcon::monetary_account_id taxpayer_account, dcon::monetary_account_id treasury_account,
	float amount, sys::date date);
economy::exact_person_economy::transfer_result pay_tax(sys::state&, dcon::obligation_id tax_obligation,
	economy::exact_person_economy::account_ref taxpayer_account,
	dcon::monetary_account_id treasury_account, float amount, sys::date date);

struct policy_tax_result {
	float rate = 0.0f;
	float assessed = 0.0f;
	float collected = 0.0f;
	dcon::fiscal_action_id assessment{};
	dcon::obligation_id obligation{};
	dcon::transaction_id transaction{};
	uint64_t exact_transaction_id = 0;
};

// Reads the enacted national policy rate, records a tax obligation against the
// exact liable actor, and collects from the supplied operating wallet or bank
// deposit. Any amount the payment rail cannot settle remains due on the
// obligation.
policy_tax_result assess_and_collect_topic_tax(sys::state&, policy::topic_id,
	dcon::nation_id jurisdiction, dcon::economic_actor_id taxpayer_actor,
	float taxable_value, economy::exact_person_economy::account_ref payer_wallet,
	dcon::deposit_account_id payer_deposit, sys::date date);

dcon::fiscal_action_id authorized_spend(sys::state&, dcon::person_id initiator,
	dcon::monetary_account_id treasury_account, dcon::monetary_account_id recipient_account,
	float amount, sys::date date);
dcon::fiscal_action_id authorized_spend_by_institution(sys::state&, dcon::institution_id authority,
	dcon::monetary_account_id treasury_account, dcon::monetary_account_id recipient_account,
	float amount, sys::date date);
dcon::fiscal_action_id authorized_spend_exact_person_by_institution(sys::state&, dcon::institution_id authority,
	dcon::monetary_account_id treasury_account, economy::exact_person_economy::person_key recipient,
	economy::exact_person_economy::account_ref recipient_account, float amount, sys::date date);

// Transfers appropriated money from the allocating institution's treasury to
// a recipient institution's treasury. The allocator needs `appropriate` and
// `spend_public_funds` over its jurisdiction, and an effective appropriation
// must name the recipient. No money is created: it moves between real accounts.
dcon::fiscal_action_id authorized_allocate(sys::state&, dcon::institution_id allocator,
	dcon::institution_id recipient, dcon::commodity_id settlement, float amount, sys::date date);

dcon::fiscal_action_id authorized_issue_public_debt_with_consent(sys::state&, dcon::person_id initiator,
	dcon::monetary_account_id treasury_account, dcon::monetary_account_id investor_account,
	float principal, sys::date due_date, float annual_interest_rate, sys::date date,
	dcon::economic_proposal_id investor_proposal);
float accrue_public_debt_interest(sys::state&, dcon::obligation_id, uint32_t days);
dcon::transaction_id service_public_debt(sys::state&, dcon::obligation_id,
	dcon::monetary_account_id treasury_account, dcon::monetary_account_id holder_account,
	float amount, sys::date date);

fiscal_position fiscal_position_for(sys::state const&, dcon::institution_id,
	dcon::commodity_id settlement);
float public_debt_held_by(sys::state const&, dcon::economic_actor_id, dcon::commodity_id);
float national_public_debt(sys::state const&, dcon::nation_id, dcon::commodity_id);

} // namespace governance::finance
