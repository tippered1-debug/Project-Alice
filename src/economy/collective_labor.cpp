#include "collective_labor.hpp"

#include "actors/organizations/organizations.hpp"
#include "actors/ownership.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/exact_person_economy.hpp"
#include "economy/relations/relations.hpp"
#include "governance/governance.hpp"
#include "governance/law/law.hpp"
#include "governance/policy.hpp"
#include "system_state.hpp"
#include "world/site.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace economy::collective_labor {
namespace policy = governance::policy;
namespace {

constexpr float epsilon = 1.0e-5f;
constexpr uint8_t active_record = 0;
constexpr uint8_t inactive_record = 1;
constexpr uint16_t agreement_term_days = 365;
constexpr int32_t bargaining_period_days = 14;
constexpr int32_t conflict_settlement_days = 35;

sys::date event_date(sys::state const& state, sys::date date = {}) {
	if(date) return date;
	if(state.current_date) return state.current_date;
	return sys::date{0};
}

bool finite_nonnegative(float value) {
	return std::isfinite(value) && value >= 0.0f;
}

dcon::organization_id union_organization(sys::state const& state, dcon::labor_union_id id) {
	return id && state.world.labor_union_is_valid(id)
		? state.world.labor_union_get_organization_from_labor_union_organization(id) : dcon::organization_id{};
}

dcon::organization_id association_organization(sys::state const& state, dcon::employer_association_id id) {
	return id && state.world.employer_association_is_valid(id)
		? state.world.employer_association_get_organization_from_employer_association_organization(id)
		: dcon::organization_id{};
}

dcon::nation_id nation_of_site(sys::state const& state, dcon::site_id site) {
	if(!site || !state.world.site_is_valid(site)) return {};
	auto province = state.world.site_get_province_from_site_location(site);
	return province ? state.world.province_get_nation_from_province_ownership(province) : dcon::nation_id{};
}

dcon::nation_id nation_of_employer(sys::state const& state, dcon::organization_id employer) {
	dcon::nation_id result{};
	for(auto factory : actors::organizations::factories_operated_by(state, employer)) {
		auto site = world::site::site_for_factory(state, factory);
		auto nation = nation_of_site(state, site);
		if(!nation) continue;
		if(result && result != nation) return {};
		result = nation;
	}
	return result;
}

dcon::nation_id nation_of_person(sys::state const& state, person_key person) {
	auto home = persons::home_site(state, person);
	if(auto nation = nation_of_site(state, home)) return nation;
	for(auto id : exact_person_economy::active_contracts_for_person(state, person)) {
		auto contract = exact_person_economy::contract(state, id);
		if(contract) {
			auto nation = nation_of_site(state, contract->workplace);
			if(nation) return nation;
		}
	}
	return {};
}

std::vector<dcon::organization_id> association_employers(sys::state const& state,
	dcon::employer_association_id association) {
	std::vector<dcon::organization_id> result;
	if(!association || !state.world.employer_association_is_valid(association)) return result;
	state.world.employer_association_for_each_employer_association_membership_association_as_association(
		association, [&](auto relation) {
			auto membership = state.world.employer_association_membership_association_get_membership(relation);
			if(state.world.employer_association_membership_get_status(membership) != active_record) return;
			auto employer = state.world.employer_association_membership_get_employer_from_employer_association_membership_employer(membership);
			if(employer) result.push_back(employer);
		});
	std::sort(result.begin(), result.end(), [](auto a, auto b) { return a.index() < b.index(); });
	result.erase(std::unique(result.begin(), result.end()), result.end());
	return result;
}

std::vector<dcon::organization_id> employers_for_unit(sys::state const& state,
	dcon::collective_bargaining_unit_id unit) {
	std::vector<dcon::organization_id> result;
	if(!unit || !state.world.collective_bargaining_unit_is_valid(unit)) return result;
	auto employer = state.world.collective_bargaining_unit_get_employer_organization(unit);
	if(employer) result.push_back(employer);
	else result = association_employers(state,
		state.world.collective_bargaining_unit_get_employer_association_id(unit));
	return result;
}

bool employer_in_association(sys::state const& state, dcon::employer_association_id association,
	dcon::organization_id employer) {
	auto members = association_employers(state, association);
	return std::find(members.begin(), members.end(), employer) != members.end();
}

bool agreement_matches_employer(sys::state const& state, dcon::collective_agreement_id agreement,
	dcon::organization_id employer) {
	if(state.world.collective_agreement_get_employer_organization(agreement) == employer) return true;
	auto association = state.world.collective_agreement_get_employer_association_id(agreement);
	return association && employer_in_association(state, association, employer);
}

bool agreement_effective(sys::state const& state, dcon::collective_agreement_id agreement, sys::date date) {
	if(!agreement || !state.world.collective_agreement_is_valid(agreement)
		|| state.world.collective_agreement_get_status(agreement) != active_record) return false;
	auto starts = state.world.collective_agreement_get_effective_from(agreement);
	auto expires = state.world.collective_agreement_get_expires_on(agreement);
	return !(starts && date < starts) && !(expires && !(date < expires));
}

policy::collective_bargaining_mode bargaining_mode(sys::state const& state, dcon::nation_id nation,
	sys::date date) {
	auto value = nation ? governance::law::effective_topic(state, governance::national(nation),
		policy::topic_id::collective_bargaining, date) : std::nullopt;
	if(auto category = value ? std::get_if<policy::category_value>(&*value) : nullptr;
		category && category->value <= uint16_t(policy::collective_bargaining_mode::sectoral_recognition))
		return policy::collective_bargaining_mode(category->value);
	return policy::collective_bargaining_mode::voluntary_recognition;
}

float legal_wage_floor(sys::state const& state, dcon::nation_id nation, sys::date date) {
	if(!nation) return 0.0f;
	auto value = governance::law::effective_topic(state, governance::national(nation),
		policy::topic_id::minimum_wage, date);
	if(auto wage = value ? std::get_if<float>(&*value) : nullptr) return std::max(0.0f, *wage);
	return 0.0f;
}

uint8_t legal_protection(sys::state const& state, dcon::nation_id nation, sys::date date) {
	if(!nation) return uint8_t(policy::labor_protection_level::basic);
	auto value = governance::law::effective_topic(state, governance::national(nation),
		policy::topic_id::labor_protection, date);
	if(auto level = value ? std::get_if<int32_t>(&*value) : nullptr)
		return uint8_t(std::clamp(*level, int32_t(0), int32_t(3)));
	return uint8_t(policy::labor_protection_level::basic);
}

bool membership_active(sys::state const& state, dcon::union_membership_id membership) {
	if(!membership || !state.world.union_membership_is_valid(membership)
		|| state.world.union_membership_get_status(membership) != active_record) return false;
	person_key person{state.world.union_membership_get_source_population_cell(membership),
		state.world.union_membership_get_ordinal(membership)};
	return persons::exists(state, person) && persons::alive(state, person);
}

person_key membership_person(sys::state const& state, dcon::union_membership_id membership) {
	return {state.world.union_membership_get_source_population_cell(membership),
		state.world.union_membership_get_ordinal(membership)};
}

std::vector<uint64_t> contracts_for_employers(sys::state const& state,
	std::vector<dcon::organization_id> const& employers) {
	std::vector<uint64_t> result;
	for(auto employer : employers) {
		auto actor = actors::organizations::actor_for_organization(state, employer);
		if(!actor) continue;
		for(auto factory : actors::organizations::factories_operated_by(state, employer))
			for(auto id : exact_person_economy::active_contracts_for_factory(state, factory)) {
				auto record = exact_person_economy::contract(state, id);
				if(record && record->employer == actor) result.push_back(id);
			}
	}
	std::sort(result.begin(), result.end());
	result.erase(std::unique(result.begin(), result.end()), result.end());
	return result;
}

float contract_daily_rate(exact_person_economy::contract_record const& contract) {
	if(!std::isfinite(contract.wage_rate) || contract.wage_rate < 0.0f || contract.pay_period_days == 0) return 0.0f;
	return contract.wage_rate / float(contract.pay_period_days);
}

float unit_average_daily_rate(sys::state const& state, dcon::collective_bargaining_unit_id unit) {
	auto ids = contracts_for_employers(state, employers_for_unit(state, unit));
	if(ids.empty()) return 0.0f;
	double total = 0.0;
	for(auto id : ids) {
		auto record = exact_person_economy::contract(state, id);
		if(record) total += std::max(contract_daily_rate(*record), wage_floor_for_contract(state, id, state.current_date));
	}
	return float(total / ids.size());
}

std::vector<uint64_t> member_contracts(sys::state const& state,
	dcon::collective_bargaining_unit_id unit, bool union_only) {
	auto ids = contracts_for_employers(state, employers_for_unit(state, unit));
	if(!union_only) return ids;
	auto union_id = state.world.collective_bargaining_unit_get_union_id(unit);
	ids.erase(std::remove_if(ids.begin(), ids.end(), [&](uint64_t id) {
		auto record = exact_person_economy::contract(state, id);
		return !record || !is_union_member(state, union_id, record->worker);
	}), ids.end());
	return ids;
}

struct recognition_tally {
	uint32_t members = 0;
	uint32_t workers = 0;
};

recognition_tally tally_unit(sys::state const& state, dcon::collective_bargaining_unit_id unit) {
	auto all = contracts_for_employers(state, employers_for_unit(state, unit));
	auto union_id = state.world.collective_bargaining_unit_get_union_id(unit);
	std::vector<person_key> workers;
	std::vector<person_key> members;
	for(auto id : all) {
		auto record = exact_person_economy::contract(state, id);
		if(!record) continue;
		if(std::find(workers.begin(), workers.end(), record->worker) == workers.end())
			workers.push_back(record->worker);
		if(is_union_member(state, union_id, record->worker)
			&& std::find(members.begin(), members.end(), record->worker) == members.end())
			members.push_back(record->worker);
	}
	return {uint32_t(members.size()), uint32_t(workers.size())};
}

bool recognition_threshold_met(sys::state const& state, dcon::collective_bargaining_unit_id unit,
	policy::collective_bargaining_mode mode) {
	auto tally = tally_unit(state, unit);
	if(tally.workers == 0 || tally.members == 0) return false;
	if(mode == policy::collective_bargaining_mode::majority_recognition)
		return uint64_t(tally.members) * 2 >= tally.workers;
	if(mode == policy::collective_bargaining_mode::sectoral_recognition)
		return uint64_t(tally.members) * 5 >= tally.workers;
	return false;
}

void set_recognized(sys::state& state, dcon::collective_bargaining_unit_id unit, sys::date date) {
	if(!state.world.collective_bargaining_unit_get_recognized_on(unit))
		state.world.collective_bargaining_unit_set_recognized_on(unit, date);
	state.world.collective_bargaining_unit_set_status(unit, uint8_t(bargaining_status::bargaining));
	if(!state.world.collective_bargaining_unit_get_bargaining_started_on(unit))
		state.world.collective_bargaining_unit_set_bargaining_started_on(unit, date);
}

bool contract_covered_by(sys::state const& state, uint64_t contract_id,
dcon::collective_agreement_id agreement, sys::date date) {
	bool found = false;
	state.world.for_each_collective_agreement_coverage([&](auto coverage) {
		if(state.world.collective_agreement_coverage_get_exact_contract_id(coverage) == contract_id
			&& state.world.collective_agreement_coverage_get_agreement_id(coverage) == agreement
			&& state.world.collective_agreement_coverage_get_status(coverage) == active_record
			&& agreement_effective(state, agreement, date)) found = true;
	});
	return found;
}

void ensure_contract_coverage(sys::state& state, dcon::collective_agreement_id agreement, sys::date date) {
	if(!agreement_effective(state, agreement, date)) return;
	auto unit = state.world.collective_agreement_get_unit_id(agreement);
	for(auto id : contracts_for_employers(state, employers_for_unit(state, unit))) {
		if(contract_covered_by(state, id, agreement, date)) continue;
		auto record = exact_person_economy::contract(state, id);
		if(!record) continue;
		auto coverage = state.world.create_collective_agreement_coverage();
		state.world.collective_agreement_coverage_set_agreement_id(coverage, agreement);
		state.world.collective_agreement_coverage_set_exact_contract_id(coverage, id);
		state.world.collective_agreement_coverage_set_source_population_cell(coverage, record->worker.source_population_cell);
		state.world.collective_agreement_coverage_set_ordinal(coverage, record->worker.ordinal);
		state.world.collective_agreement_coverage_set_covered_on(coverage, date);
		state.world.collective_agreement_coverage_set_status(coverage, active_record);
		state.world.force_create_collective_agreement_coverage_agreement(coverage, agreement);
	}
}

void close_conflict(sys::state& state, dcon::strike_action_id action, sys::date date) {
	if(!action || !state.world.strike_action_is_valid(action)
		|| state.world.strike_action_get_status(action) != uint8_t(conflict_status::active)) return;
	state.world.strike_action_set_status(action, uint8_t(conflict_status::resolved));
	state.world.strike_action_set_ended_on(action, date);
	auto unit = state.world.strike_action_get_unit_id(action);
	if(unit && state.world.collective_bargaining_unit_get_active_strike_id(unit) == action)
		state.world.collective_bargaining_unit_set_active_strike_id(unit, {});
}

bool can_vote_yes_on_settlement(sys::state const& state, dcon::labor_union_id union_id,
	uint64_t contract_id, float wage, uint8_t protection, uint8_t required_protection) {
	auto record = exact_person_economy::contract(state, contract_id);
	if(!record || !is_union_member(state, union_id, record->worker)) return false;
	auto current_rate = std::max(contract_daily_rate(*record), wage_floor_for_contract(state, contract_id, state.current_date));
	return wage + epsilon >= current_rate || protection >= required_protection || record->unpaid_wages > epsilon;
}

bool union_vote_accepts(sys::state const& state, dcon::collective_bargaining_unit_id unit,
	float wage, uint8_t protection) {
	auto members = member_contracts(state, unit, true);
	std::vector<person_key> voters;
	std::vector<person_key> yes_votes;
	for(auto id : members) {
		auto record = exact_person_economy::contract(state, id);
		if(!record) continue;
		auto person = record->worker;
		if(std::find(voters.begin(), voters.end(), person) == voters.end()) voters.push_back(person);
		if(can_vote_yes_on_settlement(state, state.world.collective_bargaining_unit_get_union_id(unit), id,
			wage, protection, state.world.collective_bargaining_unit_get_demand_protection_level(unit))
			&& std::find(yes_votes.begin(), yes_votes.end(), person) == yes_votes.end())
			yes_votes.push_back(person);
	}
	return !voters.empty() && uint64_t(yes_votes.size()) * 2 > voters.size();
}

dcon::collective_agreement_id record_agreement(sys::state& state,
	dcon::collective_bargaining_unit_id unit, float wage, uint8_t protection, sys::date date) {
	if(!unit || !state.world.collective_bargaining_unit_is_valid(unit)
		|| !std::isfinite(wage) || wage < 0.0f || protection > 3) return {};
	auto union_id = state.world.collective_bargaining_unit_get_union_id(unit);
	auto agreement = state.world.create_collective_agreement();
	state.world.collective_agreement_set_unit_id(agreement, unit);
	state.world.collective_agreement_set_union_id(agreement, union_id);
	state.world.collective_agreement_set_employer_organization(agreement,
		state.world.collective_bargaining_unit_get_employer_organization(unit));
	state.world.collective_agreement_set_employer_association_id(agreement,
		state.world.collective_bargaining_unit_get_employer_association_id(unit));
	state.world.collective_agreement_set_effective_from(agreement, date + 1);
	state.world.collective_agreement_set_expires_on(agreement, date + agreement_term_days);
	state.world.collective_agreement_set_minimum_daily_wage(agreement,
		std::max({wage, legal_wage_floor(state, state.world.labor_union_get_nation(union_id), date),
		wage_floor_for_employer(state,
			state.world.collective_bargaining_unit_get_employer_organization(unit), date)}));
	state.world.collective_agreement_set_protection_level(agreement, protection);
	state.world.collective_agreement_set_severance_days(agreement, uint16_t(protection) * 7);
	state.world.collective_agreement_set_status(agreement, active_record);
	state.world.force_create_collective_agreement_unit(agreement, unit);
	state.world.collective_bargaining_unit_set_status(unit, uint8_t(bargaining_status::agreement));
	state.world.collective_bargaining_unit_set_offer_daily_wage( unit, wage);
	state.world.collective_bargaining_unit_set_offer_protection_level(unit, protection);
	auto active_action = state.world.collective_bargaining_unit_get_active_strike_id(unit);
	close_conflict(state, active_action, date);
	ensure_contract_coverage(state, agreement, date + 1);
	return agreement;
}

dcon::strike_action_id begin_conflict(sys::state& state, dcon::collective_bargaining_unit_id unit,
	conflict_kind kind, sys::date date, uint32_t votes_for, uint32_t votes_against) {
	if(!unit || !state.world.collective_bargaining_unit_is_valid(unit)
		|| state.world.collective_bargaining_unit_get_active_strike_id(unit)) return {};
	auto action = state.world.create_strike_action();
	state.world.strike_action_set_unit_id(action, unit);
	state.world.strike_action_set_union_id(action, state.world.collective_bargaining_unit_get_union_id(unit));
	state.world.strike_action_set_employer_organization(action,
		state.world.collective_bargaining_unit_get_employer_organization(unit));
	state.world.strike_action_set_employer_association_id(action,
		state.world.collective_bargaining_unit_get_employer_association_id(unit));
	state.world.strike_action_set_kind(action, uint8_t(kind));
	state.world.strike_action_set_status(action, uint8_t(conflict_status::active));
	state.world.strike_action_set_started_on(action, date);
	state.world.strike_action_set_votes_for(action, votes_for);
	state.world.strike_action_set_votes_against(action, votes_against);
	state.world.strike_action_set_withheld_labor_today(action, 0.0f);
	state.world.strike_action_set_cumulative_lost_labor(action, 0.0f);
	state.world.strike_action_set_opening_offer_daily_wage(action,
		state.world.collective_bargaining_unit_get_offer_daily_wage(unit));
	state.world.force_create_strike_action_unit(action, unit);
	for(auto contract_id : member_contracts(state, unit, kind == conflict_kind::strike)) {
		auto record = exact_person_economy::contract(state, contract_id);
		if(!record) continue;
		auto member = state.world.create_strike_action_member();
		state.world.strike_action_member_set_strike_id(member, action);
		state.world.strike_action_member_set_exact_contract_id(member, contract_id);
		state.world.strike_action_member_set_source_population_cell(member, record->worker.source_population_cell);
		state.world.strike_action_member_set_ordinal(member, record->worker.ordinal);
		state.world.strike_action_member_set_withheld_labor(member, record->labor_capacity);
		state.world.strike_action_member_set_status(member, active_record);
		state.world.force_create_strike_action_member_action(member, action);
	}
	state.world.collective_bargaining_unit_set_active_strike_id(unit, action);
	state.world.collective_bargaining_unit_set_status(unit, uint8_t(bargaining_status::conflict));
	return action;
}

void collect_union_dues(sys::state& state, dcon::union_membership_id membership, sys::date date) {
	if(!membership_active(state, membership)
		|| state.world.union_membership_get_last_dues_date(membership) == date) return;
	state.world.union_membership_set_last_dues_date(membership, date);
	auto union_id = state.world.union_membership_union_get_labor_union(
		state.world.union_membership_get_union_membership_union(membership));
	if(!union_id || !state.world.labor_union_is_valid(union_id)) return;
	auto person = membership_person(state, membership);
	uint64_t selected_contract = 0;
	for(auto id : exact_person_economy::active_contracts_for_person(state, person)) {
		selected_contract = id;
		break;
	}
	if(!selected_contract) return;
	auto contract = exact_person_economy::contract(state, selected_contract);
	if(!contract) return;
	auto gross = exact_person_economy::wage_due(state, selected_contract);
	if(!(gross > epsilon)) return;
	auto dues_rate = state.world.labor_union_get_dues_rate(union_id);
	if(!std::isfinite(dues_rate) || dues_rate < 0.0f) return;
	auto due_today = gross * dues_rate;
	auto previous = state.world.union_membership_get_dues_arrears(membership);
	if(!finite_nonnegative(previous)) previous = 0.0f;
	auto total_due = previous + due_today;
	auto union_org = union_organization(state, union_id);
	auto settlement = exact_person_economy::settlement_of(state,
	exact_person_economy::account_ref::from_exact(contract->worker_account_id));
	if(!union_org || !settlement || total_due <= epsilon) return;
	auto treasury = actors::organizations::operating_account_for(state, union_org, settlement);
	auto worker_wallet = exact_person_economy::account_ref::from_exact(contract->worker_account_id);
	auto payable = std::min(total_due, exact_person_economy::balance(state, worker_wallet));
	auto result = payable > epsilon
		? exact_person_economy::transfer_with_result(state, worker_wallet,
			exact_person_economy::account_ref::from_dcon(treasury), payable,
			relations::transaction_kind::union_dues, date)
		: exact_person_economy::transfer_result{};
	state.world.union_membership_set_dues_arrears(membership,
		std::max(0.0f, total_due - (result.success ? payable : 0.0f)));
}

void pay_strike_benefit(sys::state& state, dcon::strike_action_id action,
	dcon::strike_action_member_id member, sys::date date) {
	if(state.world.strike_action_get_kind(action) != uint8_t(conflict_kind::strike)
		|| state.world.strike_action_member_get_status(member) != active_record) return;
	auto union_id = state.world.strike_action_get_union_id(action);
	auto person = person_key{state.world.strike_action_member_get_source_population_cell(member),
		state.world.strike_action_member_get_ordinal(member)};
	if(!is_union_member(state, union_id, person)) return;
	auto record = exact_person_economy::contract(state,
		state.world.strike_action_member_get_exact_contract_id(member));
	if(!record || record->pay_period_days == 0) return;
	auto rate = std::max(contract_daily_rate(*record),
	wage_floor_for_contract(state, record->id, date));
	auto amount = rate * record->labor_capacity
		* std::clamp(state.world.labor_union_get_strike_benefit_rate(union_id), 0.0f, 1.0f);
	if(!(amount > epsilon)) return;
	auto union_org = union_organization(state, union_id);
	auto wallet = exact_person_economy::account_ref::from_exact(record->worker_account_id);
	auto settlement = exact_person_economy::settlement_of(state, wallet);
	if(!union_org || !settlement) return;
	auto treasury = actors::organizations::operating_account_for(state, union_org, settlement);
	auto payable = std::min(amount,
	exact_person_economy::balance(state, exact_person_economy::account_ref::from_dcon(treasury)));
	if(payable > epsilon)
		(void)exact_person_economy::transfer_with_result(state,
			exact_person_economy::account_ref::from_dcon(treasury), wallet, payable,
			relations::transaction_kind::strike_benefit, date);
}

void refresh_conflict_day(sys::state& state, dcon::strike_action_id action, sys::date date) {
	float labor = 0.0f;
	state.world.strike_action_set_withheld_labor_today(action, 0.0f);
	state.world.strike_action_for_each_strike_action_member_action_as_strike(action, [&](auto relation) {
		if(state.world.strike_action_member_action_get_strike(relation) != action) return;
		auto member = state.world.strike_action_member_action_get_member(relation);
		if(state.world.strike_action_member_get_status(member) != active_record) return;
		auto record = exact_person_economy::contract(state,
			state.world.strike_action_member_get_exact_contract_id(member));
		if(!record || record->status != exact_person_economy::contract_status::active
			|| !persons::alive(state, record->worker)
			|| record->start_date && date < record->start_date
			|| record->end_date && !(date < record->end_date)) return;
		labor += std::max(0.0f, record->labor_capacity);
		pay_strike_benefit(state, action, member, date);
	});
	if(!std::isfinite(labor)) labor = 0.0f;
	state.world.strike_action_set_withheld_labor_today(action, labor);
	state.world.strike_action_set_cumulative_lost_labor(
		action, state.world.strike_action_get_cumulative_lost_labor(action) + labor);
}

} // namespace

