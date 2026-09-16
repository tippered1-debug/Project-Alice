#pragma once

#include "culture/transformation_politics.hpp"
// The synthetic lab sizes dcon arrays directly, so this header needs the
// economy declarations in its own right rather than through whichever
// translation unit happens to include it.
#include "economy/advanced_province_buildings.hpp"
#include "economy/demographics.hpp"
#include "economy/economy_constants.hpp"
#include "economy/economy_stats.hpp"
#include "economy/human_development.hpp"
#include "economy/industry_ownership.hpp"
#include "economy/market_clearing.hpp"
#include "economy/price.hpp"
#include "economy/world_trade_capacity.hpp"
#include "gamerule/gamerule.hpp"
#include "gamestate/system_state.hpp"
#include "military/military.hpp"
#include "nations/diplomatic_crisis_dynamics.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace sys::simulation {

// The snapshot deliberately contains only aggregates. Iterating entities in their
// stable dcon order makes reports reproducible without exposing a very large or
// privacy-sensitive per-entity dump.
enum class invariant_field : uint8_t {
	none,
	inflation,
	pop_size,
	pop_savings,
	pop_satisfaction,
	pop_employment,
	commodity_price,
	commodity_supply,
	commodity_demand,
	labor_price,
	labor_supply,
	labor_demand,
	labor_demand_satisfaction,
	labor_supply_sold,
	army_supply_reserve,
	supply_depot_stockpile,
	nation_treasury,
	nation_debt,
	money_supply,
	market_gdp,
	factory_profit,
	control_ratio,
	legitimacy,
	coalition_power,
	government_stability,
	government_confidence,
	banking_health,
	credit_rate,
	trade_route_volume,
	trade_route_capacity,
	trade_congestion,
	crisis_metric,
	aggregate
};

inline constexpr std::string_view invariant_field_name(invariant_field field) noexcept {
	switch(field) {
	case invariant_field::none: return "none";
	case invariant_field::inflation: return "inflation";
	case invariant_field::pop_size: return "pop_size";
	case invariant_field::pop_savings: return "pop_savings";
	case invariant_field::pop_satisfaction: return "pop_satisfaction";
	case invariant_field::pop_employment: return "pop_employment";
	case invariant_field::commodity_price: return "commodity_price";
	case invariant_field::commodity_supply: return "commodity_supply";
	case invariant_field::commodity_demand: return "commodity_demand";
	case invariant_field::labor_price: return "labor_price";
	case invariant_field::labor_supply: return "labor_supply";
	case invariant_field::labor_demand: return "labor_demand";
	case invariant_field::labor_demand_satisfaction: return "labor_demand_satisfaction";
	case invariant_field::labor_supply_sold: return "labor_supply_sold";
	case invariant_field::army_supply_reserve: return "army_supply_reserve";
	case invariant_field::supply_depot_stockpile: return "supply_depot_stockpile";
	case invariant_field::nation_treasury: return "nation_treasury";
	case invariant_field::nation_debt: return "nation_debt";
	case invariant_field::money_supply: return "money_supply";
	case invariant_field::market_gdp: return "market_gdp";
	case invariant_field::factory_profit: return "factory_profit";
	case invariant_field::control_ratio: return "control_ratio";
	case invariant_field::legitimacy: return "legitimacy";
	case invariant_field::coalition_power: return "coalition_power";
	case invariant_field::government_stability: return "government_stability";
	case invariant_field::government_confidence: return "government_confidence";
	case invariant_field::banking_health: return "banking_health";
	case invariant_field::credit_rate: return "credit_rate";
	case invariant_field::trade_route_volume: return "trade_route_volume";
	case invariant_field::trade_route_capacity: return "trade_route_capacity";
	case invariant_field::trade_congestion: return "trade_congestion";
	case invariant_field::crisis_metric: return "crisis_metric";
	case invariant_field::aggregate: return "aggregate";
	}
	return "unknown";
}

struct first_violation {
	invariant_field field = invariant_field::none;
	int32_t entity_index = -1;
	int32_t subindex = -1;
	float value = 0.0f;
};

struct invariant_counts {
	uint64_t nonfinite = 0;
	uint64_t negative = 0;
	uint64_t out_of_range = 0;
	first_violation first{};

	[[nodiscard]] constexpr uint64_t total() const noexcept {
		return nonfinite + negative + out_of_range;
	}

	[[nodiscard]] constexpr bool ok() const noexcept {
		return total() == 0;
	}
};

struct aggregate_snapshot {
	uint64_t tick = 0;
	int32_t date_raw = 0;
	bool age_of_transformation = false;
	sys::checksum_key save_checksum{};

	uint64_t pop_count = 0;
	uint64_t province_count = 0;
	uint64_t market_count = 0;
	uint64_t commodity_market_cells = 0;
	uint64_t labor_market_cells = 0;
	uint64_t army_count = 0;
	uint64_t depot_count = 0;
	uint64_t nation_count = 0;
	uint64_t factory_count = 0;
	uint64_t unprofitable_factory_count = 0;
	uint64_t owned_province_count = 0;
	uint64_t owner_controlled_province_count = 0;
	uint64_t foreign_controlled_province_count = 0;
	uint64_t rebel_controlled_province_count = 0;
	uint64_t uncontrolled_owned_province_count = 0;
	uint64_t transformed_nation_count = 0;
	uint64_t stable_government_count = 0;
	uint64_t contested_government_count = 0;
	uint64_t fragile_government_count = 0;
	uint64_t government_turnover_count = 0;
	uint64_t cabinet_member_count = 0;
	uint64_t trade_route_count = 0;

	// Endogenous consumer-price inflation. The legacy balance-decay factor is
	// retained separately so reports cannot confuse the two again.
	double inflation = 0.0;
	double consumer_price_index = 1.0;
	double consumer_demand_pressure = 0.0;
	double population = 0.0;
	double pop_savings = 0.0;
	// Population-weighted shares. Divide each by population to obtain the
	// average fulfilment of the corresponding need tier.
	double population_weighted_life_needs = 0.0;
	double population_weighted_everyday_needs = 0.0;
	double population_weighted_luxury_needs = 0.0;
	double unemployed_population = 0.0;
	double commodity_price_sum = 0.0;
	double commodity_supply = 0.0;
	double commodity_demand = 0.0;
	double machine_parts_supply = 0.0;
	double machine_parts_demand = 0.0;
	double machine_parts_import = 0.0;
	double labor_price_sum = 0.0;
	double real_labor_price_sum = 0.0;
	double labor_supply = 0.0;
	double labor_demand = 0.0;
	double employed_labor = 0.0;
	std::array<double, economy::labor::total> labor_price_by_type{};
	std::array<double, economy::labor::total> labor_demand_by_type{};
	double maximum_labor_price = 0.0;
	double maximum_labor_demand = 0.0;
	int32_t maximum_labor_price_type = -1;
	int32_t maximum_labor_demand_type = -1;
	double private_school_size = 0.0;
	double public_school_size = 0.0;
	double maximum_private_school_size = 0.0;
	double maximum_public_school_size = 0.0;
	double factory_size = 0.0;
	double rgo_target_employment = 0.0;
	double army_supply_reserve_sum = 0.0;
	double minimum_army_supply_reserve = 0.0;
	double depot_stockpile = 0.0;
	double treasury = 0.0;
	double government_debt = 0.0;
	double market_gdp = 0.0;
	// Profit is deliberately signed: a negative value is an economic signal,
	// not an invariant violation.
	double factory_profit = 0.0;
	double control_ratio_sum = 0.0;
	double minimum_control_ratio = 0.0;
	double legitimacy_sum = 0.0;
	double minimum_legitimacy = 0.0;
	double coalition_power_sum = 0.0;
	double government_stability_sum = 0.0;
	double minimum_government_stability = 0.0;
	double cabinet_confidence_sum = 0.0;
	double minimum_cabinet_confidence = 0.0;
	// Industrial ownership, weighted by each province's capitalized industry
	// value so a province with no industry cannot sway the mix.
	double industry_value = 0.0;
	double industry_value_capitalists = 0.0;
	double industry_value_landed = 0.0;
	double industry_value_state = 0.0;
	double industry_value_foreign = 0.0;
	double industry_value_workers = 0.0;
	double industry_turnover = 0.0;
	double maximum_industry_state_share = 0.0;
	double maximum_industry_foreign_share = 0.0;
	double trade_route_cargo = 0.0;
	double trade_effective_capacity = 0.0;
	double trade_congestion_sum = 0.0;
	double maximum_trade_congestion = 0.0;
	double trade_requested_cargo = 0.0;
	double trade_delivered_cargo = 0.0;
	double trade_cargo_in_transit = 0.0;
	double trade_land_capacity_demand = 0.0;
	double trade_sea_capacity_demand = 0.0;
	double minimum_foreign_settlement = 1.0;
	double maximum_exchange_rate_multiplier = 1.0;
	double market_quantity_traded = 0.0;
	double market_unfilled_life_needs = 0.0;
	double market_unfilled_intermediate = 0.0;
	double market_unfilled_luxury_needs = 0.0;
	nations::diplomatic_crisis_dynamics::escalation_stage crisis_stage =
		nations::diplomatic_crisis_dynamics::escalation_stage::inactive;
	double crisis_temperature = 0.0;
	double crisis_escalation_pressure = 0.0;
	double crisis_settlement_pressure = 0.0;
	double crisis_war_risk = 0.0;
	double baseline_natural_growth = 0.0;
	double starvation_loss = 0.0;
	double housing_loss = 0.0;
	double transition_reduction = 0.0;
	double net_natural_change = 0.0;
	double population_weighted_housing_access = 0.0;
	double population_weighted_urbanization = 0.0;
	double population_weighted_overcrowding = 0.0;
	double population_weighted_education_access = 0.0;
	double population_weighted_human_development = 0.0;
	double gross_internal_migration = 0.0;
	double gross_international_migration = 0.0;
	// Optional, single-country probe for deterministic headless regression runs.
	// It is deliberately absent unless the caller supplies a nation, so normal
	// aggregate reports and the game UI remain unaffected.
	int32_t tracked_nation_index = -1;
	double tracked_nation_population = 0.0;
	double tracked_nation_monthly_natural_change = 0.0;
	double tracked_nation_monthly_army_attrition = 0.0;
	double tracked_nation_daily_internal_migration = 0.0;
	double tracked_nation_daily_external_migration = 0.0;
	double tracked_nation_literacy = 0.0;
	double tracked_nation_estimated_literacy_change = 0.0;
	double tracked_nation_education_access = 0.0;
	double tracked_nation_machine_parts_supply = 0.0;
	double tracked_nation_machine_parts_demand = 0.0;
	double tracked_nation_machine_parts_import = 0.0;
	double tracked_nation_machine_parts_buy_probability = 0.0;
	double tracked_nation_consumer_price_index = 1.0;
	double tracked_nation_inflation = 0.0;
	double tracked_nation_real_wage_sum = 0.0;
	uint64_t tracked_nation_market_count = 0;
	double tracked_nation_legitimacy = 0.0;
	double tracked_nation_coalition_power = 0.0;
	double tracked_nation_government_stability = 0.0;
	double tracked_nation_minimum_cabinet_confidence = 0.0;
	int32_t tracked_nation_government_established_on = 0;
	uint64_t tracked_nation_government_groups = 0;
	bool tracked_nation_government_changed = false;

