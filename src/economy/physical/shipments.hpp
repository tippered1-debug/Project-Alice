#pragma once

#include "dcon_generated.hpp"

namespace sys { class state; }

namespace economy::physical::shipments {

dcon::shipment_id dispatch(sys::state&, dcon::site_id origin, dcon::site_id destination,
	dcon::commodity_id commodity, float quantity);
void advance(sys::state&);
void process_rgo_output(sys::state&);

} // namespace economy::physical::shipments
