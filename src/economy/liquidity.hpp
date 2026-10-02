#pragma once

#include "dcon_generated.hpp"

namespace sys { class state; }

namespace economy::liquidity {

// Actors hold liquid money as wallet cash (base money) and bank deposits.
// Each day an actor with a deposit keeps its wallet near a target share of its
// free liquid money, depositing the excess and withdrawing the shortfall.
inline constexpr float cash_share_target = 0.3f;
inline constexpr float cash_share_floor = 0.2f;
inline constexpr float cash_share_ceiling = 0.4f;
// An actor without a deposit opens one once its idle wallet cash exceeds this.
inline constexpr float minimum_idle_cash = 10.0f;

// A person without a profile who holds more than this in cash is a saver: they
// gain a profile, so this policy banks their savings and they can invest them.
inline constexpr float saver_cash_threshold = 50.0f;

// The nation whose banks an actor uses: its home or operating province's owner.
dcon::nation_id nation_for(sys::state const&, dcon::economic_actor_id);
// The solvent bank of a nation, in a settlement, with the most liquidity.
dcon::organization_id bank_for(sys::state const&, dcon::nation_id, dcon::commodity_id settlement);
// Rebalances one actor in one settlement. A depositor of a bank that is not
// solvent withdraws everything the bank can pay: runs follow from balances.
void manage(sys::state&, dcon::economic_actor_id, dcon::commodity_id settlement);
void recognize_savers(sys::state&);
// Recognizes new savers, then rebalances every actor except banks and treasuries.
void process(sys::state&);

} // namespace economy::liquidity