	invariant_counts observed_violations{};
};

struct validation_report {
	bool valid = true;
	invariant_counts violations{};
};

namespace detail {

inline void remember_first(invariant_counts& counts, invariant_field field, int32_t entity_index,
		int32_t subindex, float value) noexcept {
	if(counts.first.field == invariant_field::none) {
		counts.first = first_violation{field, entity_index, subindex, value};
	}
}

inline bool observe_nonnegative(invariant_counts& counts, invariant_field field, int32_t entity_index,
		int32_t subindex, float value, float maximum = std::numeric_limits<float>::infinity()) noexcept {
	if(!std::isfinite(value)) {
		++counts.nonfinite;
		remember_first(counts, field, entity_index, subindex, value);
		return false;
	}
	if(value < 0.0f) {
		++counts.negative;
		remember_first(counts, field, entity_index, subindex, value);
		return false;
	}
	if(value > maximum) {
		++counts.out_of_range;
		remember_first(counts, field, entity_index, subindex, value);
		return false;
	}
	return true;
}

inline bool observe_finite(invariant_counts& counts, invariant_field field, int32_t entity_index,
		int32_t subindex, float value) noexcept {
	if(!std::isfinite(value)) {
		++counts.nonfinite;
		remember_first(counts, field, entity_index, subindex, value);
		return false;
	}
	return true;
}

inline void write_json_number(std::ostringstream& out, float value) {
	if(std::isfinite(value)) {
		out << value;
	} else {
		out << "null";
	}
}

inline void write_checksum_hex(std::ostringstream& out, sys::checksum_key const& checksum) {
	constexpr char digits[] = "0123456789abcdef";
	for(auto byte : checksum.key) {
		out << digits[byte >> 4] << digits[byte & 0x0f];
	}
}

} // namespace detail

