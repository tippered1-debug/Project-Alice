#include "concrete_market.hpp"

#include "accounts/accounts.hpp"
#include "exchange.hpp"
#include "inventory.hpp"
#include "shipments.hpp"
#include "freight_market.hpp"
#include "exact_person_goods.hpp"
#include "system_state.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace economy::physical::concrete_market {
namespace {
constexpr uint8_t active = uint8_t(order_status::active);
constexpr float epsilon = 1.0e-5f;

bool valid(float value) { return std::isfinite(value) && value > 0.0f; }

float reserved_inventory(sys::state const& state, dcon::economic_actor_id seller,
	dcon::site_id source, dcon::commodity_id commodity) {
	float result = 0.0f;
	state.world.for_each_concrete_market_ask([&](auto ask) {
		if(state.world.concrete_market_ask_get_status(ask) != active
			|| state.world.concrete_market_ask_get_concrete_ask_seller(ask)
			&& state.world.concrete_market_ask_get_economic_actor_from_concrete_ask_seller(ask) != seller
			|| state.world.concrete_market_ask_get_concrete_ask_site(ask)
			&& state.world.concrete_market_ask_get_site_from_concrete_ask_site(ask) != source
			|| state.world.concrete_market_ask_get_concrete_ask_commodity(ask)
			&& state.world.concrete_market_ask_get_commodity_from_concrete_ask_commodity(ask) != commodity) return;
		result += std::max(0.0f, state.world.concrete_market_ask_get_reserved_quantity(ask));
	});
	return result;
}

float reserved_funds(sys::state const& state, dcon::monetary_account_id account) {
	float result = 0.0f;
	state.world.for_each_concrete_market_bid([&](auto bid) {
		if(state.world.concrete_market_bid_get_status(bid) == active
			&& state.world.concrete_market_bid_get_concrete_bid_account(bid)
			&& state.world.concrete_market_bid_get_monetary_account_from_concrete_bid_account(bid) == account)
			result += std::max(0.0f, state.world.concrete_market_bid_get_reserved_amount(bid));
	});
	return result;
}
}

float reserved_bid_amount(sys::state const& state, dcon::monetary_account_id account) {
	return reserved_funds(state, account);
}

float active_factory_bid_quantity(sys::state const& state, dcon::factory_id factory,
	dcon::site_id destination, dcon::commodity_id commodity) {
	float result = 0.0f;
	state.world.for_each_concrete_market_bid([&](auto bid) {
		if(state.world.concrete_market_bid_get_status(bid) != active
			|| state.world.concrete_market_bid_get_factory_from_concrete_bid_factory(bid) != factory
			|| state.world.concrete_market_bid_get_site_from_concrete_bid_destination(bid) != destination
			|| state.world.concrete_market_bid_get_commodity_from_concrete_bid_commodity(bid) != commodity) return;
		result += std::max(0.0f, state.world.concrete_market_bid_get_remaining_quantity(bid));
	});
	return result;
}

dcon::concrete_market_bid_id post_bid(sys::state& state, dcon::economic_actor_id buyer,
	dcon::monetary_account_id account, dcon::site_id destination, dcon::market_id market,
	dcon::commodity_id commodity, float quantity, float limit_price, order_purpose purpose) {
	if(!buyer || !account || accounts::owner_of(state, account) != buyer || !destination || !market || !commodity
		|| !state.world.commodity_is_valid(accounts::settlement_of(state, account))
		|| !valid(quantity) || !valid(limit_price)) return {};
	if(accounts::balance(state, account) + epsilon < reserved_funds(state, account) + quantity * limit_price) return {};
	auto bid = state.world.create_concrete_market_bid();
	state.world.concrete_market_bid_set_original_quantity(bid, quantity);
	state.world.concrete_market_bid_set_remaining_quantity(bid, quantity);
	state.world.concrete_market_bid_set_limit_price(bid, limit_price);
	state.world.concrete_market_bid_set_reserved_amount(bid, quantity * limit_price);
	state.world.concrete_market_bid_set_created_on(bid, state.current_date);
	state.world.concrete_market_bid_set_status(bid, active);
	state.world.concrete_market_bid_set_purpose(bid, uint8_t(purpose));
	state.world.force_create_concrete_bid_buyer(bid, buyer);
	state.world.force_create_concrete_bid_account(bid, account);
	state.world.force_create_concrete_bid_destination(bid, destination);
	state.world.force_create_concrete_bid_market(bid, market);
	state.world.force_create_concrete_bid_commodity(bid, commodity);
	return bid;
}

