#pragma once

#include "dcon_generated.hpp"
#include "date_interface.hpp"
#include "persons/persons.hpp"

#include <cstdint>
#include <vector>

namespace sys { class state; }
namespace economy::exact_person_economy { enum class contract_end_reason : uint8_t; }

namespace economy::collective_labor {

using person_key = persons::person_key;

enum class bargaining_status : uint8_t {
	awaiting_recognition = 0,
	recognized = 1,
	bargaining = 2,
	agreement = 3,
	conflict = 4,
	inactive = 5
};
enum class conflict_kind : uint8_t { strike = 1, lockout = 2 };
enum class conflict_status : uint8_t { active = 1, resolved = 2, cancelled = 3 };
enum class union_scope : uint8_t { workplace = 0, employer = 1, sectoral = 2 };

// Union dues and strike benefits are cash flows through actual member wallets
// and the union's settlement accounts.
dcon::labor_union_id create_union(sys::state&, dcon::nation_id, float dues_rate = 0.02f,
	float strike_benefit_rate = 0.25f, union_scope scope = union_scope::workplace);
dcon::labor_union_id create_workplace_union(sys::state&, dcon::factory_id, float dues_rate = 0.02f,
	float strike_benefit_rate = 0.25f);
bool join_union(sys::state&, dcon::labor_union_id, person_key, sys::date joined_on = {});
bool leave_union(sys::state&, dcon::labor_union_id, person_key, sys::date left_on = {});
bool is_union_member(sys::state const&, dcon::labor_union_id, person_key);
dcon::labor_union_id union_for_member(sys::state const&, person_key);
uint32_t member_count(sys::state const&, dcon::labor_union_id);
person_key leader_for_union(sys::state const&, dcon::labor_union_id);

dcon::employer_association_id create_employer_association(sys::state&, dcon::nation_id);
bool add_employer(sys::state&, dcon::employer_association_id, dcon::organization_id);
bool remove_employer(sys::state&, dcon::employer_association_id, dcon::organization_id);
std::vector<dcon::organization_id> employers(sys::state const&, dcon::employer_association_id);

dcon::collective_bargaining_unit_id request_bargaining(sys::state&, dcon::labor_union_id,
	dcon::organization_id employer, float demand_daily_wage,
	uint8_t protection_level = 2);
dcon::collective_bargaining_unit_id request_association_bargaining(sys::state&,
	dcon::labor_union_id, dcon::employer_association_id, float demand_daily_wage,
	uint8_t protection_level = 2);
bool recognize_by_employer(sys::state&, dcon::collective_bargaining_unit_id);
bool submit_employer_offer(sys::state&, dcon::collective_bargaining_unit_id,
	float daily_wage, uint8_t protection_level);
dcon::collective_agreement_id accept_employer_offer(sys::state&,
	dcon::collective_bargaining_unit_id, sys::date);

dcon::strike_action_id call_strike(sys::state&, dcon::collective_bargaining_unit_id, sys::date);
dcon::strike_action_id declare_lockout(sys::state&, dcon::collective_bargaining_unit_id, sys::date);
dcon::collective_agreement_id resolve_conflict(sys::state&, dcon::strike_action_id,
	float agreed_daily_wage, uint8_t protection_level, sys::date);

bool contract_is_withheld(sys::state const&, uint64_t exact_contract_id, sys::date);
bool on_strike(sys::state const&, person_key, sys::date);
float wage_floor_for_contract(sys::state const&, uint64_t exact_contract_id, sys::date);
float wage_floor_for_employer(sys::state const&, dcon::organization_id employer, sys::date);
float wage_floor_for_workplace(sys::state const&, dcon::organization_id employer,
	dcon::factory_id, sys::date);
void register_new_contract(sys::state&, uint64_t exact_contract_id, sys::date);
void contract_ended(sys::state&, uint64_t exact_contract_id);
uint8_t protection_for_contract(sys::state const&, uint64_t exact_contract_id, sys::date);
float strike_benefit_income(sys::state const&, person_key, sys::date);
bool replacement_hiring_allowed(sys::state const&, dcon::organization_id, dcon::factory_id, sys::date);
bool prepare_contract_termination(sys::state&, uint64_t,
	economy::exact_person_economy::contract_end_reason, sys::date);
float severance_liability_for_actor(sys::state const&, dcon::economic_actor_id,
	dcon::factory_id factory = {});
float expected_severance_for_contract(sys::state const&, uint64_t, sys::date);
float settle_factory_severance_claims(sys::state&, dcon::factory_id,
	dcon::monetary_account_id debtor_account, float limit);
float severance_claim_for_worker(sys::state const&, person_key);
void process(sys::state&);

} // namespace economy::collective_labor
