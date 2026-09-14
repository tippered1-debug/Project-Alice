#include "ownership.hpp"
#include "system_state.hpp"

#include <cmath>

namespace actors::ownership {

bool valid_fraction(float value) noexcept { return std::isfinite(value) && value >= 0.0f && value <= 1.0f; }

dcon::economic_actor_id actor_for_organization(sys::state const& state, dcon::organization_id organization) {
	return organization ? state.world.organization_get_economic_actor_from_organization_actor(organization) : dcon::economic_actor_id{};
}

dcon::asset_id equity_asset_for_organization(sys::state const& state, dcon::organization_id organization) {
	return organization ? state.world.organization_get_asset_from_organization_equity_asset(organization) : dcon::asset_id{};
}

dcon::asset_id asset_for_factory(sys::state const& state, dcon::factory_id factory) {
	return factory ? state.world.factory_get_asset_from_factory_asset(factory) : dcon::asset_id{};
}

dcon::economic_actor_id operator_for_deposit(sys::state const& state, dcon::resource_deposit_id deposit) {
	if(!deposit) return {};
	return actor_for_organization(state, state.world.resource_deposit_get_organization_from_resource_deposit_operator(deposit));
}

dcon::economic_actor_id operator_for_site(sys::state const& state, dcon::site_id site) {
	dcon::economic_actor_id result{};
	state.world.site_for_each_resource_deposit_site_as_site(site, [&](dcon::resource_deposit_site_id relation) {
		result = operator_for_deposit(state, state.world.resource_deposit_site_get_resource_deposit(relation));
	});
	return result;
}

dcon::economic_actor_id ensure_placeholder_organization(sys::state& state, dcon::organization_id organization) {
	if(!organization) return {};
	if(auto actor = actor_for_organization(state, organization)) return actor;
	auto actor = state.world.create_economic_actor();
	state.world.economic_actor_set_kind(actor, uint8_t(actor_kind::placeholder));
	state.world.economic_actor_set_is_legacy_placeholder(actor, 1);
	state.world.force_create_organization_actor(organization, actor);
	if(!equity_asset_for_organization(state, organization)) {
		auto equity = state.world.create_asset();
		state.world.force_create_organization_equity_asset(organization, equity);
	}
	return actor;
}

void bootstrap(sys::state& state) {
	state.world.for_each_factory([&](dcon::factory_id factory) {
		if(asset_for_factory(state, factory)) return;
		auto organization = dcon::organization_id{};
		// Legacy factories have no concrete organization yet; create a placeholder company.
		auto actor = state.world.create_economic_actor();
		state.world.economic_actor_set_kind(actor, uint8_t(actor_kind::company));
		state.world.economic_actor_set_is_legacy_placeholder(actor, 1);
		organization = state.world.create_organization();
		state.world.organization_set_kind(organization, uint8_t(actor_kind::company));
		state.world.force_create_organization_actor(organization, actor);
		state.world.force_create_organization_factory_operator(organization, factory);
		auto equity = state.world.create_asset();
		state.world.force_create_organization_equity_asset(organization, equity);
		auto asset = state.world.create_asset();
		state.world.force_create_factory_asset(factory, asset);
		auto stake = state.world.create_ownership_stake();
		state.world.ownership_stake_set_ownership_fraction(stake, 1.0f);
		state.world.ownership_stake_set_voting_fraction(stake, 1.0f);
		state.world.ownership_stake_set_economic_fraction(stake, 1.0f);
		state.world.force_create_ownership_stake_owner(stake, actor);
		state.world.force_create_ownership_stake_asset(stake, asset);
	});
	state.world.for_each_resource_deposit([&](dcon::resource_deposit_id deposit) {
		if(state.world.resource_deposit_get_organization_from_resource_deposit_operator(deposit)) return;
		auto organization = state.world.create_organization();
		state.world.organization_set_kind(organization, uint8_t(actor_kind::company));
		auto actor = ensure_placeholder_organization(state, organization);
		state.world.force_create_resource_deposit_operator(deposit, organization);
		(void)actor;
	});
}

} // namespace actors::ownership
