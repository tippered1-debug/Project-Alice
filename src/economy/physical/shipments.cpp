#include "shipments.hpp"
#include "freight_market.hpp"
#include "deposits.hpp"
#include "inventory.hpp"
#include "system_state.hpp"
#include "province.hpp"
#include "economy_production.hpp"
#include "commodity_logistics.hpp"
#include "economy_stats.hpp"
#include "world_trade_capacity.hpp"
#include "actors/ownership.hpp"
#include "economy/physical/extraction.hpp"
#include "exact_person_goods.hpp"
#include "exact_person_freight.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace economy::physical::shipments {

namespace {

enum class lifecycle : uint8_t { queued = 0, travelling = 1, delivered = 2, blocked = 3 };

struct planned_leg {
	world_trade::transport_mode mode = world_trade::transport_mode::local;
	dcon::trade_route_id trade_route{};
	dcon::site_id origin{};
	dcon::site_id destination{};
	float distance = 0.0f;
};

struct market_path_step {
	dcon::market_id market{};
	dcon::trade_route_id route{};
	world_trade::transport_mode mode = world_trade::transport_mode::land;
	float distance = 0.0f;
};

dcon::market_id market_for_site(sys::state const& state, dcon::site_id site) {
	if(!site || !state.world.site_is_valid(site)) return {};
	auto province = state.world.site_get_province_from_site_location(site);
	if(!province || !state.world.province_is_valid(province)) return {};
	auto zone = state.world.province_get_state_membership(province);
	return zone ? state.world.state_instance_get_market_from_local_market(zone) : dcon::market_id{};
}

float local_distance(sys::state& state, dcon::site_id origin, dcon::site_id destination) {
	auto from = state.world.site_get_province_from_site_location(origin);
	auto to = state.world.site_get_province_from_site_location(destination);
	return from && to ? std::max(0.0f, province::direct_distance(state, from, to)) : 0.0f;
}

bool finite_route_distance(float distance) {
	return std::isfinite(distance) && distance >= 0.0f && distance < 99998.0f;
}

bool best_route_mode(sys::state const& state, dcon::trade_route_id route,
	world_trade::transport_mode& mode, float& distance) {
	if(!route || !state.world.trade_route_is_valid(route)
		|| state.world.trade_route_get_is_trade_forbidden(route)) return false;
	auto market_a = state.world.trade_route_get_connected_markets(route, 0);
	auto market_b = state.world.trade_route_get_connected_markets(route, 1);
	if(!market_a || !market_b) return false;
	auto physically_feasible = [&](world_trade::transport_mode candidate) {
		return world_trade::canonical_capacity(state, market_a, candidate) > 0.0f
			&& world_trade::canonical_capacity(state, market_b, candidate) > 0.0f;
	};
	bool found = false;
	if(state.world.trade_route_get_is_land_route(route) && physically_feasible(world_trade::transport_mode::land)) {
		auto candidate = state.world.trade_route_get_land_distance(route);
		if(finite_route_distance(candidate)) {
			mode = world_trade::transport_mode::land;
			distance = candidate;
			found = true;
		}
	}
	if(state.world.trade_route_get_is_sea_route(route) && physically_feasible(world_trade::transport_mode::sea)) {
		auto candidate = state.world.trade_route_get_sea_distance(route);
		if(finite_route_distance(candidate) && (!found || candidate < distance)) {
			mode = world_trade::transport_mode::sea;
			distance = candidate;
			found = true;
		}
	}
	return found;
}

bool find_market_path(sys::state const& state, dcon::market_id origin,
	dcon::market_id destination, std::vector<market_path_step>& result) {
	if(!origin || !destination || !state.world.market_is_valid(origin)
		|| !state.world.market_is_valid(destination)) return false;
	if(origin == destination) return true;

	auto const market_count = state.world.market_size();
	std::vector<float> distance(market_count, std::numeric_limits<float>::infinity());
	std::vector<dcon::market_id> previous(market_count);
	std::vector<dcon::trade_route_id> previous_route(market_count);
	std::vector<world_trade::transport_mode> previous_mode(market_count,
		world_trade::transport_mode::land);
	using queue_item = std::tuple<float, uint32_t, uint32_t>;
	std::priority_queue<queue_item, std::vector<queue_item>, std::greater<queue_item>> queue;
	distance[origin.index()] = 0.0f;
	queue.emplace(0.0f, 0, uint32_t(origin.index()));

	while(!queue.empty()) {
		auto [current_distance, unused_tie, current_index] = queue.top();
		(void)unused_tie;
		queue.pop();
		dcon::market_id current{ dcon::market_id::value_base_t(current_index) };
		if(current_distance != distance[current.index()]) continue;
		if(current == destination) break;
		std::vector<dcon::trade_route_id> routes;
		state.world.market_for_each_trade_route(current, [&](dcon::trade_route_id route) {
			routes.push_back(route);
		});
		std::sort(routes.begin(), routes.end(), [](auto a, auto b) { return a.index() < b.index(); });
		for(auto route : routes) {
			world_trade::transport_mode mode{};
			float edge_distance = 0.0f;
			if(!best_route_mode(state, route, mode, edge_distance)) continue;
			auto a = state.world.trade_route_get_connected_markets(route, 0);
			auto b = state.world.trade_route_get_connected_markets(route, 1);
			auto next = a == current ? b : (b == current ? a : dcon::market_id{});
			if(!next || next.index() >= distance.size()) continue;
			auto candidate = current_distance + edge_distance;
			bool better = candidate < distance[next.index()];
			if(!better && candidate == distance[next.index()]) {
				better = !previous_route[next.index()]
					|| route.index() < previous_route[next.index()].index();
			}
			if(!better) continue;
			distance[next.index()] = candidate;
			previous[next.index()] = current;
			previous_route[next.index()] = route;
			previous_mode[next.index()] = mode;
			queue.emplace(candidate, uint32_t(next.index()), uint32_t(next.index()));
		}
	}
	if(!std::isfinite(distance[destination.index()])) return false;

	std::vector<market_path_step> reverse;
	for(auto current = destination; current != origin; current = previous[current.index()]) {
		if(!previous[current.index()] || !previous_route[current.index()]) return false;
		float edge_distance = 0.0f;
		auto edge_mode = previous_mode[current.index()];
		if(!best_route_mode(state, previous_route[current.index()], edge_mode, edge_distance)) return false;
		reverse.push_back({ current, previous_route[current.index()], edge_mode, edge_distance });
	}
	std::reverse(reverse.begin(), reverse.end());
	result = std::move(reverse);
	return true;
}

bool plan_route(sys::state& state, dcon::site_id origin, dcon::site_id destination,
	std::vector<planned_leg>& result) {
	if(!origin || !destination || origin == destination
		|| !state.world.site_is_valid(origin) || !state.world.site_is_valid(destination)) return false;
	auto origin_market = market_for_site(state, origin);
	auto destination_market = market_for_site(state, destination);
	if(!origin_market && !destination_market) {
		result.push_back({ world_trade::transport_mode::local, {}, origin, destination,
			local_distance(state, origin, destination) });
		return result.size() <= 255;
	}
	if(!origin_market || !destination_market) return false;

	auto origin_hub = deposits::market_hub_for(state, origin_market);
	auto destination_hub = deposits::market_hub_for(state, destination_market);
	if(origin_market == destination_market) {
		if(!origin_hub || !destination_hub) {
			result.push_back({ world_trade::transport_mode::local, {}, origin, destination,
				local_distance(state, origin, destination) });
			return true;
		}
		if(origin != origin_hub)
			result.push_back({ world_trade::transport_mode::local, {}, origin, origin_hub,
				local_distance(state, origin, origin_hub) });
		if(origin_hub != destination_hub)
			result.push_back({ world_trade::transport_mode::local, {}, origin_hub, destination_hub,
				local_distance(state, origin_hub, destination_hub) });
		if(destination_hub != destination)
			result.push_back({ world_trade::transport_mode::local, {}, destination_hub, destination,
				local_distance(state, destination_hub, destination) });
		return !result.empty() && result.size() <= 255;
	}

	if(!origin_hub || !destination_hub) return false;
	std::vector<market_path_step> path;
	if(!find_market_path(state, origin_market, destination_market, path)) return false;
	if(origin != origin_hub)
		result.push_back({ world_trade::transport_mode::local, {}, origin, origin_hub,
			local_distance(state, origin, origin_hub) });
	auto current_hub = origin_hub;
	for(auto const& step : path) {
		auto next_hub = deposits::market_hub_for(state, step.market);
		if(!next_hub) return false;
		result.push_back({ step.mode, step.route, current_hub, next_hub, step.distance });
		current_hub = next_hub;
	}
	if(current_hub != destination)
		result.push_back({ world_trade::transport_mode::local, {}, current_hub, destination,
			local_distance(state, current_hub, destination) });
	return !result.empty() && result.size() <= 255;
}

float canonical_leg_capacity(sys::state const& state, dcon::shipment_route_leg_id leg) {
	if(!leg || !state.world.shipment_route_leg_is_valid(leg)) return 0.0f;
	auto mode = world_trade::transport_mode(state.world.shipment_route_leg_get_mode(leg));
	if(mode != world_trade::transport_mode::local) {
		auto route = state.world.shipment_route_leg_get_trade_route(leg);
		if(!route || !state.world.trade_route_is_valid(route)) return 0.0f;
		auto a = state.world.trade_route_get_connected_markets(route, 0);
		auto b = state.world.trade_route_get_connected_markets(route, 1);
		return std::min(world_trade::canonical_capacity(state, a, mode),
			world_trade::canonical_capacity(state, b, mode));
	}
	auto site = state.world.shipment_route_leg_get_origin_site(leg);
	auto market = market_for_site(state, site);
	auto capacity = world_trade::canonical_capacity(state, market, mode);
	// Only sites without a market mapping use the explicit transitional local
	// resource. A mapped market's zero physical capacity remains zero.
	return market ? capacity : 100.0f;
}

uint64_t capacity_key(sys::state const& state, dcon::shipment_route_leg_id leg) {
	auto route = state.world.shipment_route_leg_get_trade_route(leg);
	if(route) return uint64_t(route.index()) + 1;
	auto market = market_for_site(state, state.world.shipment_route_leg_get_origin_site(leg));
	return (uint64_t(1) << 32) | uint64_t(market ? market.index() : leg.index());
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
		state.world.shipment_route_leg_set_trade_route(leg, planned.trade_route);
		state.world.shipment_route_leg_set_origin_site(leg, planned.origin);
		state.world.shipment_route_leg_set_destination_site(leg, planned.destination);
		state.world.shipment_route_leg_set_sequence(leg, uint8_t(index));
		state.world.shipment_route_leg_set_distance(leg, planned.distance);
		state.world.shipment_route_leg_set_remaining_transport_work(leg,
			index == 0 ? logistics::cargo_units(profile, amount) : 0.0f);
		state.world.shipment_route_leg_set_traversal_days(leg, compatibility_travel_days(planned.distance));
		state.world.force_create_shipment_route(leg, shipment);
	}
	if(auto legs = route_legs(state, shipment); !legs.empty())
		state.world.shipment_set_remaining_days(shipment,
			state.world.shipment_route_leg_get_traversal_days(legs.front()));
	return shipment;
}

} // namespace

