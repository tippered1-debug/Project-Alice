#include "catch.hpp"

#include "economy/accounts/accounts.hpp"
#include "economy/payroll.hpp"
#include "economy/physical/concrete_market.hpp"
#include "economy/physical/inventory.hpp"
#include "economy/physical/job_market.hpp"
#include "economy/physical/individual_consumption.hpp"
#include "persons/population_materialization.hpp"
#include "persons/persons.hpp"

#include <set>

namespace population_materialization_tests {
struct fixture {
	std::unique_ptr<sys::state> state = std::make_unique<sys::state>();
	dcon::province_id province{};
	dcon::site_id home{};
	dcon::pop_id pop{};
	dcon::pop_type_id pop_type{};
	dcon::culture_id culture{};
	dcon::religion_id religion{};

	fixture() {
		state->current_date = sys::date{30000};
		province = state->world.create_province();
		home = state->world.create_site();
		state->world.force_create_site_location(home, province);
		pop_type = state->world.create_pop_type();
		culture = state->world.create_culture();
		religion = state->world.create_religion();
		pop = state->world.create_pop();
		state->world.force_create_pop_location(pop, province);
		state->world.pop_set_poptype(pop, pop_type);
		state->world.pop_set_culture(pop, culture);
		state->world.pop_set_religion(pop, religion);
		state->world.pop_set_size(pop, 3.0f);
	}
};
}

TEST_CASE("population materialization creates exact literal persons", "[population][materialization]") {
	population_materialization_tests::fixture f;
	auto before_actors = f.state->world.economic_actor_size();
	auto generated = persons::population_materialization::materialize_population_cell(*f.state, f.pop);
	REQUIRE(generated.size() == 3);
	REQUIRE(f.state->world.person_size() == 3);
	REQUIRE(f.state->world.economic_actor_size() == before_actors + 3);
	REQUIRE(f.state->world.monetary_account_size() == 0);
	REQUIRE(f.state->world.physical_stock_size() == 0);
	REQUIRE(f.state->world.employment_contract_size() == 0);
	REQUIRE(f.state->world.person_commodity_need_size() == 0);
}

TEST_CASE("materialized persons have unique stable source keys and no weights", "[population][materialization]") {
	population_materialization_tests::fixture f;
	auto generated = persons::population_materialization::materialize_population_cell(*f.state, f.pop);
	std::set<std::pair<uint32_t, uint32_t>> keys;
	for(uint32_t ordinal = 0; ordinal < generated.size(); ++ordinal) {
		auto key = persons::population_materialization::key_for_person(*f.state, generated[ordinal]);
		REQUIRE(persons::population_materialization::is_materialized_person(*f.state, generated[ordinal]));
		REQUIRE(key.source_population_cell == uint32_t(f.pop.index()) + 1u);
		REQUIRE(key.ordinal == ordinal);
		REQUIRE(keys.insert({key.source_population_cell, key.ordinal}).second);
		REQUIRE(f.state->world.person_get_source_pop_type(generated[ordinal]) == f.pop_type);
		REQUIRE(f.state->world.person_get_source_culture(generated[ordinal]) == f.culture);
		REQUIRE(f.state->world.person_get_source_religion(generated[ordinal]) == f.religion);
	}
	REQUIRE(keys.size() == 3);
}

TEST_CASE("materialization ordering is deterministic", "[population][materialization]") {
	population_materialization_tests::fixture first;
	population_materialization_tests::fixture second;
	auto first_persons = persons::population_materialization::materialize_population_cell(*first.state, first.pop);
	auto second_persons = persons::population_materialization::materialize_population_cell(*second.state, second.pop);
	REQUIRE(first_persons.size() == second_persons.size());
	for(size_t i = 0; i < first_persons.size(); ++i) {
		REQUIRE(persons::population_materialization::key_for_person(*first.state, first_persons[i]).ordinal
			== persons::population_materialization::key_for_person(*second.state, second_persons[i]).ordinal);
		REQUIRE(first.state->world.person_get_birth_date(first_persons[i])
			== second.state->world.person_get_birth_date(second_persons[i]));
	}
	REQUIRE(first.state->world.person_get_birth_date(first_persons.front()) != first.state->current_date);
}

