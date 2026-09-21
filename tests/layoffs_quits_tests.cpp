#include "catch.hpp"

#include "economy/exact_person_economy.hpp"
#include "economy/payroll.hpp"
#include "economy/physical/labor_dynamics.hpp"
#include "economy/physical/job_market.hpp"

TEST_CASE("legacy unemployment follows the concrete contract and ignores POP labor aggregates", "[economy][labor][unemployment]") {
	individual_concrete_labor_tests::fixture f;
	auto contract = f.hire(1.0f, 10.0f);
	auto worker = f.state->world.employment_contract_get_person_from_employment_contract_person(contract);
	REQUIRE_FALSE(economy::physical::labor_dynamics::is_unemployed(*f.state, worker));
	f.state->world.province_set_labor_supply(f.province, economy::labor::no_education, 0.0f);
	REQUIRE_FALSE(economy::physical::labor_dynamics::is_unemployed(*f.state, worker));
	REQUIRE(economy::physical::labor_dynamics::quit_employment(*f.state, contract));
	REQUIRE(economy::physical::labor_dynamics::is_unemployed(*f.state, worker));
	REQUIRE_FALSE(economy::physical::job_market::submit_job_application(*f.state, worker,
		f.offer(1), f.state->current_date));
	f.state->current_date += 1;
	REQUIRE(economy::physical::job_market::submit_job_application(*f.state, worker,
		f.offer(1), f.state->current_date));
}

TEST_CASE("exact layoffs are whole-contract, sparse, and do not materialize workers", "[economy][labor][exact]") {
	individual_concrete_labor_tests::fixture f;
	auto worker = exact_person_economy_tests::register_anchor(f);
	auto offer = f.offer(1, 25.0f);
	REQUIRE(economy::exact_person_economy::submit_application(*f.state, worker, offer, f.state->current_date));
	economy::physical::job_market::process_pending_applications(*f.state);
	auto contract = economy::exact_person_economy::active_contracts_for_person(*f.state, worker).front();
	auto persons_before = f.state->world.person_size();
	auto actors_before = f.state->world.economic_actor_size();
	f.state->world.factory_set_productive_capacity(f.factory, 0.0f);
	economy::physical::labor_dynamics::process_factory_labor_dynamics(*f.state);
	REQUIRE(economy::exact_person_economy::active_contracts_for_person(*f.state, worker).empty());
	REQUIRE(economy::physical::labor_dynamics::is_unemployed(*f.state, worker));
	REQUIRE(economy::physical::labor_dynamics::displaced_exact_workers(*f.state).size() == 1);
	REQUIRE(f.state->world.person_size() == persons_before);
	REQUIRE(f.state->world.economic_actor_size() == actors_before);
	REQUIRE(economy::physical::labor_dynamics::separation_event_count(*f.state) == 1);
	REQUIRE(economy::physical::labor_dynamics::separation_event_at(*f.state, 1)->contract_id == contract);
}

TEST_CASE("exact worker arrears survive layoff and repay the former worker", "[economy][labor][payroll]") {
	individual_concrete_labor_tests::fixture f;
	auto worker = exact_person_economy_tests::register_anchor(f);
	auto offer = f.offer(1, 25.0f);
	REQUIRE(economy::exact_person_economy::submit_application(*f.state, worker, offer, f.state->current_date));
	economy::physical::job_market::process_pending_applications(*f.state);
	auto contract_id = economy::exact_person_economy::active_contracts_for_person(*f.state, worker).front();
	f.state->world.monetary_account_set_balance(f.payer, 0.0f);
	economy::payroll::settle_factory(*f.state, f.factory, 1.0f, 1.0f);
	REQUIRE(economy::exact_person_economy::contract(*f.state, contract_id)->unpaid_wages == Approx(25.0f));
	f.state->world.factory_set_productive_capacity(f.factory, 0.0f);
	economy::physical::labor_dynamics::process_factory_labor_dynamics(*f.state);
	REQUIRE(economy::exact_person_economy::contract(*f.state, contract_id)->unpaid_wages == Approx(25.0f));
	f.state->current_date += 1;
	f.state->world.monetary_account_set_balance(f.payer, 25.0f);
	economy::payroll::settle_factory(*f.state, f.factory, 0.0f, 0.0f);
	REQUIRE(economy::exact_person_economy::contract(*f.state, contract_id)->unpaid_wages == Approx(0.0f));
	REQUIRE(economy::exact_person_economy::balance(*f.state,
		economy::exact_person_economy::account_ref::from_exact(
			economy::exact_person_economy::contract(*f.state, contract_id)->worker_account_id)) == Approx(25.0f));
}

