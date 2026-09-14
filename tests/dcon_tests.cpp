#include "catch.hpp"
#include "dcon_generated.hpp"
#include "container_types.hpp"
#include "system_state.hpp"
#include "serialization.hpp"
#include "economy/physical/deposits.hpp"
#include "economy/physical/inventory.hpp"
#include "economy/physical/shipments.hpp"
#include "economy/physical/legacy_market_bridge.hpp"
#include "actors/ownership.hpp"
#include "economy/relations/relations.hpp"
#include "economy/accounts/accounts.hpp"
#include <limits>

TEST_CASE("dl_setting", "[dcon]") {
    std::unique_ptr<sys::state> state = std::make_unique<sys::state>();

    constexpr uint32_t num_test = 2000;
    dcon::army_id aid[num_test];
    for(uint32_t i = 0; i < num_test; ++i) {
        aid[i] = state->world.create_army();
        REQUIRE(aid[i].index() == int32_t(i));
    }
    dcon::navy_id nid[num_test];
    for(uint32_t i = 0; i < num_test; ++i) {
        nid[i] = state->world.create_navy();
        REQUIRE(nid[i].index() == int32_t(i));
    }
    dcon::province_id prov[num_test];
    for(uint32_t i = 0; i < num_test; ++i) {
        prov[i] = state->world.create_province();
        REQUIRE(prov[i].index() == int32_t(i));
    }

    // Set location of army enough times to make it crash due to a bug on DataContainer
    for(uint32_t n = 0; n < 100; ++n) {
        for(uint32_t i = 0; i < num_test; ++i) {
            uint32_t idx = (i + n) % num_test;
            state->world.army_set_location_from_army_location(aid[idx], prov[idx]);
            state->world.navy_set_location_from_navy_location(nid[idx], prov[idx]);
        }
    }
}

TEST_CASE("reb_setting", "[dcon]") {
    std::unique_ptr<sys::state> state = std::make_unique<sys::state>();

    constexpr uint32_t num_test = 100;
    dcon::nation_id nid[num_test];
    for(uint32_t i = 0; i < num_test; ++i) {
        nid[i] = state->world.create_nation();
        REQUIRE(nid[i].index() == int32_t(i));
    }
    dcon::rebel_faction_id rfid[num_test];
    for(uint32_t i = 0; i < num_test; ++i) {
        rfid[i] = state->world.create_rebel_faction();
        REQUIRE(rfid[i].index() == int32_t(i));
    }
    dcon::province_id prov[num_test];
    for(uint32_t i = 0; i < num_test; ++i) {
        prov[i] = state->world.create_province();
        REQUIRE(prov[i].index() == int32_t(i));
    }
    // give provinces to nations
    for(uint32_t i = 0; i < num_test; ++i) {
        state->world.province_set_nation_from_province_ownership(prov[i], nid[i]);
        state->world.province_set_nation_from_province_control(prov[i], nid[i]);
        state->world.province_set_rebel_faction_from_province_rebel_control(prov[i], dcon::rebel_faction_id{});
    }
    // oh no rebels are attacking! ~
    for(uint32_t i = 0; i < num_test; ++i) {
        state->world.province_set_nation_from_province_control(prov[i], dcon::nation_id{});
        state->world.province_set_rebel_faction_from_province_rebel_control(prov[i], rfid[i]);
    }
    // check what the rebels did
    for(uint32_t i = 0; i < num_test; ++i) {
        REQUIRE(state->world.province_get_nation_from_province_control(prov[i]) == dcon::nation_id{});
        REQUIRE(state->world.province_get_rebel_faction_from_province_rebel_control(prov[i]) == rfid[i]);
        auto prov_fat = dcon::fatten(state->world, prov[i]);
        REQUIRE(prov_fat.get_province_rebel_control_as_province().get_rebel_faction() == rfid[i]);
    }
}

