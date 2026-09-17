#pragma once

#include "dcon_generated.hpp"

namespace sys { class state; }

namespace economy::physical::freight_market {

enum class carrier_status : uint8_t { active = 0, inactive = 1 };
enum class freight_request_status : uint8_t { pending = 0, contracted = 1, fulfilled = 2, canceled = 3 };
enum class freight_offer_status : uint8_t { active = 0, inactive = 1 };
enum class freight_contract_status : uint8_t { accepted = 0, fulfilled = 1, canceled = 2 };

constexpr uint8_t mode_bit(uint8_t mode) noexcept { return uint8_t(1u << mode); }

dcon::carrier_id create_carrier(sys::state&, dcon::economic_actor_id,
	dcon::monetary_account_id, float service_capacity, uint8_t mode_mask,
	dcon::market_id market_presence = {});
dcon::freight_offer_id create_offer(sys::state&, dcon::carrier_id,
	dcon::market_id origin_market, dcon::market_id destination_market,
	uint8_t mode_mask, float service_capacity, float base_handling_charge,
	float cargo_distance_rate);
dcon::freight_request_id create_request(sys::state&, dcon::economic_actor_id,
	dcon::monetary_account_id, dcon::site_id source, dcon::site_id destination,
	dcon::commodity_id, float quantity);
dcon::freight_contract_id match_request(sys::state&, dcon::freight_request_id);
void complete_contract_for_shipment(sys::state&, dcon::shipment_id);

} // namespace economy::physical::freight_market