TEST_CASE("exact displaced search is delayed until the next date", "[economy][labor][job_market]") {
	individual_concrete_labor_tests::fixture f;
	auto worker = exact_person_economy_tests::register_anchor(f);
	auto first_offer = f.offer(1, 25.0f);
	REQUIRE(economy::exact_person_economy::submit_application(*f.state, worker, first_offer, f.state->current_date));
	economy::physical::job_market::process_pending_applications(*f.state);
	auto contract = economy::exact_person_economy::active_contracts_for_person(*f.state, worker).front();
	f.state->world.factory_set_productive_capacity(f.factory, 0.0f);
	economy::physical::labor_dynamics::process_factory_labor_dynamics(*f.state);
	auto replacement = f.offer(1, 30.0f);
	economy::physical::labor_dynamics::process_displaced_job_search(*f.state);
	REQUIRE(economy::exact_person_economy::applications_for_person(*f.state, worker).size() == 1);
	f.state->current_date += 1;
	economy::physical::labor_dynamics::process_displaced_job_search(*f.state);
	REQUIRE(economy::exact_person_economy::applications_for_person(*f.state, worker).size() == 2);
	(void)contract;
	(void)replacement;
}

TEST_CASE("surplus contraction removes whole contracts and closes open offers", "[economy][labor][contraction]") {
	individual_concrete_labor_tests::fixture f;
	f.hire(1.0f, 1.0f);
	f.hire(1.0f, 2.0f);
	f.hire(1.0f, 3.0f);
	auto offer = f.offer(1, 4.0f);
	f.state->world.factory_set_productive_capacity(f.factory, 2.0f);
	economy::physical::labor_dynamics::process_factory_labor_dynamics(*f.state);
	REQUIRE(economy::physical::concrete_labor::active_contracts_for_factory(*f.state, f.factory).size() == 2);
	REQUIRE(f.state->world.job_offer_get_status(offer)
		== uint8_t(economy::physical::job_market::offer_status::closed));
}

TEST_CASE("surplus contraction precedes job-market hiring", "[economy][labor][job_market]") {
	individual_concrete_labor_tests::fixture f;
	f.hire(1.0f, 1.0f);
	f.hire(1.0f, 2.0f);
	f.hire(1.0f, 3.0f);
	f.state->world.factory_set_productive_capacity(f.factory, 2.0f);
	auto offer = f.offer(1, 4.0f);
	auto applicant = f.person();
	auto application = economy::physical::job_market::submit_job_application(*f.state, applicant,
		offer, f.state->current_date);
	REQUIRE(application);
	economy::physical::labor_dynamics::process_factory_labor_dynamics(*f.state);
	economy::physical::job_market::process(*f.state);
	REQUIRE(economy::physical::concrete_labor::active_contracts_for_factory(*f.state, f.factory).size() == 2);
	REQUIRE(f.state->world.job_application_get_status(application)
		== uint8_t(economy::physical::job_market::application_status::rejected));
}

TEST_CASE("new vacancy is visible to exact and DCON search in the same tick", "[economy][labor][job_market][ordering]") {
	individual_concrete_labor_tests::fixture f;
	auto exact_worker = exact_person_economy_tests::register_anchor(f);
	auto legacy_worker = f.person();
	economy::physical::job_market::process_factory_vacancies(*f.state);
	std::vector<dcon::job_offer_id> offers;
	f.state->world.for_each_job_offer([&](auto offer) { offers.push_back(offer); });
	REQUIRE(offers.size() == 1);
	f.state->world.job_offer_set_openings(offers.front(), 1);
	economy::exact_person_economy::enqueue_displaced_worker(*f.state, exact_worker);
	economy::physical::job_market::process(*f.state);
	REQUIRE(economy::exact_person_economy::applications_for_person(*f.state, exact_worker).size() == 1);
	REQUIRE(economy::physical::job_market::applications_for_person(*f.state, legacy_worker).size() == 1);
	REQUIRE(economy::exact_person_economy::active_contracts_for_person(*f.state, exact_worker).size() == 1);
	auto legacy_application = economy::physical::job_market::applications_for_person(*f.state, legacy_worker).front();
	REQUIRE(f.state->world.job_application_get_status(legacy_application)
		== uint8_t(economy::physical::job_market::application_status::pending));
}

