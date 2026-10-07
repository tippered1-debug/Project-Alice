#include "power_topology.hpp"

#include "system_state.hpp"
#include "actors/organizations/organizations.hpp"
#include "governance/governance.hpp"
#include "governance/offices.hpp"
#include "governance/parties.hpp"
#include "persons/persons.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace governance::power_topology {
namespace {

constexpr std::array<std::string_view, 15> relation_names{
	"appoints", "confirms", "dismisses", "commands", "supervises", "vetoes",
	"controls_voting", "controls_management", "funds", "funding_dependence",
	"embeds_organization", "nominates_cadres", "sets_strategic_direction", "political_influence", "member_of"
};

bool valid_node(sys::state const& state, node value) {
	if(bool(value.actor) == bool(value.office)) return false;
	if(value.actor) return state.world.economic_actor_is_valid(value.actor);
	return state.world.office_is_valid(value.office)
		&& nation_of(state, institution_for_office(state, value.office));
}

node source_of(sys::state const& state, dcon::power_relationship_id id) {
	return {
		state.world.power_relationship_get_actor_from_power_relationship_source_actor(id),
		state.world.power_relationship_get_office_from_power_relationship_source_office(id)
	};
}

node target_of(sys::state const& state, dcon::power_relationship_id id) {
	return {
		state.world.power_relationship_get_actor_from_power_relationship_target_actor(id),
		state.world.power_relationship_get_office_from_power_relationship_target_office(id)
	};
}

bool equal_interval(sys::state const& state, dcon::power_relationship_id id,
	relation_kind kind, terms const& value) {
	return state.world.power_relationship_get_kind(id) == uint8_t(kind)
		&& state.world.power_relationship_get_intensity(id) == value.intensity
		&& state.world.power_relationship_get_valid_from(id) == value.valid_from
		&& state.world.power_relationship_get_valid_until(id) == value.valid_until;
}

relationship view_of(sys::state const& state, dcon::power_relationship_id id) {
	return {
		id,
		source_of(state, id),
		relation_kind(state.world.power_relationship_get_kind(id)),
		target_of(state, id),
		state.world.power_relationship_get_intensity(id),
		state.world.power_relationship_get_valid_from(id),
		state.world.power_relationship_get_valid_until(id)
	};
}

bool is_active(sys::state const& state, dcon::power_relationship_id id, sys::date date) {
	if(!id || !state.world.power_relationship_is_valid(id)) return false;
	auto from = state.world.power_relationship_get_valid_from(id);
	auto until = state.world.power_relationship_get_valid_until(id);
	if(from && date < from) return false;
	if(until && !(date < until)) return false;
	return true;
}

bool active_positive(relationship const& edge) {
	return edge.intensity > 0.0f;
}

bool member_of(sys::state const& state, dcon::person_id person, node organization, sys::date date) {
	if(!person || !organization) return false;
	auto person_actor = persons::actor_for_person(state, person);
	if(!person_actor) return false;
	for(auto const& edge : from(state, actor_node(person_actor), date))
		if(active_positive(edge) && edge.kind == relation_kind::member_of && edge.target == organization) return true;
	// Political party membership is already represented by the party subsystem;
	// it also qualifies a cadre where a scenario names the party itself.
	if(organization.actor) {
		auto party = actors::organizations::organization_for_actor(state, organization.actor);
		if(party && parties::party_of(state, person) == party) return true;
	}
	return false;
}

bool controlled_by(sys::state const& state, dcon::person_id person, node controller, sys::date date) {
	if(!person || !controller) return false;
	auto person_actor = persons::actor_for_person(state, person);
	if(controller.actor && controller.actor == person_actor) return true;
	if(controller.office && offices::tenure_of(state, person, controller.office, date)) return true;
	return member_of(state, person, controller, date);
}

bool nomination_relation(relation_kind kind) {
	return kind == relation_kind::appoints || kind == relation_kind::controls_management
		|| kind == relation_kind::nominates_cadres;
}

std::vector<node> office_targets(sys::state const& state, dcon::office_id office) {
	std::vector<node> result;
	if(!office || !state.world.office_is_valid(office)) return result;
	result.push_back(office_node(office));
	auto institution = institution_for_office(state, office);
	auto actor = institution ? actor_for_institution(state, institution) : dcon::economic_actor_id{};
	if(actor) result.push_back(actor_node(actor));
	return result;
}

bool has_electoral_rule(sys::state const& state, dcon::office_id office) {
	if(state.world.office_get_electoral_system(office) != 0) return true;
	auto institution = institution_for_office(state, office);
	return institution && state.world.institution_get_electoral_system(institution) != 0;
}

bool vetoes_node(sys::state const& state, node target, sys::date date) {
	for(auto const& edge : to(state, target, date))
		if(active_positive(edge) && edge.kind == relation_kind::vetoes) return true;
	return false;
}

} // namespace

