#include "deposits.hpp"
#include "system_state.hpp"
#include "province.hpp"
#include "world/spatial_runtime.hpp"

#include <cmath>
#include <algorithm>
#include <cassert>
#include <cstdlib>

namespace economy::physical::deposits {

namespace {
dcon::site_id make_site(sys::state& state, dcon::province_id province) {
	if(auto canonical = world::spatial_runtime::site_for_province(state, province)) return canonical;
	auto site = state.world.create_site();
	state.world.force_create_site_location(site, province);
	state.world.site_set_position(site, state.world.province_get_mid_point(province));
	return site;
}

struct bootstrap_calibration {
	float reserves = 0.0f;
	float daily_capacity = 0.0f;
	float target = 0.0f;
};

bootstrap_calibration calibrate_legacy_signals(float rgo_size, float rgo_amount) {
	// These legacy values are scenario signals, not geological tonnes.  The
	// conversion is performed only while creating a missing canonical deposit;
	// runtime extraction uses the resulting deposit state exclusively.
	assert(std::isfinite(rgo_size) && rgo_size >= 0.0f
		&& std::isfinite(rgo_amount) && rgo_amount >= 0.0f);
	if(!std::isfinite(rgo_size) || rgo_size < 0.0f
		|| !std::isfinite(rgo_amount) || rgo_amount < 0.0f) std::abort();
	constexpr float reserve_scale = 1000.0f;
	auto reserves = std::max(1.0f, rgo_size * reserve_scale);
	auto daily = std::max(1.0f, rgo_amount);
	return { reserves, daily, std::min(reserves, daily) };
}

bool valid_deposit_values(sys::state const& state, dcon::site_id site, dcon::commodity_id commodity,
	float original, float remaining, float grade, float capacity, float target, uint8_t status) {
	return site && commodity && state.world.site_is_valid(site) && state.world.commodity_is_valid(commodity)
		&& std::isfinite(original) && original >= 0.0f && std::isfinite(remaining) && remaining >= 0.0f
		&& remaining <= original && std::isfinite(grade) && grade >= 0.0f
		&& std::isfinite(capacity) && capacity >= 0.0f && std::isfinite(target) && target >= 0.0f
		&& (status <= 2) && (remaining > 0.0f || status == 2);
}

}

bool initialize_deposit(sys::state& state, dcon::resource_deposit_id deposit, dcon::site_id site,
	dcon::commodity_id commodity, float original, float remaining, float grade, float capacity,
	float target, uint8_t requested_status) {
	if(!deposit || !state.world.resource_deposit_is_valid(deposit) || !valid_deposit_values(state, site, commodity, original, remaining, grade, capacity, target, requested_status)) return false;
	auto status = remaining == 0.0f ? uint8_t(2) : requested_status;
	state.world.resource_deposit_set_commodity(deposit, commodity);
	state.world.force_create_resource_deposit_site(deposit, site);
	state.world.resource_deposit_set_original_recoverable_reserves(deposit, original);
	state.world.resource_deposit_set_remaining_recoverable_reserves(deposit, remaining);
	state.world.resource_deposit_set_grade_or_quality(deposit, grade);
	state.world.resource_deposit_set_daily_extraction_capacity(deposit, capacity);
	state.world.resource_deposit_set_target_daily_extraction(deposit, target);
	state.world.resource_deposit_set_status(deposit, status);
	return true;
}

dcon::resource_deposit_id create_deposit(sys::state& state, dcon::site_id site, dcon::commodity_id commodity,
	float original, float remaining, float grade, float capacity, float target, uint8_t status) {
	if(!valid_deposit_values(state, site, commodity, original, remaining, grade, capacity, target, status)) return {};
	auto deposit = state.world.create_resource_deposit();
	if(!initialize_deposit(state, deposit, site, commodity, original, remaining, grade, capacity, target, status)) {
		state.world.delete_resource_deposit(deposit);
		return {};
	}
	return deposit;
}

dcon::site_id extraction_site_for(sys::state const& state, dcon::province_id province, dcon::commodity_id commodity) {
	auto deposit = deposit_for(state, province, commodity);
	return deposit ? state.world.resource_deposit_get_site_from_resource_deposit_site(deposit) : dcon::site_id{};
}

dcon::resource_deposit_id deposit_for(sys::state const& state, dcon::province_id province, dcon::commodity_id commodity) {
	dcon::resource_deposit_id result{};
	state.world.province_for_each_site_location_as_province(province, [&](dcon::site_location_id location) {
		if(result)
			return;
		auto site = state.world.site_location_get_site(location);
		state.world.site_for_each_resource_deposit_site_as_site(site, [&](dcon::resource_deposit_site_id relation) {
			auto deposit = state.world.resource_deposit_site_get_resource_deposit(relation);
			if(state.world.resource_deposit_get_commodity(deposit) == commodity)
				result = deposit;
		});
	});
	return result;
}

dcon::site_id market_hub_for(sys::state const& state, dcon::market_id market) {
	return state.world.market_get_site_from_market_hub_site(market);
}

void bootstrap(sys::state& state) {
	state.world.for_each_province([&](dcon::province_id province) {
		state.world.for_each_commodity([&](dcon::commodity_id commodity) {
			auto rgo_amount = state.world.commodity_get_rgo_amount(commodity);
			auto rgo_size = state.world.province_get_rgo_size(province, commodity);
			assert(std::isfinite(rgo_amount) && rgo_amount >= 0.0f
				&& std::isfinite(rgo_size) && rgo_size >= 0.0f);
			if(!std::isfinite(rgo_amount) || rgo_amount < 0.0f
				|| !std::isfinite(rgo_size) || rgo_size < 0.0f) std::abort();
			if(rgo_amount <= 0.0f || rgo_size <= 0.0f)
				return;
			auto existing = deposit_for(state, province, commodity);
			if(existing) {
				// Existing deposits are canonical. Never re-read mutable province
				// RGO values to repair or overwrite them.
				return;
			}
			auto site = make_site(state, province);
			auto calibration = calibrate_legacy_signals(
				rgo_size, rgo_amount);
			auto deposit = create_deposit(state, site, commodity, calibration.reserves,
				calibration.reserves, 1.0f, calibration.daily_capacity, calibration.target, 0);
			if(!deposit) {
				assert(false && "scenario resource must become a canonical physical deposit");
				std::abort();
			}
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
