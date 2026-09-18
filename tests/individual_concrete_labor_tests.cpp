#include "catch.hpp"

#include "actors/organizations/organizations.hpp"
#include "compat/alice/legacy_bridge.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/industrial_production.hpp"
#include "economy/payroll.hpp"
#include "economy/physical/concrete_labor.hpp"
#include "economy/physical/job_market.hpp"
#include "persons/persons.hpp"

namespace individual_concrete_labor_tests {
struct fixture {
	std::unique_ptr<sys::state> state = std::make_unique<sys::state>();
	dcon::province_id province{};
	dcon::state_instance_id zone{};
	dcon::market_id market{};
	dcon::commodity_id settlement{};
	dcon::commodity_id output{};
	dcon::factory_type_id type{};
	dcon::factory_id factory{};
	dcon::site_id site{};
	dcon::economic_actor_id employer{};
	dcon::monetary_account_id payer{};

	fixture() {
		state->current_date = sys::date{0};
		province = state->world.create_province();
		zone = state->world.create_state_instance();
		market = state->world.create_market();
		state->world.state_instance_set_market_from_local_market(zone, market);
		state->world.market_set_zone_from_local_market(market, zone);
		state->world.province_set_state_membership(province, zone);
		settlement = state->world.create_commodity();
		output = state->world.create_commodity();
		state->world.commodity_set_is_local(output, false);
		state->world.commodity_set_money_rgo(output, false);
		state->world.market_resize_price(state->world.commodity_size());
		state->world.market_set_price(market, output, 10.0f);
		type = state->world.create_factory_type();
		state->world.factory_type_set_output(type, output);
		state->world.factory_type_set_output_amount(type, 1.0f);
		state->world.factory_type_set_base_workforce(type, 3.0f);
		factory = make_factory();
		compat::alice::bootstrap_factory_sites(*state);
		site = world::site::site_for_factory(*state, factory);
		auto company = actors::organizations::create_company(*state);
		REQUIRE(actors::organizations::bind_factory_operator(*state, company, factory));
		employer = actors::organizations::actor_for_organization(*state, company);
		payer = economy::accounts::open_account(*state, employer, settlement);
		REQUIRE(economy::accounts::bootstrap_set_balance(*state, payer, 1000.0f));
		state->world.factory_set_canonical_production(factory, 1);
		state->world.factory_set_payroll_settlement(factory, settlement);
	}

	dcon::factory_id make_factory() {
		auto result = state->world.create_factory();
		state->world.factory_set_building_type(result, type);
		state->world.factory_set_size(result, 3.0f);
		state->world.factory_set_productive_capacity(result, 3.0f);
		state->world.factory_set_productivity_factor(result, 1.0f);
		state->world.force_create_factory_location(result, province);
		return result;
	}

	dcon::employment_contract_id hire(float capacity = 1.0f, float wage = 0.0f,
		dcon::factory_id workplace_factory = dcon::factory_id{}) {
		if(!workplace_factory) workplace_factory = factory;
		auto person = persons::create_person(*state, sys::date{0});
		auto worker_account = economy::accounts::open_account(*state,
			persons::actor_for_person(*state, person), settlement);
		return economy::physical::concrete_labor::create_employment_contract(*state, person, employer,
			workplace_factory, world::site::site_for_factory(*state, workplace_factory), 0,
			capacity, wage, 1, payer, worker_account, state->current_date);
	}

	dcon::person_id person() {
		return persons::create_person(*state, sys::date{0});
	}

	dcon::job_offer_id offer(uint32_t openings, float wage = 1.0f,
		dcon::factory_id workplace_factory = dcon::factory_id{}, sys::date expires_on = {}) {
		if(!workplace_factory) workplace_factory = factory;
		return economy::physical::job_market::post_job_offer(*state, employer, workplace_factory,
			world::site::site_for_factory(*state, workplace_factory), 0, 1.0f, wage, 1,
			payer, openings, state->current_date, expires_on);
	}
};
}

