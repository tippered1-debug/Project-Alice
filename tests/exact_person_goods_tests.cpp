#include "catch.hpp"

#include "economy/exact_person_economy.hpp"
#include "economy/physical/concrete_market.hpp"
#include "economy/physical/exact_person_freight.hpp"
#include "economy/physical/exact_person_goods.hpp"
#include "economy/physical/inventory.hpp"
#include "economy/physical/job_market.hpp"
#include "economy/payroll.hpp"

namespace exact_person_goods_tests {

using person_key = persons::exact_population::person_key;

person_key worker(individual_concrete_labor_tests::fixture& f) {
	return exact_person_economy_tests::register_anchor(f);
}

}

TEST_CASE("exact consumption is capped by remaining period need", "[economy][exact][consumption]") {
	individual_concrete_labor_tests::fixture f;
	auto key = exact_person_goods_tests::worker(f);
	f.state->world.commodity_set_cost(f.output, 10.0f);
	REQUIRE(economy::physical::exact_person_goods::set_need(*f.state, key, f.output, 1.0f));
	REQUIRE(economy::physical::exact_person_goods::add_stock(*f.state, key, f.site, f.output, 3.0f) == Approx(3.0f));
	REQUIRE(economy::physical::exact_person_goods::process_consumption(*f.state, key, f.output) == Approx(1.0f));
	REQUIRE(economy::physical::exact_person_goods::process_consumption(*f.state, key, f.output) == Approx(0.0f));
	REQUIRE(economy::physical::exact_person_goods::consumed_this_period(*f.state, key, f.output) == Approx(1.0f));
	REQUIRE(economy::physical::exact_person_goods::stock_quantity(*f.state, key, f.site, f.output) == Approx(2.0f));
	REQUIRE(economy::physical::exact_person_goods::unmet_need(*f.state, key, f.output) == Approx(0.0f));
	f.state->current_date += 1;
	economy::physical::exact_person_goods::begin_period(*f.state, f.state->current_date);
	REQUIRE(economy::physical::exact_person_goods::consumed_this_period(*f.state, key, f.output) == Approx(0.0f));
	REQUIRE(economy::physical::exact_person_goods::unmet_need(*f.state, key, f.output) == Approx(0.0f));
}

TEST_CASE("exact remote asks create source stock and pending freight", "[economy][exact][goods][freight]") {
	individual_concrete_labor_tests::fixture f;
	auto key = exact_person_goods_tests::worker(f);
	f.state->world.commodity_set_cost(f.output, 2.0f);
	auto remote = f.state->world.create_site();
	// The remote site is deliberately valid but has no relation to the buyer home.
	auto seller = f.state->world.create_economic_actor();
	auto seller_account = economy::accounts::open_account(*f.state, seller, f.settlement);
	REQUIRE(economy::accounts::bootstrap_set_balance(*f.state, seller_account, 0.0f));
	REQUIRE(economy::physical::inventory::add(*f.state, remote, f.output, 1.0f, seller) == Approx(1.0f));
	auto ask = economy::physical::concrete_market::post_ask(*f.state, seller, remote, f.market, f.output, 1.0f, 2.0f, {});
	REQUIRE(ask);
	auto account = economy::exact_person_economy::open_account(*f.state, key, f.settlement);
	REQUIRE(economy::exact_person_economy::set_balance(*f.state, account, 10.0f));
	REQUIRE(economy::physical::exact_person_goods::set_need(*f.state, key, f.output, 1.0f));
	REQUIRE(economy::physical::exact_person_goods::post_bid(*f.state, key, account, f.site, f.market, f.output, 1.0f, 2.0f));
	REQUIRE(economy::physical::concrete_market::match(*f.state, f.market, f.output, f.state->current_date).empty());
	REQUIRE(economy::exact_person_economy::balance(*f.state, account) == Approx(8.0f));
	REQUIRE(economy::accounts::balance(*f.state, seller_account) == Approx(2.0f));
	REQUIRE(economy::physical::inventory::quantity(*f.state, remote, f.output, seller) == Approx(0.0f));
	REQUIRE(economy::physical::exact_person_goods::stock_quantity(*f.state, key, remote, f.output)
		== Approx(1.0f));
	REQUIRE(economy::physical::exact_person_freight::request_count(*f.state) == 1);
	REQUIRE(economy::physical::exact_person_goods::fill_count(*f.state) == 1);
}

