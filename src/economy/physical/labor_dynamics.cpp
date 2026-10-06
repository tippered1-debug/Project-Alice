#include "labor_dynamics.hpp"
#include "economy/households.hpp"

#include "economy/exact_person_economy.hpp"
#include "economy/collective_labor.hpp"
#include "economy/causal_order.hpp"
#include "economy/firm_agency.hpp"
#include "job_market.hpp"
#include "system_state.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <memory>

namespace economy::physical {

struct labor_dynamics_store {
	std::vector<labor_dynamics::separation_event> events;
	uint64_t next_event_id = 1;
};

} // namespace economy::physical

namespace economy::physical::labor_dynamics {
namespace {

constexpr float epsilon = 1.0e-5f;

std::shared_ptr<labor_dynamics_store> ensure_store(sys::state& state) {
	assert(state.labor_dynamics && "labor dynamics store must be initialized before simulation");
	if(!state.labor_dynamics) std::abort();
	return state.labor_dynamics;
}

std::shared_ptr<labor_dynamics_store> ensure_store(sys::state const& state) {
	assert(state.labor_dynamics && "labor dynamics store must be initialized before lookup");
	if(!state.labor_dynamics) std::abort();
	return state.labor_dynamics;
}

uint64_t next_event_id(labor_dynamics_store& store) {
	if(store.next_event_id == 0) return 0;
	return store.next_event_id++;
}

void record_separation(sys::state& state,
	exact_person_economy::contract_record const& contract, separation_reason reason, sys::date date) {
	auto event = separation_event{};
	event.id = next_event_id(*ensure_store(state));
	event.date = date;
	event.factory = contract.factory;
	event.employer = contract.employer;
	event.contract_id = contract.id;
	event.exact_worker = contract.worker;
	event.reason = reason;
	event.labor_capacity = contract.labor_capacity;
	event.wage_rate = contract.wage_rate;
	event.unpaid_wages = contract.unpaid_wages;
	if(event.id) ensure_store(state)->events.push_back(event);
}

bool separate_exact(sys::state& state, uint64_t contract_id, separation_reason reason, sys::date date) {
	auto record = exact_person_economy::contract(state, contract_id);
	if(!record || record->status != exact_person_economy::contract_status::active) return false;
	(void)exact_person_economy::withdraw_pending_applications(state, record->worker);
	auto end_reason = exact_person_economy::contract_end_reason::employer_layoff;
	switch(reason) {
	case separation_reason::employer_layoff:
		end_reason = exact_person_economy::contract_end_reason::employer_layoff;
		break;
	case separation_reason::worker_quit:
		end_reason = exact_person_economy::contract_end_reason::voluntary_quit;
		break;
	case separation_reason::worker_quit_arrears:
		end_reason = exact_person_economy::contract_end_reason::arrears_quit;
		break;
	case separation_reason::worker_death:
		end_reason = exact_person_economy::contract_end_reason::death;
		break;
	}
	if(!exact_person_economy::end_contract(state, contract_id,
		exact_person_economy::contract_status::terminated, date, end_reason)) return false;
	exact_person_economy::note_separation(state, record->worker, date);
	if(reason != separation_reason::worker_death && persons::alive(state, record->worker) && exact_person_economy::is_labor_force_participant(state, record->worker))
		exact_person_economy::enqueue_displaced_worker(state, record->worker);
	record_separation(state, *record, reason, date);
	return true;
}

struct candidate {
	uint64_t contract_id = 0;
	float capacity = 0.0f;
	float daily_cost = 0.0f;
	sys::date start{};
	uint64_t causal_sequence = 0;
};

bool candidate_before(candidate const& left, candidate const& right) {
	if(left.daily_cost != right.daily_cost) return left.daily_cost > right.daily_cost;
	if(left.start != right.start) return left.start > right.start;
	if(left.causal_sequence != right.causal_sequence) return left.causal_sequence < right.causal_sequence;
	return left.contract_id < right.contract_id;
}

void close_contradictory_offers(sys::state& state, dcon::factory_id factory) {
	for(auto offer : job_market::open_offers_for_factory(state, factory))
		(void)job_market::close_job_offer(state, offer);
}

void process_arrears_quits(sys::state& state, dcon::factory_id factory) {
	for(auto contract_id : exact_person_economy::active_contracts_for_factory(state, factory)) {
		auto record = exact_person_economy::contract(state, contract_id);
		if(!record) continue;
		auto threshold = record->wage_rate * record->labor_capacity;
		if(std::isfinite(threshold) && threshold > epsilon && record->unpaid_wages + epsilon >= threshold)
			(void)separate_exact(state, contract_id, separation_reason::worker_quit_arrears, state.current_date);
	}
}

} // namespace

bool is_unemployed(sys::state const& state, persons::person_key worker) {
	return exact_person_economy::is_unemployed(state, worker);
}

bool quit_exact_employment(sys::state& state, uint64_t contract, separation_reason reason) {
	return separate_exact(state, contract, reason, state.current_date);
}

void close_person_relations_on_death(sys::state& state, persons::person_key person, sys::date date) {
	if(!state.exact_person_economy || !state.labor_dynamics
		|| !persons::exists(state, person) || !date) return;
	for(auto application_id : exact_person_economy::applications_for_person(state, person))
		(void)exact_person_economy::withdraw_application(state, application_id);
	auto snapshot = exact_person_economy::export_snapshot(state);
	for(auto const& contract : snapshot.contracts)
		if(contract.worker == person && contract.status == exact_person_economy::contract_status::active)
			(void)separate_exact(state, contract.id, separation_reason::worker_death, date);
}

void process_factory_labor_dynamics(sys::state& state) {
	std::vector<dcon::factory_id> factories;
	state.world.for_each_factory([&](auto factory) { factories.push_back(factory); });
	std::sort(factories.begin(), factories.end(), [](auto left, auto right) { return left.index() < right.index(); });
	for(auto factory : factories) {
		auto desired = firm_agency::decide_factory(state, factory).desired_units;
		auto supplied = exact_person_economy::labor_supplied_to_factory(state, factory);
		if(!std::isfinite(desired) || !std::isfinite(supplied)) continue;
		process_arrears_quits(state, factory);
		supplied = exact_person_economy::labor_supplied_to_factory(state, factory);
		if(supplied >= desired - epsilon) close_contradictory_offers(state, factory);
		if(supplied <= desired + epsilon) continue;
		std::vector<candidate> candidates;
		for(auto contract_id : exact_person_economy::active_contracts_for_factory(state, factory)) {
			auto record = exact_person_economy::contract(state, contract_id);
			if(!record || !std::isfinite(record->labor_capacity) || record->labor_capacity <= epsilon || !std::isfinite(record->wage_rate) || record->pay_period_days == 0) continue;
			candidates.push_back({contract_id, record->labor_capacity,
				record->wage_rate * record->labor_capacity / float(record->pay_period_days)
					+ economy::collective_labor::expected_severance_for_contract(state,
						contract_id, state.current_date) / 365.0f,
				record->start_date, record->causal_sequence});
		}
		std::sort(candidates.begin(), candidates.end(), candidate_before);
		for(auto const& candidate : candidates) {
			if(desired <= epsilon || supplied - candidate.capacity >= desired - epsilon) {
				if(separate_exact(state, candidate.contract_id, separation_reason::employer_layoff, state.current_date))
					supplied -= candidate.capacity;
			}
		}
	}
}

void process_displaced_job_search(sys::state& state) {
	for(auto worker : exact_person_economy::displaced_workers(state)) {
		if(!persons::exists(state, worker) || !persons::alive(state, worker) || !exact_person_economy::is_labor_force_participant(state, worker) || !exact_person_economy::is_unemployed(state, worker)) {
			exact_person_economy::remove_displaced_worker(state, worker);
			continue;
		}
		if(exact_person_economy::separated_on_date(state, worker, state.current_date)) continue;
		exact_person_economy::process_job_search_for_exact_person(state, worker);
		if(exact_person_economy::person_has_active_contract(state, worker))
			exact_person_economy::remove_displaced_worker(state, worker);
		// A rural worker who stays without a job goes home to the cohort.
		else (void)households::rejoin(state, worker);
	}
}

void retire_dead_exact_workers(sys::state& state) {
	auto snapshot = exact_person_economy::export_snapshot(state);
	for(auto const& application : snapshot.applications)
		if(application.status == exact_person_economy::application_status::pending && !persons::alive(state, application.worker))
			(void)exact_person_economy::withdraw_application(state, application.id);
	for(auto const& contract : snapshot.contracts)
		if(contract.status == exact_person_economy::contract_status::active && !persons::alive(state, contract.worker))
			(void)separate_exact(state, contract.id, separation_reason::worker_death, state.current_date);
}

uint64_t separation_event_count(sys::state const& state) {
	return uint64_t(ensure_store(state)->events.size());
}

std::optional<separation_event> separation_event_at(sys::state const& state, uint64_t id) {
	if(id == 0 || id > ensure_store(state)->events.size()) return std::nullopt;
	auto const& event = ensure_store(state)->events[size_t(id - 1)];
	return event.id == id ? std::optional<separation_event>(event) : std::nullopt;
}

std::vector<persons::person_key> displaced_exact_workers(sys::state const& state) {
	return exact_person_economy::displaced_workers(state);
}

snapshot export_snapshot(sys::state const& state) {
	snapshot result;
	auto store = ensure_store(state);
	result.next_event_id = store->next_event_id;
	result.events = store->events;
	return result;
}

bool import_snapshot(sys::state& state, snapshot const& value) {
	if(value.version != 2 || value.next_event_id == 0) return false;
	auto candidate = std::make_shared<labor_dynamics_store>();
	candidate->next_event_id = value.next_event_id;
	uint64_t previous_id = 0;
	for(auto const& event : value.events) {
		if(event.id == 0 || event.id <= previous_id || event.id >= value.next_event_id || event.contract_id == 0 || !persons::exists(state, event.exact_worker) || uint8_t(event.reason) > uint8_t(separation_reason::worker_death) || !std::isfinite(event.labor_capacity) || event.labor_capacity < 0.0f || !std::isfinite(event.wage_rate) || event.wage_rate < 0.0f || !std::isfinite(event.unpaid_wages) || event.unpaid_wages < 0.0f || event.factory && !state.world.factory_is_valid(event.factory) || event.employer && !state.world.economic_actor_is_valid(event.employer) || state.current_date && event.date > state.current_date) return false;
		candidate->events.push_back(event);
		previous_id = event.id;
	}
	state.labor_dynamics = std::move(candidate);
	return true;
}

void clear_store(sys::state& state) {
	state.labor_dynamics.reset();
}

void initialize_empty_store(sys::state& state) {
	assert(!state.labor_dynamics && "labor dynamics store initialized more than once");
	if(state.labor_dynamics) std::abort();
	state.labor_dynamics = std::make_shared<labor_dynamics_store>();
}

} // namespace economy::physical::labor_dynamics
