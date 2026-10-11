#include "household_mobility.hpp"
#include "economy/households.hpp"

#include "accounts/accounts.hpp"
#include "economy/advanced_province_buildings.hpp"
#include "economy/demographics.hpp"
#include "economy/exact_person_economy.hpp"
#include "economy/physical/concrete_market.hpp"
#include "economy/physical/exact_person_goods.hpp"
#include "economy/physical/inventory.hpp"
#include "governance/governance.hpp"
#include "persons/persons.hpp"
#include "persons/exact_population.hpp"
#include "provinces/province.hpp"
#include "system_state.hpp"
#include "world/site.hpp"
#include "world/spatial_runtime.hpp"

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
constexpr float household_cash_buffer_days = 30.0f;

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
		if(!persons::register_population_cell(state, result)) return {};
	}
	return result;
}

bool same_nation(sys::state const& state, dcon::province_id left, dcon::province_id right) {
	if(!left || !right) return false;
	auto left_nation = state.world.province_get_nation_from_province_ownership(left);
	auto right_nation = state.world.province_get_nation_from_province_ownership(right);
	return left_nation && right_nation && left_nation == right_nation;
}

world::spatial_runtime::route route_for_mobility(sys::state const& state,
	dcon::site_id home, dcon::site_id workplace) {
	if(!home || !workplace || !state.world.site_is_valid(home)
		|| !state.world.site_is_valid(workplace)) return {};
	if(home == workplace) {
		world::spatial_runtime::route local{};
		local.connected = true;
		local.bottleneck_capacity = std::numeric_limits<float>::infinity();
		return local;
	}
	return world::spatial_runtime::route_for_sites(state, home, workplace);
}

float occupied_urban_housing(sys::state const& state, dcon::province_id province) {
	if(!province || !state.world.province_is_valid(province)) return std::numeric_limits<float>::infinity();
	float population = 0.0f;
	state.world.province_for_each_pop_location(province, [&](auto relation) {
		auto pop = state.world.pop_location_get_pop(relation);
		if(!pop) return;
		auto size = state.world.pop_get_size(pop);
		if(std::isfinite(size) && size > 0.0f) population += size;
	});
	auto measured_demand = state.world.province_get_service_demand_forbidden_public_supply(
		province, services::list::urban_housing);
	if(!std::isfinite(measured_demand) || measured_demand < 0.0f) measured_demand = 0.0f;
	// Exact population bootstrapping maps one POP size unit to one four-person
	// household unit. Prefer measured service demand when present; current POP
	// sizes provide a resident-based occupancy projection while that demand is
	// not yet populated.
	return std::max(population, measured_demand);
}

bool destination_has_urban_housing(sys::state const& state, dcon::province_id province,
	float incoming_capacity) {
	if(!province || !state.world.province_is_valid(province) || !std::isfinite(incoming_capacity)
		|| incoming_capacity <= 0.0f
		|| state.world.province_get_service_supply_private_size() <= uint32_t(services::list::urban_housing)
		|| state.world.province_get_service_demand_forbidden_public_supply_size() <= uint32_t(services::list::urban_housing)) return false;
	auto supply = state.world.province_get_service_supply_private(
		province, services::list::urban_housing);
	if(!std::isfinite(supply) || supply <= 0.0f) return false;
	auto occupied = occupied_urban_housing(state, province);
	return std::isfinite(occupied) && supply - occupied + epsilon >= incoming_capacity;
}

struct stock_move {
	persons::person_key owner{};
	dcon::site_id origin{};
	dcon::commodity_id commodity{};
	float quantity = 0.0f;
};

std::optional<std::vector<stock_move>> move_exact_household_stock(sys::state& state,
	std::vector<persons::person_key> const& members,
	dcon::site_id destination) {
	std::vector<stock_move> moves;
	for(auto member : members) {
		auto origin = persons::home_site(state, member);
		if(!origin || !destination || origin == destination) continue;
		state.world.for_each_commodity([&](dcon::commodity_id commodity) {
			auto amount = exact_person_goods::stock_quantity(state, member, origin, commodity);
			if(std::isfinite(amount) && amount > epsilon)
				moves.push_back({member, origin, commodity, amount});
		});
	}
	for(auto const& move : moves) {
		auto target = exact_person_goods::stock_quantity(state, move.owner, destination, move.commodity);
		if(!std::isfinite(target) || move.quantity > std::numeric_limits<float>::max() - target) return std::nullopt;
		if(exact_person_goods::stock_quantity(state, move.owner, move.origin, move.commodity) + epsilon < move.quantity) return std::nullopt;
	}
	std::vector<stock_move> completed;
	for(auto const& move : moves) {
		if(exact_person_goods::add_stock(state, move.owner, destination, move.commodity,
			move.quantity) + epsilon < move.quantity) {
			for(auto it = completed.rbegin(); it != completed.rend(); ++it) {
				(void)exact_person_goods::remove_stock(state, it->owner, destination,
					it->commodity, it->quantity);
				(void)exact_person_goods::add_stock(state, it->owner, it->origin,
					it->commodity, it->quantity);
			}
			return std::nullopt;
		}
		auto removed = exact_person_goods::remove_stock(state, move.owner, move.origin,
			move.commodity, move.quantity);
		if(removed + epsilon < move.quantity) {
			(void)exact_person_goods::remove_stock(state, move.owner, destination,
				move.commodity, move.quantity);
			(void)exact_person_goods::add_stock(state, move.owner, move.origin,
				move.commodity, removed);
			for(auto it = completed.rbegin(); it != completed.rend(); ++it) {
				(void)exact_person_goods::remove_stock(state, it->owner, destination,
					it->commodity, it->quantity);
				(void)exact_person_goods::add_stock(state, it->owner, it->origin,
					it->commodity, it->quantity);
			}
			return std::nullopt;
		}
		completed.push_back(move);
	}
	return moves;
}

