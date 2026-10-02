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

// The price at which a plant's operator offers it, or zero when it is not for
// sale. Only bankrupt plants and divesting distressed operators sell.
float asking_price_for(sys::state const&, dcon::factory_id);

// Runs insolvency resolution (voluntary recapitalization, restructuring,
// bankruptcy auction, closure), plant sales and greenfield entry.
void process(sys::state&);

} // namespace economy::industrial_dynamics
