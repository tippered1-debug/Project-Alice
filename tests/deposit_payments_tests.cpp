#include "catch.hpp"

#include "canonical_consumer_fixture.hpp"
#include "actors/ownership.hpp"
#include "economy/banking/banking.hpp"
#include "economy/liquidity.hpp"
#include "economy/wallets.hpp"
#include "economy/physical/concrete_market.hpp"
#include "economy/physical/exchange.hpp"
#include "economy/physical/inventory.hpp"

namespace deposit_payments_tests {

using account_ref = economy::exact_person_economy::account_ref;

// Base money: every operating and reserve account plus every exact account.
// Deposits are bank liabilities, not base money.
double base_money(sys::state const& state) {
	double total = 0.0;
	state.world.for_each_monetary_account([&](dcon::monetary_account_id account) {
		total += double(state.world.monetary_account_get_balance(account));
	});
	for(auto const& account : economy::exact_person_economy::export_snapshot(state).accounts) total += double(account.balance);
	return total;
}

struct fixture : canonical_consumer_tests::fixture {
	dcon::nation_id nation{};
	dcon::organization_id bank_a{};
	dcon::organization_id bank_b{};
	dcon::monetary_account_id reserve_a{};
	dcon::monetary_account_id reserve_b{};
	dcon::deposit_account_id employer_deposit{};

	dcon::organization_id make_bank(float reserves) {
		auto bank = economy::banking::create_bank(*state);
		economy::banking::bank_policy policy{};
		policy.jurisdiction = nation;
		policy.settlement = settlement;
		policy.minimum_capital_ratio = 0.08f;
		policy.liquidity_target = 0.05f;
		policy.risk_appetite = 1.0f;
		policy.max_single_borrower_exposure = 1.0f;
		policy.reserve_requirement = 0.05f;
		policy.capital_breach_grace_days = 30;
		REQUIRE(economy::banking::configure_bank_policy(*state, bank, policy));
		auto reserve = economy::banking::open_reserve_account(*state, bank, settlement);
		REQUIRE(economy::banking::bootstrap_set_reserve_balance(*state, reserve, reserves));
		economy::banking::update_bank_statuses(*state, state->current_date);
		return bank;
	}

	fixture() {
		nation = state->world.province_get_nation_from_province_ownership(province);
		bank_a = make_bank(1000.0f);
		bank_b = make_bank(1000.0f);
		reserve_a = economy::banking::reserve_account_for(*state, bank_a, settlement);
		reserve_b = economy::banking::reserve_account_for(*state, bank_b, settlement);
		actors::ownership::assign_runtime_canonical_id(*state, employer);
		employer_deposit = economy::banking::open_deposit_account(*state, bank_a, employer, settlement);
		REQUIRE(employer_deposit);
	}

	dcon::economic_actor_id make_company(float cash, dcon::monetary_account_id* account = nullptr) {
		auto company = actors::organizations::actor_for_organization(*state, actors::organizations::create_company(*state));
		actors::ownership::assign_runtime_canonical_id(*state, company);
		auto wallet = economy::accounts::open_account(*state, company, settlement);
		REQUIRE(economy::accounts::bootstrap_set_balance(*state, wallet, cash));
		if(account) *account = wallet;
		return company;
	}
};

} // namespace deposit_payments_tests

using deposit_payments_tests::account_ref;

TEST_CASE("depositing and withdrawing cash moves reserves, not just balances", "[economy][banking][deposits]") {
	deposit_payments_tests::fixture f;
	auto money = deposit_payments_tests::base_money(*f.state);
	auto worth = economy::banking::bank_balance_sheet(*f.state, f.bank_a, f.settlement).net_worth;
	REQUIRE(economy::banking::deposit_cash(*f.state, f.employer_deposit, account_ref::from_dcon(f.payer), 300.0f, f.state->current_date) == Approx(300.0f));
	REQUIRE(economy::accounts::balance(*f.state, f.payer) == Approx(9700.0f));
	REQUIRE(economy::accounts::balance(*f.state, f.reserve_a) == Approx(1300.0f));
	REQUIRE(economy::banking::deposit_balance(*f.state, f.employer_deposit) == Approx(300.0f));
	// The bank gained an asset and an equal liability.
	REQUIRE(economy::banking::bank_balance_sheet(*f.state, f.bank_a, f.settlement).net_worth == Approx(worth));
	REQUIRE(economy::banking::withdraw_cash(*f.state, f.employer_deposit, account_ref::from_dcon(f.payer), 100.0f, f.state->current_date) == Approx(100.0f));
	REQUIRE(economy::accounts::balance(*f.state, f.payer) == Approx(9800.0f));
	REQUIRE(economy::accounts::balance(*f.state, f.reserve_a) == Approx(1200.0f));
	REQUIRE(economy::banking::deposit_balance(*f.state, f.employer_deposit) == Approx(200.0f));
	REQUIRE(deposit_payments_tests::base_money(*f.state) == Approx(money));
	// Only the deposit's owner can move cash through it.
	dcon::monetary_account_id stranger_wallet{};
	(void)f.make_company(50.0f, &stranger_wallet);
	REQUIRE(economy::banking::deposit_cash(*f.state, f.employer_deposit, account_ref::from_dcon(stranger_wallet), 10.0f, f.state->current_date) == Approx(0.0f));
}

