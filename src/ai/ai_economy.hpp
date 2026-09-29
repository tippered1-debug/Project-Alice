#pragma once
#include "dcon_generated_ids.hpp"

namespace sys {
struct state;
}

namespace ai {

void get_craved_factory_types(sys::state& state, dcon::nation_id nid, dcon::market_id mid, dcon::province_id, std::vector<dcon::factory_type_id>& desired_types, bool pop_project);
void get_desired_factory_types(sys::state& state, dcon::nation_id nid, dcon::market_id mid, dcon::province_id, std::vector<dcon::factory_type_id>& desired_types, bool pop_project);
}