TEST_CASE("infrastructure_edge_connects_two_nodes", "[dcon][foundation]") {
	std::unique_ptr<sys::state> state = std::make_unique<sys::state>();

	auto const first_node = state->world.create_infrastructure_node();
	auto const second_node = state->world.create_infrastructure_node();
	auto const edge = state->world.create_infrastructure_edge();
	state->world.infrastructure_edge_set_type(edge, uint8_t(3));
	state->world.infrastructure_edge_set_distance(edge, 12.5f);
	auto const edge_from = state->world.force_create_infrastructure_edge_from(edge, first_node);
	auto const edge_to = state->world.force_create_infrastructure_edge_to(edge, second_node);

	REQUIRE(state->world.infrastructure_edge_from_get_infrastructure_edge(edge_from) == edge);
	REQUIRE(state->world.infrastructure_edge_to_get_infrastructure_edge(edge_to) == edge);
	REQUIRE(state->world.infrastructure_edge_get_node_from_infrastructure_edge_from(edge) == first_node);
	REQUIRE(state->world.infrastructure_edge_get_node_from_infrastructure_edge_to(edge) == second_node);
	auto const from_reverse = state->world.infrastructure_node_get_infrastructure_edge_from_as_node(first_node);
	auto const to_reverse = state->world.infrastructure_node_get_infrastructure_edge_to_as_node(second_node);
	REQUIRE(from_reverse.begin() != from_reverse.end());
	REQUIRE(to_reverse.begin() != to_reverse.end());
	REQUIRE(state->world.infrastructure_edge_from_get_infrastructure_edge(*from_reverse.begin()) == edge);
	REQUIRE(state->world.infrastructure_edge_to_get_infrastructure_edge(*to_reverse.begin()) == edge);
	auto incident = ::world::infrastructure::incident_edges(*state, first_node);
	REQUIRE(incident.size() == 1);
	REQUIRE(incident.front() == edge);
	REQUIRE(state->world.infrastructure_edge_get_type(edge) == uint8_t(3));
	REQUIRE(state->world.infrastructure_edge_get_distance(edge) == 12.5f);
}

TEST_CASE("factory_site_bootstrap_is_deterministic", "[world][foundation]") {
	std::unique_ptr<sys::state> state = std::make_unique<sys::state>();
	auto province = state->world.create_province();
	state->world.province_set_mid_point(province, glm::vec2{ 12.0f, 34.0f });
	auto factory = state->world.create_factory();
	state->world.force_create_factory_location(factory, province);

	::world::legacy_bridge::bootstrap_factory_sites(*state);
	auto site = state->world.factory_get_site_from_factory_site(factory);
	REQUIRE(site);
	REQUIRE(::world::site::site_for_factory(*state, factory) == site);
	REQUIRE(::world::site::province_for_site(*state, site) == province);
	REQUIRE(::world::spatial::site_position(*state, site) == glm::vec2{ 12.0f, 34.0f });

	auto site_count = state->world.site_size();
	::world::legacy_bridge::bootstrap_factory_sites(*state);
	REQUIRE(state->world.site_size() == site_count);
	REQUIRE(state->world.factory_get_site_from_factory_site(factory) == site);
}

