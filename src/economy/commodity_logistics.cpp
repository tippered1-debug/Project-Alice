#include "commodity_logistics.hpp"

#include "system_state.hpp"

#include <algorithm>
#include <cmath>

namespace economy::logistics {
namespace {

float finite_nonnegative(float value, float fallback = 0.0f) noexcept {
	return std::isfinite(value) && value >= 0.0f ? value : fallback;
}

} // namespace

commodity_profile sanitize(commodity_profile profile) noexcept {
	profile.cargo_weight = std::clamp(
		finite_nonnegative(profile.cargo_weight, 1.0f), 0.0f, 100.0f);
	profile.daily_spoilage = std::clamp(
		finite_nonnegative(profile.daily_spoilage, 0.0005f), 0.0f, 1.0f);
	profile.daily_storage_cost = std::clamp(
		finite_nonnegative(profile.daily_storage_cost, 0.00025f), 0.0f, 1.0f);
	profile.handling_cost_multiplier = std::clamp(
		finite_nonnegative(profile.handling_cost_multiplier, 1.0f), 0.0f, 100.0f);
	profile.target_inventory_days = std::clamp(
		finite_nonnegative(profile.target_inventory_days, 10.0f), 0.0f, 365.0f);
	profile.maximum_daily_release = std::clamp(
		finite_nonnegative(profile.maximum_daily_release, 0.025f), 0.0f, 1.0f);
	return profile;
}

commodity_profile profile_for(sys::state const& state,
		dcon::commodity_id commodity) noexcept {
	commodity_profile profile{};
	if(!commodity || !state.world.commodity_is_valid(commodity))
		return sanitize(profile);
	if(state.world.commodity_get_money_rgo(commodity)) {
		profile.cargo_weight = 0.0f;
		profile.daily_spoilage = 0.0f;
		profile.daily_storage_cost = 0.0f;
		profile.target_inventory_days = 0.0f;
		profile.maximum_daily_release = 1.0f;
	} else if(state.world.commodity_get_is_local(commodity)) {
		profile.cargo_weight = 0.0f;
		profile.daily_spoilage = 0.0020f;
		profile.daily_storage_cost = 0.0010f;
		profile.target_inventory_days = 3.0f;
		profile.maximum_daily_release = 0.10f;
	} else if(state.world.commodity_get_rgo_amount(commodity) > 0.0f) {
		// Raw materials are generally bulkier and harder to warehouse than an
		// equal market unit of finished manufactures.
		profile.cargo_weight = 1.35f;
		profile.daily_spoilage = 0.0015f;
		profile.daily_storage_cost = 0.00075f;
		profile.handling_cost_multiplier = 1.20f;
		profile.target_inventory_days = 6.0f;
		profile.maximum_daily_release = 0.04f;
	}
	return sanitize(profile);
}

float cargo_units(commodity_profile profile, float quantity) noexcept {
	profile = sanitize(profile);
	return finite_nonnegative(quantity) * profile.cargo_weight;
}

float target_inventory(commodity_profile profile,
		float expected_daily_demand) noexcept {
	profile = sanitize(profile);
	return finite_nonnegative(expected_daily_demand)
		* profile.target_inventory_days;
}

float inventory_release(commodity_profile profile, float raw_stock,
		float raw_expected_demand, float raw_expected_supply) noexcept {
	profile = sanitize(profile);
	auto const stock = finite_nonnegative(raw_stock);
	auto const demand = finite_nonnegative(raw_expected_demand);
	auto const supply = finite_nonnegative(raw_expected_supply);
	auto const target = target_inventory(profile, demand);
	// Inventories are working capital, not a second source of daily production.
	// Liquidating a target-stock overshoot in five days made stored output flood
	// the market, depressed the expected sale ratio and caused otherwise viable
	// RGOs to dismiss workers while their warehouses were merely adjusting. A
	// quarterly convergence horizon keeps inventories responsive without turning
	// a temporary surplus into a self-reinforcing production collapse.
	auto const excess_release = std::max(0.0f, stock - target) / 90.0f;
	auto const shortage_release = std::max(0.0f, demand - supply);
	auto const replacement_for_losses = stock * profile.daily_spoilage;
	return std::min(stock * profile.maximum_daily_release,
		std::max({excess_release, shortage_release, replacement_for_losses}));
}

float inventory_after_storage(commodity_profile profile, float stock) noexcept {
	profile = sanitize(profile);
	return finite_nonnegative(stock) * (1.0f - profile.daily_spoilage);
}

} // namespace economy::logistics
