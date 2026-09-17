#include "catch2/catch.hpp"

#include "system_state.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/physical/concrete_market.hpp"
#include "economy/physical/freight_market.hpp"
#include "economy/physical/inventory.hpp"
#include "economy/physical/shipments.hpp"
#include "economy/relations/relations.hpp"
#include "economy/world_trade_capacity.hpp"

#include <memory>

namespace {

struct fixture {
	std::unique_ptr<sys::state> state = std::make_unique<sys::state>();
	dcon::site_id source = state->world.create_site();
	dcon::site_id destination = state->world.create_site();
	dcon::commodity_id settlement = state->world.create_commodity();
	dcon::commodity_id goods = state->world.create_commodity();
	dcon::economic_actor_id buyer = state->world.create_economic_actor();
	dcon::monetary_account_id payer = economy::accounts::open_account(*state, buyer, settlement);
	fixture() { economy::accounts::bootstrap_set_balance(*state, payer, 1000.0f); }
};

struct carrier_fixture {
	dcon::economic_actor_id actor{};
	dcon::monetary_account_id account{};
	dcon::carrier_id carrier{};
	dcon::freight_offer_id offer{};
};

carrier_fixture add_carrier(fixture& f, float capacity, float charge,
	uint8_t mode_mask = (1u << 2), dcon::market_id presence = {},
	dcon::market_id offer_origin = {}, dcon::market_id offer_destination = {}) {
	carrier_fixture result{};
	result.actor = f.state->world.create_economic_actor();
	result.account = economy::accounts::open_account(*f.state, result.actor, f.settlement);
	economy::accounts::bootstrap_set_balance(*f.state, result.account, 0.0f);
	result.carrier = economy::physical::freight_market::create_carrier(
		*f.state, result.actor, result.account, capacity, mode_mask, presence);
	result.offer = economy::physical::freight_market::create_offer(
		*f.state, result.carrier, offer_origin, offer_destination,
		mode_mask, capacity, charge, 0.0f);
	return result;
}

dcon::freight_request_id request_for(fixture& f, float quantity,
	dcon::monetary_account_id payer = {}) {
	economy::physical::inventory::add(*f.state, f.source, f.goods, quantity, f.buyer);
	return economy::physical::freight_market::create_request(
		*f.state, f.buyer, payer ? payer : f.payer, f.source, f.destination, f.goods, quantity);
}

} // namespace

TEST_CASE("carrier registration persists exact actor account and capacity",
	"[economy][physical][freight]") {
	fixture f;
	auto c = add_carrier(f, 100.0f, 2.0f);
	REQUIRE(c.carrier);
	REQUIRE(f.state->world.carrier_get_economic_actor_from_carrier_actor(c.carrier) == c.actor);
	REQUIRE(f.state->world.carrier_get_monetary_account_from_carrier_account(c.carrier) == c.account);
	REQUIRE(f.state->world.carrier_get_service_capacity(c.carrier) == Approx(100.0f));
	REQUIRE(f.state->world.carrier_get_committed_capacity(c.carrier) == Approx(0.0f));
	REQUIRE(f.state->world.carrier_get_mode_mask(c.carrier)
		== (1u << 2)); // transport_mode::local
	REQUIRE(f.state->world.freight_offer_get_carrier_from_freight_offer_carrier(c.offer) == c.carrier);
}

TEST_CASE("freight requests preserve buyer-owned stock before carrier matching",
	"[economy][physical][freight]") {
	fixture f;
	auto request = request_for(f, 12.0f);
	REQUIRE(request);
	REQUIRE(f.state->world.freight_request_get_quantity(request) == Approx(12.0f));
	REQUIRE(f.state->world.freight_request_get_cargo_units(request) > 0.0f);
	REQUIRE(f.state->world.freight_request_get_site_from_freight_request_source(request) == f.source);
	REQUIRE(f.state->world.freight_request_get_site_from_freight_request_destination(request) == f.destination);
	REQUIRE(f.state->world.freight_request_get_status(request)
		== uint8_t(economy::physical::freight_market::freight_request_status::pending));
	REQUIRE(economy::physical::freight_market::match_request(*f.state, request) == dcon::freight_contract_id{});
	REQUIRE(economy::physical::inventory::quantity(*f.state, f.source, f.goods, f.buyer) == Approx(12.0f));
	REQUIRE(economy::physical::inventory::quantity(*f.state, f.destination, f.goods, f.buyer) == Approx(0.0f));
	// A later carrier availability event can consume the same buyer-owned stock.
	auto c = add_carrier(f, 100.0f, 2.0f);
	auto contract = economy::physical::freight_market::match_request(*f.state, request);
	REQUIRE(contract);
	auto shipment = f.state->world.freight_contract_get_shipment_from_freight_contract_shipment(contract);
	REQUIRE(shipment);
	economy::physical::shipments::advance(*f.state);
	REQUIRE(economy::physical::inventory::quantity(*f.state, f.destination, f.goods, f.buyer)
		== Approx(12.0f * (1.0f - 0.0005f)));
	REQUIRE(f.state->world.carrier_get_committed_capacity(c.carrier) == Approx(0.0f));
}

