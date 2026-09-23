#include "catch.hpp"
#include "dcon_generated.hpp"
#include "container_types.hpp"
#include "system_state.hpp"
#include "serialization.hpp"
#include "economy/physical/deposits.hpp"
#include "economy/physical/inventory.hpp"
#include "economy/physical/shipments.hpp"
#include "economy/physical/extraction.hpp"
#include "economy/capital_projects.hpp"
#include "economy/physical/factory_output.hpp"
#include "economy/physical/factory_inputs.hpp"
#include "economy/physical/exchange.hpp"
#include "market_clearing.hpp"
#include "actors/ownership.hpp"
#include "actors/organizations/organizations.hpp"
#include "economy/relations/relations.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/banking/banking.hpp"
#include "economy/consent/consent.hpp"
#include "economy/information/information.hpp"
#include "governance/finance/finance.hpp"
#include "governance/law/law.hpp"
#include "governance/governance.hpp"
#include "persons/persons.hpp"
#include "governance/actions/actions.hpp"
#include <limits>

static dcon::obligation_id test_create_loan(sys::state& state, dcon::organization_id bank,
	dcon::deposit_account_id borrower_account, float principal, sys::date creation_date,
	sys::date due_date, float annual_interest_rate) {
	auto borrower = state.world.deposit_account_get_economic_actor_from_deposit_account_owner(borrower_account);
	auto settlement = state.world.deposit_account_get_commodity_from_deposit_account_settlement(borrower_account);
	auto lender = ::actors::organizations::actor_for_organization(state, bank);
	auto loan = ::economy::relations::create_obligation(state, borrower, lender, principal, settlement,
		creation_date, due_date, annual_interest_rate, ::economy::relations::obligation_kind::loan);
	if(loan)
		state.world.deposit_account_set_balance(borrower_account,
			state.world.deposit_account_get_balance(borrower_account) + principal);
	return loan;
}

static dcon::fiscal_action_id test_issue_public_debt(sys::state& state, dcon::person_id issuer,
	dcon::monetary_account_id treasury, dcon::monetary_account_id investor_account, float principal,
	sys::date due_date, float annual_interest_rate, sys::date date, dcon::person_id investor_person) {
	auto institution = ::governance::finance::treasury_institution_for(state, treasury);
	auto issuer_actor = ::governance::actor_for_institution(state, institution);
	auto investor_actor = ::economy::accounts::owner_of(state, investor_account);
	auto proposal = ::economy::consent::create_proposal(state, ::economy::consent::proposal_kind::investment,
		issuer_actor, investor_actor, ::economy::accounts::settlement_of(state, investor_account), principal,
		due_date, annual_interest_rate, date);
	if(!proposal) return {};
	if(auto organization = ::actors::organizations::organization_for_actor(state, investor_actor)) {
		if(!::economy::consent::create_mandate(state, organization, investor_person,
			::economy::consent::decision_kind::invest, date)) return {};
	}
	if(!::economy::consent::accept_proposal(state, proposal, investor_actor, investor_person, date)) return {};
	return ::governance::finance::authorized_issue_public_debt_with_consent(state, issuer, treasury,
		investor_account, principal, due_date, annual_interest_rate, date, proposal);
}

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

	::compat::alice::bootstrap_factory_sites(*state);
	auto site = state->world.factory_get_site_from_factory_site(factory);
	REQUIRE(site);
	REQUIRE(::world::site::site_for_factory(*state, factory) == site);
	REQUIRE(::world::site::province_for_site(*state, site) == province);
	REQUIRE(::world::spatial::site_position(*state, site) == glm::vec2{ 12.0f, 34.0f });

	auto site_count = state->world.site_size();
	::compat::alice::bootstrap_factory_sites(*state);
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
	auto deposit = ::economy::physical::deposits::deposit_for(*state, province, commodity);
	auto deposit_asset = ::actors::ownership::asset_for_deposit(*state, deposit);
	REQUIRE(deposit_asset);
	bool has_deposit_owner = false;
	state->world.asset_for_each_ownership_stake_asset_as_asset(deposit_asset, [&](dcon::ownership_stake_asset_id relation) {
		has_deposit_owner = has_deposit_owner || bool(state->world.ownership_stake_get_economic_actor_from_ownership_stake_owner(
			state->world.ownership_stake_asset_get_ownership_stake(relation)));
	});
	REQUIRE(has_deposit_owner);
	::economy::physical::deposits::bootstrap(*state);
	REQUIRE(state->world.resource_deposit_size() == 1);
	REQUIRE(::economy::physical::deposits::extraction_site_for(*state, province, commodity) == site);
}

TEST_CASE("canonical_deposit_creation_rejects_incomplete_values", "[economy][physical][capital]") {
	auto state = std::make_unique<sys::state>();
	auto site = state->world.create_site();
	auto commodity = state->world.create_commodity();
	auto before = state->world.resource_deposit_size();
	REQUIRE_FALSE(::economy::physical::deposits::create_deposit(*state, site, commodity, 10.0f, 11.0f, 1.0f, 1.0f, 1.0f));
	REQUIRE_FALSE(::economy::physical::deposits::create_deposit(*state, site, commodity, 10.0f, 10.0f, std::numeric_limits<float>::quiet_NaN(), 1.0f, 1.0f));
	REQUIRE_FALSE(::economy::physical::deposits::create_deposit(*state, site, commodity, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f));
	REQUIRE(state->world.resource_deposit_size() == before);
}

TEST_CASE("capital_extraction_project_completes_with_initialized_owned_deposit", "[economy][capital][physical]") {
	auto state = std::make_unique<sys::state>();
	auto province = state->world.create_province();
	auto site = state->world.create_site();
	state->world.force_create_site_location(site, province);
	auto settlement = state->world.create_commodity();
	auto resource = state->world.create_commodity();
	auto owner = state->world.create_economic_actor();
	auto responsible = ::actors::organizations::create_company(*state);
	auto project = ::economy::capital_projects::create(*state, ::economy::capital_projects::project_kind::extraction_site,
		owner, responsible, site, settlement, {}, resource, 100.0f, 0.8f, 20.0f, 15.0f);
	REQUIRE(project);
	state->world.capital_project_set_progress(project, 1.0f);
	REQUIRE(::economy::capital_projects::complete(*state, project));
	auto deposit = state->world.capital_project_get_resource_deposit_from_capital_project_deposit(project);
	REQUIRE(deposit);
	REQUIRE(state->world.resource_deposit_get_commodity(deposit) == resource);
	REQUIRE(state->world.resource_deposit_get_original_recoverable_reserves(deposit) == Approx(100.0f));
	REQUIRE(state->world.resource_deposit_get_remaining_recoverable_reserves(deposit) == Approx(100.0f));
	REQUIRE(state->world.resource_deposit_get_grade_or_quality(deposit) == Approx(0.8f));
	REQUIRE(state->world.resource_deposit_get_daily_extraction_capacity(deposit) == Approx(20.0f));
	REQUIRE(state->world.resource_deposit_get_target_daily_extraction(deposit) == Approx(15.0f));
	REQUIRE(state->world.resource_deposit_get_status(deposit) == 0);
	REQUIRE(state->world.resource_deposit_get_organization_from_resource_deposit_operator(deposit) == responsible);
	REQUIRE(::actors::ownership::asset_for_deposit(*state, deposit));
	REQUIRE(state->world.capital_project_get_asset_from_capital_project_asset(project));
	REQUIRE(state->world.capital_project_get_status(project) == uint8_t(::economy::capital_projects::status::completed));
}

TEST_CASE("capital_project_delivery_rolls_back_all_legs_on_cash_failure", "[economy][capital][physical]") {
	auto state = std::make_unique<sys::state>();
	auto province = state->world.create_province();
	auto seller_site = state->world.create_site();
	auto project_site = state->world.create_site();
	state->world.force_create_site_location(seller_site, province);
	state->world.force_create_site_location(project_site, province);
	auto settlement = state->world.create_commodity();
	auto goods = state->world.create_commodity();
	auto buyer = state->world.create_economic_actor();
	auto seller = state->world.create_economic_actor();
	auto responsible = ::actors::organizations::create_company(*state);
	auto project = ::economy::capital_projects::create(*state, ::economy::capital_projects::project_kind::factory,
		buyer, responsible, project_site, settlement, dcon::factory_type_id{}, goods);
	// The factory type is not needed for material delivery; use an ordinary
	// planned project fixture and replace only the delivery-side state.
	if(!project) {
		project = ::economy::capital_projects::create(*state, ::economy::capital_projects::project_kind::infrastructure,
			buyer, responsible, project_site, settlement, {}, goods);
	}
	REQUIRE(project);
	auto project_account = state->world.capital_project_get_monetary_account_from_capital_project_account(project);
	auto seller_account = ::economy::accounts::open_account(*state, seller, settlement);
	REQUIRE(project_account); REQUIRE(seller_account);
	REQUIRE(::economy::accounts::bootstrap_set_balance(*state, project_account, 10.0f));
	REQUIRE(::economy::accounts::bootstrap_set_balance(*state, seller_account, 0.0f));
	REQUIRE(::economy::physical::inventory::add(*state, seller_site, goods, 20.0f, seller) == Approx(20.0f));
	auto transactions_before = state->world.transaction_size();
	auto shipments_before = state->world.shipment_size();
	REQUIRE_FALSE(::economy::capital_projects::deliver_material(*state, project, seller_account, seller_site, goods, 5.0f, 50.0f));
	REQUIRE(::economy::accounts::balance(*state, project_account) == Approx(10.0f));
	REQUIRE(::economy::accounts::balance(*state, seller_account) == Approx(0.0f));
	REQUIRE(::economy::physical::inventory::quantity(*state, seller_site, goods, seller) == Approx(20.0f));
	REQUIRE(state->world.shipment_size() == shipments_before);
	REQUIRE(state->world.transaction_size() == transactions_before);
}