dcon::labor_union_id create_union(sys::state& state, dcon::nation_id nation,
	float dues_rate, float strike_benefit_rate) {
	if(!nation || !state.world.nation_is_valid(nation)
		|| !std::isfinite(dues_rate) || dues_rate < 0.0f || dues_rate > 0.25f
		|| !std::isfinite(strike_benefit_rate) || strike_benefit_rate < 0.0f || strike_benefit_rate > 1.0f
		|| bargaining_mode(state, nation, state.current_date) == policy::collective_bargaining_mode::prohibited) return {};
	auto organization = actors::organizations::create_organization(state, actors::ownership::actor_kind::labor_union);
	actors::ownership::assign_runtime_canonical_id(state, organization);
	auto id = state.world.create_labor_union();
	state.world.labor_union_set_nation(id, nation);
	state.world.labor_union_set_dues_rate(id, dues_rate);
	state.world.labor_union_set_strike_benefit_rate(id, strike_benefit_rate);
	state.world.labor_union_set_formed_on(id, event_date(state));
	state.world.labor_union_set_status(id, active_record);
	state.world.force_create_labor_union_organization(id, organization);
	return id;
}

bool join_union(sys::state& state, dcon::labor_union_id union_id, person_key person, sys::date joined_on) {
	if(!union_id || !state.world.labor_union_is_valid(union_id)
		|| state.world.labor_union_get_status(union_id) != active_record
		|| !persons::exists(state, person) || !persons::alive(state, person)
		|| nation_of_person(state, person) != state.world.labor_union_get_nation(union_id)
		|| bargaining_mode(state, state.world.labor_union_get_nation(union_id), state.current_date)
			== policy::collective_bargaining_mode::prohibited) return false;
	if(auto current = union_for_member(state, person); current) return current == union_id;
	auto membership = state.world.create_union_membership();
	state.world.union_membership_set_source_population_cell(membership, person.source_population_cell);
	state.world.union_membership_set_ordinal(membership, person.ordinal);
	state.world.union_membership_set_joined_on(membership, event_date(state, joined_on));
	state.world.union_membership_set_last_dues_date(membership, {});
	state.world.union_membership_set_dues_arrears(membership, 0.0f);
	state.world.union_membership_set_status(membership, active_record);
	state.world.force_create_union_membership_union(membership, union_id);
	return true;
}

