#include "command.hpp"

#include "system_state.hpp"
#include "governance/governance.hpp"
#include "governance/offices.hpp"

namespace governance::command {
namespace {

dcon::nation_id owner_of(sys::state const& state, military::land_forces::stable_id formation) {
	auto unit = military::land_forces::find_formation(state, formation);
	return unit ? unit->owner : dcon::nation_id{};
}

} // namespace

dcon::institution_id commanding_institution(sys::state const& state, dcon::nation_id nation) {
	return find_institution(state, nation, institution_kind::military_command);
}

bool may_command(sys::state const& state, dcon::person_id person, dcon::nation_id nation, sys::date date) {
	return nation && bool(offices::exercising(state, person, authority_kind::command_forces, national(nation), date));
}

bool authorized_move(sys::state& state, dcon::person_id person, military::land_forces::stable_id formation, dcon::site_id destination) {
	if(!may_command(state, person, owner_of(state, formation), state.current_date)) return false;
	return military::land_forces::move_formation(state, formation, destination);
}

uint64_t authorized_recruit(sys::state& state, dcon::person_id person, military::land_forces::stable_id formation,
	std::span<persons::person_key const> candidates, uint64_t requested, uint16_t training_days) {
	if(!may_command(state, person, owner_of(state, formation), state.current_date)) return 0;
	return military::land_forces::recruit_personnel(state, formation, candidates, requested, training_days);
}

uint64_t authorized_demobilize(sys::state& state, dcon::person_id person, military::land_forces::stable_id formation) {
	if(!may_command(state, person, owner_of(state, formation), state.current_date)) return 0;
	return military::land_forces::demobilize(state, formation);
}

} // namespace governance::command
