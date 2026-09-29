#include "economy_pops.hpp"
#include "economy/payroll.hpp"
#include "economy_production.hpp"
#include "price.hpp"
#include "province_templates.hpp"
#include "economy_templates.hpp"
#include "demographics.hpp"
#include "money.hpp"
#include "economy_constants.hpp"
#include "economy_templates_pure.hpp"
#include "economy_pops_constants.hpp"
#include "policy_execution.hpp"
#include "advanced_province_buildings.hpp"
#include "gamerule.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace economy {
namespace pops {

namespace {
	uint8_t debug_bitfield_byte(auto const& getter, int32_t byte_index) {
		uint8_t value = 0;
		for(int32_t bit = 0; bit < 8; ++bit) {
			if(getter(byte_index * 8 + bit)) {
				value = uint8_t(value | (uint8_t(1) << bit));
			}
		}
		return value;
	}

	template<typename Tag>
	uint32_t wage_active_lanes(ve::partial_contiguous_tags<Tag> const& tags) {
		return tags.subcount;
	}

	template<typename T>
	uint32_t wage_active_lanes(T const&) {
		return ve::vector_size;
	}

	inline bool wage_debug_enabled() {
		static bool enabled = [] {
			auto const* value = std::getenv("ALICE_DEBUG_WAGE_ASSERT");
			return value && value[0] == '1';
		}();
		return enabled;
	}

	inline bool wage_trace_producers_enabled() {
		static bool enabled = [] {
			auto const* value = std::getenv("ALICE_DEBUG_WAGE_PRODUCERS");
			return value && value[0] == '1';
		}();
		return enabled;
	}

	inline bool wage_compare_enabled() {
		static bool enabled = [] {
			auto const* value = std::getenv("ALICE_DEBUG_WAGE_COMPARE");
			return value && value[0] == '1';
		}();
		return enabled;
	}

	inline bool wage_value_matches(float lhs, float rhs, float tolerance) {
		if(std::isnan(lhs) || std::isnan(rhs)) {
			return std::isnan(lhs) && std::isnan(rhs);
		}
		if(std::isinf(lhs) || std::isinf(rhs)) {
			return std::isinf(lhs) && std::isinf(rhs) && std::signbit(lhs) == std::signbit(rhs);
		}
		return std::fabs(lhs - rhs) <= tolerance;
	}