TEST_CASE("exact worker wage purchases and consumes real seller goods", "[economy][exact][goods][causal]") {
	individual_concrete_labor_tests::fixture f;
	auto key = exact_person_goods_tests::worker(f);
	f.state->world.commodity_set_cost(f.output, 10.0f);
	auto before_persons = f.state->world.person_size();
	auto before_actors = f.state->world.economic_actor_size();
	auto before_accounts = f.state->world.monetary_account_size();
	auto before_stocks = f.state->world.physical_stock_size();
	auto before_transactions = f.state->world.transaction_size();
	auto offer = f.offer(1, 10.0f);
	REQUIRE(economy::exact_person_economy::submit_application(*f.state, key, offer, f.state->current_date));
	economy::physical::job_market::process_pending_applications(*f.state);
	economy::payroll::settle_factory(*f.state, f.factory, 1.0f, 1.0f);
	auto contract_id = economy::exact_person_economy::active_contracts_for_person(*f.state, key).front();
	auto contract = economy::exact_person_economy::contract(*f.state, contract_id);
	REQUIRE(contract);
	auto worker_account = economy::exact_person_economy::account_ref::from_exact(contract->worker_account_id);
	REQUIRE(economy::exact_person_economy::balance(*f.state, worker_account) == Approx(10.0f));

	auto seller = f.state->world.create_economic_actor();
	auto seller_account = economy::accounts::open_account(*f.state, seller, f.settlement);
	REQUIRE(economy::accounts::bootstrap_set_balance(*f.state, seller_account, 0.0f));
	REQUIRE(economy::physical::inventory::add(*f.state, f.site, f.output, 1.0f, seller) == Approx(1.0f));
	REQUIRE(economy::physical::concrete_market::post_ask(*f.state, seller, f.site, f.market, f.output, 1.0f, 10.0f, {}));
	REQUIRE(economy::physical::exact_person_goods::set_need(*f.state, key, f.output, 1.0f));
	REQUIRE(economy::physical::exact_person_goods::process_purchase_decision(*f.state, key, f.output));
	REQUIRE(economy::exact_person_economy::balance(*f.state, worker_account) == Approx(0.0f));
	REQUIRE(economy::accounts::balance(*f.state, seller_account) == Approx(10.0f));
	REQUIRE(economy::physical::inventory::quantity(*f.state, f.site, f.output, seller) == Approx(0.0f));
	REQUIRE(economy::physical::exact_person_goods::stock_quantity(*f.state, key, f.site, f.output) == Approx(1.0f));
	REQUIRE(economy::physical::exact_person_goods::fill_count(*f.state) == 1);
	REQUIRE(economy::physical::concrete_market::observed_price(*f.state, f.market, f.output,
		f.state->current_date, 0.0f) == Approx(10.0f));
	REQUIRE(economy::physical::exact_person_goods::process_consumption(*f.state, key, f.output) == Approx(1.0f));
	REQUIRE(economy::physical::exact_person_goods::stock_quantity(*f.state, key, f.site, f.output) == Approx(0.0f));
	REQUIRE(economy::physical::exact_person_goods::consumed_this_period(*f.state, key, f.output) == Approx(1.0f));
	REQUIRE(economy::physical::exact_person_goods::unmet_need(*f.state, key, f.output) == Approx(0.0f));
	REQUIRE(f.state->world.person_size() == before_persons);
	REQUIRE(f.state->world.economic_actor_size() == before_actors + 1); // seller only
	REQUIRE(f.state->world.monetary_account_size() == before_accounts + 1); // seller only
	REQUIRE(f.state->world.physical_stock_size() == before_stocks + 1); // seller only
	REQUIRE(f.state->world.transaction_size() == before_transactions); // exact mixed ledger, no DCON buyer transaction
}

