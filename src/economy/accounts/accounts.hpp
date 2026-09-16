#pragma once

#include "dcon_generated.hpp"
#include "economy/relations/relations.hpp"

namespace sys { class state; }

namespace economy::accounts {

dcon::monetary_account_id open_account(sys::state&, dcon::economic_actor_id, dcon::commodity_id);
dcon::monetary_account_id find_account(sys::state const&, dcon::economic_actor_id, dcon::commodity_id);
dcon::commodity_id first_settlement_for(sys::state const&, dcon::economic_actor_id);
dcon::economic_actor_id owner_of(sys::state const&, dcon::monetary_account_id);
dcon::commodity_id settlement_of(sys::state const&, dcon::monetary_account_id);
float balance(sys::state const&, dcon::monetary_account_id);
bool bootstrap_set_balance(sys::state&, dcon::monetary_account_id, float);
dcon::transaction_id transfer(sys::state&, dcon::monetary_account_id, dcon::monetary_account_id,
	float, relations::transaction_kind, sys::date);
dcon::transaction_id settle_obligation_payment(sys::state&, dcon::obligation_id,
	dcon::monetary_account_id, dcon::monetary_account_id, float, sys::date);

float cash_inflow(sys::state const&, dcon::economic_actor_id,
	dcon::commodity_id settlement = {});
float cash_outflow(sys::state const&, dcon::economic_actor_id,
	dcon::commodity_id settlement = {});
float operating_cash_flow(sys::state const&, dcon::economic_actor_id,
	dcon::commodity_id settlement = {});

} // namespace economy::accounts
