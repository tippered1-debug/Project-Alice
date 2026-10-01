#pragma once

#include "actors/organizations/organizations.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/causal_order.hpp"
#include "economy/exact_person_economy.hpp"
#include "economy/physical/exact_person_goods.hpp"
#include "economy/physical/exact_person_freight.hpp"
#include "economy/physical/job_market.hpp"
#include "economy/physical/labor_dynamics.hpp"
#include "gamestate/system_state.hpp"
#include "gamestate/game_scene.hpp"
#include "military/land_forces.hpp"
#include "nations/strategic_statecraft.hpp"
#include "persons/exact_population.hpp"
#include "technology/technology_kernel.hpp"

namespace canonical_consumer_tests {
struct fixture {
	std::unique_ptr<sys::state> state = std::make_unique<sys::state>();
	dcon::province_id province{};
	dcon::state_instance_id zone{};
	dcon::market_id market{};
	dcon::commodity_id settlement{}, output{};
	dcon::factory_type_id type{};
	dcon::factory_id factory{};
	dcon::site_id site{};
	dcon::economic_actor_id employer{};
	dcon::monetary_account_id payer{};
	fixture() {
		state->current_date = sys::date{30000};
		persons::exact_population::initialize_empty_store(*state);
		economy::causal_order::initialize_empty_store(*state);
		economy::exact_person_economy::initialize_empty_store(*state);
		economy::physical::exact_person_goods::initialize_empty_store(*state);
		economy::physical::exact_person_freight::initialize_empty_store(*state);
		economy::physical::labor_dynamics::initialize_empty_store(*state);
		military::land_forces::initialize_empty_store(*state);
		technology::kernel::initialize_empty_store(*state);
				province = state->world.create_province();
		auto nation = state->world.create_nation();
		nations::strategic_statecraft::initialize(*state);
		state->transformation_government_state.resize(state->world.nation_size());
		state->transformation_legislation_state.resize(state->world.nation_size());
		state->world.province_set_nation_from_province_ownership(province, nation);
		state->world.province_set_mid_point_b(province, {1.0f, 0.0f, 0.0f});
		zone = state->world.create_state_instance();
		market = state->world.create_market();
		state->world.state_instance_set_market_from_local_market(zone, market);
		state->world.market_set_zone_from_local_market(market, zone);
		state->world.province_set_state_membership(province, zone);
		site = state->world.create_site();
		state->world.force_create_site_location(site, province);
		settlement = state->world.create_commodity();
		output = state->world.create_commodity();
		state->world.commodity_set_cost(output, 10.0f);
		state->world.market_resize_price(state->world.commodity_size());
		state->world.market_set_price(market, output, 10.0f);
		type = state->world.create_factory_type();
		state->world.factory_type_set_output(type, output);
		state->world.factory_type_set_output_amount(type, 1.0f);
		state->world.factory_type_set_base_workforce(type, 1.0f);
		factory = state->world.create_factory();
		state->world.factory_set_building_type(factory, type);
		state->world.factory_set_size(factory, 1.0f);
		state->world.factory_set_productive_capacity(factory, 1.0f);
		state->world.factory_set_productivity_factor(factory, 1.0f);
		state->world.force_create_factory_location(factory, province);
		state->world.force_create_factory_site(factory, site);
		auto company = actors::organizations::create_company(*state);
		REQUIRE(actors::organizations::bind_factory_operator(*state, company, factory));
		employer = actors::organizations::actor_for_organization(*state, company);
		payer = economy::accounts::open_account(*state, employer, settlement);
		REQUIRE(economy::accounts::bootstrap_set_balance(*state, payer, 10000.0f));
		state->world.factory_set_payroll_settlement(factory, settlement);
	}
	dcon::job_offer_id offer(uint32_t openings, float wage) {
		return economy::physical::job_market::post_job_offer(*state, employer, factory,
			site, 0, 1.0f, wage, 1, payer, openings, state->current_date);
	}
};
}