TEST_CASE("a bank pays out no more than its reserves, and a run leaves it constrained", "[economy][banking][deposits]") {
	deposit_payments_tests::fixture f;
	auto small = f.make_bank(50.0f);
	auto reserve = economy::banking::reserve_account_for(*f.state, small, f.settlement);
	dcon::monetary_account_id wallet{};
	auto depositor = f.make_company(0.0f, &wallet);
	auto deposit = economy::banking::open_deposit_account(*f.state, small, depositor, f.settlement);
	REQUIRE(economy::banking::bootstrap_set_deposit_balance(*f.state, deposit, 100.0f));
	auto money = deposit_payments_tests::base_money(*f.state);
	// The claim is 100 but the bank holds 50 in reserves.
	REQUIRE(economy::banking::withdraw_cash(*f.state, deposit, account_ref::from_dcon(wallet), 100.0f, f.state->current_date) == Approx(50.0f));
	REQUIRE(economy::accounts::balance(*f.state, wallet) == Approx(50.0f));
	REQUIRE(economy::accounts::balance(*f.state, reserve) == Approx(0.0f));
	REQUIRE(economy::banking::deposit_balance(*f.state, deposit) == Approx(50.0f));
	REQUIRE(economy::banking::status_of(*f.state, small) != economy::banking::bank_status::solvent);
	// Nothing more can be drawn until reserves return.
	REQUIRE(economy::banking::withdraw_cash(*f.state, deposit, account_ref::from_dcon(wallet), 10.0f, f.state->current_date) == Approx(0.0f));
	REQUIRE(deposit_payments_tests::base_money(*f.state) == Approx(money));
}

TEST_CASE("paying from a deposit settles inside a bank, between banks, or into a wallet", "[economy][banking][deposits]") {
	deposit_payments_tests::fixture f;
	REQUIRE(economy::banking::deposit_cash(*f.state, f.employer_deposit, account_ref::from_dcon(f.payer), 600.0f, f.state->current_date) == Approx(600.0f));
	dcon::monetary_account_id same_wallet{}, other_wallet{}, cash_wallet{};
	auto same = f.make_company(0.0f, &same_wallet);
	auto other = f.make_company(0.0f, &other_wallet);
	auto cash_only = f.make_company(0.0f, &cash_wallet);
	auto same_deposit = economy::banking::open_deposit_account(*f.state, f.bank_a, same, f.settlement);
	auto other_deposit = economy::banking::open_deposit_account(*f.state, f.bank_b, other, f.settlement);
	auto money = deposit_payments_tests::base_money(*f.state);

	REQUIRE(economy::banking::pay_from_deposit(*f.state, f.employer_deposit, same_deposit, {}, 100.0f,
		economy::relations::transaction_kind::purchase, f.state->current_date));
	REQUIRE(economy::banking::deposit_balance(*f.state, same_deposit) == Approx(100.0f));
	REQUIRE(economy::accounts::balance(*f.state, f.reserve_a) == Approx(1600.0f)); // reserves do not move

	REQUIRE(economy::banking::pay_from_deposit(*f.state, f.employer_deposit, other_deposit, {}, 100.0f,
		economy::relations::transaction_kind::purchase, f.state->current_date));
	REQUIRE(economy::banking::deposit_balance(*f.state, other_deposit) == Approx(100.0f));
	REQUIRE(economy::accounts::balance(*f.state, f.reserve_a) == Approx(1500.0f));
	REQUIRE(economy::accounts::balance(*f.state, f.reserve_b) == Approx(1100.0f));

	REQUIRE(economy::banking::pay_from_deposit(*f.state, f.employer_deposit, {}, account_ref::from_dcon(cash_wallet), 100.0f,
		economy::relations::transaction_kind::purchase, f.state->current_date));
	REQUIRE(economy::accounts::balance(*f.state, cash_wallet) == Approx(100.0f));
	REQUIRE(economy::accounts::balance(*f.state, f.reserve_a) == Approx(1400.0f));
	REQUIRE(economy::banking::deposit_balance(*f.state, f.employer_deposit) == Approx(300.0f));
	REQUIRE(deposit_payments_tests::base_money(*f.state) == Approx(money));
	// A payer cannot spend more than their deposit.
	REQUIRE_FALSE(economy::banking::pay_from_deposit(*f.state, f.employer_deposit, same_deposit, {}, 1000.0f,
		economy::relations::transaction_kind::purchase, f.state->current_date));
	(void)cash_only;
}

