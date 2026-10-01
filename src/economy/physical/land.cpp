#include "land.hpp"
#include "inventory.hpp"
#include "concrete_market.hpp"
#include "system_state.hpp"
#include "actors/ownership.hpp"
#include "actors/organizations/organizations.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/wallets.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace economy::physical::land {
namespace {
bool finite_nonnegative(float value) { return std::isfinite(value) && value >= 0.0f; }

float area_of(sys::state const& state, dcon::land_title_id title) {
	auto area = title ? state.world.land_title_get_area_hectares(title) : 0.0f;
	return std::isfinite(area) && area > 0.0f ? area : 0.0f;
}

dcon::asset_id asset_of(sys::state const& state, dcon::land_title_id title) {
	return title ? state.world.land_title_get_asset_from_land_title_asset(title) : dcon::asset_id{};
}
}

dcon::land_title_id create_title(sys::state& state, dcon::site_id site, float area_hectares) {
	if(!site || !state.world.site_is_valid(site) || !state.world.site_get_province_from_site_location(site)
		|| !std::isfinite(area_hectares) || area_hectares <= 0.0f) return {};
	auto title = state.world.create_land_title();
	state.world.land_title_set_area_hectares(title, area_hectares);
	state.world.force_create_land_title_site(title, site);
	return title;
}

bool set_suitability(sys::state& state, dcon::land_title_id title, dcon::commodity_id commodity, float value) {
	if(!title || !state.world.land_title_is_valid(title) || !commodity || !state.world.commodity_is_valid(commodity)
		|| !finite_nonnegative(value)) return false;
	state.world.land_title_set_suitability(title, commodity, value);
	return true;
}

float suitability(sys::state const& state, dcon::land_title_id title, dcon::commodity_id commodity) {
	if(!title || !state.world.land_title_is_valid(title) || !commodity) return 0.0f;
	auto value = state.world.land_title_get_suitability(title, commodity);
	return finite_nonnegative(value) ? value : 0.0f;
}

dcon::economic_actor_id owner_of(sys::state const& state, dcon::land_title_id title) {
	dcon::economic_actor_id result{};
	auto asset = asset_of(state, title);
	if(!asset) return result;
	state.world.asset_for_each_ownership_stake_asset_as_asset(asset, [&](dcon::ownership_stake_asset_id relation) {
		auto stake = state.world.ownership_stake_asset_get_ownership_stake(relation);
		if(state.world.ownership_stake_get_voting_fraction(stake) > 0.5f)
			result = state.world.ownership_stake_get_economic_actor_from_ownership_stake_owner(stake);
	});
	return result;
}

bool farms_land(sys::state const& state, dcon::factory_type_id type) {
	return type && state.world.factory_type_is_valid(type) && state.world.factory_type_get_farms_land(type);
}

bool farms_land(sys::state const& state, dcon::factory_id factory) {
	return factory && state.world.factory_is_valid(factory) && farms_land(state, state.world.factory_get_building_type(factory));
}

dcon::land_title_id title_for_farm(sys::state const& state, dcon::factory_id factory) {
	return factory ? state.world.factory_get_land_title_from_factory_land_title(factory) : dcon::land_title_id{};
}

dcon::factory_id farm_for_title(sys::state const& state, dcon::land_title_id title) {
	return title ? state.world.land_title_get_factory_from_factory_land_title(title) : dcon::factory_id{};
}

dcon::factory_id create_farm(sys::state& state, dcon::land_title_id title, dcon::factory_type_id type,
	dcon::organization_id operator_organization) {
	if(!title || !state.world.land_title_is_valid(title) || farm_for_title(state, title) || !farms_land(state, type)
		|| !operator_organization || !state.world.organization_is_valid(operator_organization)) return {};
	auto site = state.world.land_title_get_site_from_land_title_site(title);
	auto province = site ? state.world.site_get_province_from_site_location(site) : dcon::province_id{};
	auto output = state.world.factory_type_get_output(type);
	auto workforce = state.world.factory_type_get_base_workforce(type);
	auto reference = area_of(state, title) / reference_hectares_per_worker;
	if(!province || !output || workforce <= 0 || reference <= 0.0f || suitability(state, title, output) <= 0.0f) return {};
	auto farm = state.world.create_factory();
	state.world.factory_set_building_type(farm, type);
	// Planning may stretch labor to twice the reference ratio; diminishing
	// returns, not a hard cap, decide whether that is worth it.
	state.world.factory_set_size(farm, reference * float(workforce));
	state.world.factory_set_productive_capacity(farm, 2.0f * reference);
	state.world.factory_set_productivity_factor(farm, 1.0f);
	state.world.force_create_factory_location(farm, province);
	state.world.force_create_factory_site(farm, site);
	state.world.force_create_factory_land_title(farm, title);
	if(!actors::organizations::bind_factory_operator(state, operator_organization, farm)) {
		state.world.delete_factory(farm);
		return {};
	}
	return farm;
}