void restore_household_goods(sys::state& state,
	economy::physical::exact_person_goods::goods_snapshot const& before) {
	if(!exact_person_goods::import_snapshot(state, before)) {
		assert(false && "household relocation rollback must restore its exact goods snapshot");
		std::abort();
	}
}

float expected_daily_household_cost(sys::state const& state, persons::person_key person,
	dcon::site_id destination) {
	auto province = destination && state.world.site_is_valid(destination)
		? state.world.site_get_province_from_site_location(destination) : dcon::province_id{};
	auto market = market_for_site(state, destination);
	if(!province) return 0.0f;
	auto family = households::budget_members(state, person);
	if(family.empty()) return std::numeric_limits<float>::infinity();
	double cost = 0.0;
	auto price_for = [&](dcon::commodity_id commodity) {
		auto fallback = std::max(0.0f, state.world.commodity_get_cost(commodity));
		if(market && state.world.market_get_price_size() > uint32_t(commodity.index())) {
			auto current_price = state.world.market_get_price(market, commodity);
			if(std::isfinite(current_price) && current_price > 0.0f)
				fallback = std::max(fallback, current_price);
		}
		return market ? concrete_market::concrete_reference_price(state, market,
			commodity, state.current_date, fallback) : fallback;
	};
	for(auto member : family) {
		auto needs = exact_person_goods::needs_for_person(state, member);
		if(!needs.empty()) {
			for(auto commodity : needs) {
				auto record = exact_person_goods::need(state, member, commodity);
				if(!record) continue;
				auto price = price_for(commodity);
				if(std::isfinite(price) && price > 0.0f
					&& std::isfinite(record->desired_quantity_per_period))
					cost += double(price) * std::max(0.0f, record->desired_quantity_per_period);
			}
			continue;
		}
		auto type = persons::pop_type(state, member);
		if(!type || !state.world.pop_type_is_valid(type)) continue;
		state.world.for_each_commodity([&](dcon::commodity_id commodity) {
			auto quantity = std::max(0.0f, state.world.pop_type_get_life_needs(type, commodity))
				+ std::max(0.0f, state.world.pop_type_get_everyday_needs(type, commodity))
				+ std::max(0.0f, state.world.pop_type_get_luxury_needs(type, commodity));
			auto price = price_for(commodity);
			if(std::isfinite(quantity) && quantity > 0.0f && std::isfinite(price) && price > 0.0f)
				cost += double(quantity) * double(price);
		});
	}
	if(state.world.province_get_service_price_size() > uint32_t(services::list::urban_housing)) {
		auto rent = state.world.province_get_service_price(province, services::list::urban_housing);
		if(std::isfinite(rent) && rent > 0.0f)
			cost += double(rent) * double(family.size()) / double(households::persons_per_family);
	}
	return std::isfinite(cost) && cost <= double(std::numeric_limits<float>::max())
		? float(cost) : std::numeric_limits<float>::infinity();
}

float family_cash_buffer(sys::state const& state, persons::person_key person) {
	double cash = 0.0;
	for(auto account : exact_person_economy::accounts_for_person(state, person)) {
		auto balance = exact_person_economy::balance(state, account);
		if(std::isfinite(balance) && balance > 0.0f) cash += balance;
	}
	return std::isfinite(cash) ? float(std::min(cash / double(household_cash_buffer_days),
		double(std::numeric_limits<float>::max()))) : 0.0f;
}