TEST_CASE("employment contracts use existing person identity and factory index", "[economy][labor][concrete]") {
	individual_concrete_labor_tests::fixture f;
	auto contract = f.hire(1.0f);
	REQUIRE(contract);
	auto person = f.state->world.employment_contract_get_person_from_employment_contract_person(contract);
	REQUIRE(person);
	REQUIRE(f.state->world.employment_contract_get_economic_actor_from_employment_contract_employer(contract) == f.employer);
	REQUIRE(f.state->world.employment_contract_get_factory_from_employment_contract_factory(contract) == f.factory);
	REQUIRE(f.state->world.employment_contract_get_site_from_employment_contract_site(contract) == f.site);
	REQUIRE(economy::physical::concrete_labor::active_workers_for_factory(*f.state, f.factory).front() == person);
}

TEST_CASE("concrete labor sums workers, terminates deterministically, and isolates factories", "[economy][labor][concrete]") {
	individual_concrete_labor_tests::fixture f;
	auto first = f.hire(1.0f);
	auto second = f.hire(1.0f);
	auto third = f.hire(1.0f);
	REQUIRE((bool(first) && bool(second) && bool(third)));
	REQUIRE(economy::physical::concrete_labor::labor_supplied_to_factory(*f.state, f.factory) == Approx(3.0f));
	REQUIRE(economy::physical::concrete_labor::end_employment_contract(*f.state, second,
		economy::physical::concrete_labor::contract_status::ended, f.state->current_date));
	REQUIRE(economy::physical::concrete_labor::labor_supplied_to_factory(*f.state, f.factory) == Approx(2.0f));
	REQUIRE(economy::physical::concrete_labor::labor_supplied_to_factory(*f.state, f.factory)
		== economy::physical::concrete_labor::labor_supplied_to_factory(*f.state, f.factory));
	auto other = f.make_factory();
	auto other_company = actors::organizations::create_company(*f.state);
	REQUIRE(actors::organizations::bind_factory_operator(*f.state, other_company, other));
	REQUIRE(economy::physical::concrete_labor::labor_supplied_to_factory(*f.state, other) == Approx(0.0f));
	(void)third;
}

TEST_CASE("canonical production is constrained by concrete labor, not aggregate availability", "[economy][labor][concrete]") {
	individual_concrete_labor_tests::fixture f;
	f.hire(1.0f);
	for(auto kind : {economy::labor::no_education, economy::labor::basic_education, economy::labor::high_education})
		f.state->world.province_set_labor_demand_satisfaction(f.province, kind, 1.0f);
	auto first_output = economy::industrial_production::produce_factory(*f.state, f.factory);
	f.state->current_date += 1;
	for(auto kind : {economy::labor::no_education, economy::labor::basic_education, economy::labor::high_education})
		f.state->world.province_set_labor_demand_satisfaction(f.province, kind, 0.0f);
	auto second_output = economy::industrial_production::produce_factory(*f.state, f.factory);
	REQUIRE(first_output == Approx(1.0f));
	REQUIRE(second_output == Approx(first_output));
}

TEST_CASE("canonical factory without contracts has zero canonical payroll", "[economy][labor][concrete]") {
	individual_concrete_labor_tests::fixture f;
	REQUIRE(f.state->world.factory_get_canonical_production(f.factory));
	REQUIRE(economy::physical::concrete_labor::contracts_for_factory(*f.state, f.factory).empty());
	REQUIRE_FALSE(f.state->world.province_get_province_labor_clearing(f.province));
	REQUIRE(economy::industrial_production::produce_factory(*f.state, f.factory) == Approx(0.0f));
	economy::payroll::settle_factory(*f.state, f.factory, 0.0f, 0.0f);
	REQUIRE_FALSE(f.state->world.province_get_province_labor_clearing(f.province));
	REQUIRE(f.state->world.transaction_size() == 0);
	REQUIRE(f.state->world.obligation_size() == 0);
	REQUIRE(f.state->world.payroll_event_size() == 0);
}

