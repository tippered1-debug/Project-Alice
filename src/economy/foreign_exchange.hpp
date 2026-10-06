#pragma once

#include "economy/exact_person_economy.hpp"

#include <cstdint>
#include <vector>

namespace sys { class state; }

namespace economy::foreign_exchange {

using account_ref = exact_person_economy::account_ref;

enum class order_status : uint8_t { active = 0, partially_filled = 1, filled = 2, cancelled = 3 };

// A limit order sells the currency in `source` for the currency in `destination`.
// Open orders reserve their remaining source amount against other payments.
struct order_record {
	uint64_t id = 0;
	account_ref source{};
	account_ref destination{};
	dcon::commodity_id base_currency{};
	dcon::commodity_id quote_currency{};
	float original_sell_amount = 0.0f;
	float remaining_sell_amount = 0.0f;
	float original_target_amount = 0.0f;
	float remaining_target_amount = 0.0f; // zero for standing orders
	float limit_quote_per_base = 0.0f;
	sys::date created_on{};
	order_status status = order_status::active;
	uint8_t sells_base = 0;
	uint8_t immediate_or_cancel = 0;
};

struct fill_record {
	uint64_t id = 0;
	uint64_t bid_order_id = 0;
	uint64_t ask_order_id = 0;
	float base_amount = 0.0f;
	float quote_amount = 0.0f;
	float execution_rate = 0.0f;
	sys::date occurred_on{};
	uint64_t quote_exact_transaction_id = 0;
	dcon::transaction_id quote_dcon_transaction{};
	uint64_t base_exact_transaction_id = 0;
	dcon::transaction_id base_dcon_transaction{};
};

struct snapshot {
	uint32_t version = 1;
	uint64_t next_order_id = 1;
	uint64_t next_fill_id = 1;
	std::vector<order_record> orders;
	std::vector<fill_record> fills;
};

struct quote {
	bool available = false;
	float source_amount = 0.0f;
	float target_amount = 0.0f;
	float marginal_source_per_target = 0.0f;
};

struct position {
	dcon::commodity_id settlement{};
	float balance = 0.0f;
	float reserved = 0.0f;
	float available = 0.0f;
};

// Resting limit orders match at the resting order's price. The rate argument
// is target currency received per source currency sold.
uint64_t place_limit_order(sys::state&, account_ref source, account_ref destination,
	float sell_amount, float minimum_target_per_source, sys::date);
bool cancel_order(sys::state&, uint64_t order_id);
std::vector<order_record> const& orders(sys::state const&);
std::vector<fill_record> const& fills(sys::state const&);
float reserved_amount(sys::state const&, account_ref);
float available_balance(sys::state const&, account_ref);
position account_position(sys::state const&, account_ref);

// Quotes and executes a taker's conversion against resting orders. The quote
// is all-or-nothing for target_amount and never spends above max_source_amount.
quote quote_conversion(sys::state const&, dcon::commodity_id source_currency,
	dcon::commodity_id target_currency, float target_amount, float max_source_amount,
	account_ref exclude_source = {});
quote convert(sys::state&, account_ref source, account_ref destination,
	float target_amount, float max_source_amount, sys::date);

bool contains_conversion(sys::state const&, account_ref source, account_ref destination,
	sys::date on_or_before);

snapshot export_snapshot(sys::state const&);
bool import_snapshot(sys::state&, snapshot const&);
void initialize_empty_store(sys::state&);
void clear_store(sys::state&);

} // namespace economy::foreign_exchange
