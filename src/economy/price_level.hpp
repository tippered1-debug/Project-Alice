#pragma once

#include "dcon_generated_ids.hpp"
#include "system_state_forward.hpp"

#include <vector>

namespace economy::price_level {

// Consumer-price measurement for the transformation ruleset. Prices already
// move because of local supply and demand; this module turns those movements
// into an explicit cost-of-living index instead of manufacturing an inflation
// rate independently of the goods market.
struct market_result {
	bool enabled = false;
	float cpi = 1.0f;
	// Change in the cost of the same basket during this economy tick.
	float daily_inflation = 0.0f;
	// Basket-weighted excess demand in [-1, 1]. Positive values identify
	// demand-pull pressure; negative values identify disinflationary slack.
	float demand_pressure = 0.0f;
	float population = 0.0f;
};

struct nation_result {
	bool enabled = false;
	float cpi = 1.0f;
	float daily_inflation = 0.0f;
	float demand_pressure = 0.0f;
	float population = 0.0f;
};

// Derived, unsaved state. opening_cpi is sampled at the start of the same
// daily update whose closing prices produce markets[]. Save/load therefore
// needs no historical CPI field and remains deterministic after the next tick.
struct account {
	bool enabled = false;
	std::vector<float> opening_cpi;
	std::vector<market_result> markets;
	market_result world{};
};

// Pure helpers kept public so formula behavior can be tested independently of
// a parsed Victoria scenario.
[[nodiscard]] float calculate_index(float nominal_basket_cost,
	float reference_basket_cost);
[[nodiscard]] float calculate_daily_inflation(float opening_cpi,
	float closing_cpi);
[[nodiscard]] float calculate_real_wage(float nominal_wage, float cpi);

// Cost of the market's life + everyday POP basket relative to commodity base
// costs. Luxury demand is deliberately excluded: a luxury boom must not make
// the subsistence cost of living look as if it rose by the same amount.
[[nodiscard]] market_result evaluate_market(sys::state const& state,
	dcon::market_id market, float opening_cpi = 0.0f);
[[nodiscard]] nation_result evaluate_nation(sys::state const& state,
	dcon::nation_id nation);

[[nodiscard]] float market_cpi(sys::state const& state, dcon::market_id market);
[[nodiscard]] float real_wage(sys::state const& state, dcon::province_id province,
	int32_t labor_type);

// Sample the opening basket before the economy changes prices, then close the
// index immediately after commodity and service prices have been updated.
void begin_day(sys::state& state);
void update(sys::state& state);
void initialize(sys::state& state);

} // namespace economy::price_level
