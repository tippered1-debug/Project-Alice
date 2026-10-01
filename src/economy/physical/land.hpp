#pragma once

#include "dcon_generated.hpp"
#include "date_interface.hpp"

namespace sys { class state; }

namespace economy::physical::land {

// Land is a natural asset: a titled area with a suitability per commodity.
// A farm is an ordinary factory bound to one title. Its labor has diminishing
// returns on that fixed area: output = A * s * L^a * T^(1 - a), where T is the
// area measured in reference holdings of one worker each.
inline constexpr float labor_elasticity = 0.6f;
inline constexpr float reference_hectares_per_worker = 5.0f;
// A farm worker on a reference holding produces this multiple of the daily
// life needs of the four people the worker represents (content calibration).
inline constexpr float reference_food_security = 1.5f;
inline constexpr float persons_per_worker = 4.0f;
inline constexpr int32_t rent_period_days = 30;

enum class lease_status : uint8_t { active = 0, ended = 1 };

dcon::land_title_id create_title(sys::state&, dcon::site_id, float area_hectares);
bool set_suitability(sys::state&, dcon::land_title_id, dcon::commodity_id, float);
float suitability(sys::state const&, dcon::land_title_id, dcon::commodity_id);
// The holder of a majority voting stake in the title's asset.
dcon::economic_actor_id owner_of(sys::state const&, dcon::land_title_id);

bool farms_land(sys::state const&, dcon::factory_type_id);
bool farms_land(sys::state const&, dcon::factory_id);
dcon::land_title_id title_for_farm(sys::state const&, dcon::factory_id);
dcon::factory_id farm_for_title(sys::state const&, dcon::land_title_id);
// Creates the farm on the title's site. Nothing is created unless the title has
// area, the recipe can grow there, and the operator is an economic organization.
dcon::factory_id create_farm(sys::state&, dcon::land_title_id, dcon::factory_type_id,
	dcon::organization_id operator_organization);

dcon::land_lease_id create_lease(sys::state&, dcon::land_title_id, dcon::economic_actor_id tenant,
	dcon::commodity_id settlement, float cash_rent_per_hectare_year, float output_share,
	sys::date valid_from, sys::date valid_until);
dcon::land_lease_id active_lease_for(sys::state const&, dcon::land_title_id, sys::date);
dcon::economic_actor_id tenant_of(sys::state const&, dcon::land_lease_id);
// A leased title is farmed only by its tenant; otherwise only by its owner.
bool may_operate(sys::state const&, dcon::land_title_id, dcon::economic_actor_id, sys::date);

// Labor at which the farm works its whole area at the reference ratio.
float reference_labor(sys::state const&, dcon::factory_id);
// Output of the farm for a given labor, or zero without legal access.
float output_for_labor(sys::state const&, dcon::factory_id, float labor, sys::date);
// Share of output the operating tenant owes the owner in kind.
float output_share_owed(sys::state const&, dcon::factory_id, sys::date);
// Moves the owner's share of a harvest from the farm site to the owner.
float deliver_output_share(sys::state&, dcon::factory_id, float share_quantity, sys::date);
// Settles cash rent for every active lease once per rent period.
void settle_rents(sys::state&);

// Sets each farm recipe's output per worker from content: a worker on a
// reference holding yields reference_food_security times the value of the life
// needs of the people one worker represents, scaled by the recipe's relative
// content productivity.
void calibrate_farm_recipes(sys::state&, dcon::pop_type_id reference_pop_type);

} // namespace economy::physical::land
