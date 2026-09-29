#include "system_state.hpp"
#include "ai_economy.hpp"
#include "ai_campaign_values.hpp"
#include "economy_stats.hpp"
#include "economy_production.hpp"
#include "economy_government.hpp"
#include "construction.hpp"
#include "demographics.hpp"
#include "prng.hpp"
#include "math_fns.hpp"
#include "economy.hpp"
#include "economy_factory_view.hpp"
#include "province.hpp"
#include "money.hpp"
#include "advanced_province_buildings.hpp"
#include "economy_constants.hpp"
#include "gamerule.hpp"
#include "investment_ranking.hpp"
#include "market_access.hpp"
#include "economy/physical/concrete_market.hpp"

namespace ai {


void filter_factories_disjunctive(
	sys::state& state,
	dcon::nation_id nid,
	dcon::market_id mid,
	dcon::province_id pid,
	bool pop_project,
	std::vector<dcon::factory_type_id>& desired_types,
	float filter_profitability,
	float filter_output_probability_to_buy,
	float filter_payback_time,
	float effective_profit
) {
	assert(desired_types.empty());

	auto n = dcon::fatten(state.world, nid);
	auto wage = state.world.province_get_labor_price(pid, economy::labor::basic_education) * 2.f;

	if(!desired_types.empty()) {
		return;
	}
	std::vector<std::pair<dcon::factory_type_id, float>> scored_types;

	for(auto type : state.world.in_factory_type) {
		if(!state.world.nation_get_active_building(nid, type) && !type.get_is_available_from_start()) {
			continue;
		}
		// Is particular factory type allowed to be built in colony
		if(!economy::can_build_factory_type_in_colony(state, pid, type)) {
			continue;
		}

		auto estimated_probability_to_buy_output = economy::estimate_probability_to_buy_after_supply_increase(
			state,
			mid,
			state.world.factory_type_get_output(type),
			state.world.factory_type_get_output_amount(type) * 0.1f
		);
		bool output_is_in_demand = estimated_probability_to_buy_output < filter_output_probability_to_buy;

		float cost = economy::factory_type_build_cost(state, n, pid, type, pop_project) + 0.1f;
		// we add a probability to make a mistake:
		// if output is equal to 1, then we can underestimate it to be 0.75 or overestimate it to be equal to 1.25 at most
		// it provides a quite wide range of potential mistakes which makes the process a bit more interesting
		float output = economy::factory_type_output_cost(state, n, mid, type) * effective_profit * (1.f + std::remainder(rng::get_random(state, n.id.value * pid.value * type.id.value) / 100.f, 0.5f) - 0.25f);
		float input = economy::factory_type_input_cost(state, n, mid, type) + 0.1f;
		float profitability = (output - input - wage * type.get_base_workforce()) / input;
		float payback_time = cost / std::max(0.00001f, (output - input - wage * type.get_base_workforce()));

		{
			auto const output_commodity = state.world.factory_type_get_output(type);
			auto const output_amount = state.world.factory_type_get_output_amount(type) * 0.1f;
			auto const realized_sell_through = economy::physical::concrete_market::observed_sell_through(
				state, mid, output_commodity, state.current_date);
			auto const sell_through = realized_sell_through >= 0.0f
				? realized_sell_through
				: economy::estimate_probability_to_sell_after_supply_increase(
					state, mid, output_commodity, output_amount);
			auto const expected_input_reliability =
				economy::factory_min_input_expected_to_be_available(state, mid, type);
			auto const historical_demand = state.world.market_get_aggregated_demand_history(
				mid, output_commodity);
			auto const historical_supply = state.world.market_get_aggregated_supply_history(
				mid, output_commodity);
			auto const import_dependence = historical_demand > 0.0f
				? std::clamp((historical_demand - historical_supply) / historical_demand, 0.0f, 1.0f)
				: 0.0f;
			economy::investment::project_inputs project{};
			project.capital_cost = cost;
			project.gross_daily_revenue = output;
			project.daily_material_cost = input;
			project.daily_wage_cost = wage * type.get_base_workforce();
			project.expected_sell_through = sell_through;
			project.input_reliability = expected_input_reliability;
			project.logistics_reliability =
				economy::market_access::evaluate_province(state, pid).access;
			project.effective_tax_rate = std::clamp(1.0f - effective_profit, 0.0f, 1.0f);
			project.annual_interest_rate = 0.0f;
			project.demand_risk = 1.0f - sell_through;
			project.jobs = type.get_base_workforce();
			project.strategic_shortage = sell_through;
			project.import_dependence = import_dependence;
			auto const score = economy::investment::evaluate(project);
			auto const selected_score = pop_project
				? score.risk_adjusted_private : score.public_value;
			if(pop_project ? score.privately_viable : score.publicly_viable && std::isfinite(selected_score)) {
				desired_types.push_back(type.id);
				scored_types.emplace_back(type.id, selected_score);
			}
			continue;
		}

		if(output_is_in_demand || profitability > filter_profitability || payback_time < filter_payback_time) {
			desired_types.push_back(type.id);
		}
	}
	{
		std::sort(scored_types.begin(), scored_types.end(), [](auto const& left, auto const& right) {
			if(left.second != right.second)
				return left.second > right.second;
			return left.first.index() < right.first.index();
		});
		desired_types.clear();
		for(auto const& candidate : scored_types)
			desired_types.push_back(candidate.first);
	}
}

void get_craved_factory_types(sys::state& state, dcon::nation_id nid, dcon::market_id mid, dcon::province_id pid, std::vector<dcon::factory_type_id>& desired_types, bool pop_project) {
	assert(desired_types.empty());
	assert(economy::can_build_factory_in_colony(state, pid)); // Do not call this function if building in state is impossible in principle

	auto const tax_eff = economy::tax_collection_rate(state, nid, pid);
	auto const rich_effect = (1.0f - tax_eff * float(state.world.nation_get_rich_tax(nid)) / 100.0f);

	return filter_factories_disjunctive(
		state, nid, mid, pid, pop_project, desired_types,
		2.f, 0.f, 40.f, rich_effect
	);
}


void get_desired_factory_types(sys::state& state, dcon::nation_id nid, dcon::market_id mid, dcon::province_id pid, std::vector<dcon::factory_type_id>& desired_types, bool pop_project) {
	assert(desired_types.empty());
	assert(economy::can_build_factory_in_colony(state, pid)); // Do not call this function if building in state is impossible in principle
	auto n = dcon::fatten(state.world, nid);
	auto m = dcon::fatten(state.world, mid);
	auto sid = m.get_zone_from_local_market();

	auto const tax_eff = economy::tax_collection_rate(state, nid, pid);
	auto const rich_effect = (1.0f - tax_eff * float(state.world.nation_get_rich_tax(n)) / 100.0f);

	return filter_factories_disjunctive(
		state, nid, mid, pid, pop_project, desired_types,
		0.3f, 0.5f, 365.f, rich_effect
	);
}


}
