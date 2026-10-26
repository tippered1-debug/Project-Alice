#include "construction_industry.hpp"

#include "system_state.hpp"
#include "actors/organizations/organizations.hpp"
#include "actors/ownership.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/capital_projects.hpp"
#include "economy/economy_stats.hpp"
#include "economy/exact_person_economy.hpp"
#include "economy/payroll.hpp"
#include "economy/physical/concrete_market.hpp"
#include "economy/physical/inventory.hpp"
#include "governance/governance.hpp"
#include "world/site.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace economy::construction_industry {
namespace {
constexpr float epsilon = 1.0e-6f;
constexpr uint8_t lifecycle_bankrupt = 2;

float finite_nonnegative(float value) {
	return std::isfinite(value) && value > 0.0f ? value : 0.0f;
}

dcon::site_id works_site(sys::state const& state, dcon::factory_id works) {
	return world::site::site_for_factory(state, works);
}

dcon::province_id works_province(sys::state const& state, dcon::factory_id works) {
	auto site = works_site(state, works);
	return site ? state.world.site_get_province_from_site_location(site) : dcon::province_id{};
}

dcon::province_id project_province(sys::state const& state, dcon::capital_project_id project) {
	auto yard = state.world.capital_project_get_site_from_capital_project_site(project);
	return yard ? state.world.site_get_province_from_site_location(yard) : dcon::province_id{};
}

dcon::monetary_account_id project_account(sys::state const& state, dcon::capital_project_id project) {
	return state.world.capital_project_get_monetary_account_from_capital_project_account(project);
}

dcon::monetary_account_id contractor_account(sys::state const& state, dcon::factory_id works) {
	auto owner = actors::organizations::operator_actor_for_factory(state, works);
	auto settlement = state.world.factory_get_payroll_settlement(works);
	return owner && settlement ? accounts::find_account(state, owner, settlement) : dcon::monetary_account_id{};
}

bool operating(sys::state const& state, dcon::factory_id works) {
	return is_works(state, works)
		&& state.world.factory_get_agency_lifecycle_status(works) < lifecycle_bankrupt
		&& actors::organizations::operator_organization_for_factory(state, works)
		&& works_province(state, works);
}

std::vector<dcon::construction_contract_id> contracts_of(sys::state const& state, dcon::factory_id works) {
	std::vector<dcon::construction_contract_id> result;
	state.world.factory_for_each_construction_contract_works_as_factory(works, [&](auto relation) {
		auto contract = state.world.construction_contract_works_get_construction_contract(relation);
		if(state.world.construction_contract_get_status(contract) == uint8_t(contract_status::active))
			result.push_back(contract);
	});
	// Crews serve their contracts in the order they were signed.
	std::sort(result.begin(), result.end(), [&](auto left, auto right) {
		auto a = state.world.construction_contract_get_signed_on(left);
		auto b = state.world.construction_contract_get_signed_on(right);
		return a != b ? a < b : left.index() < right.index();
	});
	return result;
}

dcon::capital_project_id project_of(sys::state const& state, dcon::construction_contract_id contract) {
	return state.world.construction_contract_get_capital_project_from_construction_contract_project(contract);
}

dcon::factory_id works_of(sys::state const& state, dcon::construction_contract_id contract) {
	return state.world.construction_contract_get_factory_from_construction_contract_works(contract);
}

bool defaulted_before(sys::state const& state, dcon::capital_project_id project, dcon::factory_id works) {
	bool result = false;
	state.world.capital_project_for_each_construction_contract_project_as_capital_project(project, [&](auto relation) {
		auto contract = state.world.construction_contract_project_get_construction_contract(relation);
		if(state.world.construction_contract_get_status(contract) == uint8_t(contract_status::defaulted)
			&& works_of(state, contract) == works) result = true;
	});
	return result;
}

float equipment_in_use(sys::state const& state, dcon::factory_id works) {
	return std::min(crew(state, works) * equipment_per_worker, equipment_stock(state, works));
}

float committed_equipment(sys::state const& state, dcon::factory_id works, dcon::commodity_id equipment) {
	auto site = works_site(state, works);
	auto owner = actors::organizations::operator_actor_for_factory(state, works);
	auto account = contractor_account(state, works);
	float committed = 0.0f;
	state.world.for_each_concrete_market_bid([&](auto bid) {
		if(state.world.concrete_market_bid_get_status(bid) == uint8_t(physical::concrete_market::order_status::active)
			&& state.world.concrete_market_bid_get_monetary_account_from_concrete_bid_account(bid) == account
			&& state.world.concrete_market_bid_get_site_from_concrete_bid_destination(bid) == site
			&& state.world.concrete_market_bid_get_commodity_from_concrete_bid_commodity(bid) == equipment)
			committed += std::max(0.0f, state.world.concrete_market_bid_get_remaining_quantity(bid));
	});
	state.world.for_each_freight_request([&](auto request) {
		if(state.world.freight_request_get_status(request) == 0
			&& state.world.freight_request_get_economic_actor_from_freight_request_requester(request) == owner
			&& state.world.freight_request_get_site_from_freight_request_destination(request) == site
			&& state.world.freight_request_get_commodity_from_freight_request_commodity(request) == equipment)
			committed += std::max(0.0f, state.world.freight_request_get_quantity(request));
	});
	state.world.for_each_shipment([&](auto shipment) {
		auto owner_relation = state.world.shipment_get_shipment_owner(shipment);
		auto destination = state.world.shipment_get_shipment_destination(shipment);
		if(state.world.shipment_get_commodity(shipment) == equipment && owner_relation && destination
			&& state.world.shipment_owner_get_economic_actor(owner_relation) == owner
			&& state.world.shipment_destination_get_site(destination) == site)
			committed += std::max(0.0f, state.world.shipment_get_remaining_quantity(shipment));
	});
	return committed;
}

// The contractor buys the equipment its planned crews need, from cash it does
// not need for the next ten days of wages.
void procure_equipment(sys::state& state, dcon::factory_id works) {
	auto equipment = equipment_commodity(state);
	auto site = works_site(state, works);
	auto owner = actors::organizations::operator_actor_for_factory(state, works);
	auto account = contractor_account(state, works);
	if(!equipment || !site || !owner || !account) return;
	state.world.for_each_concrete_market_bid([&](auto bid) {
		if(state.world.concrete_market_bid_get_status(bid) == uint8_t(physical::concrete_market::order_status::active)
			&& state.world.concrete_market_bid_get_created_on(bid) < state.current_date
			&& state.world.concrete_market_bid_get_monetary_account_from_concrete_bid_account(bid) == account
			&& state.world.concrete_market_bid_get_site_from_concrete_bid_destination(bid) == site
			&& state.world.concrete_market_bid_get_commodity_from_concrete_bid_commodity(bid) == equipment) {
			state.world.concrete_market_bid_set_status(bid, uint8_t(physical::concrete_market::order_status::canceled));
			state.world.concrete_market_bid_set_reserved_amount(bid, 0.0f);
		}
	});
	auto target = decide_works(state, works).desired_units * equipment_per_worker;
	auto need = target - equipment_stock(state, works) - committed_equipment(state, works, equipment);
	if(!std::isfinite(need) || need <= 1.0e-4f) return;
	auto market = physical::concrete_market::market_for_site(state, site);
	auto price = market ? physical::concrete_market::canonical_reference_price(state, market, equipment, state.current_date) : 0.0f;
	auto limit = price * 1.20f;
	if(!std::isfinite(limit) || limit <= epsilon) return;
	auto wage_reserve = exact_person_economy::wage_due_for_factory(state, works) * 10.0f;
	auto budget = accounts::balance(state, account) - physical::concrete_market::reserved_bid_amount(state, account) - wage_reserve;
	auto quantity = std::min(need, budget / limit);
	if(quantity > 1.0e-4f)
		(void)physical::concrete_market::post_bid(state, owner, account, site, market, equipment, quantity, limit,
			physical::concrete_market::order_purpose::general);
}

void upkeep_contracts(sys::state& state) {
	std::vector<dcon::construction_contract_id> active;
	state.world.for_each_construction_contract([&](auto contract) {
		if(state.world.construction_contract_get_status(contract) == uint8_t(contract_status::active)) active.push_back(contract);
	});
	for(auto contract : active) {
		auto project = project_of(state, contract);
		if(!project || !state.world.capital_project_is_valid(project)) {
			(void)end_contract(state, contract, contract_status::cancelled);
			continue;
		}
		auto status = state.world.capital_project_get_status(project);
		if(status == uint8_t(capital_projects::status::completed)) (void)end_contract(state, contract, contract_status::completed);
		else if(status == uint8_t(capital_projects::status::cancelled)) (void)end_contract(state, contract, contract_status::cancelled);
		else if(!operating(state, works_of(state, contract))
			|| state.world.construction_contract_get_idle_days(contract) >= non_performance_days)
			// A failed or idle contractor walks away from the site. What it built
			// stays built and what it was paid it earned; the sponsor re-tenders.
			(void)end_contract(state, contract, contract_status::defaulted);
	}
}

void tender(sys::state& state) {
	std::vector<dcon::capital_project_id> open;
	state.world.for_each_capital_project([&](auto project) {
		if(capital_projects::accepts_work(state, project) && !active_contract(state, project)) open.push_back(project);
	});
	for(auto project : open) {
		if(!capital_projects::seal_required_work(state, project)) continue;
		auto works = select_contractor(state, project);
		if(works) (void)sign(state, project, works);
	}
}
} // namespace

