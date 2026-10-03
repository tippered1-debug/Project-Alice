#include "offices.hpp"

#include "system_state.hpp"
#include "governance/actions/actions.hpp"
#include "governance/legislature.hpp"
#include "persons/persons.hpp"

#include <vector>

namespace governance::offices {
namespace {

using actions::institutional_action_kind;

dcon::office_id appointer_of(sys::state const& state, dcon::office_id office) {
	auto relation = state.world.office_get_office_appointer_as_office(office);
	return relation ? state.world.office_appointer_get_appointer(relation) : dcon::office_id{};
}
dcon::office_id remover_of(sys::state const& state, dcon::office_id office) {
	auto relation = state.world.office_get_office_remover_as_office(office);
	return relation ? state.world.office_remover_get_remover(relation) : dcon::office_id{};
}
dcon::office_id successor_of(sys::state const& state, dcon::office_id office) {
	auto relation = state.world.office_get_office_successor_as_office(office);
	return relation ? state.world.office_successor_get_successor(relation) : dcon::office_id{};
}

dcon::nation_id nation_of_office(sys::state const& state, dcon::office_id office) {
	return nation_of(state, institution_for_office(state, office));
}

std::vector<dcon::office_tenure_id> active_tenures(sys::state const& state, dcon::office_id office) {
	std::vector<dcon::office_tenure_id> result;
	if(!office) return result;
	state.world.office_for_each_office_tenure_office_as_office(office, [&](dcon::office_tenure_office_id relation) {
		auto tenure = state.world.office_tenure_office_get_tenure(relation);
		if(state.world.office_tenure_get_active(tenure)) result.push_back(tenure);
	});
	return result;
}

dcon::office_tenure_id full_tenure(sys::state const& state, dcon::office_id office) {
	for(auto tenure : active_tenures(state, office))
		if(!state.world.office_tenure_get_acting(tenure)) return tenure;
	return {};
}

void end_tenure(sys::state& state, dcon::office_tenure_id tenure, sys::date date) {
	if(!tenure || !state.world.office_tenure_get_active(tenure)) return;
	auto started = state.world.office_tenure_get_started_on(tenure);
	state.world.office_tenure_set_active(tenure, uint8_t(0));
	state.world.office_tenure_set_ended_on(tenure, date < started ? started : date);
}

dcon::office_tenure_id start_tenure(sys::state& state, dcon::person_id person, dcon::office_id office, sys::date date, bool is_acting) {
	auto tenure = state.world.create_office_tenure();
	state.world.office_tenure_set_started_on(tenure, date);
	state.world.office_tenure_set_active(tenure, uint8_t(1));
	state.world.office_tenure_set_acting(tenure, uint8_t(is_acting ? 1 : 0));
	auto term = state.world.office_get_term_days(office);
	state.world.office_tenure_set_term_ends(tenure, !is_acting && term > 0 ? date + int32_t(term) : sys::date{});
	state.world.force_create_office_tenure_person(tenure, person);
	state.world.force_create_office_tenure_office(tenure, office);
	return tenure;
}

// Whether taking the office would combine offices an exclusive rule forbids.
bool exclusivity_conflict(sys::state const& state, dcon::person_id person, dcon::office_id office) {
	auto held = persons::active_offices_of(state, person);
	for(auto other : held) {
		if(other == office) continue;
		if(state.world.office_get_exclusive(office) || state.world.office_get_exclusive(other)) return true;
	}
	return false;
}

dcon::institutional_action_id record(sys::state& state, institutional_action_kind kind, dcon::person_id initiator,
	dcon::person_id target, dcon::office_id office, sys::date date) {
	auto action = state.world.create_institutional_action();
	state.world.institutional_action_set_kind(action, uint8_t(kind));
	state.world.institutional_action_set_occurred_on(action, date);
	if(initiator) state.world.force_create_institutional_action_initiator(action, initiator);
	if(target) state.world.force_create_institutional_action_target_person(action, target);
	state.world.force_create_institutional_action_target_office(action, office);
	return action;
}

void vacate_at_depth(sys::state& state, dcon::office_id office, sys::date date, int32_t depth) {
	for(auto tenure : active_tenures(state, office)) end_tenure(state, tenure, date);
	auto mode = succession_mode(state.world.office_get_succession(office));
	auto successor = successor_of(state, office);
	if(mode == succession_mode::none || !successor || depth > 8) return;
	auto successor_tenure = current_tenure(state, successor);
	auto heir = successor_tenure ? state.world.office_tenure_get_person_from_office_tenure_person(successor_tenure) : dcon::person_id{};
	if(!heir || !persons::alive(state, heir)) return;
	if(mode == succession_mode::acting) {
		(void)start_tenure(state, heir, office, date, true);
		(void)record(state, institutional_action_kind::succession, {}, heir, office, date);
		return;
	}
	// Full succession: the heir leaves their office for this one.
	end_tenure(state, successor_tenure, date);
	(void)start_tenure(state, heir, office, date, false);
	(void)record(state, institutional_action_kind::succession, {}, heir, office, date);
	vacate_at_depth(state, successor, date, depth + 1);
}

} // namespace

bool set_rules(sys::state& state, dcon::office_id office, office_rules const& rules) {
	if(!office || !state.world.office_is_valid(office)) return false;
	auto nation = nation_of_office(state, office);
	for(auto related : { rules.appointer, rules.remover, rules.successor })
		if(related && (related == office || nation_of_office(state, related) != nation)) return false;
	if(rules.confirmer && (nation_of(state, rules.confirmer) != nation
		|| kind_of(state, rules.confirmer) != institution_kind::legislative_chamber)) return false;
	if(rules.removal == removal_rule::by_remover && !rules.remover) return false;
	if(rules.succession != succession_mode::none && !rules.successor) return false;
	if(auto r = state.world.office_get_office_appointer_as_office(office)) state.world.delete_office_appointer(r);
	if(auto r = state.world.office_get_office_remover_as_office(office)) state.world.delete_office_remover(r);
	if(auto r = state.world.office_get_office_successor_as_office(office)) state.world.delete_office_successor(r);
	if(auto r = state.world.office_get_office_confirmer(office)) state.world.delete_office_confirmer(r);
	if(rules.appointer) state.world.force_create_office_appointer(office, rules.appointer);
	if(rules.remover) state.world.force_create_office_remover(office, rules.remover);
	if(rules.successor) state.world.force_create_office_successor(office, rules.successor);
	if(rules.confirmer) state.world.force_create_office_confirmer(office, rules.confirmer);
	state.world.office_set_removal(office, uint8_t(rules.removal));
	state.world.office_set_term_days(office, rules.term_days);
	state.world.office_set_succession(office, uint8_t(rules.succession));
	state.world.office_set_exclusive(office, uint8_t(rules.exclusive ? 1 : 0));
	return true;
}

office_rules rules_of(sys::state const& state, dcon::office_id office) {
	office_rules result{};
	if(!office) return result;
	result.appointer = appointer_of(state, office);
	result.remover = remover_of(state, office);
	result.successor = successor_of(state, office);
	result.confirmer = state.world.office_get_institution_from_office_confirmer(office);
	result.removal = removal_rule(state.world.office_get_removal(office));
	result.term_days = state.world.office_get_term_days(office);
	result.succession = succession_mode(state.world.office_get_succession(office));
	result.exclusive = state.world.office_get_exclusive(office) != 0;
	return result;
}

jurisdiction jurisdiction_of(sys::state const& state, dcon::office_id office) {
	return governance::jurisdiction_of(state, institution_for_office(state, office));
}

dcon::office_tenure_id current_tenure(sys::state const& state, dcon::office_id office) {
	dcon::office_tenure_id acting_tenure{};
	for(auto tenure : active_tenures(state, office)) {
		if(!state.world.office_tenure_get_acting(tenure)) return tenure;
		acting_tenure = tenure;
	}
	return acting_tenure;
}

dcon::person_id holder(sys::state const& state, dcon::office_id office) {
	auto tenure = current_tenure(state, office);
	return tenure ? state.world.office_tenure_get_person_from_office_tenure_person(tenure) : dcon::person_id{};
}

bool acting(sys::state const& state, dcon::office_tenure_id tenure) {
	return tenure && state.world.office_tenure_get_acting(tenure) != 0;
}

bool vacant(sys::state const& state, dcon::office_id office) {
	return !full_tenure(state, office);
}

dcon::office_tenure_id tenure_of(sys::state const& state, dcon::person_id person, dcon::office_id office, sys::date date) {
	if(!person || !persons::alive(state, person)) return {};
	for(auto tenure : active_tenures(state, office))
		if(state.world.office_tenure_get_person_from_office_tenure_person(tenure) == person
			&& !(date < state.world.office_tenure_get_started_on(tenure))) return tenure;
	return {};
}

exercise exercising(sys::state const& state, dcon::person_id person, authority_kind kind, jurisdiction scope, sys::date date) {
	if(!person || !persons::alive(state, person)) return {};
	for(auto office : persons::active_offices_of(state, person)) {
		auto tenure = tenure_of(state, person, office, date);
		if(tenure && has_authority(state, office, kind, scope, date)) return { office, tenure };
	}
	return {};
}

dcon::office_tenure_id install(sys::state& state, dcon::person_id person, dcon::office_id office, sys::date date) {
	if(!person || !office || !state.world.office_is_valid(office) || !persons::alive(state, person)
		|| !persons::born_on_or_before(state, person, date) || full_tenure(state, office)
		|| exclusivity_conflict(state, person, office)) return {};
	for(auto tenure : active_tenures(state, office)) end_tenure(state, tenure, date);
	auto tenure = start_tenure(state, person, office, date, false);
	(void)record(state, institutional_action_kind::installation, {}, person, office, date);
	return tenure;
}

dcon::institutional_action_id appoint(sys::state& state, dcon::person_id initiator, dcon::person_id candidate,
	dcon::office_id office, sys::date date) {
	if(!office || !state.world.office_is_valid(office) || !candidate || !persons::alive(state, candidate)
		|| !persons::born_on_or_before(state, candidate, date) || full_tenure(state, office)) return {};
	auto rules = rules_of(state, office);
	// The constitution names who appoints; that office's authority makes it lawful.
	if(!rules.appointer || !tenure_of(state, initiator, rules.appointer, date)
		|| !has_authority(state, rules.appointer, authority_kind::appoint, jurisdiction_of(state, office), date)) return {};
	if(rules.confirmer && !legislature::confirmation_passed(state, office, candidate, date)) return {};
	if(exclusivity_conflict(state, candidate, office)) return {};
	for(auto tenure : active_tenures(state, office)) end_tenure(state, tenure, date);
	(void)start_tenure(state, candidate, office, date, false);
	return record(state, institutional_action_kind::appointment, initiator, candidate, office, date);
}

dcon::institutional_action_id dismiss(sys::state& state, dcon::person_id initiator, dcon::office_id office, sys::date date) {
	auto tenure = current_tenure(state, office);
	if(!tenure || date < state.world.office_tenure_get_started_on(tenure)) return {};
	auto rules = rules_of(state, office);
	auto deciding = rules.removal == removal_rule::by_remover ? rules.remover
		: rules.removal == removal_rule::by_appointer ? rules.appointer : dcon::office_id{};
	if(!deciding || !tenure_of(state, initiator, deciding, date)
		|| !has_authority(state, deciding, authority_kind::dismiss, jurisdiction_of(state, office), date)) return {};
	auto target = state.world.office_tenure_get_person_from_office_tenure_person(tenure);
	auto action = record(state, institutional_action_kind::dismissal, initiator, target, office, date);
	vacate(state, office, date);
	return action;
}

bool resign(sys::state& state, dcon::person_id person, dcon::office_id office, sys::date date) {
	if(!tenure_of(state, person, office, date)) return false;
	(void)record(state, institutional_action_kind::resignation, person, person, office, date);
	vacate(state, office, date);
	return true;
}

void vacate(sys::state& state, dcon::office_id office, sys::date date) {
	if(!office || !state.world.office_is_valid(office)) return;
	vacate_at_depth(state, office, date, 0);
}

void expire_terms(sys::state& state, sys::date date) {
	std::vector<std::pair<dcon::office_id, sys::date>> expired;
	state.world.for_each_office_tenure([&](dcon::office_tenure_id tenure) {
		auto ends = state.world.office_tenure_get_term_ends(tenure);
		if(state.world.office_tenure_get_active(tenure) && ends && !(date < ends))
			expired.emplace_back(state.world.office_tenure_get_office_from_office_tenure_office(tenure), ends);
	});
	for(auto const& [office, ends] : expired) vacate(state, office, ends);
}

} // namespace governance::offices
