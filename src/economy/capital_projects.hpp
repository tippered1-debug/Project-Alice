#pragma once

#include "dcon_generated.hpp"

namespace sys { class state; }

namespace economy::capital_projects {

enum class project_kind : uint8_t { factory = 0, extraction_site = 1, infrastructure = 2 };
enum class status : uint8_t { planned = 0, funded = 1, active = 2, suspended = 3, completed = 4, cancelled = 5 };

dcon::capital_project_id create(sys::state&, project_kind, dcon::economic_actor_id,
	dcon::organization_id, dcon::site_id, dcon::commodity_id, dcon::factory_type_id = {}, dcon::commodity_id target_commodity = {});
dcon::capital_project_requirement_id add_requirement(sys::state&, dcon::capital_project_id, dcon::commodity_id, float);
dcon::transaction_id fund(sys::state&, dcon::capital_project_id, dcon::monetary_account_id, float);
dcon::fiscal_action_id fund_state(sys::state&, dcon::capital_project_id, dcon::person_id,
	dcon::monetary_account_id, float);
dcon::shipment_id deliver_material(sys::state&, dcon::capital_project_id, dcon::monetary_account_id,
	dcon::site_id, dcon::commodity_id, float, float price = 0.0f);
float consume(sys::state&, dcon::capital_project_requirement_id, float);
float material_progress(sys::state const&, dcon::capital_project_id);
bool suspend(sys::state&, dcon::capital_project_id);
bool cancel(sys::state&, dcon::capital_project_id);
bool complete(sys::state&, dcon::capital_project_id);

} // namespace economy::capital_projects
