#pragma once

#include "dcon_generated.hpp"
#include "date_interface.hpp"
#include "governance/governance.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace sys { class state; }

namespace persons {

// Stable identity for a logical human. This remains compact and independent
// of dcon::person_id, which is only allocated for sparse detailed profiles.
struct person_key {
	uint32_t source_population_cell = 0;
	uint64_t ordinal = 0;

	friend bool operator==(person_key left, person_key right) noexcept {
		return left.source_population_cell == right.source_population_cell && left.ordinal == right.ordinal;
	}
};

struct person_key_hash {
	std::size_t operator()(person_key key) const noexcept;
};

struct population_transfer_result {
	uint64_t people_moved = 0;
	bool complete = true;
};

enum class death_cause : uint8_t {
	unspecified = 0,
	demographic = 1,
	natural = 2,
	combat = 3,
	scripted = 4,
	attrition = 5
};

enum class population_transition_cause : uint8_t {
	internal_migration = 0,
	colonial_migration = 1,
	immigration = 2,
	promotion = 3,
	demotion = 4,
	assimilation = 5,
	household_relocation = 6,
	population_merge = 7,
	scripted_reclassification = 8
};

namespace policy {
inline constexpr int32_t minimum_working_age_days = 14 * 365;
inline constexpr int32_t maximum_working_age_days = 65 * 365;
}

using birth_day_index_t = int32_t;

// Low-level structural occupancy primitives. Normal governance commands must
// use governance::actions authorization APIs.
dcon::person_id create_person(sys::state&, sys::date birth_date);
dcon::person_id create_person_with_birth_day(sys::state&, birth_day_index_t);
namespace detail {
dcon::person_id create_profile_record_with_birth_day(sys::state&, birth_day_index_t);
}
dcon::economic_actor_id actor_for_person(sys::state const&, dcon::person_id);
bool alive(sys::state const&, dcon::person_id);
birth_day_index_t birth_day_index(sys::state const&, dcon::person_id);
bool has_birth_day(sys::state const&, dcon::person_id);
int32_t age_days(sys::state const&, dcon::person_id);
int32_t age_years(sys::state const&, dcon::person_id);
bool born_on_or_before(sys::state const&, dcon::person_id, sys::date);
bool is_work_eligible(sys::state const&, dcon::person_id);
dcon::office_tenure_id appoint_person(sys::state&, dcon::person_id, dcon::office_id, sys::date);
bool remove_from_office(sys::state&, dcon::office_id, sys::date);
dcon::person_id occupant_of(sys::state const&, dcon::office_id);
std::vector<dcon::office_id> active_offices_of(sys::state const&, dcon::person_id);
dcon::office_tenure_id active_tenure_for(sys::state const&, dcon::office_id);
bool person_has_authority(sys::state const&, dcon::person_id, governance::authority_kind, dcon::nation_id);
bool person_has_authority(sys::state const&, dcon::person_id, governance::authority_kind, dcon::territorial_unit_id);
dcon::office_tenure_id authority_tenure_on_or_before(sys::state const&, dcon::person_id, governance::authority_kind, dcon::nation_id, sys::date);
bool mark_dead(sys::state&, dcon::person_id, sys::date);
dcon::site_id home_site(sys::state const&, dcon::person_id);

// Canonical person lifecycle. Storage details live in exact_population; these
// APIs are the stable boundary for domain systems.
bool exists(sys::state const&, person_key);
bool alive(sys::state const&, person_key);
birth_day_index_t birth_day_index(sys::state const&, person_key);
int32_t age_days(sys::state const&, person_key, sys::date);
int32_t age_years(sys::state const&, person_key, sys::date);
int32_t death_day_index(sys::state const&, person_key);
dcon::pop_id current_population(sys::state const&, person_key);
uint32_t current_population_cell(sys::state const&, person_key);
dcon::site_id home_site(sys::state const&, person_key);
dcon::culture_id current_culture(sys::state const&, person_key);
dcon::religion_id current_religion(sys::state const&, person_key);
dcon::pop_type_id pop_type(sys::state const&, person_key);
dcon::culture_id source_culture(sys::state const&, person_key);
dcon::religion_id source_religion(sys::state const&, person_key);
dcon::pop_type_id source_pop_type(sys::state const&, person_key);
bool is_source_workforce_anchor(sys::state const&, person_key);
person_key first_living_person_in_population(sys::state const&, dcon::pop_id);
bool register_population_cell(sys::state&, dcon::pop_id, dcon::site_id home_site = {});
uint32_t source_population_cell_for_population(sys::state const&, dcon::pop_id);
dcon::pop_id population_for_source_cell(sys::state const&, uint32_t source_population_cell);
uint64_t living_people_in_population_cell(sys::state const&, uint32_t source_population_cell);
bool project_population_membership(sys::state&);
void retire_population_cell(sys::state&, dcon::pop_id);
population_transfer_result transfer_population(sys::state&, dcon::pop_id source,
	dcon::pop_id destination, float population_amount, population_transition_cause);
bool set_home_site(sys::state&, person_key, dcon::site_id);
bool transfer_population_membership(sys::state&, person_key, dcon::pop_id,
	population_transition_cause);
bool can_kill_person(sys::state const&, person_key, sys::date, death_cause);
bool kill_person(sys::state&, person_key, sys::date, death_cause, bool project_population = true);
int64_t adjust_population_size(sys::state&, dcon::pop_id, double size_delta,
	death_cause cause = death_cause::demographic);
dcon::person_id materialized_profile(sys::state const&, person_key);
dcon::person_id materialize_profile(sys::state&, person_key);
person_key canonical_key(sys::state const&, dcon::person_id);
uint64_t materialized_profile_count(sys::state const&);

bool validate_person_kernel(sys::state const&);
uint64_t person_kernel_checksum(sys::state const&);

} // namespace persons
