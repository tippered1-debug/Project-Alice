#include "households.hpp"

#include "system_state.hpp"
#include "actors/organizations/organizations.hpp"
#include "actors/ownership.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/exact_person_economy.hpp"
#include "economy/physical/concrete_market.hpp"
#include "economy/physical/deposits.hpp"
#include "economy/physical/exact_person_goods.hpp"
#include "economy/physical/factory_inputs.hpp"
#include "economy/physical/inventory.hpp"
#include "economy/physical/land.hpp"
#include "economy/physical/shipments.hpp"
#include "persons/exact_population.hpp"
#include "persons/persons.hpp"
#include "world/spatial_runtime.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace economy::households {
namespace {
constexpr float epsilon = 1.0e-5f;

bool finite_positive(float value) { return std::isfinite(value) && value > 0.0f; }

std::vector<dcon::organization_id> all_households(sys::state const& state) {
	std::vector<dcon::organization_id> result;
	state.world.for_each_organization([&](dcon::organization_id organization) {
		if(is_household(state, organization)) result.push_back(organization);
	});
	std::sort(result.begin(), result.end(), [](auto left, auto right) { return left.index() < right.index(); });
	return result;
}

// People of one population row who are not cohort members: individual
// consumers and serving soldiers.
struct exclusions {
	std::unordered_map<uint32_t, float> by_population;
};

exclusions count_exclusions(sys::state const& state) {
	exclusions result;
	if(state.exact_person_goods) {
		std::set<std::pair<uint32_t, uint64_t>> seen;
		for(auto const& need : economy::physical::exact_person_goods::need_records(state)) {
			if(!seen.emplace(need.owner.source_population_cell, need.owner.ordinal).second) continue;
			if(!persons::alive(state, need.owner)) continue;
			if(auto population = persons::current_population(state, need.owner))
				result.by_population[population.index()] += 1.0f;
		}
	}
	if(state.exact_population) {
		for(auto const& range : persons::exact_population::all_military_assignments(state)) {
			auto population = persons::population_for_source_cell(state, range.source_population_cell);
			if(!population) continue;
			result.by_population[population.index()] += float(persons::exact_population::living_people_in_person_range(
				state, range.source_population_cell, range.first_ordinal, range.count, range.ordinal_stride));
		}
	}
	return result;
}

// Living cohort members per population row of the cohort's role and province.
std::vector<std::pair<dcon::pop_id, float>> member_populations(sys::state const& state,
	dcon::organization_id household, exclusions const& excluded) {
	std::vector<std::pair<dcon::pop_id, float>> result;
	auto province = home_of(state, household);
	auto cohort_role = role_of(state, household);
	if(!province || cohort_role == role::none || !state.exact_population) return result;
	for(auto location : state.world.province_get_pop_location(province)) {
		auto pop = location.get_pop();
		if(role_for_pop_type(state, pop.get_poptype()) != cohort_role) continue;
		auto cell = persons::source_population_cell_for_population(state, pop);
		if(cell == 0) continue;
		auto living = float(persons::living_people_in_population_cell(state, cell));
		auto removed = excluded.by_population.contains(pop.id.index()) ? excluded.by_population.at(pop.id.index()) : 0.0f;
		auto count = std::max(0.0f, living - removed);
		if(count > 0.0f) result.emplace_back(pop.id, count);
	}
	std::sort(result.begin(), result.end(), [](auto const& left, auto const& right) { return left.first.index() < right.first.index(); });
	return result;
}

// Per-commodity desired quantity per day, split into life, everyday, luxury.
using needs_table = std::vector<std::array<float, 3>>;

needs_table needs_for(sys::state const& state, std::vector<std::pair<dcon::pop_id, float>> const& populations) {
	needs_table result(state.world.commodity_size(), std::array<float, 3>{});
	for(auto const& [pop, count] : populations) {
		auto type = state.world.pop_get_poptype(pop);
		state.world.for_each_commodity([&](dcon::commodity_id commodity) {
			float values[3] = {
				state.world.pop_type_get_life_needs(type, commodity),
				state.world.pop_type_get_everyday_needs(type, commodity),
				state.world.pop_type_get_luxury_needs(type, commodity) };
			for(int i = 0; i < 3; ++i)
				if(std::isfinite(values[i]) && values[i] > 0.0f) result[commodity.index()][i] += values[i] * count;
		});
	}
	return result;
}

dcon::monetary_account_id account_for(sys::state& state, dcon::economic_actor_id actor) {
	dcon::monetary_account_id result{};
	state.world.economic_actor_for_each_monetary_account_owner_as_economic_actor(actor, [&](auto relation) {
		if(!result) result = state.world.monetary_account_owner_get_monetary_account(relation);
	});
	return result ? result : economy::accounts::open_account(state, actor, economy::money);
}

std::vector<dcon::factory_id> farms_of(sys::state const& state, dcon::organization_id household) {
	std::vector<dcon::factory_id> result;
	for(auto factory : actors::organizations::factories_operated_by(state, household))
		if(economy::physical::land::farms_land(state, factory)) result.push_back(factory);
	std::sort(result.begin(), result.end(), [](auto left, auto right) { return left.index() < right.index(); });
	return result;
}
}