TEST_CASE("exact goods stay sparse for a million logical persons", "[economy][exact][goods][scale]") {
	individual_concrete_labor_tests::fixture f;
	persons::exact_population::cell_descriptor descriptor;
	descriptor.source_population_cell = 9000;
	descriptor.literal_count = 1'000'000;
	descriptor.bootstrap_base_day = f.state->current_date.to_raw_value() - 1;
	descriptor.demographic_seed = 9000;
	REQUIRE(persons::exact_population::register_synthetic_population_cell(*f.state, descriptor).result
		== persons::exact_population::status::created);
	exact_person_goods_tests::person_key key{9000, 0};
	auto account = economy::exact_person_economy::open_account(*f.state, key, f.settlement);
	REQUIRE(account);
	REQUIRE(economy::physical::exact_person_goods::set_need(*f.state, key, f.output, 1.0f));
	REQUIRE(economy::physical::exact_person_goods::bid_count(*f.state) == 0);
	REQUIRE(economy::physical::exact_person_goods::fill_count(*f.state) == 0);
	REQUIRE(persons::exact_population::logical_person_count(*f.state) == 1'000'000);
	REQUIRE(f.state->world.person_size() == 0);
	REQUIRE(f.state->world.economic_actor_size() == 1); // fixture employer only
	REQUIRE(economy::exact_person_economy::account_count(*f.state) == 1);
}

TEST_CASE("exact purchase selects only a seller-compatible settlement account", "[economy][exact][goods][settlement]") {
	individual_concrete_labor_tests::fixture f;
	auto key = exact_person_goods_tests::worker(f);
	f.state->world.commodity_set_cost(f.output, 2.0f);
	auto seller = f.state->world.create_economic_actor();
	auto seller_account = economy::accounts::open_account(*f.state, seller, f.settlement);
	REQUIRE(economy::accounts::bootstrap_set_balance(*f.state, seller_account, 0.0f));
	REQUIRE(economy::physical::inventory::add(*f.state, f.site, f.output, 1.0f, seller) == Approx(1.0f));
	REQUIRE(economy::physical::concrete_market::post_ask(*f.state, seller, f.site, f.market, f.output, 1.0f, 2.0f, {}));
	auto compatible = economy::exact_person_economy::open_account(*f.state, key, f.settlement);
	auto incompatible_settlement = f.state->world.create_commodity();
	auto incompatible = economy::exact_person_economy::open_account(*f.state, key, incompatible_settlement);
	REQUIRE(economy::exact_person_economy::set_balance(*f.state, compatible, 2.0f));
	REQUIRE(economy::exact_person_economy::set_balance(*f.state, incompatible, 100.0f));
	REQUIRE(economy::physical::exact_person_goods::set_need(*f.state, key, f.output, 1.0f));
	REQUIRE(economy::physical::exact_person_goods::process_purchase_decision(*f.state, key, f.output));
	REQUIRE(economy::exact_person_economy::balance(*f.state, compatible) == Approx(0.0f));
	REQUIRE(economy::exact_person_economy::balance(*f.state, incompatible) == Approx(100.0f));
}

