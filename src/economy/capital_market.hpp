#pragma once

#include "dcon_generated.hpp"
#include "economy/wallets.hpp"

#include <cstdint>
#include <vector>

namespace sys { class state; }

namespace economy::capital_market {

// Equity is funded only by investors who choose to buy it. An investor commits
// part of its free savings to an offering whose expected earnings yield at the
// offering price beats the return it requires; nobody is called for capital.

// Return equity must earn above the lending rate of the investor's bank.
inline constexpr float equity_risk_premium = 0.05f;
// Required return of an investor with no bank to compare with.
inline constexpr float default_required_return = 0.10f;
// Share of its free savings an investor keeps liquid instead of in equity.
inline constexpr float liquidity_preference = 0.5f;
// No single offering takes more than this share of an investor's investable funds.
inline constexpr float concentration_limit = 0.25f;

struct investor {
	dcon::economic_actor_id actor{};
	dcon::nation_id nation{};
	dcon::commodity_id settlement{};
	float investable = 0.0f;
	float required_return = 0.0f;
};

// The investors of one day, in a stable order. Commitments reduce what each has
// left to invest, so one pool serves every offering of the day.
struct pool {
	std::vector<investor> investors;
};

// The settlement an actor keeps its money in: its operating account's, a
// person's exact account's, or its deposit's.
dcon::commodity_id settlement_of(sys::state const&, dcon::economic_actor_id);
// The nation whose capital market an actor uses: its home or operating
// province's owner, else the jurisdiction of its bank.
dcon::nation_id nation_of(sys::state const&, dcon::economic_actor_id, dcon::commodity_id settlement);
// The annual return an actor requires from equity: its bank's lending rate
// plus the equity premium.
float required_return(sys::state const&, dcon::economic_actor_id, dcon::commodity_id settlement);
// Money the actor can move today: free wallet cash plus its free deposit.
float liquid_funds(sys::state const&, dcon::economic_actor_id, dcon::commodity_id settlement);
// What the actor would commit to equity now. A person or household commits
// part of its bank savings and keeps its wallet as working cash; a firm commits
// part of its liquid money above its operating reserve. Banks and governments
// commit nothing.
float investable_funds(sys::state const&, dcon::economic_actor_id, dcon::commodity_id settlement);
// Pays from the actor's deposit first, then its wallet. Fails without change
// when the two together cannot cover the amount.
bool pay(sys::state&, dcon::economic_actor_id payer, dcon::commodity_id settlement,
	wallets::account_ref destination, float amount, relations::transaction_kind);
pool build_pool(sys::state const&);
// Takes `amount` off an investor's remaining capacity in the pool.
void commit(pool&, dcon::economic_actor_id, dcon::commodity_id settlement, float amount);

// Expected daily profit of an operating plant: half observed, half modelled
// from reference prices, expected sell-through, materials and wages.
float expected_daily_profit(sys::state const&, dcon::factory_id);
// Expected annual earnings of an organization's operating plants.
float expected_annual_earnings(sys::state const&, dcon::organization_id);
// Paid-in capital plus retained earnings, never negative.
float book_value(sys::state const&, dcon::organization_id);

struct offering {
	dcon::organization_id issuer{};
	dcon::nation_id nation{};
	dcon::commodity_id settlement{};
	// Money sought.
	float amount = 0.0f;
	// Value of the existing shares: new shares are priced against it.
	float pre_money = 0.0f;
	// Expected annual earnings of the issuer once the money is invested.
	float annual_earnings = 0.0f;
};
struct subscription {
	uint32_t investor = 0;
	float amount = 0.0f;
};
// Annual earnings per unit of money invested at the offering price.
float earnings_yield(offering const&);
// Every investor of the issuer's nation and settlement whose required return the
// yield meets offers up to its concentration limit; the offering takes what it
// seeks pro rata. Plans only: nothing changes, and the issuer may still be
// unfounded.
std::vector<subscription> subscriptions(sys::state const&, pool const&, offering const&);
float total(std::vector<subscription> const&);
// Each subscriber pays the issuer and receives new shares at the offering
// price; existing holders are diluted. Returns the amount raised.
float execute(sys::state&, pool&, offering const&, std::vector<subscription> const&);
// Plans and executes: raises nothing unless at least `minimum` subscribes.
float raise(sys::state&, pool&, offering const&, float minimum = 0.0f);

} // namespace economy::capital_market