bool leave_union(sys::state& state, dcon::labor_union_id union_id, person_key person, sys::date left_on) {
	if(!union_id || !state.world.labor_union_is_valid(union_id)) return false;
	bool changed = false;
	state.world.labor_union_for_each_union_membership_union_as_labor_union(union_id, [&](auto relation) {
		auto membership = state.world.union_membership_union_get_membership(relation);
		if(membership_active(state, membership) && membership_person(state, membership) == person) {
			state.world.union_membership_set_status(membership, inactive_record);
			state.world.union_membership_set_left_on(membership, event_date(state, left_on));
			changed = true;
		}
	});
	return changed;
}

bool is_union_member(sys::state const& state, dcon::labor_union_id union_id, person_key person) {
	if(!union_id || !state.world.labor_union_is_valid(union_id)) return false;
	bool found = false;
	state.world.labor_union_for_each_union_membership_union_as_labor_union(union_id, [&](auto relation) {
		auto membership = state.world.union_membership_union_get_membership(relation);
		found = found || membership_active(state, membership) && membership_person(state, membership) == person;
	});
	return found;
}

dcon::labor_union_id union_for_member(sys::state const& state, person_key person) {
	dcon::labor_union_id result{};
	state.world.for_each_union_membership([&](dcon::union_membership_id membership) {
		if(!membership_active(state, membership) || membership_person(state, membership) != person) return;
		auto relation = state.world.union_membership_get_union_membership_union(membership);
		auto union_id = state.world.union_membership_union_get_labor_union(relation);
		if(!result || union_id.index() < result.index()) result = union_id;
	});
	return result;
}