TEST_CASE("physical_inventory_dispatch_and_arrival_conserve_stock", "[economy][physical]") {
	std::unique_ptr<sys::state> state = std::make_unique<sys::state>();
	auto first_province = state->world.create_province();
	auto second_province = state->world.create_province();
	state->world.province_set_mid_point_b(first_province, glm::vec3{ 1.0f, 0.0f, 0.0f });
	state->world.province_set_mid_point_b(second_province, glm::vec3{ 1.0f, 0.0f, 0.0f });
	auto first_site = state->world.create_site();
	auto second_site = state->world.create_site();
	state->world.force_create_site_location(first_site, first_province);
	state->world.force_create_site_location(second_site, second_province);
	auto commodity = state->world.create_commodity();

	auto canonical = ::economy::physical::inventory::ensure(*state, first_site, commodity, {});
	REQUIRE(::economy::physical::inventory::ensure(*state, first_site, commodity, {}) == canonical);
	REQUIRE(state->world.physical_stock_size() == 1);
	REQUIRE(::economy::physical::inventory::add(*state, first_site, commodity, 10.0f, {}) == 10.0f);
	REQUIRE(::economy::physical::inventory::quantity(*state, first_site, commodity, {}) == 10.0f);
	auto shipment = ::economy::physical::shipments::dispatch(*state, first_site, second_site, commodity, 6.0f, {});
	REQUIRE(shipment);
	REQUIRE(::economy::physical::inventory::quantity(*state, first_site, commodity, {}) == 4.0f);
	REQUIRE(::economy::physical::inventory::quantity(*state, second_site, commodity, {}) == 0.0f);
	state->world.shipment_set_remaining_days(shipment, 2);
	constexpr float initial_quantity = 10.0f;
	constexpr float origin_quantity = 4.0f;
	auto spoilage = economy::logistics::profile_for(*state, commodity).daily_spoilage;
	::economy::physical::shipments::advance(*state);
	REQUIRE(state->world.shipment_is_valid(shipment));
	REQUIRE(::economy::physical::inventory::quantity(*state, second_site, commodity, {}) == 0.0f);
	auto in_transit = state->world.shipment_get_remaining_quantity(shipment);
	auto spoiled = 6.0f - in_transit;
	REQUIRE(in_transit == Approx(6.0f * (1.0f - spoilage)).epsilon(0.00001));
	REQUIRE(origin_quantity + in_transit + spoiled == Approx(initial_quantity).epsilon(0.00001));
	::economy::physical::shipments::advance(*state);
	REQUIRE(!state->world.shipment_is_valid(shipment));
	REQUIRE(::economy::physical::inventory::quantity(*state, first_site, commodity, {}) == 4.0f);
	auto destination_quantity = ::economy::physical::inventory::quantity(*state, second_site, commodity, {});
	spoiled = 6.0f - destination_quantity;
	REQUIRE(destination_quantity == Approx(6.0f * (1.0f - spoilage) * (1.0f - spoilage)).epsilon(0.00001));
	REQUIRE(origin_quantity + destination_quantity + spoiled == Approx(initial_quantity).epsilon(0.00001));
}

TEST_CASE("physical_compatibility_travel_days_use_land_speed", "[economy][physical]") {
	REQUIRE(::economy::physical::shipments::compatibility_travel_days(0.0f) == 1);
	REQUIRE(::economy::physical::shipments::compatibility_travel_days(150.0f) == 1);
	REQUIRE(::economy::physical::shipments::compatibility_travel_days(150.1f) == 2);
	REQUIRE(::economy::physical::shipments::compatibility_travel_days(std::numeric_limits<float>::quiet_NaN()) == 1);
}

TEST_CASE("physical_rgo_bootstrap_is_idempotent", "[economy][physical]") {
	std::unique_ptr<sys::state> state = std::make_unique<sys::state>();
	auto province = state->world.create_province();
	auto commodity = state->world.create_commodity();
	state->world.province_resize_rgo_size(state->world.commodity_size());
	state->world.commodity_set_rgo_amount(commodity, 1.0f);
	state->world.province_set_rgo_size(province, commodity, 1.0f);

	::economy::physical::deposits::bootstrap(*state);
	auto site = ::economy::physical::deposits::extraction_site_for(*state, province, commodity);
	REQUIRE(site);
	REQUIRE(state->world.resource_deposit_size() == 1);
	REQUIRE(state->world.site_get_province_from_site_location(site) == province);
	::economy::physical::deposits::bootstrap(*state);
	REQUIRE(state->world.resource_deposit_size() == 1);
	REQUIRE(::economy::physical::deposits::extraction_site_for(*state, province, commodity) == site);
}