TEST_CASE("population estimate is allocation-free and has no hidden multiplier", "[population][materialization]") {
	population_materialization_tests::fixture f;
	auto estimate = persons::population_materialization::estimate_initial_population(*f.state);
	REQUIRE(estimate.source_population_cells == 1);
	REQUIRE(estimate.source_population_units == Approx(3.0));
	REQUIRE(estimate.intended_literal_persons == 3);
	REQUIRE(estimate.intended_economic_actors == 3);
	REQUIRE(f.state->world.person_size() == 0);
	REQUIRE(f.state->world.economic_actor_size() == 0);
}

TEST_CASE("materialization birth dates and identity are deterministic", "[population][materialization]") {
	population_materialization_tests::fixture first;
	population_materialization_tests::fixture second;
	auto first_persons = persons::population_materialization::materialize_population_cell(*first.state, first.pop);
	auto second_persons = persons::population_materialization::materialize_population_cell(*second.state, second.pop);
	for(size_t i = 0; i < first_persons.size(); ++i) {
		REQUIRE(first.state->world.person_get_birth_date(first_persons[i])
			== second.state->world.person_get_birth_date(second_persons[i]));
		auto first_key = persons::population_materialization::key_for_person(*first.state, first_persons[i]);
		auto second_key = persons::population_materialization::key_for_person(*second.state, second_persons[i]);
		REQUIRE(first_key.source_population_cell == second_key.source_population_cell);
		REQUIRE(first_key.ordinal == second_key.ordinal);
	}
}

TEST_CASE("repeated materialization is idempotent", "[population][materialization]") {
	population_materialization_tests::fixture f;
	auto first = persons::population_materialization::materialize_population_cell(*f.state, f.pop);
	auto persons_before = f.state->world.person_size();
	auto actors_before = f.state->world.economic_actor_size();
	auto second = persons::population_materialization::materialize_population_cell(*f.state, f.pop);
	REQUIRE(second == first);
	REQUIRE(f.state->world.person_size() == persons_before);
	REQUIRE(f.state->world.economic_actor_size() == actors_before);
	REQUIRE(f.state->world.population_materialization_size() == 1);
}

TEST_CASE("materialized persons share a deterministic concrete home site", "[population][materialization]") {
	population_materialization_tests::fixture f;
	auto generated = persons::population_materialization::materialize_population_cell(*f.state, f.pop);
	for(auto person : generated) {
		REQUIRE(persons::population_materialization::home_site_for_materialized_person(*f.state, person) == f.home);
		REQUIRE(f.state->world.person_get_site_from_person_home_site(person) == f.home);
	}
}

TEST_CASE("legacy POP size mutation does not rematerialize a completed cell", "[population][materialization]") {
	population_materialization_tests::fixture f;
	(void)persons::population_materialization::materialize_population_cell(*f.state, f.pop);
	f.state->world.pop_set_size(f.pop, 99.0f);
	auto persons_before = f.state->world.person_size();
	auto generated = persons::population_materialization::materialize_population_cell(*f.state, f.pop);
	REQUIRE(generated.size() == 3);
	REQUIRE(f.state->world.person_size() == persons_before);
}

TEST_CASE("source POP mutations do not rewrite materialized demographic identity", "[population][materialization]") {
	population_materialization_tests::fixture f;
	auto generated = persons::population_materialization::materialize_population_cell(*f.state, f.pop);
	auto birth = f.state->world.person_get_birth_date(generated.front());
	f.state->world.pop_set_poptype(f.pop, f.state->world.create_pop_type());
	f.state->world.pop_set_size(f.pop, 99.0f);
	(void)persons::population_materialization::materialize_population_cell(*f.state, f.pop);
	REQUIRE(f.state->world.person_get_birth_date(generated.front()) == birth);
	REQUIRE(persons::population_materialization::key_for_person(*f.state, generated.front()).ordinal == 0);
}

