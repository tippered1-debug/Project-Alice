#include "catch.hpp"

#include "economy/accounts/accounts.hpp"
#include "economy/exact_person_economy.hpp"
#include "economy/physical/concrete_market.hpp"
#include "economy/physical/exact_person_goods.hpp"
#include "economy/physical/individual_consumption.hpp"
#include "economy/physical/inventory.hpp"
#include "persons/persons.hpp"

namespace individual_money_consumption_tests {
using person_key = persons::person_key;

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
	person_key key{};
	economy::exact_person_economy::account_ref person_account{};

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
		key = persons::canonical_key(*state, person);
		REQUIRE(key.source_population_cell != 0);
		REQUIRE(economy::physical::individual_consumption::set_home_site(*state, person, home));
		person_account = economy::exact_person_economy::open_account(*state, key, settlement);
		REQUIRE(person_account);
		REQUIRE(economy::exact_person_economy::set_balance(*state, person_account, 100.0f));
	}

	std::pair<dcon::person_id, person_key> make_person(float balance) {
		auto id = persons::create_person(*state, state->current_date);
		auto person = persons::canonical_key(*state, id);
		REQUIRE(person.source_population_cell != 0);
		REQUIRE(economy::physical::individual_consumption::set_home_site(*state, id, home));
		auto account = economy::exact_person_economy::open_account(*state, person, settlement);
		REQUIRE(account);
		REQUIRE(economy::exact_person_economy::set_balance(*state, account, balance));
		return {id, person};
	}

	dcon::concrete_market_ask_id ask(float quantity = 1.0f, float price = 10.0f,
		dcon::economic_actor_id owner = dcon::economic_actor_id{}) {
		if(!owner) owner = seller;
		REQUIRE(economy::physical::inventory::add(*state, home, goods, quantity, owner) == Approx(quantity));
		return economy::physical::concrete_market::post_ask(*state, owner, home, market,
			goods, quantity, price, economy::physical::concrete_market::order_purpose::general);
	}

	std::optional<economy::physical::exact_person_goods::bid_record> bid_for(person_key who = {}) const {
		if(who.source_population_cell == 0) who = key;
		for(auto const& bid : economy::physical::exact_person_goods::export_snapshot(*state).bids)
			if(bid.buyer == who) return bid;
		return std::nullopt;
	}
};
}

TEST_CASE("individual consumption is a facade over exact-person cash and reservations", "[economy][household]") {
	individual_money_consumption_tests::fixture f;
	using namespace economy::physical::individual_consumption;
	REQUIRE(f.ask(1.0f, 20.0f));
	REQUIRE(set_need(*f.state, f.person, f.goods, 1.0f));
	process_purchase_decisions(*f.state);
	auto bid = f.bid_for();
	REQUIRE(bid);
	REQUIRE(spending_account(*f.state, f.person, f.settlement) == f.person_account);
	REQUIRE(spendable_cash(*f.state, f.person, f.settlement) == Approx(90.0f));
	REQUIRE(bid->reserved_amount == Approx(10.0f));
	REQUIRE(bid->status == economy::physical::exact_person_goods::order_status::active);
	process_purchase_decisions(*f.state);
	REQUIRE(economy::physical::exact_person_goods::bid_count(*f.state) == 1);
	REQUIRE(f.state->world.person_commodity_need_size() == 0);
}

TEST_CASE("zero exact-person cash creates no consumer bid", "[economy][household]") {
	individual_money_consumption_tests::fixture f;
	REQUIRE(economy::exact_person_economy::set_balance(*f.state, f.person_account, 0.0f));
	REQUIRE(f.ask());
	REQUIRE(economy::physical::individual_consumption::set_need(*f.state, f.person, f.goods, 1.0f));
	economy::physical::individual_consumption::process_purchase_decisions(*f.state);
	REQUIRE(economy::physical::exact_person_goods::bid_count(*f.state) == 0);
}

