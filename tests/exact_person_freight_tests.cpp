#include "catch.hpp"

#include "accounts/accounts.hpp"
#include "economy/exact_person_economy.hpp"
#include "economy/physical/concrete_market.hpp"
#include "economy/physical/exact_person_freight.hpp"
#include "economy/physical/exact_person_goods.hpp"
#include "economy/physical/freight_market.hpp"
#include "economy/physical/inventory.hpp"
#include "economy/physical/shipments.hpp"
#include "economy/relations/relations.hpp"

namespace exact_person_freight_tests {

using person_key = persons::exact_population::person_key;

struct fixture {
	std::unique_ptr<sys::state> state = std::make_unique<sys::state>();
	dcon::province_id source_province{};
	dcon::province_id destination_province{};
	dcon::state_instance_id zone{};
	dcon::site_id source{};
	dcon::site_id destination{};
	dcon::market_id market{};
	dcon::commodity_id settlement{};
	dcon::commodity_id goods{};
	dcon::economic_actor_id seller{};
	dcon::monetary_account_id seller_account{};
	person_key worker{1, 0};
	dcon::monetary_account_id carrier_account{};

	fixture() {
		state->current_date = sys::date{10};
		source_province = state->world.create_province();
		destination_province = state->world.create_province();
		zone = state->world.create_state_instance();
		market = state->world.create_market();
		state->world.state_instance_set_market_from_local_market(zone, market);
		state->world.market_set_zone_from_local_market(market, zone);
		state->world.province_set_state_membership(source_province, zone);
		state->world.province_set_state_membership(destination_province, zone);
		source = state->world.create_site();
		destination = state->world.create_site();
		state->world.force_create_site_location(source, source_province);
		state->world.force_create_site_location(destination, destination_province);
		settlement = state->world.create_commodity();
		goods = state->world.create_commodity();
		state->world.commodity_set_cost(goods, 10.0f);
		state->world.market_resize_price(state->world.commodity_size());
		state->world.market_set_price(market, goods, 10.0f);
		seller = state->world.create_economic_actor();
		seller_account = economy::accounts::open_account(*state, seller, settlement);
		REQUIRE(economy::accounts::bootstrap_set_balance(*state, seller_account, 0.0f));
		persons::exact_population::cell_descriptor descriptor;
		descriptor.source_population_cell = 1;
		descriptor.literal_count = 1;
		descriptor.bootstrap_base_day = state->current_date.to_raw_value() - 1;
		descriptor.home_site = destination;
		REQUIRE(persons::exact_population::register_synthetic_population_cell(*state, descriptor).result
			== persons::exact_population::status::created);
	}

	void seller_stock(float quantity = 5.0f) {
		REQUIRE(economy::physical::inventory::add(*state, source, goods, quantity, seller) == Approx(quantity));
		REQUIRE(economy::physical::concrete_market::post_ask(*state, seller, source, market, goods,
			quantity, 10.0f, {}));
	}

	economy::exact_person_economy::account_ref worker_account(float amount) {
		auto account = economy::exact_person_economy::open_account(*state, worker, settlement);
		REQUIRE(economy::exact_person_economy::set_balance(*state, account, amount));
		return account;
	}

	uint64_t remote_purchase(float quantity = 5.0f) {
		seller_stock(quantity);
		auto account = worker_account(quantity * 10.0f);
		REQUIRE(economy::physical::exact_person_goods::post_bid(*state, worker, account, destination,
			market, goods, quantity, 10.0f));
		REQUIRE(economy::physical::concrete_market::match(*state, market, goods, state->current_date).empty());
		REQUIRE(economy::physical::exact_person_goods::stock_quantity(*state, worker, source, goods)
			== Approx(quantity));
		return account.exact_account_id;
	}

};

}

namespace exact_person_freight_tests {

struct carrier_info {
	dcon::economic_actor_id actor{};
	dcon::monetary_account_id account{};
	dcon::carrier_id carrier{};
	dcon::freight_offer_id offer{};
};

carrier_info add_carrier(sys::state& state, dcon::commodity_id settlement, float charge = 2.0f) {
	carrier_info result;
	result.actor = state.world.create_economic_actor();
	result.account = economy::accounts::open_account(state, result.actor, settlement);
	REQUIRE(economy::accounts::bootstrap_set_balance(state, result.account, 0.0f));
	constexpr uint8_t local = 1u << 2;
	result.carrier = economy::physical::freight_market::create_carrier(state, result.actor,
		result.account, 100.0f, local);
	result.offer = economy::physical::freight_market::create_offer(state, result.carrier,
		{}, {}, local, 100.0f, charge, 0.0f);
	REQUIRE(result.carrier);
	REQUIRE(result.offer);
	return result;
}

carrier_info add_carrier(fixture& f, dcon::commodity_id settlement, float charge = 2.0f) {
	return add_carrier(*f.state, settlement, charge);
}

}

