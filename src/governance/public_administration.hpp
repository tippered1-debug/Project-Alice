#pragma once

#include "dcon_generated.hpp"
#include "governance/governance.hpp"

namespace sys { class state; }

namespace governance::public_administration {

void bootstrap(sys::state&);
void synchronize_local_governments(sys::state&);
dcon::institution_id institution_for(sys::state const&, dcon::nation_id,
	governance::institution_kind);
dcon::economic_actor_id household_sector_actor(sys::state const&, dcon::nation_id);
dcon::monetary_account_id household_sector_account(sys::state&, dcon::nation_id);
dcon::institution_id tax_authority_for(sys::state const&, dcon::nation_id);
dcon::monetary_account_id tax_treasury_for(sys::state const&, dcon::nation_id);
float treasury_cash(sys::state const&, dcon::nation_id);
float daily_budget(sys::state const&, dcon::nation_id);
void appropriate_daily_budget(sys::state&, dcon::nation_id);
void plan_public_staffing(sys::state&, dcon::nation_id);
void settle_public_payroll(sys::state&);
void post_procurement_bids(sys::state&);
void deliver_public_services(sys::state&);

} // namespace governance::public_administration
