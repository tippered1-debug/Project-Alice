#pragma once

#include "dcon_generated.hpp"

#include <cstdint>

namespace sys { class state; }

namespace governance::defense {

// The armed forces cost what the military command pays from its own treasury,
// funded by the defense appropriation. A soldier is fed and equipped by the army
// in kind; the soldier's pay goes to the household cohort the family belongs to.

// Days of pay in one monthly payroll.
inline constexpr float pay_period_days = 30.0f;
// Soldiers leave once the pay owed to their families exceeds this many months
// of the current payroll of those still serving.
inline constexpr float desertion_arrears_months = 3.0f;
// The share of such a group that leaves in one month, rounded up.
inline constexpr float monthly_desertion_share = 1.0f / 3.0f;
// Days of each consumable the command keeps in the army's stores and formations.
inline constexpr float supply_target_days = 60.0f;
// Procurement spends at most this fraction of the free treasury a day.
inline constexpr float daily_procurement_share = 0.1f;
// Highest price over the reference price a procurement bid accepts.
inline constexpr float procurement_markup = 1.1f;

dcon::institution_id military_command_for(sys::state const&, dcon::nation_id);

// One soldier's monthly pay: thirty days of the unskilled wage in the province
// the soldier's family lives in, times the command's wage multiplier.
float monthly_pay(sys::state const&, dcon::nation_id, dcon::province_id home);
// The month's pay of every soldier now serving in the nation's formations.
float monthly_payroll(sys::state const&, dcon::nation_id);
// Unpaid pay the command owes soldiers' families.
float pay_arrears(sys::state const&, dcon::nation_id);

struct payroll_result {
	float due = 0.0f;
	float paid = 0.0f;
	float arrears_paid = 0.0f;
	float arrears = 0.0f;
	uint64_t soldiers = 0;
	uint64_t deserted = 0;
};
// Pays one month: the treasury's free cash is shared pro rata over the arrears
// and current pay of each family group, arrears first. What it cannot pay is
// owed to the household cohort as a payroll obligation. Where arrears exceed
// `desertion_arrears_months` of the group's current pay, a third of its soldiers
// leave service, most recently recruited first, and rejoin their cohort.
payroll_result pay_armed_forces(sys::state&, dcon::nation_id);
// Called every day; pays on the first of the month.
void process_payroll(sys::state&);

// The command bids for what the army lacks: consumables up to
// `supply_target_days` of the formations' demand, and the authorized equipment
// of each model it has a stockpile for. Goods are delivered to the stockpile's
// site; nothing is bought without cash left after the next payroll and arrears.
void post_procurement_bids(sys::state&);
// Hands delivered goods at the army's stockpile sites to the stockpile custodian.
void take_delivery(sys::state&);

} // namespace governance::defense