	inline void wage_abort_after_log() {
		std::fflush(stderr);
		std::abort();
	}
}
#ifndef NDEBUG
void debug_check_pop_savings_phase(sys::state const& state, char const* phase_name) {
	if(!wage_debug_enabled() && !wage_trace_producers_enabled()) {
		return;
	}
	double total_savings = 0.0;
	float maximum_savings = 0.0f;
	dcon::pop_id maximum_pop{};
	state.world.execute_serial_over_pop([&](auto pops) {
		ve::apply([&](dcon::pop_id pop) {
			auto savings = state.world.pop_get_savings(pop);
			if(std::isfinite(savings) && savings >= 0.f) {
				total_savings += double(savings);
				if(savings > maximum_savings) {
					maximum_savings = savings;
					maximum_pop = pop;
				}
				return;
			}
			auto date = state.current_date.to_ymd(state.start_date);
			auto province = state.world.pop_get_province_from_pop_location(pop);
			auto pop_type = state.world.pop_get_poptype(pop);
			std::fprintf(stderr,
				"SAVINGS_PHASE_INVALID\n"
				"phase=%s\n"
				"date=%d.%d.%d\n"
				"pop_id=%d\n"
				"province_id=%d\n"
				"pop_type_id=%d\n"
				"savings=%g\n"
				"\n",
				phase_name,
				date.year, int(date.month), int(date.day),
				pop.index(),
				province.index(),
				pop_type.id.index(),
				savings);
			wage_abort_after_log();
		}, pops);
	});
	if(wage_trace_producers_enabled()) {
		auto const date = state.current_date.to_ymd(state.start_date);
		std::fprintf(stderr,
			"SAVINGS_PHASE_TOTAL phase=%s date=%d.%d.%d total=%.17g max=%.9g max_pop=%d\n",
			phase_name, date.year, int(date.month), int(date.day), total_savings,
			maximum_savings, maximum_pop.index());
		std::fflush(stderr);
	}
}
#endif


template<typename VALUE, typename POPS>
VALUE investment_rate(const sys::state& state, POPS ids) {
	using BOOL_VALUE = typename std::conditional_t<std::same_as<POPS, dcon::pop_id>, bool, ve::mask_vector>;

	auto provs = state.world.pop_get_province_from_pop_location(ids);
	auto states = state.world.province_get_state_membership(provs);
	auto markets = state.world.state_instance_get_market_from_local_market(states);
	auto nations = state.world.state_instance_get_nation_from_state_ownership(states);
	auto pop_type = state.world.pop_get_poptype(ids);
	auto nation_allows_investment = ve::apply([&](dcon::nation_id nation) {
		return nation && (state.world.nation_get_combined_issue_rules(nation)
			& can_invest) != 0;
	}, nations);

	auto capitalists_mask = pop_type == state.culture_definitions.capitalists;
	auto middle_class_investors_mask = pop_type == state.culture_definitions.artisans || pop_type == state.culture_definitions.secondary_factory_worker;
	auto farmers_mask = pop_type == state.culture_definitions.farmers;
	auto landowners_mask = pop_type == state.culture_definitions.aristocrat;

	auto modifier = [&](dcon::nation_id nation, dcon::national_modifier_value offset) {
		return nation ? state.world.nation_get_modifier_values(nation, offset) : 0.0f;
	};
	auto invest_ratio_capitalists = ve::apply([&](dcon::nation_id nation) {
		return modifier(nation, sys::national_mod_offsets::capitalist_reinvestment);
	}, nations);
	auto invest_ratio_landowners = ve::apply([&](dcon::nation_id nation) {
		return modifier(nation, sys::national_mod_offsets::aristocrat_reinvestment);
	}, nations);
	auto invest_ratio_middle_class = ve::apply([&](dcon::nation_id nation) {
		return modifier(nation, sys::national_mod_offsets::middle_class_reinvestment);
	}, nations);
	auto invest_ratio_farmers = ve::apply([&](dcon::nation_id nation) {
		return modifier(nation, sys::national_mod_offsets::farmers_reinvestment);
	}, nations);

	auto investment_ratio =
		adaptive_ve::select<BOOL_VALUE, VALUE>(
			nation_allows_investment && capitalists_mask,
			invest_ratio_capitalists + state.defines.alice_invest_capitalist,
			0.0f
		)
		+ adaptive_ve::select<BOOL_VALUE, VALUE>(
			nation_allows_investment && landowners_mask,
			invest_ratio_landowners + state.defines.alice_invest_aristocrat,
			0.0f
		)
		+ adaptive_ve::select<BOOL_VALUE, VALUE>(
			nation_allows_investment && middle_class_investors_mask,
			invest_ratio_middle_class + state.defines.alice_invest_middle_class,
			0.0f
		)
		+ adaptive_ve::select<BOOL_VALUE, VALUE>(
			nation_allows_investment && farmers_mask,
			invest_ratio_farmers + state.defines.alice_invest_farmer,
			0.0f
		);
	return investment_ratio;
}


// handle bank savings
// Note that farmers and middle_class don't do bank savings by default
// - that doens't mean they don't have savings.
// They don't use banks for savings without modifier (from tech, from example).
template<typename VALUE, typename POPS>
VALUE bank_saving_rate(const sys::state& state, POPS ids) {
	using BOOL_VALUE = typename std::conditional_t<std::same_as<POPS, dcon::pop_id>, bool, ve::mask_vector>;

	auto provs = state.world.pop_get_province_from_pop_location(ids);
	auto states = state.world.province_get_state_membership(provs);
	auto markets = state.world.state_instance_get_market_from_local_market(states);
	auto nations = state.world.state_instance_get_nation_from_state_ownership(states);
	auto pop_type = state.world.pop_get_poptype(ids);

	auto capitalists_mask = pop_type == state.culture_definitions.capitalists;
	auto middle_class_investors_mask = pop_type == state.culture_definitions.artisans || pop_type == state.culture_definitions.secondary_factory_worker;
	auto farmers_mask = pop_type == state.culture_definitions.farmers;
	auto landowners_mask = pop_type == state.culture_definitions.aristocrat;

	auto modifier = [&](dcon::nation_id nation, dcon::national_modifier_value offset) {
		return nation ? state.world.nation_get_modifier_values(nation, offset) : 0.0f;
	};
	auto bank_saving_ratio_capitalists = ve::apply([&](dcon::nation_id nation) {
		return modifier(nation, sys::national_mod_offsets::capitalist_savings);
	}, nations);
	auto bank_saving_ratio_landowners = ve::apply([&](dcon::nation_id nation) {
		return modifier(nation, sys::national_mod_offsets::aristocrat_savings);
	}, nations);
	auto bank_saving_ratio_middle_class = ve::apply([&](dcon::nation_id nation) {
		return modifier(nation, sys::national_mod_offsets::middle_class_savings);
	}, nations);
	auto bank_saving_ratio_farmers = ve::apply([&](dcon::nation_id nation) {
		return modifier(nation, sys::national_mod_offsets::farmers_savings);
	}, nations);

	auto bank_saving_ratio =
		adaptive_ve::select<BOOL_VALUE, VALUE>(
			capitalists_mask,
			bank_saving_ratio_capitalists + state.defines.alice_save_capitalist,
			0.0f
		)
		+ adaptive_ve::select<BOOL_VALUE, VALUE>(
			landowners_mask,
			bank_saving_ratio_landowners + state.defines.alice_save_aristocrat,
			0.0f
		)
		+ adaptive_ve::select<BOOL_VALUE, VALUE>(
			middle_class_investors_mask,
			bank_saving_ratio_middle_class + state.defines.alice_save_middle_class,
			0.0f
		)
		+ adaptive_ve::select<BOOL_VALUE, VALUE>(
			farmers_mask,
			bank_saving_ratio_farmers + state.defines.alice_save_farmer,
			0.0f
		);

	return bank_saving_ratio;
}

template<typename VALUE, typename POPS>
VALUE adjusted_subsistence_score(
	const sys::state& state,
	POPS p
) {
	return state.world.province_get_subsistence_score(p)
		* state.world.province_get_subsistence_employment(p)
		/ (state.world.province_get_demographics(p, demographics::total) + 1.f);
}

template<typename POPS>
auto prepare_pop_budget_templated(
	const sys::state& state, POPS ids
) {
	using VALUE = typename std::conditional_t<std::same_as<POPS, dcon::pop_id>, float, ve::fp_vector>;
	using BOOL_VALUE = typename std::conditional_t<std::same_as<POPS, dcon::pop_id>, bool, ve::mask_vector>;

	vectorized_pops_budget<VALUE> result{ };

	auto pop_size = state.world.pop_get_size(ids);
	auto savings = state.world.pop_get_savings(ids);
	auto provs = state.world.pop_get_province_from_pop_location(ids);
	auto states = state.world.province_get_state_membership(provs);
	auto markets = state.world.state_instance_get_market_from_local_market(states);
	auto nations = state.world.state_instance_get_nation_from_state_ownership(states);
	auto pop_type = state.world.pop_get_poptype(ids);
	auto strata = state.world.pop_type_get_strata(pop_type);

	VALUE life_costs = ve::apply(
		[&](dcon::market_id m, dcon::pop_type_id pt) {
			return m && pt ? state.world.market_get_life_needs_costs(m, pt) : 0.0f;
		}, markets, pop_type
	);
	VALUE everyday_costs = ve::apply(
		[&](dcon::market_id m, dcon::pop_type_id pt) {
			return m && pt ? state.world.market_get_everyday_needs_costs(m, pt) : 0.0f;
		}, markets, pop_type
	);
	VALUE luxury_costs = ve::apply(
		[&](dcon::market_id m, dcon::pop_type_id pt) {
			return m && pt ? state.world.market_get_luxury_needs_costs(m, pt) : 0.0f;
		}, markets, pop_type
	);

	if constexpr(std::same_as<POPS, dcon::pop_id>) {
		result.can_use_free_services = state.world.pop_get_is_primary_or_accepted_culture(ids) ? 1.f : 0.f;
	} else {
		result.can_use_free_services = adaptive_ve::select<BOOL_VALUE, VALUE>(state.world.pop_get_is_primary_or_accepted_culture(ids), ve::fp_vector{ 1.f }, ve::fp_vector{ 0.f });
	}

	// we want to focus on life needs first if we are poor AND our satisfaction is low

	VALUE total_cost_needs = 0.00001f + (life_costs + everyday_costs + luxury_costs) * pop_size / state.defines.alice_needs_scaling_factor;
	VALUE is_rich = adaptive_ve::min<VALUE>(
		3.f,
		adaptive_ve::max<VALUE>(0.f, savings - total_cost_needs) / total_cost_needs
	);

	VALUE base_life_costs = (0.00001f + life_costs * pop_size / state.defines.alice_needs_scaling_factor);
	VALUE is_poor = adaptive_ve::max<VALUE>(0.01f, 1.f - 4.f * savings / base_life_costs);
	//VALUE current_life = pop_demographics::get_life_needs(state, ids);
	is_poor = adaptive_ve::min<VALUE>(1.f, adaptive_ve::max<VALUE>(0.f, is_poor));

	// prepare desired spending rate for every category

	VALUE life_spending_ratio = state.defines.alice_needs_lf_spend * (1.f - is_poor) + is_poor;
	VALUE housing_spending_ratio = 0.3f;
	VALUE everyday_spending_ratio = state.defines.alice_needs_ev_spend * (1.f - is_poor);
	VALUE luxury_spending_ratio = state.defines.alice_needs_lx_spend * (1.f - is_poor);
	VALUE education_spending_ratio = (0.2f) * (1.f - is_poor);
	VALUE investment_ratio = adaptive_ve::max<VALUE>(investment_rate<VALUE>(state, ids), 0.0f);
	VALUE banking_ratio = adaptive_ve::max<VALUE>(bank_saving_rate<VALUE>(state, ids), 0.0f);

	VALUE total_spending_ratio =
		life_spending_ratio
		+ housing_spending_ratio
		+ everyday_spending_ratio
		+ luxury_spending_ratio
		+ education_spending_ratio
		+ investment_ratio
		+ banking_ratio;

	if constexpr(std::same_as<POPS, dcon::pop_id>) {
		total_spending_ratio = total_spending_ratio < 1.f ? 1.f : total_spending_ratio;
	} else {
		total_spending_ratio = adaptive_ve::select<BOOL_VALUE, VALUE>(total_spending_ratio < 1.f, ve::fp_vector{ 1.f }, total_spending_ratio);
	}

	// normalize:

	life_spending_ratio = life_spending_ratio / total_spending_ratio;
	housing_spending_ratio = housing_spending_ratio / total_spending_ratio;
	everyday_spending_ratio = everyday_spending_ratio / total_spending_ratio;
	luxury_spending_ratio = luxury_spending_ratio / total_spending_ratio;
	education_spending_ratio = education_spending_ratio / total_spending_ratio;
	investment_ratio = investment_ratio / total_spending_ratio;
	banking_ratio = banking_ratio / total_spending_ratio;

	// set actual budgets

	VALUE spend_on_life_needs = life_spending_ratio * savings;
	VALUE spend_on_housing = housing_spending_ratio * savings;
	VALUE spend_on_everyday_needs = everyday_spending_ratio * savings;
	VALUE spend_on_luxury_needs = luxury_spending_ratio * savings;
	VALUE spend_on_education = education_spending_ratio * savings;
	VALUE spend_on_investments = investment_ratio * savings;
	VALUE spend_on_bank_savings = banking_ratio * savings;

	// upload data to structure
	// here we do logic which can't be made uniform

	VALUE satisfaction = state.world.pop_get_satisfaction(ids);


	// ##########
	// life needs
	// ##########

	VALUE old_life = pop_demographics::get_life_needs(state, ids);
	VALUE subsistence = adjusted_subsistence_score<VALUE, decltype(provs)>(state, provs);
	BOOL_VALUE rgo_worker = state.world.pop_type_get_is_paid_rgo_worker(pop_type);
	subsistence = adaptive_ve::select<BOOL_VALUE, VALUE>(rgo_worker, subsistence, 0.f);
	VALUE available_subsistence = adaptive_ve::min<VALUE>(subsistence_score_life, subsistence);
	subsistence = subsistence - available_subsistence;
	VALUE qol_from_subsistence = available_subsistence / subsistence_score_life;
	// Households reduce discretionary quantity in a downturn, but basic demand
	// never disappears.  The floor prevents a zero-satisfaction feedback loop
	// where starving POPs stop demanding the goods required to recover.
	VALUE demand_scale_life = adaptive_ve::min<VALUE>(1.f,
		adaptive_ve::max<VALUE>(0.60f, 0.60f + 0.40f * satisfaction));
	result.life_needs.demand_scale = demand_scale_life;// * demand_scale_life + 0.01f;
	result.life_needs.required =
		result.life_needs.demand_scale
		* life_costs
		* pop_size
		/ state.defines.alice_needs_scaling_factor;
	auto zero_life_costs = result.life_needs.required == 0;
	/*
	auto rich_but_life_needs_are_not_satisfied = adaptive_ve::select<BOOL_VALUE, VALUE>(
		zero_life_costs,
		1.f,
		adaptive_ve::min<VALUE>
			(
				2.f,
				adaptive_ve::max<VALUE>(0.f, spend_on_life_needs - result.life_needs.required * 5.f) / result.life_needs.required
			)
	);
	*/
	result.life_needs.spent = adaptive_ve::min<VALUE>(spend_on_life_needs, result.life_needs.required * (1.f + is_rich));
	result.life_needs.satisfied_with_money_ratio = adaptive_ve::select<BOOL_VALUE, VALUE>(
		zero_life_costs,
		10.f,
		result.life_needs.spent
		/ result.life_needs.required
	);
	// subsistence gives free "level of consumption"
	result.life_needs.satisfied_for_free_ratio = qol_from_subsistence / (1.f + result.life_needs.demand_scale);
	result.spent_total = result.spent_total + result.life_needs.spent;
	savings = savings - result.life_needs.spent;


	// ##############
	// housing
	// ##############
	auto housing_price = state.world.province_get_service_price(provs, services::list::urban_housing);
	// Rural households are covered by the subsistence economy. Only the urban
	// share of a province participates in the explicit urban-housing market;
	// charging every POP made rural demand overwhelm the seeded city stock.
	// Housing is supplied by the city stock that landlords have actually built
	// and put on the market.  The maximum private size is merely developable
	// land; treating it as homes made a future construction project satisfy POPs
	// before a single dwelling existed.
	VALUE urban_capacity = state.world.province_get_advanced_province_building_private_size(
		provs, advanced_province_buildings::list::local_cities_and_towns);
	VALUE province_population = state.world.province_get_demographics(
		provs, demographics::total);
	VALUE urban_share = adaptive_ve::min<VALUE>(1.f,
		adaptive_ve::max<VALUE>(0.f, urban_capacity / (province_population + 1.f)));
	result.housing.demand_scale = urban_share;
	result.housing.required = pop_size * urban_share * housing_price;
	auto zero_housing_costs = result.housing.required == 0;
	//result.housing.spent = adaptive_ve::min<VALUE>(savings, adaptive_ve::min<VALUE>(spend_on_housing, result.housing.required));
	result.housing.spent = adaptive_ve::min<VALUE>(savings, adaptive_ve::min<VALUE>(result.housing.required * 1.5f, spend_on_housing));
	result.housing.satisfied_for_free_ratio = 0.f;
	result.housing.satisfied_with_money_ratio = safe_spending_ratio(
		zero_housing_costs,
		result.housing.spent,
		result.housing.required,
		VALUE{ 1.f }
	);
	result.spent_total = result.spent_total + result.housing.spent;
	savings = savings - result.housing.spent;


	// ##############
	// everyday needs
	// ##############

	auto old_everyday = pop_demographics::get_everyday_needs(state, ids);
	auto demand_scale_everyday = adaptive_ve::min<VALUE>(1.20f,
		adaptive_ve::max<VALUE>(0.35f, 0.35f + 0.65f * old_everyday / base_qol));
	result.everyday_needs.demand_scale = demand_scale_everyday;// * demand_scale_everyday + 0.01f;
	result.everyday_needs.required =
		result.everyday_needs.demand_scale
		* everyday_costs
		* pop_size
		/ state.defines.alice_needs_scaling_factor;
	auto zero_everyday_costs = result.everyday_needs.required == 0;
	auto rich_but_everyday_needs_are_not_satisfied = 0.f;
	/*
	adaptive_ve::select<BOOL_VALUE, VALUE>(
		zero_everyday_costs,
		1.f,
		adaptive_ve::min<VALUE>
		(
			5.f,
			adaptive_ve::max<VALUE>(0.f, spend_on_everyday_needs - result.everyday_needs.required * 5.f) / result.everyday_needs.required
		)
	);
	*/
	result.everyday_needs.spent = adaptive_ve::min<VALUE>(savings, adaptive_ve::min<VALUE>(spend_on_everyday_needs, result.everyday_needs.required * (1.f + is_rich)));
	result.everyday_needs.satisfied_with_money_ratio = adaptive_ve::select<BOOL_VALUE, VALUE>(
		zero_everyday_costs,
		10.f,
		result.everyday_needs.spent
		/ result.everyday_needs.required
	);
	result.everyday_needs.satisfied_for_free_ratio = 0.f;
	result.spent_total = result.spent_total + result.everyday_needs.spent;
	savings = savings - result.everyday_needs.spent;



	// ############
	// luxury needs
	// ############

	auto old_luxury = pop_demographics::get_luxury_needs(state, ids);
	auto demand_scale_luxury = adaptive_ve::min<VALUE>(1.50f,
		adaptive_ve::max<VALUE>(0.05f, 0.05f + 0.95f * old_luxury / base_qol));
	result.luxury_needs.demand_scale = demand_scale_luxury;// * demand_scale_luxury + 0.01f;
	result.luxury_needs.required =
		result.luxury_needs.demand_scale
		* luxury_costs
		* pop_size
		/ state.defines.alice_needs_scaling_factor;
	auto zero_luxury_costs = result.luxury_needs.required == 0;
	auto rich_but_luxury_needs_are_not_satisfied = 0.f;
	/*
	adaptive_ve::select<BOOL_VALUE, VALUE>(
		zero_luxury_costs,
		1.f,
		adaptive_ve::min<VALUE>
		(
			5.f,
			adaptive_ve::max<VALUE>(0.f, spend_on_luxury_needs - result.luxury_needs.required * 5.f) / result.luxury_needs.required
		)
	);
	*/
	result.luxury_needs.spent = adaptive_ve::min<VALUE>(savings, adaptive_ve::min<VALUE>(spend_on_luxury_needs, result.luxury_needs.required * (1.f + is_rich)));
	result.luxury_needs.satisfied_for_free_ratio = 0.f;
	result.luxury_needs.satisfied_with_money_ratio = adaptive_ve::select<BOOL_VALUE, VALUE>(
		zero_luxury_costs,
		10.f,
		result.luxury_needs.spent
		/ result.luxury_needs.required
	);
	result.spent_total = result.spent_total + result.luxury_needs.spent;
	savings = savings - result.luxury_needs.spent;



	// #########
	// education
	// #########

	auto education_price = state.world.province_get_service_price(provs, services::list::education);

	auto literacy = pop_demographics::get_literacy(state, ids);
	result.education.demand_scale = literacy * literacy / 0.5f + 0.1f;
	auto required_education = result.education.demand_scale * pop_size;
	result.education.required = required_education * education_price;

	// if education is crazy expensive and impossible to access, we want to spend 0 because it's hopeless

	auto scale_from_being_rich = adaptive_ve::select<BOOL_VALUE, VALUE>(
		required_education == 0.f,
		1.f,
		adaptive_ve::max<VALUE>
		(
			0.f,
			adaptive_ve::min<VALUE>
			(
				10.f,
				adaptive_ve::max<VALUE>(0.f, spend_on_education - result.education.required * 5.f)
				/ result.education.required
			) - 0.1f
		)
	);

	auto education_scale_nation = adaptive_ve::select<BOOL_VALUE, VALUE>(result.can_use_free_services > 0.f, 1.f, 0.f);

	auto personal_desired_spending = result.education.required * scale_from_being_rich;
	auto can_actually_spend = adaptive_ve::min<VALUE>(savings, spend_on_education);
	auto total_personal_spending = adaptive_ve::min<VALUE>(can_actually_spend, personal_desired_spending);
	auto education_scale_private = scale_from_being_rich * adaptive_ve::select<BOOL_VALUE, VALUE>(personal_desired_spending > 0.f, total_personal_spending / personal_desired_spending, 0.f);

	//auto probability_to_get_education_for_free = state.world.province_get_service_satisfaction_for_free(provs, services::list::education);
	auto expected_help_from_nation = adaptive_ve::select<BOOL_VALUE, VALUE>(result.can_use_free_services > 0.f, result.education.required, 0.f);
	auto total_expected_spending = expected_help_from_nation + total_personal_spending;

	auto potentially_free_ratio = adaptive_ve::select<BOOL_VALUE, VALUE>(total_expected_spending > 0.f, expected_help_from_nation / total_expected_spending, 0.f);

	//auto supposed_to_spend = adaptive_ve::min<VALUE>(savings, adaptive_ve::min<VALUE>(spend_on_education, result.education.required * rich_but_uneducated));
	//auto potentially_free_ratio = expected_help_from_nation / adaptive_ve::max<VALUE>(1.f, rich_but_uneducated);
	//auto ratio_of_free_education = decltype(potentially_free_ratio)(0.f);
	//ratio_of_free_education = adaptive_ve::select<BOOL_VALUE, VALUE>(result.can_use_free_services > 0.f, potentially_free_ratio, ratio_of_free_education);

	result.education.satisfied_for_free_ratio = education_scale_nation;
	result.education.spent = total_personal_spending;
	result.education.satisfied_with_money_ratio = education_scale_private;
	result.spent_total = result.spent_total + result.education.spent;
	savings = savings - result.education.spent;



	// ###########
	// investments
	// ###########

	result.investments.required = 0.f;
	result.investments.satisfied_with_money_ratio = 1.f;
	result.investments.satisfied_for_free_ratio = 0.f;
	result.investments.spent = spend_on_investments;
	result.investments.demand_scale = 0.f;
	result.spent_total = result.spent_total + result.investments.spent;
	savings = savings - result.investments.spent;

	// #####
	// banks
	// #####

	result.bank_savings.required = 0.f;
	result.bank_savings.satisfied_with_money_ratio = 1.f;
	result.bank_savings.satisfied_for_free_ratio = 0.f;
	result.bank_savings.spent = spend_on_bank_savings;
	result.bank_savings.demand_scale = 0.f;
	result.spent_total = result.spent_total + result.bank_savings.spent;
	savings = savings - result.bank_savings.spent;

	result.remaining_savings = savings;

	return result;
}


float estimate_artisan_income(sys::state const& state, dcon::province_id pid, dcon::pop_type_id ptid, float size) {
	auto const artisan_type = state.culture_definitions.artisans;
	auto key = demographics::to_key(state, artisan_type);

	if(ptid != artisan_type) {
		return 0.f;
	}

	auto artisan_profit = state.world.province_get_artisan_profit(pid);
	auto total = artisan_profit;
	auto dividents = 0.f;

	auto num_artisans = state.world.province_get_demographics(pid, key);
	auto per_artisan = num_artisans > 0.f ? dividents / num_artisans : 0.f;
	return size * per_artisan;
}

float estimate_artisan_income(sys::state const& state, dcon::pop_id pop) {
	return estimate_artisan_income(
		state,
		state.world.pop_get_province_from_pop_location(pop),
		state.world.pop_get_poptype(pop),
		state.world.pop_get_size(pop)
	);
}

constexpr inline float national_elite_trade_weight_multiplier = 1000.f;

float estimate_local_trade_income(sys::state const& state, dcon::province_id pid, dcon::market_id mid, dcon::pop_type_id ptid, float size) {
	auto sids = state.world.market_get_zone_from_local_market(mid);
	auto nation = state.world.province_get_nation_from_province_control(pid);
	auto const capis_def = state.culture_definitions.capitalists;
	auto capis_key = demographics::to_key(state, capis_def);
	auto elites = state.world.nation_get_demographics(nation, capis_key);
	auto local_population_province = state.world.province_get_demographics(pid, demographics::total);
	auto local_population_market = state.world.state_instance_get_demographics(sids, demographics::total);
	auto total = local_population_market + elites * national_elite_trade_weight_multiplier * local_population_province / (local_population_market + 1.f);

	if(total == 0.f) {
		return 0.f;
	}

	auto balance = state.world.market_get_stockpile(mid, economy::money);
	auto trade_dividents = balance > 0.f ? balance * trade_dividents_rate : 0.f;

	return size / total * trade_dividents;
}

/*
Currently ignores income from national trade.
*/
float estimate_trade_income(sys::state const& state, dcon::pop_id pop) {
	auto provs = state.world.pop_get_province_from_pop_location(pop);
	auto states = state.world.province_get_state_membership(provs);
	auto markets = state.world.state_instance_get_market_from_local_market(states);

	return estimate_local_trade_income(
		state,
		provs,
		markets,
		state.world.pop_get_poptype(pop),
		state.world.pop_get_size(pop)
	);
}

money_from_nation estimate_income_from_nation(sys::state const& state, dcon::pop_id pop) {
	auto capitalists_key = demographics::to_key(state, state.culture_definitions.capitalists);
	auto aristocracy_key = demographics::to_key(state, state.culture_definitions.aristocrat);

	auto prov = state.world.pop_get_province_from_pop_location(pop);
	auto owner = state.world.province_get_nation_from_province_ownership(prov);
	auto population = state.world.nation_get_demographics(owner, demographics::total);
	auto unemployed = population - state.world.nation_get_demographics(owner, demographics::employed);
	auto capitalists = state.world.nation_get_demographics(owner, capitalists_key);
	auto aristocrats = state.world.nation_get_demographics(owner, aristocracy_key);
	auto investors = capitalists + aristocrats;

	auto states = state.world.province_get_state_membership(prov);
	auto markets = state.world.state_instance_get_market_from_local_market(states);
	auto owner_spending = state.world.nation_get_spending_level(owner);

	auto size = state.world.pop_get_size(pop);
	auto adj_size = size / state.defines.alice_needs_scaling_factor;

	auto budget = state.world.nation_get_last_base_budget(owner);

	auto social_budget =
		owner_spending
		* budget
		* float(state.world.nation_get_social_spending(owner))
		/ 100.f;

	auto investment_budget =
		owner_spending
		* budget
		* float(state.world.nation_get_domestic_investment_spending(owner))
		/ 100.f;

	auto const p_level = state.world.nation_get_modifier_values(owner, sys::national_mod_offsets::pension_level);
	auto const unemp_level = state.world.nation_get_modifier_values(owner, sys::national_mod_offsets::unemployment_benefit);

	auto pension_ratio = p_level * population > 0.f ? p_level * population / (p_level * population + unemp_level * unemployed) : 0.f;
	auto unemployment_ratio = unemp_level * unemployed > 0.f ? unemp_level * unemployed / (p_level * population + unemp_level * unemployed) : 0.f;

	auto const pension_per_person =
		pension_ratio
		* social_budget
		/ (population + 1.f);

	auto const benefits_per_person =
		unemployment_ratio
		* social_budget
		/ (unemployed + 1.f);
	auto const social_execution = nations::policy_execution::effective_policy(
		state, owner, prov,
		nations::policy_execution::policy_kind::social_benefits).effective_execution;

	auto const payment_per_investor =
		investment_budget
		/ (investors + 1.f);

	auto const m_spending = owner_spending * float(state.world.nation_get_military_spending(owner)) / 100.0f;

	auto types = state.world.pop_get_poptype(pop);

	auto ln_types = state.world.pop_type_get_life_needs_income_type(types);
	auto en_types = state.world.pop_type_get_everyday_needs_income_type(types);
	auto lx_types = state.world.pop_type_get_luxury_needs_income_type(types);

	auto ln_costs = state.world.market_get_life_needs_costs(markets, types);
	auto en_costs = state.world.market_get_everyday_needs_costs(markets, types);
	auto lx_costs = state.world.market_get_luxury_needs_costs(markets, types);

	auto total_costs = ln_costs + en_costs + lx_costs;

	auto is_military_requires_life_needs = ln_types == int32_t(culture::income_type::military);
	auto is_military_requires_everyday_needs = en_types == int32_t(culture::income_type::military);
	auto is_military_requires_luxury_needs = lx_types == int32_t(culture::income_type::military);
	auto is_military = is_military_requires_life_needs || is_military_requires_everyday_needs || is_military_requires_luxury_needs;
	auto is_investor = (types == state.culture_definitions.capitalists) || (types == state.culture_definitions.aristocrat);

	auto mil_pay = 0.f;
	mil_pay += is_military_requires_life_needs ? m_spending * adj_size * ln_costs * payouts_spending_multiplier : 0.0f;
	mil_pay += is_military_requires_everyday_needs ? m_spending * adj_size * en_costs * payouts_spending_multiplier : 0.0f;
	mil_pay += is_military_requires_luxury_needs ? m_spending * adj_size * lx_costs * payouts_spending_multiplier : 0.0f;

	return {
		.pension = social_execution * pension_per_person * size,
		.unemployment = is_military ? 0.f : social_execution * benefits_per_person * (size - pop_demographics::get_employment(state, pop)),
		.military = mil_pay,
		.investment = size * price_properties::labor::min * 0.05f + (is_investor ? payment_per_investor * size : 0.f)
	};
}

std::vector<labor_ratio_wage> estimate_wage(sys::state const& state, dcon::province_id pid, dcon::pop_type_id ptid, bool accepted, float size) {
	float no_education_wage =
		state.world.province_get_labor_price(pid, labor::no_education)
		* state.world.province_get_labor_supply_sold(pid, labor::no_education);
	float basic_education_wage =
		state.world.province_get_labor_price(pid, labor::basic_education)
		* state.world.province_get_labor_supply_sold(pid, labor::basic_education); // craftsmen
	float high_education_wage =
		state.world.province_get_labor_price(pid, labor::high_education)
		* state.world.province_get_labor_supply_sold(pid, labor::high_education); // clerks, clergy and bureaucrats
	float guild_education_wage =
		state.world.province_get_labor_price(pid, labor::guild_education)
		* state.world.province_get_labor_supply_sold(pid, labor::guild_education); // artisans
	float high_education_and_accepted_wage =
		state.world.province_get_labor_price(pid, labor::high_education_and_accepted)
		* state.world.province_get_labor_supply_sold(pid, labor::high_education_and_accepted); // clerks, clergy and bureaucrats of accepted culture

	if(state.world.pop_type_get_is_paid_rgo_worker(ptid)) {
		auto no_education = state.world.province_get_pop_labor_distribution(pid, pop_labor::rgo_worker_no_education);
		return { {labor::no_education, no_education, no_education * size * no_education_wage } };
	} else if(state.culture_definitions.primary_factory_worker == ptid) {
		auto no_education = state.world.province_get_pop_labor_distribution(pid, pop_labor::primary_no_education);
		auto basic_education = state.world.province_get_pop_labor_distribution(pid, pop_labor::primary_basic_education);
		return {
			{labor::no_education, no_education, no_education * size * no_education_wage },
			{labor::basic_education, basic_education, basic_education * size * basic_education_wage }
		};
	} else if(state.culture_definitions.secondary_factory_worker == ptid || state.culture_definitions.bureaucrat == ptid || state.culture_definitions.clergy == ptid) {
		if(accepted) {
			auto no_education = state.world.province_get_pop_labor_distribution(pid, pop_labor::high_education_accepted_no_education);
			auto basic_education = state.world.province_get_pop_labor_distribution(pid, pop_labor::high_education_accepted_basic_education);
			auto high_education = state.world.province_get_pop_labor_distribution(pid, pop_labor::high_education_accepted_high_education);
			auto high_education_accepted = state.world.province_get_pop_labor_distribution(pid, pop_labor::high_education_accepted_high_education_accepted);
			return {
				{labor::no_education, no_education, no_education * size * no_education_wage },
				{labor::basic_education, basic_education, basic_education * size * basic_education_wage },
				{labor::high_education, high_education, high_education * size * high_education_wage },
				{labor::high_education_and_accepted, high_education_accepted, high_education_accepted * size * high_education_and_accepted_wage }
			};
		} else {
			auto no_education = state.world.province_get_pop_labor_distribution(pid, pop_labor::high_education_not_accepted_no_education);
			auto basic_education = state.world.province_get_pop_labor_distribution(pid, pop_labor::high_education_not_accepted_basic_education);
			auto high_education = state.world.province_get_pop_labor_distribution(pid, pop_labor::high_education_not_accepted_high_education);
			return {
				{labor::no_education, no_education, no_education * size * no_education_wage },
				{labor::basic_education, basic_education, basic_education * size * basic_education_wage },
				{labor::high_education, high_education, high_education * size * high_education_wage },
			};
		}
	}
	return {};
}

std::vector<labor_ratio_wage> estimate_wage(sys::state const& state, dcon::pop_id pop) {
	return estimate_wage(
		state,
		state.world.pop_get_province_from_pop_location(pop),
		state.world.pop_get_poptype(pop),
		state.world.pop_get_is_primary_or_accepted_culture(pop),
		state.world.pop_get_size(pop)
	);
}

float estimate_total_wage(sys::state const& state, dcon::pop_id pop) {
	float total = 0.f;
	auto list = estimate_wage(state, pop);
	for(auto& item : list) {
		total += item.wage;
	}
	return total;
}


float estimate_slave_income(sys::state const& state, dcon::province_id pid, dcon::pop_type_id ptid, float size) {
	float no_education_wage =
		state.world.province_get_labor_price(pid, labor::no_education)
		* state.world.province_get_labor_supply_sold(pid, labor::no_education);
	float rgo_workers_wage =
		state.world.province_get_pop_labor_distribution(pid, pop_labor::rgo_worker_no_education)
		* no_education_wage;
	auto income_from_slaves = 0.f;
	for(auto pl : state.world.province_get_pop_location(pid)) {
		if(pl.get_pop().get_poptype() == state.culture_definitions.slaves) {
			income_from_slaves += pl.get_pop().get_size() * rgo_workers_wage;
		}
	}

	float aristocrats_share = state.world.province_get_landowners_share(pid);
	float num_aristocrat = state.world.province_get_demographics(
		pid,
		demographics::to_key(state, state.culture_definitions.aristocrat)
	);
	if(income_from_slaves >= 0.f && num_aristocrat > 0.f && state.culture_definitions.aristocrat == ptid) {
		return size * income_from_slaves / num_aristocrat;
	} else {
		return 0.f;
	}
}

float estimate_slave_income(sys::state const& state, dcon::pop_id pop) {
	return estimate_slave_income(
		state,
		state.world.pop_get_province_from_pop_location(pop),
		state.world.pop_get_poptype(pop),
		state.world.pop_get_size(pop)
	);
}


//local merchants take a cut from most local monetary operations
float estimate_next_day_raw_income(
	sys::state const& state,
	dcon::pop_id pop
) {
	auto estimated =
		estimate_artisan_income(state, pop)
		+ estimate_slave_income(state, pop)
		+ estimate_trade_income(state, pop)
		+ estimate_total_wage(state, pop);

	auto from_nation = estimate_income_from_nation(state, pop);

	estimated +=
		from_nation.investment
		+ from_nation.military
		+ from_nation.pension
		+ from_nation.unemployment;

	return estimated;
}

float estimate_next_day_budget_before_taxes(
	sys::state const& state,
	dcon::pop_id pop
) {
	auto current = state.world.pop_get_savings(pop);
	current -= prepare_pop_budget(state, pop).spent_total;

	auto estimated = current
		+ estimate_artisan_income(state, pop)
		+ estimate_slave_income(state, pop)
		+ estimate_trade_income(state, pop)
		+ estimate_total_wage(state, pop);

	auto from_nation = estimate_income_from_nation(state, pop);

	estimated +=
		from_nation.investment
		+ from_nation.military
		+ from_nation.pension
		+ from_nation.unemployment;

	return estimated;
}

float estimate_trade_spending(
	sys::state const& state,
	dcon::pop_id pop
) {
	auto next_day = state.world.pop_get_savings(pop);
	return market_tax * next_day;
}

float estimate_tax_spending(
	sys::state const& state,
	dcon::pop_id pop,
	float tax_rate
) {
	auto next_day = estimate_next_day_raw_income(state, pop);
	return next_day * (1.f - market_tax) * tax_rate;
}

float estimate_pop_demand_internal_life(
	sys::state const& state, dcon::commodity_id c, dcon::pop_id pop,
	pops::vectorized_pops_budget<float>& budget,
	float mult_per_strata[3], float need_weight, float invention_factor
) {
	auto pop_type = state.world.pop_get_poptype(pop);
	auto strata = state.world.pop_type_get_strata(pop_type);
	auto pop_size = state.world.pop_get_size(pop);
	return budget.life_needs.demand_scale
		* budget.life_needs.satisfied_with_money_ratio
		* need_weight
		* mult_per_strata[strata]
		* state.defines.alice_lf_needs_scale
		* state.world.pop_type_get_life_needs(pop_type, c)
		* pop_size
		/ state.defines.alice_needs_scaling_factor;
}
float estimate_pop_demand_internal_everyday(
	sys::state const& state, dcon::commodity_id c, dcon::pop_id pop,
	pops::vectorized_pops_budget<float>& budget,
	float mult_per_strata[3], float need_weight, float invention_factor
) {
	auto pop_type = state.world.pop_get_poptype(pop);
	auto strata = state.world.pop_type_get_strata(pop_type);
	auto pop_size = state.world.pop_get_size(pop);
	return budget.everyday_needs.demand_scale
		* budget.everyday_needs.satisfied_with_money_ratio
		* need_weight
		* mult_per_strata[strata]
		* state.defines.alice_ev_needs_scale
		* state.world.pop_type_get_everyday_needs(pop_type, c)
		* pop_size
		/ state.defines.alice_needs_scaling_factor
		* invention_factor;
}
float estimate_pop_demand_internal_luxury(
	sys::state const& state, dcon::commodity_id c, dcon::pop_id pop,
	pops::vectorized_pops_budget<float>& budget,
	float mult_per_strata[3], float need_weight, float invention_factor
) {
	auto pop_type = state.world.pop_get_poptype(pop);
	auto strata = state.world.pop_type_get_strata(pop_type);
	auto pop_size = state.world.pop_get_size(pop);
	return budget.luxury_needs.demand_scale
		* budget.luxury_needs.satisfied_with_money_ratio
		* need_weight
		* mult_per_strata[strata]
		* state.defines.alice_lx_needs_scale
		* state.world.pop_type_get_luxury_needs(pop_type, c)
		* pop_size
		/ state.defines.alice_needs_scaling_factor
		* invention_factor;
}

float estimate_pop_spending_life(sys::state const& state, dcon::pop_id pop, dcon::commodity_id cid) {
	auto pid = state.world.pop_get_province_from_pop_location(pop);
	auto nation = state.world.province_get_nation_from_province_ownership(pid);
	auto zone = state.world.province_get_state_membership(pid);
	auto market = state.world.state_instance_get_market_from_local_market(zone);
	auto budget = prepare_pop_budget(state, pop);
	auto invention_count = 0.f;
	state.world.for_each_invention([&](auto iid) {
		invention_count += state.world.nation_get_active_inventions(nation, iid) ? 1.0f : 0.0f;
	});
	auto invention_factor = state.defines.invention_impact_on_demand * invention_count + 1.f;
	auto weight = state.world.market_get_life_needs_weights(market, cid);
	float mul[3] = {
		state.world.nation_get_modifier_values(
			nation, sys::national_mod_offsets::poor_life_needs) + 1.0f,
		state.world.nation_get_modifier_values(
			nation, sys::national_mod_offsets::middle_life_needs) + 1.0f,
		state.world.nation_get_modifier_values(
			nation, sys::national_mod_offsets::rich_life_needs) + 1.0f
	};
	auto demand = pops::estimate_pop_demand_internal_life(
		state, cid, pop, budget, mul, weight, invention_factor
	);
	auto actually_bought = market_clearing::fill(state, market, cid,
		market_clearing::demand_class::life_needs);
	auto cost = economy::price(state, market, cid);
	return demand * actually_bought * cost;
}

float estimate_pop_spending_everyday(sys::state const& state, dcon::pop_id pop, dcon::commodity_id cid) {
	auto pid = state.world.pop_get_province_from_pop_location(pop);
	auto nation = state.world.province_get_nation_from_province_ownership(pid);
	auto zone = state.world.province_get_state_membership(pid);
	auto market = state.world.state_instance_get_market_from_local_market(zone);
	auto budget = prepare_pop_budget(state, pop);
	auto invention_count = 0.f;
	state.world.for_each_invention([&](auto iid) {
		invention_count += state.world.nation_get_active_inventions(nation, iid) ? 1.0f : 0.0f;
	});
	auto invention_factor = state.defines.invention_impact_on_demand * invention_count + 1.f;
	auto weight = state.world.market_get_everyday_needs_weights(market, cid);
	float mul[3] = {
		state.world.nation_get_modifier_values(
			nation, sys::national_mod_offsets::poor_everyday_needs) + 1.0f,
		state.world.nation_get_modifier_values(
			nation, sys::national_mod_offsets::middle_everyday_needs) + 1.0f,
		state.world.nation_get_modifier_values(
			nation, sys::national_mod_offsets::rich_everyday_needs) + 1.0f
	};
	auto demand = pops::estimate_pop_demand_internal_everyday(
		state, cid, pop, budget, mul, weight, invention_factor
	);
	auto actually_bought = market_clearing::fill(state, market, cid,
		market_clearing::demand_class::everyday_needs);
	auto cost = economy::price(state, market, cid);
	return demand * actually_bought * cost;
}

float estimate_pop_spending_luxury(sys::state const& state, dcon::pop_id pop, dcon::commodity_id cid) {
	auto pid = state.world.pop_get_province_from_pop_location(pop);
	auto nation = state.world.province_get_nation_from_province_ownership(pid);
	auto zone = state.world.province_get_state_membership(pid);
	auto market = state.world.state_instance_get_market_from_local_market(zone);
	auto budget = prepare_pop_budget(state, pop);
	auto invention_count = 0.f;
	state.world.for_each_invention([&](auto iid) {
		invention_count += state.world.nation_get_active_inventions(nation, iid) ? 1.0f : 0.0f;
	});
	auto invention_factor = state.defines.invention_impact_on_demand * invention_count + 1.f;
	auto weight = state.world.market_get_luxury_needs_weights(market, cid);
	float mul[3] = {
		state.world.nation_get_modifier_values(
			nation, sys::national_mod_offsets::poor_luxury_needs) + 1.0f,
		state.world.nation_get_modifier_values(
			nation, sys::national_mod_offsets::middle_luxury_needs) + 1.0f,
		state.world.nation_get_modifier_values(
			nation, sys::national_mod_offsets::rich_luxury_needs) + 1.0f
	};
	auto demand = pops::estimate_pop_demand_internal_luxury(
		state, cid, pop, budget, mul, weight, invention_factor
	);
	auto actually_bought = market_clearing::fill(state, market, cid,
		market_clearing::demand_class::luxury_needs);
	auto cost = economy::price(state, market, cid);
	return demand * actually_bought * cost;
}

vectorized_pops_budget<float> prepare_pop_budget(const sys::state& state, dcon::pop_id ids) {
	return prepare_pop_budget_templated(state, ids);
}

}

