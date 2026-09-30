#include "catch.hpp"

#include "actors/organizations/organizations.hpp"
#include "actors/ownership.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/causal_order.hpp"
#include "economy/exact_person_economy.hpp"
#include "economy/firm_agency.hpp"
#include "economy/industrial_production.hpp"
#include "economy/physical/concrete_market.hpp"
#include "economy/physical/deposits.hpp"
#include "economy/physical/exact_person_freight.hpp"
#include "economy/physical/exact_person_goods.hpp"
#include "economy/physical/extraction.hpp"
#include "economy/physical/inventory.hpp"
#include "economy/physical/job_market.hpp"
#include "economy/physical/labor_dynamics.hpp"
#include "gamestate/system_state.hpp"
#include "military/land_forces.hpp"
#include "nations/strategic_statecraft.hpp"
#include "persons/exact_population.hpp"
#include "persons/persons.hpp"
#include "technology/technology_kernel.hpp"

namespace primary_production_tests {

struct fixture {
	std::unique_ptr<sys::state> state = std::make_unique<sys::state>();
	dcon::province_id province{};
	dcon::nation_id nation{};
	dcon::market_id market{};
	dcon::site_id site{};
	dcon::site_id hub{};
	dcon::commodity_id settlement{};
	dcon::commodity_id ore{};
	dcon::factory_type_id mine{};
	dcon::resource_deposit_id deposit{};
	dcon::organization_id company{};
	dcon::economic_actor_id operator_actor{};
	dcon::monetary_account_id payer{};
	dcon::factory_id plant{};
	uint64_t next_worker = 0;

	explicit fixture(float reserves = 100.0f, float daily_capacity = 4.0f, float grade = 1.0f) {
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
		nation = state->world.create_nation();
		nations::strategic_statecraft::initialize(*state);
		state->transformation_government_state.resize(state->world.nation_size());
		state->transformation_legislation_state.resize(state->world.nation_size());
		state->world.province_set_nation_from_province_ownership(province, nation);
		state->world.province_set_mid_point_b(province, {1.0f, 0.0f, 0.0f});
		auto zone = state->world.create_state_instance();
		market = state->world.create_market();
		state->world.state_instance_set_market_from_local_market(zone, market);
		state->world.market_set_zone_from_local_market(market, zone);
		state->world.province_set_state_membership(province, zone);
		site = state->world.create_site();
		state->world.force_create_site_location(site, province);
		hub = state->world.create_site();
		state->world.force_create_site_location(hub, province);
		state->world.force_create_market_hub_site(market, hub);

		settlement = state->world.create_commodity(); // index 0 is the settlement unit
		ore = state->world.create_commodity();
		state->world.commodity_set_is_mine(ore, true);
		state->world.commodity_set_cost(ore, 10.0f);
		state->world.market_resize_price(state->world.commodity_size());
		state->world.market_set_price(market, ore, 10.0f);

		mine = state->world.create_factory_type();
		state->world.factory_type_set_output(mine, ore);
		state->world.factory_type_set_output_amount(mine, 2.0f);
		state->world.factory_type_set_base_workforce(mine, 4);
		state->world.factory_type_set_extracts_deposit(mine, true);

		deposit = economy::physical::deposits::create_deposit(*state, site, ore, reserves, reserves,
			grade, daily_capacity, daily_capacity, 0);
		REQUIRE(deposit);
		company = actors::organizations::create_company(*state);
		operator_actor = actors::organizations::actor_for_organization(*state, company);
		REQUIRE(actors::organizations::bind_deposit_operator(*state, company, deposit));
		auto subsoil = state->world.create_asset();
		state->world.force_create_resource_deposit_asset(deposit, subsoil);
		REQUIRE(actors::ownership::create_stake(*state, operator_actor, subsoil, 1.0f, 1.0f, 1.0f));

		plant = economy::physical::extraction::create_enterprise(*state, deposit, mine, company);
		REQUIRE(plant);
		payer = economy::accounts::open_account(*state, operator_actor, settlement);
		REQUIRE(economy::accounts::bootstrap_set_balance(*state, payer, 10000.0f));
		state->world.factory_set_payroll_settlement(plant, settlement);

		// Residents who can be hired one at a time without a DCON population row.
		persons::exact_population::cell_descriptor residents;
		residents.source_population_cell = 900;
		residents.literal_count = 400;
		residents.bootstrap_base_day = state->current_date.to_raw_value() - 1;
		residents.demographic_seed = 900;
		residents.home_site = site;
		REQUIRE(persons::exact_population::register_synthetic_population_cell(*state, residents).result
			== persons::exact_population::status::created);
	}

