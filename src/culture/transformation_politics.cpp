#include "transformation_politics.hpp"
#include "economy/industry_ownership.hpp"

#include "culture.hpp"
#include "demographics.hpp"
#include "nations.hpp"
#include "policy_execution.hpp"
#include "politics.hpp"
#include "rebels.hpp"
#include "system_state.hpp"
#include "triggers.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>

namespace politics::transformation {
namespace {

constexpr float comparison_epsilon = 0.000001f;

float finite_or(float value, float fallback) {
	return std::isfinite(value) ? value : fallback;
}

float nonnegative(float value, float fallback = 0.0f) {
	return std::max(0.0f, finite_or(value, fallback));
}

float unit_interval(float value) {
	return std::clamp(finite_or(value, 0.0f), 0.0f, 1.0f);
}

float initialized_vote_weight(sys::state const& state, dcon::pop_id pop,
	dcon::nation_id nation) {
	// Lightweight test worlds and partially constructed scenarios may not have
	// run fill_unsaved_data yet. pop_vote_weight indexes the modifier array, so
	// an absent array means "no initialized franchise" rather than a valid read.
	if(state.world.nation_get_modifier_values_size()
		< uint32_t(sys::national_mod_offsets::count))
		return 0.0f;
	return politics::pop_vote_weight(state, pop, nation);
}

float bounded(float value, float fallback, float minimum, float maximum) {
	return std::clamp(finite_or(value, fallback), minimum, maximum);
}

float finite_nonnegative_float(double value) {
	if(!std::isfinite(value) || value <= 0.0)
		return 0.0f;
	return float(std::min(value, double(std::numeric_limits<float>::max())));
}

uint8_t maximum_coalition_groups(ruleset_config const& config) {
	return std::clamp<uint8_t>(config.maximum_coalition_groups, 1, uint8_t(interest_group_count));
}

float coalition_target(ruleset_config const& config) {
	return bounded(config.coalition_power_target, 0.50f, 0.05f, 0.95f);
}

float wealth_signal(float savings_per_capita, ruleset_config const& config) {
	auto const wealth = nonnegative(savings_per_capita);
	auto const reference = bounded(config.wealth_reference, 1.0f, 0.000001f, 1000000000.0f);
	auto const ratio = std::min(wealth / reference, 80.0f);
	return unit_interval(1.0f - std::exp(-ratio));
}

float power_per_capita(population_sample const& sample, ruleset_config const& config, float wealth) {
	auto const maximum = bounded(config.maximum_power_per_capita, 4.0f, 0.05f, 10.0f);
	auto result =
		bounded(config.base_power, 0.25f, 0.0f, 10.0f)
		+ bounded(config.wealth_power_weight, 1.25f, 0.0f, 10.0f) * wealth
		+ bounded(config.income_power_weight, 0.35f, 0.0f, 10.0f) * unit_interval(sample.income_security)
		+ bounded(config.property_power_weight, 1.50f, 0.0f, 10.0f) * unit_interval(sample.property_ownership)
		+ bounded(config.literacy_power_weight, 0.35f, 0.0f, 10.0f) * unit_interval(sample.literacy)
		+ bounded(config.consciousness_power_weight, 0.30f, 0.0f, 10.0f) * unit_interval(sample.consciousness);
	// Rights and organization determine how much private wealth or mass can be
	// converted into institutional influence. The floor preserves non-zero
	// pressure for excluded populations without treating them as voters.
	auto const institutional_access = 0.15f + 0.85f
		* unit_interval(sample.political_rights)
		* unit_interval(sample.political_organization);
	result *= institutional_access;
	return std::clamp(finite_or(result, 0.0f), 0.0f, maximum);
}

struct group_accumulator {
	double population_support = 0.0;
	double political_power = 0.0;
	double wealth = 0.0;
	double income = 0.0;
	double property = 0.0;
	double literacy = 0.0;
	double consciousness = 0.0;
};

constexpr std::array<std::array<float, interest_group_count>, interest_group_count> group_compatibility = {{
	{{1.00f, 0.65f, 0.30f, 0.10f, 0.80f, 0.75f}},
	{{0.65f, 1.00f, 0.70f, 0.25f, 0.45f, 0.75f}},
	{{0.30f, 0.70f, 1.00f, 0.75f, 0.50f, 0.70f}},
	{{0.10f, 0.25f, 0.75f, 1.00f, 0.75f, 0.55f}},
	{{0.80f, 0.45f, 0.50f, 0.75f, 1.00f, 0.70f}},
	{{0.75f, 0.75f, 0.70f, 0.55f, 0.70f, 1.00f}},
}};

float coalition_cohesion(interest_group_mask mask) {
	auto const count = std::popcount(mask);
	if(count == 0)
		return 0.0f;
	if(count == 1)
		return 1.0f;

	float sum = 0.0f;
	uint32_t pairs = 0;
	for(std::size_t first = 0; first < interest_group_count; ++first) {
		if((mask & (interest_group_mask(1u) << first)) == 0)
			continue;
		for(std::size_t second = first + 1; second < interest_group_count; ++second) {
			if((mask & (interest_group_mask(1u) << second)) == 0)
				continue;
			sum += group_compatibility[first][second];
			++pairs;
		}
	}
	return pairs > 0 ? unit_interval(sum / float(pairs)) : 1.0f;
}

struct coalition_candidate {
	bool valid = false;
	interest_group_mask mask = 0;
	uint8_t group_count = 0;
	float power_share = 0.0f;
	float support_share = 0.0f;
	float cohesion = 0.0f;
	float score = 0.0f;
	bool majority = false;
};

bool candidate_is_better(coalition_candidate const& challenger, coalition_candidate const& incumbent) {
	if(!challenger.valid)
		return false;
	if(!incumbent.valid)
		return true;
	if(challenger.score > incumbent.score + comparison_epsilon)
		return true;
	if(incumbent.score > challenger.score + comparison_epsilon)
		return false;
	// The mask is a stable interest-group ID ordering. It is the final and only
	// tie-break so identical inputs never depend on container iteration order.
	return challenger.mask < incumbent.mask;
}

coalition_candidate make_candidate(
	interest_group_mask mask,
	std::array<float, interest_group_count> const& power,
	std::array<float, interest_group_count> const& support,
	ruleset_config const& config) {
	coalition_candidate result;
	result.valid = mask != 0;
	result.mask = mask;
	result.group_count = uint8_t(std::popcount(mask));
	for(std::size_t i = 0; i < interest_group_count; ++i) {
		if((mask & (interest_group_mask(1u) << i)) == 0)
			continue;
		result.power_share += power[i];
		result.support_share += support[i];
	}
	result.power_share = unit_interval(result.power_share);
	result.support_share = unit_interval(result.support_share);
	result.cohesion = coalition_cohesion(mask);

	auto const target = coalition_target(config);
	result.majority = result.power_share + comparison_epsilon >= target;
	auto const viability = unit_interval(result.power_share / target);
	result.score =
		bounded(config.coalition_power_weight, 0.45f, 0.0f, 10.0f) * viability
		+ bounded(config.coalition_support_weight, 0.15f, 0.0f, 10.0f) * result.support_share
		+ bounded(config.coalition_cohesion_weight, 0.20f, 0.0f, 10.0f) * result.cohesion
		+ (result.majority ? bounded(config.coalition_majority_bonus, 0.25f, 0.0f, 10.0f) : 0.0f)
		- bounded(config.coalition_size_penalty, 0.025f, 0.0f, 10.0f) * float(result.group_count - 1);
	result.score = finite_or(result.score, 0.0f);
	return result;
}

void normalize_shares(std::array<float, interest_group_count>& values) {
	float total = 0.0f;
	for(auto& value : values) {
		value = nonnegative(value);
		total += value;
	}
	if(total <= comparison_epsilon) {
		values.fill(0.0f);
		return;
	}
	for(auto& value : values)
		value /= total;
}

void assign_candidate(coalition_result& result, coalition_candidate const& chosen) {
	if(!chosen.valid)
		return;
	result.groups = chosen.mask;
	result.group_count = chosen.group_count;
	result.power_share = chosen.power_share;
	result.support_share = chosen.support_share;
	result.cohesion = chosen.cohesion;
	result.score = chosen.score;
	result.has_working_majority = chosen.majority;
}

} // namespace

std::array<float, interest_group_count> affinity_for_role(population_role role) {
	switch(role) {
	case population_role::landowner:
		return {{0.80f, 0.08f, 0.02f, 0.00f, 0.08f, 0.02f}};
	case population_role::capital_owner:
		return {{0.06f, 0.80f, 0.06f, 0.02f, 0.02f, 0.04f}};
	case population_role::artisan:
		return {{0.06f, 0.48f, 0.22f, 0.14f, 0.06f, 0.04f}};
	case population_role::intellectual:
		return {{0.04f, 0.07f, 0.68f, 0.10f, 0.04f, 0.07f}};
	case population_role::administrator:
		return {{0.08f, 0.10f, 0.30f, 0.05f, 0.04f, 0.43f}};
	case population_role::officer:
		return {{0.12f, 0.10f, 0.18f, 0.04f, 0.06f, 0.50f}};
	case population_role::soldier:
		return {{0.03f, 0.03f, 0.06f, 0.30f, 0.14f, 0.44f}};
	case population_role::industrial_worker:
		return {{0.01f, 0.04f, 0.11f, 0.72f, 0.08f, 0.04f}};
	case population_role::farmer:
		return {{0.07f, 0.03f, 0.06f, 0.14f, 0.66f, 0.04f}};
	case population_role::agricultural_laborer:
		return {{0.02f, 0.02f, 0.05f, 0.35f, 0.52f, 0.04f}};
	case population_role::enslaved:
		return {{0.00f, 0.00f, 0.03f, 0.38f, 0.56f, 0.03f}};
	case population_role::other_middle:
		return {{0.05f, 0.18f, 0.35f, 0.20f, 0.10f, 0.12f}};
	case population_role::other_rich:
		return {{0.36f, 0.38f, 0.15f, 0.03f, 0.03f, 0.05f}};
	case population_role::other_poor:
	default:
		return {{0.02f, 0.03f, 0.10f, 0.48f, 0.31f, 0.06f}};
	}
}

interest_group_snapshot aggregate_interest_groups(
	std::vector<population_sample> const& samples,
	ruleset_config const& config) {
	interest_group_snapshot result;
	for(std::size_t i = 0; i < interest_group_count; ++i)
		result.groups[i].id = interest_group_id(i);
	if(!config.enabled)
		return result;

	result.enabled = true;
	std::array<group_accumulator, interest_group_count> accumulated{};
	double represented_population = 0.0;
	double electorate_population = 0.0;
	double total_support = 0.0;
	double total_power = 0.0;

	for(auto const& sample : samples) {
		auto const population = nonnegative(sample.population);
		if(population <= 0.0f)
			continue;
		auto const affinities = affinity_for_role(sample.role);
		auto const wealth = wealth_signal(sample.savings_per_capita, config);
		auto const income = unit_interval(sample.income_security);
		auto const property = unit_interval(sample.property_ownership);
		auto const literacy = unit_interval(sample.literacy);
		auto const consciousness = unit_interval(sample.consciousness);
		auto const individual_power = power_per_capita(sample, config, wealth);
		represented_population += population;
		electorate_population += double(population) * double(unit_interval(sample.political_rights));

		for(std::size_t i = 0; i < interest_group_count; ++i) {
			auto const support = double(population) * double(unit_interval(affinities[i]));
			auto const power = support * double(individual_power);
			auto& group = accumulated[i];
			group.population_support += support;
			group.political_power += power;
			group.wealth += support * wealth;
			group.income += support * income;
			group.property += support * property;
			group.literacy += support * literacy;
			group.consciousness += support * consciousness;
			total_support += support;
			total_power += power;
		}
	}

	result.represented_population = finite_nonnegative_float(represented_population);
	result.electorate_population = finite_nonnegative_float(electorate_population);
	for(std::size_t i = 0; i < interest_group_count; ++i) {
		auto const& source = accumulated[i];
		auto& destination = result.groups[i];
		destination.population_support = finite_nonnegative_float(source.population_support);
		destination.political_power = finite_nonnegative_float(source.political_power);
		if(source.population_support > 0.0) {
			auto const inverse_support = 1.0 / source.population_support;
			destination.mean_wealth_signal = float(source.wealth * inverse_support);
			destination.mean_income_security = float(source.income * inverse_support);
			destination.mean_property_ownership = float(source.property * inverse_support);
			destination.mean_literacy = float(source.literacy * inverse_support);
			destination.mean_consciousness = float(source.consciousness * inverse_support);
			destination.mean_power_per_capita = float(source.political_power * inverse_support);
			result.active_groups |= interest_group_mask(1u) << i;
		}
		if(total_support > 0.0)
			destination.support_share = float(source.population_support / total_support);
		if(total_power > 0.0)
			destination.political_power_share = float(source.political_power / total_power);
	}
	return result;
}

coalition_result select_governing_coalition(
	interest_group_snapshot const& snapshot,
	ruleset_config const& config,
	interest_group_mask previous_coalition) {
	coalition_result result;
	if(!config.enabled || !snapshot.enabled)
		return result;

	std::array<float, interest_group_count> power{};
	std::array<float, interest_group_count> support{};
	for(std::size_t i = 0; i < interest_group_count; ++i) {
		power[i] = snapshot.groups[i].political_power_share;
		support[i] = snapshot.groups[i].support_share;
	}
	normalize_shares(power);
	normalize_shares(support);

	interest_group_mask active = 0;
	for(std::size_t i = 0; i < interest_group_count; ++i) {
		if(power[i] > comparison_epsilon)
			active |= interest_group_mask(1u) << i;
	}
	if(active == 0)
		return result;

	std::array<coalition_candidate, all_group_bits + 1u> candidates{};
	coalition_candidate best_overall;
	auto const maximum_groups = maximum_coalition_groups(config);
	for(interest_group_mask mask = 1; mask <= all_group_bits; ++mask) {
		if((mask & ~active) != 0 || std::popcount(mask) > maximum_groups)
			continue;
		candidates[mask] = make_candidate(mask, power, support, config);
		if(candidate_is_better(candidates[mask], best_overall))
			best_overall = candidates[mask];
	}
	if(!best_overall.valid)
		return result;

	auto const previous_has_unknown_groups = (previous_coalition & ~all_group_bits) != 0;
	previous_coalition &= all_group_bits;
	auto const previous_is_valid = !previous_has_unknown_groups
		&& previous_coalition != 0 && candidates[previous_coalition].valid;
	coalition_candidate chosen = best_overall;
	if(previous_is_valid) {
		auto const incumbent = candidates[previous_coalition];
		coalition_candidate challenger;
		for(interest_group_mask mask = 1; mask <= all_group_bits; ++mask) {
			if(mask == previous_coalition)
				continue;
			if(candidate_is_better(candidates[mask], challenger))
				challenger = candidates[mask];
		}

		result.incumbent_score = incumbent.score;
		if(challenger.valid) {
			result.best_challenger = challenger.mask;
			result.best_challenger_score = challenger.score;
			result.challenger_advantage = challenger.score - incumbent.score;
			auto const hysteresis = bounded(config.coalition_hysteresis_margin, 0.03f, 0.0f, 1.0f);
			chosen = challenger.score <= incumbent.score + hysteresis ? incumbent : challenger;
		} else {
			chosen = incumbent;
		}
		result.retained_incumbent = chosen.mask == previous_coalition;
	} else {
		result.best_challenger = best_overall.mask;
		result.best_challenger_score = best_overall.score;
		result.challenger_advantage = best_overall.score;
	}

	assign_candidate(result, chosen);
	return result;
}

legitimacy_breakdown calculate_legitimacy(
	interest_group_snapshot const& snapshot,
	coalition_result const& coalition,
	ruleset_config const& config,
	interest_group_mask previous_coalition,
	float foreign_ownership) {
	legitimacy_breakdown result;
	if(!config.enabled || !snapshot.enabled || coalition.groups == 0)
		return result;

	auto const power = unit_interval(coalition.power_share);
	auto const support = unit_interval(coalition.support_share);
	auto const cohesion = unit_interval(coalition.cohesion);
	auto const target = coalition_target(config);
	auto const group_count = std::clamp<uint8_t>(coalition.group_count, 1, uint8_t(interest_group_count));
	auto const max_groups = maximum_coalition_groups(config);

	result.power_mandate = 35.0f * unit_interval(power / target);
	result.popular_support = 25.0f * support;
	result.coalition_cohesion = 20.0f * cohesion;
	result.social_breadth = max_groups > 1
		? 10.0f * unit_interval(float(group_count - 1) / float(max_groups - 1))
		: 0.0f;
	result.continuity = previous_coalition != 0
		&& (previous_coalition & ~all_group_bits) == 0
		&& coalition.groups == previous_coalition ? 5.0f : 0.0f;
	result.majority_bonus = power + comparison_epsilon >= target ? 5.0f : 0.0f;
	result.minority_penalty = power < target ? 15.0f * unit_interval((target - power) / target) : 0.0f;
	result.representation_gap_penalty = 15.0f * std::max(0.0f, power - support);
	result.fragmentation_penalty = 2.0f * float(group_count - 1);
	result.foreign_ownership_penalty =
		std::max(0.0f, config.maximum_foreign_ownership_penalty)
		* unit_interval(foreign_ownership);

	auto const positive =
		result.power_mandate + result.popular_support + result.coalition_cohesion
		+ result.social_breadth + result.continuity + result.majority_bonus;
	auto const penalties =
		result.minority_penalty + result.representation_gap_penalty
		+ result.fragmentation_penalty + result.foreign_ownership_penalty;
	result.total = std::clamp(finite_or(positive - penalties, 0.0f), 0.0f, 100.0f);
	return result;
}

nation_result evaluate_population(
	std::vector<population_sample> const& samples,
	ruleset_config const& config,
	interest_group_mask previous_coalition,
	float foreign_ownership) {
	nation_result result;
	if(!config.enabled)
		return result;
	result.enabled = true;
	result.interest_groups = aggregate_interest_groups(samples, config);
	result.coalition = select_governing_coalition(result.interest_groups, config, previous_coalition);
	result.legitimacy = calculate_legitimacy(result.interest_groups, result.coalition, config,
		previous_coalition, foreign_ownership);
	return result;
}

population_role population_role_for_pop_type(sys::state const& state, dcon::pop_type_id pop_type) {
	if(pop_type == state.culture_definitions.aristocrat)
		return population_role::landowner;
	if(pop_type == state.culture_definitions.capitalists)
		return population_role::capital_owner;
	if(pop_type == state.culture_definitions.artisans)
		return population_role::artisan;
	if(pop_type == state.culture_definitions.clergy)
		return population_role::intellectual;
	if(pop_type == state.culture_definitions.bureaucrat)
		return population_role::administrator;
	if(pop_type == state.culture_definitions.officers)
		return population_role::officer;
	if(pop_type == state.culture_definitions.soldiers)
		return population_role::soldier;
	if(pop_type == state.culture_definitions.primary_factory_worker
		|| pop_type == state.culture_definitions.secondary_factory_worker)
		return population_role::industrial_worker;
	if(pop_type == state.culture_definitions.farmers)
		return population_role::farmer;
	if(pop_type == state.culture_definitions.laborers)
		return population_role::agricultural_laborer;
	if(pop_type == state.culture_definitions.slaves)
		return population_role::enslaved;

	auto const strata = culture::pop_strata(state.world.pop_type_get_strata(pop_type));
	switch(strata) {
	case culture::pop_strata::rich: return population_role::other_rich;
	case culture::pop_strata::middle: return population_role::other_middle;
	case culture::pop_strata::poor:
	default: return population_role::other_poor;
	}
}

population_sample sample_from_pop(sys::state const& state, dcon::pop_id pop) {
	population_sample result;
	if(!pop)
		return result;

	result.population = nonnegative(state.world.pop_get_size(pop));
	result.role = population_role_for_pop_type(state, state.world.pop_get_poptype(pop));
	result.political_rights = 0.0f;
	result.literacy = unit_interval(pop_demographics::get_literacy(state, pop));
	result.consciousness = unit_interval(pop_demographics::get_consciousness(state, pop) / 10.0f);
	auto const movement_member = bool(
		state.world.pop_get_movement_from_pop_movement_membership(pop));
	result.political_organization = unit_interval(0.10f
		+ 0.35f * result.literacy + 0.25f * result.consciousness
		+ (movement_member ? 0.30f : 0.0f));
	if(result.role == population_role::enslaved)
		result.political_organization = std::min(0.05f, result.political_organization);
	if(result.population > 0.0f)
		result.savings_per_capita = nonnegative(state.world.pop_get_savings(pop)) / result.population;
	result.income_security =
		0.55f * unit_interval(pop_demographics::get_life_needs(state, pop))
		+ 0.30f * unit_interval(pop_demographics::get_everyday_needs(state, pop))
		+ 0.15f * unit_interval(pop_demographics::get_luxury_needs(state, pop));
	result.property_ownership = 0.0f;
	if(auto const province = state.world.pop_get_province_from_pop_location(pop); province) {
		auto const nation = state.world.province_get_nation_from_province_ownership(province);
		if(nation && result.population > 0.0f
			&& !state.world.province_get_is_colonial(province)) {
			result.political_rights = unit_interval(
				initialized_vote_weight(state, pop, nation) / result.population);
		}
		auto const landed_share = unit_interval(state.world.province_get_landowners_share(province));
		auto const capitalist_share = unit_interval(state.world.province_get_capitalists_share(province));
		auto const state_land_share = unit_interval(state.world.province_get_state_land_share(province));
		auto const foreign_land_share = unit_interval(state.world.province_get_foreign_land_share(province));
		auto const smallholder_share = unit_interval(1.f - landed_share
			- capitalist_share - state_land_share - foreign_land_share);
		// Political standing follows what a class actually owns. Land and
		// industry are blended by their market values, so a province with no
		// industry behaves exactly as it did before this existed.
		auto const industry = economy::industry_ownership::current_distribution(state, province);
		auto const land_value = nonnegative(state.world.province_get_land_market_value(province));
		auto const industry_value = nonnegative(state.world.province_get_industry_market_value(province));
		auto const total_value = land_value + industry_value;
		auto const industry_weight = total_value > 0.0f ? industry_value / total_value : 0.0f;
		auto const blend = [&](float from_land, float from_industry) {
			return unit_interval((1.0f - industry_weight) * from_land
				+ industry_weight * from_industry);
		};
		if(result.role == population_role::landowner) {
			result.property_ownership = blend(landed_share, industry.landed_elites);
		} else if(result.role == population_role::capital_owner) {
			result.property_ownership = blend(capitalist_share, industry.capitalists);
		} else if(result.role == population_role::farmer) {
			result.property_ownership = smallholder_share;
		} else if(result.role == population_role::industrial_worker) {
			// Worker-owned industry is the point of industrial democracy: it has
			// to turn into political weight, not just into income.
			result.property_ownership = unit_interval(industry_weight * industry.workers);
		}
	}
	return result;
}

std::vector<population_sample> collect_nation_population(sys::state const& state, dcon::nation_id nation) {
	std::vector<population_sample> result;
	if(!nation || !state.world.nation_is_valid(nation))
		return result;

	state.world.nation_for_each_province_ownership(nation, [&](dcon::province_ownership_id ownership) {
		auto const province = state.world.province_ownership_get_province(ownership);
		state.world.province_for_each_pop_location(province, [&](dcon::pop_location_id location) {
			result.push_back(sample_from_pop(state, state.world.pop_location_get_pop(location)));
		});
	});
	return result;
}


// Value-weighted share of a nation's land and industry that is held abroad.
float foreign_owned_share(sys::state const& state, dcon::nation_id nation) {
	if(!nation || !state.world.nation_is_valid(nation))
		return 0.0f;
	double total_value = 0.0;
	double foreign_value = 0.0;
	for(auto ownership : state.world.nation_get_province_ownership(nation)) {
		auto const province = ownership.get_province();
		auto const land_value = double(nonnegative(
			state.world.province_get_land_market_value(province)));
		auto const industry_value = double(nonnegative(
			state.world.province_get_industry_market_value(province)));
		total_value += land_value + industry_value;
		foreign_value += land_value
			* double(unit_interval(state.world.province_get_foreign_land_share(province)));
		foreign_value += industry_value
			* double(economy::industry_ownership::current_distribution(state, province).foreign);
	}
	if(total_value <= 0.0)
		return 0.0f;
	return unit_interval(float(foreign_value / total_value));
}

nation_result evaluate_nation(
	sys::state const& state,
	dcon::nation_id nation,
	ruleset_config const& config,
	interest_group_mask previous_coalition) {
	if(!config.enabled)
		return {};
	auto result = evaluate_population(collect_nation_population(state, nation), config,
		previous_coalition, foreign_owned_share(state, nation));
	if(nation.index() < state.transformation_government_state.size()
		&& state.transformation_government_state[nation.index()].party_mandate >= 0.0f) {
		result.government.party_mandate = unit_interval(
			state.transformation_government_state[nation.index()].party_mandate);
	}
	return result;
}

issue_support_result evaluate_issue_support(sys::state& state,
	dcon::nation_id nation, dcon::issue_option_id option) {
	issue_support_result result;
	auto const config = ruleset_config_for(state);
	if(!config.enabled || !nation || !option || !state.world.nation_is_valid(nation))
		return result;
	result.enabled = true;

	std::array<double, interest_group_count> group_backing{};
	std::array<double, interest_group_count> group_power{};
	double popular_backing = 0.0;
	double population = 0.0;
	double electoral_backing = 0.0;
	double electorate = 0.0;
	double power_backing = 0.0;
	double total_power = 0.0;
	auto const issue_key = pop_demographics::to_key(state, option);

	state.world.nation_for_each_province_ownership(nation, [&](dcon::province_ownership_id ownership) {
		auto const province = state.world.province_ownership_get_province(ownership);
		state.world.province_for_each_pop_location(province, [&](dcon::pop_location_id location) {
			auto const pop = state.world.pop_location_get_pop(location);
			auto const sample = sample_from_pop(state, pop);
			if(sample.population <= 0.f)
				return;
			auto const support = unit_interval(pop_demographics::get_demo(state, pop, issue_key));
			auto const affinity = affinity_for_role(sample.role);
			auto const per_capita_power = power_per_capita(sample, config, wealth_signal(sample.savings_per_capita, config));
			auto const pop_power = double(sample.population) * double(per_capita_power);
			popular_backing += double(sample.population) * double(support);
			population += double(sample.population);
			auto const vote_weight = nonnegative(
				initialized_vote_weight(state, pop, nation));
			electoral_backing += double(vote_weight) * double(support);
			electorate += double(vote_weight);
			power_backing += pop_power * double(support);
			total_power += pop_power;
			for(std::size_t i = 0; i < interest_group_count; ++i) {
				auto const affinity_power = pop_power * double(affinity[i]);
				group_power[i] += affinity_power;
				group_backing[i] += affinity_power * double(support);
			}
		});
	});

	result.popular_support = population > 0.0 ? unit_interval(float(popular_backing / population)) : 0.f;
	result.electoral_support = electorate > 0.0
		? unit_interval(float(electoral_backing / electorate)) : 0.f;
	result.political_power_support = total_power > 0.0 ? unit_interval(float(power_backing / total_power)) : 0.f;
	for(std::size_t i = 0; i < interest_group_count; ++i) {
		result.group_support[i] = group_power[i] > 0.0
			? unit_interval(float(group_backing[i] / group_power[i])) : 0.f;
	}

	if(auto const* political = cached_nation_result(state, nation); political && political->enabled) {
		float coalition_weight = 0.f;
		float opposition_weight = 0.f;
		for(std::size_t i = 0; i < interest_group_count; ++i) {
			auto const weight = political->interest_groups.groups[i].political_power_share;
			if((political->coalition.groups & (interest_group_mask(1u) << i)) != 0) {
				result.coalition_support += weight * result.group_support[i];
				coalition_weight += weight;
			} else {
				result.opposition_support += weight * result.group_support[i];
				opposition_weight += weight;
			}
		}
		result.coalition_support = coalition_weight > 0.f
			? unit_interval(result.coalition_support / coalition_weight) : 0.f;
		result.opposition_support = opposition_weight > 0.f
			? unit_interval(result.opposition_support / opposition_weight) : 0.f;
	}
	return result;
}

issue_support_result evaluate_reform_support(sys::state& state,
	dcon::nation_id nation, dcon::reform_option_id option) {
	issue_support_result result;
	auto const config = ruleset_config_for(state);
	if(!config.enabled || !nation || !option || !state.world.nation_is_valid(nation)
		|| !state.world.reform_option_is_valid(option))
		return result;
	result.enabled = true;
	auto const parent = state.world.reform_option_get_parent_reform(option);
	if(!parent)
		return result;
	auto const type = state.world.reform_get_reform_type(parent.id);
	bool const military = type == uint8_t(culture::issue_category::military);
	std::array<double, interest_group_count> group_backing{};
	std::array<double, interest_group_count> group_power{};
	double supported_population = 0.0;
	double population = 0.0;
	double power_backing = 0.0;
	double total_power = 0.0;

	state.world.nation_for_each_province_ownership(nation, [&](dcon::province_ownership_id ownership) {
		auto const province = state.world.province_ownership_get_province(ownership);
		state.world.province_for_each_pop_location(province, [&](dcon::pop_location_id location) {
			auto const pop = state.world.pop_location_get_pop(location);
			auto const sample = sample_from_pop(state, pop);
			if(sample.population <= 0.0f)
				return;
			auto const affinity = affinity_for_role(sample.role);
			auto const per_capita_power = power_per_capita(sample, config,
				wealth_signal(sample.savings_per_capita, config));
			auto const pop_power = double(sample.population) * double(per_capita_power);
			float support = 0.5f;
			for(auto ideology : state.world.in_ideology) {
				auto const ideological_weight = unit_interval(pop_demographics::get_demo(
					state, pop, pop_demographics::to_key(state, ideology.id)));
				auto const condition = military
					? state.world.ideology_get_add_military_reform(ideology.id)
					: state.world.ideology_get_add_economic_reform(ideology.id);
				float ideology_support = 0.5f;
				if(condition) {
					auto const modifier = std::clamp(trigger::evaluate_additive_modifier(
						state, condition, trigger::to_generic(nation), trigger::to_generic(nation), 0), -1.0f, 1.0f);
					ideology_support = 0.5f + 0.5f * modifier;
				}
				support += ideological_weight * (ideology_support - 0.5f);
			}
			support = unit_interval(support);
			supported_population += double(sample.population) * double(support);
			population += double(sample.population);
			power_backing += pop_power * double(support);
			total_power += pop_power;
			for(std::size_t index = 0; index < interest_group_count; ++index) {
				auto const affinity_power = pop_power * double(affinity[index]);
				group_power[index] += affinity_power;
				group_backing[index] += affinity_power * double(support);
			}
		});
	});

	result.popular_support = population > 0.0
		? unit_interval(float(supported_population / population)) : 0.0f;
	result.electoral_support = result.popular_support;
	result.political_power_support = total_power > 0.0
		? unit_interval(float(power_backing / total_power)) : 0.0f;
	for(std::size_t index = 0; index < interest_group_count; ++index)
		result.group_support[index] = group_power[index] > 0.0
			? unit_interval(float(group_backing[index] / group_power[index])) : 0.0f;

	if(auto const* political = cached_nation_result(state, nation); political && political->enabled) {
		float coalition_weight = 0.0f;
		float opposition_weight = 0.0f;
		for(std::size_t index = 0; index < interest_group_count; ++index) {
			auto const weight = political->interest_groups.groups[index].political_power_share;
			if((political->coalition.groups & (interest_group_mask(1u) << index)) != 0) {
				result.coalition_support += weight * result.group_support[index];
				coalition_weight += weight;
			} else {
				result.opposition_support += weight * result.group_support[index];
				opposition_weight += weight;
			}
		}
		result.coalition_support = coalition_weight > 0.0f
			? unit_interval(result.coalition_support / coalition_weight) : 0.0f;
		result.opposition_support = opposition_weight > 0.0f
			? unit_interval(result.opposition_support / opposition_weight) : 0.0f;
	}
	return result;
}

movement_pressure_breakdown calculate_movement_pressure(movement_pressure_inputs inputs) {
	movement_pressure_breakdown result;
	if(!inputs.enabled)
		return result;
	result.enabled = true;
	auto const power = unit_interval(inputs.political_power_support);
	auto const coalition = unit_interval(inputs.coalition_support);
	auto const legitimacy = unit_interval(inputs.legitimacy);
	auto const hardship = unit_interval(inputs.economic_hardship);
	auto const implementation_gap = unit_interval(inputs.implementation_gap);
	auto const regional_control = unit_interval(inputs.regional_control);
	auto const regional_execution = unit_interval(inputs.regional_execution);
	result.political_power_pressure = 18.f * power;
	result.coalition_opposition_pressure = 12.f * (1.f - coalition) * power;
	result.legitimacy_pressure = 12.f * (1.f - legitimacy);
	result.hardship_pressure = 25.f * hardship;
	result.implementation_pressure = 25.f * implementation_gap;
	result.regional_control_pressure = 10.f * (1.f - regional_control);
	result.regional_implementation_pressure = 10.f * (1.f - regional_execution);
	result.legitimacy_relief = 12.f * legitimacy * coalition;
	result.total_adjustment = std::clamp(
		result.political_power_pressure + result.coalition_opposition_pressure
		+ result.legitimacy_pressure + result.hardship_pressure
		+ result.implementation_pressure + result.regional_control_pressure
		+ result.regional_implementation_pressure - result.legitimacy_relief,
		-15.f, 60.f);
	return result;
}

movement_pressure_breakdown movement_pressure_for(sys::state& state,
	dcon::movement_id movement) {
	movement_pressure_inputs inputs;
	if(!ruleset_config_for(state).enabled || !movement || !state.world.movement_is_valid(movement))
		return calculate_movement_pressure(inputs);
	inputs.enabled = true;
	auto const nation = state.world.movement_get_nation_from_movement_within(movement);
	auto const option = state.world.movement_get_associated_issue_option(movement);
	if(option) {
		auto const support = evaluate_issue_support(state, nation, option);
		inputs.political_power_support = support.political_power_support;
		inputs.coalition_support = support.coalition_support;
	}
	if(auto const* political = cached_nation_result(state, nation); political && political->enabled)
		inputs.legitimacy = unit_interval(political->legitimacy.total / 100.f);

	auto const policy = option
		? nations::policy_execution::policy_kind::reform_implementation
		: nations::policy_execution::policy_kind::crime_suppression;
	double hardship = 0.0;
	double population = 0.0;
	double regional_control = 0.0;
	double regional_execution = 0.0;
	for(auto membership : state.world.movement_get_pop_movement_membership(movement)) {
		auto const pop = membership.get_pop().id;
		auto const size = nonnegative(state.world.pop_get_size(pop));
		auto const needs =
			0.7f * unit_interval(pop_demographics::get_life_needs(state, pop))
			+ 0.3f * unit_interval(pop_demographics::get_everyday_needs(state, pop));
		hardship += double(size) * double(1.f - needs);
		population += double(size);
		if(auto const province = state.world.pop_get_province_from_pop_location(pop); province) {
			regional_control += double(size) * double(unit_interval(
				state.world.province_get_control_ratio(province)));
			regional_execution += double(size) * double(
				nations::policy_execution::effective_policy(state, nation, province, policy).effective_execution);
		}
	}
	inputs.economic_hardship = population > 0.0 ? unit_interval(float(hardship / population)) : 0.f;
	inputs.implementation_gap = 1.f - nations::policy_execution::average_effective_policy(
		state, nation, policy);
	inputs.regional_control = population > 0.0
		? unit_interval(float(regional_control / population)) : 1.0f;
	inputs.regional_execution = population > 0.0
		? unit_interval(float(regional_execution / population)) : 1.0f;
	return calculate_movement_pressure(inputs);
}

concession_pressure_result calculate_concession_pressure(
	concession_pressure_inputs inputs) {
	concession_pressure_result result;
	if(!inputs.enabled)
		return result;
	result.enabled = true;
	// A movement representing roughly one eighth of the population has full
	// demographic leverage. Radicalism matters, but cannot manufacture a mass
	// constituency on its own.
	auto const movement_mass = unit_interval(inputs.matching_movement_population * 8.0f);
	result.movement_pressure = unit_interval(
		0.65f * movement_mass
		+ 0.35f * movement_mass * unit_interval(inputs.matching_movement_radicalism));
	// Once conflict has become an uprising, membership and territorial control
	// are the two observable sources of general emergency pressure.
	auto const rebel_mass = unit_interval(inputs.rebel_population * 5.0f);
	auto const occupation = unit_interval(inputs.rebel_occupation * 4.0f);
	result.rebellion_pressure = unit_interval(0.60f * rebel_mass + 0.40f * occupation);
	result.total = std::max(result.movement_pressure, result.rebellion_pressure);
	return result;
}

concession_pressure_result concession_pressure_for(sys::state& state,
	dcon::nation_id nation, dcon::issue_option_id option) {
	concession_pressure_inputs inputs;
	if(!ruleset_config_for(state).enabled || !nation || !option
		|| !state.world.nation_is_valid(nation)
		|| !state.world.issue_option_is_valid(option))
		return calculate_concession_pressure(inputs);
	inputs.enabled = true;

	auto const demographics_size = state.world.nation_get_demographics_size();
	auto const population = std::max(1.0f,
		demographics_size > uint32_t(demographics::total.index())
			? state.world.nation_get_demographics(nation, demographics::total)
			: 0.0f);
	for(auto membership : state.world.nation_get_movement_within(nation)) {
		auto const movement = membership.get_movement().id;
		if(state.world.movement_get_associated_issue_option(movement) != option)
			continue;
		inputs.matching_movement_population +=
			state.world.movement_get_pop_support(movement) / population;
		inputs.matching_movement_radicalism = std::max(
			inputs.matching_movement_radicalism,
			state.world.movement_get_radicalism(movement) / 100.0f);
	}

	double rebel_population = 0.0;
	for(auto rebellion : state.world.nation_get_rebellion_within(nation)) {
		for(auto member : rebellion.get_rebels().get_pop_rebellion_membership())
			rebel_population += nonnegative(member.get_pop().get_size());
	}
	inputs.rebel_population = unit_interval(float(rebel_population / double(population)));

	float owned = 0.0f;
	float occupied = 0.0f;
	for(auto ownership : state.world.nation_get_province_ownership(nation)) {
		++owned;
		if(ownership.get_province().get_rebel_faction_from_province_rebel_control())
			++occupied;
	}
	inputs.rebel_occupation = owned > 0.0f ? occupied / owned : 0.0f;
	// General civil disorder can force liberalisation, but cannot be farmed to
	// roll rights backwards. A movement explicitly demanding a rollback still
	// retains its own issue-specific leverage.
	auto const parent = state.world.issue_option_get_parent_issue(option);
	auto const current = parent ? state.world.nation_get_issues(nation, parent.id).id
		: dcon::issue_option_id{};
	if(current && option.index() <= current.index()) {
		inputs.rebel_population = 0.0f;
		inputs.rebel_occupation = 0.0f;
	}
	return calculate_concession_pressure(inputs);
}

void record_reform_outcome(sys::state& state,
	dcon::nation_id nation, dcon::issue_option_id option) {
	if(!ruleset_config_for(state).enabled || !nation || !option
		|| !state.world.nation_is_valid(nation))
		return;

	auto const* political = cached_nation_result(state, nation);
	if(!political || !political->enabled || political->government.groups == 0)
		return;
	if(nation.index() >= state.transformation_government_state.size())
		return;

	auto const support = evaluate_issue_support(state, nation, option);
	auto const representation_gap = std::max(
		0.0f, support.political_power_support - support.electoral_support);
	auto& government = state.transformation_government_state[nation.index()];
	for(std::size_t index = 0; index < interest_group_count; ++index) {
		if((government.groups & (interest_group_mask(1u) << index)) == 0)
			continue;
		// A negotiated reform changes confidence gradually. The electoral gap
		// is a cabinet-wide cost; group support determines who feels heard.
		auto const alignment = 2.0f * unit_interval(support.group_support[index]) - 1.0f;
		auto const delta = 0.025f * alignment
			+ 0.010f * (1.0f - representation_gap);
		government.confidence[index] = std::clamp(
			finite_or(government.confidence[index], 0.5f) + delta, 0.0f, 1.0f);
	}

	if(state.transformation_politics_cache_valid
		&& nation.index() < state.transformation_politics_cache.size()) {
		auto& result = state.transformation_politics_cache[nation.index()];
		result.government.confidence = government.confidence;
		float confidence_sum = 0.0f;
		uint32_t confidence_count = 0;
		for(std::size_t index = 0; index < interest_group_count; ++index) {
			if((government.groups & (interest_group_mask(1u) << index)) == 0)
				continue;
			confidence_sum += government.confidence[index];
			++confidence_count;
		}
		result.government.stability = confidence_count > 0
			? confidence_sum / float(confidence_count) : 0.0f;
	}
}

void record_reform_outcome(sys::state& state,
	dcon::nation_id nation, dcon::reform_option_id option) {
	if(!ruleset_config_for(state).enabled || !nation || !option
		|| !state.world.nation_is_valid(nation))
		return;
	auto const* political = cached_nation_result(state, nation);
	if(!political || !political->enabled || political->government.groups == 0)
		return;
	if(nation.index() >= state.transformation_government_state.size())
		return;
	auto const support = evaluate_reform_support(state, nation, option);
	auto& government = state.transformation_government_state[nation.index()];
	for(std::size_t index = 0; index < interest_group_count; ++index) {
		if((government.groups & (interest_group_mask(1u) << index)) == 0)
			continue;
		auto const alignment = 2.0f * unit_interval(support.group_support[index]) - 1.0f;
		government.confidence[index] = std::clamp(
			finite_or(government.confidence[index], 0.5f)
				+ 0.025f * alignment + 0.010f * support.political_power_support,
			0.0f, 1.0f);
	}
	if(state.transformation_politics_cache_valid
		&& nation.index() < state.transformation_politics_cache.size()) {
		auto& result = state.transformation_politics_cache[nation.index()];
		result.government.confidence = government.confidence;
		float confidence_sum = 0.0f;
		uint32_t confidence_count = 0;
		for(std::size_t index = 0; index < interest_group_count; ++index) {
			if((government.groups & (interest_group_mask(1u) << index)) == 0)
				continue;
			confidence_sum += government.confidence[index];
			++confidence_count;
		}
		result.government.stability = confidence_count > 0
			? confidence_sum / float(confidence_count) : 0.0f;
	}
}

void record_ruling_party_change(sys::state& state, dcon::nation_id nation,
	dcon::political_party_id old_party, dcon::political_party_id new_party,
		float election_mandate) {
	if(!ruleset_config_for(state).enabled || !nation
		|| !state.world.nation_is_valid(nation)
		|| (old_party == new_party && election_mandate < 0.0f))
		return;
	auto const demographics_size = state.world.nation_get_demographics_size();
	auto const population = demographics_size > uint32_t(demographics::total.index())
		? state.world.nation_get_demographics(nation, demographics::total) : 0.0f;
	auto party_mandate = [&](dcon::political_party_id party) {
		if(!party || population <= 0.0f)
			return 0.5f;
		auto const ideology = state.world.political_party_get_ideology(party);
		if(!ideology)
			return 0.5f;
		auto const key = demographics::to_key(state, ideology);
		if(key.index() < 0 || uint32_t(key.index()) >= demographics_size)
			return 0.5f;
		return unit_interval(
			state.world.nation_get_demographics(nation, key) / population);
	};
	auto const stored_mandate = nation.index() < state.transformation_government_state.size()
		? state.transformation_government_state[nation.index()].party_mandate : -1.0f;
	auto const old_mandate = stored_mandate >= 0.0f
		? unit_interval(stored_mandate) : party_mandate(old_party);
	auto const new_mandate = election_mandate >= 0.0f
		? unit_interval(election_mandate) : party_mandate(new_party);
	if(nation.index() < state.transformation_government_state.size()) {
		auto& government = state.transformation_government_state[nation.index()];
		government.party_mandate = new_mandate;
		auto const delta = 0.12f * (new_mandate - old_mandate)
			- (new_mandate < 0.30f ? 0.04f : 0.0f);
		for(std::size_t index = 0; index < interest_group_count; ++index) {
			if((government.groups & (interest_group_mask(1u) << index)) == 0)
				continue;
			government.confidence[index] = std::clamp(
				finite_or(government.confidence[index], 0.5f) + delta, 0.0f, 1.0f);
		}
	}
	// The party switch changes both party-specific issues and the electoral
	// mandate. Rebuild the derived coalition view before the next decision.
	invalidate_cache(state);
}

legislation_state const* active_bill(sys::state const& state, dcon::nation_id nation) {
	if(!nation || nation.index() >= state.transformation_legislation_state.size())
		return nullptr;
	auto const& bill = state.transformation_legislation_state[nation.index()];
	return bill.active ? &bill : nullptr;
}

legislation_progress bill_progress_for(sys::state& state, dcon::nation_id nation) {
	legislation_progress result;
	auto const* bill = active_bill(state, nation);
	if(!bill)
		return result;
	result.active = true;
	result.concession_pressure = bill->target == legislation_target::issue
		? concession_pressure_for(state, nation, bill->option).total : 0.0f;
	auto const negotiated_coalition = unit_interval(
		0.60f * unit_interval(bill->coalition_support)
		+ 0.20f * unit_interval(bill->party_support)
		+ 0.20f * unit_interval(bill->compromise));
	result.effective_coalition_support = std::max(negotiated_coalition,
		0.35f + 0.30f * result.concession_pressure);

	switch(bill->stage) {
	case legislation_stage::negotiation:
		result.minimum_stage_days = result.concession_pressure >= 0.60f ? 15 : 30;
		result.maximum_stage_days = 90;
		result.required_mandate = 0.42f;
		result.required_coalition_support = 0.42f;
		break;
	case legislation_stage::voting:
		result.minimum_stage_days = 15;
		result.maximum_stage_days = 15;
		result.required_mandate = 0.50f;
		result.required_coalition_support = 0.50f;
		break;
	case legislation_stage::implementation:
		result.minimum_stage_days = 30;
		result.maximum_stage_days = 30;
		result.required_mandate = 0.50f;
		result.required_execution = 0.30f;
		break;
	case legislation_stage::none:
	default:
		break;
	}
	result.time_ready = bill->stage_days >= result.minimum_stage_days;
	result.mandate_ready = bill->mandate + comparison_epsilon >= result.required_mandate;
	result.coalition_ready = result.required_coalition_support <= 0.0f
		|| result.effective_coalition_support + comparison_epsilon >= result.required_coalition_support;
	result.execution_ready = result.required_execution <= 0.0f
		|| bill->execution + comparison_epsilon >= result.required_execution;
	return result;
}

bool can_propose_bill(sys::state& state,
	dcon::nation_id nation, dcon::issue_option_id option) {
	if(!ruleset_config_for(state).enabled || !nation || !option
		|| !state.world.nation_is_valid(nation)
		|| !state.world.issue_option_is_valid(option)
		|| active_bill(state, nation))
		return false;

	auto const issue = state.world.issue_option_get_parent_issue(option);
	if(!issue)
		return false;
	auto const current = state.world.nation_get_issues(nation, issue.id).id;
	if(current == option)
		return false;
	auto const last_change = state.world.nation_get_last_issue_or_reform_change(nation);
	if(last_change && last_change + int32_t(state.defines.min_delay_between_reforms * 30) > state.current_date)
		return false;
	if(state.world.issue_get_is_next_step_only(issue.id)
		&& current && current.index() + 1 != option.index()
		&& current.index() - 1 != option.index())
		return false;
	auto const allow = state.world.issue_option_get_allow(option);
	return !allow || trigger::evaluate(state, allow,
		trigger::to_generic(nation), trigger::to_generic(nation), 0);
}

void propose_bill(sys::state& state,
	dcon::nation_id nation, dcon::issue_option_id option) {
	// Keep the legacy local-player reform cheat useful while retaining the
	// normal validation for every other actor. The bill itself still has to
	// point at a real, different option and an empty slot.
	bool const local_reform_cheat = nation == state.local_player_nation
		&& state.cheat_data.always_allow_reforms;
	if(!local_reform_cheat && !can_propose_bill(state, nation, option))
		return;
	if(!nation || !option || !state.world.nation_is_valid(nation)
		|| !state.world.issue_option_is_valid(option) || active_bill(state, nation))
		return;
	auto const issue = state.world.issue_option_get_parent_issue(option);
	if(!issue || state.world.nation_get_issues(nation, issue.id).id == option)
		return;
	state.transformation_legislation_state.resize(state.world.nation_size());
	auto& bill = state.transformation_legislation_state[nation.index()];
	bill = legislation_state{};
	bill.active = true;
	bill.target = legislation_target::issue;
	bill.option = option;
	bill.sponsor = state.world.nation_get_ruling_party(nation);
	bill.stage = legislation_stage::negotiation;
	bill.proposed_on = state.current_date.to_raw_value();
}

bool can_propose_bill(sys::state& state,
	dcon::nation_id nation, dcon::reform_option_id option) {
	if(!ruleset_config_for(state).enabled || !nation || !option
		|| !state.world.nation_is_valid(nation)
		|| !state.world.reform_option_is_valid(option)
		|| active_bill(state, nation))
		return false;
	auto const parent = state.world.reform_option_get_parent_reform(option);
	if(!parent)
		return false;
	auto const current = state.world.nation_get_reforms(nation, parent.id).id;
	if(current == option || option.index() <= current.index())
		return false;
	if(state.world.reform_get_is_next_step_only(parent.id)
		&& current && current.index() + 1 != option.index())
		return false;
	if(state.world.nation_get_last_issue_or_reform_change(nation)
		&& state.world.nation_get_last_issue_or_reform_change(nation)
			+ int32_t(state.defines.min_delay_between_reforms * 30) > state.current_date)
		return false;
	auto const military = state.world.reform_get_reform_type(parent.id)
		== uint8_t(culture::issue_category::military);
	return military
		? politics::can_enact_military_reform(state, nation, option)
		: politics::can_enact_economic_reform(state, nation, option);
}

void propose_bill(sys::state& state,
	dcon::nation_id nation, dcon::reform_option_id option) {
	bool const local_reform_cheat = nation == state.local_player_nation
		&& state.cheat_data.always_allow_reforms;
	if(!local_reform_cheat && !can_propose_bill(state, nation, option))
		return;
	if(!nation || !option || !state.world.nation_is_valid(nation)
		|| !state.world.reform_option_is_valid(option) || active_bill(state, nation))
		return;
	auto const parent = state.world.reform_option_get_parent_reform(option);
	if(!parent || state.world.nation_get_reforms(nation, parent.id).id == option)
		return;
	state.transformation_legislation_state.resize(state.world.nation_size());
	auto& bill = state.transformation_legislation_state[nation.index()];
	bill = legislation_state{};
	bill.active = true;
	bill.target = legislation_target::reform;
	bill.reform = option;
	bill.sponsor = state.world.nation_get_ruling_party(nation);
	bill.stage = legislation_stage::negotiation;
	bill.proposed_on = state.current_date.to_raw_value();
}

bool can_withdraw_bill(sys::state& state, dcon::nation_id nation) {
	if(!ruleset_config_for(state).enabled || !nation
		|| !state.world.nation_is_valid(nation))
		return false;
	auto const* bill = active_bill(state, nation);
	return bill && bill->stage != legislation_stage::implementation;
}

void withdraw_bill(sys::state& state, dcon::nation_id nation) {
	if(!can_withdraw_bill(state, nation))
		return;
	// Withdrawing is a small confidence cost: it has a political consequence
	// without punishing a player for changing their mind during negotiation.
	if(nation.index() < state.transformation_government_state.size()) {
		auto& government = state.transformation_government_state[nation.index()];
		for(std::size_t index = 0; index < interest_group_count; ++index) {
			if((government.groups & (interest_group_mask(1u) << index)) == 0)
				continue;
			government.confidence[index] = std::clamp(
				finite_or(government.confidence[index], 0.5f) - 0.01f, 0.0f, 1.0f);
		}
	}
	state.transformation_legislation_state[nation.index()] = legislation_state{};
}

namespace {

float ruling_party_bill_support(sys::state& state, dcon::nation_id nation,
	dcon::issue_id issue, dcon::issue_option_id option) {
	if(!issue || !option)
		return 0.5f;
	auto const party = state.world.nation_get_ruling_party(nation);
	if(!party)
		return 0.5f;
	auto const party_option = state.world.political_party_get_party_issues(party, issue);
	if(!party_option)
		return 0.5f;
	if(party_option == option)
		return 1.0f;
	// For stepwise issues, a party backing the same direction is still a
	// negotiable ally. A party backing the opposite direction is an explicit
	// source of resistance.
	auto const current = state.world.nation_get_issues(nation, issue).id;
	if(state.world.issue_get_is_next_step_only(issue)
		&& current
		&& ((party_option.id.index() > current.index()) == (option.index() > current.index())))
		return 0.65f;
	return 0.15f;
}

float ruling_party_bill_support(sys::state& state, dcon::nation_id nation,
	dcon::reform_option_id option) {
	if(!option)
		return 0.5f;
	auto const party = state.world.nation_get_ruling_party(nation);
	auto const parent = state.world.reform_option_get_parent_reform(option);
	if(!party || !parent)
		return 0.5f;
	auto const ideology = state.world.political_party_get_ideology(party);
	auto const military = state.world.reform_get_reform_type(parent.id)
		== uint8_t(culture::issue_category::military);
	auto const condition = military
		? state.world.ideology_get_add_military_reform(ideology)
		: state.world.ideology_get_add_economic_reform(ideology);
	if(!condition)
		return 0.5f;
	auto const modifier = std::clamp(trigger::evaluate_additive_modifier(
		state, condition, trigger::to_generic(nation), trigger::to_generic(nation), 0), -1.0f, 1.0f);
	return unit_interval(0.5f + 0.5f * modifier);
}

void fail_bill(sys::state& state, dcon::nation_id nation, float coalition_support) {
	if(nation.index() < state.transformation_government_state.size()) {
		auto& government = state.transformation_government_state[nation.index()];
		for(std::size_t index = 0; index < interest_group_count; ++index) {
			if((government.groups & (interest_group_mask(1u) << index)) == 0)
				continue;
			government.confidence[index] = std::clamp(
				finite_or(government.confidence[index], 0.5f)
					- 0.04f * (1.0f - unit_interval(coalition_support)),
				0.0f, 1.0f);
		}
	}
	if(nation.index() < state.transformation_legislation_state.size())
		state.transformation_legislation_state[nation.index()] = legislation_state{};
}

void settle_issue_unrest(sys::state& state, dcon::nation_id nation,
	dcon::issue_option_id option, float concession_pressure) {
	auto const pressure = unit_interval(concession_pressure);
	if(pressure <= 0.0f)
		return;
	auto const key = pop_demographics::to_key(state, option);
	std::vector<dcon::pop_id> populations;
	for(auto ownership : state.world.nation_get_province_ownership(nation))
		for(auto location : ownership.get_province().get_pop_location())
			populations.push_back(location.get_pop().id);
	for(auto pop : populations) {
		auto const support = unit_interval(pop_demographics::get_demo(state, pop, key));
		// Everyone receives a small de-escalation signal; the constituency that
		// actually demanded the settlement receives most of the relief.
		auto const relief = pressure * (0.35f + 1.65f * support);
		pop_demographics::set_militancy(state, pop,
			std::max(0.0f, pop_demographics::get_militancy(state, pop) - relief));
		// A concession removes political supporters from a rebel recruitment
		// pool, but does not despawn armies already in the field.
		if(state.world.pop_get_rebel_faction_from_pop_rebellion_membership(pop)
			&& pressure * support >= 0.15f)
			rebel::remove_pop_from_rebel_faction(state, pop);
	}
}

} // namespace

void advance_legislation(sys::state& state) {
	if(!ruleset_config_for(state).enabled)
		return;
	state.transformation_legislation_state.resize(state.world.nation_size());
	for(auto nation : state.world.in_nation) {
		auto& bill = state.transformation_legislation_state[nation.id.index()];
		if(!bill.active)
			continue;
		issue_support_result support;
		dcon::issue_id issue{};
		if(bill.target == legislation_target::issue) {
			if(!bill.option || !state.world.issue_option_is_valid(bill.option)) {
				bill = legislation_state{};
				continue;
			}
			auto const issue_fat = state.world.issue_option_get_parent_issue(bill.option);
			if(!issue_fat || state.world.nation_get_issues(nation.id, issue_fat.id).id == bill.option) {
				bill = legislation_state{};
				continue;
			}
			issue = issue_fat.id;
			support = evaluate_issue_support(state, nation.id, bill.option);
			bill.party_support = ruling_party_bill_support(state, nation.id, issue, bill.option);
		} else if(bill.target == legislation_target::reform) {
			if(!bill.reform || !state.world.reform_option_is_valid(bill.reform)) {
				bill = legislation_state{};
				continue;
			}
			auto const reform = state.world.reform_option_get_parent_reform(bill.reform);
			if(!reform || state.world.nation_get_reforms(nation.id, reform.id).id == bill.reform) {
				bill = legislation_state{};
				continue;
			}
			support = evaluate_reform_support(state, nation.id, bill.reform);
			bill.party_support = ruling_party_bill_support(state, nation.id, bill.reform);
		} else {
			bill = legislation_state{};
			continue;
		}
		auto const execution = nations::policy_execution::average_effective_policy(
			state, nation.id, nations::policy_execution::policy_kind::reform_implementation);
		bill.coalition_support = unit_interval(support.coalition_support);
		bill.execution = unit_interval(execution);
		auto const negotiated_coalition = unit_interval(
			0.60f * bill.coalition_support
			+ 0.20f * unit_interval(bill.party_support)
			+ 0.20f * unit_interval(bill.compromise));
		auto const concession_pressure = bill.target == legislation_target::issue
			? concession_pressure_for(state, nation.id, bill.option).total : 0.0f;
		// Strong civil pressure creates an emergency floor for both the mandate
		// and the negotiated coalition. It does not count as ordinary support and
		// only reaches the 50% passage threshold during a genuine crisis.
		auto const ordinary_mandate = unit_interval(
			0.40f * support.political_power_support
			+ 0.30f * negotiated_coalition
			+ 0.15f * support.electoral_support
			+ 0.15f * support.popular_support);
		bill.mandate = std::max(ordinary_mandate,
			0.35f + 0.30f * concession_pressure);
		bill.stage_days = uint16_t(std::min<uint32_t>(65535u, uint32_t(bill.stage_days) + 1u));
		auto const progress = bill_progress_for(state, nation.id);

		switch(bill.stage) {
		case legislation_stage::negotiation:
			if(progress.time_ready && progress.mandate_ready && progress.coalition_ready) {
				bill.stage = legislation_stage::voting;
				bill.stage_days = 0;
			} else if(bill.stage_days >= progress.maximum_stage_days) {
				fail_bill(state, nation.id, bill.coalition_support);
			} else if(bill.stage_days % 15 == 0) {
				// Compromise is the explicit negotiation resource. It can rescue
				// a bill with a narrow coalition, but never replaces popular or
				// political-power support entirely.
				bill.compromise = std::clamp(bill.compromise + 0.05f, 0.0f, 0.75f);
			}
			break;
		case legislation_stage::voting:
			if(!progress.time_ready)
				break;
			if(progress.mandate_ready && progress.coalition_ready) {
				bill.stage = legislation_stage::implementation;
				bill.stage_days = 0;
			} else {
				fail_bill(state, nation.id, bill.coalition_support);
			}
			break;
		case legislation_stage::implementation:
			if(!progress.time_ready)
				break;
			if(progress.execution_ready && progress.mandate_ready) {
				if(bill.target == legislation_target::issue) {
					auto const option = bill.option;
					auto const settlement_pressure = concession_pressure;
					bill = legislation_state{};
					nations::enact_issue(state, nation.id, option);
					record_reform_outcome(state, nation.id, option);
					settle_issue_unrest(state, nation.id, option, settlement_pressure);
				} else {
					auto const option = bill.reform;
					auto const parent = state.world.reform_option_get_parent_reform(option);
					auto const military = parent && state.world.reform_get_reform_type(parent.id)
						== uint8_t(culture::issue_category::military);
					bool const affordable = military
						? politics::can_enact_military_reform(state, nation.id, option)
						: politics::can_enact_economic_reform(state, nation.id, option);
					if(!affordable) {
						fail_bill(state, nation.id, bill.coalition_support);
					} else {
						bill = legislation_state{};
						nations::enact_reform(state, nation.id, option);
						record_reform_outcome(state, nation.id, option);
					}
				}
			} else {
				fail_bill(state, nation.id, bill.coalition_support);
			}
			break;
		case legislation_stage::none:
		default:
			bill = legislation_state{};
			break;
		}
	}
}

ruleset_config ruleset_config_for(sys::state const& state) {
	ruleset_config result;
	result.enabled = gamerule::age_of_transformation_enabled(state);
	return result;
}

void invalidate_cache(sys::state& state) {
	state.transformation_politics_cache.clear();
	state.transformation_politics_cache_valid = false;
}

void refresh_all_nations(sys::state& state) {
	auto const config = ruleset_config_for(state);
	auto const nation_count = state.world.nation_size();
	state.transformation_politics_cache.assign(nation_count, nation_result{});
	state.transformation_government_state.resize(nation_count);

	if(!config.enabled) {
		state.transformation_politics_cache_valid = true;
		return;
	}

	for(auto nation : state.world.in_nation) {
		auto const index = nation.id.index();
		auto& government = state.transformation_government_state[index];
		auto result = evaluate_nation(state, nation.id, config, government.groups);
		bool confidence_crisis_turnover = false;
		float incumbent_confidence = 0.0f;
		uint32_t incumbent_members = 0;
		for(std::size_t group = 0; group < interest_group_count; ++group) {
			if((government.groups & (interest_group_mask(1u) << group)) != 0) {
				incumbent_confidence += finite_or(government.confidence[group], 0.0f);
				++incumbent_members;
			}
		}
		// Hysteresis protects a normal incumbent from monthly noise. It should
		// not protect a cabinet whose own members have lost nearly all trust:
		// a credible challenger can then force a confidence crisis and turnover.
		if(government.groups != 0 && incumbent_members > 0
			&& (incumbent_confidence / float(incumbent_members) < 0.25f
				|| (result.government.party_mandate > 0.0f
					&& result.government.party_mandate < 0.25f))
			&& result.coalition.best_challenger != 0
			&& result.coalition.best_challenger_score
				> result.coalition.incumbent_score + comparison_epsilon) {
			government.groups = result.coalition.best_challenger;
			confidence_crisis_turnover = true;
			result = evaluate_nation(state, nation.id, config, government.groups);
		}

		// First observation establishes a government. Later refreshes preserve
		// the incumbent through the coalition selector's hysteresis and record a
		// turnover only when a challenger genuinely wins.
		if(confidence_crisis_turnover || government.groups == 0 || result.coalition.groups != government.groups) {
			government.groups = result.coalition.groups;
			government.established_on = state.current_date.to_raw_value();
			for(std::size_t group = 0; group < interest_group_count; ++group) {
				auto const in_government = (government.groups & (interest_group_mask(1u) << group)) != 0;
				government.confidence[group] = in_government ? 0.65f : 0.35f;
			}
			result.government.changed_this_refresh = true;
		} else {
			for(std::size_t group = 0; group < interest_group_count; ++group) {
				auto const in_government = (government.groups & (interest_group_mask(1u) << group)) != 0;
				auto const support = result.interest_groups.groups[group].support_share;
				auto const security = result.interest_groups.groups[group].mean_income_security;
				auto const target = in_government
					? std::clamp(0.35f + 0.30f * support + 0.20f * result.coalition.cohesion
						+ 0.15f * unit_interval(security), 0.f, 1.f)
					: std::clamp(0.25f + 0.25f * (1.f - support), 0.f, 1.f);
				government.confidence[group] = std::clamp(
					0.90f * finite_or(government.confidence[group], target) + 0.10f * target, 0.f, 1.f);
			}
		}

		result.government.groups = government.groups;
		result.government.established_on = government.established_on;
		result.government.confidence = government.confidence;
		float confidence_sum = 0.f;
		uint32_t confidence_count = 0;
		for(std::size_t group = 0; group < interest_group_count; ++group) {
			if((government.groups & (interest_group_mask(1u) << group)) != 0) {
				confidence_sum += government.confidence[group];
				++confidence_count;
			}
		}
		result.government.stability = confidence_count > 0
			? confidence_sum / float(confidence_count) : 0.f;
		state.transformation_politics_cache[index] = std::move(result);
	}
	state.transformation_politics_cache_valid = true;
}

nation_result const* cached_nation_result(sys::state& state, dcon::nation_id nation) {
	if(!nation || !state.world.nation_is_valid(nation))
		return nullptr;
	if(!state.transformation_politics_cache_valid
		|| state.transformation_politics_cache.size() != state.world.nation_size())
		refresh_all_nations(state);
	auto const index = nation.index();
	return index < state.transformation_politics_cache.size()
		? &state.transformation_politics_cache[index] : nullptr;
}

} // namespace politics::transformation
