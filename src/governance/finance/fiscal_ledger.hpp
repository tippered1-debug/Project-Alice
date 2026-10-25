#pragma once

#include "dcon_generated.hpp"
#include "date_interface.hpp"
#include "economy/relations/relations.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <vector>

namespace sys { class state; }
namespace governance::policy { enum class topic_id : uint16_t; }

namespace governance::finance {

// The state's books: a one-way projection of the canonical fiscal flows of
// every nation, for the interface. It is fed where money actually moves
// (the transaction recorders and the fiscal API) and never moves money itself.
//
// The fiscal perimeter of a nation is every institution of it except the
// household sector, the pass-through account wage taxes are collected
// through. Money moving between two institutions of one nation (the monthly
// sweep of taxes into the finance ministry, appropriations, transfers into a
// construction subaccount) is internal and never counted. What crosses the
// perimeter is classified once:
//
// - taxes, by the tax that was collected, as cash actually received;
// - dividends paid to the state as owner, as state enterprise revenue;
// - borrowing (public debt issued, loans received) is financing, not revenue;
// - payroll, procurement and other final spending, by the function of the
//   institution that spends; payments to persons outside payroll are social
//   transfers;
// - interest on public debt is debt service; principal repaid is financing;
// - deposits, withdrawals, currency exchange, equity and lending are
//   financial flows and neither revenue nor spending.
enum class revenue_source : uint8_t {
	income_tax = 0, resource_royalties, customs_duties, dividend_tax, state_enterprises, other, count
};
enum class spending_function : uint8_t {
	administration = 0, military, education, public_order, infrastructure, social_transfers, debt_service, count
};
inline constexpr size_t revenue_source_count = size_t(revenue_source::count);
inline constexpr size_t spending_function_count = size_t(spending_function::count);

// One closed day, or one month, of one nation's books. Saved with the
// canonical runtime; new fields need a new save version.
struct fiscal_day {
	int32_t nation = 0; // the nation's index
	int32_t day = 0; // sys::date::to_raw_value() of the day, or of the first day of the month
	uint8_t span = 0; // fiscal_span
	uint8_t reserved[3]{};
	std::array<float, revenue_source_count> revenue{};
	std::array<float, spending_function_count> spending{};
	float tax_assessed = 0.0f; // tax newly charged to taxpayers, collected or not
	float borrowed = 0.0f; // proceeds of public debt issued and loans received
	float principal_repaid = 0.0f; // principal of public debt and loans paid back
	float financial_net = 0.0f; // deposits, withdrawals, exchange, equity and lending; positive into the treasuries
	float treasury = 0.0f; // closing cash in every treasury of the perimeter
	float ministry = 0.0f; // closing cash of the finance ministry's own treasury
	float debt = 0.0f; // closing public debt outstanding, principal and accrued interest
};
static_assert(std::is_trivially_copyable_v<fiscal_day>);
enum class fiscal_span : uint8_t { day = 0, month = 1, open = 2 };

// How many closed days and months each nation keeps.
inline constexpr size_t kept_days = 400;
inline constexpr size_t kept_months = 36;

float revenue_total(fiscal_day const&);
float spending_total(fiscal_day const&);

// ---- feeding the books ----

// Called by every recorder of a money transfer, after the transfer took
// place. A party that is not an economic actor (an exact person's account)
// is passed as an empty id.
void observe_transfer(sys::state&, dcon::economic_actor_id payer, dcon::economic_actor_id payee,
	float amount, dcon::commodity_id settlement, economy::relations::transaction_kind kind);
// Tax actually received by `creditor`, a tax-levying institution, from `payer`.
void post_tax_collected(sys::state&, dcon::institution_id creditor, dcon::economic_actor_id payer,
	revenue_source source, float amount, dcon::commodity_id settlement);
void post_tax_assessed(sys::state&, dcon::institution_id creditor, dcon::economic_actor_id taxpayer,
	float amount, dcon::commodity_id settlement);
// Debt service paid by `debtor`, split into interest and principal.
void post_debt_service(sys::state&, dcon::institution_id debtor, float interest, float principal,
	dcon::commodity_id settlement);
// Closes the day for every nation: records the closing balances and keeps
// the day and its month. During presimulation the day's flows are dropped.
void close_fiscal_day(sys::state&, bool presimulation);

std::vector<fiscal_day> export_ledger(sys::state const&);
bool import_ledger(sys::state&, std::vector<fiscal_day> const&);
void clear_ledger(sys::state&);

// The books' classification of an institution and of its spending.
bool in_fiscal_perimeter(sys::state const&, dcon::institution_id);
spending_function function_of(sys::state const&, dcon::institution_id);
revenue_source source_of_topic(policy::topic_id);

// ---- reading the books ----

enum class fiscal_period : uint8_t { days_30 = 0, days_90 = 1, months_12 = 2 };
enum class fiscal_step : uint8_t { daily = 0, weekly = 1, monthly = 2 };
bool step_allowed(fiscal_period, fiscal_step);
fiscal_step default_step(fiscal_period);

struct fiscal_bucket {
	int32_t first_day = 0; // raw dates of the days it covers
	int32_t last_day = 0;
	bool recorded = false; // some day of it is on the books
	float revenue = 0.0f;
	float spending = 0.0f;
	float balance = 0.0f;
	float treasury = 0.0f; // closing figures of its last recorded day
	float debt = 0.0f;
};

struct public_debt_line {
	dcon::obligation_id obligation{};
	dcon::institution_id debtor{};
	dcon::economic_actor_id holder{};
	float principal = 0.0f;
	float interest_due = 0.0f;
	float annual_rate = 0.0f;
	sys::date issued{};
	sys::date due{};
	bool overdue = false;
	bool defaulted = false;
};

// A transparent assessment of the state as a borrower. Every factor is a
// ratio from the books or the obligations, scored 0 to 1 against the bounds
// below; the assessment is the weakest factor. Banks underwrite sovereign
// bonds against it the way they underwrite projects (see
// economy::banking::quote_sovereign_credit). Without 30 recorded days of
// revenue there is nothing to annualize and the state is not assessed.
struct credit_factor {
	float value = 0.0f; // the ratio itself
	float score = 0.0f; // 1 at or better than `good`, 0 at or worse than `poor`
	float good = 0.0f;
	float poor = 0.0f;
};
enum class credit_grade : uint8_t { aaa = 0, aa, a, bbb, bb, b, ccc, cc, d, count };
struct credit_assessment {
	bool assessed = false;
	float annual_revenue = 0.0f; // revenue of the recorded days annualized, up to a year of them
	float score = 0.0f;
	credit_grade grade = credit_grade::d;
	credit_factor debt_to_revenue; // public debt over annual revenue
	credit_factor interest_to_revenue; // annual interest over annual revenue
	credit_factor liquidity; // treasury cash over debt service due within 90 days
	credit_factor deficit; // budget balance over revenue, recorded days
	credit_factor record; // 1 clean, 0.4 overdue, 0 in default
};
inline constexpr int32_t credit_liquidity_days = 90;
inline constexpr int32_t credit_minimum_days = 30;
credit_grade grade_of(float score, bool defaulted);
credit_assessment assess_credit(sys::state const&, dcon::nation_id);

struct fiscal_report {
	dcon::nation_id nation{};
	fiscal_period period = fiscal_period::days_30;
	fiscal_step step = fiscal_step::daily;
	int32_t span_days = 0; // the days the period covers
	int32_t recorded_days = 0; // the days of it on the books
	int32_t first_recorded = 0; // the nation's first recorded day, raw; 0 when none
	int32_t period_first = 0; // raw dates of the period
	int32_t period_last = 0;

