#include "catch.hpp"

#include "canonical_consumer_fixture.hpp"
#include "economy/banking/banking.hpp"
#include "economy/monetary_policy.hpp"
#include "economy/relations/relations.hpp"

namespace monetary_policy_tests {

using economy::monetary_policy::issued_money;

dcon::organization_id make_bank(capital_allocation_tests::fixture& f, float reserves, float liquidity_target) {
	auto bank = economy::banking::create_bank(*f.state);
	economy::banking::bank_policy policy{};
	policy.jurisdiction = f.nation;
	policy.settlement = f.settlement;
	policy.lending_base_rate = 0.03f;
	policy.minimum_capital_ratio = 0.08f;
	policy.liquidity_target = liquidity_target;
	policy.risk_appetite = 1.0f;
	policy.max_single_borrower_exposure = 1.0f;
	policy.reserve_requirement = 0.05f;
	policy.capital_breach_grace_days = 30;
	REQUIRE(economy::banking::configure_bank_policy(*f.state, bank, policy));
	actors::ownership::assign_runtime_canonical_id(*f.state, bank);
	auto reserve = economy::banking::open_reserve_account(*f.state, bank, f.settlement);
	REQUIRE(economy::banking::bootstrap_set_reserve_balance(*f.state, reserve, reserves));
	economy::banking::update_bank_statuses(*f.state, f.state->current_date);
	return bank;
}

dcon::deposit_account_id deposit_at(capital_allocation_tests::fixture& f, dcon::organization_id bank, float amount) {
	auto owner = f.company(0.0f);
	auto deposit = economy::banking::open_deposit_account(*f.state, bank, owner, f.settlement);
	REQUIRE(deposit);
	REQUIRE(economy::banking::bootstrap_set_deposit_balance(*f.state, deposit, amount));
	return deposit;
}

// A loan of `amount` to the fixture's plant operator, paid out of the bank's reserves.
dcon::obligation_id lend_to_plant(capital_allocation_tests::fixture& f, dcon::organization_id bank, float amount) {
	f.state->world.organization_set_bank_risk_appetite(f.bank, 0.0f); // only `bank` may lend
	auto credit = economy::banking::underwrite_factory_credit(*f.state, f.factory, f.payer, 0, amount, 0.5f, 10000.0f);
	REQUIRE(credit.funded_amount == Approx(amount));
	REQUIRE(f.state->world.firm_capital_request_get_organization_from_firm_capital_request_bank(credit.request) == bank);
	return credit.obligation;
}

// A trade on the fixture's market at `price` yesterday sets the reference price.
void trade(capital_allocation_tests::fixture& f, dcon::commodity_id commodity, float price) {
	auto bid = f.state->world.create_concrete_market_bid();
	f.state->world.concrete_market_bid_set_original_quantity(bid, 1.0f);
	f.state->world.concrete_market_bid_set_created_on(bid, f.state->current_date - 1);
	f.state->world.concrete_market_bid_set_status(bid, uint8_t(economy::physical::concrete_market::order_status::filled));
	f.state->world.force_create_concrete_bid_market(bid, f.market);
	f.state->world.force_create_concrete_bid_commodity(bid, commodity);
	auto fill = f.state->world.create_concrete_trade_fill();
	f.state->world.concrete_trade_fill_set_quantity(fill, 1.0f);
	f.state->world.concrete_trade_fill_set_execution_price(fill, price);
	f.state->world.concrete_trade_fill_set_occurred_on(fill, f.state->current_date - 1);
	f.state->world.force_create_concrete_fill_bid(fill, bid);
}

} // namespace monetary_policy_tests

using monetary_policy_tests::issued_money;

TEST_CASE("the policy rate follows inflation and sets what banks charge", "[economy][monetary-policy]") {
	capital_allocation_tests::fixture f;
	f.demand(f.output, 5.0f);
	auto central_bank = economy::monetary_policy::open_central_bank(*f.state, f.nation, f.settlement);
	REQUIRE(central_bank);
	REQUIRE(economy::monetary_policy::open_central_bank(*f.state, f.nation, f.settlement) == central_bank);
	// It starts from the rate the nation's banks were authored with.
	REQUIRE(economy::monetary_policy::policy_rate(*f.state, central_bank) == Approx(0.03f));
	REQUIRE(economy::monetary_policy::price_index(*f.state, f.nation) == Approx(1.0f));

	// Stable prices: the rate eases toward the rule's 1% (neutral 2% plus a response below target).
	economy::monetary_policy::review(*f.state, central_bank);
	REQUIRE(economy::monetary_policy::policy_rate(*f.state, central_bank) == Approx(0.025f));

	// Prices up 10% in a month: the rate rises, and the bank lends at the policy
	// rate plus its liquidity premium (none: it holds reserves and no deposits).
	monetary_policy_tests::trade(f, f.output, 11.0f);
	REQUIRE(economy::monetary_policy::price_index(*f.state, f.nation) == Approx(1.1f));
	economy::monetary_policy::review(*f.state, central_bank);
	auto rate = economy::monetary_policy::policy_rate(*f.state, central_bank);
	REQUIRE(rate > 0.025f);
	REQUIRE(f.state->world.organization_get_central_bank_inflation(central_bank) > 0.0f);
	REQUIRE(f.state->world.organization_get_lending_base_rate(f.bank) == Approx(rate));
}

