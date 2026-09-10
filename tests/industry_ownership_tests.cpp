#include "economy/industry_ownership.hpp"
#include "nations/nations.hpp"

#include <cmath>
#include <limits>

namespace industry = economy::industry_ownership;

namespace {

industry::market_config open_market() {
	industry::market_config config;
	config.enabled = true;
	return config;
}

float total_of(industry::distribution value) {
	auto const parts = industry::shares(value);
	auto sum = 0.f;
	for(auto part : parts)
		sum += part;
	return sum;
}

} // namespace

TEST_CASE("an untouched province is capitalist industry", "[economy][industry]") {
	// Every new field defaults to zero, and capitalists are the residual, so an
	// old save loads as fully private industry with no migration pass.
	industry::distribution empty;
	REQUIRE(empty.capitalists == Approx(1.f));
	REQUIRE(total_of(industry::normalize(empty)) == Approx(1.f));

	industry::distribution zeroed{0.f, 0.f, 0.f, 0.f, 0.f};
	REQUIRE(industry::normalize(zeroed).capitalists == Approx(1.f));
}

TEST_CASE("shares are bounded and sum to one", "[economy][industry]") {
	auto const messy = industry::normalize(industry::distribution{
		4.f, -1.f, 2.f, std::numeric_limits<float>::quiet_NaN(), 2.f});
	REQUIRE(total_of(messy) == Approx(1.f));
	REQUIRE(messy.landed_elites >= 0.f);
	REQUIRE(messy.foreign >= 0.f);
	REQUIRE(messy.capitalists == Approx(0.5f));
}

