#pragma once

#include "spatial.hpp"
#include <vector>

namespace world::infrastructure {

std::vector<dcon::infrastructure_edge_id> incident_edges(sys::state const&, dcon::infrastructure_node_id);

}
