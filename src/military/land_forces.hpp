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
	dcon::commodity_id commodity{};
	uint8_t reserved[3]{};
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
	// Occupies the former reserved byte, preserving version-2 save layout.
	uint8_t mobilized = 0;
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
	dcon::shipment_id shared_shipment{};
	uint8_t reserved = 0;
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
	persons::death_cause cause = persons::death_cause::combat;
	uint8_t reserved[3]{};
};

struct readiness {
	float personnel_fill = 0.0f;
	float equipment_fill = 0.0f;
	float supply_days = 0.0f;
	float operational_readiness = 0.0f;
};

// How a formation is called and where it stands in the chain of command. The
// chain itself is `formation::parent_id`; the echelon only names the level, it
// never creates one.
enum class echelon : uint8_t { unspecified, theatre, army, corps, division, brigade, regiment };

struct formation_label {
	stable_id formation_id = 0;
	echelon level = echelon::unspecified;
	// Text key of the name, NUL-terminated.
	char name_key[55]{};
};
static_assert(sizeof(formation_label) == 64);

// Why a march is not advancing, or why an order was not accepted.
enum class march_block : uint8_t {
	none, unknown_formation, destroyed, not_owner, legacy_adapter, no_personnel, in_combat,
	already_there, no_site, no_route, awaiting_supply
};

// A standing order to march. The formation covers one infrastructure edge at a
// time, in whole days; `move_formation` is only the hop at the end of a leg.
struct march_order {
	stable_id formation_id = 0;
	int32_t issued_day = 0;
	dcon::site_id destination{};
	// Where the current leg ends; null until the leg is planned.
	dcon::site_id next_site{};
	uint16_t leg_days = 0;
	uint16_t leg_progress = 0;
	// Whole days left on the route as last planned, this leg included.
	uint16_t days_remaining = 0;
	march_block blocked = march_block::none;
	uint8_t reserved = 0;
};
static_assert(sizeof(march_order) == 24);

// One formation with everything under it, each formation counted once.
struct force_summary {
	uint32_t formations = 0;
	uint32_t marching = 0;
	uint32_t short_of_supply = 0; // fewer than three days of some consumable
	uint32_t below_strength = 0; // operational readiness under one half
	uint64_t personnel = 0;
	uint64_t personnel_authorization = 0;
	double equipment_tonnes = 0.0;
	double equipment_tonnes_authorized = 0.0;
	// Weighted by authorized personnel.
	float operational_readiness = 0.0f;
	// The shortest among formations that consume anything.
	float supply_days = 3650.0f;
};

struct casualty_request {
	stable_id equipment_model_id = 0;
	uint64_t quantity = 0;
};

// Native formations use one regiment/army only as a view for the battle and
// siege engines. Unlike an authored legacy mapping, it never owns their march.
struct battle_projection {
	stable_id formation_id = 0;
	uint32_t regiment_index_plus_one = 0;
	uint8_t retreating = 0;
	uint8_t reserved[3]{};
};
static_assert(sizeof(battle_projection) == 16);

struct casualty_result {
	bool applied = false;
	std::vector<persons::person_key> persons_killed;
};

struct validation_result {
	bool valid = true;
	std::vector<std::string> errors;
};

struct snapshot {
	uint32_t version = 4;
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
	// Version 3.
	std::vector<formation_label> labels;
	std::vector<march_order> marches;
	// Version 4. DCON saves the projected armies and ongoing battles.
	std::vector<battle_projection> battle_projections;
};

void initialize_empty_store(sys::state&);
bool initialized(sys::state const&);

bool add_equipment_model(sys::state&, equipment_model const&);
bool add_template(sys::state&, formation_template const&);
bool authorize_template_equipment(sys::state&, template_equipment_authorization const&);
bool set_template_consumable_requirement(sys::state&, template_consumable_requirement const&);
bool create_formation(sys::state&, formation const&);
stable_id next_runtime_id(sys::state const&, stable_id domain, stable_id seed);
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
double available_stockpile_quantity(sys::state const&, stable_id stockpile_id);
readiness derive_readiness(sys::state const&, stable_id formation_id);

// Recruitment candidates are exact Person Kernel identities, selected in
// stable identity order from currently owned, reachable sites.
std::vector<persons::person_key> recruitment_candidates(sys::state&,
	stable_id formation_id, uint64_t requested);
uint64_t recruit_personnel(sys::state&, stable_id formation_id,
	std::span<persons::person_key const> candidates, uint64_t requested,
	uint16_t training_days);
bool assign_personnel(sys::state&, stable_id formation_id,
	std::span<persons::person_key const>, uint16_t training_days);
uint64_t demobilize(sys::state&, stable_id formation_id);
void return_mobilized_inventory(sys::state&, stable_id formation_id);
bool close_person_assignment_on_death(sys::state&, persons::person_key);
void advance_training(sys::state&);
void update_daily(sys::state&);
// Relocates a formation and what it carries in one step. It takes no game time
// and asks no one's leave: scenario setup and the end of a march leg use it.
// An order to go somewhere is `order_march`.
bool move_formation(sys::state&, stable_id formation_id, dcon::site_id destination);