uint32_t compatibility_travel_days(float distance) noexcept {
	if(!std::isfinite(distance) || distance <= 0.0f)
		return 1;
	return uint32_t(std::max(1.0f, std::ceil(distance / compatibility_distance_units_per_day)));
}

bool can_dispatch(sys::state& state, dcon::site_id origin, dcon::site_id destination,
	dcon::commodity_id commodity, float quantity) {
	if(!origin || !destination || !commodity || !state.world.site_is_valid(origin)
		|| !state.world.site_is_valid(destination) || !state.world.commodity_is_valid(commodity)
		|| !std::isfinite(quantity) || quantity <= 0.0f) return false;
	route_quote quote;
	return quote_route(state, origin, destination, quote);
}

bool quote_route(sys::state& state, dcon::site_id origin, dcon::site_id destination,
	route_quote& quote) {
	if(!origin || !destination || !state.world.site_is_valid(origin)
		|| !state.world.site_is_valid(destination)) return false;
	std::vector<planned_leg> plan;
	if(!plan_route(state, origin, destination, plan) || plan.empty() || plan.size() > 255)
		return false;
	quote = {};
	quote.route_leg_count = uint8_t(plan.size());
	for(auto const& leg : plan) {
		if(!std::isfinite(leg.distance) || leg.distance < 0.0f) return false;
		quote.distance += leg.distance;
		quote.required_mode_mask |= uint8_t(1u << uint8_t(leg.mode));
		if(!quote.primary_trade_route && leg.trade_route) quote.primary_trade_route = leg.trade_route;
	}
	return std::isfinite(quote.distance);
}

