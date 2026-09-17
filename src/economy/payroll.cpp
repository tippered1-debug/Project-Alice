#include "payroll.hpp"

#include "system_state.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/relations/relations.hpp"
#include "actors/organizations/organizations.hpp"
#include "compat/alice/legacy_bridge.hpp"
#include "world/site.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace economy::payroll {
namespace {
std::vector<float> paid_no;
std::vector<float> paid_basic;
std::vector<float> paid_high;
std::vector<uint8_t> present;
sys::date active_date{};
bool initialized = false;

bool finite_positive(float v) { return std::isfinite(v) && v > 0.0f; }

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
	return actor ? accounts::find_account(state, actor, settlement) : dcon::monetary_account_id{};
}

dcon::commodity_id factory_settlement(sys::state const& state, dcon::factory_id factory) {
	auto settlement = state.world.factory_get_payroll_settlement(factory);
	return settlement && state.world.commodity_is_valid(settlement) ? settlement : dcon::commodity_id{};
}

dcon::obligation_id oldest_arrears(sys::state const& state, dcon::economic_actor_id debtor,
	dcon::economic_actor_id creditor, dcon::commodity_id settlement) {
	dcon::obligation_id result{};
	sys::date oldest{};
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
}

void begin_day(sys::state& state) {
	if(initialized && active_date == state.current_date) return;
	paid_no.assign(state.world.province_size() + 1, 0.0f);
	paid_basic.assign(state.world.province_size() + 1, 0.0f);
	paid_high.assign(state.world.province_size() + 1, 0.0f);
	present.assign(state.world.province_size() + 1, 0);
	active_date = state.current_date;
	initialized = true;
}

province_payroll for_province(sys::state const&, dcon::province_id province) {
	if(!initialized || !province) return {};
	return { paid_no[province.index()], paid_basic[province.index()], paid_high[province.index()], present[province.index()] != 0 };
}

void settle_factory(sys::state& state, dcon::factory_id factory, float actual_units, float available_units) {
	if(!factory || !state.world.factory_get_canonical_production(factory)) return;
	if(state.world.factory_get_last_payroll_date(factory) == state.current_date) return;
	begin_day(state);
	auto province = compat::alice::province_for_factory(state, factory);
	if(province) present[province.index()] = 1;
	auto operator_actor = actors::organizations::operator_actor_for_factory(state, factory);
	auto settlement = factory_settlement(state, factory);
	if(!province || !operator_actor || !settlement) return;
	auto operator_account = accounts::find_account(state, operator_actor, settlement);
	auto labor_actor = labor_actor_for(state, province);
	if(!operator_account || !labor_actor) return;
	auto labor_account = labor_account_for(state, province, settlement);
	if(!labor_account) labor_account = accounts::open_account(state, labor_actor, settlement);
	if(!labor_account) return;
	float ratio = available_units > 0.0f ? std::clamp(actual_units / available_units, 0.0f, 1.0f) : 0.0f;
	float no_workers = std::max(0.0f, state.world.factory_get_unqualified_employment(factory)) * ratio;
	float basic_workers = std::max(0.0f, state.world.factory_get_primary_employment(factory)) * ratio;
	float high_workers = std::max(0.0f, state.world.factory_get_secondary_employment(factory)) * ratio;
	float due = no_workers * state.world.province_get_labor_price(province, economy::labor::no_education)
		+ basic_workers * state.world.province_get_labor_price(province, economy::labor::basic_education)
		+ high_workers * state.world.province_get_labor_price(province, economy::labor::high_education);
	if(!std::isfinite(due) || due < 0.0f) due = 0.0f;
	float available_cash = accounts::balance(state, operator_account);
	float remaining_cash = available_cash;
	while(remaining_cash > 0.0f) {
		auto arrears = oldest_arrears(state, operator_actor, labor_actor, settlement);
		if(!arrears) break;
		auto creditor_account = labor_account;
		auto requested = std::min(remaining_cash, relations::total_due(state, arrears));
		if(!accounts::settle_obligation_payment(state, arrears, operator_account, creditor_account, requested, state.current_date)) break;
		remaining_cash -= requested;
	}
	float paid = 0.0f;
	if(due > 0.0f && remaining_cash > 0.0f) {
		auto tx = accounts::transfer(state, operator_account, labor_account, std::min(due, remaining_cash), relations::transaction_kind::payroll, state.current_date);
		if(tx) paid = std::min(due, remaining_cash);
	}
	float unpaid = due - paid;
	if(unpaid > 1.0e-6f)
		relations::create_obligation(state, operator_actor, labor_actor, unpaid, settlement, state.current_date, state.current_date, 0.0f, relations::obligation_kind::payroll);
	auto event = state.world.create_payroll_event();
	state.world.payroll_event_set_settlement(event, settlement);
	state.world.payroll_event_set_gross_due(event, due);
	state.world.payroll_event_set_paid(event, paid);
	state.world.payroll_event_set_unpaid(event, unpaid);
	state.world.payroll_event_set_occurred_on(event, state.current_date);
	state.world.force_create_payroll_event_factory(event, factory);
	state.world.force_create_payroll_event_operator(event, operator_actor);
	state.world.force_create_payroll_event_province(event, province);
	state.world.factory_set_last_payroll_date(factory, state.current_date);
	present[province.index()] = 1;
	paid_no[province.index()] += paid * (no_workers * state.world.province_get_labor_price(province, economy::labor::no_education)) / std::max(due, 1.0e-6f);
	paid_basic[province.index()] += paid * (basic_workers * state.world.province_get_labor_price(province, economy::labor::basic_education)) / std::max(due, 1.0e-6f);
	paid_high[province.index()] += paid * (high_workers * state.world.province_get_labor_price(province, economy::labor::high_education)) / std::max(due, 1.0e-6f);
}
} // namespace economy::payroll
