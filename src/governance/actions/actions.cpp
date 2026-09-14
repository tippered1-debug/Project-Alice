#include "actions.hpp"

#include "system_state.hpp"

namespace governance::actions {

namespace {
dcon::office_tenure_id active_authority_tenure_on_or_before(sys::state const& s, dcon::person_id person, governance::authority_kind kind, dcon::nation_id nation, sys::date date) {
	for(auto office : persons::active_offices_of(s, person)) {
		auto tenure = persons::active_tenure_for(s, office);
		if(tenure && s.world.office_tenure_get_started_on(tenure) <= date && governance::has_authority(s, office, kind, nation)) return tenure;
	}
	return {};
}

dcon::institutional_action_id record_appointment(sys::state& s, dcon::person_id initiator, dcon::person_id target_person, dcon::office_id target_office, sys::date date) {
	auto action = s.world.create_institutional_action();
	s.world.institutional_action_set_kind(action, uint8_t(institutional_action_kind::appointment));
	s.world.institutional_action_set_occurred_on(action, date);
	s.world.force_create_institutional_action_initiator(action, initiator);
	s.world.force_create_institutional_action_target_person(action, target_person);
	s.world.force_create_institutional_action_target_office(action, target_office);
	return action;
}

dcon::institutional_action_id record_dismissal(sys::state& s, dcon::person_id initiator, dcon::person_id target_person, dcon::office_id target_office, sys::date date) {
	auto action = s.world.create_institutional_action();
	s.world.institutional_action_set_kind(action, uint8_t(institutional_action_kind::dismissal));
	s.world.institutional_action_set_occurred_on(action, date);
	s.world.force_create_institutional_action_initiator(action, initiator);
	s.world.force_create_institutional_action_target_person(action, target_person);
	s.world.force_create_institutional_action_target_office(action, target_office);
	return action;
}
}

dcon::institutional_action_id authorized_appoint(sys::state& s, dcon::person_id initiator, dcon::person_id target_person, dcon::office_id target_office, sys::date date) {
	if(!initiator || !target_person || !target_office || !s.world.person_get_alive(initiator)) return {};
	if(persons::active_tenure_for(s, target_office)) return {};
	auto institution = governance::institution_for_office(s, target_office);
	auto nation = governance::nation_of(s, institution);
	if(!institution || !nation || !active_authority_tenure_on_or_before(s, initiator, governance::authority_kind::appoint, nation, date)) return {};
	auto tenure = persons::appoint_person(s, target_person, target_office, date);
	if(!tenure) return {};
	return record_appointment(s, initiator, target_person, target_office, date);
}

dcon::institutional_action_id authorized_dismiss(sys::state& s, dcon::person_id initiator, dcon::office_id target_office, sys::date date) {
	if(!initiator || !target_office || !s.world.person_get_alive(initiator)) return {};
	auto tenure = persons::active_tenure_for(s, target_office);
	if(!tenure) return {};
	auto target_person = s.world.office_tenure_get_person_from_office_tenure_person(tenure);
	auto institution = governance::institution_for_office(s, target_office);
	auto nation = governance::nation_of(s, institution);
	if(!institution || !nation || !active_authority_tenure_on_or_before(s, initiator, governance::authority_kind::dismiss, nation, date)) return {};
	if(date < s.world.office_tenure_get_started_on(tenure)) return {};
	if(!persons::remove_from_office(s, target_office, date)) return {};
	return record_dismissal(s, initiator, target_person, target_office, date);
}

} // namespace governance::actions