TEST_CASE("physical_rgo_arrives_once_at_legacy_market", "[economy][physical][integration]") {
	auto state = std::make_unique<sys::state>();
	state->force_age_of_transformation_ruleset = true;
	state->world.create_province(); // Keep the test province non-null for hub bootstrap.
	state->world.create_site(); // Keep both bootstrap endpoints non-null in this minimal fixture.
	auto province = state->world.create_province();
	auto capital = state->world.create_province();
	state->world.province_set_mid_point_b(province, glm::vec3{1.0f, 0.0f, 0.0f});
	state->world.province_set_mid_point_b(capital, glm::vec3{0.0f, 1.0f, 0.0f});
	auto zone = state->world.create_state_instance();
	state->world.create_market(); // Keep the fixture market relation non-null.
	auto market = state->world.create_market();
	auto commodity = state->world.create_commodity();
	state->world.state_instance_set_capital(zone, capital);
	state->world.state_instance_set_market_from_local_market(zone, market);
	state->world.market_set_zone_from_local_market(market, zone);
	state->world.province_set_state_membership(province, zone);
	state->world.province_resize_rgo_size(state->world.commodity_size());
	state->world.province_resize_rgo_output(state->world.commodity_size());
	state->world.market_resize_supply(state->world.commodity_size());
	state->world.market_resize_stockpile(state->world.commodity_size());
	state->world.commodity_set_rgo_amount(commodity, 1.0f);
	state->world.province_set_rgo_size(province, commodity, 1.0f);
	state->world.province_set_rgo_output(province, commodity, 5.0f);
	REQUIRE(state->world.province_get_state_membership(province) == zone);
	REQUIRE(state->world.state_instance_get_market_from_local_market(zone) == market);
	REQUIRE(state->world.province_get_rgo_output(province, commodity) == 5.0f);
	REQUIRE(!state->world.commodity_get_is_local(commodity));
	REQUIRE(!state->world.commodity_get_money_rgo(commodity));

	::economy::physical::deposits::bootstrap(*state);
	REQUIRE(::economy::physical::deposits::extraction_site_for(*state, province, commodity));
	::economy::physical::shipments::process_rgo_output(*state);
	REQUIRE(state->world.shipment_size() == 1);
	REQUIRE(state->world.market_get_stockpile(market, commodity) == Approx(0.0f));
	REQUIRE(state->world.market_get_supply(market, commodity) == Approx(0.0f));

	while(state->world.shipment_size() != 0)
		::economy::physical::shipments::advance(*state);
	::economy::physical::legacy_market_bridge::handoff_arrived_stock(*state);
	REQUIRE(state->world.shipment_size() == 0);
	auto expected = 5.0f * (1.0f - economy::logistics::profile_for(*state, commodity).daily_spoilage);
	REQUIRE(state->world.market_get_stockpile(market, commodity) == Approx(expected).epsilon(0.00001));
	REQUIRE(state->world.market_get_stockpile(market, commodity) == Approx(expected).epsilon(0.00001));
}

TEST_CASE("local_rgo_keeps_legacy_supply_in_physical_mode", "[economy][physical][integration]") {
	auto state = std::make_unique<sys::state>();
	state->force_age_of_transformation_ruleset = true;
	state->world.create_province();
	auto province = state->world.create_province();
	auto zone = state->world.create_state_instance();
	state->world.create_market();
	auto market = state->world.create_market();
	auto commodity = state->world.create_commodity();
	state->world.state_instance_set_capital(zone, province);
	state->world.state_instance_set_market_from_local_market(zone, market);
	state->world.market_set_zone_from_local_market(market, zone);
	state->world.province_set_state_membership(province, zone);
	state->world.province_resize_rgo_size(state->world.commodity_size());
	state->world.province_resize_rgo_output(state->world.commodity_size());
	state->world.market_resize_supply(state->world.commodity_size());
	state->world.market_resize_stockpile(state->world.commodity_size());
	state->world.commodity_set_rgo_amount(commodity, 1.0f);
	state->world.commodity_set_is_local(commodity, true);
	state->world.province_set_rgo_size(province, commodity, 1.0f);
	state->world.province_set_rgo_output(province, commodity, 3.0f);

	::economy::physical::deposits::bootstrap(*state);
	::economy::physical::shipments::process_rgo_output(*state);
	REQUIRE(state->world.market_get_supply(market, commodity) == Approx(3.0f));
	REQUIRE(state->world.shipment_size() == 0);
	REQUIRE(state->world.market_get_stockpile(market, commodity) == Approx(0.0f));
}

TEST_CASE("actors_ownership_bootstrap_is_canonical", "[actors][ownership]") {
	auto state = std::make_unique<sys::state>();
	state->world.create_factory();
	::actors::ownership::bootstrap(*state);
	REQUIRE(state->world.asset_size() == 2);
	REQUIRE(state->world.economic_actor_size() == 1);
	REQUIRE(state->world.organization_size() == 1);
	REQUIRE(state->world.ownership_stake_size() == 1);
	auto factory = dcon::factory_id{dcon::factory_id::value_base_t(0)};
	auto asset = ::actors::ownership::asset_for_factory(*state, factory);
	REQUIRE(asset);
	::actors::ownership::bootstrap(*state);
	REQUIRE(state->world.asset_size() == 2);
	REQUIRE(::actors::ownership::asset_for_factory(*state, factory) == asset);
}