std::span<formation const> all_formations(sys::state const&);
std::span<equipment_model const> all_equipment_models(sys::state const&);
std::span<formation_label const> all_labels(sys::state const&);
std::span<march_order const> all_marches(sys::state const&);
formation_label const* find_label(sys::state const&, stable_id formation_id);
march_order const* find_march(sys::state const&, stable_id formation_id);
bool set_label(sys::state&, formation_label const&);
// The formation and every formation under it, the formation itself first.
std::vector<stable_id> subtree(sys::state const&, stable_id formation_id);
force_summary summarize(sys::state const&, std::span<stable_id const> formations);
// Days of marching between two sites over provinces the nation may enter;
// negative when there is no such way.
float march_days(sys::state&, dcon::nation_id owner, dcon::site_id origin, dcon::site_id destination);
march_block can_order_march(sys::state&, dcon::nation_id as, stable_id formation_id,
	dcon::province_id destination);
// Replaces any standing order. With `with_subordinates` every formation under
// this one receives its own order from where it stands; returns how many were
// accepted.
uint32_t order_march(sys::state&, dcon::nation_id as, stable_id formation_id,
	dcon::province_id destination, bool with_subordinates);
uint32_t halt_march(sys::state&, dcon::nation_id as, stable_id formation_id, bool with_subordinates);
void advance_marches(sys::state&);
// Sends what the formation lacks for `days` of supply from the owner's depots,
// as physical shipments. Returns how many shipments left.
uint32_t request_resupply(sys::state&, dcon::nation_id as, stable_id formation_id, float days,
	bool with_subordinates);
// Fills vacancies from the owner's population through `recruit_personnel`.
uint64_t replenish(sys::state&, dcon::nation_id as, stable_id formation_id, uint16_t training_days,
	bool with_subordinates);
bool personnel_route_is_valid(sys::state&, dcon::nation_id owner, dcon::site_id origin,
	dcon::site_id destination, uint64_t personnel);
void sync_legacy_adapter_state(sys::state&);
// Creates/updates battle views in stable formation order, then resolves contact.
// Safe to repeat; does not recruit people or create equipment.
void prepare_battle_projections(sys::state&);
dcon::army_id battle_army(sys::state const&, stable_id formation_id);
bool is_battle_projection(sys::state const&, dcon::army_id);
bool is_battle_retreat(sys::state const&, dcon::army_id);
bool map_legacy_regiment(sys::state&, stable_id formation_id, dcon::regiment_id);
bool clear_legacy_regiment_mapping(sys::state&, dcon::regiment_id);
stable_id formation_for_legacy_regiment(sys::state const&, dcon::regiment_id);
float projected_regiment_strength(sys::state const&, stable_id formation_id);
float formation_combat_stat(sys::state const&, dcon::regiment_id, bool attacking);
float apply_legacy_regiment_damage(sys::state&, dcon::regiment_id, float damage,
	stable_id event_id, int32_t day, persons::death_cause cause = persons::death_cause::combat);
stable_id next_loss_event_id(sys::state const&, stable_id formation_id, int32_t day,
	persons::death_cause cause);
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
// The shared shipments that are supply columns on their way to a depot or a
// formation, in id order.
std::vector<dcon::shipment_id> supply_columns(sys::state const&);
bool is_equipment_supply_column(sys::state const&, dcon::shipment_id);
double daily_consumable_demand(sys::state const&, stable_id formation_id, consumable_kind);
double consume_daily_supply(sys::state&, stable_id formation_id, consumable_kind);
double logistics_demand(sys::state const&, stable_id formation_id);
double replacement_load(sys::state const&, stable_id formation_id);
double depot_inventory_at(sys::state const&, dcon::nation_id owner, dcon::province_id);
// What the owner's depots hold of one consumable, all depots together.
double depot_stock(sys::state const&, dcon::nation_id owner, consumable_kind);

casualty_result apply_losses(sys::state&, stable_id formation_id, uint64_t personnel_losses,
	std::span<casualty_request const> equipment_losses, stable_id event_id, int32_t day,
	persons::death_cause cause = persons::death_cause::combat);
bool destroy_formation(sys::state&, stable_id formation_id);

validation_result validate_canonical_land_forces(sys::state const&);
uint64_t canonical_checksum(sys::state const&);
snapshot export_snapshot(sys::state const&);
bool import_snapshot(sys::state&, snapshot const&);
void clear_store(sys::state&);

// Read access for defense finance. Formations are the owner's not destroyed
// ones; the custodian is the actor that holds the owner's warehouse and depot
// inventory; a stockpile's stock is what that inventory holds at its site.
std::vector<stable_id> formations_of(sys::state const&, dcon::nation_id owner);
std::vector<stockpile> stockpiles_of(sys::state const&, dcon::nation_id owner);
dcon::economic_actor_id stockpile_custodian(sys::state const&, dcon::nation_id owner);
dcon::commodity_id stockpile_commodity(sys::state const&, stockpile const&);
double stockpile_stock(sys::state const&, stockpile const&);

} // namespace military::land_forces
