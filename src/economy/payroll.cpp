#include "payroll.hpp"

#include "system_state.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/relations/relations.hpp"
#include "actors/organizations/organizations.hpp"
#include "compat/alice/legacy_bridge.hpp"
#include "economy/physical/concrete_labor.hpp"
#include "economy/economy_stats.hpp"

#include <algorithm>
#include <cmath>

namespace economy::payroll {
namespace {
dcon::economic_actor_id labor_actor_for(sys::state& state, dcon::province_id province) {
	if(!province) return {};
	auto relation = state.world.province_get_province_labor_clearing(province);
	if(relation) return state.world.province_labor_clearing_get_economic_actor(relation);
	auto actor = state.world.create_economic_actor();
	if(!actor) return {};
	state.world.force_create_province_labor_clearing(province, actor);
	return actor;
}
dcon::monetary_account_id labor_account_for(sys::state& state, dcon::province_id province, dcon::commodity_id settlement) {
	auto actor = labor_actor_for(state, province);
	if(!actor) return {};
	auto account = accounts::find_account(state, actor, settlement);
	return account ? account : accounts::open_account(state, actor, settlement);
}
dcon::obligation_id oldest_arrears(sys::state const& state, dcon::economic_actor_id debtor, dcon::economic_actor_id creditor, dcon::commodity_id settlement) {
	dcon::obligation_id result{}; sys::date oldest{};
	state.world.economic_actor_for_each_obligation_debtor_as_economic_actor(debtor, [&](auto relation) {
		auto obligation = state.world.obligation_debtor_get_obligation(relation);
		if(!obligation || state.world.obligation_get_kind(obligation) != uint8_t(relations::obligation_kind::payroll)
			|| state.world.obligation_get_status(obligation) != uint8_t(relations::obligation_status::active)
			|| state.world.obligation_get_economic_actor_from_obligation_creditor(obligation) != creditor
			|| state.world.obligation_get_settlement_commodity(obligation) != settlement) return;
		auto date = state.world.obligation_get_creation_date(obligation);
		if(!result || date < oldest) { result = obligation; oldest = date; }
	});
	return result;
}
void record_event(sys::state& state, dcon::factory_id factory, dcon::province_id province, dcon::economic_actor_id operator_actor,
	dcon::commodity_id settlement, dcon::obligation_id obligation, float due, float paid, float unpaid,
	float gross_no, float gross_basic, float gross_high, float paid_no, float paid_basic, float paid_high) {
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
	state.world.force_create_payroll_event_factory(event, factory);
	state.world.force_create_payroll_event_operator(event, operator_actor);
	state.world.force_create_payroll_event_province(event, province);
	if(obligation) state.world.force_create_payroll_event_obligation(event, obligation);
}
}

void begin_day(sys::state&) { }

province_payroll for_province(sys::state const& state, dcon::province_id province, sys::date date) {
	province_payroll result{};
	if(!province) return result;
	state.world.province_for_each_payroll_event_province_as_province(province, [&](auto relation) {
		auto event = state.world.payroll_event_province_get_payroll_event(relation);
		if(state.world.payroll_event_get_occurred_on(event) != date) return;
		result.canonical_factory = true;
		result.no_education += state.world.payroll_event_get_paid_no_education(event);
		result.basic_education += state.world.payroll_event_get_paid_basic_education(event);
		result.high_education += state.world.payroll_event_get_paid_high_education(event);
	});
	return result;
}

void settle_factory(sys::state& state, dcon::factory_id factory, float actual_units, float available_units) {
	if(!factory) return;
	// Concrete contracts are the canonical payroll authority. The legacy branch
	// below is retained only for explicitly non-canonical factories. A canonical
	// factory with no contracts has zero canonical payroll.
	auto contracts = physical::concrete_labor::contracts_for_factory(state, factory);
	if(state.world.factory_get_canonical_production(factory)) {
		if(state.world.factory_get_payroll_initialized(factory)
			&& state.world.factory_get_last_payroll_date(factory) == state.current_date) return;
		if(!contracts.empty()) {
			auto province = compat::alice::province_for_factory(state, factory);
			auto operator_actor = actors::organizations::operator_actor_for_factory(state, factory);
			for(auto contract : physical::concrete_labor::active_contracts_for_factory(state, factory)) {
				auto settlement = accounts::settlement_of(state,
					state.world.employment_contract_get_monetary_account_from_employment_contract_payer_account(contract));
				auto result = physical::concrete_labor::settle_contract_wage(state, contract);
				if(province && operator_actor && settlement) {
					record_event(state, factory, province, operator_actor, settlement, result.obligation,
						result.due, result.paid, result.unpaid, result.due, 0.0f, 0.0f,
						result.paid, 0.0f, 0.0f);
				}
			}
			state.world.factory_set_last_payroll_date(factory, state.current_date);
			state.world.factory_set_payroll_initialized(factory, 1);
			return;
		}
		state.world.factory_set_last_payroll_date(factory, state.current_date);
		state.world.factory_set_payroll_initialized(factory, 1);
		return;
	}
	if(state.world.factory_get_payroll_initialized(factory)
		&& state.world.factory_get_last_payroll_date(factory) == state.current_date) return;
	auto province = compat::alice::province_for_factory(state, factory);
	auto operator_actor = actors::organizations::operator_actor_for_factory(state, factory);
	auto settlement = state.world.factory_get_payroll_settlement(factory);
	if(!province || !operator_actor || !settlement || !state.world.commodity_is_valid(settlement)) return;
	auto operator_account = accounts::find_account(state, operator_actor, settlement);
	if(!operator_account) return;
	auto labor_actor = labor_actor_for(state, province);
	auto labor_account = labor_account_for(state, province, settlement);
	if(!labor_actor || !labor_account) return;
	float ratio = available_units > 0.0f ? std::clamp(actual_units / available_units, 0.0f, 1.0f) : 0.0f;
	float no_workers = std::max(0.0f, state.world.factory_get_unqualified_employment(factory)) * ratio;
	float basic_workers = std::max(0.0f, state.world.factory_get_primary_employment(factory)) * ratio;
	float high_workers = std::max(0.0f, state.world.factory_get_secondary_employment(factory)) * ratio;
	float gross_no = no_workers * state.world.province_get_labor_price(province, economy::labor::no_education);
	float gross_basic = basic_workers * state.world.province_get_labor_price(province, economy::labor::basic_education);
	float gross_high = high_workers * state.world.province_get_labor_price(province, economy::labor::high_education);
	float due = gross_no + gross_basic + gross_high;
	if(!std::isfinite(due) || due < 0.0f) due = 0.0f;
	float remaining_cash = accounts::balance(state, operator_account);
	while(remaining_cash > 0.0f) {
		auto arrears = oldest_arrears(state, operator_actor, labor_actor, settlement);
		if(!arrears) break;
		auto original = dcon::payroll_event_id{};
		state.world.for_each_payroll_event([&](auto event) {
			if(!original && state.world.payroll_event_get_obligation_from_payroll_event_obligation(event) == arrears) original = event;
		});
		auto requested = std::min(remaining_cash, relations::total_due(state, arrears));
		if(requested <= 0.0f || !accounts::settle_obligation_payment(state, arrears, operator_account, labor_account, requested, state.current_date)) break;
		float original_due = original ? state.world.payroll_event_get_gross_due(original) : 0.0f;
		float no = original ? requested * state.world.payroll_event_get_gross_no_education(original) / std::max(original_due, 1.0e-6f) : 0.0f;
		float basic = original ? requested * state.world.payroll_event_get_gross_basic_education(original) / std::max(original_due, 1.0e-6f) : 0.0f;
		float high = original ? requested * state.world.payroll_event_get_gross_high_education(original) / std::max(original_due, 1.0e-6f) : 0.0f;
		auto claim_factory = original ? state.world.payroll_event_get_factory_from_payroll_event_factory(original) : factory;
		auto claim_province = original ? state.world.payroll_event_get_province_from_payroll_event_province(original) : province;
		record_event(state, claim_factory, claim_province, operator_actor, settlement, arrears, 0.0f, requested, 0.0f, no, basic, high, no, basic, high);
		remaining_cash -= requested;
	}
	float paid = due > 0.0f ? std::min(due, remaining_cash) : 0.0f;
	if(paid > 0.0f && !accounts::transfer(state, operator_account, labor_account, paid, relations::transaction_kind::payroll, state.current_date)) paid = 0.0f;
	float unpaid = due - paid;
	dcon::obligation_id claim{};
	if(unpaid > 1.0e-6f) claim = relations::create_obligation(state, operator_actor, labor_actor, unpaid, settlement, state.current_date, state.current_date, 0.0f, relations::obligation_kind::payroll);
	float scale = due > 0.0f ? paid / due : 0.0f;
	record_event(state, factory, province, operator_actor, settlement, claim, due, paid, unpaid, gross_no, gross_basic, gross_high, gross_no * scale, gross_basic * scale, gross_high * scale);
	state.world.factory_set_last_payroll_date(factory, state.current_date);
	state.world.factory_set_payroll_initialized(factory, 1);
}
} // namespace economy::payroll
