#include "organizations.hpp"

#include "system_state.hpp"
#include "economy/accounts/accounts.hpp"

namespace actors::organizations {

bool is_economic_kind(ownership::actor_kind kind) noexcept {
	switch(kind) {
	case ownership::actor_kind::company:
	case ownership::actor_kind::bank:
	case ownership::actor_kind::fund:
	case ownership::actor_kind::cooperative:
	case ownership::actor_kind::other:
		return true;
	default:
		return false;
	}
}

dcon::organization_id create_organization(sys::state& s, ownership::actor_kind kind) {
	if(!is_economic_kind(kind)) return {};
	auto organization = s.world.create_organization();
	s.world.organization_set_kind(organization, uint8_t(kind));
	auto actor = s.world.create_economic_actor();
	s.world.economic_actor_set_kind(actor, uint8_t(kind));
	s.world.force_create_organization_actor(organization, actor);
	auto equity = s.world.create_asset();
	s.world.force_create_organization_equity_asset(organization, equity);
	return organization;
}

dcon::organization_id create_company(sys::state& s) {
	return create_organization(s, ownership::actor_kind::company);
}

dcon::economic_actor_id actor_for_organization(sys::state const& s, dcon::organization_id organization) {
	return organization ? s.world.organization_get_economic_actor_from_organization_actor(organization) : dcon::economic_actor_id{};
}

dcon::asset_id equity_asset_for_organization(sys::state const& s, dcon::organization_id organization) {
	return organization ? s.world.organization_get_asset_from_organization_equity_asset(organization) : dcon::asset_id{};
}

dcon::monetary_account_id operating_account_for(sys::state const& s, dcon::organization_id organization,
	dcon::commodity_id settlement) {
	return organization && settlement ? economy::accounts::find_account(s, actor_for_organization(s, organization), settlement)
		: dcon::monetary_account_id{};
}

dcon::monetary_account_id operating_account_for(sys::state& s, dcon::organization_id organization,
	dcon::commodity_id settlement) {
	if(auto existing = operating_account_for(static_cast<sys::state const&>(s), organization, settlement)) return existing;
	if(!organization || !settlement || !is_economic_kind(ownership::actor_kind(s.world.organization_get_kind(organization)))) return {};
	return economy::accounts::open_account(s, actor_for_organization(s, organization), settlement);
}

dcon::organization_id organization_for_actor(sys::state const& s, dcon::economic_actor_id actor) {
	return actor ? s.world.economic_actor_get_organization_from_organization_actor(actor) : dcon::organization_id{};
}

bool bind_factory_operator(sys::state& s, dcon::organization_id organization, dcon::factory_id factory) {
	if(!organization || !factory || !is_economic_kind(ownership::actor_kind(s.world.organization_get_kind(organization)))) return false;
	if(auto current = operator_organization_for_factory(s, factory)) return current == organization;
	s.world.force_create_organization_factory_operator(organization, factory);
	return true;
}

bool transfer_factory_operator(sys::state& s, dcon::organization_id organization, dcon::factory_id factory) {
	if(!organization || !factory || !s.world.factory_is_valid(factory)
		|| !is_economic_kind(ownership::actor_kind(s.world.organization_get_kind(organization)))) return false;
	auto current = operator_organization_for_factory(s, factory);
	if(current == organization) return true;
	auto relation = s.world.factory_get_organization_factory_operator(factory);
	if(relation) s.world.delete_organization_factory_operator(relation);
	s.world.force_create_organization_factory_operator(organization, factory);
	return true;
}

dcon::organization_id operator_organization_for_factory(sys::state const& s, dcon::factory_id factory) {
	return factory ? s.world.factory_get_organization_from_organization_factory_operator(factory) : dcon::organization_id{};
}

dcon::economic_actor_id operator_actor_for_factory(sys::state const& s, dcon::factory_id factory) {
	return actor_for_organization(s, operator_organization_for_factory(s, factory));
}

std::vector<dcon::factory_id> factories_operated_by(sys::state const& s, dcon::organization_id organization) {
	std::vector<dcon::factory_id> result;
	if(!organization) return result;
	s.world.organization_for_each_organization_factory_operator_as_organization(organization, [&](dcon::organization_factory_operator_id relation) {
		result.push_back(s.world.organization_factory_operator_get_factory(relation));
	});
	return result;
}

bool bind_deposit_operator(sys::state& s, dcon::organization_id organization, dcon::resource_deposit_id deposit) {
	if(!organization || !deposit || !is_economic_kind(ownership::actor_kind(s.world.organization_get_kind(organization)))) return false;
	if(auto current = operator_organization_for_deposit(s, deposit)) return current == organization;
	s.world.force_create_resource_deposit_operator(deposit, organization);
	return true;
}

dcon::organization_id operator_organization_for_deposit(sys::state const& s, dcon::resource_deposit_id deposit) {
	return deposit ? s.world.resource_deposit_get_organization_from_resource_deposit_operator(deposit) : dcon::organization_id{};
}

dcon::economic_actor_id operator_actor_for_deposit(sys::state const& s, dcon::resource_deposit_id deposit) {
	return actor_for_organization(s, operator_organization_for_deposit(s, deposit));
}

std::vector<dcon::resource_deposit_id> deposits_operated_by(sys::state const& s, dcon::organization_id organization) {
	std::vector<dcon::resource_deposit_id> result;
	if(!organization) return result;
	s.world.organization_for_each_resource_deposit_operator_as_organization(organization, [&](dcon::resource_deposit_operator_id relation) {
		result.push_back(s.world.resource_deposit_operator_get_resource_deposit(relation));
	});
	return result;
}

} // namespace actors::organizations
