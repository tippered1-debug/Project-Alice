#include "catch.hpp"

#include "economy/exact_person_economy.hpp"
#include "economy/firm_agency.hpp"
#include "economy/payroll.hpp"
#include "economy/physical/concrete_labor.hpp"
#include "economy/physical/job_market.hpp"

namespace exact_person_economy_tests {

using economy::exact_person_economy::person_key;

person_key register_anchor(individual_concrete_labor_tests::fixture& f, uint32_t cell = 0) {
	auto pop = f.state->world.create_pop();
	f.state->world.force_create_pop_location(pop, f.province);
	f.state->world.pop_set_size(pop, 0.25f);
	if(cell == 0) cell = uint32_t(pop.index()) + 1u;
	REQUIRE(persons::exact_population::register_population_cell(*f.state, pop, f.site).result
		== persons::exact_population::status::created);
	return person_key{cell, 0};
}

person_key register_synthetic_cell(sys::state& state, uint32_t cell, uint64_t count) {
	persons::exact_population::cell_descriptor descriptor;
	descriptor.source_population_cell = cell;
	descriptor.literal_count = count;
	descriptor.bootstrap_base_day = state.current_date ? state.current_date.to_raw_value() - 1 : 0;
	descriptor.demographic_seed = uint64_t(cell) * 101;
	REQUIRE(persons::exact_population::register_synthetic_population_cell(state, descriptor).result
		== persons::exact_population::status::created);
	return person_key{cell, 0};
}

person_key find_non_anchor_with_age(sys::state const& state, uint32_t cell, uint64_t count, bool underage) {
	for(uint64_t ordinal = 1; ordinal < count; ++ordinal) {
		person_key key{cell, ordinal};
		auto age = economy::exact_person_economy::is_work_eligible(state, key);
		if(underage && persons::exact_population::age_years(state, key, state.current_date) < 14) return key;
		if(!underage && age) return key;
	}
	return {};
}

}

TEST_CASE("exact population overrides round-trip with their nonzero key", "[population][exact][persistence]") {
	population_materialization_tests::fixture f;
	REQUIRE(persons::exact_population::register_population_cell(*f.state, f.pop, f.home).result
		== persons::exact_population::status::created);
	auto other_site = f.state->world.create_site();
	f.state->world.force_create_site_location(other_site, f.province);
	exact_person_economy_tests::person_key key{uint32_t(f.pop.index()) + 1u, 1};
	REQUIRE(persons::exact_population::set_alive(*f.state, key, false));
	REQUIRE(persons::exact_population::set_home_site(*f.state, key, other_site));
	auto snapshot = persons::exact_population::export_snapshot(*f.state);
	REQUIRE(snapshot.overrides.size() == 1);
	REQUIRE(snapshot.overrides.front().key == key);
	persons::exact_population::clear_store(*f.state);
	REQUIRE(persons::exact_population::import_snapshot(*f.state, snapshot));
	REQUIRE_FALSE(persons::exact_population::alive(*f.state, key));
	REQUIRE(persons::exact_population::home_site(*f.state, key) == other_site);
}

TEST_CASE("exact labor participation separates eligibility from individual participation", "[economy][exact][labor]") {
	individual_concrete_labor_tests::fixture f;
	auto anchor = exact_person_economy_tests::register_anchor(f);
	REQUIRE(economy::exact_person_economy::is_work_eligible(*f.state, anchor));
	REQUIRE(economy::exact_person_economy::is_labor_force_participant(*f.state, anchor));

	auto cell = exact_person_economy_tests::register_synthetic_cell(*f.state, 77, 256).source_population_cell;
	auto non_anchor = exact_person_economy_tests::find_non_anchor_with_age(*f.state, cell, 256, false);
	REQUIRE(non_anchor.source_population_cell == cell);
	REQUIRE(economy::exact_person_economy::is_work_eligible(*f.state, non_anchor));
	REQUIRE_FALSE(economy::exact_person_economy::is_labor_force_participant(*f.state, non_anchor));
	REQUIRE(economy::exact_person_economy::set_labor_force_participation(*f.state, non_anchor, true));
	REQUIRE(economy::exact_person_economy::is_labor_force_participant(*f.state, non_anchor));
	REQUIRE(economy::exact_person_economy::participation_override_count(*f.state) == 1);
	(void)anchor;
}

