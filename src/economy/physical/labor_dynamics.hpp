#pragma once

#include "dcon_generated.hpp"
#include "date_interface.hpp"
#include "persons/exact_population.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace sys { class state; }

namespace economy::physical::labor_dynamics {

enum class separation_reason : uint8_t {
	employer_layoff = 0,
	worker_quit = 1,
	worker_quit_arrears = 2,
	worker_death = 3
};

struct separation_event {
	uint64_t id = 0;
	sys::date date{};
	dcon::factory_id factory{};
	dcon::economic_actor_id employer{};
	// Exact firm contract ID.
	uint64_t contract_id = 0;
	persons::exact_population::person_key exact_worker{};
	separation_reason reason = separation_reason::employer_layoff;
	float labor_capacity = 0.0f;
	float wage_rate = 0.0f;
	float unpaid_wages = 0.0f;
};

struct snapshot {
	uint32_t version = 2;
	uint64_t next_event_id = 1;
	std::vector<separation_event> events;
};

bool is_unemployed(sys::state const&, persons::exact_population::person_key);
bool quit_exact_employment(sys::state&, uint64_t,
	separation_reason reason = separation_reason::worker_quit);

void process_factory_labor_dynamics(sys::state&);
void process_displaced_job_search(sys::state&);
void retire_dead_exact_workers(sys::state&);

uint64_t separation_event_count(sys::state const&);
std::optional<separation_event> separation_event_at(sys::state const&, uint64_t id);
std::vector<persons::exact_population::person_key> displaced_exact_workers(sys::state const&);
snapshot export_snapshot(sys::state const&);
bool import_snapshot(sys::state&, snapshot const&);
void initialize_empty_store(sys::state&);
void clear_store(sys::state&);

} // namespace economy::physical::labor_dynamics
