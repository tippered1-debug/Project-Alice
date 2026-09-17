#include "capital_projects.hpp"
#include "system_state.hpp"
#include "actors/organizations/organizations.hpp"
#include "actors/ownership.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/physical/inventory.hpp"
#include "economy/physical/shipments.hpp"
#include "economy/physical/deposits.hpp"
#include "governance/finance/finance.hpp"
#include <algorithm>
#include <cmath>

namespace economy::capital_projects {
namespace {
bool valid(sys::state const& s, dcon::capital_project_id p) { return p && s.world.capital_project_is_valid(p); }
dcon::site_id site(sys::state const& s, dcon::capital_project_id p) {
	return valid(s, p) ? s.world.capital_project_get_site_from_capital_project_site(p) : dcon::site_id{};
}
dcon::economic_actor_id sponsor(sys::state const& s, dcon::capital_project_id p) {
	return valid(s, p) ? s.world.capital_project_get_economic_actor_from_capital_project_sponsor(p) : dcon::economic_actor_id{};
}
void refresh(sys::state& s, dcon::capital_project_id p) {
	float required = 0.0f, consumed = 0.0f;
	s.world.capital_project_for_each_capital_project_requirement_project_as_capital_project(p, [&](auto rel) {
		auto r = s.world.capital_project_requirement_project_get_capital_project_requirement(rel);
		auto need = std::max(0.0f, s.world.capital_project_requirement_get_required_quantity(r));
		required += need;
		consumed += std::clamp(s.world.capital_project_requirement_get_consumed_quantity(r), 0.0f, need);
	});
	s.world.capital_project_set_progress(p, required > 0.0f ? std::clamp(consumed / required, 0.0f, 1.0f) : 0.0f);
}
}

dcon::capital_project_id create(sys::state& s, project_kind kind, dcon::economic_actor_id owner,
	dcon::organization_id responsible, dcon::site_id project_site, dcon::commodity_id settlement, dcon::factory_type_id type, dcon::commodity_id target_commodity,
	float planned_reserves, float planned_grade, float planned_daily_capacity, float planned_target_daily_extraction) {
	if(!owner || !responsible || !project_site || !settlement || !s.world.economic_actor_is_valid(owner)
		|| !s.world.organization_is_valid(responsible) || !s.world.site_is_valid(project_site)
		|| !s.world.commodity_is_valid(settlement) || (kind == project_kind::factory && !type)
		|| (type && !s.world.factory_type_is_valid(type))
		|| (kind == project_kind::extraction_site && !target_commodity)
		|| (target_commodity && !s.world.commodity_is_valid(target_commodity))
		|| !std::isfinite(planned_reserves) || planned_reserves < 0.0f
		|| !std::isfinite(planned_grade) || planned_grade < 0.0f
		|| !std::isfinite(planned_daily_capacity) || planned_daily_capacity < 0.0f
		|| !std::isfinite(planned_target_daily_extraction) || planned_target_daily_extraction < 0.0f
		|| (kind == project_kind::extraction_site && planned_reserves <= 0.0f)) return {};
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
	s.world.capital_project_set_planned_reserves(p, planned_reserves);
	s.world.capital_project_set_planned_grade(p, planned_grade);
	s.world.capital_project_set_planned_daily_capacity(p, planned_daily_capacity);
	s.world.capital_project_set_planned_target_daily_extraction(p, planned_target_daily_extraction);
	s.world.force_create_capital_project_sponsor(p, owner);
	s.world.force_create_capital_project_responsible(p, responsible);
	s.world.force_create_capital_project_site(p, project_site);
	auto account = accounts::open_account(s, owner, settlement);
	if(!account) return {};
	s.world.force_create_capital_project_account(p, account);
	return p;
}

dcon::capital_project_requirement_id add_requirement(sys::state& s, dcon::capital_project_id p, dcon::commodity_id c, float amount) {
	if(!valid(s, p) || !c || !s.world.commodity_is_valid(c) || !std::isfinite(amount) || amount <= 0.0f
		|| s.world.capital_project_get_status(p) >= uint8_t(status::completed)) return {};
	auto r = s.world.create_capital_project_requirement();
	s.world.capital_project_requirement_set_required_quantity(r, amount);
	s.world.capital_project_requirement_set_consumed_quantity(r, 0.0f);
	s.world.force_create_capital_project_requirement_project(r, p);
	s.world.force_create_capital_project_requirement_commodity(r, c);
	return r;
}

dcon::transaction_id fund(sys::state& s, dcon::capital_project_id p, dcon::monetary_account_id source, float amount) {
	if(!valid(s, p) || !source || !std::isfinite(amount) || amount <= 0.0f || sponsor(s, p) != accounts::owner_of(s, source)) return {};
	auto account = s.world.capital_project_get_monetary_account_from_capital_project_account(p);
	if(accounts::settlement_of(s, source) != accounts::settlement_of(s, account)) return {};
	auto tx = accounts::transfer(s, source, account, amount, relations::transaction_kind::transfer, s.current_date);
	if(tx && s.world.capital_project_get_status(p) == uint8_t(status::planned)) {
		s.world.capital_project_set_status(p, uint8_t(status::funded));
		s.world.capital_project_set_started_on(p, s.current_date);
	}
	return tx;
}

dcon::fiscal_action_id fund_state(sys::state& s, dcon::capital_project_id p, dcon::person_id initiator,
	dcon::monetary_account_id treasury, float amount) {
	if(!valid(s, p) || !treasury) return {};
	if(sponsor(s, p) != accounts::owner_of(s, treasury)) return {};
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
		|| (price > 0.0f && accounts::balance(s, project_account) < price)) return {};
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
	auto need = std::max(0.0f, s.world.capital_project_requirement_get_required_quantity(r)
		- s.world.capital_project_requirement_get_consumed_quantity(r));
	auto taken = physical::inventory::remove(s, site(s, p), c, std::min({amount, need, physical::inventory::quantity(s, site(s, p), c, owner)}), owner);
	s.world.capital_project_requirement_set_consumed_quantity(r, s.world.capital_project_requirement_get_consumed_quantity(r) + taken);
	if(taken > 0.0f && s.world.capital_project_get_status(p) == uint8_t(status::funded))
		s.world.capital_project_set_status(p, uint8_t(status::active));
	refresh(s, p);
	if(material_progress(s, p) >= 1.0f) complete(s, p);
	return taken;
}

