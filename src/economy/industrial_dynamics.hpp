#pragma once

#include "dcon_generated.hpp"

#include <cstdint>

namespace sys { class state; }

namespace economy::industrial_dynamics {

// Each sponsor (company, fund, cooperative, or person) reviews its investments
// once per period; first reviews are spread over the period.
inline constexpr int32_t investor_review_days = 30;
// A bankrupt plant is auctioned for this long before it is closed.
inline constexpr int32_t bankruptcy_sale_window_days = 120;
// The auction opens at this share of the plant's replacement value and falls
// to the floor share by the end of the window. A distressed operator without
// defaulted debt divests at the opening share.
inline constexpr float auction_opening_share = 0.6f;
inline constexpr float auction_floor_share = 0.2f;
inline constexpr uint16_t divestment_distress_days = 90;
// A person founding a company puts in at least this share of its capital.
inline constexpr float founder_minimum_share = 0.2f;
// A project goes ahead, scaled down, once this share of its capital is raised.
inline constexpr float minimum_funded_share = 0.5f;

// Share of a built plant's capacity that wears out per year.
inline constexpr float annual_depreciation_rate = 0.05f;

// Entry is ended by competition. Demand is the daily quantity buyers bid for
// over this window; supply is the daily output of standing plants and of
// projects being built.
inline constexpr int32_t demand_window_days = 30;
// How strongly the price an entrant expects falls as capacity outgrows demand.
inline constexpr float demand_elasticity = 1.5f;
inline constexpr float maximum_entry_sell_through = 0.95f;
struct entry_terms {
	// Share of its output an entrant expects to sell.
	float sell_through = 0.0f;
	// Expected price relative to today's reference price.
	float price_factor = 0.0f;
};
// What an entrant adding `added` daily output can expect where buyers want
// `demand` and plants (standing and being built) supply `supply`: it sells the
// demand-to-capacity ratio, and the price scales with that ratio to the power
// 1 / elasticity, between half and 1.25 times today's. No demand, no sales.
entry_terms expected_entry(float demand, float supply, float added);

// The price at which a plant's operator offers it, or zero when it is not for
// sale. Only bankrupt plants and divesting distressed operators sell.
float asking_price_for(sys::state const&, dcon::factory_id);

// Runs depreciation, insolvency resolution (voluntary recapitalization, restructuring,
// bankruptcy auction, closure), plant sales, entry of new plants and mines, and
// urban cohorts taking up new crafts.
void process(sys::state&);

} // namespace economy::industrial_dynamics
