#include "economy/credit_market.hpp"

#include <cmath>

namespace credit = economy::credit;

namespace {

credit::inputs healthy_market() {
	credit::inputs result;
	result.enabled = true;
	result.base_annual_rate = 0.05f;
	result.banking_reserves = 1'000'000.f;
	result.government_debt = 0.f;
	result.private_investment = 0.f;
	result.producer_debt = 0.f;
	result.credit_health = 1.f;
	result.private_credit_shortfall = 0.f;
	return result;
}

} // namespace

TEST_CASE("credit market is an exact legacy no-op", "[economy][credit]") {
	credit::inputs inputs;
	inputs.enabled = false;
	inputs.base_annual_rate = 0.05f;
	inputs.banking_reserves = 10.f;
	inputs.government_debt = 1'000'000.f;
	inputs.private_credit_shortfall = 1'000'000.f;

	auto const result = credit::calculate(inputs);
	REQUIRE_FALSE(result.enabled);
	REQUIRE(result.interest_cost_multiplier == Approx(1.f));
	REQUIRE(result.policy_annual_rate == Approx(0.05f));
	REQUIRE(result.private_credit_extended == Approx(0.f));
	REQUIRE(result.private_interest_due == Approx(0.f));
}

TEST_CASE("an idle bank lends at the base rate", "[economy][credit]") {
	auto const result = credit::calculate(healthy_market());
	REQUIRE(result.enabled);
	REQUIRE(result.utilization == Approx(0.f));
	REQUIRE(result.interest_cost_multiplier == Approx(1.f));
	REQUIRE(result.policy_annual_rate == Approx(0.05f));
}

