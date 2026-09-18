#include "catch.hpp"

#include "actors/organizations/organizations.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/payroll.hpp"
#include "economy/physical/concrete_labor.hpp"
#include "economy/physical/concrete_market.hpp"
#include "economy/physical/individual_consumption.hpp"
#include "economy/physical/inventory.hpp"
#include "persons/persons.hpp"

namespace individual_money_consumption_tests {
struct fixture {
	std::unique_ptr<sys::state> state = std::make_unique<sys::state>();
	dcon::province_id province{};
	dcon::state_instance_id zone{};
	dcon::market_id market{};
	dcon::commodity_id settlement{};
	dcon::commodity_id goods{};
	dcon::site_id home{};
	dcon::economic_actor_id seller{};
	dcon::monetary_account_id seller_account{};
	dcon::person_id person{};
	dcon::economic_actor_id person_actor{};
	dcon::monetary_account_id person_account{};

	fixture() {
		state->current_date = sys::date{0};
		province = state->world.create_province();
		zone = state->world.create_state_instance();
		market = state->world.create_market();
		state->world.state_instance_set_market_from_local_market(zone, market);
		state->world.market_set_zone_from_local_market(market, zone);
		state->world.province_set_state_membership(province, zone);
		settlement = state->world.create_commodity();
		goods = state->world.create_commodity();
		state->world.commodity_set_cost(goods, 10.0f);
		state->world.market_resize_price(state->world.commodity_size());
		state->world.market_set_price(market, goods, 10.0f);
		home = state->world.create_site();
		state->world.force_create_site_location(home, province);
		seller = state->world.create_economic_actor();
		seller_account = economy::accounts::open_account(*state, seller, settlement);
		REQUIRE(economy::accounts::bootstrap_set_balance(*state, seller_account, 0.0f));
		person = persons::create_person(*state, state->current_date);
		person_actor = persons::actor_for_person(*state, person);
		person_account = economy::accounts::open_account(*state, person_actor, settlement);
		REQUIRE(economy::accounts::bootstrap_set_balance(*state, person_account, 100.0f));
		REQUIRE(economy::physical::individual_consumption::set_home_site(*state, person, home));
	}

	dcon::person_id make_person(float balance) {
		auto result = persons::create_person(*state, state->current_date);
		auto actor = persons::actor_for_person(*state, result);
		auto account = economy::accounts::open_account(*state, actor, settlement);
		REQUIRE(economy::accounts::bootstrap_set_balance(*state, account, balance));
		REQUIRE(economy::physical::individual_consumption::set_home_site(*state, result, home));
		return result;
	}

	dcon::concrete_market_ask_id ask(float quantity = 1.0f, float price = 10.0f,
		dcon::economic_actor_id owner = dcon::economic_actor_id{}) {
		if(!owner) owner = seller;
		REQUIRE(economy::physical::inventory::add(*state, home, goods, quantity, owner) == Approx(quantity));
		return economy::physical::concrete_market::post_ask(*state, owner, home, market, goods,
			quantity, price, economy::physical::concrete_market::order_purpose::general);
	}

	dcon::concrete_market_bid_id bid_for(dcon::person_id who = dcon::person_id{}) const {
		if(!who) who = person;
		auto actor = persons::actor_for_person(*state, who);
		dcon::concrete_market_bid_id result{};
		state->world.for_each_concrete_market_bid([&](auto bid) {
			if(!result && state->world.concrete_market_bid_get_economic_actor_from_concrete_bid_buyer(bid) == actor)
				result = bid;
		});
		return result;
	}
};
}

TEST_CASE("individual money consumption uses exact account cash and reservations", "[economy][individual_consumption]") {
	individual_money_consumption_tests::fixture f;
	using namespace economy::physical::individual_consumption;
	REQUIRE(set_need(*f.state, f.person, f.goods, 1.0f));
	process_purchase_decisions(*f.state);
	auto bid = f.bid_for();
	REQUIRE(bid);
	REQUIRE(spending_account(*f.state, f.person) == f.person_account);
	REQUIRE(spendable_cash(*f.state, f.person) == Approx(90.0f));
	REQUIRE(f.state->world.concrete_market_bid_get_reserved_amount(bid) == Approx(10.0f));
	REQUIRE(f.state->world.concrete_market_bid_get_status(bid)
		== uint8_t(economy::physical::concrete_market::order_status::active));
	process_purchase_decisions(*f.state);
	REQUIRE(f.state->world.concrete_market_bid_size() == 1);
}

