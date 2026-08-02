#pragma once

#include "dcon_generated_ids.hpp"
#include "system_state_forward.hpp"

#include <cstdint>
#include <vector>

namespace economy::credit {

// The banking stock used to be a sink: it accrued government interest and POP
// deposits, paid out a fixed dividend trickle, and never financed anything. This
// module turns it into a lender, and turns the loan rate into a price that
// clears loanable funds against the demand for them.
//
// It owns no state. Every input is already serialized, so a rate derived here is
// identical in a continuous run and in a resumed save.
struct inputs {
	bool enabled = false;
	// max(0.01, (loan_interest modifier + 1) * define:LOAN_BASE_INTEREST).
	float base_annual_rate = 0.f;
	// national_bank holds both outstanding loans and funds still free to lend.
	float banking_reserves = 0.f;
	float government_debt = 0.f;
	float private_investment = 0.f;
	// Outstanding producer loan book. It is a committed bank asset even when
	// only part of the original negative till received live cash.
	float producer_debt = 0.f;
	// Bounded [0, 1], one being healthy. Supplied by banking_stability.
	float credit_health = 1.f;
	// Private construction that the investment pool cannot fund today.
	float private_credit_shortfall = 0.f;
};

struct market {
	bool enabled = false;
	// Share of the bank's stock already committed, government, private and
	// producer credit.
	float utilization = 0.f;
	// Share of that utilization owed to the government: the crowding-out signal.
	float government_share = 0.f;
	float policy_annual_rate = 0.f;
	// policy_annual_rate / base_annual_rate. Exactly one in classic games, so
	// interest_payment keeps its legacy value without a special case.
	float interest_cost_multiplier = 1.f;
	// Reserves the bank may still deploy today.
	float lending_capacity = 0.f;
	// Actually extended to the private investment pool today.
	float private_credit_extended = 0.f;
	// Paid by the pool back to the bank today.
	float private_interest_due = 0.f;
};

// A bank that lends every last coin cannot honour a government drawdown, so a
// fixed share of its stock is never lent out.
inline constexpr float reserve_requirement = 0.20f;
// Free reserves are deployed gradually rather than in a single day.
inline constexpr float maximum_daily_lending_share = 0.05f;
// How sharply the rate rises as loanable funds are exhausted.
inline constexpr float rate_sensitivity = 3.0f;
// Utilization above this point carries no additional price signal; the credit
// limit, not the rate, is what stops borrowing there.
inline constexpr float utilization_cap = 2.0f;
// Matches the premium banking_stability used to apply on its own, so enabling
// this module extends that hook instead of stacking a second one on top of it.
inline constexpr float maximum_risk_premium = 0.50f;
inline constexpr float maximum_rate_multiplier = 8.0f;

// Credit actually settled today, per nation. The rate, utilization and lending
// capacity can be re-derived at any time, but the flows depend on a demand
// figure that only exists inside the daily update, so they are recorded when
// they happen. Each nation writes its own slot, which keeps the surrounding
// parallel_for race-free and the summed telemetry deterministic.
struct daily_flows {
	std::vector<float> extended;
	std::vector<float> interest;
	// Producer till financing, recorded by settle_producer_credit.
	std::vector<float> producer_extended;
	std::vector<float> producer_unfunded;
	// Employment multiplier derived from the unfunded share, so a firm that
	// could not finance its losses actually has to shrink.
	std::vector<float> producer_availability;
	// Debt service settled today, for observability.
	std::vector<float> producer_interest_paid;
	std::vector<float> producer_principal_repaid;
	// Producer debt written off because the secured industry disappeared or
	// the outstanding claim exceeded the province's realizable value.
	std::vector<float> producer_writeoff;

