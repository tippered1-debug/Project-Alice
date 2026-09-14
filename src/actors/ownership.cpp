#include "ownership.hpp"
#include "system_state.hpp"

#include <cmath>

namespace actors::ownership {

bool valid_fraction(float value) noexcept { return std::isfinite(value) && value >= 0.0f && value <= 1.0f; }
constexpr float fraction_epsilon = 1.0e-5f;

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

dcon::economic_actor_id ensure_placeholder_organization(sys::state& state, dcon::organization_id organization) {
	if(!organization) return {};
	auto actor = actor_for_organization(state, organization);
	if(!actor) {
		actor = state.world.create_economic_actor();
		state.world.economic_actor_set_kind(actor, uint8_t(actor_kind::placeholder));
		state.world.economic_actor_set_is_legacy_placeholder(actor, 1);
		state.world.force_create_organization_actor(organization, actor);
	}
	if(!equity_asset_for_organization(state, organization)) {
		auto equity = state.world.create_asset();
		state.world.force_create_organization_equity_asset(organization, equity);
	}
	return actor;
}

bool set_stake_fractions(sys::state& state, dcon::ownership_stake_id stake, float ownership, float voting, float economic) {
	if(!stake || !valid_fraction(ownership) || !valid_fraction(voting) || !valid_fraction(economic)) return false;
	auto asset = state.world.ownership_stake_get_asset_from_ownership_stake_asset(stake);
	if(!asset) return false;
	float sums[3] = { ownership, voting, economic };
	state.world.asset_for_each_ownership_stake_asset_as_asset(asset, [&](dcon::ownership_stake_asset_id relation) {
		auto other = state.world.ownership_stake_asset_get_ownership_stake(relation);
		if(other != stake) {
			sums[0] += state.world.ownership_stake_get_ownership_fraction(other);
			sums[1] += state.world.ownership_stake_get_voting_fraction(other);
			sums[2] += state.world.ownership_stake_get_economic_fraction(other);
		}
	});
	if(sums[0] > 1.0f + fraction_epsilon || sums[1] > 1.0f + fraction_epsilon || sums[2] > 1.0f + fraction_epsilon) return false;
	state.world.ownership_stake_set_ownership_fraction(stake, ownership);
	state.world.ownership_stake_set_voting_fraction(stake, voting);
	state.world.ownership_stake_set_economic_fraction(stake, economic);
	return true;
}

dcon::ownership_stake_id create_stake(sys::state& state, dcon::economic_actor_id owner, dcon::asset_id asset, float ownership, float voting, float economic) {
	if(!owner || !asset || !valid_fraction(ownership) || !valid_fraction(voting) || !valid_fraction(economic)) return {};
	auto stake = state.world.create_ownership_stake();
	state.world.force_create_ownership_stake_owner(stake, owner);
	state.world.force_create_ownership_stake_asset(stake, asset);
	if(!set_stake_fractions(state, stake, ownership, voting, economic)) {
		state.world.delete_ownership_stake(stake);
		return {};
	}
	return stake;
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
		create_stake(state, actor, asset, 1.0f, 1.0f, 1.0f);
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
