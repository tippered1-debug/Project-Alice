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
	// national_bank is liquid cash. Outstanding claims are listed separately.
	float banking_reserves = 0.f;
	float government_debt = 0.f;
	float private_investment = 0.f;
	// Outstanding producer loan book: cash the bank actually advanced, plus
	// capitalized interest, less principal repaid or written off.
	float producer_debt = 0.f;
	// Bounded [0, 1], one being healthy. Supplied by banking_stability.
	float credit_health = 1.f;
	// Private construction that the investment pool cannot fund today.
	float private_credit_shortfall = 0.f;
};

struct balance_sheet {
	float cash_reserves = 0.f;
	float government_bonds = 0.f;
	float investment_loans = 0.f;
	float producer_loans = 0.f;
	float total_assets = 0.f;
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
	float private_credit_requested = 0.f;
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
// Claims cannot exceed total assets in the explicit asset statement.
inline constexpr float utilization_cap = 1.0f;
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
	std::vector<float> private_requested;
	std::vector<float> private_construction_spent;
	// Producer till financing, recorded by settle_producer_credit.
	std::vector<float> producer_extended;
	std::vector<float> producer_unfunded;
	// Employment multiplier derived from the unfunded share, so a firm that
	// could not finance its losses actually has to shrink.
	std::vector<float> producer_availability;
	// Provincial targeting keeps a distressed industrial district from shrinking
	// every healthy factory in the same country.
	std::vector<float> producer_availability_by_province;
	// Debt service settled today, for observability.
	std::vector<float> producer_interest_paid;
	std::vector<float> producer_principal_repaid;
	// Producer debt written off because the secured industry disappeared or
	// the outstanding claim exceeded the province's realizable value.
	std::vector<float> producer_writeoff;
	// Existing bank and investment-pool cash returned to the POPs that supplied
	// it. These are withdrawals/dividends, never newly created income.
	std::vector<float> bank_distribution;
	std::vector<float> investment_distribution;

	void reset(uint32_t nation_count, uint32_t province_count);
	void record(dcon::nation_id nation, float extended_amount, float interest_amount);
	void record_private_spending(dcon::nation_id nation, float amount);
	void record_producer(dcon::nation_id nation, float extended_amount, float unfunded_amount,
		float availability);
	void record_producer_province(dcon::province_id province, float availability);
	void record_debt_service(dcon::nation_id nation, float interest, float principal);
	void record_writeoff(dcon::nation_id nation, float amount);
};

// Banks and investment funds need a way to return mature or unused capital to
// their claimants. Otherwise both accounts are permanent one-way sinks even
// after every useful project has been financed.
struct circulation_inputs {
	bool enabled = false;
	bool has_claimants = false;
	float banking_reserves = 0.f;
	float private_investment = 0.f;
	// Cash interest actually received today, not an imputed return on the stock.
	float realized_bank_interest = 0.f;
	float credit_demand = 0.f;
	float private_construction_spending = 0.f;
};

struct circulation {
	float bank_profit_dividend = 0.f;
	float bank_idle_withdrawal = 0.f;
	float investment_idle_withdrawal = 0.f;

	[[nodiscard]] float bank_total() const {
		return bank_profit_dividend + bank_idle_withdrawal;
	}
	[[nodiscard]] float total() const {
		return bank_total() + investment_idle_withdrawal;
	}
};

// Banks retain half of realized interest and distribute half. Principal is
// returned only when it exceeds a 90-day demand buffer, and then over three
// years. The slow release avoids turning a temporary lull into a bank run.
inline constexpr float realized_interest_distribution_share = 0.50f;
inline constexpr float capital_demand_reserve_days = 90.f;
inline constexpr float idle_capital_release_days = 1095.f;

[[nodiscard]] circulation calculate_circulation(circulation_inputs raw_inputs);

// Settles the pure result against the two national accounts and records the
// exact cash available for distribution to POP claimants.
[[nodiscard]] circulation settle_circulation(sys::state& state,
	dcon::nation_id nation, bool has_claimants);

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

// Only settled bank cash creates a bank claim. The unfunded part of a negative
// producer till remains a separately reported operating deficit.
[[nodiscard]] float producer_debt_after_extension(float outstanding_debt,
	float extended);

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
[[nodiscard]] float producer_employment_scale(sys::state const& state,
	dcon::province_id province);

[[nodiscard]] market calculate(inputs raw_inputs);
[[nodiscard]] balance_sheet make_balance_sheet(inputs raw_inputs);

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
