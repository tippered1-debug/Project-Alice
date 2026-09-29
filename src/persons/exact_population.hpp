#pragma once

#include "dcon_generated.hpp"
#include "persons.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace sys { class state; }

namespace persons::exact_population {

inline constexpr uint32_t bootstrap_semantics_version = 3;
inline constexpr uint32_t demographic_policy_version = 1;

// This is the canonical identity of a logical human in the scalable store.
// It is deliberately independent of dcon::person_id.
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

struct cell_descriptor {
	uint32_t source_population_cell = 0;
	uint64_t literal_count = 0;
	uint32_t bootstrap_version = bootstrap_semantics_version;
	uint32_t demographic_policy = demographic_policy_version;
	int32_t bootstrap_base_day = 0;
	uint64_t demographic_seed = 0;
	dcon::site_id home_site{};
	dcon::culture_id source_culture{};
	dcon::religion_id source_religion{};
	dcon::pop_type_id source_pop_type{};
};

struct exact_person_override {
	person_key key{};
	bool has_alive = false;
	bool alive = true;
	bool has_home_site = false;
	dcon::site_id home_site{};
};

struct bridge_record {
	person_key key{};
	dcon::person_id legacy_person{};
};

// A POP row's DCON index may be reused after deletion. This binding gives the
// row lifetime a stable exact-person namespace while it is present in DCON.
struct population_cell_binding {
	uint32_t source_population_cell = 0;
	uint32_t dcon_pop_index_plus_one = 0;
};

// Compact lifecycle records keep large cohorts virtual. A retired range keeps
// historical person keys valid for ledgers while excluding them from the live
// population. Birth cohorts supply ages and demographics for appended keys.
struct person_range {
	uint32_t source_population_cell = 0;
	uint32_t reserved = 0;
	uint64_t first_ordinal = 0;
	uint64_t count = 0;
};

struct birth_cohort {
	uint32_t source_population_cell = 0;
	uint32_t reserved = 0;
	uint64_t first_ordinal = 0;
	uint64_t count = 0;
	int32_t birth_day = 0;
	dcon::site_id home_site{};
	dcon::culture_id culture{};
	dcon::religion_id religion{};
	dcon::pop_type_id pop_type{};
	uint32_t reserved_tail = 0;
};

// A person's key stays in its origin namespace. This range records where its
// live members currently belong, so migration and class changes do not rewrite
// identities or expand a cohort into one record per human.
struct population_membership_range {
	uint32_t identity_population_cell = 0;
	uint32_t current_population_cell = 0;
	uint64_t first_ordinal = 0;
	uint64_t count = 0;
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

struct population_transition_record {
	uint32_t identity_population_cell = 0;
	uint32_t from_population_cell = 0;
	uint32_t to_population_cell = 0;
	uint8_t cause = 0;
	uint8_t reserved[3]{};
	uint64_t first_ordinal = 0;
	uint64_t count = 0;
	int32_t transition_day = 0;
	uint32_t reserved_tail = 0;
};

struct population_transfer_remainder {
	uint32_t from_population_cell = 0;
	uint32_t to_population_cell = 0;
	double pending_people = 0.0;
};

// Persistence boundary for the exact store. The normal save pipeline stores
// this catalog in its versioned exact-runtime extension without reconstructing
// one DCON object per logical human.
struct catalog_snapshot {
	uint32_t bootstrap_version = bootstrap_semantics_version;
	std::vector<cell_descriptor> cells;
	std::vector<exact_person_override> overrides;
	std::vector<bridge_record> bridges;
	std::vector<population_cell_binding> source_bindings;
	std::vector<uint32_t> retired_source_cells;
	std::vector<person_range> retired_people;
	std::vector<birth_cohort> birth_cohorts;
	std::vector<population_membership_range> membership_ranges;
	std::vector<population_transition_record> transitions;
	std::vector<population_transfer_remainder> transfer_remainders;
	bool has_source_bindings = false;
	bool has_current_membership = false;
};

struct population_transfer_result {
	uint64_t people_moved = 0;
	bool complete = true;
};

enum class status : uint8_t {
	created,
	already_registered,
	invalid_source,
	invalid_home_site,
	missing_home_province,
	overflow,
	version_mismatch,
	invalid_descriptor,
	invalid_bridge
};

struct registration_result {
	status result = status::invalid_source;
	cell_descriptor descriptor{};
};

struct world_bootstrap_result {
	uint64_t registered_cells = 0;
	uint64_t unbound_populations = 0;
	uint64_t logical_people = 0;
	dcon::pop_id failed_population{};
	status failure = status::already_registered;
	bool complete = true;
};

registration_result register_population_cell(sys::state&, dcon::pop_id,
	dcon::site_id home_site = {});
registration_result register_synthetic_population_cell(sys::state&, cell_descriptor);
world_bootstrap_result bootstrap_from_current_pops(sys::state&);
// Applies population growth, casualties, or scripted demographic changes to
// exact membership. The returned value is the number of literal persons
// created or retired (negative for deaths); callers project after a batch.
int64_t adjust_population_size(sys::state&, dcon::pop_id, double size_delta);
population_transfer_result transfer_population_membership(sys::state&, dcon::pop_id source,
	dcon::pop_id destination, float population_amount, population_transition_cause);
bool transfer_population_person_membership(sys::state&, person_key, dcon::pop_id destination,
	population_transition_cause);
uint32_t current_population_cell(sys::state const&, person_key);
dcon::pop_id current_population_for_person(sys::state const&, person_key);
uint64_t living_people_in_population_cell(sys::state const&, uint32_t population_cell);
bool project_population_membership(sys::state&);

bool source_cell_registered(sys::state const&, uint32_t source_population_cell);
uint64_t literal_count_for_cell(sys::state const&, uint32_t source_population_cell);
uint64_t logical_person_count(sys::state const&);
uint64_t cell_count(sys::state const&);
uint64_t override_count(sys::state const&);
uint64_t bridge_count(sys::state const&);

bool exists(sys::state const&, person_key);
bool alive(sys::state const&, person_key);
bool set_alive(sys::state&, person_key, bool);

persons::birth_day_index_t birth_day_index(sys::state const&, person_key);
int32_t age_days(sys::state const&, person_key, sys::date current_day);
int32_t age_years(sys::state const&, person_key, sys::date current_day);
dcon::site_id home_site(sys::state const&, person_key);
bool set_home_site(sys::state&, person_key, dcon::site_id);
dcon::culture_id source_culture(sys::state const&, person_key);
dcon::religion_id source_religion(sys::state const&, person_key);
dcon::pop_type_id source_pop_type(sys::state const&, person_key);
bool is_source_workforce_anchor(sys::state const&, person_key);

std::optional<cell_descriptor> descriptor_for_cell(sys::state const&, uint32_t source_population_cell);
uint32_t source_cell_for_population(sys::state const&, dcon::pop_id);
dcon::pop_id population_for_source_cell(sys::state const&, uint32_t source_population_cell);
void retire_population_cell(sys::state&, dcon::pop_id);

// Explicit compatibility projection. Simple exact-person queries never call
// this function and therefore never create a DCON Person or EconomicActor.
dcon::person_id legacy_person_for_exact_person(sys::state const&, person_key);
dcon::person_id materialize_legacy_person_bridge(sys::state&, person_key);

catalog_snapshot export_snapshot(sys::state const&);
bool import_snapshot(sys::state&, catalog_snapshot const&);
void clear_store(sys::state&);

} // namespace persons::exact_population