bool is_works(sys::state const& state, dcon::factory_id factory) {
	return factory && state.world.factory_is_valid(factory) && state.world.factory_get_construction_works(factory) != 0;
}

bool is_works_site(sys::state const& state, dcon::site_id site) {
	if(!site) return false;
	bool result = false;
	state.world.site_for_each_factory_site_as_site(site, [&](auto relation) {
		if(is_works(state, state.world.factory_site_get_factory(relation))) result = true;
	});
	return result;
}

dcon::factory_id works_for(sys::state const& state, dcon::organization_id organization, dcon::province_id province) {
	dcon::factory_id result{};
	if(!organization || !province) return result;
	for(auto factory : actors::organizations::factories_operated_by(state, organization))
		if(operating(state, factory) && works_province(state, factory) == province && (!result || factory.index() < result.index()))
			result = factory;
	return result;
}

dcon::factory_id open_works(sys::state& state, dcon::organization_id organization, dcon::province_id province,
	dcon::commodity_id settlement) {
	if(!organization || !province || !settlement || !state.world.organization_is_valid(organization)
		|| !state.world.province_is_valid(province) || !state.world.commodity_is_valid(settlement)
		|| !actors::organizations::is_economic_kind(actors::ownership::actor_kind(state.world.organization_get_kind(organization)))
		|| actors::ownership::actor_kind(state.world.organization_get_kind(organization)) == actors::ownership::actor_kind::household)
		return {};
	if(auto existing = works_for(state, organization, province);
		existing && state.world.factory_get_payroll_settlement(existing) == settlement) return existing;
	auto owner = actors::organizations::actor_for_organization(state, organization);
	if(!owner || !actors::organizations::operating_account_for(state, organization, settlement)) return {};
	auto site = state.world.create_site();
	state.world.force_create_site_location(site, province);
	state.world.site_set_position(site, state.world.province_get_mid_point(province));
	auto works = state.world.create_factory();
	state.world.factory_set_construction_works(works, 1);
	state.world.factory_set_productive_capacity(works, 0.0f);
	state.world.factory_set_productivity_factor(works, 1.0f);
	state.world.factory_set_size(works, 0.0f);
	state.world.factory_set_payroll_settlement(works, settlement);
	state.world.force_create_factory_site(works, site);
	if(!actors::organizations::bind_factory_operator(state, organization, works)) {
		state.world.delete_factory(works);
		return {};
	}
	auto asset = state.world.create_asset();
	state.world.force_create_factory_asset(works, asset);
	auto stake = actors::ownership::create_stake(state, owner, asset, 1.0f, 1.0f, 1.0f);
	if(!stake) {
		state.world.delete_factory(works);
		state.world.delete_asset(asset);
		return {};
	}
	actors::ownership::assign_runtime_canonical_id(state, site);
	actors::ownership::assign_runtime_canonical_id(state, organization);
	actors::ownership::assign_runtime_canonical_id(state, owner);
	actors::ownership::assign_runtime_canonical_id(state, works);
	actors::ownership::assign_runtime_canonical_id(state, asset);
	actors::ownership::assign_runtime_canonical_id(state, stake);
	return works;
}

