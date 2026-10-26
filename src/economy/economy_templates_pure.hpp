#pragma once

#include "container_types.hpp"

namespace economy {

template<typename X, typename FROM, typename TO>
using convert_value_type = typename std::conditional_t<
	ve::is_vector_type_s<X>::value,
	typename ve::tagged_vector<TO>,
	TO
>;

template<typename TRADE_ROUTE>
struct trade_route_data {
	using VALUE = typename std::conditional_t<ve::is_vector_type_s<TRADE_ROUTE>::value, ve::fp_vector, float>;
	VALUE distance;
	VALUE loss;
	VALUE base_distance_cost;
	VALUE workers_satisfaction;
	VALUE effect_of_scale;
	VALUE distance_cost_scaled;
};

template<typename TRADE_ROUTE>
struct trade_route_explanation {
	using VALUE = typename std::conditional_t<ve::is_vector_type_s<TRADE_ROUTE>::value, ve::fp_vector, float>;
	using MARKET = convert_value_type<TRADE_ROUTE, dcon::trade_route_id, dcon::market_id>;
	using NATION = convert_value_type<TRADE_ROUTE, dcon::trade_route_id, dcon::nation_id>;

	MARKET origin;
	MARKET target;

	NATION origin_nation;
	NATION target_nation;

	VALUE amount_origin;
	VALUE amount_target;

	VALUE price_origin;
	VALUE price_target;

	VALUE transport_cost;
	VALUE transportaion_loss;
	VALUE distance;

	VALUE payment_per_unit;
	VALUE payment_received_per_unit;
};


}
