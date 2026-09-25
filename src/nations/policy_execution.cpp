#include "policy_execution.hpp"

#include "economy_stats.hpp"
#include "demographics.hpp"
#include "gamerule.hpp"
#include "system_state.hpp"
#include "transformation_politics.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace nations::policy_execution {
namespace {

float unit(float value) {
	return std::isfinite(value) ? std::clamp(value, 0.f, 1.f) : 0.f;
}

float budget_fraction(sys::state const& state, dcon::nation_id nation, policy_kind policy) {
	auto slider = 0.f;
	switch(policy) {
	case policy_kind::education:
		slider = float(state.world.nation_get_education_spending(nation)) / 100.f;
		break;
	case policy_kind::social_benefits:
		slider = float(state.world.nation_get_social_spending(nation)) / 100.f;
		break;
	case policy_kind::mobilization_logistics:
		return unit(state.world.nation_get_effective_land_spending(nation));
	case policy_kind::crime_suppression:
	case policy_kind::reform_implementation:
		slider = float(state.world.nation_get_administrative_spending(nation)) / 100.f;
		break;
	}
	return unit(slider * unit(state.world.nation_get_spending_level(nation)));
}

} // namespace

workload workload_for(policy_kind policy) {
	switch(policy) {
	case policy_kind::crime_suppression: return {0.50f, 0.70f, 0.35f};
	case policy_kind::education: return {0.75f, 0.80f, 0.20f};
	case policy_kind::social_benefits: return {1.00f, 0.65f, 0.15f};
	case policy_kind::reform_implementation: return {0.40f, 0.50f, 1.00f};
	case policy_kind::mobilization_logistics: return {0.80f, 0.60f, 0.50f};
	}
	return {1.f, 1.f, 0.f};
}

breakdown calculate(policy_kind policy, inputs raw_inputs) {
	breakdown result;
	result.policy = policy;
	if(!raw_inputs.enabled)
		return result;

	result.enabled = true;
	result.factors = raw_inputs;
	result.factors.national_administration = unit(raw_inputs.national_administration);
	result.factors.local_control = unit(raw_inputs.local_control);
	result.factors.funding = unit(raw_inputs.funding);
	result.factors.bureaucratic_labor = unit(raw_inputs.bureaucratic_labor);
	result.factors.political_compliance = unit(raw_inputs.political_compliance);
	result.required_work = workload_for(policy);
	result.compliance_workload_multiplier = 1.f
		+ result.required_work.resistance_labor
			* (1.f - result.factors.political_compliance);
	result.cash_coverage = unit(result.factors.funding
		/ std::max(0.0001f, result.required_work.funding_required));
	result.staff_coverage = unit(
		result.factors.bureaucratic_labor * result.factors.national_administration
		/ (std::max(0.0001f, result.required_work.labor_required)
			* result.compliance_workload_multiplier));
	result.territorial_coverage = result.factors.local_control;

	using item = std::pair<capacity_factor, float>;
	std::array<item, 3> factors{{
		{capacity_factor::local_control, result.territorial_coverage},
		{capacity_factor::funding, result.cash_coverage},
		{capacity_factor::bureaucratic_labor, result.staff_coverage},
	}};
	result.bottleneck = factors.front().first;
	result.bottleneck_value = factors.front().second;
	for(auto const& factor : factors) {
		if(factor.second < result.bottleneck_value) {
			result.bottleneck = factor.first;
			result.bottleneck_value = factor.second;
		}
	}

	// Cash, staffed offices and territorial access are complementary coverages.
	// A surplus in one cannot replace a missing claimant payment or caseworker.
	result.effective_execution = result.bottleneck_value;
	return result;
}