TEST_CASE("a concrete fill transfers money and physical ownership to one person", "[economy][household]") {
	individual_money_consumption_tests::fixture f;
	REQUIRE(f.ask());
	REQUIRE(economy::physical::individual_consumption::set_need(*f.state, f.person, f.goods, 1.0f));
	economy::physical::individual_consumption::process_purchase_decisions(*f.state);
	REQUIRE(economy::exact_person_economy::balance(*f.state, f.person_account) == Approx(90.0f));
	REQUIRE(economy::accounts::balance(*f.state, f.seller_account) == Approx(10.0f));
	REQUIRE(economy::physical::exact_person_goods::stock_quantity(*f.state, f.key, f.home, f.goods) == Approx(1.0f));
	REQUIRE(economy::physical::inventory::quantity(*f.state, f.home, f.goods, f.seller) == Approx(0.0f));
	REQUIRE(economy::physical::individual_consumption::unmet_need(*f.state, f.person, f.goods) == Approx(0.0f));
	REQUIRE(economy::physical::exact_person_goods::fill_count(*f.state) == 1);
}

TEST_CASE("a person cannot consume another person's stock", "[economy][household]") {
	individual_money_consumption_tests::fixture f;
	auto [other_id, other] = f.make_person(0.0f);
	(void)other_id;
	REQUIRE(economy::physical::exact_person_goods::add_stock(*f.state, other, f.home, f.goods, 2.0f) == Approx(2.0f));
	REQUIRE(economy::physical::individual_consumption::set_need(*f.state, f.person, f.goods, 1.0f));
	REQUIRE(economy::physical::individual_consumption::owned_consumable_quantity(*f.state, f.person, f.goods) == Approx(0.0f));
	REQUIRE(economy::physical::individual_consumption::consume_owned_goods(*f.state, f.person, f.goods, 1.0f) == Approx(0.0f));
	REQUIRE(economy::physical::exact_person_goods::stock_quantity(*f.state, other, f.home, f.goods) == Approx(2.0f));
	REQUIRE(economy::physical::individual_consumption::unmet_need(*f.state, f.person, f.goods) == Approx(1.0f));
}

TEST_CASE("consumption removes only delivered stock and records unmet quantity", "[economy][household]") {
	individual_money_consumption_tests::fixture f;
	REQUIRE(f.ask(1.0f));
	REQUIRE(economy::physical::individual_consumption::set_need(*f.state, f.person, f.goods, 2.0f));
	economy::physical::individual_consumption::process_purchase_decisions(*f.state);
	economy::physical::individual_consumption::process_consumption(*f.state);
	REQUIRE(economy::physical::individual_consumption::last_consumed_amount(*f.state, f.person, f.goods) == Approx(1.0f));
	REQUIRE(economy::physical::exact_person_goods::stock_quantity(*f.state, f.key, f.home, f.goods) == Approx(0.0f));
	REQUIRE(economy::physical::individual_consumption::unmet_need(*f.state, f.person, f.goods) == Approx(1.0f));
}

TEST_CASE("only the person with available money places a bid", "[economy][household]") {
	individual_money_consumption_tests::fixture f;
	auto [poorer_id, poorer] = f.make_person(0.0f);
	(void)poorer_id;
	REQUIRE(f.ask(1.0f, 20.0f));
	REQUIRE(economy::physical::individual_consumption::set_need(*f.state, f.person, f.goods, 1.0f));
	REQUIRE(economy::physical::individual_consumption::set_need(*f.state, poorer_id, f.goods, 1.0f));
	for(auto probability : {0.0f, 1.0f})
		f.state->world.market_set_expected_probability_to_buy(f.market, f.goods, probability);
	economy::physical::individual_consumption::process_purchase_decisions(*f.state);
	REQUIRE(f.bid_for(f.key));
	REQUIRE_FALSE(f.bid_for(poorer));
}

