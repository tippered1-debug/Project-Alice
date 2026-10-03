#pragma once

#include "dcon_generated.hpp"
#include "date_interface.hpp"
#include "governance/governance.hpp"
#include "persons/persons.hpp"

namespace sys { class state; }

namespace governance::actions {

// Values are saved: new kinds are appended.
enum class institutional_action_kind : uint8_t { appointment, dismissal, resignation, succession, installation };

// Appointment and dismissal follow the office's constitutional rules; see
// governance::offices.
dcon::institutional_action_id authorized_appoint(sys::state&, dcon::person_id initiator, dcon::person_id target_person, dcon::office_id target_office, sys::date date);
dcon::institutional_action_id authorized_dismiss(sys::state&, dcon::person_id initiator, dcon::office_id target_office, sys::date date);

} // namespace governance::actions
