#include "electorate.hpp"

#include "system_state.hpp"
#include "actors/organizations/organizations.hpp"
#include "economy/banking/banking.hpp"
#include "economy/capital_market.hpp"
#include "economy/exact_person_economy.hpp"
#include "economy/households.hpp"
#include "economy/wallets.hpp"
#include "governance/governance.hpp"
#include "persons/exact_population.hpp"

#include <algorithm>
#include <cmath>

namespace governance::electorate {
namespace {

constexpr float epsilon = 1.0e-6f;

float saturating(float value) { return value > 0.0f ? value / (1.0f + value) : 0.0f; }

} // namespace

dcon::territorial_unit_id region_of(sys::state const& state, dcon::province_id province) {
	for(auto unit = territory_of(state, province); unit; unit = parent_of(state, unit))
		if(state.world.territorial_unit_get_state_definition_from_territorial_unit_region(unit)) return unit;
	return territory_of(state, province);
}

std::vector<voter> voters(sys::state const& state, dcon::nation_id nation) {
	std::vector<voter> result;
	if(!nation) return result;
	// Household cohorts: their members are every person without an individual budget.
	state.world.for_each_organization([&](dcon::organization_id organization) {
		if(!economy::households::is_household(state, organization)) return;
		auto province = economy::households::home_of(state, organization);
		if(!province || state.world.province_get_nation_from_province_ownership(province) != nation) return;
		auto members = economy::households::members(state, organization);
		if(!(members > 0.0f)) return;
		voter value{};
		value.cohort = organization;
		value.province = province;
		value.region = region_of(state, province);
		value.adults = members * adult_share_of_members;
		auto workers = economy::households::workers(state, organization);
		value.income = economy::households::reservation_wage(state, organization) * workers / value.adults;
		auto actor = actors::organizations::actor_for_organization(state, organization);
		value.wealth = economy::capital_market::liquid_funds(state, actor, economy::capital_market::settlement_of(state, actor)) / value.adults;
		value.rural = economy::households::role_of(state, organization) == economy::households::role::peasant;
		value.dependents = true;
		result.push_back(value);
	});
	// People with an individual budget vote with the adults of their family.
	auto individuals = economy::households::individual_consumers(state);
	for(auto key : economy::exact_person_economy::account_owners(state)) {
		if(!persons::alive(state, key)) continue;
		auto home = persons::home_site(state, key);
		auto province = home ? state.world.site_get_province_from_site_location(home) : dcon::province_id{};
		if(!province || state.world.province_get_nation_from_province_ownership(province) != nation) continue;
		voter value{};
		value.person = key;
		value.province = province;
		value.region = region_of(state, province);
		auto family = individuals.contains({ key.source_population_cell, key.ordinal })
			? economy::households::family_size(state, key, individuals) : 1;
		value.adults = family > 1 ? 2.0f : 1.0f;
		value.dependents = family > 2;
		for(auto contract_id : economy::exact_person_economy::active_contracts_for_person(state, key)) {
			auto contract = economy::exact_person_economy::contract(state, contract_id);
			if(!contract || contract->pay_period_days == 0) continue;
			value.income += contract->wage_rate * contract->labor_capacity / float(contract->pay_period_days);
			if(contract->institution) value.public_employee = true;
		}
		value.income /= value.adults;
		float cash = 0.0f;
		for(auto account : economy::exact_person_economy::accounts_for_person(state, key)) cash += economy::wallets::spendable(state, account);
		if(auto profile = persons::exact_population::profile_for_person(state, key))
			if(auto actor = persons::actor_for_person(state, profile))
				state.world.economic_actor_for_each_deposit_account_owner_as_economic_actor(actor, [&](auto relation) {
					cash += std::max(0.0f, economy::banking::deposit_balance(state, state.world.deposit_account_owner_get_deposit_account(relation)));
				});
		value.wealth = cash / value.adults;
		result.push_back(value);
	}
	auto median = median_income(result);
	for(auto& value : result) assess(value, median);
	return result;
}

float median_income(std::vector<voter> const& all) {
	std::vector<std::pair<float, float>> weighted;
	double total = 0.0;
	for(auto const& value : all)
		if(value.adults > 0.0f) {
			weighted.push_back({ value.income, value.adults });
			total += value.adults;
		}
	if(weighted.empty()) return 0.0f;
	std::sort(weighted.begin(), weighted.end());
	double running = 0.0;
	for(auto const& [income, adults] : weighted) {
		running += adults;
		if(running >= total * 0.5) return income;
	}
	return weighted.back().first;
}

void assess(voter& value, float median) {
	auto relative = std::log(std::max(value.income / std::max(median, epsilon), 0.05f));
	auto riches = saturating(value.wealth / std::max(median * 60.0f, epsilon));
	auto public_bonus = value.public_employee ? 0.1f : 0.0f;
	policy::position ideal{};
	ideal[size_t(policy::dimension::tax_level)] = 0.2f - 0.08f * relative + (value.public_employee ? 0.08f : 0.0f);
	ideal[size_t(policy::dimension::progressivity)] = 0.5f - 0.35f * relative;
	ideal[size_t(policy::dimension::education)] = 0.35f + (value.dependents ? 0.3f : 0.0f) + public_bonus;
	ideal[size_t(policy::dimension::policing)] = 0.25f + 0.4f * riches + public_bonus;
	ideal[size_t(policy::dimension::public_works)] = 0.3f + (value.rural ? 0.3f : 0.0f) + public_bonus;
	ideal[size_t(policy::dimension::local_government)] = 0.3f + (value.rural ? 0.25f : 0.0f);
	value.ideal = policy::clamp(ideal);
	value.turnout = std::clamp(0.45f + 0.15f * std::tanh(relative) + (value.public_employee ? 0.1f : 0.0f) + 0.1f * riches, 0.2f, 0.9f);
}

} // namespace governance::electorate
