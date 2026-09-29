#pragma once

#include "dcon_generated_ids.hpp"
#include "persons/persons.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sys { struct state; }
namespace simple_fs { struct directory; }
namespace parsers { struct error_handler; struct scenario_building_context; }

namespace military::land_forces {

using stable_id = uint64_t;

enum class formation_status : uint8_t { active, reserve, destroyed };
enum class consumable_kind : uint8_t { food, fuel, ammunition };
enum class cargo_kind : uint8_t { equipment, consumable };
enum class stockpile_kind : uint8_t { warehouse, depot };

struct equipment_model {
	stable_id id = 0;
	uint32_t category = 0;
	dcon::commodity_id commodity{};
	float mass = 0.0f;
	float reliability = 1.0f;
	float attack = 0.0f;
	float defense = 0.0f;
	float range_km = 0.0f;
};

struct formation_template {
	stable_id id = 0;
	uint32_t personnel_authorization = 0;
};

struct template_consumable_requirement {
	stable_id template_id = 0;
	consumable_kind kind = consumable_kind::food;
	uint8_t reserved[7]{};
	// Units per person and per tonne of operational equipment per day.
	double per_person = 0.0;
	double per_equipment_tonne = 0.0;
};

struct template_equipment_authorization {
	stable_id template_id = 0;
	stable_id equipment_model_id = 0;
	uint64_t quantity = 0;
};

struct formation {
	stable_id id = 0;
	stable_id parent_id = 0;
	stable_id template_id = 0;
	// Transitional battle/UI adapter only. Canonical identity remains `id`.
	uint32_t legacy_regiment_index_plus_one = 0;
	dcon::nation_id owner{};
	dcon::site_id location{};
	uint32_t personnel_authorization = 0;
	formation_status status = formation_status::active;
	uint8_t moving = 0;
	uint8_t in_combat = 0;
	uint8_t reserved = 0;
	float operational_tempo = 0.0f;
};

// Assignment intervals are exact: every ordinal in the half-open interval is
// one living person with this formation assignment and training state.
struct personnel_assignment_range {
	stable_id formation_id = 0;
	uint32_t source_population_cell = 0;
	uint64_t first_ordinal = 0;
	uint64_t count = 0;
	uint32_t ordinal_stride = 1;
	uint16_t training_days_remaining = 0;
	uint16_t reserved = 0;
};

struct equipment_holding {
	stable_id formation_id = 0;
	stable_id equipment_model_id = 0;
	uint64_t quantity = 0;
};

struct formation_consumable_inventory {
	stable_id formation_id = 0;
	consumable_kind kind = consumable_kind::food;
	uint8_t reserved[7]{};
	double quantity = 0.0;
};

struct stockpile {
	stable_id id = 0;
	dcon::nation_id owner{};
	dcon::site_id location{};
	stockpile_kind kind = stockpile_kind::warehouse;
	cargo_kind cargo = cargo_kind::equipment;
	consumable_kind consumable = consumable_kind::food;
	uint8_t reserved[3]{};
	stable_id equipment_model_id = 0;
	double quantity = 0.0;
};

struct physical_shipment {
	stable_id id = 0;
	stable_id source_stockpile_id = 0;
	stable_id destination_stockpile_id = 0;
	stable_id destination_formation_id = 0;
	dcon::site_id destination_site{};
	cargo_kind cargo = cargo_kind::equipment;
	consumable_kind consumable = consumable_kind::food;
	uint8_t status = 0; // 0=in transit, 1=delivered
	uint8_t reserved[5]{};
	stable_id equipment_model_id = 0;
	double quantity = 0.0;
	uint16_t days_remaining = 0;
	uint16_t reserved_tail = 0;
};

struct casualty_person_record {
	stable_id event_id = 0;
	stable_id formation_id = 0;
	persons::person_key person{};
	int32_t day = 0;
};

struct casualty_equipment_record {
	stable_id event_id = 0;
	stable_id formation_id = 0;
	stable_id equipment_model_id = 0;
	uint64_t quantity = 0;
	int32_t day = 0;
};

struct casualty_event_record {
	stable_id event_id = 0;
	stable_id formation_id = 0;
	uint64_t personnel_losses = 0;
	int32_t day = 0;
};

struct readiness {
	float personnel_fill = 0.0f;
	float equipment_fill = 0.0f;
	float supply_days = 0.0f;
	float operational_readiness = 0.0f;
};

struct casualty_request {
	stable_id equipment_model_id = 0;
	uint64_t quantity = 0;
};

struct casualty_result {
	bool applied = false;
	std::vector<persons::person_key> persons_killed;
};

struct validation_result {
	bool valid = true;
	std::vector<std::string> errors;
};

struct snapshot {
	uint32_t version = 1;
	std::vector<equipment_model> equipment_models;
	std::vector<formation_template> templates;
	std::vector<template_equipment_authorization> template_equipment;
	std::vector<template_consumable_requirement> template_consumables;
	std::vector<formation> formations;
	std::vector<equipment_holding> equipment;
	std::vector<formation_consumable_inventory> consumables;
	std::vector<stockpile> stockpiles;
	std::vector<physical_shipment> shipments;
	std::vector<casualty_person_record> person_losses;
	std::vector<casualty_equipment_record> equipment_losses;
	std::vector<casualty_event_record> casualty_events;
};

void initialize_empty_store(sys::state&);
bool initialized(sys::state const&);

bool add_equipment_model(sys::state&, equipment_model const&);
bool add_template(sys::state&, formation_template const&);
bool authorize_template_equipment(sys::state&, template_equipment_authorization const&);
bool set_template_consumable_requirement(sys::state&, template_consumable_requirement const&);
bool create_formation(sys::state&, formation const&);
bool create_stockpile(sys::state&, stockpile const&);
bool set_initial_equipment_holding(sys::state&, stable_id formation_id,
	stable_id equipment_model_id, uint64_t quantity);
bool set_initial_consumable_inventory(sys::state&, stable_id formation_id,
	consumable_kind, double quantity);

formation const* find_formation(sys::state const&, stable_id);
equipment_model const* find_equipment_model(sys::state const&, stable_id);
uint64_t personnel_count(sys::state const&, stable_id formation_id);
uint64_t equipment_count(sys::state const&, stable_id formation_id, stable_id equipment_model_id);
uint64_t equipment_authorization(sys::state const&, stable_id formation_id, stable_id equipment_model_id);
double consumable_quantity(sys::state const&, stable_id formation_id, consumable_kind);
readiness derive_readiness(sys::state const&, stable_id formation_id);

// Selects currently living, unassigned adults deterministically from the POP.
// The selection stays as exact ordinal ranges after assignment.
uint64_t recruit_personnel(sys::state&, stable_id formation_id, dcon::pop_id source_pop,
	uint64_t requested, uint16_t training_days);
bool assign_personnel(sys::state&, stable_id formation_id,
	std::span<persons::person_key const>, uint16_t training_days);
uint64_t demobilize(sys::state&, stable_id formation_id);
bool close_person_assignment_on_death(sys::state&, persons::person_key);
void advance_training(sys::state&);
void update_daily(sys::state&);
bool move_formation(sys::state&, stable_id formation_id, dcon::site_id destination);
void sync_legacy_adapter_state(sys::state&);
bool map_legacy_regiment(sys::state&, stable_id formation_id, dcon::regiment_id);
bool clear_legacy_regiment_mapping(sys::state&, dcon::regiment_id);
stable_id formation_for_legacy_regiment(sys::state const&, dcon::regiment_id);
float projected_regiment_strength(sys::state const&, stable_id formation_id);
float formation_combat_stat(sys::state const&, dcon::regiment_id, bool attacking);
float apply_legacy_regiment_damage(sys::state&, dcon::regiment_id, float damage,
	stable_id event_id, int32_t day);
bool sync_legacy_regiment_projection(sys::state&, dcon::regiment_id);
bool set_operational_state(sys::state&, stable_id formation_id, bool moving, bool in_combat,
	float operational_tempo);

// Warehouse/depot/formation movement is represented by a stable physical
// shipment. Dispatch consumes source stock; arrival credits exactly one target.
bool dispatch_to_stockpile(sys::state&, stable_id shipment_id, stable_id source_stockpile_id,
	stable_id destination_stockpile_id, double quantity);
bool dispatch_to_formation(sys::state&, stable_id shipment_id, stable_id source_stockpile_id,
	stable_id destination_formation_id, double quantity);
void advance_shipments(sys::state&);
double daily_consumable_demand(sys::state const&, stable_id formation_id, consumable_kind);
double consume_daily_supply(sys::state&, stable_id formation_id, consumable_kind);
double logistics_demand(sys::state const&, stable_id formation_id);
double replacement_load(sys::state const&, stable_id formation_id);
double depot_inventory_at(sys::state const&, dcon::nation_id owner, dcon::province_id);

casualty_result apply_losses(sys::state&, stable_id formation_id, uint64_t personnel_losses,
	std::span<casualty_request const> equipment_losses, stable_id event_id, int32_t day);
bool destroy_formation(sys::state&, stable_id formation_id);

validation_result validate_canonical_land_forces(sys::state const&);
uint64_t canonical_checksum(sys::state const&);
snapshot export_snapshot(sys::state const&);
bool import_snapshot(sys::state&, snapshot const&);
void clear_store(sys::state&);

} // namespace military::land_forces