[[nodiscard]] inline aggregate_snapshot collect_snapshot(sys::state& state, uint64_t tick = 0,
		dcon::nation_id tracked_nation = {}) {
	aggregate_snapshot result{};
	result.tick = tick;
	result.date_raw = state.current_date.to_raw_value();
	result.age_of_transformation = gamerule::age_of_transformation_enabled(state);
	// Hash exactly the serialized save section (excluding local-only fields), so
	// repeated runs and save/load continuations share the same comparison key.
	result.save_checksum = state.get_save_checksum();

	if(state.price_level_account.world.enabled) {
		auto const& prices = state.price_level_account.world;
		if(detail::observe_nonnegative(result.observed_violations,
				invariant_field::inflation, -1, 0, prices.cpi))
			result.consumer_price_index = prices.cpi;
		if(detail::observe_finite(result.observed_violations,
				invariant_field::inflation, -1, 1, prices.daily_inflation))
			result.inflation = prices.daily_inflation;
		if(detail::observe_finite(result.observed_violations,
				invariant_field::inflation, -1, 2, prices.demand_pressure))
			result.consumer_demand_pressure = prices.demand_pressure;
	}

	// Aggregate monetary diagnostics were removed. Concrete account domains own
	// balances; the legacy nation-wide money ledger is no longer canonical.

	state.world.for_each_nation([&](dcon::nation_id nation) {
		++result.nation_count;
		auto const entity = int32_t(nation.index());
		auto const debt = 0.0f;
		if(detail::observe_nonnegative(result.observed_violations, invariant_field::nation_debt,
				entity, -1, debt)) {
			result.government_debt += double(debt);
		}

	});

	state.world.for_each_pop([&](dcon::pop_id pop) {
		++result.pop_count;
		auto const entity = int32_t(pop.index());
		auto const size = state.world.pop_get_size(pop);
		auto const savings = state.world.pop_get_savings(pop);
		if(detail::observe_nonnegative(result.observed_violations, invariant_field::pop_size, entity, -1, size)) {
			result.population += double(size);
			auto const satisfaction = state.world.pop_get_satisfaction(pop);
			if(detail::observe_nonnegative(result.observed_violations, invariant_field::pop_satisfaction,
					entity, -1, satisfaction, 1.0f)) {
				result.population_weighted_life_needs +=
					double(size) * pop_demographics::get_life_needs(state, pop);
				result.population_weighted_everyday_needs +=
					double(size) * pop_demographics::get_everyday_needs(state, pop);
				result.population_weighted_luxury_needs +=
					double(size) * pop_demographics::get_luxury_needs(state, pop);
			}
			auto const employed = pop_demographics::get_employment(state, pop);
			if(detail::observe_nonnegative(result.observed_violations, invariant_field::pop_employment,
					entity, 1, employed, size)) {
				result.unemployed_population += double(size - employed);
			}
			auto const development = economy::human_development::evaluate_pop(state, pop);
			auto const province = state.world.pop_get_province_from_pop_location(pop);
			auto const owner = province
				? state.world.province_get_nation_from_province_ownership(province)
				: dcon::nation_id{};
			if(development.enabled && owner) {
				auto const legacy_growth = demographics::get_pop_growth_modifiers(state, pop);
				auto const starvation = demographics::get_net_pop_starvation_penalty(
					state, pop, legacy_growth);
				auto const account = economy::human_development::calculate_account(
					size, legacy_growth, starvation, development);
				result.baseline_natural_growth += account.baseline_natural_growth;
				result.starvation_loss += account.starvation_loss;
				result.housing_loss += account.housing_loss;
				result.transition_reduction += account.transition_reduction;
				result.net_natural_change += account.net_natural_change;
				result.population_weighted_housing_access +=
					double(size) * development.factors.housing_access;
				result.population_weighted_urbanization +=
					double(size) * development.factors.urbanization;
				result.population_weighted_overcrowding +=
					double(size) * development.overcrowding;
				result.population_weighted_education_access +=
					double(size) * development.factors.education_access;
				result.population_weighted_human_development +=
					double(size) * development.human_development_index;
			}
		}
		if(detail::observe_nonnegative(result.observed_violations, invariant_field::pop_savings, entity, -1, savings)) {
			result.pop_savings += double(savings);
		}
	});

	if(tracked_nation && state.world.nation_is_valid(tracked_nation)) {
		result.tracked_nation_index = int32_t(tracked_nation.index());
		auto const national_prices = economy::price_level::evaluate_nation(state, tracked_nation);
		if(national_prices.enabled) {
			result.tracked_nation_consumer_price_index = national_prices.cpi;
			result.tracked_nation_inflation = national_prices.daily_inflation;
		}
		for(auto province : state.world.nation_get_province_ownership(tracked_nation)) {
			auto const province_id = province.get_province();
			for(auto membership : state.world.province_get_pop_location(province_id)) {
				auto const pop = membership.get_pop();
				auto const size = double(state.world.pop_get_size(pop));
				result.tracked_nation_population += size;
				result.tracked_nation_literacy += size * double(pop_demographics::get_literacy(state, pop));
				result.tracked_nation_education_access += size
					* double(economy::human_development::evaluate_pop(state, pop).factors.education_access);
			}
			result.tracked_nation_daily_internal_migration +=
				double(state.world.province_get_daily_net_migration(province_id));
			result.tracked_nation_daily_external_migration +=
				double(state.world.province_get_daily_net_immigration(province_id));
		}
		result.tracked_nation_monthly_natural_change =
			double(nations::get_monthly_pop_increase_of_nation(state, tracked_nation));
		result.tracked_nation_monthly_army_attrition =
			double(military::estimated_monthly_army_pop_attrition_loss(state, tracked_nation));
		if(result.tracked_nation_population > 0.0) {
			result.tracked_nation_literacy /= result.tracked_nation_population;
			result.tracked_nation_education_access /= result.tracked_nation_population;
		}
		result.tracked_nation_estimated_literacy_change =
			double(demographics::get_estimated_literacy_change(state, tracked_nation));

		dcon::commodity_id machine_parts{};
		state.world.for_each_commodity([&](dcon::commodity_id commodity) {
			if(state.to_string_view(state.world.commodity_get_name(commodity)) == "machine_parts")
				machine_parts = commodity;
		});
		if(machine_parts) {
			state.world.nation_for_each_state_ownership(tracked_nation, [&](auto ownership) {
				auto const local_state = state.world.state_ownership_get_state(ownership);
				auto const market = state.world.state_instance_get_market_from_local_market(local_state);
				++result.tracked_nation_market_count;
				result.tracked_nation_machine_parts_supply += state.world.market_get_supply(market, machine_parts);
				result.tracked_nation_machine_parts_demand += state.world.market_get_demand(market, machine_parts);
				result.tracked_nation_machine_parts_import += state.world.market_get_import(market, machine_parts);
				result.tracked_nation_machine_parts_buy_probability +=
					state.world.market_get_actual_probability_to_buy(market, machine_parts);
			});
			if(result.tracked_nation_market_count > 0)
				result.tracked_nation_machine_parts_buy_probability /=
					double(result.tracked_nation_market_count);
		}
	}

	dcon::commodity_id observed_machine_parts{};
	state.world.for_each_commodity([&](dcon::commodity_id commodity) {
		if(state.to_string_view(state.world.commodity_get_name(commodity)) == "machine_parts")
			observed_machine_parts = commodity;
	});
	state.world.for_each_market([&](dcon::market_id market) {
		++result.market_count;
		auto const gdp = state.world.market_get_gdp(market);
		if(detail::observe_nonnegative(result.observed_violations, invariant_field::market_gdp,
				int32_t(market.index()), -1, gdp)) {
			result.market_gdp += double(gdp);
		}
		state.world.for_each_commodity([&](dcon::commodity_id commodity) {
			++result.commodity_market_cells;
			auto const entity = int32_t(market.index());
			auto const subindex = int32_t(commodity.index());
			auto const price = state.world.market_get_price(market, commodity);
			auto const supply = state.world.market_get_supply(market, commodity);
			auto const demand = state.world.market_get_demand(market, commodity);
			auto const maximum_price = economy::price_properties::commodity::maximum(
				float(state.world.commodity_get_cost(commodity)));
			if(detail::observe_nonnegative(result.observed_violations, invariant_field::commodity_price,
					entity, subindex, price, maximum_price)) {
				result.commodity_price_sum += double(price);
			}
			if(detail::observe_nonnegative(result.observed_violations, invariant_field::commodity_supply,
					entity, subindex, supply)) {
				result.commodity_supply += double(supply);
			}
			if(detail::observe_nonnegative(result.observed_violations, invariant_field::commodity_demand,
					entity, subindex, demand)) {
				result.commodity_demand += double(demand);
			}
			if(commodity == observed_machine_parts) {
				result.machine_parts_supply += double(supply);
				result.machine_parts_demand += double(demand);
				result.machine_parts_import += double(state.world.market_get_import(market, commodity));
			}
			if(state.market_clearing_account.enabled) {
				auto const ledger_index = size_t(market.index())
					* size_t(state.market_clearing_account.commodity_count)
					+ size_t(commodity.index());
				if(ledger_index < state.market_clearing_account.quantity_traded.size()) {
					result.market_quantity_traded +=
						double(state.market_clearing_account.quantity_traded[ledger_index]);
					auto add_unfilled = [&](economy::market_clearing::demand_class category,
							double& target) {
						auto const category_index = size_t(category);
						auto const requested = state.market_clearing_account.demand[category_index][ledger_index];
						auto const fill = state.market_clearing_account.fill[category_index][ledger_index];
						target += double(std::max(0.0f, requested * (1.0f - std::clamp(fill, 0.0f, 1.0f))));
					};
					add_unfilled(economy::market_clearing::demand_class::life_needs,
						result.market_unfilled_life_needs);
					add_unfilled(economy::market_clearing::demand_class::intermediate,
						result.market_unfilled_intermediate);
					add_unfilled(economy::market_clearing::demand_class::luxury_needs,
						result.market_unfilled_luxury_needs);
				}
			}
		});
	});

	state.world.for_each_factory([&](dcon::factory_id factory) {
		++result.factory_count;
		if(state.world.factory_get_unprofitable(factory))
			++result.unprofitable_factory_count;
		auto const profit = state.world.factory_get_profit(factory);
		if(detail::observe_finite(result.observed_violations, invariant_field::factory_profit,
				int32_t(factory.index()), -1, profit)) {
			result.factory_profit += double(profit);
		}
	});

	bool has_owned_control = false;
	state.world.for_each_province([&](dcon::province_id province) {
		++result.province_count;
		auto const entity = int32_t(province.index());
		if(auto const owner = state.world.province_get_nation_from_province_ownership(province); owner) {
			++result.owned_province_count;
			auto const controller =
				state.world.province_get_nation_from_province_control(province);
			auto const rebel_controller =
				state.world.province_get_rebel_faction_from_province_rebel_control(province);
			if(controller == owner) {
				++result.owner_controlled_province_count;
			} else if(controller) {
				++result.foreign_controlled_province_count;
			} else if(rebel_controller) {
				++result.rebel_controlled_province_count;
			} else {
				++result.uncontrolled_owned_province_count;
			}
			auto const control = state.world.province_get_control_ratio(province);
			if(detail::observe_nonnegative(result.observed_violations, invariant_field::control_ratio,
					entity, -1, control, 1.0f)) {
				result.control_ratio_sum += double(control);
				if(!has_owned_control || control < result.minimum_control_ratio)
					result.minimum_control_ratio = control;
				has_owned_control = true;
			}
		}
		for(int32_t labor_type = 0; labor_type < economy::labor::total; ++labor_type) {
			++result.labor_market_cells;
			auto const price = state.world.province_get_labor_price(province, labor_type);
			auto const supply = state.world.province_get_labor_supply(province, labor_type);
			auto const demand = state.world.province_get_labor_demand(province, labor_type);
			auto const demand_satisfaction =
				state.world.province_get_labor_demand_satisfaction(province, labor_type);
			auto const supply_sold = state.world.province_get_labor_supply_sold(province, labor_type);

			if(detail::observe_nonnegative(result.observed_violations, invariant_field::labor_price,
					entity, labor_type, price, economy::price_properties::labor::max)) {
				result.labor_price_sum += double(price);
				auto const real_price = economy::price_level::real_wage(
					state, province, labor_type);
				result.real_labor_price_sum += double(real_price);
				if(result.tracked_nation_index >= 0
						&& state.world.province_get_nation_from_province_ownership(province)
						== tracked_nation)
					result.tracked_nation_real_wage_sum += double(real_price);
				result.labor_price_by_type[size_t(labor_type)] += double(price);
				if(double(price) > result.maximum_labor_price) {
					result.maximum_labor_price = double(price);
					result.maximum_labor_price_type = labor_type;
				}
			}
			if(detail::observe_nonnegative(result.observed_violations, invariant_field::labor_supply,
					entity, labor_type, supply)) {
				result.labor_supply += double(supply);
				if(std::isfinite(supply_sold) && supply_sold >= 0.0f && supply_sold <= 1.0f) {
					result.employed_labor += double(supply) * double(supply_sold);
				}
			}
			if(detail::observe_nonnegative(result.observed_violations, invariant_field::labor_demand,
					entity, labor_type, demand)) {
				result.labor_demand += double(demand);
				result.labor_demand_by_type[size_t(labor_type)] += double(demand);
				if(double(demand) > result.maximum_labor_demand) {
					result.maximum_labor_demand = double(demand);
					result.maximum_labor_demand_type = labor_type;
				}
			}
			detail::observe_nonnegative(result.observed_violations, invariant_field::labor_demand_satisfaction,
				entity, labor_type, demand_satisfaction, 1.0f);
			detail::observe_nonnegative(result.observed_violations, invariant_field::labor_supply_sold,
				entity, labor_type, supply_sold, 1.0f);
		}

		auto const private_school = double(state.world.province_get_advanced_province_building_private_size(
			province, advanced_province_buildings::list::schools_and_universities));
		auto const public_school = double(state.world.province_get_advanced_province_building_national_size(
			province, advanced_province_buildings::list::schools_and_universities));
		if(std::isfinite(private_school) && private_school >= 0.0) {
			result.private_school_size += private_school;
			result.maximum_private_school_size = std::max(result.maximum_private_school_size, private_school);
		}
		if(std::isfinite(public_school) && public_school >= 0.0) {
			result.public_school_size += public_school;
			result.maximum_public_school_size = std::max(result.maximum_public_school_size, public_school);
		}
		for(auto factory_location : state.world.province_get_factory_location(province)) {
			auto const size = double(factory_location.get_factory().get_size());
			if(std::isfinite(size) && size >= 0.0)
				result.factory_size += size;
		}
		state.world.for_each_commodity([&](dcon::commodity_id commodity) {
			auto const target = double(state.world.province_get_rgo_target_employment(province, commodity));
			if(std::isfinite(target) && target >= 0.0)
				result.rgo_target_employment += target;
		});

		{
			auto const value = double(state.world.province_get_industry_market_value(province));
			if(std::isfinite(value) && value > 0.0) {
				auto const owners = economy::industry_ownership::current_distribution(state, province);
				result.industry_value += value;
				result.industry_value_capitalists += value * double(owners.capitalists);
				result.industry_value_landed += value * double(owners.landed_elites);
				result.industry_value_state += value * double(owners.state);
				result.industry_value_foreign += value * double(owners.foreign);
				result.industry_value_workers += value * double(owners.workers);
			}
			{
				auto const owners = economy::industry_ownership::current_distribution(state, province);
				result.maximum_industry_state_share = std::max(
					result.maximum_industry_state_share, double(owners.state));
				result.maximum_industry_foreign_share = std::max(
					result.maximum_industry_foreign_share, double(owners.foreign));
			}
			auto const turnover = double(state.world.province_get_industry_market_turnover(province));
			if(std::isfinite(turnover))
				result.industry_turnover += turnover;
		}

		auto const stockpile = state.world.province_get_supply_depot_stockpile(province);
		if(detail::observe_nonnegative(result.observed_violations, invariant_field::supply_depot_stockpile,
				entity, -1, stockpile)) {
			result.depot_stockpile += double(stockpile);
		}
		if(state.world.province_get_is_supply_depot(province) || stockpile != 0.0f
				|| state.world.province_get_supply_depot_owner(province)) {
			++result.depot_count;
		}
		auto const internal_migration = state.world.province_get_daily_net_migration(province);
		if(std::isfinite(internal_migration))
			result.gross_internal_migration += std::abs(double(internal_migration)) * 0.5;
		auto const international_migration = state.world.province_get_daily_net_immigration(province);
		if(std::isfinite(international_migration))
			result.gross_international_migration += std::abs(double(international_migration)) * 0.5;
	});

	bool has_army = false;
	state.world.for_each_army([&](dcon::army_id army) {
		++result.army_count;
		auto const reserve = state.world.army_get_supply_reserve(army);
		if(detail::observe_nonnegative(result.observed_violations, invariant_field::army_supply_reserve,
				int32_t(army.index()), -1, reserve, 1.0f)) {
			result.army_supply_reserve_sum += double(reserve);
			if(!has_army || reserve < result.minimum_army_supply_reserve) {
				result.minimum_army_supply_reserve = reserve;
			}
			has_army = true;
		}
	});

	bool has_legitimacy = false;
	bool has_government = false;
	bool has_cabinet_member = false;
	if(state.transformation_politics_cache_valid
		&& state.transformation_politics_cache.size() == state.world.nation_size()) {
		for(std::size_t index = 0; index < state.transformation_politics_cache.size(); ++index) {
			auto const& political = state.transformation_politics_cache[index];
			if(!political.enabled)
				continue;
			++result.transformed_nation_count;
			if(detail::observe_nonnegative(result.observed_violations, invariant_field::legitimacy,
					int32_t(index), -1, political.legitimacy.total, 100.0f)) {
				result.legitimacy_sum += double(political.legitimacy.total);
				if(!has_legitimacy || political.legitimacy.total < result.minimum_legitimacy)
					result.minimum_legitimacy = political.legitimacy.total;
				has_legitimacy = true;
			}
			if(detail::observe_nonnegative(result.observed_violations, invariant_field::coalition_power,
					int32_t(index), -1, political.coalition.power_share, 1.0f)) {
				result.coalition_power_sum += double(political.coalition.power_share);
			}
			if(detail::observe_nonnegative(result.observed_violations, invariant_field::government_stability,
					int32_t(index), -1, political.government.stability, 1.0f)) {
				result.government_stability_sum += double(political.government.stability);
				if(!has_government || political.government.stability < result.minimum_government_stability)
					result.minimum_government_stability = political.government.stability;
				has_government = true;
			}
			if(!political.coalition.has_working_majority
				|| political.government.stability < 0.45f
				|| political.legitimacy.total < 35.0f) {
				++result.fragile_government_count;
			} else if(political.government.stability < 0.62f
				|| political.legitimacy.total < 55.0f) {
				++result.contested_government_count;
			} else {
				++result.stable_government_count;
			}
			if(political.government.changed_this_refresh)
				++result.government_turnover_count;

			double nation_minimum_confidence = 0.0;
			bool nation_has_cabinet_member = false;
			for(std::size_t group = 0;
					group < politics::transformation::interest_group_count; ++group) {
				auto const bit = politics::transformation::group_bit(
					politics::transformation::interest_group_id(group));
				if((political.government.groups & bit) == 0)
					continue;
				auto const confidence = political.government.confidence[group];
				if(detail::observe_nonnegative(result.observed_violations,
						invariant_field::government_confidence, int32_t(index), int32_t(group),
						confidence, 1.0f)) {
					++result.cabinet_member_count;
					result.cabinet_confidence_sum += double(confidence);
					if(!has_cabinet_member || confidence < result.minimum_cabinet_confidence)
						result.minimum_cabinet_confidence = confidence;
					if(!nation_has_cabinet_member || confidence < nation_minimum_confidence)
						nation_minimum_confidence = confidence;
					has_cabinet_member = true;
					nation_has_cabinet_member = true;
				}
			}

			if(tracked_nation && std::size_t(tracked_nation.index()) == index) {
				result.tracked_nation_legitimacy = political.legitimacy.total;
				result.tracked_nation_coalition_power = political.coalition.power_share;
				result.tracked_nation_government_stability = political.government.stability;
				result.tracked_nation_minimum_cabinet_confidence = nation_minimum_confidence;
				result.tracked_nation_government_established_on =
					political.government.established_on;
				result.tracked_nation_government_groups =
					uint64_t(political.government.groups);
				result.tracked_nation_government_changed =
					political.government.changed_this_refresh;
			}
		}
	}

	auto const shipment_allocation = economy::world_trade::clear_trade_shipments(state);
	state.world.for_each_market([&](dcon::market_id market) {
		result.trade_land_capacity_demand += double(shipment_allocation.requested_capacity(
			market, economy::world_trade::transport_mode::land));
		result.trade_sea_capacity_demand += double(shipment_allocation.requested_capacity(
			market, economy::world_trade::transport_mode::sea));
	});
	if(shipment_allocation.enabled) {
		state.world.for_each_nation([&](dcon::nation_id nation) {
			result.minimum_foreign_settlement = std::min(result.minimum_foreign_settlement,
				double(shipment_allocation.import_settlement(nation)));
			result.maximum_exchange_rate_multiplier = std::max(
				result.maximum_exchange_rate_multiplier,
				double(shipment_allocation.exchange_rate_multiplier(nation)));
		});
	}
	state.world.for_each_trade_route([&](dcon::trade_route_id route) {
		++result.trade_route_count;
		auto const trade = economy::world_trade::evaluate_route_capacity(state, route);
		auto const entity = int32_t(route.index());
		if(detail::observe_nonnegative(result.observed_violations, invariant_field::trade_route_volume,
				entity, -1, trade.cargo)) {
			result.trade_route_cargo += double(trade.cargo);
		}
		if(detail::observe_nonnegative(result.observed_violations, invariant_field::trade_route_capacity,
				entity, -1, trade.effective_capacity)) {
			result.trade_effective_capacity += double(trade.effective_capacity);
		}
		if(detail::observe_nonnegative(result.observed_violations, invariant_field::trade_congestion,
				entity, -1, trade.congestion, 1.0f)) {
			result.trade_congestion_sum += double(trade.congestion);
			result.maximum_trade_congestion = std::max(
				result.maximum_trade_congestion, double(trade.congestion));
		}
		result.trade_requested_cargo += double(shipment_allocation.requested(route));
		result.trade_delivered_cargo += double(shipment_allocation.actual(route));
		state.world.for_each_commodity([&](dcon::commodity_id commodity) {
			result.trade_cargo_in_transit += double(std::max(0.0f,
				state.world.trade_route_get_cargo_in_transit_0(route, commodity)));
			result.trade_cargo_in_transit += double(std::max(0.0f,
				state.world.trade_route_get_cargo_in_transit_1(route, commodity)));
		});
	});

	auto const crisis = nations::diplomatic_crisis_dynamics::evaluate_current_crisis(state);
	if(crisis.enabled) {
		result.crisis_stage = crisis.stage;
		auto observe_crisis = [&](float value, double& target, int32_t subindex) {
			if(detail::observe_nonnegative(result.observed_violations, invariant_field::crisis_metric,
					-1, subindex, value, 1.0f)) {
				target = double(value);
			}
		};
		observe_crisis(crisis.normalized_temperature, result.crisis_temperature, 0);
		observe_crisis(crisis.escalation_pressure, result.crisis_escalation_pressure, 1);
		observe_crisis(crisis.settlement_pressure, result.crisis_settlement_pressure, 2);
		observe_crisis(crisis.war_risk, result.crisis_war_risk, 3);
	}

	return result;
}

