#pragma once

#include "dcon_generated.hpp"

namespace sys { class state; }

namespace actors::ownership {

enum class actor_kind : uint8_t { placeholder = 0, company = 1, bank = 2, fund = 3, cooperative = 4, state_entity = 5, other = 6 };

dcon::economic_actor_id actor_for_organization(sys::state const&, dcon::organization_id);
dcon::asset_id equity_asset_for_organization(sys::state const&, dcon::organization_id);
dcon::asset_id asset_for_factory(sys::state const&, dcon::factory_id);
dcon::economic_actor_id operator_for_deposit(sys::state const&, dcon::resource_deposit_id);
dcon::economic_actor_id operator_for_site(sys::state const&, dcon::site_id);
dcon::economic_actor_id ensure_placeholder_organization(sys::state&, dcon::organization_id);
void bootstrap(sys::state&);
bool valid_fraction(float) noexcept;

} // namespace actors::ownership
