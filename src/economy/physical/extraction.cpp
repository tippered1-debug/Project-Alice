#include "extraction.hpp"
#include "system_state.hpp"
#include "actors/ownership.hpp"
#include "actors/organizations/organizations.hpp"
#include "governance/governance.hpp"
#include "persons/persons.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace economy::physical::extraction {
namespace {
bool finite_nonnegative(float value) { return std::isfinite(value) && value >= 0.0f; }

bool has_owner(sys::state const& state, dcon::asset_id asset) {
	bool result = false;
	if(!asset) return false;
	state.world.asset_for_each_ownership_stake_asset_as_asset(asset, [&](dcon::ownership_stake_asset_id relation) {
		auto stake = state.world.ownership_stake_asset_get_ownership_stake(relation);
		if(state.world.ownership_stake_get_economic_actor_from_ownership_stake_owner(stake)) result = true;
	});
	return result;
}

float extracted_on(sys::state const& state, dcon::resource_deposit_id deposit, sys::date date) {
	return state.world.resource_deposit_get_last_extraction_date(deposit) == date
		? std::max(0.0f, state.world.resource_deposit_get_extracted_on_last_date(deposit)) : 0.0f;
}
}

dcon::resource_extraction_right_id create_right(sys::state& state, dcon::person_id initiator,
	dcon::office_id office, dcon::resource_deposit_id deposit, dcon::economic_actor_id holder,
	dcon::nation_id granting_nation, sys::date valid_from, sys::date valid_until,
	float max_daily_quantity, sys::date date) {
	if(!initiator || !office || !deposit || !holder || !granting_nation || valid_until < valid_from || !finite_nonnegative(max_daily_quantity) || date < valid_from || date >= valid_until || !persons::alive(state, initiator)) return {};
	auto province = state.world.site_get_province_from_site_location(
		state.world.resource_deposit_get_site_from_resource_deposit_site(deposit));
	auto institution = governance::institution_for_office(state, office);
	if(!province || !institution || governance::nation_of(state, institution) != granting_nation || state.world.province_get_nation_from_province_ownership(province) != granting_nation || !persons::authority_tenure_on_or_before(state, initiator, governance::authority_kind::license, granting_nation, date)) return {};
	// Overlapping active rights for one deposit are ambiguous. Reject them at
	// creation so extraction can select a single right deterministically.
	bool overlaps = false;
	state.world.resource_deposit_for_each_resource_extraction_right_deposit_as_resource_deposit(deposit, [&](dcon::resource_extraction_right_deposit_id relation) {
		auto existing = state.world.resource_extraction_right_deposit_get_resource_extraction_right(relation);
		if(state.world.resource_extraction_right_get_status(existing) == uint8_t(right_status::active) && valid_from < state.world.resource_extraction_right_get_valid_until(existing) && state.world.resource_extraction_right_get_valid_from(existing) < valid_until)
			overlaps = true;
	});
	if(overlaps) return {};
	auto right = state.world.create_resource_extraction_right();
	state.world.resource_extraction_right_set_valid_from(right, valid_from);
	state.world.resource_extraction_right_set_valid_until(right, valid_until);
	state.world.resource_extraction_right_set_max_daily_quantity(right, max_daily_quantity);
	state.world.resource_extraction_right_set_status(right, uint8_t(right_status::active));
	state.world.force_create_resource_extraction_right_deposit(right, deposit);
	state.world.force_create_resource_extraction_right_holder(right, holder);
	state.world.force_create_resource_extraction_right_granting_nation(right, granting_nation);
	state.world.force_create_resource_extraction_right_granting_institution(right, institution);
	return right;
}

dcon::resource_extraction_right_id active_right_for(sys::state const& state, dcon::resource_deposit_id deposit, sys::date date) {
	dcon::resource_extraction_right_id result{};
	state.world.resource_deposit_for_each_resource_extraction_right_deposit_as_resource_deposit(deposit, [&](dcon::resource_extraction_right_deposit_id relation) {
		auto right = state.world.resource_extraction_right_deposit_get_resource_extraction_right(relation);
		if(!result && state.world.resource_extraction_right_get_status(right) == uint8_t(right_status::active) && state.world.resource_extraction_right_get_valid_from(right) <= date && date < state.world.resource_extraction_right_get_valid_until(right)) result = right;
	});
	return result;
}

dcon::resource_extraction_right_id active_right_for(sys::state const& state, dcon::resource_deposit_id deposit,
	dcon::economic_actor_id holder, sys::date date) {
	dcon::resource_extraction_right_id result{};
	bool ambiguous = false;
	state.world.resource_deposit_for_each_resource_extraction_right_deposit_as_resource_deposit(deposit, [&](dcon::resource_extraction_right_deposit_id relation) {
		auto right = state.world.resource_extraction_right_deposit_get_resource_extraction_right(relation);
		if(state.world.resource_extraction_right_get_status(right) != uint8_t(right_status::active) || state.world.resource_extraction_right_get_valid_from(right) > date || date >= state.world.resource_extraction_right_get_valid_until(right) || state.world.resource_extraction_right_get_economic_actor_from_resource_extraction_right_holder(right) != holder)
			return;
		if(result) ambiguous = true;
		else if(!ambiguous) result = right;
	});
	return ambiguous ? dcon::resource_extraction_right_id{} : result;
}

bool extracts_deposit(sys::state const& state, dcon::factory_type_id type) {
	return type && state.world.factory_type_is_valid(type) && state.world.factory_type_get_extracts_deposit(type);
}