TEST_CASE("physical_stock_identity_includes_owner", "[actors][ownership][economy][physical]") {
	auto state = std::make_unique<sys::state>();
	auto site = state->world.create_site();
	auto commodity = state->world.create_commodity();
	auto owner_a = state->world.create_economic_actor();
	auto owner_b = state->world.create_economic_actor();
	auto stock_a = ::economy::physical::inventory::ensure(*state, site, commodity, owner_a);
	auto stock_a_again = ::economy::physical::inventory::ensure(*state, site, commodity, owner_a);
	auto stock_b = ::economy::physical::inventory::ensure(*state, site, commodity, owner_b);
	REQUIRE(stock_a == stock_a_again);
	REQUIRE(stock_a != stock_b);
	REQUIRE(state->world.physical_stock_size() == 2);
	REQUIRE(::economy::physical::inventory::add(*state, site, commodity, 4.0f, owner_a) == 4.0f);
	REQUIRE(::economy::physical::inventory::remove(*state, site, commodity, 3.0f, owner_b) == 0.0f);
	REQUIRE(::economy::physical::inventory::quantity(*state, site, commodity, owner_a) == 4.0f);
}

TEST_CASE("ownership_stakes_allow_residuals_and_cross_holdings", "[actors][ownership]") {
	auto state = std::make_unique<sys::state>();
	auto asset = state->world.create_asset();
	auto actor_a = state->world.create_economic_actor();
	auto actor_b = state->world.create_economic_actor();
	REQUIRE(::actors::ownership::create_stake(*state, actor_a, asset, 0.4f, 0.4f, 0.4f));
	REQUIRE(::actors::ownership::create_stake(*state, actor_b, asset, 0.5f, 0.5f, 0.5f));
	REQUIRE_FALSE(::actors::ownership::create_stake(*state, state->world.create_economic_actor(), asset, 0.2f, 0.2f, 0.2f));
	auto org_a = state->world.create_organization();
	auto org_b = state->world.create_organization();
	state->world.force_create_organization_actor(org_a, actor_a);
	state->world.force_create_organization_actor(org_b, actor_b);
	auto equity_a = state->world.create_asset();
	auto equity_b = state->world.create_asset();
	state->world.force_create_organization_equity_asset(org_a, equity_a);
	state->world.force_create_organization_equity_asset(org_b, equity_b);
	REQUIRE(::actors::ownership::create_stake(*state, actor_a, equity_b, 1.0f, 1.0f, 1.0f));
	REQUIRE(::actors::ownership::create_stake(*state, actor_b, equity_a, 1.0f, 1.0f, 1.0f));
}

TEST_CASE("owner_preservation_through_shipment_arrival", "[actors][ownership][economy][physical]") {
	auto state = std::make_unique<sys::state>();
	auto origin = state->world.create_site();
	auto destination = state->world.create_site();
	auto commodity = state->world.create_commodity();
	auto owner_a = state->world.create_economic_actor();
	auto owner_b = state->world.create_economic_actor();
	::economy::physical::inventory::ensure(*state, origin, commodity, owner_b);
	::economy::physical::inventory::add(*state, origin, commodity, 5.0f, owner_a);
	auto shipment = ::economy::physical::shipments::dispatch(*state, origin, destination, commodity, 5.0f, owner_a);
	REQUIRE(shipment);
	REQUIRE(state->world.shipment_get_economic_actor_from_shipment_owner(shipment) == owner_a);
	while(state->world.shipment_is_valid(shipment)) ::economy::physical::shipments::advance(*state);
	REQUIRE(::economy::physical::inventory::quantity(*state, destination, commodity, owner_a) > 0.0f);
	REQUIRE(::economy::physical::inventory::quantity(*state, destination, commodity, owner_b) == 0.0f);
}