uint32_t member_count(sys::state const& state, dcon::labor_union_id union_id) {
	uint32_t count = 0;
	if(!union_id || !state.world.labor_union_is_valid(union_id)) return count;
	state.world.labor_union_for_each_union_membership_union_as_labor_union(union_id, [&](auto relation) {
		if(membership_active(state, state.world.union_membership_union_get_membership(relation))) ++count;
	});
	return count;
}

dcon::employer_association_id create_employer_association(sys::state& state, dcon::nation_id nation) {
	if(!nation || !state.world.nation_is_valid(nation)) return {};
	auto organization = actors::organizations::create_organization(state,
		actors::ownership::actor_kind::employer_association);
	actors::ownership::assign_runtime_canonical_id(state, organization);
	auto id = state.world.create_employer_association();
	state.world.employer_association_set_nation(id, nation);
	state.world.employer_association_set_formed_on(id, event_date(state));
	state.world.employer_association_set_status(id, active_record);
	state.world.force_create_employer_association_organization(id, organization);
	return id;
}

bool add_employer(sys::state& state, dcon::employer_association_id association,
	dcon::organization_id employer) {
	if(!association || !state.world.employer_association_is_valid(association)
		|| state.world.employer_association_get_status(association) != active_record
		|| !employer || !state.world.organization_is_valid(employer)
		|| actors::organizations::factories_operated_by(state, employer).empty()
		|| nation_of_employer(state, employer) != state.world.employer_association_get_nation(association)) return false;
	for(auto existing : employers(state, association)) if(existing == employer) return false;
	auto membership = state.world.create_employer_association_membership();
	state.world.employer_association_membership_set_employer_organization(membership, employer);
	state.world.employer_association_membership_set_joined_on(membership, event_date(state));
	state.world.employer_association_membership_set_status(membership, active_record);
	state.world.force_create_employer_association_membership_association(membership, association);
	state.world.force_create_employer_association_membership_employer(membership, employer);
	return true;
}