TEST_CASE("underage exact persons cannot apply even when participation is forced", "[economy][exact][job_market]") {
	individual_concrete_labor_tests::fixture f;
	auto cell = exact_person_economy_tests::register_synthetic_cell(*f.state, 78, 256).source_population_cell;
	auto child = exact_person_economy_tests::find_non_anchor_with_age(*f.state, cell, 256, true);
	REQUIRE(child.source_population_cell == cell);
	REQUIRE_FALSE(economy::exact_person_economy::is_work_eligible(*f.state, child));
	REQUIRE(economy::exact_person_economy::set_labor_force_participation(*f.state, child, true));
	auto offer = f.offer(1, 10.0f);
	REQUIRE_FALSE(economy::exact_person_economy::submit_application(*f.state, child, offer, f.state->current_date));
}

TEST_CASE("exact worker can apply, hire, supply labor, and receive wages without DCON worker objects", "[economy][exact][labor][payroll]") {
	individual_concrete_labor_tests::fixture f;
	auto worker = exact_person_economy_tests::register_anchor(f);
	auto offer = f.offer(1, 25.0f);
	auto dcon_persons_before = f.state->world.person_size();
	auto dcon_actors_before = f.state->world.economic_actor_size();
	auto dcon_accounts_before = f.state->world.monetary_account_size();
	auto application = economy::exact_person_economy::submit_application(*f.state, worker, offer, f.state->current_date);
	REQUIRE(application != 0);
	REQUIRE(economy::exact_person_economy::submit_application(*f.state, worker, offer, f.state->current_date) == 0);
	economy::physical::job_market::process_pending_applications(*f.state);
	auto application_record = economy::exact_person_economy::application(*f.state, application);
	REQUIRE(application_record);
	REQUIRE(application_record->status == economy::exact_person_economy::application_status::accepted);
	REQUIRE(f.state->world.job_offer_get_openings(offer) == 0);
	REQUIRE(f.state->world.person_size() == dcon_persons_before);
	REQUIRE(f.state->world.economic_actor_size() == dcon_actors_before);
	REQUIRE(f.state->world.monetary_account_size() == dcon_accounts_before);
	REQUIRE(economy::exact_person_economy::account_count(*f.state) == 1);
	REQUIRE(economy::exact_person_economy::active_contracts_for_person(*f.state, worker).size() == 1);
	REQUIRE(economy::physical::concrete_labor::labor_supplied_to_factory(*f.state, f.factory) == Approx(1.0f));
	REQUIRE(economy::physical::concrete_labor::wage_due_for_factory(*f.state, f.factory) == Approx(25.0f));
	REQUIRE(economy::firm_agency::decide_factory(*f.state, f.factory).expected_payroll_cost > 0.0f);

	economy::payroll::settle_factory(*f.state, f.factory, 1.0f, 1.0f);
	auto contract_id = economy::exact_person_economy::active_contracts_for_person(*f.state, worker).front();
	auto contract = economy::exact_person_economy::contract(*f.state, contract_id);
	REQUIRE(contract);
	auto worker_account = economy::exact_person_economy::account_ref::from_exact(contract->worker_account_id);
	REQUIRE(economy::exact_person_economy::balance(*f.state, worker_account) == Approx(25.0f));
	REQUIRE(economy::accounts::balance(*f.state, f.payer) == Approx(975.0f));
	REQUIRE(economy::exact_person_economy::transaction_count(*f.state) == 1);
	auto transaction = economy::exact_person_economy::latest_transaction(*f.state);
	REQUIRE(transaction);
	REQUIRE(transaction->source == economy::exact_person_economy::account_ref::from_dcon(f.payer));
	REQUIRE(transaction->destination == worker_account);
}