TEST_CASE("remote seller settlement cannot poison local exact purchase selection", "[economy][exact][goods][settlement][local]") {
	individual_concrete_labor_tests::fixture f;
	auto key = exact_person_goods_tests::worker(f);
	f.state->world.commodity_set_cost(f.output, 2.0f);
	auto local_seller = f.state->world.create_economic_actor();
	auto local_account = economy::accounts::open_account(*f.state, local_seller, f.settlement);
	REQUIRE(economy::accounts::bootstrap_set_balance(*f.state, local_account, 0.0f));
	REQUIRE(economy::physical::inventory::add(*f.state, f.site, f.output, 1.0f, local_seller) == Approx(1.0f));
	REQUIRE(economy::physical::concrete_market::post_ask(*f.state, local_seller, f.site, f.market,
		f.output, 1.0f, 2.0f, {}));
	auto remote = f.state->world.create_site();
	auto remote_seller = f.state->world.create_economic_actor();
	auto remote_settlement = f.state->world.create_commodity();
	auto remote_account = economy::accounts::open_account(*f.state, remote_seller, remote_settlement);
	REQUIRE(economy::accounts::bootstrap_set_balance(*f.state, remote_account, 0.0f));
	REQUIRE(economy::physical::inventory::add(*f.state, remote, f.output, 1.0f, remote_seller) == Approx(1.0f));
	REQUIRE(economy::physical::concrete_market::post_ask(*f.state, remote_seller, remote, f.market,
		f.output, 1.0f, 1.0f, {}));
	auto local_money = economy::exact_person_economy::open_account(*f.state, key, f.settlement);
	auto remote_money = economy::exact_person_economy::open_account(*f.state, key, remote_settlement);
	REQUIRE(economy::exact_person_economy::set_balance(*f.state, local_money, 2.0f));
	REQUIRE(economy::exact_person_economy::set_balance(*f.state, remote_money, 100.0f));
	REQUIRE(economy::physical::exact_person_goods::set_need(*f.state, key, f.output, 1.0f));
	REQUIRE(economy::physical::exact_person_goods::process_purchase_decision(*f.state, key, f.output));
	REQUIRE(economy::exact_person_economy::balance(*f.state, local_money) == Approx(0.0f));
	REQUIRE(economy::exact_person_economy::balance(*f.state, remote_money) == Approx(100.0f));
	REQUIRE(economy::accounts::balance(*f.state, local_account) == Approx(2.0f));
	REQUIRE(economy::physical::inventory::quantity(*f.state, remote, f.output, remote_seller) == Approx(1.0f));
	REQUIRE(economy::physical::exact_person_goods::fill_count(*f.state) == 1);
	REQUIRE(economy::physical::exact_person_goods::fill(*f.state, 1)->source == f.site);
}

namespace exact_price_history_tests {

void dcon_fill(concrete_market_tests::fixture& f, sys::date date, float quantity, float price) {
	f.state->current_date = date;
	REQUIRE(economy::physical::inventory::add(*f.state, f.source, f.goods, quantity, f.seller) == Approx(quantity));
	REQUIRE(economy::physical::concrete_market::post_ask(*f.state, f.seller, f.source, f.market,
		f.goods, quantity, price, {}));
	REQUIRE(economy::physical::concrete_market::post_bid(*f.state, f.buyer, f.buyer_account, f.source,
		f.market, f.goods, quantity, price, {}));
	REQUIRE(economy::physical::concrete_market::match(*f.state, f.market, f.goods, date).size() == 1);
}

void exact_fill(concrete_market_tests::fixture& f, uint32_t cell, sys::date date,
	float quantity, float price) {
	f.state->current_date = date;
	persons::exact_population::cell_descriptor descriptor;
	descriptor.source_population_cell = cell;
	descriptor.literal_count = 1;
	descriptor.bootstrap_base_day = date.to_raw_value() - 1;
	descriptor.home_site = f.source;
	REQUIRE(persons::exact_population::register_synthetic_population_cell(*f.state, descriptor).result
		== persons::exact_population::status::created);
	persons::exact_population::person_key key{cell, 0};
	auto account = economy::exact_person_economy::open_account(*f.state, key, f.settlement);
	REQUIRE(economy::exact_person_economy::set_balance(*f.state, account, quantity * price));
	auto seller = f.state->world.create_economic_actor();
	auto seller_account = economy::accounts::open_account(*f.state, seller, f.settlement);
	REQUIRE(economy::accounts::bootstrap_set_balance(*f.state, seller_account, 0.0f));
	REQUIRE(economy::physical::inventory::add(*f.state, f.source, f.goods, quantity, seller) == Approx(quantity));
	REQUIRE(economy::physical::concrete_market::post_ask(*f.state, seller, f.source, f.market,
		f.goods, quantity, price, {}));
	REQUIRE(economy::physical::exact_person_goods::post_bid(*f.state, key, account, f.source, f.market,
		f.goods, quantity, price));
	REQUIRE(economy::physical::concrete_market::match(*f.state, f.market, f.goods, date).empty());
	REQUIRE(economy::physical::exact_person_goods::fill_count(*f.state) == 1);
}

}

TEST_CASE("exact-only observed price contributes to concrete history", "[economy][exact][price]") {
	concrete_market_tests::fixture f;
	exact_price_history_tests::exact_fill(f, 700, sys::date{10}, 2.0f, 15.0f);
	REQUIRE(economy::physical::concrete_market::observed_price(*f.state, f.market, f.goods,
		sys::date{10}, 0.0f) == Approx(15.0f));
}