float estimate_pops_consumption(sys::state const& state, dcon::commodity_id c, dcon::province_id p) {
	auto zone = state.world.province_get_state_membership(p);
	auto market = state.world.state_instance_get_market_from_local_market(zone);

	auto satisfaction = state.world.market_get_actual_probability_to_buy(market, c);

	auto nation = state.world.province_get_nation_from_province_ownership(p);

	auto weight_life = state.world.market_get_life_needs_weights(market, c);
	auto weight_everyday = state.world.market_get_everyday_needs_weights(market, c);
	auto weight_luxury = state.world.market_get_luxury_needs_weights(market, c);

	float life_mul[3] = {
		state.world.nation_get_modifier_values(
			nation, sys::national_mod_offsets::poor_life_needs) + 1.0f,
		state.world.nation_get_modifier_values(
			nation, sys::national_mod_offsets::middle_life_needs) + 1.0f,
		state.world.nation_get_modifier_values(
			nation, sys::national_mod_offsets::rich_life_needs) + 1.0f
	};
	float everyday_mul[3] = {
		state.world.nation_get_modifier_values(
			nation, sys::national_mod_offsets::poor_everyday_needs) + 1.0f,
		state.world.nation_get_modifier_values(
			nation, sys::national_mod_offsets::middle_everyday_needs) + 1.0f,
		state.world.nation_get_modifier_values(
			nation, sys::national_mod_offsets::rich_everyday_needs) + 1.0f
	};
	float luxury_mul[3] = {
		state.world.nation_get_modifier_values(
			nation, sys::national_mod_offsets::poor_luxury_needs) + 1.0f,
		state.world.nation_get_modifier_values(
			nation, sys::national_mod_offsets::middle_luxury_needs) + 1.0f,
		state.world.nation_get_modifier_values(
			nation, sys::national_mod_offsets::rich_luxury_needs) + 1.0f,
	};

	auto invention_count = 0.f;
	state.world.for_each_invention([&](auto iid) {
		invention_count += state.world.nation_get_active_inventions(nation, iid) ? 1.0f : 0.0f;
	});
	auto invention_factor = state.defines.invention_impact_on_demand * invention_count + 1.f;

	float total = 0.f;
	state.world.province_for_each_pop_location(p, [&](auto location) {
		dcon::pop_id pop = state.world.pop_location_get_pop(location);

		auto pop_type = state.world.pop_get_poptype(pop);
		auto strata = state.world.pop_type_get_strata(pop_type);

		pops::vectorized_pops_budget<float> budget = pops::prepare_pop_budget(state, pop);

		auto consumption_life = pops::estimate_pop_demand_internal_life(
			state, c, pop, budget, life_mul, weight_life, invention_factor
		);
		auto consumption_everyday = pops::estimate_pop_demand_internal_everyday(
			state, c, pop, budget, everyday_mul, weight_everyday, invention_factor
		);
		auto consumption_luxury = pops::estimate_pop_demand_internal_luxury(
			state, c, pop, budget, luxury_mul, weight_luxury, invention_factor
		);

		total += consumption_life + consumption_everyday + consumption_luxury;
	});

	return total * satisfaction;
}
}
