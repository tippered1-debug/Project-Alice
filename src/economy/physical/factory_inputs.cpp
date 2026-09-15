#include "factory_inputs.hpp"

#include "inventory.hpp"
#include "market_clearing.hpp"
#include "deposits.hpp"
#include "shipments.hpp"
#include "system_state.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace economy::physical::factory_inputs {
namespace {

constexpr float epsilon = 1.0e-5f;

struct planned_order {
	dcon::site_id destination{};
	dcon::economic_actor_id owner{};
	economy::commodity_set inputs{};
	dcon::market_id market{};
	float input_scale = 0.0f;
	std::array<dcon::commodity_id, economy::commodity_set::set_size> commodities{};
	std::array<float, economy::commodity_set::set_size> quantities{};
	bool ready = false;
};

std::vector<planned_order> planned_orders;

bool physical_commodity(sys::state const& state, dcon::commodity_id commodity) {
	return commodity
		&& !state.world.commodity_get_is_local(commodity)
		&& !state.world.commodity_get_money_rgo(commodity);
}

float in_transit_to(sys::state const& state, dcon::site_id destination,
	dcon::commodity_id commodity, dcon::economic_actor_id owner) {
	float result = 0.0f;
	state.world.for_each_shipment([&](dcon::shipment_id shipment) {
		auto owner_relation = state.world.shipment_get_shipment_owner(shipment);
		if(state.world.shipment_get_commodity(shipment) != commodity
			|| !owner_relation
			|| state.world.shipment_owner_get_economic_actor(owner_relation) != owner)
			return;
		auto destination_relation = state.world.shipment_get_shipment_destination(shipment);
		if(destination_relation && state.world.shipment_destination_get_site(destination_relation) == destination)
			result += std::max(0.0f, state.world.shipment_get_remaining_quantity(shipment));
	});
	return result;
}

float required_for(economy::commodity_set const& inputs, dcon::commodity_id commodity, float input_scale) {
	float total = 0.0f;
	for(uint32_t i = 0; i < economy::commodity_set::set_size; ++i) {
		if(!inputs.commodity_type[i]) break;
		if(inputs.commodity_type[i] == commodity)
			total += std::max(0.0f, inputs.commodity_amounts[i]) * input_scale;
	}
	return total;
}

bool seen_before(economy::commodity_set const& inputs, uint32_t index) {
	for(uint32_t i = 0; i < index; ++i)
		if(inputs.commodity_type[i] == inputs.commodity_type[index]) return true;
	return false;
}

} // namespace

bool ordinary_physical_input(sys::state const& state, dcon::commodity_id commodity) noexcept {
	return physical_commodity(state, commodity);
}

float net_demand(sys::state const& state, dcon::site_id destination,
	dcon::economic_actor_id owner, dcon::commodity_id commodity, float required) noexcept {
	if(!std::isfinite(required) || required <= 0.0f)
		return 0.0f;
	return std::max(0.0f, required - inventory::quantity(state, destination, commodity, owner)
		- in_transit_to(state, destination, commodity, owner));
}

void begin_planning(sys::state& state) {
	planned_orders.assign(state.world.factory_size(), planned_order{});
}

bool plan(sys::state& state, dcon::factory_id factory, dcon::site_id destination,
	dcon::economic_actor_id owner, economy::commodity_set const& inputs,
	dcon::market_id market, float input_scale) {
	if(!factory || factory.index() >= planned_orders.size())
		return false;
	planned_orders[factory.index()] = { destination, owner, inputs, market, input_scale, {}, {}, false };
	if(!destination || !owner || !market || !std::isfinite(input_scale) || input_scale < 0.0f)
		return false;
	auto hub = deposits::market_hub_for(state, market);
	if(!hub)
		return false;
	uint32_t quantity_index = 0;
	for(uint32_t i = 0; i < economy::commodity_set::set_size; ++i) {
		auto commodity = inputs.commodity_type[i];
		if(!commodity) break;
		if(seen_before(inputs, i) || !physical_commodity(state, commodity)) continue;
		planned_orders[factory.index()].commodities[quantity_index] = commodity;
		planned_orders[factory.index()].quantities[quantity_index] = net_demand(
			state, destination, owner, commodity, required_for(inputs, commodity, input_scale));
		++quantity_index;
	}
	planned_orders[factory.index()].ready = true;
	return true;
}

float planned_quantity(sys::state const& state, dcon::factory_id factory,
	dcon::commodity_id commodity, float fallback) noexcept {
	if(!factory || factory.index() >= planned_orders.size()) return fallback;
	auto const& order = planned_orders[factory.index()];
	for(uint32_t i = 0; i < order.commodities.size(); ++i)
		if(order.commodities[i] == commodity)
			return order.quantities[i];
	return fallback;
}