TEST_CASE("remote exact purchase owns goods at source and leaves pending freight", "[economy][exact][freight]") {
	exact_person_freight_tests::fixture f;
	auto persons_before = f.state->world.person_size();
	auto actors_before = f.state->world.economic_actor_size();
	auto accounts_before = f.state->world.monetary_account_size();
	auto stocks_before = f.state->world.physical_stock_size();
	auto exact_account_id = f.remote_purchase();
	REQUIRE(economy::accounts::balance(*f.state, f.seller_account) == Approx(50.0f));
	REQUIRE(economy::physical::exact_person_goods::stock_quantity(*f.state, f.worker, f.source, f.goods)
		== Approx(5.0f));
	REQUIRE(economy::physical::exact_person_goods::stock_quantity(*f.state, f.worker, f.destination, f.goods)
		== Approx(0.0f));
	REQUIRE(economy::physical::exact_person_freight::request_count(*f.state) == 1);
	REQUIRE(economy::physical::exact_person_freight::reserved_source_quantity(*f.state, f.worker,
		f.source, f.goods) == Approx(5.0f));
	REQUIRE(economy::physical::exact_person_freight::create_request(*f.state, f.worker, f.source,
		f.destination, f.goods, 1.0f) == 0); // the first request reserves all stock
	REQUIRE(f.state->world.person_size() == persons_before);
	REQUIRE(f.state->world.economic_actor_size() == actors_before);
	REQUIRE(f.state->world.monetary_account_size() == accounts_before);
	REQUIRE(f.state->world.physical_stock_size() == stocks_before + 1); // seller only
	REQUIRE(economy::exact_person_economy::transaction_count(*f.state) == 1);
	REQUIRE(economy::exact_person_economy::transaction(*f.state,
		1)->destination == economy::exact_person_economy::account_ref::from_dcon(f.seller_account));
	REQUIRE(economy::exact_person_economy::balance(*f.state,
		economy::exact_person_economy::account_ref::from_exact(exact_account_id)) == Approx(0.0f));
}

TEST_CASE("exact freight matches existing carrier and reuses routed shipment lifecycle", "[economy][exact][freight]") {
	exact_person_freight_tests::fixture f;
	auto exact_account_id = f.remote_purchase();
	auto carrier = exact_person_freight_tests::add_carrier(*f.state, f.settlement);
	auto request = economy::physical::exact_person_freight::request(*f.state, 1);
	REQUIRE(request);
	auto contract_id = economy::physical::exact_person_freight::match_request(*f.state, request->id);
	REQUIRE(contract_id);
	auto contract = economy::physical::exact_person_freight::contract(*f.state, contract_id);
	REQUIRE(contract);
	REQUIRE(contract->exact_payer_account_id == exact_account_id);
	REQUIRE(economy::exact_person_economy::balance(*f.state,
		economy::exact_person_economy::account_ref::from_exact(exact_account_id)) == Approx(0.0f));
	REQUIRE(economy::accounts::balance(*f.state, carrier.account) == Approx(2.0f));
	REQUIRE(economy::exact_person_economy::transaction_count(*f.state) == 2);
	REQUIRE(contract->shipment);
	REQUIRE(f.state->world.shipment_is_valid(contract->shipment));
	REQUIRE_FALSE(f.state->world.shipment_get_shipment_owner(contract->shipment));
	REQUIRE(economy::physical::exact_person_freight::shipment_owner_count(*f.state) == 1);
	REQUIRE(economy::physical::exact_person_goods::stock_quantity(*f.state, f.worker, f.source, f.goods)
		== Approx(0.0f));
	REQUIRE(economy::physical::exact_person_goods::stock_quantity(*f.state, f.worker, f.destination, f.goods)
		== Approx(0.0f));
	economy::physical::shipments::advance(*f.state);
	REQUIRE_FALSE(f.state->world.shipment_is_valid(contract->shipment));
	REQUIRE(economy::physical::exact_person_goods::stock_quantity(*f.state, f.worker, f.destination, f.goods)
		== Approx(5.0f * (1.0f - 0.0005f)));
	REQUIRE(economy::physical::exact_person_freight::request(*f.state, request->id)->status
		== economy::physical::exact_person_freight::request_status::fulfilled);
	REQUIRE(economy::physical::exact_person_freight::contract(*f.state, contract_id)->status
		== economy::physical::exact_person_freight::contract_status::fulfilled);
	REQUIRE(f.state->world.carrier_get_committed_capacity(carrier.carrier) == Approx(0.0f));
	REQUIRE(f.state->world.freight_offer_get_committed_capacity(carrier.offer) == Approx(0.0f));
}

