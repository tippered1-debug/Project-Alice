#include "catch.hpp"

#include "actors/organizations/organizations.hpp"
#include "actors/ownership.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/capital_projects.hpp"
#include "economy/causal_order.hpp"
#include "economy/exact_person_economy.hpp"
#include "economy/industrial_production.hpp"
#include "economy/physical/exact_person_freight.hpp"
#include "economy/physical/exact_person_goods.hpp"
#include "economy/physical/inventory.hpp"
#include "economy/physical/job_market.hpp"
#include "economy/physical/labor_dynamics.hpp"
#include "economy/physical/land.hpp"
#include "gamestate/system_state.hpp"
#include "military/land_forces.hpp"
#include "nations/strategic_statecraft.hpp"
#include "persons/exact_population.hpp"
#include "technology/technology_kernel.hpp"

#include <cmath>

namespace rural_economy_tests {

struct fixture {
	std::unique_ptr<sys::state> state = std::make_unique<sys::state>();
	dcon::province_id province{};
	dcon::nation_id nation{};
	dcon::market_id market{};
	dcon::site_id site{};
	dcon::site_id hub{};
	dcon::commodity_id settlement{};
	dcon::commodity_id grain{};
	dcon::factory_type_id grain_farm{};
	dcon::land_title_id title{};
	dcon::organization_id estate{};
	dcon::economic_actor_id estate_actor{};
	dcon::monetary_account_id estate_account{};
	dcon::factory_id farm{};
	uint64_t next_worker = 0;

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

		settlement = state->world.create_commodity();
		grain = state->world.create_commodity();
		state->world.commodity_set_cost(grain, 2.0f);
		state->world.market_resize_price(state->world.commodity_size());
		state->world.market_set_price(market, grain, 2.0f);

		grain_farm = state->world.create_factory_type();
		state->world.factory_type_set_output(grain_farm, grain);
		state->world.factory_type_set_output_amount(grain_farm, 3.0f);
		state->world.factory_type_set_base_workforce(grain_farm, 4);
		state->world.factory_type_set_farms_land(grain_farm, true);

		state->world.land_title_resize_suitability(state->world.commodity_size());
		title = economy::physical::land::create_title(*state, site, 50.0f); // ten reference holdings
		REQUIRE(title);
		REQUIRE(economy::physical::land::set_suitability(*state, title, grain, 1.0f));
		estate = actors::organizations::create_company(*state);
		estate_actor = actors::organizations::actor_for_organization(*state, estate);
		auto land_asset = state->world.create_asset();
		state->world.force_create_land_title_asset(title, land_asset);
		REQUIRE(actors::ownership::create_stake(*state, estate_actor, land_asset, 1.0f, 1.0f, 1.0f));
		estate_account = economy::accounts::open_account(*state, estate_actor, settlement);
		REQUIRE(economy::accounts::bootstrap_set_balance(*state, estate_account, 10000.0f));

		farm = economy::physical::land::create_farm(*state, title, grain_farm, estate);
		REQUIRE(farm);
		state->world.factory_set_payroll_settlement(farm, settlement);

		persons::exact_population::cell_descriptor residents;
		residents.source_population_cell = 700;
		residents.literal_count = 400;
		residents.bootstrap_base_day = state->current_date.to_raw_value() - 1;
		residents.demographic_seed = 700;
		residents.home_site = site;
		REQUIRE(persons::exact_population::register_synthetic_population_cell(*state, residents).result
			== persons::exact_population::status::created);
	}

	void hire(dcon::factory_id workplace, dcon::monetary_account_id payer) {
		auto employer = actors::organizations::operator_actor_for_factory(*state, workplace);
		auto offer = economy::physical::job_market::post_job_offer(*state, employer, workplace,
			site, 0, 1.0f, 1.0f, 1, payer, 1, state->current_date);
		REQUIRE(offer);
		auto before = economy::exact_person_economy::active_contracts_for_factory(*state, workplace).size();
		for(; next_worker < 400; next_worker += 4) {
			economy::exact_person_economy::person_key worker{700, next_worker};
			if(economy::exact_person_economy::submit_application(*state, worker, offer, state->current_date)) {
				next_worker += 4;
				break;
			}
		}
		economy::exact_person_economy::process_pending_applications(*state);
		REQUIRE(economy::exact_person_economy::active_contracts_for_factory(*state, workplace).size() == before + 1);
	}

	float operator_grain(dcon::economic_actor_id owner) const {
		float in_transit = 0.0f;
		state->world.for_each_shipment([&](dcon::shipment_id shipment) {
			auto relation = state->world.shipment_get_shipment_owner(shipment);
			if(state->world.shipment_get_commodity(shipment) == grain && relation
				&& state->world.shipment_owner_get_economic_actor(relation) == owner)
				in_transit += state->world.shipment_get_remaining_quantity(shipment);
		});
		return in_transit + economy::physical::inventory::quantity(*state, site, grain, owner)
			+ economy::physical::inventory::quantity(*state, hub, grain, owner);
	}
};

} // namespace rural_economy_tests

TEST_CASE("farm labor has diminishing returns on fixed land", "[economy][rural]") {
	rural_economy_tests::fixture f;
	REQUIRE(economy::physical::land::reference_labor(*f.state, f.farm) == Approx(10.0f));
	auto at = [&](float labor) { return economy::physical::land::output_for_labor(*f.state, f.farm, labor, f.state->current_date); };
	REQUIRE(at(0.0f) == Approx(0.0f));
	// At the reference ratio every worker yields the recipe's output.
	REQUIRE(at(10.0f) == Approx(30.0f));
	// Doubling labor on the same land yields less than double.
	REQUIRE(at(20.0f) > at(10.0f));
	REQUIRE(at(20.0f) < 2.0f * at(10.0f));
	REQUIRE(at(20.0f) == Approx(30.0f * std::pow(2.0f, 0.6f)));
}

