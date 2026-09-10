#pragma once

#include "dcon_generated_ids.hpp"
#include "system_state_forward.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace economy::market_clearing {

// Demand is aggregated by economic purpose, not by individual POP or firm.
// This retains the information needed for realistic rationing while keeping
// the daily auction O(markets * commodities) rather than O(agents).
enum class demand_class : uint8_t {
	life_needs,
	everyday_needs,
	luxury_needs,
	intermediate,
	government,
	construction,
	inventory,
	trade,
	other,
	count
};

inline constexpr size_t demand_class_count =
	static_cast<size_t>(demand_class::count);

struct bid_order {
	float quantity = 0.0f;
	float limit_price = 0.0f;
	demand_class category = demand_class::other;
	uint32_t stable_order = 0;
};

struct ask_order {
	float quantity = 0.0f;
	float limit_price = 0.0f;
	uint32_t stable_order = 0;
};

struct auction_result {
	float quantity_traded = 0.0f;
	float clearing_price = 0.0f;
	float quantity_offered = 0.0f;
	float quantity_requested = 0.0f;
	std::array<float, demand_class_count> bought{};
	std::array<float, demand_class_count> fill{};
};

// Deterministic uniform-price call auction. Invalid and negative values are
// treated as zero. Equal-price orders retain stable_order ordering.
auction_result clear_call_auction(
	std::vector<bid_order> const& bids,
	std::vector<ask_order> const& asks,
	float fallback_price = 0.0f);

struct market_result {
	float quantity_traded = 0.0f;
	float aggregate_buy_fill = 0.0f;
	float aggregate_sell_fill = 0.0f;
	float clearing_price = 0.0f;
	std::array<float, demand_class_count> class_fill{};
};

// Unsaved daily ledger. It is fully rebuilt after demand is reset and therefore
// remains deterministic across save/load without changing the save format.
struct account {
	bool enabled = false;
	uint32_t market_count = 0;
	uint32_t commodity_count = 0;
	std::array<std::vector<float>, demand_class_count> demand;
	std::array<std::vector<float>, demand_class_count> fill;
	std::vector<float> clearing_price;
	std::vector<float> quantity_traded;

	void reset(uint32_t markets, uint32_t commodities, bool active);
	void record(dcon::market_id market, dcon::commodity_id commodity,
		demand_class category, float amount) noexcept;
	float recorded(dcon::market_id market, dcon::commodity_id commodity,
		demand_class category) const noexcept;
	float filled(dcon::market_id market, dcon::commodity_id commodity,
		demand_class category, float fallback) const noexcept;
};

void begin_day(sys::state& state);
void record(sys::state& state, dcon::market_id market,
	dcon::commodity_id commodity, demand_class category, float amount) noexcept;

// Reconciles the classified ledger with the authoritative aggregate demand and
// clears one commodity. In classic mode this returns the legacy proportional
// allocation exactly.
market_result settle(sys::state& state, dcon::market_id market,
	dcon::commodity_id commodity, float supply, float aggregate_demand,
	float reference_price);

float fill(sys::state const& state, dcon::market_id market,
	dcon::commodity_id commodity, demand_class category) noexcept;
float clearing_price(sys::state const& state, dcon::market_id market,
	dcon::commodity_id commodity, float fallback) noexcept;

} // namespace economy::market_clearing