dcon::shipment_id dispatch(sys::state& state, dcon::site_id origin, dcon::site_id destination,
	dcon::commodity_id commodity, float amount, dcon::economic_actor_id owner) {
	return dispatch_transfer(state, origin, destination, commodity, amount, owner, owner);
}

dcon::shipment_id dispatch_transfer(sys::state& state, dcon::site_id origin, dcon::site_id destination,
	dcon::commodity_id commodity, float amount, dcon::economic_actor_id seller, dcon::economic_actor_id buyer) {
	if(!origin || !destination || !commodity || (!seller != !buyer) || !std::isfinite(amount) || amount <= 0.0f)
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

dcon::shipment_id dispatch_exact(sys::state& state, persons::exact_population::person_key owner,
	dcon::site_id origin, dcon::site_id destination, dcon::commodity_id commodity,
	float amount, uint64_t exact_contract_id) {
	if(!persons::exact_population::exists(state, owner) || !origin || !destination || origin == destination
		|| !commodity || !std::isfinite(amount) || amount <= 0.0f) return {};
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
		if(!state.world.shipment_is_valid(shipment)
			|| lifecycle(state.world.shipment_get_lifecycle(shipment)) != lifecycle::queued) continue;
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
		if(!state.world.shipment_is_valid(shipment)
			|| lifecycle(state.world.shipment_get_lifecycle(shipment)) != lifecycle::travelling) continue;
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
			exact_person_freight::complete_external_shipment(state, shipment,
				state.world.shipment_get_remaining_quantity(shipment));
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

void process_rgo_output(sys::state& state) {
	// Canonical deposits are the source of truth. Their output never reads
	// province.rgo_output; legacy RGO is handled in the compatibility branch.
	state.world.for_each_resource_deposit([&](dcon::resource_deposit_id deposit) {
		if(state.world.resource_deposit_get_legacy_compatibility_deposit(deposit)) return;
		auto commodity = state.world.resource_deposit_get_commodity(deposit);
		if(!commodity || state.world.commodity_get_is_local(commodity) || state.world.commodity_get_money_rgo(commodity)) return;
		auto operator_actor = actors::ownership::operator_for_deposit(state, deposit);
		auto site = state.world.resource_deposit_get_site_from_resource_deposit_site(deposit);
		auto province = site ? state.world.site_get_province_from_site_location(site) : dcon::province_id{};
		auto zone = province ? state.world.province_get_state_membership(province) : dcon::state_instance_id{};
		auto market = zone ? state.world.state_instance_get_market_from_local_market(zone) : dcon::market_id{};
		auto hub = market ? deposits::market_hub_for(state, market) : dcon::site_id{};
		if(!operator_actor || !site || !hub) return;
		auto target = state.world.resource_deposit_get_target_daily_extraction(deposit);
		auto amount = extraction::extract_resource(state, deposit, operator_actor, target, state.current_date);
		if(amount > 0.0f && !dispatch(state, site, hub, commodity, amount, operator_actor)) {
			// Extraction is already a committed physical event. A failed dispatch
			// leaves the operator stock at the extraction site for a later retry.
		}
	});

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
			if(!deposit || !state.world.resource_deposit_get_legacy_compatibility_deposit(deposit))
				return;
			auto extraction = deposit ? state.world.resource_deposit_get_site_from_resource_deposit_site(deposit) : dcon::site_id{};
			if(!extraction)
				return;
			auto owner = actors::ownership::operator_for_deposit(state, deposit);
			if(!owner)
				return;
			inventory::add(state, extraction, commodity, output, owner);
			dispatch(state, extraction, hub, commodity, output, owner);
		});
	});
}

} // namespace economy::physical::shipments