bool parse_relation_kind(std::string_view value, relation_kind& result) {
	for(size_t i = 0; i < relation_names.size(); ++i) {
		if(relation_names[i] == value) {
			result = relation_kind(i);
			return true;
		}
	}
	return false;
}

std::string_view name_of(relation_kind kind) {
	auto index = size_t(kind);
	return index < relation_names.size() ? relation_names[index] : std::string_view{};
}

dcon::power_relationship_id create(sys::state& state, node source, relation_kind kind,
	node target, terms const& value) {
	if(!valid_node(state, source) || !valid_node(state, target) || source == target
		|| size_t(kind) >= relation_names.size() || !std::isfinite(value.intensity)
		|| value.intensity < 0.0f || value.intensity > 1.0f
		|| (value.valid_from && value.valid_until && !(value.valid_from < value.valid_until))) return {};

	bool duplicate = false;
	auto check = [&](dcon::power_relationship_id existing) {
		if(!duplicate && state.world.power_relationship_is_valid(existing)
			&& source_of(state, existing) == source && target_of(state, existing) == target
			&& equal_interval(state, existing, kind, value)) duplicate = true;
	};
	if(source.actor) {
		state.world.economic_actor_for_each_power_relationship_source_actor_as_actor(source.actor, [&](auto link) {
			check(state.world.power_relationship_source_actor_get_relationship(link));
		});
	} else {
		state.world.office_for_each_power_relationship_source_office_as_office(source.office, [&](auto link) {
			check(state.world.power_relationship_source_office_get_relationship(link));
		});
	}
	if(duplicate) return {};

	auto id = state.world.create_power_relationship();
	state.world.power_relationship_set_kind(id, uint8_t(kind));
	state.world.power_relationship_set_intensity(id, value.intensity);
	state.world.power_relationship_set_valid_from(id, value.valid_from);
	state.world.power_relationship_set_valid_until(id, value.valid_until);
	if(source.actor) state.world.force_create_power_relationship_source_actor(id, source.actor);
	else state.world.force_create_power_relationship_source_office(id, source.office);
	if(target.actor) state.world.force_create_power_relationship_target_actor(id, target.actor);
	else state.world.force_create_power_relationship_target_office(id, target.office);
	return id;
}

bool revoke(sys::state& state, dcon::power_relationship_id id, sys::date date) {
	if(!id || !state.world.power_relationship_is_valid(id) || !date) return false;
	auto until = state.world.power_relationship_get_valid_until(id);
	if(until && !(date < until)) return false;
	state.world.power_relationship_set_valid_until(id, date);
	return true;
}

bool valid_at(sys::state const& state, dcon::power_relationship_id id, sys::date date) {
	return is_active(state, id, date);
}