dcon::organization_id public_works_enterprise(sys::state& state, dcon::nation_id nation) {
	if(!nation || !state.world.nation_is_valid(nation)) return {};
	if(auto existing = state.world.nation_get_organization_from_nation_public_works(nation)) return existing;
	auto government = governance::actor_for_institution(state, governance::central_government_for(state, nation));
	if(!government) return {};
	auto organization = actors::organizations::create_organization(state, actors::ownership::actor_kind::state_entity);
	if(!organization) return {};
	auto equity = actors::organizations::equity_asset_for_organization(state, organization);
	auto stake = actors::ownership::create_stake(state, government, equity, 1.0f, 1.0f, 1.0f);
	if(!stake) return {};
	state.world.force_create_nation_public_works(nation, organization);
	actors::ownership::assign_runtime_canonical_id(state, organization);
	actors::ownership::assign_runtime_canonical_id(state, actors::organizations::actor_for_organization(state, organization));
	actors::ownership::assign_runtime_canonical_id(state, government);
	actors::ownership::assign_runtime_canonical_id(state, equity);
	actors::ownership::assign_runtime_canonical_id(state, stake);
	return organization;
}

dcon::commodity_id equipment_commodity(sys::state const& state) {
	dcon::commodity_id result{};
	state.world.for_each_commodity([&](auto commodity) {
		if(!result && state.world.commodity_get_construction_equipment(commodity)) result = commodity;
	});
	return result;
}

