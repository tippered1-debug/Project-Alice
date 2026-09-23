#include "firm_agency.hpp"

#include "accounts/accounts.hpp"
#include "actors/organizations/organizations.hpp"
#include "compat/alice/legacy_bridge.hpp"
#include "economy/physical/concrete_market.hpp"
#include "economy/physical/concrete_labor.hpp"
#include "economy/physical/deposits.hpp"
#include "economy/capital_projects.hpp"
#include "economy/investment_ranking.hpp"
#include "economy/exact_person_economy.hpp"
#include "economy/physical/deposits.hpp"
#include "economy/physical/factory_inputs.hpp"
#include "economy/physical/inventory.hpp"
#include "economy/economy_stats.hpp"
#include "economy/banking/banking.hpp"
#include "relations/relations.hpp"
#include "system_state.hpp"
#include "world/site.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace economy::firm_agency {
namespace {
constexpr float epsilon = 1.0e-5f;
constexpr float probing_fraction = 0.05f;
constexpr float output_target_days = 3.0f;
constexpr float cash_safety_fraction = 0.10f;
constexpr float expectation_alpha = 0.20f;
constexpr float default_sell_through = 0.75f;
constexpr float default_markup = 0.12f;

float finite_nonnegative(float value, float fallback = 0.0f) {
	return std::isfinite(value) && value > 0.0f ? value : fallback;
}

float arrears_due(sys::state const& state, dcon::economic_actor_id debtor,
	dcon::commodity_id settlement) {
	float result = 0.0f;
	state.world.economic_actor_for_each_obligation_debtor_as_economic_actor(debtor, [&](auto relation) {
		auto obligation = state.world.obligation_debtor_get_obligation(relation);
		if(!obligation || state.world.obligation_get_kind(obligation) != uint8_t(relations::obligation_kind::payroll)
			|| state.world.obligation_get_status(obligation) != uint8_t(relations::obligation_status::active)
			|| state.world.obligation_get_settlement_commodity(obligation) != settlement) return;
		result += std::max(0.0f, relations::total_due(state, obligation));
	});
	return std::isfinite(result) ? result : 0.0f;
}

float output_in_transit(sys::state const& state, dcon::economic_actor_id owner,
	dcon::commodity_id commodity, dcon::site_id production_site) {
	float result = 0.0f;
	state.world.for_each_shipment([&](auto shipment) {
		auto ownership = state.world.shipment_get_shipment_owner(shipment);
		auto origin = state.world.shipment_get_shipment_origin(shipment);
		if(state.world.shipment_get_commodity(shipment) == commodity && ownership
			&& state.world.shipment_owner_get_economic_actor(ownership) == owner && origin
			&& state.world.shipment_origin_get_site(origin) == production_site)
			result += std::max(0.0f, state.world.shipment_get_remaining_quantity(shipment));
	});
	return result;
}

float full_payroll(sys::state const& state, dcon::factory_id factory, float units, float capacity) {
	return physical::concrete_labor::wage_cost_for_factory(state, factory, units, capacity);
}
}

