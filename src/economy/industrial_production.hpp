#pragma once

#include "dcon_generated.hpp"

namespace sys { class state; }

namespace economy::industrial_production {

void bootstrap_factory(sys::state&, dcon::factory_id);
void bootstrap_factories(sys::state&);
bool set_productive_capacity(sys::state&, dcon::factory_id, float);
bool set_productivity_factor(sys::state&, dcon::factory_id, float);
bool plan_factory_inputs(sys::state&, dcon::factory_id, dcon::province_id, dcon::market_id);
float produce_factory(sys::state&, dcon::factory_id);

} // namespace economy::industrial_production