bool remove_employer(sys::state& state, dcon::employer_association_id association,
	dcon::organization_id employer) {
	if(!association || !employer) return false;
	bool changed = false;
	state.world.employer_association_for_each_employer_association_membership_association_as_association(
		association, [&](auto relation) {
			auto membership = state.world.employer_association_membership_association_get_membership(relation);
			if(state.world.employer_association_membership_get_employer_organization(membership) == employer
				&& state.world.employer_association_membership_get_status(membership) == active_record) {
				state.world.employer_association_membership_set_status(membership, inactive_record);
				changed = true;
			}
		});
	return changed;
}

std::vector<dcon::organization_id> employers(sys::state const& state,
	dcon::employer_association_id association) {
	return association_employers(state, association);
}

dcon::collective_bargaining_unit_id request_bargaining(sys::state& state,
	dcon::labor_union_id union_id, dcon::organization_id employer,
	float demand_daily_wage, uint8_t protection_level) {
	if(!union_id || !state.world.labor_union_is_valid(union_id) || !employer
		|| !state.world.organization_is_valid(employer) || !std::isfinite(demand_daily_wage)
		|| demand_daily_wage < 0.0f || protection_level > 3
		|| nation_of_employer(state, employer) != state.world.labor_union_get_nation(union_id)
		|| actors::organizations::factories_operated_by(state, employer).empty()
		|| !member_count(state, union_id)) return {};
	for(auto existing : state.world.in_collective_bargaining_unit) {
		if(state.world.collective_bargaining_unit_get_union_id(existing) == union_id
			&& state.world.collective_bargaining_unit_get_employer_organization(existing) == employer
			&& state.world.collective_bargaining_unit_get_status(existing) != uint8_t(bargaining_status::inactive)) return {};
	}
	if(bargaining_mode(state, state.world.labor_union_get_nation(union_id), state.current_date)
		== policy::collective_bargaining_mode::prohibited) return {};
	auto unit = state.world.create_collective_bargaining_unit();
	state.world.collective_bargaining_unit_set_union_id(unit, union_id);
	state.world.collective_bargaining_unit_set_employer_organization(unit, employer);
	state.world.collective_bargaining_unit_set_employer_association_id(unit, {});
	state.world.collective_bargaining_unit_set_status(unit, uint8_t(bargaining_status::awaiting_recognition));
	state.world.collective_bargaining_unit_set_created_on(unit, event_date(state));
	state.world.collective_bargaining_unit_set_demand_daily_wage(unit, demand_daily_wage);
	state.world.collective_bargaining_unit_set_offer_daily_wage(unit,
		unit_average_daily_rate(state, unit));
	state.world.collective_bargaining_unit_set_demand_protection_level(unit, protection_level);
	state.world.collective_bargaining_unit_set_offer_protection_level(unit,
		legal_protection(state, state.world.labor_union_get_nation(union_id), state.current_date));
	return unit;
}

