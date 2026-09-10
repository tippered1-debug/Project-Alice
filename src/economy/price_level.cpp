#include "price_level.hpp"

#include "culture.hpp"
#include "demographics.hpp"
#include "economy_constants.hpp"
#include "economy_stats.hpp"
#include "gamerule.hpp"
#include "system_state.hpp"

#include <algorithm>
#include <cmath>

namespace economy::price_level {
namespace {

constexpr float epsilon = 0.000001f;

float finite_nonnegative(float value) {
	return std::isfinite(value) ? std::max(0.0f, value) : 0.0f;
}

struct basket_totals {
	double nominal = 0.0;
	double reference = 0.0;
	double pressure_weighted = 0.0;
	double pressure_weight = 0.0;
	float population = 0.0f;
};

basket_totals measure_basket(sys::state const& state, dcon::market_id market) {
	basket_totals totals;
	if(!market || !state.world.market_is_valid(market))
		return totals;

	auto const zone = state.world.market_get_zone_from_local_market(market);
	if(!zone)
		return totals;
	totals.population = finite_nonnegative(
		state.world.state_instance_get_demographics(zone, demographics::total));

	for(uint32_t raw_commodity = 1; raw_commodity < state.world.commodity_size(); ++raw_commodity) {
		dcon::commodity_id const commodity{
			dcon::commodity_id::value_base_t(raw_commodity)};
		if(state.world.commodity_get_money_rgo(commodity))
			continue;

		auto const life_weight = finite_nonnegative(
			state.world.market_get_life_needs_weights(market, commodity));
		auto const everyday_weight = finite_nonnegative(
			state.world.market_get_everyday_needs_weights(market, commodity));
		double quantity = 0.0;
		state.world.for_each_pop_type([&](dcon::pop_type_id pop_type) {
			auto const population = finite_nonnegative(
				state.world.state_instance_get_demographics(
					zone, demographics::to_key(state, pop_type)));
			if(population <= 0.0f)
				return;
			auto const life = finite_nonnegative(
				state.world.pop_type_get_life_needs(pop_type, commodity));
			auto const everyday = finite_nonnegative(
				state.world.pop_type_get_everyday_needs(pop_type, commodity));
			quantity += double(population)
				* double(life * life_weight + everyday * everyday_weight);
		});
		if(quantity <= 0.0)
			continue;

		auto const price = finite_nonnegative(
			state.world.market_get_price(market, commodity));
		auto const reference_price = finite_nonnegative(
			state.world.commodity_get_cost(commodity));
		if(reference_price <= epsilon)
			continue;

		auto const reference_expenditure = quantity * double(reference_price);
		totals.nominal += quantity * double(price);
		totals.reference += reference_expenditure;

		auto const demand = finite_nonnegative(
			state.world.market_get_aggregated_demand_history(market, commodity));
		auto const supply = finite_nonnegative(
			state.world.market_get_aggregated_supply_history(market, commodity));
		auto const activity = demand + supply;
		auto const pressure = activity > epsilon
			? std::clamp((demand - supply) / activity, -1.0f, 1.0f) : 0.0f;
		totals.pressure_weighted += reference_expenditure * double(pressure);
		totals.pressure_weight += reference_expenditure;
	}
	return totals;
}

} // namespace

float calculate_index(float nominal_basket_cost, float reference_basket_cost) {
	auto const nominal = finite_nonnegative(nominal_basket_cost);
	auto const reference = finite_nonnegative(reference_basket_cost);
	if(reference <= epsilon)
		return 1.0f;
	return std::max(epsilon, nominal / reference);
}

float calculate_daily_inflation(float opening_cpi, float closing_cpi) {
	auto const opening = finite_nonnegative(opening_cpi);
	auto const closing = finite_nonnegative(closing_cpi);
	if(opening <= epsilon || closing <= epsilon)
		return 0.0f;
	return std::clamp(closing / opening - 1.0f, -0.99f, 100.0f);
}

float calculate_real_wage(float nominal_wage, float cpi) {
	auto const nominal = finite_nonnegative(nominal_wage);
	auto const price_level = finite_nonnegative(cpi);
	return price_level > epsilon ? nominal / price_level : nominal;
}

market_result evaluate_market(sys::state const& state, dcon::market_id market,
		float opening_cpi) {
	market_result result;
	result.enabled = gamerule::age_of_transformation_enabled(state);
	if(!result.enabled)
		return result;

	auto const basket = measure_basket(state, market);
	result.cpi = calculate_index(float(basket.nominal), float(basket.reference));
	result.daily_inflation = calculate_daily_inflation(opening_cpi, result.cpi);
	result.demand_pressure = basket.pressure_weight > double(epsilon)
		? std::clamp(float(basket.pressure_weighted / basket.pressure_weight), -1.0f, 1.0f)
		: 0.0f;
	result.population = basket.population;
	return result;
}

nation_result evaluate_nation(sys::state const& state, dcon::nation_id nation) {
	nation_result result;
	result.enabled = gamerule::age_of_transformation_enabled(state);
	if(!result.enabled || !nation)
		return result;

	double cpi = 0.0;
	double inflation = 0.0;
	double pressure = 0.0;
	double population = 0.0;
	state.world.nation_for_each_state_ownership(nation, [&](auto ownership) {
		auto const local_state = state.world.state_ownership_get_state(ownership);
		auto const market = state.world.state_instance_get_market_from_local_market(local_state);
		if(!market)
			return;
		auto const index = std::size_t(market.index());
		if(index >= state.price_level_account.markets.size())
			return;
		auto const& local = state.price_level_account.markets[index];
		auto const weight = double(std::max(0.0f, local.population));
		cpi += double(local.cpi) * weight;
		inflation += double(local.daily_inflation) * weight;
		pressure += double(local.demand_pressure) * weight;
		population += weight;
	});
	if(population > double(epsilon)) {
		result.cpi = float(cpi / population);
		result.daily_inflation = float(inflation / population);
		result.demand_pressure = float(pressure / population);
		result.population = float(population);
	}
	return result;
}

float market_cpi(sys::state const& state, dcon::market_id market) {
	if(!market)
		return 1.0f;
	auto const index = std::size_t(market.index());
	if(index < state.price_level_account.markets.size())
		return state.price_level_account.markets[index].cpi;
	return 1.0f;
}

float real_wage(sys::state const& state, dcon::province_id province, int32_t labor_type) {
	if(!province || labor_type < 0 || labor_type >= labor::total)
		return 0.0f;
	auto const nominal = state.world.province_get_labor_price(province, labor_type);
	auto const local_state = state.world.province_get_state_membership(province);
	if(!local_state)
		return finite_nonnegative(nominal);
	auto const market = state.world.state_instance_get_market_from_local_market(local_state);
	return calculate_real_wage(
		nominal, market_cpi(state, market));
}

void begin_day(sys::state& state) {
	auto& account = state.price_level_account;
	account.enabled = gamerule::age_of_transformation_enabled(state);
	account.opening_cpi.assign(state.world.market_size(), 1.0f);
	account.markets.assign(state.world.market_size(), {});
	account.world = {};
	account.world.enabled = account.enabled;
	if(!account.enabled)
		return;
	state.world.for_each_market([&](dcon::market_id market) {
		auto const current = evaluate_market(state, market);
		account.opening_cpi[market.index()] = current.cpi;
	});
}

void update(sys::state& state) {
	auto& account = state.price_level_account;
	if(!account.enabled)
		return;
	if(account.opening_cpi.size() != state.world.market_size())
		begin_day(state);

	double world_cpi = 0.0;
	double world_inflation = 0.0;
	double world_pressure = 0.0;
	double world_population = 0.0;
	state.world.for_each_market([&](dcon::market_id market) {
		auto const result = evaluate_market(state, market,
			account.opening_cpi[market.index()]);
		account.markets[market.index()] = result;
		auto const weight = double(std::max(0.0f, result.population));
		world_cpi += double(result.cpi) * weight;
		world_inflation += double(result.daily_inflation) * weight;
		world_pressure += double(result.demand_pressure) * weight;
		world_population += weight;
	});
	account.world.enabled = true;
	account.world.population = float(world_population);
	if(world_population > double(epsilon)) {
		account.world.cpi = float(world_cpi / world_population);
		account.world.daily_inflation = float(world_inflation / world_population);
		account.world.demand_pressure = float(world_pressure / world_population);
	}
}

void initialize(sys::state& state) {
	begin_day(state);
	update(state);
	// Loading or scenario initialization establishes a price-level baseline; it
	// is not itself a day of inflation.
	for(auto& market : state.price_level_account.markets)
		market.daily_inflation = 0.0f;
	state.price_level_account.world.daily_inflation = 0.0f;
}

} // namespace economy::price_level