TEST_CASE("exact freight payer settlement and free cash are enforced", "[economy][exact][freight][money]") {
	exact_person_freight_tests::fixture f;
	f.remote_purchase();
	auto other_settlement = f.state->world.create_commodity();
	auto wrong_carrier = exact_person_freight_tests::add_carrier(f, other_settlement, 1.0f);
	REQUIRE_FALSE(economy::physical::exact_person_freight::match_request(*f.state, 1));
	REQUIRE(f.state->world.shipment_size() == 0);
	REQUIRE(f.state->world.carrier_get_committed_capacity(wrong_carrier.carrier) == Approx(0.0f));
	REQUIRE(economy::physical::exact_person_freight::request(*f.state, 1)->status
		== economy::physical::exact_person_freight::request_status::pending);
}

TEST_CASE("exact freight cannot spend cash reserved by an active exact bid", "[economy][exact][freight][money]") {
	exact_person_freight_tests::fixture f;
	f.seller_stock();
	auto account = f.worker_account(52.0f);
	REQUIRE(economy::physical::exact_person_goods::post_bid(*f.state, f.worker, account,
		f.destination, f.market, f.goods, 5.0f, 10.0f));
	REQUIRE(economy::physical::concrete_market::match(*f.state, f.market, f.goods,
		f.state->current_date).empty());
	REQUIRE(economy::exact_person_economy::balance(*f.state,
		economy::exact_person_economy::account_ref::from_exact(account.exact_account_id)) == Approx(2.0f));
	REQUIRE(economy::physical::exact_person_goods::post_bid(*f.state, f.worker, account,
		f.destination, f.market, f.goods, 0.2f, 10.0f));
	REQUIRE(economy::physical::exact_person_goods::reserved_bid_amount(*f.state,
		account.exact_account_id) == Approx(2.0f));
	auto carrier = exact_person_freight_tests::add_carrier(f, f.settlement);
	REQUIRE_FALSE(economy::physical::exact_person_freight::match_request(*f.state, 1));
	REQUIRE(f.state->world.carrier_get_committed_capacity(carrier.carrier) == Approx(0.0f));
	REQUIRE(economy::exact_person_economy::balance(*f.state,
		economy::exact_person_economy::account_ref::from_exact(account.exact_account_id)) == Approx(2.0f));
}

TEST_CASE("incoming exact freight prevents duplicate remote acquisition", "[economy][exact][freight][consumption]") {
	exact_person_freight_tests::fixture f;
	f.remote_purchase();
	REQUIRE(economy::physical::exact_person_goods::set_need(*f.state, f.worker, f.goods, 5.0f));
	REQUIRE_FALSE(economy::physical::exact_person_goods::process_purchase_decision(*f.state, f.worker, f.goods));
	REQUIRE(economy::physical::exact_person_freight::incoming_quantity(*f.state, f.worker,
		f.destination, f.goods) == Approx(5.0f));
}

TEST_CASE("exact freight snapshot restores active external shipment mapping", "[economy][exact][freight][persistence]") {
	exact_person_freight_tests::fixture f;
	f.remote_purchase();
	auto carrier = exact_person_freight_tests::add_carrier(*f.state, f.settlement);
	economy::physical::freight_market::process_pending_requests(*f.state);
	REQUIRE(economy::physical::exact_person_freight::contract_count(*f.state) == 1);
	REQUIRE(economy::physical::exact_person_freight::shipment_owner_count(*f.state) == 1);
	auto snapshot = economy::physical::exact_person_freight::export_snapshot(*f.state);
	economy::physical::exact_person_freight::clear_store(*f.state);
	REQUIRE(economy::physical::exact_person_freight::import_snapshot(*f.state, snapshot));
	REQUIRE(economy::physical::exact_person_freight::request_count(*f.state) == 1);
	REQUIRE(economy::physical::exact_person_freight::contract_count(*f.state) == 1);
	REQUIRE(economy::physical::exact_person_freight::shipment_owner_count(*f.state) == 1);
	(void)carrier;
}

