#include "foreign_exchange.hpp"

#include "economy/accounts/accounts.hpp"
#include "economy/relations/relations.hpp"
#include "system_state.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>
#include <memory>

namespace economy {

struct foreign_exchange_store {
	uint64_t next_order_id = 1;
	uint64_t next_fill_id = 1;
	std::vector<foreign_exchange::order_record> orders;
	std::vector<foreign_exchange::fill_record> fills;
};

} // namespace economy

namespace economy::foreign_exchange {
namespace {

constexpr float epsilon = 1.0e-5f;
constexpr uint32_t snapshot_version = 1;

std::shared_ptr<foreign_exchange_store> ensure_store(sys::state& state) {
	assert(state.foreign_exchange && "FX order book must be initialized before use");
	if(!state.foreign_exchange) std::abort();
	return state.foreign_exchange;
}

std::shared_ptr<foreign_exchange_store> ensure_store(sys::state const& state) {
	assert(state.foreign_exchange && "FX order book must be initialized before lookup");
	if(!state.foreign_exchange) std::abort();
	return state.foreign_exchange;
}

bool positive_finite(float value) {
	return std::isfinite(value) && value > 0.0f;
}

bool account_valid(sys::state const& state, account_ref account) {
	auto settlement = exact_person_economy::settlement_of(state, account);
	return account && exact_person_economy::account_exists(state, account) && settlement
		&& state.world.commodity_is_valid(settlement)
		&& std::isfinite(exact_person_economy::balance(state, account))
		&& exact_person_economy::balance(state, account) >= 0.0f;
}

bool same_owner(sys::state const& state, account_ref left, account_ref right) {
	if(left.kind == exact_person_economy::account_kind::exact
		&& right.kind == exact_person_economy::account_kind::exact)
		return exact_person_economy::owner_of(state, left) == exact_person_economy::owner_of(state, right);
	if(left.kind == exact_person_economy::account_kind::dcon
		&& right.kind == exact_person_economy::account_kind::dcon)
		return accounts::owner_of(state, left.dcon_account) == accounts::owner_of(state, right.dcon_account);
	return false;
}

uint64_t next_id(uint64_t& value) {
	if(value == 0 || value == std::numeric_limits<uint64_t>::max()) return 0;
	return value++;
}

order_record* find_order(foreign_exchange_store& store, uint64_t id) {
	for(auto& order : store.orders) if(order.id == id) return &order;
	return nullptr;
}

order_record const* find_order(foreign_exchange_store const& store, uint64_t id) {
	for(auto const& order : store.orders) if(order.id == id) return &order;
	return nullptr;
}

void update_status(order_record& order) {
	if(order.remaining_target_amount > 0.0f) {
		if(order.remaining_target_amount <= epsilon) order.status = order_status::filled;
		else if(order.immediate_or_cancel && order.remaining_sell_amount <= epsilon)
			order.status = order_status::cancelled;
		else order.status = order_status::partially_filled;
	} else if(order.remaining_sell_amount <= epsilon) {
		order.status = order_status::filled;
	} else {
		order.status = order_status::partially_filled;
	}
}

struct match_amounts {
	float base = 0.0f;
	float quote = 0.0f;
};

match_amounts executable_amounts(order_record const& bid, order_record const& ask,
	float rate, float target_cap = 0.0f) {
	match_amounts result;
	if(!positive_finite(rate)) return result;
	auto base = std::min(ask.remaining_sell_amount, bid.remaining_sell_amount / rate);
	if(ask.remaining_target_amount > 0.0f)
		base = std::min(base, ask.remaining_target_amount / rate);
	if(bid.remaining_target_amount > 0.0f)
		base = std::min(base, bid.remaining_target_amount);
	if(target_cap > 0.0f) base = std::min(base, target_cap);
	if(!positive_finite(base)) return result;
	auto quote = base * rate;
	if(!positive_finite(quote)) return result;
	result.base = base;
	result.quote = quote;
	return result;
}

bool fill_orders(sys::state& state, order_record& bid, order_record& ask,
	float rate, sys::date date, float target_cap = 0.0f) {
	if(bid.status == order_status::filled || bid.status == order_status::cancelled
		|| ask.status == order_status::filled || ask.status == order_status::cancelled
		|| bid.sells_base || !ask.sells_base
		|| bid.base_currency != ask.base_currency || bid.quote_currency != ask.quote_currency
		|| same_owner(state, bid.source, ask.source)) return false;
	auto amount = executable_amounts(bid, ask, rate, target_cap);
	if(!positive_finite(amount.base) || !positive_finite(amount.quote)) return false;
	if(exact_person_economy::balance(state, bid.source) + epsilon < amount.quote
		|| exact_person_economy::balance(state, ask.source) + epsilon < amount.base
		|| amount.quote > std::numeric_limits<float>::max()
			- exact_person_economy::balance(state, ask.destination)
		|| amount.base > std::numeric_limits<float>::max()
			- exact_person_economy::balance(state, bid.destination)) return false;

	auto const bid_before = bid;
	auto const ask_before = ask;
	// Reducing the reserved balances before the transfers releases exactly the
	// two matched amounts, while the unmatched parts remain protected.
	bid.remaining_sell_amount = std::max(0.0f, bid.remaining_sell_amount - amount.quote);
	ask.remaining_sell_amount = std::max(0.0f, ask.remaining_sell_amount - amount.base);
	if(bid.remaining_target_amount > 0.0f)
		bid.remaining_target_amount = std::max(0.0f, bid.remaining_target_amount - amount.base);
	if(ask.remaining_target_amount > 0.0f)
		ask.remaining_target_amount = std::max(0.0f, ask.remaining_target_amount - amount.quote);
	update_status(bid);
	update_status(ask);

	auto quote_leg = exact_person_economy::transfer_with_result(state, bid.source,
		ask.destination, amount.quote, relations::transaction_kind::currency_exchange, date);
	if(!quote_leg.success) {
		bid = bid_before;
		ask = ask_before;
		return false;
	}
	auto base_leg = exact_person_economy::transfer_with_result(state, ask.source,
		bid.destination, amount.base, relations::transaction_kind::currency_exchange, date);
	if(!base_leg.success) {
		// Every transfer precondition was checked above. This compensating entry
		// is only a defensive path for an unexpected ledger rejection.
		(void)exact_person_economy::transfer_with_result(state, ask.destination,
			bid.source, amount.quote, relations::transaction_kind::currency_exchange, date);
		bid = bid_before;
		ask = ask_before;
		return false;
	}

	auto store = ensure_store(state);
	auto id = next_id(store->next_fill_id);
	if(!id) std::abort();
	fill_record fill;
	fill.id = id;
	fill.bid_order_id = bid.id;
	fill.ask_order_id = ask.id;
	fill.base_amount = amount.base;
	fill.quote_amount = amount.quote;
	fill.execution_rate = rate;
	fill.occurred_on = date;
	fill.quote_exact_transaction_id = quote_leg.exact_transaction_id;
	fill.quote_dcon_transaction = quote_leg.dcon_transaction_id;
	fill.base_exact_transaction_id = base_leg.exact_transaction_id;
	fill.base_dcon_transaction = base_leg.dcon_transaction_id;
	store->fills.push_back(fill);
	return true;
}

void match_new_order(sys::state& state, order_record& taker, sys::date date) {
	auto store = ensure_store(state);
	std::vector<uint64_t> makers;
	for(auto const& candidate : store->orders) {
		if(candidate.id == taker.id || candidate.status == order_status::filled
			|| candidate.status == order_status::cancelled
			|| candidate.base_currency != taker.base_currency
			|| candidate.quote_currency != taker.quote_currency
			|| candidate.sells_base == taker.sells_base) continue;
		if(exact_person_economy::balance(state, candidate.source) + epsilon
			< reserved_amount(state, candidate.source)) continue;
		makers.push_back(candidate.id);
	}
	std::sort(makers.begin(), makers.end(), [&](uint64_t left_id, uint64_t right_id) {
		auto left = find_order(*store, left_id);
		auto right = find_order(*store, right_id);
		if(!left || !right) return left_id < right_id;
		if(left->limit_quote_per_base != right->limit_quote_per_base) {
			if(taker.sells_base) return left->limit_quote_per_base > right->limit_quote_per_base;
			return left->limit_quote_per_base < right->limit_quote_per_base;
		}
		if(left->created_on != right->created_on) return left->created_on < right->created_on;
		return left->id < right->id;
	});

	for(auto maker_id : makers) {
		if(taker.status == order_status::filled || taker.status == order_status::cancelled) break;
		auto maker = find_order(*store, maker_id);
		if(!maker || maker->status == order_status::filled || maker->status == order_status::cancelled) continue;
		auto* bid = taker.sells_base ? maker : &taker;
		auto* ask = taker.sells_base ? &taker : maker;
		auto rate = maker->limit_quote_per_base;
		if(!taker.immediate_or_cancel
			&& (bid->limit_quote_per_base + epsilon < rate
				|| ask->limit_quote_per_base > rate + epsilon)) continue;
		(void)fill_orders(state, *bid, *ask, rate, date);
	}
	if(taker.immediate_or_cancel && (taker.status == order_status::active
		|| taker.status == order_status::partially_filled))
		taker.status = order_status::cancelled;
}

quote simulate_quote(sys::state const& state, dcon::commodity_id source_currency,
	dcon::commodity_id target_currency, float target_amount, float max_source_amount,
	account_ref exclude_source) {
	quote result;
	if(!source_currency || !target_currency || source_currency == target_currency
		|| !state.world.commodity_is_valid(source_currency)
		|| !state.world.commodity_is_valid(target_currency)
		|| !positive_finite(target_amount) || !positive_finite(max_source_amount)) return result;
	bool const source_is_base = source_currency.index() < target_currency.index();
	auto base = source_is_base ? source_currency : target_currency;
	auto quote_currency = source_is_base ? target_currency : source_currency;
	bool const taker_sells_base = source_is_base;
	auto store = ensure_store(state);
	std::vector<order_record const*> makers;
	for(auto const& candidate : store->orders) {
		if(candidate.status != order_status::active && candidate.status != order_status::partially_filled) continue;
		if(candidate.base_currency != base || candidate.quote_currency != quote_currency
			|| candidate.sells_base == uint8_t(taker_sells_base)
			|| (exclude_source && (candidate.source == exclude_source
				|| same_owner(state, candidate.source, exclude_source)))) continue;
		auto reserved = reserved_amount(state, candidate.source);
		if(exact_person_economy::balance(state, candidate.source) + epsilon < reserved) continue;
		makers.push_back(&candidate);
	}
	std::sort(makers.begin(), makers.end(), [&](auto left, auto right) {
		if(left->limit_quote_per_base != right->limit_quote_per_base) {
			if(taker_sells_base) return left->limit_quote_per_base > right->limit_quote_per_base;
			return left->limit_quote_per_base < right->limit_quote_per_base;
		}
		if(left->created_on != right->created_on) return left->created_on < right->created_on;
		return left->id < right->id;
	});

	auto target_left = target_amount;
	auto source_spent = 0.0f;
	for(auto maker : makers) {
		auto rate = maker->limit_quote_per_base;
		float base_amount = 0.0f;
		if(taker_sells_base) {
			base_amount = std::min({target_left / rate, max_source_amount - source_spent,
				maker->remaining_sell_amount / rate});
			if(maker->remaining_target_amount > 0.0f)
				base_amount = std::min(base_amount, maker->remaining_target_amount);
			auto quote_amount = base_amount * rate;
			if(!positive_finite(base_amount) || !positive_finite(quote_amount)) continue;
			target_left -= quote_amount;
			source_spent += base_amount;
		} else {
			base_amount = std::min({target_left, (max_source_amount - source_spent) / rate,
				maker->remaining_sell_amount / rate});
			if(maker->remaining_target_amount > 0.0f)
				base_amount = std::min(base_amount, maker->remaining_target_amount / rate);
			auto quote_amount = base_amount * rate;
			if(!positive_finite(base_amount) || !positive_finite(quote_amount)) continue;
			target_left -= base_amount;
			source_spent += quote_amount;
		}
		if(target_left <= epsilon) {
			result.available = true;
			result.source_amount = source_spent;
			result.target_amount = target_amount;
			result.marginal_source_per_target = source_spent / target_amount;
			return result;
		}
	}
	return result;
}

} // namespace

uint64_t place_limit_order(sys::state& state, account_ref source, account_ref destination,
	float sell_amount, float minimum_target_per_source, sys::date date) {
	if(!account_valid(state, source) || !account_valid(state, destination)
		|| !same_owner(state, source, destination)
		|| exact_person_economy::settlement_of(state, source)
			== exact_person_economy::settlement_of(state, destination)
		|| !positive_finite(sell_amount) || !positive_finite(minimum_target_per_source)
		|| available_balance(state, source) + epsilon < sell_amount) return 0;
	auto source_currency = exact_person_economy::settlement_of(state, source);
	auto target_currency = exact_person_economy::settlement_of(state, destination);
	bool const sells_base = source_currency.index() < target_currency.index();
	auto const base = sells_base ? source_currency : target_currency;
	auto const quote_currency = sells_base ? target_currency : source_currency;
	auto const rate = sells_base ? minimum_target_per_source : 1.0f / minimum_target_per_source;
	if(!positive_finite(rate)) return 0;
	auto store = ensure_store(state);
	auto id = next_id(store->next_order_id);
	if(!id) return 0;
	order_record order;
	order.id = id;
	order.source = source;
	order.destination = destination;
	order.base_currency = base;
	order.quote_currency = quote_currency;
	order.original_sell_amount = sell_amount;
	order.remaining_sell_amount = sell_amount;
	order.limit_quote_per_base = rate;
	order.created_on = date;
	order.sells_base = sells_base ? 1 : 0;
	store->orders.push_back(order);
	match_new_order(state, store->orders.back(), date);
	return id;
}

bool cancel_order(sys::state& state, uint64_t order_id) {
	auto order = find_order(*ensure_store(state), order_id);
	if(!order || (order->status != order_status::active
		&& order->status != order_status::partially_filled)) return false;
	order->status = order_status::cancelled;
	return true;
}

std::vector<order_record> const& orders(sys::state const& state) {
	return ensure_store(state)->orders;
}

std::vector<fill_record> const& fills(sys::state const& state) {
	return ensure_store(state)->fills;
}

float reserved_amount(sys::state const& state, account_ref account) {
	if(!state.foreign_exchange) return 0.0f;
	float result = 0.0f;
	for(auto const& order : ensure_store(state)->orders)
		if((order.status == order_status::active || order.status == order_status::partially_filled)
			&& order.source == account)
			result += std::max(0.0f, order.remaining_sell_amount);
	return result;
}

float available_balance(sys::state const& state, account_ref account) {
	return std::max(0.0f, exact_person_economy::balance(state, account)
		- reserved_amount(state, account));
}

position account_position(sys::state const& state, account_ref account) {
	position result;
	if(!account_valid(state, account)) return result;
	result.settlement = exact_person_economy::settlement_of(state, account);
	result.balance = exact_person_economy::balance(state, account);
	result.reserved = reserved_amount(state, account);
	result.available = std::max(0.0f, result.balance - result.reserved);
	return result;
}

quote quote_conversion(sys::state const& state, dcon::commodity_id source_currency,
	dcon::commodity_id target_currency, float target_amount, float max_source_amount,
	account_ref exclude_source) {
	return simulate_quote(state, source_currency, target_currency, target_amount,
		max_source_amount, exclude_source);
}

quote convert(sys::state& state, account_ref source, account_ref destination,
	float target_amount, float max_source_amount, sys::date date) {
	quote result;
	if(!account_valid(state, source) || !account_valid(state, destination)
		|| !same_owner(state, source, destination)) return result;
	auto source_currency = exact_person_economy::settlement_of(state, source);
	auto target_currency = exact_person_economy::settlement_of(state, destination);
	if(source_currency == target_currency) return result;
	auto budget = std::min(max_source_amount, available_balance(state, source));
	result = simulate_quote(state, source_currency, target_currency,
		target_amount, budget, source);
	if(!result.available) return {};
	bool const source_is_base = source_currency.index() < target_currency.index();
	auto base = source_is_base ? source_currency : target_currency;
	auto quote_currency = source_is_base ? target_currency : source_currency;
	bool const sells_base = source_is_base;
	auto rate = source_is_base
		? target_amount / budget
		: budget / target_amount;
	if(!positive_finite(rate)) return {};
	auto store = ensure_store(state);
	auto id = next_id(store->next_order_id);
	if(!id) return {};
	order_record order;
	order.id = id;
	order.source = source;
	order.destination = destination;
	order.base_currency = base;
	order.quote_currency = quote_currency;
	order.original_sell_amount = budget;
	order.remaining_sell_amount = budget;
	order.original_target_amount = target_amount;
	order.remaining_target_amount = target_amount;
	order.limit_quote_per_base = rate;
	order.created_on = date;
	order.sells_base = sells_base ? 1 : 0;
	order.immediate_or_cancel = 1;
	store->orders.push_back(order);
	match_new_order(state, store->orders.back(), date);
	if(store->orders.back().status != order_status::filled) return {};
	return result;
}

bool contains_conversion(sys::state const& state, account_ref source,
	account_ref destination, sys::date on_or_before) {
	auto store = ensure_store(state);
	for(auto const& fill : store->fills) {
		if(fill.occurred_on > on_or_before) continue;
		auto bid = find_order(*store, fill.bid_order_id);
		auto ask = find_order(*store, fill.ask_order_id);
		if(!bid || !ask) continue;
		if((bid->source == source && bid->destination == destination)
			|| (ask->source == source && ask->destination == destination)) return true;
	}
	return false;
}

snapshot export_snapshot(sys::state const& state) {
	auto store = ensure_store(state);
	snapshot result;
	result.version = snapshot_version;
	result.next_order_id = store->next_order_id;
	result.next_fill_id = store->next_fill_id;
	result.orders = store->orders;
	result.fills = store->fills;
	return result;
}

bool import_snapshot(sys::state& state, snapshot const& snapshot) {
	if(snapshot.version != snapshot_version || snapshot.next_order_id == 0
		|| snapshot.next_fill_id == 0) return false;
	auto candidate = std::make_shared<foreign_exchange_store>();
	candidate->next_order_id = snapshot.next_order_id;
	candidate->next_fill_id = snapshot.next_fill_id;
	std::vector<uint64_t> ids;
	std::vector<std::pair<account_ref, double>> reserved;
	for(auto const& order : snapshot.orders) {
		if(order.id == 0 || !account_valid(state, order.source)
			|| !account_valid(state, order.destination)
			|| !same_owner(state, order.source, order.destination)
			|| exact_person_economy::settlement_of(state, order.source)
			== exact_person_economy::settlement_of(state, order.destination)
			|| !order.base_currency || !order.quote_currency
			|| !state.world.commodity_is_valid(order.base_currency)
			|| !state.world.commodity_is_valid(order.quote_currency)
			|| order.base_currency == order.quote_currency
			|| !positive_finite(order.original_sell_amount)
			|| !std::isfinite(order.remaining_sell_amount) || order.remaining_sell_amount < 0.0f
			|| order.remaining_sell_amount > order.original_sell_amount + epsilon
			|| !std::isfinite(order.original_target_amount) || order.original_target_amount < 0.0f
			|| !std::isfinite(order.remaining_target_amount) || order.remaining_target_amount < 0.0f
			|| order.remaining_target_amount > order.original_target_amount + epsilon
			|| !positive_finite(order.limit_quote_per_base)
			|| order.sells_base > 1 || order.immediate_or_cancel > 1
			|| uint8_t(order.status) > uint8_t(order_status::cancelled)
			|| ((order.status == order_status::active
				|| order.status == order_status::partially_filled)
				&& !positive_finite(order.remaining_sell_amount))
			|| ((order.status == order_status::active
				|| order.status == order_status::partially_filled)
				&& order.immediate_or_cancel)) return false;
		auto source_currency = exact_person_economy::settlement_of(state, order.source);
		auto destination_currency = exact_person_economy::settlement_of(state, order.destination);
		if(order.base_currency != (source_currency.index() < destination_currency.index()
			? source_currency : destination_currency)
			|| order.quote_currency != (source_currency.index() < destination_currency.index()
				? destination_currency : source_currency)
			|| bool(order.sells_base) != (source_currency == order.base_currency)) return false;
		ids.push_back(order.id);
		if(order.status == order_status::active || order.status == order_status::partially_filled) {
			auto found = std::find_if(reserved.begin(), reserved.end(), [&](auto const& entry) {
				return entry.first == order.source;
			});
			if(found == reserved.end()) reserved.emplace_back(order.source, order.remaining_sell_amount);
			else found->second += order.remaining_sell_amount;
		}
		candidate->orders.push_back(order);
	}
	for(auto const& [account, amount] : reserved)
		if(amount > double(exact_person_economy::balance(state, account)) + epsilon) return false;
	std::sort(ids.begin(), ids.end());
	if(std::adjacent_find(ids.begin(), ids.end()) != ids.end()
		|| (!ids.empty() && candidate->next_order_id <= ids.back())) return false;
	std::vector<uint64_t> fill_ids;
	std::map<uint64_t, double> source_filled;
	std::map<uint64_t, double> target_filled;
	auto exact_leg_valid = [&](uint64_t transaction_id, account_ref source,
		account_ref destination, float amount, dcon::commodity_id settlement,
		sys::date date) {
		auto transaction = exact_person_economy::transaction(state, transaction_id);
		return transaction && transaction->source == source
			&& transaction->destination == destination
			&& std::abs(double(transaction->amount) - amount)
				<= std::max(1.0e-4, double(amount) * 1.0e-5)
			&& transaction->settlement == settlement
			&& transaction->kind == relations::transaction_kind::currency_exchange
			&& transaction->timestamp == date;
	};
	auto dcon_leg_valid = [&](dcon::transaction_id transaction, account_ref source,
		account_ref destination, float amount, dcon::commodity_id settlement,
		sys::date date) {
		if(!transaction || !state.world.transaction_is_valid(transaction)
			|| std::abs(double(state.world.transaction_get_amount(transaction)) - amount)
				> std::max(1.0e-4, double(amount) * 1.0e-5)
			|| state.world.transaction_get_settlement_commodity(transaction) != settlement
			|| state.world.transaction_get_kind(transaction)
				!= uint8_t(relations::transaction_kind::currency_exchange)
			|| state.world.transaction_get_timestamp(transaction) != date) return false;
		auto payer = state.world.transaction_get_economic_actor_from_transaction_payer(transaction);
		auto payee = state.world.transaction_get_economic_actor_from_transaction_payee(transaction);
		return payer == accounts::owner_of(state, source.dcon_account)
			&& payee == accounts::owner_of(state, destination.dcon_account);
	};
	for(auto const& fill : snapshot.fills) {
		auto bid = find_order(*candidate, fill.bid_order_id);
		auto ask = find_order(*candidate, fill.ask_order_id);
		if(fill.id == 0 || !bid || !ask || bid->sells_base || !ask->sells_base
			|| !positive_finite(fill.base_amount) || !positive_finite(fill.quote_amount)
			|| !positive_finite(fill.execution_rate)
			|| fill.bid_order_id == fill.ask_order_id
			|| bid->base_currency != ask->base_currency
			|| bid->quote_currency != ask->quote_currency
			|| (!bid->immediate_or_cancel
				&& bid->limit_quote_per_base + epsilon < fill.execution_rate)
			|| (!ask->immediate_or_cancel
				&& ask->limit_quote_per_base > fill.execution_rate + epsilon)
			|| std::abs(double(fill.quote_amount)
				- double(fill.base_amount) * fill.execution_rate)
				> std::max(1.0e-4, double(fill.quote_amount) * 1.0e-5)) return false;
		auto quote_valid = fill.quote_exact_transaction_id
			? !fill.quote_dcon_transaction && exact_leg_valid(fill.quote_exact_transaction_id,
				bid->source, ask->destination, fill.quote_amount, bid->quote_currency,
				fill.occurred_on)
			: !fill.quote_exact_transaction_id && dcon_leg_valid(fill.quote_dcon_transaction,
				bid->source, ask->destination, fill.quote_amount, bid->quote_currency,
				fill.occurred_on);
		auto base_valid = fill.base_exact_transaction_id
			? !fill.base_dcon_transaction && exact_leg_valid(fill.base_exact_transaction_id,
				ask->source, bid->destination, fill.base_amount, bid->base_currency,
				fill.occurred_on)
			: !fill.base_exact_transaction_id && dcon_leg_valid(fill.base_dcon_transaction,
				ask->source, bid->destination, fill.base_amount, bid->base_currency,
				fill.occurred_on);
		if(!quote_valid || !base_valid) return false;
		fill_ids.push_back(fill.id);
		source_filled[bid->id] += fill.quote_amount;
		target_filled[bid->id] += fill.base_amount;
		source_filled[ask->id] += fill.base_amount;
		target_filled[ask->id] += fill.quote_amount;
		candidate->fills.push_back(fill);
	}
	for(auto const& order : candidate->orders) {
		auto const source_total = source_filled[order.id] + order.remaining_sell_amount;
		if(std::abs(source_total - order.original_sell_amount)
			> std::max(1.0e-4, double(order.original_sell_amount) * 1.0e-5)) return false;
		if(order.original_target_amount > 0.0f) {
			auto const target_total = target_filled[order.id] + order.remaining_target_amount;
			if(std::abs(target_total - order.original_target_amount)
				> std::max(1.0e-4, double(order.original_target_amount) * 1.0e-5)) return false;
		} else if(order.remaining_target_amount > epsilon) return false;
	}
	std::sort(fill_ids.begin(), fill_ids.end());
	if(std::adjacent_find(fill_ids.begin(), fill_ids.end()) != fill_ids.end()
		|| (!fill_ids.empty() && candidate->next_fill_id <= fill_ids.back())) return false;
	state.foreign_exchange = std::move(candidate);
	return true;
}

void initialize_empty_store(sys::state& state) {
	assert(!state.foreign_exchange && "FX order book initialized more than once");
	if(state.foreign_exchange) std::abort();
	state.foreign_exchange = std::make_shared<foreign_exchange_store>();
}

void clear_store(sys::state& state) {
	state.foreign_exchange.reset();
}

} // namespace economy::foreign_exchange