TEST_CASE("matching pays exact carrier account and creates a linked shipment",
	"[economy][physical][freight]") {
	fixture f;
	auto c = add_carrier(f, 100.0f, 7.0f);
	auto request = request_for(f, 12.0f);
	auto contract = economy::physical::freight_market::match_request(*f.state, request);
	REQUIRE(contract);
	REQUIRE(f.state->world.freight_contract_get_agreed_freight_price(contract) == Approx(7.0f));
	REQUIRE(economy::accounts::balance(*f.state, f.payer) == Approx(993.0f));
	REQUIRE(economy::accounts::balance(*f.state, c.account) == Approx(7.0f));
	REQUIRE(f.state->world.freight_contract_get_monetary_account_from_freight_contract_payer_account(contract) == f.payer);
	REQUIRE(f.state->world.freight_contract_get_monetary_account_from_freight_contract_carrier_account(contract) == c.account);
	auto payment = f.state->world.freight_contract_get_transaction_from_freight_contract_payment(contract);
	REQUIRE(payment);
	REQUIRE(f.state->world.transaction_get_kind(payment)
		== uint8_t(economy::relations::transaction_kind::freight));
	auto shipment = f.state->world.freight_contract_get_shipment_from_freight_contract_shipment(contract);
	REQUIRE(shipment);
	REQUIRE(f.state->world.shipment_get_carrier_from_shipment_carrier(shipment) == c.carrier);
	REQUIRE(f.state->world.shipment_get_freight_contract_from_freight_contract_shipment(shipment) == contract);
	REQUIRE(f.state->world.carrier_get_committed_capacity(c.carrier) > 0.0f);
	REQUIRE(f.state->world.freight_offer_get_committed_capacity(c.offer) > 0.0f);
	REQUIRE_FALSE(economy::physical::freight_market::match_request(*f.state, request));
	REQUIRE(f.state->world.carrier_get_committed_capacity(c.carrier) > 0.0f);
}

TEST_CASE("matching uses the requested payer account and rejects insufficient funds atomically",
	"[economy][physical][freight]") {
	fixture f;
	auto second = economy::accounts::open_account(*f.state, f.buyer, f.settlement);
	economy::accounts::bootstrap_set_balance(*f.state, second, 10.0f);
	auto c = add_carrier(f, 100.0f, 7.0f);
	auto request = request_for(f, 4.0f, second);
	REQUIRE(economy::physical::freight_market::match_request(*f.state, request));
	REQUIRE(economy::accounts::balance(*f.state, second) == Approx(3.0f));
	REQUIRE(economy::accounts::balance(*f.state, f.payer) == Approx(1000.0f));

	fixture poor;
	poor.state->world.monetary_account_set_balance(poor.payer, 1.0f);
	auto poor_carrier = add_carrier(poor, 100.0f, 7.0f);
	auto poor_request = request_for(poor, 4.0f);
	REQUIRE_FALSE(economy::physical::freight_market::match_request(*poor.state, poor_request));
	REQUIRE(poor.state->world.freight_offer_get_committed_capacity(poor_carrier.offer) == Approx(0.0f));
	REQUIRE(poor.state->world.carrier_get_committed_capacity(poor_carrier.carrier) == Approx(0.0f));
	REQUIRE(poor.state->world.shipment_size() == 0);
}

TEST_CASE("carrier capacity and deterministic price selection bind matching",
	"[economy][physical][freight]") {
	fixture f;
	auto expensive = add_carrier(f, 100.0f, 9.0f);
	auto cheap = add_carrier(f, 100.0f, 3.0f);
	auto first_request = request_for(f, 70.0f);
	auto first_contract = economy::physical::freight_market::match_request(*f.state, first_request);
	REQUIRE(first_contract);
	REQUIRE(f.state->world.freight_contract_get_freight_offer_from_freight_contract_offer(first_contract) == cheap.offer);
	REQUIRE(f.state->world.carrier_get_committed_capacity(cheap.carrier) == Approx(70.0f));

	// Isolate the capacity assertion from the earlier price-selection carrier.
	f.state->world.carrier_set_status(expensive.carrier,
		uint8_t(economy::physical::freight_market::carrier_status::inactive));
	auto second_request = request_for(f, 60.0f);
	REQUIRE_FALSE(economy::physical::freight_market::match_request(*f.state, second_request));
	REQUIRE(f.state->world.carrier_get_committed_capacity(cheap.carrier) == Approx(70.0f));
	REQUIRE(f.state->world.carrier_get_committed_capacity(expensive.carrier) == Approx(0.0f));

	fixture ties;
	auto first = add_carrier(ties, 100.0f, 4.0f);
	auto second = add_carrier(ties, 100.0f, 4.0f);
	auto tie_request = request_for(ties, 2.0f);
	auto tie_contract = economy::physical::freight_market::match_request(*ties.state, tie_request);
	REQUIRE(tie_contract);
	REQUIRE(ties.state->world.freight_contract_get_freight_offer_from_freight_contract_offer(tie_contract) == first.offer);
	REQUIRE(first.offer.index() < second.offer.index());
}

