#pragma once

#include "dcon_generated_ids.hpp"
#include "system_state_forward.hpp"
#include "economy_stats.hpp"
#include "adaptive_ve.hpp"
#include "economy_templates_pure.hpp"
#include "world_trade_capacity.hpp"

namespace sys {
struct state;
}

namespace economy {
float estimate_port_service_price(sys::state const& state, dcon::state_instance_id s);

float transportation_between_markets_labor_demand(sys::state& state, dcon::market_id market);
float transportation_inside_market_labor_demand(sys::state& state, dcon::market_id market, dcon::province_id capital);

struct embargo_explanation {
	bool combined = false;
	bool war = false;
	bool origin_embargo = false;
	bool target_embargo = false;
	bool origin_join_embargo = false;
	bool target_join_embargo = false;
};

embargo_explanation embargo_exists(
	sys::state& state, dcon::nation_id n_A, dcon::nation_id n_B
);

trade_route_explanation<dcon::trade_route_id> explain_trade_route_commodity(sys::state const& state, dcon::trade_route_id trade_route, dcon::commodity_id cid);
trade_route_explanation<dcon::trade_route_id> explain_trade_route_commodity(
	sys::state const& state,
	dcon::trade_route_id trade_route,
	dcon::commodity_id cid,
	world_trade::shipment_allocation const& allocation
);
trade_route_explanation<ve::contiguous_tags<dcon::trade_route_id>> explain_trade_route_commodity(
	sys::state const& state,
	ve::contiguous_tags<dcon::trade_route_id> trade_route,
	trade_route_data<ve::contiguous_tags<dcon::trade_route_id>>& additional_data,
	dcon::commodity_id cid
);
trade_route_explanation<ve::partial_contiguous_tags<dcon::trade_route_id>> explain_trade_route_commodity(
	sys::state const& state,
	ve::partial_contiguous_tags<dcon::trade_route_id> trade_route,
	trade_route_data<ve::partial_contiguous_tags<dcon::trade_route_id>>& additional_data,
	dcon::commodity_id cid
);


}
