#pragma once

#include "dcon_generated.hpp"

namespace sys { class state; }

namespace economy::physical::shipments {

inline constexpr float compatibility_distance_units_per_day = 150.0f;

struct route_quote {
	float distance = 0.0f;
	uint8_t required_mode_mask = 0;
	dcon::trade_route_id primary_trade_route{};
	uint8_t route_leg_count = 0;
};

uint32_t compatibility_travel_days(float distance) noexcept;
bool quote_route(sys::state&, dcon::site_id origin, dcon::site_id destination,
	route_quote&);
bool can_dispatch(sys::state&, dcon::site_id origin, dcon::site_id destination,
	dcon::commodity_id commodity, float quantity);
dcon::shipment_id dispatch(sys::state&, dcon::site_id origin, dcon::site_id destination,
	dcon::commodity_id commodity, float quantity, dcon::economic_actor_id owner);
dcon::shipment_id dispatch_transfer(sys::state&, dcon::site_id origin, dcon::site_id destination,
	dcon::commodity_id commodity, float quantity, dcon::economic_actor_id seller,
	dcon::economic_actor_id buyer);
void advance(sys::state&);
void process_arrivals(sys::state&);
void process_rgo_output(sys::state&);

} // namespace economy::physical::shipments
