#pragma once

#include "dcon_generated.hpp"
#include "persons/persons.hpp"

#include <cstdint>

namespace sys { class state; }
namespace economy::exact_person_economy { struct account_ref; }

namespace economy::monetary::ontology {

// These are the three balance instruments used by the canonical economy.
// Exact-person accounts are a storage backend for operating accounts.
enum class instrument_kind : uint8_t {
	invalid = 0,
	operating_account = 1,
	base_money_reserve = 2,
	bank_deposit = 3
};

enum class ledger_kind : uint8_t {
	invalid = 0,
	monetary_account = 1,
	deposit_account = 2,
	exact_person_account = 3
};

struct account_ref {
	ledger_kind ledger = ledger_kind::invalid;
	dcon::monetary_account_id monetary{};
	dcon::deposit_account_id deposit{};
	uint64_t exact_person_account_id = 0;

	static account_ref from_monetary(dcon::monetary_account_id id) {
		return {ledger_kind::monetary_account, id, {}, 0};
	}
	static account_ref from_deposit(dcon::deposit_account_id id) {
		return {ledger_kind::deposit_account, {}, id, 0};
	}
	static account_ref from_exact_person(uint64_t id) {
		return {ledger_kind::exact_person_account, {}, {}, id};
	}
	explicit operator bool() const noexcept {
		return ledger == ledger_kind::monetary_account ? bool(monetary)
			: ledger == ledger_kind::deposit_account ? bool(deposit)
			: ledger == ledger_kind::exact_person_account && exact_person_account_id != 0;
	}
};

struct account_view {
	instrument_kind instrument = instrument_kind::invalid;
	ledger_kind ledger = ledger_kind::invalid;
	dcon::economic_actor_id owner{};
	persons::person_key exact_person_owner{};
	dcon::commodity_id settlement{};
	float balance = 0.0f;
};

account_ref from_exact_economy(economy::exact_person_economy::account_ref);
bool describe(sys::state const&, account_ref, account_view&);

} // namespace economy::monetary::ontology