dcon::collective_bargaining_unit_id request_association_bargaining(sys::state& state,
	dcon::labor_union_id union_id, dcon::employer_association_id association,
	float demand_daily_wage, uint8_t protection_level) {
	if(!union_id || !state.world.labor_union_is_valid(union_id) || !association
		|| !state.world.employer_association_is_valid(association)
		|| state.world.labor_union_get_nation(union_id) != state.world.employer_association_get_nation(association)
		|| !std::isfinite(demand_daily_wage) || demand_daily_wage < 0.0f || protection_level > 3
		|| employers(state, association).empty() || !member_count(state, union_id)
		|| bargaining_mode(state, state.world.labor_union_get_nation(union_id), state.current_date)
			== policy::collective_bargaining_mode::prohibited) return {};
	for(auto existing : state.world.in_collective_bargaining_unit)
		if(state.world.collective_bargaining_unit_get_union_id(existing) == union_id
			&& state.world.collective_bargaining_unit_get_employer_association_id(existing) == association
			&& state.world.collective_bargaining_unit_get_status(existing) != uint8_t(bargaining_status::inactive)) return {};
	auto unit = state.world.create_collective_bargaining_unit();
	state.world.collective_bargaining_unit_set_union_id(unit, union_id);
	state.world.collective_bargaining_unit_set_employer_organization(unit, {});
	state.world.collective_bargaining_unit_set_employer_association_id(unit, association);
	state.world.collective_bargaining_unit_set_status(unit, uint8_t(bargaining_status::awaiting_recognition));
	state.world.collective_bargaining_unit_set_created_on(unit, event_date(state));
	state.world.collective_bargaining_unit_set_demand_daily_wage(unit, demand_daily_wage);
	state.world.collective_bargaining_unit_set_offer_daily_wage(unit,
		unit_average_daily_rate(state, unit));
	state.world.collective_bargaining_unit_set_demand_protection_level(unit, protection_level);
	state.world.collective_bargaining_unit_set_offer_protection_level(unit,
		legal_protection(state, state.world.labor_union_get_nation(union_id), state.current_date));
	return unit;
}

bool recognize_by_employer(sys::state& state, dcon::collective_bargaining_unit_id unit) {
	if(!unit || !state.world.collective_bargaining_unit_is_valid(unit)
		|| state.world.collective_bargaining_unit_get_status(unit) != uint8_t(bargaining_status::awaiting_recognition)) return false;
	auto union_id = state.world.collective_bargaining_unit_get_union_id(unit);
	auto mode = bargaining_mode(state, state.world.labor_union_get_nation(union_id), state.current_date);
	if(mode == policy::collective_bargaining_mode::prohibited || tally_unit(state, unit).members == 0) return false;
	if(mode != policy::collective_bargaining_mode::voluntary_recognition
		&& !recognition_threshold_met(state, unit, mode)) return false;
	set_recognized(state, unit, event_date(state));
	return true;
}

bool submit_employer_offer(sys::state& state, dcon::collective_bargaining_unit_id unit,
	float daily_wage, uint8_t protection_level) {
	if(!unit || !state.world.collective_bargaining_unit_is_valid(unit)
		|| !std::isfinite(daily_wage) || daily_wage < 0.0f || protection_level > 3) return false;
	auto status = bargaining_status(state.world.collective_bargaining_unit_get_status(unit));
	if(status != bargaining_status::recognized && status != bargaining_status::bargaining) return false;
	auto union_id = state.world.collective_bargaining_unit_get_union_id(unit);
	if(bargaining_mode(state, state.world.labor_union_get_nation(union_id), state.current_date)
		== policy::collective_bargaining_mode::prohibited) return false;
	state.world.collective_bargaining_unit_set_offer_daily_wage(unit, daily_wage);
	state.world.collective_bargaining_unit_set_offer_protection_level(unit, protection_level);
	state.world.collective_bargaining_unit_set_status(unit, uint8_t(bargaining_status::bargaining));
	if(!state.world.collective_bargaining_unit_get_bargaining_started_on(unit))
		state.world.collective_bargaining_unit_set_bargaining_started_on(unit, event_date(state));
	return true;
}