TEST_CASE("individual money consumption with zero cash creates no bid", "[economy][individual_consumption]") {
	individual_money_consumption_tests::fixture f;
	economy::accounts::bootstrap_set_balance(*f.state, f.person_account, 0.0f);
	REQUIRE(economy::physical::individual_consumption::set_need(*f.state, f.person, f.goods, 1.0f));
	economy::physical::individual_consumption::process_purchase_decisions(*f.state);
	REQUIRE(f.state->world.concrete_market_bid_size() == 0);
}

TEST_CASE("individual fill debits exact person account and transfers exact ownership", "[economy][individual_consumption]") {
	individual_money_consumption_tests::fixture f;
	REQUIRE(f.ask());
	REQUIRE(economy::physical::individual_consumption::set_need(*f.state, f.person, f.goods, 1.0f));
	economy::physical::individual_consumption::process_purchase_decisions(*f.state);
	auto bid = f.bid_for();
	REQUIRE(bid);
	REQUIRE(f.state->world.concrete_market_bid_get_economic_actor_from_concrete_bid_buyer(bid) == f.person_actor);
	REQUIRE(f.state->world.concrete_market_bid_get_monetary_account_from_concrete_bid_account(bid) == f.person_account);
	REQUIRE(economy::accounts::balance(*f.state, f.person_account) == Approx(90.0f));
	REQUIRE(economy::physical::inventory::quantity(*f.state, f.home, f.goods, f.person_actor) == Approx(1.0f));
	REQUIRE(economy::physical::inventory::quantity(*f.state, f.home, f.goods, f.seller) == Approx(0.0f));
	REQUIRE(economy::physical::individual_consumption::unmet_need(*f.state, f.person, f.goods) == Approx(0.0f));
}

TEST_CASE("individual consumption cannot consume another person's goods", "[economy][individual_consumption]") {
	individual_money_consumption_tests::fixture f;
	auto other = f.make_person(0.0f);
	auto other_actor = persons::actor_for_person(*f.state, other);
	REQUIRE(economy::physical::inventory::add(*f.state, f.home, f.goods, 2.0f, other_actor) == Approx(2.0f));
	REQUIRE(economy::physical::individual_consumption::set_need(*f.state, f.person, f.goods, 1.0f));
	REQUIRE(economy::physical::individual_consumption::owned_consumable_quantity(*f.state, f.person, f.goods) == Approx(0.0f));
	REQUIRE(economy::physical::individual_consumption::consume_owned_goods(*f.state, f.person, f.goods, 1.0f) == Approx(0.0f));
	REQUIRE(economy::physical::inventory::quantity(*f.state, f.home, f.goods, other_actor) == Approx(2.0f));
	REQUIRE(economy::physical::individual_consumption::unmet_need(*f.state, f.person, f.goods) == Approx(1.0f));
}

TEST_CASE("individual consumption removes only owned goods and retains unmet quantity", "[economy][individual_consumption]") {
	individual_money_consumption_tests::fixture f;
	REQUIRE(f.ask(1.0f));
	REQUIRE(economy::physical::individual_consumption::set_need(*f.state, f.person, f.goods, 2.0f));
	economy::physical::individual_consumption::process_purchase_decisions(*f.state);
	economy::physical::individual_consumption::process_consumption(*f.state);
	REQUIRE(economy::physical::individual_consumption::last_consumed_amount(*f.state, f.person, f.goods) == Approx(1.0f));
	REQUIRE(economy::physical::inventory::quantity(*f.state, f.home, f.goods, f.person_actor) == Approx(0.0f));
	REQUIRE(economy::physical::individual_consumption::unmet_need(*f.state, f.person, f.goods) == Approx(1.0f));
}

TEST_CASE("different concrete balances produce different bids under identical aggregate observations", "[economy][individual_consumption]") {
	individual_money_consumption_tests::fixture f;
	auto poorer = f.make_person(0.0f);
	REQUIRE(economy::physical::individual_consumption::set_need(*f.state, f.person, f.goods, 1.0f));
	REQUIRE(economy::physical::individual_consumption::set_need(*f.state, poorer, f.goods, 1.0f));
	for(auto probability : {0.0f, 1.0f}) f.state->world.market_set_expected_probability_to_buy(f.market, f.goods, probability);
	economy::physical::individual_consumption::process_purchase_decisions(*f.state);
	REQUIRE(f.bid_for(f.person));
	REQUIRE_FALSE(f.bid_for(poorer));
}