std::vector<relationship> from(sys::state const& state, node source, sys::date date) {
	std::vector<relationship> result;
	if(!valid_node(state, source)) return result;
	if(source.actor) {
		state.world.economic_actor_for_each_power_relationship_source_actor_as_actor(source.actor, [&](auto link) {
			auto id = state.world.power_relationship_source_actor_get_relationship(link);
			if(is_active(state, id, date)) result.push_back(view_of(state, id));
		});
	} else {
		state.world.office_for_each_power_relationship_source_office_as_office(source.office, [&](auto link) {
			auto id = state.world.power_relationship_source_office_get_relationship(link);
			if(is_active(state, id, date)) result.push_back(view_of(state, id));
		});
	}
	std::sort(result.begin(), result.end(), [](auto const& a, auto const& b) { return a.id.index() < b.id.index(); });
	return result;
}

std::vector<relationship> to(sys::state const& state, node target, sys::date date) {
	std::vector<relationship> result;
	if(!valid_node(state, target)) return result;
	if(target.actor) {
		state.world.economic_actor_for_each_power_relationship_target_actor_as_actor(target.actor, [&](auto link) {
			auto id = state.world.power_relationship_target_actor_get_relationship(link);
			if(is_active(state, id, date)) result.push_back(view_of(state, id));
		});
	} else {
		state.world.office_for_each_power_relationship_target_office_as_office(target.office, [&](auto link) {
			auto id = state.world.power_relationship_target_office_get_relationship(link);
			if(is_active(state, id, date)) result.push_back(view_of(state, id));
		});
	}
	std::sort(result.begin(), result.end(), [](auto const& a, auto const& b) { return a.id.index() < b.id.index(); });
	return result;
}

bool appointment_candidate_eligible(sys::state const& state, dcon::office_id office,
	dcon::person_id candidate, sys::date date) {
	if(!office || !state.world.office_is_valid(office) || !candidate
		|| !persons::alive(state, candidate) || !persons::born_on_or_before(state, candidate, date)) return false;
	auto candidate_actor = persons::actor_for_person(state, candidate);
	if(!candidate_actor) return false;
	auto targets = office_targets(state, office);
	for(auto target : targets)
		if(vetoes_node(state, target, date)) return false;
	if(vetoes_node(state, actor_node(candidate_actor), date)) return false;

	std::vector<node> nominators;
	for(auto target : targets)
		for(auto const& edge : to(state, target, date))
			if(active_positive(edge) && (nomination_relation(edge.kind)
				|| (edge.kind == relation_kind::controls_voting
					&& has_electoral_rule(state, office))))
				nominators.push_back(edge.source);
	if(nominators.empty()) return true;
	for(auto nominator : nominators)
		if(controlled_by(state, candidate, nominator, date)) return true;
	return false;
}

bool dismissal_allowed(sys::state const& state, dcon::office_id office,
	dcon::person_id initiator, dcon::person_id incumbent, sys::date date) {
	if(!office || !state.world.office_is_valid(office) || !initiator || !incumbent
		|| !persons::alive(state, initiator)) return false;
	if(dismissal_vetoed(state, office, incumbent, date)) return false;
	auto initiator_actor = persons::actor_for_person(state, initiator);
	if(!initiator_actor) return false;
	auto targets = office_targets(state, office);
	std::vector<node> dismissers;
	for(auto target : targets)
		for(auto const& edge : to(state, target, date))
			if(active_positive(edge) && edge.kind == relation_kind::dismisses)
				dismissers.push_back(edge.source);
	if(dismissers.empty()) return true;
	for(auto source : dismissers)
		if(controlled_by(state, initiator, source, date)) return true;
	return false;
}

bool dismissal_vetoed(sys::state const& state, dcon::office_id office,
	dcon::person_id incumbent, sys::date date) {
	if(!office || !state.world.office_is_valid(office)) return true;
	for(auto target : office_targets(state, office))
		if(vetoes_node(state, target, date)) return true;
	auto incumbent_actor = persons::actor_for_person(state, incumbent);
	return incumbent_actor && vetoes_node(state, actor_node(incumbent_actor), date);
}