TEST_CASE("transactions_and_obligations_preserve_concrete_relations", "[economy][relations]") {
	auto state = std::make_unique<sys::state>();
	auto payer = state->world.create_economic_actor();
	auto payee = state->world.create_economic_actor();
	auto commodity = state->world.create_commodity();
	auto transaction = ::economy::relations::record_transaction(*state, payer, payee, 12.5f, commodity,
		::economy::relations::transaction_kind::transfer, sys::date{});
	REQUIRE(transaction);
	REQUIRE(state->world.transaction_get_economic_actor_from_transaction_payer(transaction) == payer);
	REQUIRE(state->world.transaction_get_economic_actor_from_transaction_payee(transaction) == payee);
	REQUIRE(state->world.transaction_get_amount(transaction) == 12.5f);
	REQUIRE(state->world.transaction_get_settlement_commodity(transaction) == commodity);
	REQUIRE_FALSE(::economy::relations::record_transaction(*state, payer, payee, -1.0f, commodity, {}, sys::date{}));
	REQUIRE_FALSE(::economy::relations::record_transaction(*state, payer, payee, std::numeric_limits<float>::quiet_NaN(), commodity, {}, sys::date{}));
	auto obligation = ::economy::relations::create_obligation(*state, payer, payee, 100.0f, commodity, sys::date{}, sys::date{}, 0.10f, ::economy::relations::obligation_kind::loan);
	REQUIRE(obligation);
	REQUIRE(state->world.obligation_get_economic_actor_from_obligation_debtor(obligation) == payer);
	REQUIRE(state->world.obligation_get_economic_actor_from_obligation_creditor(obligation) == payee);
	REQUIRE(::economy::relations::repay_obligation(*state, obligation, 25.0f) == 25.0f);
	REQUIRE(::economy::relations::total_due(*state, obligation) == Approx(75.0f));
	REQUIRE(::economy::relations::repay_obligation(*state, obligation, 1000.0f) == Approx(75.0f));
	REQUIRE(state->world.obligation_get_principal_outstanding(obligation) == Approx(0.0f));
	REQUIRE(state->world.obligation_get_accrued_interest(obligation) == Approx(0.0f));
	REQUIRE(state->world.obligation_get_status(obligation) == uint8_t(::economy::relations::obligation_status::paid));
}

TEST_CASE("obligation_interest_is_deterministic_and_cycles_are_supported", "[economy][relations]") {
	auto state = std::make_unique<sys::state>();
	auto a = state->world.create_economic_actor();
	auto b = state->world.create_economic_actor();
	auto c = state->world.create_economic_actor();
	auto commodity = state->world.create_commodity();
	auto ab = ::economy::relations::create_obligation(*state, a, b, 100.0f, commodity, sys::date{}, sys::date{}, 0.365f, {});
	auto ba = ::economy::relations::create_obligation(*state, b, a, 40.0f, commodity, sys::date{}, sys::date{}, 0.0f, {});
	auto bc = ::economy::relations::create_obligation(*state, b, c, 30.0f, commodity, sys::date{}, sys::date{}, 0.0f, {});
	auto ca = ::economy::relations::create_obligation(*state, c, a, 20.0f, commodity, sys::date{}, sys::date{}, 0.0f, {});
	REQUIRE(ab); REQUIRE(ba); REQUIRE(bc); REQUIRE(ca);
	REQUIRE(::economy::relations::accrue_interest(*state, ab, 10) == Approx(1.0f).epsilon(0.00001));
	REQUIRE(state->world.obligation_get_principal_outstanding(ab) == Approx(100.0f));
	REQUIRE(state->world.obligation_get_accrued_interest(ab) == Approx(1.0f).epsilon(0.00001));
	REQUIRE(::economy::relations::total_due(*state, ab) == Approx(101.0f).epsilon(0.00001));
	REQUIRE(::economy::relations::accrue_interest(*state, ab, 10) == Approx(1.0f).epsilon(0.00001));
	REQUIRE(state->world.obligation_get_accrued_interest(ab) == Approx(2.0f).epsilon(0.00001));
	REQUIRE(::economy::relations::repay_obligation(*state, ab, 1.5f) == Approx(1.5f));
	REQUIRE(state->world.obligation_get_accrued_interest(ab) == Approx(0.5f));
	REQUIRE(state->world.obligation_get_principal_outstanding(ab) == Approx(100.0f));
	REQUIRE(::economy::relations::repay_obligation(*state, ab, 10.5f) == Approx(10.5f));
	REQUIRE(state->world.obligation_get_accrued_interest(ab) == Approx(0.0f));
	REQUIRE(state->world.obligation_get_principal_outstanding(ab) == Approx(90.0f));
	REQUIRE(::economy::relations::outstanding_between(*state, a, b, commodity) == Approx(90.0f));
}

