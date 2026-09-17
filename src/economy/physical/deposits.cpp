#include "deposits.hpp"
#include "system_state.hpp"
#include "province.hpp"
#include "actors/organizations/organizations.hpp"
#include "actors/ownership.hpp"

#include <cmath>
#include <algorithm>

namespace economy::physical::deposits {

namespace {
dcon::site_id make_site(sys::state& state, dcon::province_id province) {
	auto site = state.world.create_site();
	state.world.force_create_site_location(site, province);
	state.world.site_set_position(site, state.world.province_get_mid_point(province));
	return site;
}

void initialize_legacy_deposit(sys::state& state, dcon::resource_deposit_id deposit, dcon::province_id province) {
	if(!deposit || state.world.resource_deposit_get_legacy_compatibility_deposit(deposit)) return;
	auto commodity = state.world.resource_deposit_get_commodity(deposit);
	if(!commodity || state.world.commodity_get_rgo_amount(commodity) <= 0.0f
		|| state.world.province_get_rgo_size(province, commodity) <= 0.0f
		|| state.world.resource_deposit_get_remaining_recoverable_reserves(deposit) > 0.0f)
		return;
	// Repair only the unmistakable pre-foundation/legacy shape: no initialized
	// reserves and a matching Alice RGO source. Hand-authored canonical deposits
	// with explicit reserves are never rewritten.
	auto snapshot = std::max(1.0f, state.world.province_get_rgo_size(province, commodity));
	state.world.resource_deposit_set_original_recoverable_reserves(deposit, snapshot);
	state.world.resource_deposit_set_remaining_recoverable_reserves(deposit, snapshot);
	state.world.resource_deposit_set_grade_or_quality(deposit, 1.0f);
	auto daily = std::max(1.0f, state.world.commodity_get_rgo_amount(commodity));
	state.world.resource_deposit_set_daily_extraction_capacity(deposit, daily);
	state.world.resource_deposit_set_target_daily_extraction(deposit, daily);
	state.world.resource_deposit_set_status(deposit, 0);
	state.world.resource_deposit_set_legacy_compatibility_deposit(deposit, 1);
}

void ensure_legacy_asset_and_operator(sys::state& state, dcon::resource_deposit_id deposit, bool created_now) {
	auto organization = actors::organizations::operator_organization_for_deposit(state, deposit);
	if(!organization) {
		organization = actors::organizations::create_company(state);
		if(organization) {
			actors::organizations::bind_deposit_operator(state, organization, deposit);
			if(!actors::ownership::asset_for_deposit(state, deposit)) {
				auto asset = state.world.create_asset();
				state.world.force_create_resource_deposit_asset(deposit, asset);
				actors::ownership::create_stake(state,
					actors::organizations::actor_for_organization(state, organization), asset,
					1.0f, 1.0f, 1.0f);
			}
		}
	} else if(!actors::ownership::asset_for_deposit(state, deposit)) {
		auto asset = state.world.create_asset();
		state.world.force_create_resource_deposit_asset(deposit, asset);
		// A pre-existing operator is not silently made owner. The compatibility
		// stake is only created with the legacy placeholder bootstrap above.
		(void)created_now;
	}
}
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
			if(state.world.commodity_get_rgo_amount(commodity) <= 0.0f
				|| state.world.province_get_rgo_size(province, commodity) <= 0.0f)
				return;
			auto existing = deposit_for(state, province, commodity);
			if(existing) {
				initialize_legacy_deposit(state, existing, province);
				if(state.world.resource_deposit_get_legacy_compatibility_deposit(existing))
					ensure_legacy_asset_and_operator(state, existing, false);
				return;
			}
			auto site = make_site(state, province);
			auto deposit = state.world.create_resource_deposit();
		state.world.resource_deposit_set_commodity(deposit, commodity);
		state.world.force_create_resource_deposit_site(deposit, site);
		// This is an explicitly marked compatibility snapshot. RGO size is not
		// interpreted as geological tonnes by the canonical extraction path.
		auto legacy_capacity = std::max(1.0f, state.world.province_get_rgo_size(province, commodity));
		state.world.resource_deposit_set_original_recoverable_reserves(deposit, legacy_capacity);
		state.world.resource_deposit_set_remaining_recoverable_reserves(deposit, legacy_capacity);
		state.world.resource_deposit_set_grade_or_quality(deposit, 1.0f);
		auto daily = std::max(1.0f, state.world.commodity_get_rgo_amount(commodity));
		state.world.resource_deposit_set_daily_extraction_capacity(deposit, daily);
		state.world.resource_deposit_set_target_daily_extraction(deposit, daily);
		state.world.resource_deposit_set_status(deposit, 0);
			state.world.resource_deposit_set_legacy_compatibility_deposit(deposit, 1);
		ensure_legacy_asset_and_operator(state, deposit, true);
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
