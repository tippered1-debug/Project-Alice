#pragma once

#include "dcon_generated.hpp"

namespace sys { class state; }

namespace world::spatial {

glm::vec2 settlement_position(sys::state const&, dcon::settlement_id);
glm::vec2 site_position(sys::state const&, dcon::site_id);
glm::vec2 infrastructure_node_position(sys::state const&, dcon::infrastructure_node_id);

}