dcon::land_lease_id create_lease(sys::state& state, dcon::land_title_id title, dcon::economic_actor_id tenant,
	dcon::commodity_id settlement, float cash_rent_per_hectare_year, float output_share,
	sys::date valid_from, sys::date valid_until) {
	if(!title || !state.world.land_title_is_valid(title) || !tenant || !state.world.economic_actor_is_valid(tenant)
		|| !settlement || !state.world.commodity_is_valid(settlement) || !finite_nonnegative(cash_rent_per_hectare_year)
		|| !finite_nonnegative(output_share) || output_share > 1.0f || !(valid_from < valid_until)) return {};
	auto owner = owner_of(state, title);
	if(!owner || owner == tenant) return {};
	// Overlapping leases on one title are ambiguous; reject them.
	bool overlaps = false;
	state.world.land_title_for_each_land_lease_title_as_land_title(title, [&](dcon::land_lease_title_id relation) {
		auto other = state.world.land_lease_title_get_land_lease(relation);
		if(state.world.land_lease_get_status(other) == uint8_t(lease_status::active)
			&& valid_from < state.world.land_lease_get_valid_until(other)
			&& state.world.land_lease_get_valid_from(other) < valid_until) overlaps = true;
	});
	if(overlaps) return {};
	auto lease = state.world.create_land_lease();
	state.world.land_lease_set_valid_from(lease, valid_from);
	state.world.land_lease_set_valid_until(lease, valid_until);
	state.world.land_lease_set_settlement(lease, settlement);
	state.world.land_lease_set_cash_rent_per_hectare_year(lease, cash_rent_per_hectare_year);
	state.world.land_lease_set_output_share(lease, output_share);
	state.world.land_lease_set_status(lease, uint8_t(lease_status::active));
	state.world.land_lease_set_last_settled(lease, valid_from);
	state.world.land_lease_set_unpaid_rent(lease, 0.0f);
	state.world.force_create_land_lease_title(lease, title);
	state.world.force_create_land_lease_tenant(lease, tenant);
	return lease;
}

dcon::land_lease_id active_lease_for(sys::state const& state, dcon::land_title_id title, sys::date date) {
	dcon::land_lease_id result{};
	if(!title) return result;
	state.world.land_title_for_each_land_lease_title_as_land_title(title, [&](dcon::land_lease_title_id relation) {
		auto lease = state.world.land_lease_title_get_land_lease(relation);
		if(!result && state.world.land_lease_get_status(lease) == uint8_t(lease_status::active)
			&& state.world.land_lease_get_valid_from(lease) <= date && date < state.world.land_lease_get_valid_until(lease))
			result = lease;
	});
	return result;
}

dcon::economic_actor_id tenant_of(sys::state const& state, dcon::land_lease_id lease) {
	return lease ? state.world.land_lease_get_economic_actor_from_land_lease_tenant(lease) : dcon::economic_actor_id{};
}

bool may_operate(sys::state const& state, dcon::land_title_id title, dcon::economic_actor_id actor, sys::date date) {
	if(!title || !actor || area_of(state, title) <= 0.0f) return false;
	if(auto lease = active_lease_for(state, title, date)) return tenant_of(state, lease) == actor;
	return owner_of(state, title) == actor;
}

float reference_labor(sys::state const& state, dcon::factory_id factory) {
	return area_of(state, title_for_farm(state, factory)) / reference_hectares_per_worker;
}

float output_for_labor(sys::state const& state, dcon::factory_id factory, float labor, sys::date date) {
	auto title = title_for_farm(state, factory);
	auto operator_actor = actors::organizations::operator_actor_for_factory(state, factory);
	if(!farms_land(state, factory) || !title || !finite_nonnegative(labor) || labor <= 0.0f
		|| !may_operate(state, title, operator_actor, date)) return 0.0f;
	auto type = state.world.factory_get_building_type(factory);
	auto productivity = state.world.factory_get_productivity_factor(factory);
	auto a = std::max(0.0f, state.world.factory_type_get_output_amount(type))
		* (std::isfinite(productivity) && productivity > 0.0f ? productivity : 1.0f)
		* suitability(state, title, state.world.factory_type_get_output(type));
	auto t = reference_labor(state, factory);
	if(a <= 0.0f || t <= 0.0f) return 0.0f;
	auto result = a * std::pow(labor, labor_elasticity) * std::pow(t, 1.0f - labor_elasticity);
	return std::isfinite(result) ? std::max(0.0f, result) : 0.0f;
}

