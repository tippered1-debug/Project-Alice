#include "site.hpp"
#include "system_state.hpp"

namespace world::site {

dcon::site_id site_for_factory(sys::state const& state, dcon::factory_id factory) {
	return state.world.factory_get_site_from_factory_site(factory);
}

dcon::province_id province_for_site(sys::state const& state, dcon::site_id site) {
	return state.world.site_get_province_from_site_location(site);
}

}
