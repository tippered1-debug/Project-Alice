#pragma once

#include "dcon_generated.hpp"
#include "persons/persons.hpp"

namespace sys { class state; }

namespace economy::estates {

// When a person dies, everything they hold passes to an heir: the household
// cohort of their role in their home province, or, without one, the treasury
// of the nation owning that province. Nothing stays with the dead.
dcon::economic_actor_id heir_for(sys::state const&, persons::person_key);
// Moves a dead person's cash, goods, ownership stakes, and bank deposits to
// their heir. Returns true once nothing is left to settle; goods with pending
// freight wait for a later day.
bool settle(sys::state&, persons::person_key);
// Settles every dead person who still holds cash, goods, stakes, or deposits.
void process(sys::state&);

} // namespace economy::estates