TEST_CASE("capital_project_completion_failure_leaves_no_target_or_ownership", "[economy][capital][physical]") {
	auto state = std::make_unique<sys::state>();
	auto site = state->world.create_site(); // deliberately has no province location
	auto settlement = state->world.create_commodity();
	auto owner = state->world.create_economic_actor();
	auto responsible = ::actors::organizations::create_company(*state);
	auto factory_type = state->world.create_factory_type();
	auto project = ::economy::capital_projects::create(*state, ::economy::capital_projects::project_kind::factory,
		owner, responsible, site, settlement, factory_type, {});
	REQUIRE(project);
	state->world.capital_project_set_progress(project, 1.0f);
	auto deposits_before = state->world.resource_deposit_size();
	auto assets_before = state->world.asset_size();
	auto stakes_before = state->world.ownership_stake_size();
	REQUIRE_FALSE(::economy::capital_projects::complete(*state, project));
	REQUIRE(state->world.capital_project_get_status(project) != uint8_t(::economy::capital_projects::status::completed));
	REQUIRE(state->world.capital_project_get_resource_deposit_from_capital_project_deposit(project) == dcon::resource_deposit_id{});
	REQUIRE(state->world.capital_project_get_asset_from_capital_project_asset(project) == dcon::asset_id{});
	REQUIRE(state->world.resource_deposit_size() == deposits_before);
	REQUIRE(state->world.asset_size() == assets_before);
	REQUIRE(state->world.ownership_stake_size() == stakes_before);
}

TEST_CASE("canonical_rgo_bootstrap_isolated_from_legacy_output", "[economy][physical][integration]") {
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
	auto deposit = ::economy::physical::deposits::deposit_for(*state, province, commodity);
	REQUIRE(deposit);
	REQUIRE_FALSE(state->world.resource_deposit_get_legacy_compatibility_deposit(deposit));
	auto extraction_site = ::economy::physical::deposits::extraction_site_for(*state, province, commodity);
	auto operator_actor = ::actors::ownership::operator_for_deposit(*state, deposit);
	auto inventory_before = ::economy::physical::inventory::quantity(*state, extraction_site, commodity, operator_actor);
	auto remaining = state->world.resource_deposit_get_remaining_recoverable_reserves(deposit);
	::economy::physical::shipments::process_rgo_output(*state);
	REQUIRE(state->world.shipment_size() == 0);
	REQUIRE(state->world.market_get_stockpile(market, commodity) == Approx(0.0f));
	REQUIRE(state->world.market_get_supply(market, commodity) == Approx(0.0f));
	REQUIRE(::economy::physical::inventory::quantity(*state, extraction_site, commodity, operator_actor) == Approx(inventory_before));
	state->world.province_set_rgo_output(province, commodity, 999.0f);
	::economy::physical::shipments::process_rgo_output(*state);
	REQUIRE(state->world.shipment_size() == 0);
	REQUIRE(state->world.resource_deposit_get_remaining_recoverable_reserves(deposit) == Approx(remaining));
	REQUIRE(::economy::physical::inventory::quantity(*state, extraction_site, commodity, operator_actor) == Approx(inventory_before));
}

TEST_CASE("canonical_resource_extraction_is_bounded_and_owner_separate", "[economy][physical][extraction]") {
	auto state = std::make_unique<sys::state>();
	auto province = state->world.create_province();
	auto site = state->world.create_site();
	state->world.force_create_site_location(site, province);
	auto commodity = state->world.create_commodity();
	auto deposit = state->world.create_resource_deposit();
	state->world.resource_deposit_set_commodity(deposit, commodity);
	state->world.resource_deposit_set_original_recoverable_reserves(deposit, 100.0f);
	state->world.resource_deposit_set_remaining_recoverable_reserves(deposit, 100.0f);
	state->world.resource_deposit_set_grade_or_quality(deposit, 1.0f);
	state->world.resource_deposit_set_daily_extraction_capacity(deposit, 20.0f);
	state->world.resource_deposit_set_target_daily_extraction(deposit, 20.0f);
	state->world.resource_deposit_set_status(deposit, 0);
	state->world.force_create_resource_deposit_site(deposit, site);
	auto owner = state->world.create_economic_actor();
	auto operator_org = ::actors::organizations::create_company(*state);
	auto operator_actor = ::actors::organizations::actor_for_organization(*state, operator_org);
	REQUIRE(::actors::organizations::bind_deposit_operator(*state, operator_org, deposit));
	auto asset = state->world.create_asset();
	state->world.force_create_resource_deposit_asset(deposit, asset);
	REQUIRE(::actors::ownership::create_stake(*state, owner, asset, 1.0f, 1.0f, 1.0f));
	auto right = state->world.create_resource_extraction_right();
	state->world.resource_extraction_right_set_valid_from(right, sys::date{0});
	state->world.resource_extraction_right_set_valid_until(right, sys::date{100});
	state->world.resource_extraction_right_set_max_daily_quantity(right, 20.0f);
	state->world.resource_extraction_right_set_status(right, 0);
	state->world.force_create_resource_extraction_right_deposit(right, deposit);
	state->world.force_create_resource_extraction_right_holder(right, operator_actor);
	REQUIRE(::economy::physical::extraction::extract_resource(*state, deposit, operator_actor, 10.0f, sys::date{1}) == Approx(10.0f));
	REQUIRE(state->world.resource_deposit_get_remaining_recoverable_reserves(deposit) == Approx(90.0f));
	REQUIRE(::economy::physical::inventory::quantity(*state, site, commodity, operator_actor) == Approx(10.0f));
	REQUIRE(state->world.extraction_event_size() == 1);
	REQUIRE(::economy::physical::extraction::extract_resource(*state, deposit, operator_actor, 50.0f, sys::date{1}) == Approx(10.0f));
	REQUIRE(state->world.resource_deposit_get_remaining_recoverable_reserves(deposit) == Approx(80.0f));
	REQUIRE(state->world.extraction_event_size() == 2);
	REQUIRE(::actors::ownership::asset_for_deposit(*state, deposit) == asset);
	REQUIRE(::economy::physical::inventory::quantity(*state, site, commodity, owner) == Approx(0.0f));
	REQUIRE(::economy::physical::inventory::quantity(*state, site, commodity, operator_actor) == Approx(20.0f));
}

TEST_CASE("canonical_extraction_without_right_has_zero_mutation", "[economy][physical][extraction]") {
	auto state = std::make_unique<sys::state>();
	auto province = state->world.create_province();
	auto site = state->world.create_site();
	state->world.force_create_site_location(site, province);
	auto commodity = state->world.create_commodity();
	auto deposit = state->world.create_resource_deposit();
	state->world.resource_deposit_set_commodity(deposit, commodity);
	state->world.resource_deposit_set_remaining_recoverable_reserves(deposit, 7.0f);
	state->world.resource_deposit_set_daily_extraction_capacity(deposit, 7.0f);
	state->world.resource_deposit_set_target_daily_extraction(deposit, 7.0f);
	state->world.resource_deposit_set_status(deposit, 0);
	state->world.force_create_resource_deposit_site(deposit, site);
	auto organization = ::actors::organizations::create_company(*state);
	auto actor = ::actors::organizations::actor_for_organization(*state, organization);
	REQUIRE(::actors::organizations::bind_deposit_operator(*state, organization, deposit));
	state->world.force_create_resource_deposit_asset(deposit, state->world.create_asset());
	REQUIRE(::economy::physical::extraction::extract_resource(*state, deposit, actor, 7.0f, sys::date{1}) == Approx(0.0f));
	REQUIRE(state->world.resource_deposit_get_remaining_recoverable_reserves(deposit) == Approx(7.0f));
	REQUIRE(state->world.extraction_event_size() == 0);
	REQUIRE(::economy::physical::inventory::quantity(*state, site, commodity, actor) == Approx(0.0f));
}

TEST_CASE("physical_exchange_settles_concrete_stock_and_cash_atomically", "[economy][physical][exchange]") {
	auto state = std::make_unique<sys::state>();
	auto site = state->world.create_site();
	state->world.create_commodity();
	auto settlement = state->world.create_commodity();
	auto goods = state->world.create_commodity();
	auto seller = state->world.create_economic_actor();
	auto buyer = state->world.create_economic_actor();
	REQUIRE(settlement);
	REQUIRE(economy::accounts::open_account(*state, seller, settlement));
	REQUIRE(economy::accounts::open_account(*state, buyer, settlement));
	auto seller_account = economy::accounts::find_account(*state, seller, settlement);
	auto buyer_account = economy::accounts::find_account(*state, buyer, settlement);
	REQUIRE(economy::accounts::bootstrap_set_balance(*state, seller_account, 0.0f));
	REQUIRE(economy::accounts::bootstrap_set_balance(*state, buyer_account, 1000.0f));
	REQUIRE(economy::physical::inventory::add(*state, site, goods, 100.0f, seller) == Approx(100.0f));
	auto transactions_before = state->world.transaction_size();
	REQUIRE(economy::physical::exchange::purchase(*state, site, goods, seller, buyer, 20.0f, 5.0f, settlement, sys::date{}));
	REQUIRE(economy::physical::inventory::quantity(*state, site, goods, seller) == Approx(80.0f));
	REQUIRE(economy::physical::inventory::quantity(*state, site, goods, buyer) == Approx(20.0f));
	REQUIRE(economy::accounts::balance(*state, buyer_account) == Approx(900.0f));
	REQUIRE(economy::accounts::balance(*state, seller_account) == Approx(100.0f));
	REQUIRE(state->world.transaction_size() == transactions_before + 1);
	REQUIRE_FALSE(economy::physical::exchange::purchase(*state, site, goods, seller, buyer, 1000.0f, 5.0f, settlement, sys::date{}));
	REQUIRE_FALSE(economy::physical::exchange::purchase(*state, site, goods, seller, buyer, 1.0f, 1000.0f, settlement, sys::date{}));
	REQUIRE_FALSE(economy::physical::exchange::purchase(*state, site, goods, seller, buyer, 0.0f, 5.0f, settlement, sys::date{}));
	REQUIRE_FALSE(economy::physical::exchange::purchase(*state, site, goods, seller, seller, 1.0f, 5.0f, settlement, sys::date{}));
	REQUIRE(state->world.transaction_size() == transactions_before + 1);
	REQUIRE(economy::physical::inventory::quantity(*state, site, goods, seller) == Approx(80.0f));
	REQUIRE(economy::physical::inventory::quantity(*state, site, goods, buyer) == Approx(20.0f));
	REQUIRE(economy::accounts::balance(*state, buyer_account) == Approx(900.0f));
	REQUIRE(economy::accounts::balance(*state, seller_account) == Approx(100.0f));
	REQUIRE_FALSE(economy::physical::exchange::purchase(*state, site, goods, seller, buyer, 1.0f, 5.0f, dcon::commodity_id{}, sys::date{}));
	REQUIRE(state->world.transaction_size() == transactions_before + 1);
}

