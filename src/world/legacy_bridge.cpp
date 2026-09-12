#include "legacy_bridge.hpp"
#include "system_state.hpp"

namespace world::legacy_bridge {

dcon::province_id province_for_factory(sys::state const& state, dcon::factory_id factory) {
	auto site = site::site_for_factory(state, factory);
	if(site) {
		auto province = site::province_for_site(state, site);
		if(province)
			return province;
	}
	return state.world.factory_get_province_from_factory_location(factory);
}

void bootstrap_factory_sites(sys::state& state) {
	state.world.for_each_factory([&](dcon::factory_id factory) {
		auto site = site::site_for_factory(state, factory);
		auto legacy_province = state.world.factory_get_province_from_factory_location(factory);
		if(!site) {
			if(!legacy_province)
				return;
			site = state.world.create_site();
			state.world.force_create_site_location(site, legacy_province);
			state.world.site_set_position(site, state.world.province_get_mid_point(legacy_province));
			state.world.force_create_factory_site(factory, site);
			return;
		}

		if(!state.world.site_get_province_from_site_location(site) && legacy_province)
			state.world.force_create_site_location(site, legacy_province);
		if(state.world.site_get_position(site) == glm::vec2{} && legacy_province)
			state.world.site_set_position(site, state.world.province_get_mid_point(legacy_province));
	});
}

}