TEST_CASE("mixed exact transfer validates atomically and cannot mint money", "[economy][exact][money]") {
	individual_concrete_labor_tests::fixture f;
	auto worker = exact_person_economy_tests::register_anchor(f);
	auto account = economy::exact_person_economy::open_account(*f.state, worker, f.settlement);
	auto exact_other = economy::exact_person_economy::open_account(*f.state, worker, f.state->world.create_commodity());
	auto source = economy::exact_person_economy::account_ref::from_dcon(f.payer);
	auto source_before = economy::accounts::balance(*f.state, f.payer);
	REQUIRE_FALSE(economy::exact_person_economy::transfer(*f.state, source, exact_other, 10.0f,
		economy::relations::transaction_kind::payroll, f.state->current_date));
	REQUIRE(economy::accounts::balance(*f.state, f.payer) == Approx(source_before));
	REQUIRE(economy::exact_person_economy::balance(*f.state, account) == Approx(0.0f));
	REQUIRE(economy::exact_person_economy::transaction_count(*f.state) == 0);
	REQUIRE_FALSE(economy::exact_person_economy::transfer(*f.state, source, account, 2000.0f,
		economy::relations::transaction_kind::payroll, f.state->current_date));
	REQUIRE(economy::accounts::balance(*f.state, f.payer) == Approx(source_before));
}

TEST_CASE("exact insufficient wages remain person-specific arrears", "[economy][exact][payroll]") {
	individual_concrete_labor_tests::fixture f;
	auto worker = exact_person_economy_tests::register_anchor(f);
	auto offer = f.offer(1, 25.0f);
	REQUIRE(economy::exact_person_economy::submit_application(*f.state, worker, offer, f.state->current_date));
	economy::physical::job_market::process_pending_applications(*f.state);
	f.state->world.monetary_account_set_balance(f.payer, 0.0f);
	economy::payroll::settle_factory(*f.state, f.factory, 1.0f, 1.0f);
	auto contract_id = economy::exact_person_economy::active_contracts_for_person(*f.state, worker).front();
	REQUIRE(economy::exact_person_economy::contract(*f.state, contract_id)->unpaid_wages == Approx(25.0f));
	REQUIRE(economy::exact_person_economy::balance(*f.state,
		economy::exact_person_economy::account_ref::from_exact(
			economy::exact_person_economy::contract(*f.state, contract_id)->worker_account_id)) == Approx(0.0f));
	REQUIRE(f.state->world.obligation_size() == 0);
}

TEST_CASE("ending exact contract removes its concrete labor immediately", "[economy][exact][labor]") {
	individual_concrete_labor_tests::fixture f;
	auto worker = exact_person_economy_tests::register_anchor(f);
	auto offer = f.offer(1);
	REQUIRE(economy::exact_person_economy::submit_application(*f.state, worker, offer, f.state->current_date));
	economy::physical::job_market::process_pending_applications(*f.state);
	auto contract_id = economy::exact_person_economy::active_contracts_for_person(*f.state, worker).front();
	REQUIRE(economy::physical::concrete_labor::labor_supplied_to_factory(*f.state, f.factory) == Approx(1.0f));
	REQUIRE(economy::exact_person_economy::end_contract(*f.state, contract_id,
		economy::exact_person_economy::contract_status::terminated, f.state->current_date));
	REQUIRE(economy::physical::concrete_labor::labor_supplied_to_factory(*f.state, f.factory) == Approx(0.0f));
}

