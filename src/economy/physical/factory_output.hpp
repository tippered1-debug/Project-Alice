#pragma once

#include "dcon_generated.hpp"

namespace sys { class state; }

namespace economy::physical::factory_output {

bool materialize_and_dispatch(sys::state&, dcon::factory_id, float produced_amount);

} // namespace economy::physical::factory_output
