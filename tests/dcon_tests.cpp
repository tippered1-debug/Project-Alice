#include "catch.hpp"
#include "dcon_generated.hpp"
#include "container_types.hpp"
#include "system_state.hpp"
#include "serialization.hpp"
#include "economy/physical/deposits.hpp"
#include "economy/physical/inventory.hpp"
#include "economy/physical/shipments.hpp"
#include "economy/physical/factory_output.hpp"
#include "economy/physical/factory_inputs.hpp"
#include "market_clearing.hpp"
#include "economy/physical/legacy_market_bridge.hpp"
#include "actors/ownership.hpp"
#include "actors/organizations/organizations.hpp"
#include "economy/relations/relations.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/banking/banking.hpp"
#include "governance/finance/finance.hpp"
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

TEST_CASE("physical_factory_inputs_use_operator_stock_and_bottleneck_ratio", "[economy][physical][factory]") {
	auto state = std::make_unique<sys::state>();
	auto site = state->world.create_site();
	auto owner = state->world.create_economic_actor();
	auto asset_owner = state->world.create_economic_actor();
	auto first = state->world.create_commodity();
	auto second = state->world.create_commodity();
	state->world.commodity_set_is_local(first, false);
	state->world.commodity_set_money_rgo(first, false);
	state->world.commodity_set_is_local(second, false);
	state->world.commodity_set_money_rgo(second, false);
	economy::commodity_set inputs{};
	inputs.commodity_type[0] = first;
	inputs.commodity_amounts[0] = 2.0f;
	inputs.commodity_type[1] = second;
	inputs.commodity_amounts[1] = 4.0f;
	REQUIRE(::economy::physical::inventory::add(*state, site, first, 2.0f, owner) == Approx(2.0f));
	REQUIRE(::economy::physical::inventory::add(*state, site, second, 1.0f, owner) == Approx(1.0f));
	// The second input is the bottleneck: 1 / 4 = 0.25, so production is capped at 25%.
	auto availability = ::economy::physical::factory_inputs::evaluate(*state, site, owner, inputs, {}, 1.0f);
	REQUIRE(availability.active);
	REQUIRE(availability.physical_ratio == Approx(0.25f));
	REQUIRE(::economy::physical::factory_inputs::consume(*state, site, owner, inputs, 1.0f, availability.physical_ratio));
	REQUIRE(::economy::physical::inventory::quantity(*state, site, first, owner) == Approx(1.5f));
	REQUIRE(::economy::physical::inventory::quantity(*state, site, second, owner) == Approx(0.0f));
	REQUIRE(::economy::physical::inventory::quantity(*state, site, first, asset_owner) == Approx(0.0f));
	REQUIRE(::economy::physical::inventory::quantity(*state, site, second, asset_owner) == Approx(0.0f));
	REQUIRE(::economy::physical::inventory::remove(*state, site, second, 1.0f, owner) == Approx(0.0f));
}

TEST_CASE("physical_factory_inputs_full_and_half_supply_consume_once", "[economy][physical][factory]") {
	auto state = std::make_unique<sys::state>();
	auto site = state->world.create_site();
	auto owner = state->world.create_economic_actor();
	auto commodity = state->world.create_commodity();
	state->world.commodity_set_is_local(commodity, false);
	state->world.commodity_set_money_rgo(commodity, false);
	economy::commodity_set inputs{};
	inputs.commodity_type[0] = commodity;
	inputs.commodity_amounts[0] = 2.0f;
	REQUIRE(::economy::physical::inventory::add(*state, site, commodity, 2.0f, owner) == Approx(2.0f));
	auto full = ::economy::physical::factory_inputs::evaluate(*state, site, owner, inputs, {}, 1.0f);
	REQUIRE(full.physical_ratio == Approx(1.0f));
	REQUIRE(::economy::physical::factory_inputs::consume(*state, site, owner, inputs, 1.0f, 1.0f));
	REQUIRE(::economy::physical::inventory::quantity(*state, site, commodity, owner) == Approx(0.0f));
	REQUIRE(::economy::physical::inventory::add(*state, site, commodity, 1.0f, owner) == Approx(1.0f));
	auto half = ::economy::physical::factory_inputs::evaluate(*state, site, owner, inputs, {}, 1.0f);
	REQUIRE(half.physical_ratio == Approx(0.5f));
	REQUIRE(::economy::physical::factory_inputs::consume(*state, site, owner, inputs, 1.0f, half.physical_ratio));
	REQUIRE(::economy::physical::inventory::quantity(*state, site, commodity, owner) == Approx(0.0f));
}