TEST_CASE("concrete wage settlement uses exact receiver and records arrears", "[economy][labor][concrete]") {
	individual_concrete_labor_tests::fixture f;
	auto contract = f.hire(1.0f, 100.0f);
	auto person = f.state->world.employment_contract_get_person_from_employment_contract_person(contract);
	auto worker_actor = persons::actor_for_person(*f.state, person);
	auto exact_worker = f.state->world.employment_contract_get_monetary_account_from_employment_contract_worker_account(contract);
	auto other_worker = economy::accounts::open_account(*f.state, worker_actor, f.settlement);
	REQUIRE(exact_worker != other_worker);
	REQUIRE(economy::physical::concrete_labor::wage_due(*f.state, contract) == Approx(100.0f));
	f.state->world.monetary_account_set_balance(f.payer, 500.0f);
	economy::payroll::settle_factory(*f.state, f.factory, 1.0f, 1.0f);
	REQUIRE(economy::accounts::balance(*f.state, f.payer) == Approx(400.0f));
	REQUIRE(economy::accounts::balance(*f.state, exact_worker) == Approx(100.0f));
	REQUIRE(economy::accounts::balance(*f.state, other_worker) == Approx(0.0f));

	f.state->current_date += 1;
	f.state->world.monetary_account_set_balance(f.payer, 0.0f);
	auto transactions_before = f.state->world.transaction_size();
	economy::payroll::settle_factory(*f.state, f.factory, 1.0f, 1.0f);
	REQUIRE(economy::accounts::balance(*f.state, f.payer) == Approx(0.0f));
	REQUIRE(economy::accounts::balance(*f.state, exact_worker) == Approx(100.0f));
	REQUIRE(f.state->world.transaction_size() == transactions_before);
	REQUIRE(economy::relations::outstanding_between(*f.state, f.employer, worker_actor, f.settlement) == Approx(100.0f));
}

TEST_CASE("factory posts concrete job and accepted application creates exact contract", "[economy][job_market]") {
	individual_concrete_labor_tests::fixture f;
	auto job = f.offer(1, 25.0f);
	auto worker = f.person();
	REQUIRE((job && worker));
	auto worker_actor = persons::actor_for_person(*f.state, worker);
	auto first_account = economy::accounts::open_account(*f.state, worker_actor, f.settlement);
	auto second_account = economy::accounts::open_account(*f.state, worker_actor, f.settlement);
	auto expected_worker_account = first_account.index() < second_account.index() ? first_account : second_account;
	auto application = economy::physical::job_market::submit_job_application(*f.state, worker, job, f.state->current_date);
	REQUIRE(application);
	economy::physical::job_market::process_pending_applications(*f.state);
	REQUIRE(f.state->world.job_application_get_status(application)
		== uint8_t(economy::physical::job_market::application_status::accepted));
	auto contracts = economy::physical::concrete_labor::contracts_for_factory(*f.state, f.factory);
	REQUIRE(contracts.size() == 1);
	auto contract = contracts.front();
	REQUIRE(f.state->world.employment_contract_get_person_from_employment_contract_person(contract) == worker);
	REQUIRE(f.state->world.employment_contract_get_economic_actor_from_employment_contract_employer(contract) == f.employer);
	REQUIRE(f.state->world.employment_contract_get_factory_from_employment_contract_factory(contract) == f.factory);
	REQUIRE(f.state->world.employment_contract_get_site_from_employment_contract_site(contract) == f.site);
	REQUIRE(f.state->world.employment_contract_get_monetary_account_from_employment_contract_payer_account(contract) == f.payer);
	REQUIRE(f.state->world.employment_contract_get_monetary_account_from_employment_contract_worker_account(contract)
		== expected_worker_account);
	REQUIRE(economy::physical::concrete_labor::labor_supplied_to_factory(*f.state, f.factory) == Approx(1.0f));
	REQUIRE(f.state->world.job_offer_get_openings(job) == 0);
}