[[nodiscard]] inline validation_report validate_snapshot(aggregate_snapshot const& snapshot) noexcept {
	validation_report report{};
	report.violations = snapshot.observed_violations;

	auto validate_aggregate = [&](double value) {
		if(!std::isfinite(value)) {
			++report.violations.nonfinite;
			detail::remember_first(report.violations, invariant_field::aggregate, -1, -1, float(value));
		} else if(value < 0.0) {
			++report.violations.negative;
			detail::remember_first(report.violations, invariant_field::aggregate, -1, -1, float(value));
		}
	};

	if(!std::isfinite(snapshot.inflation) || !std::isfinite(snapshot.consumer_demand_pressure)) {
		++report.violations.nonfinite;
		detail::remember_first(report.violations, invariant_field::inflation,
			-1, -1, float(snapshot.inflation));
	}
	validate_aggregate(snapshot.consumer_price_index);
	validate_aggregate(snapshot.population);
	validate_aggregate(snapshot.pop_savings);
	validate_aggregate(snapshot.population_weighted_life_needs);
	validate_aggregate(snapshot.population_weighted_everyday_needs);
	validate_aggregate(snapshot.population_weighted_luxury_needs);
	validate_aggregate(snapshot.unemployed_population);
	validate_aggregate(snapshot.commodity_price_sum);
	validate_aggregate(snapshot.commodity_supply);
	validate_aggregate(snapshot.commodity_demand);
	validate_aggregate(snapshot.labor_price_sum);
	validate_aggregate(snapshot.real_labor_price_sum);
	validate_aggregate(snapshot.labor_supply);
	validate_aggregate(snapshot.labor_demand);
	validate_aggregate(snapshot.employed_labor);
	validate_aggregate(snapshot.army_supply_reserve_sum);
	validate_aggregate(snapshot.minimum_army_supply_reserve);
	validate_aggregate(snapshot.depot_stockpile);
	validate_aggregate(snapshot.treasury);
	validate_aggregate(snapshot.government_debt);
	validate_aggregate(snapshot.market_gdp);
	if(!std::isfinite(snapshot.factory_profit)) {
		++report.violations.nonfinite;
		detail::remember_first(report.violations, invariant_field::aggregate, -1, -1,
			float(snapshot.factory_profit));
	}
	validate_aggregate(snapshot.control_ratio_sum);
	validate_aggregate(snapshot.minimum_control_ratio);
	validate_aggregate(snapshot.legitimacy_sum);
	validate_aggregate(snapshot.minimum_legitimacy);
	validate_aggregate(snapshot.coalition_power_sum);
	validate_aggregate(snapshot.government_stability_sum);
	validate_aggregate(snapshot.minimum_government_stability);
	validate_aggregate(snapshot.cabinet_confidence_sum);
	validate_aggregate(snapshot.minimum_cabinet_confidence);
	validate_aggregate(snapshot.industry_value);
	validate_aggregate(snapshot.industry_value_capitalists);
	validate_aggregate(snapshot.industry_value_landed);
	validate_aggregate(snapshot.industry_value_state);
	validate_aggregate(snapshot.industry_value_foreign);
	validate_aggregate(snapshot.industry_value_workers);
	validate_aggregate(snapshot.industry_turnover);
	validate_aggregate(snapshot.trade_route_cargo);
	validate_aggregate(snapshot.trade_effective_capacity);
	validate_aggregate(snapshot.trade_congestion_sum);
	validate_aggregate(snapshot.maximum_trade_congestion);
	validate_aggregate(snapshot.trade_requested_cargo);
	validate_aggregate(snapshot.trade_delivered_cargo);
	validate_aggregate(snapshot.trade_cargo_in_transit);
	validate_aggregate(snapshot.trade_land_capacity_demand);
	validate_aggregate(snapshot.trade_sea_capacity_demand);
	validate_aggregate(snapshot.minimum_foreign_settlement);
	validate_aggregate(snapshot.maximum_exchange_rate_multiplier);
	validate_aggregate(snapshot.market_quantity_traded);
	validate_aggregate(snapshot.market_unfilled_life_needs);
	validate_aggregate(snapshot.market_unfilled_intermediate);
	validate_aggregate(snapshot.market_unfilled_luxury_needs);
	validate_aggregate(snapshot.crisis_temperature);
	validate_aggregate(snapshot.crisis_escalation_pressure);
	validate_aggregate(snapshot.crisis_settlement_pressure);
	validate_aggregate(snapshot.crisis_war_risk);
	if(!std::isfinite(snapshot.baseline_natural_growth)) {
		++report.violations.nonfinite;
		detail::remember_first(report.violations, invariant_field::aggregate,
			-1, -1, float(snapshot.baseline_natural_growth));
	}
	validate_aggregate(snapshot.starvation_loss);
	validate_aggregate(snapshot.housing_loss);
	validate_aggregate(snapshot.transition_reduction);
	if(!std::isfinite(snapshot.net_natural_change)) {
		++report.violations.nonfinite;
		detail::remember_first(report.violations, invariant_field::aggregate,
			-1, -1, float(snapshot.net_natural_change));
	}
	validate_aggregate(snapshot.population_weighted_housing_access);
	validate_aggregate(snapshot.population_weighted_urbanization);
	validate_aggregate(snapshot.population_weighted_overcrowding);
	validate_aggregate(snapshot.population_weighted_education_access);
	validate_aggregate(snapshot.population_weighted_human_development);
	validate_aggregate(snapshot.gross_internal_migration);
	validate_aggregate(snapshot.gross_international_migration);
	validate_aggregate(snapshot.tracked_nation_legitimacy);
	validate_aggregate(snapshot.tracked_nation_coalition_power);
	validate_aggregate(snapshot.tracked_nation_government_stability);
	validate_aggregate(snapshot.tracked_nation_minimum_cabinet_confidence);
	validate_aggregate(snapshot.tracked_nation_consumer_price_index);
	validate_aggregate(snapshot.tracked_nation_real_wage_sum);
	if(!std::isfinite(snapshot.tracked_nation_inflation)) {
		++report.violations.nonfinite;
		detail::remember_first(report.violations, invariant_field::inflation,
			snapshot.tracked_nation_index, -1, float(snapshot.tracked_nation_inflation));
	}

	report.valid = report.violations.ok();
	return report;
}