	void hire(dcon::factory_id workplace = dcon::factory_id{}) {
		if(!workplace) workplace = plant;
		auto employer = actors::organizations::operator_actor_for_factory(*state, workplace);
		auto offer = economy::physical::job_market::post_job_offer(*state, employer, workplace,
			site, 0, 1.0f, 1.0f, 1, payer, 1, state->current_date);
		REQUIRE(offer);
		auto before = economy::exact_person_economy::active_contracts_for_factory(*state, workplace).size();
		for(; next_worker < 400; next_worker += 4) {
			economy::exact_person_economy::person_key worker{900, next_worker};
			if(economy::exact_person_economy::submit_application(*state, worker, offer, state->current_date)) {
				next_worker += 4;
				break;
			}
		}
		economy::exact_person_economy::process_pending_applications(*state);
		REQUIRE(economy::exact_person_economy::active_contracts_for_factory(*state, workplace).size() == before + 1);
	}

	float remaining() const {
		return state->world.resource_deposit_get_remaining_recoverable_reserves(deposit);
	}

	// Output leaves the site at once: it is either in transit to the hub or
	// stocked there. Both are owned by the operator.
	float operator_output_outside_deposit() const {
		float in_transit = 0.0f;
		state->world.for_each_shipment([&](dcon::shipment_id shipment) {
			if(state->world.shipment_get_commodity(shipment) == ore)
				in_transit += state->world.shipment_get_remaining_quantity(shipment);
		});
		return in_transit
			+ economy::physical::inventory::quantity(*state, site, ore, operator_actor)
			+ economy::physical::inventory::quantity(*state, hub, ore, operator_actor);
	}
};

} // namespace primary_production_tests

TEST_CASE("extraction plant is an ordinary factory bound to one deposit", "[economy][primary]") {
	primary_production_tests::fixture f;
	REQUIRE(economy::physical::extraction::extracts_deposit(*f.state, f.plant));
	REQUIRE(economy::physical::extraction::deposit_for_enterprise(*f.state, f.plant) == f.deposit);
	REQUIRE(economy::physical::extraction::enterprise_for_deposit(*f.state, f.deposit) == f.plant);
	REQUIRE(f.state->world.factory_get_site_from_factory_site(f.plant) == f.site);
	REQUIRE(actors::organizations::operator_organization_for_factory(*f.state, f.plant) == f.company);
	// Full capacity extracts exactly the deposit's daily capacity.
	REQUIRE(f.state->world.factory_get_productive_capacity(f.plant) * 2.0f == Approx(4.0f));
	REQUIRE_FALSE(economy::physical::extraction::create_enterprise(*f.state, f.deposit, f.mine, f.company));
}

TEST_CASE("extraction produces nothing without labor", "[economy][primary]") {
	primary_production_tests::fixture f;
	REQUIRE(economy::industrial_production::produce_factory(*f.state, f.plant) == Approx(0.0f));
	REQUIRE(f.remaining() == Approx(100.0f));
	REQUIRE(f.operator_output_outside_deposit() == Approx(0.0f));
}

TEST_CASE("extracted output depletes reserves by exactly what reaches the market path", "[economy][primary]") {
	primary_production_tests::fixture f;
	f.hire();
	auto output = economy::industrial_production::produce_factory(*f.state, f.plant);
	REQUIRE(output > 0.0f);
	REQUIRE(output <= 2.0f + 1.0e-4f); // one worker, two tonnes per labor unit
	REQUIRE(f.remaining() == Approx(100.0f - output));
	REQUIRE(f.operator_output_outside_deposit() == Approx(output));
	// The same day cannot extract past the deposit's daily capacity.
	f.hire();
	f.hire();
	auto second = economy::industrial_production::produce_factory(*f.state, f.plant);
	REQUIRE(output + second <= 4.0f + 1.0e-4f);
	REQUIRE(f.remaining() == Approx(100.0f - output - second));
}

