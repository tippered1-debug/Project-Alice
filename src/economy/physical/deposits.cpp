#include "deposits.hpp"
#include "system_state.hpp"
#include "province.hpp"

#include <cmath>

namespace economy::physical::deposits {

namespace {
dcon::site_id make_site(sys::state& state, dcon::province_id province) {
	auto site = state.world.create_site();
	state.world.force_create_site_location(site, province);
	state.world.site_set_position(site, state.world.province_get_mid_point(province));
	return site;
}
}

dcon::site_id extraction_site_for(sys::state const& state, dcon::province_id province, dcon::commodity_id commodity) {
	for(auto deposit : state.world.in_resource_deposit) {
		if(state.world.resource_deposit_get_commodity(deposit) != commodity)
			continue;
		auto site = state.world.resource_deposit_get_site_from_resource_deposit_site(deposit.id);
		if(site && state.world.site_get_province_from_site_location(site) == province)
			return site;
	}
	return dcon::site_id{};
}

dcon::site_id market_hub_for(sys::state const& state, dcon::market_id market) {
	return state.world.market_get_site_from_market_hub_site(market);
}

void bootstrap(sys::state& state) {
	state.world.for_each_province([&](dcon::province_id province) {
		state.world.for_each_commodity([&](dcon::commodity_id commodity) {
			if(state.world.commodity_get_rgo_amount(commodity) <= 0.0f
				|| state.world.province_get_rgo_size(province, commodity) <= 0.0f)
				return;
			if(extraction_site_for(state, province, commodity))
				return;
			auto site = make_site(state, province);
			auto deposit = state.world.create_resource_deposit();
		state.world.resource_deposit_set_commodity(deposit, commodity);
		state.world.force_create_resource_deposit_site(deposit, site);
		});
	});

	state.world.for_each_market([&](dcon::market_id market) {
		if(state.world.market_get_site_from_market_hub_site(market))
			return;
		auto zone = state.world.market_get_zone_from_local_market(market);
		auto province = state.world.state_instance_get_capital(zone);
		if(!province)
			return;
		auto site = make_site(state, province);
		state.world.force_create_market_hub_site(market, site);
	});
}

} // namespace economy::physical::deposits