TEST_CASE("a farm produces through hired labor and the common output path", "[economy][rural]") {
	rural_economy_tests::fixture f;
	REQUIRE(economy::industrial_production::produce_factory(*f.state, f.farm) == Approx(0.0f));
	f.hire(f.farm, f.estate_account);
	auto output = economy::industrial_production::produce_factory(*f.state, f.farm);
	REQUIRE(output > 0.0f);
	REQUIRE(output <= 3.0f * std::pow(10.0f, 0.4f) + 1.0e-3f);
	REQUIRE(f.operator_grain(f.estate_actor) == Approx(output));
}

TEST_CASE("land nobody may legally work stays idle", "[economy][rural]") {
	rural_economy_tests::fixture f;
	auto squatter = actors::organizations::create_company(*f.state);
	REQUIRE(actors::organizations::transfer_factory_operator(*f.state, squatter, f.farm));
	REQUIRE(economy::physical::land::output_for_labor(*f.state, f.farm, 10.0f, f.state->current_date) == Approx(0.0f));
}

TEST_CASE("a tenant farms leased land and owes the owner's share in kind", "[economy][rural]") {
	rural_economy_tests::fixture f;
	auto tenant = actors::organizations::create_company(*f.state);
	auto tenant_actor = actors::organizations::actor_for_organization(*f.state, tenant);
	auto tenant_account = economy::accounts::open_account(*f.state, tenant_actor, f.settlement);
	REQUIRE(economy::accounts::bootstrap_set_balance(*f.state, tenant_account, 5000.0f));
	REQUIRE(actors::organizations::transfer_factory_operator(*f.state, tenant, f.farm));
	// The owner may not farm land it has leased out; the tenant may not farm it before the lease.
	REQUIRE_FALSE(economy::physical::land::may_operate(*f.state, f.title, tenant_actor, f.state->current_date));
	auto lease = economy::physical::land::create_lease(*f.state, f.title, tenant_actor, f.settlement,
		0.0f, 0.4f, f.state->current_date, f.state->current_date + 365);
	REQUIRE(lease);
	REQUIRE(economy::physical::land::may_operate(*f.state, f.title, tenant_actor, f.state->current_date));
	REQUIRE_FALSE(economy::physical::land::may_operate(*f.state, f.title, f.estate_actor, f.state->current_date));
	REQUIRE_FALSE(economy::physical::land::create_lease(*f.state, f.title, tenant_actor, f.settlement,
		0.0f, 0.1f, f.state->current_date + 10, f.state->current_date + 20));

	f.hire(f.farm, tenant_account);
	auto output = economy::industrial_production::produce_factory(*f.state, f.farm);
	REQUIRE(output > 0.0f);
	REQUIRE(economy::physical::inventory::quantity(*f.state, f.site, f.grain, f.estate_actor) == Approx(0.4f * output));
	REQUIRE(f.operator_grain(tenant_actor) == Approx(0.6f * output));
}

TEST_CASE("cash rent settles each period and accrues what the tenant cannot pay", "[economy][rural]") {
	rural_economy_tests::fixture f;
	auto tenant = actors::organizations::create_company(*f.state);
	auto tenant_actor = actors::organizations::actor_for_organization(*f.state, tenant);
	auto tenant_account = economy::accounts::open_account(*f.state, tenant_actor, f.settlement);
	REQUIRE(economy::accounts::bootstrap_set_balance(*f.state, tenant_account, 100.0f));
	// 50 ha at 73 per hectare-year is 10 per day.
	auto lease = economy::physical::land::create_lease(*f.state, f.title, tenant_actor, f.settlement,
		73.0f, 0.0f, f.state->current_date, f.state->current_date + 365);
	REQUIRE(lease);
	auto owner_before = economy::accounts::balance(*f.state, f.estate_account);
	f.state->current_date += 10;
	economy::physical::land::settle_rents(*f.state);
	REQUIRE(economy::accounts::balance(*f.state, f.estate_account) == Approx(owner_before)); // not yet a full period
	f.state->current_date += 20;
	economy::physical::land::settle_rents(*f.state);
	// 300 was due, the tenant held 100.
	REQUIRE(economy::accounts::balance(*f.state, tenant_account) == Approx(0.0f));
	REQUIRE(economy::accounts::balance(*f.state, f.estate_account) == Approx(owner_before + 100.0f));
	REQUIRE(f.state->world.land_lease_get_unpaid_rent(lease) == Approx(200.0f));
}

TEST_CASE("farm recipes are calibrated from the needs of the people a worker feeds", "[economy][rural]") {
	rural_economy_tests::fixture f;
	auto farmers = f.state->world.create_pop_type();
	f.state->world.pop_type_resize_life_needs(f.state->world.commodity_size());
	f.state->world.pop_type_set_life_needs(farmers, f.grain, 2.5f);
	economy::physical::land::calibrate_farm_recipes(*f.state, farmers);
	// 1.5 x (4 people x 2.5 grain x cost 2) / cost 2 = 15 grain per worker.
	REQUIRE(f.state->world.factory_type_get_output_amount(f.grain_farm) == Approx(15.0f));
}

TEST_CASE("construction cannot create a farm without land", "[economy][rural]") {
	rural_economy_tests::fixture f;
	REQUIRE_FALSE(economy::capital_projects::create(*f.state, economy::capital_projects::project_kind::factory,
		f.estate_actor, f.estate, f.site, f.settlement, f.grain_farm));
	REQUIRE_FALSE(economy::physical::land::create_farm(*f.state, f.title, f.grain_farm, f.estate));
}