dcon::organization_id create(sys::state& state, dcon::province_id home, role cohort_role, dcon::commodity_id settlement) {
	if(!home || !state.world.province_is_valid(home) || cohort_role == role::none || household_for(state, home, cohort_role)
		|| !settlement || !state.world.commodity_is_valid(settlement)) return {};
	auto organization = actors::organizations::create_organization(state, actors::ownership::actor_kind::household);
	if(!organization) return {};
	state.world.organization_set_household_role(organization, uint8_t(cohort_role));
	state.world.force_create_household_home(organization, home);
	if(!economy::accounts::open_account(state, actors::organizations::actor_for_organization(state, organization), settlement)) return {};
	return organization;
}

bool is_household(sys::state const& state, dcon::organization_id organization) {
	return organization && state.world.organization_is_valid(organization)
		&& state.world.organization_get_kind(organization) == uint8_t(actors::ownership::actor_kind::household);
}

bool is_household(sys::state const& state, dcon::economic_actor_id actor) {
	return actor && is_household(state, actors::organizations::organization_for_actor(state, actor));
}

role role_of(sys::state const& state, dcon::organization_id organization) {
	return is_household(state, organization) ? role(state.world.organization_get_household_role(organization)) : role::none;
}

dcon::province_id home_of(sys::state const& state, dcon::organization_id organization) {
	return is_household(state, organization) ? state.world.organization_get_province_from_household_home(organization) : dcon::province_id{};
}

dcon::site_id home_site(sys::state const& state, dcon::organization_id organization) {
	auto province = home_of(state, organization);
	return province ? world::spatial_runtime::site_for_province(state, province) : dcon::site_id{};
}

dcon::organization_id household_for(sys::state const& state, dcon::province_id province, role cohort_role) {
	dcon::organization_id result{};
	if(!province) return result;
	state.world.province_for_each_household_home_as_province(province, [&](dcon::household_home_id relation) {
		auto organization = state.world.household_home_get_organization(relation);
		if(role_of(state, organization) == cohort_role) result = organization;
	});
	return result;
}

role role_for_pop_type(sys::state const& state, dcon::pop_type_id type) {
	if(!type) return role::none;
	if(type == state.culture_definitions.farmers || type == state.culture_definitions.laborers) return role::peasant;
	if(type == state.culture_definitions.aristocrat) return role::landed;
	return role::none;
}

float members(sys::state const& state, dcon::organization_id organization) {
	return is_household(state, organization) ? std::max(0.0f, state.world.organization_get_household_members(organization)) : 0.0f;
}