TEST_CASE("grade scales output per worker and reserve depletion together", "[economy][primary]") {
	primary_production_tests::fixture f(100.0f, 4.0f, 0.5f);
	f.hire();
	auto output = economy::industrial_production::produce_factory(*f.state, f.plant);
	REQUIRE(output > 0.0f);
	REQUIRE(output <= 1.0f + 1.0e-4f);
	REQUIRE(f.remaining() == Approx(100.0f - output));
}

TEST_CASE("a depleted deposit stops its plant", "[economy][primary]") {
	primary_production_tests::fixture f(1.0f, 4.0f, 1.0f);
	f.hire();
	f.hire();
	f.hire();
	float total = 0.0f;
	for(int day = 0; day < 60 && f.remaining() > 0.0f; ++day) {
		auto before = f.remaining();
		auto output = economy::industrial_production::produce_factory(*f.state, f.plant);
		REQUIRE(output >= 0.0f);
		REQUIRE(f.remaining() == Approx(std::max(0.0f, before - output)).margin(1.0e-5f));
		total += output;
		f.state->current_date += 1;
	}
	REQUIRE(total == Approx(1.0f));
	REQUIRE(f.remaining() == Approx(0.0f));
	REQUIRE(f.operator_output_outside_deposit() == Approx(1.0f));
	REQUIRE(f.state->world.resource_deposit_get_status(f.deposit)
		== uint8_t(economy::physical::extraction::deposit_status::depleted));
	f.state->current_date += 1;
	REQUIRE(economy::industrial_production::produce_factory(*f.state, f.plant) == Approx(0.0f));
	REQUIRE(economy::physical::extraction::daily_ceiling(*f.state, f.plant, f.state->current_date) == Approx(0.0f));
}

TEST_CASE("a plant whose operator does not control the deposit extracts nothing", "[economy][primary]") {
	primary_production_tests::fixture f;
	auto other = actors::organizations::create_company(*f.state);
	REQUIRE(actors::organizations::transfer_factory_operator(*f.state, other, f.plant));
	f.operator_actor = actors::organizations::actor_for_organization(*f.state, other);
	f.payer = economy::accounts::open_account(*f.state, f.operator_actor, f.settlement);
	REQUIRE(economy::accounts::bootstrap_set_balance(*f.state, f.payer, 10000.0f));
	f.hire();
	REQUIRE(economy::physical::extraction::daily_ceiling(*f.state, f.plant, f.state->current_date) == Approx(0.0f));
	REQUIRE(economy::industrial_production::produce_factory(*f.state, f.plant) == Approx(0.0f));
	REQUIRE(f.remaining() == Approx(100.0f));

	// A right held by the plant's operator grants access, capped by its quota.
	auto right = f.state->world.create_resource_extraction_right();
	f.state->world.resource_extraction_right_set_valid_from(right, f.state->current_date);
	f.state->world.resource_extraction_right_set_valid_until(right, f.state->current_date + 100);
	f.state->world.resource_extraction_right_set_max_daily_quantity(right, 1.0f);
	f.state->world.resource_extraction_right_set_status(right, 0);
	f.state->world.force_create_resource_extraction_right_deposit(right, f.deposit);
	f.state->world.force_create_resource_extraction_right_holder(right, f.operator_actor);
	REQUIRE(economy::physical::extraction::daily_ceiling(*f.state, f.plant, f.state->current_date) == Approx(1.0f));
	auto output = economy::industrial_production::produce_factory(*f.state, f.plant);
	REQUIRE(output > 0.0f);
	REQUIRE(output <= 1.0f + 1.0e-4f);
}

