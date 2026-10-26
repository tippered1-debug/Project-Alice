#pragma once

#include "dcon_generated.hpp"
#include <vector>
#include <type_traits>

namespace sys { class state; }

namespace economy { struct capital_projects_store; }

namespace economy::capital_projects {

// Value 4 once converted a material recipe into military equipment without
// labor. Equipment is a manufactured commodity made by ordinary factories and
// bought in the concrete market; capital projects build fixed capital only.
enum class project_kind : uint8_t { factory = 0, extraction_plant = 1, infrastructure = 2, factory_expansion = 3, retired_military_production = 4, naval_construction = 5 };
enum class status : uint8_t { planned = 0, funded = 1, active = 2, suspended = 3, completed = 4, cancelled = 5 };

dcon::capital_project_id create(sys::state&, project_kind, dcon::economic_actor_id,
	dcon::organization_id, dcon::site_id, dcon::commodity_id, dcon::factory_type_id = {}, dcon::commodity_id target_commodity = {},
	float planned_daily_capacity = 1.0f);
// Builds an extraction plant on an existing deposit. Construction never creates
// reserves: the deposit is authored nature, and the operating company must be
// allowed to operate it when the project is created and when it completes.
dcon::capital_project_id create_extraction_plant(sys::state&, dcon::economic_actor_id sponsor,
	dcon::organization_id operator_company, dcon::resource_deposit_id, dcon::factory_type_id,
	dcon::commodity_id settlement);
dcon::capital_project_requirement_id add_requirement(sys::state&, dcon::capital_project_id, dcon::commodity_id, float);
dcon::transaction_id fund(sys::state&, dcon::capital_project_id, dcon::monetary_account_id, float);
dcon::fiscal_action_id fund_state(sys::state&, dcon::capital_project_id, dcon::person_id,
	dcon::monetary_account_id, float);
dcon::shipment_id deliver_material(sys::state&, dcon::capital_project_id, dcon::monetary_account_id,
	dcon::site_id, dcon::commodity_id, float, float price = 0.0f);
// Physical construction. Materials are installed only by work: a day of a
// contractor's work installs each material in proportion to the work. Work
// stops when any material is missing at the yard. See construction_industry.
float material_quantity(sys::state const&, dcon::capital_project_id);
float build_days(sys::state const&, dcon::capital_project_id);
// The work a project needs: fixed when its first contract is signed, an
// estimate from its recipe and reference schedule before that.
float required_work(sys::state const&, dcon::capital_project_id);
bool seal_required_work(sys::state&, dcon::capital_project_id);
float work_done(sys::state const&, dcon::capital_project_id);
// Additional work the materials now at the yard allow.
float workable(sys::state const&, dcon::capital_project_id);
// Installs materials for the work and completes the project when the work and
// every material are in. Returns the work actually applied.
float apply_work(sys::state&, dcon::capital_project_id, float work);
// The smaller of the work and material fractions; a projection only.
float physical_progress(sys::state const&, dcon::capital_project_id);
bool accepts_work(sys::state const&, dcon::capital_project_id);
float free_cash(sys::state const&, dcon::capital_project_id);
float remaining_labor_cost(sys::state const&, dcon::capital_project_id);
float labor_cost_estimate(sys::state const&, dcon::province_id, float material_quantity, float build_days);
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
// Saves from before the construction industry: consumed materials count as
// installed work, and retired military production orders are cancelled.
void adopt_pre_industry_projects(sys::state&);
dcon::capital_project_id project_for(sys::state const&, dcon::province_building_construction_id);
dcon::capital_project_id project_for(sys::state const&, dcon::province_naval_construction_id);
dcon::capital_project_id project_for(sys::state const&, dcon::factory_construction_id);
economy::commodity_set consumed_materials(sys::state const&, dcon::capital_project_id);
dcon::capital_project_id create_infrastructure(sys::state&, dcon::economic_actor_id, dcon::organization_id,
	dcon::site_id, dcon::commodity_id settlement, uint8_t building_type);
void adopt_legacy_requests(sys::state&);
void project_legacy_requests(sys::state&);
void process_projects(sys::state&);

} // namespace economy::capital_projects