bool confirmation_vote_eligible(sys::state const& state, dcon::office_id office,
	dcon::institution_id chamber, dcon::person_id voter, sys::date date) {
	if(!office || !state.world.office_is_valid(office) || !chamber || !voter) return false;
	std::vector<node> targets{ office_node(office) };
	if(auto institution = institution_for_office(state, office)) {
		if(auto actor = actor_for_institution(state, institution)) targets.push_back(actor_node(actor));
	}
	std::vector<node> confirmers;
	for(auto target : targets)
		for(auto const& edge : to(state, target, date))
			if(active_positive(edge) && edge.kind == relation_kind::confirms)
				confirmers.push_back(edge.source);
	if(confirmers.empty()) return true;
	auto chamber_actor = actor_for_institution(state, chamber);
	for(auto source : confirmers)
		if(source.actor == chamber_actor || controlled_by(state, voter, source, date)) return true;
	return false;
}

dcon::person_id nominated_member(sys::state const& state, node target,
	std::vector<dcon::person_id> const& candidates, sys::date date) {
	if(!target || vetoes_node(state, target, date)) return {};
	std::vector<node> nominators;
	for(auto const& edge : to(state, target, date))
		if(active_positive(edge) && edge.kind == relation_kind::nominates_cadres)
			nominators.push_back(edge.source);
	for(auto candidate : candidates) {
		if(!candidate || !persons::alive(state, candidate)
			|| !persons::born_on_or_before(state, candidate, date)) continue;
		auto actor = persons::actor_for_person(state, candidate);
		if(!actor || vetoes_node(state, actor_node(actor), date)) continue;
		if(nominators.empty()) return candidate;
		for(auto nominator : nominators)
			if(controlled_by(state, candidate, nominator, date)) return candidate;
	}
	return {};
}

bool command_chain_allows(sys::state const& state, dcon::office_id exercising_office,
	dcon::person_id commander, dcon::nation_id nation, sys::date date) {
	if(!exercising_office || !commander || !nation) return false;
	auto defense = find_institution(state, nation, institution_kind::military_command);
	std::vector<relationship> command_edges;
	if(defense) {
		auto defense_actor = actor_for_institution(state, defense);
		if(defense_actor) {
			for(auto const& edge : to(state, actor_node(defense_actor), date))
				if(active_positive(edge) && edge.kind == relation_kind::commands)
					command_edges.push_back(edge);
		}
	}
	for(auto const& edge : to(state, office_node(exercising_office), date))
		if(active_positive(edge) && edge.kind == relation_kind::commands)
			command_edges.push_back(edge);
	if(command_edges.empty()) return true;
	auto commander_actor = persons::actor_for_person(state, commander);
	for(auto const& edge : command_edges) {
		if(edge.source.actor == commander_actor || controlled_by(state, commander, edge.source, date)) return true;
	}
	return false;
}

bool management_authorized(sys::state const& state, dcon::person_id person,
	dcon::economic_actor_id target, sys::date date) {
	if(!person || !target) return false;
	auto person_actor = persons::actor_for_person(state, person);
	if(!person_actor) return false;
	for(auto const& edge : to(state, actor_node(target), date)) {
		if(!active_positive(edge) || !nomination_relation(edge.kind)) continue;
		if(edge.source.actor == person_actor || controlled_by(state, person, edge.source, date)) return true;
	}
	return false;
}

bool voting_authorized(sys::state const& state, dcon::person_id person,
	dcon::economic_actor_id target, sys::date date) {
	if(!person || !target) return false;
	auto person_actor = persons::actor_for_person(state, person);
	if(!person_actor) return false;
	for(auto const& edge : to(state, actor_node(target), date)) {
		if(!active_positive(edge) || edge.kind != relation_kind::controls_voting) continue;
		if(edge.source.actor == person_actor || controlled_by(state, person, edge.source, date)) return true;
	}
	return false;
}

} // namespace governance::power_topology