float workers(sys::state const& state, dcon::organization_id organization) {
	return is_household(state, organization) ? std::max(0.0f, state.world.organization_get_household_workers(organization)) : 0.0f;
}

float reservation_wage(sys::state const& state, dcon::organization_id organization) {
	auto value = is_household(state, organization) ? state.world.organization_get_household_reservation_wage(organization) : 0.0f;
	return std::isfinite(value) ? std::max(0.0f, value) : 0.0f;
}

bool self_working_operator(sys::state const& state, dcon::factory_id factory) {
	return role_of(state, actors::organizations::operator_organization_for_factory(state, factory)) == role::peasant;
}

float self_employed_labor(sys::state const& state, dcon::factory_id factory) {
	if(!self_working_operator(state, factory) || !economy::physical::land::farms_land(state, factory)) return 0.0f;
	auto household = actors::organizations::operator_organization_for_factory(state, factory);
	float total = 0.0f;
	for(auto farm : farms_of(state, household)) total += economy::physical::land::reference_labor(state, farm);
	auto own = economy::physical::land::reference_labor(state, factory);
	return total > 0.0f ? workers(state, household) * own / total : 0.0f;
}

void refresh_membership(sys::state& state) {
	auto excluded = count_exclusions(state);
	for(auto household : all_households(state)) {
		float count = 0.0f;
		for(auto const& [pop, people] : member_populations(state, household, excluded)) count += people;
		auto previous_workers = workers(state, household);
		state.world.organization_set_household_members(household, count);
		state.world.organization_set_household_workers(household, count * workers_per_member);
		// What a worker gives up by leaving: yesterday's own production per worker,
		// valued at market reference prices.
		auto site = home_site(state, household);
		auto market = site ? economy::physical::concrete_market::market_for_site(state, site) : dcon::market_id{};
		float value = 0.0f;
		for(auto farm : farms_of(state, household)) {
			auto type = state.world.factory_get_building_type(farm);
			auto output = state.world.factory_type_get_output(type);
			auto price = market ? economy::physical::concrete_market::concrete_reference_price(state, market, output,
				state.current_date, std::max(0.01f, state.world.commodity_get_cost(output))) : 0.0f;
			value += std::max(0.0f, state.world.factory_get_output(farm)) * price;
		}
		if(previous_workers > epsilon) {
			auto today = value / previous_workers;
			auto current = reservation_wage(state, household);
			auto next = current > 0.0f ? current + reservation_smoothing * (today - current) : today;
			state.world.organization_set_household_reservation_wage(household, std::isfinite(next) ? std::max(0.0f, next) : 0.0f);
		}
	}
}

