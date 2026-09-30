#pragma once
#include "system_state.hpp"
#include "economy_constants.hpp"
#include "capital_projects.hpp"
#include "adaptive_ve.hpp"
#include "advanced_province_buildings.hpp"
#include "demographics_templates.hpp"
#include "economy_common_api_containers.hpp"

namespace economy {

template<typename F>
void for_each_new_factory(sys::state& state, dcon::province_id s, F&& func) {
	state.world.for_each_capital_project([&](auto p) {
		if(state.world.capital_project_get_project_kind(p) != uint8_t(capital_projects::project_kind::factory)
			|| state.world.capital_project_get_status(p) >= uint8_t(capital_projects::status::completed)) return;
		auto site = state.world.capital_project_get_site_from_capital_project_site(p);
		if(state.world.site_get_province_from_site_location(site) == s)
			func(new_factory{capital_projects::material_progress(state, p), state.world.capital_project_get_factory_type(p)});
	});
}

template<typename F>
void for_each_upgraded_factory(sys::state& state, dcon::province_id s, F&& func) {
	state.world.for_each_capital_project([&](auto p) {
		if(state.world.capital_project_get_project_kind(p) != uint8_t(capital_projects::project_kind::factory_expansion)
			|| state.world.capital_project_get_status(p) >= uint8_t(capital_projects::status::completed)) return;
		auto site = state.world.capital_project_get_site_from_capital_project_site(p);
		if(state.world.site_get_province_from_site_location(site) == s)
			func(upgraded_factory{capital_projects::material_progress(state, p), state.world.capital_project_get_factory_type(p), {}});
	});
}
} // namespace economy
