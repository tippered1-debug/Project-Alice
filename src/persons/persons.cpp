#include "persons.hpp"

#include "actors/ownership.hpp"
#include "governance/governance.hpp"
#include "system_state.hpp"

namespace persons {

dcon::person_id create_person(sys::state& s, sys::date birth_date) {
	auto p = s.world.create_person();
	s.world.person_set_birth_date(p, birth_date);
	s.world.person_set_alive(p, uint8_t(1));
	auto actor = s.world.create_economic_actor();
	s.world.economic_actor_set_kind(actor, uint8_t(actors::ownership::actor_kind::person));
	s.world.force_create_person_actor(p, actor);
	return p;
}

dcon::economic_actor_id actor_for_person(sys::state const& s, dcon::person_id p) {
	return p ? s.world.person_get_economic_actor_from_person_actor(p) : dcon::economic_actor_id{};
}

dcon::office_tenure_id active_tenure_for(sys::state const& s, dcon::office_id office) {
	dcon::office_tenure_id result{};
	if(!office) return result;
	s.world.office_for_each_office_tenure_office_as_office(office, [&](dcon::office_tenure_office_id relation) {
		auto tenure = s.world.office_tenure_office_get_tenure(relation);
		if(s.world.office_tenure_get_active(tenure)) result = tenure;
	});
	return result;
}

dcon::office_tenure_id appoint_person(sys::state& s, dcon::person_id person, dcon::office_id office, sys::date date) {
	if(!person || !office || !s.world.person_get_alive(person) || date < s.world.person_get_birth_date(person)) return {};
	if(auto active = active_tenure_for(s, office)) {
		auto occupant = s.world.office_tenure_get_person_from_office_tenure_person(active);
		return occupant == person ? active : dcon::office_tenure_id{};
	}
	auto tenure = s.world.create_office_tenure();
	s.world.office_tenure_set_started_on(tenure, date);
	s.world.office_tenure_set_active(tenure, uint8_t(1));
	s.world.force_create_office_tenure_person(tenure, person);
	s.world.force_create_office_tenure_office(tenure, office);
	return tenure;
}

bool remove_from_office(sys::state& s, dcon::office_id office, sys::date date) {
	auto tenure = active_tenure_for(s, office);
	if(!tenure || date < s.world.office_tenure_get_started_on(tenure)) return false;
	s.world.office_tenure_set_active(tenure, uint8_t(0));
	s.world.office_tenure_set_ended_on(tenure, date);
	return true;
}

dcon::person_id occupant_of(sys::state const& s, dcon::office_id office) {
	auto tenure = active_tenure_for(s, office);
	return tenure ? s.world.office_tenure_get_person_from_office_tenure_person(tenure) : dcon::person_id{};
}

std::vector<dcon::office_id> active_offices_of(sys::state const& s, dcon::person_id person) {
	std::vector<dcon::office_id> result;
	if(!person) return result;
	s.world.person_for_each_office_tenure_person_as_person(person, [&](dcon::office_tenure_person_id relation) {
		auto tenure = s.world.office_tenure_person_get_tenure(relation);
		if(s.world.office_tenure_get_active(tenure)) result.push_back(s.world.office_tenure_get_office_from_office_tenure_office(tenure));
	});
	return result;
}

template<typename Scope>
bool has_authority_via_offices(sys::state const& s, dcon::person_id person, governance::authority_kind kind, Scope scope) {
	for(auto office : active_offices_of(s, person)) {
		if(governance::has_authority(s, office, kind, scope)) return true;
	}
	return false;
}

bool person_has_authority(sys::state const& s, dcon::person_id person, governance::authority_kind kind, dcon::nation_id nation) {
	return has_authority_via_offices(s, person, kind, nation);
}

dcon::office_tenure_id authority_tenure_on_or_before(sys::state const& s, dcon::person_id person,
	governance::authority_kind kind, dcon::nation_id nation, sys::date date) {
	for(auto office : active_offices_of(s, person)) {
		auto tenure = active_tenure_for(s, office);
		if(tenure && s.world.office_tenure_get_started_on(tenure) <= date
			&& governance::has_authority(s, office, kind, nation)) return tenure;
	}
	return {};
}

bool person_has_authority(sys::state const& s, dcon::person_id person, governance::authority_kind kind, dcon::territorial_unit_id territorial_unit) {
	return has_authority_via_offices(s, person, kind, territorial_unit);
}

bool mark_dead(sys::state& s, dcon::person_id person, sys::date date) {
	if(!person || !s.world.person_get_alive(person) || date < s.world.person_get_birth_date(person)) return false;
	auto offices = active_offices_of(s, person);
	for(auto office : offices) {
		auto tenure = active_tenure_for(s, office);
		if(tenure && s.world.office_tenure_get_started_on(tenure) > date) return false;
	}
	s.world.person_set_alive(person, uint8_t(0));
	s.world.person_set_death_date(person, date);
	for(auto office : offices) remove_from_office(s, office, date);
	return true;
}

} // namespace persons