TEST_CASE("same-date DCON and exact fills form one observed VWAP", "[economy][exact][price]") {
	concrete_market_tests::fixture f;
	exact_price_history_tests::dcon_fill(f, sys::date{11}, 1.0f, 10.0f);
	exact_price_history_tests::exact_fill(f, 701, sys::date{11}, 3.0f, 20.0f);
	REQUIRE(economy::physical::concrete_market::observed_price(*f.state, f.market, f.goods,
		sys::date{11}, 0.0f) == Approx(17.5f));
}

TEST_CASE("concrete reference uses latest eligible fill date across both ledgers", "[economy][exact][price]") {
	concrete_market_tests::fixture f;
	exact_price_history_tests::dcon_fill(f, sys::date{10}, 1.0f, 10.0f);
	exact_price_history_tests::exact_fill(f, 702, sys::date{11}, 1.0f, 15.0f);
	REQUIRE(economy::physical::concrete_market::concrete_reference_price(*f.state, f.market, f.goods,
		sys::date{12}, 0.0f) == Approx(15.0f));
}

TEST_CASE("latest concrete reference mixes same-day DCON and exact fills only", "[economy][exact][price]") {
	concrete_market_tests::fixture f;
	exact_price_history_tests::exact_fill(f, 703, sys::date{10}, 1.0f, 8.0f);
	exact_price_history_tests::dcon_fill(f, sys::date{11}, 1.0f, 10.0f);
	exact_price_history_tests::exact_fill(f, 704, sys::date{11}, 3.0f, 20.0f);
	REQUIRE(economy::physical::concrete_market::concrete_reference_price(*f.state, f.market, f.goods,
		sys::date{12}, 0.0f) == Approx(17.5f));
}

TEST_CASE("unfilled and expired exact bids do not affect concrete prices", "[economy][exact][price]") {
	concrete_market_tests::fixture f;
	persons::exact_population::cell_descriptor descriptor;
	descriptor.source_population_cell = 705;
	descriptor.literal_count = 1;
	descriptor.bootstrap_base_day = 9;
	descriptor.home_site = f.source;
	REQUIRE(persons::exact_population::register_synthetic_population_cell(*f.state, descriptor).result
		== persons::exact_population::status::created);
	auto key = persons::exact_population::person_key{705, 0};
	auto account = economy::exact_person_economy::open_account(*f.state, key, f.settlement);
	REQUIRE(economy::exact_person_economy::set_balance(*f.state, account, 10.0f));
	REQUIRE(economy::physical::exact_person_goods::post_bid(*f.state, key, account, f.source, f.market,
		f.goods, 1.0f, 10.0f));
	REQUIRE(economy::physical::concrete_market::observed_price(*f.state, f.market, f.goods,
		f.state->current_date, 99.0f) == Approx(99.0f));
	f.state->current_date += 1;
	economy::physical::concrete_market::expire(*f.state, f.state->current_date);
	REQUIRE(economy::physical::concrete_market::observed_price(*f.state, f.market, f.goods,
		f.state->current_date, 99.0f) == Approx(99.0f));
}

TEST_CASE("exact goods snapshot preserves sparse orders and fills", "[economy][exact][goods][persistence]") {
	individual_concrete_labor_tests::fixture f;
	auto key = exact_person_goods_tests::worker(f);
	auto account = economy::exact_person_economy::open_account(*f.state, key, f.settlement);
	REQUIRE(economy::exact_person_economy::set_balance(*f.state, account, 5.0f));
	REQUIRE(economy::physical::exact_person_goods::set_need(*f.state, key, f.output, 1.0f));
	REQUIRE(economy::physical::exact_person_goods::add_stock(*f.state, key, f.site, f.output, 1.0f) == Approx(1.0f));
	auto snapshot = economy::physical::exact_person_goods::export_snapshot(*f.state);
	economy::physical::exact_person_goods::clear_store(*f.state);
	REQUIRE(economy::physical::exact_person_goods::import_snapshot(*f.state, snapshot));
	REQUIRE(economy::physical::exact_person_goods::need(*f.state, key, f.output));
	REQUIRE(economy::physical::exact_person_goods::stock_quantity(*f.state, key, f.site, f.output) == Approx(1.0f));
}
