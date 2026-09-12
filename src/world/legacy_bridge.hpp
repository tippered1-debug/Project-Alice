#pragma once

#include "site.hpp"

namespace world::legacy_bridge {

void bootstrap_factory_sites(sys::state&);
dcon::province_id province_for_factory(sys::state const&, dcon::factory_id);

}
