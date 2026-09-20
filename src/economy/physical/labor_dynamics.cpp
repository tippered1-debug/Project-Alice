#include "labor_dynamics.hpp"

#include "actors/organizations/organizations.hpp"
#include "compat/alice/legacy_bridge.hpp"
#include "economy/exact_person_economy.hpp"
#include "economy/causal_order.hpp"
#include "economy/firm_agency.hpp"
#include "concrete_labor.hpp"
#include "job_market.hpp"
#include "persons/persons.hpp"
#include "system_state.hpp"
#include "economy/relations/relations.hpp"

#include <algorithm>
#include <cmath>
#include <memory>

namespace economy::physical {

struct labor_dynamics_store {
	std::vector<labor_dynamics::separation_event> events;
	std::vector<std::pair<dcon::person_id, sys::date>> legacy_separations;
	uint64_t next_event_id = 1;
};

} // namespace economy::physical

namespace economy::physical::labor_dynamics {
namespace {

constexpr float epsilon = 1.0e-5f;

std::shared_ptr<labor_dynamics_store> ensure_store(sys::state& state) {
	if(!state.labor_dynamics)
		state.labor_dynamics = std::make_shared<labor_dynamics_store>();
	return state.labor_dynamics;
}

std::shared_ptr<labor_dynamics_store> ensure_store(sys::state const& state) {
	return ensure_store(const_cast<sys::state&>(state));
}

uint64_t next_event_id(labor_dynamics_store& store) {
	if(store.next_event_id == 0) return 0;
	return store.next_event_id++;
}

void remember_legacy_separation(sys::state& state, dcon::person_id worker) {
	if(!worker || !state.current_date) return;
	for(auto& record : ensure_store(state)->legacy_separations)
		if(record.first == worker) { record.second = state.current_date; return; }
	ensure_store(state)->legacy_separations.emplace_back(worker, state.current_date);
}

void record_legacy_separation(sys::state& state, dcon::employment_contract_id contract,
	separation_reason reason) {
	auto worker = state.world.employment_contract_get_person_from_employment_contract_person(contract);
	auto employer = state.world.employment_contract_get_economic_actor_from_employment_contract_employer(contract);
	auto factory = state.world.employment_contract_get_factory_from_employment_contract_factory(contract);
	auto event = separation_event{};
	event.id = next_event_id(*ensure_store(state));
	event.date = state.current_date;
	event.factory = factory;
	event.employer = employer;
	event.worker_contract_kind = contract_kind::legacy;
	event.contract_id = contract.index();
	event.legacy_worker = worker;
	event.reason = reason;
	event.labor_capacity = state.world.employment_contract_get_labor_capacity(contract);
	event.wage_rate = state.world.employment_contract_get_wage_rate(contract);
	event.unpaid_wages = concrete_labor::unpaid_wages(state, contract);
	if(event.id) ensure_store(state)->events.push_back(event);
	remember_legacy_separation(state, worker);
}

void record_exact_separation(sys::state& state, exact_person_economy::contract_record const& contract,
	separation_reason reason) {
	auto event = separation_event{};
	event.id = next_event_id(*ensure_store(state));
	event.date = state.current_date;
	event.factory = contract.factory;
	event.employer = contract.employer;
	event.worker_contract_kind = contract_kind::exact;
	event.contract_id = contract.id;
	event.exact_worker = contract.worker;
	event.reason = reason;
	event.labor_capacity = contract.labor_capacity;
	event.wage_rate = contract.wage_rate;
	event.unpaid_wages = contract.unpaid_wages;
	if(event.id) ensure_store(state)->events.push_back(event);
}

bool separate_legacy(sys::state& state, dcon::employment_contract_id contract, separation_reason reason) {
	if(!contract || !state.world.employment_contract_is_valid(contract)
		|| state.world.employment_contract_get_status(contract) != uint8_t(concrete_labor::contract_status::active)) return false;
	auto worker = state.world.employment_contract_get_person_from_employment_contract_person(contract);
	for(auto application : job_market::applications_for_person(state, worker))
		(void)job_market::withdraw_job_application(state, application);
	if(!concrete_labor::terminate_employment_contract(state, contract, state.current_date)) return false;
	record_legacy_separation(state, contract, reason);
	return true;
}

bool separate_exact(sys::state& state, uint64_t contract_id, separation_reason reason) {
	auto record = exact_person_economy::contract(state, contract_id);
	if(!record || record->status != exact_person_economy::contract_status::active) return false;
	(void)exact_person_economy::withdraw_pending_applications(state, record->worker);
	if(!exact_person_economy::end_contract(state, contract_id,
		exact_person_economy::contract_status::terminated, state.current_date)) return false;
	exact_person_economy::note_separation(state, record->worker, state.current_date);
	if(exact_person_economy::is_labor_force_participant(state, record->worker))
		exact_person_economy::enqueue_displaced_worker(state, record->worker);
	record_exact_separation(state, *record, reason);
	return true;
}

struct candidate {
	contract_kind kind = contract_kind::legacy;
	dcon::employment_contract_id legacy{};
	uint64_t exact = 0;
	float capacity = 0.0f;
	float daily_cost = 0.0f;
	sys::date start{};
	uint64_t causal_sequence = 0;
	uint64_t stable_id = 0;
};

bool candidate_before(candidate const& left, candidate const& right) {
	if(left.daily_cost != right.daily_cost) return left.daily_cost > right.daily_cost;
	if(left.start != right.start) return left.start > right.start;
	if(left.causal_sequence != right.causal_sequence) return left.causal_sequence < right.causal_sequence;
	return left.stable_id < right.stable_id;
}

void close_contradictory_offers(sys::state& state, dcon::factory_id factory) {
	for(auto offer : job_market::open_offers_for_factory(state, factory))
		(void)job_market::close_job_offer(state, offer);
}

void process_arrears_quits(sys::state& state, dcon::factory_id factory) {
	for(auto contract : concrete_labor::active_contracts_for_factory(state, factory)) {
		auto capacity = state.world.employment_contract_get_labor_capacity(contract);
		auto wage = state.world.employment_contract_get_wage_rate(contract);
		auto threshold = wage * capacity;
		if(std::isfinite(threshold) && threshold > epsilon
			&& concrete_labor::unpaid_wages(state, contract) + epsilon >= threshold)
			(void)separate_legacy(state, contract, separation_reason::worker_quit_arrears);
	}
	for(auto contract_id : exact_person_economy::active_contracts_for_factory(state, factory)) {
		auto record = exact_person_economy::contract(state, contract_id);
		if(!record) continue;
		auto threshold = record->wage_rate * record->labor_capacity;
		if(std::isfinite(threshold) && threshold > epsilon
			&& record->unpaid_wages + epsilon >= threshold)
			(void)separate_exact(state, contract_id, separation_reason::worker_quit_arrears);
	}
}

} // namespace

bool is_unemployed(sys::state const& state, dcon::person_id person) {
	return concrete_labor::is_unemployed(state, person);
}

bool is_unemployed(sys::state const& state, persons::exact_population::person_key worker) {
	return exact_person_economy::is_unemployed(state, worker);
}

bool quit_employment(sys::state& state, dcon::employment_contract_id contract, separation_reason reason) {
	return separate_legacy(state, contract, reason);
}

bool quit_exact_employment(sys::state& state, uint64_t contract, separation_reason reason) {
	return separate_exact(state, contract, reason);
}

void process_factory_labor_dynamics(sys::state& state) {
	std::vector<dcon::factory_id> factories;
	state.world.for_each_factory([&](auto factory) { factories.push_back(factory); });
	std::sort(factories.begin(), factories.end(), [](auto left, auto right) { return left.index() < right.index(); });
	for(auto factory : factories) {
		if(!state.world.factory_get_canonical_production(factory)) continue;
		auto decision = firm_agency::decide_factory(state, factory);
		auto desired = decision.desired_units;
		auto supplied = concrete_labor::labor_supplied_to_factory(state, factory);
		if(!std::isfinite(desired) || !std::isfinite(supplied)) continue;
		// Payroll has already run in the economy tick. Arrears therefore trigger
		// a worker-initiated separation before employer surplus selection.
		process_arrears_quits(state, factory);
		supplied = concrete_labor::labor_supplied_to_factory(state, factory);
		if(supplied >= desired - epsilon) close_contradictory_offers(state, factory);
		if(supplied > desired + epsilon) {
			std::vector<candidate> candidates;
			for(auto contract : concrete_labor::active_contracts_for_factory(state, factory)) {
				auto capacity = state.world.employment_contract_get_labor_capacity(contract);
				auto period = state.world.employment_contract_get_pay_period_days(contract);
				auto wage = state.world.employment_contract_get_wage_rate(contract);
				if(!std::isfinite(capacity) || capacity <= epsilon || !std::isfinite(wage) || period == 0) continue;
				candidates.push_back({contract_kind::legacy, contract, 0, capacity,
					wage * capacity / float(period), state.world.employment_contract_get_start_date(contract),
					economy::causal_order::sequence_for_dcon(state, economy::causal_order::event_kind::employment_contract,
						uint64_t(contract.index())), uint64_t(contract.index())});
			}
			for(auto contract_id : exact_person_economy::active_contracts_for_factory(state, factory)) {
				auto record = exact_person_economy::contract(state, contract_id);
				if(!record || !std::isfinite(record->labor_capacity) || record->labor_capacity <= epsilon
					|| !std::isfinite(record->wage_rate) || record->pay_period_days == 0) continue;
				candidates.push_back({contract_kind::exact, {}, contract_id, record->labor_capacity,
					record->wage_rate * record->labor_capacity / float(record->pay_period_days), record->start_date,
					record->causal_sequence, contract_id});
			}
			std::sort(candidates.begin(), candidates.end(), candidate_before);
			for(auto const& candidate : candidates) {
				if(desired <= epsilon || supplied - candidate.capacity >= desired - epsilon) {
					bool separated = candidate.kind == contract_kind::legacy
						? separate_legacy(state, candidate.legacy, separation_reason::employer_layoff)
						: separate_exact(state, candidate.exact, separation_reason::employer_layoff);
					if(separated) supplied -= candidate.capacity;
				}
			}
		}
	}
}

void process_displaced_job_search(sys::state& state) {
	for(auto worker : exact_person_economy::displaced_workers(state)) {
		if(!persons::exact_population::exists(state, worker)
			|| !persons::exact_population::alive(state, worker)
			|| !exact_person_economy::is_labor_force_participant(state, worker)
			|| !exact_person_economy::is_unemployed(state, worker)) {
			exact_person_economy::remove_displaced_worker(state, worker);
			continue;
		}
		if(exact_person_economy::separated_on_date(state, worker, state.current_date)) continue;
		exact_person_economy::process_job_search_for_exact_person(state, worker);
		if(exact_person_economy::person_has_active_contract(state, worker))
			exact_person_economy::remove_displaced_worker(state, worker);
	}
}

uint64_t separation_event_count(sys::state const& state) {
	return uint64_t(ensure_store(state)->events.size());
}

std::optional<separation_event> separation_event_at(sys::state const& state, uint64_t id) {
	for(auto const& event : ensure_store(state)->events)
		if(event.id == id) return event;
	return std::nullopt;
}

std::vector<persons::exact_population::person_key> displaced_exact_workers(sys::state const& state) {
	return exact_person_economy::displaced_workers(state);
}

bool legacy_separated_on_date(sys::state const& state, dcon::person_id person, sys::date date) {
	if(!person || !date) return false;
	for(auto const& [worker, separated] : ensure_store(state)->legacy_separations)
		if(worker == person) return separated == date;
	return false;
}

} // namespace economy::physical::labor_dynamics
