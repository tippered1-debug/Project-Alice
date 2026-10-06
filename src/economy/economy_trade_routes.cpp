#include "economy_trade_routes.hpp"
#include "economy.hpp"
#include "economy_stats.hpp"
#include "system_state.hpp"
#include "economy_government.hpp"
#include "adaptive_ve.hpp"
#include "province_templates.hpp"
#include "advanced_province_buildings.hpp"
#include "economy_constants.hpp"
#include "economy_templates.hpp"
#include "money.hpp"
#include "province.hpp"
#include "price.hpp"
#include "world_trade_capacity.hpp"
#include "market_clearing.hpp"

// implements trade routes
// when changing logic of trade routes please update it everywhere
// due to performance reasons we have to duplicate it

namespace economy {

// value in [0, 1] range
// 0 means that trade profit due to price difference is pocketed by exporters
// 1 means that trade profit due to price difference is pocketed by importers
constexpr inline float import_profit_priority = 0.05f;

//constexpr inline float buy_optimism = 0.2f;
//constexpr inline float sell_optimism = 0.2f;

float trade_route_labour_demand_for_cargo(
	sys::state const& state,
	dcon::trade_route_id trade_route,
	float cargo
) {
	if(!std::isfinite(cargo) || cargo <= 0.f) return 0.f;
	auto const effect_of_scale = std::max(
		trade_effect_of_scale_lower_bound,
		1.f - cargo * effect_of_transportation_scale);
	auto const distance = state.world.trade_route_get_distance(trade_route);
	if(!std::isfinite(distance) || distance <= 0.f) return 0.f;
	auto const demanded = cargo * effect_of_scale * distance
		/ trade_distance_covered_by_pair_of_workers_per_unit_of_good;
	return std::isfinite(demanded) && demanded > 0.f ? demanded : 0.f;
}

// Read-only UI estimate derived from canonical shipment allocation.
float transportation_between_markets_labor_demand(sys::state& state, dcon::market_id market) {
	auto const allocation = world_trade::clear_trade_shipments(state);
	auto total = 0.f;
	for(auto route : state.world.market_get_trade_route(market))
		total += trade_route_labour_demand_for_cargo(state, route, allocation.actual(route));
	return total;
}

// Read-only UI estimate based on the cargo actually allocated to each route.
float transportation_inside_market_labor_demand(sys::state& state, dcon::market_id market, dcon::province_id capital) {
	(void)capital;
	auto const allocation = world_trade::clear_trade_shipments(state);
	auto total = 0.0f;
	for(auto route : state.world.market_get_trade_route(market))
		total += trade_route_labour_demand_for_cargo(state, route, allocation.actual(route));
	return total;
}

void make_trade_volume_tooltip(
	sys::state& state,
	text::columnar_layout& contents,
	dcon::trade_route_id route,
	dcon::commodity_id cid,
	dcon::market_id point_of_view
) {
	(void)point_of_view;
	auto const allocation = world_trade::clear_trade_shipments(state);
	text::add_line(state, contents, "alice_trade_actual_quantity",
		text::variable_type::x, text::fp_two_places{allocation.requested(route, cid)},
		text::variable_type::y, text::fp_two_places{allocation.actual(route, cid)});
}

embargo_explanation embargo_exists(
	sys::state& state, dcon::nation_id n_A, dcon::nation_id n_B
) {
	auto sphere_A = state.world.nation_get_in_sphere_of(n_A);
	auto sphere_B = state.world.nation_get_in_sphere_of(n_B);
	auto overlord_A = state.world.overlord_get_ruler(
		state.world.nation_get_overlord_as_subject(n_A)
	);
	auto overlord_B = state.world.overlord_get_ruler(
		state.world.nation_get_overlord_as_subject(n_B)
	);
	// Subjects have embargo of overlords propagated onto them
	auto market_leader_A = nations::get_market_leader(state, n_A);
	auto market_leader_B = nations::get_market_leader(state, n_B);

	// if market capital controllers are at war then we will break the link
	auto at_war = military::are_at_war(state, n_A, n_B);

	auto is_A_civ = state.world.nation_get_is_civilized(n_A);
	auto is_B_civ = state.world.nation_get_is_civilized(n_B);

	// sphere joins embargo
	// subject joins embargo
	// diplomatic embargos
	auto A_joins_sphere_wide_embargo = military::are_at_war(state, market_leader_A, market_leader_B);
	auto B_joins_sphere_wide_embargo = military::are_at_war(state, market_leader_B, market_leader_A);

	auto A_has_embargo = non_war_embargo_status(state, n_A, n_B, market_leader_A, market_leader_B);

	auto B_has_embargo = non_war_embargo_status(state, n_B, n_A, market_leader_B, market_leader_A);

	embargo_explanation result;

	result.war = at_war;
	result.origin_embargo = A_has_embargo;
	result.target_embargo = B_has_embargo;
	result.origin_join_embargo = A_joins_sphere_wide_embargo;
	result.target_join_embargo = B_joins_sphere_wide_embargo;

	result.combined = at_war
		|| A_has_embargo
		|| B_has_embargo
		|| A_joins_sphere_wide_embargo
		|| B_joins_sphere_wide_embargo;

	return result;
}

// Legacy route estimates are a read model. Concrete market fills now move the
// goods and settle buyer/seller accounts; this helper only describes projected
// route quantities for compatibility UI and budget estimates.

template<typename TRADE_ROUTE>
trade_and_tariff<TRADE_ROUTE> explain_trade_route_commodity_internal(
	sys::state const& state,
	TRADE_ROUTE trade_route,
	tariff_data<TRADE_ROUTE>& additional_data,
	dcon::commodity_id cid,
	typename trade_and_tariff<TRADE_ROUTE>::VALUE shipment_scale,
	typename trade_and_tariff<TRADE_ROUTE>::VALUE transport_effect_scale
) {
	using VALUE = typename std::conditional_t<ve::is_vector_type_s<TRADE_ROUTE>::value, ve::fp_vector, float>;
	auto current_volume = state.world.trade_route_get_volume(trade_route, cid);

	auto m0 = ve::apply([&](auto route, auto volume) {
		return state.world.trade_route_get_connected_markets(route, 0);
	}, trade_route, current_volume);


	auto origin = ve::apply([&](auto route, auto volume) {
		return volume > 0.f
			? state.world.trade_route_get_connected_markets(route, 0)
			: state.world.trade_route_get_connected_markets(route, 1);
	}, trade_route, current_volume);
	auto target = ve::apply([&](auto route, auto volume) {
		return volume > 0.f
			? state.world.trade_route_get_connected_markets(route, 1)
			: state.world.trade_route_get_connected_markets(route, 0);
	}, trade_route, current_volume);


	auto origin_is_0 = origin == m0;
	auto origin_is_1 = !origin_is_0;


	auto s_origin = state.world.market_get_zone_from_local_market(origin);
	auto s_target = state.world.market_get_zone_from_local_market(target);
	auto n_origin = state.world.state_instance_get_nation_from_state_ownership(s_origin);
	auto n_target = state.world.state_instance_get_nation_from_state_ownership(s_target);
	auto capital_origin = state.world.state_instance_get_capital(s_origin);
	auto capital_target = state.world.state_instance_get_capital(s_target);

	auto actual_n_origin = state.world.province_get_nation_from_province_control(capital_origin);
	auto actual_n_target = state.world.province_get_nation_from_province_control(capital_target);

	actual_n_origin = adaptive_ve::select(actual_n_origin != dcon::nation_id{ }, actual_n_origin, n_origin);
	actual_n_target = adaptive_ve::select(actual_n_target != dcon::nation_id{ }, actual_n_target, n_target);

	auto price_origin = state.world.market_get_price(origin, cid);
	auto price_target = state.world.market_get_price(target, cid);

	auto sat = ve::apply([&](dcon::market_id market) {
		return market_clearing::fill(state, market, cid,
			market_clearing::demand_class::trade);
	}, origin);

	auto absolute_volume =
		sat * adaptive_ve::abs(current_volume) * shipment_scale;

	auto import_amount = absolute_volume * additional_data.loss;
	auto transport_cost =
		additional_data.distance_cost_scaled * transport_effect_scale;

	const VALUE cut_domestic = economy::merchant_cut_domestic;
	const VALUE cut_foreign = economy::merchant_cut_foreign;

	auto merchant_cut = ve::select(actual_n_origin == actual_n_target, cut_domestic, cut_foreign);

	auto export_tariff = ve::select(origin_is_0, additional_data.export_tariff[0], additional_data.export_tariff[1]);
	auto import_tariff = ve::select(origin_is_0, additional_data.import_tariff[1], additional_data.import_tariff[0]);

	return {
		.origin = origin,
		.target = target,
		.origin_nation = actual_n_origin,
		.target_nation = actual_n_target,

		.amount_origin = absolute_volume,
		.amount_target = import_amount,

		.tariff_origin = absolute_volume * price_origin * export_tariff,
		.tariff_target = import_amount * price_target * import_tariff,

		.tariff_rate_origin = export_tariff,
		.tariff_rate_target = import_tariff,

		.price_origin = price_origin,
		.price_target = price_target,

		.transport_cost = transport_cost,
		.transportaion_loss = additional_data.loss,
		.distance = additional_data.distance,

		.payment_per_unit = price_origin
			* (1.f + export_tariff)
			* (1.f - import_profit_priority + merchant_cut)
			+ price_target
			* import_tariff
			+ transport_cost,

		// the rest of payment is handled as satisfaction of generic demand
		.payment_received_per_unit = price_origin * (merchant_cut - import_profit_priority)
	};
}

//trade_and_tariff<dcon::trade_route_id> explain_trade_route_commodity(
//	sys::state& state,
//	dcon::trade_route_id trade_route,
//	tariff_data<dcon::trade_route_id>& additional_data,
//	dcon::commodity_id cid
//) {
//	return explain_trade_route_commodity_internal<dcon::trade_route_id>(state, trade_route, additional_data, cid);
//}
trade_and_tariff<ve::contiguous_tags<dcon::trade_route_id>> explain_trade_route_commodity(
	sys::state const& state,
	ve::contiguous_tags<dcon::trade_route_id> trade_route,
	tariff_data<ve::contiguous_tags<dcon::trade_route_id>>& additional_data,
	dcon::commodity_id cid
) {
	auto const allocation = world_trade::clear_trade_shipments(state);
	auto const shipment_scale = ve::apply(
		[&](dcon::trade_route_id route) {
			return allocation.scale(route, cid);
		}, trade_route);
	auto const actual_cargo = ve::apply(
		[&](dcon::trade_route_id route) {
			return allocation.actual(route);
		}, trade_route);
	auto const actual_effect_of_scale = ve::max(
		trade_effect_of_scale_lower_bound,
		1.f - actual_cargo * effect_of_transportation_scale);
	auto const transport_effect_scale = actual_effect_of_scale
		/ ve::max(0.000001f, additional_data.effect_of_scale);
	return explain_trade_route_commodity_internal(
		state, trade_route, additional_data, cid,
		shipment_scale, transport_effect_scale);
}
trade_and_tariff<ve::partial_contiguous_tags<dcon::trade_route_id>> explain_trade_route_commodity(
	sys::state const& state,
	ve::partial_contiguous_tags<dcon::trade_route_id> trade_route,
	tariff_data<ve::partial_contiguous_tags<dcon::trade_route_id>>& additional_data,
	dcon::commodity_id cid
) {
	auto const allocation = world_trade::clear_trade_shipments(state);
	auto const shipment_scale = ve::apply(
		[&](dcon::trade_route_id route) {
			return allocation.scale(route, cid);
		}, trade_route);
	auto const actual_cargo = ve::apply(
		[&](dcon::trade_route_id route) {
			return allocation.actual(route);
		}, trade_route);
	auto const actual_effect_of_scale = ve::max(
		trade_effect_of_scale_lower_bound,
		1.f - actual_cargo * effect_of_transportation_scale);
	auto const transport_effect_scale = actual_effect_of_scale
		/ ve::max(0.000001f, additional_data.effect_of_scale);
	return explain_trade_route_commodity_internal(
		state, trade_route, additional_data, cid,
		shipment_scale, transport_effect_scale);
}

bool is_trade_route_relevant(sys::state& state, dcon::trade_route_id trade_route, dcon::nation_id n) {
	auto origin = state.world.trade_route_get_connected_markets(trade_route, 0);
	auto target = state.world.trade_route_get_connected_markets(trade_route, 1);
	auto s_origin = state.world.market_get_zone_from_local_market(origin);
	auto s_target = state.world.market_get_zone_from_local_market(target);
	auto n_origin = state.world.state_instance_get_nation_from_state_ownership(s_origin);
	auto n_target = state.world.state_instance_get_nation_from_state_ownership(s_target);
	auto capital_origin = state.world.state_instance_get_capital(s_origin);
	auto capital_target = state.world.state_instance_get_capital(s_target);
	auto controller_capital_origin = state.world.province_get_nation_from_province_control(capital_origin);
	auto controller_capital_target = state.world.province_get_nation_from_province_control(capital_target);

	controller_capital_origin = controller_capital_origin ? controller_capital_origin : n_origin;
	controller_capital_target = controller_capital_target ? controller_capital_target : n_target;

	if(controller_capital_origin == n) {
		return true;
	}
	if(controller_capital_target == n) {
		return true;
	}
	return false;
}

float estimate_port_service_price(sys::state const& state, dcon::state_instance_id s) {
	auto port_weight_target_total = 0.f;
	auto price_port_target = 0.f;
	province::for_each_province_in_state_instance(state, s, [&](dcon::province_id pid) {
		auto price = state.world.province_get_service_price(pid, services::list::port_capacity);
		auto size = 100.f + state.world.province_get_advanced_province_building_max_private_size(pid, advanced_province_buildings::list::civilian_ports);
		auto local_weight = size / (price_properties::service::epsilon + price);
		port_weight_target_total += local_weight;
	});
	province::for_each_province_in_state_instance(state, s, [&](dcon::province_id pid) {
		auto price = state.world.province_get_service_price(pid, services::list::port_capacity);
		auto size = 100.f + state.world.province_get_advanced_province_building_max_private_size(pid, advanced_province_buildings::list::civilian_ports);
		auto local_weight = size/ (price_properties::service::epsilon + price);
		price_port_target += local_weight / port_weight_target_total * price;
	});
	return price_port_target;
}

trade_and_tariff<dcon::trade_route_id> explain_trade_route_commodity(
		sys::state const& state, dcon::trade_route_id trade_route,
		dcon::commodity_id cid,
		world_trade::shipment_allocation const& allocation) {
	auto current_volume = state.world.trade_route_get_volume(trade_route, cid);
	auto origin =
		current_volume > 0.f
		? state.world.trade_route_get_connected_markets(trade_route, 0)
		: state.world.trade_route_get_connected_markets(trade_route, 1);
	auto target =
		current_volume <= 0.f
		? state.world.trade_route_get_connected_markets(trade_route, 0)
		: state.world.trade_route_get_connected_markets(trade_route, 1);

	auto s_origin = state.world.market_get_zone_from_local_market(origin);
	auto s_target = state.world.market_get_zone_from_local_market(target);
	auto n_origin = state.world.state_instance_get_nation_from_state_ownership(s_origin);
	auto n_target = state.world.state_instance_get_nation_from_state_ownership(s_target);
	auto capital_origin = state.world.state_instance_get_capital(s_origin);
	auto capital_target = state.world.state_instance_get_capital(s_target);
	auto controller_capital_origin = state.world.province_get_nation_from_province_control(capital_origin);
	auto controller_capital_target = state.world.province_get_nation_from_province_control(capital_target);

	controller_capital_origin = controller_capital_origin ? controller_capital_origin : n_origin;
	controller_capital_target = controller_capital_target ? controller_capital_target : n_target;

	auto origin_apply_tariff = current_volume > 0.f
		? state.world.trade_route_get_is_tariff_applied_0(trade_route)
		: state.world.trade_route_get_is_tariff_applied_1(trade_route);

	auto target_apply_tariff = current_volume > 0.f
		? state.world.trade_route_get_is_tariff_applied_1(trade_route)
		: state.world.trade_route_get_is_tariff_applied_0(trade_route);

	auto sat = market_clearing::fill(state, origin, cid,
		market_clearing::demand_class::trade);
	auto absolute_volume =
		sat * std::abs(current_volume) * allocation.scale(trade_route, cid);
	auto distance = state.world.trade_route_get_distance(trade_route);

	auto trade_good_loss_mult = std::max(0.f, 1.f - trade_loss_per_distance_unit * distance);
	auto import_amount = absolute_volume * trade_good_loss_mult;

	auto const transported_cargo = allocation.actual(trade_route);
	auto effect_of_scale = std::max(
		trade_effect_of_scale_lower_bound,
		1.f - transported_cargo * effect_of_transportation_scale);
	auto const route_capacity =
		world_trade::evaluate_route_shipment_capacity(
			state, allocation, trade_route);

	auto is_sea_route = state.world.trade_route_get_is_sea_route(trade_route);
	auto is_land_route = state.world.trade_route_get_is_land_route(trade_route);

	auto wage_origin = state.world.province_get_labor_price(capital_origin, labor::no_education);
	auto wage_target = state.world.province_get_labor_price(capital_target, labor::no_education);

	auto transport_cost_origin = is_land_route ? wage_origin : 0.f;
	auto transport_cost_target = is_land_route ? wage_target : 0.f;

	auto port_capacity = 0.f;

	transport_cost_origin = is_sea_route ? estimate_port_service_price(state, s_origin) : transport_cost_origin;
	transport_cost_target = is_sea_route ? estimate_port_service_price(state, s_target) : transport_cost_target;

	auto transport_cost =
		distance
		/ trade_distance_covered_by_pair_of_workers_per_unit_of_good
		* (transport_cost_origin + transport_cost_target) * effect_of_scale
		* route_capacity.transport_cost_multiplier;

	auto export_tariff = origin_apply_tariff ? effective_tariff_export_rate(state, controller_capital_origin, origin) : 0.f;
	auto import_tariff = target_apply_tariff ? effective_tariff_import_rate(state, controller_capital_target, target) : 0.f;

	auto price_origin = price(state, origin, cid);
	auto price_target = price(state, target, cid);

	if(controller_capital_origin == controller_capital_target) {
		return {
			.origin = origin,
			.target = target,
			.origin_nation = controller_capital_origin,
			.target_nation = controller_capital_target,

			.amount_origin = absolute_volume,
			.amount_target = import_amount,

			.tariff_origin = 0.f,
			.tariff_target = 0.f,

			.tariff_rate_origin = 0.f,
			.tariff_rate_target = 0.f,

			.price_origin = price_origin,
			.price_target = price_target,

			.transport_cost = transport_cost,
			.transportaion_loss = trade_good_loss_mult,
			.distance = distance,

			.payment_per_unit = price_origin * (1.f - import_profit_priority + economy::merchant_cut_domestic) + transport_cost,
			// the rest of payment is handled as satisfaction of generic demand
			.payment_received_per_unit = price_origin * (economy::merchant_cut_domestic - import_profit_priority)
		};
	} else {
		return {
			.origin = origin,
			.target = target,
			.origin_nation = controller_capital_origin,
			.target_nation = controller_capital_target,

			.amount_origin = absolute_volume,
			.amount_target = import_amount,
			.tariff_origin = absolute_volume * price_origin * export_tariff,
			.tariff_target = import_amount * price_target * import_tariff,

			.tariff_rate_origin = export_tariff,
			.tariff_rate_target = import_tariff,

			.price_origin = price_origin,
			.price_target = price_target,

			.transport_cost = transport_cost,
			.transportaion_loss = trade_good_loss_mult,
			.distance = distance,

			.payment_per_unit = price_origin
				* (1.f + export_tariff)
				* (1.f - import_profit_priority + economy::merchant_cut_foreign)
				+ price(state, target, cid)
				* import_tariff
				+ transport_cost,
			// the rest of payment is handled as satisfaction of generic demand
			.payment_received_per_unit = price_origin * (economy::merchant_cut_foreign - import_profit_priority)
		};
	}
}

trade_and_tariff<dcon::trade_route_id> explain_trade_route_commodity(
		sys::state const& state, dcon::trade_route_id trade_route,
		dcon::commodity_id cid) {
	auto const allocation = world_trade::clear_trade_shipments(state);
	return explain_trade_route_commodity(state, trade_route, cid, allocation);
}

// DO NOT USE OUTSIDE OF UI
std::vector<trade_breakdown_item> explain_national_tariff(sys::state& state, dcon::nation_id n, bool import_flag, bool export_flag) {
	std::vector<trade_breakdown_item> result;
	auto const shipment_allocation = world_trade::clear_trade_shipments(state);
	auto buffer_volume_per_nation = state.world.nation_make_vectorizable_float_buffer();
	auto buffer_tariff_per_nation = state.world.nation_make_vectorizable_float_buffer();

	state.world.for_each_commodity([&](dcon::commodity_id cid) {
		state.world.execute_serial_over_nation([&](auto nids) {
			buffer_volume_per_nation.set(nids, 0.f);
			buffer_tariff_per_nation.set(nids, 0.f);
		});

		state.world.for_each_trade_route([&](auto route) {
			if(!is_trade_route_relevant(state, route, n)) return;
			trade_and_tariff route_data = explain_trade_route_commodity(
				state, route, cid, shipment_allocation);

			if(import_flag && route_data.target_nation == n) {
				buffer_volume_per_nation.get(route_data.origin_nation) += route_data.amount_target;
				buffer_tariff_per_nation.get(route_data.origin_nation) += route_data.tariff_target;
			}

			if(export_flag && route_data.origin_nation == n) {
				buffer_volume_per_nation.get(route_data.target_nation) += route_data.amount_origin;
				buffer_tariff_per_nation.get(route_data.target_nation) += route_data.tariff_origin;
			}
		});

		state.world.for_each_nation([&](auto nid) {
			trade_breakdown_item item = {
				.trade_partner = nid,
				.commodity = cid,
				.traded_amount = buffer_volume_per_nation.get(nid),
				.tariff = buffer_tariff_per_nation.get(nid)
			};

			if(item.traded_amount == 0.f || item.tariff < 0.001f) {
				return;
			}

			result.push_back(item);
		});
	});

	return result;
}




}