production_decision decide_factory(sys::state const& state, dcon::factory_id factory) {
	production_decision result{};
	if(!factory || !state.world.factory_get_canonical_production(factory)) return result;
	auto type = state.world.factory_get_building_type(factory);
	auto province = compat::alice::province_for_factory(state, factory);
	auto site = world::site::site_for_factory(state, factory);
	auto owner = actors::organizations::operator_actor_for_factory(state, factory);
	auto market = province ? state.world.state_instance_get_market_from_local_market(
		state.world.province_get_state_membership(province)) : dcon::market_id{};
	if(!type || !province || !site || !owner || !market) return result;
	auto capacity = finite_nonnegative(state.world.factory_get_productive_capacity(factory));
	auto productivity = std::max(0.0f, finite_nonnegative(state.world.factory_get_productivity_factor(factory), 1.0f));
	auto output_commodity = state.world.factory_type_get_output(type);
	auto output_per_unit = finite_nonnegative(state.world.factory_type_get_output_amount(type)) * productivity;
	if(capacity <= epsilon || !output_commodity || output_per_unit <= epsilon) return result;
	auto output_price = state.world.factory_get_agency_expected_selling_price(factory);
	if(!std::isfinite(output_price) || output_price <= epsilon)
		output_price = physical::concrete_market::canonical_reference_price(state, market, output_commodity, state.current_date, 0.0f);
	result.expected_unit_revenue = finite_nonnegative(output_price) * output_per_unit;
	result.output_inventory = physical::inventory::quantity(state, site, output_commodity, owner);
	if(auto hub = physical::deposits::market_hub_for(state, market); hub)
		result.output_inventory += physical::inventory::quantity(state, hub, output_commodity, owner);
	result.output_inventory += output_in_transit(state, owner, output_commodity, site);

	auto const& inputs = state.world.factory_type_get_inputs(type);
	float input_cost_per_unit = 0.0f;
	for(uint32_t i = 0; i < economy::commodity_set::set_size; ++i) {
		auto commodity = inputs.commodity_type[i];
		if(!commodity) break;
		if(!physical::factory_inputs::ordinary_physical_input(state, commodity)) continue;
		auto price = physical::concrete_market::canonical_reference_price(state, market, commodity, state.current_date, 0.0f);
		input_cost_per_unit += finite_nonnegative(inputs.commodity_amounts[i]) * finite_nonnegative(price);
	}
	result.expected_variable_cost = input_cost_per_unit;
	result.expected_payroll_cost = full_payroll(state, factory, capacity, capacity);
	result.expected_gross_margin = result.expected_unit_revenue - result.expected_variable_cost
		- (capacity > epsilon ? result.expected_payroll_cost / capacity : 0.0f);

	// Production is anchored in persistent firm expectations and moves toward a
	// stock-adjusted target with inertia. The firm does not inspect today's fill.
	float sell_through = state.world.factory_get_agency_expected_sell_through(factory);
	if(!std::isfinite(sell_through) || sell_through <= epsilon) sell_through = default_sell_through;
	float desired = state.world.factory_get_agency_planned_units(factory);
	if(!std::isfinite(desired) || desired <= epsilon) desired = capacity * sell_through;
	float procurement_reliability = state.world.factory_get_agency_expected_input_reliability(factory);
	if(!std::isfinite(procurement_reliability) || procurement_reliability <= epsilon) procurement_reliability = 0.85f;
	desired = std::min(desired, capacity * (0.5f + 0.5f * std::clamp(procurement_reliability, 0.1f, 1.0f)));
	if(result.expected_gross_margin < -epsilon) desired = std::min(desired, capacity * probing_fraction);
	if(result.output_inventory > output_per_unit * capacity * output_target_days)
		desired *= std::clamp((output_per_unit * capacity * output_target_days)
			/ std::max(result.output_inventory, epsilon), 0.0f, 1.0f);

	auto funding = physical::factory_inputs::procurement_account_for(state, owner);
	auto account = funding.account;
	auto payroll_settlement = state.world.factory_get_payroll_settlement(factory);
	auto payroll_account = payroll_settlement ? accounts::find_account(state, owner, payroll_settlement) : dcon::monetary_account_id{};
	auto procurement_cash = account ? std::max(0.0f, accounts::balance(state, account)
		- physical::concrete_market::reserved_bid_amount(state, account)) : 0.0f;
	auto payroll_cash = payroll_account ? std::max(0.0f, accounts::balance(state, payroll_account)
		- physical::concrete_market::reserved_bid_amount(state, payroll_account)
		- arrears_due(state, owner, payroll_settlement)
		- exact_person_economy::unpaid_wages_for_factory(state, factory)) : 0.0f;
	if(account && payroll_account && account == payroll_account)
		procurement_cash = payroll_cash = std::max(0.0f, procurement_cash
			- arrears_due(state, owner, payroll_settlement)
			- exact_person_economy::unpaid_wages_for_factory(state, factory));
	procurement_cash *= (1.0f - cash_safety_fraction);
	payroll_cash *= (1.0f - cash_safety_fraction);
	result.cash_limited_units = desired;
	auto procurement_cost = [&](float units) {
		float total = 0.0f;
		auto reliability = state.world.factory_get_agency_expected_input_reliability(factory);
		if(!std::isfinite(reliability) || reliability <= epsilon) reliability = 0.85f;
		auto cover = 1.0f + 0.5f * (1.0f - std::clamp(reliability, 0.1f, 1.0f));
		auto bid_markup = 0.05f + (1.0f - std::clamp(reliability, 0.1f, 1.0f)) * 0.15f;
		for(uint32_t i = 0; i < economy::commodity_set::set_size; ++i) {
			auto commodity = inputs.commodity_type[i];
			if(!commodity) break;
			if(physical::factory_inputs::ordinary_physical_input(state, commodity)) {
				auto price = physical::concrete_market::canonical_reference_price(state, market, commodity, state.current_date, 0.0f);
				auto required = finite_nonnegative(inputs.commodity_amounts[i]) * units * cover;
				total += std::max(0.0f, physical::factory_inputs::net_demand(state, site, owner, commodity, required)
					- physical::factory_inputs::active_factory_commitment(state, factory, site, commodity))
					* finite_nonnegative(price) * (1.0f + bid_markup);
			}
		}
		return total;
	};
	auto affordable = [&](float units) {
		auto procurement = procurement_cost(units);
		auto payroll = full_payroll(state, factory, units, capacity);
		if(account && payroll_account && account == payroll_account)
			return procurement + payroll <= procurement_cash + epsilon;
		return (!account ? procurement <= epsilon : procurement <= procurement_cash + epsilon)
			&& (!payroll_account ? payroll <= epsilon : payroll <= payroll_cash + epsilon);
	};
	if(!affordable(0.0f)) result.cash_limited_units = 0.0f;
	else {
		float low = 0.0f, high = desired;
		for(int i = 0; i < 32; ++i) {
			auto middle = (low + high) * 0.5f;
			if(affordable(middle)) low = middle;
			else high = middle;
		}
		result.cash_limited_units = low;
	}
	result.desired_units = std::clamp(std::min(desired, result.cash_limited_units), 0.0f, capacity);
	result.desired_output = result.desired_units * output_per_unit;
	result.desired_utilization = capacity > epsilon ? result.desired_units / capacity : 0.0f;
	for(uint32_t i = 0; i < economy::commodity_set::set_size; ++i) {
		auto commodity = inputs.commodity_type[i];
		if(!commodity) break;
		result.required_inputs.commodity_type[i] = commodity;
		result.required_inputs.commodity_amounts[i] = finite_nonnegative(inputs.commodity_amounts[i]) * result.desired_units;
	}
	return result;
}

