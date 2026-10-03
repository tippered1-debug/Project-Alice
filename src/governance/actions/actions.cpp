#include "actions.hpp"

#include "governance/offices.hpp"
#include "system_state.hpp"

namespace governance::actions {

dcon::institutional_action_id authorized_appoint(sys::state& s, dcon::person_id initiator, dcon::person_id target_person, dcon::office_id target_office, sys::date date) {
	return offices::appoint(s, initiator, target_person, target_office, date);
}

dcon::institutional_action_id authorized_dismiss(sys::state& s, dcon::person_id initiator, dcon::office_id target_office, sys::date date) {
	return offices::dismiss(s, initiator, target_office, date);
}

} // namespace governance::actions