TEST_CASE("legacy POP cash and need observations do not change a canonical bid", "[economy][household]") {
	auto bid_quantity = [](float savings, float probability, float satisfaction) {
		individual_money_consumption_tests::fixture f;
		auto pop = f.state->world.create_pop();
		f.state->world.pop_set_savings(pop, savings);
		f.state->world.market_set_expected_probability_to_buy(f.market, f.goods, probability);
		auto pop_type = f.state->world.create_pop_type();
		f.state->world.market_set_satisfied_ratio_of_max_life_needs(f.market, pop_type, satisfaction);
		REQUIRE(f.ask(1.0f, 20.0f));
		REQUIRE(economy::physical::individual_consumption::set_need(*f.state, f.person, f.goods, 1.0f));
		economy::physical::individual_consumption::process_purchase_decisions(*f.state);
		auto bid = f.bid_for();
		REQUIRE(bid);
		return bid->original_quantity;
	};
	REQUIRE(bid_quantity(0.0f, 0.0f, 0.0f) == Approx(bid_quantity(999.0f, 1.0f, 1.0f)));
}

TEST_CASE("canonical consumer processing creates no synthetic goods or DCON needs", "[economy][household]") {
	individual_money_consumption_tests::fixture f;
	REQUIRE(f.ask(1.0f, 20.0f));
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
	REQUIRE(f.state->world.person_commodity_need_size() == 0);
	REQUIRE(economy::physical::individual_consumption::last_consumed_amount(*f.state, f.person, f.goods) == Approx(0.0f));
}

TEST_CASE("same canonical state produces the same exact-person bid", "[economy][household]") {
	auto bid_quantity = [] {
		individual_money_consumption_tests::fixture f;
		REQUIRE(f.ask(1.0f, 20.0f));
		REQUIRE(economy::physical::individual_consumption::set_need(*f.state, f.person, f.goods, 1.0f));
		economy::physical::individual_consumption::process_purchase_decisions(*f.state);
		return f.bid_for()->original_quantity;
	};
	REQUIRE(bid_quantity() == Approx(bid_quantity()));
}

TEST_CASE("period needs count only the person's physical stock", "[economy][household]") {
	individual_money_consumption_tests::fixture f;
	using namespace economy::physical::individual_consumption;
	REQUIRE(economy::physical::exact_person_goods::add_stock(*f.state, f.key, f.home, f.goods, 1.0f) == Approx(1.0f));
	REQUIRE(set_need(*f.state, f.person, f.goods, 1.0f));
	REQUIRE(consume_owned_goods(*f.state, f.person, f.goods, 1.0f) == Approx(1.0f));
	REQUIRE(unmet_need(*f.state, f.person, f.goods) == Approx(0.0f));
	process_consumption(*f.state);
	REQUIRE(unmet_need(*f.state, f.person, f.goods) == Approx(0.0f));
	f.state->current_date += 1;
	begin_period(*f.state, f.state->current_date);
	REQUIRE(unmet_need(*f.state, f.person, f.goods) == Approx(1.0f));
}

TEST_CASE("consumer uses the account settlement accepted by the seller", "[economy][household]") {
	individual_money_consumption_tests::fixture f;
	using namespace economy::physical::individual_consumption;
	auto other_settlement = f.state->world.create_commodity();
	auto other_account = economy::exact_person_economy::open_account(*f.state, f.key, other_settlement);
	REQUIRE(economy::exact_person_economy::set_balance(*f.state, f.person_account, 0.0f));
	REQUIRE(economy::exact_person_economy::set_balance(*f.state, other_account, 1000.0f));
	REQUIRE(f.ask());
	REQUIRE(set_need(*f.state, f.person, f.goods, 1.0f));
	REQUIRE(spending_account(*f.state, f.person, f.settlement) == f.person_account);
	REQUIRE(spending_account(*f.state, f.person, other_settlement) == other_account);
	process_purchase_decisions(*f.state);
	REQUIRE_FALSE(f.bid_for());
	REQUIRE(economy::exact_person_economy::set_balance(*f.state, f.person_account, 100.0f));
	process_purchase_decisions(*f.state);
	REQUIRE(f.bid_for());
	REQUIRE(economy::exact_person_economy::balance(*f.state, other_account) == Approx(1000.0f));
	REQUIRE(economy::exact_person_economy::balance(*f.state, f.person_account) == Approx(90.0f));
}