float equipment_stock(sys::state const& state, dcon::factory_id works) {
	auto equipment = equipment_commodity(state);
	auto owner = actors::organizations::operator_actor_for_factory(state, works);
	auto site = works_site(state, works);
	return equipment && owner && site ? std::max(0.0f, physical::inventory::quantity(state, site, equipment, owner)) : 0.0f;
}

float crew(sys::state const& state, dcon::factory_id works) {
	return is_works(state, works) ? finite_nonnegative(exact_person_economy::labor_supplied_to_factory(state, works)) : 0.0f;
}

float daily_work_capacity(sys::state const& state, dcon::factory_id works) {
	if(!operating(state, works)) return 0.0f;
	auto labor = crew(state, works);
	auto equipped = equipment_in_use(state, works) / equipment_per_worker;
	auto productivity = finite_nonnegative(state.world.factory_get_productivity_factor(works));
	if(productivity <= 0.0f) productivity = 1.0f;
	return (labor * hand_work_per_worker + equipped * equipped_work_bonus) * productivity;
}

float committed_crew(sys::state const& state, dcon::factory_id works) {
	float result = 0.0f;
	for(auto contract : contracts_of(state, works))
		result += finite_nonnegative(state.world.construction_contract_get_planned_crew(contract));
	return result;
}

float offer_wage(sys::state const& state, dcon::factory_id works) {
	auto province = works_province(state, works);
	auto market_wage = province ? state.world.province_get_labor_price(province, economy::labor::no_education) : 0.0f;
	return std::max(1.0f, finite_nonnegative(market_wage));
}

float quote(sys::state const& state, dcon::factory_id works) {
	float wages = 0.0f, labor = 0.0f;
	for(auto id : exact_person_economy::active_contracts_for_factory(state, works)) {
		auto record = exact_person_economy::contract(state, id);
		if(!record || record->pay_period_days == 0) continue;
		wages += finite_nonnegative(record->wage_rate) * finite_nonnegative(record->labor_capacity) / float(record->pay_period_days);
		labor += finite_nonnegative(record->labor_capacity);
	}
	auto wage = labor > epsilon ? wages / labor : offer_wage(state, works);
	auto work_per_worker = labor > epsilon ? daily_work_capacity(state, works) / labor : hand_work_per_worker;
	return std::max(epsilon, wage / std::max(hand_work_per_worker, work_per_worker) * (1.0f + contractor_margin));
}

float reference_crew(float material_quantity) {
	return std::max(1.0f, finite_nonnegative(material_quantity) * crew_per_material_unit);
}

float required_work(float material_quantity, float build_days) {
	auto days = std::isfinite(build_days) && build_days >= 1.0f ? build_days : default_build_days;
	return days * reference_crew(material_quantity);
}

float reference_unit_price(sys::state const& state, dcon::province_id province) {
	auto wage = province ? finite_nonnegative(state.world.province_get_labor_price(province, economy::labor::no_education)) : 0.0f;
	return std::max(1.0f, wage) / hand_work_per_worker * (1.0f + contractor_margin);
}

dcon::construction_contract_id active_contract(sys::state const& state, dcon::capital_project_id project) {
	dcon::construction_contract_id result{};
	if(!project || !state.world.capital_project_is_valid(project)) return result;
	state.world.capital_project_for_each_construction_contract_project_as_capital_project(project, [&](auto relation) {
		auto contract = state.world.construction_contract_project_get_construction_contract(relation);
		if(state.world.construction_contract_get_status(contract) == uint8_t(contract_status::active)) result = contract;
	});
	return result;
}

