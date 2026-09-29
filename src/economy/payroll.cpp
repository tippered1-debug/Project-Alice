#include "payroll.hpp"

#include "system_state.hpp"
#include "economy/accounts/accounts.hpp"
#include "actors/organizations/organizations.hpp"
#include "compat/alice/legacy_bridge.hpp"
#include "economy/exact_person_economy.hpp"
#include "economy/economy_stats.hpp"
#include "economy/causal_order.hpp"
#include "governance/governance.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>

namespace economy::payroll {
namespace {
dcon::payroll_event_id record_event(sys::state& state, dcon::factory_id factory, dcon::province_id province, dcon::economic_actor_id operator_actor,
	dcon::commodity_id settlement, dcon::obligation_id obligation, float due, float paid, float unpaid,
	float gross_no, float gross_basic, float gross_high, float paid_no, float paid_basic, float paid_high) {
	if(!province || !operator_actor || !settlement) return {};
	auto event = state.world.create_payroll_event();
	state.world.payroll_event_set_settlement(event, settlement);
	state.world.payroll_event_set_gross_due(event, due);
	state.world.payroll_event_set_paid(event, paid);
	state.world.payroll_event_set_unpaid(event, unpaid);
	state.world.payroll_event_set_gross_no_education(event, gross_no);
	state.world.payroll_event_set_gross_basic_education(event, gross_basic);
	state.world.payroll_event_set_gross_high_education(event, gross_high);
	state.world.payroll_event_set_paid_no_education(event, paid_no);
	state.world.payroll_event_set_paid_basic_education(event, paid_basic);
	state.world.payroll_event_set_paid_high_education(event, paid_high);
	state.world.payroll_event_set_occurred_on(event, state.current_date);
	if(factory) state.world.force_create_payroll_event_factory(event, factory);
	state.world.force_create_payroll_event_operator(event, operator_actor);
	state.world.force_create_payroll_event_province(event, province);
	if(obligation) state.world.force_create_payroll_event_obligation(event, obligation);
	return event;
}

struct wage_claim {
	uint64_t exact_contract = 0;
	float current_due = 0.0f;
	float arrears = 0.0f;
	sys::date arrears_since{};
	dcon::commodity_id settlement{};
	uint64_t causal_sequence = 0;
	uint64_t stable_id = 0;
};

bool causal_claim_before(wage_claim const& left, wage_claim const& right) {
	if(economy::causal_order::before({{}, left.causal_sequence}, {{}, right.causal_sequence})) return true;
	if(economy::causal_order::before({{}, right.causal_sequence}, {{}, left.causal_sequence})) return false;
	return left.stable_id < right.stable_id;
}

bool arrears_claim_before(wage_claim const& left, wage_claim const& right) {
	if(left.arrears_since != right.arrears_since) return left.arrears_since < right.arrears_since;
	return causal_claim_before(left, right);
}
}

void begin_day(sys::state&) { }

void record_public_payroll(sys::state& state, dcon::province_id province,
	dcon::commodity_id settlement, float due_no, float due_basic, float due_high,
	float paid_no, float paid_basic, float paid_high) {
	if(!province || !settlement) return;
	for(auto value : {due_no, due_basic, due_high, paid_no, paid_basic, paid_high})
		if(!std::isfinite(value) || value < 0.0f) return;
	auto due = due_no + due_basic + due_high;
	if(due <= 1.0e-6f) return;
	dcon::payroll_event_id event{};
	std::vector<dcon::payroll_event_id> expired_public_events;
	state.world.province_for_each_payroll_event_province_as_province(province, [&](auto relation) {
		auto candidate = state.world.payroll_event_province_get_payroll_event(relation);
		if(state.world.payroll_event_get_factory_from_payroll_event_factory(candidate)) return;
		auto occurred = state.world.payroll_event_get_occurred_on(candidate);
		if(occurred < state.current_date) {
			expired_public_events.push_back(candidate);
			return;
		}
		if(occurred == state.current_date
			&& state.world.payroll_event_get_settlement(candidate) == settlement) event = candidate;
	});
	for(auto expired : expired_public_events) state.world.delete_payroll_event(expired);
	if(!event) {
		auto actor = governance::central_government_for(state,
			state.world.province_get_nation_from_province_ownership(province));
		if(!actor) return;
		(void)record_event(state, {}, province, governance::actor_for_institution(state, actor),
			settlement, {}, due, std::min(due, paid_no + paid_basic + paid_high),
			std::max(0.0f, due - paid_no - paid_basic - paid_high),
			due_no, due_basic, due_high, paid_no, paid_basic, paid_high);
		return;
	}
	state.world.payroll_event_set_gross_due(event, state.world.payroll_event_get_gross_due(event) + due);
	state.world.payroll_event_set_paid(event, state.world.payroll_event_get_paid(event)
		+ std::min(due, paid_no + paid_basic + paid_high));
	state.world.payroll_event_set_unpaid(event, state.world.payroll_event_get_unpaid(event)
		+ std::max(0.0f, due - paid_no - paid_basic - paid_high));
	state.world.payroll_event_set_gross_no_education(event, state.world.payroll_event_get_gross_no_education(event) + due_no);
	state.world.payroll_event_set_gross_basic_education(event, state.world.payroll_event_get_gross_basic_education(event) + due_basic);
	state.world.payroll_event_set_gross_high_education(event, state.world.payroll_event_get_gross_high_education(event) + due_high);
	state.world.payroll_event_set_paid_no_education(event, state.world.payroll_event_get_paid_no_education(event) + paid_no);
	state.world.payroll_event_set_paid_basic_education(event, state.world.payroll_event_get_paid_basic_education(event) + paid_basic);
	state.world.payroll_event_set_paid_high_education(event, state.world.payroll_event_get_paid_high_education(event) + paid_high);
}

province_payroll for_province(sys::state const& state, dcon::province_id province, sys::date date) {
	province_payroll result{};
	if(!province) return result;
	state.world.province_for_each_payroll_event_province_as_province(province, [&](auto relation) {
		auto event = state.world.payroll_event_province_get_payroll_event(relation);
		if(state.world.payroll_event_get_occurred_on(event) != date) return;
		auto factory = state.world.payroll_event_get_factory_from_payroll_event_factory(event);
		if(factory) {
			result.canonical_factory = true;
			result.no_education += state.world.payroll_event_get_paid_no_education(event);
			result.basic_education += state.world.payroll_event_get_paid_basic_education(event);
			result.high_education += state.world.payroll_event_get_paid_high_education(event);
		} else {
			result.public_no_education_due += state.world.payroll_event_get_gross_no_education(event);
			result.public_basic_education_due += state.world.payroll_event_get_gross_basic_education(event);
			result.public_high_education_due += state.world.payroll_event_get_gross_high_education(event);
		}
	});
	return result;
}

void settle_factory(sys::state& state, dcon::factory_id factory, float, float) {
	if(!factory || !state.world.factory_is_valid(factory)) return;
	if(state.world.factory_get_payroll_initialized(factory)
		&& state.world.factory_get_last_payroll_date(factory) == state.current_date) return;
	auto province = compat::alice::province_for_factory(state, factory);
	auto operator_actor = actors::organizations::operator_actor_for_factory(state, factory);
	if(!province || !operator_actor) {
		assert(false && "canonical factory payroll requires a firm and workplace");
		std::abort();
	}
	auto build_claims = [&]() {
		std::vector<wage_claim> claims;
		for(auto contract_id : exact_person_economy::contracts_for_factory(state, factory)) {
			auto record = exact_person_economy::contract(state, contract_id);
			if(!record) continue;
			claims.push_back({contract_id, exact_person_economy::wage_due(state, contract_id),
				std::max(0.0f, record->unpaid_wages), record->arrears_since,
				accounts::settlement_of(state, record->payer_account), record->causal_sequence, contract_id});
		}
		return claims;
	};
	constexpr float epsilon = 1.0e-6f;
	auto arrears_claims = build_claims();
	arrears_claims.erase(std::remove_if(arrears_claims.begin(), arrears_claims.end(),
		[](auto const& claim) { return claim.arrears <= epsilon; }), arrears_claims.end());
	std::sort(arrears_claims.begin(), arrears_claims.end(), arrears_claim_before);
	for(auto const& claim : arrears_claims) {
		auto result = exact_person_economy::settle_contract_arrears_only(state, claim.exact_contract);
		if(result.arrears_repaid <= epsilon || !claim.settlement) continue;
		record_event(state, factory, province, operator_actor, claim.settlement, {},
			0.0f, result.arrears_repaid, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
	}
	bool arrears_remaining = false;
	for(auto const& claim : build_claims())
		if(claim.arrears > epsilon) { arrears_remaining = true; break; }
	if(!arrears_remaining) {
		auto current_claims = build_claims();
		current_claims.erase(std::remove_if(current_claims.begin(), current_claims.end(),
			[](auto const& claim) { return claim.current_due <= epsilon; }), current_claims.end());
		std::sort(current_claims.begin(), current_claims.end(), causal_claim_before);
		for(auto const& claim : current_claims) {
			auto result = exact_person_economy::settle_current_contract_wage_only(state, claim.exact_contract);
			if(result.current_due <= epsilon || !claim.settlement) continue;
			auto record = exact_person_economy::contract(state, claim.exact_contract);
			float gross[3]{};
			if(record && record->occupation < 3) gross[record->occupation] = result.current_due;
			auto paid_scale = result.current_due > epsilon ? result.current_paid / result.current_due : 0.0f;
			record_event(state, factory, province, operator_actor, claim.settlement, {},
				result.current_due, result.current_paid, std::max(0.0f, result.current_due - result.current_paid),
				gross[0], gross[1], gross[2], gross[0] * paid_scale, gross[1] * paid_scale, gross[2] * paid_scale);
		}
	}
	state.world.factory_set_last_payroll_date(factory, state.current_date);
	state.world.factory_set_payroll_initialized(factory, 1);
}
} // namespace economy::payroll
