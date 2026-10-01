#pragma once

#include "dcon_generated.hpp"
#include "persons/persons.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace sys { class state; }

namespace economy::households {

// A household cohort is the shared budget of the rural people of one province
// who hold no individual economic records. Members are not stored: a living
// person of the cohort's role in its province belongs to it unless that person
// has individual needs (an exact consumer, such as a hired worker) or serves in
// a military formation. Every member has an identical per-capita share.
// peasant: farmers and labourers; landed: aristocrats; urban: every other free
// role. Slaves are bonded labor and belong to no free cohort.
enum class role : uint8_t { none = 0, peasant = 1, landed = 2, urban = 3 };

// One worker in four people, matching the source workforce unit of POP sizes.
inline constexpr float workers_per_member = 0.25f;
// A source workforce anchor and the three ordinals after it form one family.
inline constexpr int32_t persons_per_family = 4;
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

// People with an individual budget: exact consumers with needs of their own.
std::set<std::pair<uint32_t, uint64_t>> individual_consumers(sys::state const&);
// An individual budget covers the person and, for a workforce anchor, the
// living dependents of their family who share their population and have no
// individual budget or military service of their own.
int32_t family_size(sys::state const&, persons::person_key, std::set<std::pair<uint32_t, uint64_t>> const& individuals);

float members(sys::state const&, dcon::organization_id);
float workers(sys::state const&, dcon::organization_id);
// Daily value of the cohort's own production per worker: what a member gives
// up by leaving the cohort for a wage.
float reservation_wage(sys::state const&, dcon::organization_id);

// A peasant or urban cohort works its own farms and workshops with its
// members' labor, unpaid.
bool self_working_operator(sys::state const&, dcon::factory_id);
float self_employed_labor(sys::state const&, dcon::factory_id);
bool is_craft(sys::state const&, dcon::factory_type_id);
// Creates a workshop for a craft recipe at a site. Its labor is the members'
// own work; its inputs are bought like any factory's.
dcon::factory_id create_workshop(sys::state&, dcon::site_id, dcon::factory_type_id, dcon::organization_id operator_organization);

// Where every living person's budget is: a cohort, an individual budget (with
// family), military service, bonded labor, or nowhere.
struct coverage {
	double living = 0.0;
	double in_cohorts = 0.0;
	double individual = 0.0;
	double military = 0.0;
	double bonded = 0.0;
	double uncovered = 0.0;
};
coverage audit(sys::state const&);

// Recounts every cohort's members and workers and updates reservation wages.
// Runs before production so the day's self-employed labor is current.
void refresh_membership(sys::state&);
// Gathers produce and rent home, consumes own stock, sells surplus, buys the rest.
void process_daily(sys::state&);

// A displaced worker who found no job for this many days returns to the cohort
// of their role in their home province, bringing their cash and goods.
inline constexpr int32_t days_before_rejoining = 30;
bool rejoin(sys::state&, persons::person_key);
// Ends a living person's individual budget and makes them a cohort member
// again: their goods and cash pass to the cohort of their role in their home
// province. Fails without change for a person under contract, in military
// service, without such a cohort, or with freight still pending.
bool return_to_household(sys::state&, persons::person_key);
// Returns every individual consumer who has no contract and no cash of their
// own, except displaced workers still inside their job-search window.
void release_idle_consumers(sys::state&);

// Adds each cohort's desired and consumed quantities, by life/everyday/luxury,
// to the per-population totals used for the POP satisfaction projection.
void add_population_totals(sys::state const&, std::map<uint32_t, std::array<double, 6>>& totals);

} // namespace economy::households
