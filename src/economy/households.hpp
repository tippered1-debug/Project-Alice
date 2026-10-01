#pragma once

#include "dcon_generated.hpp"
#include "persons/persons.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace sys { class state; }

namespace economy::households {

// A household cohort is the shared budget of the rural people of one province
// who hold no individual economic records. Members are not stored: a living
// person of the cohort's role in its province belongs to it unless that person
// has individual needs (an exact consumer, such as a hired worker) or serves in
// a military formation. Every member has an identical per-capita share.
enum class role : uint8_t { none = 0, peasant = 1, landed = 2 };

// One worker in four people, matching the source workforce unit of POP sizes.
inline constexpr float workers_per_member = 0.25f;
// Days of the members' own needs a cohort keeps of its own produce before selling.
inline constexpr float retention_days = 60.0f;
inline constexpr float purchase_markup = 1.05f;
inline constexpr float sale_discount = 0.97f;
// Daily weight of today's production value in the reservation wage.
inline constexpr float reservation_smoothing = 0.05f;

dcon::organization_id create(sys::state&, dcon::province_id home, role, dcon::commodity_id settlement);
bool is_household(sys::state const&, dcon::organization_id);
bool is_household(sys::state const&, dcon::economic_actor_id);
role role_of(sys::state const&, dcon::organization_id);
dcon::province_id home_of(sys::state const&, dcon::organization_id);
dcon::site_id home_site(sys::state const&, dcon::organization_id);
dcon::organization_id household_for(sys::state const&, dcon::province_id, role);
// The role whose cohort a population row of this type belongs to.
role role_for_pop_type(sys::state const&, dcon::pop_type_id);

float members(sys::state const&, dcon::organization_id);
float workers(sys::state const&, dcon::organization_id);
// Daily value of the cohort's own production per worker: what a member gives
// up by leaving the cohort for a wage.
float reservation_wage(sys::state const&, dcon::organization_id);

// A peasant cohort works its own farms with its members' labor, unpaid.
bool self_working_operator(sys::state const&, dcon::factory_id);
float self_employed_labor(sys::state const&, dcon::factory_id);

// Recounts every cohort's members and workers and updates reservation wages.
// Runs before production so the day's self-employed labor is current.
void refresh_membership(sys::state&);
// Gathers produce and rent home, consumes own stock, sells surplus, buys the rest.
void process_daily(sys::state&);

// A displaced worker who found no job for this many days returns to the cohort
// of their role in their home province, bringing their cash and goods.
inline constexpr int32_t days_before_rejoining = 30;
bool rejoin(sys::state&, persons::person_key);

// Adds each cohort's desired and consumed quantities, by life/everyday/luxury,
// to the per-population totals used for the POP satisfaction projection.
void add_population_totals(sys::state const&, std::map<uint32_t, std::array<double, 6>>& totals);

} // namespace economy::households
