#pragma once

#include "dcon_generated.hpp"

#include <cstdint>
#include <vector>

namespace sys { class state; }

namespace persons::population_materialization {

struct person_key {
	uint32_t source_population_cell = 0;
	uint32_t ordinal = 0;
};

struct materialization_measurement {
	uint64_t elapsed_microseconds = 0;
	uint64_t persons_created = 0;
	uint64_t persons_total = 0;
	uint64_t economic_actors_total = 0;
	uint64_t materialization_markers = 0;
};

std::vector<dcon::person_id> materialize_population_cell(sys::state&, dcon::pop_id,
	dcon::site_id home_site = {});
std::vector<dcon::person_id> materialize_initial_population(sys::state&);
materialization_measurement measure_initial_population_materialization(sys::state&);

person_key key_for_person(sys::state const&, dcon::person_id);
bool is_materialized_person(sys::state const&, dcon::person_id);
dcon::site_id home_site_for_materialized_person(sys::state const&, dcon::person_id);

} // namespace persons::population_materialization
