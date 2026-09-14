#pragma once

#include "dcon_generated.hpp"
#include "date_interface.hpp"

namespace sys { class state; }

namespace economy::relations {

enum class transaction_kind : uint8_t { transfer = 0, repayment = 1, interest = 2, other = 3 };
enum class obligation_status : uint8_t { active = 0, paid = 1, defaulted = 2, written_off = 3 };
enum class obligation_kind : uint8_t { loan = 0, trade_credit = 1, other = 2 };

dcon::transaction_id record_transaction(sys::state&, dcon::economic_actor_id payer,
	dcon::economic_actor_id payee, float amount, dcon::commodity_id settlement,
	transaction_kind kind, sys::date timestamp);
dcon::obligation_id create_obligation(sys::state&, dcon::economic_actor_id debtor,
	dcon::economic_actor_id creditor, float principal, dcon::commodity_id settlement,
	sys::date creation_date, sys::date due_date, float annual_interest_rate, obligation_kind kind);
float accrue_interest(sys::state&, dcon::obligation_id, uint32_t days);
float repay_obligation(sys::state&, dcon::obligation_id, float amount);
bool write_off(sys::state&, dcon::obligation_id);
float outstanding_between(sys::state const&, dcon::economic_actor_id debtor,
	dcon::economic_actor_id creditor, dcon::commodity_id settlement);

} // namespace economy::relations
