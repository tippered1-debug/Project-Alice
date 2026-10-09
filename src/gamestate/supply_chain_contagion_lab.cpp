#include "supply_chain_contagion_lab.hpp"

#include "actors/organizations/organizations.hpp"
#include "actors/ownership.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/causal_order.hpp"
#include "economy/exact_person_economy.hpp"
#include "economy/firm_agency.hpp"
#include "economy/industrial_production.hpp"
#include "economy/physical/concrete_market.hpp"
#include "economy/physical/deposits.hpp"
#include "economy/physical/exact_person_freight.hpp"
#include "economy/physical/exact_person_goods.hpp"
#include "economy/physical/extraction.hpp"
#include "economy/physical/factory_inputs.hpp"
#include "economy/physical/freight_market.hpp"
#include "economy/physical/inventory.hpp"
#include "economy/physical/job_market.hpp"
#include "economy/physical/labor_dynamics.hpp"
#include "economy/physical/shipments.hpp"
#include "economy/payroll.hpp"
#include "economy/world_trade_capacity.hpp"
#include "gamestate/serialization.hpp"
#include "gamestate/system_state.hpp"
#include "military/land_forces.hpp"
#include "nations/nations.hpp"
#include "nations/strategic_statecraft.hpp"
#include "persons/exact_population.hpp"
#include "technology/technology_kernel.hpp"
#include "world/spatial_runtime.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace sys::simulation::supply_chain_lab {
namespace {

constexpr float epsilon = 1.0e-5f;
constexpr uint8_t land_mode = 1u << uint8_t(economy::world_trade::transport_mode::land);

struct lab_handles {
	dcon::commodity_id settlement{};
	dcon::commodity_id ore{};
	dcon::commodity_id product{};
	dcon::nation_id nations[3]{};
	dcon::province_id provinces[3]{};
	dcon::market_id markets[3]{};
	dcon::site_id sites[3]{};
	dcon::infrastructure_node_id nodes[3]{};
	dcon::organization_id organizations[3]{};
	dcon::economic_actor_id actors[3]{};
	dcon::monetary_account_id accounts[3]{};
	dcon::factory_id factories[3]{};
	dcon::resource_deposit_id deposits[3]{};
	dcon::freight_offer_id offers[2]{};
	dcon::unilateral_relationship_id embargo{};
	dcon::infrastructure_edge_id ab_edge{};
	dcon::infrastructure_edge_id cb_edge{};
	persons::exact_population::person_key workers[3]{};
};

struct lab_world {
	std::unique_ptr<sys::state> state = std::make_unique<sys::state>();
	lab_handles h{};
	float seeded_money = 0.0f;
};

float total_money(sys::state const& state);

void initialize_stores(sys::state& state) {
	persons::exact_population::initialize_empty_store(state);
	economy::causal_order::initialize_empty_store(state);
	economy::exact_person_economy::initialize_empty_store(state);
	economy::physical::exact_person_goods::initialize_empty_store(state);
	economy::physical::exact_person_freight::initialize_empty_store(state);
	economy::physical::labor_dynamics::initialize_empty_store(state);
	military::land_forces::initialize_empty_store(state);
	technology::kernel::initialize_empty_store(state);
	nations::strategic_statecraft::initialize(state);
}

dcon::infrastructure_edge_id connect(sys::state& state,
	dcon::infrastructure_node_id left, dcon::infrastructure_node_id right, float distance) {
	auto edge = state.world.create_infrastructure_edge();
	state.world.infrastructure_edge_set_type(edge, 2);
	state.world.infrastructure_edge_set_distance(edge, distance);
	state.world.force_create_infrastructure_edge_from(edge, left);
	state.world.force_create_infrastructure_edge_to(edge, right);
	return edge;
}

void add_worker(lab_world& world, uint32_t country, float wage) {
	auto& state = *world.state;
	auto& h = world.h;
	persons::exact_population::cell_descriptor cell;
	cell.source_population_cell = 7010u + country;
	cell.literal_count = 8;
	cell.bootstrap_base_day = state.current_date.to_raw_value() - 1;
	cell.demographic_seed = uint64_t(4242u + country);
	cell.home_site = h.sites[country];
	if(persons::exact_population::register_synthetic_population_cell(state, cell).result
		!= persons::exact_population::status::created)
		throw std::runtime_error("could not register lab worker cell");
	h.workers[country] = {cell.source_population_cell, 0};
	auto offer = economy::physical::job_market::post_job_offer(state, h.actors[country],
		h.factories[country], h.sites[country], 0, 1.0f, wage, 1,
		h.accounts[country], 1, state.current_date);
	if(!offer || !economy::exact_person_economy::submit_application(state,
		h.workers[country], offer, state.current_date))
		throw std::runtime_error("could not submit lab worker application");
	economy::exact_person_economy::process_pending_applications(state);
	if(economy::exact_person_economy::active_contracts_for_factory(state, h.factories[country]).empty())
		throw std::runtime_error("lab worker did not receive a canonical contract");
}

lab_world initialize(config const& cfg) {
	lab_world world;
	auto& state = *world.state;
	auto& h = world.h;
	initialize_stores(state);
	state.current_date = sys::date{30000};
	state.start_date = sys::absolute_time_point{sys::year_month_day{1836, 1, 1}};
	state.end_date = sys::absolute_time_point{sys::year_month_day{2036, 1, 1}};
	state.game_seed = cfg.seed;
	state.inflation = 1.0f;

	h.settlement = state.world.create_commodity();
	h.ore = state.world.create_commodity();
	h.product = state.world.create_commodity();
	state.world.commodity_set_cost(h.settlement, 1.0f);
	state.world.commodity_set_cost(h.ore, 20.0f);
	state.world.commodity_set_cost(h.product, 100.0f);
	state.world.commodity_set_is_mine(h.ore, true);

	for(uint32_t country = 0; country < 3; ++country) {
		h.nations[country] = state.world.create_nation();
		h.provinces[country] = state.world.create_province();
		auto zone = state.world.create_state_instance();
		h.markets[country] = state.world.create_market();
		state.world.state_instance_set_market_from_local_market(zone, h.markets[country]);
		state.world.market_set_zone_from_local_market(h.markets[country], zone);
		state.world.province_set_state_membership(h.provinces[country], zone);
		state.world.province_set_nation_from_province_ownership(h.provinces[country], h.nations[country]);
		state.world.province_set_control_ratio(h.provinces[country], 1.0f);
		state.world.province_set_mid_point_b(h.provinces[country],
			glm::vec3{float(country) * 0.01f, 0.0f, 0.0f});
		h.sites[country] = state.world.create_site();
		state.world.force_create_site_location(h.sites[country], h.provinces[country]);
		state.world.force_create_market_hub_site(h.markets[country], h.sites[country]);
		h.nodes[country] = state.world.create_infrastructure_node();
		state.world.force_create_infrastructure_node_location(h.nodes[country], h.provinces[country]);
		state.world.infrastructure_node_set_position(h.nodes[country],
			state.world.province_get_mid_point(h.provinces[country]));
	}
	state.world.market_resize_price(state.world.commodity_size());
	for(uint32_t country = 0; country < 3; ++country) {
		state.world.market_set_price(h.markets[country], h.ore, 20.0f);
		state.world.market_set_price(h.markets[country], h.product, 100.0f);
	}

	h.ab_edge = connect(state, h.nodes[0], h.nodes[1], 30.0f);
	h.cb_edge = connect(state, h.nodes[2], h.nodes[1], 40.0f);

	const float a_share = std::clamp(cfg.a_dependency_share, 0.02f, 0.98f);
	const float supply_capacities[3] = {a_share, 1.0f - a_share, 0.0f};
	for(uint32_t country = 0; country < 3; ++country) {
		h.organizations[country] = actors::organizations::create_company(state);
		h.actors[country] = actors::organizations::actor_for_organization(state, h.organizations[country]);
		h.accounts[country] = economy::accounts::open_account(state, h.actors[country], h.settlement);
		if(!h.accounts[country] || !economy::accounts::bootstrap_set_balance(state,
			h.accounts[country], country == 1 ? 100000.0f : 1000.0f))
			throw std::runtime_error("could not fund lab firm account");

		auto type = state.world.create_factory_type();
		state.world.factory_type_set_base_workforce(type, 1);
		if(country == 1) {
			state.world.factory_type_set_output(type, h.product);
			state.world.factory_type_set_output_amount(type, 1.0f);
			economy::commodity_set recipe{};
			recipe.commodity_type[0] = h.ore;
			recipe.commodity_amounts[0] = 1.0f;
			state.world.factory_type_set_inputs(type, recipe);
			auto factory = state.world.create_factory();
			state.world.factory_set_building_type(factory, type);
			state.world.factory_set_size(factory, 1.0f);
			state.world.factory_set_productive_capacity(factory, 1.0f);
			state.world.factory_set_productivity_factor(factory, 1.0f);
			state.world.force_create_factory_location(factory, h.provinces[country]);
			state.world.force_create_factory_site(factory, h.sites[country]);
			if(!actors::organizations::bind_factory_operator(state, h.organizations[country], factory))
				throw std::runtime_error("could not bind B factory");
			h.factories[country] = factory;
		} else {
			state.world.factory_type_set_output(type, h.ore);
			state.world.factory_type_set_output_amount(type, 1.0f);
			state.world.factory_type_set_extracts_deposit(type, true);
			h.deposits[country] = economy::physical::deposits::create_deposit(state,
				h.sites[country], h.ore, 5000.0f, 5000.0f, 1.0f,
				supply_capacities[country], supply_capacities[country], 0);
			auto asset = state.world.create_asset();
			state.world.force_create_resource_deposit_asset(h.deposits[country], asset);
			if(!actors::organizations::bind_deposit_operator(state, h.organizations[country], h.deposits[country])
				|| !actors::ownership::create_stake(state, h.actors[country], asset, 1.0f, 1.0f, 1.0f))
				throw std::runtime_error("could not bind canonical extraction rights");
			h.factories[country] = economy::physical::extraction::create_enterprise(
				state, h.deposits[country], type, h.organizations[country]);
			if(!h.factories[country]) throw std::runtime_error("could not create primary enterprise");
		}
		state.world.factory_set_payroll_settlement(h.factories[country], h.settlement);
	}

	add_worker(world, 0, 0.2f);
	add_worker(world, 1, 2.0f);
	add_worker(world, 2, 0.2f);
	if(!economy::physical::exact_person_goods::set_need(state, h.workers[1], h.product, 0.15f))
		throw std::runtime_error("could not set canonical household need");
	{
		auto consumer_money = economy::exact_person_economy::open_account(state, h.workers[1], h.settlement);
		if(!economy::exact_person_economy::set_balance(state, consumer_money, 100.0f))
			throw std::runtime_error("could not seed B worker's opening savings");
	}

	// Real, route-scoped land carriers connect each supplier to B. Charges are
	// settled separately from the commodity purchase by the freight subsystem.
	for(uint32_t supplier = 0; supplier < 2; ++supplier) {
		auto carrier_actor = state.world.create_economic_actor();
		auto carrier_account = economy::accounts::open_account(state, carrier_actor, h.settlement);
		economy::accounts::bootstrap_set_balance(state, carrier_account, 0.0f);
		auto carrier = economy::physical::freight_market::create_carrier(state, carrier_actor,
			carrier_account, 1000.0f, land_mode, h.markets[supplier]);
		h.offers[supplier] = economy::physical::freight_market::create_offer(state, carrier,
			h.markets[supplier], h.markets[1], land_mode, 1000.0f, 0.1f, 0.001f);
		if(!h.offers[supplier]) throw std::runtime_error("could not create supplier freight offer");
	}
	world.seeded_money = total_money(state);
	return world;
}

float total_money(sys::state const& state) {
	float total = 0.0f;
	state.world.for_each_monetary_account([&](dcon::monetary_account_id account) {
		total += state.world.monetary_account_get_balance(account);
	});
	for(auto const& account : economy::exact_person_economy::export_snapshot(state).accounts)
		total += account.balance;
	return total;
}

uint64_t checksum(std::vector<std::string> const& rows, std::vector<std::string> const& events) {
	uint64_t value = 1469598103934665603ull;
	auto fold = [&](std::string const& row) {
		for(unsigned char byte : row) {
			value ^= byte;
			value *= 1099511628211ull;
		}
	};
	for(auto const& row : rows) fold(row);
	for(auto const& event : events) fold(event);
	return value;
}

std::string number(float value) {
	if(!std::isfinite(value)) return {};
	std::ostringstream out;
	out.imbue(std::locale::classic());
	out << std::fixed << std::setprecision(6) << value;
	return out.str();
}

uint32_t country_for_site(sys::state const& state, dcon::site_id site) {
	auto province = site ? state.world.site_get_province_from_site_location(site) : dcon::province_id{};
	auto nation = province ? state.world.province_get_nation_from_province_ownership(province) : dcon::nation_id{};
	for(uint32_t i = 0; i < 3; ++i) if(nation == dcon::nation_id{dcon::nation_id::value_base_t(i)}) return i;
	return 3;
}

float active_contract_count(sys::state const& state, dcon::factory_id factory) {
	return float(economy::exact_person_economy::active_contracts_for_factory(state, factory).size());
}

float payroll_value(sys::state const& state, dcon::factory_id factory, bool unpaid) {
	float value = 0.0f;
	state.world.for_each_payroll_event([&](dcon::payroll_event_id event) {
		if(state.world.payroll_event_get_factory_from_payroll_event_factory(event) != factory
			|| state.world.payroll_event_get_occurred_on(event) != state.current_date) return;
		value += unpaid ? state.world.payroll_event_get_unpaid(event)
			: state.world.payroll_event_get_paid(event);
	});
	return value;
}

void event(run_output& out, uint32_t day, std::string const& id, std::string const& parent,
	std::string const& kind, std::string const& from, std::string const& to,
	std::string const& commodity, float quantity, float cash, std::string const& trace) {
	std::ostringstream row;
	row << day << ',' << id << ',' << parent << ',' << kind << ',' << from << ',' << to
		<< ',' << commodity << ',' << number(quantity) << ',' << number(cash) << ',' << trace;
	out.events_csv.push_back(row.str());
}

void write_day(lab_world& world, config const& cfg, uint32_t day, run_output& out,
	std::unordered_map<uint32_t, uint32_t>& shipment_birth_day,
	float day_input_consumption) {
	(void)cfg;
	auto& state = *world.state;
	auto const& h = world.h;
	float imports[3]{};
	float exports[3]{};
	float trade_price_quantity = 0.0f;
	float trade_value = 0.0f;
	float freight_cost[3]{};
	float filled_quantity = 0.0f;
	float unmet_order_quantity = 0.0f;
	float todays_a_paid = 0.0f;
	std::vector<dcon::concrete_trade_fill_id> today_fills;
	state.world.for_each_concrete_trade_fill([&](auto fill) {
		if(state.world.concrete_trade_fill_get_occurred_on(fill) != state.current_date) return;
		auto ask = state.world.concrete_trade_fill_get_concrete_market_ask_from_concrete_fill_ask(fill);
		auto bid = state.world.concrete_trade_fill_get_concrete_market_bid_from_concrete_fill_bid(fill);
		if(!ask || !bid) return;
		auto commodity = state.world.concrete_market_ask_get_commodity_from_concrete_ask_commodity(ask);
		auto source = state.world.concrete_market_ask_get_site_from_concrete_ask_site(ask);
		auto destination = state.world.concrete_market_bid_get_site_from_concrete_bid_destination(bid);
		auto from = country_for_site(state, source);
		auto to = country_for_site(state, destination);
		auto quantity = state.world.concrete_trade_fill_get_quantity(fill);
		today_fills.push_back(fill);
		if(commodity == h.ore && from < 3 && to < 3) {
			exports[from] += quantity;
			imports[to] += quantity;
			filled_quantity += quantity;
			trade_price_quantity += quantity;
			trade_value += quantity * state.world.concrete_trade_fill_get_execution_price(fill);
			if(from == 0 && to == 1) {
				todays_a_paid += quantity;
			}
		}
		auto transaction = state.world.concrete_trade_fill_get_transaction_from_concrete_fill_transaction(fill);
		auto cash = transaction ? state.world.transaction_get_amount(transaction) : 0.0f;
		std::string id = "fill-" + std::to_string(fill.index());
		auto request = state.world.concrete_trade_fill_get_freight_request_from_concrete_fill_freight_request(fill);
		auto shipment = state.world.concrete_trade_fill_get_shipment_from_concrete_fill_shipment(fill);
		std::string parent = request ? "request-" + std::to_string(request.index()) : "";
		if(commodity == h.ore) {
			event(out, day, id, parent, "goods_purchase_paid", from < 3 ? std::string(1, char('A' + from)) : "?",
				to < 3 ? std::string(1, char('A' + to)) : "?", "critical_ore_units",
				quantity, cash, "fill transaction; ownership changes at source before freight");
			if(request) {
				if(shipment && state.world.shipment_is_valid(shipment)) {
					event(out, day, "shipment-" + std::to_string(shipment.index()), id,
						"shipment_dispatched", from < 3 ? std::string(1, char('A' + from)) : "?",
						to < 3 ? std::string(1, char('A' + to)) : "?", "critical_ore_units", quantity,
						0.0f, "freight contract links this paid fill to physical cargo");
					shipment_birth_day[shipment.index()] = day;
				}
				if(state.world.freight_request_get_status(request)
					== uint8_t(economy::physical::freight_market::freight_request_status::pending))
					event(out, day, parent, id, "paid_cargo_waiting_at_source",
						from < 3 ? std::string(1, char('A' + from)) : "?",
						to < 3 ? std::string(1, char('A' + to)) : "?", "critical_ore_units", quantity,
						cash, "buyer owns physical cargo; no carrier contract; not delivered");
			}
		}
	});

	state.world.for_each_freight_contract([&](dcon::freight_contract_id contract) {
		if(state.world.freight_contract_get_created_on(contract) != state.current_date) return;
		auto shipment = state.world.freight_contract_get_shipment_from_freight_contract_shipment(contract);
		auto payer = state.world.freight_contract_get_monetary_account_from_freight_contract_payer_account(contract);
		auto account_owner = economy::accounts::owner_of(state, payer);
		uint32_t importer = 3;
		for(uint32_t i = 0; i < 3; ++i) if(h.actors[i] == account_owner) importer = i;
		auto price = state.world.freight_contract_get_agreed_freight_price(contract);
		if(importer < 3) freight_cost[importer] += price;
		event(out, day, "freight-" + std::to_string(contract.index()),
			"request-" + std::to_string(state.world.freight_contract_get_freight_request_from_freight_contract_request(contract).index()),
			"freight_contract_paid", "B", importer < 3 ? std::string(1, char('A' + importer)) : "?",
			"critical_ore_units", state.world.freight_contract_get_quantity(contract), price,
			shipment ? "separate freight payment; physical shipment linked" : "contract has no live shipment");
	});

	state.world.for_each_factory([&](dcon::factory_id factory) {
		if(factory != h.factories[1]) return;
		state.world.for_each_concrete_market_bid([&](dcon::concrete_market_bid_id bid) {
			if(state.world.concrete_market_bid_get_factory_from_concrete_bid_factory(bid) != factory
				|| state.world.concrete_market_bid_get_created_on(bid) != state.current_date
				|| state.world.concrete_market_bid_get_commodity_from_concrete_bid_commodity(bid) != h.ore) return;
			unmet_order_quantity += std::max(0.0f,
				state.world.concrete_market_bid_get_remaining_quantity(bid));
		});
	});

	float paid_undelivered = 0.0f;
	float in_transit = 0.0f;
	float delayed = 0.0f;
	float blocked = 0.0f;
	state.world.for_each_freight_request([&](dcon::freight_request_id request) {
		if(state.world.freight_request_get_commodity_from_freight_request_commodity(request) != h.ore) return;
		auto buyer = state.world.freight_request_get_economic_actor_from_freight_request_requester(request);
		if(buyer != h.actors[1]) return;
		if(state.world.freight_request_get_status(request)
			== uint8_t(economy::physical::freight_market::freight_request_status::pending))
			paid_undelivered += state.world.freight_request_get_quantity(request);
	});
	state.world.for_each_shipment([&](dcon::shipment_id shipment) {
		if(state.world.shipment_get_commodity(shipment) != h.ore) return;
		auto owner_relation = state.world.shipment_get_shipment_owner(shipment);
		auto owner = owner_relation ? state.world.shipment_owner_get_economic_actor(owner_relation)
			: dcon::economic_actor_id{};
		if(owner != h.actors[1]) return;
		auto quantity = state.world.shipment_get_remaining_quantity(shipment);
		in_transit += quantity;
		auto lifecycle = state.world.shipment_get_lifecycle(shipment);
		if(lifecycle == 0) delayed += quantity;
		if(lifecycle == 3) blocked += quantity;
	});

	float raw_output[3]{};
	float factory_output[3]{};
	for(uint32_t country = 0; country < 3; ++country) {
		if(state.world.factory_get_output(h.factories[country]) >= 0.0f) {
			auto amount = state.world.factory_get_output(h.factories[country]);
			if(country == 1) factory_output[country] = amount;
			else raw_output[country] = amount;
		}
	}
	float input_stock = economy::physical::inventory::quantity(state, h.sites[1], h.ore, h.actors[1]);
	float household_consumption = 0.0f;
	if(auto need = economy::physical::exact_person_goods::need(state, h.workers[1], h.product);
		need && need->last_consumed_on == state.current_date)
		household_consumption = need->last_consumed_quantity;
	for(auto const& fill : economy::physical::exact_person_goods::fill_records()) {
		if(fill.occurred_on != state.current_date || fill.commodity != h.product) continue;
		event(out, day, "person-fill-" + std::to_string(fill.id), "production-" + std::to_string(day),
			"household_purchase_paid", "B", "B", "finished_product_units", fill.quantity,
			fill.quantity * fill.execution_price,
			"exact-person goods purchase; physical stock consumed only after delivery");
	}
	float price = trade_price_quantity > epsilon ? trade_value / trade_price_quantity : 20.0f;
	float b_balance = economy::accounts::balance(state, h.accounts[1]);
	float b_profit = state.world.factory_get_agency_recent_profit(h.factories[1]);
	float b_wage_paid = payroll_value(state, h.factories[1], false);
	float b_wage_unpaid = payroll_value(state, h.factories[1], true);
	float employment[3] = {active_contract_count(state, h.factories[0]),
		active_contract_count(state, h.factories[1]), active_contract_count(state, h.factories[2])};
	float money_error = std::abs(total_money(state) - world.seeded_money);
	out.maximum_money_conservation_error = std::max(out.maximum_money_conservation_error, money_error);
	out.household_invariants_valid = out.household_invariants_valid
		&& economy::physical::exact_person_goods::validate_canonical_household_economy(state);
	for(uint32_t country = 0; country < 3; ++country) {
		std::ostringstream row;
		row << day << ',' << char('A' + country) << ',' << number(imports[country]) << ','
			<< number(exports[country]) << ',' << number(country == 1 ? input_stock : 0.0f) << ','
			<< number(country == 1 ? paid_undelivered : 0.0f) << ','
			<< number(country == 1 ? in_transit : 0.0f) << ','
			<< number(country == 1 ? delayed : 0.0f) << ','
			<< number(country == 1 ? blocked : 0.0f) << ','
			<< number(country == 1 ? float(today_fills.size()) : 0.0f) << ','
			<< number(country == 1 ? unmet_order_quantity : 0.0f) << ','
			<< number(price) << ',' << number(country == 1 ? freight_cost[country] : 0.0f) << ','
			<< number(country == 1 ? factory_output[country] : raw_output[country]) << ','
			<< number(state.world.factory_get_actual_utilization(h.factories[country])) << ','
			<< number(country == 1 ? b_profit : state.world.factory_get_agency_recent_profit(h.factories[country])) << ','
			<< number(economy::accounts::balance(state, h.accounts[country])) << ','
			<< number(employment[country]) << ','
			<< number(country == 1 ? b_wage_paid : payroll_value(state, h.factories[country], false)) << ','
			<< number(country == 1 ? b_wage_unpaid : payroll_value(state, h.factories[country], true)) << ','
			<< number(country == 1 ? household_consumption : 0.0f)
			<< ",,not_observed_fixture_boundary";
		out.timeseries_csv.push_back(row.str());
	}
	(void)day_input_consumption;
	(void)todays_a_paid;
}

bool restore_snapshot(lab_world& world) {
	auto& old = *world.state;
	std::vector<uint8_t> bytes(sys::sizeof_save_section(old));
	auto const* end = sys::write_save_section(bytes.data(), old);
	if(end != bytes.data() + bytes.size()) return false;
	// Read the canonical section back into the same state object so fixture
	// handles remain stable while both DCON and handwritten runtime stores load.
	return sys::read_save_section(bytes.data(), end, old) == end;
}

} // namespace

