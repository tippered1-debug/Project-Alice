#include "dividends.hpp"

#include "system_state.hpp"
#include "actors/organizations/organizations.hpp"
#include "actors/ownership.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/banking/banking.hpp"
#include "economy/capital_projects.hpp"
#include "economy/exact_person_economy.hpp"
#include "economy/households.hpp"
#include "economy/liquidity.hpp"
#include "governance/finance/finance.hpp"
#include "governance/governance.hpp"
#include "governance/policy.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace economy::dividends {
namespace {
constexpr float epsilon = 1.0e-4f;

bool is_bank(sys::state const& state, dcon::organization_id organization) {
	return state.world.organization_get_kind(organization) == uint8_t(actors::ownership::actor_kind::bank);
}

dcon::monetary_account_id first_operating_account(sys::state const& state, dcon::economic_actor_id actor) {
	dcon::monetary_account_id result{};
	state.world.economic_actor_for_each_monetary_account_owner_as_economic_actor(actor, [&](auto relation) {
		auto account = state.world.monetary_account_owner_get_monetary_account(relation);
		if(!result || account.index() < result.index()) result = account;
	});
	return result;
}

dcon::nation_id tax_jurisdiction_for(sys::state const& state, dcon::economic_actor_id actor) {
	if(auto nation = economy::liquidity::nation_for(state, actor)) return nation;
	auto institution = state.world.economic_actor_get_institution_from_institution_actor(actor);
	return institution ? governance::nation_of(state, institution) : dcon::nation_id{};
}
}

wallets::account_ref payout_account(sys::state const& state, dcon::organization_id organization) {
	if(!organization || !state.world.organization_is_valid(organization)) return {};
	if(is_bank(state, organization)) {
		auto account = economy::banking::reserve_account_for(state, organization,
			state.world.organization_get_bank_settlement_currency(organization));
		return account ? wallets::account_ref::from_dcon(account) : wallets::account_ref{};
	}
	auto account = first_operating_account(state, actors::organizations::actor_for_organization(state, organization));
	return account ? wallets::account_ref::from_dcon(account) : wallets::account_ref{};
}

dcon::commodity_id payout_settlement(sys::state const& state, dcon::organization_id organization) {
	auto account = payout_account(state, organization);
	return account ? economy::accounts::settlement_of(state, account.dcon_account) : dcon::commodity_id{};
}

bool winding_up(sys::state const& state, dcon::organization_id organization) {
	if(!organization || is_bank(state, organization) || economy::households::is_household(state, organization)) return false;
	if(!actors::organizations::factories_operated_by(state, organization).empty()
		|| !actors::organizations::deposits_operated_by(state, organization).empty()) return false;
	auto actor = actors::organizations::actor_for_organization(state, organization);
	bool busy = false;
	state.world.economic_actor_for_each_capital_project_sponsor_as_economic_actor(actor, [&](auto relation) {
		auto project = state.world.capital_project_sponsor_get_capital_project(relation);
		if(state.world.capital_project_get_status(project) < uint8_t(economy::capital_projects::status::completed)) busy = true;
	});
	state.world.organization_for_each_capital_project_responsible_as_organization(organization, [&](auto relation) {
		auto project = state.world.capital_project_responsible_get_capital_project(relation);
		if(state.world.capital_project_get_status(project) < uint8_t(economy::capital_projects::status::completed)) busy = true;
	});
	state.world.economic_actor_for_each_obligation_debtor_as_economic_actor(actor, [&](auto relation) {
		auto obligation = state.world.obligation_debtor_get_obligation(relation);
		if(state.world.obligation_get_status(obligation) == uint8_t(economy::relations::obligation_status::active)) busy = true;
	});
	return !busy;
}

float operating_reserve(sys::state const& state, dcon::organization_id organization) {
	float daily_costs = 0.0f;
	float unpaid_wages = 0.0f;
	for(auto factory : actors::organizations::factories_operated_by(state, organization)) {
		daily_costs += std::max(0.0f, state.world.factory_get_agency_recent_costs(factory));
		unpaid_wages += std::max(0.0f, economy::exact_person_economy::unpaid_wages_for_factory(state, factory));
	}
	auto result = unpaid_wages + cash_buffer_days * daily_costs;
	return std::isfinite(result) ? result : 0.0f;
}

