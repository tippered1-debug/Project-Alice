#pragma once

#include "actors/ownership.hpp"

namespace sys { class state; }

namespace actors::organizations {

bool is_economic_kind(ownership::actor_kind kind) noexcept;
dcon::organization_id create_organization(sys::state&, ownership::actor_kind);
dcon::organization_id create_company(sys::state&);
dcon::economic_actor_id actor_for_organization(sys::state const&, dcon::organization_id);
dcon::asset_id equity_asset_for_organization(sys::state const&, dcon::organization_id);
dcon::monetary_account_id operating_account_for(sys::state&, dcon::organization_id, dcon::commodity_id);
dcon::monetary_account_id operating_account_for(sys::state const&, dcon::organization_id, dcon::commodity_id);
dcon::organization_id organization_for_actor(sys::state const&, dcon::economic_actor_id);

bool bind_factory_operator(sys::state&, dcon::organization_id, dcon::factory_id);
bool transfer_factory_operator(sys::state&, dcon::organization_id, dcon::factory_id);
dcon::organization_id operator_organization_for_factory(sys::state const&, dcon::factory_id);
dcon::economic_actor_id operator_actor_for_factory(sys::state const&, dcon::factory_id);
std::vector<dcon::factory_id> factories_operated_by(sys::state const&, dcon::organization_id);

bool bind_deposit_operator(sys::state&, dcon::organization_id, dcon::resource_deposit_id);
dcon::organization_id operator_organization_for_deposit(sys::state const&, dcon::resource_deposit_id);
dcon::economic_actor_id operator_actor_for_deposit(sys::state const&, dcon::resource_deposit_id);
std::vector<dcon::resource_deposit_id> deposits_operated_by(sys::state const&, dcon::organization_id);

} // namespace actors::organizations
