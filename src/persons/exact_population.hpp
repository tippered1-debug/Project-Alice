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

// This is an isolated persistence boundary for the exact store. The normal
// save pipeline does not yet serialize it; callers may export and restore this
// snapshot without reconstructing one object per logical human.
struct catalog_snapshot {
	uint32_t bootstrap_version = bootstrap_semantics_version;
	std::vector<cell_descriptor> cells;
	std::vector<exact_person_override> overrides;
	std::vector<bridge_record> bridges;
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

registration_result register_population_cell(sys::state&, dcon::pop_id,
	dcon::site_id home_site = {});
registration_result register_synthetic_population_cell(sys::state&, cell_descriptor);

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

// Explicit compatibility projection. Simple exact-person queries never call
// this function and therefore never create a DCON Person or EconomicActor.
dcon::person_id legacy_person_for_exact_person(sys::state const&, person_key);
dcon::person_id materialize_legacy_person_bridge(sys::state&, person_key);

catalog_snapshot export_snapshot(sys::state const&);
bool import_snapshot(sys::state&, catalog_snapshot const&);
void clear_store(sys::state&);

} // namespace persons::exact_population