TEST_CASE("materialization version mismatch does not append or duplicate", "[population][materialization]") {
	population_materialization_tests::fixture f;
	auto generated = persons::population_materialization::materialize_population_cell(*f.state, f.pop);
	auto marker = f.state->world.pop_get_population_materialization_from_population_materialization_source(f.pop);
	f.state->world.population_materialization_set_bootstrap_version(marker,
		persons::population_materialization::bootstrap_semantics_version - 1);
	auto persons_before = f.state->world.person_size();
	auto actors_before = f.state->world.economic_actor_size();
	auto result = persons::population_materialization::materialize_population_cell_with_status(*f.state, f.pop);
	REQUIRE(result.status == persons::population_materialization::materialization_status::version_mismatch);
	REQUIRE(result.persons == generated);
	REQUIRE(f.state->world.person_size() == persons_before);
	REQUIRE(f.state->world.economic_actor_size() == actors_before);
}

TEST_CASE("legacy POP savings mutation cannot alter individual account state", "[population][materialization]") {
	population_materialization_tests::fixture f;
	auto generated = persons::population_materialization::materialize_population_cell(*f.state, f.pop);
	auto settlement = f.state->world.create_commodity();
	auto account = economy::accounts::open_account(*f.state,
		persons::actor_for_person(*f.state, generated.front()), settlement);
	REQUIRE(economy::accounts::bootstrap_set_balance(*f.state, account, 42.0f));
	f.state->world.pop_set_savings(f.pop, 999.0f);
	(void)persons::population_materialization::materialize_population_cell(*f.state, f.pop);
	REQUIRE(economy::accounts::balance(*f.state, account) == Approx(42.0f));
	REQUIRE(f.state->world.monetary_account_size() == 1);
}

TEST_CASE("legacy POP employment and satisfaction mutations do not create individual state", "[population][materialization]") {
	population_materialization_tests::fixture f;
	(void)persons::population_materialization::materialize_population_cell(*f.state, f.pop);
	f.state->world.pop_set_uemployment(f.pop, uint8_t(1));
	f.state->world.pop_set_satisfaction(f.pop, 0.0f);
	(void)persons::population_materialization::materialize_population_cell(*f.state, f.pop);
	REQUIRE(f.state->world.person_size() == 3);
	REQUIRE(f.state->world.employment_contract_size() == 0);
	REQUIRE(f.state->world.person_commodity_need_size() == 0);
}

TEST_CASE("materialization itself creates no synthetic economic state", "[population][materialization]") {
	population_materialization_tests::fixture f;
	(void)persons::population_materialization::materialize_population_cell(*f.state, f.pop);
	REQUIRE(f.state->world.monetary_account_size() == 0);
	REQUIRE(f.state->world.physical_stock_size() == 0);
	REQUIRE(f.state->world.transaction_size() == 0);
	REQUIRE(f.state->world.job_application_size() == 0);
	REQUIRE(f.state->world.employment_contract_size() == 0);
	REQUIRE(f.state->world.person_commodity_need_size() == 0);
}

TEST_CASE("materialized unemployed persons participate in individual job search", "[population][materialization][job_market]") {
	individual_concrete_labor_tests::fixture f;
	auto pop = f.state->world.create_pop();
	f.state->world.force_create_pop_location(pop, f.province);
	f.state->world.pop_set_size(pop, 1.0f);
	auto generated = persons::population_materialization::materialize_population_cell(*f.state, pop, f.site);
	REQUIRE(generated.size() == 1);
	auto job = f.offer(1, 25.0f);
	economy::physical::job_market::process_job_search(*f.state);
	REQUIRE(economy::physical::job_market::applications_for_person(*f.state, generated.front()).size() == 1);
	REQUIRE(f.state->world.job_application_get_job_offer_from_job_application_offer(
		economy::physical::job_market::applications_for_person(*f.state, generated.front()).front()) == job);
}

