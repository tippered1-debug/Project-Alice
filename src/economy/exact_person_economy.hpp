#pragma once

#include "dcon_generated.hpp"
#include "date_interface.hpp"
#include "economy/relations/relations.hpp"
#include "persons/exact_population.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace sys { class state; }

namespace economy::exact_person_economy {

using person_key = persons::exact_population::person_key;

enum class account_kind : uint8_t { invalid = 0, dcon = 1, exact = 2 };

struct account_ref {
	account_kind kind = account_kind::invalid;
	dcon::monetary_account_id dcon_account{};
	uint64_t exact_account_id = 0;

	static account_ref from_dcon(dcon::monetary_account_id account) { return {account_kind::dcon, account, 0}; }
	static account_ref from_exact(uint64_t account) { return {account_kind::exact, {}, account}; }
	friend bool operator==(account_ref left, account_ref right) noexcept {
		return left.kind == right.kind && left.dcon_account == right.dcon_account
			&& left.exact_account_id == right.exact_account_id;
	}
	explicit operator bool() const noexcept {
		return kind == account_kind::dcon ? bool(dcon_account)
			: kind == account_kind::exact && exact_account_id != 0;
	}
};

enum class application_status : uint8_t { pending = 0, accepted = 1, rejected = 2, withdrawn = 3 };
enum class contract_status : uint8_t { active = 0, ended = 1, terminated = 2 };

struct account_record {
	uint64_t id = 0;
	person_key owner{};
	dcon::commodity_id settlement{};
	float balance = 0.0f;
};

struct application_record {
	uint64_t id = 0;
	person_key worker{};
	dcon::job_offer_id offer{};
	sys::date applied_on{};
	uint64_t causal_sequence = 0;
	application_status status = application_status::pending;
};

struct contract_record {
	uint64_t id = 0;
	person_key worker{};
	dcon::economic_actor_id employer{};
	dcon::factory_id factory{};
	dcon::site_id workplace{};
	uint8_t occupation = 0;
	float labor_capacity = 0.0f;
	float wage_rate = 0.0f;
	uint16_t pay_period_days = 0;
	dcon::monetary_account_id payer_account{};
	uint64_t worker_account_id = 0;
	sys::date start_date{};
	contract_status status = contract_status::active;
	sys::date end_date{};
	float unpaid_wages = 0.0f;
	sys::date arrears_since{};
	uint64_t causal_sequence = 0;
};

struct transaction_record {
	uint64_t id = 0;
	account_ref source{};
	account_ref destination{};
	float amount = 0.0f;
	dcon::commodity_id settlement{};
	relations::transaction_kind kind = relations::transaction_kind::other;
	sys::date timestamp{};
};

struct transfer_result {
	bool success = false;
	uint64_t exact_transaction_id = 0;
	dcon::transaction_id dcon_transaction_id{};
};

struct wage_settlement {
	float due = 0.0f;
	float paid = 0.0f;
	float unpaid = 0.0f;
	float current_due = 0.0f;
	float arrears_before = 0.0f;
	float current_paid = 0.0f;
	float arrears_repaid = 0.0f;
	float arrears_after = 0.0f;
	float total_transferred = 0.0f;
	uint64_t transaction_id = 0;
};

// Isolated persistence boundary. The normal Project Alice save pipeline does
// not yet serialize this snapshot; callers must explicitly persist/restore it.
struct economy_snapshot {
	uint32_t version = 1;
	std::vector<std::pair<person_key, bool>> participation_overrides;
	std::vector<account_record> accounts;
	std::vector<application_record> applications;
	std::vector<contract_record> contracts;
	std::vector<transaction_record> transactions;
	std::vector<std::pair<person_key, sys::date>> last_separation_dates;
	std::vector<person_key> displaced_workers;
};

bool is_work_eligible(sys::state const&, person_key);
bool is_labor_force_participant(sys::state const&, person_key);
bool is_unemployed(sys::state const&, person_key);
bool set_labor_force_participation(sys::state&, person_key, bool);
uint64_t participation_override_count(sys::state const&);

account_ref open_account(sys::state&, person_key, dcon::commodity_id settlement);
account_ref find_account(sys::state const&, person_key, dcon::commodity_id settlement);
bool account_exists(sys::state const&, account_ref);
person_key owner_of(sys::state const&, account_ref);
dcon::commodity_id settlement_of(sys::state const&, account_ref);
float balance(sys::state const&, account_ref);
bool set_balance(sys::state&, account_ref, float);
uint64_t account_count(sys::state const&);
std::vector<account_ref> accounts_for_person(sys::state const&, person_key);

// Transfers touching an exact account are recorded in the exact ledger. This
// narrow primitive also supports DCON-to-DCON by delegating to normal accounts.
bool transfer(sys::state&, account_ref source, account_ref destination, float amount,
	relations::transaction_kind, sys::date timestamp);
transfer_result transfer_with_result(sys::state&, account_ref source, account_ref destination,
	float amount, relations::transaction_kind, sys::date timestamp);
uint64_t transaction_count(sys::state const&);
std::optional<transaction_record> latest_transaction(sys::state const&);
std::optional<transaction_record> transaction(sys::state const&, uint64_t transaction_id);

uint64_t submit_application(sys::state&, person_key, dcon::job_offer_id, sys::date applied_on);
bool withdraw_application(sys::state&, uint64_t application_id);
std::optional<application_record> application(sys::state const&, uint64_t application_id);
std::vector<uint64_t> applications_for_person(sys::state const&, person_key);
std::vector<uint64_t> applications_for_offer(sys::state const&, dcon::job_offer_id);
void process_pending_applications(sys::state&);
bool accept_pending_application(sys::state&, uint64_t application_id);
void process_job_search_for_exact_person(sys::state&, person_key);

std::optional<contract_record> contract(sys::state const&, uint64_t contract_id);
std::vector<uint64_t> active_contracts_for_factory(sys::state const&, dcon::factory_id);
std::vector<uint64_t> contracts_for_factory(sys::state const&, dcon::factory_id);
std::vector<uint64_t> active_contracts_for_person(sys::state const&, person_key);
bool person_has_active_contract(sys::state const&, person_key);
float labor_supplied_to_factory(sys::state const&, dcon::factory_id);
float wage_due(sys::state const&, uint64_t contract_id);
float wage_due_for_factory(sys::state const&, dcon::factory_id);
float unpaid_wages_for_factory(sys::state const&, dcon::factory_id);
wage_settlement settle_contract_wage(sys::state&, uint64_t contract_id);
bool end_contract(sys::state&, uint64_t contract_id, contract_status, sys::date end_date);

bool separated_on_date(sys::state const&, person_key, sys::date);
void note_separation(sys::state&, person_key, sys::date);
void enqueue_displaced_worker(sys::state&, person_key);
void remove_displaced_worker(sys::state&, person_key);
std::vector<person_key> displaced_workers(sys::state const&);
bool withdraw_pending_applications(sys::state&, person_key);

economy_snapshot export_snapshot(sys::state const&);
bool import_snapshot(sys::state&, economy_snapshot const&);
void clear_store(sys::state&);

} // namespace economy::exact_person_economy