TEST_CASE("a buyer with an empty wallet pays at market from their deposit", "[economy][banking][deposits][market]") {
	deposit_payments_tests::fixture f;
	REQUIRE(economy::banking::deposit_cash(*f.state, f.employer_deposit, account_ref::from_dcon(f.payer), 500.0f, f.state->current_date) == Approx(500.0f));
	REQUIRE(economy::accounts::bootstrap_set_balance(*f.state, f.payer, 0.0f));
	auto bid = economy::physical::concrete_market::post_bid(*f.state, f.employer, f.payer, f.site, f.market,
		f.output, 10.0f, 10.0f, economy::physical::concrete_market::order_purpose::general);
	REQUIRE(bid);
	REQUIRE(f.state->world.concrete_market_bid_get_deposit_account_from_concrete_bid_deposit(bid) == f.employer_deposit);
	REQUIRE(economy::physical::concrete_market::reserved_deposit_amount(*f.state, f.employer_deposit) == Approx(100.0f));
	REQUIRE(economy::physical::concrete_market::reserved_bid_amount(*f.state, f.payer) == Approx(0.0f));
	// The seller has no deposit, so the bank pays its wallet out of reserves.
	dcon::monetary_account_id seller_wallet{};
	auto seller = f.make_company(0.0f, &seller_wallet);
	REQUIRE(economy::physical::inventory::add(*f.state, f.site, f.output, 10.0f, seller) == Approx(10.0f));
	REQUIRE(economy::physical::exchange::purchase_with_deposit(*f.state, f.site, f.output, seller, f.employer,
		f.employer_deposit, 10.0f, 10.0f, f.state->current_date));
	REQUIRE(economy::accounts::balance(*f.state, seller_wallet) == Approx(100.0f));
	REQUIRE(economy::banking::deposit_balance(*f.state, f.employer_deposit) == Approx(400.0f));
	REQUIRE(economy::accounts::balance(*f.state, f.reserve_a) == Approx(1400.0f));
	REQUIRE(economy::physical::inventory::quantity(*f.state, f.site, f.output, f.employer) == Approx(10.0f));
}

TEST_CASE("actors keep a share of liquid money as cash and bank the rest", "[economy][banking][liquidity]") {
	deposit_payments_tests::fixture f;
	REQUIRE(economy::accounts::bootstrap_set_balance(*f.state, f.payer, 1000.0f));
	economy::liquidity::manage(*f.state, f.employer, f.settlement);
	REQUIRE(economy::accounts::balance(*f.state, f.payer) == Approx(300.0f));
	REQUIRE(economy::banking::deposit_balance(*f.state, f.employer_deposit) == Approx(700.0f));
	// Spending drops cash below the floor; the deposit tops it back up.
	REQUIRE(economy::accounts::bootstrap_set_balance(*f.state, f.payer, 50.0f));
	economy::liquidity::manage(*f.state, f.employer, f.settlement);
	REQUIRE(economy::accounts::balance(*f.state, f.payer) == Approx(0.3f * 750.0f));
	REQUIRE(economy::banking::deposit_balance(*f.state, f.employer_deposit) == Approx(750.0f - 0.3f * 750.0f));
}

TEST_CASE("an actor with idle cash opens a deposit at a bank of its country", "[economy][banking][liquidity]") {
	deposit_payments_tests::fixture f;
	dcon::monetary_account_id wallet{};
	auto saver = f.make_company(100.0f, &wallet);
	REQUIRE(actors::organizations::transfer_factory_operator(*f.state,
		actors::organizations::organization_for_actor(*f.state, saver), f.factory));
	REQUIRE(economy::liquidity::nation_for(*f.state, saver) == f.nation);
	REQUIRE(economy::liquidity::bank_for(*f.state, f.nation, f.settlement));
	economy::liquidity::manage(*f.state, saver, f.settlement);
	auto deposit = economy::banking::deposit_account_for(*f.state, saver, f.settlement);
	REQUIRE(deposit);
	REQUIRE(economy::banking::deposit_balance(*f.state, deposit) == Approx(70.0f));
	REQUIRE(economy::accounts::balance(*f.state, wallet) == Approx(30.0f));
}

TEST_CASE("depositors of a troubled bank take out what it can pay", "[economy][banking][liquidity]") {
	deposit_payments_tests::fixture f;
	REQUIRE(economy::banking::deposit_cash(*f.state, f.employer_deposit, account_ref::from_dcon(f.payer), 800.0f, f.state->current_date) == Approx(800.0f));
	f.state->world.organization_set_bank_status(f.bank_a, uint8_t(economy::banking::bank_status::constrained));
	economy::liquidity::manage(*f.state, f.employer, f.settlement);
	REQUIRE(economy::banking::deposit_balance(*f.state, f.employer_deposit) == Approx(0.0f));
	REQUIRE(economy::accounts::balance(*f.state, f.payer) == Approx(10000.0f));
	REQUIRE(economy::accounts::balance(*f.state, f.reserve_a) == Approx(1000.0f));
}
