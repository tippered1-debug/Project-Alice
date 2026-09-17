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
	auto daily = std::max(1.0f, state.world.commodity_get_rgo_amount(commodity));
	initialize_deposit(state, deposit, state.world.resource_deposit_get_site_from_resource_deposit_site(deposit),
		commodity, snapshot, snapshot, 1.0f, daily, daily, 0, true);
}

bool asset_has_owner(sys::state const& state, dcon::asset_id asset) {
	bool owned = false;
	if(!asset) return false;
	state.world.asset_for_each_ownership_stake_asset_as_asset(asset, [&](dcon::ownership_stake_asset_id relation) {
		auto stake = state.world.ownership_stake_asset_get_ownership_stake(relation);
		if(state.world.ownership_stake_get_economic_actor_from_ownership_stake_owner(stake)) owned = true;
	});
	return owned;
}

dcon::economic_actor_id create_legacy_placeholder_owner(sys::state& state) {
	auto organization = actors::organizations::create_company(state);
	if(!organization) return {};
	auto actor = actors::organizations::actor_for_organization(state, organization);
	if(actor) state.world.economic_actor_set_is_legacy_placeholder(actor, 1);
	return actor;
}

void ensure_legacy_asset_and_operator(sys::state& state, dcon::resource_deposit_id deposit) {
	auto organization = actors::organizations::operator_organization_for_deposit(state, deposit);
	if(!organization) {
		organization = actors::organizations::create_company(state);
		if(organization) {
			actors::organizations::bind_deposit_operator(state, organization, deposit);
			state.world.economic_actor_set_is_legacy_placeholder(
				actors::organizations::actor_for_organization(state, organization), 1);
		}
	}
	auto asset = actors::ownership::asset_for_deposit(state, deposit);
	if(!asset) {
		auto asset = state.world.create_asset();
		state.world.force_create_resource_deposit_asset(deposit, asset);
	}
	asset = actors::ownership::asset_for_deposit(state, deposit);
	if(asset && !asset_has_owner(state, asset)) {
		// A real pre-existing operator is never silently made owner. Unknown
		// legacy ownership is represented by a separate deterministic placeholder.
		auto operator_actor = actors::organizations::operator_actor_for_deposit(state, deposit);
		auto owner = operator_actor && state.world.economic_actor_get_is_legacy_placeholder(operator_actor)
			? operator_actor : create_legacy_placeholder_owner(state);
		if(owner) actors::ownership::create_stake(state, owner, asset, 1.0f, 1.0f, 1.0f);
	}
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
	float target, uint8_t requested_status, bool legacy_compatibility) {
	if(!deposit || !state.world.resource_deposit_is_valid(deposit)
		|| !valid_deposit_values(state, site, commodity, original, remaining, grade, capacity, target, requested_status)) return false;
	auto status = remaining == 0.0f ? uint8_t(2) : requested_status;
	state.world.resource_deposit_set_commodity(deposit, commodity);
	state.world.force_create_resource_deposit_site(deposit, site);
	state.world.resource_deposit_set_original_recoverable_reserves(deposit, original);
	state.world.resource_deposit_set_remaining_recoverable_reserves(deposit, remaining);
	state.world.resource_deposit_set_grade_or_quality(deposit, grade);
	state.world.resource_deposit_set_daily_extraction_capacity(deposit, capacity);
	state.world.resource_deposit_set_target_daily_extraction(deposit, target);
	state.world.resource_deposit_set_status(deposit, status);
	state.world.resource_deposit_set_legacy_compatibility_deposit(deposit, legacy_compatibility ? 1 : 0);
	return true;
}

dcon::resource_deposit_id create_deposit(sys::state& state, dcon::site_id site, dcon::commodity_id commodity,
	float original, float remaining, float grade, float capacity, float target, uint8_t status, bool legacy_compatibility) {
	if(!valid_deposit_values(state, site, commodity, original, remaining, grade, capacity, target, status)) return {};
	auto deposit = state.world.create_resource_deposit();
	if(!initialize_deposit(state, deposit, site, commodity, original, remaining, grade, capacity, target, status, legacy_compatibility)) {
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
			if(state.world.commodity_get_rgo_amount(commodity) <= 0.0f
				|| state.world.province_get_rgo_size(province, commodity) <= 0.0f)
				return;
			auto existing = deposit_for(state, province, commodity);
			if(existing) {
				initialize_legacy_deposit(state, existing, province);
				if(state.world.resource_deposit_get_legacy_compatibility_deposit(existing))
					ensure_legacy_asset_and_operator(state, existing);
				return;
			}
			auto site = make_site(state, province);
		// This is an explicitly marked compatibility snapshot. RGO size is not
		// interpreted as geological tonnes by the canonical extraction path.
			auto legacy_capacity = std::max(1.0f, state.world.province_get_rgo_size(province, commodity));
			auto daily = std::max(1.0f, state.world.commodity_get_rgo_amount(commodity));
			auto deposit = create_deposit(state, site, commodity, legacy_capacity, legacy_capacity, 1.0f, daily, daily, 0, true);
			if(!deposit) return;
		ensure_legacy_asset_and_operator(state, deposit);
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
