#include "electorate.hpp"

#include "system_state.hpp"
#include "actors/organizations/organizations.hpp"
#include "economy/banking/banking.hpp"
#include "economy/capital_market.hpp"
#include "economy/collective_labor.hpp"
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
		auto role = economy::households::role_of(state, organization);
		value.rural = role == economy::households::role::peasant;
		value.worker = role == economy::households::role::peasant || role == economy::households::role::urban;
		value.shareholder = false;
		state.world.economic_actor_for_each_ownership_stake_owner_as_economic_actor(actor, [&](auto relation) {
			value.shareholder = value.shareholder || state.world.ownership_stake_get_economic_fraction(
				state.world.ownership_stake_owner_get_ownership_stake(relation)) > 0.0f;
		});
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
			value.worker = true;
			value.wage_arrears = value.wage_arrears || contract->unpaid_wages > 1.0e-5f;
			value.income += economy::exact_person_economy::wage_due(state, contract_id);
			if(contract->institution) value.public_employee = true;
		}
		value.union_member = bool(economy::collective_labor::union_for_member(state, key));
		value.on_strike = economy::collective_labor::on_strike(state, key, state.current_date);
		value.income += economy::collective_labor::strike_benefit_income(state, key, state.current_date);
		value.unemployed = economy::exact_person_economy::is_unemployed(state, key);
		value.income /= value.adults;
		float cash = 0.0f;
		for(auto account : economy::exact_person_economy::accounts_for_person(state, key)) cash += economy::wallets::spendable(state, account);
		if(auto profile = persons::exact_population::profile_for_person(state, key))
			if(auto actor = persons::actor_for_person(state, profile)) {
				state.world.economic_actor_for_each_deposit_account_owner_as_economic_actor(actor, [&](auto relation) {
					auto deposit = state.world.deposit_account_owner_get_deposit_account(relation);
					auto balance = std::max(0.0f, economy::banking::deposit_balance(state, deposit));
					cash += balance;
					value.depositor = value.depositor || balance > epsilon;
				});
				state.world.economic_actor_for_each_ownership_stake_owner_as_economic_actor(actor, [&](auto relation) {
					value.shareholder = value.shareholder || state.world.ownership_stake_get_economic_fraction(
						state.world.ownership_stake_owner_get_ownership_stake(relation)) > 0.0f;
				});
				state.world.economic_actor_for_each_obligation_debtor_as_economic_actor(actor, [&](auto relation) {
					auto obligation = state.world.obligation_debtor_get_obligation(relation);
					value.debtor = value.debtor || state.world.obligation_get_principal_outstanding(obligation) > epsilon;
				});
			}
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
	using policy::topic_id;
	policy::position ideal;
	policy::salience issue_salience;
	auto prefer = [&](topic_id topic, policy::policy_value position, float weight) {
		if(weight <= 0.0f) return;
		(void)policy::set(ideal, topic, position);
		issue_salience.push_back({topic, weight});
	};
	prefer(topic_id::income_tax, 0.2f - 0.08f * relative + (value.public_employee ? 0.08f : 0.0f), 0.75f + 0.25f * riches);
	prefer(topic_id::progressivity, 0.5f - 0.35f * relative, 0.5f + 0.5f * std::abs(relative));
	prefer(topic_id::education_appropriation, 0.35f + (value.dependents ? 0.3f : 0.0f) + public_bonus,
		0.25f + (value.dependents ? 0.75f : 0.0f) + public_bonus);
	prefer(topic_id::policing_appropriation, 0.25f + 0.4f * riches + public_bonus, 0.25f + 0.3f * riches);
	prefer(topic_id::public_works_appropriation, 0.3f + (value.rural ? 0.3f : 0.0f) + public_bonus,
		0.2f + (value.rural ? 0.8f : 0.0f));
	prefer(topic_id::local_government_appropriation, 0.3f + (value.rural ? 0.25f : 0.0f),
		0.2f + (value.rural ? 0.8f : 0.0f));
	if(value.worker || value.unemployed) {
		prefer(topic_id::minimum_wage, value.wage_arrears ? 8.0f : (value.worker ? 4.0f : 5.0f),
			value.wage_arrears ? 1.0f : 0.65f);
		prefer(topic_id::labor_protection, int32_t(value.wage_arrears || value.unemployed ? 3 : 2),
			value.wage_arrears ? 1.0f : 0.55f);
		prefer(topic_id::collective_bargaining, policy::category_value{uint16_t(value.worker ? 2 : 3)},
			value.worker ? 0.8f : 0.7f);
		prefer(topic_id::unemployment_replacement, value.unemployed ? 0.6f : 0.35f,
			value.unemployed ? 1.0f : 0.25f);
		prefer(topic_id::unemployment_duration, int32_t(value.unemployed ? 180 : 90),
			value.unemployed ? 1.0f : 0.2f);
	}
	if(value.union_member || value.on_strike) {
		prefer(topic_id::collective_bargaining, policy::category_value{uint16_t(
			policy::collective_bargaining_mode::sectoral_recognition)}, value.on_strike ? 1.5f : 1.1f);
		prefer(topic_id::labor_protection, int32_t(policy::labor_protection_level::just_cause),
			value.on_strike ? 1.5f : 1.0f);
	}
	if(value.shareholder) prefer(topic_id::dividend_tax, riches > 0.5f ? 0.05f : 0.15f, 0.9f);
	if(value.rural) prefer(topic_id::resource_royalty, 0.1f, 0.45f);
	if(value.depositor) prefer(topic_id::bank_reserve_requirement, 0.12f, 0.2f);
	value.ideal = policy::clamp(std::move(ideal));
	value.issue_salience = std::move(issue_salience);
	value.turnout = std::clamp(0.45f + 0.15f * std::tanh(relative) + (value.public_employee ? 0.1f : 0.0f) + 0.1f * riches, 0.2f, 0.9f);
}

} // namespace governance::electorate