dcon::factory_id contractor_for(sys::state const& state, dcon::capital_project_id project) {
	auto contract = active_contract(state, project);
	return contract ? works_of(state, contract) : dcon::factory_id{};
}

dcon::factory_id select_contractor(sys::state& state, dcon::capital_project_id project) {
	auto province = project_province(state, project);
	auto account = project_account(state, project);
	auto settlement = account ? accounts::settlement_of(state, account) : dcon::commodity_id{};
	if(!province || !settlement) return {};
	auto need = reference_crew(capital_projects::material_quantity(state, project));
	dcon::factory_id best{};
	float best_quote = 0.0f, best_committed = 0.0f;
	state.world.for_each_factory([&](dcon::factory_id works) {
		if(!operating(state, works) || works_province(state, works) != province
			|| state.world.factory_get_payroll_settlement(works) != settlement
			|| !contractor_account(state, works) || defaulted_before(state, project, works)) return;
		auto committed = committed_crew(state, works);
		if(committed + need > std::max(minimum_contractor_crew, 2.0f * crew(state, works))) return;
		auto price = quote(state, works);
		if(!best || price < best_quote || (price == best_quote && committed < best_committed)) {
			best = works;
			best_quote = price;
			best_committed = committed;
		}
	});
	if(best) return best;
	// No local contractor has room: the responsible organization builds with
	// its own crew, through the same works, payroll and contract.
	auto responsible = state.world.capital_project_get_organization_from_capital_project_responsible(project);
	return open_works(state, responsible, province, settlement);
}

dcon::construction_contract_id sign(sys::state& state, dcon::capital_project_id project, dcon::factory_id works) {
	if(!capital_projects::accepts_work(state, project) || active_contract(state, project) || !operating(state, works)
		|| works_province(state, works) != project_province(state, project)
		|| state.world.factory_get_payroll_settlement(works) != accounts::settlement_of(state, project_account(state, project))
		|| !contractor_account(state, works) || !capital_projects::seal_required_work(state, project)) return {};
	auto contract = state.world.create_construction_contract();
	state.world.construction_contract_set_status(contract, uint8_t(contract_status::active));
	state.world.construction_contract_set_signed_on(contract, state.current_date);
	state.world.construction_contract_set_ended_on(contract, sys::date{});
	state.world.construction_contract_set_unit_price(contract, quote(state, works));
	state.world.construction_contract_set_planned_crew(contract, reference_crew(capital_projects::material_quantity(state, project)));
	state.world.construction_contract_set_work_performed(contract, 0.0f);
	state.world.construction_contract_set_amount_paid(contract, 0.0f);
	state.world.construction_contract_set_last_work_on(contract, sys::date{});
	state.world.construction_contract_set_idle_days(contract, 0);
	state.world.construction_contract_set_stalled_days(contract, 0);
	state.world.force_create_construction_contract_project(contract, project);
	state.world.force_create_construction_contract_works(contract, works);
	return contract;
}

bool end_contract(sys::state& state, dcon::construction_contract_id contract, contract_status status) {
	if(!contract || !state.world.construction_contract_is_valid(contract) || status == contract_status::active
		|| state.world.construction_contract_get_status(contract) != uint8_t(contract_status::active)) return false;
	// Payment follows performed work daily, so ending a contract settles
	// nothing: no advance to recover and no balance owed.
	state.world.construction_contract_set_status(contract, uint8_t(status));
	state.world.construction_contract_set_ended_on(contract, state.current_date);
	return true;
}

void end_contracts_for(sys::state& state, dcon::capital_project_id project, contract_status status) {
	if(auto contract = active_contract(state, project)) (void)end_contract(state, contract, status);
}

firm_agency::production_decision decide_works(sys::state const& state, dcon::factory_id works) {
	firm_agency::production_decision result{};
	if(!operating(state, works)) return result;
	float crews = 0.0f, revenue = 0.0f;
	for(auto contract : contracts_of(state, works)) {
		auto project = project_of(state, contract);
		if(!capital_projects::accepts_work(state, project)
			|| state.world.construction_contract_get_stalled_days(contract) >= stand_down_days) continue;
		auto planned = finite_nonnegative(state.world.construction_contract_get_planned_crew(contract));
		crews += planned;
		revenue += planned * finite_nonnegative(state.world.construction_contract_get_unit_price(contract)) * hand_work_per_worker;
	}
	result.desired_units = crews;
	result.desired_utilization = crews > epsilon ? 1.0f : 0.0f;
	result.expected_unit_revenue = crews > epsilon ? revenue / crews : 0.0f;
	result.expected_payroll_cost = exact_person_economy::wage_due_for_factory(state, works);
	result.cash_limited_units = crews;
	result.payroll_settlement = state.world.factory_get_payroll_settlement(works);
	result.procurement_settlement = result.payroll_settlement;
	return result;
}