void process_daily(sys::state& state) {
	namespace market_ns = economy::physical::concrete_market;
	auto excluded = count_exclusions(state);
	// Titles each actor owns, to collect rent paid in kind at distant farms.
	std::unordered_map<uint32_t, std::vector<dcon::land_title_id>> titles_by_owner;
	state.world.for_each_land_title([&](dcon::land_title_id title) {
		if(auto owner = economy::physical::land::owner_of(state, title)) titles_by_owner[owner.index()].push_back(title);
	});
	std::set<uint32_t> traded;
	for(auto household : all_households(state)) {
		auto actor = actors::organizations::actor_for_organization(state, household);
		auto home = home_site(state, household);
		auto market = home ? market_ns::market_for_site(state, home) : dcon::market_id{};
		auto hub = market ? economy::physical::deposits::market_hub_for(state, market) : dcon::site_id{};
		if(!actor || !home || !market || !hub) continue;
		auto account = account_for(state, actor);

		// 1. Bring harvests and rent in kind home.
		std::vector<dcon::site_id> sources;
		for(auto farm : farms_of(state, household)) sources.push_back(state.world.factory_get_site_from_factory_site(farm));
		if(auto it = titles_by_owner.find(actor.index()); it != titles_by_owner.end())
			for(auto title : it->second) sources.push_back(state.world.land_title_get_site_from_land_title_site(title));
		std::sort(sources.begin(), sources.end(), [](auto left, auto right) { return left.index() < right.index(); });
		sources.erase(std::unique(sources.begin(), sources.end()), sources.end());
		for(auto source : sources) {
			if(!source || source == home) continue;
			state.world.for_each_commodity([&](dcon::commodity_id commodity) {
				auto quantity = economy::physical::inventory::quantity(state, source, commodity, actor);
				if(quantity > epsilon) (void)economy::physical::shipments::dispatch(state, source, home, commodity, quantity, actor);
			});
		}

		// 2. Consume own stock first: life, then everyday, then luxury.
		auto populations = member_populations(state, household, excluded);
		auto needs = needs_for(state, populations);
		double desired[3] = {};
		double consumed[3] = {};
		std::vector<float> daily_need(needs.size(), 0.0f);
		state.world.for_each_commodity([&](dcon::commodity_id commodity) {
			auto const& row = needs[commodity.index()];
			auto total = row[0] + row[1] + row[2];
			daily_need[commodity.index()] = total;
			for(int i = 0; i < 3; ++i) desired[i] += row[i];
			if(total <= epsilon) return;
			auto taken = economy::physical::inventory::remove(state, home, commodity,
				std::min(total, economy::physical::inventory::quantity(state, home, commodity, actor)), actor);
			for(int i = 0; i < 3 && taken > 0.0f; ++i) {
				auto part = std::min(taken, row[i]);
				consumed[i] += part;
				taken -= part;
			}
		});
		state.world.organization_set_household_life_satisfaction(household, desired[0] > 0.0 ? float(std::clamp(consumed[0] / desired[0], 0.0, 1.0)) : 1.0f);
		state.world.organization_set_household_everyday_satisfaction(household, desired[1] > 0.0 ? float(std::clamp(consumed[1] / desired[1], 0.0, 1.0)) : 1.0f);
		state.world.organization_set_household_luxury_satisfaction(household, desired[2] > 0.0 ? float(std::clamp(consumed[2] / desired[2], 0.0, 1.0)) : 1.0f);

		// 3. Keep a season of own needs; sell the rest through the common ask path.
		std::vector<bool> selling(needs.size(), false);
		auto farms = farms_of(state, household);
		state.world.for_each_commodity([&](dcon::commodity_id commodity) {
			auto stock = economy::physical::inventory::quantity(state, home, commodity, actor);
			auto surplus = stock - daily_need[commodity.index()] * retention_days;
			if(surplus > epsilon && hub != home)
				(void)economy::physical::shipments::dispatch(state, home, hub, commodity, surplus, actor);
			auto source = hub != home ? hub : home;
			auto offered = hub != home ? economy::physical::inventory::quantity(state, hub, commodity, actor) : std::max(0.0f, surplus);
			if(offered <= epsilon) return;
			auto price = market_ns::concrete_reference_price(state, market, commodity, state.current_date,
				std::max(0.01f, state.world.commodity_get_cost(commodity))) * sale_discount;
			dcon::factory_id producer{};
			for(auto farm : farms)
				if(state.world.factory_type_get_output(state.world.factory_get_building_type(farm)) == commodity) producer = farm;
			if(finite_positive(price) && market_ns::post_ask(state, actor, source, market, commodity, offered, price,
				market_ns::order_purpose::general, producer)) {
				selling[commodity.index()] = true;
				traded.insert(commodity.index());
			}
		});

		// 4. Buy tomorrow's remaining needs with cash, life needs first.
		if(!account) continue;
		auto cash = economy::accounts::balance(state, account) - market_ns::reserved_bid_amount(state, account);
		for(int category = 0; category < 3 && cash > epsilon; ++category) {
			state.world.for_each_commodity([&](dcon::commodity_id commodity) {
				auto const& row = needs[commodity.index()];
				auto total = daily_need[commodity.index()];
				if(cash <= epsilon || row[category] <= epsilon || selling[commodity.index()]) return;
				auto missing = economy::physical::factory_inputs::net_demand(state, home, actor, commodity, total);
				auto quantity = missing * row[category] / total;
				auto price = market_ns::concrete_reference_price(state, market, commodity, state.current_date,
					std::max(0.01f, state.world.commodity_get_cost(commodity))) * purchase_markup;
				if(!finite_positive(quantity) || !finite_positive(price)) return;
				quantity = std::min(quantity, cash / price);
				if(quantity > epsilon && market_ns::post_bid(state, actor, account, home, market, commodity, quantity, price,
					market_ns::order_purpose::general)) {
					cash -= quantity * price;
					traded.insert(commodity.index());
				}
			});
		}
	}
	for(auto index : traded)
		(void)market_ns::match_all(state, dcon::commodity_id{ dcon::commodity_id::value_base_t(index) }, state.current_date);
}

