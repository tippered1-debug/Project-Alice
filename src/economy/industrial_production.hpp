#pragma once

#include "dcon_generated.hpp"

namespace sys { class state; }

namespace economy::industrial_production {

void bootstrap_factory(sys::state&, dcon::factory_id);
void bootstrap_factories(sys::state&);
bool plan_factory_inputs(sys::state&, dcon::factory_id, dcon::province_id, dcon::market_id);
float produce_factory(sys::state&, dcon::factory_id);

} // namespace economy::industrial_production
