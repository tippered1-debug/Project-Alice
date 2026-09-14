#pragma once

#include "dcon_generated.hpp"
#include "date_interface.hpp"
#include "governance/governance.hpp"

namespace sys { class state; }

namespace persons {

// Low-level structural occupancy primitives. Normal governance commands must
// use governance::actions authorization APIs.
dcon::person_id create_person(sys::state&, sys::date birth_date);
dcon::economic_actor_id actor_for_person(sys::state const&, dcon::person_id);
dcon::office_tenure_id appoint_person(sys::state&, dcon::person_id, dcon::office_id, sys::date);
bool remove_from_office(sys::state&, dcon::office_id, sys::date);
dcon::person_id occupant_of(sys::state const&, dcon::office_id);
std::vector<dcon::office_id> active_offices_of(sys::state const&, dcon::person_id);
dcon::office_tenure_id active_tenure_for(sys::state const&, dcon::office_id);
bool person_has_authority(sys::state const&, dcon::person_id, governance::authority_kind, dcon::nation_id);
bool person_has_authority(sys::state const&, dcon::person_id, governance::authority_kind, dcon::territorial_unit_id);
bool mark_dead(sys::state&, dcon::person_id, sys::date);

} // namespace persons
