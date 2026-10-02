#pragma once

#include "dcon_generated.hpp"
#include "economy/firm_agency.hpp"

namespace sys { class state; }

// The construction industry. A capital project is built only by the crew of
// a contractor's construction works: exact workers hired through the job
// market, paid through ordinary payroll, mechanized by equipment the
// contractor physically holds. A day of work installs the project's materials
// in proportion to the work performed, and the sponsor pays the contractor for
// exactly the work it received. Neither money nor material is created or
// destroyed by contracting, cancellation, or default.
namespace economy::construction_industry {

enum class contract_status : uint8_t { active = 0, completed = 1, cancelled = 2, defaulted = 3 };

// Work is measured in reference worker-days: one unequipped worker for one day.
inline constexpr float hand_work_per_worker = 1.0f;
// An equipped worker performs this much more work per day.
inline constexpr float equipped_work_bonus = 1.0f;
// Equipment units one worker needs to be fully equipped.
inline constexpr float equipment_per_worker = 0.5f;
// Share of the equipment in use that wears out per day of full use.
inline constexpr float equipment_daily_wear = 0.002f;
// Crew a project is staffed with at its reference schedule, per unit of material.
inline constexpr float crew_per_material_unit = 0.05f;
inline constexpr float contractor_margin = 0.15f;
// A contractor may take on a contract while its committed crews fit within
// twice its current crew, or this many workers for a small firm.
inline constexpr float minimum_contractor_crew = 25.0f;
// Days a workable site may go without work before the sponsor terminates the
// contract for non-performance.
inline constexpr uint16_t non_performance_days = 30;
// Days a site may be blocked by its sponsor (no materials or no cash) before
// the contractor stands its crew down for that contract.
inline constexpr uint16_t stand_down_days = 7;
inline constexpr float default_build_days = 30.0f;

bool is_works(sys::state const&, dcon::factory_id);
bool is_works_site(sys::state const&, dcon::site_id);
dcon::factory_id works_for(sys::state const&, dcon::organization_id, dcon::province_id);
// Opens a construction works for an organization in a province. The works is
// an establishment record with no product recipe: it has a site, an operator,
// an asset owned by the operator, and a payroll settlement. It holds no stock
// and no capacity of its own; its crew and equipment are its capacity.
dcon::factory_id open_works(sys::state&, dcon::organization_id, dcon::province_id, dcon::commodity_id settlement);
// The state-owned construction enterprise of a nation, created on first use.
dcon::organization_id public_works_enterprise(sys::state&, dcon::nation_id);

dcon::commodity_id equipment_commodity(sys::state const&);
float equipment_stock(sys::state const&, dcon::factory_id);
float crew(sys::state const&, dcon::factory_id);
float daily_work_capacity(sys::state const&, dcon::factory_id);
float committed_crew(sys::state const&, dcon::factory_id);
float offer_wage(sys::state const&, dcon::factory_id);
// Price per work unit the contractor asks for a new contract.
float quote(sys::state const&, dcon::factory_id);

float reference_crew(float material_quantity);
float required_work(float material_quantity, float build_days);
// Estimated price of one work unit in a province, for budgeting.
float reference_unit_price(sys::state const&, dcon::province_id);

dcon::construction_contract_id active_contract(sys::state const&, dcon::capital_project_id);
dcon::factory_id contractor_for(sys::state const&, dcon::capital_project_id);
// Picks the cheapest local contractor with room for the project, or the
// works of the project's responsible organization when none has room.
dcon::factory_id select_contractor(sys::state&, dcon::capital_project_id);
dcon::construction_contract_id sign(sys::state&, dcon::capital_project_id, dcon::factory_id works);
bool end_contract(sys::state&, dcon::construction_contract_id, contract_status);
void end_contracts_for(sys::state&, dcon::capital_project_id, contract_status);

// Staffing plan of a works, used by the job market and layoffs through
// firm_agency::decide_factory.
firm_agency::production_decision decide_works(sys::state const&, dcon::factory_id);
// One day of the crew's work across its contracts, in signing order.
float perform_work(sys::state&, dcon::factory_id);
// Daily: contract upkeep and default, tendering, equipment procurement,
// construction work, and payroll of every works.
void process(sys::state&);

} // namespace economy::construction_industry
