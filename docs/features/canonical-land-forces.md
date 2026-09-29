# Canonical Land Forces

Canonical land forces are authored under `common/canonical_runtime/`. The
tables are required, including when empty. They use UTF-8, semicolon
separators, one header row, and `#` comment lines. Quoting is not supported.
Stable keys use ASCII letters, digits, `.`, `_`, and `-`; runtime IDs are
deterministic hashes of the table domain and key.

## Equipment and templates

`military_equipment.csv` has columns
`equipment_id;category;commodity;mass;reliability;attack;defense;range_km`.
The commodity may be empty. Mass must be positive; reliability is from 0 to 1;
combat values and range are nonnegative.

`formation_templates.csv` has columns
`template_id;personnel_authorization`.
`formation_template_equipment.csv` has columns
`template_id;equipment_id;quantity` and declares authorized equipment counts.
`formation_template_consumables.csv` has columns
`template_id;consumable;per_person;per_equipment_tonne`. Consumables are
`food`, `fuel`, or `ammunition`; rates are daily quantities.

## Formations and personnel

`military_formations.csv` has columns
`formation_id;parent_formation_id;template_id;owner_tag;province_id;status;operational_tempo;legacy_regiment_index`.
The parent key and legacy index can be empty. `province_id` is the original
scenario province number, and `legacy_regiment_index` is a zero-based DCON
regiment index used only to connect the legacy battle/UI adapter. Every
authored legacy regiment must have exactly one formation mapping. Status is
`active`, `reserve`, or `destroyed`; operational tempo is from 0 to 1.

`formation_personnel.csv` has columns
`formation_id;source_population_cell;first_ordinal;count;ordinal_stride;training_days`.
Each row assigns the exact living people in the specified ordinal range. The
source population cell is the exact-person catalog cell, not a mutable POP
index. Stride is 1 or 4. Rows cannot overlap, refer to dead people, or exceed
the formation's personnel authorization. The source POP must have a valid
friendly supply route to the formation site. Recruitment selects living,
unassigned people and records their formation assignment and training time.

## Equipment, supplies, and depots

`formation_equipment.csv` has columns
`formation_id;equipment_id;quantity`. Initial quantities must fit the
formation template's authorization. `formation_consumables.csv` has columns
`formation_id;consumable;quantity` and seeds local food, fuel, or ammunition.

`military_stockpiles.csv` has columns
`stockpile_id;owner_tag;province_id;stockpile_kind;cargo_kind;equipment_id;consumable;quantity`.
Stockpile kind is `warehouse` or `depot`; cargo kind is `equipment` or
`consumable`. Equipment cargo names an equipment model and leaves `consumable`
empty. Consumable cargo names `food`, `fuel`, or `ammunition` and leaves
`equipment_id` empty. Stock is consumed when a shipment is dispatched. It
arrives only after a valid spatial supply route and its transit time; an
unreachable or enemy-controlled route cannot create stock at the destination.

## Runtime ownership and saves

Exact living people assigned to formations are protected from civilian
population retirement and cannot be recruited twice. Readiness is derived
from trained personnel, authorized equipment holdings, and local consumable
days. Combat and attrition select exact assigned people deterministically,
record their deaths and equipment losses, and then project the resulting
population totals. Reinforcement requires available people and physical
equipment stock reachable over a friendly route; a legacy regiment's scalar
strength cannot create either. Casualty events record their cause, including
combat and attrition.

Legacy regiment strength is a derived adapter value for existing UI and battle
integration. Unmapped legacy regiments are rejected by canonical scenario
loading and cannot receive canonical land damage. The canonical formation,
assignment, equipment, shipment, depot, and loss ledgers are included in save
sections and deterministic checksums.