TEST_CASE("job matching fills openings in stable application order", "[economy][job_market]") {
	individual_concrete_labor_tests::fixture f;
	auto job = f.offer(2);
	auto first_person = f.person();
	auto second_person = f.person();
	auto third_person = f.person();
	auto first = economy::physical::job_market::submit_job_application(*f.state, first_person, job, f.state->current_date);
	auto second = economy::physical::job_market::submit_job_application(*f.state, second_person, job, f.state->current_date);
	auto third = economy::physical::job_market::submit_job_application(*f.state, third_person, job, f.state->current_date);
	REQUIRE((first && second && third));
	economy::physical::job_market::process_pending_applications(*f.state);
	REQUIRE(f.state->world.job_application_get_status(first) == uint8_t(economy::physical::job_market::application_status::accepted));
	REQUIRE(f.state->world.job_application_get_status(second) == uint8_t(economy::physical::job_market::application_status::accepted));
	REQUIRE(f.state->world.job_application_get_status(third) == uint8_t(economy::physical::job_market::application_status::pending));
	REQUIRE(economy::physical::concrete_labor::active_workers_for_factory(*f.state, f.factory).size() == 2);
	REQUIRE(f.state->world.job_offer_get_openings(job) == 0);
}

TEST_CASE("job matching prevents incompatible double employment", "[economy][job_market]") {
	individual_concrete_labor_tests::fixture f;
	auto first_job = f.offer(1);
	auto second_job = f.offer(1);
	auto worker = f.person();
	auto first = economy::physical::job_market::submit_job_application(*f.state, worker, first_job, f.state->current_date);
	auto second = economy::physical::job_market::submit_job_application(*f.state, worker, second_job, f.state->current_date);
	REQUIRE((first && second));
	economy::physical::job_market::process_pending_applications(*f.state);
	REQUIRE(f.state->world.job_application_get_status(first) == uint8_t(economy::physical::job_market::application_status::accepted));
	REQUIRE(f.state->world.job_application_get_status(second) == uint8_t(economy::physical::job_market::application_status::rejected));
	REQUIRE(economy::physical::concrete_labor::active_workers_for_factory(*f.state, f.factory).size() == 1);
}

TEST_CASE("closed, expired, and full offers do not hire until reopened", "[economy][job_market]") {
	individual_concrete_labor_tests::fixture f;
	auto closed = f.offer(1);
	REQUIRE(economy::physical::job_market::close_job_offer(*f.state, closed));
	REQUIRE_FALSE(economy::physical::job_market::submit_job_application(*f.state, f.person(), closed, f.state->current_date));
	auto expired = f.offer(1, 1.0f, {}, f.state->current_date);
	f.state->current_date += 1;
	REQUIRE_FALSE(economy::physical::job_market::submit_job_application(*f.state, f.person(), expired, f.state->current_date));
	auto full = f.offer(0);
	auto pending = economy::physical::job_market::submit_job_application(*f.state, f.person(), full, f.state->current_date);
	REQUIRE(pending);
	economy::physical::job_market::process_pending_applications(*f.state);
	REQUIRE(f.state->world.job_application_get_status(pending) == uint8_t(economy::physical::job_market::application_status::pending));
	REQUIRE(economy::physical::job_market::add_job_offer_openings(*f.state, full, 1));
	economy::physical::job_market::process_pending_applications(*f.state);
	REQUIRE(f.state->world.job_application_get_status(pending) == uint8_t(economy::physical::job_market::application_status::accepted));
}

TEST_CASE("job matching ignores aggregate labor mutations and creates no synthetic worker money", "[economy][job_market]") {
	individual_concrete_labor_tests::fixture f;
	auto job = f.offer(1);
	auto worker = f.person();
	auto application = economy::physical::job_market::submit_job_application(*f.state, worker, job, f.state->current_date);
	REQUIRE(application);
	auto persons_before = f.state->world.person_size();
	auto transactions_before = f.state->world.transaction_size();
	f.state->world.province_set_labor_demand_satisfaction(f.province, economy::labor::no_education, 0.0f);
	f.state->world.province_set_labor_supply(f.province, economy::labor::no_education, 0.0f);
	economy::physical::job_market::process(*f.state);
	REQUIRE(f.state->world.job_application_get_status(application)
		== uint8_t(economy::physical::job_market::application_status::accepted));
	REQUIRE(f.state->world.person_size() == persons_before);
	REQUIRE(f.state->world.transaction_size() == transactions_before);
	REQUIRE(economy::physical::concrete_labor::active_workers_for_factory(*f.state, f.factory).size() == 1);
}