TEST_CASE("limited canonical payroll follows neutral contract causal order", "[economy][labor][payroll][ordering]") {
	auto run = [](bool exact_first) {
		individual_concrete_labor_tests::fixture f;
		economy::exact_person_economy::person_key exact_worker{};
		uint64_t exact_contract_id = 0;
		dcon::employment_contract_id legacy_contract{};
		if(exact_first) {
			exact_worker = exact_person_economy_tests::register_anchor(f);
			auto offer = f.offer(1, 10.0f);
			REQUIRE(economy::exact_person_economy::submit_application(*f.state, exact_worker, offer, f.state->current_date));
			economy::physical::job_market::process_pending_applications(*f.state);
			exact_contract_id = economy::exact_person_economy::active_contracts_for_person(*f.state, exact_worker).front();
			legacy_contract = f.hire(1.0f, 10.0f);
		} else {
			legacy_contract = f.hire(1.0f, 10.0f);
			exact_worker = exact_person_economy_tests::register_anchor(f);
			auto offer = f.offer(1, 10.0f);
			REQUIRE(economy::exact_person_economy::submit_application(*f.state, exact_worker, offer, f.state->current_date));
			economy::physical::job_market::process_pending_applications(*f.state);
			exact_contract_id = economy::exact_person_economy::active_contracts_for_person(*f.state, exact_worker).front();
		}
		REQUIRE(legacy_contract);
		REQUIRE(exact_contract_id != 0);
		f.state->world.monetary_account_set_balance(f.payer, 10.0f);
		economy::payroll::settle_factory(*f.state, f.factory, 1.0f, 1.0f);
		auto legacy_worker_account = f.state->world.employment_contract_get_monetary_account_from_employment_contract_worker_account(legacy_contract);
		auto exact_record = economy::exact_person_economy::contract(*f.state, exact_contract_id);
		REQUIRE(exact_record);
		auto exact_worker_account = economy::exact_person_economy::account_ref::from_exact(exact_record->worker_account_id);
		if(exact_first) {
			REQUIRE(economy::exact_person_economy::balance(*f.state, exact_worker_account) == Approx(10.0f));
			REQUIRE(economy::accounts::balance(*f.state, legacy_worker_account) == Approx(0.0f));
			REQUIRE(economy::physical::concrete_labor::unpaid_wages(*f.state, legacy_contract) == Approx(10.0f));
		} else {
			REQUIRE(economy::exact_person_economy::balance(*f.state, exact_worker_account) == Approx(0.0f));
			REQUIRE(economy::accounts::balance(*f.state, legacy_worker_account) == Approx(10.0f));
			REQUIRE(exact_record->unpaid_wages == Approx(10.0f));
		}
	};
	run(false);
	run(true);
}

TEST_CASE("mixed representation arrears use oldest debt before current wages", "[economy][labor][payroll][arrears]") {
	auto run = [](bool exact_is_older) {
		individual_concrete_labor_tests::fixture f;
		economy::exact_person_economy::person_key exact_worker{};
		uint64_t exact_contract_id = 0;
		dcon::employment_contract_id legacy_contract{};
		if(exact_is_older) {
			exact_worker = exact_person_economy_tests::register_anchor(f);
			auto offer = f.offer(1, 10.0f);
			REQUIRE(economy::exact_person_economy::submit_application(*f.state, exact_worker, offer, f.state->current_date));
			economy::physical::job_market::process_pending_applications(*f.state);
			exact_contract_id = economy::exact_person_economy::active_contracts_for_person(*f.state, exact_worker).front();
			f.state->world.monetary_account_set_balance(f.payer, 0.0f);
			economy::payroll::settle_factory(*f.state, f.factory, 1.0f, 1.0f);
			f.state->current_date += 1;
			legacy_contract = f.hire(1.0f, 10.0f);
		} else {
			legacy_contract = f.hire(1.0f, 10.0f);
			f.state->world.monetary_account_set_balance(f.payer, 0.0f);
			economy::payroll::settle_factory(*f.state, f.factory, 1.0f, 1.0f);
			f.state->current_date += 1;
			exact_worker = exact_person_economy_tests::register_anchor(f);
			auto offer = f.offer(1, 10.0f);
			REQUIRE(economy::exact_person_economy::submit_application(*f.state, exact_worker, offer, f.state->current_date));
			economy::physical::job_market::process_pending_applications(*f.state);
			exact_contract_id = economy::exact_person_economy::active_contracts_for_person(*f.state, exact_worker).front();
		}
		REQUIRE(legacy_contract);
		f.state->world.monetary_account_set_balance(f.payer, 0.0f);
		economy::payroll::settle_factory(*f.state, f.factory, 1.0f, 1.0f);
		f.state->current_date += 1;
		f.state->world.monetary_account_set_balance(f.payer, 10.0f);
		economy::payroll::settle_factory(*f.state, f.factory, 1.0f, 1.0f);
		auto exact_record = economy::exact_person_economy::contract(*f.state, exact_contract_id);
		REQUIRE(exact_record);
		if(exact_is_older) {
			REQUIRE(exact_record->unpaid_wages == Approx(10.0f));
			REQUIRE(economy::physical::concrete_labor::unpaid_wages(*f.state, legacy_contract) == Approx(20.0f));
		} else {
			REQUIRE(economy::physical::concrete_labor::unpaid_wages(*f.state, legacy_contract) == Approx(10.0f));
			REQUIRE(exact_record->unpaid_wages == Approx(20.0f));
		}
	};
	run(true);
	run(false);
}

