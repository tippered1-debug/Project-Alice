#include "ownership.hpp"
#include "actors/organizations/organizations.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/physical/concrete_market.hpp"
#include "system_state.hpp"

#include <cmath>
#include <unordered_set>
#include <vector>

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

dcon::asset_id asset_for_deposit(sys::state const& state, dcon::resource_deposit_id deposit) {
	return deposit ? state.world.resource_deposit_get_asset_from_resource_deposit_asset(deposit) : dcon::asset_id{};
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

float contribute_equity_to_factory(sys::state& state, dcon::factory_id factory,
	dcon::economic_actor_id firm, dcon::monetary_account_id firm_account, float requested_amount) {
	if(!factory || !state.world.factory_is_valid(factory) || !firm || !firm_account
		|| economy::accounts::owner_of(state, firm_account) != firm
		|| !std::isfinite(requested_amount) || requested_amount <= 0.0f) return 0.0f;
	auto settlement = economy::accounts::settlement_of(state, firm_account);
	auto asset = asset_for_factory(state, factory);
	if(!settlement || !asset) return 0.0f;
	float contributed = 0.0f;
	std::unordered_set<uint32_t> funded_owners;
	auto call_owners = [&](dcon::asset_id ownership_asset) {
		if(!ownership_asset) return;
		state.world.asset_for_each_ownership_stake_asset_as_asset(ownership_asset, [&](dcon::ownership_stake_asset_id relation) {
			if(contributed >= requested_amount) return;
			auto stake = state.world.ownership_stake_asset_get_ownership_stake(relation);
			auto owner = state.world.ownership_stake_get_economic_actor_from_ownership_stake_owner(stake);
			auto share = state.world.ownership_stake_get_economic_fraction(stake);
			if(!owner || owner == firm || share <= 0.0f || !funded_owners.insert(owner.index()).second) return;
			auto account = economy::accounts::find_account(state, owner, settlement);
			if(!account) return;
			auto cash = std::max(0.0f, economy::accounts::balance(state, account)
				- economy::physical::concrete_market::reserved_bid_amount(state, account));
			// Owners put in real cash, pro-rata to their equity and subject to a
			// liquidity cap. A company does not inject into itself as its own owner.
			auto contribution = std::min({requested_amount - contributed,
				requested_amount * std::clamp(share, 0.0f, 1.0f), cash * 0.20f});
			if(contribution > 1.0e-5f && economy::accounts::transfer(state, account, firm_account,
				contribution, economy::relations::transaction_kind::equity_contribution, state.current_date))
				contributed += contribution;
		});
	};
	call_owners(asset);
	// When the operator owns the plant asset, look through to actual holders of
	// the operator company's equity instead of treating the company as its own
	// capital source.
	state.world.asset_for_each_ownership_stake_asset_as_asset(asset, [&](dcon::ownership_stake_asset_id relation) {
		auto stake = state.world.ownership_stake_asset_get_ownership_stake(relation);
		auto owner = state.world.ownership_stake_get_economic_actor_from_ownership_stake_owner(stake);
		if(owner != firm) return;
		auto organization = organizations::organization_for_actor(state, owner);
		call_owners(organizations::equity_asset_for_organization(state, organization));
	});
	return contributed;
}

bool issue_equity(sys::state& state, dcon::asset_id asset, dcon::economic_actor_id investor,
	float investment, float pre_money_value) {
	if(!asset || !investor || !std::isfinite(investment) || investment <= 0.0f
		|| !std::isfinite(pre_money_value) || pre_money_value < 0.0f) return false;
	std::vector<dcon::ownership_stake_id> existing;
	state.world.asset_for_each_ownership_stake_asset_as_asset(asset, [&](dcon::ownership_stake_asset_id relation) {
		existing.push_back(state.world.ownership_stake_asset_get_ownership_stake(relation));
	});
	if(existing.empty()) return bool(create_stake(state, investor, asset, 1.0f, 1.0f, 1.0f));
	auto total = std::max(1.0e-5f, pre_money_value + investment);
	auto dilution = std::clamp(pre_money_value / total, 0.0f, 1.0f);
	auto new_fraction = std::clamp(investment / total, 0.0f, 1.0f);
	for(auto stake : existing) {
		if(!stake || !state.world.ownership_stake_is_valid(stake)) continue;
		state.world.ownership_stake_set_ownership_fraction(stake,
			state.world.ownership_stake_get_ownership_fraction(stake) * dilution);
		state.world.ownership_stake_set_voting_fraction(stake,
			state.world.ownership_stake_get_voting_fraction(stake) * dilution);
		state.world.ownership_stake_set_economic_fraction(stake,
			state.world.ownership_stake_get_economic_fraction(stake) * dilution);
	}
	return bool(create_stake(state, investor, asset, new_fraction, new_fraction, new_fraction));
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
		auto organization = organizations::operator_organization_for_factory(state, factory);
		bool created_legacy_placeholder = false;
		if(!organization) {
			organization = organizations::create_company(state);
			if(organization) {
				created_legacy_placeholder = true;
				state.world.economic_actor_set_is_legacy_placeholder(organizations::actor_for_organization(state, organization), 1);
				organizations::bind_factory_operator(state, organization, factory);
			}
		}
		if(!asset_for_factory(state, factory) && organization) {
			auto asset = state.world.create_asset();
			state.world.force_create_factory_asset(factory, asset);
			if(created_legacy_placeholder)
				create_stake(state, organizations::actor_for_organization(state, organization), asset, 1.0f, 1.0f, 1.0f);
		}
	});
	state.world.for_each_resource_deposit([&](dcon::resource_deposit_id deposit) {
		auto organization = organizations::operator_organization_for_deposit(state, deposit);
		if(!organization) {
			organization = organizations::create_company(state);
			if(organization) {
				state.world.economic_actor_set_is_legacy_placeholder(organizations::actor_for_organization(state, organization), 1);
				organizations::bind_deposit_operator(state, organization, deposit);
			}
		} else {
			ensure_placeholder_organization(state, organization);
		}
	});
}

} // namespace actors::ownership
