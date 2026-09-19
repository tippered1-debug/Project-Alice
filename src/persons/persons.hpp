#pragma once

#include "dcon_generated.hpp"
#include "date_interface.hpp"
#include "governance/governance.hpp"

namespace sys { class state; }

namespace persons {

namespace policy {
inline constexpr int32_t minimum_working_age_days = 14 * 365;
inline constexpr int32_t maximum_working_age_days = 65 * 365;
}

using birth_day_index_t = int32_t;

// Low-level structural occupancy primitives. Normal governance commands must
// use governance::actions authorization APIs.
dcon::person_id create_person(sys::state&, sys::date birth_date);
dcon::person_id create_person_with_birth_day(sys::state&, birth_day_index_t);
dcon::economic_actor_id actor_for_person(sys::state const&, dcon::person_id);
birth_day_index_t birth_day_index(sys::state const&, dcon::person_id);
bool has_birth_day(sys::state const&, dcon::person_id);
int32_t age_days(sys::state const&, dcon::person_id);
int32_t age_years(sys::state const&, dcon::person_id);
bool born_on_or_before(sys::state const&, dcon::person_id, sys::date);
bool is_work_eligible(sys::state const&, dcon::person_id);
dcon::office_tenure_id appoint_person(sys::state&, dcon::person_id, dcon::office_id, sys::date);
bool remove_from_office(sys::state&, dcon::office_id, sys::date);
dcon::person_id occupant_of(sys::state const&, dcon::office_id);
std::vector<dcon::office_id> active_offices_of(sys::state const&, dcon::person_id);
dcon::office_tenure_id active_tenure_for(sys::state const&, dcon::office_id);
bool person_has_authority(sys::state const&, dcon::person_id, governance::authority_kind, dcon::nation_id);
bool person_has_authority(sys::state const&, dcon::person_id, governance::authority_kind, dcon::territorial_unit_id);
dcon::office_tenure_id authority_tenure_on_or_before(sys::state const&, dcon::person_id, governance::authority_kind, dcon::nation_id, sys::date);
bool mark_dead(sys::state&, dcon::person_id, sys::date);

} // namespace persons
