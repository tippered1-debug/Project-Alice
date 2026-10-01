#include "household_mobility.hpp"
#include "economy/households.hpp"

#include "accounts/accounts.hpp"
#include "economy/advanced_province_buildings.hpp"
#include "economy/demographics.hpp"
#include "economy/exact_person_economy.hpp"
#include "economy/physical/exact_person_goods.hpp"
#include "economy/physical/inventory.hpp"
#include "governance/governance.hpp"
#include "persons/persons.hpp"
#include "provinces/province.hpp"
#include "system_state.hpp"
#include "world/site.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>
#include <optional>
#include <vector>

namespace economy::physical::household_mobility {
namespace {
constexpr float epsilon = 1.0e-5f;
constexpr float long_commute_km = 90.0f;
constexpr float maximum_commute_penalty = 0.45f;
constexpr float commute_penalty_per_km = 0.003f;

dcon::market_id market_for_site(sys::state const& state, dcon::site_id site) {
	if(!site || !state.world.site_is_valid(site)) return {};
	auto province = state.world.site_get_province_from_site_location(site);
	if(!province || !state.world.province_is_valid(province)) return {};
	auto zone = state.world.province_get_state_membership(province);
	return zone ? state.world.state_instance_get_market_from_local_market(zone) : dcon::market_id{};
}

dcon::pop_id pop_for_cell_id(sys::state const& state, uint32_t source_cell) {
	return persons::population_for_source_cell(state, source_cell);
}

dcon::pop_id pop_for_person_cell(sys::state& state, uint32_t source_cell,
	dcon::province_id province, dcon::culture_id culture, dcon::religion_id religion,
	dcon::pop_type_id type) {
	if(!source_cell || !province) return {};
	auto source = pop_for_cell_id(state, source_cell);
	if(source && state.world.pop_is_valid(source) && state.world.pop_get_province_from_pop_location(source) == province && state.world.pop_get_culture(source) == culture && state.world.pop_get_religion(source) == religion && state.world.pop_get_poptype(source) == type) return source;
	dcon::pop_id result{};
	state.world.province_for_each_pop_location(province, [&](auto relation) {
		auto pop = state.world.pop_location_get_pop(relation);
		if(!result && pop && state.world.pop_get_culture(pop) == culture && state.world.pop_get_religion(pop) == religion && state.world.pop_get_poptype(pop) == type) result = pop;
	});
	return result;
}

dcon::pop_id find_or_create_population_cell(sys::state& state, dcon::province_id province,
	dcon::pop_id source) {
	if(!province || !source) return {};
	auto culture = state.world.pop_get_culture(source);
	auto religion = state.world.pop_get_religion(source);
	auto type = state.world.pop_get_poptype(source);
	dcon::pop_id result{};
	state.world.province_for_each_pop_location(province, [&](auto relation) {
		auto pop = state.world.pop_location_get_pop(relation);
		if(!result && pop && state.world.pop_get_culture(pop) == culture && state.world.pop_get_religion(pop) == religion && state.world.pop_get_poptype(pop) == type) result = pop;
	});
	if(!result) {
		result = state.world.create_pop();
		state.world.force_create_pop_location(result, province);
		state.world.pop_set_culture(result, culture);
		state.world.pop_set_religion(result, religion);
		state.world.pop_set_poptype(result, type);
		state.world.pop_set_uemployment(result, state.world.pop_get_uemployment(source));
		state.world.pop_set_uliteracy(result, state.world.pop_get_uliteracy(source));
		state.world.pop_set_umilitancy(result, state.world.pop_get_umilitancy(source));
		state.world.pop_set_uconsciousness(result, state.world.pop_get_uconsciousness(source));
		state.world.pop_set_satisfaction(result, 0.0f); // Filled by the physical consumption projection.
		state.world.pop_set_is_primary_or_accepted_culture(result,
			state.world.pop_get_is_primary_or_accepted_culture(source));
	}
	if(persons::source_population_cell_for_population(state, result) == 0) {
		if(!persons::register_population_cell(state, result)) {
			assert(false && "new household population cell must register in exact population");
			std::abort();
		}
	}
	return result;
}

bool transfer_population_unit(sys::state& state, uint32_t source_cell,
	dcon::province_id origin, dcon::province_id destination,
	dcon::culture_id culture, dcon::religion_id religion, dcon::pop_type_id type,
	persons::person_key member) {
	if(!source_cell || !origin || !destination || origin == destination) return false;
	if(persons::current_population_cell(state, member) != source_cell) return false;
	auto source = pop_for_person_cell(state, source_cell, origin, culture, religion, type);
	if(!source) return false;
	auto target = find_or_create_population_cell(state, destination, source);
	if(!target || target == source) return false;
	return persons::transfer_population_membership(state, member,
		target, persons::population_transition_cause::household_relocation);
}

bool same_nation(sys::state const& state, dcon::province_id left, dcon::province_id right) {
	if(!left || !right) return false;
	auto left_nation = state.world.province_get_nation_from_province_ownership(left);
	auto right_nation = state.world.province_get_nation_from_province_ownership(right);
	return left_nation && right_nation && left_nation == right_nation;
}

bool destination_has_urban_housing(sys::state const& state, dcon::province_id province) {
	return province && state.world.province_is_valid(province)
		&& state.world.province_get_advanced_province_building_private_size(
			province, advanced_province_buildings::list::local_cities_and_towns) > epsilon;
}

void move_exact_household_stock(sys::state& state, persons::person_key person,
	dcon::site_id origin, dcon::site_id destination) {
	if(!origin || !destination || origin == destination) return;
	state.world.for_each_commodity([&](dcon::commodity_id commodity) {
		auto amount = exact_person_goods::stock_quantity(state, person, origin, commodity);
		if(!std::isfinite(amount) || amount <= epsilon) return;
		auto removed = exact_person_goods::remove_stock(state, person, origin, commodity, amount);
		auto added = exact_person_goods::add_stock(state, person, destination, commodity, removed);
		if(added + epsilon < removed)
			(void)exact_person_goods::add_stock(state, person, origin, commodity, removed - added);
	});
}

void import_scenario_need_profile_once(sys::state& state, persons::person_key person,
	std::vector<std::pair<dcon::commodity_id, float>> const& profile) {
	if(!persons::exists(state, person) || !persons::alive(state, person)
		|| exact_person_goods::need_profile_imported(state, person)) return;
	if(!exact_person_goods::needs_for_person(state, person).empty()) {
		(void)exact_person_goods::mark_need_profile_imported(state, person);
		return;
	}
	// One-way scenario migration: the old static need definition is summed into
	// the canonical profile once per person. Profiles are compiled once per POP
	// type during this update, so importing sparse consumers does not scan every
	// commodity separately for every person. Income, satisfaction, and market
	// weights are not inputs to daily decisions.
	for(auto const& [commodity, desired] : profile)
		(void)exact_person_goods::set_need(state, person, commodity, desired);
	(void)exact_person_goods::mark_need_profile_imported(state, person);
}

} // namespace

float commute_adjusted_daily_wage(sys::state const& state, dcon::site_id home,
	dcon::job_offer_id offer) {
	if(!home || !state.world.site_is_valid(home) || !offer || !state.world.job_offer_is_valid(offer)) return 0.0f;
	auto workplace = state.world.job_offer_get_site_from_job_offer_site(offer);
	auto wage = state.world.job_offer_get_wage_rate(offer);
	auto capacity = state.world.job_offer_get_labor_capacity(offer);
	auto period = state.world.job_offer_get_pay_period_days(offer);
	if(!std::isfinite(wage) || wage < 0.0f || !std::isfinite(capacity) || capacity <= 0.0f || period == 0) return 0.0f;
	return commute_adjusted_daily_wage(state, home, workplace, wage * capacity / float(period));
}

float commute_adjusted_daily_wage(sys::state const& state, dcon::site_id home,
	dcon::site_id workplace, float gross_daily_wage) {
	if(!home || !state.world.site_is_valid(home) || !workplace || !state.world.site_is_valid(workplace) || !std::isfinite(gross_daily_wage) || gross_daily_wage < 0.0f) return 0.0f;
	auto home_province = state.world.site_get_province_from_site_location(home);
	auto work_province = state.world.site_get_province_from_site_location(workplace);
	if(!home_province || !work_province || !same_nation(state, home_province, work_province)) return 0.0f;
	auto distance = province::direct_distance_km(const_cast<sys::state&>(state), home_province, work_province);
	if(!std::isfinite(distance) || distance < 0.0f) return 0.0f;
	auto penalty = std::clamp(distance * commute_penalty_per_km, 0.0f, maximum_commute_penalty);
	return std::max(0.0f, gross_daily_wage * (1.0f - penalty));
}

uint8_t qualification_rank(sys::state const& state, dcon::pop_type_id type) {
	if(!type || !state.world.pop_type_is_valid(type)) return 0;
	if(type == state.culture_definitions.secondary_factory_worker || type == state.culture_definitions.bureaucrat || type == state.culture_definitions.clergy) return 2;
	if(type == state.culture_definitions.primary_factory_worker || type == state.culture_definitions.artisans) return 1;
	return 0;
}

bool relocate_for_job(sys::state& state, persons::person_key person,
	dcon::site_id workplace) {
	if(!persons::exists(state, person) || !persons::alive(state, person) || !workplace || !state.world.site_is_valid(workplace)) return false;
	auto home = persons::home_site(state, person);
	if(!home || home == workplace) return false;
	auto origin = state.world.site_get_province_from_site_location(home);
	auto destination = state.world.site_get_province_from_site_location(workplace);
	if(!origin || !destination || !same_nation(state, origin, destination) || !destination_has_urban_housing(state, destination)) return false;
	auto distance = province::direct_distance_km(state, origin, destination);
	if(!std::isfinite(distance) || distance < long_commute_km) return false;
	auto source_cell = persons::current_population_cell(state, person);
	auto culture = persons::current_culture(state, person);
	auto religion = persons::current_religion(state, person);
	auto type = persons::pop_type(state, person);
	auto source = persons::population_for_source_cell(state, source_cell);
	if(!source || state.world.pop_get_culture(source) != culture
		|| state.world.pop_get_religion(source) != religion
		|| state.world.pop_get_poptype(source) != type) return false;
	if(!transfer_population_unit(state, source_cell, origin, destination,
		culture, religion, type, person)) return false;
	if(!persons::set_home_site(state, person, workplace)) return false;
	move_exact_household_stock(state, person, home, workplace);
	return true;
}

void update_employed_households(sys::state& state) {
	// Individual consumers are workers under contract and people living on their
	// own cash. An emptied account of someone who rejoined a cohort is neither.
	std::vector<persons::person_key> consumers;
	for(auto owner : exact_person_economy::account_owners(state)) {
		bool funded = false;
		for(auto account : exact_person_economy::accounts_for_person(state, owner))
			if(exact_person_economy::balance(state, account) > 0.0f) funded = true;
		if(funded) consumers.push_back(owner);
	}
	state.world.for_each_factory([&](dcon::factory_id factory) {
		for(auto contract_id : exact_person_economy::active_contracts_for_factory(state, factory)) {
			auto record = exact_person_economy::contract(state, contract_id);
			if(record && persons::alive(state, record->worker)) consumers.push_back(record->worker);
		}
	});
	state.world.for_each_nation([&](dcon::nation_id nation) {
		for(auto institution : governance::institutions_of(state, nation)) {
			for(auto contract_id : exact_person_economy::active_contracts_for_institution(state, institution)) {
				auto record = exact_person_economy::contract(state, contract_id);
				if(record && persons::alive(state, record->worker)) consumers.push_back(record->worker);
			}
		}
	});
	std::sort(consumers.begin(), consumers.end(), [](auto left, auto right) {
		return left.source_population_cell == right.source_population_cell
			? left.ordinal < right.ordinal
			: left.source_population_cell < right.source_population_cell;
	});
	consumers.erase(std::unique(consumers.begin(), consumers.end()), consumers.end());
	std::map<uint32_t, std::vector<std::pair<dcon::commodity_id, float>>> profiles;
	auto profile_for = [&](dcon::pop_type_id type) -> std::vector<std::pair<dcon::commodity_id, float>> const& {
		auto profile = profiles.find(type ? uint32_t(type.index()) : 0u);
		if(profile == profiles.end()) {
			std::vector<std::pair<dcon::commodity_id, float>> compiled;
			if(type && state.world.pop_type_is_valid(type)) {
				state.world.for_each_commodity([&](dcon::commodity_id commodity) {
					auto life = std::max(0.0f, state.world.pop_type_get_life_needs(type, commodity));
					auto everyday = std::max(0.0f, state.world.pop_type_get_everyday_needs(type, commodity));
					auto luxury = std::max(0.0f, state.world.pop_type_get_luxury_needs(type, commodity));
					auto desired = life + everyday + luxury;
					if(std::isfinite(desired) && desired > epsilon)
						compiled.emplace_back(commodity, desired);
				});
			}
			profile = profiles.emplace(type ? uint32_t(type.index()) : 0u, std::move(compiled)).first;
		}
		return profile->second;
	};
	// An individual budget feeds the person's family: a worker's needs are the
	// per-capita profile times the living dependents who rely on them.
	auto individuals = economy::households::individual_consumers(state);
	for(auto person : consumers) individuals.emplace(person.source_population_cell, person.ordinal);
	auto family_profile = [&](persons::person_key person) {
		auto family = float(std::max(1, economy::households::family_size(state, person, individuals)));
		auto scaled = profile_for(persons::pop_type(state, person));
		for(auto& [commodity, desired] : scaled) desired *= family;
		return scaled;
	};
	for(auto person : consumers) {
		if(!persons::alive(state, person)) continue;
		auto contracts = exact_person_economy::active_contracts_for_person(state, person);
		if(!contracts.empty()) {
			if(auto contract = exact_person_economy::contract(state, contracts.front()))
				(void)relocate_for_job(state, person, contract->workplace);
		}
		if(exact_person_goods::need_profile_imported(state, person)) {
			// Families change through births, deaths, and members taking their own
			// budget; refresh each consumer's family needs once per month.
			if((state.current_date.to_raw_value() + int32_t(person.ordinal % 30)) % 30 == 0)
				for(auto const& [commodity, desired] : family_profile(person))
					(void)exact_person_goods::set_need(state, person, commodity, desired);
			continue;
		}
		if(!exact_person_goods::needs_for_person(state, person).empty()) {
			(void)exact_person_goods::mark_need_profile_imported(state, person);
			continue;
		}
		import_scenario_need_profile_once(state, person, family_profile(person));
	}
}

} // namespace economy::physical::household_mobility
