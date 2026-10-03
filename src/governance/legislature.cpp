#include "legislature.hpp"

#include "system_state.hpp"
#include "governance/offices.hpp"
#include "persons/persons.hpp"

namespace governance::legislature {
namespace {

dcon::motion_id create_motion(sys::state& state, dcon::institution_id chamber, motion_kind kind, sys::date date) {
	auto motion = state.world.create_motion();
	state.world.motion_set_kind(motion, uint8_t(kind));
	state.world.motion_set_opened_on(motion, date);
	state.world.force_create_motion_chamber(motion, chamber);
	return motion;
}

dcon::vote_id record_vote(sys::state& state, dcon::motion_id motion, dcon::person_id person, bool in_favor, sys::date date) {
	dcon::vote_id existing{};
	state.world.motion_for_each_vote_motion_as_motion(motion, [&](auto relation) {
		auto vote = state.world.vote_motion_get_vote(relation);
		if(state.world.vote_get_person_from_vote_person(vote) == person) existing = vote;
	});
	auto vote = existing ? existing : state.world.create_vote();
	state.world.vote_set_in_favor(vote, uint8_t(in_favor ? 1 : 0));
	state.world.vote_set_cast_on(vote, date);
	if(!existing) {
		state.world.force_create_vote_motion(vote, motion);
		state.world.force_create_vote_person(vote, person);
	}
	return vote;
}

} // namespace

bool is_chamber(sys::state const& state, dcon::institution_id institution) {
	return institution && state.world.institution_is_valid(institution)
		&& kind_of(state, institution) == institution_kind::legislative_chamber;
}

std::vector<dcon::institution_id> chambers_of(sys::state const& state, dcon::institution_id legislature) {
	std::vector<dcon::institution_id> result;
	for(auto child : children_of(state, legislature))
		if(is_chamber(state, child)) result.push_back(child);
	return result;
}

std::vector<dcon::office_id> seats_of(sys::state const& state, dcon::institution_id chamber) {
	std::vector<dcon::office_id> result;
	for(auto office : offices_of(state, chamber))
		if(state.world.office_get_kind(office) == uint8_t(office_kind::legislator)) result.push_back(office);
	return result;
}

dcon::office_id seat_of(sys::state const& state, dcon::person_id person, dcon::institution_id chamber, sys::date date) {
	for(auto seat : seats_of(state, chamber))
		if(offices::tenure_of(state, person, seat, date)) return seat;
	return {};
}

uint32_t filled_seats(sys::state const& state, dcon::institution_id chamber, sys::date date) {
	uint32_t result = 0;
	for(auto seat : seats_of(state, chamber)) {
		auto tenure = offices::current_tenure(state, seat);
		if(tenure && !(date < state.world.office_tenure_get_started_on(tenure))) ++result;
	}
	return result;
}

dcon::motion_id find_instrument_motion(sys::state const& state, dcon::institution_id chamber, dcon::legal_instrument_id instrument, motion_kind kind) {
	dcon::motion_id result{};
	if(!chamber || !instrument) return result;
	state.world.institution_for_each_motion_chamber_as_institution(chamber, [&](auto relation) {
		auto motion = state.world.motion_chamber_get_motion(relation);
		if(state.world.motion_get_kind(motion) == uint8_t(kind)
			&& state.world.motion_get_legal_instrument_from_motion_instrument(motion) == instrument) result = motion;
	});
	return result;
}

dcon::motion_id find_confirmation_motion(sys::state const& state, dcon::institution_id chamber, dcon::office_id office, dcon::person_id candidate) {
	dcon::motion_id result{};
	if(!chamber || !office || !candidate) return result;
	state.world.institution_for_each_motion_chamber_as_institution(chamber, [&](auto relation) {
		auto motion = state.world.motion_chamber_get_motion(relation);
		if(state.world.motion_get_kind(motion) == uint8_t(motion_kind::confirm)
			&& state.world.motion_get_office_from_motion_office(motion) == office
			&& state.world.motion_get_person_from_motion_candidate(motion) == candidate) result = motion;
	});
	return result;
}

dcon::vote_id vote_on_instrument(sys::state& state, dcon::person_id person, dcon::institution_id chamber,
	dcon::legal_instrument_id instrument, motion_kind kind, bool in_favor, sys::date date) {
	if(kind == motion_kind::confirm || !instrument || !state.world.legal_instrument_is_valid(instrument)
		|| !is_chamber(state, chamber) || !seat_of(state, person, chamber, date)) return {};
	auto motion = find_instrument_motion(state, chamber, instrument, kind);
	if(!motion) {
		motion = create_motion(state, chamber, kind, date);
		state.world.force_create_motion_instrument(motion, instrument);
	}
	return record_vote(state, motion, person, in_favor, date);
}

dcon::vote_id vote_on_confirmation(sys::state& state, dcon::person_id person, dcon::institution_id chamber,
	dcon::office_id office, dcon::person_id candidate, bool in_favor, sys::date date) {
	if(!office || !candidate || !is_chamber(state, chamber) || offices::rules_of(state, office).confirmer != chamber
		|| !seat_of(state, person, chamber, date)) return {};
	auto motion = find_confirmation_motion(state, chamber, office, candidate);
	if(!motion) {
		motion = create_motion(state, chamber, motion_kind::confirm, date);
		state.world.force_create_motion_office(motion, office);
		state.world.force_create_motion_candidate(motion, candidate);
	}
	return record_vote(state, motion, person, in_favor, date);
}

bool passed(sys::state const& state, dcon::motion_id motion, sys::date date, float threshold) {
	if(!motion || !state.world.motion_is_valid(motion)) return false;
	auto chamber = state.world.motion_get_institution_from_motion_chamber(motion);
	auto filled = filled_seats(state, chamber, date);
	if(filled == 0) return false;
	uint32_t in_favor = 0;
	state.world.motion_for_each_vote_motion_as_motion(motion, [&](auto relation) {
		auto vote = state.world.vote_motion_get_vote(relation);
		auto person = state.world.vote_get_person_from_vote_person(vote);
		// Only votes cast by today's seat holders count.
		if(state.world.vote_get_in_favor(vote) && !(date < state.world.vote_get_cast_on(vote))
			&& seat_of(state, person, chamber, date)) ++in_favor;
	});
	return float(in_favor) > threshold * float(filled);
}

std::vector<dcon::institution_id> deciding_chambers(sys::state const& state, authority_kind kind, jurisdiction scope, sys::date date) {
	std::vector<dcon::institution_id> result;
	auto nation = scope.nation ? scope.nation : governance::nation_of(state, scope.territory);
	for(auto institution : institutions_of(state, nation))
		if(is_chamber(state, institution) && has_authority(state, institution, kind, scope, date)) result.push_back(institution);
	return result;
}

bool instrument_passed(sys::state const& state, dcon::legal_instrument_id instrument, motion_kind kind,
	authority_kind authority, jurisdiction scope, sys::date date, float threshold) {
	auto chambers = deciding_chambers(state, authority, scope, date);
	if(chambers.empty()) return false;
	for(auto chamber : chambers)
		if(!passed(state, find_instrument_motion(state, chamber, instrument, kind), date, threshold)) return false;
	return true;
}

bool confirmation_passed(sys::state const& state, dcon::office_id office, dcon::person_id candidate, sys::date date) {
	auto chamber = offices::rules_of(state, office).confirmer;
	return chamber && passed(state, find_confirmation_motion(state, chamber, office, candidate), date, simple_majority);
}

} // namespace governance::legislature