// Returns exactly one JSON object followed by '\n', suitable for appending to a
// JSONL stream. Invalid floating-point samples are represented by their counters;
// a non-finite first sample is emitted as JSON null rather than invalid NaN text.
[[nodiscard]] inline std::string serialize_jsonl(aggregate_snapshot const& snapshot,
		validation_report const& validation) {
	std::ostringstream out;
	out.imbue(std::locale::classic());
	out << std::setprecision(17);
	out << "{\"tick\":" << snapshot.tick
		<< ",\"date_raw\":" << snapshot.date_raw
		<< ",\"valid\":" << (validation.valid ? "true" : "false")
		<< ",\"save_checksum\":\"";
	detail::write_checksum_hex(out, snapshot.save_checksum);
	out << "\""
		<< ",\"ruleset\":{\"age_of_transformation\":"
		<< (snapshot.age_of_transformation ? "true" : "false") << "}"
		<< ",\"counts\":{\"pops\":" << snapshot.pop_count
		<< ",\"provinces\":" << snapshot.province_count
		<< ",\"owned_provinces\":" << snapshot.owned_province_count
		<< ",\"owner_controlled_provinces\":"
		<< snapshot.owner_controlled_province_count
		<< ",\"foreign_controlled_provinces\":"
		<< snapshot.foreign_controlled_province_count
		<< ",\"rebel_controlled_provinces\":"
		<< snapshot.rebel_controlled_province_count
		<< ",\"uncontrolled_owned_provinces\":"
		<< snapshot.uncontrolled_owned_province_count
		<< ",\"markets\":" << snapshot.market_count
		<< ",\"nations\":" << snapshot.nation_count
		<< ",\"transformed_nations\":" << snapshot.transformed_nation_count
		<< ",\"stable_governments\":" << snapshot.stable_government_count
		<< ",\"contested_governments\":" << snapshot.contested_government_count
		<< ",\"fragile_governments\":" << snapshot.fragile_government_count
		<< ",\"government_turnovers\":" << snapshot.government_turnover_count
		<< ",\"cabinet_members\":" << snapshot.cabinet_member_count
		<< ",\"factories\":" << snapshot.factory_count
		<< ",\"unprofitable_factories\":" << snapshot.unprofitable_factory_count
		<< ",\"trade_routes\":" << snapshot.trade_route_count
		<< ",\"commodity_market_cells\":" << snapshot.commodity_market_cells
		<< ",\"labor_market_cells\":" << snapshot.labor_market_cells
		<< ",\"armies\":" << snapshot.army_count
		<< ",\"depots\":" << snapshot.depot_count << "}"
		<< ",\"economy\":{\"inflation\":" << snapshot.inflation
		<< ",\"consumer_price_index\":" << snapshot.consumer_price_index
		<< ",\"consumer_demand_pressure\":" << snapshot.consumer_demand_pressure
		<< ",\"population\":" << snapshot.population
		<< ",\"pop_savings\":" << snapshot.pop_savings
		<< ",\"market_gdp\":" << snapshot.market_gdp
		<< ",\"factory_profit\":" << snapshot.factory_profit
		<< ",\"commodity_price_sum\":" << snapshot.commodity_price_sum
		<< ",\"commodity_supply\":" << snapshot.commodity_supply
		<< ",\"commodity_demand\":" << snapshot.commodity_demand
		<< ",\"machine_parts_supply\":" << snapshot.machine_parts_supply
		<< ",\"machine_parts_demand\":" << snapshot.machine_parts_demand
		<< ",\"machine_parts_import\":" << snapshot.machine_parts_import << "}"
		<< ",\"demography\":{\"baseline_natural_growth\":"
		<< snapshot.baseline_natural_growth
		<< ",\"starvation_loss\":" << snapshot.starvation_loss
		<< ",\"housing_loss\":" << snapshot.housing_loss
		<< ",\"transition_reduction\":" << snapshot.transition_reduction
		<< ",\"net_natural_change\":" << snapshot.net_natural_change
		<< ",\"housing_access_population_sum\":"
		<< snapshot.population_weighted_housing_access
		<< ",\"urbanization_population_sum\":"
		<< snapshot.population_weighted_urbanization
		<< ",\"overcrowding_population_sum\":"
		<< snapshot.population_weighted_overcrowding
		<< ",\"education_access_population_sum\":"
		<< snapshot.population_weighted_education_access
		<< ",\"human_development_population_sum\":"
		<< snapshot.population_weighted_human_development
		<< ",\"gross_internal_migration\":" << snapshot.gross_internal_migration
		<< ",\"gross_international_migration\":"
		<< snapshot.gross_international_migration << "}"
		<< ",\"tracked_nation\":{\"index\":" << snapshot.tracked_nation_index
		<< ",\"population\":" << snapshot.tracked_nation_population
		<< ",\"monthly_natural_change\":" << snapshot.tracked_nation_monthly_natural_change
		<< ",\"monthly_army_attrition\":" << snapshot.tracked_nation_monthly_army_attrition
		<< ",\"daily_internal_migration\":" << snapshot.tracked_nation_daily_internal_migration
		<< ",\"daily_external_migration\":" << snapshot.tracked_nation_daily_external_migration
		<< ",\"literacy\":" << snapshot.tracked_nation_literacy
		<< ",\"estimated_literacy_change\":" << snapshot.tracked_nation_estimated_literacy_change
		<< ",\"education_access\":" << snapshot.tracked_nation_education_access
		<< ",\"machine_parts_supply\":" << snapshot.tracked_nation_machine_parts_supply
		<< ",\"machine_parts_demand\":" << snapshot.tracked_nation_machine_parts_demand
		<< ",\"machine_parts_import\":" << snapshot.tracked_nation_machine_parts_import
		<< ",\"machine_parts_buy_probability\":" << snapshot.tracked_nation_machine_parts_buy_probability
		<< ",\"consumer_price_index\":" << snapshot.tracked_nation_consumer_price_index
		<< ",\"inflation\":" << snapshot.tracked_nation_inflation
		<< ",\"real_wage_sum\":" << snapshot.tracked_nation_real_wage_sum
		<< ",\"legitimacy\":" << snapshot.tracked_nation_legitimacy
		<< ",\"coalition_power\":" << snapshot.tracked_nation_coalition_power
		<< ",\"government_stability\":" << snapshot.tracked_nation_government_stability
		<< ",\"minimum_cabinet_confidence\":"
		<< snapshot.tracked_nation_minimum_cabinet_confidence
		<< ",\"government_established_on\":"
		<< snapshot.tracked_nation_government_established_on
		<< ",\"government_groups\":" << snapshot.tracked_nation_government_groups
		<< ",\"government_changed\":"
		<< (snapshot.tracked_nation_government_changed ? "true" : "false") << "}"
		<< ",\"living_standards\":{\"life_needs_population_sum\":"
		<< snapshot.population_weighted_life_needs
		<< ",\"everyday_needs_population_sum\":"
		<< snapshot.population_weighted_everyday_needs
		<< ",\"luxury_needs_population_sum\":"
		<< snapshot.population_weighted_luxury_needs
		<< ",\"unemployed_population\":" << snapshot.unemployed_population << "}"
		<< ",\"finance\":{\"treasury\":" << snapshot.treasury
		<< ",\"government_debt\":" << snapshot.government_debt
		<< ",\"industry\":{\"value\":" << snapshot.industry_value
		<< ",\"value_capitalists\":" << snapshot.industry_value_capitalists
		<< ",\"value_landed\":" << snapshot.industry_value_landed
		<< ",\"value_state\":" << snapshot.industry_value_state
		<< ",\"value_foreign\":" << snapshot.industry_value_foreign
		<< ",\"value_workers\":" << snapshot.industry_value_workers
		<< ",\"turnover\":" << snapshot.industry_turnover
		<< ",\"maximum_state_share\":" << snapshot.maximum_industry_state_share
		<< ",\"maximum_foreign_share\":" << snapshot.maximum_industry_foreign_share << "}"
		<< ",\"labor\":{\"price_sum\":" << snapshot.labor_price_sum
		<< ",\"real_price_sum\":" << snapshot.real_labor_price_sum
		<< ",\"supply\":" << snapshot.labor_supply
		<< ",\"demand\":" << snapshot.labor_demand
		<< ",\"employed\":" << snapshot.employed_labor
		<< ",\"price_by_type\":[";
	for(size_t index = 0; index < snapshot.labor_price_by_type.size(); ++index) {
		if(index != 0) out << ',';
		out << snapshot.labor_price_by_type[index];
	}
	out << "],\"demand_by_type\":[";
	for(size_t index = 0; index < snapshot.labor_demand_by_type.size(); ++index) {
		if(index != 0) out << ',';
		out << snapshot.labor_demand_by_type[index];
	}
	out << "],\"maximum_price\":" << snapshot.maximum_labor_price
		<< ",\"maximum_price_type\":" << snapshot.maximum_labor_price_type
		<< ",\"maximum_demand\":" << snapshot.maximum_labor_demand
		<< ",\"maximum_demand_type\":" << snapshot.maximum_labor_demand_type << "}"
		<< ",\"production_scale\":{\"private_school_size\":" << snapshot.private_school_size
		<< ",\"public_school_size\":" << snapshot.public_school_size
		<< ",\"maximum_private_school_size\":" << snapshot.maximum_private_school_size
		<< ",\"maximum_public_school_size\":" << snapshot.maximum_public_school_size
		<< ",\"factory_size\":" << snapshot.factory_size
		<< ",\"rgo_target_employment\":" << snapshot.rgo_target_employment << "}"
		<< ",\"logistics\":{\"army_supply_reserve_sum\":" << snapshot.army_supply_reserve_sum
		<< ",\"minimum_army_supply_reserve\":" << snapshot.minimum_army_supply_reserve
		<< ",\"depot_stockpile\":" << snapshot.depot_stockpile << "}"
		<< ",\"administration\":{\"control_ratio_sum\":" << snapshot.control_ratio_sum
		<< ",\"minimum_control_ratio\":" << snapshot.minimum_control_ratio << "}"
		<< ",\"politics\":{\"legitimacy_sum\":" << snapshot.legitimacy_sum
		<< ",\"minimum_legitimacy\":" << snapshot.minimum_legitimacy
		<< ",\"coalition_power_sum\":" << snapshot.coalition_power_sum
		<< ",\"government_stability_sum\":" << snapshot.government_stability_sum
		<< ",\"minimum_government_stability\":"
		<< snapshot.minimum_government_stability
		<< ",\"cabinet_confidence_sum\":" << snapshot.cabinet_confidence_sum
		<< ",\"minimum_cabinet_confidence\":"
		<< snapshot.minimum_cabinet_confidence << "}"
		<< ",\"trade\":{\"cargo\":" << snapshot.trade_route_cargo
		<< ",\"effective_capacity\":" << snapshot.trade_effective_capacity
		<< ",\"congestion_sum\":" << snapshot.trade_congestion_sum
		<< ",\"maximum_congestion\":" << snapshot.maximum_trade_congestion
		<< ",\"requested_cargo\":" << snapshot.trade_requested_cargo
		<< ",\"delivered_cargo\":" << snapshot.trade_delivered_cargo
		<< ",\"cargo_in_transit\":" << snapshot.trade_cargo_in_transit
		<< ",\"land_capacity_demand\":" << snapshot.trade_land_capacity_demand
		<< ",\"sea_capacity_demand\":" << snapshot.trade_sea_capacity_demand
		<< ",\"minimum_foreign_settlement\":" << snapshot.minimum_foreign_settlement
		<< ",\"maximum_exchange_rate_multiplier\":"
		<< snapshot.maximum_exchange_rate_multiplier << "}"
		<< ",\"market_clearing\":{\"quantity_traded\":" << snapshot.market_quantity_traded
		<< ",\"unfilled_life_needs\":" << snapshot.market_unfilled_life_needs
		<< ",\"unfilled_intermediate\":" << snapshot.market_unfilled_intermediate
		<< ",\"unfilled_luxury_needs\":" << snapshot.market_unfilled_luxury_needs << "}"
		<< ",\"crisis\":{\"stage\":\""
		<< nations::diplomatic_crisis_dynamics::stage_name(snapshot.crisis_stage)
		<< "\",\"normalized_temperature\":" << snapshot.crisis_temperature
		<< ",\"escalation_pressure\":" << snapshot.crisis_escalation_pressure
		<< ",\"settlement_pressure\":" << snapshot.crisis_settlement_pressure
		<< ",\"war_risk\":" << snapshot.crisis_war_risk << "}"
		<< ",\"violations\":{\"total\":" << validation.violations.total()
		<< ",\"nonfinite\":" << validation.violations.nonfinite
		<< ",\"negative\":" << validation.violations.negative
		<< ",\"out_of_range\":" << validation.violations.out_of_range
		<< ",\"first\":{\"field\":\"" << invariant_field_name(validation.violations.first.field)
		<< "\",\"entity\":" << validation.violations.first.entity_index
		<< ",\"subindex\":" << validation.violations.first.subindex
		<< ",\"value\":";
	detail::write_json_number(out, validation.violations.first.value);
	out << "}}}\n";
	return out.str();
}

