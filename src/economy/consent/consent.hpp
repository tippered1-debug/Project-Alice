#pragma once

#include "dcon_generated.hpp"
#include "date_interface.hpp"

#include <cstdint>

namespace sys { class state; }

namespace economy::consent {

enum class decision_kind : uint8_t { borrow = 0, lend = 1, invest = 2 };
enum class proposal_kind : uint8_t { loan = 0, investment = 1 };
enum class proposal_status : uint8_t { pending = 0, executed = 1, rejected = 2 };

dcon::organization_decision_mandate_id create_mandate(sys::state&, dcon::organization_id,
	dcon::person_id, decision_kind, sys::date started_on, sys::date ended_on = {});
bool can_decide_for_actor(sys::state const&, dcon::person_id, dcon::economic_actor_id,
	decision_kind, sys::date);

dcon::economic_proposal_id create_proposal(sys::state&, proposal_kind,
	dcon::economic_actor_id actor_a, dcon::economic_actor_id actor_b,
	dcon::commodity_id settlement, float amount, sys::date due_date,
	float annual_interest_rate, sys::date created_on);
dcon::economic_decision_id accept_proposal(sys::state&, dcon::economic_proposal_id,
	dcon::economic_actor_id, dcon::person_id, sys::date);
dcon::economic_decision_id reject_proposal(sys::state&, dcon::economic_proposal_id,
	dcon::economic_actor_id, dcon::person_id, sys::date);
bool proposal_fully_accepted(sys::state const&, dcon::economic_proposal_id, sys::date);
bool mark_executed(sys::state&, dcon::economic_proposal_id);

} // namespace economy::consent
