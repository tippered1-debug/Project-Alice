#include "construction.hpp"
#include "capital_projects.hpp"
#include "physical/concrete_market.hpp"
#include "economy_stats.hpp"
#include "province_templates.hpp"
#include "text.hpp"
#include "money.hpp"
#include "province.hpp"

namespace economy {

void build_land_unit_construction_tooltip(
	sys::state& state,
	text::columnar_layout& contents,
	const dcon::province_land_construction_id conid
) {
	auto details = explain_land_unit_construction(state, conid);
	auto unit = state.world.province_land_construction_get_type(conid);
	auto& goods = state.military_definitions.unit_base_definitions[unit].build_cost;
	auto cgoods = economy::commodity_set{};

	{
		auto name = state.military_definitions.unit_base_definitions[unit].name;
		auto box = text::open_layout_box(contents, 0);
		text::add_to_layout_box(state, contents, box, name);
		text::close_layout_box(contents, box);
	}

	for(uint32_t i = 0; i < economy::commodity_set::set_size; ++i) {
		if(goods.commodity_type[i]) {
			auto box = text::open_layout_box(contents, 0);

			auto cid = goods.commodity_type[i];
			std::string padding = cid.index() < 10 ? "0" : "";
			std::string description = "@$" + padding + std::to_string(cid.index());
			text::add_unparsed_text_to_layout_box(state, contents, box, description);

			text::add_to_layout_box(state, contents, box, state.world.commodity_get_name(goods.commodity_type[i]));
			text::add_to_layout_box(state, contents, box, std::string_view{ ": " });
			text::add_to_layout_box(state, contents, box, text::fp_one_place{ cgoods.commodity_amounts[i] });
			text::add_to_layout_box(state, contents, box, std::string_view{ " / " });
			text::add_to_layout_box(state, contents, box, text::fp_one_place{ goods.commodity_amounts[i] * details.cost_multiplier });
			text::close_layout_box(contents, box);
		}
	}
}

void build_naval_unit_construction_tooltip(
	sys::state& state,
	text::columnar_layout& contents,
	const dcon::province_naval_construction_id conid
) {
	auto details = explain_naval_unit_construction(state, conid);
	auto unit = state.world.province_naval_construction_get_type(conid);
	auto& goods = state.military_definitions.unit_base_definitions[unit].build_cost;
	auto cgoods = capital_projects::consumed_materials(state, capital_projects::project_for(state, conid));

	{
		auto name = state.military_definitions.unit_base_definitions[unit].name;
		auto box = text::open_layout_box(contents, 0);
		text::add_to_layout_box(state, contents, box, name);
		text::close_layout_box(contents, box);
	}

	for(uint32_t i = 0; i < economy::commodity_set::set_size; ++i) {
		if(goods.commodity_type[i]) {
			auto box = text::open_layout_box(contents, 0);

			auto cid = goods.commodity_type[i];
			std::string padding = cid.index() < 10 ? "0" : "";
			std::string description = "@$" + padding + std::to_string(cid.index());
			text::add_unparsed_text_to_layout_box(state, contents, box, description);

			text::add_to_layout_box(state, contents, box, state.world.commodity_get_name(goods.commodity_type[i]));
			text::add_to_layout_box(state, contents, box, std::string_view{ ": " });
			text::add_to_layout_box(state, contents, box, text::fp_one_place{ cgoods.commodity_amounts[i] });
			text::add_to_layout_box(state, contents, box, std::string_view{ " / " });
			text::add_to_layout_box(state, contents, box, text::fp_one_place{ goods.commodity_amounts[i] * details.cost_multiplier });
			text::close_layout_box(contents, box);
		}
	}
}

economy::commodity_set calculate_factory_upgrade_goods_cost(
	sys::state& state,
	dcon::nation_id n,
	dcon::province_id pid,
	dcon::factory_type_id upgrade_target,
	bool is_pop_project
) {
	economy::commodity_set res{};
	auto& base_cost = state.world.factory_type_get_construction_costs(upgrade_target);
	float factory_mod = factory_build_cost_multiplier(state, n, pid, is_pop_project);


	for(uint32_t j = 0; j < commodity_set::set_size; ++j) {
		if(base_cost.commodity_type[j]) {
			res.commodity_type[j] = base_cost.commodity_type[j];
			res.commodity_amounts[j] = base_cost.commodity_amounts[j] * factory_mod;
		} else {
			break;
		}
	}

	return res;
}

economy::commodity_set calculate_factory_refit_goods_cost(sys::state& state, dcon::nation_id n, dcon::province_id pid, dcon::factory_type_id from, dcon::factory_type_id to) {
	auto& from_cost = state.world.factory_type_get_construction_costs(from);
	auto& to_cost = state.world.factory_type_get_construction_costs(to);

	float level = 1;

	for(auto f : state.world.province_get_factory_location(pid)) {
		if(f.get_factory().get_building_type() == from) {
			level = f.get_factory().get_size() / f.get_factory().get_building_type().get_base_workforce();
		}
	}


	// Refit cost = (to_cost) - (from_cost) + (0.1f * to_cost)
	float refit_mod = 1.0f + state.defines.alice_factory_refit_cost_modifier;

	economy::commodity_set res;

	// First take 110% of to_cost as a baseline
	if(!(n == state.local_player_nation && state.cheat_data.instant_industry)) {
		for(uint32_t j = 0; j < commodity_set::set_size; ++j) {
			if(to_cost.commodity_type[j]) {
				res.commodity_type[j] = to_cost.commodity_type[j];
				res.commodity_amounts[j] = to_cost.commodity_amounts[j] * refit_mod * level;
			} else {
				break;
			}
		}
	}

	// Substract from_cost to represent refit discount
	if(!(n == state.local_player_nation && state.cheat_data.instant_industry)) {
		for(uint32_t i = 0; i < commodity_set::set_size; ++i) {
			if(!from_cost.commodity_type[i]) {
				break;
			}

			auto from_amount = from_cost.commodity_amounts[i] * level;
			auto from_commodity = from_cost.commodity_type[i];

			for(uint32_t j = 0; j < commodity_set::set_size; ++j) {
				if(!res.commodity_type[j]) {
					break;
				}

				if(res.commodity_type[j] == from_commodity) {
					res.commodity_amounts[j] = std::max(res.commodity_amounts[j] - from_amount, 0.f);
				}
			}
		}
	}

	return res;
}
float calculate_factory_refit_money_cost(sys::state& state, dcon::nation_id n, dcon::province_id pid, dcon::factory_type_id from, dcon::factory_type_id to) {
	auto goods_cost = calculate_factory_refit_goods_cost(state, n, pid, from, to);

	float admin_eff = state.world.province_get_control_ratio(pid);
	float admin_cost_factor = 2.0f - admin_eff;
	float factory_mod = state.world.nation_get_modifier_values(n, sys::national_mod_offsets::factory_cost) + 1.0f;

	auto total = 0.0f;
	for(uint32_t i = 0; i < economy::commodity_set::set_size; i++) {
		if(goods_cost.commodity_type[i]) {
			total += economy::price(
				state,
				state.world.province_get_state_membership(pid),
				goods_cost.commodity_type[i]
			) * goods_cost.commodity_amounts[i]
				* factory_mod
				* admin_cost_factor;
		}
	}

	return total;
}

float global_province_construction_time_modifier(sys::state& state) {
	return state.defines.alice_province_building_build_time_mult;
}
float global_land_construction_time_modifier(sys::state& state) {
	return state.defines.alice_land_unit_build_time_mult;
}
float global_naval_construction_time_modifier(sys::state& state) {
	return state.defines.alice_naval_unit_build_time_mult;
}

float global_factory_construction_time_modifier(sys::state& state) {
	return state.defines.alice_factory_build_time_mult;
}

float build_cost_multiplier(sys::state& state, dcon::province_id location, bool is_pop_project) {
	float admin_eff = state.world.province_get_control_ratio(location);
	// make factories cheaper to make it a bit easier to get into industry and compensate for low control
	return (is_pop_project ? 1.f : 2.0f - admin_eff) * 0.5f;
}

float factory_build_cost_multiplier(sys::state& state, dcon::nation_id n, dcon::province_id location, bool is_pop_project) {
	return
		build_cost_multiplier(state, location, is_pop_project)
		* (std::max(0.f, state.world.nation_get_modifier_values(n, sys::national_mod_offsets::factory_cost)) + 1.0f)
		* (std::max(0.1f, state.world.nation_get_modifier_values(n, sys::national_mod_offsets::factory_owner_cost)));
}

float land_unit_construction_time(
	sys::state& state,
	dcon::unit_type_id utid,
	dcon::nation_id builder
) {
	return global_land_construction_time_modifier(state)
		* std::max(1, state.world.nation_get_unit_stats(builder, utid).build_time);
}

float naval_unit_construction_time(
	sys::state& state,
	dcon::unit_type_id utid,
	dcon::nation_id builder
) {
	return global_naval_construction_time_modifier(state)
		* std::max(1, state.world.nation_get_unit_stats(builder, utid).build_time);
}

float province_building_construction_time(
	sys::state& state,
	economy::province_building_type building_type
) {
	assert(0 <= int32_t(building_type) && int32_t(building_type) < int32_t(economy::max_building_types));
	return global_province_construction_time_modifier(state)
		* float(state.economy_definitions.building_definitions[int32_t(building_type)].time);
}

float factory_building_construction_time(
	sys::state& state, dcon::factory_type_id ftid, bool is_upgrade
) {
	return global_factory_construction_time_modifier(state)
		* float(state.world.factory_type_get_construction_time(ftid))
		* (is_upgrade ? 0.5f : 1.0f);
}

// it's registered as demand separately, do not add actual demand here
unit_construction_data explain_land_unit_construction(
	sys::state& state,
	dcon::province_land_construction_id construction
) {
	auto province = state.world.pop_get_province_from_pop_location(state.world.province_land_construction_get_pop(construction));
	auto owner = state.world.province_get_nation_from_province_ownership(province);
	auto local_zone = state.world.province_get_state_membership(province);
	auto unit_type = state.world.province_land_construction_get_type(construction);
	unit_construction_data result = {
		.can_be_advanced = (owner && state.world.province_get_nation_from_province_control(province) == owner),
		.construction_time = land_unit_construction_time(state, unit_type, owner),
		.cost_multiplier = build_cost_multiplier(state, province, false),
		.owner = owner,
		.market = state.world.state_instance_get_market_from_local_market(local_zone),
		.province = province,
		.unit_type = unit_type
	};
	return result;
}

unit_construction_data explain_naval_unit_construction(
	sys::state& state,
	dcon::province_naval_construction_id construction
) {
	auto province = state.world.province_naval_construction_get_province(construction);
	auto owner = state.world.province_get_nation_from_province_ownership(province);
	auto local_zone = state.world.province_get_state_membership(province);
	auto unit_type = state.world.province_naval_construction_get_type(construction);
	unit_construction_data result = {
		.can_be_advanced = (owner && state.world.province_get_nation_from_province_control(province) == owner),
		.construction_time = naval_unit_construction_time(state, unit_type, owner),
		.cost_multiplier = build_cost_multiplier(state, province, false),
		.owner = owner,
		.market = state.world.state_instance_get_market_from_local_market(local_zone),
		.province = province,
		.unit_type = unit_type
	};
	return result;
}

struct factory_construction_data {
	bool can_be_advanced;
	bool is_pop_project;
	bool is_upgrade;
	float construction_time;
	float cost_multiplier;
	dcon::nation_id owner;
	dcon::market_id market;
	dcon::province_id province;
	dcon::state_instance_id state_instance;
	dcon::factory_type_id building_type;
	dcon::factory_type_id refit_target;
};

factory_construction_data explain_factory_building_construction(
	sys::state& state,
	dcon::factory_construction_id construction
) {
	auto owner = state.world.factory_construction_get_nation(construction);
	auto province = state.world.factory_construction_get_province(construction);
	auto local_zone = state.world.province_get_state_membership(province);
	auto market = state.world.state_instance_get_market_from_local_market(local_zone);
	auto refit_target = state.world.factory_construction_get_refit_target(construction);
	auto building_type = state.world.factory_construction_get_type(construction);
	auto is_pop_project = state.world.factory_construction_get_is_pop_project(construction);
	auto is_upgrade = state.world.factory_construction_get_is_upgrade(construction);
	factory_construction_data result = {
		.can_be_advanced = (owner && state.world.province_get_nation_from_province_control(province) == owner),
		.is_pop_project = is_pop_project,
		.is_upgrade = is_upgrade,
		.construction_time = factory_building_construction_time(state, building_type, is_upgrade),
		.cost_multiplier = factory_build_cost_multiplier(state, owner, province, is_pop_project),
		.owner = owner,
		.market = state.world.state_instance_get_market_from_local_market(local_zone),
		.province = province,
		.state_instance = local_zone,
		.building_type = building_type,
		.refit_target = refit_target
	};
	return result;
}


float factory_construction_progress(sys::state& state, dcon::factory_construction_id construction) {
	return capital_projects::material_progress(state, capital_projects::project_for(state, construction));
}

void factory_construction_tooltip(sys::state& state, text::columnar_layout& contents, dcon::factory_construction_id fcid) {
	auto fat_fcid = dcon::fatten(state.world, fcid);
	auto ftid = state.world.factory_construction_get_type(fcid);

	auto details = explain_factory_building_construction(state, fcid);
	auto base_cost =
		details.refit_target
		? calculate_factory_refit_goods_cost(
			state, details.owner, details.province, details.building_type, details.refit_target
		)
		: state.world.factory_type_get_construction_costs(details.building_type);
	auto current_purchased = capital_projects::consumed_materials(state, capital_projects::project_for(state, fcid));

	float total = 0.0f;
	float purchased = 0.0f;

	float factory_mod = economy::factory_build_cost_multiplier(state, fat_fcid.get_nation(), fat_fcid.get_province(), fat_fcid.get_is_pop_project());
	float refit_discount = (fat_fcid.get_refit_target()) ? state.defines.alice_factory_refit_cost_modifier : 1.0f;
	auto market = state.world.state_instance_get_market_from_local_market(fat_fcid.get_province().get_state_membership());

	text::add_line(state, contents, state.world.factory_type_get_name(fat_fcid.get_type()));

	if(fat_fcid.get_is_pop_project()) {
		text::add_line(state, contents, "pop_project");
	} else {
		text::add_line(state, contents, "state_project");
	}

	text::add_line(state, contents, "alice_construction_cost");

	// List factory type construction costs
	for(uint32_t i = 0; i < economy::commodity_set::set_size; ++i) {
		auto cid = base_cost.commodity_type[i];
		if(!cid) break;

		auto commodity_price = state.world.market_get_price(market, cid);
		auto current = current_purchased.commodity_amounts[i];
		auto required = base_cost.commodity_amounts[i] * details.cost_multiplier;

		total += required * commodity_price;
		purchased += std::min(current, required) * commodity_price;

		auto left = std::max(0.f, required - current);

		text::substitution_map m;
		text::add_to_substitution_map(m, text::variable_type::name, state.world.commodity_get_name(cid));
		text::add_to_substitution_map(m, text::variable_type::val, text::fp_currency{ commodity_price });
		text::add_to_substitution_map(m, text::variable_type::need, text::fp_four_places{ left });
		text::add_to_substitution_map(m, text::variable_type::cost, text::fp_currency{ commodity_price * left });
		auto box = text::open_layout_box(contents, 0);
		text::localised_format_box(state, contents, box, "alice_factory_input_item", m);
		text::close_layout_box(contents, box);
	}

	text::add_line_break_to_layout(state, contents);
	auto progress = total > 0.0f ? purchased / total : 0.0f;
	text::add_line(state, contents, "alice_factory_construction_explain_3", text::variable_type::x, text::fp_currency{ purchased });
	text::add_line(state, contents, "alice_factory_construction_explain_4", text::variable_type::x, text::fp_currency{ total });
	text::add_line(state, contents, "alice_factory_construction_explain_5", text::variable_type::x, text::fp_percentage{ progress });
};



construction_spending_explanation explain_construction_spending(sys::state& state, dcon::nation_id n, float dedicated_budget) {
	construction_spending_explanation result{};
	for(auto const& row : capital_projects::export_requests(state)) {
		if(row.nation != n || state.world.capital_project_get_status(row.project) >= uint8_t(capital_projects::status::completed)) continue;
		++result.ongoing_projects;
		auto account = state.world.capital_project_get_monetary_account_from_capital_project_account(row.project);
		auto reserved = physical::concrete_market::reserved_bid_amount(state, account);
		result.estimated_spendings += reserved;
		if(row.building) result.province_buildings.push_back({row.building, reserved});
		if(row.naval) result.naval_units.push_back({row.naval, reserved});
		if(row.factory) result.factories.push_back({row.factory, reserved});
	}
	result.budget_limit_per_project = result.ongoing_projects ? std::max(0.0f, dedicated_budget) / float(result.ongoing_projects) : 0.0f;
	return result;
}
construction_spending_explanation explain_construction_spending_now(sys::state& s, dcon::nation_id n) { return explain_construction_spending(s, n, 0.0f); }
float estimate_construction_spending_from_budget(sys::state& s, dcon::nation_id n, float budget) { return std::min(std::max(0.0f, budget), explain_construction_spending(s, n, budget).estimated_spendings); }
float estimate_construction_spending(sys::state& s, dcon::nation_id n) { return explain_construction_spending_now(s, n).estimated_spendings; }
float estimate_private_construction_spendings(sys::state& s, dcon::nation_id n) {
	float total = 0.0f;
	s.world.for_each_capital_project([&](auto p) {
		if(s.world.capital_project_get_state_funded(p) || s.world.capital_project_get_status(p) >= uint8_t(capital_projects::status::completed)) return;
		auto site = s.world.capital_project_get_site_from_capital_project_site(p);
		if(s.world.province_get_nation_from_province_ownership(s.world.site_get_province_from_site_location(site)) != n) return;
		total += physical::concrete_market::reserved_bid_amount(s, s.world.capital_project_get_monetary_account_from_capital_project_account(p));
	});
	return total;
}
bool is_colony(sys::state& state, dcon::province_id p) {
	return state.world.province_get_is_colonial(p);
}

bool is_colony(sys::state& state, dcon::state_instance_id s) {
	return state.world.province_get_is_colonial(state.world.state_instance_get_capital(s));
}

// Check rules for factories in colonies: can a factory be built in provided province
bool can_build_factory_in_colony(sys::state& state, dcon::province_id p) {
	if(state.world.province_get_is_colonial(p) && state.defines.alice_allow_factories_in_colonies == 0.f) {
		return false;
	}

	return true;
}

// Check rules for factories in colonies: can a factory be built in provided state
bool can_build_factory_in_colony(sys::state& state, dcon::state_instance_id s) {
	auto p = state.world.state_instance_get_capital(s);
	return can_build_factory_in_colony(state, p);
}

// Check rules for factories in colonies: can this factory type be built in provided state
bool can_build_factory_type_in_colony(sys::state& state, dcon::state_instance_id s, dcon::factory_type_id ft) {
	if(!is_colony(state, s)) return true;
	return can_build_factory_in_colony(state, s) && state.world.factory_type_get_can_be_built_in_colonies(ft);
}

// Check rules for factories in colonies: : can this factory type be built in provided province
bool can_build_factory_type_in_colony(sys::state& state, dcon::province_id p, dcon::factory_type_id ft) {
	if(!is_colony(state, p)) return true;
	return can_build_factory_in_colony(state, p) && state.world.factory_type_get_can_be_built_in_colonies(ft);
}

}
