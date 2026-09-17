#include "concrete_labor.hpp"

#include "accounts/accounts.hpp"
#include "concrete_market.hpp"
#include "economy/relations/relations.hpp"
#include "persons/persons.hpp"
#include "system_state.hpp"
#include "world/site.hpp"

#include <algorithm>
#include <cmath>

namespace economy::physical::concrete_labor {
namespace {
constexpr float epsilon = 1.0e-6f;

bool finite_positive(float value) {
	return std::isfinite(value) && value > 0.0f;
}

bool active_on(sys::state const& state, dcon::employment_contract_id contract) {
	if(!contract || !state.world.employment_contract_is_valid(contract)
		|| state.world.employment_contract_get_status(contract) != uint8_t(contract_status::active)) return false;
	auto person = state.world.employment_contract_get_person_from_employment_contract_person(contract);
	if(!person || !state.world.person_get_alive(person)) return false;
	if(state.current_date && state.world.employment_contract_get_start_date(contract) > state.current_date) return false;
	auto end_date = state.world.employment_contract_get_end_date(contract);
	return !end_date || end_date > state.current_date;
}

dcon::obligation_id active_wage_obligation(sys::state const& state, dcon::employment_contract_id contract) {
	auto obligation = state.world.employment_contract_get_obligation_from_employment_contract_obligation(contract);
	return obligation && state.world.obligation_get_status(obligation) == uint8_t(relations::obligation_status::active)
		? obligation : dcon::obligation_id{};
}

dcon::obligation_id ensure_wage_obligation(sys::state& state, dcon::employment_contract_id contract, float amount) {
	if(!finite_positive(amount)) return active_wage_obligation(state, contract);
	auto employer = state.world.employment_contract_get_economic_actor_from_employment_contract_employer(contract);
	auto person = state.world.employment_contract_get_person_from_employment_contract_person(contract);
	auto worker = persons::actor_for_person(state, person);
	auto payer = state.world.employment_contract_get_monetary_account_from_employment_contract_payer_account(contract);
	auto settlement = accounts::settlement_of(state, payer);
	if(!employer || !worker || !settlement) return {};
	if(auto obligation = active_wage_obligation(state, contract)) {
		state.world.obligation_set_principal_outstanding(obligation,
			state.world.obligation_get_principal_outstanding(obligation) + amount);
		return obligation;
	}
	auto obligation = relations::create_obligation(state, employer, worker, amount, settlement,
		state.current_date, state.current_date, 0.0f, relations::obligation_kind::payroll);
	if(obligation) state.world.force_create_employment_contract_obligation(contract, obligation);
	return obligation;
}

float free_cash(sys::state const& state, dcon::monetary_account_id account) {
	if(!account) return 0.0f;
	return std::max(0.0f, accounts::balance(state, account)
		- concrete_market::reserved_bid_amount(state, account));
}
}

dcon::employment_contract_id create_employment_contract(sys::state& state, dcon::person_id person,
	dcon::economic_actor_id employer, dcon::factory_id factory, dcon::site_id workplace,
	uint8_t occupation, float labor_capacity, float wage_rate, uint16_t pay_period_days,
	dcon::monetary_account_id payer_account, dcon::monetary_account_id worker_account,
	sys::date start_date) {
	if(!start_date) start_date = state.current_date;
	if(!start_date) start_date = sys::date{0};
	if(!person || !state.world.person_is_valid(person) || !state.world.person_get_alive(person)
		|| !employer || !factory || !state.world.factory_is_valid(factory)
		|| !std::isfinite(labor_capacity) || labor_capacity <= 0.0f
		|| !std::isfinite(wage_rate) || wage_rate < 0.0f || pay_period_days == 0
		|| !payer_account || !worker_account
		|| start_date < state.world.person_get_birth_date(person)) return {};
	if(!workplace) workplace = world::site::site_for_factory(state, factory);
	if(!workplace || !state.world.site_is_valid(workplace)) return {};
	auto worker_actor = persons::actor_for_person(state, person);
	auto payer_settlement = accounts::settlement_of(state, payer_account);
	auto factory_settlement = state.world.factory_get_payroll_settlement(factory);
	if(!worker_actor || accounts::owner_of(state, payer_account) != employer
		|| accounts::owner_of(state, worker_account) != worker_actor
		|| !payer_settlement || payer_settlement != accounts::settlement_of(state, worker_account)
		|| (factory_settlement && factory_settlement != payer_settlement)) return {};
	auto contract = state.world.create_employment_contract();
	state.world.employment_contract_set_occupation(contract, occupation);
	state.world.employment_contract_set_labor_capacity(contract, labor_capacity);
	state.world.employment_contract_set_wage_rate(contract, wage_rate);
	state.world.employment_contract_set_pay_period_days(contract, pay_period_days);
	state.world.employment_contract_set_start_date(contract, start_date);
	state.world.employment_contract_set_end_date(contract, {});
	state.world.employment_contract_set_status(contract, uint8_t(contract_status::active));
	state.world.force_create_employment_contract_person(contract, person);
	state.world.force_create_employment_contract_employer(contract, employer);
	state.world.force_create_employment_contract_factory(contract, factory);
	state.world.force_create_employment_contract_site(contract, workplace);
	state.world.force_create_employment_contract_payer_account(contract, payer_account);
	state.world.force_create_employment_contract_worker_account(contract, worker_account);
	return contract;
}

bool end_employment_contract(sys::state& state, dcon::employment_contract_id contract,
	contract_status status, sys::date end_date) {
	if(!contract || !state.world.employment_contract_is_valid(contract)
		|| status == contract_status::active || !end_date
		|| end_date < state.world.employment_contract_get_start_date(contract)) return false;
	state.world.employment_contract_set_status(contract, uint8_t(status));
	state.world.employment_contract_set_end_date(contract, end_date);
	return true;
}

bool terminate_employment_contract(sys::state& state, dcon::employment_contract_id contract, sys::date end_date) {
	return end_employment_contract(state, contract, contract_status::terminated, end_date);
}

std::vector<dcon::employment_contract_id> contracts_for_factory(sys::state const& state, dcon::factory_id factory) {
	std::vector<dcon::employment_contract_id> result;
	if(!factory) return result;
	state.world.factory_for_each_employment_contract_factory_as_factory(factory, [&](auto relation) {
		auto contract = state.world.employment_contract_factory_get_employment_contract(relation);
		if(contract) result.push_back(contract);
	});
	std::sort(result.begin(), result.end(), [](auto left, auto right) { return left.index() < right.index(); });
	return result;
}

std::vector<dcon::employment_contract_id> active_contracts_for_factory(sys::state const& state, dcon::factory_id factory) {
	std::vector<dcon::employment_contract_id> result;
	for(auto contract : contracts_for_factory(state, factory))
		if(active_on(state, contract)) result.push_back(contract);
	return result;
}

std::vector<dcon::person_id> active_workers_for_factory(sys::state const& state, dcon::factory_id factory) {
	std::vector<dcon::person_id> result;
	for(auto contract : active_contracts_for_factory(state, factory))
		result.push_back(state.world.employment_contract_get_person_from_employment_contract_person(contract));
	return result;
}

float labor_supplied_to_factory(sys::state const& state, dcon::factory_id factory) {
	float result = 0.0f;
	for(auto contract : active_contracts_for_factory(state, factory)) {
		auto amount = state.world.employment_contract_get_labor_capacity(contract);
		if(std::isfinite(amount) && amount > 0.0f) result += amount;
	}
	return std::isfinite(result) ? result : 0.0f;
}

float wage_due(sys::state const& state, dcon::employment_contract_id contract) {
	if(!active_on(state, contract)) return 0.0f;
	auto rate = state.world.employment_contract_get_wage_rate(contract);
	auto capacity = state.world.employment_contract_get_labor_capacity(contract);
	auto period = state.world.employment_contract_get_pay_period_days(contract);
	if(!std::isfinite(rate) || rate < 0.0f || !std::isfinite(capacity) || capacity <= 0.0f || period == 0) return 0.0f;
	auto result = rate * capacity / float(period);
	return std::isfinite(result) && result > 0.0f ? result : 0.0f;
}

float wage_due_for_factory(sys::state const& state, dcon::factory_id factory) {
	float result = 0.0f;
	for(auto contract : active_contracts_for_factory(state, factory)) result += wage_due(state, contract);
	return std::isfinite(result) ? result : 0.0f;
}

float wage_cost_for_factory(sys::state const& state, dcon::factory_id factory, float production_units, float production_capacity) {
	if(!std::isfinite(production_units) || !std::isfinite(production_capacity) || production_capacity <= epsilon) return 0.0f;
	return wage_due_for_factory(state, factory) * std::clamp(production_units / production_capacity, 0.0f, 1.0f);
}

wage_settlement settle_contract_wage(sys::state& state, dcon::employment_contract_id contract) {
	wage_settlement result{};
	result.due = wage_due(state, contract);
	if(result.due <= epsilon) return result;
	auto payer = state.world.employment_contract_get_monetary_account_from_employment_contract_payer_account(contract);
	auto worker = state.world.employment_contract_get_monetary_account_from_employment_contract_worker_account(contract);
	if(!payer || !worker) { result.unpaid = result.due; result.obligation = ensure_wage_obligation(state, contract, result.unpaid); return result; }
	auto available = free_cash(state, payer);
	if(auto arrears = active_wage_obligation(state, contract); arrears && available > epsilon) {
		auto repayment = std::min(available, std::max(0.0f, relations::total_due(state, arrears)));
		if(repayment > epsilon && accounts::settle_obligation_payment(state, arrears, payer, worker, repayment, state.current_date))
			available -= repayment;
	}
	result.paid = std::min(result.due, available);
	if(result.paid > epsilon && !accounts::transfer(state, payer, worker, result.paid,
		relations::transaction_kind::payroll, state.current_date)) result.paid = 0.0f;
	result.unpaid = std::max(0.0f, result.due - result.paid);
	result.obligation = ensure_wage_obligation(state, contract, result.unpaid);
	return result;
}

} // namespace economy::physical::concrete_labor