TEST_CASE("monetary_accounts_transfer_atomically", "[economy][accounts]") {
	auto state = std::make_unique<sys::state>();
	auto actor_a = state->world.create_economic_actor();
	auto actor_b = state->world.create_economic_actor();
	auto usd = state->world.create_commodity();
	auto eur = state->world.create_commodity();
	auto source = ::economy::accounts::open_account(*state, actor_a, usd);
	auto destination = ::economy::accounts::open_account(*state, actor_b, usd);
	auto second_usd = ::economy::accounts::open_account(*state, actor_a, usd);
	auto euros = ::economy::accounts::open_account(*state, actor_b, eur);
	REQUIRE(source); REQUIRE(destination); REQUIRE(second_usd); REQUIRE(euros);
	REQUIRE(::economy::accounts::owner_of(*state, source) == actor_a);
	REQUIRE(::economy::accounts::settlement_of(*state, source) == usd);
	REQUIRE(::economy::accounts::bootstrap_set_balance(*state, source, 100.0f));
	REQUIRE(::economy::accounts::bootstrap_set_balance(*state, destination, 7.0f));
	auto transaction = ::economy::accounts::transfer(*state, source, destination, 25.0f,
		::economy::relations::transaction_kind::transfer, sys::date{});
	REQUIRE(transaction);
	REQUIRE(::economy::accounts::balance(*state, source) == Approx(75.0f));
	REQUIRE(::economy::accounts::balance(*state, destination) == Approx(32.0f));
	REQUIRE(state->world.transaction_get_economic_actor_from_transaction_payer(transaction) == actor_a);
	REQUIRE(state->world.transaction_get_economic_actor_from_transaction_payee(transaction) == actor_b);
	REQUIRE(state->world.transaction_get_monetary_account_from_transaction_source_account(transaction) == source);
	REQUIRE(state->world.transaction_get_monetary_account_from_transaction_destination_account(transaction) == destination);
	REQUIRE_FALSE(::economy::accounts::transfer(*state, source, euros, 1.0f, {}, sys::date{}));
	REQUIRE_FALSE(::economy::accounts::transfer(*state, source, source, 1.0f, {}, sys::date{}));
	REQUIRE_FALSE(::economy::accounts::transfer(*state, source, destination, -1.0f, {}, sys::date{}));
	REQUIRE(::economy::accounts::balance(*state, source) == Approx(75.0f));
}

TEST_CASE("obligation_payment_settles_cash_and_debt_atomically", "[economy][accounts][relations]") {
	auto state = std::make_unique<sys::state>();
	auto debtor = state->world.create_economic_actor();
	auto creditor = state->world.create_economic_actor();
	auto commodity = state->world.create_commodity();
	auto debtor_account = ::economy::accounts::open_account(*state, debtor, commodity);
	auto creditor_account = ::economy::accounts::open_account(*state, creditor, commodity);
	::economy::accounts::bootstrap_set_balance(*state, debtor_account, 100.0f);
	auto obligation = ::economy::relations::create_obligation(*state, debtor, creditor, 100.0f, commodity, {}, {}, 0.0f, {});
	REQUIRE(obligation);
	REQUIRE(::economy::accounts::settle_obligation_payment(*state, obligation, debtor_account, creditor_account, 60.0f, {}) != dcon::transaction_id{});
	REQUIRE(::economy::accounts::balance(*state, debtor_account) == Approx(40.0f));
	REQUIRE(::economy::accounts::balance(*state, creditor_account) == Approx(60.0f));
	REQUIRE(::economy::relations::total_due(*state, obligation) == Approx(40.0f));
	REQUIRE(::economy::accounts::settle_obligation_payment(*state, obligation, debtor_account, creditor_account, 1000.0f, {}));
	REQUIRE(::economy::accounts::balance(*state, debtor_account) == Approx(0.0f));
	REQUIRE(::economy::relations::total_due(*state, obligation) == Approx(0.0f));
	REQUIRE(state->world.obligation_get_status(obligation) == uint8_t(::economy::relations::obligation_status::paid));
}