void fulfill(sys::state& state) {
	state.world.for_each_factory([&](dcon::factory_id factory) {
		if(factory.index() >= planned_orders.size() || !planned_orders[factory.index()].ready)
			return;
		auto const& order = planned_orders[factory.index()];
		auto hub = deposits::market_hub_for(state, order.market);
		if(!hub) return;
	for(uint32_t i = 0; i < economy::commodity_set::set_size; ++i) {
		auto commodity = order.inputs.commodity_type[i];
		if(!commodity) break;
		if(seen_before(order.inputs, i) || !physical_commodity(state, commodity)) continue;
		auto required = required_for(order.inputs, commodity, order.input_scale);
		auto planned = 0.0f;
		for(uint32_t quantity_index = 0; quantity_index < order.commodities.size(); ++quantity_index)
			if(order.commodities[quantity_index] == commodity) {
				planned = order.quantities[quantity_index];
				break;
			}
		if(planned <= 0.0f) continue;
		auto allocated = planned * std::clamp(market_clearing::fill(
			state, order.market, commodity, market_clearing::demand_class::intermediate), 0.0f, 1.0f);
		allocated = std::min(allocated, std::max(0.0f, state.world.market_get_stockpile(order.market, commodity)));
		if(allocated <= 0.0f) continue;
		state.world.market_set_stockpile(order.market, commodity,
			std::max(0.0f, state.world.market_get_stockpile(order.market, commodity) - allocated));
		inventory::add(state, hub, commodity, allocated, order.owner);
		if(!shipments::dispatch(state, hub, order.destination, commodity, allocated, order.owner)) {
			inventory::add(state, hub, commodity, allocated, order.owner);
			state.world.market_set_stockpile(order.market, commodity,
				state.world.market_get_stockpile(order.market, commodity) + allocated);
		}
	}
});
}

availability evaluate(sys::state const& state, dcon::site_id site, dcon::economic_actor_id owner,
	economy::commodity_set const& inputs, dcon::market_id market, float input_scale) {
	availability result{};
	if(!std::isfinite(input_scale) || input_scale < 0.0f)
		return result;

	result.legacy_ratio = 1.0f;
	result.physical_ratio = 1.0f;
	bool has_physical = false;
	for(uint32_t i = 0; i < economy::commodity_set::set_size; ++i) {
		auto commodity = inputs.commodity_type[i];
		if(!commodity) break;
		if(seen_before(inputs, i)) continue;
		if(physical_commodity(state, commodity)) {
			has_physical = true;
			if(input_scale > 0.0f) {
				auto required = required_for(inputs, commodity, input_scale);
				if(required > 0.0f)
					result.physical_ratio = std::min(result.physical_ratio,
						std::clamp(inventory::quantity(state, site, commodity, owner) / required, 0.0f, 1.0f));
			}
		} else if(market) {
			result.legacy_ratio = std::min(result.legacy_ratio,
				std::clamp(market_clearing::fill(state, market, commodity,
					market_clearing::demand_class::intermediate), 0.0f, 1.0f));
		}
	}
	result.active = has_physical && site && owner;
	return result;
}

bool consume(sys::state& state, dcon::site_id site, dcon::economic_actor_id owner,
	economy::commodity_set const& inputs, float input_scale, float ratio) {
	if(!site || !owner || !std::isfinite(input_scale) || input_scale < 0.0f
		|| !std::isfinite(ratio) || ratio < 0.0f || ratio > 1.0f + epsilon)
		return false;
	ratio = std::clamp(ratio, 0.0f, 1.0f);

	std::array<dcon::commodity_id, economy::commodity_set::set_size> commodities{};
	std::array<float, economy::commodity_set::set_size> amounts{};
	uint32_t count = 0;
	for(uint32_t i = 0; i < economy::commodity_set::set_size; ++i) {
		auto commodity = inputs.commodity_type[i];
		if(!commodity) break;
		if(seen_before(inputs, i) || !physical_commodity(state, commodity)) continue;
		commodities[count] = commodity;
		amounts[count] = required_for(inputs, commodity, input_scale) * ratio;
		++count;
	}

	for(uint32_t i = 0; i < count; ++i) {
		if(amounts[i] <= 0.0f) continue;
		if(inventory::quantity(state, site, commodities[i], owner) + epsilon < amounts[i])
			return false;
	}

	std::array<float, economy::commodity_set::set_size> removed{};
	for(uint32_t i = 0; i < count; ++i) {
		removed[i] = inventory::remove(state, site, commodities[i], amounts[i], owner);
		if(removed[i] + epsilon < amounts[i]) {
			for(uint32_t j = 0; j < i; ++j)
				if(removed[j] > 0.0f) inventory::add(state, site, commodities[j], removed[j], owner);
			return false;
		}
	}
	return true;
}

} // namespace economy::physical::factory_inputs
