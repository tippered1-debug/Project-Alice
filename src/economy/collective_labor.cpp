#include "collective_labor.hpp"

#include "actors/organizations/organizations.hpp"
#include "actors/ownership.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/economy_stats.hpp"
#include "economy/exact_person_economy.hpp"
#include "economy/firm_agency.hpp"
#include "economy/physical/labor_dynamics.hpp"
#include "economy/physical/job_market.hpp"
#include "economy/relations/relations.hpp"
#include "governance/governance.hpp"
#include "governance/law/law.hpp"
#include "governance/policy.hpp"
#include "system_state.hpp"
#include "persons/exact_population.hpp"
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
constexpr int32_t workplace_review_days = 30;
constexpr int32_t negotiation_review_days = 7;

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

dcon::nation_id nation_of_factory(sys::state const& state, dcon::factory_id factory) {
	return nation_of_site(state, world::site::site_for_factory(state, factory));
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
	auto unit = state.world.collective_agreement_get_unit_id(agreement);
	if(state.world.collective_agreement_get_employer_organization(agreement) == employer)
		return !state.world.collective_bargaining_unit_get_workplace_factory(unit);
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

uint16_t contractual_severance_days(sys::state const& state, uint64_t exact_contract_id,
	sys::date date) {
	uint16_t days = 0;
	state.world.for_each_collective_agreement_coverage([&](dcon::collective_agreement_coverage_id coverage) {
		if(state.world.collective_agreement_coverage_get_status(coverage) != active_record
			|| state.world.collective_agreement_coverage_get_exact_contract_id(coverage) != exact_contract_id) return;
		auto agreement = state.world.collective_agreement_coverage_get_agreement_id(coverage);
		if(agreement_effective(state, agreement, date))
			days = std::max(days, state.world.collective_agreement_get_severance_days(agreement));
	});
	return days;
}

uint16_t statutory_severance_days(sys::state const& state,
	exact_person_economy::contract_record const& record, sys::date date) {
	auto protection = legal_protection(state, nation_of_site(state, record.workplace), date);
	switch(policy::labor_protection_level(protection)) {
	case policy::labor_protection_level::basic: return 7;
	case policy::labor_protection_level::due_process: return 14;
	case policy::labor_protection_level::just_cause: return 21;
	case policy::labor_protection_level::none: return 0;
	}
	return 0;
}

bool membership_active(sys::state const& state, dcon::union_membership_id membership) {
	if(!membership || !state.world.union_membership_is_valid(membership)
		|| state.world.union_membership_get_status(membership) != active_record) return false;
	auto relation = state.world.union_membership_get_union_membership_union(membership);
	auto union_id = state.world.union_membership_union_get_labor_union(relation);
	if(!union_id || !state.world.labor_union_is_valid(union_id)
		|| state.world.labor_union_get_status(union_id) != active_record) return false;
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

std::vector<uint64_t> contracts_for_unit(sys::state const& state,
	dcon::collective_bargaining_unit_id unit) {
	auto result = contracts_for_employers(state, employers_for_unit(state, unit));
	auto workplace = state.world.collective_bargaining_unit_get_workplace_factory(unit);
	if(workplace)
		result.erase(std::remove_if(result.begin(), result.end(), [&](uint64_t id) {
			auto record = exact_person_economy::contract(state, id);
			return !record || record->factory != workplace;
		}), result.end());
	return result;
}

bool union_covers_contract(sys::state const& state, dcon::labor_union_id union_id,
	exact_person_economy::contract_record const& record) {
	if(!union_id || !state.world.labor_union_is_valid(union_id)
		|| !record.factory || !state.world.factory_is_valid(record.factory)) return false;
	if(nation_of_site(state, record.workplace) != state.world.labor_union_get_nation(union_id)) return false;
	auto scope = union_scope(state.world.labor_union_get_scope(union_id));
	if(scope == union_scope::workplace)
		return state.world.labor_union_get_scope_workplace(union_id) == record.factory;
	if(scope == union_scope::employer)
		return state.world.labor_union_get_scope_employer(union_id)
			== actors::organizations::organization_for_actor(state, record.employer);
	auto sector = state.world.labor_union_get_scope_factory_type(union_id);
	return !sector || state.world.factory_get_building_type(record.factory) == sector;
}

float contract_daily_rate(exact_person_economy::contract_record const& contract) {
	if(!std::isfinite(contract.wage_rate) || contract.wage_rate < 0.0f || contract.pay_period_days == 0) return 0.0f;
	return contract.wage_rate / float(contract.pay_period_days);
}

float unit_average_daily_rate(sys::state const& state, dcon::collective_bargaining_unit_id unit) {
	auto ids = contracts_for_unit(state, unit);
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
	auto ids = contracts_for_unit(state, unit);
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
	auto all = contracts_for_unit(state, unit);
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
	for(auto id : contracts_for_unit(state, unit)) {
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

bool agreement_covers_employer_and_factory(sys::state const& state,
	dcon::collective_agreement_id agreement, dcon::organization_id employer,
	dcon::factory_id factory) {
	auto unit = state.world.collective_agreement_get_unit_id(agreement);
	auto workplace = state.world.collective_bargaining_unit_get_workplace_factory(unit);
	if(workplace && workplace != factory) return false;
	return state.world.collective_agreement_get_employer_organization(agreement) == employer
		|| agreement_matches_employer(state, agreement, employer);
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
	state.world.for_each_collective_agreement([&](auto prior) {
		if(state.world.collective_agreement_get_unit_id(prior) == unit
			&& state.world.collective_agreement_get_status(prior) == active_record)
			state.world.collective_agreement_set_status(prior, inactive_record);
	});
	auto agreement = state.world.create_collective_agreement();
	state.world.collective_agreement_set_unit_id(agreement, unit);
	state.world.collective_agreement_set_union_id(agreement, union_id);
	state.world.collective_agreement_set_representative_profile(agreement,
		state.world.collective_bargaining_unit_get_representative_profile(unit));
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
	if(union_id) {
		state.world.labor_union_set_last_success_on(union_id, date);
		state.world.labor_union_set_success_count(union_id, uint16_t(std::min<uint32_t>(
			std::numeric_limits<uint16_t>::max(),
			uint32_t(state.world.labor_union_get_success_count(union_id)) + 1)));
	}
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

person_key ensure_union_leader(sys::state& state, dcon::labor_union_id union_id) {
	if(!union_id || !state.world.labor_union_is_valid(union_id)) return {};
	person_key current{state.world.labor_union_get_leader_source_population_cell(union_id),
		state.world.labor_union_get_leader_ordinal(union_id)};
	if(current.source_population_cell && is_union_member(state, union_id, current)) {
		auto profile = persons::exact_population::profile_for_person(state, current);
		if(!profile) profile = persons::exact_population::materialize_person_profile(state, current);
		state.world.labor_union_set_leader_profile(union_id, profile);
		return current;
	}
	person_key selected{};
	sys::date selected_on{};
	state.world.labor_union_for_each_union_membership_union_as_labor_union(union_id,
		[&](auto relation) {
			auto membership = state.world.union_membership_union_get_membership(relation);
			if(!membership_active(state, membership)) return;
		auto person = membership_person(state, membership);
			auto joined = state.world.union_membership_get_joined_on(membership);
			if(!selected.source_population_cell || joined < selected_on
				|| (joined == selected_on && person.source_population_cell < selected.source_population_cell)
				|| (joined == selected_on && person.source_population_cell == selected.source_population_cell
					&& person.ordinal < selected.ordinal)) {
				selected = person;
				selected_on = joined;
			}
		});
	state.world.labor_union_set_leader_source_population_cell(union_id, selected.source_population_cell);
	state.world.labor_union_set_leader_ordinal(union_id, selected.ordinal);
	if(selected.source_population_cell) {
		auto profile = persons::exact_population::profile_for_person(state, selected);
		if(!profile) profile = persons::exact_population::materialize_person_profile(state, selected);
		state.world.labor_union_set_leader_profile(union_id, profile);
	} else {
		state.world.labor_union_set_leader_profile(union_id, {});
	}
	return selected;
}

dcon::labor_workplace_observation_id ensure_workplace_observation(sys::state& state,
	dcon::factory_id factory) {
	if(!factory || !state.world.factory_is_valid(factory)) return {};
	auto existing = state.world.factory_get_observation_from_labor_workplace_observation_factory(factory);
	if(existing && state.world.labor_workplace_observation_is_valid(existing)) return existing;
	auto employer = actors::organizations::operator_organization_for_factory(state, factory);
	auto site = world::site::site_for_factory(state, factory);
	auto province = site ? state.world.site_get_province_from_site_location(site) : dcon::province_id{};
	auto type = state.world.factory_get_building_type(factory);
	if(!employer || !province || !type) return {};
	auto observation = state.world.create_labor_workplace_observation();
	state.world.labor_workplace_observation_set_employer_organization(observation, employer);
	state.world.labor_workplace_observation_set_province(observation, province);
	state.world.labor_workplace_observation_set_factory_type(observation, type);
	state.world.labor_workplace_observation_set_status(observation, active_record);
	state.world.force_create_labor_workplace_observation_factory(observation, factory);
	return observation;
}

float person_liquid_assets(sys::state const& state, person_key person) {
	float amount = 0.0f;
	for(auto account : exact_person_economy::accounts_for_person(state, person))
		amount += std::max(0.0f, exact_person_economy::balance(state, account));
	return std::isfinite(amount) ? amount : 0.0f;
}

float bargaining_unit_payroll(sys::state const& state, dcon::collective_bargaining_unit_id unit) {
	float total = 0.0f;
	for(auto id : contracts_for_unit(state, unit)) total += exact_person_economy::wage_due(state, id);
	return std::isfinite(total) ? total : 0.0f;
}

float bargaining_unit_treasury(sys::state const& state, dcon::collective_bargaining_unit_id unit) {
	auto union_id = state.world.collective_bargaining_unit_get_union_id(unit);
	if(!union_id) return 0.0f;
	float total = 0.0f;
	auto union_org = union_organization(state, union_id);
	auto contracts = member_contracts(state, unit, true);
	for(auto id : contracts) {
		auto record = exact_person_economy::contract(state, id);
		if(!record) continue;
		auto settlement = exact_person_economy::settlement_of(state,
			exact_person_economy::account_ref::from_exact(record->worker_account_id));
		if(!settlement || !union_org) continue;
		auto account = actors::organizations::operating_account_for(state, union_org, settlement);
		total = std::max(total, accounts::balance(state, account));
	}
	return std::isfinite(total) ? std::max(0.0f, total) : 0.0f;
}

float strike_daily_benefit_obligation(sys::state const& state, dcon::collective_bargaining_unit_id unit) {
	auto union_id = state.world.collective_bargaining_unit_get_union_id(unit);
	float total = 0.0f;
	for(auto id : member_contracts(state, unit, true)) {
		auto record = exact_person_economy::contract(state, id);
		if(!record) continue;
		auto rate = std::max(contract_daily_rate(*record), wage_floor_for_contract(state, id, state.current_date));
		total += rate * record->labor_capacity
			* std::clamp(state.world.labor_union_get_strike_benefit_rate(union_id), 0.0f, 1.0f);
	}
	return std::isfinite(total) ? total : 0.0f;
}

float union_demand_from_economy(sys::state const& state,
	dcon::collective_bargaining_unit_id unit, sys::date date, bool in_conflict) {
	auto union_id = state.world.collective_bargaining_unit_get_union_id(unit);
	if(!union_id) return 0.0f;
	auto contracts = member_contracts(state, unit, true);
	if(contracts.empty()) return 0.0f;
	double wage_sum = 0.0;
	double capacity_sum = 0.0;
	double arrears = 0.0;
	double liquid_days = 0.0;
	double members = 0.0;
	float profitability = 0.0f;
	for(auto id : contracts) {
		auto record = exact_person_economy::contract(state, id);
		if(!record || record->labor_capacity <= epsilon) continue;
		auto daily = std::max(contract_daily_rate(*record), wage_floor_for_contract(state, id, date));
		wage_sum += double(daily) * record->labor_capacity;
		capacity_sum += record->labor_capacity;
		arrears += std::max(0.0f, record->unpaid_wages);
		liquid_days += std::clamp(person_liquid_assets(state, record->worker)
			/ std::max(daily * record->labor_capacity, epsilon), 0.0f, 90.0f);
		members += 1.0;
		if(record->factory) {
			float payroll = 0.0f;
			for(auto factory_contract : exact_person_economy::active_contracts_for_factory(state, record->factory)) {
				auto worker = exact_person_economy::contract(state, factory_contract);
				if(worker) payroll += contract_daily_rate(*worker) * worker->labor_capacity;
			}
			payroll = std::max(payroll, epsilon);
			profitability = std::max(profitability,
				state.world.factory_get_agency_recent_profit(record->factory) / payroll);
		}
	}
	if(capacity_sum <= epsilon || members <= 0.0) return 0.0f;
	auto base = float(wage_sum / capacity_sum);
	auto arrears_ratio = std::clamp(float(arrears / std::max(wage_sum, double(epsilon))), 0.0f, 1.0f);
	auto member_days = float(liquid_days / members);
	auto durability = in_conflict
		? std::clamp(std::min(member_days / 30.0f,
			bargaining_unit_treasury(state, unit)
				/ std::max(strike_daily_benefit_obligation(state, unit) * 30.0f, epsilon)), 0.0f, 1.0f)
		: 1.0f;
	auto prosperity_share = std::clamp(std::max(0.0f, profitability), 0.0f, 0.6f);
	auto grievance = std::clamp(arrears_ratio * 0.35f + prosperity_share * 0.18f, 0.0f, 0.35f);
	auto uplift = std::clamp(0.02f + grievance + prosperity_share * 0.12f, 0.02f, 0.30f);
	return std::max(base, wage_floor_for_employer(state,
		state.world.collective_bargaining_unit_get_employer_organization(unit), date))
		* (1.0f + uplift * durability);
}

uint8_t protection_demand_from_economy(sys::state const& state,
	dcon::collective_bargaining_unit_id unit, sys::date date) {
	auto union_id = state.world.collective_bargaining_unit_get_union_id(unit);
	auto required = legal_protection(state, state.world.labor_union_get_nation(union_id), date);
	float arrears = 0.0f;
	uint32_t members = 0;
	for(auto id : member_contracts(state, unit, true)) {
		auto record = exact_person_economy::contract(state, id);
		if(!record) continue;
		++members;
		if(record->unpaid_wages > epsilon) arrears += 1.0f;
	}
	if(members && arrears / float(members) >= 0.2f)
		required = std::max(required, uint8_t(policy::labor_protection_level::due_process));
	if(members && arrears / float(members) >= 0.6f)
		required = std::max(required, uint8_t(policy::labor_protection_level::just_cause));
	return std::min<uint8_t>(required, 3);
}

float employer_max_offer_from_economy(sys::state const& state,
	dcon::collective_bargaining_unit_id unit, sys::date date) {
	auto employers = employers_for_unit(state, unit);
	if(employers.empty()) return 0.0f;
	float weighted_wage = 0.0f;
	float weighted_capacity = 0.0f;
	float total_profit = 0.0f;
	float total_payroll = 0.0f;
	float financial_capacity = 1.0f;
	for(auto employer : employers) {
		for(auto factory : actors::organizations::factories_operated_by(state, employer)) {
			auto ids = exact_person_economy::active_contracts_for_factory(state, factory);
			float capacity = 0.0f;
			float payroll = 0.0f;
			for(auto id : ids) {
				auto record = exact_person_economy::contract(state, id);
				if(!record) continue;
				capacity += record->labor_capacity;
				payroll += std::max(contract_daily_rate(*record),
					wage_floor_for_contract(state, id, date)) * record->labor_capacity;
				weighted_wage += std::max(contract_daily_rate(*record),
					wage_floor_for_contract(state, id, date)) * record->labor_capacity;
				weighted_capacity += record->labor_capacity;
			}
			if(capacity <= epsilon) continue;
			auto profit = state.world.factory_get_agency_recent_profit(factory);
			total_profit += profit;
			total_payroll += payroll;
			financial_capacity = std::min(financial_capacity,
				std::clamp(1.0f + profit / std::max(payroll, 1.0f), 0.25f, 1.25f));
		}
	}
	if(weighted_capacity <= epsilon) return 0.0f;
	auto base = weighted_wage / weighted_capacity;
	auto margin_room = std::clamp(total_profit / std::max(total_payroll, epsilon), -0.50f, 0.50f);
	auto action = state.world.collective_bargaining_unit_get_active_strike_id(unit);
	auto strike_cost = action && state.world.strike_action_is_valid(action)
		? state.world.strike_action_get_lost_revenue_today(action) / weighted_capacity : 0.0f;
	// Margin and actual foregone contribution can fund a concession; cash stress
	// limits how much the firm can sustain. Neither term moves merely with time.
	auto increase = base * std::clamp(std::max(0.0f, margin_room) * 0.18f
		+ std::min(0.20f, strike_cost / std::max(base, epsilon) * 0.15f), 0.0f, 0.25f)
		* financial_capacity;
	return base + increase;
}

void refresh_strike_lost_revenue(sys::state& state, dcon::strike_action_id action) {
	std::vector<std::pair<dcon::factory_id, float>> withheld;
	state.world.strike_action_for_each_strike_action_member_action_as_strike(action, [&](auto relation) {
		auto member = state.world.strike_action_member_action_get_member(relation);
		auto record = exact_person_economy::contract(state,
			state.world.strike_action_member_get_exact_contract_id(member));
		if(!record || !record->factory || state.world.strike_action_member_get_status(member) != active_record) return;
		auto found = std::find_if(withheld.begin(), withheld.end(), [&](auto const& item) {
			return item.first == record->factory;
		});
		if(found == withheld.end()) withheld.push_back({record->factory,
			std::max(0.0f, record->labor_capacity)});
		else found->second += std::max(0.0f, record->labor_capacity);
	});
	float lost_revenue = 0.0f;
	for(auto const& [factory, absent_labor] : withheld) {
		auto total_labor = 0.0f;
		for(auto id : exact_person_economy::active_contracts_for_factory(state, factory)) {
			auto record = exact_person_economy::contract(state, id);
			if(record) total_labor += std::max(0.0f, record->labor_capacity);
		}
		if(total_labor <= epsilon) continue;
		auto decision = firm_agency::decide_factory(state, factory);
		lost_revenue += std::max(0.0f, decision.expected_unit_revenue)
			* std::max(0.0f, decision.desired_output)
			* std::clamp(absent_labor / total_labor, 0.0f, 1.0f);
	}
	if(!std::isfinite(lost_revenue)) lost_revenue = 0.0f;
	state.world.strike_action_set_lost_revenue_today(action, lost_revenue);
	state.world.strike_action_set_cumulative_lost_revenue(action,
		state.world.strike_action_get_cumulative_lost_revenue(action) + lost_revenue);
}

float contract_local_wage_gap(sys::state const& state,
	exact_person_economy::contract_record const& record) {
	if(!record.workplace || !state.world.site_is_valid(record.workplace) || record.pay_period_days == 0) return 0.0f;
	auto province = state.world.site_get_province_from_site_location(record.workplace);
	auto lane = record.occupation == 0 ? economy::labor::no_education
		: record.occupation == 1 ? economy::labor::basic_education
		: economy::labor::high_education;
	auto alternative = province ? state.world.province_get_labor_price(province, lane) : 0.0f;
	auto daily = contract_daily_rate(record);
	return alternative > epsilon
		? std::clamp((alternative - daily) / alternative, 0.0f, 1.0f) : 0.0f;
}

dcon::labor_union_id workplace_union(sys::state const& state, dcon::factory_id factory) {
	dcon::labor_union_id result{};
	if(!factory || !state.world.factory_is_valid(factory)) return result;
	auto nation = nation_of_factory(state, factory);
	auto employer = actors::organizations::operator_organization_for_factory(state, factory);
	auto factory_type = state.world.factory_get_building_type(factory);
	int best_score = -1;
	state.world.for_each_labor_union([&](dcon::labor_union_id union_id) {
		if(state.world.labor_union_get_nation(union_id) != nation) return;
		auto scope = union_scope(state.world.labor_union_get_scope(union_id));
		int specificity = 0;
		if(scope == union_scope::workplace
			&& state.world.labor_union_get_scope_workplace(union_id) == factory) specificity = 3;
		else if(scope == union_scope::employer
			&& state.world.labor_union_get_scope_employer(union_id) == employer) specificity = 2;
		else if(scope == union_scope::sectoral) {
			auto sector = state.world.labor_union_get_scope_factory_type(union_id);
			if(!sector || sector == factory_type) specificity = 1;
		}
		if(specificity == 0) return;
		auto active = state.world.labor_union_get_status(union_id) == active_record ? 1 : 0;
		auto score = specificity * 2 + active;
		if(score > best_score) {
			best_score = score;
			result = union_id;
		}
	});
	return result;
}

void review_workplace(sys::state& state, dcon::labor_workplace_observation_id observation,
	dcon::factory_id factory, sys::date date) {
	if(!observation || !factory) return;
	auto contracts = exact_person_economy::active_contracts_for_factory(state, factory);
	state.world.labor_workplace_observation_set_reviewed_on(observation, date);
	if(contracts.empty()) {
		state.world.labor_workplace_observation_set_status(observation, inactive_record);
		if(auto local_union = workplace_union(state, factory); local_union && member_count(state, local_union) == 0)
			state.world.labor_union_set_status(local_union, inactive_record);
		return;
	}
	state.world.labor_workplace_observation_set_status(observation, active_record);
	float wage_sum = 0.0f, capacity = 0.0f, payroll = 0.0f, arrears = 0.0f, local_gap = 0.0f;
	std::vector<person_key> workers;
	for(auto id : contracts) {
		auto record = exact_person_economy::contract(state, id);
		if(!record || !persons::alive(state, record->worker)) continue;
		auto worker_capacity = std::max(0.0f, record->labor_capacity);
		capacity += worker_capacity;
		wage_sum += contract_daily_rate(*record) * worker_capacity;
		payroll += exact_person_economy::wage_due(state, id);
		arrears += std::max(0.0f, record->unpaid_wages);
		local_gap += contract_local_wage_gap(state, *record);
		if(std::find(workers.begin(), workers.end(), record->worker) == workers.end()) workers.push_back(record->worker);
	}
	if(workers.empty() || capacity <= epsilon) return;
	auto average_wage = wage_sum / capacity;
	auto profit = state.world.factory_get_agency_recent_profit(factory);
	auto previous_wage = state.world.labor_workplace_observation_get_average_daily_wage(observation);
	auto deterioration = previous_wage > epsilon
		? std::clamp((previous_wage - average_wage) / previous_wage, 0.0f, 1.0f) : 0.0f;
	auto rent_share = std::clamp(std::max(0.0f, profit) / std::max(payroll, epsilon), 0.0f, 0.75f);
	auto wage_gap = local_gap / float(workers.size());
	uint32_t recent_layoffs = 0;
	auto last_event = state.world.labor_workplace_observation_get_last_separation_event_id(observation);
	auto event_count = physical::labor_dynamics::separation_event_count(state);
	for(auto id = last_event + 1; id <= event_count; ++id) {
		auto event = physical::labor_dynamics::separation_event_at(state, id);
		if(event && event->factory == factory
			&& event->reason == physical::labor_dynamics::separation_reason::employer_layoff
			&& date.to_raw_value() - event->date.to_raw_value() <= workplace_review_days) ++recent_layoffs;
	}
	state.world.labor_workplace_observation_set_last_separation_event_id(observation, event_count);
	state.world.labor_workplace_observation_set_previous_average_daily_wage(observation, average_wage);
	state.world.labor_workplace_observation_set_average_daily_wage(observation, average_wage);
	state.world.labor_workplace_observation_set_previous_recent_profit(observation, profit);
	state.world.labor_workplace_observation_set_recent_profit(observation, profit);
	auto common_grievance = std::clamp(0.06f
		+ std::clamp(arrears / std::max(payroll, epsilon), 0.0f, 1.0f) * 0.34f
		+ std::min(1.0f, float(recent_layoffs) / float(workers.size())) * 0.28f
		+ wage_gap * 0.18f + deterioration * 0.12f + rent_share * 0.65f, 0.0f, 1.0f);
	auto nation = nation_of_factory(state, factory);
	if(!nation || bargaining_mode(state, nation, date) == policy::collective_bargaining_mode::prohibited) {
		if(auto existing = workplace_union(state, factory)) {
			state.world.labor_union_set_status(existing, inactive_record);
			state.world.labor_union_for_each_union_membership_union_as_labor_union(existing, [&](auto relation) {
				auto membership = state.world.union_membership_union_get_membership(relation);
				if(state.world.union_membership_get_status(membership) == active_record) {
					state.world.union_membership_set_status(membership, inactive_record);
					state.world.union_membership_set_left_on(membership, date);
				}
			});
		}
		return;
	}
	auto union_id = workplace_union(state, factory);
	if(union_id && state.world.labor_union_get_status(union_id) != active_record)
		state.world.labor_union_set_status(union_id, active_record);
	std::vector<person_key> interested;
	for(auto id : contracts) {
		auto record = exact_person_economy::contract(state, id);
		if(!record) continue;
		auto person = record->worker;
		auto current_union = union_for_member(state, person);
		if(current_union && current_union != union_id) {
			bool represented_elsewhere = false;
			for(auto other_id : exact_person_economy::active_contracts_for_person(state, person)) {
				auto other = exact_person_economy::contract(state, other_id);
				if(other && union_covers_contract(state, current_union, *other)) represented_elsewhere = true;
			}
			if(!represented_elsewhere) (void)leave_union(state, current_union, person, date);
		}
		auto daily_income = std::max(contract_daily_rate(*record), wage_floor_for_contract(state, id, date))
			* std::max(0.0f, record->labor_capacity);
		auto arrears_pressure = record->unpaid_wages > epsilon
			? std::clamp(record->unpaid_wages / std::max(daily_income, epsilon), 0.0f, 1.0f) * 0.24f : 0.0f;
		auto liquid_days = person_liquid_assets(state, person) / std::max(daily_income, epsilon);
		auto security = liquid_days > 60.0f ? 0.10f : liquid_days < 2.0f ? -0.03f : 0.0f;
		auto covered = wage_floor_for_contract(state, id, date) > epsilon;
		auto free_rider = covered && !is_union_member(state, union_id, person) ? 0.16f : 0.0f;
		auto density = workers.empty() ? 0.0f : std::clamp(
			float(member_count(state, union_id)) / float(workers.size()), 0.0f, 1.0f);
		auto success = union_id ? std::min(0.20f,
			float(state.world.labor_union_get_success_count(union_id)) * 0.04f) : 0.0f;
		auto failure = union_id ? std::min(0.22f,
			float(state.world.labor_union_get_failure_count(union_id)) * 0.05f) : 0.0f;
		auto dues = union_id ? state.world.labor_union_get_dues_rate(union_id) : 0.02f;
		if(common_grievance + arrears_pressure + density * 0.24f + success + security
			- failure - dues * 2.0f - free_rider >= 0.30f) interested.push_back(person);
	}
	if(!union_id && interested.size() >= std::max<size_t>(2, (workers.size() + 2) / 3)) {
		union_id = create_workplace_union(state, factory);
		if(union_id) for(auto person : interested) (void)join_union(state, union_id, person, date);
	}
	if(!union_id || !state.world.labor_union_is_valid(union_id)) return;
	for(auto person : interested) if(!is_union_member(state, union_id, person))
		(void)join_union(state, union_id, person, date);
	for(auto id : contracts) {
		auto record = exact_person_economy::contract(state, id);
		if(record && is_union_member(state, union_id, record->worker)
			&& common_grievance < 0.16f && record->unpaid_wages <= epsilon
			&& state.world.labor_union_get_failure_count(union_id) > 0)
			(void)leave_union(state, union_id, record->worker, date);
	}
	if(member_count(state, union_id) == 0) {
		if(date.to_raw_value() - state.world.labor_union_get_formed_on(union_id).to_raw_value() > 180)
			state.world.labor_union_set_status(union_id, inactive_record);
		return;
	}
	(void)ensure_union_leader(state, union_id);
	if(state.world.labor_union_get_scope(union_id) == uint8_t(union_scope::workplace)) {
		auto employer = state.world.labor_workplace_observation_get_employer_organization(observation);
		std::vector<person_key> employer_workers;
		uint32_t represented = 0;
		for(auto plant : actors::organizations::factories_operated_by(state, employer))
			for(auto id : exact_person_economy::active_contracts_for_factory(state, plant)) {
				auto record = exact_person_economy::contract(state, id);
				if(!record) continue;
				if(std::find(employer_workers.begin(), employer_workers.end(), record->worker) == employer_workers.end())
					employer_workers.push_back(record->worker);
				if(is_union_member(state, union_id, record->worker)) ++represented;
			}
		if(employer_workers.size() > workers.size() && represented * 2 >= employer_workers.size()) {
			state.world.labor_union_set_scope(union_id, uint8_t(union_scope::employer));
			state.world.labor_union_set_scope_employer(union_id, employer);
		}
	}
	if(bargaining_mode(state, nation, date) == policy::collective_bargaining_mode::sectoral_recognition
		&& state.world.labor_union_get_scope(union_id) != uint8_t(union_scope::sectoral)) {
		uint32_t exposed_employers = 0;
		std::vector<dcon::organization_id> seen_employers;
		state.world.for_each_labor_workplace_observation([&](auto other_observation) {
			auto relation = state.world.labor_workplace_observation_get_labor_workplace_observation_factory(other_observation);
			auto other_factory = relation ? state.world.labor_workplace_observation_factory_get_factory(relation) : dcon::factory_id{};
			if(other_factory == factory || nation_of_factory(state, other_factory) != nation
				|| state.world.labor_workplace_observation_get_factory_type(other_observation)
					!= state.world.labor_union_get_scope_factory_type(union_id)) return;
			auto other_employer = state.world.labor_workplace_observation_get_employer_organization(other_observation);
			if(std::find(seen_employers.begin(), seen_employers.end(), other_employer) != seen_employers.end()) return;
			float other_payroll = 0.0f;
			for(auto id : exact_person_economy::active_contracts_for_factory(state, other_factory)) {
				auto record = exact_person_economy::contract(state, id);
				if(record) other_payroll += contract_daily_rate(*record) * record->labor_capacity;
			}
			if(other_payroll <= epsilon
				|| state.world.factory_get_agency_recent_profit(other_factory) / other_payroll < 0.20f) return;
			seen_employers.push_back(other_employer);
		});
		exposed_employers = uint32_t(seen_employers.size());
		if(exposed_employers > 0) state.world.labor_union_set_scope(union_id,
			uint8_t(union_scope::sectoral));
	}
}

bool association_has_live_agreement(sys::state const& state,
	dcon::employer_association_id association, sys::date date) {
	bool result = false;
	state.world.for_each_collective_agreement([&](auto agreement) {
		if(state.world.collective_agreement_get_employer_association_id(agreement) == association
			&& agreement_effective(state, agreement, date)) result = true;
	});
	return result;
}

void review_employer_associations(sys::state& state, sys::date date) {
	struct sector_group { dcon::nation_id nation{}; dcon::factory_type_id type{}; std::vector<dcon::organization_id> employers; };
	std::vector<sector_group> groups;
	state.world.for_each_labor_workplace_observation([&](auto observation) {
		if(state.world.labor_workplace_observation_get_status(observation) != active_record) return;
		auto relation = state.world.labor_workplace_observation_get_labor_workplace_observation_factory(observation);
		auto factory = relation ? state.world.labor_workplace_observation_factory_get_factory(relation) : dcon::factory_id{};
		auto nation = nation_of_factory(state, factory);
		auto type = state.world.labor_workplace_observation_get_factory_type(observation);
		if(!nation || !type) return;
		bool union_exposure = false;
		for(auto id : exact_person_economy::active_contracts_for_factory(state, factory)) {
			auto record = exact_person_economy::contract(state, id);
			auto union_id = record ? union_for_member(state, record->worker) : dcon::labor_union_id{};
			if(record && union_id && union_covers_contract(state, union_id, *record)) union_exposure = true;
		}
		if(!union_exposure) return;
		auto employer = state.world.labor_workplace_observation_get_employer_organization(observation);
		auto it = std::find_if(groups.begin(), groups.end(), [&](auto const& group) {
			return group.nation == nation && group.type == type;
		});
		if(it == groups.end()) groups.push_back({nation, type, {employer}});
		else if(std::find(it->employers.begin(), it->employers.end(), employer) == it->employers.end())
			it->employers.push_back(employer);
	});
	for(auto const& group : groups) {
		if(group.employers.size() < 2) continue;
		dcon::employer_association_id association{};
		state.world.for_each_employer_association([&](auto candidate) {
			if(!association && state.world.employer_association_get_status(candidate) == active_record
				&& state.world.employer_association_get_nation(candidate) == group.nation
				&& state.world.employer_association_get_factory_type(candidate) == group.type)
				association = candidate;
		});
		if(!association) association = create_employer_association(state, group.nation);
		if(!association) continue;
		state.world.employer_association_set_factory_type(association, group.type);
		for(auto employer : group.employers)
			if(!employer_in_association(state, association, employer)) (void)add_employer(state, association, employer);
	}
	state.world.for_each_employer_association([&](auto association) {
		if(state.world.employer_association_get_status(association) != active_record) return;
		auto members = employers(state, association);
		if(!association_has_live_agreement(state, association, date))
			for(auto employer : members) {
				bool exposed = false;
				for(auto factory : actors::organizations::factories_operated_by(state, employer))
					for(auto id : exact_person_economy::active_contracts_for_factory(state, factory)) {
						auto record = exact_person_economy::contract(state, id);
						if(record && union_for_member(state, record->worker)) exposed = true;
					}
				if(!exposed) (void)remove_employer(state, association, employer);
			}
		if(employers(state, association).empty() && !association_has_live_agreement(state, association, date))
			state.world.employer_association_set_status(association, inactive_record);
	});
}

void ensure_autonomous_bargaining_units(sys::state& state, sys::date date) {
	std::vector<std::pair<dcon::labor_union_id, dcon::organization_id>> represented;
	state.world.for_each_union_membership([&](auto membership) {
		if(!membership_active(state, membership)) return;
		auto relation = state.world.union_membership_get_union_membership_union(membership);
		auto union_id = state.world.union_membership_union_get_labor_union(relation);
		for(auto id : exact_person_economy::active_contracts_for_person(state, membership_person(state, membership))) {
			auto record = exact_person_economy::contract(state, id);
			if(!record || !union_covers_contract(state, union_id, *record)) continue;
			auto employer = actors::organizations::organization_for_actor(state, record->employer);
			if(employer && std::find(represented.begin(), represented.end(), std::pair{union_id, employer}) == represented.end())
				represented.push_back({union_id, employer});
		}
	});
	for(auto const& [union_id, employer] : represented) {
		auto nation = state.world.labor_union_get_nation(union_id);
		if(bargaining_mode(state, nation, date) == policy::collective_bargaining_mode::prohibited) continue;
		bool belongs_to_active_sector_association = false;
		if(state.world.labor_union_get_scope(union_id) == uint8_t(union_scope::sectoral))
			state.world.for_each_employer_association([&](auto association) {
				if(state.world.employer_association_get_status(association) == active_record
					&& state.world.employer_association_get_nation(association) == nation
					&& state.world.employer_association_get_factory_type(association)
						== state.world.labor_union_get_scope_factory_type(union_id)
					&& employer_in_association(state, association, employer))
					belongs_to_active_sector_association = true;
			});
		if(belongs_to_active_sector_association) continue;
		bool has_unit = false;
		for(auto unit : state.world.in_collective_bargaining_unit)
			if(state.world.collective_bargaining_unit_get_union_id(unit) == union_id
				&& state.world.collective_bargaining_unit_get_employer_organization(unit) == employer
				&& state.world.collective_bargaining_unit_get_status(unit) != uint8_t(bargaining_status::inactive))
				has_unit = true;
		if(!has_unit) (void)request_bargaining(state, union_id, employer, 0.0f,
			legal_protection(state, nation, date));
	}
	state.world.for_each_employer_association([&](auto association) {
		if(state.world.employer_association_get_status(association) != active_record) return;
		auto nation = state.world.employer_association_get_nation(association);
		if(bargaining_mode(state, nation, date) != policy::collective_bargaining_mode::sectoral_recognition) return;
		state.world.for_each_labor_union([&](auto union_id) {
			if(state.world.labor_union_get_status(union_id) != active_record
				|| state.world.labor_union_get_nation(union_id) != nation
				|| state.world.labor_union_get_scope(union_id) != uint8_t(union_scope::sectoral)
				|| state.world.labor_union_get_scope_factory_type(union_id)
					!= state.world.employer_association_get_factory_type(association)
				|| !member_count(state, union_id)) return;
			bool represented = false;
			for(auto employer : employers(state, association))
				for(auto factory : actors::organizations::factories_operated_by(state, employer))
					for(auto id : exact_person_economy::active_contracts_for_factory(state, factory)) {
						auto record = exact_person_economy::contract(state, id);
						if(record && is_union_member(state, union_id, record->worker)) represented = true;
					}
			if(!represented) return;
			bool exists = false;
			for(auto unit : state.world.in_collective_bargaining_unit)
				if(state.world.collective_bargaining_unit_get_union_id(unit) == union_id
					&& state.world.collective_bargaining_unit_get_employer_association_id(unit) == association
					&& state.world.collective_bargaining_unit_get_status(unit) != uint8_t(bargaining_status::inactive))
					exists = true;
			if(!exists) (void)request_association_bargaining(state, union_id, association, 0.0f,
				legal_protection(state, nation, date));
		});
	});
}

} // namespace

dcon::labor_union_id create_union(sys::state& state, dcon::nation_id nation,
	float dues_rate, float strike_benefit_rate, union_scope scope) {
	if(!nation || !state.world.nation_is_valid(nation)
		|| !std::isfinite(dues_rate) || dues_rate < 0.0f || dues_rate > 0.25f
		|| !std::isfinite(strike_benefit_rate) || strike_benefit_rate < 0.0f || strike_benefit_rate > 1.0f
		|| uint8_t(scope) > uint8_t(union_scope::sectoral)
		|| bargaining_mode(state, nation, state.current_date) == policy::collective_bargaining_mode::prohibited) return {};
	auto organization = actors::organizations::create_organization(state, actors::ownership::actor_kind::labor_union);
	actors::ownership::assign_runtime_canonical_id(state, organization);
	auto id = state.world.create_labor_union();
	state.world.labor_union_set_nation(id, nation);
	state.world.labor_union_set_scope(id, uint8_t(scope));
	state.world.labor_union_set_dues_rate(id, dues_rate);
	state.world.labor_union_set_strike_benefit_rate(id, strike_benefit_rate);
	state.world.labor_union_set_formed_on(id, event_date(state));
	state.world.labor_union_set_status(id, active_record);
	state.world.force_create_labor_union_organization(id, organization);
	return id;
}

dcon::labor_union_id create_workplace_union(sys::state& state, dcon::factory_id factory,
	float dues_rate, float strike_benefit_rate) {
	if(!factory || !state.world.factory_is_valid(factory)) return {};
	auto employer = actors::organizations::operator_organization_for_factory(state, factory);
	auto nation = nation_of_factory(state, factory);
	if(!employer || !nation || !state.world.factory_get_building_type(factory)) return {};
	auto id = create_union(state, nation, dues_rate, strike_benefit_rate);
	if(!id) return {};
	state.world.labor_union_set_scope(id, uint8_t(union_scope::workplace));
	state.world.labor_union_set_scope_employer(id, employer);
	state.world.labor_union_set_scope_workplace(id, factory);
	state.world.labor_union_set_scope_factory_type(id, state.world.factory_get_building_type(factory));
	return id;
}

bool join_union(sys::state& state, dcon::labor_union_id union_id, person_key person, sys::date joined_on) {
	if(!union_id || !state.world.labor_union_is_valid(union_id)
		|| state.world.labor_union_get_status(union_id) != active_record
		|| !persons::exists(state, person) || !persons::alive(state, person)
		|| nation_of_person(state, person) != state.world.labor_union_get_nation(union_id)
		|| bargaining_mode(state, state.world.labor_union_get_nation(union_id), state.current_date)
			== policy::collective_bargaining_mode::prohibited) return false;
	if(state.world.labor_union_get_scope(union_id) == uint8_t(union_scope::workplace)
		&& !state.world.labor_union_get_scope_workplace(union_id)) {
		for(auto contract_id : exact_person_economy::active_contracts_for_person(state, person)) {
			auto record = exact_person_economy::contract(state, contract_id);
			if(!record || !record->factory) continue;
			state.world.labor_union_set_scope_workplace(union_id, record->factory);
			state.world.labor_union_set_scope_employer(union_id,
				actors::organizations::organization_for_actor(state, record->employer));
			state.world.labor_union_set_scope_factory_type(union_id,
				state.world.factory_get_building_type(record->factory));
			break;
		}
	} else if(state.world.labor_union_get_scope(union_id) == uint8_t(union_scope::employer)
		&& !state.world.labor_union_get_scope_employer(union_id)) {
		for(auto contract_id : exact_person_economy::active_contracts_for_person(state, person)) {
			auto record = exact_person_economy::contract(state, contract_id);
			if(!record || !record->factory) continue;
			state.world.labor_union_set_scope_employer(union_id,
				actors::organizations::organization_for_actor(state, record->employer));
			state.world.labor_union_set_scope_factory_type(union_id,
				state.world.factory_get_building_type(record->factory));
			break;
		}
	} else if(state.world.labor_union_get_scope(union_id) == uint8_t(union_scope::sectoral)
		&& !state.world.labor_union_get_scope_factory_type(union_id)) {
		for(auto contract_id : exact_person_economy::active_contracts_for_person(state, person)) {
			auto record = exact_person_economy::contract(state, contract_id);
			if(!record || !record->factory) continue;
			state.world.labor_union_set_scope_factory_type(union_id,
				state.world.factory_get_building_type(record->factory));
			break;
		}
	}
	bool represented = false;
	for(auto contract_id : exact_person_economy::active_contracts_for_person(state, person)) {
		auto record = exact_person_economy::contract(state, contract_id);
		if(record && union_covers_contract(state, union_id, *record)) { represented = true; break; }
	}
	if(!represented) return false;
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

person_key leader_for_union(sys::state const& state, dcon::labor_union_id union_id) {
	if(!union_id || !state.world.labor_union_is_valid(union_id)) return {};
	return {state.world.labor_union_get_leader_source_population_cell(union_id),
		state.world.labor_union_get_leader_ordinal(union_id)};
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
	auto association_type = state.world.employer_association_get_factory_type(association);
	bool matching_sector = false;
	for(auto factory : actors::organizations::factories_operated_by(state, employer))
		if(!association_type || state.world.factory_get_building_type(factory) == association_type)
			matching_sector = true;
	if(!matching_sector) return false;
	if(!association_type) {
		for(auto factory : actors::organizations::factories_operated_by(state, employer)) {
			auto type = state.world.factory_get_building_type(factory);
			if(type) { state.world.employer_association_set_factory_type(association, type); break; }
		}
	}
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
	if(!association || !employer || association_has_live_agreement(state, association, event_date(state))) return false;
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
	(void)ensure_union_leader(state, union_id);
	auto unit = state.world.create_collective_bargaining_unit();
	state.world.collective_bargaining_unit_set_union_id(unit, union_id);
	state.world.collective_bargaining_unit_set_representative_profile(unit,
		state.world.labor_union_get_leader_profile(union_id));
	state.world.collective_bargaining_unit_set_employer_organization(unit, employer);
	state.world.collective_bargaining_unit_set_employer_association_id(unit, {});
	state.world.collective_bargaining_unit_set_workplace_factory(unit,
		state.world.labor_union_get_scope(union_id) == uint8_t(union_scope::workplace)
			? state.world.labor_union_get_scope_workplace(union_id) : dcon::factory_id{});
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
	auto scope_type = state.world.labor_union_get_scope_factory_type(union_id);
	auto association_type = state.world.employer_association_get_factory_type(association);
	if(scope_type && association_type && scope_type != association_type) return {};
	bool member_employed_by_association = false;
	for(auto employer : employers(state, association))
		for(auto factory : actors::organizations::factories_operated_by(state, employer))
			for(auto id : exact_person_economy::active_contracts_for_factory(state, factory)) {
				auto record = exact_person_economy::contract(state, id);
				if(record && union_covers_contract(state, union_id, *record)
					&& is_union_member(state, union_id, record->worker)) member_employed_by_association = true;
			}
	if(!member_employed_by_association) return {};
	(void)ensure_union_leader(state, union_id);
	for(auto existing : state.world.in_collective_bargaining_unit)
		if(state.world.collective_bargaining_unit_get_union_id(existing) == union_id
			&& state.world.collective_bargaining_unit_get_employer_association_id(existing) == association
			&& state.world.collective_bargaining_unit_get_status(existing) != uint8_t(bargaining_status::inactive)) return {};
	auto unit = state.world.create_collective_bargaining_unit();
	state.world.collective_bargaining_unit_set_union_id(unit, union_id);
	state.world.collective_bargaining_unit_set_representative_profile(unit,
		state.world.labor_union_get_leader_profile(union_id));
	state.world.collective_bargaining_unit_set_employer_organization(unit, {});
	state.world.collective_bargaining_unit_set_employer_association_id(unit, association);
	state.world.collective_bargaining_unit_set_workplace_factory(unit, {});
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
	auto daily_fund_cost = strike_daily_benefit_obligation(state, unit);
	auto fund_runway = daily_fund_cost > epsilon
		? bargaining_unit_treasury(state, unit) / daily_fund_cost : 0.0f;
	float mean_liquidity_days = 0.0f;
	uint32_t liquidity_count = 0;
	for(auto id : voters) {
		auto record = exact_person_economy::contract(state, id);
		if(!record) continue;
		auto daily = std::max(contract_daily_rate(*record), wage_floor_for_contract(state, id, state.current_date))
			* std::max(record->labor_capacity, epsilon);
		mean_liquidity_days += std::clamp(person_liquid_assets(state, record->worker)
			/ std::max(daily, epsilon), 0.0f, 90.0f);
		++liquidity_count;
	}
	if(liquidity_count) mean_liquidity_days /= float(liquidity_count);
	auto expected_conflict_days = std::clamp((fund_runway + mean_liquidity_days) * 0.5f, 3.0f, 30.0f);
	auto benefit_rate = std::clamp(state.world.labor_union_get_strike_benefit_rate(union_id), 0.0f, 1.0f);
	for(auto id : voters) {
		auto contract = exact_person_economy::contract(state, id);
		if(!contract) continue;
		auto person = contract->worker;
		if(std::find(voter_people.begin(), voter_people.end(), person) == voter_people.end())
			voter_people.push_back(person);
		auto rate = std::max(contract_daily_rate(*contract), wage_floor_for_contract(state, id, state.current_date));
		auto liquidity_days = person_liquid_assets(state, person)
			/ std::max(rate * contract->labor_capacity, epsilon);
		auto wage_gain = std::max(0.0f, demand - rate) * 365.0f;
		auto protection_gain = required_protection > offered_protection ? rate * 24.0f : 0.0f;
		auto arrears_gain = contract->unpaid_wages > epsilon ? std::min(contract->unpaid_wages, rate * 30.0f) : 0.0f;
		auto benefit_cover = benefit_rate * (fund_runway > 0.0f
			? std::clamp(fund_runway / expected_conflict_days, 0.0f, 1.0f) : 0.0f);
		auto conflict_cost = rate * expected_conflict_days * (1.0f - benefit_cover);
		bool has_major_claim = contract->unpaid_wages > epsilon || required_protection > offered_protection;
		if(has_major_claim || ((wage_gain + arrears_gain + protection_gain) > conflict_cost * 1.25f
			&& (liquidity_days + fund_runway > 2.0f || wage_gain > conflict_cost * 2.0f))) {
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
	uint32_t workers = uint32_t(contracts_for_unit(state, unit).size());
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
	auto record = exact_person_economy::contract(state, exact_contract_id);
	if(exact_contract_id == 0 || !record
		|| record->status != exact_person_economy::contract_status::active
		|| record->end_date && date >= record->end_date) return result;
	state.world.for_each_collective_agreement_coverage([&](dcon::collective_agreement_coverage_id coverage) {
		if(state.world.collective_agreement_coverage_get_status(coverage) != active_record
			|| state.world.collective_agreement_coverage_get_exact_contract_id(coverage) != exact_contract_id) return;
		auto agreement = state.world.collective_agreement_coverage_get_agreement_id(coverage);
		if(agreement_effective(state, agreement, date))
			result = std::max(result, state.world.collective_agreement_get_minimum_daily_wage(agreement));
	});
	return result;
}

bool replacement_hiring_allowed(sys::state const& state, dcon::organization_id employer,
	dcon::factory_id factory, sys::date date) {
	if(!employer || !factory) return true;
	auto nation = nation_of_factory(state, factory);
	if(legal_protection(state, nation, date)
		< uint8_t(policy::labor_protection_level::due_process)) return true;
	bool blocked = false;
	state.world.for_each_strike_action([&](dcon::strike_action_id action) {
		if(blocked || state.world.strike_action_get_status(action) != uint8_t(conflict_status::active)
			|| state.world.strike_action_get_kind(action) != uint8_t(conflict_kind::strike)) return;
		auto unit = state.world.strike_action_get_unit_id(action);
		if(!unit || state.world.collective_bargaining_unit_get_employer_organization(unit) != employer) return;
		auto scoped_factory = state.world.collective_bargaining_unit_get_workplace_factory(unit);
		if(!scoped_factory || scoped_factory == factory) blocked = true;
	});
	return !blocked;
}

float severance_claim_for_worker(sys::state const& state, person_key person) {
	float result = 0.0f;
	state.world.for_each_labor_severance_claim([&](dcon::labor_severance_claim_id claim) {
		if(state.world.labor_severance_claim_get_status(claim) != active_record
			|| state.world.labor_severance_claim_get_source_population_cell(claim) != person.source_population_cell
			|| state.world.labor_severance_claim_get_ordinal(claim) != person.ordinal) return;
		result += std::max(0.0f, state.world.labor_severance_claim_get_outstanding_amount(claim));
	});
	return std::isfinite(result) ? result : 0.0f;
}

float expected_severance_for_contract(sys::state const& state, uint64_t contract_id,
	sys::date date) {
	auto record = exact_person_economy::contract(state, contract_id);
	if(!record || record->status != exact_person_economy::contract_status::active
		|| record->pay_period_days == 0) return 0.0f;
	auto days = std::max(contractual_severance_days(state, contract_id, date),
		statutory_severance_days(state, *record, date));
	auto daily = std::max(contract_daily_rate(*record), wage_floor_for_contract(state, contract_id, date));
	auto result = daily * std::max(0.0f, record->labor_capacity) * float(days);
	return std::isfinite(result) && result > 0.0f ? result : 0.0f;
}

float severance_liability_for_actor(sys::state const& state, dcon::economic_actor_id employer,
	dcon::factory_id factory) {
	if(!employer) return 0.0f;
	float total = 0.0f;
	if(factory) {
		state.world.factory_for_each_labor_severance_claim_factory_as_factory(factory,
			[&](auto relation) {
				auto claim = state.world.labor_severance_claim_factory_get_claim(relation);
				if(state.world.labor_severance_claim_get_employer_from_labor_severance_claim_employer(claim)
					== employer && state.world.labor_severance_claim_get_status(claim) == active_record)
					total += std::max(0.0f, state.world.labor_severance_claim_get_outstanding_amount(claim));
			});
	} else {
		state.world.economic_actor_for_each_labor_severance_claim_employer_as_employer(employer,
			[&](auto relation) {
				auto claim = state.world.labor_severance_claim_employer_get_claim(relation);
				if(state.world.labor_severance_claim_get_status(claim) == active_record)
					total += std::max(0.0f, state.world.labor_severance_claim_get_outstanding_amount(claim));
			});
	}
	return std::isfinite(total) ? total : 0.0f;
}

float settle_factory_severance_claims(sys::state& state, dcon::factory_id factory,
	dcon::monetary_account_id debtor_account, float limit) {
	if(!factory || !debtor_account || !finite_nonnegative(limit) || limit <= epsilon) return 0.0f;
	auto debtor = accounts::owner_of(state, debtor_account);
	auto settlement = accounts::settlement_of(state, debtor_account);
	if(!debtor || !settlement) return 0.0f;
	std::vector<dcon::labor_severance_claim_id> claims;
	state.world.factory_for_each_labor_severance_claim_factory_as_factory(factory,
		[&](auto relation) {
			auto claim = state.world.labor_severance_claim_factory_get_claim(relation);
			if(state.world.labor_severance_claim_get_employer_from_labor_severance_claim_employer(claim)
				== debtor && state.world.labor_severance_claim_get_status(claim) == active_record)
				claims.push_back(claim);
		});
	std::sort(claims.begin(), claims.end(), [&](auto left, auto right) {
		return state.world.labor_severance_claim_get_created_on(left)
			< state.world.labor_severance_claim_get_created_on(right);
	});
	float recovered = 0.0f;
	for(auto claim : claims) {
		if(recovered + epsilon >= limit
			|| state.world.labor_severance_claim_get_settlement(claim) != settlement) break;
		auto outstanding = std::max(0.0f,
			state.world.labor_severance_claim_get_outstanding_amount(claim));
		auto contract = exact_person_economy::contract(state,
			state.world.labor_severance_claim_get_exact_contract_id(claim));
		if(!contract) continue;
		auto worker_wallet = exact_person_economy::account_ref::from_exact(
			contract->worker_account_id);
		auto payable = std::min({limit - recovered, outstanding, accounts::balance(state, debtor_account)});
		if(payable <= epsilon) break;
		auto paid = exact_person_economy::transfer_with_result(state,
			exact_person_economy::account_ref::from_dcon(debtor_account), worker_wallet, payable,
			relations::transaction_kind::severance, state.current_date);
		if(!paid.success) break;
		auto remaining = std::max(0.0f, outstanding - payable);
		state.world.labor_severance_claim_set_outstanding_amount(claim, remaining);
		if(remaining <= epsilon)
			state.world.labor_severance_claim_set_status(claim, inactive_record);
		recovered += payable;
	}
	return recovered;
}

bool prepare_contract_termination(sys::state& state, uint64_t contract_id,
	exact_person_economy::contract_end_reason reason, sys::date date) {
	auto record = exact_person_economy::contract(state, contract_id);
	if(!record || record->status != exact_person_economy::contract_status::active) return false;
	auto protection = std::max(legal_protection(state, nation_of_site(state, record->workplace), date),
		protection_for_contract(state, contract_id, date));
	if(reason == exact_person_economy::contract_end_reason::unspecified
		&& protection >= uint8_t(policy::labor_protection_level::due_process)) return false;
	if(reason == exact_person_economy::contract_end_reason::misconduct
		&& protection >= uint8_t(policy::labor_protection_level::due_process)) return false;
	if(contract_is_withheld(state, contract_id, date)
		&& protection >= uint8_t(policy::labor_protection_level::due_process)
		&& reason != exact_person_economy::contract_end_reason::employer_closure) return false;
	if(reason != exact_person_economy::contract_end_reason::employer_layoff
		&& reason != exact_person_economy::contract_end_reason::employer_closure) return true;
	auto amount = expected_severance_for_contract(state, contract_id, date);
	if(!finite_nonnegative(amount) || amount <= epsilon) return true;
	auto claim = state.world.create_labor_severance_claim();
	state.world.labor_severance_claim_set_exact_contract_id(claim, contract_id);
	state.world.labor_severance_claim_set_source_population_cell(claim, record->worker.source_population_cell);
	state.world.labor_severance_claim_set_ordinal(claim, record->worker.ordinal);
	state.world.labor_severance_claim_set_settlement(claim,
		exact_person_economy::settlement_of(state,
			exact_person_economy::account_ref::from_exact(record->worker_account_id)));
	state.world.labor_severance_claim_set_original_amount(claim, amount);
	state.world.labor_severance_claim_set_outstanding_amount(claim, amount);
	state.world.labor_severance_claim_set_created_on(claim, date);
	state.world.labor_severance_claim_set_status(claim, active_record);
	state.world.force_create_labor_severance_claim_employer(claim, record->employer);
	if(record->factory) state.world.force_create_labor_severance_claim_factory(claim, record->factory);
	if(record->factory && record->payer_account)
		(void)settle_factory_severance_claims(state, record->factory, record->payer_account, amount);
	return true;
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

float wage_floor_for_workplace(sys::state const& state, dcon::organization_id employer,
	dcon::factory_id factory, sys::date date) {
	float result = 0.0f;
	if(!employer || !factory) return wage_floor_for_employer(state, employer, date);
	state.world.for_each_collective_agreement([&](dcon::collective_agreement_id agreement) {
		if(!agreement_effective(state, agreement, date)) return;
		auto unit = state.world.collective_agreement_get_unit_id(agreement);
		auto scoped_factory = state.world.collective_bargaining_unit_get_workplace_factory(unit);
		if(scoped_factory && scoped_factory != factory) return;
		if(agreement_matches_employer(state, agreement, employer)
			|| state.world.collective_agreement_get_employer_organization(agreement) == employer)
			result = std::max(result, state.world.collective_agreement_get_minimum_daily_wage(agreement));
	});
	return result;
}

void register_new_contract(sys::state& state, uint64_t exact_contract_id, sys::date date) {
	auto record = exact_person_economy::contract(state, exact_contract_id);
	if(!record || !record->factory) return;
	(void)ensure_workplace_observation(state, record->factory);
	auto employer = actors::organizations::organization_for_actor(state, record->employer);
	if(!employer) return;
	std::vector<dcon::collective_agreement_id> agreements;
	state.world.for_each_collective_agreement([&](auto agreement) {
		if(agreement_effective(state, agreement, date)
			&& agreement_covers_employer_and_factory(state, agreement, employer, record->factory))
			agreements.push_back(agreement);
	});
	for(auto agreement : agreements) {
		auto coverage = state.world.create_collective_agreement_coverage();
		state.world.collective_agreement_coverage_set_agreement_id(coverage, agreement);
		state.world.collective_agreement_coverage_set_exact_contract_id(coverage, exact_contract_id);
		state.world.collective_agreement_coverage_set_source_population_cell(coverage,
			record->worker.source_population_cell);
		state.world.collective_agreement_coverage_set_ordinal(coverage, record->worker.ordinal);
		state.world.collective_agreement_coverage_set_covered_on(coverage, date);
		state.world.collective_agreement_coverage_set_status(coverage, active_record);
		state.world.force_create_collective_agreement_coverage_agreement(coverage, agreement);
	}
}

void contract_ended(sys::state& state, uint64_t exact_contract_id) {
	state.world.for_each_collective_agreement_coverage([&](auto coverage) {
		if(state.world.collective_agreement_coverage_get_exact_contract_id(coverage) == exact_contract_id
			&& state.world.collective_agreement_coverage_get_status(coverage) == active_record)
			state.world.collective_agreement_coverage_set_status(coverage, inactive_record);
	});
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

	// Existing exact contracts from older saves get one event-driven workplace
	// observation seed. New hires attach their observation in register_new_contract.
	bool has_observation = false;
	state.world.for_each_labor_workplace_observation([&](auto) { has_observation = true; });
	if(!has_observation) state.world.for_each_factory([&](auto factory) {
		if(!exact_person_economy::active_contracts_for_factory(state, factory).empty())
			(void)ensure_workplace_observation(state, factory);
	});
	std::vector<dcon::labor_workplace_observation_id> observations;
	state.world.for_each_labor_workplace_observation([&](auto item) { observations.push_back(item); });
	for(auto observation : observations) {
		auto relation = state.world.labor_workplace_observation_get_labor_workplace_observation_factory(observation);
		auto factory = relation ? state.world.labor_workplace_observation_factory_get_factory(relation) : dcon::factory_id{};
		auto reviewed = state.world.labor_workplace_observation_get_reviewed_on(observation);
		bool due = !reviewed || date.to_raw_value() - reviewed.to_raw_value() >= workplace_review_days;
		if(due && factory && (uint32_t(date.to_raw_value()) + factory.index()) % workplace_review_days == 0) {
			review_workplace(state, observation, factory, date);
		}
	}
	if(uint32_t(date.to_raw_value()) % workplace_review_days == 0)
		review_employer_associations(state, date);
	if(uint32_t(date.to_raw_value()) % workplace_review_days == 0) {
		for(auto membership : memberships) {
			if(!membership_active(state, membership)) continue;
			auto relation = state.world.union_membership_get_union_membership_union(membership);
			auto union_id = state.world.union_membership_union_get_labor_union(relation);
			auto person = membership_person(state, membership);
			bool represented = false;
			for(auto id : exact_person_economy::active_contracts_for_person(state, person)) {
				auto record = exact_person_economy::contract(state, id);
				if(record && union_covers_contract(state, union_id, *record)) represented = true;
			}
			if(!represented) (void)leave_union(state, union_id, person, date);
		}
	}
	if(uint32_t(date.to_raw_value()) % 7 == 0) ensure_autonomous_bargaining_units(state, date);

	// Organizations with no constituency enter an inactive estate; their
	// operating accounts and any agreements remain canonical records.
	state.world.for_each_labor_union([&](auto union_id) {
		if(state.world.labor_union_get_status(union_id) != active_record) return;
		bool employment_base = false;
		state.world.labor_union_for_each_union_membership_union_as_labor_union(union_id, [&](auto relation) {
			if(employment_base) return;
			auto membership = state.world.union_membership_union_get_membership(relation);
			if(!membership_active(state, membership)) return;
			for(auto id : exact_person_economy::active_contracts_for_person(state,
				membership_person(state, membership))) {
				auto record = exact_person_economy::contract(state, id);
				if(record && union_covers_contract(state, union_id, *record)) employment_base = true;
			}
		});
		if(!employment_base && date.to_raw_value() - state.world.labor_union_get_formed_on(union_id).to_raw_value() > 180)
			state.world.labor_union_set_status(union_id, inactive_record);
	});

	// Open renewal talks before expiry. The current agreement remains in force
	// while the parties renegotiate and ends only on its own legal date.
	std::vector<dcon::collective_agreement_id> agreements;
	state.world.for_each_collective_agreement([&](auto item) { agreements.push_back(item); });
	for(auto agreement : agreements) {
		auto expires = state.world.collective_agreement_get_expires_on(agreement);
		auto unit = state.world.collective_agreement_get_unit_id(agreement);
		if(state.world.collective_agreement_get_status(agreement) == active_record
			&& expires && !(date < expires)) {
			state.world.collective_agreement_set_status(agreement, inactive_record);
			state.world.for_each_collective_agreement_coverage([&](auto coverage) {
				if(state.world.collective_agreement_coverage_get_agreement_id(coverage) == agreement)
					state.world.collective_agreement_coverage_set_status(coverage, inactive_record);
			});
			if(unit && state.world.collective_bargaining_unit_get_status(unit) == uint8_t(bargaining_status::agreement))
				state.world.collective_bargaining_unit_set_status(unit, uint8_t(bargaining_status::bargaining));
		}
		if(state.world.collective_agreement_get_status(agreement) != active_record) continue;
		if(expires && date < expires && expires.to_raw_value() - date.to_raw_value() <= 30
			&& unit && state.world.collective_bargaining_unit_get_status(unit) == uint8_t(bargaining_status::agreement)) {
			state.world.collective_bargaining_unit_set_status(unit, uint8_t(bargaining_status::bargaining));
			state.world.collective_bargaining_unit_set_last_reviewed_on(unit, {});
		}
		ensure_contract_coverage(state, agreement, date);
	}

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
		auto tally = tally_unit(state, unit);
		if(mode != policy::collective_bargaining_mode::voluntary_recognition
			&& recognition_threshold_met(state, unit, mode)) set_recognized(state, unit, date);
		else if(mode == policy::collective_bargaining_mode::voluntary_recognition && tally.workers > 0) {
			auto density = float(tally.members) / float(tally.workers);
			auto current_wage = unit_average_daily_rate(state, unit);
			auto employer_ceiling = employer_max_offer_from_economy(state, unit, date);
			bool has_arrears = false;
			for(auto id : contracts_for_unit(state, unit)) {
				auto record = exact_person_economy::contract(state, id);
				has_arrears = has_arrears || (record && record->unpaid_wages > epsilon);
			}
			if(density >= 0.40f && (employer_ceiling > current_wage * 1.02f || has_arrears))
				set_recognized(state, unit, date);
			else if(density >= 0.20f)
				state.world.collective_bargaining_unit_set_status(unit,
					uint8_t(bargaining_status::bargaining));
		}
		status = bargaining_status(state.world.collective_bargaining_unit_get_status(unit));
		if(status == bargaining_status::awaiting_recognition || status == bargaining_status::recognized) continue;
		state.world.collective_bargaining_unit_set_status(unit, uint8_t(bargaining_status::bargaining));
		state.world.collective_bargaining_unit_set_demand_daily_wage(unit,
			union_demand_from_economy(state, unit, date, false));
		state.world.collective_bargaining_unit_set_demand_protection_level(unit,
			protection_demand_from_economy(state, unit, date));
		auto maximum_offer = employer_max_offer_from_economy(state, unit, date);
		auto legal = legal_protection(state, nation, date);
		state.world.collective_bargaining_unit_set_offer_daily_wage(unit, maximum_offer);
		state.world.collective_bargaining_unit_set_offer_protection_level(unit, legal);
		if(state.world.collective_bargaining_unit_get_demand_daily_wage(unit) <= maximum_offer + epsilon
			&& legal >= state.world.collective_bargaining_unit_get_demand_protection_level(unit)) {
			(void)accept_employer_offer(state, unit, date);
			continue;
		}
		auto last_reviewed = state.world.collective_bargaining_unit_get_last_reviewed_on(unit);
		bool agreement_still_live = false;
		state.world.for_each_collective_agreement([&](auto agreement) {
			if(state.world.collective_agreement_get_unit_id(agreement) == unit
				&& agreement_effective(state, agreement, date)) agreement_still_live = true;
		});
		if(!agreement_still_live && (!last_reviewed
			|| date.to_raw_value() - last_reviewed.to_raw_value() >= negotiation_review_days)) {
			state.world.collective_bargaining_unit_set_last_reviewed_on(unit, date);
			(void)call_strike(state, unit, date);
		}
	}

	std::vector<dcon::strike_action_id> actions;
	state.world.for_each_strike_action([&](auto item) { actions.push_back(item); });
	for(auto action : actions) {
		if(state.world.strike_action_get_status(action) != uint8_t(conflict_status::active)) continue;
		refresh_conflict_day(state, action, date);
		refresh_strike_lost_revenue(state, action);
		auto unit = state.world.strike_action_get_unit_id(action);
		if(!unit || !state.world.collective_bargaining_unit_is_valid(unit)) continue;
		bool strike = state.world.strike_action_get_kind(action) == uint8_t(conflict_kind::strike);
		state.world.collective_bargaining_unit_set_demand_daily_wage(unit,
			union_demand_from_economy(state, unit, date, strike));
		state.world.collective_bargaining_unit_set_offer_daily_wage(unit,
			employer_max_offer_from_economy(state, unit, date));
		state.world.collective_bargaining_unit_set_status(unit, uint8_t(bargaining_status::bargaining));
		if(state.world.collective_bargaining_unit_get_demand_daily_wage(unit)
			<= state.world.collective_bargaining_unit_get_offer_daily_wage(unit) + epsilon
			&& state.world.collective_bargaining_unit_get_offer_protection_level(unit)
			>= state.world.collective_bargaining_unit_get_demand_protection_level(unit)) {
			if(accept_employer_offer(state, unit, date)) continue;
		}
		if(strike) {
			uint32_t active = 0, willing = 0;
			auto union_id = state.world.strike_action_get_union_id(action);
			auto daily_fund_cost = strike_daily_benefit_obligation(state, unit);
			auto fund_runway = daily_fund_cost > epsilon
				? bargaining_unit_treasury(state, unit) / daily_fund_cost : 0.0f;
			auto expected_days = std::clamp(fund_runway, 0.0f, 30.0f);
			if(expected_days < 3.0f) expected_days = 3.0f;
			auto benefit_rate = std::clamp(state.world.labor_union_get_strike_benefit_rate(union_id), 0.0f, 1.0f);
			state.world.strike_action_for_each_strike_action_member_action_as_strike(action, [&](auto relation) {
				auto member = state.world.strike_action_member_action_get_member(relation);
				if(state.world.strike_action_member_get_status(member) != active_record) return;
				++active;
				auto record = exact_person_economy::contract(state,
					state.world.strike_action_member_get_exact_contract_id(member));
				if(!record) return;
				auto rate = std::max(contract_daily_rate(*record),
					wage_floor_for_contract(state, record->id, date));
				auto liquid_days = person_liquid_assets(state, record->worker)
					/ std::max(rate * record->labor_capacity, epsilon);
				auto demand = state.world.collective_bargaining_unit_get_demand_daily_wage(unit);
				auto wage_gain = std::max(0.0f, demand - rate) * 365.0f;
				auto benefit_cover = benefit_rate * (expected_days > 0.0f
					? std::clamp(fund_runway / expected_days, 0.0f, 1.0f) : 0.0f);
				auto conflict_cost = rate * expected_days * (1.0f - benefit_cover);
				bool urgent_claim = record->unpaid_wages > epsilon
					|| state.world.collective_bargaining_unit_get_demand_protection_level(unit)
						> state.world.collective_bargaining_unit_get_offer_protection_level(unit);
				if(urgent_claim || (wage_gain > conflict_cost * 1.25f
					&& liquid_days + fund_runway > 2.0f))
					++willing;
			});
			if(active == 0 || willing * 2 <= active) {
				state.world.strike_action_set_status(action, uint8_t(conflict_status::cancelled));
				state.world.strike_action_set_ended_on(action, date);
				state.world.collective_bargaining_unit_set_active_strike_id(unit, {});
				state.world.collective_bargaining_unit_set_status(unit, uint8_t(bargaining_status::bargaining));
				state.world.labor_union_set_last_failure_on(union_id, date);
				state.world.labor_union_set_failure_count(union_id, uint16_t(std::min<uint32_t>(
					std::numeric_limits<uint16_t>::max(),
					uint32_t(state.world.labor_union_get_failure_count(union_id)) + 1)));
			}
		}
	}

	// A lockout is chosen only by a business that has poor current margins but
	// enough saleable stock and payroll runway to use the stoppage economically.
	for(auto unit : units) {
		if(state.world.collective_bargaining_unit_get_status(unit) != uint8_t(bargaining_status::bargaining)
			|| state.world.collective_bargaining_unit_get_active_strike_id(unit)) continue;
		if(state.world.collective_bargaining_unit_get_demand_daily_wage(unit)
			<= state.world.collective_bargaining_unit_get_offer_daily_wage(unit) + epsilon) continue;
		bool justified = false;
		for(auto employer : employers_for_unit(state, unit))
			for(auto factory : actors::organizations::factories_operated_by(state, employer)) {
				auto decision = firm_agency::decide_factory(state, factory);
				auto payroll = exact_person_economy::wage_due_for_factory(state, factory);
				auto profit = state.world.factory_get_agency_recent_profit(factory);
				auto settlement = state.world.factory_get_payroll_settlement(factory);
				auto actor = actors::organizations::actor_for_organization(state, employer);
				auto cash_account = settlement ? accounts::find_account(state, actor, settlement)
					: dcon::monetary_account_id{};
				auto runway = cash_account && payroll > epsilon
					? accounts::balance(state, cash_account) / payroll : 0.0f;
				if(profit < -epsilon && decision.output_inventory > decision.desired_output * 2.0f
					&& runway >= 14.0f) justified = true;
			}
		if(justified) (void)declare_lockout(state, unit, date);
	}
}

} // namespace economy::collective_labor