TEST_CASE("fulfilled freight releases carrier capacity after delivery",
	"[economy][physical][freight]") {
	fixture f;
	auto c = add_carrier(f, 100.0f, 2.0f);
	auto request = request_for(f, 8.0f);
	auto contract = economy::physical::freight_market::match_request(*f.state, request);
	REQUIRE(contract);
	REQUIRE(economy::physical::inventory::quantity(*f.state, f.source, f.goods, f.buyer) == Approx(0.0f));
	economy::physical::shipments::advance(*f.state);
	REQUIRE(economy::physical::inventory::quantity(*f.state, f.destination, f.goods, f.buyer)
		== Approx(8.0f * (1.0f - 0.0005f)));
	REQUIRE(f.state->world.freight_contract_get_quantity(contract) == Approx(8.0f));
	REQUIRE(f.state->world.carrier_get_committed_capacity(c.carrier) == Approx(0.0f));
	REQUIRE(f.state->world.freight_offer_get_committed_capacity(c.offer) == Approx(0.0f));
	REQUIRE(f.state->world.freight_contract_get_status(contract) == 1);
}

TEST_CASE("carrier service and infrastructure capacity are independent constraints",
	"[economy][physical][freight][routed-shipment]") {
	fixture f;
	auto origin_state = f.state->world.create_state_instance();
	auto destination_state = f.state->world.create_state_instance();
	auto origin_market = f.state->world.create_market();
	auto destination_market = f.state->world.create_market();
	auto origin_province = f.state->world.create_province();
	auto destination_province = f.state->world.create_province();
	f.state->world.force_create_site_location(f.source, origin_province);
	f.state->world.force_create_site_location(f.destination, destination_province);
	f.state->world.province_set_state_membership(origin_province, origin_state);
	f.state->world.province_set_state_membership(destination_province, destination_state);
	f.state->world.market_set_zone_from_local_market(origin_market, origin_state);
	f.state->world.market_set_zone_from_local_market(destination_market, destination_state);
	f.state->world.market_set_max_throughput(origin_market, 1.0f);
	f.state->world.market_set_max_throughput(destination_market, 1.0f);
	auto origin_hub = f.state->world.create_site();
	auto destination_hub = f.state->world.create_site();
	f.state->world.force_create_site_location(origin_hub, origin_province);
	f.state->world.force_create_site_location(destination_hub, destination_province);
	f.state->world.force_create_market_hub_site(origin_market, origin_hub);
	f.state->world.force_create_market_hub_site(destination_market, destination_hub);
	auto route = f.state->world.force_create_trade_route(origin_market, destination_market);
	f.state->world.trade_route_set_is_land_route(route, true);
	f.state->world.trade_route_set_land_distance(route, 150.0f);
	f.state->world.trade_route_resize_volume(f.state->world.commodity_size());

	constexpr uint8_t land = 1u << 0;
	constexpr uint8_t local = 1u << 2;
	auto carrier = add_carrier(f, 200.0f, 1.0f, land | local);
	auto request = request_for(f, 150.0f);
	REQUIRE(request);
	auto quoted_distance = f.state->world.freight_request_get_route_distance(request);
	auto physical_capacity_before = economy::world_trade::canonical_capacity(
		*f.state, origin_market, economy::world_trade::transport_mode::land);
	f.state->world.trade_route_set_volume(route, f.goods, 1000000.0f);
	f.state->world.market_set_max_throughput(origin_market, 999999.0f);
	REQUIRE(f.state->world.freight_request_get_route_distance(request) == Approx(quoted_distance));
	REQUIRE(economy::world_trade::canonical_capacity(
		*f.state, origin_market, economy::world_trade::transport_mode::land)
		== Approx(physical_capacity_before));

	auto contract = economy::physical::freight_market::match_request(*f.state, request);
	REQUIRE(contract);
	REQUIRE(f.state->world.freight_contract_get_agreed_freight_price(contract)
		== Approx(1.0f + quoted_distance));
	auto shipment = f.state->world.freight_contract_get_shipment_from_freight_contract_shipment(contract);
	REQUIRE(shipment);
	economy::physical::shipments::advance(*f.state);
	REQUIRE(f.state->world.shipment_is_valid(shipment));
	REQUIRE(f.state->world.shipment_get_lifecycle(shipment) == 0);
	REQUIRE(f.state->world.carrier_get_committed_capacity(carrier.carrier) == Approx(150.0f));
}