struct run_options {
	uint64_t ticks = 0;
	uint64_t snapshot_cadence = 30;
	bool include_initial_snapshot = true;
	bool include_final_snapshot = true;
	bool fail_on_invariant = true;
	dcon::nation_id tracked_nation{};
};

struct run_result {
	uint64_t ticks_completed = 0;
	uint64_t snapshots_emitted = 0;
	bool completed = true;
	validation_report last_validation{};
};

struct synthetic_lab_result {
	dcon::nation_id nation{};
	dcon::province_id province{};
	dcon::market_id market{};
};

// Build a small, deterministic world for model and balance diagnostics when no
// Victoria 2 scenario is available. This is intentionally a laboratory fixture,
// not replacement game content: full world ticks still require parsed scenario
// definitions, while this state exercises isolated economy/politics models.
[[nodiscard]] inline synthetic_lab_result initialize_synthetic_lab(sys::state& state) {
	state.start_date = sys::absolute_time_point{sys::year_month_day{1836, 1, 1}};
	state.current_date = sys::date{sys::year_month_day{1836, 1, 1}, state.start_date};
	state.end_date = sys::absolute_time_point{sys::year_month_day{2036, 1, 1}};
	state.game_seed = 424242;
	state.inflation = 1.0f;
	state.force_age_of_transformation_ruleset = true;

	auto const money = state.world.create_commodity();
	auto const staple = state.world.create_commodity();
	state.world.commodity_set_cost(money, 1.0f);
	state.world.commodity_set_cost(staple, 1.0f);
	state.world.commodity_set_is_available_from_start(money, true);
	state.world.commodity_set_is_available_from_start(staple, true);

	auto const workers = state.world.create_pop_type();
	auto const owners = state.world.create_pop_type();
	state.culture_definitions.primary_factory_worker = workers;
	state.culture_definitions.capitalists = owners;

	auto const nation = state.world.create_nation();
	auto const province = state.world.create_province();
	auto const state_instance = state.world.create_state_instance();
	auto const market = state.world.create_market();
	state.province_definitions.first_sea_province =
		dcon::province_id{dcon::province_id::value_base_t(1)};

	state.world.force_create_province_ownership(province, nation);
	state.world.force_create_province_control(province, nation);
	state.world.province_set_state_membership(province, state_instance);
	state.world.state_instance_set_capital(state_instance, province);
	state.world.state_instance_set_market_from_local_market(state_instance, market);
	state.world.market_set_zone_from_local_market(market, state_instance);
	state.world.nation_set_capital(nation, province);
	state.world.province_set_rgo(province, staple);
	state.world.province_set_control_scale(province, 1.0f);
	state.world.province_set_control_ratio(province, 1.0f);
	state.world.province_set_capitalists_share(province, 0.20f);
	state.world.province_set_industry_market_value(province, 1'000'000.0f);
	state.world.province_set_smoothed_factory_profit(province, 500.0f);
	state.world.province_set_industry_state_share(province, 0.15f);
	state.world.province_set_industry_foreign_share(province, 0.10f);
	state.world.province_set_industry_worker_share(province, 0.05f);
	state.world.province_set_industry_landed_share(province, 0.10f);

	auto const worker_pop = state.world.create_pop();
	state.world.pop_set_poptype(worker_pop, workers);
	state.world.pop_set_size(worker_pop, 800'000.0f);
	state.world.pop_set_savings(worker_pop, 800'000.0f);
	state.world.pop_set_satisfaction(worker_pop, 0.65f);
	state.world.pop_set_uliteracy(worker_pop, pop_demographics::to_pu16(0.55f));
	state.world.pop_set_uconsciousness(worker_pop, pop_demographics::to_pmc(4.0f));
	state.world.force_create_pop_location(worker_pop, province);

	auto const owner_pop = state.world.create_pop();
	state.world.pop_set_poptype(owner_pop, owners);
	state.world.pop_set_size(owner_pop, 200'000.0f);
	state.world.pop_set_savings(owner_pop, 2'000'000.0f);
	state.world.pop_set_satisfaction(owner_pop, 0.85f);
	state.world.pop_set_uliteracy(owner_pop, pop_demographics::to_pu16(0.80f));
	state.world.pop_set_uconsciousness(owner_pop, pop_demographics::to_pmc(6.0f));
	state.world.force_create_pop_location(owner_pop, province);

	state.world.pop_type_resize_life_needs(state.world.commodity_size());
	state.world.pop_type_resize_everyday_needs(state.world.commodity_size());
	state.world.pop_type_resize_luxury_needs(state.world.commodity_size());
	for(auto type : state.world.in_pop_type) {
		state.world.pop_type_set_life_needs(type, staple, 1.0f);
		state.world.pop_type_set_everyday_needs(type, staple, 0.5f);
		state.world.pop_type_set_luxury_needs(type, staple, 0.1f);
	}

	state.world.pop_resize_udemographics(pop_demographics::size(state));
	state.world.province_resize_demographics(demographics::size(state));
	state.world.nation_resize_demographics(demographics::size(state));
	state.world.state_instance_resize_demographics(demographics::size(state));
	state.world.nation_resize_stockpiles(state.world.commodity_size());
	state.world.nation_resize_modifier_values(sys::national_mod_offsets::count);
	state.world.province_resize_modifier_values(sys::provincial_mod_offsets::count);
	state.world.nation_resize_rgo_goods_output(state.world.commodity_size());
	state.world.nation_resize_factory_goods_output(state.world.commodity_size());
	state.world.nation_resize_factory_goods_throughput(state.world.commodity_size());
	state.world.nation_resize_rgo_size(state.world.commodity_size());
	state.world.nation_resize_unlocked_commodities(state.world.commodity_size());
	state.world.nation_resize_active_unit(0);
	state.world.nation_resize_active_crime(0);
	state.world.nation_resize_active_building(0);
	state.world.nation_resize_unit_stats(0);
	state.world.nation_resize_max_building_level(economy::max_building_types);
	state.world.nation_resize_stockpile_targets(state.world.commodity_size());
	state.world.nation_resize_drawing_on_stockpiles(state.world.commodity_size());

	state.world.market_resize_price(state.world.commodity_size());
	state.world.market_resize_supply(state.world.commodity_size());
	state.world.market_resize_demand(state.world.commodity_size());
	state.world.market_resize_stockpile(state.world.commodity_size());
	state.world.market_resize_consumption(state.world.commodity_size());
	state.world.market_resize_intermediate_demand(state.world.commodity_size());
	state.world.market_resize_import(state.world.commodity_size());
	state.world.market_resize_export(state.world.commodity_size());
	state.world.market_resize_army_demand(state.world.commodity_size());
	state.world.market_resize_navy_demand(state.world.commodity_size());
	state.world.market_resize_construction_demand(state.world.commodity_size());
	state.world.market_resize_private_construction_demand(state.world.commodity_size());
	state.world.market_resize_actual_probability_to_buy(state.world.commodity_size());
	state.world.market_resize_actual_probability_to_sell(state.world.commodity_size());
	state.world.market_resize_expected_probability_to_buy(state.world.commodity_size());
	state.world.market_resize_expected_probability_to_sell(state.world.commodity_size());
	state.world.market_resize_aggregated_demand_history(state.world.commodity_size());
	state.world.market_resize_aggregated_supply_history(state.world.commodity_size());
	state.world.market_resize_life_needs_weights(state.world.commodity_size());
	state.world.market_resize_everyday_needs_weights(state.world.commodity_size());
	state.world.market_resize_luxury_needs_weights(state.world.commodity_size());
	state.world.market_resize_life_needs_costs(state.world.pop_type_size());
	state.world.market_resize_everyday_needs_costs(state.world.pop_type_size());
	state.world.market_resize_luxury_needs_costs(state.world.pop_type_size());
	state.world.market_resize_life_needs_scale(state.world.pop_type_size());
	state.world.market_resize_everyday_needs_scale(state.world.pop_type_size());
	state.world.market_resize_luxury_needs_scale(state.world.pop_type_size());
	state.world.market_resize_satisfied_ratio_of_max_life_needs(state.world.pop_type_size());
	state.world.market_resize_satisfied_ratio_of_max_everyday_needs(state.world.pop_type_size());
	state.world.market_resize_satisfied_ratio_of_max_luxury_needs(state.world.pop_type_size());
	state.world.market_resize_satisfied_ratio_of_demanded_life_needs(state.world.pop_type_size());
	state.world.market_resize_satisfied_ratio_of_demanded_everyday_needs(state.world.pop_type_size());
	state.world.market_resize_satisfied_ratio_of_demanded_luxury_needs(state.world.pop_type_size());
	state.world.commodity_resize_price_record(economy::price_history_length);
	state.world.nation_resize_gdp_record(economy::gdp_history_length);
	for(auto commodity : state.world.in_commodity) {
		auto const price = state.world.commodity_get_cost(commodity);
		state.world.market_set_price(market, commodity, price);
		state.world.market_set_supply(market, commodity, 1'000'000.0f);
		state.world.market_set_demand(market, commodity, 900'000.0f);
		state.world.market_set_stockpile(market, commodity, 100'000.0f);
	}
	for(auto type : state.world.in_pop_type) {
		state.world.market_set_life_needs_costs(market, type, 1.0f);
		state.world.market_set_everyday_needs_costs(market, type, 0.5f);
		state.world.market_set_luxury_needs_costs(market, type, 0.1f);
	}
	state.world.market_set_life_needs_weights(market, staple, 1.0f);
	state.world.market_set_everyday_needs_weights(market, staple, 1.0f);
	state.world.market_set_aggregated_demand_history(market, staple, 900'000.0f);
	state.world.market_set_aggregated_supply_history(market, staple, 1'000'000.0f);
	state.world.state_instance_set_demographics(state_instance, demographics::total,
		1'000'000.0f);
	state.world.state_instance_set_demographics(state_instance,
		demographics::to_key(state, workers), 800'000.0f);
	state.world.state_instance_set_demographics(state_instance,
		demographics::to_key(state, owners), 200'000.0f);
	state.world.market_set_gdp(market, 1'000'000.0f);

	state.world.province_resize_labor_price(economy::labor::total);
	state.world.province_resize_labor_supply(economy::labor::total);
	state.world.province_resize_labor_demand(economy::labor::total);
	state.world.province_resize_labor_demand_satisfaction(economy::labor::total);
	state.world.province_resize_labor_supply_sold(economy::labor::total);
	state.world.province_resize_pop_labor_distribution(economy::pop_labor::total);
	state.world.province_resize_rgo_target_employment(state.world.commodity_size());
	services::initialize_size_of_dcon_arrays(state);
	advanced_province_buildings::initialize_size_of_dcon_arrays(state);
	state.world.province_set_labor_price(province, economy::labor::no_education, 1.0f);
	state.world.province_set_labor_supply(province, economy::labor::no_education, 800'000.0f);
	state.world.province_set_labor_demand(province, economy::labor::no_education, 760'000.0f);
	state.world.province_set_labor_demand_satisfaction(
		province, economy::labor::no_education, 0.95f);
	state.world.province_set_labor_supply_sold(province, economy::labor::no_education, 0.95f);
	state.world.province_set_pop_labor_distribution(
		province, economy::pop_labor::primary_no_education, 1.0f);

	state.world.nation_set_stockpiles(nation, money, 1'000'000.0f);
	pop_demographics::set_employment(state, worker_pop, 760'000.0f);
	pop_demographics::set_employment(state, owner_pop, 200'000.0f);
	politics::transformation::refresh_all_nations(state);
	// The lab bypasses scenario loading, so seed the money-supply baseline the
	// same way a loaded save does. Without it the first observed day would be
	// reported as if the entire supply had appeared from nowhere.
	economy::price_level::initialize(state);

	return synthetic_lab_result{nation, province, market};
}

using line_sink = std::function<void(std::string_view)>;

namespace detail {

template<typename TickFunction, typename SinkFunction>
run_result run_ticks_with(sys::state& state, run_options const& options, TickFunction&& tick_function,
		SinkFunction&& sink_function) {
	run_result result{};
	auto emit_snapshot = [&](uint64_t tick) {
		if(gamerule::age_of_transformation_enabled(state)
			&& (!state.transformation_politics_cache_valid
				|| state.transformation_politics_cache.size() != state.world.nation_size())) {
			politics::transformation::refresh_all_nations(state);
		}
		auto snapshot = collect_snapshot(state, tick, options.tracked_nation);
		result.last_validation = validate_snapshot(snapshot);
		auto line = serialize_jsonl(snapshot, result.last_validation);
		std::invoke(sink_function, std::string_view(line));
		++result.snapshots_emitted;
		if(options.fail_on_invariant && !result.last_validation.valid) {
			result.completed = false;
			return false;
		}
		return true;
	};

	if(options.include_initial_snapshot && !emit_snapshot(0)) {
		return result;
	}

	for(uint64_t tick = 1; tick <= options.ticks; ++tick) {
		std::invoke(tick_function, state);
		result.ticks_completed = tick;


		auto const cadence_due = options.snapshot_cadence != 0 && tick % options.snapshot_cadence == 0;
		auto const final_due = options.include_final_snapshot && tick == options.ticks;
		if((cadence_due || final_due) && !emit_snapshot(tick)) {
			return result;
		}
	}

	// With zero requested ticks, the final state is the initial state. Avoid a
	// duplicate line when the initial snapshot was already requested.
	if(options.ticks == 0 && options.include_final_snapshot && !options.include_initial_snapshot) {
		emit_snapshot(0);
	}
	return result;
}

} // namespace detail

// Synchronously advances exactly options.ticks game ticks unless an observed
// snapshot violates an invariant and fail_on_invariant is enabled. The sink is
// called synchronously; its string_view is valid only for the duration of the call.
[[nodiscard]] inline run_result run_ticks(sys::state& state, run_options const& options,
		line_sink sink = {}) {
	auto tick = [](sys::state& target) { target.single_game_tick(); };
	if(sink) {
		return detail::run_ticks_with(state, options, tick, sink);
	}
	return detail::run_ticks_with(state, options, tick, [](std::string_view) {});
}

// Advances the self-contained laboratory through a deterministic demand cycle.
// It deliberately avoids the world AI, map, military and event pipelines, whose
// generated arrays and definitions only exist after loading a real scenario.
[[nodiscard]] inline run_result run_synthetic_lab(sys::state& state,
		synthetic_lab_result const& lab, run_options const& options, line_sink sink = {}) {
	uint64_t day = 0;
	auto tick = [&](sys::state& target) {
		++day;
		target.current_date += 1;
		auto const phase = float(int32_t(day % 120) - 60) / 60.0f;
		auto const demand = 900'000.0f + phase * 180'000.0f;
		auto const supply = 1'000'000.0f;
		auto const employment_ratio = std::clamp(demand / supply, 0.65f, 1.0f);
		// A deterministic credit shock turns the seeded claim into a bad loan;
		// this keeps the lab useful for exercising both the ledger and write-off
		// path without pretending to be a full scenario campaign.
		if(day == 60) {
			target.world.province_set_industry_market_value(lab.province, 0.0f);
			target.world.province_set_smoothed_factory_profit(lab.province, 0.0f);
		}
		auto const worker_pop = dcon::pop_id{dcon::pop_id::value_base_t(0)};
		auto const owner_pop = dcon::pop_id{dcon::pop_id::value_base_t(1)};
		auto const staple = dcon::commodity_id{dcon::commodity_id::value_base_t(1)};
		economy::price_level::begin_day(target);

		target.world.market_set_demand(lab.market, staple, demand);
		target.world.market_set_supply(lab.market, staple, supply);
		target.world.market_set_aggregated_demand_history(lab.market, staple, demand);
		target.world.market_set_aggregated_supply_history(lab.market, staple, supply);
		auto price = target.world.market_get_price(lab.market, staple);
		price += economy::price_properties::commodity::change<float>(price, supply, demand);
		price = std::clamp(price, economy::price_properties::commodity::min,
			economy::price_properties::commodity::maximum(
				target.world.commodity_get_cost(staple)));
		target.world.market_set_price(lab.market, staple, price);
		economy::price_level::update(target);
		target.world.market_set_gdp(lab.market, demand * employment_ratio);
		target.world.province_set_labor_demand(
			lab.province, economy::labor::no_education, 800'000.0f * employment_ratio);
		target.world.province_set_labor_demand_satisfaction(
			lab.province, economy::labor::no_education, employment_ratio);
		target.world.province_set_labor_supply_sold(
			lab.province, economy::labor::no_education, employment_ratio);
		pop_demographics::set_employment(target, worker_pop, 800'000.0f * employment_ratio);
		target.world.pop_set_satisfaction(worker_pop, employment_ratio);
		target.world.pop_set_satisfaction(owner_pop, std::clamp(0.75f + phase * 0.10f, 0.0f, 1.0f));
		target.world.pop_set_savings(worker_pop, std::max(
			0.0f, target.world.pop_get_savings(worker_pop) + (employment_ratio - 0.80f) * 1'000.0f));
		if(day % 30 == 0)
			politics::transformation::refresh_all_nations(target);
	};
	if(sink)
		return detail::run_ticks_with(state, options, tick, sink);
	return detail::run_ticks_with(state, options, tick, [](std::string_view) {});
}

} // namespace sys::simulation
