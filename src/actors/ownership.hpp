#pragma once

#include "dcon_generated.hpp"

#include <string>
#include <vector>

namespace sys { class state; }

namespace actors::ownership {

enum class actor_kind : uint8_t { invalid = 0, company = 1, bank = 2, fund = 3, cooperative = 4, state_entity = 5, other = 6, person = 7, household = 8, party = 9, labor_union = 10, employer_association = 11 };

dcon::economic_actor_id actor_for_organization(sys::state const&, dcon::organization_id);
dcon::asset_id equity_asset_for_organization(sys::state const&, dcon::organization_id);
dcon::asset_id asset_for_factory(sys::state const&, dcon::factory_id);
dcon::asset_id asset_for_deposit(sys::state const&, dcon::resource_deposit_id);
dcon::economic_actor_id operator_for_deposit(sys::state const&, dcon::resource_deposit_id);
dcon::ownership_stake_id create_stake(sys::state&, dcon::economic_actor_id, dcon::asset_id, float, float, float);
void assign_runtime_canonical_id(sys::state&, dcon::economic_actor_id);
void assign_runtime_canonical_id(sys::state&, dcon::organization_id);
void assign_runtime_canonical_id(sys::state&, dcon::site_id);
void assign_runtime_canonical_id(sys::state&, dcon::factory_id);
void assign_runtime_canonical_id(sys::state&, dcon::resource_deposit_id);
void assign_runtime_canonical_id(sys::state&, dcon::asset_id);
void assign_runtime_canonical_id(sys::state&, dcon::ownership_stake_id);
bool set_stake_fractions(sys::state&, dcon::ownership_stake_id, float, float, float);
bool issue_equity(sys::state&, dcon::asset_id, dcon::economic_actor_id investor,
	float investment, float pre_money_value);
void collect_canonical_ownership_errors(sys::state const&, std::vector<std::string>&);
bool canonical_ownership_is_valid(sys::state const&);
void validate_canonical_ownership(sys::state const&);
bool valid_fraction(float) noexcept;

} // namespace actors::ownership