bool extracts_deposit(sys::state const& state, dcon::factory_id factory) {
	return factory && state.world.factory_is_valid(factory)
		&& extracts_deposit(state, state.world.factory_get_building_type(factory));
}

dcon::resource_deposit_id deposit_for_enterprise(sys::state const& state, dcon::factory_id factory) {
	return factory ? state.world.factory_get_resource_deposit_from_factory_resource_deposit(factory) : dcon::resource_deposit_id{};
}

dcon::factory_id enterprise_for_deposit(sys::state const& state, dcon::resource_deposit_id deposit) {
	return deposit ? state.world.resource_deposit_get_factory_from_factory_resource_deposit(deposit) : dcon::factory_id{};
}

dcon::factory_id create_enterprise(sys::state& state, dcon::resource_deposit_id deposit,
	dcon::factory_type_id type, dcon::organization_id operator_organization) {
	if(!deposit || !state.world.resource_deposit_is_valid(deposit) || enterprise_for_deposit(state, deposit)
		|| !extracts_deposit(state, type) || !operator_organization
		|| !state.world.organization_is_valid(operator_organization)) return {};
	auto commodity = state.world.resource_deposit_get_commodity(deposit);
	auto site = state.world.resource_deposit_get_site_from_resource_deposit_site(deposit);
	auto province = site ? state.world.site_get_province_from_site_location(site) : dcon::province_id{};
	auto output_amount = state.world.factory_type_get_output_amount(type);
	auto workforce = state.world.factory_type_get_base_workforce(type);
	auto grade = state.world.resource_deposit_get_grade_or_quality(deposit);
	auto capacity = state.world.resource_deposit_get_daily_extraction_capacity(deposit);
	if(!commodity || state.world.factory_type_get_output(type) != commodity || !province
		|| !std::isfinite(output_amount) || output_amount <= 0.0f || workforce <= 0
		|| !std::isfinite(grade) || grade <= 0.0f || !std::isfinite(capacity) || capacity <= 0.0f) return {};
	auto units = capacity / (output_amount * grade);
	if(!std::isfinite(units) || units <= 0.0f) return {};
	auto factory = state.world.create_factory();
	state.world.factory_set_building_type(factory, type);
	state.world.factory_set_size(factory, units * float(workforce));
	state.world.factory_set_productive_capacity(factory, units);
	state.world.factory_set_productivity_factor(factory, 1.0f);
	state.world.force_create_factory_location(factory, province);
	state.world.force_create_factory_site(factory, site);
	state.world.force_create_factory_resource_deposit(factory, deposit);
	if(!actors::organizations::bind_factory_operator(state, operator_organization, factory)) {
		state.world.delete_factory(factory);
		return {};
	}
	return factory;
}

float output_grade(sys::state const& state, dcon::factory_id factory) {
	if(!extracts_deposit(state, factory)) return 1.0f;
	auto deposit = deposit_for_enterprise(state, factory);
	auto grade = deposit ? state.world.resource_deposit_get_grade_or_quality(deposit) : 0.0f;
	return std::isfinite(grade) && grade > 0.0f ? grade : 0.0f;
}

float daily_ceiling(sys::state const& state, dcon::factory_id factory, sys::date date) {
	auto deposit = deposit_for_enterprise(state, factory);
	if(!deposit || !extracts_deposit(state, factory)
		|| state.world.resource_deposit_get_status(deposit) != uint8_t(deposit_status::active)) return 0.0f;
	auto operator_actor = actors::organizations::operator_actor_for_factory(state, factory);
	if(!operator_actor || !has_owner(state, actors::ownership::asset_for_deposit(state, deposit))) return 0.0f;
	auto limit = state.world.resource_deposit_get_daily_extraction_capacity(deposit);
	// The deposit's operator controls extraction. Anyone else needs an active
	// right, which also caps the daily quantity.
	if(actors::ownership::operator_for_deposit(state, deposit) != operator_actor) {
		auto right = active_right_for(state, deposit, operator_actor, date);
		if(!right) return 0.0f;
		limit = std::min(limit, state.world.resource_extraction_right_get_max_daily_quantity(right));
	}
	auto remaining = state.world.resource_deposit_get_remaining_recoverable_reserves(deposit);
	auto result = std::min(limit, remaining);
	return std::isfinite(result) ? std::max(0.0f, result) : 0.0f;
}

float available_today(sys::state const& state, dcon::factory_id factory, sys::date date) {
	auto ceiling = daily_ceiling(state, factory, date);
	if(ceiling <= 0.0f) return 0.0f;
	return std::max(0.0f, ceiling - extracted_on(state, deposit_for_enterprise(state, factory), date));
}

float commit(sys::state& state, dcon::factory_id factory, float quantity, sys::date date) {
	if(!std::isfinite(quantity) || quantity <= 0.0f) return 0.0f;
	auto deposit = deposit_for_enterprise(state, factory);
	auto actual = std::min(quantity, available_today(state, factory, date));
	if(!deposit || actual <= 0.0f) return 0.0f;
	auto remaining = state.world.resource_deposit_get_remaining_recoverable_reserves(deposit) - actual;
	state.world.resource_deposit_set_extracted_on_last_date(deposit, extracted_on(state, deposit, date) + actual);
	state.world.resource_deposit_set_last_extraction_date(deposit, date);
	if(remaining <= 1.0e-5f) {
		state.world.resource_deposit_set_remaining_recoverable_reserves(deposit, 0.0f);
		state.world.resource_deposit_set_status(deposit, uint8_t(deposit_status::depleted));
	} else {
		state.world.resource_deposit_set_remaining_recoverable_reserves(deposit, remaining);
	}
	return actual;
}

} // namespace economy::physical::extraction
