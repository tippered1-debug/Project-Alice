#pragma once

#include "culture/transformation_politics.hpp"
// The synthetic lab sizes dcon arrays directly, so this header needs the
// economy declarations in its own right rather than through whichever
// translation unit happens to include it.
#include "economy/advanced_province_buildings.hpp"
#include "economy/banking_stability.hpp"
#include "economy/credit_market.hpp"
#include "economy/demographics.hpp"
#include "economy/economy_constants.hpp"
#include "economy/economy_stats.hpp"
#include "economy/human_development.hpp"
#include "economy/industry_ownership.hpp"
#include "economy/monetary_system.hpp"
#include "economy/price.hpp"
#include "economy/world_trade_capacity.hpp"
#include "gamerule/gamerule.hpp"
#include "gamestate/system_state.hpp"
#include "military/military.hpp"
#include "nations/diplomatic_crisis_dynamics.hpp"

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
	national_bank,
	private_investment,
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
	case invariant_field::national_bank: return "national_bank";
	case invariant_field::private_investment: return "private_investment";
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

	double inflation = 0.0;
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
	double labor_price_sum = 0.0;
	double labor_supply = 0.0;
	double labor_demand = 0.0;
	double employed_labor = 0.0;
	double army_supply_reserve_sum = 0.0;
	double minimum_army_supply_reserve = 0.0;
	double depot_stockpile = 0.0;
	double treasury = 0.0;
	double government_debt = 0.0;
	double national_bank = 0.0;
	double private_investment = 0.0;
	// Money-supply account. money_market_cash and money_unaccounted are signed
	// on purpose: merchants may hold a negative net balance, and money can go
	// missing as easily as it can appear.
	double money_pop_savings = 0.0;
	double money_market_cash = 0.0;
	double money_treasury = 0.0;
	double money_national_bank = 0.0;
	double money_private_investment = 0.0;
	double money_producer_banks = 0.0;
	double money_building_savings = 0.0;
	double money_total = 0.0;
	double money_expected_total = 0.0;
	double money_gold_emission = 0.0;
	double money_unaccounted = 0.0;
	double money_gross_total = 0.0;
	double money_net_to_gross = 1.0;
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
	double banking_health_sum = 0.0;
	double minimum_banking_health = 0.0;
	double banking_stress_sum = 0.0;
	// Credit market. The rate is a price, so its extremes matter as much as its
	// average: a single exhausted lender is the interesting observation.
	double credit_policy_rate_sum = 0.0;
	double maximum_credit_policy_rate = 0.0;
	double credit_utilization_sum = 0.0;
	double credit_government_share_sum = 0.0;
	double credit_lending_capacity = 0.0;
	double credit_extended_to_private = 0.0;
	double credit_private_interest_due = 0.0;
	// Producer tills financed by the bank today, and the deficit it could not
	// reach. The unfunded figure is the size of the old free overdraft.
	double credit_producer_extended = 0.0;
	double credit_producer_unfunded = 0.0;
	// Outstanding producer loan book, and the day's service on it. A stock the
	// flow-only version had no way to represent.
	double credit_producer_debt = 0.0;
	double credit_producer_interest_paid = 0.0;
	double credit_producer_principal_repaid = 0.0;
	double credit_producer_writeoff = 0.0;
	uint64_t credit_market_count = 0;
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

	if(detail::observe_nonnegative(result.observed_violations, invariant_field::inflation, -1, -1,
			state.inflation)) {
		result.inflation = state.inflation;
	}

	{
		auto const& ledger = state.monetary_account;
		result.money_pop_savings = ledger.current.pop_savings;
		result.money_market_cash = ledger.current.market_cash;
		result.money_treasury = ledger.current.treasury;
		result.money_national_bank = ledger.current.national_bank;
		result.money_private_investment = ledger.current.private_investment;
		result.money_producer_banks = ledger.current.producer_banks;
		result.money_building_savings = ledger.current.building_savings;
		result.money_total = ledger.current.total();
		result.money_expected_total = ledger.last_balance.expected_total;
		result.money_gold_emission = ledger.last_balance.gold_emission;
		result.money_unaccounted = ledger.last_balance.unaccounted;
		result.money_gross_total = ledger.last_balance.gross_total;
		result.money_net_to_gross = ledger.last_balance.net_to_gross;
		// The aggregates are validated in doubles by validate_snapshot. Narrowing
		// them to float here would report a spurious violation for exactly the
		// runaway totals this account exists to catch.
	}

	bool has_banking_result = false;
	state.world.for_each_nation([&](dcon::nation_id nation) {
		++result.nation_count;
		auto const entity = int32_t(nation.index());
		auto const treasury = state.world.nation_get_stockpiles(nation, economy::money);
		auto const debt = state.world.nation_get_local_loan(nation);
		auto const bank = state.world.nation_get_national_bank(nation);
		auto const investment = state.world.nation_get_private_investment(nation);
		if(detail::observe_nonnegative(result.observed_violations, invariant_field::nation_treasury,
				entity, -1, treasury)) {
			result.treasury += double(treasury);
		}
		if(detail::observe_nonnegative(result.observed_violations, invariant_field::nation_debt,
				entity, -1, debt)) {
			result.government_debt += double(debt);
		}
		if(detail::observe_nonnegative(result.observed_violations, invariant_field::national_bank,
				entity, -1, bank)) {
			result.national_bank += double(bank);
		}
		if(detail::observe_nonnegative(result.observed_violations, invariant_field::private_investment,
				entity, -1, investment)) {
			result.private_investment += double(investment);
		}

		auto const banking = economy::banking_stability::evaluate_nation(state, nation);
		if(banking.enabled
			&& detail::observe_nonnegative(result.observed_violations, invariant_field::banking_health,
				entity, -1, banking.credit_health, 1.0f)) {
			result.banking_health_sum += double(banking.credit_health);
			result.banking_stress_sum += double(banking.financial_stress);
			if(!has_banking_result || banking.credit_health < result.minimum_banking_health)
				result.minimum_banking_health = banking.credit_health;
			has_banking_result = true;
		}

		// Rate, utilization and capacity are pure functions of serialized state
		// and can be re-derived here. The settled flows cannot: they depend on a
		// construction shortfall that only exists inside the daily update, so
		// they are read back from what settlement actually recorded.
		auto const credit = economy::credit::evaluate_nation(state, nation);
		if(credit.enabled
			&& detail::observe_nonnegative(result.observed_violations, invariant_field::credit_rate,
				entity, -1, credit.policy_annual_rate)) {
			++result.credit_market_count;
			result.credit_policy_rate_sum += double(credit.policy_annual_rate);
			result.maximum_credit_policy_rate = std::max(
				result.maximum_credit_policy_rate, double(credit.policy_annual_rate));
			result.credit_utilization_sum += double(credit.utilization);
			result.credit_government_share_sum += double(credit.government_share);
			result.credit_lending_capacity += double(credit.lending_capacity);
			auto const settled = std::size_t(nation.index());
			if(settled < state.credit_daily_flows.extended.size()) {
				result.credit_extended_to_private +=
					double(state.credit_daily_flows.extended[settled]);
				result.credit_private_interest_due +=
					double(state.credit_daily_flows.interest[settled]);
			}
			if(settled < state.credit_daily_flows.producer_interest_paid.size()) {
				result.credit_producer_interest_paid +=
					double(state.credit_daily_flows.producer_interest_paid[settled]);
				result.credit_producer_principal_repaid +=
					double(state.credit_daily_flows.producer_principal_repaid[settled]);
				result.credit_producer_writeoff +=
					double(state.credit_daily_flows.producer_writeoff[settled]);
			}
			if(settled < state.credit_daily_flows.producer_extended.size()) {
				result.credit_producer_extended +=
					double(state.credit_daily_flows.producer_extended[settled]);
				result.credit_producer_unfunded +=
					double(state.credit_daily_flows.producer_unfunded[settled]);
			}
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
		for(auto province : state.world.nation_get_province_ownership(tracked_nation)) {
			auto const province_id = province.get_province();
			for(auto membership : state.world.province_get_pop_location(province_id)) {
				result.tracked_nation_population += double(state.world.pop_get_size(membership.get_pop()));
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
	}

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
			}
			detail::observe_nonnegative(result.observed_violations, invariant_field::labor_demand_satisfaction,
				entity, labor_type, demand_satisfaction, 1.0f);
			detail::observe_nonnegative(result.observed_violations, invariant_field::labor_supply_sold,
				entity, labor_type, supply_sold, 1.0f);
		}

		{
			auto const debt = double(state.world.province_get_producer_debt(province));
			if(std::isfinite(debt) && debt > 0.0)
				result.credit_producer_debt += debt;
		}

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

	validate_aggregate(snapshot.inflation);
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
	validate_aggregate(snapshot.labor_supply);
	validate_aggregate(snapshot.labor_demand);
	validate_aggregate(snapshot.employed_labor);
	validate_aggregate(snapshot.army_supply_reserve_sum);
	validate_aggregate(snapshot.minimum_army_supply_reserve);
	validate_aggregate(snapshot.depot_stockpile);
	validate_aggregate(snapshot.treasury);
	validate_aggregate(snapshot.government_debt);
	validate_aggregate(snapshot.national_bank);
	validate_aggregate(snapshot.private_investment);
	validate_aggregate(snapshot.money_pop_savings);
	validate_aggregate(snapshot.money_treasury);
	validate_aggregate(snapshot.money_national_bank);
	validate_aggregate(snapshot.money_private_investment);
	validate_aggregate(snapshot.money_building_savings);
	validate_aggregate(snapshot.money_gross_total);
	validate_aggregate(snapshot.money_gold_emission);
	// Only the gross position is required to be non-negative. The net supply is
	// signed: producer tills overdraw without limit in the base game, so on a
	// real scenario the world's net cash legitimately crosses zero. Failing the
	// run there would turn ordinary vanilla behaviour into a hard error. The
	// signal lives in net_to_gross and the producer balance instead.
	auto validate_signed = [&](double value, invariant_field field) {
		if(!std::isfinite(value)) {
			++report.violations.nonfinite;
			detail::remember_first(report.violations, field, -1, -1, float(value));
		}
	};
	validate_signed(snapshot.money_total, invariant_field::money_supply);
	validate_signed(snapshot.money_market_cash, invariant_field::money_supply);
	// Producer tills run collectively negative in the base game: firms pay wages
	// and buy inputs from a balance that is allowed to overdraw without limit.
	// That is an economic finding, not an invalid sample.
	validate_signed(snapshot.money_producer_banks, invariant_field::money_supply);
	validate_signed(snapshot.money_expected_total, invariant_field::money_supply);
	validate_signed(snapshot.money_unaccounted, invariant_field::money_supply);
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
	validate_aggregate(snapshot.banking_health_sum);
	validate_aggregate(snapshot.minimum_banking_health);
	validate_aggregate(snapshot.banking_stress_sum);
	validate_aggregate(snapshot.credit_policy_rate_sum);
	validate_aggregate(snapshot.maximum_credit_policy_rate);
	validate_aggregate(snapshot.credit_utilization_sum);
	validate_aggregate(snapshot.credit_government_share_sum);
	validate_aggregate(snapshot.credit_lending_capacity);
	validate_aggregate(snapshot.credit_extended_to_private);
	validate_aggregate(snapshot.credit_private_interest_due);
	validate_aggregate(snapshot.credit_producer_extended);
	validate_aggregate(snapshot.credit_producer_unfunded);
	validate_aggregate(snapshot.credit_producer_debt);
	validate_aggregate(snapshot.credit_producer_interest_paid);
	validate_aggregate(snapshot.credit_producer_principal_repaid);
	validate_aggregate(snapshot.credit_producer_writeoff);
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
		<< ",\"population\":" << snapshot.population
		<< ",\"pop_savings\":" << snapshot.pop_savings
		<< ",\"market_gdp\":" << snapshot.market_gdp
		<< ",\"factory_profit\":" << snapshot.factory_profit
		<< ",\"commodity_price_sum\":" << snapshot.commodity_price_sum
		<< ",\"commodity_supply\":" << snapshot.commodity_supply
		<< ",\"commodity_demand\":" << snapshot.commodity_demand << "}"
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
		<< ",\"national_bank\":" << snapshot.national_bank
		<< ",\"private_investment\":" << snapshot.private_investment << "}"
		<< ",\"credit\":{\"markets\":" << snapshot.credit_market_count
		<< ",\"policy_rate_sum\":" << snapshot.credit_policy_rate_sum
		<< ",\"maximum_policy_rate\":" << snapshot.maximum_credit_policy_rate
		<< ",\"utilization_sum\":" << snapshot.credit_utilization_sum
		<< ",\"government_share_sum\":" << snapshot.credit_government_share_sum
		<< ",\"lending_capacity\":" << snapshot.credit_lending_capacity
		<< ",\"extended_to_private\":" << snapshot.credit_extended_to_private
		<< ",\"private_interest_due\":" << snapshot.credit_private_interest_due
		<< ",\"producer_extended\":" << snapshot.credit_producer_extended
		<< ",\"producer_unfunded\":" << snapshot.credit_producer_unfunded
		<< ",\"producer_debt\":" << snapshot.credit_producer_debt
		<< ",\"producer_interest_paid\":" << snapshot.credit_producer_interest_paid
		<< ",\"producer_principal_repaid\":" << snapshot.credit_producer_principal_repaid
		<< ",\"producer_writeoff\":" << snapshot.credit_producer_writeoff << "}"
		<< ",\"industry\":{\"value\":" << snapshot.industry_value
		<< ",\"value_capitalists\":" << snapshot.industry_value_capitalists
		<< ",\"value_landed\":" << snapshot.industry_value_landed
		<< ",\"value_state\":" << snapshot.industry_value_state
		<< ",\"value_foreign\":" << snapshot.industry_value_foreign
		<< ",\"value_workers\":" << snapshot.industry_value_workers
		<< ",\"turnover\":" << snapshot.industry_turnover
		<< ",\"maximum_state_share\":" << snapshot.maximum_industry_state_share
		<< ",\"maximum_foreign_share\":" << snapshot.maximum_industry_foreign_share << "}"
		<< ",\"money\":{\"pop_savings\":" << snapshot.money_pop_savings
		<< ",\"market_cash\":" << snapshot.money_market_cash
		<< ",\"treasury\":" << snapshot.money_treasury
		<< ",\"national_bank\":" << snapshot.money_national_bank
		<< ",\"private_investment\":" << snapshot.money_private_investment
		<< ",\"producer_banks\":" << snapshot.money_producer_banks
		<< ",\"building_savings\":" << snapshot.money_building_savings
		<< ",\"total\":" << snapshot.money_total
		<< ",\"expected_total\":" << snapshot.money_expected_total
		<< ",\"gold_emission\":" << snapshot.money_gold_emission
		<< ",\"unaccounted\":" << snapshot.money_unaccounted

		<< ",\"gross_total\":" << snapshot.money_gross_total
		<< ",\"net_to_gross\":" << snapshot.money_net_to_gross << "}"
		<< ",\"labor\":{\"price_sum\":" << snapshot.labor_price_sum
		<< ",\"supply\":" << snapshot.labor_supply
		<< ",\"demand\":" << snapshot.labor_demand
		<< ",\"employed\":" << snapshot.employed_labor << "}"
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
		<< ",\"banking\":{\"credit_health_sum\":" << snapshot.banking_health_sum
		<< ",\"minimum_credit_health\":" << snapshot.minimum_banking_health
		<< ",\"financial_stress_sum\":" << snapshot.banking_stress_sum << "}"
		<< ",\"trade\":{\"cargo\":" << snapshot.trade_route_cargo
		<< ",\"effective_capacity\":" << snapshot.trade_effective_capacity
		<< ",\"congestion_sum\":" << snapshot.trade_congestion_sum
		<< ",\"maximum_congestion\":" << snapshot.maximum_trade_congestion << "}"
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

	state.world.province_set_nation_from_province_ownership(province, nation);
	state.world.province_set_nation_from_province_control(province, nation);
	state.world.province_set_state_membership(province, state_instance);
	state.world.state_instance_set_capital(state_instance, province);
	state.world.state_instance_set_market_from_local_market(state_instance, market);
	state.world.market_set_zone_from_local_market(market, state_instance);
	state.world.nation_set_capital(nation, province);
	state.world.province_set_rgo(province, staple);
	state.world.province_set_control_scale(province, 1.0f);
	state.world.province_set_control_ratio(province, 1.0f);
	state.world.province_set_capitalists_share(province, 0.20f);

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
	state.world.market_set_gdp(market, 1'000'000.0f);

	state.world.province_resize_labor_price(economy::labor::total);
	state.world.province_resize_labor_supply(economy::labor::total);
	state.world.province_resize_labor_demand(economy::labor::total);
	state.world.province_resize_labor_demand_satisfaction(economy::labor::total);
	state.world.province_resize_labor_supply_sold(economy::labor::total);
	state.world.province_resize_pop_labor_distribution(economy::pop_labor::total);
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
	state.world.nation_set_national_bank(nation, 500'000.0f);
	state.world.nation_set_private_investment(nation, 100'000.0f);
	pop_demographics::set_employment(state, worker_pop, 760'000.0f);
	pop_demographics::set_employment(state, owner_pop, 200'000.0f);
	politics::transformation::refresh_all_nations(state);
	// The lab bypasses scenario loading, so seed the money-supply baseline the
	// same way a loaded save does. Without it the first observed day would be
	// reported as if the entire supply had appeared from nowhere.
	economy::monetary::initialize(state);

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

		if(state.money_audit.enabled) {
			auto const report = economy::monetary::format_audit(state);
			if(!report.empty())
				std::fputs(report.c_str(), stdout);
		}

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
		auto const worker_pop = dcon::pop_id{dcon::pop_id::value_base_t(0)};
		auto const owner_pop = dcon::pop_id{dcon::pop_id::value_base_t(1)};
		auto const staple = dcon::commodity_id{dcon::commodity_id::value_base_t(1)};

		target.world.market_set_demand(lab.market, staple, demand);
		target.world.market_set_supply(lab.market, staple, supply);
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