	void reset(uint32_t nation_count);
	void record(dcon::nation_id nation, float extended_amount, float interest_amount);
	void record_producer(dcon::nation_id nation, float extended_amount, float unfunded_amount,
		float availability);
	void record_debt_service(dcon::nation_id nation, float interest, float principal);
	void record_writeoff(dcon::nation_id nation, float amount);
};

// Producers keep their own tills and, in the base game, may overdraw them
// without any limit: a firm pays wages and buys inputs from a balance that
// simply goes more negative, forever, at no cost. That is unbounded
// interest-free credit and it is the largest single distortion in the model.
//
// This routes the shortfall through the bank instead. What the bank can fund is
// a real transfer, so it conserves money and is constrained by reserves. What
// it cannot fund is reported as an unfunded deficit rather than silently
// financed.
struct producer_financing {
	float requested = 0.f;
	float extended = 0.f;
	float unfunded = 0.f;
	float interest = 0.f;
};

[[nodiscard]] producer_financing finance_producers(market const& priced, float shortfall);

// Servicing an existing loan book. Credit that is only a flow has no memory:
// firms borrow, pay a day's interest, and nothing accumulates, so there is no
// debt overhang, no deleveraging and no credit cycle. The debt stock gives the
// borrowing a life beyond the day it happened.
struct debt_service {
	// Interest accrued on the outstanding stock for one day.
	float interest = 0.f;
	// Principal the borrower could actually repay out of a positive till.
	float repayment = 0.f;
	// Interest the borrower could not pay, which capitalizes into the stock.
	float capitalized = 0.f;
	float closing_debt = 0.f;
};

// A firm repays out of a till that is in surplus, never out of one already
// overdrawn. Unpaid interest is added to the principal, which is what makes a
// debt spiral possible.
[[nodiscard]] debt_service service_debt(float outstanding_debt, float available_cash,
	float annual_rate);

// A negative till is observed repeatedly until it recovers. Keep the loan
// book at least as large as that till without counting the same deficit twice.
[[nodiscard]] float producer_debt_after_shortfall(float outstanding_debt,
	float shortfall);

[[nodiscard]] bool producer_debt_is_unrecoverable(float outstanding_debt,
	float industry_value, bool has_industry, bool valuation_ready);

// Fraction of the daily rate charged as amortization on top of interest.
inline constexpr float principal_repayment_rate = 0.004f;

// A firm that cannot finance its losses has to shrink; that is what makes the
// till a budget constraint rather than an accounting line. The contraction is
// capped per day so a credit squeeze is a squeeze and not an instant collapse.
inline constexpr float maximum_daily_employment_contraction = 0.05f;

// A producer loan is secured by the industrial asset. Once the claim is more
// than three times the asset's realizable value, continuing to capitalize it
// is not lending anymore: it is a bad loan that needs to be written off.
inline constexpr float producer_bankruptcy_debt_to_value = 3.0f;

// Returns the employment multiplier for one day. One when the deficit was fully
// financed, or when there was no deficit at all.
[[nodiscard]] float employment_availability(float unfunded, float requested);

// Employment multiplier for a nation's producers, for the current day. Exactly
// one unless Alice: Age of Transformation is enabled.
[[nodiscard]] float producer_employment_scale(sys::state const& state, dcon::nation_id nation);

[[nodiscard]] market calculate(inputs raw_inputs);

// Project-state adapter. Returns a disabled, exactly-legacy market unless
// Alice: Age of Transformation is enabled.
[[nodiscard]] market evaluate_nation(sys::state const& state, dcon::nation_id nation,
	float private_credit_shortfall = 0.f);

// Moves the day's credit between the bank and the investment pool. Both stocks
// already exist, so this is a transfer: it creates and destroys no money.
void settle_nation(sys::state& state, dcon::nation_id nation, market const& result);

// Accrues interest on the outstanding producer loan book and takes repayment
// from tills that are in surplus. Runs before new lending, so a firm services
// what it already owes before borrowing more.
void service_producer_debt(sys::state& state);

// Funds overdrawn producer tills from the bank, pro rata across the provinces
// that are short. Runs serially over nations so the distribution is
// deterministic. An exact no-op unless Alice: Age of Transformation is enabled.
void settle_producer_credit(sys::state& state);

// Producer debt is secured by the province. On a change of sovereign the old
// bank therefore realizes the claim immediately instead of silently handing
// an old owner's loan book to the conqueror.
void on_province_owner_changed(sys::state& state, dcon::province_id province,
	dcon::nation_id old_owner, dcon::nation_id new_owner);

} // namespace economy::credit