bool rejoin(sys::state& state, persons::person_key person) {
	using namespace economy::exact_person_economy;
	if(!persons::alive(state, person) || person_has_active_contract(state, person)
		|| persons::exact_population::has_military_assignment(state, person)) return false;
	auto separated = last_separation_date(state, person);
	if(!separated || state.current_date.to_raw_value() - separated->to_raw_value() < days_before_rejoining) return false;
	auto population = persons::current_population(state, person);
	auto home = persons::home_site(state, person);
	auto province = home ? state.world.site_get_province_from_site_location(home) : dcon::province_id{};
	auto cohort_role = population ? role_for_pop_type(state, state.world.pop_get_poptype(population)) : role::none;
	auto household = household_for(state, province, cohort_role);
	if(!household) return false;
	auto actor = actors::organizations::actor_for_organization(state, household);
	// Goods first: release fails without change while freight is pending.
	if(!economy::physical::exact_person_goods::release_to(state, person, actor)) return false;
	(void)withdraw_pending_applications(state, person);
	for(auto account : accounts_for_person(state, person)) {
		auto cash = balance(state, account);
		if(!(cash > 0.0f)) continue;
		auto settlement = settlement_of(state, account);
		auto destination = economy::accounts::find_account(state, actor, settlement);
		if(!destination) destination = economy::accounts::open_account(state, actor, settlement);
		if(destination) (void)transfer(state, account, account_ref::from_dcon(destination), cash,
			relations::transaction_kind::transfer, state.current_date);
	}
	remove_displaced_worker(state, person);
	return true;
}

void add_population_totals(sys::state const& state, std::map<uint32_t, std::array<double, 6>>& totals) {
	auto excluded = count_exclusions(state);
	for(auto household : all_households(state)) {
		float satisfaction[3] = {
			state.world.organization_get_household_life_satisfaction(household),
			state.world.organization_get_household_everyday_satisfaction(household),
			state.world.organization_get_household_luxury_satisfaction(household) };
		for(auto const& [pop, people] : member_populations(state, household, excluded)) {
			auto type = state.world.pop_get_poptype(pop);
			double per_capita[3] = {};
			state.world.for_each_commodity([&](dcon::commodity_id commodity) {
				float values[3] = {
					state.world.pop_type_get_life_needs(type, commodity),
					state.world.pop_type_get_everyday_needs(type, commodity),
					state.world.pop_type_get_luxury_needs(type, commodity) };
				for(int i = 0; i < 3; ++i) if(std::isfinite(values[i]) && values[i] > 0.0f) per_capita[i] += values[i];
			});
			auto& row = totals[pop.index()];
			for(int i = 0; i < 3; ++i) {
				auto wanted = per_capita[i] * double(people);
				row[i] += wanted;
				row[i + 3] += wanted * double(std::isfinite(satisfaction[i]) ? std::clamp(satisfaction[i], 0.0f, 1.0f) : 0.0f);
			}
		}
	}
}

} // namespace economy::households
