#include "power_topology.hpp"

#include "system_state.hpp"
#include "governance/governance.hpp"

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

} // namespace governance::power_topology