TEST_CASE("physical_factory_inputs_leave_local_and_missing_structure_on_legacy_path", "[economy][physical][factory]") {
	auto state = std::make_unique<sys::state>();
	auto commodity = state->world.create_commodity();
	state->world.commodity_set_is_local(commodity, true);
	state->world.commodity_set_money_rgo(commodity, false);
	economy::commodity_set inputs{};
	inputs.commodity_type[0] = commodity;
	inputs.commodity_amounts[0] = 1.0f;
	auto local = ::economy::physical::factory_inputs::evaluate(*state, {}, {}, inputs, {}, 1.0f);
	REQUIRE_FALSE(local.active);
	REQUIRE(local.physical_ratio == Approx(1.0f));
	auto ordinary = state->world.create_commodity();
	state->world.commodity_set_is_local(ordinary, false);
	state->world.commodity_set_money_rgo(ordinary, false);
	inputs.commodity_type[0] = ordinary;
	auto missing = ::economy::physical::factory_inputs::evaluate(*state, {}, {}, inputs, {}, 1.0f);
	REQUIRE_FALSE(missing.active);
	REQUIRE_FALSE(::economy::physical::factory_inputs::consume(*state, {}, {}, inputs, 1.0f, 1.0f));
}

TEST_CASE("physical_factory_input_procurement_bridges_market_to_factory_site", "[economy][physical][factory][integration]") {
	auto state = std::make_unique<sys::state>();
	state->force_age_of_transformation_ruleset = true;
	auto province = state->world.create_province();
	auto destination = state->world.create_site();
	state->world.force_create_site_location(destination, province);
	auto hub = state->world.create_site();
	state->world.force_create_site_location(hub, province);
	auto market = state->world.create_market();
	state->world.force_create_market_hub_site(market, hub);
	auto factory = state->world.create_factory();
	auto owner = state->world.create_economic_actor();
	auto commodity = state->world.create_commodity();
	state->world.market_resize_stockpile(state->world.commodity_size());
	state->world.market_resize_actual_probability_to_buy(state->world.commodity_size());
	state->world.commodity_set_is_local(commodity, false);
	state->world.commodity_set_money_rgo(commodity, false);
	economy::commodity_set inputs{};
	inputs.commodity_type[0] = commodity;
	inputs.commodity_amounts[0] = 4.0f;

	::economy::physical::factory_inputs::begin_planning(*state);
	REQUIRE(::economy::physical::factory_inputs::plan(*state, factory, destination, owner, inputs, market, 1.0f));
	REQUIRE(::economy::physical::factory_inputs::planned_quantity(*state, factory, commodity, -1.0f) == Approx(4.0f));
	// The market has not settled yet, and fulfillment must not touch its stockpile.
	REQUIRE(state->world.market_get_stockpile(market, commodity) == Approx(0.0f));
	::economy::market_clearing::begin_day(*state);
	::economy::market_clearing::record(*state, market, commodity,
		::economy::market_clearing::demand_class::intermediate, 4.0f);
	auto settled = ::economy::market_clearing::settle(*state, market, commodity, 4.0f, 4.0f, 1.0f);
	REQUIRE(settled.class_fill[static_cast<size_t>(::economy::market_clearing::demand_class::intermediate)] == Approx(1.0f));
	::economy::physical::factory_inputs::fulfill(*state);
	REQUIRE(state->world.market_get_stockpile(market, commodity) == Approx(0.0f));
	REQUIRE(::economy::physical::inventory::quantity(*state, hub, commodity, owner) == Approx(0.0f));
	REQUIRE(::economy::physical::inventory::quantity(*state, destination, commodity, owner) == Approx(0.0f));
	REQUIRE(state->world.shipment_size() == 1);
	state->world.for_each_shipment([&](dcon::shipment_id shipment) {
		state->world.shipment_set_remaining_days(shipment, 1);
	});
	::economy::physical::shipments::advance(*state);
	REQUIRE(state->world.shipment_size() == 0);
	auto surviving = 4.0f * (1.0f - economy::logistics::profile_for(*state, commodity).daily_spoilage);
	REQUIRE(::economy::physical::inventory::quantity(*state, destination, commodity, owner) == Approx(surviving));
	REQUIRE(::economy::physical::factory_inputs::consume(*state, destination, owner, inputs, 1.0f, surviving / 4.0f));
	REQUIRE(::economy::physical::inventory::quantity(*state, destination, commodity, owner) == Approx(0.0f));
}