intervention parse_intervention(std::string const& value) {
	if(value == "baseline" || value == "none") return intervention::none;
	if(value == "embargo" || value == "trade-embargo") return intervention::trade_embargo;
	if(value == "blockade" || value == "transport-blockade") return intervention::transport_blockade;
	if(value == "substitution" || value == "supplier-substitution") return intervention::supplier_substitution;
	throw std::invalid_argument("unknown supply-chain intervention: " + value);
}

std::string_view intervention_name(intervention value) noexcept {
	switch(value) {
	case intervention::none: return "baseline";
	case intervention::trade_embargo: return "trade_embargo";
	case intervention::transport_blockade: return "transport_blockade";
	case intervention::supplier_substitution: return "supplier_substitution";
	}
	return "unknown";
}

run_output run(config const& cfg, uint32_t save_restore_day) {
	if(cfg.days == 0 || cfg.shock_day == 0 || cfg.shock_end_day <= cfg.shock_day)
		throw std::invalid_argument("lab needs positive horizon and a non-empty intervention window");
	lab_world world = initialize(cfg);
	auto& state = *world.state;
	auto& h = world.h;
	run_output out;
	out.timeseries_csv.push_back("day,country,imports_units,exports_units,b_input_stock_units,paid_undelivered_units,in_transit_units,delayed_units,blocked_units,orders_filled_count,orders_unmet_units,observed_ore_price_cash_per_unit,freight_cost_cash_per_day,production_units_per_day,capacity_utilization_ratio,firm_recent_profit_cash,firm_cash_balance_cash,employed_contracts,wages_paid_cash,wages_unpaid_cash,household_consumption_units,government_revenue_cash,government_revenue_status");
	out.events_csv.push_back("day,event_id,parent_id,event,country_from,country_to,commodity,quantity_units,cash_amount,trace");
	std::unordered_map<uint32_t, uint32_t> shipment_birth_day;
	float a_to_b_fills_before_shock = 0.0f;
	float delivered_a_to_b_before_shock = 0.0f;
	float consumed_before_shock = 0.0f;
	float max_pre_shock_output = 0.0f;
	std::unordered_set<uint32_t> fulfilled_contracts_seen;
	bool intervention_applied = false;
	bool intervention_ended = false;
	uint32_t restore_on = save_restore_day;
	for(uint32_t day = 1; day <= cfg.days; ++day) {
		state.current_date += 1;
		if(cfg.shock != intervention::none && day == cfg.shock_day) {
			intervention_applied = true;
			if(cfg.shock == intervention::trade_embargo) {
				// B is the exporter-side source of this unilateral relationship so
				// the existing concrete-market embargo predicate blocks A -> B.
				h.embargo = state.world.force_create_unilateral_relationship(h.nations[0], h.nations[1]);
				nations::do_embargo(state, h.embargo, false);
				event(out, day, "shock-1", "", "trade_embargo_started", "B", "A",
					"critical_ore_units", 0.0f, 0.0f, "nations::do_embargo; new trade matching only");
			} else {
				if(h.ab_edge && state.world.infrastructure_edge_is_valid(h.ab_edge))
					state.world.delete_infrastructure_edge(h.ab_edge);
					event(out, day, "shock-1", "", cfg.shock == intervention::supplier_substitution
						? "supplier_substitution_under_A_B_blockade" : "transport_edge_removed",
					"A", "B", "critical_ore_units", 0.0f, 0.0f,
					"canonical spatial infrastructure graph no longer routes A-B");
			}
		}
		if(intervention_applied && !intervention_ended && day == cfg.shock_end_day) {
			intervention_ended = true;
			if(cfg.shock == intervention::trade_embargo && h.embargo)
				nations::remove_embargo(state, h.embargo, false);
			if(cfg.shock == intervention::transport_blockade || cfg.shock == intervention::supplier_substitution)
				h.ab_edge = connect(state, h.nodes[0], h.nodes[1], 30.0f);
			event(out, day, "shock-2", "shock-1", "intervention_ended", "B", "A",
				"critical_ore_units", 0.0f, 0.0f, "embargo removed or canonical infrastructure edge restored");
		}

		if(restore_on && day == restore_on) {
			if(!restore_snapshot(world)) throw std::runtime_error("canonical save/restore failed");
		}
		// This is the economically causal subset of economy::daily_update:
		// canonical firm bids and concrete matching; freight contracts and routed
		// arrivals; actual factories and payroll; then exact-person consumption.
		auto stock_before_production = economy::physical::inventory::quantity(
			state, h.sites[1], h.ore, h.actors[1]);
		economy::physical::factory_inputs::begin_planning(state);
		for(uint32_t supplier = 0; supplier < 2; ++supplier)
			(void)economy::industrial_production::produce_factory(state, h.factories[supplier]);
		(void)economy::industrial_production::plan_factory_inputs(
			state, h.factories[1], h.provinces[1], h.markets[1]);
		economy::physical::factory_inputs::fulfill(state);
		economy::firm_agency::update_decisions(state);
		economy::physical::freight_market::process_pending_requests(state);
		economy::physical::shipments::process_arrivals(state);
		auto stock_after_arrivals = economy::physical::inventory::quantity(
			state, h.sites[1], h.ore, h.actors[1]);
		(void)stock_before_production;
		float output_before = state.world.factory_get_output(h.factories[1]);
		auto actual_output = economy::industrial_production::produce_factory(state, h.factories[1]);
		auto stock_after_production = economy::physical::inventory::quantity(
			state, h.sites[1], h.ore, h.actors[1]);
		float actual_consumption = std::max(0.0f, stock_after_arrivals - stock_after_production);
		if(day < cfg.shock_day) {
			consumed_before_shock += actual_consumption;
			max_pre_shock_output = std::max(max_pre_shock_output, actual_output);
		}
		(void)output_before;
		(void)economy::firm_agency::post_output_asks(state);
		economy::physical::exact_person_goods::process_daily(state);
		float a_fill_today = 0.0f;
		float c_fill_today = 0.0f;
		float a_delivery_today = 0.0f;
		state.world.for_each_concrete_trade_fill([&](auto fill) {
			if(state.world.concrete_trade_fill_get_occurred_on(fill) != state.current_date) return;
			auto ask = state.world.concrete_trade_fill_get_concrete_market_ask_from_concrete_fill_ask(fill);
			auto bid = state.world.concrete_trade_fill_get_concrete_market_bid_from_concrete_fill_bid(fill);
			if(!ask || !bid || state.world.concrete_market_ask_get_commodity_from_concrete_ask_commodity(ask) != h.ore) return;
			auto origin = country_for_site(state,
				state.world.concrete_market_ask_get_site_from_concrete_ask_site(ask));
			auto destination = country_for_site(state,
				state.world.concrete_market_bid_get_site_from_concrete_bid_destination(bid));
			if(destination == 1 && origin == 0) a_fill_today += state.world.concrete_trade_fill_get_quantity(fill);
			if(destination == 1 && origin == 2) c_fill_today += state.world.concrete_trade_fill_get_quantity(fill);
		});
		if(day < cfg.shock_day) a_to_b_fills_before_shock += a_fill_today;
		state.world.for_each_freight_contract([&](dcon::freight_contract_id contract) {
			if(state.world.freight_contract_get_status(contract)
				!= uint8_t(economy::physical::freight_market::freight_contract_status::fulfilled)
				|| !fulfilled_contracts_seen.insert(contract.index()).second) return;
			auto request = state.world.freight_contract_get_freight_request_from_freight_contract_request(contract);
			if(!request || state.world.freight_request_get_commodity_from_freight_request_commodity(request) != h.ore) return;
			auto source = state.world.freight_request_get_site_from_freight_request_source(request);
			auto destination = state.world.freight_request_get_site_from_freight_request_destination(request);
			if(country_for_site(state, source) == 0 && country_for_site(state, destination) == 1) {
				a_delivery_today += state.world.freight_contract_get_quantity(contract);
				auto born = shipment_birth_day.find(state.world.freight_contract_get_shipment_from_freight_contract_shipment(contract).index());
				if(born != shipment_birth_day.end() && day >= cfg.shock_day && born->second < cfg.shock_day)
					event(out, day, "arrival-" + std::to_string(contract.index()),
						"shipment-" + std::to_string(born->first), "shipment_arrived_after_shock",
						"A", "B", "critical_ore_units", state.world.freight_contract_get_quantity(contract),
						0.0f, "travelling cargo completed after edge removal; route is not revalidated in transit");
			}
		});
		if(day < cfg.shock_day) delivered_a_to_b_before_shock += a_delivery_today;
		if(intervention_applied && day >= cfg.shock_day && day < cfg.shock_end_day) {
			out.a_to_b_import_during_shock += a_fill_today;
			out.c_to_b_import_during_shock += c_fill_today;
			out.b_factory_output_during_shock += actual_output;
		}
		if(intervention_ended && day >= cfg.shock_end_day) {
			out.a_to_b_import_after_shock += a_fill_today;
			out.b_factory_output_after_shock += actual_output;
		}
		if(actual_consumption > epsilon)
			event(out, day, "input-consumption-" + std::to_string(day), "production-" + std::to_string(day),
				"factory_input_consumed", "A/C", "B", "critical_ore_units", actual_consumption,
				0.0f, "physical inventory::remove through industrial_production::produce_factory");
		if(actual_output > epsilon)
		event(out, day, "production-" + std::to_string(day), "input-consumption-" + std::to_string(day),
			"factory_output_materialized", "B", "B", "finished_product_units", actual_output,
			0.0f, "canonical recipe, hired labor and physical inputs produced this quantity");
		auto wage_paid = payroll_value(state, h.factories[1], false);
		if(wage_paid > epsilon)
			event(out, day, "payroll-" + std::to_string(day), "production-" + std::to_string(day),
				"factory_payroll_paid", "B", "B", "cash_units", wage_paid, wage_paid,
				"payroll::settle_factory transfers to the exact contract worker");
		if(day >= cfg.shock_day && a_delivery_today > epsilon)
			out.a_cargo_arrived_after_shock += a_delivery_today;
		write_day(world, cfg, day, out, shipment_birth_day,
			actual_consumption);
		out.ticks_completed = day;
	}
	out.baseline_a_to_b_import_quantity = a_to_b_fills_before_shock;
	out.baseline_b_input_consumption = consumed_before_shock;
	out.baseline_b_factory_output = max_pre_shock_output;
	out.baseline_trade_valid = a_to_b_fills_before_shock > epsilon
		&& delivered_a_to_b_before_shock > epsilon && consumed_before_shock > epsilon
		&& max_pre_shock_output > epsilon;
	out.canonical_baseline_trade_valid = out.baseline_trade_valid;
	out.final_checksum = checksum(out.timeseries_csv, out.events_csv);
	return out;
}

bool write_csv(run_output const& output, std::string const& timeseries_path,
	std::string const& events_path) {
	std::ofstream timeseries(timeseries_path, std::ios::out | std::ios::trunc);
	std::ofstream events(events_path, std::ios::out | std::ios::trunc);
	if(!timeseries || !events) return false;
	for(auto const& row : output.timeseries_csv) timeseries << row << '\n';
	for(auto const& row : output.events_csv) events << row << '\n';
	return bool(timeseries) && bool(events);
}

} // namespace sys::simulation::supply_chain_lab
