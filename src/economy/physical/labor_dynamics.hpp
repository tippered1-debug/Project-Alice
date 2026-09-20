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
	worker_quit_arrears = 2
};

enum class contract_kind : uint8_t { legacy = 0, exact = 1 };

struct separation_event {
	uint64_t id = 0;
	sys::date date{};
	dcon::factory_id factory{};
	dcon::economic_actor_id employer{};
	contract_kind worker_contract_kind = contract_kind::legacy;
	// DCON index for legacy contracts; exact contract ID for exact contracts.
	uint64_t contract_id = 0;
	dcon::person_id legacy_worker{};
	persons::exact_population::person_key exact_worker{};
	separation_reason reason = separation_reason::employer_layoff;
	float labor_capacity = 0.0f;
	float wage_rate = 0.0f;
	float unpaid_wages = 0.0f;
};

bool is_unemployed(sys::state const&, dcon::person_id);
bool is_unemployed(sys::state const&, persons::exact_population::person_key);
bool quit_employment(sys::state&, dcon::employment_contract_id,
	separation_reason reason = separation_reason::worker_quit);
bool quit_exact_employment(sys::state&, uint64_t,
	separation_reason reason = separation_reason::worker_quit);

void process_factory_labor_dynamics(sys::state&);
void process_displaced_job_search(sys::state&);

uint64_t separation_event_count(sys::state const&);
std::optional<separation_event> separation_event_at(sys::state const&, uint64_t id);
std::vector<persons::exact_population::person_key> displaced_exact_workers(sys::state const&);
bool legacy_separated_on_date(sys::state const&, dcon::person_id, sys::date);

} // namespace economy::physical::labor_dynamics