float desired_production(sys::state const& state, dcon::factory_id factory) {
	return decide_factory(state, factory).desired_output;
}

void update_decisions(sys::state& state) {
	std::unordered_set<uint32_t> serviced_actors;
	std::unordered_set<uint32_t> defaulted_actors;
	state.world.for_each_factory([&](dcon::factory_id factory) {
		if(!state.world.factory_get_canonical_production(factory)) return;
		auto actor = actors::organizations::operator_actor_for_factory(state, factory);
		if(!actor || !serviced_actors.insert(actor.index()).second) return;
		auto result = banking::service_actor_loans(state, actor, state.current_date, 30);
		if(result.defaulted_loans > 0) defaulted_actors.insert(actor.index());
	});

	state.world.for_each_factory([&](dcon::factory_id factory) {
		if(!state.world.factory_get_canonical_production(factory)) return;
		auto type = state.world.factory_get_building_type(factory);
		auto province = compat::alice::province_for_factory(state, factory);
		auto site = world::site::site_for_factory(state, factory);
		auto owner = actors::organizations::operator_actor_for_factory(state, factory);
		auto market = province ? state.world.state_instance_get_market_from_local_market(
			state.world.province_get_state_membership(province)) : dcon::market_id{};
		if(!type || !site || !owner || !market) return;
		if(defaulted_actors.contains(owner.index()))
			state.world.factory_set_agency_distress_days(factory, std::max<uint16_t>(90,
				state.world.factory_get_agency_distress_days(factory)));
		auto output = state.world.factory_type_get_output(type);
		auto hub = physical::deposits::market_hub_for(state, market);
		auto cursor = state.world.factory_get_agency_last_observation_date(factory);
		float offered = 0.0f, sold = 0.0f, revenue = 0.0f, sold_quantity = 0.0f;
		state.world.for_each_concrete_market_ask([&](auto ask) {
			if(state.world.concrete_market_ask_get_factory_from_concrete_ask_factory(ask) != factory
				|| state.world.concrete_market_ask_get_created_on(ask) <= cursor
				|| state.world.concrete_market_ask_get_created_on(ask) > state.current_date
				|| state.world.concrete_market_ask_get_commodity_from_concrete_ask_commodity(ask) != output) return;
			offered += std::max(0.0f, state.world.concrete_market_ask_get_original_quantity(ask));
		});
		state.world.for_each_concrete_trade_fill([&](auto fill) {
			auto ask = state.world.concrete_trade_fill_get_concrete_market_ask_from_concrete_fill_ask(fill);
			if(!ask || state.world.concrete_market_ask_get_factory_from_concrete_ask_factory(ask) != factory
				|| state.world.concrete_market_ask_get_commodity_from_concrete_ask_commodity(ask) != output
				|| state.world.concrete_market_ask_get_created_on(ask) <= cursor
				|| state.world.concrete_market_ask_get_created_on(ask) > state.current_date) return;
			auto quantity = std::max(0.0f, state.world.concrete_trade_fill_get_quantity(fill));
			sold += quantity;
			revenue += quantity * std::max(0.0f, state.world.concrete_trade_fill_get_execution_price(fill));
			sold_quantity += quantity;
		});
		if(offered > epsilon) {
			auto expected = state.world.factory_get_agency_expected_sell_through(factory);
			if(!std::isfinite(expected) || expected <= epsilon) expected = default_sell_through;
			auto observed = std::clamp(sold / offered, 0.0f, 1.0f);
			expected += expectation_alpha * (observed - expected);
			state.world.factory_set_agency_expected_sell_through(factory, std::clamp(expected, 0.05f, 1.0f));
			state.world.factory_set_agency_recent_revenue(factory, revenue);
			state.world.factory_set_agency_recent_unsold(factory, std::max(0.0f, offered - sold));
			if(sold_quantity > epsilon) {
				auto realized_price = revenue / sold_quantity;
				auto prior_price = state.world.factory_get_agency_expected_selling_price(factory);
				state.world.factory_set_agency_expected_selling_price(factory,
					prior_price > epsilon ? prior_price + expectation_alpha * (realized_price - prior_price) : realized_price);
			}
		}
		state.world.factory_set_agency_last_observation_date(factory, state.current_date);
		float inbound_quantity = 0.0f, inbound_days = 0.0f;
		state.world.for_each_shipment([&](auto shipment) {
			auto shipment_owner = state.world.shipment_get_shipment_owner(shipment);
			auto destination = state.world.shipment_get_shipment_destination(shipment);
			if(!shipment_owner || !destination
				|| state.world.shipment_owner_get_economic_actor(shipment_owner) != owner
				|| state.world.shipment_destination_get_site(destination) != site) return;
			auto quantity = std::max(0.0f, state.world.shipment_get_remaining_quantity(shipment));
			inbound_quantity += quantity;
			inbound_days += quantity * float(std::max<uint32_t>(1, state.world.shipment_get_remaining_days(shipment)));
		});
		if(inbound_quantity > epsilon) {
			auto observed_days = inbound_days / inbound_quantity;
			auto expected_days = state.world.factory_get_agency_expected_delivery_days(factory);
			if(!std::isfinite(expected_days) || expected_days <= epsilon) expected_days = observed_days;
			state.world.factory_set_agency_expected_delivery_days(factory,
				std::clamp(expected_days + expectation_alpha * (observed_days - expected_days), 1.0f, 60.0f));
		}
		if(offered > epsilon) {
			auto capacity = finite_nonnegative(state.world.factory_get_productive_capacity(factory));
			auto plan = finite_nonnegative(state.world.factory_get_agency_planned_units(factory));
			auto variable_cost = 0.0f;
			auto const& recipe = state.world.factory_type_get_inputs(type);
			for(uint32_t i = 0; i < economy::commodity_set::set_size; ++i) {
				auto commodity = recipe.commodity_type[i];
				if(!commodity) break;
				if(!physical::factory_inputs::ordinary_physical_input(state, commodity)) continue;
				variable_cost += finite_nonnegative(recipe.commodity_amounts[i])
					* physical::concrete_market::canonical_reference_price(state, market, commodity, state.current_date, 0.0f);
			}
			auto wage_per_unit = capacity > epsilon
				? physical::concrete_labor::wage_cost_for_factory(state, factory, capacity, capacity) / capacity : 0.0f;
			auto costs = (variable_cost + wage_per_unit) * std::max(plan, sold);
			state.world.factory_set_agency_recent_costs(factory, costs);
			state.world.factory_set_agency_recent_profit(factory, revenue - costs);
			state.world.factory_set_agency_cashflow(factory, revenue - costs);
			state.world.factory_set_agency_expected_wage_per_worker(factory, wage_per_unit);
		}

		auto capacity = finite_nonnegative(state.world.factory_get_productive_capacity(factory));
		auto sell_through = state.world.factory_get_agency_expected_sell_through(factory);
		if(!std::isfinite(sell_through) || sell_through <= epsilon) sell_through = default_sell_through;
		auto last_plan_date = state.world.factory_get_agency_last_planning_date(factory);
		if(last_plan_date && state.current_date.to_raw_value() - last_plan_date.to_raw_value() < 8) return;
		auto distress_days = state.world.factory_get_agency_distress_days(factory);
		auto recent_profit = state.world.factory_get_agency_recent_profit(factory);
		bool distressed = sell_through < 0.35f || (recent_profit < -epsilon && offered > epsilon);
		if(distressed) distress_days = uint16_t(std::min<uint32_t>(65535, uint32_t(distress_days) + 7));
		else distress_days = uint16_t(distress_days > 7 ? distress_days - 7 : 0);
		state.world.factory_set_agency_distress_days(factory, distress_days);
		if(distress_days >= 90 && capacity > 0.1f) {
			capacity = std::max(0.1f, capacity * 0.98f);
			state.world.factory_set_productive_capacity(factory, capacity);
		}
		auto prior_plan = state.world.factory_get_agency_planned_units(factory);
		if(!std::isfinite(prior_plan) || prior_plan <= epsilon) prior_plan = capacity * default_sell_through;
		auto output_per_unit = finite_nonnegative(state.world.factory_type_get_output_amount(type))
			* finite_nonnegative(state.world.factory_get_productivity_factor(factory), 1.0f);
		auto const& inputs = state.world.factory_type_get_inputs(type);
		float input_unit_cost = 0.0f;
		for(uint32_t i = 0; i < economy::commodity_set::set_size; ++i) {
			auto commodity = inputs.commodity_type[i];
			if(!commodity) break;
			if(physical::factory_inputs::ordinary_physical_input(state, commodity))
				input_unit_cost += finite_nonnegative(inputs.commodity_amounts[i])
					* physical::concrete_market::canonical_reference_price(state, market, commodity, state.current_date, 0.0f);
		}
		state.world.factory_set_agency_expected_input_unit_cost(factory, input_unit_cost);
		auto stock = physical::inventory::quantity(state, site, output, owner)
			+ (hub ? physical::inventory::quantity(state, hub, output, owner) : 0.0f)
			+ output_in_transit(state, owner, output, site);
		auto target_stock = capacity * output_per_unit * 2.0f * sell_through;
		state.world.factory_set_agency_target_output_inventory(factory, target_stock);
		auto reliability = state.world.factory_get_agency_expected_input_reliability(factory);
		if(!std::isfinite(reliability) || reliability <= epsilon) reliability = 0.85f;
		state.world.factory_set_agency_target_input_coverage(factory, 1.0f + 0.5f * (1.0f - reliability));
		auto stock_pressure = target_stock > epsilon ? (target_stock - stock) / target_stock : 0.0f;
		auto target = capacity * std::clamp(sell_through + 0.30f * stock_pressure, 0.05f, 1.0f);
		auto max_step = std::max(0.05f, capacity * 0.10f);
		auto planned = std::clamp(target, std::max(0.0f, prior_plan - max_step), std::min(capacity, prior_plan + max_step));
		state.world.factory_set_agency_planned_units(factory, planned);
		state.world.factory_set_agency_target_employment(factory, planned);
		if(state.world.factory_get_agency_expected_selling_price(factory) <= epsilon)
			state.world.factory_set_agency_expected_selling_price(factory,
				physical::concrete_market::canonical_reference_price(state, market, output, state.current_date, 0.0f));
		auto markup = state.world.factory_get_agency_ask_markup(factory);
		if(!std::isfinite(markup) || markup <= epsilon) markup = default_markup;
		if(sell_through > 0.90f && stock < target_stock * 0.5f) markup += 0.01f;
		else if(sell_through < 0.55f || stock > target_stock * 1.5f) markup -= 0.015f;
		auto funding = physical::factory_inputs::procurement_account_for(state, owner);
		auto cash = funding.account ? std::max(0.0f, accounts::balance(state, funding.account)
			- physical::concrete_market::reserved_bid_amount(state, funding.account)) : 0.0f;
		if(cash < std::max(0.01f, state.world.factory_get_agency_recent_costs(factory) * 0.5f)) markup -= 0.02f;
		state.world.factory_set_agency_ask_markup(factory, std::clamp(markup, 0.02f, 0.60f));
		auto existing_project = state.world.factory_get_agency_expansion_project(factory);
		if(existing_project && (!state.world.capital_project_is_valid(existing_project)
			|| state.world.capital_project_get_status(existing_project) >= uint8_t(capital_projects::status::completed))) {
			state.world.factory_set_agency_expansion_project(factory, dcon::capital_project_id{});
			existing_project = {};
		}
		if(!existing_project && capacity > epsilon && planned >= capacity * 0.85f
			&& state.world.factory_get_actual_utilization(factory) >= 0.80f
			&& sell_through >= 0.88f && recent_profit > epsilon && distress_days == 0) {
			auto added_capacity = std::max(0.10f, capacity * 0.10f);
			auto const& construction = state.world.factory_type_get_construction_costs(type);
			auto construction_scale = added_capacity * 0.5f;
			float capital_cost = 0.0f;
			for(uint32_t i = 0; i < economy::commodity_set::set_size; ++i) {
				auto commodity = construction.commodity_type[i];
				if(!commodity) break;
				if(!physical::factory_inputs::ordinary_physical_input(state, commodity)) continue;
				auto quantity = finite_nonnegative(construction.commodity_amounts[i]) * construction_scale;
				capital_cost += quantity * physical::concrete_market::canonical_reference_price(
					state, market, commodity, state.current_date, 0.0f);
			}
			economy::investment::project_inputs proposal{};
			proposal.capital_cost = capital_cost;
			proposal.gross_daily_revenue = state.world.factory_get_agency_expected_selling_price(factory)
				* output_per_unit * added_capacity;
			proposal.daily_material_cost = input_unit_cost * added_capacity * sell_through;
			proposal.daily_wage_cost = state.world.factory_get_agency_expected_wage_per_worker(factory) * added_capacity;
			proposal.expected_sell_through = sell_through;
			proposal.input_reliability = reliability;
			proposal.logistics_reliability = 1.0f / (1.0f + 0.02f
				* std::max(0.0f, state.world.factory_get_agency_expected_delivery_days(factory)));
			proposal.annual_interest_rate = 0.05f;
			proposal.demand_risk = 1.0f - sell_through;
			proposal.jobs = added_capacity;
			auto score = economy::investment::evaluate(proposal);
			auto funding = physical::factory_inputs::procurement_account_for(state, owner);
			auto available_cash = funding.account ? std::max(0.0f, accounts::balance(state, funding.account)
				- physical::concrete_market::reserved_bid_amount(state, funding.account)) : 0.0f;
			if(score.privately_viable && capital_cost > epsilon && available_cash >= capital_cost * 1.50f) {
				auto project = capital_projects::create_factory_expansion(state, factory, added_capacity, funding.settlement);
				bool requirements_added = bool(project);
				for(uint32_t i = 0; requirements_added && i < economy::commodity_set::set_size; ++i) {
					auto commodity = construction.commodity_type[i];
					if(!commodity) break;
					if(!physical::factory_inputs::ordinary_physical_input(state, commodity)) continue;
					auto quantity = finite_nonnegative(construction.commodity_amounts[i]) * construction_scale;
					if(quantity > epsilon && !capital_projects::add_requirement(state, project, commodity, quantity))
						requirements_added = false;
				}
			if(requirements_added && capital_projects::fund(state, project, funding.account, capital_cost * 1.50f))
					state.world.factory_set_agency_expansion_project(factory, project);
				else if(project) (void)capital_projects::cancel(state, project);
			}
		}
		state.world.factory_set_agency_last_planning_date(factory, state.current_date);
	});
}

