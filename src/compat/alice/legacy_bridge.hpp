#pragma once

#include "world/site.hpp"

namespace compat::alice {

void bootstrap_factory_sites(sys::state&);
dcon::province_id province_for_factory(sys::state const&, dcon::factory_id);

} // namespace compat::alice
