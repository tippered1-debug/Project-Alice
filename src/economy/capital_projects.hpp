#pragma once

#include "dcon_generated.hpp"
#include <vector>
#include <type_traits>

namespace sys { class state; }

namespace economy { struct capital_projects_store; }

namespace economy::capital_projects {

enum class project_kind : uint8_t { factory = 0, extraction_site = 1, infrastructure = 2, factory_expansion = 3, military_production = 4, naval_construction = 5 };
enum class status : uint8_t { planned = 0, funded = 1, active = 2, suspended = 3, completed = 4, cancelled = 5 };

dcon::capital_project_id create(sys::state&, project_kind, dcon::economic_actor_id,
	dcon::organization_id, dcon::site_id, dcon::commodity_id, dcon::factory_type_id = {}, dcon::commodity_id target_commodity = {},
	float planned_reserves = 100.0f, float planned_grade = 1.0f, float planned_daily_capacity = 1.0f,
	float planned_target_daily_extraction = 1.0f);
dcon::capital_project_requirement_id add_requirement(sys::state&, dcon::capital_project_id, dcon::commodity_id, float);
dcon::transaction_id fund(sys::state&, dcon::capital_project_id, dcon::monetary_account_id, float);
dcon::fiscal_action_id fund_state(sys::state&, dcon::capital_project_id, dcon::person_id,
	dcon::monetary_account_id, float);
dcon::shipment_id deliver_material(sys::state&, dcon::capital_project_id, dcon::monetary_account_id,
	dcon::site_id, dcon::commodity_id, float, float price = 0.0f);
float consume(sys::state&, dcon::capital_project_requirement_id, float);
float material_progress(sys::state const&, dcon::capital_project_id);
bool suspend(sys::state&, dcon::capital_project_id);
bool resume(sys::state&, dcon::capital_project_id);
bool is_construction_site(sys::state const&, dcon::site_id);
bool cancel(sys::state&, dcon::capital_project_id);
bool complete(sys::state&, dcon::capital_project_id);
dcon::capital_project_id create_factory_expansion(sys::state&, dcon::factory_id, float added_capacity,
	dcon::commodity_id settlement);
dcon::capital_project_id create_greenfield_factory(sys::state&, dcon::economic_actor_id sponsor,
	dcon::organization_id operator_company, dcon::site_id, dcon::factory_type_id,
	dcon::commodity_id settlement, float planned_capacity = 1.0f);
// Save-backed adapter metadata. Legacy queue rows are presentation only;
// purchased_goods is never imported as stock or as consumed requirements.
struct request_record {
	dcon::capital_project_id project{};
	dcon::province_building_construction_id building{};
	dcon::province_naval_construction_id naval{};
	dcon::factory_construction_id factory{};
	dcon::nation_id nation{};
	dcon::ship_id ship{};
	dcon::unit_type_id unit{};
	uint8_t building_type = 255;
	uint8_t target_level = 0;
	uint8_t reserved[3]{};
};
static_assert(std::has_unique_object_representations_v<request_record>, "AOEX construction rows must not contain padding");
std::vector<request_record> export_requests(sys::state const&);
bool import_requests(sys::state&, std::vector<request_record> const&);
void clear_requests(sys::state&);
void isolate_pre_cut_projects(sys::state&);
dcon::capital_project_id project_for(sys::state const&, dcon::province_building_construction_id);
dcon::capital_project_id project_for(sys::state const&, dcon::province_naval_construction_id);
dcon::capital_project_id project_for(sys::state const&, dcon::factory_construction_id);
economy::commodity_set consumed_materials(sys::state const&, dcon::capital_project_id);
dcon::capital_project_id create_infrastructure(sys::state&, dcon::economic_actor_id, dcon::organization_id,
	dcon::site_id, dcon::commodity_id settlement, uint8_t building_type);
dcon::capital_project_id create_military_production(sys::state&, dcon::economic_actor_id, dcon::organization_id,
	dcon::site_id, dcon::commodity_id settlement, dcon::commodity_id output, float quantity,
	economy::commodity_set const& recipe);
void adopt_legacy_requests(sys::state&);
void project_legacy_requests(sys::state&);
void process_projects(sys::state&);

} // namespace economy::capital_projects
