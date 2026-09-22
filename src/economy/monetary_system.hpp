#pragma once

#include "dcon_generated_ids.hpp"
#include "system_state_forward.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace economy::monetary {

// Money accounting. This module measures the world's money supply and reports
// what cannot be explained; it deliberately does not correct anything.
//
// An earlier version renormalized the supply to hide the residual. Measurement
// on a real scenario showed why that was the wrong instrument: producer tills
// overdraw without limit, household savings are their mirror image, and the net
// supply becomes a rounding error between two multi-million positions. Scaling
// that residue moves gross balances by orders of magnitude more than the
// residual it corrects. The defect is unbounded producer credit, and it belongs
// to the credit market, not to a monetary valve.
//
// The stock list is exhaustive by construction: it is every `tag{save}` money
// float in dcon_generated.txt. Producers keep their own tills (province
// rgo_bank, factory_bank, artisan_bank) and advanced province buildings keep
// private savings. Leaving those out makes a transfer into a till read as money
// vanishing from the world.
//
// market_cash and producer_banks are signed. Merchants and firms may run a net
// overdraft, so a net position is the only honest way to aggregate them.
struct stocks {
	double pop_savings = 0.0;
	double market_cash = 0.0;
	double treasury = 0.0;
	double national_bank = 0.0;
	double private_investment = 0.0;
	// province rgo_bank + factory_bank + artisan_bank
	double producer_banks = 0.0;
	// advanced_province_building_private_savings, summed over building types
	double building_savings = 0.0;

	[[nodiscard]] constexpr double total() const noexcept {
		return pop_savings + market_cash + treasury + national_bank
			+ private_investment + producer_banks + building_savings;
	}

	// Sum of absolute positions. The ratio of total() to gross() says how much
	// of the world's cash is real versus offsetting claims.
	[[nodiscard]] double gross() const noexcept;
};

// The per-day accounting question: given yesterday's money supply and the gold
// actually minted into treasuries today, how much of today's supply is
// unexplained?
struct balance_inputs {
	bool has_previous = false;
	double previous_total = 0.0;
	double observed_total = 0.0;
	// Actual emission recorded by update_rgo_profit, not an estimate.
	double gold_emission = 0.0;
	double gross_total = 0.0;
};

struct balance {
	bool has_previous = false;
	double previous_total = 0.0;
	double observed_total = 0.0;
	double gold_emission = 0.0;
	// previous_total + gold_emission: what the supply should have been.
	double expected_total = 0.0;
	// observed_total - expected_total. Positive means money appeared from
	// nowhere, negative that it went missing.
	double unaccounted = 0.0;
	double gross_total = 0.0;
	// observed_total / gross_total. One in a world of purely positive balances;
	// near zero once household savings are financed by an equal and opposite
	// producer overdraft.
	double net_to_gross = 1.0;
};

// Runtime-only money account. It is derived from serialized stocks and is
// rebuilt by initialize() after a load rather than saved.
struct account {
	bool has_previous = false;
	stocks current{};
	double previous_total = 0.0;
	// Accumulated during the day by the gold RGO payout, reset by begin_day().
	double gold_emission_today = 0.0;
	balance last_balance{};
};

// The blanket decay applied to POP savings and market cash in classic games.
// It is not a monetary policy. The transformation ruleset uses a factor of one
// and measures inflation from consumer prices instead.
inline constexpr float legacy_inflation = 0.999f;

[[nodiscard]] balance calculate(balance_inputs raw_inputs);

[[nodiscard]] stocks measure(sys::state const& state);

// Compatibility repair for saves produced by the former unbounded labor
// feedback loop. Returns the redenomination factor (1 when no repair was
// needed) and scales every serialized money stock by the same amount.
[[nodiscard]] double repair_runaway_nominal_stocks(sys::state& state);

// Called at the start of economy::daily_update.
void begin_day(sys::state& state);

// Recorded by the gold RGO payout so the ledger uses the emission that actually
// happened rather than an independent estimate of it.
void record_gold_emission(sys::state& state, float amount);

// Called at the end of economy::daily_update. Closes the day's books. Changes
// no balance in any mode.
void update(sys::state& state);

// Rebuilds the unsaved account from serialized stocks. A save is taken on a day
// boundary, so the measured supply at load time is exactly the previous total a
// continuous run would have held.
void initialize(sys::state& state);

// Per-phase money audit. When enabled, the day's phases each report how much
// money appeared or vanished inside them, which attributes creation to a named
// part of the update instead of leaving it as a daily aggregate. Off by default;
// it costs a full pass over every stock at every phase boundary.
struct phase_delta {
	// Owned, not a view: the caller's phase name is a temporary.
	std::string phase;
	double before = 0.0;
	double after = 0.0;

	[[nodiscard]] constexpr double change() const noexcept { return after - before; }
};

struct audit {
	bool enabled = false;
	double running_total = 0.0;
	std::vector<phase_delta> phases;
};

void audit_phase(sys::state& state, std::string_view phase);

// Formats the last completed day as one line per phase that moved money.
[[nodiscard]] std::string format_audit(sys::state const& state, double threshold = 0.5);

} // namespace economy::monetary
