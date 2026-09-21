#include "exact_person_economy.hpp"

#include "accounts/accounts.hpp"
#include "actors/organizations/organizations.hpp"
#include "economy/physical/concrete_market.hpp"
#include "economy/causal_order.hpp"
#include "system_state.hpp"
#include "world/site.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <unordered_map>

namespace economy {

struct exact_person_economy_store {
	std::unordered_map<exact_person_economy::person_key, bool,
		persons::exact_population::person_key_hash> participation_overrides;
	std::vector<exact_person_economy::account_record> accounts;
	std::unordered_map<uint64_t, std::size_t> account_by_id;
	std::vector<exact_person_economy::application_record> applications;
	std::vector<exact_person_economy::contract_record> contracts;
	std::vector<exact_person_economy::transaction_record> transactions;
	std::vector<std::pair<exact_person_economy::person_key, sys::date>> last_separation_dates;
	std::vector<exact_person_economy::person_key> displaced_workers;
	uint64_t next_account_id = 1;
	uint64_t next_application_id = 1;
	uint64_t next_contract_id = 1;
	uint64_t next_transaction_id = 1;
};

} // namespace economy

namespace economy::exact_person_economy {
namespace {

constexpr uint32_t snapshot_version = 1;
constexpr float epsilon = 1.0e-6f;

std::shared_ptr<exact_person_economy_store> ensure_store(sys::state& state) {
	if(!state.exact_person_economy)
		state.exact_person_economy = std::make_shared<exact_person_economy_store>();
	return state.exact_person_economy;
}

std::shared_ptr<exact_person_economy_store> ensure_store(sys::state const& state) {
	return ensure_store(const_cast<sys::state&>(state));
}

bool finite_positive(float amount) {
	return std::isfinite(amount) && amount > 0.0f;
}

bool valid_date_or_current(sys::state const& state, sys::date date) {
	return bool(date) || bool(state.current_date) || date == sys::date{};
}

bool offer_open(sys::state const& state, dcon::job_offer_id offer) {
	if(!offer || !state.world.job_offer_is_valid(offer)
		|| state.world.job_offer_get_status(offer) != 0
		|| state.world.job_offer_get_openings(offer) == 0) return false;
	auto expires = state.world.job_offer_get_expires_on(offer);
	return !(expires && state.current_date && expires < state.current_date);
}

bool active_contract_on(sys::state const& state, contract_record const& record) {
	return record.status == contract_status::active
		&& persons::exact_population::exists(state, record.worker)
		&& persons::exact_population::alive(state, record.worker)
		&& (!state.current_date || !record.start_date || record.start_date <= state.current_date)
		&& (!record.end_date || !state.current_date || record.end_date > state.current_date);
}

account_record* exact_account(sys::state& state, uint64_t id) {
	auto store = ensure_store(state);
	auto it = store->account_by_id.find(id);
	return it == store->account_by_id.end() ? nullptr : &store->accounts[it->second];
}

account_record const* exact_account(sys::state const& state, uint64_t id) {
	return exact_account(const_cast<sys::state&>(state), id);
}

bool valid_account_ref(sys::state const& state, account_ref ref) {
	if(ref.kind == account_kind::dcon)
		return ref.dcon_account && state.world.monetary_account_is_valid(ref.dcon_account)
			&& accounts::settlement_of(state, ref.dcon_account);
	if(ref.kind == account_kind::exact) {
		auto account = exact_account(state, ref.exact_account_id);
		return account && persons::exact_population::exists(state, account->owner)
			&& account->settlement && state.world.commodity_is_valid(account->settlement)
			&& std::isfinite(account->balance) && account->balance >= 0.0f;
	}
	return false;
}

uint64_t next_id(uint64_t& next) {
	if(next == 0 || next == std::numeric_limits<uint64_t>::max()) return 0;
	return next++;
}

bool duplicate_pending_application(sys::state const& state, person_key worker, dcon::job_offer_id offer) {
	for(auto const& application : ensure_store(state)->applications) {
		if(application.worker == worker && application.offer == offer
			&& application.status == application_status::pending) return true;
	}
	return false;
}

bool has_open_pending_application(sys::state const& state, person_key worker) {
	for(auto const& application : ensure_store(state)->applications) {
		if(application.worker == worker && application.status == application_status::pending
			&& offer_open(state, application.offer)) return true;
	}
	return false;
}

bool accepts_exact_worker(sys::state const& state, person_key worker, dcon::job_offer_id offer) {
	return persons::exact_population::exists(state, worker)
		&& persons::exact_population::alive(state, worker)
		&& is_work_eligible(state, worker)
		&& is_labor_force_participant(state, worker)
		&& !separated_on_date(state, worker, state.current_date)
		&& offer_open(state, offer)
		&& !person_has_active_contract(state, worker);
}

uint64_t create_contract_for_offer(sys::state& state, person_key worker, dcon::job_offer_id offer) {
	if(!accepts_exact_worker(state, worker, offer)) return 0;
	auto employer = state.world.job_offer_get_economic_actor_from_job_offer_employer(offer);
	auto factory = state.world.job_offer_get_factory_from_job_offer_factory(offer);
	auto workplace = state.world.job_offer_get_site_from_job_offer_site(offer);
	auto payer = state.world.job_offer_get_monetary_account_from_job_offer_payer_account(offer);
	if(!employer || !factory || !state.world.factory_is_valid(factory) || !workplace
		|| !state.world.site_is_valid(workplace) || !payer
		|| accounts::owner_of(state, payer) != employer) return 0;
	auto settlement = accounts::settlement_of(state, payer);
	if(!settlement || !state.world.commodity_is_valid(settlement)) return 0;
	auto factory_settlement = state.world.factory_get_payroll_settlement(factory);
	if(factory_settlement && factory_settlement != settlement) return 0;
	auto labor_capacity = state.world.job_offer_get_labor_capacity(offer);
	auto wage_rate = state.world.job_offer_get_wage_rate(offer);
	auto pay_period_days = state.world.job_offer_get_pay_period_days(offer);
	if(!std::isfinite(labor_capacity) || labor_capacity <= 0.0f
		|| !std::isfinite(wage_rate) || wage_rate < 0.0f || pay_period_days == 0) return 0;
	auto store = ensure_store(state);
	for(auto const& existing : store->contracts)
		if(existing.worker == worker && active_contract_on(state, existing)) return 0;
	auto worker_account = open_account(state, worker, settlement);
	if(worker_account.kind != account_kind::exact) return 0;
	contract_record record;
	record.id = next_id(store->next_contract_id);
	if(record.id == 0) return 0;
	record.worker = worker;
	record.employer = employer;
	record.factory = factory;
	record.workplace = workplace;
	record.occupation = state.world.job_offer_get_occupation(offer);
	record.labor_capacity = labor_capacity;
	record.wage_rate = wage_rate;
	record.pay_period_days = pay_period_days;
	record.payer_account = payer;
	record.worker_account_id = worker_account.exact_account_id;
	record.start_date = state.current_date;
	record.causal_sequence = causal_order::allocate(state, causal_order::event_kind::employment_contract);
	if(record.causal_sequence == 0) return 0;
	if(!valid_date_or_current(state, record.start_date)
		|| !std::isfinite(record.labor_capacity) || record.labor_capacity <= 0.0f
		|| !std::isfinite(record.wage_rate) || record.wage_rate < 0.0f
		|| record.pay_period_days == 0) return 0;
	store->contracts.push_back(record);
	return record.id;
}

void sort_ids(std::vector<uint64_t>& ids) {
	std::sort(ids.begin(), ids.end());
}

} // namespace

bool is_work_eligible(sys::state const& state, person_key worker) {
	if(!persons::exact_population::exists(state, worker)
		|| !persons::exact_population::alive(state, worker)) return false;
	auto age = persons::exact_population::age_days(state, worker, state.current_date);
	return age >= persons::policy::minimum_working_age_days
		&& age < persons::policy::maximum_working_age_days;
}

bool is_labor_force_participant(sys::state const& state, person_key worker) {
	if(!persons::exact_population::exists(state, worker)) return false;
	auto store = ensure_store(state);
	if(auto it = store->participation_overrides.find(worker); it != store->participation_overrides.end())
		return it->second;
	return persons::exact_population::is_source_workforce_anchor(state, worker) && is_work_eligible(state, worker);
}

bool is_unemployed(sys::state const& state, person_key worker) {
	return persons::exact_population::exists(state, worker)
		&& persons::exact_population::alive(state, worker)
		&& is_work_eligible(state, worker)
		&& is_labor_force_participant(state, worker)
		&& !person_has_active_contract(state, worker);
}

bool set_labor_force_participation(sys::state& state, person_key worker, bool participating) {
	if(!persons::exact_population::exists(state, worker)) return false;
	auto default_value = persons::exact_population::is_source_workforce_anchor(state, worker)
		&& is_work_eligible(state, worker);
	auto store = ensure_store(state);
	if(participating == default_value) store->participation_overrides.erase(worker);
	else store->participation_overrides[worker] = participating;
	return true;
}

uint64_t participation_override_count(sys::state const& state) {
	return uint64_t(ensure_store(state)->participation_overrides.size());
}

account_ref open_account(sys::state& state, person_key owner, dcon::commodity_id settlement) {
	if(!persons::exact_population::exists(state, owner) || !settlement
		|| !state.world.commodity_is_valid(settlement)) return {};
	auto store = ensure_store(state);
	for(auto const& account : store->accounts)
		if(account.owner == owner && account.settlement == settlement) return account_ref::from_exact(account.id);
	account_record record;
	record.id = next_id(store->next_account_id);
	if(record.id == 0) return {};
	record.owner = owner;
	record.settlement = settlement;
	record.balance = 0.0f;
	store->account_by_id.emplace(record.id, store->accounts.size());
	store->accounts.push_back(record);
	return account_ref::from_exact(record.id);
}

account_ref find_account(sys::state const& state, person_key owner, dcon::commodity_id settlement) {
	for(auto const& account : ensure_store(state)->accounts)
		if(account.owner == owner && account.settlement == settlement) return account_ref::from_exact(account.id);
	return {};
}

bool account_exists(sys::state const& state, account_ref ref) {
	return valid_account_ref(state, ref);
}

person_key owner_of(sys::state const& state, account_ref ref) {
	if(ref.kind == account_kind::exact) {
		auto account = exact_account(state, ref.exact_account_id);
		return account ? account->owner : person_key{};
	}
	return {};
}

dcon::commodity_id settlement_of(sys::state const& state, account_ref ref) {
	if(ref.kind == account_kind::dcon) return accounts::settlement_of(state, ref.dcon_account);
	if(ref.kind == account_kind::exact) {
		auto account = exact_account(state, ref.exact_account_id);
		return account ? account->settlement : dcon::commodity_id{};
	}
	return {};
}

float balance(sys::state const& state, account_ref ref) {
	if(ref.kind == account_kind::dcon) return accounts::balance(state, ref.dcon_account);
	if(ref.kind == account_kind::exact) {
		auto account = exact_account(state, ref.exact_account_id);
		return account ? account->balance : 0.0f;
	}
	return 0.0f;
}

bool set_balance(sys::state& state, account_ref ref, float amount) {
	if(!std::isfinite(amount) || amount < 0.0f || !valid_account_ref(state, ref)) return false;
	if(ref.kind == account_kind::dcon) {
		state.world.monetary_account_set_balance(ref.dcon_account, amount);
		return true;
	}
	if(auto account = exact_account(state, ref.exact_account_id)) {
		account->balance = amount;
		return true;
	}
	return false;
}

uint64_t account_count(sys::state const& state) {
	return uint64_t(ensure_store(state)->accounts.size());
}

std::vector<account_ref> accounts_for_person(sys::state const& state, person_key owner) {
	std::vector<account_ref> result;
	for(auto const& account : ensure_store(state)->accounts)
		if(account.owner == owner) result.push_back(account_ref::from_exact(account.id));
	std::sort(result.begin(), result.end(), [](auto left, auto right) {
		return left.exact_account_id < right.exact_account_id;
	});
	return result;
}

transfer_result transfer_with_result(sys::state& state, account_ref source, account_ref destination, float amount,
	relations::transaction_kind kind, sys::date timestamp) {
	transfer_result result;
	auto source_balance = balance(state, source);
	auto destination_balance = balance(state, destination);
	if(!valid_account_ref(state, source) || !valid_account_ref(state, destination)
		|| source == destination || !finite_positive(amount)
		|| settlement_of(state, source) != settlement_of(state, destination)
		|| !std::isfinite(source_balance) || !std::isfinite(destination_balance)
		|| source_balance < amount
		|| amount > std::numeric_limits<float>::max() - destination_balance) return result;
	if(source.kind == account_kind::dcon && destination.kind == account_kind::dcon) {
		result.dcon_transaction_id = accounts::transfer(state, source.dcon_account, destination.dcon_account, amount, kind, timestamp);
		result.success = bool(result.dcon_transaction_id);
		return result;
	}
	auto store = ensure_store(state);
	auto transaction_id = next_id(store->next_transaction_id);
	if(transaction_id == 0) return result;
	if(source.kind == account_kind::exact) {
		auto account = exact_account(state, source.exact_account_id);
		account->balance -= amount;
	} else {
		state.world.monetary_account_set_balance(source.dcon_account, source_balance - amount);
	}
	if(destination.kind == account_kind::exact) {
		auto account = exact_account(state, destination.exact_account_id);
		account->balance += amount;
	} else {
		state.world.monetary_account_set_balance(destination.dcon_account, destination_balance + amount);
	}
	transaction_record record;
	record.id = transaction_id;
	record.source = source;
	record.destination = destination;
	record.amount = amount;
	record.settlement = settlement_of(state, source);
	record.kind = kind;
	record.timestamp = timestamp;
	store->transactions.push_back(record);
	result.success = true;
	result.exact_transaction_id = transaction_id;
	return result;
}

bool transfer(sys::state& state, account_ref source, account_ref destination, float amount,
	relations::transaction_kind kind, sys::date timestamp) {
	return transfer_with_result(state, source, destination, amount, kind, timestamp).success;
}

uint64_t transaction_count(sys::state const& state) {
	return uint64_t(ensure_store(state)->transactions.size());
}

std::optional<transaction_record> latest_transaction(sys::state const& state) {
	auto store = ensure_store(state);
	return store->transactions.empty() ? std::nullopt
		: std::optional<transaction_record>(store->transactions.back());
}

std::optional<transaction_record> transaction(sys::state const& state, uint64_t transaction_id) {
	if(transaction_id == 0) return std::nullopt;
	for(auto const& record : ensure_store(state)->transactions)
		if(record.id == transaction_id) return record;
	return std::nullopt;
}

uint64_t submit_application(sys::state& state, person_key worker, dcon::job_offer_id offer, sys::date applied_on) {
	if(!accepts_exact_worker(state, worker, offer) || duplicate_pending_application(state, worker, offer)) return 0;
	if(!applied_on) applied_on = state.current_date;
	if(!applied_on || applied_on < state.world.job_offer_get_created_on(offer)) return 0;
	auto store = ensure_store(state);
	application_record record;
	record.id = next_id(store->next_application_id);
	if(record.id == 0) return 0;
	record.worker = worker;
	record.offer = offer;
	record.applied_on = applied_on;
	record.causal_sequence = causal_order::allocate(state, causal_order::event_kind::job_application);
	if(record.causal_sequence == 0) return 0;
	store->applications.push_back(record);
	return record.id;
}

bool withdraw_application(sys::state& state, uint64_t application_id) {
	for(auto& application : ensure_store(state)->applications)
		if(application.id == application_id && application.status == application_status::pending) {
			application.status = application_status::withdrawn;
			return true;
		}
	return false;
}

std::optional<application_record> application(sys::state const& state, uint64_t application_id) {
	for(auto const& record : ensure_store(state)->applications)
		if(record.id == application_id) return record;
	return std::nullopt;
}

std::vector<uint64_t> applications_for_person(sys::state const& state, person_key worker) {
	std::vector<uint64_t> result;
	for(auto const& application : ensure_store(state)->applications)
		if(application.worker == worker) result.push_back(application.id);
	sort_ids(result);
	return result;
}

std::vector<uint64_t> applications_for_offer(sys::state const& state, dcon::job_offer_id offer) {
	std::vector<uint64_t> result;
	for(auto const& application : ensure_store(state)->applications)
		if(application.offer == offer) result.push_back(application.id);
	sort_ids(result);
	return result;
}

bool accept_pending_application(sys::state& state, uint64_t id) {
	auto record = application(state, id);
	if(!record || record->status != application_status::pending) return false;
	if(!accepts_exact_worker(state, record->worker, record->offer)) {
		for(auto& mutable_record : ensure_store(state)->applications)
			if(mutable_record.id == id) mutable_record.status = application_status::rejected;
		return false;
	}
	auto contract_id = create_contract_for_offer(state, record->worker, record->offer);
	if(contract_id == 0) {
		for(auto& mutable_record : ensure_store(state)->applications)
			if(mutable_record.id == id) mutable_record.status = application_status::rejected;
		return false;
	}
	for(auto& mutable_record : ensure_store(state)->applications)
		if(mutable_record.id == id) mutable_record.status = application_status::accepted;
	state.world.job_offer_set_openings(record->offer, state.world.job_offer_get_openings(record->offer) - 1);
	return true;
}

void process_pending_applications(sys::state& state) {
	std::vector<uint64_t> pending;
	for(auto const& application : ensure_store(state)->applications)
		if(application.status == application_status::pending) pending.push_back(application.id);
	std::sort(pending.begin(), pending.end(), [&](uint64_t left, uint64_t right) {
		auto a = application(state, left); auto b = application(state, right);
		if(causal_order::before({a->applied_on, a->causal_sequence}, {b->applied_on, b->causal_sequence})) return true;
		if(causal_order::before({b->applied_on, b->causal_sequence}, {a->applied_on, a->causal_sequence})) return false;
		return left < right;
	});
	for(auto id : pending) (void)accept_pending_application(state, id);
}

void process_job_search_for_exact_person(sys::state& state, person_key worker) {
	if(!persons::exact_population::exists(state, worker) || !is_work_eligible(state, worker)
		|| !is_labor_force_participant(state, worker) || person_has_active_contract(state, worker)
		|| has_open_pending_application(state, worker)) return;
	std::vector<dcon::job_offer_id> offers;
	state.world.for_each_job_offer([&](auto offer) { if(offer_open(state, offer)) offers.push_back(offer); });
	std::sort(offers.begin(), offers.end(), [](auto left, auto right) { return left.index() < right.index(); });
	dcon::job_offer_id best{};
	for(auto offer : offers) {
		if(!accepts_exact_worker(state, worker, offer)) continue;
		if(!best || state.world.job_offer_get_wage_rate(offer) > state.world.job_offer_get_wage_rate(best)
			|| (state.world.job_offer_get_wage_rate(offer) == state.world.job_offer_get_wage_rate(best)
				&& offer.index() < best.index())) best = offer;
	}
	if(best) (void)submit_application(state, worker, best, state.current_date);
}

std::optional<contract_record> contract(sys::state const& state, uint64_t contract_id) {
	for(auto const& record : ensure_store(state)->contracts)
		if(record.id == contract_id) return record;
	return std::nullopt;
}

std::vector<uint64_t> active_contracts_for_factory(sys::state const& state, dcon::factory_id factory) {
	std::vector<uint64_t> result;
	for(auto const& record : ensure_store(state)->contracts)
		if(record.factory == factory && active_contract_on(state, record)) result.push_back(record.id);
	sort_ids(result);
	return result;
}

std::vector<uint64_t> contracts_for_factory(sys::state const& state, dcon::factory_id factory) {
	std::vector<uint64_t> result;
	for(auto const& record : ensure_store(state)->contracts)
		if(record.factory == factory) result.push_back(record.id);
	sort_ids(result);
	return result;
}

std::vector<uint64_t> active_contracts_for_person(sys::state const& state, person_key worker) {
	std::vector<uint64_t> result;
	for(auto const& record : ensure_store(state)->contracts)
		if(record.worker == worker && active_contract_on(state, record)) result.push_back(record.id);
	sort_ids(result);
	return result;
}

bool person_has_active_contract(sys::state const& state, person_key worker) {
	return !active_contracts_for_person(state, worker).empty();
}

float labor_supplied_to_factory(sys::state const& state, dcon::factory_id factory) {
	float result = 0.0f;
	for(auto id : active_contracts_for_factory(state, factory)) {
		auto record = contract(state, id);
		if(record && std::isfinite(record->labor_capacity) && record->labor_capacity > 0.0f)
			result += record->labor_capacity;
	}
	return std::isfinite(result) ? result : 0.0f;
}

float wage_due(sys::state const& state, uint64_t contract_id) {
	auto record = contract(state, contract_id);
	if(!record || !active_contract_on(state, *record) || !std::isfinite(record->wage_rate)
		|| record->wage_rate < 0.0f || !std::isfinite(record->labor_capacity)
		|| record->labor_capacity <= 0.0f || record->pay_period_days == 0) return 0.0f;
	auto due = record->wage_rate * record->labor_capacity / float(record->pay_period_days);
	return std::isfinite(due) && due > 0.0f ? due : 0.0f;
}

float wage_due_for_factory(sys::state const& state, dcon::factory_id factory) {
	float result = 0.0f;
	for(auto id : active_contracts_for_factory(state, factory)) result += wage_due(state, id);
	return std::isfinite(result) ? result : 0.0f;
}

float unpaid_wages_for_factory(sys::state const& state, dcon::factory_id factory) {
	float result = 0.0f;
	for(auto const& record : ensure_store(state)->contracts)
		if(record.factory == factory && std::isfinite(record.unpaid_wages) && record.unpaid_wages > 0.0f)
			result += record.unpaid_wages;
	return std::isfinite(result) ? result : 0.0f;
}

wage_settlement settle_contract_arrears_only(sys::state& state, uint64_t contract_id) {
	wage_settlement result;
	auto record = contract(state, contract_id);
	if(!record) return result;
	result.arrears_before = std::max(0.0f, record->unpaid_wages);
	if(!std::isfinite(result.arrears_before) || result.arrears_before <= epsilon) {
		result.arrears_after = 0.0f;
		return result;
	}
	auto payer = account_ref::from_dcon(record->payer_account);
	auto worker = account_ref::from_exact(record->worker_account_id);
	if(account_exists(state, payer) && account_exists(state, worker)) {
		auto available = std::max(0.0f, balance(state, payer)
			- physical::concrete_market::reserved_bid_amount(state, record->payer_account));
		result.total_transferred = std::min(result.arrears_before, available);
		if(result.total_transferred > epsilon) {
			auto transfer_result = transfer_with_result(state, payer, worker, result.total_transferred,
				relations::transaction_kind::payroll, state.current_date);
			if(transfer_result.success) result.transaction_id = transfer_result.exact_transaction_id;
			else result.total_transferred = 0.0f;
		} else result.total_transferred = 0.0f;
		result.arrears_repaid = result.total_transferred;
	}
	result.paid = result.total_transferred;
	result.arrears_after = std::max(0.0f, result.arrears_before - result.arrears_repaid);
	result.unpaid = result.arrears_after;
	for(auto& mutable_record : ensure_store(state)->contracts) {
		if(mutable_record.id != contract_id) continue;
		mutable_record.unpaid_wages = result.unpaid;
		if(result.unpaid <= epsilon) mutable_record.arrears_since = {};
		else if(!mutable_record.arrears_since)
			mutable_record.arrears_since = state.current_date ? state.current_date : mutable_record.start_date;
	}
	return result;
}

wage_settlement settle_current_contract_wage_only(sys::state& state, uint64_t contract_id) {
	wage_settlement result;
	auto record = contract(state, contract_id);
	if(!record) return result;
	result.current_due = wage_due(state, contract_id);
	result.due = result.current_due;
	result.arrears_before = std::max(0.0f, record->unpaid_wages);
	if(!std::isfinite(result.arrears_before)) result.arrears_before = 0.0f;
	if(!std::isfinite(result.current_due) || result.current_due <= epsilon) {
		result.arrears_after = result.arrears_before;
		result.unpaid = result.arrears_after;
		return result;
	}
	auto payer = account_ref::from_dcon(record->payer_account);
	auto worker = account_ref::from_exact(record->worker_account_id);
	if(account_exists(state, payer) && account_exists(state, worker)) {
		auto available = std::max(0.0f, balance(state, payer)
			- physical::concrete_market::reserved_bid_amount(state, record->payer_account));
		result.current_paid = std::min(result.current_due, available);
		if(result.current_paid > epsilon) {
			auto transfer_result = transfer_with_result(state, payer, worker, result.current_paid,
				relations::transaction_kind::payroll, state.current_date);
			if(transfer_result.success) result.transaction_id = transfer_result.exact_transaction_id;
			else result.current_paid = 0.0f;
		} else result.current_paid = 0.0f;
	}
	result.total_transferred = result.current_paid;
	result.paid = result.current_paid;
	result.arrears_after = result.arrears_before + std::max(0.0f, result.current_due - result.current_paid);
	result.unpaid = result.arrears_after;
	for(auto& mutable_record : ensure_store(state)->contracts) {
		if(mutable_record.id != contract_id) continue;
		mutable_record.unpaid_wages = result.unpaid;
		if(result.unpaid <= epsilon) mutable_record.arrears_since = {};
		else if(!mutable_record.arrears_since)
			mutable_record.arrears_since = state.current_date ? state.current_date : mutable_record.start_date;
	}
	return result;
}

wage_settlement settle_contract_wage(sys::state& state, uint64_t contract_id) {
	auto arrears = settle_contract_arrears_only(state, contract_id);
	auto current = settle_current_contract_wage_only(state, contract_id);
	current.arrears_before = arrears.arrears_before;
	current.arrears_repaid = arrears.arrears_repaid;
	current.total_transferred += arrears.total_transferred;
	current.paid = current.total_transferred;
	current.transaction_id = current.transaction_id ? current.transaction_id : arrears.transaction_id;
	return current;
}

bool end_contract(sys::state& state, uint64_t contract_id, contract_status new_status, sys::date end_date) {
	if(new_status == contract_status::active || !end_date) return false;
	for(auto& record : ensure_store(state)->contracts)
		if(record.id == contract_id && record.status == contract_status::active
			&& end_date >= record.start_date) {
			record.status = new_status;
			record.end_date = end_date;
			return true;
		}
	return false;
}

bool separated_on_date(sys::state const& state, person_key worker, sys::date date) {
	if(!date) return false;
	for(auto const& [key, separated] : ensure_store(state)->last_separation_dates)
		if(key == worker) return separated == date;
	return false;
}

void note_separation(sys::state& state, person_key worker, sys::date date) {
	if(!persons::exact_population::exists(state, worker) || !date) return;
	for(auto& [key, separated] : ensure_store(state)->last_separation_dates)
		if(key == worker) { separated = date; return; }
	ensure_store(state)->last_separation_dates.emplace_back(worker, date);
}

void enqueue_displaced_worker(sys::state& state, person_key worker) {
	if(!persons::exact_population::exists(state, worker)) return;
	for(auto existing : ensure_store(state)->displaced_workers)
		if(existing == worker) return;
	ensure_store(state)->displaced_workers.push_back(worker);
	std::sort(ensure_store(state)->displaced_workers.begin(), ensure_store(state)->displaced_workers.end(),
		[](auto left, auto right) {
			return left.source_population_cell == right.source_population_cell
				? left.ordinal < right.ordinal : left.source_population_cell < right.source_population_cell;
		});
}

void remove_displaced_worker(sys::state& state, person_key worker) {
	auto& queue = ensure_store(state)->displaced_workers;
	queue.erase(std::remove(queue.begin(), queue.end(), worker), queue.end());
}

std::vector<person_key> displaced_workers(sys::state const& state) {
	return ensure_store(state)->displaced_workers;
}

bool withdraw_pending_applications(sys::state& state, person_key worker) {
	bool changed = false;
	for(auto& application : ensure_store(state)->applications)
		if(application.worker == worker && application.status == application_status::pending) {
			application.status = application_status::withdrawn;
			changed = true;
		}
	return changed;
}

economy_snapshot export_snapshot(sys::state const& state) {
	economy_snapshot result;
	result.version = snapshot_version;
	auto store = ensure_store(state);
	for(auto const& [key, value] : store->participation_overrides)
		result.participation_overrides.emplace_back(key, value);
	result.accounts = store->accounts;
	result.applications = store->applications;
	result.contracts = store->contracts;
	result.transactions = store->transactions;
	result.last_separation_dates = store->last_separation_dates;
	result.displaced_workers = store->displaced_workers;
	std::sort(result.participation_overrides.begin(), result.participation_overrides.end(), [](auto const& left, auto const& right) {
		return left.first.source_population_cell == right.first.source_population_cell
			? left.first.ordinal < right.first.ordinal
			: left.first.source_population_cell < right.first.source_population_cell;
	});
	std::sort(result.last_separation_dates.begin(), result.last_separation_dates.end(), [](auto const& left, auto const& right) {
		return left.first.source_population_cell == right.first.source_population_cell
			? left.first.ordinal < right.first.ordinal
			: left.first.source_population_cell < right.first.source_population_cell;
	});
	std::sort(result.displaced_workers.begin(), result.displaced_workers.end(), [](auto left, auto right) {
		return left.source_population_cell == right.source_population_cell
			? left.ordinal < right.ordinal : left.source_population_cell < right.source_population_cell;
	});
	return result;
}

bool import_snapshot(sys::state& state, economy_snapshot const& snapshot) {
	if(snapshot.version != snapshot_version) return false;
	auto candidate = std::make_shared<exact_person_economy_store>();
	for(auto const& [key, value] : snapshot.participation_overrides) {
		if(!persons::exact_population::exists(state, key)
			|| candidate->participation_overrides.contains(key)) return false;
		candidate->participation_overrides.emplace(key, value);
	}
	for(auto const& record : snapshot.accounts) {
		if(record.id == 0 || !persons::exact_population::exists(state, record.owner)
			|| !record.settlement || !state.world.commodity_is_valid(record.settlement)
			|| !std::isfinite(record.balance) || record.balance < 0.0f
			|| candidate->account_by_id.contains(record.id)) return false;
		for(auto const& existing : candidate->accounts)
			if(existing.owner == record.owner && existing.settlement == record.settlement) return false;
		candidate->account_by_id.emplace(record.id, candidate->accounts.size());
		candidate->accounts.push_back(record);
		candidate->next_account_id = std::max(candidate->next_account_id, record.id + 1);
	}
	for(auto record : snapshot.applications) {
		if(record.id == 0 || !persons::exact_population::exists(state, record.worker)
			|| !record.offer || !state.world.job_offer_is_valid(record.offer)
			|| uint8_t(record.status) > uint8_t(application_status::withdrawn)) return false;
		for(auto const& existing : candidate->applications)
			if(existing.id == record.id) return false;
		if(record.causal_sequence == 0) record.causal_sequence = causal_order::allocate(state, causal_order::event_kind::job_application);
		if(record.causal_sequence == 0) return false;
		causal_order::observe(state, record.causal_sequence);
		candidate->applications.push_back(record);
		candidate->next_application_id = std::max(candidate->next_application_id, record.id + 1);
	}
	for(auto record : snapshot.contracts) {
		if(record.id == 0 || !persons::exact_population::exists(state, record.worker)
			|| !record.employer || !state.world.economic_actor_is_valid(record.employer)
			|| !record.factory || !state.world.factory_is_valid(record.factory)
			|| !record.workplace || !state.world.site_is_valid(record.workplace)
			|| !record.payer_account || !state.world.monetary_account_is_valid(record.payer_account)
			|| !record.worker_account_id || !candidate->account_by_id.contains(record.worker_account_id)
			|| !std::isfinite(record.unpaid_wages) || record.unpaid_wages < 0.0f
			|| uint8_t(record.status) > uint8_t(contract_status::terminated)) return false;
		auto account = candidate->accounts[candidate->account_by_id.at(record.worker_account_id)];
		if(account.owner != record.worker || accounts::owner_of(state, record.payer_account) != record.employer
			|| accounts::settlement_of(state, record.payer_account) != account.settlement
			|| (state.world.factory_get_payroll_settlement(record.factory)
				&& state.world.factory_get_payroll_settlement(record.factory) != account.settlement)) return false;
		for(auto const& existing : candidate->contracts)
			if(existing.id == record.id || (existing.worker == record.worker && existing.status == contract_status::active
				&& record.status == contract_status::active)) return false;
		if(record.causal_sequence == 0) record.causal_sequence = causal_order::allocate(state, causal_order::event_kind::employment_contract);
		if(record.causal_sequence == 0) return false;
		if(record.unpaid_wages > epsilon && !record.arrears_since) record.arrears_since = record.start_date;
		causal_order::observe(state, record.causal_sequence);
		candidate->contracts.push_back(record);
		candidate->next_contract_id = std::max(candidate->next_contract_id, record.id + 1);
	}
	auto snapshot_account = [&](account_ref ref) -> account_record const* {
		if(ref.kind != account_kind::exact) return nullptr;
		auto it = candidate->account_by_id.find(ref.exact_account_id);
		return it == candidate->account_by_id.end() ? nullptr : &candidate->accounts[it->second];
	};
	auto snapshot_account_valid = [&](account_ref ref) {
		if(ref.kind == account_kind::dcon)
			return ref.dcon_account && state.world.monetary_account_is_valid(ref.dcon_account)
				&& accounts::settlement_of(state, ref.dcon_account);
		auto account = snapshot_account(ref);
		return account && account->settlement && state.world.commodity_is_valid(account->settlement);
	};
	auto snapshot_settlement = [&](account_ref ref) {
		if(ref.kind == account_kind::dcon) return accounts::settlement_of(state, ref.dcon_account);
		auto account = snapshot_account(ref);
		return account ? account->settlement : dcon::commodity_id{};
	};
	for(auto const& record : snapshot.transactions) {
		if(record.id == 0 || !snapshot_account_valid(record.source) || !snapshot_account_valid(record.destination)
			|| record.source.kind == account_kind::dcon && record.destination.kind == account_kind::dcon
			|| record.source == record.destination || !finite_positive(record.amount)
			|| snapshot_settlement(record.source) != snapshot_settlement(record.destination)
			|| snapshot_settlement(record.source) != record.settlement) return false;
		for(auto const& existing : candidate->transactions)
			if(existing.id == record.id) return false;
		candidate->transactions.push_back(record);
		candidate->next_transaction_id = std::max(candidate->next_transaction_id, record.id + 1);
	}
	for(auto const& [worker, date] : snapshot.last_separation_dates) {
		if(!persons::exact_population::exists(state, worker) || !date
			|| (state.current_date && date > state.current_date)
			|| std::any_of(candidate->last_separation_dates.begin(), candidate->last_separation_dates.end(),
				[&](auto const& existing) { return existing.first == worker; })) return false;
		candidate->last_separation_dates.emplace_back(worker, date);
	}
	for(auto worker : snapshot.displaced_workers) {
		if(!persons::exact_population::exists(state, worker)
			|| std::any_of(candidate->displaced_workers.begin(), candidate->displaced_workers.end(),
				[&](auto existing) { return existing == worker; })
			|| std::any_of(candidate->contracts.begin(), candidate->contracts.end(),
				[&](auto const& record) { return record.worker == worker && record.status == contract_status::active; })) return false;
		candidate->displaced_workers.push_back(worker);
	}
	state.exact_person_economy = std::move(candidate);
	return true;
}

void clear_store(sys::state& state) {
	state.exact_person_economy.reset();
}

} // namespace economy::exact_person_economy
