#include "command.hpp"

#include "system_state.hpp"
#include "governance/governance.hpp"
#include "governance/offices.hpp"
#include "governance/power_topology.hpp"

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
	if(!nation || !person || !persons::alive(state, person)) return false;
	for(auto office : persons::active_offices_of(state, person)) {
		if(!offices::tenure_of(state, person, office, date)
			|| !has_authority(state, office, authority_kind::command_forces, national(nation), date)) continue;
		if(power_topology::command_chain_allows(state, office, person, nation, date)) return true;
	}
	return false;
}

bool authorized_move(sys::state& state, dcon::person_id person, military::land_forces::stable_id formation, dcon::site_id destination) {
	auto const owner = owner_of(state, formation);
	if(!may_command(state, person, owner, state.current_date) || !destination || !state.world.site_is_valid(destination)) return false;
	// An order, not a relocation: the formation marches there in game time.
	return military::land_forces::order_march(state, owner, formation,
		state.world.site_get_province_from_site_location(destination), false) != 0;
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

dcon::person_id commander(sys::state const& state, dcon::nation_id nation) {
	if(!nation) return {};
	for(auto institution : institutions_of(state, nation))
		for(auto office : offices_of(state, institution)) {
			auto person = offices::holder(state, office);
			if(person && may_command(state, person, nation, state.current_date)) return person;
		}
	return {};
}

bool forces_answer(sys::state const& state, dcon::nation_id nation) {
	if(!nation) return false;
	if(commander(state, nation)) return true;
	// No one may command. That stops the forces only where the institution's
	// own chain of command has been declared and so stands broken.
	auto defense = commanding_institution(state, nation);
	auto actor = defense ? actor_for_institution(state, defense) : dcon::economic_actor_id{};
	if(!actor) return true;
	for(auto const& edge : power_topology::to(state, power_topology::actor_node(actor), state.current_date))
		if(edge.kind == power_topology::relation_kind::commands && edge.intensity > 0.0f) return false;
	return true;
}

uint32_t authorized_march(sys::state& state, dcon::person_id person, military::land_forces::stable_id formation,
	dcon::province_id destination, bool with_subordinates) {
	auto const owner = owner_of(state, formation);
	if(!may_command(state, person, owner, state.current_date)) return 0;
	return military::land_forces::order_march(state, owner, formation, destination, with_subordinates);
}

uint32_t authorized_halt(sys::state& state, dcon::person_id person, military::land_forces::stable_id formation,
	bool with_subordinates) {
	auto const owner = owner_of(state, formation);
	if(!may_command(state, person, owner, state.current_date)) return 0;
	return military::land_forces::halt_march(state, owner, formation, with_subordinates);
}

uint32_t authorized_resupply(sys::state& state, dcon::person_id person, military::land_forces::stable_id formation,
	float days, bool with_subordinates) {
	auto const owner = owner_of(state, formation);
	if(!may_command(state, person, owner, state.current_date)) return 0;
	return military::land_forces::request_resupply(state, owner, formation, days, with_subordinates);
}

uint64_t authorized_replenish(sys::state& state, dcon::person_id person, military::land_forces::stable_id formation,
	uint16_t training_days, bool with_subordinates) {
	auto const owner = owner_of(state, formation);
	if(!may_command(state, person, owner, state.current_date)) return 0;
	return military::land_forces::replenish(state, owner, formation, training_days, with_subordinates);
}

} // namespace governance::command