dcon::collective_agreement_id accept_employer_offer(sys::state& state,
	dcon::collective_bargaining_unit_id unit, sys::date date) {
	if(!unit || !state.world.collective_bargaining_unit_is_valid(unit)
		|| bargaining_status(state.world.collective_bargaining_unit_get_status(unit)) != bargaining_status::bargaining) return {};
	auto wage = state.world.collective_bargaining_unit_get_offer_daily_wage(unit);
	auto protection = state.world.collective_bargaining_unit_get_offer_protection_level(unit);
	if(!union_vote_accepts(state, unit, wage, protection)) return {};
	return record_agreement(state, unit, wage, protection, event_date(state, date));
}

dcon::strike_action_id call_strike(sys::state& state,
	dcon::collective_bargaining_unit_id unit, sys::date date) {
	if(!unit || !state.world.collective_bargaining_unit_is_valid(unit)
		|| bargaining_status(state.world.collective_bargaining_unit_get_status(unit)) != bargaining_status::bargaining) return {};
	auto union_id = state.world.collective_bargaining_unit_get_union_id(unit);
	if(bargaining_mode(state, state.world.labor_union_get_nation(union_id), state.current_date)
		== policy::collective_bargaining_mode::prohibited) return {};
	auto demand = state.world.collective_bargaining_unit_get_demand_daily_wage(unit);
	auto offer = state.world.collective_bargaining_unit_get_offer_daily_wage(unit);
	auto required_protection = state.world.collective_bargaining_unit_get_demand_protection_level(unit);
	auto offered_protection = state.world.collective_bargaining_unit_get_offer_protection_level(unit);
	if(demand <= offer + epsilon && offered_protection >= required_protection) return {};
	auto voters = member_contracts(state, unit, true);
	std::vector<person_key> voter_people;
	std::vector<person_key> yes_people;
	for(auto id : voters) {
		auto contract = exact_person_economy::contract(state, id);
		if(!contract) continue;
		auto person = contract->worker;
		if(std::find(voter_people.begin(), voter_people.end(), person) == voter_people.end())
			voter_people.push_back(person);
		auto rate = std::max(contract_daily_rate(*contract), wage_floor_for_contract(state, id, state.current_date));
		if(demand > rate + epsilon || contract->unpaid_wages > epsilon
			|| required_protection > offered_protection) {
			if(std::find(yes_people.begin(), yes_people.end(), person) == yes_people.end())
				yes_people.push_back(person);
		}
	}
	if(voter_people.empty() || uint64_t(yes_people.size()) * 2 <= voter_people.size()) return {};
	return begin_conflict(state, unit, conflict_kind::strike, event_date(state, date),
		uint32_t(yes_people.size()), uint32_t(voter_people.size()) - uint32_t(yes_people.size()));
}

dcon::strike_action_id declare_lockout(sys::state& state,
	dcon::collective_bargaining_unit_id unit, sys::date date) {
	if(!unit || !state.world.collective_bargaining_unit_is_valid(unit)
		|| bargaining_status(state.world.collective_bargaining_unit_get_status(unit)) != bargaining_status::bargaining) return {};
	uint32_t workers = uint32_t(contracts_for_employers(state, employers_for_unit(state, unit)).size());
	if(workers == 0) return {};
	return begin_conflict(state, unit, conflict_kind::lockout, event_date(state, date), 0, 0);
}

dcon::collective_agreement_id resolve_conflict(sys::state& state, dcon::strike_action_id action,
	float agreed_daily_wage, uint8_t protection_level, sys::date date) {
	if(!action || !state.world.strike_action_is_valid(action)
		|| state.world.strike_action_get_status(action) != uint8_t(conflict_status::active)
		|| !std::isfinite(agreed_daily_wage) || agreed_daily_wage < 0.0f || protection_level > 3) return {};
	auto unit = state.world.strike_action_get_unit_id(action);
	if(!union_vote_accepts(state, unit, agreed_daily_wage, protection_level)) return {};
	state.world.collective_bargaining_unit_set_offer_daily_wage(unit, agreed_daily_wage);
	state.world.collective_bargaining_unit_set_offer_protection_level(unit, protection_level);
	return record_agreement(state, unit, agreed_daily_wage, protection_level, event_date(state, date));
}

bool contract_is_withheld(sys::state const& state, uint64_t exact_contract_id, sys::date date) {
	if(exact_contract_id == 0) return false;
	bool withheld = false;
	state.world.for_each_strike_action_member([&](dcon::strike_action_member_id member) {
		if(withheld || state.world.strike_action_member_get_status(member) != active_record
			|| state.world.strike_action_member_get_exact_contract_id(member) != exact_contract_id) return;
		auto action = state.world.strike_action_member_get_strike_id(member);
		if(action && state.world.strike_action_is_valid(action)
			&& state.world.strike_action_get_status(action) == uint8_t(conflict_status::active)
			&& !(date < state.world.strike_action_get_started_on(action))) withheld = true;
	});
	return withheld;
}

bool on_strike(sys::state const& state, person_key person, sys::date date) {
	for(auto id : exact_person_economy::active_contracts_for_person(state, person))
		if(contract_is_withheld(state, id, date)) return true;
	return false;
}

float wage_floor_for_contract(sys::state const& state, uint64_t exact_contract_id, sys::date date) {
	float result = 0.0f;
	if(exact_contract_id == 0) return result;
	state.world.for_each_collective_agreement_coverage([&](dcon::collective_agreement_coverage_id coverage) {
		if(state.world.collective_agreement_coverage_get_status(coverage) != active_record
			|| state.world.collective_agreement_coverage_get_exact_contract_id(coverage) != exact_contract_id) return;
		auto agreement = state.world.collective_agreement_coverage_get_agreement_id(coverage);
		if(agreement_effective(state, agreement, date))
			result = std::max(result, state.world.collective_agreement_get_minimum_daily_wage(agreement));
	});
	return result;
}

