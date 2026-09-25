#include "household_mobility.hpp"

#include "accounts/accounts.hpp"
#include "economy/advanced_province_buildings.hpp"
#include "economy/demographics.hpp"
#include "economy/exact_person_economy.hpp"
#include "economy/physical/concrete_labor.hpp"
#include "economy/physical/concrete_market.hpp"
#include "economy/physical/exact_person_goods.hpp"
#include "economy/physical/inventory.hpp"
#include "economy/physical/individual_consumption.hpp"
#include "governance/governance.hpp"
#include "persons/exact_population.hpp"
#include "persons/persons.hpp"
#include "persons/population_materialization.hpp"
#include "provinces/province.hpp"
#include "system_state.hpp"
#include "world/site.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <vector>

namespace economy::physical::household_mobility {
namespace {
constexpr float epsilon = 1.0e-5f;
constexpr float long_commute_km = 90.0f;
constexpr float maximum_commute_penalty = 0.45f;
constexpr float commute_penalty_per_km = 0.003f;
constexpr float household_consumption_share = 0.80f;

struct need_item {
	dcon::commodity_id commodity{};
	float quantity = 0.0f;
	float price = 0.0f;
};

dcon::market_id market_for_site(sys::state const& state, dcon::site_id site) {
	if(!site || !state.world.site_is_valid(site)) return {};
	auto province = state.world.site_get_province_from_site_location(site);
	if(!province || !state.world.province_is_valid(province)) return {};
	auto zone = state.world.province_get_state_membership(province);
	return zone ? state.world.state_instance_get_market_from_local_market(zone) : dcon::market_id{};
}

dcon::pop_id pop_for_person_cell(sys::state& state, uint32_t source_cell,
	dcon::province_id province, dcon::culture_id culture, dcon::religion_id religion,
	dcon::pop_type_id type) {
	if(!source_cell || !province) return {};
	auto source = dcon::pop_id{dcon::pop_id::value_base_t(source_cell - 1u)};
	if(source && state.world.pop_is_valid(source)
		&& state.world.pop_get_province_from_pop_location(source) == province
		&& state.world.pop_get_culture(source) == culture
		&& state.world.pop_get_religion(source) == religion
		&& state.world.pop_get_poptype(source) == type) return source;
	dcon::pop_id result{};
	state.world.province_for_each_pop_location(province, [&](auto relation) {
		auto pop = state.world.pop_location_get_pop(relation);
		if(!result && pop && state.world.pop_get_culture(pop) == culture
			&& state.world.pop_get_religion(pop) == religion
			&& state.world.pop_get_poptype(pop) == type) result = pop;
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
		if(!result && pop && state.world.pop_get_culture(pop) == culture
			&& state.world.pop_get_religion(pop) == religion
			&& state.world.pop_get_poptype(pop) == type) result = pop;
	});
	if(result) return result;
	result = state.world.create_pop();
	state.world.force_create_pop_location(result, province);
	state.world.pop_set_culture(result, culture);
	state.world.pop_set_religion(result, religion);
	state.world.pop_set_poptype(result, type);
	state.world.pop_set_size(result, 0.0f);
	state.world.pop_set_savings(result, 0.0f);
	state.world.pop_set_uemployment(result, state.world.pop_get_uemployment(source));
	state.world.pop_set_uliteracy(result, state.world.pop_get_uliteracy(source));
	state.world.pop_set_umilitancy(result, state.world.pop_get_umilitancy(source));
	state.world.pop_set_uconsciousness(result, state.world.pop_get_uconsciousness(source));
	state.world.pop_set_satisfaction(result, state.world.pop_get_satisfaction(source));
	state.world.pop_set_is_primary_or_accepted_culture(result,
		state.world.pop_get_is_primary_or_accepted_culture(source));
	return result;
}

bool transfer_population_unit(sys::state& state, uint32_t source_cell,
	dcon::province_id origin, dcon::province_id destination,
	dcon::culture_id culture, dcon::religion_id religion, dcon::pop_type_id type) {
	if(!source_cell || !origin || !destination || origin == destination) return true;
	auto source = pop_for_person_cell(state, source_cell, origin, culture, religion, type);
	if(!source) return false;
	auto target = find_or_create_population_cell(state, destination, source);
	if(!target || target == source) return false;
	constexpr float population_units_per_literal_person =
		1.0f / float(persons::population_materialization::literal_person_multiplier);
	auto source_size = state.world.pop_get_size(source);
	auto target_size = state.world.pop_get_size(target);
	auto source_savings = state.world.pop_get_savings(source);
	auto target_savings = state.world.pop_get_savings(target);
	if(!std::isfinite(source_size) || source_size < population_units_per_literal_person
		|| !std::isfinite(target_size) || target_size < 0.0f
		|| !std::isfinite(source_savings) || source_savings < 0.0f
		|| !std::isfinite(target_savings) || target_savings < 0.0f) return false;
	auto moved_savings = source_size > epsilon
		? source_savings * population_units_per_literal_person / source_size : 0.0f;
	if(!std::isfinite(moved_savings) || moved_savings > source_savings
		|| population_units_per_literal_person > std::numeric_limits<float>::max() - target_size
		|| moved_savings > std::numeric_limits<float>::max() - target_savings) return false;
	state.world.pop_set_size(source, source_size - population_units_per_literal_person);
	state.world.pop_set_size(target, target_size + population_units_per_literal_person);
	state.world.pop_set_savings(source, source_savings - moved_savings);
	state.world.pop_set_savings(target, target_savings + moved_savings);
	return true;
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

void move_legacy_household_stock(sys::state& state, dcon::person_id person,
	dcon::site_id origin, dcon::site_id destination) {
	auto owner = persons::actor_for_person(state, person);
	if(!owner || !origin || !destination || origin == destination) return;
	state.world.for_each_commodity([&](dcon::commodity_id commodity) {
		auto amount = inventory::quantity(state, origin, commodity, owner);
		if(!std::isfinite(amount) || amount <= epsilon) return;
		auto removed = inventory::remove(state, origin, commodity, amount, owner);
		auto added = inventory::add(state, destination, commodity, removed, owner);
		if(added + epsilon < removed)
			(void)inventory::add(state, origin, commodity, removed - added, owner);
	});
}

void move_exact_household_stock(sys::state& state, persons::exact_population::person_key person,
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

dcon::pop_type_id source_type_for_person(sys::state const& state, dcon::person_id person) {
	auto type = state.world.person_get_source_pop_type(person);
	if(type && state.world.pop_type_is_valid(type)) return type;
	auto source_cell = state.world.person_get_source_population_cell(person);
	if(source_cell) {
		auto source = dcon::pop_id{dcon::pop_id::value_base_t(source_cell - 1u)};
		if(source && state.world.pop_is_valid(source)) return state.world.pop_get_poptype(source);
	}
	return {};
}

std::vector<need_item> consumption_profile(sys::state const& state,
	dcon::pop_type_id type, dcon::market_id market, float daily_income) {
	std::vector<need_item> life, everyday, luxury;
	float life_cost = 0.0f, everyday_cost = 0.0f, luxury_cost = 0.0f;
	if(!type || !state.world.pop_type_is_valid(type) || !market
		|| !std::isfinite(daily_income) || daily_income <= epsilon) return {};
	state.world.for_each_commodity([&](dcon::commodity_id commodity) {
		auto life_qty = std::max(0.0f, state.world.pop_type_get_life_needs(type, commodity));
		auto everyday_qty = std::max(0.0f, state.world.pop_type_get_everyday_needs(type, commodity));
		auto luxury_qty = std::max(0.0f, state.world.pop_type_get_luxury_needs(type, commodity));
		if(life_qty <= epsilon && everyday_qty <= epsilon && luxury_qty <= epsilon) return;
		auto fallback = std::max(0.01f, state.world.commodity_get_cost(commodity));
		auto price = concrete_market::canonical_reference_price(state, market, commodity,
			state.current_date, fallback);
		if(!std::isfinite(price) || price <= epsilon) return;
		if(life_qty > epsilon) { life.push_back({commodity, life_qty, price}); life_cost += life_qty * price; }
		if(everyday_qty > epsilon) { everyday.push_back({commodity, everyday_qty, price}); everyday_cost += everyday_qty * price; }
		if(luxury_qty > epsilon) { luxury.push_back({commodity, luxury_qty, price}); luxury_cost += luxury_qty * price; }
	});
	// Low income first protects life needs; everyday and luxury baskets only
	// enter once the preceding tier is affordable at current market prices.
	auto remaining_income = daily_income * household_consumption_share;
	auto life_scale = life_cost > epsilon
		? std::clamp(remaining_income / life_cost, 0.0f, 1.0f) : 0.0f;
	remaining_income = std::max(0.0f, remaining_income - life_cost * life_scale);
	auto everyday_scale = everyday_cost > epsilon
		? std::clamp(remaining_income / everyday_cost, 0.0f, 1.0f) : 0.0f;
	remaining_income = std::max(0.0f, remaining_income - everyday_cost * everyday_scale);
	auto luxury_scale = luxury_cost > epsilon
		? std::clamp(remaining_income / luxury_cost, 0.0f, 1.0f) : 0.0f;
	std::vector<need_item> result;
	result.reserve(life.size() + everyday.size() + luxury.size());
	for(auto item : life) { item.quantity *= life_scale; if(item.quantity > epsilon) result.push_back(item); }
	for(auto item : everyday) { item.quantity *= everyday_scale; if(item.quantity > epsilon) result.push_back(item); }
	for(auto item : luxury) { item.quantity *= luxury_scale; if(item.quantity > epsilon) result.push_back(item); }
	std::sort(result.begin(), result.end(), [](auto const& left, auto const& right) {
		return left.commodity.index() < right.commodity.index();
	});
	std::vector<need_item> consolidated;
	for(auto const& item : result) {
		if(!consolidated.empty() && consolidated.back().commodity == item.commodity)
			consolidated.back().quantity += item.quantity;
		else consolidated.push_back(item);
	}
	return consolidated;
}

void process_legacy_household(sys::state& state, dcon::person_id person,
	dcon::pop_type_id type, dcon::site_id home, float daily_income) {
	auto market = market_for_site(state, home);
	for(auto const& item : consumption_profile(state, type, market, daily_income))
		(void)individual_consumption::set_need(state, person, item.commodity, item.quantity);
}

void process_exact_household(sys::state& state, persons::exact_population::person_key person,
	dcon::pop_type_id type, dcon::site_id home, float daily_income,
	std::unordered_set<uint32_t>& matched_markets) {
	auto market = market_for_site(state, home);
	if(!market) return;
	auto market_key = uint32_t(market.index());
	if(matched_markets.insert(market_key).second)
		exact_person_goods::begin_period(state, state.current_date);
	auto profile = consumption_profile(state, type, market, daily_income);
	for(auto const& item : profile)
		(void)exact_person_goods::set_need(state, person, item.commodity, item.quantity);
	for(auto const& item : profile)
		(void)exact_person_goods::process_purchase_decision(state, person, item.commodity);
	for(auto const& item : profile)
		(void)exact_person_goods::process_consumption(state, person, item.commodity);
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
	if(!home || !state.world.site_is_valid(home) || !workplace || !state.world.site_is_valid(workplace)
		|| !std::isfinite(gross_daily_wage) || gross_daily_wage < 0.0f) return 0.0f;
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
	if(type == state.culture_definitions.secondary_factory_worker
		|| type == state.culture_definitions.bureaucrat
		|| type == state.culture_definitions.clergy) return 2;
	if(type == state.culture_definitions.primary_factory_worker
		|| type == state.culture_definitions.artisans) return 1;
	return 0;
}

bool relocate_for_job(sys::state& state, dcon::person_id person, dcon::site_id workplace) {
	if(!person || !state.world.person_is_valid(person) || !state.world.person_get_alive(person)
		|| !workplace || !state.world.site_is_valid(workplace)) return false;
	auto home = individual_consumption::home_site(state, person);
	if(!home || home == workplace) return false;
	auto origin = state.world.site_get_province_from_site_location(home);
	auto destination = state.world.site_get_province_from_site_location(workplace);
	if(!origin || !destination || !same_nation(state, origin, destination)
		|| !destination_has_urban_housing(state, destination)) return false;
	auto distance = province::direct_distance_km(state, origin, destination);
	if(!std::isfinite(distance) || distance < long_commute_km) return false;
	auto source_cell = state.world.person_get_source_population_cell(person);
	auto type = source_type_for_person(state, person);
	if(source_cell && type) {
		auto culture = state.world.person_get_source_culture(person);
		auto religion = state.world.person_get_source_religion(person);
		if(!culture || !religion) {
			auto source = dcon::pop_id{dcon::pop_id::value_base_t(source_cell - 1u)};
			if(source && state.world.pop_is_valid(source)) {
				if(!culture) culture = state.world.pop_get_culture(source);
				if(!religion) religion = state.world.pop_get_religion(source);
			}
		}
		if(!transfer_population_unit(state, source_cell, origin, destination, culture, religion, type)) return false;
	}
	if(!individual_consumption::set_home_site(state, person, workplace)) return false;
	move_legacy_household_stock(state, person, home, workplace);
	return true;
}

bool relocate_for_job(sys::state& state, persons::exact_population::person_key person,
	dcon::site_id workplace) {
	if(!persons::exact_population::exists(state, person)
		|| !persons::exact_population::alive(state, person)
		|| !workplace || !state.world.site_is_valid(workplace)) return false;
	auto home = persons::exact_population::home_site(state, person);
	if(!home || home == workplace) return false;
	auto origin = state.world.site_get_province_from_site_location(home);
	auto destination = state.world.site_get_province_from_site_location(workplace);
	if(!origin || !destination || !same_nation(state, origin, destination)
		|| !destination_has_urban_housing(state, destination)) return false;
	auto distance = province::direct_distance_km(state, origin, destination);
	if(!std::isfinite(distance) || distance < long_commute_km) return false;
	auto descriptor = persons::exact_population::descriptor_for_cell(state, person.source_population_cell);
	if(descriptor) {
		auto source = dcon::pop_id{dcon::pop_id::value_base_t(person.source_population_cell - 1u)};
		if(source && state.world.pop_is_valid(source)
			&& state.world.pop_get_culture(source) == descriptor->source_culture
			&& state.world.pop_get_religion(source) == descriptor->source_religion
			&& state.world.pop_get_poptype(source) == descriptor->source_pop_type) {
			if(!transfer_population_unit(state, person.source_population_cell, origin, destination,
				descriptor->source_culture, descriptor->source_religion, descriptor->source_pop_type)) return false;
		}
	}
	if(!persons::exact_population::set_home_site(state, person, workplace)) return false;
	move_exact_household_stock(state, person, home, workplace);
	return true;
}

void update_employed_households(sys::state& state) {
	std::unordered_set<uint32_t> seen_people;
	std::unordered_set<uint32_t> matched_exact_markets;
	state.world.for_each_factory([&](dcon::factory_id factory) {
		for(auto contract : concrete_labor::active_contracts_for_factory(state, factory)) {
			auto person = state.world.employment_contract_get_person_from_employment_contract_person(contract);
			if(!person || !seen_people.insert(uint32_t(person.index())).second) continue;
			auto type = source_type_for_person(state, person);
			auto home = individual_consumption::home_site(state, person);
			process_legacy_household(state, person, type, home, concrete_labor::wage_due(state, contract));
		}
		for(auto contract : exact_person_economy::active_contracts_for_factory(state, factory)) {
			auto record = exact_person_economy::contract(state, contract);
			if(!record || !persons::exact_population::alive(state, record->worker)) continue;
			auto type = persons::exact_population::source_pop_type(state, record->worker);
			auto home = persons::exact_population::home_site(state, record->worker);
			auto income = record->pay_period_days != 0
				? record->wage_rate * record->labor_capacity / float(record->pay_period_days) : 0.0f;
			process_exact_household(state, record->worker, type, home, income, matched_exact_markets);
		}
	});
	state.world.for_each_nation([&](dcon::nation_id nation) {
		for(auto institution : governance::institutions_of(state, nation)) {
			for(auto contract : concrete_labor::active_contracts_for_institution(state, institution)) {
				auto person = state.world.employment_contract_get_person_from_employment_contract_person(contract);
				if(!person || !seen_people.insert(uint32_t(person.index())).second) continue;
				process_legacy_household(state, person, source_type_for_person(state, person),
					individual_consumption::home_site(state, person), concrete_labor::wage_due(state, contract));
			}
			for(auto contract_id : exact_person_economy::active_contracts_for_institution(state, institution)) {
				auto record = exact_person_economy::contract(state, contract_id);
				if(!record || !persons::exact_population::alive(state, record->worker)) continue;
				auto income = record->pay_period_days != 0
					? record->wage_rate * record->labor_capacity / float(record->pay_period_days) : 0.0f;
				process_exact_household(state, record->worker,
					persons::exact_population::source_pop_type(state, record->worker),
					persons::exact_population::home_site(state, record->worker), income,
					matched_exact_markets);
			}
		}
	});
}

} // namespace economy::physical::household_mobility
