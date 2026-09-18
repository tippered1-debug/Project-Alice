#pragma once

#include "dcon_generated.hpp"

namespace sys { class state; }

namespace economy::physical::individual_consumption {

bool set_home_site(sys::state&, dcon::person_id, dcon::site_id);
dcon::site_id home_site(sys::state const&, dcon::person_id);

dcon::person_commodity_need_id set_need(sys::state&, dcon::person_id,
	dcon::commodity_id, float desired_quantity_per_period);
dcon::person_commodity_need_id add_need(sys::state&, dcon::person_id,
	dcon::commodity_id, float quantity);
float unmet_need(sys::state const&, dcon::person_id, dcon::commodity_id);

dcon::monetary_account_id spending_account(sys::state const&, dcon::person_id);
float spendable_cash(sys::state const&, dcon::person_id);

float owned_consumable_quantity(sys::state const&, dcon::person_id, dcon::commodity_id);
float consume_owned_goods(sys::state&, dcon::person_id, dcon::commodity_id, float quantity);
float last_consumed_amount(sys::state const&, dcon::person_id, dcon::commodity_id);

void process_purchase_decisions(sys::state&);
void process_consumption(sys::state&);
void process(sys::state&);

} // namespace economy::physical::individual_consumption
