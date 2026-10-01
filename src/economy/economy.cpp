#include "economy.hpp"
#include "economy_government.hpp"
#include "economy_stats.hpp"
#include "economy_production.hpp"
#include "economy_trade_routes.hpp"
#include "world_trade_capacity.hpp"
#include "commodity_logistics.hpp"
#include "economy/physical/factory_inputs.hpp"
#include "economy/firm_agency.hpp"
#include "economy/capital_projects.hpp"
#include "economy/industrial_dynamics.hpp"
#include "economy/banking/banking.hpp"
#include "market_access.hpp"
#include "cargo_transit.hpp"
#include "construction.hpp"
#include "demographics.hpp"
#include "demographics_templates.hpp"
#include "dcon_generated.hpp"
#include "ai_economy.hpp"
#include "system_state.hpp"
#include "prng.hpp"
#include "province_templates.hpp"
#include "triggers.hpp"
#include "advanced_province_buildings.hpp"
#include "price.hpp"
#include "economy_pops.hpp"
#include "commodities.hpp"
#include "province.hpp"
#include "economy/physical/land.hpp"
#include "money.hpp"
#include "economy_constants.hpp"
#include "economy_factory_view.hpp"
#include "compat/alice/legacy_bridge.hpp"
#include "compat/technology_legacy_adapter.hpp"
#include "events.hpp"
#include "commands.hpp"
#include "land_ownership.hpp"
#include "labor_relations.hpp"
#include "policy_execution.hpp"
#include "gamerule.hpp"
#include "economy/physical/shipments.hpp"
#include "economy/physical/deposits.hpp"
#include "economy/physical/freight_market.hpp"
#include "economy/physical/job_market.hpp"
#include "economy/physical/labor_dynamics.hpp"
#include "economy/physical/exact_person_goods.hpp"
#include "economy/physical/household_mobility.hpp"
#include "economy/physical/concrete_market.hpp"
#include "economy/exact_person_economy.hpp"
#include "world/spatial_runtime.hpp"
#include "governance/public_administration.hpp"
#include <vector>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#include <unordered_set>