float household_offer_surplus_if_accessible(sys::state const& state,
	persons::person_key person, dcon::job_offer_id offer) {
	if(!persons::exists(state, person) || !persons::alive(state, person)
		|| !offer || !state.world.job_offer_is_valid(offer))
		return -std::numeric_limits<float>::infinity();
	auto home = persons::home_site(state, person);
	auto workplace = state.world.job_offer_get_site_from_job_offer_site(offer);
	if(!home || !workplace || !state.world.site_is_valid(home)
		|| !state.world.site_is_valid(workplace)) return -std::numeric_limits<float>::infinity();
	auto route = route_for_mobility(state, home, workplace);
	if(!route.connected || !std::isfinite(route.distance)) return -std::numeric_limits<float>::infinity();
	auto origin = state.world.site_get_province_from_site_location(home);
	auto destination = state.world.site_get_province_from_site_location(workplace);
	if(!same_nation(state, origin, destination)) return -std::numeric_limits<float>::infinity();
	if(route.distance >= long_commute_km && origin != destination) {
		auto family = households::budget_members(state, person);
		auto incoming_capacity = float(family.size()) / float(households::persons_per_family);
		if(!destination_has_urban_housing(state, destination, incoming_capacity))
			return -std::numeric_limits<float>::infinity();
	}
	auto net_wage = commute_adjusted_daily_wage(state, home, offer);
	if(net_wage <= 0.0f) return -std::numeric_limits<float>::infinity();
	auto expected_cost = expected_daily_household_cost(state, person, workplace);
	if(!std::isfinite(expected_cost)) return -std::numeric_limits<float>::infinity();
	auto cash_buffer = family_cash_buffer(state, person);
	double surplus = double(net_wage) + double(cash_buffer) - double(expected_cost);
	if(!std::isfinite(surplus) || surplus + double(epsilon) < 0.0)
		return -std::numeric_limits<float>::infinity();
	return float(std::clamp(surplus,
		-double(std::numeric_limits<float>::max()),
		double(std::numeric_limits<float>::max())));
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
	auto route = route_for_mobility(state, home, workplace);
	if(!route.connected || !std::isfinite(route.distance) || route.distance < 0.0f) return 0.0f;
	auto penalty = std::clamp(route.distance * commute_penalty_per_km, 0.0f, maximum_commute_penalty);
	return std::max(0.0f, gross_daily_wage * (1.0f - penalty));
}

bool job_offer_accessible(sys::state const& state, persons::person_key person,
	dcon::job_offer_id offer) {
	return std::isfinite(household_offer_surplus_if_accessible(state, person, offer));
}

float expected_household_daily_surplus(sys::state const& state, persons::person_key person,
	dcon::job_offer_id offer) {
	return household_offer_surplus_if_accessible(state, person, offer);
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
	if(!home || !state.world.site_is_valid(home) || home == workplace) return false;
	auto origin = state.world.site_get_province_from_site_location(home);
	auto destination = state.world.site_get_province_from_site_location(workplace);
	if(!origin || !destination || origin == destination || !same_nation(state, origin, destination)) return false;
	auto route = route_for_mobility(state, home, workplace);
	if(!route.connected || !std::isfinite(route.distance) || route.distance < long_commute_km) return false;
	auto members = households::budget_members(state, person);
	if(members.empty()) return false;
	auto incoming_capacity = float(members.size()) / float(households::persons_per_family);
	if(!destination_has_urban_housing(state, destination, incoming_capacity)) return false;

	struct member_move {
		persons::person_key person{};
		dcon::site_id home{};
		dcon::pop_id source{};
		dcon::pop_id target{};
	};
	std::vector<member_move> moves;
	moves.reserve(members.size());
	for(auto member : members) {
		auto old_home = persons::home_site(state, member);
		auto source_cell = persons::current_population_cell(state, member);
		auto culture = persons::current_culture(state, member);
		auto religion = persons::current_religion(state, member);
		auto type = persons::pop_type(state, member);
		if(!old_home || !state.world.site_is_valid(old_home)
			|| state.world.site_get_province_from_site_location(old_home) != origin
			|| source_cell == 0 || !culture || !religion || !type) return false;
		auto source = pop_for_person_cell(state, source_cell, origin, culture, religion, type);
		if(!source) return false;
		auto target = find_or_create_population_cell(state, destination, source);
		if(!target || target == source) return false;
		moves.push_back({member, old_home, source, target});
	}
	if(!persons::exact_population::can_project_population_membership(state)) return false;
	auto membership_before = persons::exact_population::export_snapshot(state);
	auto goods_before = exact_person_goods::export_snapshot(state);
	auto stock_moves = move_exact_household_stock(state, members, workplace);
	if(!stock_moves) {
		restore_household_goods(state, goods_before);
		return false;
	}
	std::vector<persons::population_membership_move> membership_moves;
	membership_moves.reserve(moves.size());
	for(auto const& move : moves) {
		membership_moves.push_back({move.person, move.target});
	}
	if(!persons::transfer_population_memberships(state, membership_moves,
		persons::population_transition_cause::household_relocation)) {
		restore_household_goods(state, goods_before);
		return false;
	}
	for(auto const& move : moves) {
		if(!persons::set_home_site(state, move.person, workplace)) {
			if(!persons::exact_population::import_snapshot(state, membership_before)
				|| !persons::project_population_membership(state)) {
				assert(false && "household relocation rollback must restore its preflight snapshot");
				std::abort();
			}
			for(auto const& reset : moves) {
				if(!persons::set_home_site(state, reset.person, reset.home)) {
					assert(false && "household relocation rollback must restore every home site");
					std::abort();
				}
			}
			restore_household_goods(state, goods_before);
			return false;
		}
	}
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
