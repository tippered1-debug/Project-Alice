#include "capital_projects.hpp"
#include "system_state.hpp"
#include "actors/organizations/organizations.hpp"
#include "actors/ownership.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/physical/inventory.hpp"
#include "economy/physical/shipments.hpp"
#include "economy/physical/deposits.hpp"
#include "economy/physical/extraction.hpp"
#include "economy/physical/land.hpp"
#include "economy/physical/concrete_market.hpp"
#include "economy/physical/factory_inputs.hpp"
#include "economy/physical/exchange.hpp"
#include "governance/finance/finance.hpp"
#include "world/site.hpp"
#include "world/spatial_runtime.hpp"
#include "governance/governance.hpp"
#include "military/military.hpp"
#include "economy/economy.hpp"
#include <algorithm>
#include <cmath>

namespace economy { struct capital_projects_store { std::vector<capital_projects::request_record> requests; }; }

namespace economy::capital_projects {
namespace {
bool valid(sys::state const& s, dcon::capital_project_id p) { return p && s.world.capital_project_is_valid(p); }
dcon::site_id site(sys::state const& s, dcon::capital_project_id p) {
	return valid(s, p) ? s.world.capital_project_get_site_from_capital_project_site(p) : dcon::site_id{};
}
dcon::economic_actor_id sponsor(sys::state const& s, dcon::capital_project_id p) {
	return valid(s, p) ? s.world.capital_project_get_economic_actor_from_capital_project_sponsor(p) : dcon::economic_actor_id{};
}
bool requirements_satisfied(sys::state const& s, dcon::capital_project_id p) {
	if(!valid(s, p)) return false;
	bool any = false, all = true;
	s.world.capital_project_for_each_capital_project_requirement_project_as_capital_project(p, [&](auto rel) {
		auto r = s.world.capital_project_requirement_project_get_capital_project_requirement(rel);
		auto required = s.world.capital_project_requirement_get_required_quantity(r);
		auto consumed = s.world.capital_project_requirement_get_consumed_quantity(r);
		any = true;
		if(!std::isfinite(required) || required <= 0.0f || !std::isfinite(consumed) || consumed != required) all = false;
	});
	return any && all;
}
void refresh(sys::state& s, dcon::capital_project_id p) {
	s.world.capital_project_set_progress(p, material_progress(s, p));
}
}

dcon::capital_project_id create(sys::state& s, project_kind kind, dcon::economic_actor_id owner,
	dcon::organization_id responsible, dcon::site_id project_site, dcon::commodity_id settlement, dcon::factory_type_id type, dcon::commodity_id target_commodity,
	float planned_daily_capacity) {
	if(!owner || !responsible || !project_site || !settlement || !s.world.economic_actor_is_valid(owner)
		|| !s.world.organization_is_valid(responsible) || !s.world.site_is_valid(project_site)
		|| !s.world.commodity_is_valid(settlement)
		|| uint8_t(kind) > uint8_t(project_kind::naval_construction)
		|| ((kind == project_kind::factory || kind == project_kind::factory_expansion) && !type)
		|| (type && !s.world.factory_type_is_valid(type))
		// An extraction recipe needs a deposit; only create_extraction_plant binds one.
		|| ((kind == project_kind::factory || kind == project_kind::extraction_plant)
			&& physical::extraction::extracts_deposit(s, type) != (kind == project_kind::extraction_plant))
		// A farm recipe needs a land title; construction does not create farms.
		|| physical::land::farms_land(s, type)
		|| (target_commodity && !s.world.commodity_is_valid(target_commodity))
		|| !std::isfinite(planned_daily_capacity) || planned_daily_capacity < 0.0f) return {};
	// A project owns a dedicated yard in the shared inventory. Two orders by
	// the same sponsor in the same province must never consume each other's stock.
	auto province = s.world.site_get_province_from_site_location(project_site);
	if(!province) return {};
	auto yard = s.world.create_site();
	s.world.force_create_site_location(yard, province);
	s.world.site_set_position(yard, s.world.site_get_position(project_site));
	actors::ownership::assign_runtime_canonical_id(s, yard);
	auto p = s.world.create_capital_project();
	s.world.capital_project_set_project_kind(p, uint8_t(kind));
	s.world.capital_project_set_status(p, uint8_t(status::planned));
	s.world.capital_project_set_created_on(p, s.current_date);
	s.world.capital_project_set_started_on(p, sys::date{});
	s.world.capital_project_set_completed_on(p, sys::date{});
	s.world.capital_project_set_progress(p, 0.0f);
	s.world.capital_project_set_state_funded(p, 0);
	s.world.capital_project_set_factory_type(p, type);
	s.world.capital_project_set_target_commodity(p, target_commodity);
	s.world.capital_project_set_planned_daily_capacity(p, planned_daily_capacity);
	s.world.force_create_capital_project_sponsor(p, owner);
	s.world.force_create_capital_project_responsible(p, responsible);
	s.world.force_create_capital_project_site(p, yard);
	auto account = accounts::open_account(s, owner, settlement);
	if(!account) return {};
	s.world.force_create_capital_project_account(p, account);
	return p;
}

dcon::capital_project_requirement_id add_requirement(sys::state& s, dcon::capital_project_id p, dcon::commodity_id c, float amount) {
	if(!valid(s, p) || !c || !s.world.commodity_is_valid(c) || !physical::factory_inputs::ordinary_physical_input(s, c) || !std::isfinite(amount) || amount <= 0.0f
		|| s.world.capital_project_get_status(p) != uint8_t(status::planned)) return {};
	dcon::capital_project_requirement_id existing{};
	s.world.capital_project_for_each_capital_project_requirement_project_as_capital_project(p, [&](auto rel) {
		auto row = s.world.capital_project_requirement_project_get_capital_project_requirement(rel);
		if(s.world.capital_project_requirement_get_commodity_from_capital_project_requirement_commodity(row) == c) existing = row;
	});
	if(existing) {
		auto total = s.world.capital_project_requirement_get_required_quantity(existing) + amount;
		if(!std::isfinite(total)) return {};
		s.world.capital_project_requirement_set_required_quantity(existing, total);
		return existing;
	}
	auto r = s.world.create_capital_project_requirement();
	s.world.capital_project_requirement_set_required_quantity(r, amount);
	s.world.capital_project_requirement_set_consumed_quantity(r, 0.0f);
	s.world.force_create_capital_project_requirement_project(r, p);
	s.world.force_create_capital_project_requirement_commodity(r, c);
	return r;
}

dcon::transaction_id fund(sys::state& s, dcon::capital_project_id p, dcon::monetary_account_id source, float amount) {
	if(!valid(s, p) || s.world.capital_project_get_status(p) >= uint8_t(status::completed) || !source || !s.world.monetary_account_is_valid(source) || !std::isfinite(amount) || amount <= 0.0f || sponsor(s, p) != accounts::owner_of(s, source)) return {};
	auto account = s.world.capital_project_get_monetary_account_from_capital_project_account(p);
	if(accounts::settlement_of(s, source) != accounts::settlement_of(s, account)
		|| amount > accounts::balance(s, source) - physical::concrete_market::reserved_bid_amount(s, source)) return {};
	auto tx = accounts::transfer(s, source, account, amount, relations::transaction_kind::transfer, s.current_date);
	if(tx && s.world.capital_project_get_status(p) == uint8_t(status::planned)) {
		s.world.capital_project_set_status(p, uint8_t(status::funded));
		s.world.capital_project_set_started_on(p, s.current_date);
	}
	return tx;
}

dcon::fiscal_action_id fund_state(sys::state& s, dcon::capital_project_id p, dcon::person_id initiator,
	dcon::monetary_account_id treasury, float amount) {
	if(!valid(s, p) || s.world.capital_project_get_status(p) >= uint8_t(status::completed) || !treasury) return {};
	if(sponsor(s, p) != accounts::owner_of(s, treasury)
		|| !std::isfinite(amount) || amount <= 0.0f
		|| amount > accounts::balance(s, treasury) - physical::concrete_market::reserved_bid_amount(s, treasury)) return {};
	auto account = s.world.capital_project_get_monetary_account_from_capital_project_account(p);
	auto action = governance::finance::authorized_spend(s, initiator, treasury, account, amount, s.current_date);
	if(action) {
		s.world.capital_project_set_state_funded(p, 1);
		s.world.capital_project_set_status(p, uint8_t(status::funded));
		s.world.capital_project_set_started_on(p, s.current_date);
	}
	return action;
}

dcon::shipment_id deliver_material(sys::state& s, dcon::capital_project_id p, dcon::monetary_account_id seller_account,
	dcon::site_id seller_site, dcon::commodity_id c, float amount, float price) {
	if(!valid(s, p) || !seller_account || !seller_site || !c || !std::isfinite(amount) || amount <= 0.0f
		|| !std::isfinite(price) || price < 0.0f) return {};
	if(s.world.capital_project_get_status(p) == uint8_t(status::suspended)
		|| s.world.capital_project_get_status(p) >= uint8_t(status::completed)) return {};
	auto seller = accounts::owner_of(s, seller_account);
	auto project_account = s.world.capital_project_get_monetary_account_from_capital_project_account(p);
	if(!seller || !project_account || accounts::settlement_of(s, seller_account) != accounts::settlement_of(s, project_account)
		|| (price > 0.0f && accounts::balance(s, project_account) - physical::concrete_market::reserved_bid_amount(s, project_account) < price)) return {};
	if(physical::inventory::quantity(s, seller_site, c, seller) < amount) return {};
	// Dispatch first: it validates and commits the physical leg, and its owner is
	// the project sponsor. Cash is transferred only after that leg is guaranteed.
	auto shipment = physical::shipments::dispatch_transfer(s, seller_site, site(s, p), c, amount, seller, sponsor(s, p));
	if(!shipment) return {};
	if(price > 0.0f && !accounts::transfer(s, project_account, seller_account, price, relations::transaction_kind::purchase, s.current_date)) {
		s.world.delete_shipment(shipment);
		physical::inventory::add(s, seller_site, c, amount, seller);
		return {};
	}
	return shipment;
}

float consume(sys::state& s, dcon::capital_project_requirement_id r, float amount) {
	if(!r || !s.world.capital_project_requirement_is_valid(r) || !std::isfinite(amount) || amount <= 0.0f) return 0.0f;
	auto p = s.world.capital_project_requirement_get_capital_project_from_capital_project_requirement_project(r);
	if(!valid(s, p) || s.world.capital_project_get_status(p) == uint8_t(status::suspended)
		|| s.world.capital_project_get_status(p) >= uint8_t(status::completed)) return 0.0f;
	auto c = s.world.capital_project_requirement_get_commodity_from_capital_project_requirement_commodity(r);
	auto owner = sponsor(s, p);
	auto required = s.world.capital_project_requirement_get_required_quantity(r);
	auto used = s.world.capital_project_requirement_get_consumed_quantity(r);
	if(!std::isfinite(required) || required <= 0.0f || !std::isfinite(used) || used < 0.0f || used > required) return 0.0f;
	auto need = required - used;
	auto taken = physical::inventory::remove(s, site(s, p), c, std::min({amount, need, physical::inventory::quantity(s, site(s, p), c, owner)}), owner);
	s.world.capital_project_requirement_set_consumed_quantity(r, s.world.capital_project_requirement_get_consumed_quantity(r) + taken);
	if(taken > 0.0f) {
		s.world.capital_project_set_status(p, uint8_t(status::active));
		if(!s.world.capital_project_get_started_on(p)) s.world.capital_project_set_started_on(p, s.current_date);
	}
	refresh(s, p);
	if(requirements_satisfied(s, p)) complete(s, p);
	return taken;
}

float material_progress(sys::state const& s, dcon::capital_project_id p) {
	if(!valid(s, p)) return 0.0f;
	double required = 0.0, consumed = 0.0;
	bool malformed = false, all = true;
	s.world.capital_project_for_each_capital_project_requirement_project_as_capital_project(p, [&](auto rel) {
		auto r = s.world.capital_project_requirement_project_get_capital_project_requirement(rel);
		auto need = s.world.capital_project_requirement_get_required_quantity(r);
		auto used = s.world.capital_project_requirement_get_consumed_quantity(r);
		if(!std::isfinite(need) || need <= 0.0f || !std::isfinite(used) || used < 0.0f || used > need) { malformed = true; return; }
		required += need;
		consumed += used;
		if(used != need) all = false;
	});
	// Progress is a projection, never permission to create an asset. An empty
	// recipe cannot be completed for free, even if a save/UI says progress=1.
	if(malformed || required <= 0.0) return 0.0f;
	return all ? 1.0f : std::min(float(consumed / required), std::nextafter(1.0f, 0.0f));
}
namespace {
void release_bids(sys::state& s, dcon::capital_project_id p) {
	auto account = s.world.capital_project_get_monetary_account_from_capital_project_account(p);
	s.world.for_each_concrete_market_bid([&](auto bid) {
		if(s.world.concrete_market_bid_get_monetary_account_from_concrete_bid_account(bid) == account
			&& s.world.concrete_market_bid_get_status(bid) == 0) {
			s.world.concrete_market_bid_set_status(bid, uint8_t(physical::concrete_market::order_status::canceled));
			s.world.concrete_market_bid_set_reserved_amount(bid, 0.0f);
		}
	});
}
}

bool suspend(sys::state& s, dcon::capital_project_id p) { if(!valid(s,p) || s.world.capital_project_get_status(p) >= uint8_t(status::completed)) return false; release_bids(s, p); s.world.capital_project_set_status(p,uint8_t(status::suspended)); return true; }
bool resume(sys::state& s, dcon::capital_project_id p) {
	if(!valid(s, p) || s.world.capital_project_get_status(p) != uint8_t(status::suspended)) return false;
	s.world.capital_project_set_status(p, uint8_t(status::active));
	return true;
}
bool is_construction_site(sys::state const& s, dcon::site_id yard) {
	bool result = false;
	s.world.for_each_capital_project([&](auto p) {
		if(s.world.capital_project_get_status(p) < uint8_t(status::completed) && site(s, p) == yard) result = true;
	});
	return result;
}
bool cancel(sys::state& s, dcon::capital_project_id p) { if(!valid(s,p) || s.world.capital_project_get_status(p) == uint8_t(status::completed)) return false; release_bids(s, p); s.world.capital_project_set_status(p,uint8_t(status::cancelled)); return true; }

bool complete(sys::state& s, dcon::capital_project_id p) {
	if(!valid(s,p) || !requirements_satisfied(s,p) || s.world.capital_project_get_status(p) == uint8_t(status::suspended)
		|| s.world.capital_project_get_status(p) >= uint8_t(status::completed)) return false;
	if(s.world.capital_project_get_factory_from_capital_project_factory(p)
		|| s.world.capital_project_get_asset_from_capital_project_asset(p)) return false;
	auto project_site = site(s, p);
	auto responsible = s.world.capital_project_get_organization_from_capital_project_responsible(p);
	if(!project_site || !responsible || !actors::organizations::is_economic_kind(
		actors::ownership::actor_kind(s.world.organization_get_kind(responsible)))) return false;
	if(s.world.capital_project_get_project_kind(p) == uint8_t(project_kind::factory)) {
		if(!s.world.capital_project_get_factory_type(p)
			|| !s.world.factory_type_is_valid(s.world.capital_project_get_factory_type(p))) return false;
		auto province = s.world.site_get_province_from_site_location(project_site);
		if(!province) return false;
		auto f = s.world.create_factory();
		s.world.factory_set_building_type(f, s.world.capital_project_get_factory_type(p));
		auto planned_capacity = std::max(0.0f, s.world.capital_project_get_planned_daily_capacity(p));
		if(planned_capacity <= 0.0f) planned_capacity = 1.0f;
		s.world.factory_set_productive_capacity(f, planned_capacity);
		s.world.factory_set_size(f, planned_capacity * float(std::max<int32_t>(1,
			s.world.factory_type_get_base_workforce(s.world.capital_project_get_factory_type(p)))));
		s.world.factory_set_productivity_factor(f, 1.0f);
		s.world.factory_set_target_utilization(f, 1.0f);
		s.world.factory_set_actual_utilization(f, 0.0f);
		s.world.factory_set_payroll_settlement(f,
			s.world.monetary_account_get_commodity_from_monetary_account_settlement(
				s.world.capital_project_get_monetary_account_from_capital_project_account(p)));
		auto output = s.world.factory_type_get_output(s.world.capital_project_get_factory_type(p));
		assert(output && "new factories require a physical output commodity");
		s.world.force_create_factory_location(f, province);
		s.world.force_create_factory_site(f, project_site);
		actors::ownership::assign_runtime_canonical_id(s, project_site);
		actors::ownership::assign_runtime_canonical_id(s, responsible);
		actors::ownership::assign_runtime_canonical_id(s, sponsor(s, p));
		actors::ownership::assign_runtime_canonical_id(s, f);
		auto asset = s.world.create_asset();
		actors::ownership::assign_runtime_canonical_id(s, asset);
		s.world.force_create_factory_asset(f, asset);
		auto stake = actors::ownership::create_stake(s, sponsor(s,p), asset, 1.0f, 1.0f, 1.0f);
		actors::ownership::assign_runtime_canonical_id(s, stake);
		if(!stake || !actors::organizations::bind_factory_operator(s, responsible, f)) {
			if(stake) s.world.delete_ownership_stake(stake);
			s.world.delete_factory(f);
			s.world.delete_asset(asset);
			return false;
		}
		s.world.force_create_capital_project_factory(p, f);
		s.world.force_create_capital_project_asset(p, asset);
	} else if(s.world.capital_project_get_project_kind(p) == uint8_t(project_kind::extraction_plant)) {
		// The plant is capital built on an existing deposit; the deposit itself
		// is never created here. Access is checked again at completion because
		// operator control or a right may have lapsed while the project was built.
		auto deposit = s.world.capital_project_get_resource_deposit_from_capital_project_target_deposit(p);
		auto type = s.world.capital_project_get_factory_type(p);
		if(!deposit || physical::extraction::enterprise_for_deposit(s, deposit)
			|| !physical::extraction::may_operate(s, deposit,
				actors::organizations::actor_for_organization(s, responsible), s.current_date)) return false;
		auto f = physical::extraction::create_enterprise(s, deposit, type, responsible);
		if(!f) return false;
		s.world.factory_set_target_utilization(f, 1.0f);
		s.world.factory_set_actual_utilization(f, 0.0f);
		s.world.factory_set_payroll_settlement(f,
			s.world.monetary_account_get_commodity_from_monetary_account_settlement(
				s.world.capital_project_get_monetary_account_from_capital_project_account(p)));
		actors::ownership::assign_runtime_canonical_id(s, s.world.factory_get_site_from_factory_site(f));
		actors::ownership::assign_runtime_canonical_id(s, responsible);
		actors::ownership::assign_runtime_canonical_id(s, sponsor(s, p));
		actors::ownership::assign_runtime_canonical_id(s, f);
		auto asset = s.world.create_asset();
		actors::ownership::assign_runtime_canonical_id(s, asset);
		s.world.force_create_factory_asset(f, asset);
		auto stake = actors::ownership::create_stake(s, sponsor(s, p), asset, 1.0f, 1.0f, 1.0f);
		actors::ownership::assign_runtime_canonical_id(s, stake);
		if(!stake) {
			s.world.delete_factory(f);
			s.world.delete_asset(asset);
			return false;
		}
		s.world.force_create_capital_project_factory(p, f);
		s.world.force_create_capital_project_asset(p, asset);
	} else if(s.world.capital_project_get_project_kind(p) == uint8_t(project_kind::factory_expansion)) {
		auto factory = s.world.capital_project_get_factory_from_capital_project_target_factory(p);
		if(!factory || !s.world.factory_is_valid(factory)
			) return false;
		auto added_capacity = std::max(0.0f, s.world.capital_project_get_planned_daily_capacity(p));
		s.world.factory_set_productive_capacity(factory,
			s.world.factory_get_productive_capacity(factory) + added_capacity);
		s.world.factory_set_size(factory,
			s.world.factory_get_size(factory) + added_capacity
				* float(std::max<int32_t>(1, s.world.factory_type_get_base_workforce(s.world.factory_get_building_type(factory)))));
		s.world.force_create_capital_project_factory(p, factory);
	} else if(s.world.capital_project_get_project_kind(p) == uint8_t(project_kind::military_production)) {
		auto output = s.world.capital_project_get_target_commodity(p);
		auto quantity = s.world.capital_project_get_planned_daily_capacity(p);
		if(!physical::factory_inputs::ordinary_physical_input(s, output) || !std::isfinite(quantity) || quantity <= 0.0f) return false;
		if(physical::inventory::add(s, project_site, output, quantity, sponsor(s, p)) < quantity) return false;
	} else if(s.world.capital_project_get_project_kind(p) == uint8_t(project_kind::naval_construction)) {
		if(!s.capital_projects_runtime) return false;
		auto it = std::find_if(s.capital_projects_runtime->requests.begin(), s.capital_projects_runtime->requests.end(), [&](auto const& r) { return r.project == p; });
		if(it == s.capital_projects_runtime->requests.end() || !it->nation || !s.world.nation_is_valid(it->nation)
			|| !it->unit || it->unit.index() >= s.military_definitions.unit_base_definitions.size()
			|| s.military_definitions.unit_base_definitions[it->unit].is_land || it->ship) return false;
		auto province = s.world.site_get_province_from_site_location(project_site);
		auto navy = s.world.create_navy();
		s.world.navy_set_controller_from_navy_control(navy, it->nation);
		s.world.navy_set_location_from_navy_location(navy, province);
		it->ship = military::create_new_ship(s, it->nation, it->unit);
		s.world.ship_set_navy_from_navy_membership(it->ship, navy);
	} else if(s.world.capital_project_get_project_kind(p) == uint8_t(project_kind::infrastructure)) {
		auto node = world::spatial_runtime::node_for_site(s, project_site);
		if(!node) node = s.world.create_infrastructure_node();
		auto province = s.world.site_get_province_from_site_location(site(s,p));
		if(!province) {
			s.world.delete_infrastructure_node(node);
			return false;
		}
		s.world.infrastructure_node_set_position(node, s.world.province_get_mid_point(province));
		s.world.force_create_infrastructure_node_location(node, province);
		s.world.force_create_capital_project_node(p, node);
		if(s.capital_projects_runtime) for(auto const& r : s.capital_projects_runtime->requests) {
			if(r.project != p || r.building_type >= uint8_t(economy::max_building_types)) continue;
			s.world.province_set_building_level(province, r.building_type,
				std::max(s.world.province_get_building_level(province, r.building_type), r.target_level));
			if(r.building_type == uint8_t(economy::province_building_type::railroad)) s.world.for_each_infrastructure_edge([&](auto edge) {
				auto from = s.world.infrastructure_edge_get_node_from_infrastructure_edge_from(edge);
				auto to = s.world.infrastructure_edge_get_node_from_infrastructure_edge_to(edge);
				auto a = s.world.infrastructure_node_get_province_from_infrastructure_node_location(from);
				auto b = s.world.infrastructure_node_get_province_from_infrastructure_node_location(to);
				if(a == province || b == province) s.world.infrastructure_edge_set_type(edge,
					std::min(s.world.province_get_building_level(a, r.building_type), s.world.province_get_building_level(b, r.building_type)));
			});
		}
	}
	else return false;
	release_bids(s, p);
	s.world.capital_project_set_status(p,uint8_t(status::completed));
	s.world.capital_project_set_completed_on(p,s.current_date);
	s.world.capital_project_set_progress(p,1.0f);
	return true;
}

dcon::capital_project_id create_factory_expansion(sys::state& s, dcon::factory_id factory,
	float added_capacity, dcon::commodity_id settlement) {
	if(!factory || !s.world.factory_is_valid(factory)
		|| !std::isfinite(added_capacity) || added_capacity <= 0.0f) return {};
	auto owner = actors::organizations::operator_actor_for_factory(s, factory);
	auto responsible = actors::organizations::operator_organization_for_factory(s, factory);
	auto project_site = world::site::site_for_factory(s, factory);
	auto type = s.world.factory_get_building_type(factory);
	if(!settlement) settlement = s.world.factory_get_payroll_settlement(factory);
	if(!settlement) settlement = physical::exchange::settlement_for_purchase(s, owner);
	if(!owner || !responsible || !project_site || !type || !settlement) return {};
	auto project = create(s, project_kind::factory_expansion, owner, responsible, project_site,
		settlement, type, {}, added_capacity);
	if(project) s.world.force_create_capital_project_target_factory(project, factory);
	return project;
}

dcon::capital_project_id create_extraction_plant(sys::state& s, dcon::economic_actor_id sponsor_actor,
	dcon::organization_id operator_company, dcon::resource_deposit_id deposit, dcon::factory_type_id type,
	dcon::commodity_id settlement) {
	if(!deposit || !s.world.resource_deposit_is_valid(deposit) || !operator_company
		|| !physical::extraction::extracts_deposit(s, type)
		|| s.world.factory_type_get_output(type) != s.world.resource_deposit_get_commodity(deposit)
		|| physical::extraction::enterprise_for_deposit(s, deposit)
		|| !physical::extraction::may_operate(s, deposit,
			actors::organizations::actor_for_organization(s, operator_company), s.current_date)) return {};
	auto deposit_site = s.world.resource_deposit_get_site_from_resource_deposit_site(deposit);
	auto project = create(s, project_kind::extraction_plant, sponsor_actor, operator_company, deposit_site,
		settlement, type, s.world.resource_deposit_get_commodity(deposit));
	if(!project) return {};
	s.world.force_create_capital_project_target_deposit(project, deposit);
	// Materials scale with the plant's capacity exactly as for a greenfield
	// factory. A recipe without a construction bill cannot be built.
	auto per_unit = s.world.factory_type_get_output_amount(type) * s.world.resource_deposit_get_grade_or_quality(deposit);
	auto units = per_unit > 0.0f ? s.world.resource_deposit_get_daily_extraction_capacity(deposit) / per_unit : 0.0f;
	auto const& construction = s.world.factory_type_get_construction_costs(type);
	bool added_any = false;
	for(uint32_t i = 0; i < economy::commodity_set::set_size && std::isfinite(units) && units > 0.0f; ++i) {
		auto commodity = construction.commodity_type[i];
		if(!commodity) break;
		if(!physical::factory_inputs::ordinary_physical_input(s, commodity)) continue;
		auto amount = std::max(0.0f, construction.commodity_amounts[i]) * units * 0.5f;
		if(amount > 1.0e-5f) {
			if(!add_requirement(s, project, commodity, amount)) {
				(void)cancel(s, project);
				return {};
			}
			added_any = true;
		}
	}
	if(!added_any) { cancel(s, project); return {}; }
	return project;
}

dcon::capital_project_id create_greenfield_factory(sys::state& s, dcon::economic_actor_id sponsor_actor,
	dcon::organization_id operator_company, dcon::site_id project_site, dcon::factory_type_id type,
	dcon::commodity_id settlement, float planned_capacity) {
	if(!type || !s.world.factory_type_is_valid(type) || !std::isfinite(planned_capacity)
		|| planned_capacity <= 0.0f) return {};
	auto project = create(s, project_kind::factory, sponsor_actor, operator_company, project_site,
		settlement, type, {}, planned_capacity);
	if(!project) return {};
	auto const& construction = s.world.factory_type_get_construction_costs(type);
	bool added_any = false;
	for(uint32_t i = 0; i < economy::commodity_set::set_size; ++i) {
		auto commodity = construction.commodity_type[i];
		if(!commodity) break;
		if(!physical::factory_inputs::ordinary_physical_input(s, commodity)) continue;
		auto amount = std::max(0.0f, construction.commodity_amounts[i]) * planned_capacity * 0.5f;
		if(amount > 1.0e-5f) {
			if(!add_requirement(s, project, commodity, amount)) {
				(void)cancel(s, project);
				return {};
			}
			added_any = true;
		}
	}
	if(!added_any) { cancel(s, project); return {}; }
	return project;
}


namespace {
std::vector<request_record>& requests(sys::state& s) {
	if(!s.capital_projects_runtime) s.capital_projects_runtime = std::make_shared<capital_projects_store>();
	return s.capital_projects_runtime->requests;
}
bool valid_recipe(sys::state const& s, economy::commodity_set const& recipe) {
	bool any = false;
	for(uint32_t i = 0; i < economy::commodity_set::set_size; ++i) {
		auto c = recipe.commodity_type[i];
		if(!c) break;
		if(!physical::factory_inputs::ordinary_physical_input(s, c) || !std::isfinite(recipe.commodity_amounts[i]) || recipe.commodity_amounts[i] <= 0.0f) return false;
		any = true;
	}
	return any;
}
bool add_recipe(sys::state& s, dcon::capital_project_id p, economy::commodity_set const& recipe, float scale = 1.0f) {
	bool any = false;
	for(uint32_t i = 0; i < economy::commodity_set::set_size; ++i) {
		auto c = recipe.commodity_type[i];
		if(!c) break;
		if(!physical::factory_inputs::ordinary_physical_input(s, c) || !std::isfinite(recipe.commodity_amounts[i]) || recipe.commodity_amounts[i] <= 0.0f) return false;
		if(!add_requirement(s, p, c, recipe.commodity_amounts[i] * scale)) return false;
		any = true;
	}
	return any;
}
dcon::capital_project_id public_request(sys::state& s, dcon::nation_id nation, dcon::province_id province, project_kind kind, dcon::factory_type_id type = {}) {
	if(!nation || !province) return {};
	auto authority = governance::central_government_for(s, nation);
	auto owner = governance::actor_for_institution(s, authority);
	auto settlement = physical::exchange::settlement_for_purchase(s, owner);
	if(!settlement) return {};
	auto location = s.world.create_site();
	s.world.force_create_site_location(location, province);
	auto contractor = actors::organizations::create_company(s);
	auto p = create(s, kind, owner, contractor, location, settlement, type);
	if(p) s.world.force_create_institution_treasury_account(
		s.world.capital_project_get_monetary_account_from_capital_project_account(p), authority);
	return p;
}
void fund_public_request(sys::state& s, request_record const& r) {
	if(!valid(s, r.project) || s.world.capital_project_get_status(r.project) == uint8_t(status::suspended)
		|| s.world.capital_project_get_status(r.project) >= uint8_t(status::completed)) return;
	auto account = s.world.capital_project_get_monetary_account_from_capital_project_account(r.project);
	auto authority = governance::finance::treasury_institution_for(s, account);
	if(!authority) return;
	auto treasury = governance::finance::treasury_account_for(s, authority, accounts::settlement_of(s, account));
	if(!treasury || treasury == account) return;
	auto yard = site(s, r.project);
	auto market = physical::concrete_market::market_for_site(s, yard);
	float needed = 0.0f;
	s.world.capital_project_for_each_capital_project_requirement_project_as_capital_project(r.project, [&](auto rel) {
		auto req = s.world.capital_project_requirement_project_get_capital_project_requirement(rel);
		auto c = s.world.capital_project_requirement_get_commodity_from_capital_project_requirement_commodity(req);
		auto remaining = std::max(0.0f, s.world.capital_project_requirement_get_required_quantity(req) - s.world.capital_project_requirement_get_consumed_quantity(req)
			- physical::inventory::quantity(s, yard, c, sponsor(s, r.project)));
		s.world.for_each_freight_request([&](auto request) {
			if(s.world.freight_request_get_status(request) == 0 && s.world.freight_request_get_economic_actor_from_freight_request_requester(request) == sponsor(s, r.project)
				&& s.world.freight_request_get_site_from_freight_request_destination(request) == yard && s.world.freight_request_get_commodity_from_freight_request_commodity(request) == c)
				remaining = std::max(0.0f, remaining - std::max(0.0f, s.world.freight_request_get_quantity(request)));
		});
		s.world.for_each_shipment([&](auto shipment) {
			if(s.world.shipment_get_site_from_shipment_destination(shipment) == yard && s.world.shipment_get_commodity(shipment) == c)
				remaining = std::max(0.0f, remaining - std::max(0.0f, s.world.shipment_get_remaining_quantity(shipment)));
		});
		needed += remaining * physical::concrete_market::canonical_reference_price(s, market, c, s.current_date) * 1.20f;
	});
	auto amount = std::min(std::max(0.0f, needed - accounts::balance(s, account)),
		std::max(0.0f, accounts::balance(s, treasury) - physical::concrete_market::reserved_bid_amount(s, treasury)) * float(s.world.nation_get_construction_spending(r.nation)) / 100.0f);
	if(amount > 0.0f && governance::finance::authorized_spend_by_institution(s, authority, treasury, account, amount, s.current_date)) {
		s.world.capital_project_set_state_funded(r.project, 1);
		if(s.world.capital_project_get_status(r.project) == uint8_t(status::planned)) {
			s.world.capital_project_set_status(r.project, uint8_t(status::funded));
			s.world.capital_project_set_started_on(r.project, s.current_date);
		}
	}
}
}
std::vector<request_record> export_requests(sys::state const& s) { return s.capital_projects_runtime ? s.capital_projects_runtime->requests : std::vector<request_record>{}; }
void clear_requests(sys::state& s) { s.capital_projects_runtime.reset(); }
void isolate_pre_cut_projects(sys::state& s) {
	s.world.for_each_capital_project([&](auto p) {
		if(s.world.capital_project_get_status(p) >= uint8_t(status::completed)) return;
		auto old = site(s, p);
		if(!old) return;
		auto yard = s.world.create_site();
		s.world.force_create_site_location(yard, s.world.site_get_province_from_site_location(old));
		s.world.site_set_position(yard, s.world.site_get_position(old));
		actors::ownership::assign_runtime_canonical_id(s, yard);
		auto relation = s.world.capital_project_get_capital_project_site(p);
		if(relation) s.world.delete_capital_project_site(relation);
		s.world.force_create_capital_project_site(p, yard);
		release_bids(s, p);
		refresh(s, p);
	});
}
bool import_requests(sys::state& s, std::vector<request_record> const& rows) {
	std::vector<dcon::capital_project_id> seen;
	for(auto const& r : rows) {
		if(!valid(s, r.project) || std::find(seen.begin(), seen.end(), r.project) != seen.end()
			|| (r.building_type != 255 && r.building_type >= uint8_t(economy::max_building_types))
			|| (r.building && !s.world.province_building_construction_is_valid(r.building))
			|| (r.naval && !s.world.province_naval_construction_is_valid(r.naval))
			|| (r.factory && !s.world.factory_construction_is_valid(r.factory))
			|| (r.ship && !s.world.ship_is_valid(r.ship))
			|| (r.nation && !s.world.nation_is_valid(r.nation))
			|| (r.unit && r.unit.index() >= s.military_definitions.unit_base_definitions.size())) return false;
		seen.push_back(r.project);
	}
	requests(s) = rows;
	return true;
}
dcon::capital_project_id project_for(sys::state const& s, dcon::province_building_construction_id order) {
	if(s.capital_projects_runtime) for(auto const& r : s.capital_projects_runtime->requests) if(order && r.building == order) return r.project;
	return {};
}
dcon::capital_project_id project_for(sys::state const& s, dcon::province_naval_construction_id order) {
	if(s.capital_projects_runtime) for(auto const& r : s.capital_projects_runtime->requests) if(order && r.naval == order) return r.project;
	return {};
}
dcon::capital_project_id project_for(sys::state const& s, dcon::factory_construction_id order) {
	if(s.capital_projects_runtime) for(auto const& r : s.capital_projects_runtime->requests) if(order && r.factory == order) return r.project;
	return {};
}
economy::commodity_set consumed_materials(sys::state const& s, dcon::capital_project_id p) {
	economy::commodity_set result{};
	uint32_t i = 0;
	if(valid(s, p)) s.world.capital_project_for_each_capital_project_requirement_project_as_capital_project(p, [&](auto rel) {
		auto req = s.world.capital_project_requirement_project_get_capital_project_requirement(rel);
		auto c = s.world.capital_project_requirement_get_commodity_from_capital_project_requirement_commodity(req);
		for(uint32_t j = 0; j < i; ++j) if(result.commodity_type[j] == c) { result.commodity_amounts[j] += s.world.capital_project_requirement_get_consumed_quantity(req); return; }
		if(i < economy::commodity_set::set_size) { result.commodity_type[i] = c; result.commodity_amounts[i++] = s.world.capital_project_requirement_get_consumed_quantity(req); }
	});
	return result;
}
dcon::capital_project_id create_infrastructure(sys::state& s, dcon::economic_actor_id owner, dcon::organization_id company,
	dcon::site_id location, dcon::commodity_id settlement, uint8_t building_type) {
	if(building_type >= uint8_t(economy::max_building_types) || !valid_recipe(s, s.economy_definitions.building_definitions[building_type].cost)) return {};
	auto p = create(s, project_kind::infrastructure, owner, company, location, settlement);
	if(!p) return {};
	if(!add_recipe(s, p, s.economy_definitions.building_definitions[building_type].cost)) { cancel(s, p); return {}; }
	request_record r; r.project = p; r.building_type = building_type;
	r.target_level = uint8_t(std::min(255, int(s.world.province_get_building_level(s.world.site_get_province_from_site_location(location), building_type)) + 1));
	requests(s).push_back(r);
	return p;
}
dcon::capital_project_id create_military_production(sys::state& s, dcon::economic_actor_id owner, dcon::organization_id company,
	dcon::site_id location, dcon::commodity_id settlement, dcon::commodity_id output, float quantity, economy::commodity_set const& recipe) {
	if(!valid_recipe(s, recipe) || !physical::factory_inputs::ordinary_physical_input(s, output) || !std::isfinite(quantity) || quantity <= 0.0f) return {};
	auto p = create(s, project_kind::military_production, owner, company, location, settlement, {}, output, quantity);
	if(p && !add_recipe(s, p, recipe)) { cancel(s, p); return {}; }
	return p;
}
void adopt_legacy_requests(sys::state& s) {
	// Old saves preserve the request, never the synthetic purchased_goods.
	s.world.for_each_province_building_construction([&](auto order) {
		auto p = project_for(s, order);
		if(!p) {
			auto building_type = s.world.province_building_construction_get_type(order);
			if(building_type >= uint8_t(economy::max_building_types) || s.world.province_building_construction_get_is_pop_project(order)
				|| !valid_recipe(s, s.economy_definitions.building_definitions[building_type].cost)) return;
			auto nation = s.world.province_building_construction_get_nation(order);
			auto province = s.world.province_building_construction_get_province(order);
			p = public_request(s, nation, province, project_kind::infrastructure);
			if(!p) return;
			if(!add_recipe(s, p, s.economy_definitions.building_definitions[building_type].cost)) { cancel(s, p); return; }
			request_record r; r.project = p; r.building = order; r.nation = nation; r.building_type = building_type;
			r.target_level = uint8_t(std::min(255, int(s.world.province_get_building_level(province, building_type)) + 1));
			requests(s).push_back(r);
		}
	});
	s.world.for_each_province_naval_construction([&](auto order) {
		if(project_for(s, order)) return;
		auto nation = s.world.province_naval_construction_get_nation(order);
		auto province = s.world.province_naval_construction_get_province(order);
		auto unit = s.world.province_naval_construction_get_type(order);
		if(!unit || unit.index() >= s.military_definitions.unit_base_definitions.size() || s.military_definitions.unit_base_definitions[unit].is_land) return;
		if(!valid_recipe(s, s.military_definitions.unit_base_definitions[unit].build_cost)) return;
		auto p = public_request(s, nation, province, project_kind::naval_construction);
		if(!p) return;
		if(!add_recipe(s, p, s.military_definitions.unit_base_definitions[unit].build_cost)) { cancel(s, p); return; }
		request_record r; r.project = p; r.naval = order; r.nation = nation; r.unit = unit;
		requests(s).push_back(r);
	});
	s.world.for_each_factory_construction([&](auto order) {
		if(project_for(s, order)) return;
		auto province = s.world.factory_construction_get_province(order);
		auto type = s.world.factory_construction_get_type(order);
		if(!type || s.world.factory_construction_get_refit_target(order) || !valid_recipe(s, s.world.factory_type_get_construction_costs(type))) return;
		dcon::capital_project_id p{};
		auto nation = s.world.factory_construction_get_nation(order);
		if(s.world.factory_construction_get_is_upgrade(order)) {
			dcon::factory_id target{};
			for(auto f : s.world.province_get_factory_location(province)) if(f.get_factory().get_building_type() == type) { target = f.get_factory(); break; }
			if(!target) return;
			p = create_factory_expansion(s, target, 1.0f, {});
		} else if(!s.world.factory_construction_get_is_pop_project(order)) {
			p = public_request(s, nation, province, project_kind::factory, type);
		}
		if(!p) return;
		if(!add_recipe(s, p, s.world.factory_type_get_construction_costs(type))) { cancel(s, p); return; }
		request_record r; r.project = p; r.factory = order;
		if(s.world.capital_project_get_project_kind(p) == uint8_t(project_kind::factory)) r.nation = nation;
		else {
			auto source = accounts::find_account(s, sponsor(s, p), accounts::settlement_of(s, s.world.capital_project_get_monetary_account_from_capital_project_account(p)));
			float estimate = 0.0f;
			auto market = physical::concrete_market::market_for_site(s, site(s, p));
			auto const& recipe = s.world.factory_type_get_construction_costs(type);
			for(uint32_t i = 0; i < economy::commodity_set::set_size && recipe.commodity_type[i]; ++i)
				estimate += recipe.commodity_amounts[i] * physical::concrete_market::canonical_reference_price(s, market, recipe.commodity_type[i], s.current_date) * 1.20f;
			auto amount = source ? std::min(estimate, std::max(0.0f, accounts::balance(s, source) - physical::concrete_market::reserved_bid_amount(s, source))) : 0.0f;
			if(amount > 0.0f) fund(s, p, source, amount);
		}
		requests(s).push_back(r);
	});

	for(auto const& r : export_requests(s)) {
		if((r.building && !s.world.province_building_construction_is_valid(r.building))
			|| (r.naval && !s.world.province_naval_construction_is_valid(r.naval))
			|| (r.factory && !s.world.factory_construction_is_valid(r.factory))) {
			cancel(s, r.project);
			for(auto& row : requests(s)) if(row.project == r.project) { row.building = {}; row.naval = {}; row.factory = {}; }
			continue;
		}
		if(r.nation) fund_public_request(s, r);
	}
}
void project_legacy_requests(sys::state& s) {
	s.world.market_resize_construction_demand(s.world.commodity_size());
	s.world.market_resize_private_construction_demand(s.world.commodity_size());
	s.world.for_each_market([&](auto market) {
		s.world.for_each_commodity([&](auto c) {
			s.world.market_set_construction_demand(market, c, 0.0f);
			s.world.market_set_private_construction_demand(market, c, 0.0f);
		});
	});
	s.world.for_each_concrete_market_bid([&](auto bid) {
		if(s.world.concrete_market_bid_get_status(bid) != 0) return;
		auto account = s.world.concrete_market_bid_get_monetary_account_from_concrete_bid_account(bid);
		bool project_account = false;
		s.world.for_each_capital_project([&](auto p) { if(s.world.capital_project_get_monetary_account_from_capital_project_account(p) == account) project_account = true; });
		if(!project_account) return;
		auto market = s.world.concrete_market_bid_get_market_from_concrete_bid_market(bid);
		auto c = s.world.concrete_market_bid_get_commodity_from_concrete_bid_commodity(bid);
		s.world.market_set_construction_demand(market, c, s.world.market_get_construction_demand(market, c) + std::max(0.0f, s.world.concrete_market_bid_get_remaining_quantity(bid)));
	});
	s.world.for_each_province_land_construction([&](auto order) { s.world.province_land_construction_get_purchased_goods(order) = {}; });
	s.world.for_each_province_building_construction([&](auto order) { s.world.province_building_construction_get_purchased_goods(order) = {}; });
	s.world.for_each_province_naval_construction([&](auto order) { s.world.province_naval_construction_get_purchased_goods(order) = {}; });
	s.world.for_each_factory_construction([&](auto order) { s.world.factory_construction_get_purchased_goods(order) = {}; });
	for(auto& r : requests(s)) {
		auto p = r.project;
		if(!valid(s, p)) continue;
		refresh(s, p);
		auto materials = consumed_materials(s, p);
		auto project_array = [&](auto& target, economy::commodity_set const& recipe) {
			for(uint32_t i = 0; i < economy::commodity_set::set_size; ++i) for(uint32_t j = 0; j < economy::commodity_set::set_size; ++j)
				if(recipe.commodity_type[i] && recipe.commodity_type[i] == materials.commodity_type[j]) target.commodity_amounts[i] = materials.commodity_amounts[j];
		};
		if(r.building && s.world.province_building_construction_is_valid(r.building)) project_array(s.world.province_building_construction_get_purchased_goods(r.building), s.economy_definitions.building_definitions[r.building_type].cost);
		if(r.naval && s.world.province_naval_construction_is_valid(r.naval)) project_array(s.world.province_naval_construction_get_purchased_goods(r.naval), s.military_definitions.unit_base_definitions[r.unit].build_cost);
		if(r.factory && s.world.factory_construction_is_valid(r.factory)) project_array(s.world.factory_construction_get_purchased_goods(r.factory), s.world.factory_type_get_construction_costs(s.world.factory_construction_get_type(r.factory)));
		if(s.world.capital_project_get_status(p) == uint8_t(status::completed)) {
			if(r.building && s.world.province_building_construction_is_valid(r.building)) s.world.delete_province_building_construction(r.building);
			if(r.naval && s.world.province_naval_construction_is_valid(r.naval)) s.world.delete_province_naval_construction(r.naval);
			if(r.factory && s.world.factory_construction_is_valid(r.factory)) s.world.delete_factory_construction(r.factory);
			r.building = {}; r.naval = {}; r.factory = {};
		}
	}
}

void process_projects(sys::state& s) {
	// A previous day's reservation cannot suppress today's procurement. Other
	// market clients retain their own scheduler and expiry behavior.
	s.world.for_each_concrete_market_bid([&](auto bid) {
		if(s.world.concrete_market_bid_get_status(bid) != 0 || s.world.concrete_market_bid_get_created_on(bid) >= s.current_date) return;
		auto account = s.world.concrete_market_bid_get_monetary_account_from_concrete_bid_account(bid);
		bool project_account = false;
		s.world.for_each_capital_project([&](auto p) { if(s.world.capital_project_get_monetary_account_from_capital_project_account(p) == account) project_account = true; });
		if(project_account) {
			s.world.concrete_market_bid_set_status(bid, uint8_t(physical::concrete_market::order_status::canceled));
			s.world.concrete_market_bid_set_reserved_amount(bid, 0.0f);
		}
	});
	adopt_legacy_requests(s);
	s.world.for_each_capital_project([&](dcon::capital_project_id project) {
		if(s.world.capital_project_get_status(project) == uint8_t(status::suspended)
			|| s.world.capital_project_get_status(project) >= uint8_t(status::completed)) return;
		auto project_status = s.world.capital_project_get_status(project);
		if((project_status == uint8_t(status::funded) || project_status == uint8_t(status::active))
			&& requirements_satisfied(s, project)) {
			(void)complete(s, project);
			return;
		}
		auto owner = s.world.capital_project_get_economic_actor_from_capital_project_sponsor(project);
		auto site = s.world.capital_project_get_site_from_capital_project_site(project);
		auto account = s.world.capital_project_get_monetary_account_from_capital_project_account(project);
		auto market = physical::concrete_market::market_for_site(s, site);
		if(!owner || !site || !account) return;
		struct procurement_line { dcon::commodity_id commodity; float remaining; float limit; };
		std::vector<procurement_line> lines;
		s.world.capital_project_for_each_capital_project_requirement_project_as_capital_project(project, [&](auto relation) {
			auto requirement = s.world.capital_project_requirement_project_get_capital_project_requirement(relation);
			auto commodity = s.world.capital_project_requirement_get_commodity_from_capital_project_requirement_commodity(requirement);
			auto remaining = std::max(0.0f, s.world.capital_project_requirement_get_required_quantity(requirement)
				- s.world.capital_project_requirement_get_consumed_quantity(requirement));
			if(remaining <= 0.0f || !commodity) return;
			auto available = physical::inventory::quantity(s, site, commodity, owner);
			if(available > 0.0f) (void)consume(s, requirement, available);
			remaining = std::max(0.0f, s.world.capital_project_requirement_get_required_quantity(requirement)
				- s.world.capital_project_requirement_get_consumed_quantity(requirement));
			if(remaining <= 0.0f) return;
			float committed = 0.0f;
			s.world.for_each_freight_request([&](auto request) {
				if(s.world.freight_request_get_status(request) == 0
					&& s.world.freight_request_get_economic_actor_from_freight_request_requester(request) == owner
					&& s.world.freight_request_get_site_from_freight_request_destination(request) == site
					&& s.world.freight_request_get_commodity_from_freight_request_commodity(request) == commodity)
					committed += std::max(0.0f, s.world.freight_request_get_quantity(request));
			});
			s.world.for_each_shipment([&](auto shipment) {
				auto owner_relation = s.world.shipment_get_shipment_owner(shipment);
				auto destination = s.world.shipment_get_shipment_destination(shipment);
				if(s.world.shipment_get_commodity(shipment) == commodity && owner_relation && destination
					&& s.world.shipment_owner_get_economic_actor(owner_relation) == owner
					&& s.world.shipment_destination_get_site(destination) == site)
					committed += std::max(0.0f, s.world.shipment_get_remaining_quantity(shipment));
			});
			s.world.for_each_concrete_market_bid([&](auto bid) {
				if(s.world.concrete_market_bid_get_status(bid) == 0
					&& s.world.concrete_market_bid_get_monetary_account_from_concrete_bid_account(bid) == account
					&& s.world.concrete_market_bid_get_site_from_concrete_bid_destination(bid) == site
					&& s.world.concrete_market_bid_get_commodity_from_concrete_bid_commodity(bid) == commodity)
					committed += std::max(0.0f, s.world.concrete_market_bid_get_remaining_quantity(bid));
			});
			if(!market) return;
			auto price = physical::concrete_market::canonical_reference_price(s, market, commodity, s.current_date);
			auto limit = price * 1.20f;
			if(remaining > committed && limit > 0.0f && std::isfinite(limit)) lines.push_back({commodity, std::max(remaining - committed, 1.0e-4f), limit});
		});
		// A tiny physical replacement lot clears transport losses without rounding
		// an unfinished requirement into completion. Surplus remains real stock.
		// Allocate scarce cash across the authored recipe, independent of row
		// insertion order, then emit bids in a stable commodity order.
		std::sort(lines.begin(), lines.end(), [](auto const& a, auto const& b) { return a.commodity.index() < b.commodity.index(); });
		double cost = 0.0;
		for(auto const& line : lines) cost += double(line.remaining) * line.limit;
		auto cash = std::max(0.0f, accounts::balance(s, account) - physical::concrete_market::reserved_bid_amount(s, account));
		auto ratio = cost > 0.0 ? std::min(1.0, double(cash) / cost) : 0.0;
		for(auto const& line : lines) {
			auto free_cash = std::max(0.0f, accounts::balance(s, account) - physical::concrete_market::reserved_bid_amount(s, account));
			auto quantity = std::min(float(line.remaining * ratio), free_cash / line.limit);
			if(quantity > 1.0e-5f) physical::concrete_market::post_bid(s, owner, account, site, market, line.commodity,
				quantity, line.limit, physical::concrete_market::order_purpose::general);
		}
	});
	// Offer physical stock for the same shared clearing used by factory inputs.
	// Subtract existing ask reservations; posting twice cannot sell stock twice.
	s.world.for_each_physical_stock([&](auto stock) {
		auto c = s.world.physical_stock_get_commodity_from_physical_stock_commodity(stock);
		auto source = s.world.physical_stock_get_site_from_physical_stock_site(stock);
		auto relation = s.world.physical_stock_get_physical_stock_owner(stock);
		auto owner = relation ? s.world.physical_stock_owner_get_economic_actor(relation) : dcon::economic_actor_id{};
		bool requested = false;
		s.world.for_each_concrete_market_bid([&](auto bid) {
			if(s.world.concrete_market_bid_get_status(bid) == 0 && s.world.concrete_market_bid_get_commodity_from_concrete_bid_commodity(bid) == c
				&& s.world.concrete_market_bid_get_economic_actor_from_concrete_bid_buyer(bid) != owner) requested = true;
		});
		if(!requested || !owner || !source || is_construction_site(s, source)) return;
		float reserved = 0.0f;
		s.world.for_each_concrete_market_ask([&](auto ask) {
			if(s.world.concrete_market_ask_get_status(ask) == 0 && s.world.concrete_market_ask_get_economic_actor_from_concrete_ask_seller(ask) == owner
				&& s.world.concrete_market_ask_get_site_from_concrete_ask_site(ask) == source && s.world.concrete_market_ask_get_commodity_from_concrete_ask_commodity(ask) == c)
				reserved += std::max(0.0f, s.world.concrete_market_ask_get_reserved_quantity(ask));
		});
		auto amount = physical::inventory::quantity(s, source, c, owner) - reserved;
		auto market = physical::concrete_market::market_for_site(s, source);
		auto price = physical::concrete_market::canonical_reference_price(s, market, c, s.current_date);
		if(amount > 1.0e-5f && price > 0.0f) physical::concrete_market::post_ask(s, owner, source, market, c, amount, price, physical::concrete_market::order_purpose::general);
	});
	project_legacy_requests(s);
}
} // namespace economy::capital_projects