dcon::concrete_market_ask_id post_ask(sys::state& state, dcon::economic_actor_id seller,
	dcon::site_id source, dcon::market_id market, dcon::commodity_id commodity,
	float quantity, float minimum_price, order_purpose purpose) {
	if(!seller || !source || !market || !commodity || !valid(quantity) || !valid(minimum_price)) return {};
	auto available = inventory::quantity(state, source, commodity, seller) - reserved_inventory(state, seller, source, commodity);
	if(!std::isfinite(available) || available + epsilon < quantity) return {};
	auto ask = state.world.create_concrete_market_ask();
	state.world.concrete_market_ask_set_original_quantity(ask, quantity);
	state.world.concrete_market_ask_set_remaining_quantity(ask, quantity);
	state.world.concrete_market_ask_set_minimum_price(ask, minimum_price);
	state.world.concrete_market_ask_set_reserved_quantity(ask, quantity);
	state.world.concrete_market_ask_set_created_on(ask, state.current_date);
	state.world.concrete_market_ask_set_status(ask, active);
	state.world.concrete_market_ask_set_purpose(ask, uint8_t(purpose));
	state.world.force_create_concrete_ask_seller(ask, seller);
	state.world.force_create_concrete_ask_site(ask, source);
	state.world.force_create_concrete_ask_market(ask, market);
	state.world.force_create_concrete_ask_commodity(ask, commodity);
	return ask;
}