TEST_CASE("canonical payroll clears every representation's arrears before current wages", "[economy][labor][payroll][arrears][ordering]") {
	auto run = [](bool exact_has_smaller_arrears) {
		individual_concrete_labor_tests::fixture f;
		dcon::employment_contract_id legacy_contract{};
		uint64_t exact_contract_id = 0;
		if(exact_has_smaller_arrears) {
			legacy_contract = f.hire(1.0f, 10.0f);
			f.state->world.monetary_account_set_balance(f.payer, 0.0f);
			economy::payroll::settle_factory(*f.state, f.factory, 1.0f, 1.0f);
			f.state->current_date += 1;
			auto exact_worker = exact_person_economy_tests::register_anchor(f);
			auto offer = f.offer(1, 10.0f);
			REQUIRE(economy::exact_person_economy::submit_application(*f.state, exact_worker, offer, f.state->current_date));
			economy::physical::job_market::process_pending_applications(*f.state);
			exact_contract_id = economy::exact_person_economy::active_contracts_for_person(*f.state, exact_worker).front();
			f.state->world.monetary_account_set_balance(f.payer, 0.0f);
			auto current = economy::exact_person_economy::settle_current_contract_wage_only(*f.state, exact_contract_id);
			REQUIRE(current.unpaid == Approx(10.0f));
			f.state->world.monetary_account_set_balance(f.payer, 2.0f);
			auto partial = economy::exact_person_economy::settle_contract_arrears_only(*f.state, exact_contract_id);
			REQUIRE(partial.arrears_repaid == Approx(2.0f));
		} else {
			auto exact_worker = exact_person_economy_tests::register_anchor(f);
			auto offer = f.offer(1, 10.0f);
			REQUIRE(economy::exact_person_economy::submit_application(*f.state, exact_worker, offer, f.state->current_date));
			economy::physical::job_market::process_pending_applications(*f.state);
			exact_contract_id = economy::exact_person_economy::active_contracts_for_person(*f.state, exact_worker).front();
			f.state->world.monetary_account_set_balance(f.payer, 0.0f);
			economy::payroll::settle_factory(*f.state, f.factory, 1.0f, 1.0f);
			f.state->current_date += 1;
			legacy_contract = f.hire(1.0f, 10.0f);
			f.state->world.monetary_account_set_balance(f.payer, 0.0f);
			auto current = economy::physical::concrete_labor::settle_current_contract_wage_only(*f.state, legacy_contract);
			REQUIRE(current.unpaid == Approx(10.0f));
			f.state->world.monetary_account_set_balance(f.payer, 2.0f);
			auto partial = economy::physical::concrete_labor::settle_contract_arrears_only(*f.state, legacy_contract);
			REQUIRE(partial.arrears_repaid == Approx(2.0f));
		}
		f.state->world.monetary_account_set_balance(f.payer, 15.0f);
		economy::payroll::settle_factory(*f.state, f.factory, 1.0f, 1.0f);

		auto exact_record = economy::exact_person_economy::contract(*f.state, exact_contract_id);
		REQUIRE(exact_record);
		if(exact_has_smaller_arrears) {
			REQUIRE(economy::physical::concrete_labor::unpaid_wages(*f.state, legacy_contract) == Approx(0.0f));
			REQUIRE(exact_record->unpaid_wages == Approx(3.0f));
		} else {
			REQUIRE(economy::physical::concrete_labor::unpaid_wages(*f.state, legacy_contract) == Approx(3.0f));
			REQUIRE(exact_record->unpaid_wages == Approx(0.0f));
		}
		f.state->world.for_each_payroll_event([&](auto event) {
			if(f.state->world.payroll_event_get_occurred_on(event) == f.state->current_date)
				REQUIRE(f.state->world.payroll_event_get_gross_due(event) == Approx(0.0f));
		});
	};
	run(true);
	run(false);
}