TEST_CASE("an extraction recipe without a bound deposit produces nothing", "[economy][primary]") {
	primary_production_tests::fixture f;
	auto unbound = f.state->world.create_factory();
	f.state->world.factory_set_building_type(unbound, f.mine);
	f.state->world.factory_set_size(unbound, 4.0f);
	f.state->world.factory_set_productive_capacity(unbound, 1.0f);
	f.state->world.factory_set_productivity_factor(unbound, 1.0f);
	f.state->world.force_create_factory_location(unbound, f.province);
	f.state->world.force_create_factory_site(unbound, f.site);
	REQUIRE(actors::organizations::bind_factory_operator(*f.state, f.company, unbound));
	f.state->world.factory_set_payroll_settlement(unbound, f.settlement);
	f.hire(unbound);
	REQUIRE(economy::industrial_production::produce_factory(*f.state, unbound) == Approx(0.0f));
	REQUIRE(f.remaining() == Approx(100.0f));
}

TEST_CASE("open job offers recruit unemployed local residents", "[economy][primary][labor]") {
	primary_production_tests::fixture f;
	auto pop_type = f.state->world.create_pop_type();
	auto pop = f.state->world.create_pop();
	f.state->world.force_create_pop_location(pop, f.province);
	f.state->world.pop_set_poptype(pop, pop_type);
	f.state->world.pop_set_culture(pop, f.state->world.create_culture());
	f.state->world.pop_set_religion(pop, f.state->world.create_religion());
	f.state->world.pop_set_size(pop, 5.0f);
	REQUIRE(persons::exact_population::register_population_cell(*f.state, pop, f.site).result
		== persons::exact_population::status::created);

	auto offer = economy::physical::job_market::post_job_offer(*f.state, f.operator_actor, f.plant,
		f.site, 0, 1.0f, 1.0f, 1, f.payer, 2, f.state->current_date);
	REQUIRE(offer);
	economy::physical::job_market::recruit_local_applicants(*f.state);
	economy::exact_person_economy::process_pending_applications(*f.state);
	auto hired = economy::exact_person_economy::active_contracts_for_factory(*f.state, f.plant);
	REQUIRE(hired.size() == 2);
	for(auto id : hired) {
		auto contract = economy::exact_person_economy::contract(*f.state, id);
		REQUIRE(contract);
		REQUIRE(persons::is_source_workforce_anchor(*f.state, contract->worker));
		REQUIRE(persons::current_population(*f.state, contract->worker) == pop);
	}
	// Filled offers recruit no one else.
	economy::physical::job_market::recruit_local_applicants(*f.state);
	economy::exact_person_economy::process_pending_applications(*f.state);
	REQUIRE(economy::exact_person_economy::active_contracts_for_factory(*f.state, f.plant).size() == 2);

	// The hired residents are the plant's labor.
	REQUIRE(economy::exact_person_economy::labor_supplied_to_factory(*f.state, f.plant) == Approx(2.0f));
	auto output = economy::industrial_production::produce_factory(*f.state, f.plant);
	REQUIRE(output > 0.0f);
	REQUIRE(f.remaining() == Approx(100.0f - output));
}

TEST_CASE("residents of another province are not recruited", "[economy][primary][labor]") {
	primary_production_tests::fixture f;
	auto elsewhere = f.state->world.create_province();
	f.state->world.province_set_nation_from_province_ownership(elsewhere, f.nation);
	auto home = f.state->world.create_site();
	f.state->world.force_create_site_location(home, elsewhere);
	auto pop = f.state->world.create_pop();
	f.state->world.force_create_pop_location(pop, elsewhere);
	f.state->world.pop_set_poptype(pop, f.state->world.create_pop_type());
	f.state->world.pop_set_culture(pop, f.state->world.create_culture());
	f.state->world.pop_set_religion(pop, f.state->world.create_religion());
	f.state->world.pop_set_size(pop, 5.0f);
	REQUIRE(persons::exact_population::register_population_cell(*f.state, pop, home).result
		== persons::exact_population::status::created);
	REQUIRE(economy::physical::job_market::post_job_offer(*f.state, f.operator_actor, f.plant,
		f.site, 0, 1.0f, 1.0f, 1, f.payer, 2, f.state->current_date));
	economy::physical::job_market::recruit_local_applicants(*f.state);
	economy::exact_person_economy::process_pending_applications(*f.state);
	REQUIRE(economy::exact_person_economy::active_contracts_for_factory(*f.state, f.plant).empty());
}