TEST_CASE("a bank with more reserves behind its deposits lends cheaper", "[economy][monetary-policy]") {
	capital_allocation_tests::fixture f;
	auto rich = monetary_policy_tests::make_bank(f, 2000.0f, 0.05f);
	auto thin = monetary_policy_tests::make_bank(f, 500.0f, 0.05f);
	(void)monetary_policy_tests::deposit_at(f, rich, 10000.0f);
	(void)monetary_policy_tests::deposit_at(f, thin, 10000.0f);
	// Requirement 500 each: four times covered pays nothing, exactly covered pays the full premium.
	REQUIRE(economy::monetary_policy::liquidity_premium(*f.state, rich) == Approx(0.0f));
	REQUIRE(economy::monetary_policy::liquidity_premium(*f.state, thin) == Approx(0.015f));
	auto central_bank = economy::monetary_policy::open_central_bank(*f.state, f.nation, f.settlement);
	economy::monetary_policy::review(*f.state, central_bank);
	REQUIRE(f.state->world.organization_get_lending_base_rate(thin)
		== Approx(f.state->world.organization_get_lending_base_rate(rich) + 0.015f));
}

TEST_CASE("depositors earn a share of what the bank's loans earn", "[economy][monetary-policy]") {
	capital_allocation_tests::fixture f;
	auto bank = monetary_policy_tests::make_bank(f, 5000.0f, 0.05f);
	(void)monetary_policy_tests::lend_to_plant(f, bank, 1000.0f);
	auto deposit = monetary_policy_tests::deposit_at(f, bank, 2000.0f);
	// 80% of 3% on 1000 spread over 2000 of deposits.
	REQUIRE(economy::monetary_policy::deposit_rate(*f.state, bank) == Approx(0.012f));
	auto money = capital_allocation_tests::base_money(*f.state);
	auto worth = economy::banking::bank_balance_sheet(*f.state, bank, f.settlement).net_worth;
	auto interest = economy::monetary_policy::pay_deposit_interest(*f.state, deposit);
	REQUIRE(interest == Approx(2000.0f * 0.012f * 30.0f / 365.0f));
	REQUIRE(economy::banking::deposit_balance(*f.state, deposit) == Approx(2000.0f + interest));
	REQUIRE(economy::banking::bank_balance_sheet(*f.state, bank, f.settlement).net_worth == Approx(worth - interest));
	// Interest is a bank liability, not new base money.
	REQUIRE(capital_allocation_tests::base_money(*f.state) == Approx(money));
}

TEST_CASE("the central bank lends reserves to an illiquid solvent bank and retires them on repayment", "[economy][monetary-policy]") {
	capital_allocation_tests::fixture f;
	// Reserves of 100 against a liquidity requirement of 200, with a good loan book.
	auto bank = monetary_policy_tests::make_bank(f, 1100.0f, 0.2f);
	(void)monetary_policy_tests::lend_to_plant(f, bank, 1000.0f);
	(void)monetary_policy_tests::deposit_at(f, bank, 1000.0f);
	auto reserve = economy::banking::reserve_account_for(*f.state, bank, f.settlement);
	REQUIRE(economy::accounts::balance(*f.state, reserve) == Approx(100.0f));
	auto unlent = capital_allocation_tests::base_money(*f.state);
	REQUIRE(economy::monetary_policy::lend_reserves(*f.state, bank) == Approx(100.0f));
	auto central_bank = economy::monetary_policy::central_bank_for(*f.state, f.nation, f.settlement);
	REQUIRE(central_bank);
	REQUIRE(economy::accounts::balance(*f.state, reserve) == Approx(200.0f));
	// The reserves are new base money, and all of it is accounted for as issued.
	REQUIRE(issued_money(*f.state, central_bank) == Approx(100.0f));
	REQUIRE(capital_allocation_tests::base_money(*f.state) - issued_money(*f.state, central_bank) == Approx(unlent));
	REQUIRE(economy::banking::status_of(*f.state, bank) == economy::banking::bank_status::solvent);
	// Nothing more is lent while it holds its requirement.
	REQUIRE(economy::monetary_policy::lend_reserves(*f.state, bank) == Approx(0.0f));

	// Reserves come back; ten days later the bank repays with interest.
	REQUIRE(economy::banking::bootstrap_set_reserve_balance(*f.state, reserve, 600.0f));
	auto before = capital_allocation_tests::base_money(*f.state) - issued_money(*f.state, central_bank);
	f.state->current_date = f.state->current_date + 10;
	auto repaid = economy::monetary_policy::repay_reserves(*f.state, bank);
	REQUIRE(repaid > 100.0f);
	REQUIRE(issued_money(*f.state, central_bank) == Approx(0.0f).margin(1.0e-3));
	// Retiring the principal destroys exactly the money that was issued.
	REQUIRE(capital_allocation_tests::base_money(*f.state) - issued_money(*f.state, central_bank) == Approx(before));
	auto income = economy::accounts::find_account(*f.state,
		actors::organizations::actor_for_organization(*f.state, central_bank), f.settlement);
	REQUIRE(economy::accounts::balance(*f.state, income) == Approx(repaid - 100.0f));
}

TEST_CASE("the central bank does not lend to an insolvent bank or without collateral", "[economy][monetary-policy]") {
	capital_allocation_tests::fixture f;
	// No loans to pledge.
	auto unsecured = monetary_policy_tests::make_bank(f, 100.0f, 0.2f);
	(void)monetary_policy_tests::deposit_at(f, unsecured, 50.0f);
	f.state->world.organization_set_bank_liquidity_target(unsecured, 4.0f);
	REQUIRE(economy::monetary_policy::lend_reserves(*f.state, unsecured) == Approx(0.0f));
	// Negative net worth.
	auto broken = monetary_policy_tests::make_bank(f, 1100.0f, 0.2f);
	(void)monetary_policy_tests::lend_to_plant(f, broken, 1000.0f);
	(void)monetary_policy_tests::deposit_at(f, broken, 5000.0f);
	REQUIRE(economy::monetary_policy::lend_reserves(*f.state, broken) == Approx(0.0f));
}
