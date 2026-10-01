#include "ownership.hpp"
#include "economy/wallets.hpp"
#include "actors/organizations/organizations.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/physical/concrete_market.hpp"
#include "system_state.hpp"

#include <cmath>
#include <cassert>
#include <cstdlib>
#include <string_view>
#include <unordered_set>
#include <vector>
#include <string>

namespace actors::ownership {

bool valid_fraction(float value) noexcept { return std::isfinite(value) && value >= 0.0f && value <= 1.0f; }
constexpr float fraction_epsilon = 1.0e-5f;

uint64_t runtime_id(std::string_view kind, uint32_t index, uint64_t date) {
	uint64_t hash = 14695981039346656037ULL;
	auto add = [&](unsigned char value) {
		hash ^= uint64_t(value);
		hash *= 1099511628211ULL;
	};
	for(auto value : std::string_view("runtime:")) add(uint8_t(value));
	for(auto value : kind) add(uint8_t(value));
	add(':');
	for(auto value : std::to_string(date)) add(uint8_t(value));
	add(':');
	for(auto value : std::to_string(index)) add(uint8_t(value));
	return hash == 0 ? 1 : hash;
}

namespace {
bool asset_has_complete_ownership(sys::state const& state, dcon::asset_id asset) {
	if(!asset || !state.world.asset_is_valid(asset)) return false;
	float ownership = 0.0f;
	float voting = 0.0f;
	float economic = 0.0f;
	uint32_t stakes = 0;
	bool valid = true;
	std::unordered_set<uint32_t> owners;
	state.world.asset_for_each_ownership_stake_asset_as_asset(asset, [&](dcon::ownership_stake_asset_id relation) {
		auto stake = state.world.ownership_stake_asset_get_ownership_stake(relation);
		auto owner = state.world.ownership_stake_get_economic_actor_from_ownership_stake_owner(stake);
		if(!stake || !state.world.ownership_stake_is_valid(stake)
			|| state.world.ownership_stake_get_canonical_id(stake) == 0 || !owner
			|| !state.world.economic_actor_is_valid(owner)
			|| state.world.economic_actor_get_canonical_id(owner) == 0
			|| !owners.insert(owner.index()).second) {
			valid = false;
			return;
		}
		auto const ownership_share = state.world.ownership_stake_get_ownership_fraction(stake);
		auto const voting_share = state.world.ownership_stake_get_voting_fraction(stake);
		auto const economic_share = state.world.ownership_stake_get_economic_fraction(stake);
		if(!valid_fraction(ownership_share) || !valid_fraction(voting_share)
			|| !valid_fraction(economic_share)) {
			valid = false;
			return;
		}
		ownership += ownership_share;
		voting += voting_share;
		economic += economic_share;
		++stakes;
	});
	return valid && stakes != 0 && std::abs(ownership - 1.0f) <= fraction_epsilon
		&& std::abs(voting - 1.0f) <= fraction_epsilon
		&& std::abs(economic - 1.0f) <= fraction_epsilon;
}

std::string factory_label(sys::state const& state, dcon::factory_id factory) {
	auto site = state.world.factory_get_site_from_factory_site(factory);
	return "factory dcon:" + std::to_string(factory.index()) + " at site dcon:"
		+ std::to_string(site.index());
}

bool has_organization_graph(sys::state const& state, dcon::organization_id organization) {
	if(!organization || !state.world.organization_is_valid(organization)
		|| state.world.organization_get_canonical_id(organization) == 0) return false;
	auto actor = organizations::actor_for_organization(state, organization);
	auto equity_asset = organizations::equity_asset_for_organization(state, organization);
	auto kind = actor_kind(state.world.organization_get_kind(organization));
	bool const identity = organizations::is_economic_kind(kind)
		&& actor && state.world.economic_actor_is_valid(actor)
		&& state.world.economic_actor_get_canonical_id(actor) != 0
		&& actor_kind(state.world.economic_actor_get_kind(actor)) == kind;
	// A household cohort's members are its residual claimants; it has no equity.
	if(kind == actor_kind::household) return identity && !equity_asset;
	return identity
		&& equity_asset && state.world.asset_is_valid(equity_asset)
		&& state.world.asset_get_canonical_id(equity_asset) != 0
		&& asset_has_complete_ownership(state, equity_asset);
}
}

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

void assign_runtime_canonical_id(sys::state& state, dcon::economic_actor_id actor) {
	if(!actor) return;
	auto id = state.world.economic_actor_get_canonical_id(actor);
	if(id == 0) state.world.economic_actor_set_canonical_id(actor,
		runtime_id("actor", actor.index(), state.current_date.to_raw_value()));
}

void assign_runtime_canonical_id(sys::state& state, dcon::organization_id organization) {
	if(!organization) return;
	auto id = state.world.organization_get_canonical_id(organization);
	if(id == 0) state.world.organization_set_canonical_id(organization,
		runtime_id("organization", organization.index(), state.current_date.to_raw_value()));
	assign_runtime_canonical_id(state, organizations::actor_for_organization(state, organization));
	assign_runtime_canonical_id(state, organizations::equity_asset_for_organization(state, organization));
}

void assign_runtime_canonical_id(sys::state& state, dcon::site_id site) {
	if(!site) return;
	if(state.world.site_get_canonical_id(site) == 0)
		state.world.site_set_canonical_id(site, runtime_id("site", site.index(), state.current_date.to_raw_value()));
}

void assign_runtime_canonical_id(sys::state& state, dcon::factory_id factory) {
	if(!factory) return;
	if(state.world.factory_get_canonical_id(factory) == 0)
		state.world.factory_set_canonical_id(factory, runtime_id("factory", factory.index(), state.current_date.to_raw_value()));
}

void assign_runtime_canonical_id(sys::state& state, dcon::resource_deposit_id deposit) {
	if(!deposit) return;
	if(state.world.resource_deposit_get_canonical_id(deposit) == 0)
		state.world.resource_deposit_set_canonical_id(deposit, runtime_id("deposit", deposit.index(), state.current_date.to_raw_value()));
}

void assign_runtime_canonical_id(sys::state& state, dcon::asset_id asset) {
	if(!asset) return;
	if(state.world.asset_get_canonical_id(asset) == 0)
		state.world.asset_set_canonical_id(asset, runtime_id("asset", asset.index(), state.current_date.to_raw_value()));
}

void assign_runtime_canonical_id(sys::state& state, dcon::ownership_stake_id stake) {
	if(!stake) return;
	if(state.world.ownership_stake_get_canonical_id(stake) == 0)
		state.world.ownership_stake_set_canonical_id(stake, runtime_id("stake", stake.index(), state.current_date.to_raw_value()));
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
	if(!factory || !state.world.factory_is_valid(factory) || !firm || !firm_account || economy::accounts::owner_of(state, firm_account) != firm || !std::isfinite(requested_amount) || requested_amount <= 0.0f) return 0.0f;
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
			// The owner's own ledger: a person's exact account, a treasury, or an
			// organization's operating account.
			auto account = economy::wallets::account_for(state, owner, settlement);
			if(!account) return;
			auto cash = economy::wallets::spendable(state, account);
			// Owners put in real cash, pro-rata to their equity and subject to a
			// liquidity cap. A company does not inject into itself as its own owner.
			auto contribution = std::min({requested_amount - contributed,
				requested_amount * std::clamp(share, 0.0f, 1.0f), cash * 0.20f});
			if(contribution > 1.0e-5f && economy::wallets::pay(state, account,
				economy::wallets::account_ref::from_dcon(firm_account), contribution,
				economy::relations::transaction_kind::equity_contribution))
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
	if(contributed > 0.0f) {
		auto organization = organizations::organization_for_actor(state, firm);
		if(organization) {
			state.world.organization_set_paid_in_equity(organization,
				state.world.organization_get_paid_in_equity(organization) + contributed);
			auto equity_asset = organizations::equity_asset_for_organization(state, organization);
			if(equity_asset)
				state.world.asset_set_appraised_value(equity_asset,
					std::max(0.0f, state.world.organization_get_paid_in_equity(organization)
						+ state.world.organization_get_retained_earnings(organization)));
		}
	}
	return contributed;
}

bool issue_equity(sys::state& state, dcon::asset_id asset, dcon::economic_actor_id investor,
	float investment, float pre_money_value) {
	if(!asset || !investor || !std::isfinite(investment) || investment <= 0.0f || !std::isfinite(pre_money_value) || pre_money_value < 0.0f) return false;
	assign_runtime_canonical_id(state, investor);
	assign_runtime_canonical_id(state, asset);
	auto organization = state.world.asset_get_organization_from_organization_equity_asset(asset);
	if(organization) assign_runtime_canonical_id(state, organization);
	std::vector<dcon::ownership_stake_id> existing;
	state.world.asset_for_each_ownership_stake_asset_as_asset(asset, [&](dcon::ownership_stake_asset_id relation) {
		existing.push_back(state.world.ownership_stake_asset_get_ownership_stake(relation));
	});
	if(existing.empty()) {
		auto created = create_stake(state, investor, asset, 1.0f, 1.0f, 1.0f);
		assign_runtime_canonical_id(state, created);
		if(!created) return false;
		if(organization) {
			state.world.organization_set_paid_in_equity(organization,
				state.world.organization_get_paid_in_equity(organization) + investment);
			state.world.asset_set_appraised_value(asset,
				std::max(0.0f, state.world.organization_get_paid_in_equity(organization)
					+ state.world.organization_get_retained_earnings(organization)));
		}
		return true;
	}
	auto total = std::max(1.0e-5f, pre_money_value + investment);
	auto dilution = std::clamp(pre_money_value / total, 0.0f, 1.0f);
	auto new_fraction = std::clamp(investment / total, 0.0f, 1.0f);
	dcon::ownership_stake_id investor_stake{};
	for(auto stake : existing) {
		if(!stake || !state.world.ownership_stake_is_valid(stake)) continue;
		state.world.ownership_stake_set_ownership_fraction(stake,
			state.world.ownership_stake_get_ownership_fraction(stake) * dilution);
		state.world.ownership_stake_set_voting_fraction(stake,
			state.world.ownership_stake_get_voting_fraction(stake) * dilution);
		state.world.ownership_stake_set_economic_fraction(stake,
			state.world.ownership_stake_get_economic_fraction(stake) * dilution);
		if(state.world.ownership_stake_get_economic_actor_from_ownership_stake_owner(stake) == investor)
			investor_stake = stake;
	}
	if(investor_stake) {
		auto issued = set_stake_fractions(state, investor_stake,
			state.world.ownership_stake_get_ownership_fraction(investor_stake) + new_fraction,
			state.world.ownership_stake_get_voting_fraction(investor_stake) + new_fraction,
			state.world.ownership_stake_get_economic_fraction(investor_stake) + new_fraction);
		if(!issued) return false;
	} else {
		auto created = create_stake(state, investor, asset, new_fraction, new_fraction, new_fraction);
		if(!created) return false;
		assign_runtime_canonical_id(state, created);
	}
	if(organization) {
		state.world.organization_set_paid_in_equity(organization,
			state.world.organization_get_paid_in_equity(organization) + investment);
		state.world.asset_set_appraised_value(asset,
			std::max(0.0f, state.world.organization_get_paid_in_equity(organization)
				+ state.world.organization_get_retained_earnings(organization)));
	}
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

void collect_canonical_ownership_errors(sys::state const& state, std::vector<std::string>& errors) {
	std::unordered_set<uint32_t> productive_assets;
	state.world.for_each_factory([&](dcon::factory_id factory) {
		auto label = factory_label(state, factory);
		auto site = state.world.factory_get_site_from_factory_site(factory);
		if(!site || !state.world.site_is_valid(site)) {
			errors.push_back(label + " has no valid canonical site");
			return;
		}
		if(state.world.site_get_canonical_id(site) == 0)
			errors.push_back(label + " site has no deterministic stable site ID");
		if(state.world.factory_get_canonical_id(factory) == 0)
			errors.push_back(label + " has no deterministic stable factory ID");
		auto organization = organizations::operator_organization_for_factory(state, factory);
		if(!organization || !state.world.organization_is_valid(organization)) {
			errors.push_back(label + " has no explicitly declared canonical operator organization");
			return;
		}
		auto asset = asset_for_factory(state, factory);
		if(!has_organization_graph(state, organization))
			errors.push_back(label + " operator organization " + std::to_string(organization.index())
				+ " has no stable actor/equity ownership graph");
		if(!asset || !state.world.asset_is_valid(asset)) {
			errors.push_back(label + " has no canonical asset record");
			return;
		}
		if(state.world.asset_get_canonical_id(asset) == 0)
			errors.push_back(label + " asset has no deterministic stable ID");
		if(!productive_assets.insert(asset.index()).second)
			errors.push_back(label + " shares its asset record with another productive asset");
		if(!asset_has_complete_ownership(state, asset))
			errors.push_back(label + " asset has no unique, complete owner stake graph totaling 1.0");
	});
	state.world.for_each_resource_deposit([&](dcon::resource_deposit_id deposit) {
		auto organization = organizations::operator_organization_for_deposit(state, deposit);
		if(!organization || !state.world.organization_is_valid(organization)) {
			auto site = state.world.resource_deposit_get_site_from_resource_deposit_site(deposit);
			errors.push_back("resource deposit dcon:" + std::to_string(deposit.index()) + " at site dcon:"
				+ std::to_string(site.index()) + " has no explicitly declared canonical operator organization");
			return;
		}
		auto asset = asset_for_deposit(state, deposit);
		auto site = state.world.resource_deposit_get_site_from_resource_deposit_site(deposit);
		auto label = "resource deposit dcon:" + std::to_string(deposit.index()) + " at site dcon:"
			+ std::to_string(site.index());
		if(!site || !state.world.site_is_valid(site))
			errors.push_back(label + " has no valid canonical site");
		else if(state.world.site_get_canonical_id(site) == 0)
			errors.push_back(label + " site has no deterministic stable site ID");
		if(state.world.resource_deposit_get_canonical_id(deposit) == 0)
			errors.push_back(label + " has no deterministic stable deposit ID");
		if(!has_organization_graph(state, organization))
			errors.push_back(label + " operator organization " + std::to_string(organization.index())
				+ " has no stable actor/equity ownership graph");
		if(!asset || !state.world.asset_is_valid(asset)) {
			errors.push_back(label + " has no canonical asset record");
			return;
		}
		if(state.world.asset_get_canonical_id(asset) == 0)
			errors.push_back(label + " asset has no deterministic stable ID");
		if(!productive_assets.insert(asset.index()).second)
			errors.push_back(label + " shares its asset record with another productive asset");
		if(!asset_has_complete_ownership(state, asset))
			errors.push_back(label + " asset has no unique, complete owner stake graph totaling 1.0");
	});
	state.world.for_each_land_title([&](dcon::land_title_id title) {
		auto label = "land title dcon:" + std::to_string(title.index());
		auto asset = state.world.land_title_get_asset_from_land_title_asset(title);
		if(state.world.land_title_get_canonical_id(title) == 0)
			errors.push_back(label + " has no deterministic stable ID");
		if(!asset || !state.world.asset_is_valid(asset)) {
			errors.push_back(label + " has no canonical asset record");
			return;
		}
		if(!productive_assets.insert(asset.index()).second)
			errors.push_back(label + " shares its asset record with another productive asset");
		if(!asset_has_complete_ownership(state, asset))
			errors.push_back(label + " asset has no unique, complete owner stake graph totaling 1.0");
	});
}

bool canonical_ownership_is_valid(sys::state const& state) {
	std::vector<std::string> errors;
	collect_canonical_ownership_errors(state, errors);
	return errors.empty();
}

void validate_canonical_ownership(sys::state const& state) {
	auto const valid = canonical_ownership_is_valid(state);
	assert(valid && "every factory and resource deposit requires authored canonical firm, asset, and ownership data");
	if(!valid) std::abort();
}

} // namespace actors::ownership