	std::array<float, revenue_source_count> revenue{};
	std::array<float, spending_function_count> spending{};
	float revenue_total = 0.0f;
	float spending_total = 0.0f;
	float balance = 0.0f;
	bool previous_known = false; // the period before is fully on the books
	float previous_revenue = 0.0f;
	float previous_spending = 0.0f;
	float tax_assessed = 0.0f;
	float borrowed = 0.0f;
	float principal_repaid = 0.0f;
	float financial_net = 0.0f;

	float treasury = 0.0f; // now
	float ministry = 0.0f;
	float treasury_opening = 0.0f; // closing cash of the day before the period, or of its first day
	float treasury_per_day = 0.0f;
	float tax_receivables = 0.0f; // assessed and not yet paid

	float public_debt = 0.0f;
	float debt_principal = 0.0f;
	float debt_accrued_interest = 0.0f;
	float debt_overdue = 0.0f;
	float debt_defaulted = 0.0f;
	float annual_interest = 0.0f; // principal times rate, over every live obligation
	float average_rate = 0.0f;
	sys::date next_payment{};
	float next_payment_amount = 0.0f;
	bool issuance_allowed = true;
	std::optional<float> debt_ceiling;
	std::vector<public_debt_line> obligations; // earliest due first
	credit_assessment credit;

	std::vector<fiscal_bucket> buckets; // the chart and the sparklines, oldest first

	// The fiscal law in force.
	bool law_in_force = false;
	float base_rate = 0.0f;
	float progressivity = 0.0f;
	std::array<float, 3> stratum_rates{}; // poor, middle, rich
	std::array<float, 4> priorities{}; // education, public order, infrastructure, local government; shares of their sum
	sys::date next_bill{}; // a new fiscal bill may not be laid before this
};

fiscal_report report(sys::state const&, dcon::nation_id, fiscal_period, fiscal_step);

// The outcome of the last fiscal bill the player laid before the chambers;
// it is an interface notice, not saved.
enum class bill_outcome : uint8_t {
	none = 0, enacted, rejected, no_assent, too_soon, no_constitution, no_enactor, unchanged, invalid
};
struct bill_notice {
	sys::date laid_on{};
	bill_outcome outcome = bill_outcome::none;
	uint32_t ayes = 0;
	uint32_t noes = 0;
	uint32_t seats = 0;
};
void note_bill(sys::state&, dcon::nation_id, bill_notice const&);
bill_notice last_bill(sys::state const&, dcon::nation_id);

// The outcome of the last debt operation the player ordered; not saved.
enum class debt_outcome : uint8_t {
	none = 0, placed, repaid, no_office, no_bank, declined, prohibited, ceiling, no_cash, invalid
};
struct debt_notice {
	sys::date on{};
	debt_outcome outcome = debt_outcome::none;
	float amount = 0.0f;
	float annual_rate = 0.0f;
	dcon::organization_id bank{};
};
void note_debt(sys::state&, dcon::nation_id, debt_notice const&);
debt_notice last_debt(sys::state const&, dcon::nation_id);

} // namespace governance::finance