TEST_CASE("physical_factory_procurement_demand_is_net_of_stock_and_transit", "[economy][physical][factory]") {
	auto state = std::make_unique<sys::state>();
	auto province = state->world.create_province();
	auto origin = state->world.create_site();
	auto destination = state->world.create_site();
	state->world.force_create_site_location(origin, province);
	state->world.force_create_site_location(destination, province);
	auto commodity = state->world.create_commodity();
	state->world.commodity_set_is_local(commodity, false);
	state->world.commodity_set_money_rgo(commodity, false);
	auto owner = state->world.create_economic_actor();
	REQUIRE(::economy::physical::factory_inputs::net_demand(*state, destination, owner, commodity, 100.0f) == Approx(100.0f));
	::economy::physical::inventory::add(*state, destination, commodity, 100.0f, owner);
	REQUIRE(::economy::physical::factory_inputs::net_demand(*state, destination, owner, commodity, 100.0f) == Approx(0.0f));
	::economy::physical::inventory::remove(*state, destination, commodity, 100.0f, owner);
	::economy::physical::inventory::add(*state, destination, commodity, 20.0f, owner);
	::economy::physical::inventory::add(*state, origin, commodity, 30.0f, owner);
	REQUIRE(::economy::physical::shipments::dispatch(*state, origin, destination, commodity, 30.0f, owner));
	REQUIRE(::economy::physical::factory_inputs::net_demand(*state, destination, owner, commodity, 100.0f) == Approx(50.0f));
	REQUIRE(::economy::physical::factory_inputs::net_demand(*state, destination, owner, commodity, 10.0f) == Approx(0.0f));
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

TEST_CASE("commercial_banking_conserves_deposits_reserves_and_loans", "[economy][banking]") {
	auto state = std::make_unique<sys::state>();
	auto settlement = state->world.create_commodity();
	auto wrong_settlement = state->world.create_commodity();
	auto bank_a = ::economy::banking::create_bank(*state);
	auto bank_b = ::economy::banking::create_bank(*state);
	auto bank_a_actor = ::actors::organizations::actor_for_organization(*state, bank_a);
	auto bank_b_actor = ::actors::organizations::actor_for_organization(*state, bank_b);
	REQUIRE(bank_a); REQUIRE(bank_b); REQUIRE(bank_a != bank_b);
	REQUIRE(state->world.economic_actor_get_kind(bank_a_actor) == uint8_t(::actors::ownership::actor_kind::bank));

	auto reserve_a = ::economy::banking::open_reserve_account(*state, bank_a, settlement);
	auto reserve_b = ::economy::banking::open_reserve_account(*state, bank_b, settlement);
	REQUIRE(reserve_a); REQUIRE(reserve_b);
	REQUIRE(::economy::banking::open_reserve_account(*state, bank_a, settlement) == reserve_a);
	REQUIRE(::economy::banking::bootstrap_set_reserve_balance(*state, reserve_a, 1000.0f));
	REQUIRE(::economy::banking::bootstrap_set_reserve_balance(*state, reserve_b, 1000.0f));

	auto borrower = state->world.create_economic_actor();
	auto same_bank_payee = state->world.create_economic_actor();
	auto other_bank_payee = state->world.create_economic_actor();
	auto borrower_deposit = ::economy::banking::open_deposit_account(*state, bank_a, borrower, settlement);
	auto same_bank_deposit = ::economy::banking::open_deposit_account(*state, bank_a, same_bank_payee, settlement);
	auto other_bank_deposit = ::economy::banking::open_deposit_account(*state, bank_b, other_bank_payee, settlement);
	REQUIRE(borrower_deposit); REQUIRE(same_bank_deposit); REQUIRE(other_bank_deposit);
	REQUIRE(::economy::banking::bootstrap_set_deposit_balance(*state, borrower_deposit, 200.0f));
	REQUIRE(::economy::banking::bank_balance_sheet(*state, bank_a, settlement).net_worth == Approx(800.0f));

	auto loan = ::economy::banking::originate_loan(*state, bank_a, borrower_deposit, 100.0f,
		sys::date{1}, sys::date{100}, 0.12f);
	REQUIRE(loan);
	REQUIRE(::economy::banking::deposit_balance(*state, borrower_deposit) == Approx(300.0f));
	REQUIRE(::economy::relations::total_due(*state, loan) == Approx(100.0f));
	// Origination creates a loan asset and a matching deposit liability, without moving reserves.
	auto after_origination = ::economy::banking::bank_balance_sheet(*state, bank_a, settlement);
	REQUIRE(after_origination.settlement_assets == Approx(1000.0f));
	REQUIRE(after_origination.loan_assets == Approx(100.0f));
	REQUIRE(after_origination.deposit_liabilities == Approx(300.0f));
	REQUIRE(after_origination.net_worth == Approx(800.0f));

	REQUIRE(::economy::banking::transfer_deposit(*state, borrower_deposit, same_bank_deposit, 50.0f, sys::date{2}));
	REQUIRE(::economy::banking::deposit_balance(*state, borrower_deposit) == Approx(250.0f));
	REQUIRE(::economy::banking::deposit_balance(*state, same_bank_deposit) == Approx(50.0f));
	REQUIRE(::economy::accounts::balance(*state, reserve_a) == Approx(1000.0f));

	REQUIRE(::economy::banking::transfer_deposit(*state, borrower_deposit, other_bank_deposit, 75.0f, sys::date{3}));
	REQUIRE(::economy::banking::deposit_balance(*state, borrower_deposit) == Approx(175.0f));
	REQUIRE(::economy::banking::deposit_balance(*state, other_bank_deposit) == Approx(75.0f));
	REQUIRE(::economy::accounts::balance(*state, reserve_a) == Approx(925.0f));
	REQUIRE(::economy::accounts::balance(*state, reserve_b) == Approx(1075.0f));
	REQUIRE(::economy::banking::bank_balance_sheet(*state, bank_a, settlement).net_worth == Approx(800.0f));
	REQUIRE(::economy::banking::bank_balance_sheet(*state, bank_b, settlement).net_worth == Approx(1000.0f));

	auto interest = ::economy::banking::accrue_loan_interest(*state, loan, 10);
	REQUIRE(interest == Approx(100.0f * 0.12f * 10.0f / 365.0f).epsilon(0.00001));
	auto accepted = ::economy::banking::repay_loan(*state, loan, borrower_deposit, 20.0f, sys::date{4});
	REQUIRE(accepted == Approx(20.0f));
	REQUIRE(::economy::banking::deposit_balance(*state, borrower_deposit) == Approx(155.0f));
	REQUIRE(state->world.obligation_get_accrued_interest(loan) == Approx(0.0f).epsilon(0.00001));
	REQUIRE(state->world.obligation_get_principal_outstanding(loan) == Approx(80.32877f).epsilon(0.0001));

	// Write-off removes only the concrete loan asset; the already-created deposit liability remains.
	auto second_loan = ::economy::banking::originate_loan(*state, bank_a, same_bank_deposit, 40.0f,
		sys::date{5}, sys::date{100}, 0.0f);
	REQUIRE(second_loan);
	auto before_writeoff = ::economy::banking::bank_balance_sheet(*state, bank_a, settlement);
	auto same_bank_balance = ::economy::banking::deposit_balance(*state, same_bank_deposit);
	REQUIRE(::economy::banking::write_off_loan(*state, second_loan));
	REQUIRE(::economy::banking::deposit_balance(*state, same_bank_deposit) == Approx(same_bank_balance));
	auto after_writeoff = ::economy::banking::bank_balance_sheet(*state, bank_a, settlement);
	REQUIRE(after_writeoff.loan_assets == Approx(before_writeoff.loan_assets - 40.0f));
	REQUIRE(after_writeoff.net_worth == Approx(before_writeoff.net_worth - 40.0f));

	// Every failure is rejected before any deposit or reserve is changed.
	auto borrower_before_failure = ::economy::banking::deposit_balance(*state, borrower_deposit);
	auto reserve_before_failure = ::economy::accounts::balance(*state, reserve_a);
	REQUIRE_FALSE(::economy::banking::transfer_deposit(*state, borrower_deposit, other_bank_deposit,
		10000.0f, sys::date{6}));
	REQUIRE_FALSE(::economy::banking::transfer_deposit(*state, borrower_deposit,
		::economy::banking::open_deposit_account(*state, bank_b, other_bank_payee, wrong_settlement), 1.0f, sys::date{6}));
	REQUIRE(::economy::banking::bootstrap_set_reserve_balance(*state, reserve_a, 0.0f));
	REQUIRE_FALSE(::economy::banking::transfer_deposit(*state, borrower_deposit, other_bank_deposit, 1.0f, sys::date{6}));
	REQUIRE(::economy::banking::bootstrap_set_reserve_balance(*state, reserve_a, reserve_before_failure));
	REQUIRE(::economy::banking::deposit_balance(*state, borrower_deposit) == Approx(borrower_before_failure));
	REQUIRE(::economy::accounts::balance(*state, reserve_a) == Approx(reserve_before_failure));
	REQUIRE_FALSE(::economy::banking::repay_loan(*state, loan, same_bank_deposit, 1.0f, sys::date{7}));
	REQUIRE_FALSE(::economy::banking::open_deposit_account(*state,
		::actors::organizations::create_company(*state), borrower, settlement));
	REQUIRE_FALSE(::economy::banking::open_reserve_account(*state,
		::actors::organizations::create_company(*state), settlement));
	REQUIRE(bank_b_actor);
}

TEST_CASE("commercial_banking_state_survives_save_load", "[economy][banking][serialization]") {
	auto state = std::make_unique<sys::state>();
	auto settlement = state->world.create_commodity();
	auto second_settlement = state->world.create_commodity();
	auto bank = ::economy::banking::create_bank(*state);
	auto reserve = ::economy::banking::open_reserve_account(*state, bank, settlement);
	auto second_reserve = ::economy::banking::open_reserve_account(*state, bank, second_settlement);
	auto borrower = state->world.create_economic_actor();
	auto deposit = ::economy::banking::open_deposit_account(*state, bank, borrower, settlement);
	auto second_deposit = ::economy::banking::open_deposit_account(*state, bank, borrower, second_settlement);
	REQUIRE(::economy::banking::bootstrap_set_reserve_balance(*state, reserve, 321.0f));
	REQUIRE(::economy::banking::bootstrap_set_reserve_balance(*state, second_reserve, 654.0f));
	REQUIRE(::economy::banking::bootstrap_set_deposit_balance(*state, deposit, 45.0f));
	REQUIRE(::economy::banking::bootstrap_set_deposit_balance(*state, second_deposit, 23.0f));
	auto loan = ::economy::banking::originate_loan(*state, bank, deposit, 12.0f,
		sys::date{1}, sys::date{10}, 0.05f);
	REQUIRE(loan);

	std::vector<uint8_t> bytes(sys::sizeof_save_section(*state));
	auto const* end = sys::write_save_section(bytes.data(), *state);
	REQUIRE(end == bytes.data() + bytes.size());

	// The normal loader starts from the scenario-shaped object counts.
	auto loaded = std::make_unique<sys::state>();
	auto loaded_settlement = loaded->world.create_commodity();
	auto loaded_second_settlement = loaded->world.create_commodity();
	auto loaded_bank = ::economy::banking::create_bank(*loaded);
	auto loaded_reserve = ::economy::banking::open_reserve_account(*loaded, loaded_bank, loaded_settlement);
	auto loaded_second_reserve = ::economy::banking::open_reserve_account(*loaded, loaded_bank, loaded_second_settlement);
	auto loaded_borrower = loaded->world.create_economic_actor();
	auto loaded_deposit = ::economy::banking::open_deposit_account(*loaded, loaded_bank, loaded_borrower, loaded_settlement);
	auto loaded_second_deposit = ::economy::banking::open_deposit_account(*loaded, loaded_bank, loaded_borrower, loaded_second_settlement);
	auto loaded_loan = ::economy::banking::originate_loan(*loaded, loaded_bank, loaded_deposit, 12.0f,
		sys::date{1}, sys::date{10}, 0.05f);
	REQUIRE(loaded_loan);
	sys::read_save_section(bytes.data(), end, *loaded);

	REQUIRE(::economy::banking::reserve_account_for(*loaded, loaded_bank, loaded_settlement) == loaded_reserve);
	REQUIRE(::economy::banking::reserve_account_for(*loaded, loaded_bank, loaded_second_settlement) == loaded_second_reserve);
	REQUIRE(::economy::accounts::balance(*loaded, loaded_reserve) == Approx(321.0f));
	REQUIRE(::economy::accounts::balance(*loaded, loaded_second_reserve) == Approx(654.0f));
	REQUIRE(::economy::banking::deposit_balance(*loaded, loaded_deposit) == Approx(57.0f));
	REQUIRE(::economy::banking::deposit_balance(*loaded, loaded_second_deposit) == Approx(23.0f));
	REQUIRE(::economy::relations::total_due(*loaded, loaded_loan) == Approx(12.0f));
	REQUIRE(::economy::banking::bank_balance_sheet(*loaded, loaded_bank, loaded_settlement).loan_assets == Approx(12.0f));
	REQUIRE(::economy::banking::bank_balance_sheet(*loaded, loaded_bank, loaded_second_settlement).settlement_assets == Approx(654.0f));
	REQUIRE(::economy::banking::bank_balance_sheet(*loaded, loaded_bank, loaded_second_settlement).deposit_liabilities == Approx(23.0f));
}

TEST_CASE("commercial_banking_balance_sheets_are_settlement_specific", "[economy][banking]") {
	auto state = std::make_unique<sys::state>();
	auto usd = state->world.create_commodity();
	auto eur = state->world.create_commodity();
	auto bank = ::economy::banking::create_bank(*state);
	auto reserve_usd = ::economy::banking::open_reserve_account(*state, bank, usd);
	auto reserve_eur = ::economy::banking::open_reserve_account(*state, bank, eur);
	REQUIRE(::economy::banking::bootstrap_set_reserve_balance(*state, reserve_usd, 100.0f));
	REQUIRE(::economy::banking::bootstrap_set_reserve_balance(*state, reserve_eur, 200.0f));
	auto usd_owner = state->world.create_economic_actor();
	auto eur_owner = state->world.create_economic_actor();
	auto usd_deposit = ::economy::banking::open_deposit_account(*state, bank, usd_owner, usd);
	auto eur_deposit = ::economy::banking::open_deposit_account(*state, bank, eur_owner, eur);
	REQUIRE(::economy::banking::bootstrap_set_deposit_balance(*state, usd_deposit, 70.0f));

	auto eur_loan = ::economy::banking::originate_loan(*state, bank, eur_deposit, 40.0f,
		sys::date{1}, sys::date{10}, 0.0f);
	REQUIRE(eur_loan);
	auto usd_sheet = ::economy::banking::bank_balance_sheet(*state, bank, usd);
	auto eur_sheet = ::economy::banking::bank_balance_sheet(*state, bank, eur);
	REQUIRE(usd_sheet.settlement_assets == Approx(100.0f));
	REQUIRE(usd_sheet.loan_assets == Approx(0.0f));
	REQUIRE(usd_sheet.deposit_liabilities == Approx(70.0f));
	REQUIRE(usd_sheet.total_assets == Approx(100.0f));
	REQUIRE(usd_sheet.total_liabilities == Approx(70.0f));
	REQUIRE(usd_sheet.net_worth == Approx(30.0f));
	REQUIRE(eur_sheet.settlement_assets == Approx(200.0f));
	REQUIRE(eur_sheet.loan_assets == Approx(40.0f));
	REQUIRE(eur_sheet.deposit_liabilities == Approx(40.0f));
	REQUIRE(eur_sheet.net_worth == Approx(200.0f));
	REQUIRE(usd_sheet.settlement_assets != Approx(300.0f));
	REQUIRE(eur_sheet.settlement_assets != Approx(300.0f));

	auto generic_creditor = state->world.create_economic_actor();
	auto bank_actor = ::actors::organizations::actor_for_organization(*state, bank);
	auto bank_debt = ::economy::relations::create_obligation(*state, bank_actor, generic_creditor,
		50.0f, usd, sys::date{1}, sys::date{10}, 0.0f,
		::economy::relations::obligation_kind::trade_credit);
	REQUIRE(bank_debt);
	usd_sheet = ::economy::banking::bank_balance_sheet(*state, bank, usd);
	REQUIRE(usd_sheet.other_financial_liabilities == Approx(50.0f));
	REQUIRE(usd_sheet.net_worth == Approx(-20.0f));
	REQUIRE(::economy::relations::repay_obligation(*state, bank_debt, 20.0f) == Approx(20.0f));
	REQUIRE(::economy::banking::bank_balance_sheet(*state, bank, usd).other_financial_liabilities == Approx(30.0f));
	REQUIRE(::economy::relations::write_off(*state, bank_debt));
	usd_sheet = ::economy::banking::bank_balance_sheet(*state, bank, usd);
	REQUIRE(usd_sheet.other_financial_liabilities == Approx(0.0f));
	REQUIRE(usd_sheet.net_worth == Approx(30.0f));

	REQUIRE(::economy::banking::repay_loan(*state, eur_loan, eur_deposit, 15.0f, sys::date{2}) == Approx(15.0f));
	REQUIRE(::economy::banking::bank_balance_sheet(*state, bank, eur).loan_assets == Approx(25.0f));
	auto paid_loan = ::economy::banking::originate_loan(*state, bank, eur_deposit, 10.0f,
		sys::date{3}, sys::date{10}, 0.0f);
	REQUIRE(paid_loan);
	REQUIRE(::economy::banking::repay_loan(*state, paid_loan, eur_deposit, 10.0f, sys::date{4}) == Approx(10.0f));
	REQUIRE(state->world.obligation_get_status(paid_loan) == uint8_t(::economy::relations::obligation_status::paid));
	REQUIRE(::economy::banking::bank_balance_sheet(*state, bank, eur).loan_assets == Approx(25.0f));
	REQUIRE(::economy::relations::write_off(*state, eur_loan));
	REQUIRE(::economy::banking::bank_balance_sheet(*state, bank, eur).loan_assets == Approx(0.0f));
}

TEST_CASE("state_finance_treasury_tax_spending_and_public_debt", "[governance][finance]") {
	auto state = std::make_unique<sys::state>();
	auto nation = state->world.create_nation();
	auto institution = ::governance::create_institution(*state, nation, ::governance::institution_kind::central_government);
	auto office = ::governance::create_office(*state, institution, ::governance::office_kind::finance_minister);
	auto person = ::persons::create_person(*state, sys::date{1});
	REQUIRE(::persons::appoint_person(*state, person, office, sys::date{10}));
	REQUIRE(::governance::grant_authority_to_office(*state, office, ::governance::authority_kind::levy_tax, nation));
	REQUIRE(::governance::grant_authority_to_office(*state, office, ::governance::authority_kind::spend_public_funds, nation));
	REQUIRE(::governance::grant_authority_to_office(*state, office, ::governance::authority_kind::issue_public_debt, nation));
	auto settlement = state->world.create_commodity();
	auto treasury = ::governance::finance::open_treasury_account(*state, institution, settlement);
	REQUIRE(treasury);
	auto taxpayer = state->world.create_economic_actor();
	auto taxpayer_account = ::economy::accounts::open_account(*state, taxpayer, settlement);
	REQUIRE(::economy::accounts::bootstrap_set_balance(*state, taxpayer_account, 100.0f));
	auto assessment = ::governance::finance::authorized_assess_tax(*state, person, taxpayer, treasury,
		100.0f, sys::date{100}, sys::date{20});
	REQUIRE(assessment);
	auto tax = state->world.fiscal_action_get_obligation_from_fiscal_action_resulting_obligation(assessment);
	REQUIRE(tax);
	REQUIRE(::governance::finance::fiscal_position_for(*state, institution, settlement).tax_receivables == Approx(100.0f));
	REQUIRE(::governance::finance::pay_tax(*state, tax, taxpayer_account, treasury, 100.0f, sys::date{21}));
	REQUIRE(::economy::relations::total_due(*state, tax) == Approx(0.0f));
	REQUIRE(::economy::accounts::balance(*state, treasury) == Approx(100.0f));
	auto recipient = state->world.create_economic_actor();
	auto recipient_account = ::economy::accounts::open_account(*state, recipient, settlement);
	REQUIRE(::governance::finance::authorized_spend(*state, person, treasury, recipient_account, 40.0f, sys::date{22}));
	REQUIRE(::economy::accounts::balance(*state, treasury) == Approx(60.0f));
	auto investor = state->world.create_economic_actor();
	auto investor_account = ::economy::accounts::open_account(*state, investor, settlement);
	REQUIRE(::economy::accounts::bootstrap_set_balance(*state, investor_account, 50.0f));
	auto debt_action = ::governance::finance::authorized_issue_public_debt(*state, person, treasury,
		investor_account, 50.0f, sys::date{200}, 0.10f, sys::date{23});
	REQUIRE(debt_action);
	auto debt = state->world.fiscal_action_get_obligation_from_fiscal_action_resulting_obligation(debt_action);
	REQUIRE(debt);
	REQUIRE(::economy::relations::total_due(*state, debt) == Approx(50.0f));
	REQUIRE(::governance::finance::fiscal_position_for(*state, institution, settlement).public_debt_outstanding == Approx(50.0f));
	REQUIRE(::governance::finance::accrue_public_debt_interest(*state, debt, 10) == Approx(50.0f * 0.10f * 10.0f / 365.0f).epsilon(0.00001));
	REQUIRE(::governance::finance::service_public_debt(*state, debt, treasury, investor_account, 10.0f, sys::date{24}));
	REQUIRE(::economy::relations::total_due(*state, debt) == Approx(50.0f + 50.0f * 0.10f * 10.0f / 365.0f - 10.0f).epsilon(0.0001));
	REQUIRE(::governance::finance::fiscal_position_for(*state, institution, settlement).treasury_cash == Approx(100.0f));

	// Authority and all monetary validation happen before persisted mutations.
	auto before_actions = state->world.fiscal_action_size();
	auto foreign = state->world.create_commodity();
	auto foreign_account = ::economy::accounts::open_account(*state, recipient, foreign);
	REQUIRE_FALSE(::governance::finance::authorized_spend(*state, person, treasury, foreign_account, 1.0f, sys::date{25}));
	REQUIRE(state->world.fiscal_action_size() == before_actions);
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
