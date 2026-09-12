#include "spatial.hpp"
#include "system_state.hpp"

namespace world::spatial {

glm::vec2 settlement_position(sys::state const& state, dcon::settlement_id id) {
	return state.world.settlement_get_position(id);
}

glm::vec2 site_position(sys::state const& state, dcon::site_id id) {
	return state.world.site_get_position(id);
}

glm::vec2 infrastructure_node_position(sys::state const& state, dcon::infrastructure_node_id id) {
	return state.world.infrastructure_node_get_position(id);
}

}
