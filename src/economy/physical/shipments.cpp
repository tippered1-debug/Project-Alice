#include "shipments.hpp"
#include "deposits.hpp"
#include "inventory.hpp"
#include "system_state.hpp"
#include "province.hpp"
#include "economy_production.hpp"
#include "commodity_logistics.hpp"
#include "economy_stats.hpp"
#include "actors/ownership.hpp"

#include <algorithm>
#include <cmath>

namespace economy::physical::shipments {

uint32_t compatibility_travel_days(float distance) noexcept {
	if(!std::isfinite(distance) || distance <= 0.0f)
		return 1;
	return uint32_t(std::max(1.0f, std::ceil(distance / compatibility_distance_units_per_day)));
}

dcon::shipment_id dispatch(sys::state& state, dcon::site_id origin, dcon::site_id destination,
	dcon::commodity_id commodity, float amount, dcon::economic_actor_id owner) {
	if(!origin || !destination || !commodity || !std::isfinite(amount) || amount <= 0.0f)
		return dcon::shipment_id{};
	auto removed = inventory::remove(state, origin, commodity, amount, owner);
	if(removed <= 0.0f)
		return dcon::shipment_id{};
	auto shipment = state.world.create_shipment();
	state.world.shipment_set_commodity(shipment, commodity);
	state.world.shipment_set_remaining_quantity(shipment, removed);
	auto from = state.world.site_get_province_from_site_location(origin);
	auto to = state.world.site_get_province_from_site_location(destination);
	auto distance = (from && to) ? province::direct_distance(state, from, to) : 0.0f;
	state.world.shipment_set_remaining_days(shipment, compatibility_travel_days(distance));
	state.world.force_create_shipment_origin(shipment, origin);
	state.world.force_create_shipment_destination(shipment, destination);
	if(owner) state.world.force_create_shipment_owner(shipment, owner);
	return shipment;
}

void advance(sys::state& state) {
	state.world.for_each_shipment([&](dcon::shipment_id shipment) {
		dcon::commodity_id commodity = state.world.shipment_get_commodity(shipment);
		auto profile = logistics::profile_for(state, commodity);
		auto remaining = std::max(0.0f, state.world.shipment_get_remaining_quantity(shipment));
		remaining *= std::max(0.0f, 1.0f - profile.daily_spoilage);
		if(state.world.shipment_get_remaining_days(shipment) > 1) {
			state.world.shipment_set_remaining_days(shipment, state.world.shipment_get_remaining_days(shipment) - 1);
			state.world.shipment_set_remaining_quantity(shipment, remaining);
			return;
		}
		auto destination_relation = state.world.shipment_get_shipment_destination(shipment);
		auto destination = state.world.shipment_destination_get_site(destination_relation);
		auto owner_relation = state.world.shipment_get_shipment_owner(shipment);
		auto owner = state.world.shipment_owner_get_economic_actor(owner_relation);
		inventory::add(state, destination, commodity, remaining, owner);
		state.world.delete_shipment(shipment);
	});
}

void process_arrivals(sys::state& state) {
	advance(state);
}

void process_rgo_output(sys::state& state) {
	state.world.for_each_province([&](dcon::province_id province) {
		auto zone = state.world.province_get_state_membership(province);
		auto market = state.world.state_instance_get_market_from_local_market(zone);
		auto hub = deposits::market_hub_for(state, market);
		if(!hub)
			return;
		state.world.for_each_commodity([&](dcon::commodity_id commodity) {
			if(state.world.commodity_get_rgo_amount(commodity) <= 0.0f
				|| state.world.commodity_get_money_rgo(commodity))
				return;
			auto output = state.world.province_get_rgo_output(province, commodity);
			if(output <= 0.0f)
				return;
			if(state.world.commodity_get_is_local(commodity)) {
				register_domestic_supply(state, market, commodity, output, economy_reason::rgo);
				return;
			}
			auto deposit = deposits::deposit_for(state, province, commodity);
			auto extraction = deposit ? state.world.resource_deposit_get_site_from_resource_deposit_site(deposit) : dcon::site_id{};
			if(!extraction)
				return;
			auto owner = actors::ownership::operator_for_deposit(state, deposit);
			inventory::add(state, extraction, commodity, output, owner);
			dispatch(state, extraction, hub, commodity, output, owner);
		});
	});
}

} // namespace economy::physical::shipments