TEST_CASE("industry is valued on a smoothed profit, never negatively",
		"[economy][industry]") {
	// One good day barely moves a nine-month average.
	auto const nudged = industry::update_smoothed_profit(100.f, 1'000.f);
	REQUIRE(nudged > 100.f);
	REQUIRE(nudged < 110.f);

	REQUIRE(industry::capitalized_value(100.f) == Approx(100.f * 365.f * 10.f));
	// You cannot be paid to take a loss-making works.
	REQUIRE(industry::capitalized_value(-500.f) == Approx(0.f));
	REQUIRE(std::isfinite(industry::capitalized_value(
		std::numeric_limits<float>::quiet_NaN())));
}

TEST_CASE("the ownership market is an exact legacy no-op", "[economy][industry]") {
	industry::market_config disabled;
	disabled.enabled = false;
	std::array<industry::group_finance, industry::owner_group_count> rich{};
	for(auto& finance : rich)
		finance.liquid_savings = 1'000'000.f;

	industry::distribution start{0.6f, 0.2f, 0.1f, 0.1f, 0.f};
	auto const result = industry::clear_market(start, rich, 1'000'000.f, disabled);
	REQUIRE_FALSE(result.enabled);
	REQUIRE(result.after.capitalists == Approx(result.before.capitalists));
	REQUIRE(result.turnover == Approx(0.f));
}

TEST_CASE("nationalization moves industry to the state and pays for it",
		"[economy][industry]") {
	auto config = open_market();
	config.nationalization_rate = 0.01f;
	config.compensation_rate = 0.5f;
	config.available_public_funds = 1'000'000.f;
	std::array<industry::group_finance, industry::owner_group_count> finances{};

	industry::distribution start{0.8f, 0.2f, 0.f, 0.f, 0.f};
	auto const result = industry::clear_market(start, finances, 1'000'000.f, config);
	REQUIRE(result.enabled);
	REQUIRE(result.after.state > result.before.state);
	REQUIRE(result.after.capitalists < result.before.capitalists);
	// Owners are compensated and the treasury carries the cost.
	REQUIRE(result.compensation > 0.f);
	REQUIRE(result.public_cost == Approx(result.compensation));
	REQUIRE(result.cash_delta[std::size_t(industry::owner_group::capitalists)] > 0.f);
	REQUIRE(total_of(result.after) == Approx(1.f));
}

TEST_CASE("a treasury that cannot pay expropriates less", "[economy][industry]") {
	auto generous = open_market();
	generous.nationalization_rate = 0.02f;
	generous.compensation_rate = 1.f;
	generous.available_public_funds = 1'000'000.f;
	auto broke = generous;
	broke.available_public_funds = 1.f;

	std::array<industry::group_finance, industry::owner_group_count> finances{};
	industry::distribution start{1.f, 0.f, 0.f, 0.f, 0.f};

	auto const funded = industry::clear_market(start, finances, 1'000'000.f, generous);
	auto const unfunded = industry::clear_market(start, finances, 1'000'000.f, broke);
	REQUIRE(unfunded.after.state < funded.after.state);
	REQUIRE(unfunded.public_cost <= Approx(1.f));
}

TEST_CASE("privatization sells state industry into private hands",
		"[economy][industry]") {
	auto config = open_market();
	config.privatization_rate = 0.01f;
	std::array<industry::group_finance, industry::owner_group_count> finances{};

	industry::distribution start{0.2f, 0.f, 0.8f, 0.f, 0.f};
	auto const result = industry::clear_market(start, finances, 500'000.f, config);
	REQUIRE(result.after.state < result.before.state);
	REQUIRE(result.after.capitalists > result.before.capitalists);
	// The treasury receives and the buyers pay.
	REQUIRE(result.cash_delta[std::size_t(industry::owner_group::state)] > 0.f);
	REQUIRE(result.cash_delta[std::size_t(industry::owner_group::capitalists)] < 0.f);
}

TEST_CASE("banning foreign investment winds the holding down",
		"[economy][industry]") {
	auto config = open_market();
	config.foreign_investment_allowed = false;
	config.foreign_divestment_rate = 0.01f;
	config.compensation_rate = 0.5f;
	config.available_public_funds = 1'000'000.f;
	std::array<industry::group_finance, industry::owner_group_count> finances{};

	industry::distribution concession{0.3f, 0.f, 0.1f, 0.6f, 0.f};
	auto const result = industry::clear_market(concession, finances, 1'000'000.f, config);
	REQUIRE(result.after.foreign < result.before.foreign);
	REQUIRE(result.after.capitalists > result.before.capitalists);
}

TEST_CASE("the franchise opens industry to its workforce", "[economy][industry]") {
	auto config = open_market();
	config.worker_buyout_rate = 0.01f;
	config.compensation_rate = 0.75f;
	config.available_public_funds = 1'000'000.f;
	std::array<industry::group_finance, industry::owner_group_count> finances{};

	industry::distribution start{1.f, 0.f, 0.f, 0.f, 0.f};
	auto const result = industry::clear_market(start, finances, 1'000'000.f, config);
	REQUIRE(result.after.workers > 0.f);
	REQUIRE(result.after.capitalists < 1.f);
	REQUIRE(total_of(result.after) == Approx(1.f));
}

TEST_CASE("buyers may only commit cash above their reserve", "[economy][industry]") {
	auto config = open_market();
	config.voluntary_ask_rate = 0.5f;
	std::array<industry::group_finance, industry::owner_group_count> destitute{};
	// Savings exist but are entirely spoken for by six months of needs.
	destitute[std::size_t(industry::owner_group::capitalists)].liquid_savings = 600.f;
	destitute[std::size_t(industry::owner_group::capitalists)].monthly_essential_needs = 100.f;

	industry::distribution start{0.5f, 0.5f, 0.f, 0.f, 0.f};
	auto const result = industry::clear_market(start, destitute, 1'000'000.f, config);
	REQUIRE(result.bids[std::size_t(industry::owner_group::capitalists)] == Approx(0.f));
	REQUIRE(result.turnover == Approx(0.f));
}

TEST_CASE("hardship forces owners to list their stake", "[economy][industry]") {
	auto config = open_market();
	std::array<industry::group_finance, industry::owner_group_count> finances{};
	finances[std::size_t(industry::owner_group::workers)].hardship = 1.f;
	finances[std::size_t(industry::owner_group::capitalists)].liquid_savings = 1'000'000.f;

	industry::distribution start{0.5f, 0.f, 0.f, 0.f, 0.5f};
	auto const result = industry::clear_market(start, finances, 1'000'000.f, config);
	REQUIRE(result.distress_asks > 0.f);
	REQUIRE(result.after.workers < result.before.workers);
	REQUIRE(total_of(result.after) == Approx(1.f));
}

TEST_CASE("a month cannot turn over more than its cap", "[economy][industry]") {
	auto config = open_market();
	config.maximum_monthly_turnover = 0.004f;
	config.voluntary_ask_rate = 1.f;
	std::array<industry::group_finance, industry::owner_group_count> flush{};
	for(auto& finance : flush)
		finance.liquid_savings = 1'000'000'000.f;

	industry::distribution start{0.5f, 0.5f, 0.f, 0.f, 0.f};
	auto const result = industry::clear_market(start, flush, 1'000.f, config);
	REQUIRE(result.turnover <= Approx(0.004f));
}

TEST_CASE("profit tax uses realized profit and debits its payers",
		"[economy][industry][tax]") {
	auto config = open_market();
	config.annual_profit_tax_rate = 0.10f;
	config.monthly_taxable_profit = 1'000.f;
	config.voluntary_ask_rate = 0.f;
	std::array<industry::group_finance, industry::owner_group_count> finances{};
	finances[std::size_t(industry::owner_group::capitalists)].liquid_savings = 1'000.f;
	finances[std::size_t(industry::owner_group::workers)].liquid_savings = 1'000.f;

	industry::distribution start{0.75f, 0.f, 0.f, 0.f, 0.25f};
	auto const result = industry::clear_market(start, finances, 9'000'000.f, config);
	REQUIRE(result.profit_tax == Approx(100.f));
	REQUIRE(result.cash_delta[std::size_t(industry::owner_group::capitalists)]
		== Approx(-75.f));
	REQUIRE(result.cash_delta[std::size_t(industry::owner_group::workers)]
		== Approx(-25.f));
	// Changing only the asset valuation cannot change a profit assessment.
	auto const cheap = industry::clear_market(start, finances, 1.f, config);
	REQUIRE(cheap.profit_tax == Approx(result.profit_tax));
}

TEST_CASE("ownership purchases and profit tax share one cash budget",
		"[economy][industry][tax][market]") {
	auto config = open_market();
	config.maximum_monthly_turnover = 0.10f;
	config.voluntary_ask_rate = 1.f;
	config.annual_profit_tax_rate = 0.10f;
	config.monthly_taxable_profit = 1'000.f;
	std::array<industry::group_finance, industry::owner_group_count> finances{};
	finances[std::size_t(industry::owner_group::capitalists)].liquid_savings = 100.f;

	industry::distribution start{0.f, 0.f, 0.f, 0.f, 1.f};
	auto const result = industry::clear_market(start, finances, 1'000.f, config);
	auto const capitalists = std::size_t(industry::owner_group::capitalists);
	auto const workers = std::size_t(industry::owner_group::workers);
	REQUIRE(result.cash_delta[capitalists] == Approx(-100.f));
	REQUIRE(result.cash_delta[workers] == Approx(10.f));
	REQUIRE(result.profit_tax == Approx(90.f));
	REQUIRE(result.cash_delta[capitalists] + result.cash_delta[workers]
		+ result.profit_tax == Approx(0.f));
}

TEST_CASE("dividends follow ownership rather than a single class",
		"[economy][industry]") {
	// This is the whole point of the system: a capitalist's income becomes a
	// return on what they own instead of a payout from an abstract pool.
	industry::distribution mixed{0.5f, 0.1f, 0.2f, 0.15f, 0.05f};
	auto const split = industry::split_dividend(mixed, 1'000.f);
	REQUIRE(split.enabled);
	REQUIRE(split.payout[std::size_t(industry::owner_group::capitalists)] == Approx(500.f));
	REQUIRE(split.payout[std::size_t(industry::owner_group::state)] == Approx(200.f));
	REQUIRE(split.payout[std::size_t(industry::owner_group::foreign)] == Approx(150.f));
	REQUIRE(split.payout[std::size_t(industry::owner_group::workers)] == Approx(50.f));
	REQUIRE(split.retained == Approx(0.f));

	auto sum = 0.f;
	for(auto value : split.payout)
		sum += value;
	REQUIRE(sum == Approx(1'000.f));
}

TEST_CASE("a dividend of nothing is distributed to nobody", "[economy][industry]") {
	auto const none = industry::split_dividend(industry::distribution{}, 0.f);
	REQUIRE_FALSE(none.enabled);
	auto const negative = industry::split_dividend(industry::distribution{}, -100.f);
	REQUIRE_FALSE(negative.enabled);
}

TEST_CASE("historical profiles place industry where it actually was",
		"[economy][industry]") {
	auto const concession = industry::historical_initial_distribution(
		industry::historical_profile::foreign_concession);
	REQUIRE(concession.foreign > concession.capitalists);

	auto const state_led = industry::historical_initial_distribution(
		industry::historical_profile::state_led);
	REQUIRE(state_led.state > 0.3f);

	auto const junker = industry::historical_initial_distribution(
		industry::historical_profile::junker_industrial);
	REQUIRE(junker.landed_elites > 0.25f);

	// Every profile still describes a whole industry.
	for(auto profile : {industry::historical_profile::liberal_private,
			industry::historical_profile::state_led,
			industry::historical_profile::foreign_concession,
			industry::historical_profile::junker_industrial,
			industry::historical_profile::cooperative}) {
		REQUIRE(total_of(industry::historical_initial_distribution(profile)) == Approx(1.f));
	}
	// An unassigned profile leaves industry private.
	REQUIRE(industry::historical_initial_distribution(
		industry::historical_profile::unassigned).capitalists == Approx(1.f));
}

TEST_CASE("profiles are looked up by country tag", "[economy][industry]") {
	// Deliberately the engine's own packing rather than a local copy: a byte
	// order that disagrees with nations::tag_to_int makes every lookup miss and
	// every country silently fall back to unassigned.
	auto const tag = [](char a, char b, char c) {
		return nations::tag_to_int(a, b, c);
	};
	REQUIRE(industry::profile_for_tag(tag('R', 'U', 'S'))
		== industry::historical_profile::state_led);
	REQUIRE(industry::profile_for_tag(tag('P', 'E', 'R'))
		== industry::historical_profile::foreign_concession);
	REQUIRE(industry::profile_for_tag(tag('P', 'R', 'U'))
		== industry::historical_profile::junker_industrial);
	REQUIRE(industry::profile_for_tag(tag('Z', 'Z', 'Z'))
		== industry::historical_profile::unassigned);
}

namespace transformation = politics::transformation;

TEST_CASE("foreign ownership costs a government legitimacy", "[economy][industry][politics]") {
	transformation::ruleset_config config;
	config.enabled = true;

	// A single dominant group so the coalition is unambiguous.
	std::vector<transformation::population_sample> samples;
	transformation::population_sample owners;
	owners.role = transformation::population_role::capital_owner;
	owners.population = 100'000.f;
	owners.savings_per_capita = 10.f;
	owners.income_security = 0.9f;
	owners.property_ownership = 1.f;
	owners.literacy = 0.9f;
	samples.push_back(owners);
	transformation::population_sample workers;
	workers.role = transformation::population_role::industrial_worker;
	workers.population = 900'000.f;
	workers.income_security = 0.7f;
	samples.push_back(workers);

	auto const domestic = transformation::evaluate_population(samples, config, 0, 0.f);
	auto const concession = transformation::evaluate_population(samples, config, 0, 1.f);
	REQUIRE(domestic.enabled);
	REQUIRE(concession.enabled);
	REQUIRE(domestic.legitimacy.foreign_ownership_penalty == Approx(0.f));
	REQUIRE(concession.legitimacy.foreign_ownership_penalty
		== Approx(config.maximum_foreign_ownership_penalty));
	// Owning the country from abroad is a legitimacy problem for whoever governs it.
	REQUIRE(concession.legitimacy.total < domestic.legitimacy.total);
	REQUIRE(concession.legitimacy.total >= 0.f);

	// Half foreign costs half the penalty, and the value stays bounded.
	auto const half = transformation::evaluate_population(samples, config, 0, 0.5f);
	REQUIRE(half.legitimacy.foreign_ownership_penalty
		== Approx(config.maximum_foreign_ownership_penalty * 0.5f));
	auto const absurd = transformation::evaluate_population(samples, config, 0, 40.f);
	REQUIRE(absurd.legitimacy.foreign_ownership_penalty
		== Approx(config.maximum_foreign_ownership_penalty));
}

TEST_CASE("classic legitimacy is unchanged by the new input",
		"[economy][industry][politics]") {
	transformation::ruleset_config config;
	config.enabled = true;
	std::vector<transformation::population_sample> samples;
	transformation::population_sample sample;
	sample.role = transformation::population_role::capital_owner;
	sample.population = 1'000.f;
	sample.property_ownership = 1.f;
	samples.push_back(sample);

	// The default argument is zero, so every existing caller keeps its result.
	auto const defaulted = transformation::evaluate_population(samples, config, 0);
	auto const explicit_zero = transformation::evaluate_population(samples, config, 0, 0.f);
	REQUIRE(defaulted.legitimacy.total == Approx(explicit_zero.legitimacy.total));
	REQUIRE(defaulted.legitimacy.foreign_ownership_penalty == Approx(0.f));
}

TEST_CASE("foreign capital buys in rather than being granted a share",
		"[economy][industry]") {
	auto config = open_market();
	config.foreign_investment_allowed = true;
	config.foreign_investment_rate = 0.01f;
	config.foreign_investment_target = 0.4f;
	std::array<industry::group_finance, industry::owner_group_count> finances{};

	industry::distribution start{1.f, 0.f, 0.f, 0.f, 0.f};
	auto const result = industry::clear_market(start, finances, 1'000'000.f, config);
	REQUIRE(result.after.foreign > 0.f);
	REQUIRE(result.after.capitalists < 1.f);
	// The local owners are paid for what they sold, and the buyers pay for it.
	REQUIRE(result.cash_delta[std::size_t(industry::owner_group::capitalists)] > 0.f);
	REQUIRE(result.cash_delta[std::size_t(industry::owner_group::foreign)] < 0.f);
	REQUIRE(total_of(result.after) == Approx(1.f));
}

TEST_CASE("foreign holdings stop at what was actually invested",
		"[economy][industry]") {
	auto config = open_market();
	config.foreign_investment_allowed = true;
	config.foreign_investment_rate = 0.5f;
	config.foreign_investment_target = 0.2f;
	std::array<industry::group_finance, industry::owner_group_count> finances{};

	// Already at the target: nothing more flows in, however fast the rate.
	industry::distribution at_target{0.8f, 0.f, 0.f, 0.2f, 0.f};
	auto const settled = industry::clear_market(at_target, finances, 1'000'000.f, config);
	REQUIRE(settled.after.foreign == Approx(0.2f).margin(0.001f));

	// Above the target it does not grow either.
	industry::distribution above{0.4f, 0.f, 0.f, 0.6f, 0.f};
	auto const excess = industry::clear_market(above, finances, 1'000'000.f, config);
	REQUIRE(excess.after.foreign <= Approx(0.6f));
}

TEST_CASE("a closed economy admits no foreign capital", "[economy][industry]") {
	auto config = open_market();
	config.foreign_investment_allowed = false;
	config.foreign_investment_rate = 0.5f;
	config.foreign_investment_target = 0.9f;
	std::array<industry::group_finance, industry::owner_group_count> finances{};

	industry::distribution start{1.f, 0.f, 0.f, 0.f, 0.f};
	auto const result = industry::clear_market(start, finances, 1'000'000.f, config);
	REQUIRE(result.after.foreign == Approx(0.f));
}