TEST_CASE("million logical persons do not allocate exact freight state", "[economy][exact][freight][scale]") {
	exact_person_freight_tests::fixture f;
	persons::exact_population::cell_descriptor descriptor;
	descriptor.source_population_cell = 100000;
	descriptor.literal_count = 1'000'000;
	descriptor.bootstrap_base_day = f.state->current_date.to_raw_value() - 1;
	descriptor.home_site = f.destination;
	REQUIRE(persons::exact_population::register_synthetic_population_cell(*f.state, descriptor).result
		== persons::exact_population::status::created);
	REQUIRE(persons::exact_population::logical_person_count(*f.state) == 1'000'001);
	REQUIRE(economy::physical::exact_person_freight::request_count(*f.state) == 0);
	REQUIRE(economy::physical::exact_person_freight::contract_count(*f.state) == 0);
	REQUIRE(economy::physical::exact_person_freight::shipment_owner_count(*f.state) == 0);
}

TEST_CASE("exact labor wage can fund remote purchase, freight, arrival, and consumption", "[economy][exact][freight][causal]") {
	individual_concrete_labor_tests::fixture f;
	auto worker = exact_person_economy_tests::register_anchor(f);
	auto offer = f.offer(1, 20.0f);
	REQUIRE(economy::exact_person_economy::submit_application(*f.state, worker, offer, f.state->current_date));
	economy::physical::job_market::process_pending_applications(*f.state);
	economy::payroll::settle_factory(*f.state, f.factory, 1.0f, 1.0f);
	auto contract_id = economy::exact_person_economy::active_contracts_for_person(*f.state, worker).front();
	auto labor_contract = economy::exact_person_economy::contract(*f.state, contract_id);
	REQUIRE(labor_contract);
	auto worker_account = economy::exact_person_economy::account_ref::from_exact(labor_contract->worker_account_id);
	REQUIRE(economy::exact_person_economy::balance(*f.state, worker_account) == Approx(20.0f));
	auto remote = f.state->world.create_site();
	f.state->world.force_create_site_location(remote, f.province);
	auto seller = f.state->world.create_economic_actor();
	auto seller_account = economy::accounts::open_account(*f.state, seller, f.settlement);
	REQUIRE(economy::accounts::bootstrap_set_balance(*f.state, seller_account, 0.0f));
	REQUIRE(economy::physical::inventory::add(*f.state, remote, f.output, 1.0f, seller) == Approx(1.0f));
	REQUIRE(economy::physical::concrete_market::post_ask(*f.state, seller, remote, f.market, f.output,
		1.0f, 10.0f, {}));
	REQUIRE(economy::physical::exact_person_goods::set_need(*f.state, worker, f.output, 1.0f));
	REQUIRE(economy::physical::exact_person_goods::process_purchase_decision(*f.state, worker, f.output));
	REQUIRE(economy::accounts::balance(*f.state, seller_account) == Approx(10.0f));
	REQUIRE(economy::physical::exact_person_goods::stock_quantity(*f.state, worker, remote, f.output)
		== Approx(1.0f));
	REQUIRE(economy::physical::exact_person_goods::stock_quantity(*f.state, worker, f.site, f.output)
		== Approx(0.0f));
	auto carrier = exact_person_freight_tests::add_carrier(*f.state, f.settlement);
	economy::physical::freight_market::process_pending_requests(*f.state);
	REQUIRE(economy::accounts::balance(*f.state, carrier.account) == Approx(2.0f));
	REQUIRE(economy::physical::exact_person_goods::stock_quantity(*f.state, worker, remote, f.output)
		== Approx(0.0f));
	economy::physical::shipments::advance(*f.state);
	REQUIRE(economy::physical::exact_person_goods::stock_quantity(*f.state, worker, f.site, f.output)
		== Approx(1.0f * (1.0f - 0.0005f)));
	REQUIRE(economy::physical::exact_person_goods::process_consumption(*f.state, worker, f.output)
		== Approx(1.0f * (1.0f - 0.0005f)));
	REQUIRE(economy::physical::exact_person_goods::unmet_need(*f.state, worker, f.output) > 0.0f);
	REQUIRE_FALSE(persons::exact_population::legacy_person_for_exact_person(*f.state, worker));
}
