#pragma once

#include "dcon_generated.hpp"

namespace sys { class state; }

namespace economy::physical::extraction {

enum class deposit_status : uint8_t { active = 0, suspended = 1, depleted = 2 };
enum class right_status : uint8_t { active = 0, revoked = 1, expired = 2 };

dcon::resource_extraction_right_id create_right(sys::state&, dcon::person_id initiator,
	dcon::office_id office, dcon::resource_deposit_id deposit, dcon::economic_actor_id holder,
	dcon::nation_id granting_nation, sys::date valid_from, sys::date valid_until,
	float max_daily_quantity, sys::date date);
dcon::resource_extraction_right_id active_right_for(sys::state const&, dcon::resource_deposit_id, sys::date);
dcon::resource_extraction_right_id active_right_for(sys::state const&, dcon::resource_deposit_id,
	dcon::economic_actor_id holder, sys::date);
float extract_resource(sys::state&, dcon::resource_deposit_id, dcon::economic_actor_id,
	float requested_quantity, sys::date);

} // namespace economy::physical::extraction