TEST_CASE("partial canonical arrears repayment blocks current wages", "[economy][labor][payroll][arrears]") {
	auto run = [](bool exact_worker) {
		individual_concrete_labor_tests::fixture f;
		dcon::employment_contract_id legacy_contract{};
		economy::exact_person_economy::person_key exact_key{};
		uint64_t exact_contract_id = 0;
		if(exact_worker) {
			exact_key = exact_person_economy_tests::register_anchor(f);
			auto offer = f.offer(1, 10.0f);
			REQUIRE(economy::exact_person_economy::submit_application(*f.state, exact_key, offer, f.state->current_date));
			economy::physical::job_market::process_pending_applications(*f.state);
			exact_contract_id = economy::exact_person_economy::active_contracts_for_person(*f.state, exact_key).front();
		} else {
			legacy_contract = f.hire(1.0f, 10.0f);
		}
		f.state->world.monetary_account_set_balance(f.payer, 0.0f);
		economy::payroll::settle_factory(*f.state, f.factory, 1.0f, 1.0f);
		f.state->current_date += 1;
		f.state->world.monetary_account_set_balance(f.payer, 4.0f);
		economy::payroll::settle_factory(*f.state, f.factory, 1.0f, 1.0f);
		if(exact_worker) {
			auto record = economy::exact_person_economy::contract(*f.state, exact_contract_id);
			REQUIRE(record);
			REQUIRE(record->unpaid_wages == Approx(6.0f));
			auto account = economy::exact_person_economy::account_ref::from_exact(record->worker_account_id);
			REQUIRE(economy::exact_person_economy::balance(*f.state, account) == Approx(4.0f));
		} else {
			REQUIRE(economy::physical::concrete_labor::unpaid_wages(*f.state, legacy_contract) == Approx(6.0f));
			auto account = f.state->world.employment_contract_get_monetary_account_from_employment_contract_worker_account(legacy_contract);
			REQUIRE(economy::accounts::balance(*f.state, account) == Approx(4.0f));
		}
		f.state->world.for_each_payroll_event([&](auto event) {
			if(f.state->world.payroll_event_get_occurred_on(event) == f.state->current_date)
				REQUIRE(f.state->world.payroll_event_get_gross_due(event) == Approx(0.0f));
		});
	};
	run(false);
	run(true);
}

TEST_CASE("exact displacement cooldown and queue survive the isolated snapshot", "[economy][labor][persistence]") {
	individual_concrete_labor_tests::fixture f;
	auto worker = exact_person_economy_tests::register_anchor(f);
	auto offer = f.offer(1, 25.0f);
	REQUIRE(economy::exact_person_economy::submit_application(*f.state, worker, offer, f.state->current_date));
	economy::physical::job_market::process_pending_applications(*f.state);
	f.state->world.factory_set_productive_capacity(f.factory, 0.0f);
	economy::physical::labor_dynamics::process_factory_labor_dynamics(*f.state);
	REQUIRE(economy::exact_person_economy::separated_on_date(*f.state, worker, f.state->current_date));
	auto snapshot = economy::exact_person_economy::export_snapshot(*f.state);
	economy::exact_person_economy::clear_store(*f.state);
	REQUIRE(economy::exact_person_economy::import_snapshot(*f.state, snapshot));
	REQUIRE(economy::exact_person_economy::separated_on_date(*f.state, worker, f.state->current_date));
	REQUIRE(economy::physical::labor_dynamics::displaced_exact_workers(*f.state).size() == 1);
}

