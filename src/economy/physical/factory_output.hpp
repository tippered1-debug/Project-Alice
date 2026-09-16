#pragma once

#include "dcon_generated.hpp"
#include "date_interface.hpp"

namespace sys { class state; }

namespace economy::physical::factory_output {

bool materialize_and_dispatch(sys::state&, dcon::factory_id, float produced_amount);
dcon::transaction_id sell_output(sys::state&, dcon::factory_id, dcon::economic_actor_id buyer,
	float quantity, float unit_price, dcon::commodity_id settlement, sys::date timestamp);

} // namespace economy::physical::factory_output
