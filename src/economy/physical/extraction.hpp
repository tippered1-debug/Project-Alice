#pragma once

#include "dcon_generated.hpp"

namespace sys { class state; }

namespace economy::physical::extraction {

enum class deposit_status : uint8_t { active = 0, suspended = 1, depleted = 2 };
enum class right_status : uint8_t { active = 0, revoked = 1, expired = 2 };

dcon::resource_extraction_right_id create_right(sys::state&, dcon::person_id initiator,
	dcon::office_id office, dcon::resource_deposit_id deposit, dcon::economic_actor_id holder,
	dcon::nation_id granting_nation, sys::date valid_from, sys::date valid_until,
	float max_daily_quantity, sys::date date);
dcon::resource_extraction_right_id active_right_for(sys::state const&, dcon::resource_deposit_id, sys::date);
dcon::resource_extraction_right_id active_right_for(sys::state const&, dcon::resource_deposit_id,
	dcon::economic_actor_id holder, sys::date);

// A primary enterprise is an ordinary factory whose recipe extracts from the
// one deposit bound to it. The deposit limits output; labor, inventory,
// shipments, asks, and payroll follow the common factory path.
bool extracts_deposit(sys::state const&, dcon::factory_type_id);
bool extracts_deposit(sys::state const&, dcon::factory_id);
dcon::resource_deposit_id deposit_for_enterprise(sys::state const&, dcon::factory_id);
dcon::factory_id enterprise_for_deposit(sys::state const&, dcon::resource_deposit_id);
// The national royalty jurisdiction for an operator's resource output sold at
// its mine site or the mine's local market hub. Unrelated resales are excluded.
dcon::nation_id royalty_jurisdiction_for_sale(sys::state const&, dcon::economic_actor_id seller,
	dcon::site_id sale_site, dcon::commodity_id commodity);
// Creates the enterprise at the deposit's site. Full capacity extracts the
// deposit's daily capacity. Nothing is created unless every binding holds.
dcon::factory_id create_enterprise(sys::state&, dcon::resource_deposit_id,
	dcon::factory_type_id, dcon::organization_id operator_organization);
// Whether an actor may operate a plant on the deposit: the deposit is active
// and owned, and the actor is its operator or holds an active right.
bool may_operate(sys::state const&, dcon::resource_deposit_id, dcon::economic_actor_id, sys::date);
// Output multiplier from ore grade; 1 for an ordinary factory.
float output_grade(sys::state const&, dcon::factory_id);
// Daily output the operator may legally and physically extract, before
// today's extraction. Zero without access, an owned deposit, or reserves.
float daily_ceiling(sys::state const&, dcon::factory_id, sys::date);
// Output still extractable today after what was already committed.
float available_today(sys::state const&, dcon::factory_id, sys::date);
// Depletes reserves by output that has already been materialized.
float commit(sys::state&, dcon::factory_id, float quantity, sys::date);

} // namespace economy::physical::extraction
