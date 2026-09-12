#include "shipments.hpp"
#include "deposits.hpp"
#include "inventory.hpp"
#include "legacy_market_bridge.hpp"
#include "system_state.hpp"
#include "province.hpp"
#include "economy_production.hpp"
#include "commodity_logistics.hpp"

#include <algorithm>
#include <cmath>

namespace economy::physical::shipments {

dcon::shipment_id dispatch(sys::state& state, dcon::site_id origin, dcon::site_id destination,
	dcon::commodity_id commodity, float amount) {
	if(!origin || !destination || !commodity || !std::isfinite(amount) || amount <= 0.0f)
		return dcon::shipment_id{};
	auto removed = inventory::remove(state, origin, commodity, amount);
	if(removed <= 0.0f)
		return dcon::shipment_id{};
	auto shipment = state.world.create_shipment();
	state.world.shipment_set_commodity(shipment, commodity);
	state.world.shipment_set_remaining_quantity(shipment, removed);
	auto from = state.world.site_get_province_from_site_location(origin);
	auto to = state.world.site_get_province_from_site_location(destination);
	auto distance = (from && to) ? province::direct_distance(state, from, to) : 0.0f;
	state.world.shipment_set_remaining_days(shipment, uint32_t(std::max(1.0f, std::ceil(distance))));
	state.world.force_create_shipment_origin(shipment, origin);
	state.world.force_create_shipment_destination(shipment, destination);
	return shipment;
}

void advance(sys::state& state) {
	state.world.for_each_shipment([&](dcon::shipment_id shipment) {
		auto commodity = state.world.shipment_get_commodity(shipment);
		auto profile = logistics::profile_for(state, commodity);
		auto remaining = std::max(0.0f, state.world.shipment_get_remaining_quantity(shipment));
		remaining *= std::max(0.0f, 1.0f - profile.daily_spoilage);
		if(state.world.shipment_get_remaining_days(shipment) > 1) {
			state.world.shipment_set_remaining_days(shipment, state.world.shipment_get_remaining_days(shipment) - 1);
			state.world.shipment_set_remaining_quantity(shipment, remaining);
			return;
		}
		auto destination = state.world.shipment_get_site_from_shipment_destination(shipment);
		inventory::add(state, destination, commodity, remaining);
		state.world.delete_shipment(shipment);
	});
}

void process_rgo_output(sys::state& state) {
	advance(state);
	legacy_market_bridge::handoff_arrived_stock(state);
	state.world.for_each_province([&](dcon::province_id province) {
		auto zone = state.world.province_get_state_membership(province);
		auto market = state.world.state_instance_get_market_from_local_market(zone);
		auto hub = deposits::market_hub_for(state, market);
		if(!hub)
			return;
		state.world.for_each_commodity([&](dcon::commodity_id commodity) {
			if(state.world.commodity_get_rgo_amount(commodity) <= 0.0f
				|| state.world.commodity_get_money_rgo(commodity)
				|| state.world.commodity_get_is_local(commodity))
				return;
			auto output = state.world.province_get_rgo_output(province, commodity);
			if(output <= 0.0f)
				return;
			auto extraction = deposits::extraction_site_for(state, province, commodity);
			if(!extraction)
				return;
			inventory::add(state, extraction, commodity, output);
			dispatch(state, extraction, hub, commodity, output);
		});
	});
}

} // namespace economy::physical::shipments