TEST_CASE("physical_factory_output_uses_operator_owned_shipment", "[economy][physical][factory]") {
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
	::compat::alice::bootstrap_factory_sites(*state);
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
	REQUIRE(::economy::physical::inventory::quantity(*state, hub, commodity, operator_actor) == Approx(produced * std::pow(1.0f - economy::logistics::profile_for(*state, commodity).daily_spoilage, float(travel_days))).epsilon(0.00001));
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
	REQUIRE(availability.fully_canonical);
	REQUIRE(availability.legacy_ratio == Approx(1.0f));
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
	REQUIRE(full.fully_canonical);
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
	REQUIRE_FALSE(local.fully_canonical);
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
	auto seller = state->world.create_economic_actor();
	state->world.create_commodity();
	auto settlement = state->world.create_commodity();
	auto commodity = state->world.create_commodity();
	state->world.market_resize_stockpile(state->world.commodity_size());
	state->world.market_resize_actual_probability_to_buy(state->world.commodity_size());
	state->world.market_resize_price(state->world.commodity_size());
	state->world.commodity_set_cost(commodity, 5.0f);
	state->world.market_set_price(market, commodity, 5.0f);
	state->world.commodity_set_is_local(commodity, false);
	state->world.commodity_set_money_rgo(commodity, false);
	economy::commodity_set inputs{};
	inputs.commodity_type[0] = commodity;
	inputs.commodity_amounts[0] = 4.0f;
	REQUIRE(economy::accounts::open_account(*state, owner, settlement));
	REQUIRE(economy::accounts::open_account(*state, seller, settlement));
	auto buyer_account = economy::accounts::find_account(*state, owner, settlement);
	REQUIRE(economy::accounts::bootstrap_set_balance(*state, buyer_account, 100.0f));
	REQUIRE(::economy::physical::inventory::add(*state, hub, commodity, 4.0f, seller) == Approx(4.0f));

	::economy::physical::factory_inputs::begin_planning(*state);
	REQUIRE(::economy::physical::factory_inputs::plan(*state, factory, destination, owner, inputs, market, 1.0f));
	REQUIRE(::economy::physical::factory_inputs::planned_quantity(*state, factory, commodity, -1.0f) == Approx(4.0f));
	// The market has not settled yet, and fulfillment must not touch its stockpile.
	REQUIRE(state->world.market_get_stockpile(market, commodity) == Approx(0.0f));
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
	::economy::physical::shipments::process_legacy_rgo_output(*state);
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

	auto loan = test_create_loan(*state, bank_a, borrower_deposit, 100.0f,
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
	auto second_loan = test_create_loan(*state, bank_a, same_bank_deposit, 40.0f,
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
	auto loan = test_create_loan(*state, bank, deposit, 12.0f,
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
	auto loaded_loan = test_create_loan(*loaded, loaded_bank, loaded_deposit, 12.0f,
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

	auto eur_loan = test_create_loan(*state, bank, eur_deposit, 40.0f,
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
	auto paid_loan = test_create_loan(*state, bank, eur_deposit, 10.0f,
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
	auto before_invalid_assessment = state->world.obligation_size();
	auto before_invalid_actions = state->world.fiscal_action_size();
	REQUIRE_FALSE(::governance::finance::authorized_assess_tax(*state, person, taxpayer, treasury,
		1.0f, sys::date{19}, sys::date{20}));
	REQUIRE(state->world.obligation_size() == before_invalid_assessment);
	REQUIRE(state->world.fiscal_action_size() == before_invalid_actions);
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
	auto investor_person = ::persons::create_person(*state, sys::date{1});
	auto investor = ::persons::actor_for_person(*state, investor_person);
	auto investor_account = ::economy::accounts::open_account(*state, investor, settlement);
	REQUIRE(::economy::accounts::bootstrap_set_balance(*state, investor_account, 50.0f));
	auto debt_action = test_issue_public_debt(*state, person, treasury, investor_account,
		50.0f, sys::date{200}, 0.10f, sys::date{23}, investor_person);
	REQUIRE(debt_action);
	auto debt = state->world.fiscal_action_get_obligation_from_fiscal_action_resulting_obligation(debt_action);
	REQUIRE(debt);
	REQUIRE(::economy::relations::total_due(*state, debt) == Approx(50.0f));
	REQUIRE(::governance::finance::fiscal_position_for(*state, institution, settlement).public_debt_outstanding == Approx(50.0f));
	REQUIRE(::governance::finance::accrue_public_debt_interest(*state, debt, 10) == Approx(50.0f * 0.10f * 10.0f / 365.0f).epsilon(0.00001));
	REQUIRE(::governance::finance::service_public_debt(*state, debt, treasury, investor_account, 10.0f, sys::date{24}));
	REQUIRE(::economy::relations::total_due(*state, debt) == Approx(50.0f + 50.0f * 0.10f * 10.0f / 365.0f - 10.0f).epsilon(0.0001));
	REQUIRE(::governance::finance::fiscal_position_for(*state, institution, settlement).treasury_cash == Approx(100.0f));

	// Defaulted claims remain visible to derived views but are not payable by these APIs.
	auto defaulted_tax = ::economy::relations::create_obligation(*state, taxpayer,
		::governance::actor_for_institution(*state, institution), 10.0f, settlement,
		sys::date{25}, sys::date{30}, 0.0f, ::economy::relations::obligation_kind::tax);
	state->world.obligation_set_status(defaulted_tax, uint8_t(::economy::relations::obligation_status::defaulted));
	REQUIRE(::economy::accounts::bootstrap_set_balance(*state, taxpayer_account, 10.0f));
	auto taxpayer_cash_before_default = ::economy::accounts::balance(*state, taxpayer_account);
	auto treasury_cash_before_default = ::economy::accounts::balance(*state, treasury);
	REQUIRE_FALSE(::governance::finance::pay_tax(*state, defaulted_tax, taxpayer_account, treasury, 10.0f, sys::date{26}));
	REQUIRE(::economy::accounts::balance(*state, taxpayer_account) == Approx(taxpayer_cash_before_default));
	REQUIRE(::economy::accounts::balance(*state, treasury) == Approx(treasury_cash_before_default));
	REQUIRE(::economy::relations::total_due(*state, defaulted_tax) == Approx(10.0f));

	// Authority and all monetary validation happen before persisted mutations.
	auto before_actions = state->world.fiscal_action_size();
	auto foreign = state->world.create_commodity();
	auto foreign_account = ::economy::accounts::open_account(*state, recipient, foreign);
	REQUIRE_FALSE(::governance::finance::authorized_spend(*state, person, treasury, foreign_account, 1.0f, sys::date{25}));
	REQUIRE(state->world.fiscal_action_size() == before_actions);

	auto bank = ::economy::banking::create_bank(*state);
	auto bank_reserve = ::economy::banking::open_reserve_account(*state, bank, settlement);
	REQUIRE(::economy::banking::bootstrap_set_reserve_balance(*state, bank_reserve, 500.0f));
	auto bank_representative = ::persons::create_person(*state, sys::date{1});
	auto bank_debt_action = test_issue_public_debt(*state, person, treasury, bank_reserve,
		100.0f, sys::date{300}, 0.0f, sys::date{27}, bank_representative);
	REQUIRE(bank_debt_action);
	auto bank_debt = state->world.fiscal_action_get_obligation_from_fiscal_action_resulting_obligation(bank_debt_action);
	REQUIRE(::economy::accounts::balance(*state, bank_reserve) == Approx(400.0f));
	auto bank_sheet = ::economy::banking::bank_balance_sheet(*state, bank, settlement);
	REQUIRE(bank_sheet.public_debt_assets == Approx(100.0f));
	REQUIRE(bank_sheet.total_assets == Approx(500.0f));
	REQUIRE(bank_sheet.net_worth == Approx(500.0f));
	REQUIRE(::economy::accounts::balance(*state, treasury) == Approx(200.0f));
	REQUIRE(::governance::finance::service_public_debt(*state, bank_debt, treasury, bank_reserve, 40.0f, sys::date{28}));
	REQUIRE(::economy::accounts::balance(*state, treasury) == Approx(160.0f));
	REQUIRE(::economy::accounts::balance(*state, bank_reserve) == Approx(440.0f));
	REQUIRE(::economy::banking::bank_balance_sheet(*state, bank, settlement).public_debt_assets == Approx(60.0f));
	state->world.obligation_set_status(bank_debt, uint8_t(::economy::relations::obligation_status::defaulted));
	auto treasury_before_default_service = ::economy::accounts::balance(*state, treasury);
	auto reserve_before_default_service = ::economy::accounts::balance(*state, bank_reserve);
	REQUIRE_FALSE(::governance::finance::service_public_debt(*state, bank_debt, treasury, bank_reserve, 1.0f, sys::date{29}));
	REQUIRE(::economy::accounts::balance(*state, treasury) == Approx(treasury_before_default_service));
	REQUIRE(::economy::accounts::balance(*state, bank_reserve) == Approx(reserve_before_default_service));
	REQUIRE(::economy::relations::total_due(*state, bank_debt) == Approx(60.0f));
}

TEST_CASE("law_policy_controls_public_debt_with_historical_effectiveness", "[governance][law][finance]") {
	auto state = std::make_unique<sys::state>();
	auto nation = state->world.create_nation();
	auto institution = ::governance::create_institution(*state, nation, ::governance::institution_kind::central_government);
	auto office = ::governance::create_office(*state, institution, ::governance::office_kind::finance_minister);
	auto person = ::persons::create_person(*state, sys::date{1});
	REQUIRE(::persons::appoint_person(*state, person, office, sys::date{1}));
	REQUIRE(::governance::grant_authority_to_office(*state, office, ::governance::authority_kind::legislate, nation));
	REQUIRE(::governance::grant_authority_to_office(*state, office, ::governance::authority_kind::issue_public_debt, nation));
	auto second_office = ::governance::create_office(*state, institution, ::governance::office_kind::agency_director);
	auto second_person = ::persons::create_person(*state, sys::date{2});
	REQUIRE(::persons::appoint_person(*state, second_person, second_office, sys::date{20}));
	REQUIRE(::governance::grant_authority_to_office(*state, second_office, ::governance::authority_kind::legislate, nation));
	auto settlement = state->world.create_commodity();
	auto treasury = ::governance::finance::open_treasury_account(*state, institution, settlement);
	auto investor_person = ::persons::create_person(*state, sys::date{1});
	auto investor = ::persons::actor_for_person(*state, investor_person);
	auto investor_account = ::economy::accounts::open_account(*state, investor, settlement);
	REQUIRE(::economy::accounts::bootstrap_set_balance(*state, investor_account, 1000.0f));
	// Future-effective law does not constrain an earlier issue.
	REQUIRE(test_issue_public_debt(*state, person, treasury, investor_account, 80.0f, sys::date{100}, 0.0f, sys::date{15}, investor_person));
	auto draft = ::governance::law::create_draft_instrument(*state, ::governance::law::legal_instrument_kind::statute, nation);
	REQUIRE(draft);
	auto invalid_count = state->world.legal_instrument_size();
	REQUIRE_FALSE(::governance::law::create_draft_instrument(*state, ::governance::law::legal_instrument_kind::statute, dcon::nation_id{9999}));
	REQUIRE(state->world.legal_instrument_size() == invalid_count);
	REQUIRE(::governance::law::add_public_debt_ceiling_rule(*state, draft, settlement, 100.0f));
	REQUIRE_FALSE(::governance::law::add_public_debt_ceiling_rule(*state, draft, settlement, 200.0f));
	auto no_legislate = ::persons::create_person(*state, sys::date{1});
	auto no_legislate_office = ::governance::create_office(*state, institution, ::governance::office_kind::agency_director);
	REQUIRE(::persons::appoint_person(*state, no_legislate, no_legislate_office, sys::date{1}));
	REQUIRE_FALSE(::governance::law::authorized_enact(*state, no_legislate, draft, sys::date{10}, sys::date{10}));
	auto regulation_without_regulate = ::governance::law::create_draft_instrument(*state, ::governance::law::legal_instrument_kind::regulation, nation);
	REQUIRE(::governance::law::add_public_debt_prohibition_rule(*state, regulation_without_regulate, settlement));
	REQUIRE_FALSE(::governance::law::authorized_enact(*state, second_person, regulation_without_regulate, sys::date{10}, sys::date{10}));
	REQUIRE_FALSE(::governance::law::authorized_enact(*state, second_person, draft, sys::date{19}, sys::date{19}));
	auto dead_person = ::persons::create_person(*state, sys::date{1});
	REQUIRE(::persons::mark_dead(*state, dead_person, sys::date{2}));
	REQUIRE_FALSE(::governance::law::authorized_enact(*state, dead_person, draft, sys::date{10}, sys::date{10}));
	auto enactment = ::governance::law::authorized_enact(*state, person, draft, sys::date{10}, sys::date{20});
	REQUIRE(enactment);
	REQUIRE(!::governance::law::public_debt_policy_for(*state, nation, settlement, sys::date{15}).ceiling);
	REQUIRE(::governance::law::public_debt_policy_for(*state, nation, settlement, sys::date{20}).ceiling == Approx(100.0f));
	auto conflicting = ::governance::law::create_draft_instrument(*state, ::governance::law::legal_instrument_kind::statute, nation);
	REQUIRE(::governance::law::add_public_debt_ceiling_rule(*state, conflicting, settlement, 200.0f));
	REQUIRE_FALSE(::governance::law::authorized_enact(*state, person, conflicting, sys::date{21}, sys::date{21}));
	auto second_settlement = state->world.create_commodity();
	auto future_a = ::governance::law::create_draft_instrument(*state, ::governance::law::legal_instrument_kind::statute, nation);
	REQUIRE(::governance::law::add_public_debt_ceiling_rule(*state, future_a, second_settlement, 100.0f));
	REQUIRE(::governance::law::authorized_enact(*state, person, future_a, sys::date{10}, sys::date{40}));
	auto future_b = ::governance::law::create_draft_instrument(*state, ::governance::law::legal_instrument_kind::statute, nation);
	REQUIRE(::governance::law::add_public_debt_ceiling_rule(*state, future_b, second_settlement, 200.0f));
	REQUIRE_FALSE(::governance::law::authorized_enact(*state, person, future_b, sys::date{20}, sys::date{20}));
	auto obligations_before = state->world.obligation_size();
	auto transactions_before = state->world.transaction_size();
	auto actions_before = state->world.fiscal_action_size();
	auto treasury_before = ::economy::accounts::balance(*state, treasury);
	auto investor_before = ::economy::accounts::balance(*state, investor_account);
	REQUIRE_FALSE(test_issue_public_debt(*state, person, treasury, investor_account, 30.0f, sys::date{100}, 0.0f, sys::date{20}, investor_person));
	REQUIRE(state->world.obligation_size() == obligations_before);
	REQUIRE(state->world.transaction_size() == transactions_before);
	REQUIRE(state->world.fiscal_action_size() == actions_before);
	REQUIRE(::economy::accounts::balance(*state, treasury) == Approx(treasury_before));
	REQUIRE(::economy::accounts::balance(*state, investor_account) == Approx(investor_before));
	REQUIRE(test_issue_public_debt(*state, person, treasury, investor_account, 20.0f, sys::date{100}, 0.0f, sys::date{21}, investor_person));
	auto repeal_action = ::governance::law::authorized_repeal(*state, second_person, draft, sys::date{30});
	REQUIRE(repeal_action);
	REQUIRE(state->world.legal_action_get_person_from_legal_action_initiator(repeal_action) == second_person);
	REQUIRE(state->world.legal_action_get_office_from_legal_action_authorizing_office(repeal_action) == second_office);
	REQUIRE(state->world.legal_action_get_institution_from_legal_action_issuing_institution(repeal_action) == institution);
	REQUIRE(state->world.legal_instrument_get_person_from_legal_instrument_enactor(draft) == person);
	REQUIRE(state->world.legal_instrument_get_office_from_legal_instrument_authorizing_office(draft) == office);
	REQUIRE(::governance::law::instrument_is_effective(*state, draft, sys::date{25}));
	REQUIRE_FALSE(::governance::law::instrument_is_effective(*state, draft, sys::date{30}));
	auto unauthorized_repealer = ::persons::create_person(*state, sys::date{1});
	REQUIRE_FALSE(::governance::law::authorized_repeal(*state, unauthorized_repealer, draft, sys::date{25}));
	REQUIRE(test_issue_public_debt(*state, person, treasury, investor_account, 30.0f, sys::date{100}, 0.0f, sys::date{31}, investor_person));

	auto prohibited = ::governance::law::create_draft_instrument(*state, ::governance::law::legal_instrument_kind::regulation, nation);
	REQUIRE(::governance::grant_authority_to_office(*state, office, ::governance::authority_kind::regulate, nation));
	REQUIRE(::governance::law::add_public_debt_prohibition_rule(*state, prohibited, settlement));
	REQUIRE(::governance::law::authorized_enact(*state, person, prohibited, sys::date{40}, sys::date{40}));
	auto after_prohibition = state->world.obligation_size();
	REQUIRE_FALSE(test_issue_public_debt(*state, person, treasury, investor_account, 1.0f, sys::date{100}, 0.0f, sys::date{41}, investor_person));
	REQUIRE(state->world.obligation_size() == after_prohibition);
	auto replacement = ::governance::law::create_draft_instrument(*state, ::governance::law::legal_instrument_kind::statute, nation);
	REQUIRE(::governance::law::add_public_debt_ceiling_rule(*state, replacement, settlement, 200.0f));
	REQUIRE(::governance::law::authorized_enact(*state, person, replacement, sys::date{30}, sys::date{30}));

}

TEST_CASE("law_relations_survive_save_load", "[governance][law][serialization]") {
	auto state = std::make_unique<sys::state>();
	auto nation = state->world.create_nation();
	auto institution = ::governance::create_institution(*state, nation, ::governance::institution_kind::central_government);
	auto office = ::governance::create_office(*state, institution, ::governance::office_kind::finance_minister);
	auto person = ::persons::create_person(*state, sys::date{1});
	REQUIRE(::persons::appoint_person(*state, person, office, sys::date{1}));
	REQUIRE(::governance::grant_authority_to_office(*state, office, ::governance::authority_kind::legislate, nation));
	REQUIRE(::governance::grant_authority_to_office(*state, office, ::governance::authority_kind::regulate, nation));
	auto settlement = state->world.create_commodity();
	auto ceiling = ::governance::law::create_draft_instrument(*state, ::governance::law::legal_instrument_kind::statute, nation);
	REQUIRE(::governance::law::add_public_debt_ceiling_rule(*state, ceiling, settlement, 100.0f));
	auto enactment = ::governance::law::authorized_enact(*state, person, ceiling, sys::date{10}, sys::date{20});
	auto prohibition = ::governance::law::create_draft_instrument(*state, ::governance::law::legal_instrument_kind::regulation, nation);
	REQUIRE(::governance::law::add_public_debt_prohibition_rule(*state, prohibition, settlement));
	REQUIRE(::governance::law::authorized_enact(*state, person, prohibition, sys::date{12}, sys::date{12}));
	auto repeal = ::governance::law::authorized_repeal(*state, person, prohibition, sys::date{30});
	REQUIRE(enactment); REQUIRE(repeal);

	std::vector<uint8_t> bytes(sys::sizeof_save_section(*state));
	auto const* end = sys::write_save_section(bytes.data(), *state);
	auto loaded = std::make_unique<sys::state>();
	auto lnation = loaded->world.create_nation();
	auto linstitution = ::governance::create_institution(*loaded, lnation, ::governance::institution_kind::central_government);
	auto loffice = ::governance::create_office(*loaded, linstitution, ::governance::office_kind::finance_minister);
	auto lperson = ::persons::create_person(*loaded, sys::date{1});
	REQUIRE(::persons::appoint_person(*loaded, lperson, loffice, sys::date{1}));
	REQUIRE(::governance::grant_authority_to_office(*loaded, loffice, ::governance::authority_kind::legislate, lnation));
	REQUIRE(::governance::grant_authority_to_office(*loaded, loffice, ::governance::authority_kind::regulate, lnation));
	auto lsettlement = loaded->world.create_commodity();
	auto lceiling = ::governance::law::create_draft_instrument(*loaded, ::governance::law::legal_instrument_kind::statute, lnation);
	REQUIRE(::governance::law::add_public_debt_ceiling_rule(*loaded, lceiling, lsettlement, 100.0f));
	auto lenactment = ::governance::law::authorized_enact(*loaded, lperson, lceiling, sys::date{10}, sys::date{20});
	auto lprohibition = ::governance::law::create_draft_instrument(*loaded, ::governance::law::legal_instrument_kind::regulation, lnation);
	REQUIRE(::governance::law::add_public_debt_prohibition_rule(*loaded, lprohibition, lsettlement));
	REQUIRE(::governance::law::authorized_enact(*loaded, lperson, lprohibition, sys::date{12}, sys::date{12}));
	auto lrepeal = ::governance::law::authorized_repeal(*loaded, lperson, lprohibition, sys::date{30});
	REQUIRE(lenactment); REQUIRE(lrepeal);
	sys::read_save_section(bytes.data(), end, *loaded);
	REQUIRE(loaded->world.legal_instrument_get_nation_from_legal_instrument_nation_jurisdiction(lceiling) == lnation);
	REQUIRE(loaded->world.legal_instrument_get_person_from_legal_instrument_enactor(lceiling) == lperson);
	REQUIRE(loaded->world.legal_instrument_get_office_from_legal_instrument_authorizing_office(lceiling) == loffice);
	REQUIRE(loaded->world.legal_instrument_get_institution_from_legal_instrument_issuing_institution(lceiling) == linstitution);
	REQUIRE(loaded->world.legal_instrument_get_enacted_on(lceiling) == sys::date{10});
	REQUIRE(loaded->world.legal_instrument_get_effective_from(lceiling) == sys::date{20});
	REQUIRE(loaded->world.legal_instrument_get_repealed_on(lprohibition) == sys::date{30});
	REQUIRE(loaded->world.legal_action_get_legal_instrument_from_legal_action_instrument(lenactment) == lceiling);
	REQUIRE(loaded->world.legal_action_get_legal_instrument_from_legal_action_instrument(lrepeal) == lprohibition);
	bool found_ceiling = false;
	for(auto rule : loaded->world.in_policy_rule)
		if(loaded->world.policy_rule_get_settlement(rule) == lsettlement && loaded->world.policy_rule_get_amount(rule) == Approx(100.0f)) found_ceiling = true;
	REQUIRE(found_ceiling);
	REQUIRE(::governance::law::instrument_is_effective(*loaded, lceiling, sys::date{25}));
	REQUIRE(::governance::law::instrument_is_effective(*loaded, lprohibition, sys::date{25}));
	REQUIRE_FALSE(::governance::law::instrument_is_effective(*loaded, lprohibition, sys::date{30}));
	REQUIRE(::governance::law::public_debt_policy_for(*loaded, lnation, lsettlement, sys::date{25}).ceiling == Approx(100.0f));
	REQUIRE(::governance::law::public_debt_policy_for(*loaded, lnation, lsettlement, sys::date{25}).issuance_allowed == false);
	REQUIRE(::governance::law::public_debt_policy_for(*loaded, lnation, lsettlement, sys::date{31}).issuance_allowed);
}

TEST_CASE("state_finance_relations_survive_save_load", "[governance][finance][serialization]") {
	auto state = std::make_unique<sys::state>();
	auto nation = state->world.create_nation();
	auto institution = ::governance::create_institution(*state, nation, ::governance::institution_kind::central_government);
	auto office = ::governance::create_office(*state, institution, ::governance::office_kind::finance_minister);
	auto person = ::persons::create_person(*state, sys::date{1});
	REQUIRE(::persons::appoint_person(*state, person, office, sys::date{10}));
	for(auto kind : {::governance::authority_kind::levy_tax, ::governance::authority_kind::issue_public_debt})
		REQUIRE(::governance::grant_authority_to_office(*state, office, kind, nation));
	auto settlement = state->world.create_commodity();
	auto treasury = ::governance::finance::open_treasury_account(*state, institution, settlement);
	auto taxpayer = state->world.create_economic_actor();
	auto taxpayer_account = ::economy::accounts::open_account(*state, taxpayer, settlement);
	REQUIRE(::economy::accounts::bootstrap_set_balance(*state, taxpayer_account, 20.0f));
	auto tax_action = ::governance::finance::authorized_assess_tax(*state, person, taxpayer, treasury, 20.0f, sys::date{30}, sys::date{20});
	auto tax = state->world.fiscal_action_get_obligation_from_fiscal_action_resulting_obligation(tax_action);
	auto tax_payment = ::governance::finance::pay_tax(*state, tax, taxpayer_account, treasury, 20.0f, sys::date{21});
	auto bank = ::economy::banking::create_bank(*state);
	auto reserve = ::economy::banking::open_reserve_account(*state, bank, settlement);
	REQUIRE(::economy::banking::bootstrap_set_reserve_balance(*state, reserve, 100.0f));
	auto bank_representative = ::persons::create_person(*state, sys::date{1});
	auto debt_action = test_issue_public_debt(*state, person, treasury, reserve, 40.0f, sys::date{100}, 0.0f, sys::date{22}, bank_representative);
	auto debt = state->world.fiscal_action_get_obligation_from_fiscal_action_resulting_obligation(debt_action);
	REQUIRE(tax_action); REQUIRE(tax_payment); REQUIRE(debt_action); REQUIRE(debt);

	std::vector<uint8_t> bytes(sys::sizeof_save_section(*state));
	auto const* end = sys::write_save_section(bytes.data(), *state);
	auto loaded = std::make_unique<sys::state>();
	auto lnation = loaded->world.create_nation();
	auto linstitution = ::governance::create_institution(*loaded, lnation, ::governance::institution_kind::central_government);
	auto loffice = ::governance::create_office(*loaded, linstitution, ::governance::office_kind::finance_minister);
	auto lperson = ::persons::create_person(*loaded, sys::date{1});
	REQUIRE(::persons::appoint_person(*loaded, lperson, loffice, sys::date{10}));
	for(auto kind : {::governance::authority_kind::levy_tax, ::governance::authority_kind::issue_public_debt})
		REQUIRE(::governance::grant_authority_to_office(*loaded, loffice, kind, lnation));
	auto lsettlement = loaded->world.create_commodity();
	auto ltreasury = ::governance::finance::open_treasury_account(*loaded, linstitution, lsettlement);
	auto ltaxpayer = loaded->world.create_economic_actor();
	auto ltaxpayer_account = ::economy::accounts::open_account(*loaded, ltaxpayer, lsettlement);
	REQUIRE(::economy::accounts::bootstrap_set_balance(*loaded, ltaxpayer_account, 20.0f));
	auto ltax_action = ::governance::finance::authorized_assess_tax(*loaded, lperson, ltaxpayer, ltreasury, 20.0f, sys::date{30}, sys::date{20});
	auto ltax = loaded->world.fiscal_action_get_obligation_from_fiscal_action_resulting_obligation(ltax_action);
	auto ltax_payment = ::governance::finance::pay_tax(*loaded, ltax, ltaxpayer_account, ltreasury, 20.0f, sys::date{21});
	auto lbank = ::economy::banking::create_bank(*loaded);
	auto lreserve = ::economy::banking::open_reserve_account(*loaded, lbank, lsettlement);
	REQUIRE(::economy::banking::bootstrap_set_reserve_balance(*loaded, lreserve, 100.0f));
	auto lbank_representative = ::persons::create_person(*loaded, sys::date{1});
	auto ldebt_action = test_issue_public_debt(*loaded, lperson, ltreasury, lreserve, 40.0f, sys::date{100}, 0.0f, sys::date{22}, lbank_representative);
	auto ldebt = loaded->world.fiscal_action_get_obligation_from_fiscal_action_resulting_obligation(ldebt_action);
	sys::read_save_section(bytes.data(), end, *loaded);
	REQUIRE(::governance::finance::treasury_institution_for(*loaded, ltreasury) == linstitution);
	REQUIRE(::economy::relations::total_due(*loaded, ltax) == Approx(0.0f));
	REQUIRE(::economy::relations::total_due(*loaded, ldebt) == Approx(40.0f));
	REQUIRE(loaded->world.fiscal_action_get_person_from_fiscal_action_initiator(ltax_action) == lperson);
	REQUIRE(loaded->world.fiscal_action_get_office_from_fiscal_action_authorizing_office(ldebt_action) == loffice);
	REQUIRE(loaded->world.fiscal_action_get_institution_from_fiscal_action_treasury_institution(ldebt_action) == linstitution);
	REQUIRE(loaded->world.fiscal_action_get_monetary_account_from_fiscal_action_treasury_account(ldebt_action) == ltreasury);
	REQUIRE(loaded->world.fiscal_action_get_obligation_from_fiscal_action_resulting_obligation(ldebt_action) == ldebt);
	REQUIRE(loaded->world.fiscal_action_get_transaction_from_fiscal_action_resulting_transaction(ldebt_action));
	REQUIRE(::governance::finance::fiscal_position_for(*loaded, linstitution, lsettlement).treasury_cash == Approx(60.0f));
	REQUIRE(::economy::banking::bank_balance_sheet(*loaded, lbank, lsettlement).public_debt_assets == Approx(40.0f));
}

TEST_CASE("actor_consent_gates_loans_and_public_debt", "[economy][consent][finance]") {
	auto state = std::make_unique<sys::state>();
	auto settlement = state->world.create_commodity();
	auto nation = state->world.create_nation();
	auto institution = ::governance::create_institution(*state, nation, ::governance::institution_kind::central_government);
	auto office = ::governance::create_office(*state, institution, ::governance::office_kind::finance_minister);
	auto issuer = ::persons::create_person(*state, sys::date{1});
	REQUIRE(::persons::appoint_person(*state, issuer, office, sys::date{1}));
	REQUIRE(::governance::grant_authority_to_office(*state, office, ::governance::authority_kind::issue_public_debt, nation));
	auto bank = ::economy::banking::create_bank(*state);
	auto bank_reserve = ::economy::banking::open_reserve_account(*state, bank, settlement);
	REQUIRE(::economy::banking::bootstrap_set_reserve_balance(*state, bank_reserve, 1000.0f));
	auto bank_actor = ::actors::organizations::actor_for_organization(*state, bank);
	auto borrower = ::persons::create_person(*state, sys::date{1});
	auto borrower_actor = ::persons::actor_for_person(*state, borrower);
	auto invalid_terms = ::economy::consent::create_proposal(*state, ::economy::consent::proposal_kind::loan,
		bank_actor, borrower_actor, settlement, 10.0f, sys::date{9}, 0.0f, sys::date{10});
	REQUIRE_FALSE(invalid_terms);
	REQUIRE_FALSE(::economy::consent::create_proposal(*state, ::economy::consent::proposal_kind::loan,
		bank_actor, borrower_actor, settlement, 10.0f, sys::date{20},
		std::numeric_limits<float>::quiet_NaN(), sys::date{10}));
	auto borrower_account = ::economy::banking::open_deposit_account(*state, bank, borrower_actor, settlement);
	REQUIRE(borrower_account);
	auto bank_representative = ::persons::create_person(*state, sys::date{1});
	REQUIRE(::economy::consent::create_mandate(*state, bank, bank_representative,
		::economy::consent::decision_kind::lend, sys::date{1}));
	auto loan_proposal = ::economy::consent::create_proposal(*state, ::economy::consent::proposal_kind::loan,
		bank_actor, borrower_actor, settlement, 100.0f, sys::date{100}, 0.05f, sys::date{1});
	REQUIRE(loan_proposal);
	auto decisions_before_backdate = state->world.economic_decision_size();
	REQUIRE_FALSE(::economy::consent::accept_proposal(*state, loan_proposal, bank_actor, bank_representative, sys::date{0}));
	REQUIRE_FALSE(::economy::consent::reject_proposal(*state, loan_proposal, bank_actor, bank_representative, sys::date{0}));
	REQUIRE(state->world.economic_decision_size() == decisions_before_backdate);
	REQUIRE_FALSE(::economy::banking::originate_loan_with_consent(*state, bank, borrower_account, 100.0f,
		sys::date{1}, sys::date{100}, 0.0f, loan_proposal));
	REQUIRE(state->world.obligation_size() == 0);
	REQUIRE(::economy::consent::accept_proposal(*state, loan_proposal, bank_actor, bank_representative, sys::date{1}));
	REQUIRE(::economy::consent::accept_proposal(*state, loan_proposal, borrower_actor, borrower, sys::date{1}));
	REQUIRE_FALSE(::economy::banking::originate_loan_with_consent(*state, bank, borrower_account, 100.0f,
		sys::date{1}, sys::date{1000}, 0.50f, loan_proposal));
	REQUIRE(state->world.obligation_size() == 0);
	auto loan = ::economy::banking::originate_loan_with_consent(*state, bank, borrower_account, 100.0f,
		sys::date{1}, sys::date{100}, 0.05f, loan_proposal);
	REQUIRE(loan);
	REQUIRE(::economy::consent::proposal_fully_accepted(*state, loan_proposal, sys::date{1}) == false);
	REQUIRE(state->world.economic_proposal_get_status(loan_proposal) == uint8_t(::economy::consent::proposal_status::executed));
	REQUIRE(::economy::banking::deposit_balance(*state, borrower_account) == Approx(100.0f));
	REQUIRE_FALSE(::economy::banking::originate_loan_with_consent(*state, bank, borrower_account, 100.0f,
		sys::date{1}, sys::date{100}, 0.0f, loan_proposal));
	REQUIRE(state->world.obligation_size() == 1);

	auto treasury = ::governance::finance::open_treasury_account(*state, institution, settlement);
	auto investor = ::persons::create_person(*state, sys::date{1});
	auto investor_actor = ::persons::actor_for_person(*state, investor);
	auto investor_account = ::economy::accounts::open_account(*state, investor_actor, settlement);
	REQUIRE(::economy::accounts::bootstrap_set_balance(*state, investor_account, 80.0f));
	auto issuer_actor = ::governance::actor_for_institution(*state, institution);
	auto debt_proposal = ::economy::consent::create_proposal(*state, ::economy::consent::proposal_kind::investment,
		issuer_actor, investor_actor, settlement, 80.0f, sys::date{100}, 0.05f, sys::date{2});
	REQUIRE(debt_proposal);
	auto obligations_before = state->world.obligation_size();
	auto actions_before = state->world.fiscal_action_size();
	REQUIRE_FALSE(::governance::finance::authorized_issue_public_debt_with_consent(*state, issuer, treasury,
		investor_account, 80.0f, sys::date{100}, 0.0f, sys::date{2}, debt_proposal));
	REQUIRE(state->world.obligation_size() == obligations_before);
	REQUIRE(state->world.fiscal_action_size() == actions_before);
	REQUIRE(::economy::consent::accept_proposal(*state, debt_proposal, investor_actor, investor, sys::date{2}));
	REQUIRE_FALSE(::governance::finance::authorized_issue_public_debt_with_consent(*state, issuer, treasury,
		investor_account, 80.0f, sys::date{1000}, 0.50f, sys::date{2}, debt_proposal));
	REQUIRE(state->world.obligation_size() == obligations_before);
	REQUIRE(state->world.fiscal_action_size() == actions_before);
	auto debt_action = ::governance::finance::authorized_issue_public_debt_with_consent(*state, issuer, treasury,
		investor_account, 80.0f, sys::date{100}, 0.05f, sys::date{2}, debt_proposal);
	REQUIRE(debt_action);
	REQUIRE(state->world.economic_proposal_get_status(debt_proposal) == uint8_t(::economy::consent::proposal_status::executed));
	REQUIRE(::economy::accounts::balance(*state, investor_account) == Approx(0.0f));

	// Organization investors require their own representative and a matching,
	// active mandate; personal authority does not leak across organizations.
	auto company = ::actors::organizations::create_company(*state);
	auto company_actor = ::actors::organizations::actor_for_organization(*state, company);
	auto company_representative = ::persons::create_person(*state, sys::date{1});
	auto unrelated = ::persons::create_person(*state, sys::date{1});
	REQUIRE(::economy::consent::create_mandate(*state, company, company_representative,
		::economy::consent::decision_kind::invest, sys::date{5}, sys::date{10}));
	REQUIRE_FALSE(::economy::consent::can_decide_for_actor(*state, unrelated, company_actor,
		::economy::consent::decision_kind::invest, sys::date{6}));
	REQUIRE_FALSE(::economy::consent::can_decide_for_actor(*state, company_representative, company_actor,
		::economy::consent::decision_kind::borrow, sys::date{6}));
	REQUIRE_FALSE(::economy::consent::can_decide_for_actor(*state, company_representative, company_actor,
		::economy::consent::decision_kind::invest, sys::date{4}));
	REQUIRE(::economy::consent::can_decide_for_actor(*state, company_representative, company_actor,
		::economy::consent::decision_kind::invest, sys::date{6}));
	REQUIRE_FALSE(::economy::consent::can_decide_for_actor(*state, company_representative, company_actor,
		::economy::consent::decision_kind::invest, sys::date{10}));
	auto company_account = ::economy::accounts::open_account(*state, company_actor, settlement);
	REQUIRE(::economy::accounts::bootstrap_set_balance(*state, company_account, 10.0f));
	auto company_proposal = ::economy::consent::create_proposal(*state,
		::economy::consent::proposal_kind::investment, issuer_actor, company_actor,
		settlement, 10.0f, sys::date{100}, 0.0f, sys::date{5});
	REQUIRE_FALSE(::economy::consent::accept_proposal(*state, company_proposal, company_actor, unrelated, sys::date{6}));
	REQUIRE(::economy::consent::accept_proposal(*state, company_proposal, company_actor, company_representative, sys::date{6}));
	REQUIRE(::governance::finance::authorized_issue_public_debt_with_consent(*state, issuer, treasury,
		company_account, 10.0f, sys::date{100}, 0.0f, sys::date{6}, company_proposal));
}

TEST_CASE("information_beliefs_are_stale_private_and_provenance_bound", "[economy][information][consent]") {
	auto state = std::make_unique<sys::state>();
	auto nation = state->world.create_nation();
	auto settlement = state->world.create_commodity();
	auto source = state->world.create_economic_actor();
	auto recipient = ::persons::create_person(*state, sys::date{1});
	auto recipient_actor = ::persons::actor_for_person(*state, recipient);
	auto report = ::economy::information::publish_report(*state,
		::economy::information::information_fact_kind::national_public_debt, source, recipient_actor,
		nation, settlement, 70.0f, sys::date{5}, sys::date{8}, sys::date{10}, 0.8f);
	REQUIRE(report);
	REQUIRE_FALSE(::economy::information::latest_belief(*state, recipient_actor,
		::economy::information::information_fact_kind::national_public_debt, nation, settlement, sys::date{9}));
	REQUIRE_FALSE(::economy::information::adopt_report_as_belief(*state, recipient_actor, report, sys::date{9}));
	auto first_belief = ::economy::information::adopt_report_as_belief(*state, recipient_actor, report, sys::date{10});
	REQUIRE(first_belief);
	auto first_view = ::economy::information::latest_belief(*state, recipient_actor,
		::economy::information::information_fact_kind::national_public_debt, nation, settlement, sys::date{15});
	REQUIRE(first_view);
	REQUIRE(*first_view == first_belief);
	REQUIRE(state->world.belief_get_estimated_value(first_belief) == Approx(70.0f));
	auto second_report = ::economy::information::publish_report(*state,
		::economy::information::information_fact_kind::national_public_debt, source, recipient_actor,
		nation, settlement, 140.0f, sys::date{20}, sys::date{20}, sys::date{20}, 1.0f);
	auto second_belief = ::economy::information::adopt_report_as_belief(*state, recipient_actor, second_report, sys::date{20});
	REQUIRE(second_belief);
	auto third_report = ::economy::information::publish_report(*state,
		::economy::information::information_fact_kind::national_public_debt, source, recipient_actor,
		nation, settlement, 130.0f, sys::date{20}, sys::date{20}, sys::date{20}, 1.0f);
	auto third_belief = ::economy::information::adopt_report_as_belief(*state, recipient_actor, third_report, sys::date{20});
	REQUIRE(third_belief);
	REQUIRE(::economy::information::latest_belief(*state, recipient_actor,
		::economy::information::information_fact_kind::national_public_debt, nation, settlement, sys::date{15}) == first_belief);
	REQUIRE(::economy::information::latest_belief(*state, recipient_actor,
		::economy::information::information_fact_kind::national_public_debt, nation, settlement, sys::date{20}) == third_belief);
	auto outsider = state->world.create_economic_actor();
	REQUIRE_FALSE(::economy::information::adopt_report_as_belief(*state, outsider, report, sys::date{10}));
	auto proposal = ::economy::consent::create_proposal(*state, ::economy::consent::proposal_kind::investment,
		source, recipient_actor, settlement, 10.0f, sys::date{30}, 0.0f, sys::date{10});
	REQUIRE(proposal);
	REQUIRE_FALSE(::economy::consent::accept_proposal_with_basis(*state, proposal, recipient_actor, recipient,
		sys::date{19}, {second_belief}));
	auto decision = ::economy::consent::accept_proposal_with_basis(*state, proposal, recipient_actor, recipient,
		sys::date{20}, {first_belief, second_belief, third_belief});
	REQUIRE(decision);
	REQUIRE_FALSE(::economy::consent::accept_proposal_with_basis(*state, proposal, recipient_actor, recipient,
		sys::date{20}, {second_belief, second_belief}));
	REQUIRE_FALSE(::economy::consent::accept_proposal_with_basis(*state, proposal, recipient_actor, recipient,
		sys::date{20}, {first_belief}));
	auto second_proposal = ::economy::consent::create_proposal(*state, ::economy::consent::proposal_kind::investment,
		source, recipient_actor, settlement, 11.0f, sys::date{30}, 0.0f, sys::date{20});
	REQUIRE(second_proposal);
	auto second_decision = ::economy::consent::accept_proposal_with_basis(*state, second_proposal, recipient_actor, recipient,
		sys::date{20}, {second_belief});
	REQUIRE(second_decision);
	uint32_t decisions_supported_by_second_belief = 0;
	state->world.belief_for_each_economic_decision_belief_basis_belief_as_belief(second_belief,
		[&](dcon::economic_decision_belief_basis_belief_id relation) {
			auto supported = state->world.economic_decision_belief_basis_decision_get_economic_decision(
			dcon::economic_decision_belief_basis_decision_id(relation.index()));
			if(supported) ++decisions_supported_by_second_belief;
		});
	REQUIRE(decisions_supported_by_second_belief == 2);
	std::vector<uint8_t> bytes(sys::sizeof_save_section(*state));
	auto const* end = sys::write_save_section(bytes.data(), *state);
	auto loaded = std::make_unique<sys::state>();
	auto lnation = loaded->world.create_nation();
	auto lsettlement = loaded->world.create_commodity();
	auto lsource = loaded->world.create_economic_actor();
	auto lrecipient = ::persons::create_person(*loaded, sys::date{1});
	auto lrecipient_actor = ::persons::actor_for_person(*loaded, lrecipient);
	auto lreport = ::economy::information::publish_report(*loaded,
		::economy::information::information_fact_kind::national_public_debt, lsource, lrecipient_actor,
		lnation, lsettlement, 70.0f, sys::date{5}, sys::date{8}, sys::date{10}, 0.8f);
	auto lfirst_belief = ::economy::information::adopt_report_as_belief(*loaded, lrecipient_actor, lreport, sys::date{10});
	auto lsecond_report = ::economy::information::publish_report(*loaded,
		::economy::information::information_fact_kind::national_public_debt, lsource, lrecipient_actor,
		lnation, lsettlement, 140.0f, sys::date{20}, sys::date{20}, sys::date{20}, 1.0f);
	auto lsecond_belief = ::economy::information::adopt_report_as_belief(*loaded, lrecipient_actor, lsecond_report, sys::date{20});
	auto lthird_report = ::economy::information::publish_report(*loaded,
		::economy::information::information_fact_kind::national_public_debt, lsource, lrecipient_actor,
		lnation, lsettlement, 130.0f, sys::date{20}, sys::date{20}, sys::date{20}, 1.0f);
	auto lthird_belief = ::economy::information::adopt_report_as_belief(*loaded, lrecipient_actor, lthird_report, sys::date{20});
	auto lproposal = ::economy::consent::create_proposal(*loaded, ::economy::consent::proposal_kind::investment,
		lsource, lrecipient_actor, lsettlement, 10.0f, sys::date{30}, 0.0f, sys::date{10});
	auto ldecision = ::economy::consent::accept_proposal_with_basis(*loaded, lproposal, lrecipient_actor, lrecipient,
		sys::date{20}, {lfirst_belief, lsecond_belief, lthird_belief});
	sys::read_save_section(bytes.data(), end, *loaded);
	REQUIRE(loaded->world.information_report_get_economic_actor_from_information_report_source(lreport) == lsource);
	REQUIRE(loaded->world.information_report_get_economic_actor_from_information_report_recipient(lreport) == lrecipient_actor);
	REQUIRE(loaded->world.information_report_get_nation_from_information_report_subject(lreport) == lnation);
	REQUIRE(loaded->world.information_report_get_commodity_from_information_report_settlement(lreport) == lsettlement);
	REQUIRE(loaded->world.information_report_get_fact_kind(lreport) == uint8_t(::economy::information::information_fact_kind::national_public_debt));
	REQUIRE(loaded->world.information_report_get_reported_value(lreport) == Approx(70.0f));
	REQUIRE(loaded->world.information_report_get_fact_date(lreport) == sys::date{5});
	REQUIRE(loaded->world.information_report_get_published_on(lreport) == sys::date{8});
	REQUIRE(loaded->world.information_report_get_received_on(lreport) == sys::date{10});
	REQUIRE(loaded->world.information_report_get_confidence(lreport) == Approx(0.8f));
	REQUIRE(loaded->world.belief_get_estimated_value(lfirst_belief) == Approx(70.0f));
	REQUIRE(loaded->world.belief_get_estimated_value(lsecond_belief) == Approx(140.0f));
	REQUIRE(loaded->world.belief_get_estimated_value(lthird_belief) == Approx(130.0f));
	REQUIRE(loaded->world.belief_get_economic_actor_from_belief_holder(lfirst_belief) == lrecipient_actor);
	REQUIRE(loaded->world.belief_get_information_report_from_belief_source(lfirst_belief) == lreport);
	REQUIRE(loaded->world.belief_get_formed_on(lsecond_belief) == sys::date{20});
	REQUIRE(::economy::information::latest_belief(*loaded, lrecipient_actor,
		::economy::information::information_fact_kind::national_public_debt, lnation, lsettlement, sys::date{15}) == lfirst_belief);
	REQUIRE(::economy::information::latest_belief(*loaded, lrecipient_actor,
		::economy::information::information_fact_kind::national_public_debt, lnation, lsettlement, sys::date{20}) == lthird_belief);
	uint32_t loaded_basis_count = 0;
	bool loaded_second_belief_found = false;
	loaded->world.economic_decision_for_each_economic_decision_belief_basis_decision_as_economic_decision(ldecision,
		[&](dcon::economic_decision_belief_basis_decision_id relation) {
			auto basis = loaded->world.economic_decision_belief_basis_decision_get_economic_decision_belief_basis(relation);
			auto belief = loaded->world.economic_decision_belief_basis_get_belief_from_economic_decision_belief_basis_belief(basis);
			++loaded_basis_count;
			loaded_second_belief_found = loaded_second_belief_found || belief == lsecond_belief;
		});
	REQUIRE(loaded_basis_count == 3);
	REQUIRE(loaded_second_belief_found);
}

TEST_CASE("information_provenance_reaches_public_debt_execution", "[economy][information][consent][finance]") {
	auto state = std::make_unique<sys::state>();
	auto nation = state->world.create_nation();
	auto settlement = state->world.create_commodity();
	auto institution = ::governance::create_institution(*state, nation, ::governance::institution_kind::central_government);
	auto office = ::governance::create_office(*state, institution, ::governance::office_kind::finance_minister);
	auto issuer = ::persons::create_person(*state, sys::date{1});
	REQUIRE(::persons::appoint_person(*state, issuer, office, sys::date{1}));
	REQUIRE(::governance::grant_authority_to_office(*state, office, ::governance::authority_kind::issue_public_debt, nation));
	auto treasury = ::governance::finance::open_treasury_account(*state, institution, settlement);
	auto issuer_actor = ::governance::actor_for_institution(*state, institution);

	// Objective debt is established independently of the investor's information state.
	auto objective_holder_person = ::persons::create_person(*state, sys::date{1});
	auto objective_holder = ::persons::actor_for_person(*state, objective_holder_person);
	auto objective_account = ::economy::accounts::open_account(*state, objective_holder, settlement);
	REQUIRE(::economy::accounts::bootstrap_set_balance(*state, objective_account, 100.0f));
	REQUIRE(test_issue_public_debt(*state, issuer, treasury, objective_account,
		100.0f, sys::date{200}, 0.0f, sys::date{1}, objective_holder_person));
	REQUIRE(::governance::finance::national_public_debt(*state, nation, settlement) == Approx(100.0f));

	auto investor = ::persons::create_person(*state, sys::date{1});
	auto investor_actor = ::persons::actor_for_person(*state, investor);
	auto investor_account = ::economy::accounts::open_account(*state, investor_actor, settlement);
	REQUIRE(::economy::accounts::bootstrap_set_balance(*state, investor_account, 20.0f));
	REQUIRE_FALSE(::economy::information::latest_belief(*state, investor_actor,
		::economy::information::information_fact_kind::national_public_debt, nation, settlement, sys::date{10}));

	auto report = ::economy::information::publish_report(*state,
		::economy::information::information_fact_kind::national_public_debt, issuer_actor, investor_actor,
		nation, settlement, 60.0f, sys::date{5}, sys::date{6}, sys::date{10}, 1.0f);
	REQUIRE(report);
	auto belief = ::economy::information::adopt_report_as_belief(*state, investor_actor, report, sys::date{10});
	REQUIRE(belief);
	REQUIRE(state->world.belief_get_estimated_value(belief) == Approx(60.0f));
	REQUIRE(::economy::information::latest_belief(*state, investor_actor,
		::economy::information::information_fact_kind::national_public_debt, nation, settlement, sys::date{10}) == belief);

	auto proposal = ::economy::consent::create_proposal(*state, ::economy::consent::proposal_kind::investment,
		issuer_actor, investor_actor, settlement, 20.0f, sys::date{100}, 0.0f, sys::date{10});
	REQUIRE(proposal);
	auto decision = ::economy::consent::accept_proposal_with_basis(*state, proposal, investor_actor, investor,
		sys::date{10}, {belief});
	REQUIRE(decision);
	auto action = ::governance::finance::authorized_issue_public_debt_with_consent(*state, issuer, treasury,
		investor_account, 20.0f, sys::date{100}, 0.0f, sys::date{10}, proposal);
	REQUIRE(action);
	REQUIRE(::governance::finance::national_public_debt(*state, nation, settlement) == Approx(120.0f));
	REQUIRE(state->world.economic_proposal_get_status(proposal) == uint8_t(::economy::consent::proposal_status::executed));

	uint32_t basis_count = 0;
	bool basis_found = false;
	state->world.economic_decision_for_each_economic_decision_belief_basis_decision_as_economic_decision(decision,
		[&](dcon::economic_decision_belief_basis_decision_id relation) {
			++basis_count;
			auto basis = state->world.economic_decision_belief_basis_decision_get_economic_decision_belief_basis(relation);
			basis_found = basis_found
				|| state->world.economic_decision_belief_basis_get_belief_from_economic_decision_belief_basis_belief(basis) == belief;
		});
	REQUIRE(basis_count == 1);
	REQUIRE(basis_found);
}

TEST_CASE("actor_consent_relations_survive_save_load", "[economy][consent][serialization]") {
	auto state = std::make_unique<sys::state>();
	auto settlement = state->world.create_commodity();
	auto organization = ::actors::organizations::create_company(*state);
	auto representative = ::persons::create_person(*state, sys::date{1});
	auto representative_actor = ::persons::actor_for_person(*state, representative);
	auto mandate = ::economy::consent::create_mandate(*state, organization, representative,
		::economy::consent::decision_kind::lend, sys::date{1}, sys::date{20});
	auto proposal = ::economy::consent::create_proposal(*state, ::economy::consent::proposal_kind::loan,
		::actors::organizations::actor_for_organization(*state, organization), representative_actor,
		settlement, 25.0f, sys::date{50}, 0.02f, sys::date{2});
	REQUIRE(proposal);
	REQUIRE(::economy::consent::accept_proposal(*state, proposal,
		::actors::organizations::actor_for_organization(*state, organization), representative, sys::date{2}));
	REQUIRE(::economy::consent::accept_proposal(*state, proposal, representative_actor, representative, sys::date{2}));
	REQUIRE(::economy::consent::mark_executed(*state, proposal));
	std::vector<uint8_t> bytes(sys::sizeof_save_section(*state));
	auto const* end = sys::write_save_section(bytes.data(), *state);

	auto loaded = std::make_unique<sys::state>();
	auto lsettlement = loaded->world.create_commodity();
	auto lorganization = ::actors::organizations::create_company(*loaded);
	auto lrepresentative = ::persons::create_person(*loaded, sys::date{1});
	auto lmandate = ::economy::consent::create_mandate(*loaded, lorganization, lrepresentative,
		::economy::consent::decision_kind::lend, sys::date{1}, sys::date{20});
	auto lproposal = ::economy::consent::create_proposal(*loaded, ::economy::consent::proposal_kind::loan,
		::actors::organizations::actor_for_organization(*loaded, lorganization),
		::persons::actor_for_person(*loaded, lrepresentative), lsettlement, 25.0f, sys::date{50}, 0.02f, sys::date{2});
	REQUIRE(::economy::consent::accept_proposal(*loaded, lproposal,
		::actors::organizations::actor_for_organization(*loaded, lorganization), lrepresentative, sys::date{2}));
	REQUIRE(::economy::consent::accept_proposal(*loaded, lproposal,
		::persons::actor_for_person(*loaded, lrepresentative), lrepresentative, sys::date{2}));
	REQUIRE(::economy::consent::mark_executed(*loaded, lproposal));
	sys::read_save_section(bytes.data(), end, *loaded);
	REQUIRE(loaded->world.organization_decision_mandate_get_decision_kind(lmandate) == uint8_t(::economy::consent::decision_kind::lend));
	REQUIRE(loaded->world.organization_decision_mandate_get_started_on(lmandate) == sys::date{1});
	REQUIRE(loaded->world.organization_decision_mandate_get_ended_on(lmandate) == sys::date{20});
	REQUIRE(loaded->world.economic_proposal_get_status(lproposal) == uint8_t(::economy::consent::proposal_status::executed));
	REQUIRE(loaded->world.economic_proposal_get_amount(lproposal) == Approx(25.0f));
	REQUIRE(loaded->world.economic_proposal_get_due_date(lproposal) == sys::date{50});
	REQUIRE(loaded->world.economic_proposal_get_annual_interest_rate(lproposal) == Approx(0.02f));
	uint32_t decisions = 0;
	for(auto decision : loaded->world.in_economic_decision)
		if(loaded->world.economic_decision_get_economic_proposal_from_economic_decision_proposal(decision) == lproposal) ++decisions;
	REQUIRE(decisions == 2);
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