float output_share_owed(sys::state const& state, dcon::factory_id factory, sys::date date) {
	auto lease = active_lease_for(state, title_for_farm(state, factory), date);
	if(!lease || tenant_of(state, lease) != actors::organizations::operator_actor_for_factory(state, factory)) return 0.0f;
	auto share = state.world.land_lease_get_output_share(lease);
	return finite_nonnegative(share) ? std::min(share, 1.0f) : 0.0f;
}

float deliver_output_share(sys::state& state, dcon::factory_id factory, float share_quantity, sys::date) {
	auto title = title_for_farm(state, factory);
	auto owner = owner_of(state, title);
	auto site = state.world.factory_get_site_from_factory_site(factory);
	auto type = state.world.factory_get_building_type(factory);
	auto commodity = type ? state.world.factory_type_get_output(type) : dcon::commodity_id{};
	if(!owner || !site || !commodity || !finite_nonnegative(share_quantity) || share_quantity <= 0.0f) return 0.0f;
	return inventory::add(state, site, commodity, share_quantity, owner);
}

void settle_rents(sys::state& state) {
	std::vector<dcon::land_lease_id> leases;
	state.world.for_each_land_lease([&](dcon::land_lease_id lease) { leases.push_back(lease); });
	std::sort(leases.begin(), leases.end(), [](auto left, auto right) { return left.index() < right.index(); });
	for(auto lease : leases) {
		if(state.world.land_lease_get_status(lease) != uint8_t(lease_status::active)) continue;
		auto title = state.world.land_lease_get_land_title_from_land_lease_title(lease);
		auto last = state.world.land_lease_get_last_settled(lease);
		auto until = state.world.land_lease_get_valid_until(lease);
		auto end = state.current_date < until ? state.current_date : until;
		auto days = end.to_raw_value() - last.to_raw_value();
		bool expired = !(state.current_date < until);
		if(days < rent_period_days && !expired) continue;
		auto rate = state.world.land_lease_get_cash_rent_per_hectare_year(lease);
		auto due = state.world.land_lease_get_unpaid_rent(lease)
			+ (days > 0 ? rate * area_of(state, title) * float(days) / 365.0f : 0.0f);
		auto settlement = state.world.land_lease_get_settlement(lease);
		auto tenant = tenant_of(state, lease);
		auto owner = owner_of(state, title);
		float paid = 0.0f;
		if(due > 0.0f && tenant && owner) {
			auto source = economy::wallets::account_for(state, tenant, settlement);
			auto destination = economy::wallets::open_for(state, owner, settlement);
			auto amount = std::min(due, economy::wallets::spendable(state, source));
			if(amount > 0.0f && economy::wallets::pay(state, source, destination, amount, relations::transaction_kind::rent))
				paid = amount;
		}
		state.world.land_lease_set_unpaid_rent(lease, std::max(0.0f, due - paid));
		state.world.land_lease_set_last_settled(lease, end);
		if(expired) state.world.land_lease_set_status(lease, uint8_t(lease_status::ended));
	}
}

void calibrate_farm_recipes(sys::state& state, dcon::pop_type_id reference_pop_type) {
	if(!reference_pop_type || !state.world.pop_type_is_valid(reference_pop_type)) return;
	double family_value = 0.0;
	state.world.for_each_commodity([&](dcon::commodity_id commodity) {
		auto need = state.world.pop_type_get_life_needs(reference_pop_type, commodity);
		auto cost = state.world.commodity_get_cost(commodity);
		if(std::isfinite(need) && need > 0.0f && std::isfinite(cost) && cost > 0.0f)
			family_value += double(need) * double(cost);
	});
	family_value *= persons_per_worker;
	double total_value = 0.0;
	int32_t count = 0;
	state.world.for_each_factory_type([&](dcon::factory_type_id type) {
		if(!farms_land(state, type)) return;
		total_value += std::max(0.0f, state.world.factory_type_get_output_amount(type));
		++count;
	});
	if(family_value <= 0.0 || count == 0 || total_value <= 0.0) return;
	auto mean_value = total_value / count;
	state.world.for_each_factory_type([&](dcon::factory_type_id type) {
		if(!farms_land(state, type)) return;
		auto cost = state.world.commodity_get_cost(state.world.factory_type_get_output(type));
		auto content_value = std::max(0.0f, state.world.factory_type_get_output_amount(type));
		if(!(cost > 0.0f)) return;
		auto per_worker = reference_food_security * family_value / double(cost) * (content_value / mean_value);
		state.world.factory_type_set_output_amount(type, float(per_worker));
	});
}

} // namespace economy::physical::land
