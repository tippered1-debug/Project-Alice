#include "catch.hpp"
#include "dcon_generated.hpp"
#include "container_types.hpp"
#include "system_state.hpp"
#include "serialization.hpp"
#include "economy/physical/deposits.hpp"
#include "economy/physical/inventory.hpp"
#include "economy/physical/shipments.hpp"
#include "economy/physical/factory_output.hpp"
#include "economy/physical/legacy_market_bridge.hpp"
#include "actors/ownership.hpp"
#include "actors/organizations/organizations.hpp"
#include "economy/relations/relations.hpp"
#include "economy/accounts/accounts.hpp"
#include "governance/governance.hpp"
#include "persons/persons.hpp"
#include "governance/actions/actions.hpp"
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

TEST_CASE("physical_factory_output_uses_operator_owned_shipment_and_handoff", "[economy][physical][factory]") {
	auto state = std::make_unique<sys::state>();
	state->force_age_of_transformation_ruleset = true;
	state->map_state.map_data.world_circumference = 10000.0f;
	auto factory_province = state->world.create_province();
	auto capital_province = state->world.create_province();
	state->world.province_set_mid_point_b(factory_province, glm::vec3{1.0f, 0.0f, 0.0f});
	state->world.province_set_mid_point_b(capital_province, glm::vec3{0.0f, 1.0f, 0.0f});
	auto zone = state->world.create_state_instance();
	auto market = state->world.create_market();
	state->world.state_instance_set_capital(zone, capital_province);
	state->world.state_instance_set_market_from_local_market(zone, market);
	state->world.market_set_zone_from_local_market(market, zone);
	state->world.province_set_state_membership(factory_province, zone);
	state->world.province_set_state_membership(capital_province, zone);
	auto commodity = state->world.create_commodity();
	state->world.market_resize_stockpile(state->world.commodity_size());
	state->world.market_resize_supply(state->world.commodity_size());
	state->world.commodity_set_is_local(commodity, false);
	state->world.commodity_set_money_rgo(commodity, false);
	auto factory_type = state->world.create_factory_type();
	state->world.factory_type_set_output(factory_type, commodity);
	auto factory = state->world.create_factory();
	state->world.factory_set_building_type(factory, factory_type);
	state->world.force_create_factory_location(factory, factory_province);
	::world::legacy_bridge::bootstrap_factory_sites(*state);
	::economy::physical::deposits::bootstrap(*state);
	auto company = ::actors::organizations::create_company(*state);
	REQUIRE(::actors::organizations::bind_factory_operator(*state, company, factory));
	auto operator_actor = ::actors::organizations::operator_actor_for_factory(*state, factory);
	constexpr float produced = 12.0f;
	REQUIRE(::economy::physical::factory_output::materialize_and_dispatch(*state, factory, produced));
	REQUIRE(state->world.shipment_size() == 1);
	auto factory_site = ::world::site::site_for_factory(*state, factory);
	auto hub = ::economy::physical::deposits::market_hub_for(*state, market);
	REQUIRE(factory_site);
	REQUIRE(hub);
	REQUIRE(::economy::physical::inventory::quantity(*state, factory_site, commodity, operator_actor) == Approx(0.0f));
	dcon::shipment_id shipment{};
	state->world.for_each_shipment([&](dcon::shipment_id candidate) { shipment = candidate; });
	REQUIRE(shipment);
	REQUIRE(state->world.shipment_is_valid(shipment));
	auto origin_relation = state->world.shipment_get_shipment_origin(shipment);
	REQUIRE(origin_relation);
	REQUIRE(state->world.shipment_origin_is_valid(origin_relation));
	REQUIRE(state->world.shipment_origin_get_site(origin_relation) == factory_site);
	auto destination_relation = state->world.shipment_get_shipment_destination(shipment);
	REQUIRE(state->world.shipment_destination_is_valid(destination_relation));
	REQUIRE(state->world.shipment_destination_get_site(destination_relation) == hub);
	auto owner_relation = state->world.shipment_get_shipment_owner(shipment);
	REQUIRE(state->world.shipment_owner_is_valid(owner_relation));
	REQUIRE(state->world.shipment_owner_get_economic_actor(owner_relation) == operator_actor);
	REQUIRE(state->world.shipment_get_commodity(shipment) == commodity);
	REQUIRE(state->world.shipment_get_remaining_quantity(shipment) == Approx(produced));
	auto travel_days = state->world.shipment_get_remaining_days(shipment);
	REQUIRE(travel_days > 1);
	REQUIRE(state->world.market_get_stockpile(market, commodity) == Approx(0.0f));
	::economy::physical::shipments::process_arrivals(*state);
	REQUIRE(state->world.shipment_is_valid(shipment));
	REQUIRE(state->world.market_get_stockpile(market, commodity) == Approx(0.0f));
	while(state->world.shipment_size() != 0)
		::economy::physical::shipments::process_arrivals(*state);
	REQUIRE(::economy::physical::inventory::quantity(*state, hub, commodity, operator_actor) == Approx(0.0f));
	REQUIRE(state->world.market_get_stockpile(market, commodity) == Approx(produced * std::pow(1.0f - economy::logistics::profile_for(*state, commodity).daily_spoilage, float(travel_days))).epsilon(0.00001));
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

TEST_CASE("factory_bootstrap_does_not_turn_existing_operator_into_owner", "[actors][ownership]") {
	auto state = std::make_unique<sys::state>();
	auto factory = state->world.create_factory();
	auto real_operator = ::actors::organizations::create_company(*state);
	REQUIRE(::actors::organizations::bind_factory_operator(*state, real_operator, factory));
	REQUIRE(state->world.ownership_stake_size() == 0);
	::actors::ownership::bootstrap(*state);
	auto asset = ::actors::ownership::asset_for_factory(*state, factory);
	REQUIRE(asset);
	REQUIRE(state->world.ownership_stake_size() == 0);
	REQUIRE(::actors::organizations::operator_organization_for_factory(*state, factory) == real_operator);
	::actors::ownership::bootstrap(*state);
	REQUIRE(::actors::ownership::asset_for_factory(*state, factory) == asset);
	REQUIRE(state->world.ownership_stake_size() == 0);

	auto legacy_factory = state->world.create_factory();
	::actors::ownership::bootstrap(*state);
	auto legacy_operator = ::actors::organizations::operator_organization_for_factory(*state, legacy_factory);
	REQUIRE(legacy_operator);
	REQUIRE(state->world.economic_actor_get_is_legacy_placeholder(
		::actors::organizations::actor_for_organization(*state, legacy_operator)));
	REQUIRE(::actors::ownership::asset_for_factory(*state, legacy_factory));
	REQUIRE(state->world.ownership_stake_size() == 1);
	::actors::ownership::bootstrap(*state);
	REQUIRE(state->world.ownership_stake_size() == 1);
}

TEST_CASE("economic_organizations_have_explicit_operations_and_ownership", "[actors][organizations]") {
	auto state = std::make_unique<sys::state>();
	REQUIRE_FALSE(::actors::organizations::create_organization(*state, ::actors::ownership::actor_kind::person));
	REQUIRE_FALSE(::actors::organizations::create_organization(*state, ::actors::ownership::actor_kind::state_entity));
	auto company_a = ::actors::organizations::create_company(*state);
	auto company_b = ::actors::organizations::create_organization(*state, ::actors::ownership::actor_kind::fund);
	REQUIRE(company_a); REQUIRE(company_b); REQUIRE(company_a != company_b);
	auto actor_a = ::actors::organizations::actor_for_organization(*state, company_a);
	auto actor_b = ::actors::organizations::actor_for_organization(*state, company_b);
	REQUIRE(actor_a); REQUIRE(actor_b); REQUIRE(actor_a != actor_b);
	REQUIRE(::actors::organizations::organization_for_actor(*state, actor_a) == company_a);
	REQUIRE(::actors::organizations::equity_asset_for_organization(*state, company_a));
	REQUIRE(::actors::organizations::equity_asset_for_organization(*state, company_b));
	auto factory_a = state->world.create_factory();
	auto factory_b = state->world.create_factory();
	REQUIRE(::actors::organizations::bind_factory_operator(*state, company_a, factory_a));
	REQUIRE(::actors::organizations::bind_factory_operator(*state, company_a, factory_a));
	REQUIRE_FALSE(::actors::organizations::bind_factory_operator(*state, company_b, factory_a));
	REQUIRE(::actors::organizations::bind_factory_operator(*state, company_a, factory_b));
	REQUIRE(::actors::organizations::operator_organization_for_factory(*state, factory_a) == company_a);
	REQUIRE(::actors::organizations::operator_actor_for_factory(*state, factory_a) == actor_a);
	REQUIRE(::actors::organizations::factories_operated_by(*state, company_a).size() == 2);
	auto deposit_a = state->world.create_resource_deposit();
	auto deposit_b = state->world.create_resource_deposit();
	REQUIRE(::actors::organizations::bind_deposit_operator(*state, company_a, deposit_a));
	REQUIRE(::actors::organizations::bind_deposit_operator(*state, company_a, deposit_a));
	REQUIRE_FALSE(::actors::organizations::bind_deposit_operator(*state, company_b, deposit_a));
	REQUIRE(::actors::organizations::bind_deposit_operator(*state, company_a, deposit_b));
	REQUIRE(::actors::organizations::operator_organization_for_deposit(*state, deposit_a) == company_a);
	REQUIRE(::actors::organizations::operator_actor_for_deposit(*state, deposit_a) == actor_a);
	REQUIRE(::actors::organizations::deposits_operated_by(*state, company_a).size() == 2);
	auto person = ::persons::create_person(*state, sys::date{1});
	auto person_actor = ::persons::actor_for_person(*state, person);
	auto factory_asset = state->world.create_asset();
	state->world.force_create_factory_asset(factory_a, factory_asset);
	REQUIRE(::actors::ownership::create_stake(*state, person_actor, factory_asset, 1.0f, 1.0f, 1.0f));
	REQUIRE(::actors::organizations::operator_actor_for_factory(*state, factory_a) == actor_a);
	REQUIRE(state->world.ownership_stake_get_economic_actor_from_ownership_stake_owner(
		dcon::ownership_stake_id{dcon::ownership_stake_id::value_base_t(0)}) == person_actor);
	auto commodity = state->world.create_commodity();
	auto account = ::economy::accounts::open_account(*state, actor_a, commodity);
	REQUIRE(account);
	auto obligation = ::economy::relations::create_obligation(*state, actor_a, actor_b, 25.0f, commodity, {}, {}, 0.0f, {});
	REQUIRE(obligation);
	auto site = state->world.create_site();
	REQUIRE(::economy::physical::inventory::add(*state, site, commodity, 4.0f, actor_a) == Approx(4.0f));
	REQUIRE(::economy::physical::inventory::quantity(*state, site, commodity, actor_a) == Approx(4.0f));
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

TEST_CASE("governance_institutions_and_authority_are_concrete", "[governance]") {
	auto state = std::make_unique<sys::state>();
	auto nation = state->world.create_nation();
	auto other_nation = state->world.create_nation();
	auto government = ::governance::create_institution(*state, nation, ::governance::institution_kind::central_government);
	auto ministry = ::governance::create_institution(*state, nation, ::governance::institution_kind::ministry);
	auto agency = ::governance::create_institution(*state, nation, ::governance::institution_kind::regulator);
	auto other_government = ::governance::create_institution(*state, other_nation, ::governance::institution_kind::central_government);
	auto office = ::governance::create_office(*state, ministry, ::governance::office_kind::finance_minister);
	auto second_office = ::governance::create_office(*state, ministry, ::governance::office_kind::agency_director);
	REQUIRE(::governance::nation_of(*state, government) == nation);
	REQUIRE(::governance::actor_for_institution(*state, government));
	REQUIRE(state->world.economic_actor_get_kind(::governance::actor_for_institution(*state, government)) == uint8_t(actors::ownership::actor_kind::state_entity));
	REQUIRE(::governance::institution_for_office(*state, office) == ministry);
	REQUIRE(::governance::institution_for_office(*state, second_office) == ministry);
	REQUIRE(::governance::set_parent(*state, ministry, government));
	REQUIRE(::governance::set_parent(*state, agency, government));
	REQUIRE(::governance::children_of(*state, government).size() == 2);
	REQUIRE_FALSE(::governance::set_parent(*state, government, government));
	REQUIRE_FALSE(::governance::set_parent(*state, government, ministry));
	REQUIRE(::governance::parent_of(*state, ministry) == government);
	REQUIRE(::governance::set_parent(*state, ministry, government));
	REQUIRE(::governance::set_parent(*state, agency, ministry));
	REQUIRE(::governance::parent_of(*state, agency) == ministry);
	REQUIRE_FALSE(::governance::set_parent(*state, government, agency));
	REQUIRE(::governance::set_parent(*state, agency, government));
	auto grant = ::governance::grant_authority_to_institution(*state, ministry, ::governance::authority_kind::levy_tax, nation);
	REQUIRE(grant);
	REQUIRE(::governance::grant_authority_to_institution(*state, ministry, ::governance::authority_kind::levy_tax, nation) == grant);
	REQUIRE_FALSE(::governance::grant_authority_to_institution(*state, ministry, ::governance::authority_kind::levy_tax, other_nation));
	REQUIRE(::governance::has_authority(*state, ministry, ::governance::authority_kind::levy_tax, nation));
	REQUIRE_FALSE(::governance::has_authority(*state, government, ::governance::authority_kind::levy_tax, nation));
	REQUIRE_FALSE(::governance::grant_authority_to_office(*state, office, ::governance::authority_kind::appoint, other_nation));
	REQUIRE(::governance::grant_authority_to_office(*state, office, ::governance::authority_kind::appoint, nation));
	auto territorial_unit = state->world.create_territorial_unit();
	REQUIRE(::governance::grant_authority_to_institution(*state, government, ::governance::authority_kind::administer, territorial_unit));
	REQUIRE(::governance::grant_authority_to_office(*state, office, ::governance::authority_kind::license, territorial_unit));
	REQUIRE_FALSE(::governance::has_authority(*state, government, ::governance::authority_kind::levy_tax, nation));
	REQUIRE(::governance::revoke_authority(*state, grant));
	REQUIRE_FALSE(::governance::has_authority(*state, ministry, ::governance::authority_kind::levy_tax, nation));
	REQUIRE(::governance::central_government_for(*state, nation) == government);
	REQUIRE(::governance::central_government_for(*state, nation) == government);
	REQUIRE(::governance::central_government_for(*state, other_nation) == other_government);
	REQUIRE(::governance::actor_for_institution(*state, government) != ::governance::actor_for_institution(*state, other_government));
	auto local_government = state->world.create_local_government();
	REQUIRE(::governance::bind_local_government(*state, local_government, government));
	REQUIRE(::governance::bind_local_government(*state, local_government, government));
	REQUIRE_FALSE(::governance::bind_local_government(*state, local_government, ministry));
}

TEST_CASE("persons_occupy_offices_and_retain_assets_after_death", "[persons][governance]") {
	auto state = std::make_unique<sys::state>();
	auto nation = state->world.create_nation();
	auto institution = ::governance::create_institution(*state, nation, ::governance::institution_kind::ministry);
	auto office = ::governance::create_office(*state, institution, ::governance::office_kind::finance_minister);
	auto second_office = ::governance::create_office(*state, institution, ::governance::office_kind::agency_director);
	auto person = ::persons::create_person(*state, sys::date{1});
	auto other_person = ::persons::create_person(*state, sys::date{2});
	auto actor = ::persons::actor_for_person(*state, person);
	REQUIRE(actor);
	REQUIRE(state->world.economic_actor_get_kind(actor) == uint8_t(actors::ownership::actor_kind::person));
	REQUIRE(actor != ::persons::actor_for_person(*state, other_person));
	auto commodity = state->world.create_commodity();
	auto account = ::economy::accounts::open_account(*state, actor, commodity);
	REQUIRE(account);
	auto asset = state->world.create_asset();
	REQUIRE(::actors::ownership::create_stake(*state, actor, asset, 1.0f, 1.0f, 1.0f));
	REQUIRE(::persons::appoint_person(*state, person, office, sys::date{10}));
	auto same_tenure = ::persons::appoint_person(*state, person, office, sys::date{11});
	REQUIRE(same_tenure == ::persons::active_tenure_for(*state, office));
	REQUIRE_FALSE(::persons::appoint_person(*state, other_person, office, sys::date{12}));
	REQUIRE(::persons::occupant_of(*state, office) == person);
	REQUIRE(::persons::appoint_person(*state, person, second_office, sys::date{10}));
	REQUIRE(::persons::active_offices_of(*state, person).size() == 2);
	auto office_grant = ::governance::grant_authority_to_office(*state, office, ::governance::authority_kind::levy_tax, nation);
	REQUIRE(office_grant);
	REQUIRE(::persons::person_has_authority(*state, person, ::governance::authority_kind::levy_tax, nation));
	REQUIRE(::governance::grant_authority_to_institution(*state, institution, ::governance::authority_kind::regulate, nation));
	REQUIRE_FALSE(::persons::person_has_authority(*state, person, ::governance::authority_kind::regulate, nation));
	REQUIRE(::persons::remove_from_office(*state, office, sys::date{20}));
	REQUIRE_FALSE(::persons::occupant_of(*state, office));
	REQUIRE_FALSE(::persons::person_has_authority(*state, person, ::governance::authority_kind::levy_tax, nation));
	REQUIRE(state->world.office_tenure_size() == 2);
	REQUIRE(::persons::appoint_person(*state, other_person, office, sys::date{21}));
	REQUIRE(::persons::mark_dead(*state, person, sys::date{30}));
	REQUIRE_FALSE(state->world.person_get_alive(person));
	REQUIRE(state->world.person_get_death_date(person) == sys::date{30});
	REQUIRE_FALSE(::persons::appoint_person(*state, person, office, sys::date{31}));
	REQUIRE_FALSE(::persons::occupant_of(*state, second_office));
	REQUIRE(::persons::active_offices_of(*state, person).empty());
	REQUIRE(state->world.office_tenure_size() == 3);
	REQUIRE(::persons::actor_for_person(*state, person) == actor);
	REQUIRE(::economy::accounts::owner_of(*state, account) == actor);
	bool ownership_survived = false;
	state->world.economic_actor_for_each_ownership_stake_owner_as_economic_actor(actor, [&](dcon::ownership_stake_owner_id relation) {
		ownership_survived = ownership_survived || bool(state->world.ownership_stake_owner_get_ownership_stake(relation));
	});
	REQUIRE(ownership_survived);
}

TEST_CASE("person_occupancy_temporal_invariants_are_atomic", "[persons][governance]") {
	auto state = std::make_unique<sys::state>();
	auto nation = state->world.create_nation();
	auto institution = ::governance::create_institution(*state, nation, ::governance::institution_kind::ministry);
	auto office = ::governance::create_office(*state, institution, ::governance::office_kind::finance_minister);
	auto second_office = ::governance::create_office(*state, institution, ::governance::office_kind::agency_director);
	auto person = ::persons::create_person(*state, sys::date{100});
	REQUIRE_FALSE(::persons::appoint_person(*state, person, office, sys::date{99}));
	REQUIRE(state->world.office_tenure_size() == 0);
	auto tenure = ::persons::appoint_person(*state, person, office, sys::date{110});
	REQUIRE(tenure);
	REQUIRE_FALSE(::persons::remove_from_office(*state, office, sys::date{109}));
	REQUIRE(::persons::active_tenure_for(*state, office) == tenure);
	REQUIRE(state->world.office_tenure_get_active(tenure));
	REQUIRE(state->world.office_tenure_get_started_on(tenure) == sys::date{110});
	REQUIRE(state->world.office_tenure_get_ended_on(tenure) == sys::date{});
	REQUIRE(::persons::remove_from_office(*state, office, sys::date{120}));
	REQUIRE_FALSE(state->world.office_tenure_get_active(tenure));
	REQUIRE(state->world.office_tenure_get_started_on(tenure) == sys::date{110});
	REQUIRE(state->world.office_tenure_get_ended_on(tenure) == sys::date{120});
	REQUIRE(::persons::appoint_person(*state, person, second_office, sys::date{150}));
	REQUIRE_FALSE(::persons::mark_dead(*state, person, sys::date{140}));
	REQUIRE(state->world.person_get_alive(person));
	REQUIRE(state->world.person_get_death_date(person) == sys::date{});
	REQUIRE(::persons::active_tenure_for(*state, second_office));
	REQUIRE(state->world.office_tenure_get_active(::persons::active_tenure_for(*state, second_office)));
	REQUIRE(state->world.office_tenure_get_ended_on(tenure) == sys::date{120});
	REQUIRE(::persons::mark_dead(*state, person, sys::date{160}));
	REQUIRE_FALSE(state->world.person_get_alive(person));
	REQUIRE(state->world.person_get_death_date(person) == sys::date{160});
	auto second_tenure = ::persons::active_tenure_for(*state, second_office);
	REQUIRE_FALSE(second_tenure);
	REQUIRE(state->world.office_tenure_size() == 2);
	auto second_person = ::persons::create_person(*state, sys::date{200});
	REQUIRE_FALSE(::persons::mark_dead(*state, second_person, sys::date{199}));
	REQUIRE(state->world.person_get_alive(second_person));
	REQUIRE(state->world.person_get_death_date(second_person) == sys::date{});
}

TEST_CASE("governance_actions_require_current_office_authority", "[governance][actions][persons]") {
	auto state = std::make_unique<sys::state>();
	auto nation_a = state->world.create_nation();
	auto nation_b = state->world.create_nation();
	auto institution_a = ::governance::create_institution(*state, nation_a, ::governance::institution_kind::central_government);
	auto institution_b = ::governance::create_institution(*state, nation_b, ::governance::institution_kind::central_government);
	auto authority_office = ::governance::create_office(*state, institution_a, ::governance::office_kind::president);
	auto target_office = ::governance::create_office(*state, institution_a, ::governance::office_kind::finance_minister);
	auto foreign_office = ::governance::create_office(*state, institution_b, ::governance::office_kind::finance_minister);
	auto initiator = ::persons::create_person(*state, sys::date{1});
	auto target = ::persons::create_person(*state, sys::date{1});
	auto foreign_target = ::persons::create_person(*state, sys::date{1});
	REQUIRE_FALSE(::governance::actions::authorized_appoint(*state, initiator, target, target_office, sys::date{10}));
	REQUIRE(state->world.institutional_action_size() == 0);
	REQUIRE_FALSE(::persons::occupant_of(*state, target_office));
	REQUIRE(::persons::appoint_person(*state, initiator, authority_office, sys::date{10}));
	REQUIRE(::governance::grant_authority_to_office(*state, authority_office, ::governance::authority_kind::appoint, nation_a));
	auto appointment = ::governance::actions::authorized_appoint(*state, initiator, target, target_office, sys::date{20});
	REQUIRE(appointment);
	REQUIRE(state->world.institutional_action_get_kind(appointment) == uint8_t(::governance::actions::institutional_action_kind::appointment));
	REQUIRE(state->world.institutional_action_get_occurred_on(appointment) == sys::date{20});
	REQUIRE(state->world.institutional_action_get_person_from_institutional_action_initiator(appointment) == initiator);
	REQUIRE(state->world.institutional_action_get_person_from_institutional_action_target_person(appointment) == target);
	REQUIRE(state->world.institutional_action_get_office_from_institutional_action_target_office(appointment) == target_office);
	REQUIRE(::persons::occupant_of(*state, target_office) == target);
	REQUIRE_FALSE(::governance::actions::authorized_appoint(*state, initiator, foreign_target, foreign_office, sys::date{21}));
	REQUIRE(state->world.institutional_action_size() == 1);
	REQUIRE_FALSE(::governance::actions::authorized_dismiss(*state, initiator, target_office, sys::date{25}));
	REQUIRE(state->world.institutional_action_size() == 1);
	REQUIRE(::governance::grant_authority_to_office(*state, authority_office, ::governance::authority_kind::dismiss, nation_a));
	auto dismissal = ::governance::actions::authorized_dismiss(*state, initiator, target_office, sys::date{30});
	REQUIRE(dismissal);
	REQUIRE(state->world.institutional_action_get_kind(dismissal) == uint8_t(::governance::actions::institutional_action_kind::dismissal));
	REQUIRE(state->world.institutional_action_get_occurred_on(dismissal) == sys::date{30});
	REQUIRE(state->world.institutional_action_get_person_from_institutional_action_initiator(dismissal) == initiator);
	REQUIRE(state->world.institutional_action_get_person_from_institutional_action_target_person(dismissal) == target);
	REQUIRE_FALSE(::persons::occupant_of(*state, target_office));
	REQUIRE(::persons::appoint_person(*state, foreign_target, foreign_office, sys::date{40}));
	REQUIRE(state->world.institutional_action_size() == 2);
	REQUIRE_FALSE(::governance::actions::authorized_dismiss(*state, initiator, foreign_office, sys::date{41}));
	REQUIRE(state->world.institutional_action_size() == 2);
	REQUIRE(::persons::remove_from_office(*state, authority_office, sys::date{50}));
	REQUIRE_FALSE(::governance::actions::authorized_appoint(*state, initiator, target, target_office, sys::date{51}));
	REQUIRE_FALSE(::governance::actions::authorized_dismiss(*state, initiator, foreign_office, sys::date{51}));
	REQUIRE(state->world.institutional_action_size() == 2);
	auto self = ::persons::create_person(*state, sys::date{1});
	auto self_office = ::governance::create_office(*state, institution_a, ::governance::office_kind::judge_seat);
	REQUIRE(::governance::grant_authority_to_office(*state, self_office, ::governance::authority_kind::appoint, nation_a));
	REQUIRE_FALSE(::governance::actions::authorized_appoint(*state, self, self, self_office, sys::date{60}));
	REQUIRE(state->world.institutional_action_size() == 2);
	auto self_authority_office = ::governance::create_office(*state, institution_a, ::governance::office_kind::agency_director);
	REQUIRE(::persons::appoint_person(*state, self, self_authority_office, sys::date{60}));
	REQUIRE(::governance::grant_authority_to_office(*state, self_authority_office, ::governance::authority_kind::appoint, nation_a));
	auto self_appointment = ::governance::actions::authorized_appoint(*state, self, self, self_office, sys::date{70});
	REQUIRE(self_appointment);
	REQUIRE(::persons::occupant_of(*state, self_office) == self);
	auto dead_target = ::persons::create_person(*state, sys::date{1});
	REQUIRE(::persons::mark_dead(*state, dead_target, sys::date{80}));
	REQUIRE_FALSE(::governance::actions::authorized_appoint(*state, self, dead_target, target_office, sys::date{81}));
	REQUIRE(state->world.institutional_action_size() == 3);
	auto unborn_target = ::persons::create_person(*state, sys::date{100});
	REQUIRE_FALSE(::governance::actions::authorized_appoint(*state, self, unborn_target, target_office, sys::date{90}));
	REQUIRE(state->world.institutional_action_size() == 3);
	REQUIRE(::persons::appoint_person(*state, target, target_office, sys::date{100}));
	REQUIRE_FALSE(::governance::actions::authorized_dismiss(*state, self, target_office, sys::date{99}));
	REQUIRE(state->world.institutional_action_size() == 3);
	REQUIRE(::persons::occupant_of(*state, target_office) == target);
	REQUIRE(::persons::mark_dead(*state, self, sys::date{110}));
	REQUIRE_FALSE(::governance::actions::authorized_appoint(*state, self, foreign_target, foreign_office, sys::date{111}));
	REQUIRE(state->world.institutional_action_size() == 3);
}

TEST_CASE("institutional_actions_are_vacancy_and_authority_date_aware", "[governance][actions]") {
	auto state = std::make_unique<sys::state>();
	auto nation = state->world.create_nation();
	auto institution = ::governance::create_institution(*state, nation, ::governance::institution_kind::central_government);
	auto authority_office = ::governance::create_office(*state, institution, ::governance::office_kind::president);
	auto target_office = ::governance::create_office(*state, institution, ::governance::office_kind::finance_minister);
	auto initiator = ::persons::create_person(*state, sys::date{1});
	auto target = ::persons::create_person(*state, sys::date{1});
	REQUIRE(::persons::appoint_person(*state, initiator, authority_office, sys::date{100}));
	REQUIRE(::governance::grant_authority_to_office(*state, authority_office, ::governance::authority_kind::appoint, nation));
	REQUIRE_FALSE(::governance::actions::authorized_appoint(*state, initiator, target, target_office, sys::date{99}));
	REQUIRE(state->world.institutional_action_size() == 0);
	REQUIRE_FALSE(::persons::occupant_of(*state, target_office));
	auto appointment = ::governance::actions::authorized_appoint(*state, initiator, target, target_office, sys::date{100});
	REQUIRE(appointment);
	REQUIRE(::persons::occupant_of(*state, target_office) == target);
	REQUIRE(state->world.institutional_action_size() == 1);
	REQUIRE_FALSE(::governance::actions::authorized_appoint(*state, initiator, target, target_office, sys::date{101}));
	REQUIRE(state->world.institutional_action_size() == 1);
	REQUIRE(::persons::occupant_of(*state, target_office) == target);
	REQUIRE(::persons::remove_from_office(*state, target_office, sys::date{110}));
	REQUIRE(::persons::appoint_person(*state, target, target_office, sys::date{1}));
	REQUIRE(::governance::grant_authority_to_office(*state, authority_office, ::governance::authority_kind::dismiss, nation));
	REQUIRE_FALSE(::governance::actions::authorized_dismiss(*state, initiator, target_office, sys::date{99}));
	REQUIRE(state->world.institutional_action_size() == 1);
	auto active = ::persons::active_tenure_for(*state, target_office);
	REQUIRE(active);
	REQUIRE(state->world.office_tenure_get_active(active));
	REQUIRE(::governance::actions::authorized_dismiss(*state, initiator, target_office, sys::date{100}));
	REQUIRE(state->world.institutional_action_size() == 2);
	REQUIRE_FALSE(::persons::active_tenure_for(*state, target_office));
}