float material_progress(sys::state const& s, dcon::capital_project_id p) { return valid(s, p) ? std::clamp(s.world.capital_project_get_progress(p), 0.0f, 1.0f) : 0.0f; }
bool suspend(sys::state& s, dcon::capital_project_id p) { if(!valid(s,p) || s.world.capital_project_get_status(p) >= uint8_t(status::completed)) return false; s.world.capital_project_set_status(p,uint8_t(status::suspended)); return true; }
bool cancel(sys::state& s, dcon::capital_project_id p) { if(!valid(s,p) || s.world.capital_project_get_status(p) == uint8_t(status::completed)) return false; s.world.capital_project_set_status(p,uint8_t(status::cancelled)); return true; }

bool complete(sys::state& s, dcon::capital_project_id p) {
	if(!valid(s,p) || material_progress(s,p) < 1.0f || s.world.capital_project_get_status(p) == uint8_t(status::cancelled)) return false;
	if(s.world.capital_project_get_factory_from_capital_project_factory(p)
		|| s.world.capital_project_get_resource_deposit_from_capital_project_deposit(p)
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
		s.world.force_create_factory_location(f, province);
		s.world.force_create_factory_site(f, project_site);
		auto asset = s.world.create_asset();
		s.world.force_create_factory_asset(f, asset);
		auto stake = actors::ownership::create_stake(s, sponsor(s,p), asset, 1.0f, 1.0f, 1.0f);
		if(!stake || !actors::organizations::bind_factory_operator(s, responsible, f)) {
			if(stake) s.world.delete_ownership_stake(stake);
			s.world.delete_factory(f);
			s.world.delete_asset(asset);
			return false;
		}
		s.world.force_create_capital_project_factory(p, f);
		s.world.force_create_capital_project_asset(p, asset);
	} else if(s.world.capital_project_get_project_kind(p) == uint8_t(project_kind::extraction_site)) {
		auto d = physical::deposits::create_deposit(s, site(s,p), s.world.capital_project_get_target_commodity(p),
			s.world.capital_project_get_planned_reserves(p), s.world.capital_project_get_planned_reserves(p),
			s.world.capital_project_get_planned_grade(p), s.world.capital_project_get_planned_daily_capacity(p),
			s.world.capital_project_get_planned_target_daily_extraction(p));
		if(!d) return false;
		if(!actors::organizations::bind_deposit_operator(s, responsible, d)) {
			s.world.delete_resource_deposit(d);
			return false;
		}
		auto asset = s.world.create_asset();
		auto stake = actors::ownership::create_stake(s, sponsor(s,p), asset, 1.0f, 1.0f, 1.0f);
		if(!stake) {
			s.world.delete_resource_deposit(d);
			s.world.delete_asset(asset);
			return false;
		}
		s.world.force_create_resource_deposit_asset(d, asset);
		s.world.force_create_capital_project_deposit(p, d);
		s.world.force_create_capital_project_asset(p, asset);
	} else {
		auto node = s.world.create_infrastructure_node();
		auto province = s.world.site_get_province_from_site_location(site(s,p));
		if(!province) {
			s.world.delete_infrastructure_node(node);
			return false;
		}
		s.world.infrastructure_node_set_position(node, s.world.province_get_mid_point_b(province));
		s.world.force_create_infrastructure_node_location(node, province);
		s.world.force_create_capital_project_node(p, node);
	}
	s.world.capital_project_set_status(p,uint8_t(status::completed));
	s.world.capital_project_set_completed_on(p,s.current_date);
	s.world.capital_project_set_progress(p,1.0f);
	return true;
}
} // namespace economy::capital_projects
