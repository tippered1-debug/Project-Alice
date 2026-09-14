#include "factory_inputs.hpp"

#include "inventory.hpp"
#include "market_clearing.hpp"
#include "system_state.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace economy::physical::factory_inputs {
namespace {

constexpr float epsilon = 1.0e-5f;

bool physical_commodity(sys::state const& state, dcon::commodity_id commodity) {
	return commodity
		&& !state.world.commodity_get_is_local(commodity)
		&& !state.world.commodity_get_money_rgo(commodity);
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
