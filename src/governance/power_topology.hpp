#pragma once

#include "dcon_generated.hpp"
#include "date_interface.hpp"

#include <string_view>
#include <vector>

namespace sys { class state; }

namespace governance::power_topology {

// Effective relationships between political and economic actors. These are
// descriptive social or organizational ties: they do not create constitutional
// authority, property title, or money transfers.
enum class relation_kind : uint8_t {
	appoints,
	confirms,
	dismisses,
	commands,
	supervises,
	vetoes,
	controls_voting,
	controls_management,
	funds,
	funding_dependence,
	embeds_organization,
	nominates_cadres,
	sets_strategic_direction,
	political_influence,
	member_of
};

// A graph node is either an economic actor (which covers organizations,
// institutions, and individually represented people) or a constitutional
// office. Exactly one endpoint must be set.
struct node {
	dcon::economic_actor_id actor{};
	dcon::office_id office{};
	explicit operator bool() const noexcept { return bool(actor) != bool(office); }
	friend bool operator==(node const&, node const&) = default;
};

inline node actor_node(dcon::economic_actor_id actor) { return { actor, {} }; }
inline node office_node(dcon::office_id office) { return { {}, office }; }

struct terms {
	sys::date valid_from{};
	sys::date valid_until{}; // exclusive; empty means no end
	float intensity = 1.0f;
};

struct relationship {
	dcon::power_relationship_id id{};
	node source{};
	relation_kind kind = relation_kind::appoints;
	node target{};
	float intensity = 1.0f;
	sys::date valid_from{};
	sys::date valid_until{};
};

bool parse_relation_kind(std::string_view, relation_kind&);
std::string_view name_of(relation_kind);

// Adds a dated edge to the saved power graph. Exact duplicate edges are
// rejected. The interval is [valid_from, valid_until); either date may be
// empty to leave that bound open.
dcon::power_relationship_id create(sys::state&, node source, relation_kind,
	node target, terms const& = {});

// Ends a relationship from `date`, preserving its historical record.
bool revoke(sys::state&, dcon::power_relationship_id, sys::date);
bool valid_at(sys::state const&, dcon::power_relationship_id, sys::date);

// Active incoming and outgoing ties, ordered by relationship ID.
std::vector<relationship> from(sys::state const&, node, sys::date);
std::vector<relationship> to(sys::state const&, node, sys::date);

} // namespace governance::power_topology