float wage_floor_for_employer(sys::state const& state, dcon::organization_id employer, sys::date date) {
	float result = 0.0f;
	if(!employer) return result;
	state.world.for_each_collective_agreement([&](dcon::collective_agreement_id agreement) {
		if(agreement_effective(state, agreement, date) && agreement_matches_employer(state, agreement, employer))
			result = std::max(result, state.world.collective_agreement_get_minimum_daily_wage(agreement));
	});
	return result;
}

uint8_t protection_for_contract(sys::state const& state, uint64_t exact_contract_id, sys::date date) {
	uint8_t result = 0;
	state.world.for_each_collective_agreement_coverage([&](dcon::collective_agreement_coverage_id coverage) {
		if(state.world.collective_agreement_coverage_get_status(coverage) != active_record
			|| state.world.collective_agreement_coverage_get_exact_contract_id(coverage) != exact_contract_id) return;
		auto agreement = state.world.collective_agreement_coverage_get_agreement_id(coverage);
		if(agreement_effective(state, agreement, date))
			result = std::max(result, state.world.collective_agreement_get_protection_level(agreement));
	});
	return result;
}

float strike_benefit_income(sys::state const& state, person_key person, sys::date date) {
	float result = 0.0f;
	for(auto const& transaction : exact_person_economy::transaction_records(state))
		if(transaction.kind == relations::transaction_kind::strike_benefit && transaction.timestamp == date
			&& transaction.destination.kind == exact_person_economy::account_kind::exact
			&& exact_person_economy::owner_of(state, transaction.destination) == person)
			result += transaction.amount;
	return std::isfinite(result) ? result : 0.0f;
}

void process(sys::state& state) {
	if(!state.exact_person_economy) return;
	auto date = event_date(state);
	std::vector<dcon::union_membership_id> memberships;
	state.world.for_each_union_membership([&](auto item) { memberships.push_back(item); });
	for(auto membership : memberships) collect_union_dues(state, membership, date);

	std::vector<dcon::collective_bargaining_unit_id> units;
	state.world.for_each_collective_bargaining_unit([&](auto item) { units.push_back(item); });
	for(auto unit : units) {
		auto status = bargaining_status(state.world.collective_bargaining_unit_get_status(unit));
		if(status == bargaining_status::inactive || status == bargaining_status::agreement
			|| status == bargaining_status::conflict) continue;
		auto union_id = state.world.collective_bargaining_unit_get_union_id(unit);
		if(!union_id || !state.world.labor_union_is_valid(union_id)) continue;
		auto nation = state.world.labor_union_get_nation(union_id);
		auto mode = bargaining_mode(state, nation, date);
		if(mode == policy::collective_bargaining_mode::prohibited) continue;
		if(mode != policy::collective_bargaining_mode::voluntary_recognition
			&& recognition_threshold_met(state, unit, mode)) set_recognized(state, unit, date);
		status = bargaining_status(state.world.collective_bargaining_unit_get_status(unit));
		if(status == bargaining_status::awaiting_recognition || status == bargaining_status::recognized) continue;
		if(state.world.collective_bargaining_unit_get_demand_daily_wage(unit)
			<= state.world.collective_bargaining_unit_get_offer_daily_wage(unit) + epsilon
			&& state.world.collective_bargaining_unit_get_offer_protection_level(unit)
			>= state.world.collective_bargaining_unit_get_demand_protection_level(unit)) {
			(void)accept_employer_offer(state, unit, date);
			continue;
		}
		auto started = state.world.collective_bargaining_unit_get_bargaining_started_on(unit);
		if(started && date >= started + bargaining_period_days)
			(void)call_strike(state, unit, date);
	}

	std::vector<dcon::strike_action_id> actions;
	state.world.for_each_strike_action([&](auto item) { actions.push_back(item); });
	for(auto action : actions) {
		if(state.world.strike_action_get_status(action) != uint8_t(conflict_status::active)) continue;
		refresh_conflict_day(state, action, date);
		auto started = state.world.strike_action_get_started_on(action);
		if(started && date >= started + conflict_settlement_days) {
			auto unit = state.world.strike_action_get_unit_id(action);
			auto opening = state.world.strike_action_get_opening_offer_daily_wage(action);
			auto demand = state.world.collective_bargaining_unit_get_demand_daily_wage(unit);
			auto elapsed_days = date.to_raw_value() - started.to_raw_value();
			auto progress = std::clamp(float(elapsed_days) / float(conflict_settlement_days), 0.0f, 1.0f);
			auto offer = opening + (demand - opening) * progress;
			state.world.collective_bargaining_unit_set_offer_daily_wage(unit, offer);
			state.world.collective_bargaining_unit_set_offer_protection_level(unit,
				std::max(state.world.collective_bargaining_unit_get_offer_protection_level(unit),
					state.world.collective_bargaining_unit_get_demand_protection_level(unit)));
			state.world.collective_bargaining_unit_set_status(unit, uint8_t(bargaining_status::bargaining));
			if(offer + epsilon >= demand) {
				auto agreement = accept_employer_offer(state, unit, date);
				if(!agreement) close_conflict(state, action, date);
			}
		}
	}

	std::vector<dcon::collective_agreement_id> agreements;
	state.world.for_each_collective_agreement([&](auto item) { agreements.push_back(item); });
	for(auto agreement : agreements) {
		auto expires = state.world.collective_agreement_get_expires_on(agreement);
		if(expires && !(date < expires)) {
			state.world.collective_agreement_set_status(agreement, inactive_record);
			auto unit = state.world.collective_agreement_get_unit_id(agreement);
			if(unit && state.world.collective_bargaining_unit_is_valid(unit)) {
				state.world.collective_bargaining_unit_set_status(unit,
					uint8_t(bargaining_status::bargaining));
				state.world.collective_bargaining_unit_set_bargaining_started_on(unit, date);
			}
			continue;
		}
		ensure_contract_coverage(state, agreement, date);
	}
}

} // namespace economy::collective_labor