TEST_CASE("million-person exact population keeps contraction sparse", "[economy][labor][scale]") {
	individual_concrete_labor_tests::fixture f;
	persons::exact_population::cell_descriptor descriptor;
	descriptor.source_population_cell = 1000;
	descriptor.literal_count = 1'000'000;
	descriptor.bootstrap_base_day = f.state->current_date.to_raw_value() - 1;
	descriptor.demographic_seed = 1000;
	REQUIRE(persons::exact_population::register_synthetic_population_cell(*f.state, descriptor).result
		== persons::exact_population::status::created);
	economy::exact_person_economy::person_key worker{1000, 0};
	auto persons_before = f.state->world.person_size();
	auto actors_before = f.state->world.economic_actor_size();
	auto offer = f.offer(1, 25.0f);
	REQUIRE(economy::exact_person_economy::submit_application(*f.state, worker, offer, f.state->current_date));
	economy::physical::job_market::process_pending_applications(*f.state);
	f.state->world.factory_set_productive_capacity(f.factory, 0.0f);
	economy::physical::labor_dynamics::process_factory_labor_dynamics(*f.state);
	REQUIRE(persons::exact_population::logical_person_count(*f.state) == 1'000'000);
	REQUIRE(economy::physical::labor_dynamics::displaced_exact_workers(*f.state).size() == 1);
	REQUIRE(f.state->world.person_size() == persons_before);
	REQUIRE(f.state->world.economic_actor_size() == actors_before);
}

TEST_CASE("exact arrears below threshold do not quit, threshold arrears quit after payroll", "[economy][labor][arrears]") {
	individual_concrete_labor_tests::fixture f;
	auto worker = exact_person_economy_tests::register_anchor(f);
	auto offer = f.offer(1, 25.0f);
	REQUIRE(economy::exact_person_economy::submit_application(*f.state, worker, offer, f.state->current_date));
	economy::physical::job_market::process_pending_applications(*f.state);
	auto contract_id = economy::exact_person_economy::active_contracts_for_person(*f.state, worker).front();
	f.state->world.monetary_account_set_balance(f.payer, 10.0f);
	economy::payroll::settle_factory(*f.state, f.factory, 1.0f, 1.0f);
	REQUIRE(economy::exact_person_economy::contract(*f.state, contract_id)->unpaid_wages == Approx(15.0f));
	f.state->world.monetary_account_set_balance(f.payer, 100.0f);
	economy::physical::labor_dynamics::process_factory_labor_dynamics(*f.state);
	REQUIRE(economy::exact_person_economy::active_contracts_for_person(*f.state, worker).size() == 1);
	f.state->current_date += 1;
	f.state->world.monetary_account_set_balance(f.payer, 0.0f);
	economy::payroll::settle_factory(*f.state, f.factory, 1.0f, 1.0f);
	REQUIRE(economy::exact_person_economy::contract(*f.state, contract_id)->unpaid_wages == Approx(40.0f));
	economy::physical::labor_dynamics::process_factory_labor_dynamics(*f.state);
	REQUIRE(economy::exact_person_economy::active_contracts_for_person(*f.state, worker).empty());
	REQUIRE(economy::physical::labor_dynamics::is_unemployed(*f.state, worker));
	REQUIRE(economy::exact_person_economy::contract(*f.state, contract_id)->unpaid_wages == Approx(40.0f));
	auto event = economy::physical::labor_dynamics::separation_event_at(*f.state, 1);
	REQUIRE(event);
	REQUIRE(event->reason == economy::physical::labor_dynamics::separation_reason::worker_quit_arrears);
	REQUIRE_FALSE(economy::exact_person_economy::submit_application(*f.state, worker,
		f.offer(1, 30.0f), f.state->current_date));
}

TEST_CASE("exact invalid payroll account does not double-count current due", "[economy][payroll][exact]") {
	individual_concrete_labor_tests::fixture f;
	auto worker = exact_person_economy_tests::register_anchor(f);
	auto offer = f.offer(1, 25.0f);
	REQUIRE(economy::exact_person_economy::submit_application(*f.state, worker, offer, f.state->current_date));
	economy::physical::job_market::process_pending_applications(*f.state);
	auto contract_id = economy::exact_person_economy::active_contracts_for_person(*f.state, worker).front();
	f.state->world.delete_monetary_account(f.payer);
	auto result = economy::exact_person_economy::settle_contract_wage(*f.state, contract_id);
	REQUIRE(result.arrears_after == Approx(25.0f));
	REQUIRE(result.unpaid == Approx(25.0f));
	REQUIRE(economy::exact_person_economy::contract(*f.state, contract_id)->unpaid_wages == Approx(25.0f));
}
