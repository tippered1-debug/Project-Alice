#pragma once

#include "spatial.hpp"

namespace world::site {

dcon::site_id site_for_factory(sys::state const&, dcon::factory_id);
dcon::province_id province_for_site(sys::state const&, dcon::site_id);

}
