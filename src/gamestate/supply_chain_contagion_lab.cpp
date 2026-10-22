#include "game_scene.hpp"
#include "economy/exact_person_economy.hpp"
#include "supply_chain_contagion_lab.hpp"

#include "actors/organizations/organizations.hpp"
#include "actors/ownership.hpp"
#include "culture/culture.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/banking/banking.hpp"
#include "economy/causal_order.hpp"
#include "economy/consent/consent.hpp"
#include "economy/economy.hpp"
#include "economy/exact_person_economy.hpp"
#include "economy/firm_agency.hpp"
#include "economy/industrial_production.hpp"
#include "economy/money.hpp"
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
#include "economy/relations/relations.hpp"
#include "economy/payroll.hpp"
#include "economy/world_trade_capacity.hpp"
#include "gamestate/serialization.hpp"
#include "gamestate/system_state.hpp"
#include "governance/constitution.hpp"
#include "governance/elections.hpp"
#include "governance/finance/finance.hpp"
#include "governance/government.hpp"
#include "governance/law/law.hpp"
#include "governance/offices.hpp"
#include "governance/parties.hpp"
#include "governance/public_administration.hpp"
#include "military/land_forces.hpp"
#include "nations/nations.hpp"
#include "nations/strategic_statecraft.hpp"
#include "persons/exact_population.hpp"
#include "persons/persons.hpp"
#include "technology/technology_kernel.hpp"
#include "world/spatial_runtime.hpp"

#include <algorithm>
#include <array>
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
	dcon::state_definition_id state_definitions[3]{};
	dcon::pop_type_id pop_types[3]{};
	dcon::organization_id banks[3]{};
	dcon::monetary_account_id bank_reserves[3]{};
	dcon::deposit_account_id bank_deposits[3]{};
	dcon::person_id bank_representatives[3]{};
	dcon::institution_id finance_ministries[3]{};
	dcon::office_id finance_offices[3]{};
	dcon::person_id finance_ministers[3]{};
	dcon::monetary_account_id treasuries[3]{};
	std::vector<dcon::obligation_id> sovereign_bonds[3];
	dcon::freight_offer_id offers[2]{};
	dcon::unilateral_relationship_id embargo{};
	dcon::infrastructure_edge_id ab_edge{};
	dcon::infrastructure_edge_id cb_edge{};
	persons::exact_population::person_key workers[3]{};
	std::vector<persons::exact_population::person_key> workforce[3];
};