float perform_work(sys::state& state, dcon::factory_id works) {
	if(!operating(state, works)) return 0.0f;
	auto capacity = daily_work_capacity(state, works);
	auto payee = contractor_account(state, works);
	auto productivity = finite_nonnegative(state.world.factory_get_productivity_factor(works));
	if(productivity <= 0.0f) productivity = 1.0f;
	float total = 0.0f;
	for(auto contract : contracts_of(state, works)) {
		auto project = project_of(state, contract);
		if(!capital_projects::accepts_work(state, project)) continue;
		auto price = state.world.construction_contract_get_unit_price(contract);
		auto remaining = std::max(0.0f, state.world.capital_project_get_required_work(project)
			- state.world.capital_project_get_work_done(project));
		// A site absorbs at most its planned crew working fully equipped.
		auto site_limit = finite_nonnegative(state.world.construction_contract_get_planned_crew(contract))
			* (hand_work_per_worker + equipped_work_bonus) * productivity;
		auto by_materials = capital_projects::workable(state, project);
		auto by_cash = std::isfinite(price) && price > epsilon ? capital_projects::free_cash(state, project) / price : 0.0f;
		auto sponsor_ready = by_materials > epsilon && by_cash > epsilon;
		auto work = std::min({std::max(0.0f, capacity - total), remaining, site_limit, by_materials, by_cash});
		float applied = 0.0f;
		if(payee && std::isfinite(work) && work > epsilon) applied = capital_projects::apply_work(state, project, work);
		if(applied > 0.0f) {
			auto payment = applied * price;
			[[maybe_unused]] auto paid = accounts::transfer(state, project_account(state, project), payee, payment,
				relations::transaction_kind::purchase, state.current_date);
			assert(paid && "construction work is paid from cash checked before the work was done");
			state.world.construction_contract_set_work_performed(contract, state.world.construction_contract_get_work_performed(contract) + applied);
			state.world.construction_contract_set_amount_paid(contract, state.world.construction_contract_get_amount_paid(contract) + (paid ? payment : 0.0f));
			state.world.construction_contract_set_last_work_on(contract, state.current_date);
			state.world.construction_contract_set_idle_days(contract, 0);
			state.world.construction_contract_set_stalled_days(contract, 0);
			total += applied;
			if(state.world.capital_project_get_status(project) == uint8_t(capital_projects::status::completed))
				(void)end_contract(state, contract, contract_status::completed);
		} else if(sponsor_ready) {
			// The site was ready and paid for, and the contractor did not work it.
			state.world.construction_contract_set_idle_days(contract,
				uint16_t(std::min<uint32_t>(65535, state.world.construction_contract_get_idle_days(contract) + 1u)));
		} else {
			state.world.construction_contract_set_stalled_days(contract,
				uint16_t(std::min<uint32_t>(65535, state.world.construction_contract_get_stalled_days(contract) + 1u)));
		}
	}
	// Equipment in use wears out with the work it does.
	if(total > 0.0f && capacity > epsilon) {
		auto equipment = equipment_commodity(state);
		auto worn = equipment_in_use(state, works) * equipment_daily_wear * std::min(1.0f, total / capacity);
		if(equipment && worn > 0.0f)
			(void)physical::inventory::remove(state, works_site(state, works), equipment, worn,
				actors::organizations::operator_actor_for_factory(state, works));
	}
	state.world.factory_set_output(works, total);
	state.world.factory_set_actual_utilization(works, capacity > epsilon ? std::clamp(total / capacity, 0.0f, 1.0f) : 0.0f);
	return total;
}

void process(sys::state& state) {
	upkeep_contracts(state);
	tender(state);
	std::vector<dcon::factory_id> works;
	state.world.for_each_factory([&](dcon::factory_id factory) {
		if(operating(state, factory)) works.push_back(factory);
	});
	for(auto factory : works) procure_equipment(state, factory);
	for(auto factory : works) {
		auto capacity = daily_work_capacity(state, factory);
		auto done = perform_work(state, factory);
		// Wages are due for the crew whether the day's sites were ready or not.
		payroll::settle_factory(state, factory, done, capacity);
	}
}

} // namespace economy::construction_industry