TEST_CASE("scarce loanable funds raise the rate", "[economy][credit]") {
	auto scarce = healthy_market();
	scarce.government_debt = 800'000.f;
	auto const result = credit::calculate(scarce);

	REQUIRE(result.utilization == Approx(800'000.f / 1'800'000.f));
	REQUIRE(result.policy_annual_rate > 0.05f);
	REQUIRE(result.interest_cost_multiplier == Approx(
		1.f + 3.f * result.utilization * result.utilization));
}

TEST_CASE("the rate is bounded when the bank is exhausted", "[economy][credit]") {
	auto exhausted = healthy_market();
	exhausted.banking_reserves = 1.f;
	exhausted.government_debt = 1'000'000.f;
	exhausted.credit_health = 0.f;

	auto const result = credit::calculate(exhausted);
	REQUIRE(std::isfinite(result.policy_annual_rate));
	REQUIRE(result.interest_cost_multiplier > 1.f);
	REQUIRE(result.interest_cost_multiplier <= credit::maximum_rate_multiplier);
	// A bank with nothing free lends nothing, whatever the demand.
	REQUIRE(result.lending_capacity == Approx(0.f));
}

TEST_CASE("government claims raise rates without double-subtracting cash", "[economy][credit]") {
	auto free_market = healthy_market();
	free_market.private_credit_shortfall = 1'000'000.f;
	auto indebted = free_market;
	indebted.government_debt = 700'000.f;

	auto const unencumbered = credit::calculate(free_market);
	auto const crowded = credit::calculate(indebted);

	REQUIRE(unencumbered.private_credit_extended > 0.f);
	REQUIRE(crowded.private_credit_extended == Approx(unencumbered.private_credit_extended));
	REQUIRE(crowded.government_share == Approx(1.f));
	// Borrowing is dearer for everyone, not only for the treasury.
	REQUIRE(crowded.policy_annual_rate > unencumbered.policy_annual_rate);
}

TEST_CASE("lending is capped by free reserves, not by demand", "[economy][credit]") {
	auto hungry = healthy_market();
	hungry.private_credit_shortfall = 1'000'000'000.f;

	auto const result = credit::calculate(hungry);
	// 1,000,000 * (1 - 0.20) * 0.05 * 1.0
	REQUIRE(result.lending_capacity == Approx(40'000.f));
	REQUIRE(result.private_credit_extended == Approx(40'000.f));
}

TEST_CASE("bank balance sheet separates cash from outstanding claims", "[economy][credit]") {
	auto inputs = healthy_market();
	inputs.government_debt = 200.f;
	inputs.private_investment = 300.f;
	inputs.producer_debt = 400.f;
	auto const balance = credit::make_balance_sheet(inputs);
	REQUIRE(balance.cash_reserves == Approx(1'000'000.f));
	REQUIRE(balance.government_bonds == Approx(200.f));
	REQUIRE(balance.investment_loans == Approx(300.f));
	REQUIRE(balance.producer_loans == Approx(400.f));
	REQUIRE(balance.total_assets == Approx(1'000'900.f));
}

TEST_CASE("a bank in poor health lends less", "[economy][credit]") {
	auto shaken = healthy_market();
	shaken.private_credit_shortfall = 1'000'000.f;
	shaken.credit_health = 0.25f;

	auto const result = credit::calculate(shaken);
	REQUIRE(result.private_credit_extended == Approx(10'000.f));
	// Risk shows up in the price as well as in the quantity.
	REQUIRE(result.policy_annual_rate > 0.05f);
}

TEST_CASE("deployed capital services the bank instead of sitting idle", "[economy][credit]") {
	auto deployed = healthy_market();
	deployed.private_investment = 365'000.f;

	auto const result = credit::calculate(deployed);
	REQUIRE(result.private_interest_due > 0.f);
	// One day of the annual rate on the deployed stock.
	REQUIRE(result.private_interest_due
		== Approx(365'000.f * result.policy_annual_rate / 365.f));
	REQUIRE(result.private_interest_due <= deployed.private_investment);
}

TEST_CASE("financial circulation pays realized yield and slowly returns idle capital",
		"[economy][credit][circulation]") {
	credit::circulation_inputs inputs;
	inputs.enabled = true;
	inputs.has_claimants = true;
	inputs.banking_reserves = 1'000'000.f;
	inputs.private_investment = 500'000.f;
	inputs.realized_bank_interest = 100.f;
	inputs.credit_demand = 1'000.f;
	inputs.private_construction_spending = 500.f;

	auto const result = credit::calculate_circulation(inputs);
	REQUIRE(result.bank_profit_dividend == Approx(50.f));
	REQUIRE(result.bank_idle_withdrawal > 0.f);
	REQUIRE(result.investment_idle_withdrawal > 0.f);
	REQUIRE(result.bank_total() <= inputs.banking_reserves);
	REQUIRE(result.investment_idle_withdrawal <= inputs.private_investment);
	REQUIRE(result.total() == Approx(result.bank_total()
		+ result.investment_idle_withdrawal));
}

TEST_CASE("active credit and construction needs protect liquidity buffers",
		"[economy][credit][circulation]") {
	credit::circulation_inputs inputs;
	inputs.enabled = true;
	inputs.has_claimants = true;
	inputs.banking_reserves = 1'000'000.f;
	inputs.private_investment = 500'000.f;
	inputs.realized_bank_interest = 100.f;
	inputs.credit_demand = 20'000.f;
	inputs.private_construction_spending = 10'000.f;

	auto const result = credit::calculate_circulation(inputs);
	// Profit can be paid, but principal stays available for observed demand.
	REQUIRE(result.bank_profit_dividend == Approx(50.f));
	REQUIRE(result.bank_idle_withdrawal == Approx(0.f));
	REQUIRE(result.investment_idle_withdrawal == Approx(0.f));
}

TEST_CASE("financial circulation is a legacy no-op and requires a claimant",
		"[economy][credit][circulation]") {
	credit::circulation_inputs inputs;
	inputs.banking_reserves = 1'000'000.f;
	inputs.private_investment = 500'000.f;
	inputs.realized_bank_interest = 100.f;
	REQUIRE(credit::calculate_circulation(inputs).total() == Approx(0.f));

	inputs.enabled = true;
	REQUIRE(credit::calculate_circulation(inputs).total() == Approx(0.f));
	inputs.has_claimants = true;
	REQUIRE(credit::calculate_circulation(inputs).total() > 0.f);
}

TEST_CASE("broken circulation inputs remain finite and nonnegative",
		"[economy][credit][circulation]") {
	credit::circulation_inputs inputs;
	inputs.enabled = true;
	inputs.has_claimants = true;
	inputs.banking_reserves = std::numeric_limits<float>::quiet_NaN();
	inputs.private_investment = -5.f;
	inputs.realized_bank_interest = std::numeric_limits<float>::infinity();
	inputs.credit_demand = -10.f;
	inputs.private_construction_spending = std::numeric_limits<float>::quiet_NaN();
	auto const result = credit::calculate_circulation(inputs);
	REQUIRE(std::isfinite(result.total()));
	REQUIRE(result.total() >= 0.f);
}

TEST_CASE("credit settlement moves money without creating it", "[economy][credit]") {
	// settle_nation only ever transfers between two existing stocks, so the
	// money-supply anchor must never see a residual from it.
	credit::market result;
	result.enabled = true;
	result.private_credit_extended = 40'000.f;
	result.private_interest_due = 5'000.f;

	auto reserves = 1'000'000.f;
	auto pool = 100'000.f;
	auto const before = reserves + pool;

	auto const extended = std::min(result.private_credit_extended, reserves);
	reserves -= extended;
	pool += extended;
	auto const interest = std::min(result.private_interest_due, pool);
	pool -= interest;
	reserves += interest;

	REQUIRE(reserves + pool == Approx(before));
	REQUIRE(pool > 100'000.f);
	REQUIRE(reserves < 1'000'000.f);
}

TEST_CASE("degenerate credit inputs stay finite", "[economy][credit]") {
	credit::inputs broken;
	broken.enabled = true;
	broken.base_annual_rate = std::numeric_limits<float>::quiet_NaN();
	broken.banking_reserves = -1.f;
	broken.government_debt = std::numeric_limits<float>::infinity();
	broken.private_investment = std::numeric_limits<float>::quiet_NaN();
	broken.credit_health = 7.f;
	broken.private_credit_shortfall = -5.f;

	auto const result = credit::calculate(broken);
	REQUIRE(std::isfinite(result.policy_annual_rate));
	REQUIRE(std::isfinite(result.interest_cost_multiplier));
	REQUIRE(std::isfinite(result.lending_capacity));
	REQUIRE(result.private_credit_extended >= 0.f);
	REQUIRE(result.private_interest_due >= 0.f);
}

TEST_CASE("producer financing is bounded by the bank, not by the deficit",
		"[economy][credit]") {
	auto market = credit::calculate(healthy_market());
	REQUIRE(market.lending_capacity == Approx(40'000.f));

	// A firm that has overdrawn far beyond what the bank can lend gets only what
	// the bank actually has. The rest is an unfunded deficit, reported rather
	// than silently financed the way an unbounded till used to.
	auto const large = credit::finance_producers(market, 1'000'000.f);
	REQUIRE(large.requested == Approx(1'000'000.f));
	REQUIRE(large.extended == Approx(40'000.f));
	REQUIRE(large.unfunded == Approx(960'000.f));
	REQUIRE(large.interest > 0.f);

	// A small deficit is funded in full.
	auto const small = credit::finance_producers(market, 1'000.f);
	REQUIRE(small.extended == Approx(1'000.f));
	REQUIRE(small.unfunded == Approx(0.f));
}

TEST_CASE("producer financing is a legacy no-op", "[economy][credit]") {
	credit::inputs disabled;
	disabled.enabled = false;
	auto const result = credit::finance_producers(credit::calculate(disabled), 5'000.f);
	REQUIRE(result.extended == Approx(0.f));
	// Nothing is financed and nothing is hidden: the whole deficit is reported.
	REQUIRE(result.unfunded == Approx(5'000.f));
}

TEST_CASE("government debt prices producer credit without double-counting reserves", "[economy][credit]") {
	auto indebted = healthy_market();
	indebted.government_debt = 700'000.f;
	auto const crowded = credit::finance_producers(credit::calculate(indebted), 1'000'000.f);
	auto const free = credit::finance_producers(credit::calculate(healthy_market()), 1'000'000.f);
	REQUIRE(crowded.extended == Approx(free.extended));
	REQUIRE(crowded.unfunded == Approx(free.unfunded));
}

TEST_CASE("producer debt raises the price of all new credit", "[economy][credit]") {
	auto clear = healthy_market();
	auto encumbered = clear;
	encumbered.producer_debt = 800'000.f;

	auto const free = credit::calculate(clear);
	auto const stressed = credit::calculate(encumbered);
	REQUIRE(stressed.utilization == Approx(800'000.f / 1'800'000.f));
	REQUIRE(stressed.policy_annual_rate > free.policy_annual_rate);
	REQUIRE(stressed.lending_capacity == Approx(free.lending_capacity));
}

TEST_CASE("producer financing sanitizes a broken deficit", "[economy][credit]") {
	auto const market = credit::calculate(healthy_market());
	REQUIRE(credit::finance_producers(market, -5.f).requested == Approx(0.f));
	REQUIRE(credit::finance_producers(
		market, std::numeric_limits<float>::quiet_NaN()).extended == Approx(0.f));
}

TEST_CASE("an unfinanced deficit forces producers to shrink", "[economy][credit]") {
	// No deficit, or a fully financed one, leaves hiring alone.
	REQUIRE(credit::employment_availability(0.f, 0.f) == Approx(1.f));
	REQUIRE(credit::employment_availability(0.f, 100'000.f) == Approx(1.f));

	// A wholly unfinanced deficit contracts employment by the daily cap, not by
	// the whole shortfall: a credit squeeze is a squeeze, not a collapse.
	REQUIRE(credit::employment_availability(100'000.f, 100'000.f)
		== Approx(1.f - credit::maximum_daily_employment_contraction));

	// Partial financing contracts proportionally less.
	auto const half = credit::employment_availability(50'000.f, 100'000.f);
	REQUIRE(half > 1.f - credit::maximum_daily_employment_contraction);
	REQUIRE(half < 1.f);
}

TEST_CASE("employment availability stays bounded on broken input", "[economy][credit]") {
	REQUIRE(credit::employment_availability(
		std::numeric_limits<float>::quiet_NaN(), 100.f) == Approx(1.f));
	// More unfunded than requested cannot contract past the cap.
	REQUIRE(credit::employment_availability(1'000'000.f, 1.f)
		== Approx(1.f - credit::maximum_daily_employment_contraction));
	REQUIRE(credit::employment_availability(-5.f, 100.f) == Approx(1.f));
}

TEST_CASE("credit contraction is confined to distressed provinces",
		"[economy][credit][employment][integration]") {
	auto state = std::make_unique<sys::state>();
	state->force_age_of_transformation_ruleset = true;
	auto const distressed = state->world.create_province();
	auto const healthy = state->world.create_province();
	state->credit_daily_flows.reset(state->world.nation_size(),
		state->world.province_size());
	state->credit_daily_flows.record_producer_province(distressed, 0.95f);

	REQUIRE(credit::producer_employment_scale(*state, distressed)
		== Approx(0.95f));
	REQUIRE(credit::producer_employment_scale(*state, healthy)
		== Approx(1.f));
	state->force_age_of_transformation_ruleset = false;
	REQUIRE(credit::producer_employment_scale(*state, distressed)
		== Approx(1.f));
}

TEST_CASE("a debt with no principal is nothing to service", "[economy][credit]") {
	auto const none = credit::service_debt(0.f, 10'000.f, 0.05f);
	REQUIRE(none.interest == Approx(0.f));
	REQUIRE(none.repayment == Approx(0.f));
	REQUIRE(none.closing_debt == Approx(0.f));
}

TEST_CASE("a firm in surplus pays interest and amortizes", "[economy][credit]") {
	auto const serviced = credit::service_debt(365'000.f, 1'000'000.f, 0.05f);
	// One day of the annual rate on the outstanding stock.
	REQUIRE(serviced.interest == Approx(365'000.f * 0.05f / 365.f));
	REQUIRE(serviced.capitalized == Approx(0.f));
	REQUIRE(serviced.repayment
		== Approx(365'000.f * credit::principal_repayment_rate));
	// The book shrinks when it is being serviced.
	REQUIRE(serviced.closing_debt < 365'000.f);
}

TEST_CASE("unpaid interest capitalizes into the debt", "[economy][credit]") {
	// An overdrawn till has nothing to pay with, so the interest is added to the
	// principal. This is what lets a firm spiral without anyone lending it more.
	auto const spiral = credit::service_debt(100'000.f, 0.f, 0.10f);
	REQUIRE(spiral.interest == Approx(0.f));
	REQUIRE(spiral.repayment == Approx(0.f));
	REQUIRE(spiral.capitalized > 0.f);
	REQUIRE(spiral.closing_debt > 100'000.f);
	REQUIRE(spiral.closing_debt
		== Approx(100'000.f + 100'000.f * 0.10f / 365.f));
}

TEST_CASE("partial cash pays interest before principal", "[economy][credit]") {
	auto const full_interest = 100'000.f * 0.10f / 365.f;
	// Just enough for the interest and nothing else.
	auto const tight = credit::service_debt(100'000.f, full_interest, 0.10f);
	REQUIRE(tight.interest == Approx(full_interest));
	REQUIRE(tight.capitalized == Approx(0.f));
	REQUIRE(tight.repayment == Approx(0.f));

	// Half the interest: the rest capitalizes and the book grows.
	auto const strained = credit::service_debt(100'000.f, full_interest * 0.5f, 0.10f);
	REQUIRE(strained.interest == Approx(full_interest * 0.5f));
	REQUIRE(strained.capitalized == Approx(full_interest * 0.5f));
	REQUIRE(strained.closing_debt > 100'000.f);
}

TEST_CASE("debt service survives broken input", "[economy][credit]") {
	auto const broken = credit::service_debt(
		std::numeric_limits<float>::quiet_NaN(), -5.f,
		std::numeric_limits<float>::infinity());
	REQUIRE(std::isfinite(broken.interest));
	REQUIRE(std::isfinite(broken.closing_debt));
	REQUIRE(broken.closing_debt >= 0.f);
	REQUIRE(credit::service_debt(-100.f, 100.f, 0.05f).closing_debt == Approx(0.f));
}

TEST_CASE("servicing a debt only moves money, never creates it", "[economy][credit]") {
	// Interest and repayment both come out of the till and land in the bank, so
	// the money-supply account must never see a residual from this.
	auto const serviced = credit::service_debt(50'000.f, 10'000.f, 0.08f);
	auto till = 10'000.f;
	auto reserves = 500'000.f;
	auto const before = till + reserves;

	auto const taken = serviced.interest + serviced.repayment;
	till -= taken;
	reserves += taken;

	REQUIRE(till + reserves == Approx(before));
	REQUIRE(till >= 0.f);
	// Capitalized interest is an accrual on the book, not a movement of cash.
	REQUIRE(serviced.closing_debt >= 0.f);
}

TEST_CASE("producer debt records only settled bank advances",
		"[economy][credit]") {
	REQUIRE(credit::producer_debt_after_extension(0.f, 475'000.f)
		== Approx(475'000.f));
	// An unfunded shortfall creates no bank asset.
	REQUIRE(credit::producer_debt_after_extension(475'000.f, 0.f)
		== Approx(475'000.f));
	// A later cash advance adds exactly its settled amount to the claim.
	REQUIRE(credit::producer_debt_after_extension(475'000.f, 115'000.f)
		== Approx(590'000.f));
}

TEST_CASE("producer debt becomes a bad loan only after valuation exists",
		"[economy][credit]") {
	REQUIRE_FALSE(credit::producer_debt_is_unrecoverable(
		1'000.f, 0.f, true, false));
	REQUIRE(credit::producer_debt_is_unrecoverable(
		31'000.f, 10'000.f, true, true));
	REQUIRE_FALSE(credit::producer_debt_is_unrecoverable(
		30'000.f, 10'000.f, true, true));
	REQUIRE(credit::producer_debt_is_unrecoverable(
		1'000.f, 0.f, false, false));
}
