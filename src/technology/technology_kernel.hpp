#pragma once

#include "dcon_generated.hpp"
#include "date_interface.hpp"
#include "persons/persons.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace sys { class state; }

namespace technology::kernel {

using stable_id = uint64_t;

enum class research_role : uint8_t {
	university = 0,
	public_laboratory = 1,
	corporate_rd = 2,
	military_government = 3,
	other = 4
};

enum class program_status : uint8_t {
	planned = 0,
	active = 1,
	paused = 2,
	completed = 3,
	cancelled = 4
};

struct capability_definition {
	stable_id id = 0;
	stable_id domain = 0;
	float research_effort = 0.0f;
	uint8_t codified = 0;
	uint8_t transferable = 0;
};

struct capability_prerequisite {
	stable_id capability = 0;
	stable_id prerequisite = 0;
};

// A data-authored capability can enable any number of concrete factory
// processes. Other enabler kinds are deliberately left to future adapters.
struct factory_process_requirement {
	stable_id capability = 0;
	stable_id process = 0;
	dcon::factory_type_id factory_type{};
};

struct research_organization {
	stable_id id = 0;
	dcon::organization_id organization{};
	dcon::site_id site{};
	dcon::monetary_account_id funding_account{};
	research_role role = research_role::other;
	float effectiveness = 1.0f;
};

struct capability_holder {
	stable_id organization = 0;
	stable_id capability = 0;
	float maturity = 1.0f;
};

struct research_program {
	stable_id id = 0;
	stable_id organization = 0;
	stable_id capability = 0;
	program_status status = program_status::planned;
	sys::date start_date{};
	float effort_accumulated = 0.0f;
	float required_effort = 0.0f;
	float cost_per_effort = 0.0f;
};

struct research_assignment {
	stable_id program = 0;
	persons::person_key person{};
	float allocation_fraction = 0.0f;
	// Zero derives a compact productivity proxy from the person's current
	// population literacy. A positive value is an explicit scenario input.
	float productivity_input = 0.0f;
};

struct capability_adoption {
	stable_id organization = 0;
	stable_id capability = 0;
	sys::date date{};
};

struct capability_transfer {
	stable_id id = 0;
	stable_id source_organization = 0;
	stable_id destination_organization = 0;
	stable_id capability = 0;
	sys::date date{};
	stable_id authorization = 0;
};

struct snapshot {
	uint32_t version = 1;
	uint8_t canonical_runtime_active = 0;
	std::vector<capability_definition> capabilities;
	std::vector<capability_prerequisite> prerequisites;
	std::vector<factory_process_requirement> factory_processes;
	std::vector<research_organization> organizations;
	std::vector<capability_holder> holders;
	std::vector<research_program> programs;
	std::vector<research_assignment> assignments;
	std::vector<capability_adoption> adoptions;
	std::vector<capability_transfer> transfers;
};

stable_id stable_id_for(std::string_view namespace_key, std::string_view authored_id) noexcept;
bool canonical_runtime_active(sys::state const&);

bool install_scenario_state(sys::state&, snapshot const&);
bool add_initial_holder(sys::state&, capability_holder const&);
bool transfer_capability(sys::state&, stable_id source_organization,
	stable_id destination_organization, stable_id capability, sys::date date,
	stable_id authorization, stable_id transfer_id = 0);
bool adopt_capability(sys::state&, stable_id organization, stable_id capability, sys::date date);

bool organization_holds(sys::state const&, stable_id organization, stable_id capability);
bool organization_adopted(sys::state const&, stable_id organization, stable_id capability);
bool organization_can_use_capability(sys::state const&, stable_id organization, stable_id capability);
bool organization_can_operate_factory_type(sys::state const&, dcon::organization_id,
	dcon::factory_type_id);
bool factory_process_has_canonical_requirement(sys::state const&, dcon::factory_type_id);

void update_daily(sys::state&);
void initialize_empty_store(sys::state&);
void clear_store(sys::state&);
snapshot export_snapshot(sys::state const&);
bool import_snapshot(sys::state&, snapshot const&);
// Serialization reads scenario references before the DCON payload. Callers
// must validate_canonical_technology_state after that payload is restored.
void load_snapshot_unvalidated(sys::state&, snapshot const&);
bool validate_canonical_technology_state(sys::state const&, std::vector<std::string>& errors);
uint64_t deterministic_checksum(sys::state const&);

} // namespace technology::kernel