std::vector<dcon::concrete_trade_fill_id> match(sys::state& state, dcon::market_id market,
	dcon::commodity_id commodity, sys::date date) {
	struct candidate_bid {
		bool exact = false;
		dcon::concrete_market_bid_id dcon_bid{};
		uint64_t exact_bid = 0;
		sys::date created_on{};
	};
	std::vector<candidate_bid> bids;
	std::vector<dcon::concrete_market_ask_id> asks;
	state.world.for_each_concrete_market_bid([&](auto bid) {
		if(state.world.concrete_market_bid_get_status(bid) == active
			&& state.world.concrete_market_bid_get_market_from_concrete_bid_market(bid) == market
			&& state.world.concrete_market_bid_get_commodity_from_concrete_bid_commodity(bid) == commodity)
			bids.push_back({false, bid, 0, state.world.concrete_market_bid_get_created_on(bid)});
	});
	for(auto bid : exact_person_goods::active_bids(state, market, commodity))
		bids.push_back({true, {}, bid.id, bid.created_on});
	state.world.for_each_concrete_market_ask([&](auto ask) {
		if(state.world.concrete_market_ask_get_status(ask) == active
			&& state.world.concrete_market_ask_get_market_from_concrete_ask_market(ask) == market
			&& state.world.concrete_market_ask_get_commodity_from_concrete_ask_commodity(ask) == commodity) asks.push_back(ask);
	});
	std::sort(bids.begin(), bids.end(), [&](auto const& a, auto const& b) {
		if(a.created_on != b.created_on) return a.created_on < b.created_on;
		if(a.exact != b.exact) return !a.exact; // DCON wins an equal-date cross-kind tie.
		return a.exact ? a.exact_bid < b.exact_bid : a.dcon_bid.index() < b.dcon_bid.index();
	});
	std::sort(asks.begin(), asks.end(), [&](auto a, auto b) { auto ap = state.world.concrete_market_ask_get_minimum_price(a), bp = state.world.concrete_market_ask_get_minimum_price(b); return ap == bp ? a.index() < b.index() : ap < bp; });
	std::vector<dcon::concrete_trade_fill_id> result;
	for(auto const& candidate : bids) {
		if(candidate.exact) {
			auto exact_bid = exact_person_goods::bid(state, candidate.exact_bid);
			if(!exact_bid) continue;
			for(auto ask : asks) {
				if(state.world.concrete_market_ask_get_remaining_quantity(ask) <= epsilon) continue;
				if(state.world.concrete_market_ask_get_minimum_price(ask) > exact_bid->limit_price) break;
				(void)exact_person_goods::try_fill(state, candidate.exact_bid, ask, date);
				if(!exact_person_goods::bid(state, candidate.exact_bid)
					|| exact_person_goods::bid(state, candidate.exact_bid)->status != exact_person_goods::order_status::active) break;
			}
			continue;
		}
		auto bid = candidate.dcon_bid;
		for(auto ask : asks) {
			if(state.world.concrete_market_bid_get_remaining_quantity(bid) <= epsilon || state.world.concrete_market_ask_get_remaining_quantity(ask) <= epsilon) continue;
			auto buyer = state.world.concrete_market_bid_get_economic_actor_from_concrete_bid_buyer(bid);
			auto seller = state.world.concrete_market_ask_get_economic_actor_from_concrete_ask_seller(ask);
			if(!buyer || !seller || buyer == seller) continue;
			auto price = state.world.concrete_market_ask_get_minimum_price(ask);
			if(price > state.world.concrete_market_bid_get_limit_price(bid)) break;
			auto destination = state.world.concrete_market_bid_get_site_from_concrete_bid_destination(bid);
			auto source = state.world.concrete_market_ask_get_site_from_concrete_ask_site(ask);
			auto account = state.world.concrete_market_bid_get_monetary_account_from_concrete_bid_account(bid);
			auto quantity = std::min(state.world.concrete_market_bid_get_remaining_quantity(bid), state.world.concrete_market_ask_get_remaining_quantity(ask));
			quantity = std::min(quantity, inventory::quantity(state, source, commodity, seller));
			quantity = std::min(quantity, std::max(0.0f, (accounts::balance(state, account) - reserved_funds(state, account) + state.world.concrete_market_bid_get_reserved_amount(bid)) / price));
			if(!valid(quantity)) continue;
			auto transaction = exchange::purchase_with_account(state, source, commodity, seller, buyer, account, quantity, price, date);
			if(!transaction) continue;
			state.world.concrete_market_bid_set_remaining_quantity(bid, std::max(0.0f, state.world.concrete_market_bid_get_remaining_quantity(bid) - quantity));
			state.world.concrete_market_bid_set_reserved_amount(bid, state.world.concrete_market_bid_get_remaining_quantity(bid) * state.world.concrete_market_bid_get_limit_price(bid));
			state.world.concrete_market_ask_set_remaining_quantity(ask, std::max(0.0f, state.world.concrete_market_ask_get_remaining_quantity(ask) - quantity));
			state.world.concrete_market_ask_set_reserved_quantity(ask, state.world.concrete_market_ask_get_remaining_quantity(ask));
			if(state.world.concrete_market_bid_get_remaining_quantity(bid) <= epsilon) state.world.concrete_market_bid_set_status(bid, uint8_t(order_status::filled));
			if(state.world.concrete_market_ask_get_remaining_quantity(ask) <= epsilon) state.world.concrete_market_ask_set_status(ask, uint8_t(order_status::filled));
			// The current bid's consumed reservation is updated before freight
			// affordability is evaluated, so only genuinely free payer cash is
			// available for the separate freight payment.
			auto request = freight_market::create_request(state, buyer, account, source, destination, commodity, quantity);
			auto contract = request ? freight_market::match_request(state, request) : dcon::freight_contract_id{};
			auto shipment = contract ? state.world.freight_contract_get_shipment_from_freight_contract_shipment(contract) : dcon::shipment_id{};
			auto fill = state.world.create_concrete_trade_fill();
			state.world.concrete_trade_fill_set_quantity(fill, quantity);
			state.world.concrete_trade_fill_set_execution_price(fill, price);
			state.world.concrete_trade_fill_set_occurred_on(fill, date);
			state.world.force_create_concrete_fill_bid(fill, bid);
			state.world.force_create_concrete_fill_ask(fill, ask);
			state.world.force_create_concrete_fill_transaction(fill, transaction);
			if(request) state.world.force_create_concrete_fill_freight_request(fill, request);
			if(shipment) state.world.force_create_concrete_fill_shipment(fill, shipment);
			result.push_back(fill);
		}
	}
	return result;
}