TEST_CASE("underage persons cannot apply for individual jobs", "[population][job_market]") {
	individual_concrete_labor_tests::fixture f;
	auto child = persons::create_person(*f.state, f.state->current_date - 10 * 365);
	auto job = f.offer(1, 25.0f);
	REQUIRE_FALSE(persons::is_work_eligible(*f.state, child));
	REQUIRE_FALSE(economy::physical::job_market::submit_job_application(*f.state, child, job, f.state->current_date));
	economy::physical::job_market::process_job_search(*f.state);
	REQUIRE(economy::physical::job_market::applications_for_person(*f.state, child).empty());
}

TEST_CASE("aggregate labor mutations do not change concrete person eligibility", "[population][job_market]") {
	population_materialization_tests::fixture f;
	auto generated = persons::population_materialization::materialize_population_cell(*f.state, f.pop);
	auto person = generated.front();
	REQUIRE(persons::is_work_eligible(*f.state, person));
	f.state->world.pop_set_uemployment(f.pop, uint8_t(1));
	f.state->world.pop_set_satisfaction(f.pop, 0.0f);
	REQUIRE(persons::is_work_eligible(*f.state, person));
}

TEST_CASE("materialized person receives exact wage and uses individual consumption", "[population][materialization][economy]") {
	individual_concrete_labor_tests::fixture f;
	auto pop = f.state->world.create_pop();
	f.state->world.force_create_pop_location(pop, f.province);
	f.state->world.pop_set_size(pop, 1.0f);
	auto generated = persons::population_materialization::materialize_population_cell(*f.state, pop, f.site);
	auto worker = generated.front();
	auto job = f.offer(1, 25.0f);
	REQUIRE(economy::physical::job_market::submit_job_application(*f.state, worker, job, f.state->current_date));
	economy::physical::job_market::process_pending_applications(*f.state);
	auto contracts = economy::physical::concrete_labor::contracts_for_factory(*f.state, f.factory);
	REQUIRE(contracts.size() == 1);
	auto contract = contracts.front();
	auto worker_account = f.state->world.employment_contract_get_monetary_account_from_employment_contract_worker_account(contract);
	economy::payroll::settle_factory(*f.state, f.factory, 1.0f, 1.0f);
	REQUIRE(economy::accounts::balance(*f.state, worker_account) == Approx(25.0f));
	auto seller = f.state->world.create_economic_actor();
	(void)economy::accounts::open_account(*f.state, seller, f.settlement);
	REQUIRE(economy::physical::inventory::add(*f.state, f.site, f.output, 1.0f, seller) == Approx(1.0f));
	REQUIRE(economy::physical::concrete_market::post_ask(*f.state, seller, f.site, f.market, f.output,
		1.0f, 10.0f, economy::physical::concrete_market::order_purpose::general));
	REQUIRE(economy::physical::individual_consumption::set_need(*f.state, worker, f.output, 1.0f));
	economy::physical::individual_consumption::process_purchase_decisions(*f.state);
	REQUIRE(economy::accounts::balance(*f.state, worker_account) == Approx(15.0f));
	economy::physical::individual_consumption::process_consumption(*f.state);
	REQUIRE(economy::physical::inventory::quantity(*f.state, f.site, f.output,
		persons::actor_for_person(*f.state, worker)) == Approx(0.0f));
	REQUIRE(economy::physical::individual_consumption::unmet_need(*f.state, worker, f.output) == Approx(0.0f));
}

TEST_CASE("materialization measurement reports sparse exact-person storage", "[population][materialization][benchmark]") {
	population_materialization_tests::fixture f;
	auto measurement = persons::population_materialization::measure_initial_population_materialization(*f.state);
	REQUIRE(measurement.persons_created == 3);
	REQUIRE(measurement.persons_total == 3);
	REQUIRE(measurement.economic_actors_total == 3);
	REQUIRE(measurement.materialization_markers == 1);
	REQUIRE(measurement.elapsed_microseconds >= 0);
}
