#pragma once

#include "dcon_generated.hpp"
#include "economy/wallets.hpp"

namespace sys { class state; }

namespace economy::dividends {

// Firm and bank profit reaches its owners as cash. Each organization with an
// equity asset reviews its payout once per period (staggered by organization).
inline constexpr int32_t payout_period_days = 30;
// Share of positive retained earnings paid out per review.
inline constexpr float payout_ratio = 0.5f;
// A firm keeps this many days of recent operating costs plus unpaid wages.
inline constexpr float cash_buffer_days = 90.0f;
// A bank keeps its capital ratio this far above its regulatory minimum.
inline constexpr float bank_capital_buffer = 0.02f;

// The settlement and account an organization pays dividends from: a bank's
// reserve account, otherwise its first operating account.
wallets::account_ref payout_account(sys::state const&, dcon::organization_id);
dcon::commodity_id payout_settlement(sys::state const&, dcon::organization_id);
// An organization with no operations, projects, or debts left returns all of
// its cash to its owners.
bool winding_up(sys::state const&, dcon::organization_id);
// The amount the policy allows this organization to pay now.
float payable(sys::state const&, dcon::organization_id);
// Pays `amount` from the organization's payout account to its equity owners in
// proportion to their economic fractions. Returns the amount actually paid.
float distribute(sys::state&, dcon::organization_id, float amount);
// Reviews one organization now: pays what the policy allows and books it
// against retained earnings (and, when winding up, paid-in equity).
float pay(sys::state&, dcon::organization_id);
void process(sys::state&);

} // namespace economy::dividends
