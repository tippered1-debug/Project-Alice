#pragma once

#include "dcon_generated.hpp"

namespace sys { class state; }

namespace compat::alice {

bool physical_path_enabled(sys::state const&) noexcept;
void handoff_arrived_stock(sys::state&);

} // namespace compat::alice