TEST_CASE("exact job search is explicit and does not bridge the worker", "[economy][exact][job_market]") {
	individual_concrete_labor_tests::fixture f;
	auto worker = exact_person_economy_tests::register_anchor(f);
	auto lower = f.offer(1, 2.0f);
	auto higher = f.offer(1, 5.0f);
	economy::exact_person_economy::process_job_search_for_exact_person(*f.state, worker);
	auto applications = economy::exact_person_economy::applications_for_person(*f.state, worker);
	REQUIRE(applications.size() == 1);
	REQUIRE(economy::exact_person_economy::application(*f.state, applications.front())->offer == higher);
	REQUIRE_FALSE(persons::exact_population::legacy_person_for_exact_person(*f.state, worker));
	REQUIRE(f.state->world.job_offer_get_openings(lower) == 1);
}

TEST_CASE("million logical humans activate one exact economic record set", "[economy][exact][scale]") {
	individual_concrete_labor_tests::fixture f;
	persons::exact_population::cell_descriptor descriptor;
	descriptor.source_population_cell = 1000;
	descriptor.literal_count = 1'000'000;
	descriptor.bootstrap_base_day = f.state->current_date.to_raw_value() - 1;
	descriptor.demographic_seed = 1000;
	REQUIRE(persons::exact_population::register_synthetic_population_cell(*f.state, descriptor).result
		== persons::exact_population::status::created);
	auto worker = exact_person_economy_tests::person_key{1000, 0};
	auto persons_before = f.state->world.person_size();
	auto actors_before = f.state->world.economic_actor_size();
	auto offer = f.offer(1, 3.0f);
	REQUIRE(economy::exact_person_economy::submit_application(*f.state, worker, offer, f.state->current_date));
	economy::physical::job_market::process_pending_applications(*f.state);
	for(auto ordinal : {uint64_t(1), uint64_t(500'000), uint64_t(999'999)})
		REQUIRE(persons::exact_population::exists(*f.state, {1000, ordinal}));
	REQUIRE(persons::exact_population::logical_person_count(*f.state) == 1'000'000);
	REQUIRE(economy::exact_person_economy::account_count(*f.state) == 1);
	REQUIRE(economy::exact_person_economy::applications_for_person(*f.state, worker).size() == 1);
	REQUIRE(economy::exact_person_economy::active_contracts_for_person(*f.state, worker).size() == 1);
	REQUIRE(f.state->world.person_size() == persons_before);
	REQUIRE(f.state->world.economic_actor_size() == actors_before);
}

TEST_CASE("exact economic snapshot restores accounts applications contracts and ledger", "[economy][exact][persistence]") {
	individual_concrete_labor_tests::fixture f;
	auto worker = exact_person_economy_tests::register_anchor(f);
	REQUIRE(economy::exact_person_economy::set_labor_force_participation(*f.state, worker, false));
	auto offer = f.offer(1, 5.0f);
	REQUIRE(economy::exact_person_economy::submit_application(*f.state, worker, offer, f.state->current_date) == 0);
	REQUIRE(economy::exact_person_economy::set_labor_force_participation(*f.state, worker, true));
	auto application = economy::exact_person_economy::submit_application(*f.state, worker, offer, f.state->current_date);
	REQUIRE(application);
	economy::physical::job_market::process_pending_applications(*f.state);
	auto contract_id = economy::exact_person_economy::active_contracts_for_person(*f.state, worker).front();
	economy::payroll::settle_factory(*f.state, f.factory, 1.0f, 1.0f);
	REQUIRE(economy::exact_person_economy::set_labor_force_participation(*f.state, worker, false));
	auto snapshot = economy::exact_person_economy::export_snapshot(*f.state);
	economy::exact_person_economy::clear_store(*f.state);
	REQUIRE(economy::exact_person_economy::import_snapshot(*f.state, snapshot));
	REQUIRE(economy::exact_person_economy::account_count(*f.state) == 1);
	REQUIRE(economy::exact_person_economy::application(*f.state, application)->status
		== economy::exact_person_economy::application_status::accepted);
	REQUIRE(economy::exact_person_economy::contract(*f.state, contract_id));
	REQUIRE(economy::exact_person_economy::transaction_count(*f.state) == 1);
	REQUIRE_FALSE(economy::exact_person_economy::is_labor_force_participant(*f.state, worker));
}
