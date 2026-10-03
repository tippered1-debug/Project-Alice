#pragma once

#include "dcon_generated.hpp"

namespace sys { class state; }

namespace economy::monetary_policy {

// Each nation has a central bank per settlement. It measures consumer prices,
// sets a policy rate by a rule, and lends reserves to banks that are illiquid
// but solvent. Commercial banks lend at the policy rate plus a premium that
// falls as their reserves grow, so savings lower the cost of credit, and
// depositors earn a share of what bank loans earn.
// The central bank is the economic arm of the constitutional central bank
// institution: who appoints its governor and who sets its mandate are the
// constitution's and the law's.

inline constexpr float inflation_target = 0.02f;
inline constexpr float neutral_real_rate = 0.02f;
// Response to inflation beyond one-for-one (the Taylor principle).
inline constexpr float inflation_response = 0.5f;
// Share of the gap to the rule's rate closed at each review.
inline constexpr float rate_smoothing = 0.25f;
inline constexpr float maximum_policy_rate = 0.3f;
inline constexpr int32_t review_period_days = 30;
// Weight of the latest month in smoothed inflation.
inline constexpr float inflation_smoothing = 0.5f;
// A bank whose reserves are at its liquidity requirement pays this premium;
// the premium falls to zero at twice the requirement.
inline constexpr float maximum_liquidity_premium = 0.03f;
// Depositors receive this share of the yield of the bank's performing loans,
// never more than its lending rate.
inline constexpr float deposit_pass_through = 0.8f;
// The discount window lends at the policy rate plus this penalty, against at
// most this share of the bank's performing loans, for this term.
inline constexpr float discount_penalty = 0.02f;
inline constexpr float discount_collateral_share = 0.5f;
inline constexpr int32_t discount_term_days = 30;

dcon::organization_id central_bank_for(sys::state const&, dcon::nation_id, dcon::commodity_id settlement);
// Opens the central bank on first use. Its first policy rate is the average
// lending rate the nation's banks were authored with, else the neutral rate.
dcon::organization_id open_central_bank(sys::state&, dcon::nation_id, dcon::commodity_id settlement);
bool is_central_bank(sys::state const&, dcon::organization_id);
// The constitutional institution a central bank organization embodies. The
// bank acts only while that institution holds `issue_currency` over the nation;
// its inflation target is the law's, else the default.
dcon::institution_id public_institution_of(sys::state const&, dcon::organization_id);
bool authorized(sys::state const&, dcon::organization_id, sys::date);
float inflation_target_of(sys::state const&, dcon::nation_id, sys::date);
float policy_rate(sys::state const&, dcon::organization_id central_bank);
// Base money the central bank has created and not yet retired.
float issued_money(sys::state const&, dcon::organization_id central_bank);

// Cost of what buyers bid for in the nation's markets over the last review
// period, at today's reference prices relative to base costs.
float price_index(sys::state const&, dcon::nation_id);
float liquidity_premium(sys::state const&, dcon::organization_id bank);
float deposit_rate(sys::state const&, dcon::organization_id bank);

// Measures inflation, moves the policy rate toward the rule's rate, and resets
// the lending rate of every bank of the nation in that settlement.
void review(sys::state&, dcon::organization_id central_bank);
// Discount window: lends a bank short of reserves what it lacks, against its
// performing loans, unless it is insolvent. Returns the amount lent.
float lend_reserves(sys::state&, dcon::organization_id bank);
// Repays discount loans out of reserves above the bank's requirement, or rolls
// them over at maturity. Principal repaid is retired base money; interest is
// the central bank's income, remitted to the government. Returns the amount repaid.
float repay_reserves(sys::state&, dcon::organization_id bank);
// Credits a month of interest to a deposit; the bank owes it.
float pay_deposit_interest(sys::state&, dcon::deposit_account_id);
void process(sys::state&);

} // namespace economy::monetary_policy