namespace economy {

#ifndef NDEBUG
namespace {
	bool labor_distribution_debug_enabled() {
		static bool enabled = [] {
			auto const* value = std::getenv("ALICE_DEBUG_LABOR_DISTRIBUTION");
			return value && value[0] == '1';
		}();
		return enabled;
	}
}
#endif

void sanity_check([[maybe_unused]] sys::state& state) {
	// Compatibility hook; canonical domains validate their own invariants.
}

int32_t most_recent_price_record_index(sys::state& state) {
	return (state.current_date.value >> 4) % price_history_length;
}
int32_t previous_price_record_index(sys::state& state) {
	return ((state.current_date.value >> 4) + price_history_length - 1) % price_history_length;
}

int32_t most_recent_gdp_record_index(sys::state& state) {
	auto date = state.current_date.to_ymd(state.start_date);
	return (date.year * 4 + date.month / 3) % gdp_history_length;
}
int32_t previous_gdp_record_index(sys::state& state) {
	auto date = state.current_date.to_ymd(state.start_date);
	return ((date.year * 4 + date.month / 3) + gdp_history_length - 1) % gdp_history_length;
}


void populate_army_consumption(sys::state& state);
void populate_navy_consumption(sys::state& state);

// Returns factory types for which commodity is an output good
std::vector<dcon::factory_type_id> commodity_get_factory_types_as_output(sys::state const& state, dcon::commodity_id output_good) {
	std::vector<dcon::factory_type_id> types;
	for(auto t : state.world.in_factory_type) {
		if(t.get_output() == output_good) {
			types.push_back(t);
		}
	}
	return types;
}

// Frozen scenario fields for legacy UI; canonical bids never read them.
void initialize_compatibility_needs_weights(sys::state& state, dcon::market_id n) {
	auto zone = state.world.market_get_zone_from_local_market(n);
	auto nation = state.world.state_instance_get_nation_from_state_ownership(zone);
	{
		state.world.for_each_commodity([&](dcon::commodity_id c) {
			state.world.market_set_life_needs_weights(n, c, 0.001f);
		});
	}
	{
		state.world.for_each_commodity([&](dcon::commodity_id c) {
			state.world.market_set_everyday_needs_weights(n, c, 0.001f);
		});
	}
	{
		state.world.for_each_commodity([&](dcon::commodity_id c) {
			state.world.market_set_luxury_needs_weights(n, c, 0.001f);
		});
	}
}

void convert_commodities_into_ingredients(
	sys::state& state,
	std::vector<float>& buffer_commodities,
	std::vector<float>& buffer_ingredients,
	std::vector<float>& buffer_weights
) {
	state.world.for_each_commodity([&](dcon::commodity_id c) {
		float amount = buffer_commodities[c.index()];

		if(state.world.commodity_get_rgo_amount(c) > 0.f) {
			buffer_ingredients[c.index()] += amount;
		} else {
			//calculate input vectors weights:
			std::vector<float> weights;
			float total_weight = 0.f;
			float non_zero_count = 0.f;

			state.world.for_each_factory_type([&](dcon::factory_type_id t) {
				auto o = state.world.factory_type_get_output(t);
				if(o == c) {
					auto& inputs = state.world.factory_type_get_inputs(t);

					float weight_current = 0;

					for(uint32_t i = 0; i < economy::commodity_set::set_size; ++i) {
						if(inputs.commodity_type[i]) {
							float weight_input = buffer_weights[inputs.commodity_type[i].index()];
							total_weight += weight_input;
							weight_current += weight_input;
						} else {
							break;
						}
					}

					if(weight_current > 0.f)
						non_zero_count++;

					weights.push_back(weight_current);
				}
			});

			if(total_weight == 0) {
				for(size_t i = 0; i < weights.size(); i++) {
					weights[i] = 1.f;
					total_weight++;
				}
			} else {
				float average_weight = total_weight / non_zero_count;
				for(size_t i = 0; i < weights.size(); i++) {
					if(weights[i] == 0.f) {
						weights[i] = average_weight;
						total_weight += average_weight;
					}
				}
			}

			//now we have weights and can use them for transformation of output into ingredients:
			size_t index = 0;

			state.world.for_each_factory_type([&](dcon::factory_type_id t) {
				auto o = state.world.factory_type_get_output(t);
				if(o == c) {
					auto& inputs = state.world.factory_type_get_inputs(t);
					float output_power = state.world.factory_type_get_output_amount(t);

					float weight_current = weights[index] / total_weight;
					index++;

					for(uint32_t i = 0; i < economy::commodity_set::set_size; ++i) {
						if(inputs.commodity_type[i]) {

							buffer_ingredients[inputs.commodity_type[i].index()] += inputs.commodity_amounts[i] * amount / output_power * weight_current;

							float weight_input = buffer_weights[inputs.commodity_type[i].index()];
							total_weight += weight_input;
							weight_current += weight_input;
						} else {
							break;
						}
					}
				}
			});
		}
	});
}

void presimulate(sys::state& state) {
	// set control to something reasonable to kickstart national economy
	state.world.execute_serial_over_province([&](auto pids){
		state.world.province_set_control_ratio(pids, 0.5f);
		state.world.province_set_control_scale(pids, state.world.province_get_demographics(pids, demographics::total) * 0.5f);
	});
	// economic updates without construction
#ifdef NDEBUG
	uint32_t steps = uint32_t(state.defines.alice_economy_presim_days);
#else
	uint32_t steps = 2;
#endif
	for(uint32_t i = 0; i < steps; i++) {
		float presim_completion = float(i) / float(steps);
		float employment_gradient_mult = 1000.0f / std::max(presim_completion * 1000.0f, 1.0f);
		update_employment(state, true, employment_gradient_mult);
		daily_update(state, true, (float)i / (float)steps);
	}
}

bool has_building(sys::state const& state, dcon::state_instance_id si, dcon::factory_type_id fac) {
	auto sdef = state.world.state_instance_get_definition(si);
	auto owner = state.world.state_instance_get_nation_from_state_ownership(si);
	for(auto p : state.world.state_definition_get_abstract_state_membership(sdef)) {
		if(p.get_province().get_nation_from_province_ownership() == owner) {
			for(auto b : p.get_province().get_factory_location()) {
				if(b.get_factory().get_building_type() == fac)
					return true;
			}
		}
	}
	return false;
}

bool is_bankrupt_debtor_to(sys::state& state, dcon::nation_id debt_holder, dcon::nation_id debtor) {
	return state.world.nation_get_is_bankrupt(debt_holder) &&
		state.world.unilateral_relationship_get_owns_debt_of(
				state.world.get_unilateral_relationship_by_unilateral_pair(debtor, debt_holder)) > 0.1f;
}

bool nation_is_constructing_factories(sys::state& state, dcon::nation_id n) {
	auto rng = state.world.nation_get_factory_construction(n);
	return rng.begin() != rng.end();
}

bool factory_is_closed(sys::state const& state, dcon::factory_id f) {
	auto basic_scale = state.world.factory_get_primary_employment(f) + state.world.factory_get_unqualified_employment(f);
	if(basic_scale < factory_closed_threshold) {
		return true;
	}
	return false;
}

bool nation_has_closed_factories(sys::state& state, dcon::nation_id n) { // TODO - should be "good" now
	auto nation_fat = dcon::fatten(state.world, n);
	for(auto prov_owner : nation_fat.get_province_ownership()) {
		auto prov = prov_owner.get_province();
		for(auto factloc : prov.get_factory_location()) {
			if(factory_is_closed(state, factloc.get_factory())) {
				return true;
			}
		}
	}
	return false;
}

// Check if source gives trade rights to target. Includes derived rights from sphere/overlord
dcon::unilateral_relationship_id nation_gives_free_trade_rights(sys::state& state, dcon::nation_id source, dcon::nation_id target) {

	auto market_leader_target = nations::get_market_leader(state, target);
	auto market_leader_source = nations::get_market_leader(state, source);
	dcon::unilateral_relationship_id source_tariffs_rel;
	if(market_leader_target == market_leader_source) {
		source_tariffs_rel = state.world.get_unilateral_relationship_by_unilateral_pair(target, source);
	}
	else {
		source_tariffs_rel = state.world.get_unilateral_relationship_by_unilateral_pair(market_leader_target, market_leader_source);
	}
	if(source_tariffs_rel) {
		auto enddt = state.world.unilateral_relationship_get_no_tariffs_until(source_tariffs_rel);
		// Enddt empty signalises revoken agreement
		// Enddt > cur_date signalises that the agreement can't be broken
		if(enddt) {
			return source_tariffs_rel;
		}
	}
	return dcon::unilateral_relationship_id{};
}
// Check if source gives trade rights to target. Only include direct relationship
dcon::unilateral_relationship_id nation_gives_direct_free_trade_rights(sys::state& state, dcon::nation_id source, dcon::nation_id target) {
	auto source_tariffs_rel = state.world.get_unilateral_relationship_by_unilateral_pair(target, source);
	if(source_tariffs_rel) {
		auto enddt = state.world.unilateral_relationship_get_no_tariffs_until(source_tariffs_rel);
		// Enddt empty signalises revoken agreement
		// Enddt > cur_date signalises that the agreement can't be broken
		if(enddt) {
			return source_tariffs_rel;
		}
	}
	return dcon::unilateral_relationship_id{};
}















void initialize(sys::state& state) {
	state.world.for_each_commodity([&](dcon::commodity_id c) {
		state.world.execute_serial_over_market([&](auto markets) {
			state.world.market_set_price(markets, c, state.world.commodity_get_cost(c));

			state.world.market_set_aggregated_demand_history(markets, c, ve::fp_vector{});
			state.world.market_set_aggregated_supply_history(markets, c, ve::fp_vector{});
			state.world.market_set_demand(markets, c, ve::fp_vector{});
			state.world.market_set_supply(markets, c, ve::fp_vector{});
			state.world.market_set_consumption(markets, c, ve::fp_vector{});
		});

		auto fc = fatten(state.world, c);

		for(uint32_t i = 0; i < price_history_length; ++i) {
			fc.set_price_record(i, fc.get_cost());
		}
		// fc.set_global_market_pool();
	});

	services::reset_demand(state);
	services::reset_supply(state);
	services::reset_price(state);

	/*
	auto savings_buffer = state.world.pop_type_make_vectorizable_float_buffer();
	state.world.for_each_pop_type([&](dcon::pop_type_id t) {
		auto ft = fatten(state.world, t);
		state.world.for_each_commodity([&](dcon::commodity_id c) {
			savings_buffer.get(t) +=
				state.world.commodity_get_is_available_from_start(c)
				?
				state.world.commodity_get_cost(c) * ft.get_life_needs(c)
				+ 0.5f * state.world.commodity_get_cost(c) * ft.get_everyday_needs(c)
				: 0.0f;
		});
		auto strata = (ft.get_strata() * 2) + 1;
		savings_buffer.get(t) *= strata;
	});
	*/


	state.world.for_each_pop([&](dcon::pop_id p) {
		state.world.pop_set_satisfaction(p, 0.4f);
	});

	sanity_check(state);

	state.world.for_each_nation([&](dcon::nation_id n) {
		auto fn = fatten(state.world, n);
		fn.set_administrative_spending(int8_t(35));
		fn.set_military_spending(int8_t(60));
		fn.set_education_spending(int8_t(100));
		fn.set_social_spending(int8_t(100));
		fn.set_land_spending(int8_t(100));
		fn.set_naval_spending(int8_t(100));
		fn.set_construction_spending(int8_t(100));
		fn.set_overseas_spending(int8_t(100));

		fn.set_poor_tax(int8_t(75));
		fn.set_middle_tax(int8_t(75));
		fn.set_rich_tax(int8_t(75));

		fn.set_spending_level(1.0f);
	});

	state.world.for_each_market([&](dcon::market_id n) {
		initialize_compatibility_needs_weights(state, n);
		state.world.for_each_commodity([&](dcon::commodity_id c) {
			state.world.market_set_expected_probability_to_buy(n, c, 0.0f);
			state.world.market_set_actual_probability_to_buy(n, c, 0.0f);
			state.world.market_set_expected_probability_to_sell(n, c, 0.0f);
			state.world.market_set_actual_probability_to_sell(n, c, 0.0f);
		});
	});

	province::ve_for_each_land_province(state, [&](auto ids) {
		for(int32_t i = 0; i < labor::total; i++) {
			state.world.province_set_labor_price(ids, i, 0.0001f);
		}
	});

	update_employment(state, true, 1.f);

	populate_army_consumption(state);
	populate_navy_consumption(state);

	state.world.for_each_nation([&](dcon::nation_id n) {
	});


	// civilian ports
	state.world.for_each_province([&](auto pid) {
		if(state.world.province_get_is_coast(pid)) {
			auto naval_base_level = state.world.province_get_building_level(pid, (uint8_t)(economy::province_building_type::naval_base));
			auto population = state.world.province_get_demographics(pid, demographics::total);
			state.world.province_set_advanced_province_building_max_private_size(
				pid,
				advanced_province_buildings::list::civilian_ports,
				naval_base_level * 25000.f + population * 0.0001f + 100.f
			);
			// This is existing civilian port activity, not merely an expansion
			// ceiling. Starting private_size at zero leaves the port service with
			// no supply, which makes its satisfaction zero and disconnects every
			// sea route before the port has any revenue from which to recover.
			state.world.province_set_advanced_province_building_private_size(
				pid,
				advanced_province_buildings::list::civilian_ports,
				naval_base_level * 25000.f + population * 0.0001f + 100.f
			);
		}
	});

	state.world.for_each_province([&](auto pid) {
		auto starting_urban_population_proxy =
			state.world.province_get_demographics(pid, demographics::literacy) * 0.3f
			+ state.world.province_get_demographics(pid, demographics::to_key(state, state.culture_definitions.artisans)) * 2.f;

		state.world.province_set_advanced_province_building_max_private_size(
			pid,
			advanced_province_buildings::list::local_cities_and_towns,
			starting_urban_population_proxy
		);
		// The initial city proxy is existing housing, not merely theoretical
		// expansion capacity. Leaving private size at zero created a world with
		// cities on the map but no homes in the service market.
		state.world.province_set_advanced_province_building_private_size(
			pid,
			advanced_province_buildings::list::local_cities_and_towns,
			starting_urban_population_proxy
		);

	});
}

float sphere_leader_share_factor(sys::state& state, dcon::nation_id sphere_leader, dcon::nation_id sphere_member) {
	/*
	Share factor : If the nation is a civ and is a secondary power start with define : SECOND_RANK_BASE_SHARE_FACTOR, and
	otherwise start with define : CIV_BASE_SHARE_FACTOR.Also calculate the sphere owner's foreign investment in the nation as a
	fraction of the total foreign investment in the nation (I believe that this is treated as zero if there is no foreign
	investment at all). The share factor is (1 - base share factor) x sphere owner investment fraction + base share factor. For
	uncivs, the share factor is simply equal to define:UNCIV_BASE_SHARE_FACTOR (so 1, by default). If a nation isn't in a sphere,
	we let the share factor be 0 if it needs to be used in any other calculation.
	*/
	if(state.world.nation_get_is_civilized(sphere_member)) {
		float base = state.world.nation_get_rank(sphere_member) <= state.defines.colonial_rank
			? state.defines.second_rank_base_share_factor
			: state.defines.civ_base_share_factor;
		auto const ul = state.world.get_unilateral_relationship_by_unilateral_pair(sphere_member, sphere_leader);
		float sl_investment = state.world.unilateral_relationship_get_foreign_investment(ul);
		float total_investment = nations::get_foreign_investment(state, sphere_member);
		float investment_fraction = total_investment > 0.0001f ? sl_investment / total_investment : 0.0f;
		return base + (1.0f - base) * investment_fraction;
	} else {
		return state.defines.unciv_base_share_factor;
	}
}


void update_factory_triggered_modifiers(sys::state& state) {
	state.world.for_each_factory([&](dcon::factory_id f) {
		auto fac_type = fatten(state.world, state.world.factory_get_building_type(f));
		float sum = 1.0f;
		auto prov = ::compat::alice::province_for_factory(state, f);
		auto pstate = state.world.province_get_state_membership(prov);
		auto powner = state.world.province_get_nation_from_province_ownership(prov);

		if(powner && pstate) {
			for(auto bonus : fac_type.get_factory_bonuses()) {
				if(bonus.trigger && trigger::evaluate(state, bonus.trigger, trigger::to_generic(pstate), trigger::to_generic(powner), 0)) {
					sum -= bonus.amount;
				}
			}
		}

		state.world.factory_set_triggered_modifiers(f, sum);
	});
}

float subsistence_size(sys::state const& state, dcon::province_id p) {
	auto rgo_ownership = state.world.province_get_landowners_share(p)
		+ state.world.province_get_capitalists_share(p)
		+ state.world.province_get_state_land_share(p)
		+ state.world.province_get_foreign_land_share(p);
	return state.world.province_get_rgo_base_size(p) * (1.f - rgo_ownership);
}

float subsistence_capacity_ratio(sys::state const& state) {
	return std::isfinite(state.defines.alice_subsistence_capacity_ratio)
		? std::clamp(state.defines.alice_subsistence_capacity_ratio, 0.f, 1.f)
		: 0.85f;
}

float subsistence_shortage_ratio(float potential_employment,
	float available_employment) {
	if(!std::isfinite(potential_employment)
		|| !std::isfinite(available_employment)
		|| potential_employment <= 0.f) {
		return 0.f;
	}
	auto const available = std::clamp(available_employment, 0.f,
		potential_employment);
	return std::clamp(
		(potential_employment - available) / potential_employment,
		0.f, 1.f);
}

float subsistence_max_pseudoemployment(sys::state& state, dcon::province_id p) {
	return subsistence_size(state, p) * subsistence_capacity_ratio(state);
}

void update_local_subsistence_factor(sys::state& state) {
	province::ve_for_each_land_province(state, [&](auto ids) {
		auto quality = (ve::to_float(state.world.province_get_life_rating(ids)) - 10.f) / 10.f;
		quality = ve::max(quality, 0.f) + 0.01f;
		auto score = (subsistence_factor * quality) + subsistence_score_life * 0.9f;
		state.world.province_set_subsistence_score(ids, score);
	});
}


int32_t factory_priority(sys::state const& state, dcon::factory_id f) {
	return (state.world.factory_get_priority_low(f) ? 1 : 0) + (state.world.factory_get_priority_high(f) ? 2 : 0);
}
void set_factory_priority(sys::state& state, dcon::factory_id f, int32_t priority) {
	state.world.factory_set_priority_high(f, priority >= 2);
	state.world.factory_set_priority_low(f, (priority & 1) != 0);
}
bool factory_is_profitable(sys::state const& state, dcon::factory_id f) {
	return state.world.factory_get_unprofitable(f) == false;
}

struct commodity_profit_holder {
	float profit = 0.0f;
	dcon::commodity_id c;
};

float factory_unqualified_employment(sys::state const& state, dcon::factory_id f) {
	return state.world.factory_get_unqualified_employment(f);
}
float factory_primary_employment(sys::state const& state, dcon::factory_id f) {
	return state.world.factory_get_primary_employment(f);
}
float factory_secondary_employment(sys::state const& state, dcon::factory_id f) {
	return state.world.factory_get_secondary_employment(f);
}

float factory_total_employment(sys::state const& state, dcon::factory_id f) {
	auto pid = ::compat::alice::province_for_factory(state, f);
	return (
		state.world.factory_get_unqualified_employment(f)
		* state.world.province_get_labor_demand_satisfaction(pid, labor::no_education)
		+
		state.world.factory_get_primary_employment(f)
		* state.world.province_get_labor_demand_satisfaction(pid, labor::basic_education)
		+
		state.world.factory_get_secondary_employment(f)
		* state.world.province_get_labor_demand_satisfaction(pid, labor::high_education)
	);
}





void update_pops_employment(sys::state& state) {
	assert(state.exact_population && "exact employment projection requires canonical population");
	if(!state.exact_population) std::abort();
	update_employment(state, false, 0.0f);
	using person_key = persons::person_key;
	std::unordered_set<person_key, persons::person_key_hash> employed;
	state.world.for_each_factory([&](dcon::factory_id factory) {
		for(auto id : exact_person_economy::active_contracts_for_factory(state, factory)) {
		auto record = exact_person_economy::contract(state, id);
			if(record && persons::alive(state, record->worker)) employed.insert(record->worker);
		}
	});
	state.world.for_each_nation([&](dcon::nation_id nation) {
		for(auto institution : governance::institutions_of(state, nation)) {
			for(auto id : exact_person_economy::active_contracts_for_institution(state, institution)) {
				auto record = exact_person_economy::contract(state, id);
				if(record && persons::alive(state, record->worker)) employed.insert(record->worker);
			}
		}
	});
	std::unordered_map<uint32_t, uint64_t> employed_by_cell;
	for(auto worker : employed) {
		auto cell = persons::current_population_cell(state, worker);
		if(cell != 0) ++employed_by_cell[cell];
	}
	state.world.for_each_pop([&](dcon::pop_id pop) {
		auto cell = persons::source_population_cell_for_population(state, pop);
		assert(cell != 0 && "employment projection requires every DCON POP to have canonical identity");
		if(cell == 0) std::abort();
		auto total = persons::living_people_in_population_cell(state, cell);
		auto count = employed_by_cell[cell];
		auto fraction = total > 0 ? std::clamp(float(count) / float(total), 0.0f, 1.0f) : 0.0f;
		pop_demographics::set_raw_employment(state, pop, fraction);
	});
}

float convex_function(float x) {
	return 1.f - (1.f - x) * (1.f - x);
}

void populate_army_consumption(sys::state& state) {
	uint32_t total_commodities = state.world.commodity_size();
	for(uint32_t i = 1; i < total_commodities; ++i) {
		dcon::commodity_id cid{ dcon::commodity_id::value_base_t(i) };
		state.world.execute_serial_over_market([&](auto ids) {
			state.world.market_set_army_demand(ids, cid, 0.0f);
		});
	}

	state.world.for_each_regiment([&](dcon::regiment_id r) {
		auto reg = fatten(state.world, r);
		auto type = state.world.regiment_get_type(r);
		auto owner = reg.get_army_from_army_membership().get_controller_from_army_control();
		auto pop = reg.get_pop_from_regiment_source();
		auto location = pop.get_pop_location().get_province().get_state_membership();
		// if the regiment has no pop attached (may happen temporarily until it gets deleted) don't do army demand
		if(!location) {
			return;
		}
		auto market = location.get_market_from_local_market();
		auto strength = reg.get_strength();

		if(owner && type) {
			auto o_sc_mod = std::max(
				0.01f,
				state.world.nation_get_modifier_values(owner, sys::national_mod_offsets::supply_consumption)
				+ 1.0f
			);
			auto& supply_cost = state.military_definitions.unit_base_definitions[type].supply_cost;
			for(uint32_t i = 0; i < commodity_set::set_size; ++i) {
				if(supply_cost.commodity_type[i]) {
					// Day-to-day consumption
					// Strength under 100% reduces unit supply consumption
					auto& curr_demand = state.world.market_get_army_demand(market, supply_cost.commodity_type[i]);
					state.world.market_set_army_demand(market, supply_cost.commodity_type[i], curr_demand + supply_cost.commodity_amounts[i]
						* state.world.nation_get_unit_stats(owner, type).supply_consumption
						* o_sc_mod * strength);

				} else {
					break;
				}
			}
			auto& build_cost = state.military_definitions.unit_base_definitions[type].build_cost;
			auto reinforcement = military::unit_calculate_reinforcement<military::reinforcement_estimation_type::full_supplies>(state, reg);

			for(uint32_t i = 0; i < commodity_set::set_size; ++i) {
				if(build_cost.commodity_type[i]) {
					if(reinforcement > 0) {
						// Regiment needs reinforcement - add extra consumption. Every 1% of reinforcement demands 1% of unit cost. Divide to spread the demand out over the month
						auto& curr_demand = state.world.market_get_army_demand(market, build_cost.commodity_type[i]);
						state.world.market_set_army_demand(market, build_cost.commodity_type[i], curr_demand + (build_cost.commodity_amounts[i] * reinforcement) / unit_reinforcement_demand_divisor);
					}
				} else {
					break;
				}
			}
		}
	});
}

void populate_navy_consumption(sys::state& state) {
	uint32_t total_commodities = state.world.commodity_size();
	for(uint32_t i = 1; i < total_commodities; ++i) {
		dcon::commodity_id cid{ dcon::commodity_id::value_base_t(i) };
		state.world.execute_serial_over_market([&](auto ids) {
			state.world.market_set_navy_demand(ids, cid, 0.0f);
		});
	}

	state.world.for_each_ship([&](dcon::ship_id r) {
		auto shp = fatten(state.world, r);
		auto type = state.world.ship_get_type(r);
		auto owner = shp.get_navy_from_navy_membership().get_controller_from_navy_control();
		auto market = owner.get_capital().get_state_membership().get_market_from_local_market();

		if(owner && type) {
			auto o_sc_mod = std::max(
				0.01f,
				state.world.nation_get_modifier_values(owner, sys::national_mod_offsets::supply_consumption)
				+ 1.0f
			);

			auto& supply_cost = state.military_definitions.unit_base_definitions[type].supply_cost;
			for(uint32_t i = 0; i < commodity_set::set_size; ++i) {
				if(supply_cost.commodity_type[i]) {
					auto& curr_demand = state.world.market_get_navy_demand(market, supply_cost.commodity_type[i]);
					state.world.market_set_navy_demand(market, supply_cost.commodity_type[i], curr_demand + supply_cost.commodity_amounts[i]
						* state.world.nation_get_unit_stats(owner, type).supply_consumption
						* o_sc_mod);

				} else {
					break;
				}
			}

			auto& build_cost = state.military_definitions.unit_base_definitions[type].build_cost;

			for(uint32_t i = 0; i < commodity_set::set_size; ++i) {
				if(build_cost.commodity_type[i]) {
					auto reinforcement = military::unit_calculate_reinforcement<military::reinforcement_estimation_type::full_supplies>(state, shp);
					if(reinforcement > 0) {
						// Ship needs repair - add extra consumption. Every 1% of reinforcement demands 1% of unit cost
						// add only a fraction of the build cost per day, to spread it out over the month
						auto& curr_demand = state.world.market_get_navy_demand(market, build_cost.commodity_type[i]);
						state.world.market_set_navy_demand(market, build_cost.commodity_type[i], curr_demand + (build_cost.commodity_amounts[i] * reinforcement) / unit_reinforcement_demand_divisor);
					}
				} else {
					break;
				}
			}
		}
	});
}


void daily_update(sys::state& state, bool presimulation, float presimulation_stage) {
	assert(state.exact_population && state.exact_person_economy && state.exact_person_goods
		&& state.exact_person_freight && state.labor_dynamics && state.causal_order
		&& "canonical population and economic runtime must be initialized before a simulation day");
	if(!state.exact_population || !state.exact_person_economy || !state.exact_person_goods
		|| !state.exact_person_freight || !state.labor_dynamics || !state.causal_order)
		std::abort();
	sanity_check(state);



	/* initialization parallel block */

	concurrency::parallel_for(0, 8, [&](int32_t index) {
		switch(index) {
		case 0:
			populate_navy_consumption(state);
			break;
		case 2:
			update_factory_triggered_modifiers(state);
			break;
		case 6:
			state.world.execute_serial_over_nation([&](auto id) {
				state.world.nation_set_subsidy_token_total(id, 0.f);
			});
			break;
		case 7:
			break;
		}
	});


	auto market_leader = ve::vectorizable_buffer<dcon::nation_id, dcon::nation_id>(state.world.nation_size());


	// This must run serial
	populate_army_consumption(state);

	state.world.for_each_nation([&](auto ids) {
		market_leader.set(ids, nations::get_market_leader(state, ids));
	});

	if(state.trade_route_cached_values_out_of_date) {
		state.trade_route_cached_values_out_of_date = false;

		ankerl::unordered_dense::map<int32_t, bool> direct_block;
		ankerl::unordered_dense::map<int32_t, bool> trade_closed;
		ankerl::unordered_dense::map<int32_t, bool> direct_no_tariffs;
		ankerl::unordered_dense::map<int32_t, bool> no_tariffs;

		// US3AC9. Wartime embargoes

		state.world.for_each_war([&](auto war) {
			state.world.war_for_each_war_participant(war, [&](auto attacker_candidate) {
				if(!state.world.war_participant_get_is_attacker(attacker_candidate)) return;
				auto attacker = state.world.war_participant_get_nation(attacker_candidate);

				state.world.war_for_each_war_participant(war, [&](auto defender_candidate) {
					if(state.world.war_participant_get_is_attacker(defender_candidate)) return;
					auto defender = state.world.war_participant_get_nation(defender_candidate);

					auto index_pair = attacker.index() * state.world.nation_size() + defender.index();
					auto index_pair_T = defender.index() * state.world.nation_size() + attacker.index();
					direct_block[index_pair] = true;
					direct_block[index_pair_T] = true;
				});
			});
		});

		// US3AC10. diplomatic embargos
		state.world.for_each_unilateral_relationship([&](auto rel) {
			if(state.world.unilateral_relationship_get_embargo(rel)) {
				dcon::nation_id source = state.world.unilateral_relationship_get_source(rel);
				dcon::nation_id target = state.world.unilateral_relationship_get_target(rel);

				auto index_pair = source.index() * state.world.nation_size() + target.index();
				auto index_pair_T = target.index() * state.world.nation_size() + source.index();

				direct_block[index_pair] = true;
				direct_block[index_pair_T] = true;
			}
		});

		// US3AC11. US3AC12. sphere joins market leader
		state.world.for_each_nation([&](auto A) {
			dcon::nation_id market_leader_A = market_leader.get(A);
			state.world.for_each_nation([&](auto B) {
				dcon::nation_id market_leader_B = market_leader.get(B);
				int32_t base_pair = A.index() * state.world.nation_size() + B.index();

				int32_t indices[3]{
					market_leader_A.index() * int32_t(state.world.nation_size()) + market_leader_B.index(),
					market_leader_A.index() * int32_t(state.world.nation_size()) + B.index(),
					A.index() * int32_t(state.world.nation_size()) + market_leader_B.index(),
				};

				if(market_leader_A && market_leader_B) {
					if(direct_block.find(indices[0]) != direct_block.end()) {
						trade_closed[base_pair] = true;
					}
				}
				if(market_leader_A) {
					if(direct_block.find(indices[1]) != direct_block.end()) {
						trade_closed[base_pair] = true;
					}
				}
				if(market_leader_B) {
					if(direct_block.find(indices[2]) != direct_block.end()) {
						trade_closed[base_pair] = true;
					}
				}

				{
					if(direct_block.find(base_pair) != direct_block.end()) {
						trade_closed[base_pair] = true;
					}
				}
			});
		});

		// US3AC15. Equal/unequal trade treaties
		state.world.for_each_unilateral_relationship([&](auto rel) {
			if(state.world.unilateral_relationship_get_no_tariffs_until(rel)) {
				dcon::nation_id n1 = state.world.unilateral_relationship_get_source(rel);
				dcon::nation_id n2 = state.world.unilateral_relationship_get_target(rel);

				auto index_pair = n1.index() * state.world.nation_size() + n2.index();
				direct_no_tariffs[index_pair] = true;
			}
		});
		// Reflexivity of free trade
		state.world.for_each_nation([&](auto nid) {
			auto index_pair = nid.index() * state.world.nation_size() + nid.index();
			direct_no_tariffs[index_pair] = true;
		});
		state.world.for_each_nation([&](auto nid) {
			dcon::nation_id sphere = state.world.nation_get_in_sphere_of(nid);
			if(sphere) {
				auto index_pair = nid.index() * state.world.nation_size() + sphere.index();
				direct_no_tariffs[index_pair] = true;
			}
		});
		state.world.for_each_overlord([&](auto ovid) {
			dcon::nation_id subject = state.world.overlord_get_subject(ovid);
			dcon::nation_id overlord = state.world.overlord_get_ruler(ovid);
			auto index_pair = subject.index() * state.world.nation_size() + overlord.index();
			direct_no_tariffs[index_pair] = true;
		});

		state.world.for_each_nation([&](auto A) {
			dcon::nation_id market_leader_A = market_leader.get(A);
			state.world.for_each_nation([&](auto B) {
				dcon::nation_id market_leader_B = market_leader.get(B);

				int32_t base_pair = A.index() * state.world.nation_size() + B.index();

				int32_t indices[3]{
					market_leader_A.index() * int32_t(state.world.nation_size()) + market_leader_B.index(),
					market_leader_A.index() * int32_t(state.world.nation_size()) + B.index(),
					A.index() * int32_t(state.world.nation_size()) + market_leader_B.index(),
				};

				if(market_leader_A && market_leader_B) {
					if(direct_no_tariffs.find(indices[0]) != direct_no_tariffs.end()) {
						no_tariffs[base_pair] = true;
					}
				}
				if(market_leader_A) {
					if(direct_no_tariffs.find(indices[1]) != direct_no_tariffs.end()) {
						no_tariffs[base_pair] = true;
					}
				}
				if(market_leader_B) {
					if(direct_no_tariffs.find(indices[2]) != direct_no_tariffs.end()) {
						no_tariffs[base_pair] = true;
					}
				}
				{
					if(direct_no_tariffs.find(base_pair) != direct_no_tariffs.end()) {
						no_tariffs[base_pair] = true;
					}
				}
			});
		});

		// update cache:

		state.world.for_each_trade_route([&](auto route) {
			auto A = state.world.trade_route_get_connected_markets(route, 0);
			auto B = state.world.trade_route_get_connected_markets(route, 1);
			auto s_A = state.world.market_get_zone_from_local_market(A);
			auto s_B = state.world.market_get_zone_from_local_market(B);

			auto capital_A = state.world.state_instance_get_capital(s_A);
			auto capital_B = state.world.state_instance_get_capital(s_B);

			auto controller_A = state.world.province_get_nation_from_province_control(capital_A);
			auto controller_B = state.world.province_get_nation_from_province_control(capital_B);

			auto index = controller_A.index() * state.world.nation_size() + controller_B.index();
			auto index_T = controller_B.index() * state.world.nation_size() + controller_A.index();

			if(trade_closed.find(index) != trade_closed.end()) {
				state.world.trade_route_set_is_trade_forbidden(route, true);
			} else {
				state.world.trade_route_set_is_trade_forbidden(route, false);
			}

			if(no_tariffs.find(index) != no_tariffs.end()) {
				state.world.trade_route_set_is_tariff_applied_0(route, false);
			} else {
				state.world.trade_route_set_is_tariff_applied_0(route, true);
			}

			if(no_tariffs.find(index_T) != no_tariffs.end()) {
				state.world.trade_route_set_is_tariff_applied_1(route, false);
			} else {
				state.world.trade_route_set_is_tariff_applied_1(route, true);
			}
		});
	};


	sanity_check(state);

	/* end initialization parallel block */

	uint32_t total_commodities = state.world.commodity_size();

	/*
		update scoring for provinces
	*/

	update_local_subsistence_factor(state);
	state.world.execute_serial_over_market([&](auto markets) {
		state.world.market_set_gdp(markets, 0.0f);
	});

	services::reset_demand(state);

	// Factories order inputs against their canonical firm accounts and inventories.
	update_production_consumption(state);
	// Production sees only inventory that has actually arrived at the factory.
		economy::physical::factory_inputs::fulfill(state);


	state.world.for_each_commodity([&](auto cid) {
		bool is_potential_rgo = state.world.commodity_get_rgo_amount(cid) > 0.f;
		bool already_known_to_exist = state.world.commodity_get_actually_exists_in_nature(cid);
		if(is_potential_rgo && !already_known_to_exist) {
			for(auto pid : state.world.in_province) {
				auto potential = state.world.province_get_rgo_size(pid, cid);
				if(potential > 0) {
					state.world.commodity_set_actually_exists_in_nature(cid, true);
					break;
				}
			}
		}
	});

	sanity_check(state);

	// national income is handled at the end,
	// so we have money stockpile from previous day
	// and can safely use it to calculate spendings and generate demand

	// national spendings ideally should follow the priority:
	// 1) loans interest (otherwise interest gets diluted and can be abused with high admin spending)
	// 2) international treaties (currently it is handled somewhere else, requires investigation)
	// 3) admin spending (can't pay for stuff without taxes)
	// 4) everything else

	// if nation is unable to pay interest it is considered bankrupt
	// money stockpiles after interest are considered BASEBUDGET
	// admin spendings take a ratio of BASEBUDGET to pay for bureaucracy
	// denote them as ADMIN
	// the rest of spending is divided into two groups:
	// 1) LEGACY: these expenses depend on some predefined value
	// 2) RELATIVE: these spendings are equal to ratio of BASEBUDGET
	// if LEGACY and RELATIVE - ADMIN expenses are larger than BASEBUDGET - ADMIN
	// then scale them down

	// AI is considered bankrupt when money stockpiles are lower than 0,
	// which should happen basically never

	// we have to recalculate loan related variables every new round, because they depend on themselves

	for(auto nation : state.world.in_nation) {
		auto budget = governance::public_administration::daily_budget(state, nation.id);
		state.world.nation_set_last_base_budget(nation.id, budget);
		state.world.nation_set_spending_level(nation.id, 0.0f);
		state.world.nation_set_subsidy_token_price(nation.id, 0.0f);
	}

	{
		for(auto nation : state.world.in_nation) {
			governance::public_administration::appropriate_daily_budget(state, nation.id);
			governance::public_administration::plan_public_staffing(state, nation.id);
		}
		governance::public_administration::synchronize_local_governments(state);
	}


	sanity_check(state);


	// Aggregate province building growth has no canonical site and is not simulated.


	sanity_check(state);



	sanity_check(state);

	// finally we can move to production:

	// ##########
	// # SUPPLY #
	// ##########

	services::reset_supply(state);



	update_pops_employment(state);


	sanity_check(state);
	{
		::economy::firm_agency::update_decisions(state);
		::economy::industrial_dynamics::process(state);
	}

	// produce goods and services

	// Aggregate artisan output has no canonical producer inventory and cannot
	// enter the market as synthetic supply.


	// Legacy province building production cannot inject aggregate goods.


	{
		::economy::physical::freight_market::process_pending_requests(state);
		::economy::physical::shipments::process_arrivals(state);
		governance::public_administration::deliver_public_services(state);
		::economy::capital_projects::process_projects(state);
	}

	update_factories_production(state);
	::economy::physical::land::settle_rents(state);
	::economy::firm_agency::post_output_asks(state);
	::economy::physical::labor_dynamics::process_factory_labor_dynamics(state);
	::economy::physical::job_market::process(state);
	{
		governance::public_administration::settle_public_payroll(state);
		::economy::physical::household_mobility::update_employed_households(state);
		::economy::physical::exact_person_goods::process_daily(state);
#ifndef NDEBUG
		assert(::economy::physical::exact_person_goods::validate_canonical_household_economy(state)
			&& "canonical household invariants failed after daily consumer settlement");
#endif
	}

	{
		// Intermediate inputs are charged before this day's output is registered.
		// A tiny market that consumes stored inputs but produces nothing can
		// therefore end with negative local value added. GDP is a gross production
		// level, so close that accounting edge at zero after every production path
		// has contributed.
		state.world.execute_serial_over_market([&](auto markets) {
			state.world.market_set_gdp(markets,
				ve::max(0.0f, state.world.market_get_gdp(markets)));
		});
	}

	// Public administration runs on institution contracts and public service delivery.


	/*
	Start with updating rgo/factory "banks"
	*/

	// Canonical firm agency owns realized profit and cashflow. The old aggregate
	// province wage/profit calculation is no longer a second firm ledger.

	// Service what is already owed before advancing anything new, so a firm
	// cannot borrow its way out of interest it has not paid.
	// A till that has just gone negative is a firm financing itself. Route that
	// through the bank so the credit is finite, priced and money-conserving,
	// instead of an unbounded free overdraft.




	collect_taxes(state);

	auto collected_tariff_buffer = state.world.nation_make_vectorizable_float_buffer();
	for(auto mid : state.world.in_market) {
		auto controller = mid.get_zone_from_local_market().get_capital().get_nation_from_province_control();
		// skip if rebel controlled
		if(!controller) {
			continue;
		}
		auto old_value = collected_tariff_buffer.get(controller);
		auto collected = mid.get_tariff_collected();
		collected_tariff_buffer.set(controller, old_value + collected);
		mid.set_tariff_collected(0.f);
	};
	// Tariff proceeds require an explicit concrete fiscal account; do not credit
	// the legacy nation stockpile.


	sanity_check(state);

	// Freeze the exact-person quantity basket before projecting closing quotes.
	price_level::begin_day(state);

	// DCON receives a one-way view of canonical orders, fills, inventories,
	// quotes, and exact-contract wages. It no longer discovers a second market price.
	::economy::physical::concrete_market::project_to_legacy_markets(state);
	::economy::physical::job_market::project_labor_price_view(state);

	services::update_price(state);


	// Inflation is the change in the exact-person consumption basket caused by the
	// price update above. It is observed here, not imposed on prices or balances.
	price_level::update(state);


	// update median prices

	concurrency::parallel_for(uint32_t(1), total_commodities, [&](uint32_t k) {
		dcon::commodity_id cid{ dcon::commodity_id::value_base_t(k) };
		state.world.commodity_set_median_price(cid, median_price(state, cid));
	});


	sanity_check(state);

	if(state.cheat_data.ecodump) {
		state.world.for_each_commodity([&](dcon::commodity_id c) {
			float states_count = 0.f;
			float total_price = 0.f;
			float total_production = 0.f;
			float total_demand = 0.f;

			state.world.for_each_market([&](auto id) {
				states_count++;
				total_price += state.world.market_get_price(id, c);
				total_production += state.world.market_get_supply(id, c);
				total_demand += state.world.market_get_demand(id, c);
			});

			state.cheat_data.prices_dump_buffer += std::to_string(total_price / states_count) + ",";
			state.cheat_data.supply_dump_buffer += std::to_string(total_production) + ",";
			state.cheat_data.demand_dump_buffer += std::to_string(total_demand) + ",";
		});

		state.cheat_data.prices_dump_buffer += "\n";
		state.cheat_data.supply_dump_buffer += "\n";
		state.cheat_data.demand_dump_buffer += "\n";
	}

	/*
	DIPLOMATIC EXPENSES
	*/

	for(auto n : state.world.in_nation) {
		// Subject money transfers
		auto rel = state.world.nation_get_overlord_as_subject(n);
		auto overlord = state.world.overlord_get_ruler(rel);

		(void)overlord;

		for(auto uni : n.get_unilateral_relationship_as_source()) {
			if(uni.get_war_subsidies()) {
				auto sub_size = estimate_war_subsidies(state, uni.get_target(), uni.get_source());

				(void)sub_size; // war subsidies need a concrete account transfer
			}
			if(uni.get_reparations() && state.current_date < n.get_reparations_until()) {
				auto const tax_eff = nations::tribute_efficiency(state, n);
				auto total_tax_base = n.get_total_rich_income() + n.get_total_middle_income() + n.get_total_poor_income();

				auto payout = total_tax_base * tax_eff * state.defines.reparations_tax_hit;
				(void)payout;
			}
		}
	}

	sanity_check(state);

	// make constructions:
	::economy::physical::shipments::project_route_volumes_to_legacy_view(state);
	if(!::economy::exact_person_economy::project_population_cash_balances(state)) {
		assert(false && "canonical household cash must project into the DCON read model");
		std::abort();
	}

	if(!presimulation) {
	}

	sanity_check(state);

	// ####################
	// # STATS COLLECTION #
	// ####################

	//write gdp and total savings to file
	if(state.cheat_data.ecodump) {
		float total_savings_pops[20] = { };

		for(int i = 0; i < 20; i++) {
			total_savings_pops[i] = 0.f;
		}
		state.world.for_each_pop([&](auto pop) {
			total_savings_pops[state.world.pop_get_poptype(pop).id.index()] += state.world.pop_get_savings(pop);
		});

		float total_savings_markets = 0.f;
		state.world.for_each_market([&](auto market) {
			total_savings_markets += state.world.market_get_stockpile(market, economy::money);
		});

		float total_savings_nations = 0.f;
		float total_investment_pool = 0.f;
		state.world.for_each_nation([&](auto nation) {
		(void)nation;
			(void)nation;
		});

		if(state.cheat_data.savings_buffer.size() == 0) {
			state.world.for_each_pop_type([&](auto pop_type) {
				state.cheat_data.savings_buffer += std::to_string(total_savings_pops[pop_type.index()]);
				state.cheat_data.savings_buffer += ";";
			});
			state.cheat_data.savings_buffer += std::to_string(total_savings_markets);
			state.cheat_data.savings_buffer += ";";
			state.cheat_data.savings_buffer += std::to_string(total_savings_nations);
			state.cheat_data.savings_buffer += ";";
			state.cheat_data.savings_buffer += std::to_string(total_investment_pool);
			state.cheat_data.savings_buffer += "\n";
		}
	}


	sanity_check(state);
	// Interbank payment instructions are settled together after the day's
	// banking commands, using stable transaction IDs and reserve netting.
	(void)banking::clear_interbank_payments(state, state.current_date);
	banking::update_bank_statuses(state, state.current_date);
#ifndef NDEBUG
	{
		std::vector<std::string> banking_errors;
		assert(banking::validate_canonical_banking_state(state, banking_errors)
			&& "canonical banking invariants failed after the daily settlement phase");
	}
#endif
}

void regenerate_unsaved_values(sys::state& state) {
	// Once the old runaway reached float overflow, invalid prices and money
	// values were serialized too.  Replace only invalid values; legitimate large
	// finite balances in established campaigns are intentionally preserved.
	state.world.for_each_market([&](dcon::market_id market) {
		state.world.for_each_commodity([&](dcon::commodity_id commodity) {
			auto value = state.world.market_get_price(market, commodity);
			if(!std::isfinite(value) || value <= 0.f) {
				value = std::max(price_properties::commodity::min,
					float(state.world.commodity_get_cost(commodity)));
			}
			state.world.market_set_price(market, commodity,
				std::min(value, price_properties::commodity::maximum(float(state.world.commodity_get_cost(commodity)))));
		});
	});
	state.world.for_each_province([&](dcon::province_id province) {
		for(int32_t labor_type = 0; labor_type < labor::total; ++labor_type) {
			auto value = state.world.province_get_labor_price(province, labor_type);
			if(!std::isfinite(value) || value <= 0.f)
				value = price_properties::labor::min;
			state.world.province_set_labor_price(province, labor_type,
				std::min(value, price_properties::labor::max));
		}
		for(int32_t service = 0; service < services::list::total; ++service) {
			auto value = state.world.province_get_service_price(province, service);
			if(!std::isfinite(value) || value <= 0.f)
				value = price_properties::service::min;
			state.world.province_set_service_price(province, service,
				std::min(value, price_properties::service::max));
		}
	});
	price_level::initialize(state);

	state.culture_definitions.rgo_workers.clear();
	for(auto pt : state.world.in_pop_type) {
		if(pt.get_is_paid_rgo_worker())
			state.culture_definitions.rgo_workers.push_back(pt);
	}

	auto const total_commodities = state.world.commodity_size();
	for(uint32_t k = 1; k < total_commodities; ++k) {
		dcon::commodity_id cid{ dcon::commodity_id::value_base_t(k) };
		for(auto pt : state.world.in_pop_type) {
			if(pt != state.culture_definitions.slaves) {
				if(pt.get_life_needs(cid) > 0.0f)
					state.world.commodity_set_is_life_need(cid, true);
				if(pt.get_everyday_needs(cid) > 0.0f)
					state.world.commodity_set_is_everyday_need(cid, true);
				if(pt.get_luxury_needs(cid) > 0.0f)
					state.world.commodity_set_is_luxury_need(cid, true);
			}
		}
	}

	state.world.market_resize_intermediate_demand(state.world.commodity_size());

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

}

float government_consumption(sys::state& state, dcon::nation_id n, dcon::commodity_id c) {
	auto overseas_factor =
		state.defines.province_overseas_penalty *
		float(
			state.world.nation_get_owned_province_count(n)
			- state.world.nation_get_central_province_count(n)
		);
	auto o_adjust = 0.0f;
	if(overseas_factor > 0) {
		if(
			state.world.commodity_get_overseas_penalty(c)
			&& (
				state.world.commodity_get_is_available_from_start(c)
				|| state.world.nation_get_unlocked_commodities(n, c)
				)
		) {
			o_adjust = overseas_factor;
		}
	}

	auto total = 0.f;

	state.world.nation_for_each_state_ownership_as_nation(n, [&](auto soid) {
		auto market =
			state.world.state_instance_get_market_from_local_market(
				state.world.state_ownership_get_state(soid)
			);
		total = total + state.world.market_get_army_demand(market, c);
		total = total + state.world.market_get_navy_demand(market, c);

	});

	return total + o_adjust;
}

float nation_pop_consumption(sys::state& state, dcon::nation_id n, dcon::commodity_id c) {
 float amount = 0.0f;
 state.world.nation_for_each_province_ownership(n, [&](auto ownership) {
  amount += estimate_pops_consumption(state, c, state.world.province_ownership_get_province(ownership));
 });
 return amount;
}

float nation_total_imports(sys::state& state, dcon::nation_id n) {
	float t_total = 0.0f;

	auto const total_commodities = state.world.commodity_size();

	for(uint32_t k = 1; k < total_commodities; ++k) {
		dcon::commodity_id cid{ dcon::commodity_id::value_base_t(k) };
		state.world.nation_for_each_state_ownership(n, [&](auto soid) {
			auto local_state = state.world.state_ownership_get_state(soid);
			auto market = state.world.state_instance_get_market_from_local_market(local_state);
			t_total += price(state, market, cid) * state.world.market_get_import(market, cid);
		});
	}

	return t_total;
}

float nation_total_exports(sys::state& state, dcon::nation_id n) {
	float t_total = 0.0f;

	auto const total_commodities = state.world.commodity_size();

	for(uint32_t k = 1; k < total_commodities; ++k) {
		dcon::commodity_id cid{ dcon::commodity_id::value_base_t(k) };
		state.world.nation_for_each_state_ownership(n, [&](auto soid) {
			auto local_state = state.world.state_ownership_get_state(soid);
			auto market = state.world.state_instance_get_market_from_local_market(local_state);
			t_total += price(state, market, cid) * state.world.market_get_export(market, cid);
		});
	}

	return t_total;
}

float estimate_gold_income(sys::state& state, dcon::nation_id n) {
	auto amount = 0.f;
	for(auto poid : state.world.nation_get_province_ownership_as_nation(n)) {
		auto prov = poid.get_province();
		state.world.for_each_commodity([&](dcon::commodity_id c) {
			if(state.world.commodity_get_money_rgo(c)) {
				amount +=
					state.world.province_get_rgo_output(prov, c)
					* state.world.commodity_get_cost(c);
			}
		});
	}
	return amount * state.defines.gold_to_cash_rate;
}

float estimate_tariff_import_income(sys::state& state, dcon::nation_id n) {
	float result = 0.f;
	// The tariff breakdown is displayed every frame. Build the network
	// shipment allocation once for the whole estimate; rebuilding it inside
	// explain_trade_route_commodity for every route/commodity made the UI
	// allocate and clear the entire trade network thousands of times per second.
	auto const shipment_allocation = world_trade::clear_trade_shipments(state);
	state.world.for_each_commodity([&](dcon::commodity_id cid) {
		state.world.for_each_trade_route([&](auto route) {
			if(!economy::is_trade_route_relevant(state, route, n)) return;
			trade_and_tariff route_data = explain_trade_route_commodity(state, route, cid, shipment_allocation);
			if(route_data.target_nation == n) {
				result += route_data.tariff_target;
			}
		});
	});
	return result;
}

float estimate_tariff_export_income(sys::state& state, dcon::nation_id n) {
	float result = 0.f;
	auto const shipment_allocation = world_trade::clear_trade_shipments(state);
	state.world.for_each_commodity([&](dcon::commodity_id cid) {
		state.world.for_each_trade_route([&](auto route) {
			if(!economy::is_trade_route_relevant(state, route, n)) return;
			trade_and_tariff route_data = explain_trade_route_commodity(state, route, cid, shipment_allocation);
			if(route_data.origin_nation == n) {
				result += route_data.tariff_origin;
			}
		});
	});
	return result;
}

float estimate_war_subsidies_income(sys::state& state, dcon::nation_id n) {
	float total = 0.0f;

	for(auto uni : state.world.nation_get_unilateral_relationship_as_target(n)) {
		if(uni.get_war_subsidies()) {
			total += estimate_war_subsidies(state, uni.get_target(), uni.get_source());
		}
	}
	return total;
}
float estimate_reparations_income(sys::state& state, dcon::nation_id n) {
	float total = 0.0f;
	for(auto uni : state.world.nation_get_unilateral_relationship_as_target(n)) {
		if(uni.get_reparations() && state.current_date < uni.get_source().get_reparations_until()) {
			auto source = uni.get_source();
			auto const tax_eff = nations::tribute_efficiency(state, n);
			auto total_tax_base = state.world.nation_get_total_rich_income(source) +
				state.world.nation_get_total_middle_income(source) +
				state.world.nation_get_total_poor_income(source);
			auto payout = total_tax_base * tax_eff * state.defines.reparations_tax_hit;
			total += payout;
		}
	}
	return total;
}

float estimate_war_subsidies_spending(sys::state& state, dcon::nation_id n) {
	float total = 0.0f;

	for(auto uni : state.world.nation_get_unilateral_relationship_as_source(n)) {
		if(uni.get_war_subsidies()) {
			total += estimate_war_subsidies(state, uni.get_target(), uni.get_source());
		}
	}

	return total;
}

float estimate_reparations_spending(sys::state& state, dcon::nation_id n) {
	float total = 0.0f;
	if(state.current_date < state.world.nation_get_reparations_until(n)) {
		for(auto uni : state.world.nation_get_unilateral_relationship_as_source(n)) {
			if(uni.get_reparations()) {
				auto const tax_eff = nations::tribute_efficiency(state, n);
				auto total_tax_base = state.world.nation_get_total_rich_income(n) +
					state.world.nation_get_total_middle_income(n) +
					state.world.nation_get_total_poor_income(n);
				auto payout = total_tax_base * tax_eff * state.defines.reparations_tax_hit;
				total += payout;
			}
		}
	}
	return total;
}

float estimate_diplomatic_balance(sys::state& state, dcon::nation_id n) {
	float w_sub = estimate_war_subsidies_income(state, n) - estimate_war_subsidies_spending(state, n);
	float w_reps = estimate_reparations_income(state, n) - estimate_reparations_spending(state, n);
	float subject_payments = estimate_subject_payments_received(state, n) - estimate_subject_payments_paid(state, n);
	return w_sub + w_reps + subject_payments;
}
float estimate_diplomatic_income(sys::state& state, dcon::nation_id n) {
	// potential OOS in subject_payments (if parallelizing over nations while changing tax levels)
	float w_sub = estimate_war_subsidies_income(state, n);
	float w_reps = estimate_reparations_income(state, n);
	float subject_payments = estimate_subject_payments_received(state, n);
	return w_sub + w_reps + subject_payments;
}
float estimate_diplomatic_expenses(sys::state& state, dcon::nation_id n) {
	float w_sub = estimate_war_subsidies_spending(state, n);
	float w_reps = estimate_reparations_spending(state, n);
	float subject_payments = estimate_subject_payments_paid(state, n);
	return w_sub + w_reps + subject_payments;
}



float estimate_max_domestic_investment(sys::state& state, dcon::nation_id n) {
	return economy::estimate_next_budget(state, n);
}

float estimate_current_domestic_investment(sys::state& state, dcon::nation_id n) {
	return estimate_max_domestic_investment(state, n) * float(state.world.nation_get_domestic_investment_spending(n)) / 100.0f;
}

float estimate_land_spending(sys::state& state, dcon::nation_id n) {
	float total = 0.0f;
	uint32_t total_commodities = state.world.commodity_size();
	state.world.nation_for_each_state_ownership(n, [&](auto soid) {
		auto local_state = state.world.state_ownership_get_state(soid);
		auto market = state.world.state_instance_get_market_from_local_market(local_state);
		for(uint32_t i = 1; i < total_commodities; ++i) {
			dcon::commodity_id cid{ dcon::commodity_id::value_base_t(i) };
			total +=
				state.world.market_get_army_demand(market, cid)
				* price(state, market, cid)
				* state.world.market_get_actual_probability_to_buy(market, cid);
		}
	});
	return total;
}

float estimate_naval_spending(sys::state& state, dcon::nation_id n) {
	float total = 0.0f;
	uint32_t total_commodities = state.world.commodity_size();
	state.world.nation_for_each_state_ownership(n, [&](auto soid) {
		auto local_state = state.world.state_ownership_get_state(soid);
		auto market = state.world.state_instance_get_market_from_local_market(local_state);
		for(uint32_t i = 1; i < total_commodities; ++i) {
			dcon::commodity_id cid{ dcon::commodity_id::value_base_t(i) };
			total += state.world.market_get_navy_demand(market, cid)
				* price(state, market, cid)
				* state.world.market_get_actual_probability_to_buy(market, cid);
		}
	});
	return total;
}

float estimate_war_subsidies(sys::state& state, dcon::nation_id target, dcon::nation_id source) {
	/* total-nation-tax-base x defines:WARSUBSIDIES_PERCENT */

	auto target_m_costs = (state.world.nation_get_total_rich_income(target) + state.world.nation_get_total_middle_income(target) + state.world.nation_get_total_poor_income(target)) * state.defines.warsubsidies_percent;
	auto source_m_costs = (state.world.nation_get_total_rich_income(source) + state.world.nation_get_total_middle_income(source) + state.world.nation_get_total_poor_income(source)) * state.defines.warsubsidies_percent;
	return std::min(target_m_costs, source_m_costs);
}

float estimate_subject_payments_paid(sys::state& state, dcon::nation_id n) {
	auto tax = explain_tax_income(state, n);
	auto collected_tax = tax.mid + tax.poor + tax.rich;

	auto rel = state.world.nation_get_overlord_as_subject(n);
	auto overlord = state.world.overlord_get_ruler(rel);

	if(overlord) {
		auto transferamt = collected_tax;

		if(state.world.nation_get_is_substate(n)) {
			transferamt *= state.defines.alice_substate_subject_money_transfer / 100.f;
		} else {
			transferamt *= state.defines.alice_puppet_subject_money_transfer / 100.f;
		}

		(void)n; (void)transferamt;
		return 0.f;
	}

	return 0;
}

float estimate_subject_payments_received(sys::state& state, dcon::nation_id o) {
	auto res = 0.0f;
	for(auto n : state.world.in_nation) {
		auto rel = state.world.nation_get_overlord_as_subject(n);
		auto overlord = state.world.overlord_get_ruler(rel);

		if(overlord == o) {
			auto tax = explain_tax_income(state, n);
			auto const collected_tax = tax.poor + tax.mid + tax.rich;
			auto transferamt = collected_tax;

			if(state.world.nation_get_is_substate(n)) {
				transferamt *= state.defines.alice_substate_subject_money_transfer / 100.f;
			} else {
				transferamt *= state.defines.alice_puppet_subject_money_transfer / 100.f;
			}

			res += transferamt;
		}
	}

	return res;
}

construction_status province_building_construction(sys::state& state, dcon::province_id p, province_building_type t) {
	for(auto row : state.world.province_get_province_building_construction(p)) if(row.get_type() == uint8_t(t))
		return {capital_projects::material_progress(state, capital_projects::project_for(state, row.id)), true};
	return {0.0f, false};
}
construction_status factory_upgrade(sys::state& state, dcon::factory_id f) {
	construction_status result{0.0f, false};
	state.world.for_each_capital_project([&](auto p) {
		if(state.world.capital_project_get_factory_from_capital_project_target_factory(p) == f
			&& state.world.capital_project_get_status(p) < uint8_t(capital_projects::status::completed))
			result = {capital_projects::material_progress(state, p), true};
	});
	return result;
}
float unit_construction_progress(sys::state&, dcon::province_land_construction_id) { return 0.0f; }
float unit_construction_progress(sys::state& state, dcon::province_naval_construction_id c) {
	return capital_projects::material_progress(state, capital_projects::project_for(state, c));
}

// This is used specifically in AI calculations, and omits subject income calculation because that requires iterating over all subjects and calculating their tax income seperately, will will cause OOS when parallelized over nations in ai::update_budget
float estimate_daily_income_ai(sys::state& state, dcon::nation_id n) {
	auto tax = explain_tax_income(state, n);
	auto tariff = estimate_tariff_export_income(state, n) + estimate_tariff_import_income(state, n);
	auto gold = estimate_gold_income(state, n);
	auto diplomacy = estimate_war_subsidies_income(state, n) + estimate_reparations_income(state, n);
	return tax.mid + tax.poor + tax.rich + tariff + gold + diplomacy;
}

/* TODO -
 * This should return what we think the income will be next day, and as a result wont account for any unprecedented actions
 * return value is passed directly into text::fp_currency{} without adulteration.
 */
float estimate_daily_income(sys::state& state, dcon::nation_id n) {
	auto tax = explain_tax_income(state, n);
	auto tariff = estimate_tariff_export_income(state, n) + estimate_tariff_import_income(state, n);
	auto gold = estimate_gold_income(state, n);
	auto diplomacy = estimate_diplomatic_income(state, n);
	return tax.mid + tax.poor + tax.rich + tariff + gold + diplomacy;
}

void try_add_factory_to_state(sys::state& state, dcon::state_instance_id, dcon::factory_type_id) {
	assert(false && "scripted factory creation must use a funded canonical capital project");
	std::abort();
}

command::budget_settings_data budget_minimums(sys::state& state, dcon::nation_id n) {
	command::budget_settings_data result;
	result.education_spending = 0;
	result.military_spending = 0;
	result.administrative_spending = 0;
	result.social_spending = 0;
	result.land_spending = 0;
	result.naval_spending = 0;
	result.construction_spending = 0;
	result.poor_tax = 0;
	result.middle_tax = 0;
	result.rich_tax = 0;
	result.tariffs_import = 0;
	result.tariffs_export = 0;
	result.domestic_investment = 0;
	result.overseas = 0;

	{
		auto min_tariff = int32_t(100.0f * state.world.nation_get_modifier_values(n, sys::national_mod_offsets::min_tariff));
		result.tariffs_import = int8_t(std::clamp(min_tariff, 0, 100));
		result.tariffs_export = int8_t(std::clamp(min_tariff, 0, 100));
	}
	{
		auto min_tax = int32_t(100.0f * state.world.nation_get_modifier_values(n, sys::national_mod_offsets::min_tax));
		result.poor_tax = int8_t(std::clamp(min_tax, 0, 100));
		result.middle_tax = int8_t(std::clamp(min_tax, 0, 100));
		result.rich_tax = int8_t(std::clamp(min_tax, 0, 100));
	}
	{
		auto min_spend = int32_t(100.0f * state.world.nation_get_modifier_values(n, sys::national_mod_offsets::min_military_spending));
		result.military_spending = int8_t(std::clamp(min_spend, 0, 100));
	}
	{
		auto min_spend = int32_t(100.0f * state.world.nation_get_modifier_values(n, sys::national_mod_offsets::min_social_spending));
		result.social_spending = int8_t(std::clamp(min_spend, 0, 100));
	}
	{
		auto min_spend = int32_t(100.0f * state.world.nation_get_modifier_values(n, sys::national_mod_offsets::min_domestic_investment));
		result.domestic_investment = int8_t(std::clamp(min_spend, 0, 100));
	}
	{
		auto min_spend = int32_t(100.0f * state.world.nation_get_modifier_values(n, sys::national_mod_offsets::min_land_upkeep));
		result.land_spending = int8_t(std::clamp(min_spend, 0, 100));
	}
	return result;
}
command::budget_settings_data budget_maximums(sys::state& state, dcon::nation_id n) {
	command::budget_settings_data result;
	result.education_spending = 100;
	result.military_spending = 100;
	result.administrative_spending = 100;
	result.social_spending = 100;
	result.land_spending = 100;
	result.naval_spending = 100;
	result.construction_spending = 100;
	result.poor_tax = 100;
	result.middle_tax = 100;
	result.rich_tax = 100;
	result.tariffs_import = 100;
	result.tariffs_export = 100;
	result.domestic_investment = 100;
	result.overseas = 100;
	result.subsidies = 100;

	{
		auto min_tariff = int32_t(100.0f * state.world.nation_get_modifier_values(n, sys::national_mod_offsets::min_tariff));
		auto max_tariff = int32_t(100.0f * state.world.nation_get_modifier_values(n, sys::national_mod_offsets::max_tariff));
		max_tariff = std::max(min_tariff, max_tariff);

		result.tariffs_import = int8_t(std::clamp(max_tariff, 0, 100));
		result.tariffs_export = int8_t(std::clamp(max_tariff, 0, 100));
	}
	{
		auto min_tax = int32_t(100.0f * state.world.nation_get_modifier_values(n, sys::national_mod_offsets::min_tax));
		auto max_tax = int32_t(100.0f * state.world.nation_get_modifier_values(n, sys::national_mod_offsets::max_tax));
		if(max_tax <= 0)
			max_tax = 100;
		max_tax = std::max(min_tax, max_tax);

		result.poor_tax = int8_t(std::clamp(max_tax, 0, 100));
		result.middle_tax = int8_t(std::clamp(max_tax, 0, 100));
		result.rich_tax = int8_t(std::clamp(max_tax, 0, 100));
	}
	{
		auto min_spend =
			int32_t(100.0f * state.world.nation_get_modifier_values(n, sys::national_mod_offsets::min_military_spending));
		auto max_spend =
			int32_t(100.0f * state.world.nation_get_modifier_values(n, sys::national_mod_offsets::max_military_spending));
		if(max_spend <= 0)
			max_spend = 100;
		max_spend = std::max(min_spend, max_spend);

		result.military_spending = int8_t(std::clamp(max_spend, 0, 100));
	}
	{
		auto min_spend = int32_t(100.0f * state.world.nation_get_modifier_values(n, sys::national_mod_offsets::min_social_spending));
		auto max_spend = int32_t(100.0f * state.world.nation_get_modifier_values(n, sys::national_mod_offsets::max_social_spending));
		if(max_spend <= 0)
			max_spend = 100;
		max_spend = std::max(min_spend, max_spend);

		result.social_spending = int8_t(std::clamp(max_spend, 0, 100));
	}
	{
		auto min_spend = int32_t(100.0f * state.world.nation_get_modifier_values(n, sys::national_mod_offsets::min_domestic_investment));
		auto max_spend = int32_t(100.0f * state.world.nation_get_modifier_values(n, sys::national_mod_offsets::max_domestic_investment));
		if(max_spend <= 0)
			max_spend = 100;
		max_spend = std::max(min_spend, max_spend);

		result.domestic_investment = int8_t(std::clamp(max_spend, 0, 100));
	}
	return result;
}
void bound_budget_settings(sys::state& state, dcon::nation_id n) {
	{
		auto min_tariff = int32_t(100.0f * state.world.nation_get_modifier_values(n, sys::national_mod_offsets::min_tariff));
		auto max_tariff = int32_t(100.0f * state.world.nation_get_modifier_values(n, sys::national_mod_offsets::max_tariff));
		max_tariff = std::max(min_tariff, max_tariff);

		{
			auto& tariff = state.world.nation_get_tariffs_import(n);
			state.world.nation_set_tariffs_import(n, int8_t(std::clamp(std::clamp(int32_t(tariff), min_tariff, max_tariff), 0, 100)));
		}

		{
			auto& tariff = state.world.nation_get_tariffs_export(n);
			state.world.nation_set_tariffs_export(n, int8_t(std::clamp(std::clamp(int32_t(tariff), min_tariff, max_tariff), 0, 100)));
		}
	}
	{
		auto min_tax = int32_t(100.0f * state.world.nation_get_modifier_values(n, sys::national_mod_offsets::min_tax));
		auto max_tax = int32_t(100.0f * state.world.nation_get_modifier_values(n, sys::national_mod_offsets::max_tax));
		if(max_tax <= 0)
			max_tax = 100;
		max_tax = std::max(min_tax, max_tax);

		auto& ptax = state.world.nation_get_poor_tax(n);
		state.world.nation_set_poor_tax(n, int8_t(std::clamp(std::clamp(int32_t(ptax), min_tax, max_tax), 0, 100)));
		auto& mtax = state.world.nation_get_middle_tax(n);
		state.world.nation_set_middle_tax(n, int8_t(std::clamp(std::clamp(int32_t(mtax), min_tax, max_tax), 0, 100)));
		auto& rtax = state.world.nation_get_rich_tax(n);
		state.world.nation_set_rich_tax(n, int8_t(std::clamp(std::clamp(int32_t(rtax), min_tax, max_tax), 0, 100)));
	}
	{
		auto min_spend =
			int32_t(100.0f * state.world.nation_get_modifier_values(n, sys::national_mod_offsets::min_military_spending));
		auto max_spend =
			int32_t(100.0f * state.world.nation_get_modifier_values(n, sys::national_mod_offsets::max_military_spending));
		if(max_spend <= 0)
			max_spend = 100;
		max_spend = std::max(min_spend, max_spend);

		auto& v = state.world.nation_get_military_spending(n);
		state.world.nation_set_military_spending(n, int8_t(std::clamp(std::clamp(int32_t(v), min_spend, max_spend), 0, 100)));
	}
	{
		auto min_spend = int32_t(100.0f * state.world.nation_get_modifier_values(n, sys::national_mod_offsets::min_social_spending));
		auto max_spend = int32_t(100.0f * state.world.nation_get_modifier_values(n, sys::national_mod_offsets::max_social_spending));
		if(max_spend <= 0)
			max_spend = 100;
		max_spend = std::max(min_spend, max_spend);

		auto& v = state.world.nation_get_social_spending(n);
		state.world.nation_set_social_spending(n, int8_t(std::clamp(std::clamp(int32_t(v), min_spend, max_spend), 0, 100)));
	}
	{
		auto min_spend = int32_t(100.0f * state.world.nation_get_modifier_values(n, sys::national_mod_offsets::min_domestic_investment));
		auto max_spend = int32_t(100.0f * state.world.nation_get_modifier_values(n, sys::national_mod_offsets::max_domestic_investment));
		if(max_spend <= 0)
			max_spend = 100;
		max_spend = std::max(min_spend, max_spend);

		auto& v = state.world.nation_get_domestic_investment_spending(n);
		state.world.nation_set_domestic_investment_spending(n, int8_t(std::clamp(std::clamp(int32_t(v), min_spend, max_spend), 0, 100)));
	}
	{
		auto min_spend =
			int32_t(100.0f * state.world.nation_get_modifier_values(n, sys::national_mod_offsets::min_land_upkeep));
		auto max_spend = 100;
		max_spend = std::max(min_spend, max_spend);

		auto& v = state.world.nation_get_land_spending(n);
		state.world.nation_set_land_spending(n, int8_t(std::clamp(std::clamp(int32_t(v), min_spend, max_spend), 0, 100)));
	}
}


dcon::modifier_id get_province_selector_modifier(sys::state& state) {
	return state.economy_definitions.selector_modifier;
}

dcon::modifier_id get_province_immigrator_modifier(sys::state& state) {
	return state.economy_definitions.immigrator_modifier;
}

float estimate_investment_pool_daily_loss(sys::state& state, dcon::nation_id n) {
	(void)n;
	return 0.f;
}

// Does this commodity has any factory using potentials mechanic
// Since in vanilla there are no such factories, it will return false.
bool get_commodity_uses_potentials(sys::state& state, dcon::commodity_id c) {
	for(auto type : state.world.in_factory_type) {
		auto output = type.get_output();
		if(output == c && output.get_uses_potentials()) {
			return true;
		}
	}
	return false;
}

float calculate_province_factory_limit(sys::state& state, dcon::province_id pid, dcon::commodity_id c) {
	return state.world.province_get_factory_max_size(pid, c);
}

float calculate_state_factory_limit(sys::state& state, dcon::state_instance_id sid, dcon::commodity_id c) {
	float result = 0.f;
	province::for_each_province_in_state_instance(state, sid, [&](dcon::province_id pid) {
		result += calculate_province_factory_limit(state, pid, c);
	});
	return result;
}

float calculate_nation_factory_limit(sys::state& state, dcon::nation_id nid, dcon::commodity_id c) {
	float res = 0;
	for(auto p : state.world.in_province) {
		if(p.get_nation_from_province_ownership() != nid) {
			continue;
		}

		if(p.get_factory_limit_was_set_during_scenario_creation()) {
			res += calculate_province_factory_limit(state, p, c);
		}
	}

	return res;
}

bool do_resource_potentials_allow_construction(sys::state& state, [[maybe_unused]] dcon::nation_id source, dcon::province_id location, dcon::factory_type_id type) {
	/* If mod uses Factory Province limits */
	auto output = state.world.factory_type_get_output(type);
	auto limit = economy::calculate_province_factory_limit(state, location, output);

	if(!output.get_uses_potentials()) {
		return true;
	}

	// Is there a potential for this commodity limit?
	if(limit <= 0) {
		return false;
	}

	return true;
}

bool do_resource_potentials_allow_upgrade(sys::state& state, [[maybe_unused]] dcon::nation_id source, dcon::province_id location, dcon::factory_type_id type) {
	/* If mod uses Factory Province limits */
	auto output = state.world.factory_type_get_output(type);

	if(!output.get_uses_potentials()) {
		return true;
	}

	auto limit = economy::calculate_province_factory_limit(state, location, output);

	// Will upgrade put us over the limit?
	auto existing_levels = 0.f;
	for(auto f : state.world.province_get_factory_location(location)) {
		if(f.get_factory().get_building_type() == type) {
			existing_levels += f.get_factory().get_size();
		}
		if(existing_levels + state.world.factory_type_get_base_workforce(type) > limit) {
			return false;
		}
	}

	return true;
}

bool do_resource_potentials_allow_refit(sys::state& state, [[maybe_unused]] dcon::nation_id source, dcon::province_id location, dcon::factory_type_id from, dcon::factory_type_id refit_target) {
	/* If mod uses Factory Province limits */
	auto output = state.world.factory_type_get_output(from);
	auto limit = economy::calculate_province_factory_limit(state, location, output);

	if(!output.get_uses_potentials()) {
		return true;
	}

	auto refit_levels = 0.f;
	// How many levels changed factory has
	for(auto f : state.world.province_get_factory_location(location)) {
		if(f.get_factory().get_building_type() == from) {
			refit_levels = f.get_factory().get_size();
		}
	}
	// Will that put us over the limit?
	auto existing_levels = 0.f;
	for(auto f : state.world.province_get_factory_location(location)) {
		if(f.get_factory().get_building_type() == refit_target) {
			existing_levels += f.get_factory().get_size();
		}
		if(existing_levels + refit_levels > limit) {
			return false;
		}
	}

	return true;
}

} // namespace economy