TEST_CASE("pending freight requests are matched by the regular pending-request pass",
	"[economy][physical][freight]") {
	fixture f;
	auto request = request_for(f, 6.0f);
	REQUIRE(request);
	auto carrier = add_carrier(f, 100.0f, 2.0f);
	economy::physical::freight_market::process_pending_requests(*f.state);
	REQUIRE(f.state->world.freight_request_get_status(request)
		== uint8_t(economy::physical::freight_market::freight_request_status::contracted));
	dcon::freight_contract_id contract{};
	f.state->world.freight_request_for_each_freight_contract_request(request,
		[&](dcon::freight_contract_request_id relation) {
			contract = f.state->world.freight_contract_request_get_freight_contract(relation);
		});
	REQUIRE(contract);
	REQUIRE(f.state->world.freight_contract_get_shipment_from_freight_contract_shipment(contract));
	REQUIRE(economy::accounts::balance(*f.state, carrier.account) == Approx(2.0f));
}

TEST_CASE("freight matching cannot spend cash reserved by an active concrete bid",
	"[economy][physical][freight]") {
	fixture f;
	f.state->world.monetary_account_set_balance(f.payer, 100.0f);
	auto market = f.state->world.create_market();
	auto bid = economy::physical::concrete_market::post_bid(
		*f.state, f.buyer, f.payer, f.destination, market, f.goods, 10.0f, 10.0f, {});
	REQUIRE(bid);
	REQUIRE(f.state->world.concrete_market_bid_get_reserved_amount(bid) == Approx(100.0f));
	auto carrier = add_carrier(f, 100.0f, 2.0f);
	auto request = request_for(f, 1.0f);
	REQUIRE_FALSE(economy::physical::freight_market::match_request(*f.state, request));
	REQUIRE(economy::accounts::balance(*f.state, f.payer) == Approx(100.0f));
	REQUIRE(f.state->world.freight_offer_get_committed_capacity(carrier.offer) == Approx(0.0f));
	REQUIRE(f.state->world.carrier_get_committed_capacity(carrier.carrier) == Approx(0.0f));
	REQUIRE(f.state->world.shipment_size() == 0);
}

TEST_CASE("carrier home presence permits an explicitly scoped inter-market offer",
	"[economy][physical][freight][routed-shipment]") {
	fixture f;
	auto origin_state = f.state->world.create_state_instance();
	auto destination_state = f.state->world.create_state_instance();
	auto origin_market = f.state->world.create_market();
	auto destination_market = f.state->world.create_market();
	auto origin_province = f.state->world.create_province();
	auto destination_province = f.state->world.create_province();
	f.state->world.force_create_site_location(f.source, origin_province);
	f.state->world.force_create_site_location(f.destination, destination_province);
	f.state->world.province_set_state_membership(origin_province, origin_state);
	f.state->world.province_set_state_membership(destination_province, destination_state);
	f.state->world.market_set_zone_from_local_market(origin_market, origin_state);
	f.state->world.market_set_zone_from_local_market(destination_market, destination_state);
	auto origin_hub = f.state->world.create_site();
	auto destination_hub = f.state->world.create_site();
	f.state->world.force_create_site_location(origin_hub, origin_province);
	f.state->world.force_create_site_location(destination_hub, destination_province);
	f.state->world.force_create_market_hub_site(origin_market, origin_hub);
	f.state->world.force_create_market_hub_site(destination_market, destination_hub);
	auto route = f.state->world.force_create_trade_route(origin_market, destination_market);
	f.state->world.trade_route_set_is_land_route(route, true);
	f.state->world.trade_route_set_land_distance(route, 150.0f);
	f.state->world.trade_route_resize_volume(f.state->world.commodity_size());

	constexpr uint8_t land_and_local = (1u << 0) | (1u << 2);
	auto carrier = add_carrier(f, 100.0f, 3.0f, land_and_local,
		origin_market, origin_market, destination_market);
	auto request = request_for(f, 5.0f);
	REQUIRE(request);
	REQUIRE(economy::physical::freight_market::match_request(*f.state, request));
	REQUIRE(f.state->world.freight_offer_get_origin_market(carrier.offer) == origin_market);
	REQUIRE(f.state->world.freight_offer_get_destination_market(carrier.offer) == destination_market);
}
