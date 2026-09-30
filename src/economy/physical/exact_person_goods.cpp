#include "exact_person_goods.hpp"

#include "accounts/accounts.hpp"
#include "economy/causal_order.hpp"
#include "economy/relations/relations.hpp"
#include "concrete_market.hpp"
#include "exact_person_freight.hpp"
#include "inventory.hpp"
#include "system_state.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <type_traits>
#include <unordered_map>
#include <tuple>

namespace economy::physical {

struct exact_person_goods_store {
	std::vector<exact_person_goods::stock_record> stocks;
	std::vector<exact_person_goods::need_record> needs;
	std::vector<exact_person_goods::bid_record> bids;
	std::vector<exact_person_goods::fill_record> fills;
	std::vector<exact_person_goods::person_key> imported_need_profiles;
	uint64_t next_bid_id = 1;
	uint64_t next_fill_id = 1;
};

} // namespace economy::physical

namespace economy::physical::exact_person_goods {
namespace {

constexpr uint32_t snapshot_version = 2;
constexpr float epsilon = 1.0e-5f;

std::shared_ptr<exact_person_goods_store> ensure_store(sys::state& state) {
	assert(state.exact_person_goods && "exact person goods store must be initialized before simulation");
	if(!state.exact_person_goods) std::abort();
	return state.exact_person_goods;
}

std::shared_ptr<exact_person_goods_store> ensure_store(sys::state const& state) {
	assert(state.exact_person_goods && "exact person goods store must be initialized before lookup");
	if(!state.exact_person_goods) std::abort();
	return state.exact_person_goods;
}

bool positive_finite(float value) { return std::isfinite(value) && value > 0.0f; }
bool nonnegative_finite(float value) { return std::isfinite(value) && value >= 0.0f; }

uint64_t next_id(uint64_t& next) {
	if(next == 0 || next == std::numeric_limits<uint64_t>::max()) return 0;
	return next++;
}

stock_record* stock_for(sys::state& state, person_key owner, dcon::site_id site,
	dcon::commodity_id commodity) {
	for(auto& record : ensure_store(state)->stocks)
		if(record.owner == owner && record.site == site && record.commodity == commodity) return &record;
	return nullptr;
}

stock_record const* stock_for(sys::state const& state, person_key owner, dcon::site_id site,
	dcon::commodity_id commodity) {
	return stock_for(const_cast<sys::state&>(state), owner, site, commodity);
}

need_record* need_for(sys::state& state, person_key owner, dcon::commodity_id commodity) {
	for(auto& record : ensure_store(state)->needs)
		if(record.owner == owner && record.commodity == commodity) return &record;
	return nullptr;
}

need_record const* need_for(sys::state const& state, person_key owner, dcon::commodity_id commodity) {
	return need_for(const_cast<sys::state&>(state), owner, commodity);
}

bid_record* bid_for(sys::state& state, uint64_t id) {
	for(auto& record : ensure_store(state)->bids) if(record.id == id) return &record;
	return nullptr;
}

bid_record const* bid_for(sys::state const& state, uint64_t id) {
	return bid_for(const_cast<sys::state&>(state), id);
}

float reserved_exact(sys::state const& state, uint64_t account_id, uint64_t except_bid = 0) {
	float result = 0.0f;
	std::vector<bid_record const*> reservations;
	for(auto const& bid : ensure_store(state)->bids)
		if(bid.id != except_bid && bid.status == order_status::active && bid.exact_account_id == account_id)
			reservations.push_back(&bid);
	std::sort(reservations.begin(), reservations.end(), [](auto left, auto right) {
		return std::tuple{left->commodity.index(), left->destination.index(),
			left->created_on.to_raw_value(), left->limit_price,
			left->causal_sequence, left->id}
			< std::tuple{right->commodity.index(), right->destination.index(),
				right->created_on.to_raw_value(), right->limit_price,
				right->causal_sequence, right->id};
	});
	for(auto bid : reservations) result += std::max(0.0f, bid->reserved_amount);
	return result;
}

dcon::market_id market_for_site(sys::state const& state, dcon::site_id site) {
	if(!site || !state.world.site_is_valid(site)) return {};
	auto province = state.world.site_get_province_from_site_location(site);
	if(!province || !state.world.province_is_valid(province)) return {};
	auto zone = state.world.province_get_state_membership(province);
	return zone ? state.world.state_instance_get_market_from_local_market(zone) : dcon::market_id{};
}

float refresh_unmet(sys::state const& state, need_record& need) {
	auto owned = stock_quantity(state, need.owner, persons::home_site(state, need.owner), need.commodity);
	need.unmet_quantity = std::max(0.0f, need.desired_quantity_per_period
		- std::max(0.0f, need.consumed_this_period) - owned);
	return need.unmet_quantity;
}

bool active_equivalent_bid(sys::state const& state, person_key owner, dcon::site_id destination,
	dcon::commodity_id commodity) {
	for(auto const& bid : ensure_store(state)->bids)
		if(bid.status == order_status::active && bid.buyer == owner && bid.destination == destination && bid.commodity == commodity) return true;
	return false;
}

std::vector<dcon::commodity_id> seller_settlements(sys::state const& state,
	dcon::market_id /*market*/, dcon::commodity_id commodity) {
	std::vector<dcon::commodity_id> result;
	state.world.for_each_concrete_market_ask([&](auto ask) {
		if(state.world.concrete_market_ask_get_status(ask) != uint8_t(order_status::active)
			|| state.world.concrete_market_ask_get_commodity_from_concrete_ask_commodity(ask) != commodity) return;
		auto seller = state.world.concrete_market_ask_get_economic_actor_from_concrete_ask_seller(ask);
		state.world.economic_actor_for_each_monetary_account_owner_as_economic_actor(seller, [&](auto relation) {
			auto account = state.world.monetary_account_owner_get_monetary_account(relation);
			auto settlement = accounts::settlement_of(state, account);
			if(account && state.world.monetary_account_is_valid(account) && settlement && state.world.commodity_is_valid(settlement)) result.push_back(settlement);
		});
	});
	std::sort(result.begin(), result.end(), [](auto a, auto b) { return a.index() < b.index(); });
	result.erase(std::unique(result.begin(), result.end()), result.end());
	return result;
}

} // namespace

float stock_quantity(sys::state const& state, person_key owner, dcon::site_id site,
	dcon::commodity_id commodity) {
	if(!persons::exists(state, owner) || !site || !commodity) return 0.0f;
	auto record = stock_for(state, owner, site, commodity);
	return record ? std::max(0.0f, record->quantity) : 0.0f;
}

float add_stock(sys::state& state, person_key owner, dcon::site_id site,
	dcon::commodity_id commodity, float amount) {
	if(!persons::exists(state, owner) || !site || !state.world.site_is_valid(site) || !commodity || !state.world.commodity_is_valid(commodity) || !positive_finite(amount)) return 0.0f;
	auto record = stock_for(state, owner, site, commodity);
	auto current = record ? record->quantity : 0.0f;
	if(!nonnegative_finite(current) || amount > std::numeric_limits<float>::max() - current) return 0.0f;
	if(!record) {
		record = &ensure_store(state)->stocks.emplace_back();
		record->owner = owner; record->site = site; record->commodity = commodity;
	}
	record->quantity += amount;
	for(auto& need : ensure_store(state)->needs)
		if(need.owner == owner && need.commodity == commodity) refresh_unmet(state, need);
	return amount;
}

float remove_stock(sys::state& state, person_key owner, dcon::site_id site,
	dcon::commodity_id commodity, float amount) {
	if(!positive_finite(amount)) return 0.0f;
	auto record = stock_for(state, owner, site, commodity);
	if(!record) return 0.0f;
	auto removed = std::min(std::max(0.0f, record->quantity), amount);
	record->quantity -= removed;
	if(record->quantity <= 0.0f) {
		auto& stocks = ensure_store(state)->stocks;
		stocks.erase(std::remove_if(stocks.begin(), stocks.end(), [&](auto const& item) {
			return item.owner == owner && item.site == site && item.commodity == commodity;
		}), stocks.end());
	}
	for(auto& need : ensure_store(state)->needs)
		if(need.owner == owner && need.commodity == commodity) refresh_unmet(state, need);
	return removed;
}

bool set_need(sys::state& state, person_key owner, dcon::commodity_id commodity, float desired) {
	if(!persons::exists(state, owner) || !commodity || !state.world.commodity_is_valid(commodity) || !nonnegative_finite(desired)) return false;
	auto record = need_for(state, owner, commodity);
	if(record && record->consumed_this_period > desired + epsilon) return false;
	if(!record) {
		record = &ensure_store(state)->needs.emplace_back();
		record->owner = owner; record->commodity = commodity; record->consumption_period_start = state.current_date;
	}
	record->desired_quantity_per_period = desired;
	refresh_unmet(state, *record);
	return true;
}

std::optional<need_record> need(sys::state const& state, person_key owner, dcon::commodity_id commodity) {
	if(auto record = need_for(state, owner, commodity)) return *record;
	return std::nullopt;
}

std::vector<dcon::commodity_id> needs_for_person(sys::state const& state, person_key owner) {
	std::vector<dcon::commodity_id> result;
	for(auto const& record : ensure_store(state)->needs)
		if(record.owner == owner) result.push_back(record.commodity);
	std::sort(result.begin(), result.end(), [](auto left, auto right) {
		return left.index() < right.index();
	});
	result.erase(std::unique(result.begin(), result.end()), result.end());
	return result;
}

bool need_profile_imported(sys::state const& state, person_key owner) {
	auto const& imported = ensure_store(state)->imported_need_profiles;
	return std::find(imported.begin(), imported.end(), owner) != imported.end();
}

bool mark_need_profile_imported(sys::state& state, person_key owner) {
	if(!persons::exists(state, owner)) return false;
	auto& imported = ensure_store(state)->imported_need_profiles;
	if(std::find(imported.begin(), imported.end(), owner) == imported.end()) {
		imported.push_back(owner);
		std::sort(imported.begin(), imported.end(), [](person_key left, person_key right) {
			return left.source_population_cell == right.source_population_cell
				? left.ordinal < right.ordinal
				: left.source_population_cell < right.source_population_cell;
		});
	}
	return true;
}

void refresh_unmet_needs(sys::state& state, person_key owner) {
	for(auto& record : ensure_store(state)->needs)
		if(record.owner == owner) refresh_unmet(state, record);
}

float unmet_need(sys::state const& state, person_key owner, dcon::commodity_id commodity) {
	if(auto record = need_for(state, owner, commodity)) {
		return std::max(0.0f, record->desired_quantity_per_period
			- std::max(0.0f, record->consumed_this_period)
			- stock_quantity(state, owner, persons::home_site(state, owner), commodity));
	}
	return 0.0f;
}

float consumed_this_period(sys::state const& state, person_key owner, dcon::commodity_id commodity) {
	if(auto record = need_for(state, owner, commodity)) return std::max(0.0f, record->consumed_this_period);
	return 0.0f;
}

void begin_period(sys::state& state, sys::date period) {
	for(auto& record : ensure_store(state)->needs) {
		if(period <= record.consumption_period_start) continue;
		record.consumed_this_period = 0.0f;
		record.consumption_period_start = period;
		refresh_unmet(state, record);
	}
}

float consume_stock(sys::state& state, person_key owner, dcon::commodity_id commodity,
	float requested) {
	auto record = need_for(state, owner, commodity);
	if(!record || !persons::alive(state, owner) || !positive_finite(requested)) return 0.0f;
	auto remaining = std::max(0.0f, record->desired_quantity_per_period - record->consumed_this_period);
	auto site = persons::home_site(state, owner);
	auto amount = std::min({requested, remaining,
		stock_quantity(state, owner, site, commodity)});
	auto consumed = remove_stock(state, owner, site, commodity, amount);
	record->consumed_this_period += consumed;
	record->last_consumed_quantity = consumed;
	record->last_consumed_on = state.current_date;
	refresh_unmet(state, *record);
	return consumed;
}

float process_consumption(sys::state& state, person_key owner, dcon::commodity_id commodity) {
	auto record = need_for(state, owner, commodity);
	if(!record) return 0.0f;
	return consume_stock(state, owner, commodity,
		std::max(0.0f, record->desired_quantity_per_period - record->consumed_this_period));
}

uint64_t post_bid(sys::state& state, person_key buyer, economy::exact_person_economy::account_ref account,
	dcon::site_id destination, dcon::market_id market, dcon::commodity_id commodity,
	float quantity, float limit_price, order_purpose purpose) {
	using namespace economy::exact_person_economy;
	if(!persons::exists(state, buyer) || !persons::alive(state, buyer) || account.kind != account_kind::exact || owner_of(state, account) != buyer || !destination || !market || !commodity || !state.world.site_is_valid(destination) || !state.world.market_is_valid(market) || !state.world.commodity_is_valid(commodity) || !positive_finite(quantity) || !positive_finite(limit_price)) return 0;
	auto free = balance(state, account) - reserved_exact(state, account.exact_account_id);
	if(!std::isfinite(free) || free + epsilon < quantity * limit_price) return 0;
	auto id = next_id(ensure_store(state)->next_bid_id);
	if(!id) return 0;
	bid_record record;
	record.id = id; record.buyer = buyer; record.exact_account_id = account.exact_account_id;
	record.destination = destination; record.market = market; record.commodity = commodity;
	record.original_quantity = quantity; record.remaining_quantity = quantity;
	record.limit_price = limit_price; record.reserved_amount = quantity * limit_price;
	record.created_on = state.current_date; record.purpose = purpose;
	record.causal_sequence = economy::causal_order::allocate(state, economy::causal_order::event_kind::goods_bid);
	if(!record.causal_sequence) return 0;
	ensure_store(state)->bids.push_back(record);
	return id;
}

std::optional<bid_record> bid(sys::state const& state, uint64_t id) {
	if(auto record = bid_for(state, id)) return *record;
	return std::nullopt;
}

std::vector<bid_reference> active_bids(sys::state const& state, dcon::market_id market, dcon::commodity_id commodity) {
	std::vector<bid_reference> result;
	for(auto const& bid : ensure_store(state)->bids)
		if(bid.status == order_status::active && bid.market == market && bid.commodity == commodity)
			result.push_back({bid.id, bid.created_on, bid.causal_sequence});
	std::sort(result.begin(), result.end(), [](auto const& left, auto const& right) {
		return economy::causal_order::before({left.created_on, left.causal_sequence},
			{right.created_on, right.causal_sequence});
	});
	return result;
}

uint64_t try_fill(sys::state& state, uint64_t exact_bid_id, dcon::concrete_market_ask_id ask,
	sys::date date) {
	auto bid = bid_for(state, exact_bid_id);
	if(!bid || bid->status != order_status::active || !ask || state.world.concrete_market_ask_get_status(ask) != uint8_t(order_status::active)) return 0;
	auto source = state.world.concrete_market_ask_get_site_from_concrete_ask_site(ask);
	auto seller = state.world.concrete_market_ask_get_economic_actor_from_concrete_ask_seller(ask);
	auto commodity = state.world.concrete_market_ask_get_commodity_from_concrete_ask_commodity(ask);
	auto price = state.world.concrete_market_ask_get_minimum_price(ask);
	if(!source || commodity != bid->commodity || !seller || !positive_finite(price) || price > bid->limit_price || !positive_finite(bid->remaining_quantity)) return 0;
	using namespace economy::exact_person_economy;
	auto buyer_account = account_ref::from_exact(bid->exact_account_id);
	auto settlement = settlement_of(state, buyer_account);
	auto seller_account = accounts::find_account(state, seller, settlement);
	if(!seller_account || accounts::owner_of(state, seller_account) != seller || inventory::quantity(state, source, commodity, seller) <= 0.0f) return 0;
	auto quantity = std::min(bid->remaining_quantity,
		state.world.concrete_market_ask_get_remaining_quantity(ask));
	quantity = std::min(quantity, inventory::quantity(state, source, commodity, seller));
	auto free = balance(state, buyer_account) - reserved_exact(state, bid->exact_account_id) + bid->reserved_amount;
	quantity = std::min(quantity, std::max(0.0f, free / price));
	if(!positive_finite(quantity) || quantity * price > free + epsilon) return 0;
	auto fill_id = next_id(ensure_store(state)->next_fill_id);
	if(!fill_id) return 0;
	if(inventory::remove(state, source, commodity, quantity, seller) != quantity) return 0;
	if(add_stock(state, bid->buyer, source, commodity, quantity) != quantity) {
		(void)inventory::add(state, source, commodity, quantity, seller);
		return 0;
	}
	uint64_t freight_request = 0;
	if(source != bid->destination) {
		freight_request = exact_person_freight::create_request(state, bid->buyer, source,
			bid->destination, commodity, quantity, fill_id);
		if(!freight_request) {
			(void)remove_stock(state, bid->buyer, source, commodity, quantity);
			(void)inventory::add(state, source, commodity, quantity, seller);
			return 0;
		}
	}
	auto transfer = transfer_with_result(state, buyer_account, account_ref::from_dcon(seller_account),
		quantity * price, relations::transaction_kind::purchase, date);
	if(!transfer.success) {
		if(freight_request) exact_person_freight::cancel_request(state, freight_request);
		inventory::add(state, source, commodity, quantity, seller);
		remove_stock(state, bid->buyer, source, commodity, quantity);
		return 0;
	}
	bid->remaining_quantity = std::max(0.0f, bid->remaining_quantity - quantity);
	bid->reserved_amount = bid->remaining_quantity * bid->limit_price;
	if(bid->remaining_quantity <= epsilon) bid->status = order_status::filled;
	auto ask_remaining = std::max(0.0f, state.world.concrete_market_ask_get_remaining_quantity(ask) - quantity);
	state.world.concrete_market_ask_set_remaining_quantity(ask, ask_remaining);
	state.world.concrete_market_ask_set_reserved_quantity(ask, ask_remaining);
	if(ask_remaining <= epsilon) state.world.concrete_market_ask_set_status(ask, uint8_t(order_status::filled));
	fill_record fill;
	fill.id = fill_id; fill.exact_bid_id = exact_bid_id; fill.dcon_ask = ask;
	fill.exact_transaction_id = transfer.exact_transaction_id; fill.quantity = quantity;
	fill.execution_price = price; fill.source = source; fill.destination = bid->destination;
	// The bid's destination market is the buyer-side market signal. The ask's
	// market remains available through the concrete ask relation.
	fill.market = bid->market; fill.commodity = commodity; fill.occurred_on = date;
	ensure_store(state)->fills.push_back(fill);
	return fill_id;
}

std::optional<fill_record> fill(sys::state const& state, uint64_t id) {
	if(id == 0) return std::nullopt;
	for(auto const& record : ensure_store(state)->fills) if(record.id == id) return record;
	return std::nullopt;
}

void expire(sys::state& state, sys::date date) {
	for(auto& bid : ensure_store(state)->bids)
		if(bid.status == order_status::active && bid.created_on < date) {
			bid.status = order_status::canceled;
			bid.reserved_amount = 0.0f;
		}
}

void cancel_dead_person_orders(sys::state& state) {
	for(auto& bid : ensure_store(state)->bids)
		if(bid.status == order_status::active && !persons::alive(state, bid.buyer)) {
			bid.status = order_status::canceled;
			bid.reserved_amount = 0.0f;
		}
}

bool process_purchase_decision(sys::state& state, person_key buyer, dcon::commodity_id commodity) {
	if(!persons::exists(state, buyer) || !persons::alive(state, buyer)) return false;
	auto site = persons::home_site(state, buyer);
	auto market = market_for_site(state, site);
	auto record = need_for(state, buyer, commodity);
	if(!site || !market || !record || active_equivalent_bid(state, buyer, site, commodity)) return false;
	auto home_stock = stock_quantity(state, buyer, site, commodity);
	auto incoming = exact_person_freight::incoming_quantity(state, buyer, site, commodity);
	auto remaining_to_acquire = std::max(0.0f, record->desired_quantity_per_period
		- std::max(0.0f, record->consumed_this_period) - home_stock - incoming);
	if(remaining_to_acquire <= epsilon) return false;
	auto price = concrete_market::concrete_reference_price(state, market, commodity, state.current_date,
		std::max(0.01f, state.world.commodity_get_cost(commodity)));
	if(!positive_finite(price)) return false;
	economy::exact_person_economy::account_ref selected{};
	float best_cash = -std::numeric_limits<float>::infinity();
	for(auto settlement : seller_settlements(state, market, commodity))
		for(auto candidate : economy::exact_person_economy::accounts_for_person(state, buyer))
			if(economy::exact_person_economy::settlement_of(state, candidate) == settlement) {
				auto cash = economy::exact_person_economy::balance(state, candidate)
					- reserved_exact(state, candidate.exact_account_id);
				auto const selected_settlement = selected
					? economy::exact_person_economy::settlement_of(state, selected)
					: dcon::commodity_id{};
				if(cash > best_cash || (cash == best_cash
					&& (!selected || settlement.index() < selected_settlement.index()))) {
					selected = candidate; best_cash = cash;
				}
			}
	if(!selected || best_cash <= epsilon) return false;
	auto quantity = std::min(remaining_to_acquire, best_cash / price);
	auto id = post_bid(state, buyer, selected, site, market, commodity, quantity, price);
	return id != 0;
}

void process_purchase_decisions(sys::state& state, person_key buyer) {
	if(!persons::exists(state, buyer) || !persons::alive(state, buyer)) return;
	std::vector<dcon::commodity_id> commodities;
	for(auto const& record : ensure_store(state)->needs)
		if(record.owner == buyer) commodities.push_back(record.commodity);
	std::sort(commodities.begin(), commodities.end(), [](auto left, auto right) {
		return left.index() < right.index();
	});
	commodities.erase(std::unique(commodities.begin(), commodities.end()), commodities.end());
	std::vector<dcon::commodity_id> matches;
	for(auto commodity : commodities) {
		if(process_purchase_decision(state, buyer, commodity)) matches.push_back(commodity);
	}
	for(auto commodity : matches)
		(void)concrete_market::match_all(state, commodity, state.current_date);
}

void process_daily(sys::state& state) {
	expire(state, state.current_date);
	cancel_dead_person_orders(state);
	begin_period(state, state.current_date);
	auto store = ensure_store(state);
	std::vector<need_record*> ordered;
	ordered.reserve(store->needs.size());
	for(auto& record : store->needs) ordered.push_back(&record);
	std::sort(ordered.begin(), ordered.end(), [](auto left, auto right) {
		if(left->owner.source_population_cell != right->owner.source_population_cell)
			return left->owner.source_population_cell < right->owner.source_population_cell;
		if(left->owner.ordinal != right->owner.ordinal)
			return left->owner.ordinal < right->owner.ordinal;
		return left->commodity.index() < right->commodity.index();
	});

	std::vector<dcon::commodity_id> matches;
	for(auto record : ordered) {
		if(!persons::alive(state, record->owner)) continue;
		if(process_purchase_decision(state, record->owner, record->commodity))
			matches.push_back(record->commodity);
	}
	std::sort(matches.begin(), matches.end(), [](auto left, auto right) {
		return left.index() < right.index();
	});
	matches.erase(std::unique(matches.begin(), matches.end()), matches.end());
	for(auto commodity : matches)
		(void)concrete_market::match_all(state, commodity, state.current_date);

	for(auto record : ordered) {
		if(persons::alive(state, record->owner))
			(void)process_consumption(state, record->owner, record->commodity);
	}
}

namespace {

bool close_enough(double left, double right) {
	return std::isfinite(left) && std::isfinite(right)
		&& std::abs(left - right) <= std::max(1.0e-5, std::abs(right) * 1.0e-5);
}

bool person_key_less(person_key left, person_key right) {
	return left.source_population_cell == right.source_population_cell
		? left.ordinal < right.ordinal
		: left.source_population_cell < right.source_population_cell;
}

bool validate_canonical_state(sys::state const& state) {
	using namespace economy::exact_person_economy;
	auto economy_state = economy::exact_person_economy::export_snapshot(state);
	std::map<uint64_t, double> reservations;
	std::set<std::tuple<uint32_t, uint64_t, uint32_t, uint32_t>> stock_keys;
	std::set<std::pair<std::pair<uint32_t, uint64_t>, uint32_t>> account_keys;
	for(auto const& account : economy_state.accounts) {
		if(!persons::exists(state, account.owner) || !account.settlement
			|| !state.world.commodity_is_valid(account.settlement)
			|| !std::isfinite(account.balance) || account.balance < 0.0f
			|| !account_keys.emplace(std::make_pair(account.owner.source_population_cell,
				account.owner.ordinal), uint32_t(account.settlement.index())).second) return false;
	}
	for(auto const& contract : economy_state.contracts) {
		if(!persons::exists(state, contract.worker)
			|| !contract.employer || !state.world.economic_actor_is_valid(contract.employer)
			|| !contract.worker_account_id
			|| !account_exists(state, account_ref::from_exact(contract.worker_account_id))
			|| owner_of(state, account_ref::from_exact(contract.worker_account_id)) != contract.worker
			|| !contract.payer_account || !state.world.monetary_account_is_valid(contract.payer_account)
			|| accounts::owner_of(state, contract.payer_account) != contract.employer
			|| !std::isfinite(contract.labor_capacity) || contract.labor_capacity < 0.0f
			|| !std::isfinite(contract.wage_rate) || contract.wage_rate < 0.0f
			|| !std::isfinite(contract.unpaid_wages) || contract.unpaid_wages < 0.0f
			|| (contract.status == contract_status::active && !persons::alive(state, contract.worker))) return false;
	}

	auto store = ensure_store(state);
	for(auto const& stock : store->stocks) {
		if(!persons::exists(state, stock.owner) || !stock.site
			|| !state.world.site_is_valid(stock.site) || !stock.commodity
			|| !state.world.commodity_is_valid(stock.commodity)
			|| !positive_finite(stock.quantity)
			|| !stock_keys.emplace(stock.owner.source_population_cell, stock.owner.ordinal,
				uint32_t(stock.site.index()), uint32_t(stock.commodity.index())).second) return false;
	}
	std::set<std::pair<std::pair<uint32_t, uint64_t>, uint32_t>> need_keys;
	for(auto const& need : store->needs) {
		if(!persons::exists(state, need.owner) || !need.commodity
			|| !state.world.commodity_is_valid(need.commodity)
			|| !nonnegative_finite(need.desired_quantity_per_period)
			|| !nonnegative_finite(need.consumed_this_period)
			|| !nonnegative_finite(need.unmet_quantity)
			|| !nonnegative_finite(need.last_consumed_quantity)
			|| need.consumed_this_period > need.desired_quantity_per_period + epsilon
			|| !need_keys.emplace(std::make_pair(need.owner.source_population_cell,
				need.owner.ordinal), uint32_t(need.commodity.index())).second) return false;
		if(!close_enough(need.unmet_quantity, unmet_need(state, need.owner, need.commodity))) return false;
	}
	std::set<std::tuple<uint32_t, uint64_t, uint32_t, uint32_t>> active_equivalents;
	for(auto const& owner : store->imported_need_profiles)
		if(!persons::exists(state, owner)) return false;
	for(auto const& bid : store->bids) {
		if(!bid.id || !persons::exists(state, bid.buyer) || !bid.exact_account_id
			|| owner_of(state, account_ref::from_exact(bid.exact_account_id)) != bid.buyer
			|| !bid.destination || !state.world.site_is_valid(bid.destination)
			|| !bid.market || !state.world.market_is_valid(bid.market)
			|| !bid.commodity || !state.world.commodity_is_valid(bid.commodity)
			|| !positive_finite(bid.original_quantity)
			|| !nonnegative_finite(bid.remaining_quantity)
			|| !positive_finite(bid.limit_price)
			|| !nonnegative_finite(bid.reserved_amount)
			|| bid.remaining_quantity > bid.original_quantity + epsilon
			|| bid.reserved_amount > bid.remaining_quantity * bid.limit_price + epsilon) return false;
		if(bid.status == order_status::active) {
			if(!persons::alive(state, bid.buyer) || bid.causal_sequence == 0
				|| !active_equivalents.emplace(bid.buyer.source_population_cell, bid.buyer.ordinal,
					uint32_t(bid.destination.index()), uint32_t(bid.commodity.index())).second) return false;
			reservations[bid.exact_account_id] += bid.reserved_amount;
		} else if(bid.reserved_amount > epsilon) return false;
	}
	for(auto const& account : economy_state.accounts)
		if(reservations[account.id] > double(account.balance) + epsilon) return false;

	std::map<uint64_t, double> fill_by_bid;
	std::map<uint64_t, double> sold_by_ask;
	std::set<uint64_t> fill_ids;
	for(auto const& fill : store->fills) {
		auto bid = bid_for(state, fill.exact_bid_id);
		auto ask = fill.dcon_ask;
		auto transfer = economy::exact_person_economy::transaction(state,
			fill.exact_transaction_id);
		if(!fill.id || !fill_ids.insert(fill.id).second || !bid || !ask
			|| !state.world.concrete_market_ask_is_valid(ask)
			|| !transfer || !positive_finite(fill.quantity)
			|| !positive_finite(fill.execution_price)
			|| fill.commodity != bid->commodity
			|| fill.source != state.world.concrete_market_ask_get_site_from_concrete_ask_site(ask)
			|| fill.commodity != state.world.concrete_market_ask_get_commodity_from_concrete_ask_commodity(ask)
			|| !close_enough(double(fill.quantity) * fill.execution_price, transfer->amount)
			|| transfer->source != account_ref::from_exact(bid->exact_account_id)
			|| transfer->kind != relations::transaction_kind::purchase
			|| transfer->timestamp != fill.occurred_on) return false;
		auto seller = state.world.concrete_market_ask_get_economic_actor_from_concrete_ask_seller(ask);
		if(transfer->destination.kind != account_kind::dcon
			|| accounts::owner_of(state, transfer->destination.dcon_account) != seller
			|| accounts::settlement_of(state, transfer->destination.dcon_account)
				!= transfer->settlement) return false;
		fill_by_bid[fill.exact_bid_id] += fill.quantity;
		sold_by_ask[uint64_t(ask.index())] += fill.quantity;
	}
	for(auto const& bid : store->bids) {
		if(!close_enough(fill_by_bid[bid.id],
			double(bid.original_quantity - bid.remaining_quantity))) return false;
		if(bid.status == order_status::filled && bid.remaining_quantity > epsilon) return false;
	}
	std::map<uint64_t, double> dcon_sold_by_ask;
	state.world.for_each_concrete_trade_fill([&](auto fill) {
		auto ask = state.world.concrete_trade_fill_get_concrete_market_ask_from_concrete_fill_ask(fill);
		if(ask) dcon_sold_by_ask[uint64_t(ask.index())] +=
			state.world.concrete_trade_fill_get_quantity(fill);
	});
	bool asks_valid = true;
	state.world.for_each_concrete_market_ask([&](auto ask) {
		auto original = state.world.concrete_market_ask_get_original_quantity(ask);
		auto remaining = state.world.concrete_market_ask_get_remaining_quantity(ask);
		if(!nonnegative_finite(original) || !nonnegative_finite(remaining)
			|| !close_enough(sold_by_ask[uint64_t(ask.index())]
				+ dcon_sold_by_ask[uint64_t(ask.index())], double(original - remaining)))
			asks_valid = false;
	});
	if(!asks_valid) return false;

	auto freight = exact_person_freight::export_snapshot(state);
	for(auto const& fill : store->fills) {
		if(fill.source == fill.destination) continue;
		auto bid = bid_for(state, fill.exact_bid_id);
		auto request = std::find_if(freight.requests.begin(), freight.requests.end(),
			[&](auto const& item) { return item.originating_fill_id == fill.id; });
		if(!bid || request == freight.requests.end() || request->requester != bid->buyer
			|| request->source != fill.source || request->destination != fill.destination
			|| request->commodity != fill.commodity
			|| !close_enough(request->quantity, fill.quantity)
			|| request->status == exact_person_freight::request_status::canceled) return false;
	}
	bool legacy_consumer_state = false;
	state.world.for_each_person_commodity_need([&](auto) { legacy_consumer_state = true; });
	return !legacy_consumer_state;
}

void hash_bytes(uint64_t& hash, void const* data, size_t size) {
	auto bytes = static_cast<unsigned char const*>(data);
	for(size_t index = 0; index < size; ++index) {
		hash ^= uint64_t(bytes[index]);
		hash *= 1099511628211ull;
	}
}

template<typename T>
void hash_scalar(uint64_t& hash, T value) {
	static_assert(std::is_trivially_copyable_v<T>);
	hash_bytes(hash, &value, sizeof(value));
}

void hash_person(uint64_t& hash, person_key person) {
	hash_scalar(hash, person.source_population_cell);
	hash_scalar(hash, person.ordinal);
}

void hash_account_ref(sys::state const& state, uint64_t& hash,
	economy::exact_person_economy::account_ref account) {
	hash_scalar(hash, uint8_t(account.kind));
	if(account.kind == economy::exact_person_economy::account_kind::exact) {
		hash_person(hash, economy::exact_person_economy::owner_of(state, account));
		hash_scalar(hash, uint32_t(economy::exact_person_economy::settlement_of(state, account).index()));
	} else if(account.kind == economy::exact_person_economy::account_kind::dcon) {
		hash_scalar(hash, uint32_t(accounts::owner_of(state, account.dcon_account).index()));
		hash_scalar(hash, uint32_t(accounts::settlement_of(state, account.dcon_account).index()));
	}
}

} // namespace

bool validate_canonical_household_economy(sys::state const& state) {
	return validate_canonical_state(state);
}

uint64_t canonical_household_checksum(sys::state const& state) {
	using namespace economy::exact_person_economy;
	uint64_t hash = 14695981039346656037ull;
	auto economy_state = economy::exact_person_economy::export_snapshot(state);
	std::sort(economy_state.accounts.begin(), economy_state.accounts.end(), [](auto const& left, auto const& right) {
		if(person_key_less(left.owner, right.owner)) return true;
		if(person_key_less(right.owner, left.owner)) return false;
		return left.settlement.index() < right.settlement.index();
	});
	for(auto const& account : economy_state.accounts) {
		hash_person(hash, account.owner);
		hash_scalar(hash, uint32_t(account.settlement.index()));
		hash_scalar(hash, account.balance);
	}
	auto contracts = economy_state.contracts;
	std::sort(contracts.begin(), contracts.end(), [](auto const& left, auto const& right) {
		if(person_key_less(left.worker, right.worker)) return true;
		if(person_key_less(right.worker, left.worker)) return false;
		if(left.start_date != right.start_date) return left.start_date < right.start_date;
		return left.causal_sequence < right.causal_sequence;
	});
	for(auto const& contract : contracts) {
		hash_person(hash, contract.worker);
		hash_scalar(hash, uint32_t(contract.employer.index()));
		hash_scalar(hash, uint32_t(contract.factory.index()));
		hash_scalar(hash, uint32_t(contract.institution.index()));
		hash_scalar(hash, uint32_t(contract.workplace.index()));
		hash_scalar(hash, uint8_t(contract.status));
		hash_scalar(hash, contract.labor_capacity);
		hash_scalar(hash, contract.wage_rate);
		hash_scalar(hash, contract.pay_period_days);
		hash_scalar(hash, contract.start_date.to_raw_value());
		hash_scalar(hash, contract.end_date.to_raw_value());
		hash_scalar(hash, contract.unpaid_wages);
		hash_scalar(hash, contract.causal_sequence);
	}
	auto transactions = economy_state.transactions;
	std::sort(transactions.begin(), transactions.end(), [](auto const& left, auto const& right) {
		return left.id < right.id;
	});
	for(auto const& transaction : transactions) {
		hash_scalar(hash, transaction.id);
		hash_account_ref(state, hash, transaction.source);
		hash_account_ref(state, hash, transaction.destination);
		hash_scalar(hash, transaction.amount);
		hash_scalar(hash, uint32_t(transaction.settlement.index()));
		hash_scalar(hash, uint8_t(transaction.kind));
		hash_scalar(hash, transaction.timestamp.to_raw_value());
	}
	auto goods = export_snapshot(state);
	std::sort(goods.imported_need_profiles.begin(), goods.imported_need_profiles.end(), person_key_less);
	for(auto person : goods.imported_need_profiles) hash_person(hash, person);
	std::sort(goods.stocks.begin(), goods.stocks.end(), [](auto const& left, auto const& right) {
		if(person_key_less(left.owner, right.owner)) return true;
		if(person_key_less(right.owner, left.owner)) return false;
		return std::tuple{left.site.index(), left.commodity.index()}
			< std::tuple{right.site.index(), right.commodity.index()};
	});
	for(auto const& stock : goods.stocks) {
		hash_person(hash, stock.owner);
		hash_scalar(hash, uint32_t(stock.site.index()));
		hash_scalar(hash, uint32_t(stock.commodity.index()));
		hash_scalar(hash, stock.quantity);
	}
	std::sort(goods.needs.begin(), goods.needs.end(), [](auto const& left, auto const& right) {
		if(person_key_less(left.owner, right.owner)) return true;
		if(person_key_less(right.owner, left.owner)) return false;
		return left.commodity.index() < right.commodity.index();
	});
	for(auto const& need : goods.needs) {
		hash_person(hash, need.owner);
		hash_scalar(hash, uint32_t(need.commodity.index()));
		hash_scalar(hash, need.desired_quantity_per_period);
		hash_scalar(hash, need.consumed_this_period);
		hash_scalar(hash, need.consumption_period_start.to_raw_value());
		hash_scalar(hash, need.unmet_quantity);
		hash_scalar(hash, need.last_consumed_quantity);
		hash_scalar(hash, need.last_consumed_on.to_raw_value());
	}
	std::sort(goods.bids.begin(), goods.bids.end(), [](auto const& left, auto const& right) {
		if(person_key_less(left.buyer, right.buyer)) return true;
		if(person_key_less(right.buyer, left.buyer)) return false;
		return std::tuple{left.created_on.to_raw_value(), left.causal_sequence,
			left.commodity.index(), left.destination.index(), left.id}
			< std::tuple{right.created_on.to_raw_value(), right.causal_sequence,
				right.commodity.index(), right.destination.index(), right.id};
	});
	for(auto const& bid : goods.bids) {
		hash_scalar(hash, bid.id);
		hash_person(hash, bid.buyer);
		hash_scalar(hash, uint32_t(bid.destination.index()));
		hash_scalar(hash, uint32_t(bid.market.index()));
		hash_scalar(hash, uint32_t(bid.commodity.index()));
		hash_scalar(hash, bid.original_quantity);
		hash_scalar(hash, bid.remaining_quantity);
		hash_scalar(hash, bid.limit_price);
		hash_scalar(hash, bid.reserved_amount);
		hash_scalar(hash, bid.created_on.to_raw_value());
		hash_scalar(hash, bid.causal_sequence);
		hash_scalar(hash, uint8_t(bid.status));
	}
	std::sort(goods.fills.begin(), goods.fills.end(), [](auto const& left, auto const& right) {
		if(left.occurred_on != right.occurred_on) return left.occurred_on < right.occurred_on;
		if(left.exact_bid_id != right.exact_bid_id) return left.exact_bid_id < right.exact_bid_id;
		return left.id < right.id;
	});
	for(auto const& fill : goods.fills) {
		hash_scalar(hash, fill.id);
		hash_scalar(hash, fill.exact_bid_id);
		hash_scalar(hash, uint32_t(fill.dcon_ask.index()));
		hash_scalar(hash, fill.exact_transaction_id);
		hash_scalar(hash, fill.quantity);
		hash_scalar(hash, fill.execution_price);
		hash_scalar(hash, uint32_t(fill.source.index()));
		hash_scalar(hash, uint32_t(fill.destination.index()));
		hash_scalar(hash, uint32_t(fill.market.index()));
		hash_scalar(hash, uint32_t(fill.commodity.index()));
		hash_scalar(hash, fill.occurred_on.to_raw_value());
	}
	auto freight = exact_person_freight::export_snapshot(state);
	std::sort(freight.requests.begin(), freight.requests.end(), [](auto const& left, auto const& right) {
		if(person_key_less(left.requester, right.requester)) return true;
		if(person_key_less(right.requester, left.requester)) return false;
		return std::tuple{left.created_on.to_raw_value(), left.causal_sequence,
			left.commodity.index(), left.source.index(), left.destination.index(), left.id}
			< std::tuple{right.created_on.to_raw_value(), right.causal_sequence,
				right.commodity.index(), right.source.index(), right.destination.index(), right.id};
	});
	for(auto const& request : freight.requests) {
		hash_scalar(hash, request.id);
		hash_person(hash, request.requester);
		hash_scalar(hash, uint32_t(request.source.index()));
		hash_scalar(hash, uint32_t(request.destination.index()));
		hash_scalar(hash, uint32_t(request.commodity.index()));
		hash_scalar(hash, request.quantity);
		hash_scalar(hash, request.cargo_units);
		hash_scalar(hash, request.created_on.to_raw_value());
		hash_scalar(hash, request.causal_sequence);
		hash_scalar(hash, request.route_distance);
		hash_scalar(hash, request.required_mode_mask);
		hash_scalar(hash, uint32_t(request.primary_trade_route.index()));
		hash_scalar(hash, request.route_leg_count);
		hash_scalar(hash, uint8_t(request.status));
		hash_scalar(hash, request.originating_fill_id);
	}
	std::sort(freight.contracts.begin(), freight.contracts.end(), [](auto const& left, auto const& right) {
		return left.id < right.id;
	});
	for(auto const& contract : freight.contracts) {
		hash_scalar(hash, contract.id);
		hash_scalar(hash, contract.request_id);
		hash_person(hash, contract.requester);
		hash_scalar(hash, uint32_t(contract.offer.index()));
		hash_scalar(hash, uint32_t(contract.carrier.index()));
		hash_scalar(hash, uint32_t(contract.carrier_account.index()));
		hash_scalar(hash, contract.exact_payer_account_id);
		hash_scalar(hash, uint32_t(contract.commodity.index()));
		hash_scalar(hash, uint32_t(contract.source.index()));
		hash_scalar(hash, uint32_t(contract.destination.index()));
		hash_scalar(hash, contract.quantity);
		hash_scalar(hash, contract.cargo_units);
		hash_scalar(hash, contract.agreed_freight_price);
		hash_scalar(hash, contract.exact_payment_transaction_id);
		hash_scalar(hash, uint32_t(contract.shipment.index()));
		hash_scalar(hash, contract.created_on.to_raw_value());
		hash_scalar(hash, uint8_t(contract.status));
	}
	std::sort(freight.shipment_owners.begin(), freight.shipment_owners.end(), [](auto const& left, auto const& right) {
		return left.shipment.index() < right.shipment.index();
	});
	for(auto const& owner : freight.shipment_owners) {
		hash_scalar(hash, uint32_t(owner.shipment.index()));
		hash_person(hash, owner.owner);
		hash_scalar(hash, owner.contract_id);
	}
	return hash;
}

price_observation observation_for_date(sys::state const& state, dcon::market_id market,
	dcon::commodity_id commodity, sys::date date) {
	price_observation result;
	std::vector<fill_record const*> fills;
	for(auto const& fill : ensure_store(state)->fills)
		if(fill.market == market && fill.commodity == commodity && fill.occurred_on == date)
			fills.push_back(&fill);
	std::sort(fills.begin(), fills.end(), [](auto left, auto right) {
		return std::tuple{left->source.index(), left->execution_price,
			left->exact_bid_id, left->id}
			< std::tuple{right->source.index(), right->execution_price,
				right->exact_bid_id, right->id};
	});
	for(auto fill : fills) {
		result.quantity += fill->quantity;
		result.value += fill->quantity * fill->execution_price;
		}
	return result;
}

std::vector<market_activity_record> market_activity_for_date(sys::state const& state, sys::date date) {
	std::unordered_map<uint64_t, market_activity_record> activity;
	auto get_record = [&](dcon::market_id market, dcon::commodity_id commodity) -> market_activity_record* {
		if(!market || !state.world.market_is_valid(market) || !commodity || !state.world.commodity_is_valid(commodity)) return nullptr;
		auto key = (uint64_t(market.index()) << 32) | uint64_t(commodity.index());
		auto [it, inserted] = activity.try_emplace(key);
		if(inserted) {
			it->second.market = market;
			it->second.commodity = commodity;
		}
		return &it->second;
	};

	std::vector<bid_record const*> bids;
	for(auto const& bid : ensure_store(state)->bids)
		if(bid.created_on == date) bids.push_back(&bid);
	std::sort(bids.begin(), bids.end(), [](auto left, auto right) {
		if(left->market != right->market) return left->market.index() < right->market.index();
		if(left->commodity != right->commodity) return left->commodity.index() < right->commodity.index();
		if(person_key_less(left->buyer, right->buyer)) return true;
		if(person_key_less(right->buyer, left->buyer)) return false;
		return left->causal_sequence < right->causal_sequence;
	});
	for(auto bid : bids) {
		if(auto record = get_record(bid->market, bid->commodity))
			record->submitted_demand += std::max(0.0f, bid->original_quantity);
	}
	std::vector<fill_record const*> fills;
	for(auto const& fill : ensure_store(state)->fills)
		if(fill.occurred_on == date) fills.push_back(&fill);
	std::sort(fills.begin(), fills.end(), [](auto left, auto right) {
		if(left->market != right->market) return left->market.index() < right->market.index();
		if(left->commodity != right->commodity) return left->commodity.index() < right->commodity.index();
		return std::tuple{left->source.index(), left->execution_price,
			left->exact_bid_id, left->id}
			< std::tuple{right->source.index(), right->execution_price,
				right->exact_bid_id, right->id};
	});
	for(auto fill : fills) {
		if(auto record = get_record(fill->market, fill->commodity)) {
			record->traded_quantity += std::max(0.0f, fill->quantity);
			record->trade_value += std::max(0.0f, fill->quantity) * std::max(0.0f, fill->execution_price);
			auto origin = concrete_market::market_for_site(state, fill->source);
			if(origin && origin != fill->market) {
				record->imports += std::max(0.0f, fill->quantity);
				if(auto origin_record = get_record(origin, fill->commodity))
					origin_record->exports += std::max(0.0f, fill->quantity);
			}
		}
	}
	std::vector<need_record const*> needs;
	for(auto const& need : ensure_store(state)->needs)
		if(need.last_consumed_on == date && need.last_consumed_quantity > 0.0f)
			needs.push_back(&need);
	std::sort(needs.begin(), needs.end(), [](auto left, auto right) {
		if(person_key_less(left->owner, right->owner)) return true;
		if(person_key_less(right->owner, left->owner)) return false;
		return left->commodity.index() < right->commodity.index();
	});
	for(auto need : needs) {
		auto home = persons::home_site(state, need->owner);
		if(auto record = get_record(market_for_site(state, home), need->commodity))
			record->consumed_quantity += need->last_consumed_quantity;
	}

	std::vector<market_activity_record> result;
	result.reserve(activity.size());
	for(auto& [key, record] : activity) {
		(void)key;
		result.push_back(record);
	}
	std::sort(result.begin(), result.end(), [](auto const& left, auto const& right) {
		if(left.market != right.market) return left.market.index() < right.market.index();
		return left.commodity.index() < right.commodity.index();
	});
	return result;
}

float observed_price(sys::state const& state, dcon::market_id market, dcon::commodity_id commodity, sys::date date) {
	auto result = observation_for_date(state, market, commodity, date);
	return result.quantity > epsilon ? result.value / result.quantity : 0.0f;
}

std::optional<sys::date> latest_fill_date(sys::state const& state, dcon::market_id market,
	dcon::commodity_id commodity, sys::date query_date) {
	std::optional<sys::date> result;
	for(auto const& fill : ensure_store(state)->fills)
		if(fill.market == market && fill.commodity == commodity && fill.occurred_on < query_date && !result || fill.occurred_on > *result) result = fill.occurred_on;
	return result;
}

float concrete_reference_price(sys::state const& state, dcon::market_id market, dcon::commodity_id commodity, sys::date date) {
	sys::date latest{}; bool found = false; float quantity = 0.0f, value = 0.0f;
	for(auto const& fill : ensure_store(state)->fills) {
		if(fill.market != market || fill.commodity != commodity || fill.occurred_on >= date) continue;
		if(!found || fill.occurred_on > latest) { found = true; latest = fill.occurred_on; quantity = 0.0f; value = 0.0f; }
		if(fill.occurred_on == latest) { quantity += fill.quantity; value += fill.quantity * fill.execution_price; }
	}
	return quantity > epsilon ? value / quantity : -1.0f;
}

uint64_t bid_count(sys::state const& state) { return uint64_t(ensure_store(state)->bids.size()); }
uint64_t fill_count(sys::state const& state) { return uint64_t(ensure_store(state)->fills.size()); }

float reserved_bid_amount(sys::state const& state, uint64_t exact_account_id) {
	return reserved_exact(state, exact_account_id);
}

goods_snapshot export_snapshot(sys::state const& state) {
	goods_snapshot result; result.version = snapshot_version;
	result.stocks = ensure_store(state)->stocks; result.needs = ensure_store(state)->needs;
	result.bids = ensure_store(state)->bids; result.fills = ensure_store(state)->fills;
	result.imported_need_profiles = ensure_store(state)->imported_need_profiles;
	return result;
}

bool import_snapshot(sys::state& state, goods_snapshot const& snapshot) {
	if(snapshot.version == 0 || snapshot.version > snapshot_version) return false;
	auto candidate = std::make_shared<exact_person_goods_store>();
	for(auto const& record : snapshot.stocks) {
		if(!persons::exists(state, record.owner) || !record.site || !state.world.site_is_valid(record.site) || !record.commodity || !state.world.commodity_is_valid(record.commodity) || !positive_finite(record.quantity)) return false;
		candidate->stocks.push_back(record);
	}
	for(auto const& record : snapshot.needs) {
		if(!persons::exists(state, record.owner) || !record.commodity || !state.world.commodity_is_valid(record.commodity) || !nonnegative_finite(record.desired_quantity_per_period) || !nonnegative_finite(record.consumed_this_period) || !nonnegative_finite(record.unmet_quantity)) return false;
		candidate->needs.push_back(record);
	}
	for(auto record : snapshot.bids) {
		if(!record.id || !persons::exists(state, record.buyer) || !economy::exact_person_economy::account_exists(state,
				economy::exact_person_economy::account_ref::from_exact(record.exact_account_id)) || economy::exact_person_economy::owner_of(state,
				economy::exact_person_economy::account_ref::from_exact(record.exact_account_id)) != record.buyer || !record.destination || !state.world.site_is_valid(record.destination) || !record.market || !state.world.market_is_valid(record.market) || !record.commodity || !state.world.commodity_is_valid(record.commodity) || !positive_finite(record.original_quantity) || !nonnegative_finite(record.remaining_quantity) || record.remaining_quantity > record.original_quantity + epsilon || !positive_finite(record.limit_price) || !nonnegative_finite(record.reserved_amount) || uint8_t(record.status) > uint8_t(order_status::filled)) return false;
		if(record.causal_sequence == 0) return false;
		economy::causal_order::observe(state, record.causal_sequence);
		candidate->bids.push_back(record); candidate->next_bid_id = std::max(candidate->next_bid_id, record.id + 1);
	}
	for(auto const& record : snapshot.fills) {
		if(!record.id || !record.exact_bid_id || !record.dcon_ask || !state.world.concrete_market_ask_is_valid(record.dcon_ask) || std::none_of(candidate->bids.begin(), candidate->bids.end(), [&](auto const& bid) { return bid.id == record.exact_bid_id; }) || !economy::exact_person_economy::transaction(state, record.exact_transaction_id) || !record.source || !record.destination || !state.world.site_is_valid(record.source) || !state.world.site_is_valid(record.destination) || !record.market || !state.world.market_is_valid(record.market) || !record.commodity || !state.world.commodity_is_valid(record.commodity) || !positive_finite(record.quantity) || !positive_finite(record.execution_price)) return false;
		candidate->fills.push_back(record); candidate->next_fill_id = std::max(candidate->next_fill_id, record.id + 1);
	}
	if(snapshot.version >= 2) {
		for(auto owner : snapshot.imported_need_profiles) {
			if(!persons::exists(state, owner)
				|| std::find(candidate->imported_need_profiles.begin(),
					candidate->imported_need_profiles.end(), owner)
				!= candidate->imported_need_profiles.end()) return false;
			candidate->imported_need_profiles.push_back(owner);
		}
	} else {
		// Older exact saves stored need records but did not store an explicit
		// import marker. Treat every already-accounted person as migrated so the
		// legacy scenario profile is never read back after restore.
		for(auto owner : economy::exact_person_economy::account_owners(state))
			candidate->imported_need_profiles.push_back(owner);
		for(auto const& need : candidate->needs)
			candidate->imported_need_profiles.push_back(need.owner);
		std::sort(candidate->imported_need_profiles.begin(),
			candidate->imported_need_profiles.end(), [](person_key left, person_key right) {
				return left.source_population_cell == right.source_population_cell
					? left.ordinal < right.ordinal
					: left.source_population_cell < right.source_population_cell;
			});
		candidate->imported_need_profiles.erase(std::unique(
			candidate->imported_need_profiles.begin(), candidate->imported_need_profiles.end()),
			candidate->imported_need_profiles.end());
	}
	state.exact_person_goods = std::move(candidate);
	return true;
}

void initialize_empty_store(sys::state& state) {
	assert(!state.exact_person_goods && "exact person goods store initialized more than once");
	if(state.exact_person_goods) std::abort();
	state.exact_person_goods = std::make_shared<exact_person_goods_store>();
}

void clear_store(sys::state& state) { state.exact_person_goods.reset(); }

} // namespace economy::physical::exact_person_goods
