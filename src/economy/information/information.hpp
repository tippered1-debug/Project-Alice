#pragma once

#include "dcon_generated.hpp"
#include "date_interface.hpp"

#include <cstdint>
#include <optional>

namespace sys { class state; }

namespace economy::information {

enum class information_fact_kind : uint8_t { national_public_debt = 0 };

dcon::information_report_id publish_report(sys::state&, information_fact_kind,
	dcon::economic_actor_id source, dcon::economic_actor_id recipient, dcon::nation_id subject,
	dcon::commodity_id settlement, float reported_value, sys::date fact_date,
	sys::date published_on, sys::date received_on, float confidence);
dcon::belief_id adopt_report_as_belief(sys::state&, dcon::economic_actor_id holder,
	dcon::information_report_id report, sys::date date);
std::optional<dcon::belief_id> latest_belief(sys::state const&, dcon::economic_actor_id holder,
	information_fact_kind, dcon::nation_id subject, dcon::commodity_id settlement, sys::date date);

} // namespace economy::information
