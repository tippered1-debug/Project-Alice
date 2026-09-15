#pragma once

#include "container_types_dcon.hpp"
#include "dcon_generated.hpp"

namespace sys { class state; }

namespace economy::physical::factory_inputs {

struct availability {
	bool active = false;
	float legacy_ratio = 1.0f;
	float physical_ratio = 1.0f;
};

availability evaluate(sys::state const&, dcon::site_id, dcon::economic_actor_id,
	economy::commodity_set const&, dcon::market_id, float input_scale);

void begin_planning(sys::state&);
bool plan(sys::state&, dcon::factory_id, dcon::site_id, dcon::economic_actor_id,
	economy::commodity_set const&, dcon::market_id, float input_scale);
float planned_quantity(sys::state const&, dcon::factory_id, dcon::commodity_id,
	float fallback) noexcept;
void fulfill(sys::state&);

bool ordinary_physical_input(sys::state const&, dcon::commodity_id) noexcept;
float net_demand(sys::state const&, dcon::site_id, dcon::economic_actor_id,
	dcon::commodity_id, float required) noexcept;

bool consume(sys::state&, dcon::site_id, dcon::economic_actor_id,
	economy::commodity_set const&, float input_scale, float ratio);

} // namespace economy::physical::factory_inputs