breakdown effective_policy(sys::state const& state, dcon::nation_id nation,
	dcon::province_id province, policy_kind policy) {
	inputs derived;
	if(!gamerule::age_of_transformation_enabled(state))
		return calculate(policy, derived);
	derived.enabled = true;
	if(!nation || !province || !state.world.nation_is_valid(nation)
		|| !state.world.province_is_valid(province)) {
		derived.national_administration = 0.f;
		derived.local_control = 0.f;
		derived.funding = 0.f;
		derived.bureaucratic_labor = 0.f;
		derived.political_compliance = 0.f;
		return calculate(policy, derived);
	}

	derived.national_administration = state.world.nation_get_administrative_efficiency(nation);
	derived.local_control = state.world.province_get_control_ratio(province);
	derived.funding = budget_fraction(state, nation, policy);
	auto const population = std::max(0.0f,
		state.world.province_get_demographics(province, demographics::total));
	auto const teachers = std::max(0.0f,
		state.world.province_get_public_teacher_staffing(province));
	auto const police = std::max(0.0f,
		state.world.province_get_public_police_staffing(province));
	auto const bureaucrats = std::max(0.0f,
		state.world.province_get_public_bureaucrat_staffing(province));
	auto const education_staff = population > 0.0f
		? unit(teachers / (population * 0.001f)) : 1.0f;
	auto const police_staff = population > 0.0f
		? unit(police / (population * 0.0015f)) : 1.0f;
	auto const bureaucracy_staff = population > 0.0f
		? unit(bureaucrats / (population * 0.0005f)) : 1.0f;
	auto const high_education_labor = std::max(
		state.world.province_get_labor_demand_satisfaction(
			province, economy::labor::high_education_and_accepted),
		state.world.province_get_labor_demand_satisfaction(
			province, economy::labor::high_education));
	switch(policy) {
	case policy_kind::education:
		derived.bureaucratic_labor = std::min(high_education_labor, education_staff);
		break;
	case policy_kind::crime_suppression:
		derived.bureaucratic_labor = std::min(
			state.world.province_get_labor_demand_satisfaction(province, economy::labor::no_education),
			police_staff);
		break;
	case policy_kind::social_benefits:
	case policy_kind::reform_implementation:
		derived.bureaucratic_labor = std::min(high_education_labor, bureaucracy_staff);
		break;
	case policy_kind::mobilization_logistics:
		derived.bureaucratic_labor = high_education_labor;
		break;
	}
	derived.political_compliance = 0.25f;
	auto const index = nation.index();
	if(state.transformation_politics_cache_valid
		&& index < state.transformation_politics_cache.size()
		&& state.transformation_politics_cache[index].enabled) {
		auto const* political_result = &state.transformation_politics_cache[index];
		derived.political_compliance = 0.25f + 0.75f * unit(political_result->legitimacy.total / 100.f);
		// A legitimate government can still fail to execute policy when its own
		// cabinet is falling apart. Keep the legacy-compatible fallback for
		// freshly-created synthetic states with no established cabinet.
		if(political_result->government.groups != 0)
			derived.political_compliance *= 0.70f + 0.30f * unit(political_result->government.stability);
	}
	return calculate(policy, derived);
}

float average_effective_policy(sys::state const& state, dcon::nation_id nation,
	policy_kind policy) {
	if(!gamerule::age_of_transformation_enabled(state))
		return 1.f;
	if(!nation || !state.world.nation_is_valid(nation))
		return 0.f;

	double weighted_execution = 0.0;
	double total_weight = 0.0;
	for(auto ownership : state.world.nation_get_province_ownership(nation)) {
		auto const province = ownership.get_province().id;
		auto const population = std::max(
			0.f, state.world.province_get_demographics(province, demographics::total));
		// Empty strategic provinces still consume a small administrative share.
		auto const weight = std::max(1.f, population);
		weighted_execution += double(effective_policy(state, nation, province, policy).effective_execution)
			* double(weight);
		total_weight += double(weight);
	}
	if(total_weight <= 0.0)
		return 0.f;
	return unit(float(weighted_execution / total_weight));
}

} // namespace nations::policy_execution
