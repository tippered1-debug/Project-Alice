#include "exact_person_goods.hpp"

#include "accounts/accounts.hpp"
#include "economy/relations/relations.hpp"
#include "concrete_market.hpp"
#include "exact_person_freight.hpp"
#include "inventory.hpp"
#include "system_state.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <unordered_map>

namespace economy::physical {

struct exact_person_goods_store {
	std::vector<exact_person_goods::stock_record> stocks;
	std::vector<exact_person_goods::need_record> needs;
	std::vector<exact_person_goods::bid_record> bids;
	std::vector<exact_person_goods::fill_record> fills;
	uint64_t next_bid_id = 1;
	uint64_t next_fill_id = 1;
};

} // namespace economy::physical

namespace economy::physical::exact_person_goods {
namespace {

constexpr uint32_t snapshot_version = 1;
constexpr float epsilon = 1.0e-5f;

std::shared_ptr<exact_person_goods_store> ensure_store(sys::state& state) {
	if(!state.exact_person_goods)
		state.exact_person_goods = std::make_shared<exact_person_goods_store>();
	return state.exact_person_goods;
}

std::shared_ptr<exact_person_goods_store> ensure_store(sys::state const& state) {
	return ensure_store(const_cast<sys::state&>(state));
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
	for(auto const& bid : ensure_store(state)->bids)
		if(bid.id != except_bid && bid.status == order_status::active && bid.exact_account_id == account_id)
			result += std::max(0.0f, bid.reserved_amount);
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
	auto owned = stock_quantity(state, need.owner, persons::exact_population::home_site(state, need.owner), need.commodity);
	need.unmet_quantity = std::max(0.0f, need.desired_quantity_per_period
		- std::max(0.0f, need.consumed_this_period) - owned);
	return need.unmet_quantity;
}

bool active_equivalent_bid(sys::state const& state, person_key owner, dcon::site_id destination,
	dcon::commodity_id commodity) {
	for(auto const& bid : ensure_store(state)->bids)
		if(bid.status == order_status::active && bid.buyer == owner
			&& bid.destination == destination && bid.commodity == commodity) return true;
	return false;
}

std::vector<dcon::commodity_id> seller_settlements(sys::state const& state,
	dcon::market_id market, dcon::commodity_id commodity) {
	std::vector<dcon::commodity_id> result;
	state.world.for_each_concrete_market_ask([&](auto ask) {
		if(state.world.concrete_market_ask_get_status(ask) != uint8_t(order_status::active)
			|| state.world.concrete_market_ask_get_market_from_concrete_ask_market(ask) != market
			|| state.world.concrete_market_ask_get_commodity_from_concrete_ask_commodity(ask) != commodity) return;
		auto seller = state.world.concrete_market_ask_get_economic_actor_from_concrete_ask_seller(ask);
		state.world.economic_actor_for_each_monetary_account_owner_as_economic_actor(seller, [&](auto relation) {
			auto account = state.world.monetary_account_owner_get_monetary_account(relation);
			auto settlement = accounts::settlement_of(state, account);
			if(account && state.world.monetary_account_is_valid(account) && settlement
				&& state.world.commodity_is_valid(settlement)) result.push_back(settlement);
		});
	});
	std::sort(result.begin(), result.end(), [](auto a, auto b) { return a.index() < b.index(); });
	result.erase(std::unique(result.begin(), result.end()), result.end());
	return result;
}

} // namespace

float stock_quantity(sys::state const& state, person_key owner, dcon::site_id site,
	dcon::commodity_id commodity) {
	if(!persons::exact_population::exists(state, owner) || !site || !commodity) return 0.0f;
	auto record = stock_for(state, owner, site, commodity);
	return record ? std::max(0.0f, record->quantity) : 0.0f;
}

float add_stock(sys::state& state, person_key owner, dcon::site_id site,
	dcon::commodity_id commodity, float amount) {
	if(!persons::exact_population::exists(state, owner) || !site || !state.world.site_is_valid(site)
		|| !commodity || !state.world.commodity_is_valid(commodity) || !positive_finite(amount)) return 0.0f;
	auto record = stock_for(state, owner, site, commodity);
	auto current = record ? record->quantity : 0.0f;
	if(!nonnegative_finite(current) || amount > std::numeric_limits<float>::max() - current) return 0.0f;
	if(!record) {
		record = &ensure_store(state)->stocks.emplace_back();
		record->owner = owner; record->site = site; record->commodity = commodity;
	}
	record->quantity += amount;
	return amount;
}

float remove_stock(sys::state& state, person_key owner, dcon::site_id site,
	dcon::commodity_id commodity, float amount) {
	if(!positive_finite(amount)) return 0.0f;
	auto record = stock_for(state, owner, site, commodity);
	if(!record) return 0.0f;
	auto removed = std::min(std::max(0.0f, record->quantity), amount);
	record->quantity -= removed;
	return removed;
}

bool set_need(sys::state& state, person_key owner, dcon::commodity_id commodity, float desired) {
	if(!persons::exact_population::exists(state, owner) || !commodity
		|| !state.world.commodity_is_valid(commodity) || !nonnegative_finite(desired)) return false;
	auto record = need_for(state, owner, commodity);
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

float unmet_need(sys::state const& state, person_key owner, dcon::commodity_id commodity) {
	if(auto record = need_for(state, owner, commodity)) {
		return std::max(0.0f, record->desired_quantity_per_period
			- std::max(0.0f, record->consumed_this_period)
			- stock_quantity(state, owner, persons::exact_population::home_site(state, owner), commodity));
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

float process_consumption(sys::state& state, person_key owner, dcon::commodity_id commodity) {
	auto record = need_for(state, owner, commodity);
	if(!record || !persons::exact_population::alive(state, owner)) return 0.0f;
	auto remaining = std::max(0.0f, record->desired_quantity_per_period - record->consumed_this_period);
	auto site = persons::exact_population::home_site(state, owner);
	auto amount = std::min(remaining, stock_quantity(state, owner, site, commodity));
	auto consumed = remove_stock(state, owner, site, commodity, amount);
	record->consumed_this_period += consumed;
	record->last_consumed_quantity = consumed;
	record->last_consumed_on = state.current_date;
	refresh_unmet(state, *record);
	return consumed;
}

uint64_t post_bid(sys::state& state, person_key buyer, economy::exact_person_economy::account_ref account,
	dcon::site_id destination, dcon::market_id market, dcon::commodity_id commodity,
	float quantity, float limit_price, order_purpose purpose) {
	using namespace economy::exact_person_economy;
	if(!persons::exact_population::exists(state, buyer) || !persons::exact_population::alive(state, buyer)
		|| account.kind != account_kind::exact || owner_of(state, account) != buyer
		|| !destination || !market || !commodity || !state.world.site_is_valid(destination)
		|| !state.world.market_is_valid(market) || !state.world.commodity_is_valid(commodity)
		|| !positive_finite(quantity) || !positive_finite(limit_price)) return 0;
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
			result.push_back({bid.id, bid.created_on});
	std::sort(result.begin(), result.end(), [](auto const& left, auto const& right) {
		return left.created_on == right.created_on ? left.id < right.id : left.created_on < right.created_on;
	});
	return result;
}

uint64_t try_fill(sys::state& state, uint64_t exact_bid_id, dcon::concrete_market_ask_id ask,
	sys::date date) {
	auto bid = bid_for(state, exact_bid_id);
	if(!bid || bid->status != order_status::active || !ask
		|| state.world.concrete_market_ask_get_status(ask) != uint8_t(order_status::active)) return 0;
	auto source = state.world.concrete_market_ask_get_site_from_concrete_ask_site(ask);
	auto seller = state.world.concrete_market_ask_get_economic_actor_from_concrete_ask_seller(ask);
	auto market = state.world.concrete_market_ask_get_market_from_concrete_ask_market(ask);
	auto commodity = state.world.concrete_market_ask_get_commodity_from_concrete_ask_commodity(ask);
	auto price = state.world.concrete_market_ask_get_minimum_price(ask);
	if(!source || market != bid->market || commodity != bid->commodity
		|| !seller || !positive_finite(price) || price > bid->limit_price
		|| !positive_finite(bid->remaining_quantity)) return 0;
	using namespace economy::exact_person_economy;
	auto buyer_account = account_ref::from_exact(bid->exact_account_id);
	auto settlement = settlement_of(state, buyer_account);
	auto seller_account = accounts::find_account(state, seller, settlement);
	if(!seller_account || accounts::owner_of(state, seller_account) != seller
		|| inventory::quantity(state, source, commodity, seller) <= 0.0f) return 0;
	auto quantity = std::min(bid->remaining_quantity,
		state.world.concrete_market_ask_get_remaining_quantity(ask));
	quantity = std::min(quantity, inventory::quantity(state, source, commodity, seller));
	auto free = balance(state, buyer_account) - reserved_exact(state, bid->exact_account_id) + bid->reserved_amount;
	quantity = std::min(quantity, std::max(0.0f, free / price));
	if(!positive_finite(quantity) || quantity * price > free + epsilon) return 0;
	auto fill_id = next_id(ensure_store(state)->next_fill_id);
	if(!fill_id) return 0;
	if(add_stock(state, bid->buyer, source, commodity, quantity) != quantity) return 0;
	if(inventory::remove(state, source, commodity, quantity, seller) != quantity) {
		remove_stock(state, bid->buyer, source, commodity, quantity);
		return 0;
	}
	auto transfer = transfer_with_result(state, buyer_account, account_ref::from_dcon(seller_account),
		quantity * price, relations::transaction_kind::purchase, date);
	if(!transfer.success) {
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
	fill.market = market; fill.commodity = commodity; fill.occurred_on = date;
	ensure_store(state)->fills.push_back(fill);
	if(source != bid->destination)
		(void)exact_person_freight::create_request(state, bid->buyer, source, bid->destination,
			commodity, quantity, fill_id);
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

bool process_purchase_decision(sys::state& state, person_key buyer, dcon::commodity_id commodity) {
	if(!persons::exact_population::exists(state, buyer) || !persons::exact_population::alive(state, buyer)) return false;
	auto site = persons::exact_population::home_site(state, buyer);
	auto market = market_for_site(state, site);
	auto record = need_for(state, buyer, commodity);
	if(!site || !market || !record
		|| active_equivalent_bid(state, buyer, site, commodity)) return false;
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
				if(cash > best_cash || (cash == best_cash && candidate.exact_account_id < selected.exact_account_id)) {
					selected = candidate; best_cash = cash;
				}
			}
	if(!selected || best_cash <= epsilon) return false;
	auto quantity = std::min(remaining_to_acquire, best_cash / price);
	auto id = post_bid(state, buyer, selected, site, market, commodity, quantity, price);
	if(!id) return false;
	(void)concrete_market::match(state, market, commodity, state.current_date);
	return true;
}

void process_purchase_decisions(sys::state& state, person_key buyer) {
	for(auto const& record : ensure_store(state)->needs)
		if(record.owner == buyer) (void)process_purchase_decision(state, buyer, record.commodity);
}

price_observation observation_for_date(sys::state const& state, dcon::market_id market,
	dcon::commodity_id commodity, sys::date date) {
	price_observation result;
	for(auto const& fill : ensure_store(state)->fills)
		if(fill.market == market && fill.commodity == commodity && fill.occurred_on == date) {
			result.quantity += fill.quantity;
			result.value += fill.quantity * fill.execution_price;
		}
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
		if(fill.market == market && fill.commodity == commodity && fill.occurred_on < query_date
			&& (!result || fill.occurred_on > *result)) result = fill.occurred_on;
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
	return result;
}

bool import_snapshot(sys::state& state, goods_snapshot const& snapshot) {
	if(snapshot.version != snapshot_version) return false;
	auto candidate = std::make_shared<exact_person_goods_store>();
	for(auto const& record : snapshot.stocks) {
		if(!persons::exact_population::exists(state, record.owner) || !record.site || !state.world.site_is_valid(record.site)
			|| !record.commodity || !state.world.commodity_is_valid(record.commodity) || !positive_finite(record.quantity)) return false;
		candidate->stocks.push_back(record);
	}
	for(auto const& record : snapshot.needs) {
		if(!persons::exact_population::exists(state, record.owner) || !record.commodity
			|| !state.world.commodity_is_valid(record.commodity) || !nonnegative_finite(record.desired_quantity_per_period)
			|| !nonnegative_finite(record.consumed_this_period) || !nonnegative_finite(record.unmet_quantity)) return false;
		candidate->needs.push_back(record);
	}
	for(auto const& record : snapshot.bids) {
		if(!record.id || !persons::exact_population::exists(state, record.buyer)
			|| !economy::exact_person_economy::account_exists(state,
				economy::exact_person_economy::account_ref::from_exact(record.exact_account_id))
			|| economy::exact_person_economy::owner_of(state,
				economy::exact_person_economy::account_ref::from_exact(record.exact_account_id)) != record.buyer
			|| !record.destination || !state.world.site_is_valid(record.destination) || !record.market
			|| !state.world.market_is_valid(record.market) || !record.commodity || !state.world.commodity_is_valid(record.commodity)
			|| !positive_finite(record.original_quantity) || !nonnegative_finite(record.remaining_quantity)
			|| record.remaining_quantity > record.original_quantity + epsilon
			|| !positive_finite(record.limit_price) || !nonnegative_finite(record.reserved_amount)
			|| uint8_t(record.status) > uint8_t(order_status::filled)) return false;
		candidate->bids.push_back(record); candidate->next_bid_id = std::max(candidate->next_bid_id, record.id + 1);
	}
	for(auto const& record : snapshot.fills) {
		if(!record.id || !record.exact_bid_id || !record.dcon_ask || !state.world.concrete_market_ask_is_valid(record.dcon_ask)
			|| std::none_of(candidate->bids.begin(), candidate->bids.end(), [&](auto const& bid) { return bid.id == record.exact_bid_id; })
			|| !economy::exact_person_economy::transaction(state, record.exact_transaction_id)
			|| !record.source || !record.destination || !state.world.site_is_valid(record.source)
			|| !state.world.site_is_valid(record.destination) || !record.market || !state.world.market_is_valid(record.market)
			|| !record.commodity || !state.world.commodity_is_valid(record.commodity)
			|| !positive_finite(record.quantity) || !positive_finite(record.execution_price)) return false;
		candidate->fills.push_back(record); candidate->next_fill_id = std::max(candidate->next_fill_id, record.id + 1);
	}
	state.exact_person_goods = std::move(candidate);
	return true;
}

void clear_store(sys::state& state) { state.exact_person_goods.reset(); }

} // namespace economy::physical::exact_person_goods
