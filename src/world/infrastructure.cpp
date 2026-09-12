#include "infrastructure.hpp"
#include "system_state.hpp"

#include <algorithm>

namespace world::infrastructure {

std::vector<dcon::infrastructure_edge_id> incident_edges(
	sys::state const& state, dcon::infrastructure_node_id node) {
	std::vector<dcon::infrastructure_edge_id> result;
	state.world.infrastructure_node_for_each_infrastructure_edge_from_as_node(node, [&](dcon::infrastructure_edge_from_id relation) {
		result.push_back(state.world.infrastructure_edge_from_get_infrastructure_edge(relation));
	});
	state.world.infrastructure_node_for_each_infrastructure_edge_to_as_node(node, [&](dcon::infrastructure_edge_to_id relation) {
		auto edge = state.world.infrastructure_edge_to_get_infrastructure_edge(relation);
		if(std::find(result.begin(), result.end(), edge) == result.end())
			result.push_back(edge);
	});
	return result;
}

}