struct lab_world {
	std::unique_ptr<sys::state> state = std::make_unique<sys::state>(sys::simulation_only);
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

void add_worker(lab_world& world, uint32_t country, uint64_t ordinal, float wage,
	float labor_capacity = 1.0f) {
	auto& state = *world.state;
	auto& h = world.h;
	persons::exact_population::person_key worker{7010u + country, ordinal};
	if(h.workforce[country].empty()) h.workers[country] = worker;
	auto offer = economy::physical::job_market::post_job_offer(state, h.actors[country],
		h.factories[country], h.sites[country], 0, labor_capacity, wage, 1,
		h.accounts[country], 1, state.current_date);
	if(!offer || !economy::exact_person_economy::submit_application(state,
		worker, offer, state.current_date))
		throw std::runtime_error("could not submit lab worker application");
	economy::exact_person_economy::process_pending_applications(state);
	if(economy::exact_person_economy::active_contracts_for_person(state, worker).empty())
		throw std::runtime_error("lab worker did not receive a canonical contract");
	if(country == 1) {
		auto wallet = economy::exact_person_economy::find_account(state, worker, h.settlement);
		if(!wallet || !economy::exact_person_economy::set_balance(state, wallet,
			economy::exact_person_economy::balance(state, wallet) + 400.0f))
			throw std::runtime_error("could not seed B household savings");
	}
	h.workforce[country].push_back(worker);
}

void found_government(lab_world& world, uint32_t country) {
	auto& state = *world.state;
	auto& h = world.h;
	governance::constitution::document document;
	std::string error;
	if(!governance::constitution::model("parliamentary_republic", "unitary", document, error))
		throw std::runtime_error("could not make lab constitution: " + error);
	for(auto& election : document.elections) election.term_days = 120;
	governance::constitution::founded founded;
	if(!governance::constitution::found(state, h.nations[country], document,
		state.current_date, founded, error))
		throw std::runtime_error("could not found lab constitution: " + error);
	h.finance_ministries[country] = founded.institution("finance");
	h.finance_offices[country] = founded.office("finance_minister");
	if(!h.finance_ministries[country] || !h.finance_offices[country])
		throw std::runtime_error("lab constitution has no finance ministry or minister");
	governance::government::bootstrap(state, h.nations[country], state.current_date);
	h.finance_ministers[country] = governance::offices::holder(state, h.finance_offices[country]);
	if(!h.finance_ministers[country])
		throw std::runtime_error("canonical government did not appoint a finance minister");
}

void establish_banks_and_bonds(lab_world& world, uint32_t country) {
	auto& state = *world.state;
	auto& h = world.h;
	auto bank = economy::banking::create_bank(state);
	economy::banking::bank_policy policy{};
	policy.jurisdiction = h.nations[country];
	policy.settlement = h.settlement;
	policy.lending_base_rate = 0.03f;
	policy.minimum_capital_ratio = 0.08f;
	policy.liquidity_target = 0.05f;
	policy.risk_appetite = 1.0f;
	policy.max_single_borrower_exposure = 1.0f;
	policy.reserve_requirement = 0.05f;
	policy.capital_breach_grace_days = 1;
	if(!bank || !economy::banking::configure_bank_policy(state, bank, policy))
		throw std::runtime_error("could not configure a national commercial bank");
	auto reserves = economy::banking::open_reserve_account(state, bank, h.settlement);
	if(!reserves || !economy::banking::bootstrap_set_reserve_balance(state, reserves, 2000.0f))
		throw std::runtime_error("could not seed bank reserves");
	auto depositor_person = persons::create_person(state, sys::date{1});
	auto depositor_actor = persons::actor_for_person(state, depositor_person);
	if(!depositor_person || !depositor_actor) throw std::runtime_error("could not create bank depositor");
	actors::ownership::assign_runtime_canonical_id(state, depositor_actor);
	auto deposit = economy::banking::open_deposit_account(state, bank, depositor_actor, h.settlement);
	if(!deposit || !economy::banking::bootstrap_set_deposit_balance(state, deposit, 1820.0f))
		throw std::runtime_error("could not create bank deposit liability");
	auto representative = persons::create_person(state, sys::date{1});
	if(!representative || !economy::consent::create_mandate(state, bank, representative,
	economy::consent::decision_kind::invest, state.current_date))
		throw std::runtime_error("could not establish a bank investment mandate");
	h.banks[country] = bank;
	h.bank_reserves[country] = reserves;
	h.bank_deposits[country] = deposit;
	h.bank_representatives[country] = representative;
	h.treasuries[country] = governance::finance::treasury_account_for(state,
		h.finance_ministries[country], h.settlement);
	if(!h.treasuries[country]) throw std::runtime_error("finance ministry treasury is missing");
	if(country != 1) return;
	constexpr float principal = 100.0f;
	constexpr float annual_rate = 0.05f;
	auto due_date = state.current_date + 210;
	auto government_actor = governance::actor_for_institution(state, h.finance_ministries[country]);
	auto bank_actor = actors::organizations::actor_for_organization(state, bank);
	auto proposal = economy::consent::create_proposal(state,
		economy::consent::proposal_kind::investment, government_actor, bank_actor,
		h.settlement, principal, due_date, annual_rate, state.current_date);
	if(!proposal || !economy::consent::accept_proposal(state, proposal, bank_actor,
	representative, state.current_date))
		throw std::runtime_error("bank did not consent to the sovereign bond terms");
	auto issuance = governance::finance::authorized_issue_public_debt_with_consent(state,
	h.finance_ministers[country], h.treasuries[country], reserves, principal,
	due_date, annual_rate, state.current_date, proposal);
	if(!issuance) throw std::runtime_error("authorized public-debt issuance failed");
	auto bond = state.world.fiscal_action_get_obligation_from_fiscal_action_resulting_obligation(issuance);
	if(!bond) throw std::runtime_error("issued bond has no legal obligation");
	h.sovereign_bonds[country].push_back(bond);
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

	// A minimal state has no commodity rows until the first one is created;
	// reserve the canonical commodity id for money before adding trade goods.
	if(!state.world.commodity_is_valid(economy::money)) (void)state.world.create_commodity();
	// Public administration, taxes, wages and debt all settle in the canonical
	// money commodity. A second "cash" commodity would split this one economy.
	h.settlement = economy::money;
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
		h.state_definitions[country] = state.world.create_state_definition();
		state.world.force_create_abstract_state_membership(h.provinces[country], h.state_definitions[country]);
		h.markets[country] = state.world.create_market();
		state.world.state_instance_set_market_from_local_market(zone, h.markets[country]);
		state.world.state_instance_set_capital(zone, h.provinces[country]);
		state.world.market_set_zone_from_local_market(h.markets[country], zone);
		state.world.province_set_state_membership(h.provinces[country], zone);
		state.world.province_set_nation_from_province_ownership(h.provinces[country], h.nations[country]);
		state.world.province_set_control_ratio(h.provinces[country], 1.0f);
		state.world.nation_set_capital(h.nations[country], h.provinces[country]);
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

	for(uint32_t country = 0; country < 3; ++country) {
		h.pop_types[country] = state.world.create_pop_type();
		state.world.pop_type_set_strata(h.pop_types[country], uint8_t(culture::pop_strata::poor));
		persons::exact_population::cell_descriptor people;
		people.source_population_cell = 7010u + country;
		people.literal_count = 120;
		people.bootstrap_base_day = state.current_date.to_raw_value() - 1;
		people.demographic_seed = uint64_t(4242u + country);
		people.home_site = h.sites[country];
		people.source_pop_type = h.pop_types[country];
		people.source_culture = state.world.create_culture();
		people.source_religion = state.world.create_religion();
		if(persons::exact_population::register_synthetic_population_cell(state, people).result
			!= persons::exact_population::status::created)
			throw std::runtime_error("could not register the canonical electorate");
		for(uint64_t ordinal = 0; ordinal < people.literal_count; ++ordinal) {
			auto key = persons::exact_population::person_key{people.source_population_cell, ordinal};
			auto account = economy::exact_person_economy::open_account(state, key, h.settlement);
			if(!account || !economy::exact_person_economy::set_balance(state, account,
				country == 1 ? 100.0f : 20.0f))
				throw std::runtime_error("could not open a household's real money account");
		}
	}
	state.world.market_resize_price(state.world.commodity_size());
	for(uint32_t country = 0; country < 3; ++country) {
		state.world.market_set_price(h.markets[country], h.ore, 20.0f);
		state.world.market_set_price(h.markets[country], h.product, 100.0f);
	}
	h.ab_edge = connect(state, h.nodes[0], h.nodes[1], 30.0f);
	h.cb_edge = connect(state, h.nodes[2], h.nodes[1], 40.0f);

	const float a_share = std::clamp(cfg.a_dependency_share, 0.02f, 0.98f);
	const float supply_capacities[3] = {a_share, 0.0f, 1.0f - a_share};
	for(uint32_t country = 0; country < 3; ++country) {
		h.organizations[country] = actors::organizations::create_company(state);
		h.actors[country] = actors::organizations::actor_for_organization(state, h.organizations[country]);
		h.accounts[country] = economy::accounts::open_account(state, h.actors[country], h.settlement);
		if(!h.accounts[country] || !economy::accounts::bootstrap_set_balance(state,
			h.accounts[country], country == 1 ? 1000.0f : 1000.0f))
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

	for(uint32_t country = 0; country < 3; ++country) {
		uint32_t hired = 0;
		for(uint64_t ordinal = 0; ordinal < 120; ++ordinal) {
			auto person = persons::exact_population::person_key{7010u + country, ordinal};
			if(!economy::exact_person_economy::is_work_eligible(state, person)) continue;
			add_worker(world, country, ordinal, country == 1 ? 5.0f : 0.2f,
				country == 1 ? 1.0f / 16.0f : 1.0f);
			if(++hired >= (country == 1 ? 16u : 1u)) break;
		}
		if(hired != (country == 1 ? 16u : 1u))
			throw std::runtime_error("synthetic workforce has too few working-age residents");
	}
	for(auto worker : h.workforce[1])
		if(!economy::physical::exact_person_goods::set_need(state, worker, h.product, 0.03125f))
			throw std::runtime_error("could not set canonical household need");

	for(uint32_t country = 0; country < 3; ++country) found_government(world, country);
	governance::public_administration::bootstrap(state);
	for(uint32_t country = 0; country < 3; ++country) establish_banks_and_bonds(world, country);
	economy::banking::update_bank_statuses(state, state.current_date);

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

std::string csv_text(std::string const& value) {
	if(value.find_first_of(",\"\r\n") == std::string::npos) return value;
	std::string escaped = "\"";
	for(char character : value) {
		if(character == '"') escaped += '"';
		escaped += character;
	}
	escaped += '"';
	return escaped;
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
	row << day << ',' << csv_text(id) << ',' << csv_text(parent) << ',' << csv_text(kind) << ','
		<< csv_text(from) << ',' << csv_text(to) << ',' << csv_text(commodity) << ','
		<< number(quantity) << ',' << number(cash) << ',' << csv_text(trace);
	out.events_csv.push_back(row.str());
}

struct tax_flow { float assessed = 0.0f; float collected = 0.0f; };
tax_flow taxes_for_day(sys::state const&, dcon::nation_id);
float national_income(sys::state const&, dcon::nation_id);
uint32_t unemployment_count(sys::state const&, uint32_t country);
float household_consumption(sys::state const&, lab_handles const&, uint32_t country);

void record_employment_starts(sys::state const& state, lab_handles const& h,
	run_output& out, uint32_t day, std::unordered_set<uint64_t>& seen) {
	auto snapshot = economy::exact_person_economy::export_snapshot(state);
	for(auto const& contract : snapshot.contracts) {
		if(contract.status != economy::exact_person_economy::contract_status::active
			|| contract.start_date != state.current_date || !seen.insert(contract.id).second) continue;
		uint32_t country = 3;
		for(uint32_t i = 0; i < 3; ++i) if(contract.factory == h.factories[i]) country = i;
		if(country == 3) continue;
		event(out, day, "employment-start-" + std::to_string(contract.id), "",
			"employment_contract_started", std::string(1, char('A' + country)),
			std::string(1, char('A' + country)), "worker_contracts", contract.labor_capacity,
			contract.pay_period_days ? contract.wage_rate / float(contract.pay_period_days) : 0.0f,
			"canonical job-market contract accepted; worker_cell="
				+ std::to_string(contract.worker.source_population_cell) + ";worker_ordinal="
				+ std::to_string(contract.worker.ordinal) + ";wage_rate=" + number(contract.wage_rate));
	}
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
	float consumption[3]{};
	for(uint32_t country = 0; country < 3; ++country)
		consumption[country] = household_consumption(state, h, country);
	for(auto const& fill : economy::physical::exact_person_goods::fill_records(state)) {
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
		auto nation = h.nations[country];
		auto tax = taxes_for_day(state, nation);
		auto finance_position = governance::finance::fiscal_position_for(state,
			h.finance_ministries[country], h.settlement);
		auto bank_sheet = economy::banking::bank_balance_sheet(state,
			h.banks[country], h.settlement);
		auto chamber = governance::government::confidence_chamber(state, nation);
		std::string seats_summary;
		for(auto party : governance::parties::parties_of(state, nation)) {
			auto seats = governance::government::seats_of(state, party, chamber, state.current_date);
			if(!seats) continue;
			if(!seats_summary.empty()) seats_summary += ';';
			seats_summary += std::to_string(party.index()) + ':' + std::to_string(seats);
		}
		std::string coalition_summary;
		for(auto party : governance::government::coalition(state, nation)) {
			if(!coalition_summary.empty()) coalition_summary += ';';
			coalition_summary += std::to_string(party.index());
		}
		std::string vote_summary;
		for(auto const& [party, share] : governance::elections::last_shares(state, nation)) {
			if(!vote_summary.empty()) vote_summary += ';';
			vote_summary += std::to_string(party) + ':' + number(share);
		}
		float enacted_statutes = 0.0f;
		state.world.for_each_legal_instrument([&](dcon::legal_instrument_id instrument) {
			if(state.world.legal_instrument_get_nation_from_legal_instrument_nation_jurisdiction(instrument) == nation
				&& state.world.legal_instrument_get_kind(instrument) == uint8_t(governance::law::legal_instrument_kind::statute)
				&& state.world.legal_instrument_get_status(instrument) == uint8_t(governance::law::legal_status::enacted))
				++enacted_statutes;
		});
		float tax_rate = governance::law::effective_amount(state, governance::national(nation),
			governance::law::policy_rule_kind::income_tax_rate, state.current_date, 0).value_or(0.0f);
		uint32_t defaulted = 0;
		for(auto bond : h.sovereign_bonds[country])
			if(state.world.obligation_get_status(bond) == uint8_t(economy::relations::obligation_status::defaulted)) ++defaulted;
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
			<< number(consumption[country]) << ',' << number(national_income(state, nation)) << ','
			<< unemployment_count(state, country) << ',' << number(tax.assessed) << ',' << number(tax.collected) << ','
			<< number(governance::public_administration::treasury_cash(state, nation)) << ','
			<< number(finance_position.public_debt_outstanding) << ','
			<< number(finance_position.public_debt_defaulted) << ','
			<< number(economy::accounts::balance(state, h.bank_reserves[country])) << ','
			<< number(bank_sheet.public_debt_claims) << ',' << number(bank_sheet.public_debt_assets) << ','
			<< number(bank_sheet.defaulted_public_debt_impairment) << ',' << number(bank_sheet.net_worth) << ','
			<< number(bank_sheet.capital_ratio) << ','
			<< uint32_t(economy::banking::status_of(state, h.banks[country])) << ','
			<< number(economy::banking::available_factory_credit(state, h.factories[country], h.settlement)) << ','
			<< number(tax_rate) << ',' << defaulted << ','
			<< (vote_summary.empty() ? "none" : vote_summary) << ','
			<< (seats_summary.empty() ? "none" : seats_summary) << ','
			<< (coalition_summary.empty() ? "none" : coalition_summary) << ','
			<< number(enacted_statutes);
		out.timeseries_csv.push_back(row.str());
	}
	(void)day_input_consumption;
	(void)todays_a_paid;
}

tax_flow taxes_for_day(sys::state const& state, dcon::nation_id nation) {
	tax_flow result{};
	auto household_sector = governance::public_administration::household_sector_actor(state, nation);
	state.world.for_each_fiscal_action([&](dcon::fiscal_action_id action) {
		if(state.world.fiscal_action_get_kind(action)
			!= uint8_t(governance::finance::fiscal_action_kind::tax_assessment)
			|| state.world.fiscal_action_get_occurred_on(action) != state.current_date) return;
		auto obligation = state.world.fiscal_action_get_obligation_from_fiscal_action_resulting_obligation(action);
		if(!obligation || state.world.obligation_get_economic_actor_from_obligation_debtor(obligation)
			!= household_sector) return;
		auto face = state.world.obligation_get_original_principal(obligation);
		result.assessed += face;
		result.collected += std::clamp(face - economy::relations::total_due(state, obligation), 0.0f, face);
	});
	return result;
}

float national_income(sys::state const& state, dcon::nation_id nation) {
	return state.world.nation_get_total_poor_income(nation)
		+ state.world.nation_get_total_middle_income(nation)
		+ state.world.nation_get_total_rich_income(nation);
}

void record_public_debt_service_events(sys::state const& state, lab_handles const& h,
	run_output& out, uint32_t day, std::unordered_set<uint32_t>& seen) {
	for(auto bond : h.sovereign_bonds[1]) {
		state.world.obligation_for_each_transaction_obligation_as_obligation(bond, [&](auto relation) {
			auto transaction = state.world.transaction_obligation_get_transaction(relation);
			if(!transaction || state.world.transaction_get_kind(transaction)
				!= uint8_t(economy::relations::transaction_kind::public_debt_service)
				|| state.world.transaction_get_timestamp(transaction) != state.current_date
				|| !seen.insert(transaction.index()).second) return;
			auto sheet = economy::banking::bank_balance_sheet(state, h.banks[1], h.settlement);
			auto amount = state.world.transaction_get_amount(transaction);
			event(out, day, "debt-service-" + std::to_string(transaction.index()),
				"bond-issued-" + std::to_string(bond.index()), "sovereign_debt_service_paid",
				"B", "B", "cash_units", amount, amount,
				"obligation_id=" + std::to_string(bond.index()) + ";treasury_after="
					+ number(economy::accounts::balance(state, h.treasuries[1])) + ";bank_reserves_after="
					+ number(economy::accounts::balance(state, h.bank_reserves[1])) + ";bank_capital="
					+ number(sheet.net_worth) + ";available_credit="
					+ number(economy::banking::available_factory_credit(state, h.factories[1], h.settlement)));
		});
	}
}

uint32_t unemployment_count(sys::state const& state, uint32_t country) {
	uint32_t result = 0;
	for(uint64_t ordinal = 0; ordinal < 120; ++ordinal)
		if(economy::exact_person_economy::is_unemployed(state,
			{7010u + country, ordinal})) ++result;
	return result;
}

float household_consumption(sys::state const& state, lab_handles const& h, uint32_t country) {
	float result = 0.0f;
	for(auto person : h.workforce[country])
		if(auto need = economy::physical::exact_person_goods::need(state, person, h.product);
			need && need->last_consumed_on == state.current_date)
			result += need->last_consumed_quantity;
	return result;
}

void record_election_events(sys::state const& state, run_output& out, uint32_t day,
	std::unordered_set<uint32_t>& seen) {
	state.world.for_each_election([&](dcon::election_id election) {
		if(state.world.election_get_held_on(election) != state.current_date
			|| !seen.insert(election.index()).second) return;
		auto chamber = state.world.election_get_institution_from_election_institution(election);
		auto nation = chamber ? governance::nation_of(state, chamber) : dcon::nation_id{};
		if(!nation) return;
		uint32_t country = 3;
		for(uint32_t i = 0; i < 3; ++i) if(nation == dcon::nation_id{dcon::nation_id::value_base_t(i)}) country = i;
		if(country == 3) return;
		++out.elections_recorded;
		state.world.election_for_each_election_result_election_as_election(election, [&](auto relation) {
			auto result = state.world.election_result_election_get_election_result(relation);
			auto party = state.world.election_result_get_organization_from_election_result_party(result);
			event(out, day, "election-" + std::to_string(election.index()) + "-party-" + std::to_string(party.index()),
				"election-" + std::to_string(election.index()), "party_election_result",
				std::string(1, char('A' + country)), std::string(1, char('A' + country)),
				"party_votes", state.world.election_result_get_votes(result), 0.0f,
				"party_id=" + std::to_string(party.index()) + ";seats="
					+ std::to_string(state.world.election_result_get_seats(result))
					+ ";chamber_id=" + std::to_string(chamber.index()));
		});
	});
}

void record_government_events(sys::state const& state, run_output& out, uint32_t day,
	std::array<std::string, 3>& coalition_signatures) {
	for(uint32_t country = 0; country < 3; ++country) {
		auto nation = dcon::nation_id{dcon::nation_id::value_base_t(country)};
		auto coalition = governance::government::coalition(state, nation);
		std::sort(coalition.begin(), coalition.end(), [&](auto a, auto b) {
			return state.world.organization_get_canonical_id(a) < state.world.organization_get_canonical_id(b);
		});
		std::string signature;
		for(auto party : coalition) {
			if(!signature.empty()) signature += ';';
			signature += std::to_string(party.index());
		}
		if(signature != coalition_signatures[country]) {
			event(out, day, "coalition-" + std::to_string(country) + "-" + std::to_string(day), "",
				"government_coalition_formed", std::string(1, char('A' + country)),
				std::string(1, char('A' + country)), "party_ids", float(coalition.size()), 0.0f,
				"party_ids=" + (signature.empty() ? "none" : signature));
			coalition_signatures[country] = signature;
		}
	}
}

void record_law_events(sys::state const& state, run_output& out, uint32_t day,
	std::unordered_set<uint32_t>& seen) {
	state.world.for_each_legal_instrument([&](dcon::legal_instrument_id instrument) {
		if(state.world.legal_instrument_get_status(instrument)
			!= uint8_t(governance::law::legal_status::enacted)
			|| state.world.legal_instrument_get_enacted_on(instrument) != state.current_date
			|| !seen.insert(instrument.index()).second) return;
		auto nation = state.world.legal_instrument_get_nation_from_legal_instrument_nation_jurisdiction(instrument);
		uint32_t country = 3;
		for(uint32_t i = 0; i < 3; ++i) if(nation == dcon::nation_id{dcon::nation_id::value_base_t(i)}) country = i;
		if(country == 3) return;
		++out.statutes_recorded;
		std::string rules;
		state.world.legal_instrument_for_each_legal_instrument_policy_rule_as_legal_instrument(instrument, [&](auto relation) {
			auto rule = state.world.legal_instrument_policy_rule_get_policy_rule(relation);
			if(!rules.empty()) rules += '|';
		rules += "kind=" + std::to_string(state.world.policy_rule_get_kind(rule))
				+ ";parameter=" + std::to_string(state.world.policy_rule_get_parameter(rule))
				+ ";amount=" + number(state.world.policy_rule_get_amount(rule));
			if(state.world.policy_rule_get_kind(rule)
				== uint8_t(governance::law::policy_rule_kind::substantive_policy)) {
				auto topic = governance::policy::topic_id(state.world.policy_rule_get_topic_id(rule));
				auto topic_definition = governance::policy::definition(topic);
				rules += ";topic=" + std::to_string(state.world.policy_rule_get_topic_id(rule))
					+ ":" + (topic_definition ? std::string(topic_definition->name) : "unknown")
					+ ";category=" + std::to_string(state.world.policy_rule_get_category_value(rule))
					+ ";boolean=" + std::to_string(state.world.policy_rule_get_boolean_value(rule))
					+ ";auxiliary=" + number(state.world.policy_rule_get_auxiliary(rule));
			}
		});
		event(out, day, "law-" + std::to_string(instrument.index()), "",
			"new_law_enacted", std::string(1, char('A' + country)), std::string(1, char('A' + country)),
			"legal_instrument", 1.0f, 0.0f,
			"instrument_id=" + std::to_string(instrument.index()) + ";kind="
				+ std::to_string(state.world.legal_instrument_get_kind(instrument)) + ";effective_day="
				+ std::to_string(state.world.legal_instrument_get_effective_from(instrument).to_raw_value())
				+ ";rules=" + (rules.empty() ? "none" : rules));
	});
}

bool issue_countercyclical_bond(lab_world& world, float principal, uint32_t day,
	run_output& out) {
	if(!std::isfinite(principal) || principal <= epsilon) return false;
	auto& state = *world.state;
	auto& h = world.h;
	constexpr uint32_t tenor_days = 210;
	constexpr float annual_rate = 0.05f;
	auto bank = h.banks[1];
	auto reserves = h.bank_reserves[1];
	principal = std::min(principal, economy::accounts::balance(state, reserves));
	if(principal <= epsilon) return false;
	auto minister = governance::offices::holder(state, h.finance_offices[1]);
	auto bank_actor = actors::organizations::actor_for_organization(state, bank);
	auto government_actor = governance::actor_for_institution(state, h.finance_ministries[1]);
	auto due_date = state.current_date + tenor_days;
	auto proposal = economy::consent::create_proposal(state,
		economy::consent::proposal_kind::investment, government_actor, bank_actor,
		h.settlement, principal, due_date, annual_rate, state.current_date);
	if(!minister || !proposal || !economy::consent::accept_proposal(state, proposal,
		bank_actor, h.bank_representatives[1], state.current_date)) return false;
	auto issuance = governance::finance::authorized_issue_public_debt_with_consent(state,
		minister, h.treasuries[1], reserves, principal, due_date, annual_rate,
		state.current_date, proposal);
	if(!issuance) return false;
	auto bond = state.world.fiscal_action_get_obligation_from_fiscal_action_resulting_obligation(issuance);
	if(!bond) return false;
	h.sovereign_bonds[1].push_back(bond);
	std::string coalition_ids;
	for(auto party : governance::government::coalition(state, h.nations[1])) {
		if(!coalition_ids.empty()) coalition_ids += ';';
		coalition_ids += std::to_string(party.index());
	}
	event(out, day, "stabilizer-bond-" + std::to_string(bond.index()), "tax-shortfall-trigger",
		"countercyclical_public_debt_issued", "B", "B", "cash_units", principal, principal,
		"finance minister authorized issuance; bank consented; reserve cash transferred to treasury"
			+ std::string(";finance_minister_person_id=") + std::to_string(minister.index())
			+ ";coalition_party_ids=" + (coalition_ids.empty() ? "none" : coalition_ids));
	return true;
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
	if(cfg.days == 0 || cfg.days > 50000 || cfg.shock_day == 0 || cfg.shock_end_day <= cfg.shock_day)
		throw std::invalid_argument("lab needs positive horizon and a non-empty intervention window");
	lab_world world = initialize(cfg);
	auto& state = *world.state;
	auto& h = world.h;
	run_output out;
	out.timeseries_csv.push_back("day,country,imports_units,exports_units,b_input_stock_units,paid_undelivered_units,in_transit_units,delayed_units,blocked_units,orders_filled_count,orders_unmet_units,observed_ore_price_cash_per_unit,freight_cost_cash_per_day,production_units_per_day,capacity_utilization_ratio,firm_recent_profit_cash,firm_cash_balance_cash,employed_contracts,wages_paid_cash,wages_unpaid_cash,household_consumption_units,wage_income_cash,unemployed_people,tax_assessed_cash,tax_collected_cash,treasury_cash,public_debt_outstanding_cash,public_debt_defaulted_cash,bank_reserves_cash,bank_debt_claims_face_cash,bank_debt_carrying_assets_cash,bank_default_impairment_cash,bank_net_worth_cash,bank_capital_ratio,bank_status,available_new_credit_cash,income_tax_rate,defaulted_bonds,latest_party_vote_shares,lower_chamber_party_seats,coalition_party_ids,enacted_statutes");
	out.events_csv.push_back("day,event_id,parent_id,event,country_from,country_to,commodity,quantity_units,cash_amount,trace");
	std::unordered_set<uint32_t> seen_elections, seen_laws, defaulted_bonds_seen, seen_credit_requests;
	std::unordered_set<uint32_t> seen_debt_transactions;
	std::unordered_set<uint64_t> seen_employment_contracts;
	std::array<std::string, 3> coalition_signatures{};
	record_employment_starts(state, h, out, 0, seen_employment_contracts);
	record_election_events(state, out, 0, seen_elections);
	record_government_events(state, out, 0, coalition_signatures);
	record_law_events(state, out, 0, seen_laws);
	for(uint32_t country = 0; country < 3; ++country)
		for(auto bond : h.sovereign_bonds[country])
			event(out, 0, "bond-issued-" + std::to_string(bond.index()), "",
				"consented_sovereign_bond_issued", std::string(1, char('A' + country)),
				std::string(1, char('A' + country)), "cash_units",
				state.world.obligation_get_original_principal(bond),
				state.world.obligation_get_original_principal(bond),
				"finance ministry and bank signed the obligation; issuance moved reserve cash to treasury");
	std::unordered_map<uint32_t, uint32_t> shipment_birth_day;
	float a_to_b_fills_before_shock = 0.0f;
	float delivered_a_to_b_before_shock = 0.0f;
	float consumed_before_shock = 0.0f;
	float max_pre_shock_output = 0.0f;
	std::unordered_set<uint32_t> fulfilled_contracts_seen;
	std::vector<float> baseline_tax_window;
	std::vector<float> shock_tax_window;
	bool stabilizer_triggered = false;
	bool intervention_applied = false;
	bool intervention_ended = false;
	uint32_t restore_on = save_restore_day;
	out.minimum_post_default_credit_available = std::numeric_limits<float>::infinity();
	float previous_employment[3] = {active_contract_count(state, h.factories[0]),
		active_contract_count(state, h.factories[1]), active_contract_count(state, h.factories[2])};
	for(uint32_t day = 1; day <= cfg.days; ++day) {
		state.current_date += 1;
		if(cfg.shock != intervention::none && day == cfg.shock_day) {
			intervention_applied = true;
			if(cfg.shock == intervention::trade_embargo) {
				// A is the exporter-side source of this unilateral relationship so
				// the existing concrete-market embargo predicate blocks A -> B.
				h.embargo = state.world.force_create_unilateral_relationship(h.nations[0], h.nations[1]);
				nations::do_embargo(state, h.embargo, false);
				event(out, day, "shock-1", "", "trade_embargo_started", "A", "B",
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
		// Run the whole canonical daily loop: input orders, real freight, factories,
		// wages, labor adjustment, households, taxation, public administration,
		// elections, coalition formation, lawmaking and bank settlement.
		economy::daily_update(state, false, 0.0f);
		float actual_output = std::max(0.0f, state.world.factory_get_output(h.factories[1]));
		// This factory's authored recipe consumes exactly one ore per product unit.
		float actual_consumption = actual_output;
		if(day < cfg.shock_day) {
			consumed_before_shock += actual_consumption;
			max_pre_shock_output = std::max(max_pre_shock_output, actual_output);
		}
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

		auto b_tax = taxes_for_day(state, h.nations[1]);
		for(uint32_t country = 0; country < 3; ++country) {
			auto tax = taxes_for_day(state, h.nations[country]);
			if(tax.assessed > epsilon || tax.collected > epsilon)
				event(out, day, "tax-flow-" + std::to_string(country) + "-" + std::to_string(day), "",
					"income_tax_assessed_collected", std::string(1, char('A' + country)),
					std::string(1, char('A' + country)), "cash_units", tax.assessed, tax.collected,
					"tax obligations assessed and settled through canonical public-finance accounts");
		}
		if(day < cfg.shock_day) {
			baseline_tax_window.push_back(b_tax.collected);
			if(baseline_tax_window.size() > 30) baseline_tax_window.erase(baseline_tax_window.begin());
		}
		if(day >= cfg.shock_day) {
			shock_tax_window.push_back(b_tax.collected);
			if(shock_tax_window.size() > 30) shock_tax_window.erase(shock_tax_window.begin());
		}
		if(cfg.countercyclical_borrowing && intervention_applied && !stabilizer_triggered
			&& day >= cfg.shock_day + 30
			&& baseline_tax_window.size() == 30 && shock_tax_window.size() == 30) {
			float baseline_mean = 0.0f, shock_mean = 0.0f;
			for(auto value : baseline_tax_window) baseline_mean += value / float(baseline_tax_window.size());
			for(auto value : shock_tax_window) shock_mean += value / float(shock_tax_window.size());
			if(baseline_mean > epsilon && shock_mean < baseline_mean * 0.80f) {
				stabilizer_triggered = true;
				float realized_revenue_gap = std::max(0.0f, baseline_mean - shock_mean);
				float principal = realized_revenue_gap * 90.0f;
				event(out, day, "tax-shortfall-trigger", "", "wage_tax_revenue_shortfall_triggered",
					"B", "B", "cash_units", realized_revenue_gap, 0.0f,
					"baseline_30_day_mean=" + number(baseline_mean) + ";observed_30_day_mean="
						+ number(shock_mean) + ";policy_threshold_share=0.800000;requested_quarterly_borrowing="
						+ number(principal));
				if(!issue_countercyclical_bond(world, principal, day, out))
					event(out, day, "stabilizer-bond-rejected", "tax-shortfall-trigger",
						"countercyclical_public_debt_rejected", "B", "B", "cash_units", principal, 0.0f,
						"finance authority, debt law, bank consent or reserve constraint rejected the proposed issue"
							";finance_minister_person_id=" + std::to_string(
								governance::offices::holder(state, h.finance_offices[1]).index()));
			}
		}

		record_election_events(state, out, day, seen_elections);
		record_employment_starts(state, h, out, day, seen_employment_contracts);
		record_government_events(state, out, day, coalition_signatures);
		record_law_events(state, out, day, seen_laws);
		for(auto bond : h.sovereign_bonds[1]) {
			if(state.world.obligation_get_status(bond) == uint8_t(economy::relations::obligation_status::defaulted)
				&& defaulted_bonds_seen.insert(bond.index()).second) {
				out.sovereign_default_observed = true;
				event(out, day, "default-" + std::to_string(bond.index()), "bond-issued-" + std::to_string(bond.index()),
					"sovereign_payment_defaulted", "B", "B", "legal_claim_cash",
					economy::relations::total_due(state, bond), 0.0f,
					"statutory grace period elapsed; creditor retains the legal claim at face value");
				auto sheet = economy::banking::bank_balance_sheet(state, h.banks[1], h.settlement);
				event(out, day, "bank-impairment-" + std::to_string(bond.index()),
					"default-" + std::to_string(bond.index()), "bank_sovereign_asset_impaired",
					"B", "B", "cash_units", sheet.defaulted_public_debt_assets,
					0.0f,
					"legal_claim_retained=" + number(sheet.public_debt_claims) + ";carrying_asset="
						+ number(sheet.public_debt_assets) + ";impairment="
						+ number(sheet.defaulted_public_debt_impairment) + ";bank_net_worth=" + number(sheet.net_worth)
						+ ";capital_ratio=" + number(sheet.capital_ratio) + ";available_credit="
						+ number(economy::banking::available_factory_credit(state, h.factories[1], h.settlement)));
			}
		}
		record_public_debt_service_events(state, h, out, day, seen_debt_transactions);
		for(uint32_t country = 0; country < 3; ++country) {
			auto factory = h.factories[country];
			state.world.for_each_firm_capital_request([&](dcon::firm_capital_request_id request) {
				if(state.world.firm_capital_request_get_requested_on(request) != state.current_date) return;
				auto linked_factory = state.world.firm_capital_request_get_factory_from_firm_capital_request_factory(request);
				if(linked_factory != factory) return;
				if(!seen_credit_requests.insert(request.index()).second) return;
				auto status = state.world.firm_capital_request_get_status(request);
				auto bank = state.world.firm_capital_request_get_organization_from_firm_capital_request_bank(request);
				event(out, day, "credit-request-" + std::to_string(request.index()), "",
					status == 1 || status == 2 ? "firm_credit_funded" : "firm_credit_rejected",
					std::string(1, char('A' + country)), std::string(1, char('A' + country)),
					"cash_units", state.world.firm_capital_request_get_requested_amount(request),
					state.world.firm_capital_request_get_funded_amount(request),
					"factory_id=" + std::to_string(factory.index()) + ";bank_id="
						+ std::to_string(bank.index()) + ";status=" + std::to_string(status));
			});
		}
		for(uint32_t country = 0; country < 3; ++country) {
			auto employed = active_contract_count(state, h.factories[country]);
			if(employed + epsilon < previous_employment[country])
				event(out, day, "employment-loss-" + std::to_string(country) + "-" + std::to_string(day), "",
					"employment_contracts_ended", std::string(1, char('A' + country)),
					std::string(1, char('A' + country)), "worker_contracts",
					previous_employment[country] - employed, 0.0f,
					"active canonical contracts fell after firm decisions and labor-dynamics settlement");
			previous_employment[country] = employed;
		}
		std::vector<std::string> banking_errors;
		out.banking_invariants_valid = economy::banking::validate_canonical_banking_state(state, banking_errors)
			&& out.banking_invariants_valid;
		float money_error = std::abs(total_money(state) - world.seeded_money);
		out.maximum_money_conservation_error = std::max(out.maximum_money_conservation_error, money_error);
		out.monetary_invariants_valid = out.monetary_invariants_valid && money_error < 1.0e-2f;
		out.household_invariants_valid = out.household_invariants_valid
			&& economy::physical::exact_person_goods::validate_canonical_household_economy(state);
		auto b_sheet = economy::banking::bank_balance_sheet(state, h.banks[1], h.settlement);
		out.maximum_defaulted_bank_claims = std::max(out.maximum_defaulted_bank_claims,
			b_sheet.defaulted_public_debt_assets);
		if(b_sheet.defaulted_public_debt_assets > epsilon) {
			out.minimum_post_default_credit_available = std::min(out.minimum_post_default_credit_available,
				economy::banking::available_factory_credit(state, h.factories[1], h.settlement));
		}
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
	if(!std::isfinite(out.minimum_post_default_credit_available)) out.minimum_post_default_credit_available = 0.0f;
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
