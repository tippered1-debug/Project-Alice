#include "extraction.hpp"
#include "inventory.hpp"
#include "system_state.hpp"
#include "actors/ownership.hpp"
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
}

dcon::resource_extraction_right_id create_right(sys::state& state, dcon::person_id initiator,
	dcon::office_id office, dcon::resource_deposit_id deposit, dcon::economic_actor_id holder,
	dcon::nation_id granting_nation, sys::date valid_from, sys::date valid_until,
	float max_daily_quantity, sys::date date) {
	if(!initiator || !office || !deposit || !holder || !granting_nation || valid_until < valid_from
		|| !finite_nonnegative(max_daily_quantity) || date < valid_from || date >= valid_until
		|| !state.world.person_get_alive(initiator)) return {};
	auto province = state.world.site_get_province_from_site_location(
		state.world.resource_deposit_get_site_from_resource_deposit_site(deposit));
	auto institution = governance::institution_for_office(state, office);
	if(!province || !institution || governance::nation_of(state, institution) != granting_nation
		|| state.world.province_get_nation_from_province_ownership(province) != granting_nation
		|| !persons::authority_tenure_on_or_before(state, initiator, governance::authority_kind::license, granting_nation, date)) return {};
	// Overlapping active rights for one deposit are ambiguous. Reject them at
	// creation so extraction can select a single right deterministically.
	bool overlaps = false;
	state.world.resource_deposit_for_each_resource_extraction_right_deposit_as_resource_deposit(deposit, [&](dcon::resource_extraction_right_deposit_id relation) {
		auto existing = state.world.resource_extraction_right_deposit_get_resource_extraction_right(relation);
		if(state.world.resource_extraction_right_get_status(existing) == uint8_t(right_status::active)
			&& valid_from < state.world.resource_extraction_right_get_valid_until(existing)
			&& state.world.resource_extraction_right_get_valid_from(existing) < valid_until)
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
		if(!result && state.world.resource_extraction_right_get_status(right) == uint8_t(right_status::active)
			&& state.world.resource_extraction_right_get_valid_from(right) <= date
			&& date < state.world.resource_extraction_right_get_valid_until(right)) result = right;
	});
	return result;
}

dcon::resource_extraction_right_id active_right_for(sys::state const& state, dcon::resource_deposit_id deposit,
	dcon::economic_actor_id holder, sys::date date) {
	dcon::resource_extraction_right_id result{};
	bool ambiguous = false;
	state.world.resource_deposit_for_each_resource_extraction_right_deposit_as_resource_deposit(deposit, [&](dcon::resource_extraction_right_deposit_id relation) {
		auto right = state.world.resource_extraction_right_deposit_get_resource_extraction_right(relation);
		if(state.world.resource_extraction_right_get_status(right) != uint8_t(right_status::active)
			|| state.world.resource_extraction_right_get_valid_from(right) > date
			|| date >= state.world.resource_extraction_right_get_valid_until(right)
			|| state.world.resource_extraction_right_get_economic_actor_from_resource_extraction_right_holder(right) != holder)
			return;
		if(result) ambiguous = true;
		else if(!ambiguous) result = right;
	});
	return ambiguous ? dcon::resource_extraction_right_id{} : result;
}

float extract_resource(sys::state& state, dcon::resource_deposit_id deposit, dcon::economic_actor_id operator_actor,
	float requested_quantity, sys::date date) {
	if(!deposit || !operator_actor || !std::isfinite(requested_quantity) || requested_quantity <= 0.0f
		|| state.world.resource_deposit_get_status(deposit) != uint8_t(deposit_status::active)) return 0.0f;
	auto site = state.world.resource_deposit_get_site_from_resource_deposit_site(deposit);
	if(!site || !actors::ownership::asset_for_deposit(state, deposit)
		|| !has_owner(state, actors::ownership::asset_for_deposit(state, deposit))
		|| actors::ownership::operator_for_deposit(state, deposit) != operator_actor) return 0.0f;
	auto right = active_right_for(state, deposit, operator_actor, date);
	if(!right) return 0.0f;
	auto holder = state.world.resource_extraction_right_get_economic_actor_from_resource_extraction_right_holder(right);
	if(holder != operator_actor) return 0.0f;
	auto extracted_today = 0.0f;
	state.world.resource_deposit_for_each_extraction_event_deposit_as_resource_deposit(deposit, [&](dcon::extraction_event_deposit_id relation) {
		auto event = state.world.extraction_event_deposit_get_extraction_event(relation);
		if(state.world.extraction_event_get_date(event) == date) extracted_today += state.world.extraction_event_get_quantity(event);
	});
	auto remaining = state.world.resource_deposit_get_remaining_recoverable_reserves(deposit);
	auto capacity = state.world.resource_deposit_get_daily_extraction_capacity(deposit);
	auto right_limit = state.world.resource_extraction_right_get_max_daily_quantity(right);
	auto actual = std::min({requested_quantity, std::max(0.0f, capacity - extracted_today), std::max(0.0f, right_limit - extracted_today), remaining});
	if(!std::isfinite(actual) || actual <= 0.0f) return 0.0f;
	auto commodity = state.world.resource_deposit_get_commodity(deposit);
	if(!commodity || !std::isfinite(remaining - actual)
		|| !std::isfinite(inventory::quantity(state, site, commodity, operator_actor) + actual)) return 0.0f;
	auto stock = inventory::find(state, site, commodity, operator_actor);
	auto old_stock_quantity = stock ? state.world.physical_stock_get_quantity(stock) : 0.0f;
	if(inventory::add(state, site, commodity, actual, operator_actor) != actual) {
		auto changed = inventory::find(state, site, commodity, operator_actor);
		if(stock) state.world.physical_stock_set_quantity(stock, old_stock_quantity);
		else if(changed) state.world.delete_physical_stock(changed);
		return 0.0f;
	}
	auto event = state.world.create_extraction_event();
	if(!event) {
		if(stock) state.world.physical_stock_set_quantity(stock, old_stock_quantity);
		else if(auto changed = inventory::find(state, site, commodity, operator_actor)) state.world.delete_physical_stock(changed);
		return 0.0f;
	}
	state.world.extraction_event_set_commodity(event, commodity);
	state.world.extraction_event_set_quantity(event, actual);
	state.world.extraction_event_set_date(event, date);
	state.world.force_create_extraction_event_deposit(event, deposit);
	state.world.force_create_extraction_event_operator(event, operator_actor);
	// Commit the reserve/status mutation only after the stock and provenance
	// object exist. DCON allocation is infallible; the explicit rollback below
	// still protects the invariant if inventory semantics change later.
	state.world.resource_deposit_set_remaining_recoverable_reserves(deposit, remaining - actual);
	if(remaining - actual <= 1.0e-5f) state.world.resource_deposit_set_status(deposit, uint8_t(deposit_status::depleted));
	return actual;
}
} // namespace economy::physical::extraction
