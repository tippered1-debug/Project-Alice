#include "labor_relations.hpp"

#include "demographics.hpp"
#include "gamerule.hpp"
#include "system_state.hpp"
#include "transformation_laws.hpp"

#include <algorithm>
#include <cmath>

namespace economy::labor_relations {
namespace {

float nonnegative(float value) {
	return std::isfinite(value) ? std::max(0.f, value) : 0.f;
}

float unit(float value) {
	return std::isfinite(value) ? std::clamp(value, 0.f, 1.f) : 0.f;
}

bool is_labor_organization_option(sys::state const& state,
		dcon::issue_option_id option) {
	if(!option || !state.world.issue_option_is_valid(option))
		return false;
	auto const name = state.world.issue_option_get_name(option);
	return name == state.lookup_key("state_controlled")
		|| name == state.lookup_key("non_socialist")
		|| name == state.lookup_key("all_trade_unions")
		|| name == state.lookup_key("alice_collective_bargaining_recognized")
		|| name == state.lookup_key("alice_collective_bargaining_protected");
}

} // namespace

float estimate_membership_share(membership_inputs raw_inputs) {
	auto const workers = nonnegative(raw_inputs.workers);
	if(workers <= 0.f)
		return 0.f;
	auto const literacy = unit(raw_inputs.literacy);
	auto const consciousness = unit(raw_inputs.consciousness);
	auto const movement_share = unit(
		nonnegative(raw_inputs.labor_movement_workers) / workers);

	float institutional_membership = 0.f;
	float ceiling = 0.f;
	switch(raw_inputs.regime) {
	case organization_regime::recognized:
		institutional_membership = 0.10f + 0.15f * literacy
			+ 0.10f * consciousness;
		ceiling = 0.60f;
		break;
	case organization_regime::protected_right:
		institutional_membership = 0.20f + 0.25f * literacy
			+ 0.15f * consciousness;
		ceiling = 0.85f;
		break;
	case organization_regime::prohibited:
		// Underground organization exists, but it is small and a reform movement
		// is only partial evidence of durable workplace membership.
		institutional_membership = 0.02f + 0.03f * literacy
			+ 0.05f * consciousness;
		ceiling = 0.15f;
		break;
	}
	auto const movement_floor = raw_inputs.regime == organization_regime::prohibited
		? movement_share * 0.50f : movement_share;
	return std::clamp(std::max(institutional_membership, movement_floor),
		0.f, ceiling);
}

result calculate(inputs raw_inputs) {
	result output;
	if(!raw_inputs.enabled)
		return output;
	output.enabled = true;
	auto const workers = nonnegative(raw_inputs.workers);
	output.membership_share = workers > 0.f
		? unit(nonnegative(raw_inputs.organized_workers) / workers) : 0.f;
	auto const literacy = unit(raw_inputs.literacy);
	auto const consciousness = unit(raw_inputs.consciousness);
	auto const leverage = unit(raw_inputs.legal_leverage);
	output.organization = unit(output.membership_share
		* (0.40f + 0.30f * literacy + 0.30f * consciousness)
		* (0.25f + 0.75f * leverage));
	auto const unemployment = 1.f - unit(raw_inputs.employment);
	auto const unmet_needs = 1.f - unit(raw_inputs.life_needs_coverage);
	output.hardship = unit(0.60f * unmet_needs + 0.40f * unemployment);
	// A strike is lost labor, not a throughput modifier. It needs organized
	// workers, hardship and militancy at the same time and is capped daily.
	output.strike_participation = std::clamp(
		output.organization * output.hardship * unit(raw_inputs.militancy) * 0.50f,
		0.f, 0.25f);
	output.labor_availability = 1.f - output.strike_participation;
	return output;
}

result evaluate_province(sys::state const& state, dcon::province_id province) {
	inputs input;
	if(!gamerule::age_of_transformation_enabled(state)
		|| !province || !state.world.province_is_valid(province))
		return calculate(input);
	auto const nation = state.world.province_get_nation_from_province_ownership(province);
	if(!nation)
		return calculate(input);
	input.enabled = true;
	auto const collective_law = politics::transformation::laws::for_nation(
		state, nation).collective_bargaining;
	auto membership_regime = organization_regime::prohibited;
	switch(collective_law) {
	case politics::transformation::laws::collective_bargaining_regime::recognized:
		membership_regime = organization_regime::recognized;
		input.legal_leverage = 0.65f;
		break;
	case politics::transformation::laws::collective_bargaining_regime::protected_right:
		membership_regime = organization_regime::protected_right;
		input.legal_leverage = 1.f;
		break;
	case politics::transformation::laws::collective_bargaining_regime::prohibited:
		input.legal_leverage = 0.f;
		break;
	}
	float labor_movement_workers = 0.f;
	double literacy = 0.0;
	double consciousness = 0.0;
	double militancy = 0.0;
	double employment = 0.0;
	double needs = 0.0;
	for(auto location : state.world.province_get_pop_location(province)) {
		auto const pop = location.get_pop().id;
		auto const type = state.world.pop_get_poptype(pop);
		if(type != state.culture_definitions.primary_factory_worker
			&& type != state.culture_definitions.secondary_factory_worker)
			continue;
		auto const size = nonnegative(state.world.pop_get_size(pop));
		if(size <= 0.f)
			continue;
		input.workers += size;
		if(auto const movement =
				state.world.pop_get_movement_from_pop_movement_membership(pop);
			movement && is_labor_organization_option(state,
				state.world.movement_get_associated_issue_option(movement)))
			labor_movement_workers += size;
		literacy += double(size) * unit(pop_demographics::get_literacy(state, pop));
		consciousness += double(size) * unit(
			pop_demographics::get_consciousness(state, pop) / 10.f);
		militancy += double(size) * unit(
			pop_demographics::get_militancy(state, pop) / 10.f);
		employment += double(nonnegative(pop_demographics::get_employment(state, pop)));
		needs += double(size) * unit(pop_demographics::get_life_needs(state, pop));
	}
	if(input.workers > 0.f) {
		input.literacy = float(literacy / input.workers);
		input.consciousness = float(consciousness / input.workers);
		input.militancy = float(militancy / input.workers);
		input.employment = float(employment / input.workers);
		input.life_needs_coverage = float(needs / input.workers);
		input.organized_workers = input.workers * estimate_membership_share({
			.workers = input.workers,
			.labor_movement_workers = labor_movement_workers,
			.literacy = input.literacy,
			.consciousness = input.consciousness,
			.regime = membership_regime,
		});
	}
	return calculate(input);
}

} // namespace economy::labor_relations