float payable(sys::state const& state, dcon::organization_id organization) {
	if(!organization || !state.world.organization_is_valid(organization)
		|| economy::households::is_household(state, organization)) return 0.0f;
	auto source = payout_account(state, organization);
	if(!source) return 0.0f;
	auto cash = wallets::spendable(state, source);
	auto retained = std::max(0.0f, state.world.organization_get_retained_earnings(organization));
	if(is_bank(state, organization)) {
		if(economy::banking::status_of(state, organization) != economy::banking::bank_status::solvent) return 0.0f;
		auto sheet = economy::banking::bank_balance_sheet(state, organization,
			state.world.organization_get_bank_settlement_currency(organization));
		auto minimum = state.world.organization_get_bank_minimum_capital_ratio(organization) + bank_capital_buffer;
		// The capital ratio scales with net worth while its risk base is unchanged.
		auto by_capital = sheet.capital_ratio > minimum && sheet.net_worth > 0.0f
			? sheet.net_worth * (1.0f - minimum / sheet.capital_ratio) : 0.0f;
		auto by_liquidity = sheet.settlement_assets - sheet.required_liquidity;
		auto result = std::min({payout_ratio * retained, by_capital, by_liquidity, cash});
		return std::isfinite(result) ? std::max(0.0f, result) : 0.0f;
	}
	if(winding_up(state, organization)) return cash;
	auto free = cash - operating_reserve(state, organization);
	auto result = std::min(payout_ratio * retained, free);
	return std::isfinite(result) ? std::max(0.0f, result) : 0.0f;
}

float distribute(sys::state& state, dcon::organization_id organization, float amount) {
	auto source = payout_account(state, organization);
	auto settlement = payout_settlement(state, organization);
	auto equity = actors::organizations::equity_asset_for_organization(state, organization);
	auto self = actors::organizations::actor_for_organization(state, organization);
	if(!source || !settlement || !equity || !(amount > epsilon) || !std::isfinite(amount)) return 0.0f;
	struct holder { dcon::economic_actor_id owner; float fraction; };
	std::vector<holder> holders;
	float total = 0.0f;
	state.world.asset_for_each_ownership_stake_asset_as_asset(equity, [&](dcon::ownership_stake_asset_id relation) {
		auto stake = state.world.ownership_stake_asset_get_ownership_stake(relation);
		auto owner = state.world.ownership_stake_get_economic_actor_from_ownership_stake_owner(stake);
		auto fraction = state.world.ownership_stake_get_economic_fraction(stake);
		if(!owner || owner == self || !(fraction > 0.0f)) return;
		holders.push_back({owner, fraction});
		total += fraction;
	});
	if(holders.empty() || !(total > 0.0f)) return 0.0f;
	std::sort(holders.begin(), holders.end(), [](auto const& left, auto const& right) { return left.owner.index() < right.owner.index(); });
	amount = std::min(amount, wallets::spendable(state, source));
	float paid = 0.0f;
	for(auto const& [owner, fraction] : holders) {
		auto share = amount * fraction / total;
		auto destination = wallets::open_for(state, owner, settlement);
		if(share > epsilon && destination && wallets::pay(state, source, destination, share, relations::transaction_kind::dividend)) {
			paid += share;
			if(auto nation = tax_jurisdiction_for(state, owner))
				(void)governance::finance::assess_and_collect_topic_tax(state,
					governance::policy::topic_id::dividend_tax, nation, owner, share,
					destination, {}, state.current_date);
		}
	}
	return paid;
}

float pay(sys::state& state, dcon::organization_id organization) {
	auto amount = payable(state, organization);
	if(!(amount > epsilon)) return 0.0f;
	auto paid = distribute(state, organization, amount);
	if(!(paid > 0.0f) || is_bank(state, organization)) return paid; // bank equity resyncs from its balance sheet
	auto retained = state.world.organization_get_retained_earnings(organization);
	auto from_retained = std::min(paid, std::max(0.0f, retained));
	state.world.organization_set_retained_earnings(organization, retained - from_retained);
	// A winding-up distribution beyond earnings returns paid-in capital.
	if(paid - from_retained > 0.0f)
		state.world.organization_set_paid_in_equity(organization,
			std::max(0.0f, state.world.organization_get_paid_in_equity(organization) - (paid - from_retained)));
	if(auto equity = actors::organizations::equity_asset_for_organization(state, organization))
		state.world.asset_set_appraised_value(equity, std::max(0.0f, state.world.organization_get_paid_in_equity(organization)
			+ state.world.organization_get_retained_earnings(organization)));
	return paid;
}

void process(sys::state& state) {
	std::vector<dcon::organization_id> organizations;
	auto day = state.current_date.to_raw_value();
	state.world.for_each_organization([&](dcon::organization_id organization) {
		if((day + int32_t(organization.index())) % payout_period_days != 0) return;
		if(!actors::organizations::equity_asset_for_organization(state, organization)) return;
		organizations.push_back(organization);
	});
	std::sort(organizations.begin(), organizations.end(), [](auto left, auto right) { return left.index() < right.index(); });
	for(auto organization : organizations) (void)pay(state, organization);
}

} // namespace economy::dividends
