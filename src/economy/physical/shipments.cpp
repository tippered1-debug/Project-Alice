#include "shipments.hpp"
#include "freight_market.hpp"
#include "deposits.hpp"
#include "inventory.hpp"
#include "system_state.hpp"
#include "commodity_logistics.hpp"
#include "world_trade_capacity.hpp"
#include "actors/ownership.hpp"
#include "economy/physical/extraction.hpp"
#include "exact_person_goods.hpp"
#include "exact_person_freight.hpp"
#include "world/spatial_runtime.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <vector>

namespace economy::physical::shipments {

namespace {

enum class lifecycle : uint8_t { queued = 0, travelling = 1, delivered = 2, blocked = 3 };

struct planned_leg {
	world_trade::transport_mode mode = world_trade::transport_mode::local;
	dcon::site_id origin{};
	dcon::site_id destination{};
	float distance = 0.0f;
	uint32_t traversal_days = 1;
};

dcon::market_id market_for_site(sys::state const& state, dcon::site_id site) {
	if(!site || !state.world.site_is_valid(site)) return {};
	auto province = state.world.site_get_province_from_site_location(site);
	if(!province || !state.world.province_is_valid(province)) return {};
	auto zone = state.world.province_get_state_membership(province);
	return zone ? state.world.state_instance_get_market_from_local_market(zone) : dcon::market_id{};
}

bool plan_spatial_route(sys::state const& state, dcon::site_id origin,
	dcon::site_id destination, std::vector<planned_leg>& result) {
	if(!origin || !destination || origin == destination) return false;
	auto route = world::spatial_runtime::route_for_sites(state, origin, destination);
	if(!route.connected || !std::isfinite(route.distance) || route.distance < 0.0f) return false;
	if(route.edges.empty()) {
		auto const origin_node = world::spatial_runtime::node_for_site(state, origin);
		if(!origin_node || origin_node != world::spatial_runtime::node_for_site(state, destination))
			return false;
		result.push_back({ world_trade::transport_mode::local, origin, destination,
			route.distance, 1 });
		return true;
	}
	if(!std::isfinite(route.travel_days) || route.travel_days <= 0.0f) return false;
	result.push_back({ world_trade::transport_mode::land, origin, destination,
		route.distance, uint32_t(std::max(1.0f, std::ceil(route.travel_days))) });
	return true;
}

bool plan_route(sys::state& state, dcon::site_id origin, dcon::site_id destination,
	std::vector<planned_leg>& result) {
	if(!origin || !destination || origin == destination || !state.world.site_is_valid(origin) || !state.world.site_is_valid(destination)) return false;
	return plan_spatial_route(state, origin, destination, result)
		&& !result.empty() && result.size() <= 255;
}

float canonical_leg_capacity(sys::state const& state, dcon::shipment_route_leg_id leg) {
	if(!leg || !state.world.shipment_route_leg_is_valid(leg)) return 0.0f;
	auto site = state.world.shipment_route_leg_get_origin_site(leg);
	auto destination = state.world.shipment_route_leg_get_destination_site(leg);
	if(auto spatial = world::spatial_runtime::route_for_sites(state, site, destination);
		spatial.connected && spatial.bottleneck_capacity > 0.0f)
		return std::isfinite(spatial.bottleneck_capacity)
			? spatial.bottleneck_capacity : std::numeric_limits<float>::max();
	return 0.0f;
}

uint64_t capacity_key(sys::state const& state, dcon::shipment_route_leg_id leg) {
	auto origin = state.world.shipment_route_leg_get_origin_site(leg);
	auto destination = state.world.shipment_route_leg_get_destination_site(leg);
	auto market = market_for_site(state, origin);
	if(!market) {
		auto first = std::min(origin.index(), destination.index());
		auto second = std::max(origin.index(), destination.index());
		return (uint64_t(1) << 48) | (uint64_t(first) << 24) | uint64_t(second);
	}
	return (uint64_t(1) << 32) | uint64_t(market.index());
}

std::vector<dcon::shipment_route_leg_id> route_legs(sys::state const& state, dcon::shipment_id shipment) {
	std::vector<dcon::shipment_route_leg_id> result;
	state.world.shipment_for_each_shipment_route(shipment, [&](dcon::shipment_route_id relation) {
		auto leg = state.world.shipment_route_get_shipment_route_leg(relation);
		if(leg) result.push_back(leg);
	});
	std::sort(result.begin(), result.end(), [&](auto a, auto b) {
		auto sa = state.world.shipment_route_leg_get_sequence(a);
		auto sb = state.world.shipment_route_leg_get_sequence(b);
		return sa == sb ? a.index() < b.index() : sa < sb;
	});
	return result;
}

dcon::shipment_id create_shipment_from_plan(sys::state& state,
	dcon::site_id origin, dcon::site_id destination, dcon::commodity_id commodity,
	float amount, dcon::economic_actor_id owner, std::vector<planned_leg> const& plan) {
	if(plan.empty() || !std::isfinite(amount) || amount <= 0.0f) return {};
	auto shipment = state.world.create_shipment();
	state.world.shipment_set_commodity(shipment, commodity);
	state.world.shipment_set_remaining_quantity(shipment, amount);
	state.world.shipment_set_lifecycle(shipment, uint8_t(lifecycle::queued));
	state.world.shipment_set_current_leg(shipment, 0);
	state.world.shipment_set_route_leg_count(shipment, uint8_t(plan.size()));
	state.world.force_create_shipment_origin(shipment, origin);
	state.world.force_create_shipment_destination(shipment, destination);
	if(owner) state.world.force_create_shipment_owner(shipment, owner);
	auto profile = logistics::profile_for(state, commodity);
	for(size_t index = 0; index < plan.size(); ++index) {
		auto const& planned = plan[index];
		auto leg = state.world.create_shipment_route_leg();
		state.world.shipment_route_leg_set_mode(leg, uint8_t(planned.mode));
		state.world.shipment_route_leg_set_origin_site(leg, planned.origin);
		state.world.shipment_route_leg_set_destination_site(leg, planned.destination);
		state.world.shipment_route_leg_set_sequence(leg, uint8_t(index));
		state.world.shipment_route_leg_set_distance(leg, planned.distance);
		state.world.shipment_route_leg_set_remaining_transport_work(leg,
			index == 0 ? logistics::cargo_units(profile, amount) : 0.0f);
		state.world.shipment_route_leg_set_traversal_days(leg, planned.traversal_days);
		state.world.force_create_shipment_route(leg, shipment);
	}
	if(auto legs = route_legs(state, shipment); !legs.empty())
		state.world.shipment_set_remaining_days(shipment,
			state.world.shipment_route_leg_get_traversal_days(legs.front()));
	return shipment;
}

} // namespace

bool can_dispatch(sys::state& state, dcon::site_id origin, dcon::site_id destination,
	dcon::commodity_id commodity, float quantity) {
	if(!origin || !destination || !commodity || !state.world.site_is_valid(origin) || !state.world.site_is_valid(destination) || !state.world.commodity_is_valid(commodity) || !std::isfinite(quantity) || quantity <= 0.0f) return false;
	route_quote quote;
	return quote_route(state, origin, destination, quote);
}

bool quote_route(sys::state& state, dcon::site_id origin, dcon::site_id destination,
	route_quote& quote) {
	if(!origin || !destination || !state.world.site_is_valid(origin) || !state.world.site_is_valid(destination)) return false;
	std::vector<planned_leg> plan;
	if(!plan_route(state, origin, destination, plan) || plan.empty() || plan.size() > 255)
		return false;
	quote = {};
	quote.route_leg_count = uint8_t(plan.size());
	for(auto const& leg : plan) {
		if(!std::isfinite(leg.distance) || leg.distance < 0.0f) return false;
		quote.distance += leg.distance;
		quote.required_mode_mask |= uint8_t(1u << uint8_t(leg.mode));
	}
	return std::isfinite(quote.distance);
}

dcon::shipment_id dispatch(sys::state& state, dcon::site_id origin, dcon::site_id destination,
	dcon::commodity_id commodity, float amount, dcon::economic_actor_id owner) {
	return dispatch_transfer(state, origin, destination, commodity, amount, owner, owner);
}

dcon::shipment_id dispatch_transfer(sys::state& state, dcon::site_id origin, dcon::site_id destination,
	dcon::commodity_id commodity, float amount, dcon::economic_actor_id seller, dcon::economic_actor_id buyer) {
	if(!origin || !destination || !commodity || !seller != !buyer || !std::isfinite(amount) || amount <= 0.0f)
		return dcon::shipment_id{};
	std::vector<planned_leg> plan;
	if(!plan_route(state, origin, destination, plan))
		return dcon::shipment_id{};
	auto removed = inventory::remove(state, origin, commodity, amount, seller);
	if(removed <= 0.0f)
		return dcon::shipment_id{};
	auto shipment = create_shipment_from_plan(state, origin, destination, commodity, removed, buyer, plan);
	if(!shipment) inventory::add(state, origin, commodity, removed, seller);
	return shipment;
}

dcon::shipment_id dispatch_exact(sys::state& state, persons::person_key owner,
	dcon::site_id origin, dcon::site_id destination, dcon::commodity_id commodity,
	float amount, uint64_t exact_contract_id) {
	if(!persons::exists(state, owner) || !origin || !destination || origin == destination || !commodity || !std::isfinite(amount) || amount <= 0.0f) return {};
	std::vector<planned_leg> plan;
	if(!plan_route(state, origin, destination, plan)) return {};
	auto removed = exact_person_goods::remove_stock(state, owner, origin, commodity, amount);
	if(removed != amount) {
		if(removed > 0.0f) exact_person_goods::add_stock(state, owner, origin, commodity, removed);
		return {};
	}
	auto shipment = create_shipment_from_plan(state, origin, destination, commodity, removed, {}, plan);
	if(!shipment || !exact_person_freight::register_shipment_owner(state, shipment, owner, exact_contract_id)) {
		if(shipment) state.world.delete_shipment(shipment);
		exact_person_goods::add_stock(state, owner, origin, commodity, removed);
		return {};
	}
	return shipment;
}

void advance(sys::state& state) {
	std::vector<dcon::shipment_id> shipments;
	state.world.for_each_shipment([&](dcon::shipment_id shipment) { shipments.push_back(shipment); });
	std::sort(shipments.begin(), shipments.end(), [](auto a, auto b) { return a.index() < b.index(); });
	std::unordered_map<uint64_t, float> available_capacity;
	std::unordered_map<uint64_t, bool> initialized_capacity;
	for(auto shipment : shipments) {
		if(!state.world.shipment_is_valid(shipment)) continue;
		auto commodity = state.world.shipment_get_commodity(shipment);
		auto profile = logistics::profile_for(state, commodity);
		auto remaining = std::max(0.0f, state.world.shipment_get_remaining_quantity(shipment));
		remaining *= std::max(0.0f, 1.0f - profile.daily_spoilage);
		state.world.shipment_set_remaining_quantity(shipment, remaining);
	}

	for(auto shipment : shipments) {
		if(!state.world.shipment_is_valid(shipment) || lifecycle(state.world.shipment_get_lifecycle(shipment)) != lifecycle::queued) continue;
		auto legs = route_legs(state, shipment);
		auto profile = logistics::profile_for(state, state.world.shipment_get_commodity(shipment));
		auto current = state.world.shipment_get_current_leg(shipment);
		if(current >= legs.size()) {
			state.world.shipment_set_lifecycle(shipment, uint8_t(lifecycle::blocked));
			continue;
		}
		auto leg = legs[current];
		auto key = capacity_key(state, leg);
		if(!initialized_capacity[key]) {
			available_capacity[key] = canonical_leg_capacity(state, leg);
			initialized_capacity[key] = true;
		}
		// Consumed capacity is never refunded. Only the unconsumed work is
		// reduced with the cargo that spoiled while this leg was queued.
		auto work = std::max(0.0f, state.world.shipment_route_leg_get_remaining_transport_work(leg));
		work *= std::max(0.0f, 1.0f - profile.daily_spoilage);
		state.world.shipment_route_leg_set_remaining_transport_work(leg, work);
		auto admitted = std::min(work, std::max(0.0f, available_capacity[key]));
		state.world.shipment_route_leg_set_remaining_transport_work(leg, work - admitted);
		available_capacity[key] -= admitted;
		if(work - admitted <= 0.00001f) {
			state.world.shipment_set_lifecycle(shipment, uint8_t(lifecycle::travelling));
			state.world.shipment_set_remaining_days(shipment,
				state.world.shipment_route_leg_get_traversal_days(leg));
		}
	}

	for(auto shipment : shipments) {
		if(!state.world.shipment_is_valid(shipment) || lifecycle(state.world.shipment_get_lifecycle(shipment)) != lifecycle::travelling) continue;
		auto days = state.world.shipment_get_remaining_days(shipment);
		if(days > 1) {
			state.world.shipment_set_remaining_days(shipment, days - 1);
			continue;
		}
		auto legs = route_legs(state, shipment);
		auto profile = logistics::profile_for(state, state.world.shipment_get_commodity(shipment));
		auto current = state.world.shipment_get_current_leg(shipment);
		if(current + 1 < legs.size()) {
			auto next_leg = legs[current + 1];
			state.world.shipment_route_leg_set_remaining_transport_work(next_leg,
				logistics::cargo_units(profile, state.world.shipment_get_remaining_quantity(shipment)));
			state.world.shipment_set_current_leg(shipment, uint8_t(current + 1));
			state.world.shipment_set_lifecycle(shipment, uint8_t(lifecycle::queued));
			state.world.shipment_set_remaining_days(shipment,
				state.world.shipment_route_leg_get_traversal_days(next_leg));
			continue;
		}
		auto destination_relation = state.world.shipment_get_shipment_destination(shipment);
		auto destination = state.world.shipment_destination_get_site(destination_relation);
		if(exact_person_freight::is_external_shipment(state, shipment)) {
			if(exact_person_freight::complete_external_shipment(state, shipment,
				state.world.shipment_get_remaining_quantity(shipment)))
				state.world.delete_shipment(shipment);
			continue;
		}
		auto owner_relation = state.world.shipment_get_shipment_owner(shipment);
		auto owner = owner_relation ? state.world.shipment_owner_get_economic_actor(owner_relation) : dcon::economic_actor_id{};
		inventory::add(state, destination, state.world.shipment_get_commodity(shipment),
			state.world.shipment_get_remaining_quantity(shipment), owner);
		freight_market::complete_contract_for_shipment(state, shipment);
		state.world.delete_shipment(shipment);
	}
}

void process_arrivals(sys::state& state) {
	advance(state);
}

void project_route_volumes_to_legacy_view(sys::state& state) {
	// Trade-route volume is a UI/read-model field. It is derived from shipments
	// physically occupying their current route leg; it never creates trade.
	state.world.for_each_trade_route([&](dcon::trade_route_id route) {
		state.world.for_each_commodity([&](dcon::commodity_id commodity) {
			state.world.trade_route_set_volume(route, commodity, 0.0f);
		});
	});

	state.world.for_each_shipment([&](dcon::shipment_id shipment) {
		auto const status = lifecycle(state.world.shipment_get_lifecycle(shipment));
		if(status != lifecycle::queued && status != lifecycle::travelling) return;
		auto const quantity = state.world.shipment_get_remaining_quantity(shipment);
		if(!std::isfinite(quantity) || quantity <= 0.0f) return;
		auto const legs = route_legs(state, shipment);
		auto const current = state.world.shipment_get_current_leg(shipment);
		if(current >= legs.size()) return;
		auto const leg = legs[current];
		auto const route = state.world.shipment_route_leg_get_trade_route(leg);
		if(!route || !state.world.trade_route_is_valid(route)) return;
		auto const origin = market_for_site(state,
			state.world.shipment_route_leg_get_origin_site(leg));
		auto const destination = market_for_site(state,
			state.world.shipment_route_leg_get_destination_site(leg));
		auto const market_a = state.world.trade_route_get_connected_markets(route, 0);
		auto const market_b = state.world.trade_route_get_connected_markets(route, 1);
		float direction = 0.0f;
		if(origin == market_a && destination == market_b) direction = 1.0f;
		else if(origin == market_b && destination == market_a) direction = -1.0f;
		if(direction == 0.0f) return;
		auto const commodity = state.world.shipment_get_commodity(shipment);
		auto const old_volume = state.world.trade_route_get_volume(route, commodity);
		state.world.trade_route_set_volume(route, commodity,
			old_volume + direction * quantity);
	});
}

void process_rgo_output(sys::state& state) {
	// Canonical deposits are the only source of canonical extraction.  In
	// particular this path never consults province.rgo_output.
	state.world.for_each_province([&](dcon::province_id province) {
		state.world.for_each_commodity([&](dcon::commodity_id commodity) {
			state.world.province_set_rgo_output(province, commodity, 0.0f);
			state.world.province_set_rgo_output_per_worker(province, commodity, 0.0f);
		});
	});
	state.world.for_each_resource_deposit([&](dcon::resource_deposit_id deposit) {
		auto commodity = state.world.resource_deposit_get_commodity(deposit);
		if(!commodity) {
			assert(false && "canonical resource deposit requires a commodity");
			std::abort();
		}
		auto operator_actor = actors::ownership::operator_for_deposit(state, deposit);
		auto site = state.world.resource_deposit_get_site_from_resource_deposit_site(deposit);
		auto province = site ? state.world.site_get_province_from_site_location(site) : dcon::province_id{};
		auto zone = province ? state.world.province_get_state_membership(province) : dcon::state_instance_id{};
		auto market = zone ? state.world.state_instance_get_market_from_local_market(zone) : dcon::market_id{};
		auto hub = market ? deposits::market_hub_for(state, market) : dcon::site_id{};
		if(!operator_actor || !site || !hub) {
			assert(false && "canonical resource deposit requires an operator, site, and market hub");
			std::abort();
		}
		auto target = state.world.resource_deposit_get_target_daily_extraction(deposit);
		auto amount = extraction::extract_resource(state, deposit, operator_actor, target, state.current_date);
		if(amount > 0.0f) {
			auto previous = state.world.province_get_rgo_output(province, commodity);
			state.world.province_set_rgo_output(province, commodity, previous + amount);
		}
		if(amount > 0.0f && !dispatch(state, site, hub, commodity, amount, operator_actor)) {
			// Extraction is already a committed physical event. A failed dispatch
			// leaves the operator stock at the extraction site for a later retry.
		}
	});
}

} // namespace economy::physical::shipments
