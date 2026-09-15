#include "consent.hpp"

#include "actors/organizations/organizations.hpp"
#include "persons/persons.hpp"
#include "system_state.hpp"

#include <cmath>

namespace economy::consent {

namespace {
bool valid_amount(float amount) { return std::isfinite(amount) && amount > 0.0f; }
bool valid_kind(proposal_kind kind) { return uint8_t(kind) <= uint8_t(proposal_kind::investment); }
bool valid_decision_kind(decision_kind kind) { return uint8_t(kind) <= uint8_t(decision_kind::invest); }

bool mandate_active(sys::state const& state, dcon::organization_id organization, dcon::person_id person,
	decision_kind kind, sys::date date) {
	bool result = false;
	state.world.organization_for_each_organization_decision_mandate_organization_as_organization(organization,
		[&](dcon::organization_decision_mandate_organization_id relation) {
			auto mandate = state.world.organization_decision_mandate_organization_get_organization_decision_mandate(relation);
			if(!mandate || state.world.organization_decision_mandate_get_person_from_organization_decision_mandate_person(mandate) != person) return;
			if(state.world.organization_decision_mandate_get_decision_kind(mandate) == uint8_t(kind)
				&& state.world.organization_decision_mandate_get_started_on(mandate) <= date
				&& (!state.world.organization_decision_mandate_get_ended_on(mandate) || date < state.world.organization_decision_mandate_get_ended_on(mandate))) result = true;
		});
	return result;
}

decision_kind required_for(sys::state const& state, dcon::economic_proposal_id proposal, dcon::economic_actor_id actor) {
	auto kind = proposal_kind(state.world.economic_proposal_get_kind(proposal));
	if(kind == proposal_kind::loan) {
		auto lender = state.world.economic_proposal_get_economic_actor_from_economic_proposal_actor_a(proposal);
		return actor == lender ? decision_kind::lend : decision_kind::borrow;
	}
	return decision_kind::invest;
}

bool accepted_by(sys::state const& state, dcon::economic_proposal_id proposal, dcon::economic_actor_id actor) {
	for(auto decision : state.world.in_economic_decision) {
		if(state.world.economic_decision_get_economic_proposal_from_economic_decision_proposal(decision) == proposal
			&& state.world.economic_decision_get_economic_actor_from_economic_decision_deciding_actor(decision) == actor
			&& state.world.economic_decision_get_accepted(decision) != 0) return true;
	}
	return false;
}

bool rejected_by(sys::state const& state, dcon::economic_proposal_id proposal) {
	for(auto decision : state.world.in_economic_decision)
		if(state.world.economic_decision_get_economic_proposal_from_economic_decision_proposal(decision) == proposal
			&& state.world.economic_decision_get_accepted(decision) == 0) return true;
	return false;
}
}

dcon::organization_decision_mandate_id create_mandate(sys::state& state, dcon::organization_id organization,
	dcon::person_id person, decision_kind kind, sys::date started_on, sys::date ended_on) {
	if(!organization || !state.world.organization_is_valid(organization) || !person || !state.world.person_is_valid(person)
		|| !valid_decision_kind(kind) || (ended_on && ended_on < started_on)) return {};
	auto mandate = state.world.create_organization_decision_mandate();
	state.world.organization_decision_mandate_set_decision_kind(mandate, uint8_t(kind));
	state.world.organization_decision_mandate_set_started_on(mandate, started_on);
	state.world.organization_decision_mandate_set_ended_on(mandate, ended_on);
	state.world.force_create_organization_decision_mandate_organization(mandate, organization);
	state.world.force_create_organization_decision_mandate_person(mandate, person);
	return mandate;
}

bool can_decide_for_actor(sys::state const& state, dcon::person_id person, dcon::economic_actor_id actor,
	decision_kind kind, sys::date date) {
	if(!person || !state.world.person_is_valid(person) || !state.world.person_get_alive(person)
		|| !actor || !state.world.economic_actor_is_valid(actor) || !valid_decision_kind(kind)) return false;
	if(persons::actor_for_person(state, person) == actor) return true;
	auto organization = actors::organizations::organization_for_actor(state, actor);
	if(!organization) return false;
	return mandate_active(state, organization, person, kind, date);
}

dcon::economic_proposal_id create_proposal(sys::state& state, proposal_kind kind,
	dcon::economic_actor_id actor_a, dcon::economic_actor_id actor_b, dcon::commodity_id settlement,
	float amount, sys::date created_on) {
	if(!valid_kind(kind) || !actor_a || !actor_b || actor_a == actor_b
		|| !state.world.economic_actor_is_valid(actor_a) || !state.world.economic_actor_is_valid(actor_b)
		|| !settlement || !state.world.commodity_is_valid(settlement) || !valid_amount(amount)) return {};
	auto proposal = state.world.create_economic_proposal();
	state.world.economic_proposal_set_kind(proposal, uint8_t(kind));
	state.world.economic_proposal_set_settlement(proposal, settlement);
	state.world.economic_proposal_set_amount(proposal, amount);
	state.world.economic_proposal_set_created_on(proposal, created_on);
	state.world.economic_proposal_set_status(proposal, uint8_t(proposal_status::pending));
	state.world.force_create_economic_proposal_actor_a(proposal, actor_a);
	state.world.force_create_economic_proposal_actor_b(proposal, actor_b);
	return proposal;
}

dcon::economic_decision_id accept_proposal(sys::state& state, dcon::economic_proposal_id proposal,
	dcon::economic_actor_id actor, dcon::person_id person, sys::date date) {
	if(!proposal || !state.world.economic_proposal_is_valid(proposal)
		|| state.world.economic_proposal_get_status(proposal) != uint8_t(proposal_status::pending)
		|| !actor || !person || !can_decide_for_actor(state, person, actor, required_for(state, proposal, actor), date)) return {};
	auto a = state.world.economic_proposal_get_economic_actor_from_economic_proposal_actor_a(proposal);
	auto b = state.world.economic_proposal_get_economic_actor_from_economic_proposal_actor_b(proposal);
	if(actor != a && actor != b) return {};
	if(accepted_by(state, proposal, actor)) return {};
	auto decision = state.world.create_economic_decision();
	state.world.economic_decision_set_accepted(decision, 1);
	state.world.economic_decision_set_occurred_on(decision, date);
	state.world.force_create_economic_decision_proposal(decision, proposal);
	state.world.force_create_economic_decision_deciding_actor(decision, actor);
	state.world.force_create_economic_decision_deciding_person(decision, person);
	return decision;
}

dcon::economic_decision_id reject_proposal(sys::state& state, dcon::economic_proposal_id proposal,
	dcon::economic_actor_id actor, dcon::person_id person, sys::date date) {
	if(!proposal || !state.world.economic_proposal_is_valid(proposal)
		|| state.world.economic_proposal_get_status(proposal) != uint8_t(proposal_status::pending)
		|| !actor || !person || !can_decide_for_actor(state, person, actor, required_for(state, proposal, actor), date)) return {};
	auto a = state.world.economic_proposal_get_economic_actor_from_economic_proposal_actor_a(proposal);
	auto b = state.world.economic_proposal_get_economic_actor_from_economic_proposal_actor_b(proposal);
	if(actor != a && actor != b) return {};
	auto decision = state.world.create_economic_decision();
	state.world.economic_decision_set_accepted(decision, 0);
	state.world.economic_decision_set_occurred_on(decision, date);
	state.world.force_create_economic_decision_proposal(decision, proposal);
	state.world.force_create_economic_decision_deciding_actor(decision, actor);
	state.world.force_create_economic_decision_deciding_person(decision, person);
	state.world.economic_proposal_set_status(proposal, uint8_t(proposal_status::rejected));
	return decision;
}

bool proposal_fully_accepted(sys::state const& state, dcon::economic_proposal_id proposal, sys::date date) {
	if(!proposal || !state.world.economic_proposal_is_valid(proposal)
		|| state.world.economic_proposal_get_status(proposal) != uint8_t(proposal_status::pending)) return false;
	auto a = state.world.economic_proposal_get_economic_actor_from_economic_proposal_actor_a(proposal);
	auto b = state.world.economic_proposal_get_economic_actor_from_economic_proposal_actor_b(proposal);
	if(rejected_by(state, proposal) || date < state.world.economic_proposal_get_created_on(proposal)) return false;
	// Investment proposals represent the issuer's already-authorized offer.  The
	// investor's acceptance is the additional consent required before execution.
	if(proposal_kind(state.world.economic_proposal_get_kind(proposal)) == proposal_kind::investment)
		return accepted_by(state, proposal, b);
	return accepted_by(state, proposal, a) && accepted_by(state, proposal, b);
}

bool mark_executed(sys::state& state, dcon::economic_proposal_id proposal) {
	if(!proposal || !state.world.economic_proposal_is_valid(proposal)
		|| state.world.economic_proposal_get_status(proposal) != uint8_t(proposal_status::pending)) return false;
	state.world.economic_proposal_set_status(proposal, uint8_t(proposal_status::executed));
	return true;
}

} // namespace economy::consent
