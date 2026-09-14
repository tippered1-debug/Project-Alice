#pragma once

#include "dcon_generated.hpp"

namespace sys { class state; }

namespace economy::physical::shipments {

inline constexpr float compatibility_distance_units_per_day = 150.0f;

uint32_t compatibility_travel_days(float distance) noexcept;
dcon::shipment_id dispatch(sys::state&, dcon::site_id origin, dcon::site_id destination,
	dcon::commodity_id commodity, float quantity, dcon::economic_actor_id owner);
void advance(sys::state&);
void process_arrivals(sys::state&);
void process_rgo_output(sys::state&);

} // namespace economy::physical::shipments