float observed_price(sys::state const& state, dcon::market_id market, dcon::commodity_id commodity, sys::date date, float fallback) {
	float quantity = 0.0f, value = 0.0f;
	state.world.for_each_concrete_trade_fill([&](auto fill) {
		if(state.world.concrete_trade_fill_get_occurred_on(fill) != date) return;
		auto bid = state.world.concrete_trade_fill_get_concrete_market_bid_from_concrete_fill_bid(fill);
		if(!bid || state.world.concrete_market_bid_get_market_from_concrete_bid_market(bid) != market || state.world.concrete_market_bid_get_commodity_from_concrete_bid_commodity(bid) != commodity) return;
		quantity += state.world.concrete_trade_fill_get_quantity(fill);
		value += state.world.concrete_trade_fill_get_quantity(fill) * state.world.concrete_trade_fill_get_execution_price(fill);
	});
	if(state.exact_person_goods) {
		auto exact = exact_person_goods::observation_for_date(state, market, commodity, date);
		quantity += exact.quantity;
		value += exact.value;
	}
	return quantity > epsilon ? value / quantity : fallback;
}

float concrete_reference_price(sys::state const& state, dcon::market_id market,
	dcon::commodity_id commodity, sys::date date, float fallback) {
	float quantity = 0.0f, value = 0.0f;
	std::optional<sys::date> latest;
	state.world.for_each_concrete_trade_fill([&](auto fill) {
		auto occurred = state.world.concrete_trade_fill_get_occurred_on(fill);
		auto bid = state.world.concrete_trade_fill_get_concrete_market_bid_from_concrete_fill_bid(fill);
		if(!bid || occurred >= date || state.world.concrete_market_bid_get_market_from_concrete_bid_market(bid) != market
			|| state.world.concrete_market_bid_get_commodity_from_concrete_bid_commodity(bid) != commodity) return;
		if(!latest || occurred > *latest) { latest = occurred; quantity = 0.0f; value = 0.0f; }
		if(occurred == *latest) {
			quantity += state.world.concrete_trade_fill_get_quantity(fill);
			value += state.world.concrete_trade_fill_get_quantity(fill) * state.world.concrete_trade_fill_get_execution_price(fill);
		}
	});
	if(state.exact_person_goods) {
		auto exact_latest = exact_person_goods::latest_fill_date(state, market, commodity, date);
		if(exact_latest && (!latest || *exact_latest > *latest)) {
			latest = exact_latest;
			quantity = 0.0f;
			value = 0.0f;
		}
		if(exact_latest && latest && *exact_latest == *latest) {
			auto exact = exact_person_goods::observation_for_date(state, market, commodity, *latest);
			quantity += exact.quantity;
			value += exact.value;
		}
	}
	if(quantity > epsilon) return value / quantity;
	return fallback;
}

float canonical_reference_price(sys::state const& state, dcon::market_id market,
	dcon::commodity_id commodity, sys::date date, float fallback) {
	if(auto history = concrete_reference_price(state, market, commodity, date, -1.0f);
		valid(history)) return history;
	// The canonical fallback is immutable scenario/bootstrap cost.  In
	// particular, it must not observe mutable legacy market equilibrium state.
	if(valid(fallback)) return fallback;
	auto bootstrap_cost = commodity ? state.world.commodity_get_cost(commodity) : 0.0f;
	return valid(bootstrap_cost) ? bootstrap_cost : 0.0f;
}

float legacy_compatibility_reference_price(sys::state const& state, dcon::market_id market,
	dcon::commodity_id commodity, sys::date date, float fallback) {
	if(auto history = concrete_reference_price(state, market, commodity, date, -1.0f);
		valid(history)) return history;
	auto reference = state.world.market_get_price(market, commodity);
	if(valid(reference)) return reference;
	return valid(fallback) ? fallback : 0.0f;
}

void expire(sys::state& state, sys::date date) {
	exact_person_goods::expire(state, date);
	state.world.for_each_concrete_market_bid([&](auto bid) { if(state.world.concrete_market_bid_get_status(bid) == active && state.world.concrete_market_bid_get_created_on(bid) < date) { state.world.concrete_market_bid_set_status(bid, uint8_t(order_status::canceled)); state.world.concrete_market_bid_set_reserved_amount(bid, 0.0f); } });
	state.world.for_each_concrete_market_ask([&](auto ask) { if(state.world.concrete_market_ask_get_status(ask) == active && state.world.concrete_market_ask_get_created_on(ask) < date) { state.world.concrete_market_ask_set_status(ask, uint8_t(order_status::canceled)); state.world.concrete_market_ask_set_reserved_quantity(ask, 0.0f); } });
}
} // namespace economy::physical::concrete_market