void post_output_asks(sys::state& state) {
	state.world.for_each_factory([&](dcon::factory_id factory) {
		if(!state.world.factory_get_canonical_production(factory)) return;
		auto type = state.world.factory_get_building_type(factory);
		auto province = compat::alice::province_for_factory(state, factory);
		auto owner = actors::organizations::operator_actor_for_factory(state, factory);
		auto output = type ? state.world.factory_type_get_output(type) : dcon::commodity_id{};
		auto market = province ? state.world.state_instance_get_market_from_local_market(
			state.world.province_get_state_membership(province)) : dcon::market_id{};
		auto hub = market ? physical::deposits::market_hub_for(state, market) : dcon::site_id{};
		if(!owner || !output || !hub || state.world.commodity_get_is_local(output)
			|| state.world.commodity_get_money_rgo(output)) return;
		auto quantity = physical::inventory::quantity(state, hub, output, owner);
		if(quantity <= epsilon) return;
		auto reference = state.world.factory_get_agency_expected_selling_price(factory);
		if(!std::isfinite(reference) || reference <= epsilon)
			reference = physical::concrete_market::canonical_reference_price(state, market, output, state.current_date, 0.0f);
		auto markup = state.world.factory_get_agency_ask_markup(factory);
		if(!std::isfinite(markup) || markup <= epsilon) markup = default_markup;
		auto unit_cost_floor = std::max(0.0f, state.world.factory_get_agency_expected_input_unit_cost(factory)
			+ state.world.factory_get_agency_expected_wage_per_worker(factory));
		auto target_stock = state.world.factory_get_agency_target_output_inventory(factory);
		bool clearance = state.world.factory_get_agency_recent_profit(factory) < 0.0f
			|| (target_stock > epsilon && state.world.factory_get_agency_recent_unsold(factory) > target_stock);
		auto cost_floor = unit_cost_floor * (clearance ? 0.85f : 1.0f);
		auto minimum_price = std::max(reference * (clearance ? 0.75f : 1.0f), cost_floor)
			* (1.0f + std::clamp(markup, 0.02f, 0.60f));
		if(minimum_price > epsilon && std::isfinite(minimum_price))
			(void)physical::concrete_market::post_ask(state, owner, hub, market, output,
				quantity, minimum_price, physical::concrete_market::order_purpose::general, factory);
	});
}

void observe_production(sys::state& state, dcon::factory_id factory, float planned_units, float realized_units) {
	if(!factory || !state.world.factory_get_canonical_production(factory)) return;
	planned_units = finite_nonnegative(planned_units);
	realized_units = finite_nonnegative(realized_units);
	auto unmet = std::max(0.0f, planned_units - realized_units);
	state.world.factory_set_agency_recent_unmet_input(factory, unmet);
	auto reliability = state.world.factory_get_agency_expected_input_reliability(factory);
	if(!std::isfinite(reliability) || reliability <= epsilon) reliability = 0.85f;
	auto observed = planned_units > epsilon ? std::clamp(realized_units / planned_units, 0.0f, 1.0f) : reliability;
	state.world.factory_set_agency_expected_input_reliability(factory,
		std::clamp(reliability + expectation_alpha * (observed - reliability), 0.10f, 1.0f));
}
} // namespace economy::firm_agency
