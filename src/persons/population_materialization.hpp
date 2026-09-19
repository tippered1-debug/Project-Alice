#pragma once

#include "dcon_generated.hpp"

#include <cstdint>
#include <vector>

namespace sys { class state; }

namespace persons::population_materialization {

inline constexpr uint32_t bootstrap_semantics_version = 3;
inline constexpr uint32_t literal_person_multiplier = 4;
inline constexpr uint32_t supported_person_capacity = 100000;
inline constexpr uint32_t supported_economic_actor_capacity = 50000;

struct person_key {
	uint32_t source_population_cell = 0;
	uint32_t ordinal = 0;
};

enum class materialization_status : uint8_t {
	created,
	already_materialized,
	version_mismatch,
	invalid_source,
	invalid_home_site,
	missing_home_province,
	overflow,
	capacity_exceeded
};

struct cell_materialization_result {
	std::vector<dcon::person_id> persons;
	materialization_status status = materialization_status::invalid_source;
};

struct initial_population_materialization_result {
	std::vector<dcon::person_id> persons;
	materialization_status status = materialization_status::invalid_source;
};

struct population_estimate {
	uint64_t source_population_cells = 0;
	double source_population_units = 0.0;
	uint64_t intended_literal_persons = 0;
	uint64_t intended_economic_actors = 0;
	uint64_t largest_source_cell = 0;
	uint32_t semantics_version = bootstrap_semantics_version;
	bool intended_literal_persons_known = true;
	bool capacity_exceeded = false;
	bool overflow = false;
};

struct materialization_measurement {
	uint64_t elapsed_microseconds = 0;
	uint64_t persons_created = 0;
	uint64_t economic_actors_created = 0;
	uint64_t persons_total = 0;
	uint64_t economic_actors_total = 0;
	uint64_t materialization_markers = 0;
	double persons_per_second = 0.0;
};

cell_materialization_result materialize_population_cell_with_status(sys::state&, dcon::pop_id,
	dcon::site_id home_site = {});
std::vector<dcon::person_id> materialize_population_cell(sys::state&, dcon::pop_id,
	dcon::site_id home_site = {});
initial_population_materialization_result materialize_initial_population_with_status(sys::state&);
std::vector<dcon::person_id> materialize_initial_population(sys::state&);
population_estimate estimate_initial_population(sys::state const&);
materialization_measurement measure_initial_population_materialization(sys::state&);
materialization_measurement measure_synthetic_population_materialization(sys::state&, uint32_t);
materialization_status materialization_status_for_population_cell(sys::state const&, dcon::pop_id);
uint32_t bootstrap_age_days(person_key);
persons::birth_day_index_t bootstrap_birth_day(sys::date, person_key);

person_key key_for_person(sys::state const&, dcon::person_id);
bool is_materialized_person(sys::state const&, dcon::person_id);
dcon::site_id home_site_for_materialized_person(sys::state const&, dcon::person_id);

} // namespace persons::population_materialization