TEST_CASE("aggregate POP savings and needs observations do not change canonical person purchase", "[economy][individual_consumption]") {
	auto bid_quantity = [](float savings, float probability, float satisfaction) {
		individual_money_consumption_tests::fixture f;
		auto pop = f.state->world.create_pop();
		f.state->world.pop_set_savings(pop, savings);
		f.state->world.market_set_expected_probability_to_buy(f.market, f.goods, probability);
		auto pop_type = f.state->world.create_pop_type();
		f.state->world.market_set_satisfied_ratio_of_max_life_needs(f.market, pop_type, satisfaction);
		REQUIRE(economy::physical::individual_consumption::set_need(*f.state, f.person, f.goods, 1.0f));
		economy::physical::individual_consumption::process_purchase_decisions(*f.state);
		return f.state->world.concrete_market_bid_get_original_quantity(f.bid_for());
	};
	REQUIRE(bid_quantity(0.0f, 0.0f, 0.0f) == Approx(bid_quantity(999.0f, 1.0f, 1.0f)));
}

TEST_CASE("individual purchase and consumption are deterministic and create no synthetic goods", "[economy][individual_consumption]") {
	individual_money_consumption_tests::fixture f;
	REQUIRE(economy::physical::individual_consumption::set_need(*f.state, f.person, f.goods, 1.0f));
	auto persons_before = f.state->world.person_size();
	auto actors_before = f.state->world.economic_actor_size();
	auto stocks_before = f.state->world.physical_stock_size();
	auto transactions_before = f.state->world.transaction_size();
	economy::physical::individual_consumption::process(*f.state);
	REQUIRE(f.bid_for());
	REQUIRE(f.state->world.person_size() == persons_before);
	REQUIRE(f.state->world.economic_actor_size() == actors_before);
	REQUIRE(f.state->world.physical_stock_size() == stocks_before);
	REQUIRE(f.state->world.transaction_size() == transactions_before);
	REQUIRE(economy::physical::individual_consumption::last_consumed_amount(*f.state, f.person, f.goods) == Approx(0.0f));
}

TEST_CASE("identical concrete state produces identical individual bid", "[economy][individual_consumption]") {
	auto bid_quantity = [] {
		individual_money_consumption_tests::fixture f;
		REQUIRE(economy::physical::individual_consumption::set_need(*f.state, f.person, f.goods, 1.0f));
		economy::physical::individual_consumption::process_purchase_decisions(*f.state);
		return f.state->world.concrete_market_bid_get_original_quantity(f.bid_for());
	};
	REQUIRE(bid_quantity() == Approx(bid_quantity()));
}

TEST_CASE("wage to individual purchase ownership and consumption closes without POP savings", "[economy][individual_consumption][economy][labor]") {
	individual_concrete_labor_tests::fixture f;
	auto worker = f.person();
	auto worker_actor = persons::actor_for_person(*f.state, worker);
	auto worker_account = economy::accounts::open_account(*f.state, worker_actor, f.settlement);
	economy::accounts::bootstrap_set_balance(*f.state, worker_account, 0.0f);
	REQUIRE(economy::physical::individual_consumption::set_home_site(*f.state, worker, f.site));
	auto contract = economy::physical::concrete_labor::create_employment_contract(*f.state, worker, f.employer,
		f.factory, f.site, 0, 1.0f, 100.0f, 1, f.payer, worker_account, f.state->current_date);
	REQUIRE(contract);
	economy::payroll::settle_factory(*f.state, f.factory, 1.0f, 1.0f);
	REQUIRE(economy::accounts::balance(*f.state, worker_account) == Approx(100.0f));
	auto seller = f.state->world.create_economic_actor();
	auto seller_account = economy::accounts::open_account(*f.state, seller, f.settlement);
	economy::accounts::bootstrap_set_balance(*f.state, seller_account, 0.0f);
	REQUIRE(economy::physical::inventory::add(*f.state, f.site, f.output, 1.0f, seller) == Approx(1.0f));
	REQUIRE(economy::physical::concrete_market::post_ask(*f.state, seller, f.site, f.market, f.output,
		1.0f, 10.0f, economy::physical::concrete_market::order_purpose::general));
	REQUIRE(economy::physical::individual_consumption::set_need(*f.state, worker, f.output, 1.0f));
	economy::physical::individual_consumption::process_purchase_decisions(*f.state);
	REQUIRE(economy::accounts::balance(*f.state, worker_account) == Approx(90.0f));
	REQUIRE(economy::physical::inventory::quantity(*f.state, f.site, f.output, worker_actor) == Approx(1.0f));
	economy::physical::individual_consumption::process_consumption(*f.state);
	REQUIRE(economy::physical::inventory::quantity(*f.state, f.site, f.output, worker_actor) == Approx(0.0f));
	REQUIRE(economy::physical::individual_consumption::last_consumed_amount(*f.state, worker, f.output) == Approx(1.0f));
	REQUIRE(economy::physical::individual_consumption::unmet_need(*f.state, worker, f.output) == Approx(0.0f));
}
