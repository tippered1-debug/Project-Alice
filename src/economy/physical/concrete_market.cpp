#include "concrete_market.hpp"
#include "economy/capital_projects.hpp"
#include "economy/banking/banking.hpp"

#include "accounts/accounts.hpp"
#include "economy/causal_order.hpp"
#include "exchange.hpp"
#include "inventory.hpp"
#include "shipments.hpp"
#include "freight_market.hpp"
#include "exact_person_goods.hpp"
#include "commodity_logistics.hpp"
#include "world/spatial_runtime.hpp"
#include "system_state.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <unordered_map>
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
		if(state.world.concrete_market_ask_get_status(ask) != active || state.world.concrete_market_ask_get_concrete_ask_seller(ask) && state.world.concrete_market_ask_get_economic_actor_from_concrete_ask_seller(ask) != seller || state.world.concrete_market_ask_get_concrete_ask_site(ask) && state.world.concrete_market_ask_get_site_from_concrete_ask_site(ask) != source || state.world.concrete_market_ask_get_concrete_ask_commodity(ask) && state.world.concrete_market_ask_get_commodity_from_concrete_ask_commodity(ask) != commodity) return;
		result += std::max(0.0f, state.world.concrete_market_ask_get_reserved_quantity(ask));
	});
	// Paid cargo waiting for a carrier is still physically at its origin, but
	// it has already been committed to delivery and cannot be offered again.
	state.world.for_each_freight_request([&](auto request) {
		if(state.world.freight_request_get_status(request) == 0
			&& state.world.freight_request_get_economic_actor_from_freight_request_requester(request) == seller
			&& state.world.freight_request_get_site_from_freight_request_source(request) == source
			&& state.world.freight_request_get_commodity_from_freight_request_commodity(request) == commodity)
			result += std::max(0.0f, state.world.freight_request_get_quantity(request));
	});
	return result;
}

// A bid reserves money from the instrument that funds it: its buyer's wallet,
// or a bank deposit when one is attached.
float reserved_funds(sys::state const& state, dcon::monetary_account_id account) {
	float result = 0.0f;
	state.world.for_each_concrete_market_bid([&](auto bid) {
		if(state.world.concrete_market_bid_get_status(bid) == active && state.world.concrete_market_bid_get_concrete_bid_account(bid)
			&& !state.world.concrete_market_bid_get_deposit_account_from_concrete_bid_deposit(bid)
			&& state.world.concrete_market_bid_get_monetary_account_from_concrete_bid_account(bid) == account)
			result += std::max(0.0f, state.world.concrete_market_bid_get_reserved_amount(bid));
	});
	return result;
}

float reserved_deposit_funds(sys::state const& state, dcon::deposit_account_id deposit) {
	float result = 0.0f;
	state.world.for_each_concrete_market_bid([&](auto bid) {
		if(state.world.concrete_market_bid_get_status(bid) == active
			&& state.world.concrete_market_bid_get_deposit_account_from_concrete_bid_deposit(bid) == deposit)
			result += std::max(0.0f, state.world.concrete_market_bid_get_reserved_amount(bid));
	});
	return result;
}

float route_distance_for_match(sys::state& state, dcon::site_id origin,
	dcon::site_id destination, float& travel_days) {
	travel_days = 1.0f;
	if(!origin || !destination || origin == destination) return 0.0f;
	if(auto spatial = world::spatial_runtime::route_for_sites(state, origin, destination);
		spatial.connected) {
		travel_days = std::max(1.0f, spatial.travel_days);
		return std::max(0.0f, spatial.generalized_cost);
	}
	return std::numeric_limits<float>::infinity();
}

struct ask_candidate {
	dcon::concrete_market_ask_id ask{};
	float landed_price = 0.0f;
	float transport_price = 0.0f;
};
}

dcon::market_id market_for_site(sys::state const& state, dcon::site_id site) {
	if(!site || !state.world.site_is_valid(site)) return {};
	auto province = state.world.site_get_province_from_site_location(site);
	if(!province || !state.world.province_is_valid(province)) return {};
	auto zone = state.world.province_get_state_membership(province);
	return zone ? state.world.state_instance_get_market_from_local_market(zone) : dcon::market_id{};
}

float landed_unit_cost(sys::state& state, dcon::site_id origin, dcon::site_id destination,
	dcon::commodity_id commodity, float goods_price) {
	if(!std::isfinite(goods_price) || goods_price < 0.0f) return std::numeric_limits<float>::infinity();
	if(!origin || !destination || origin == destination) return goods_price;
	float travel_days = 1.0f;
	auto route_cost = route_distance_for_match(state, origin, destination, travel_days);
	if(!std::isfinite(route_cost)) return std::numeric_limits<float>::infinity();
	if(route_cost <= 0.0f && origin != destination) return goods_price;
	auto profile = logistics::profile_for(state, commodity);
	auto daily_spoilage = std::clamp(std::isfinite(profile.daily_spoilage)
		? profile.daily_spoilage : 0.0f, 0.0f, 1.0f);
	auto expected_spoilage = 1.0f - std::pow(1.0f - daily_spoilage, travel_days);
	auto cargo_weight = std::max(1.0f, std::isfinite(profile.cargo_weight)
		? profile.cargo_weight : 1.0f);
	// This is a deterministic landed-cost estimate used only to rank competing
	// asks. The actual freight charge is settled by freight_market after the
	// goods transaction and the physical shipment remains separately observable.
	auto transport = route_cost * 0.01f * cargo_weight;
	return goods_price + transport + goods_price * expected_spoilage;
}

float reserved_deposit_amount(sys::state const& state, dcon::deposit_account_id deposit) {
	return reserved_deposit_funds(state, deposit);
}

float reserved_bid_amount(sys::state const& state, dcon::monetary_account_id account) {
	return reserved_funds(state, account);
}

float active_factory_bid_quantity(sys::state const& state, dcon::factory_id factory,
	dcon::site_id destination, dcon::commodity_id commodity) {
	float result = 0.0f;
	state.world.for_each_concrete_market_bid([&](auto bid) {
		if(state.world.concrete_market_bid_get_status(bid) != active || state.world.concrete_market_bid_get_factory_from_concrete_bid_factory(bid) != factory || state.world.concrete_market_bid_get_site_from_concrete_bid_destination(bid) != destination || state.world.concrete_market_bid_get_commodity_from_concrete_bid_commodity(bid) != commodity) return;
		result += std::max(0.0f, state.world.concrete_market_bid_get_remaining_quantity(bid));
	});
	return result;
}

dcon::concrete_market_bid_id post_bid(sys::state& state, dcon::economic_actor_id buyer,
	dcon::monetary_account_id account, dcon::site_id destination, dcon::market_id market,
	dcon::commodity_id commodity, float quantity, float limit_price, order_purpose purpose) {
	if(!buyer || !account || accounts::owner_of(state, account) != buyer || !destination || !market || !commodity || !state.world.commodity_is_valid(accounts::settlement_of(state, account)) || !valid(quantity) || !valid(limit_price)) return {};
	// A buyer whose wallet cannot cover the order pays from their bank deposit
	// instead, as long as their bank is not insolvent.
	dcon::deposit_account_id funding{};
	if(accounts::balance(state, account) + epsilon < reserved_funds(state, account) + quantity * limit_price) {
		funding = economy::banking::deposit_account_for(state, buyer, accounts::settlement_of(state, account));
		if(!funding || economy::banking::status_of(state, state.world.deposit_account_get_organization_from_deposit_account_bank(funding))
				== economy::banking::bank_status::insolvent
			|| economy::banking::deposit_balance(state, funding) + epsilon < reserved_deposit_funds(state, funding) + quantity * limit_price)
			return {};
	}
	auto bid = state.world.create_concrete_market_bid();
	state.world.concrete_market_bid_set_original_quantity(bid, quantity);
	state.world.concrete_market_bid_set_remaining_quantity(bid, quantity);
	state.world.concrete_market_bid_set_limit_price(bid, limit_price);
	state.world.concrete_market_bid_set_reserved_amount(bid, quantity * limit_price);
	state.world.concrete_market_bid_set_created_on(bid, state.current_date);
	state.world.concrete_market_bid_set_status(bid, active);
	state.world.concrete_market_bid_set_purpose(bid, uint8_t(purpose));
	if(!economy::causal_order::sequence_for_dcon(state, economy::causal_order::event_kind::goods_bid,
		uint64_t(bid.index()))) return {};
	state.world.force_create_concrete_bid_buyer(bid, buyer);
	state.world.force_create_concrete_bid_account(bid, account);
	if(funding) state.world.force_create_concrete_bid_deposit(bid, funding);
	state.world.force_create_concrete_bid_destination(bid, destination);
	state.world.force_create_concrete_bid_market(bid, market);
	state.world.force_create_concrete_bid_commodity(bid, commodity);
	return bid;
}

dcon::concrete_market_ask_id post_ask(sys::state& state, dcon::economic_actor_id seller,
	dcon::site_id source, dcon::market_id market, dcon::commodity_id commodity,
	float quantity, float minimum_price, order_purpose purpose, dcon::factory_id factory) {
	if(!seller || !source || !market || !commodity || !valid(quantity) || !valid(minimum_price)
		|| capital_projects::is_construction_site(state, source)) return {};
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
	if(factory && state.world.factory_is_valid(factory))
		state.world.force_create_concrete_ask_factory(ask, factory);
	return ask;
}

std::vector<dcon::concrete_trade_fill_id> match_impl(sys::state& state,
	std::optional<dcon::market_id> market_scope, dcon::commodity_id commodity, sys::date date) {
	struct candidate_bid {
		bool exact = false;
		dcon::concrete_market_bid_id dcon_bid{};
		uint64_t exact_bid = 0;
		sys::date created_on{};
		uint64_t causal_sequence = 0;
		uint64_t stable_id = 0;
	};
	std::vector<candidate_bid> bids;
	std::vector<dcon::concrete_market_ask_id> asks;
	state.world.for_each_concrete_market_bid([&](auto bid) {
		if(state.world.concrete_market_bid_get_status(bid) == active
			&& (!market_scope || state.world.concrete_market_bid_get_market_from_concrete_bid_market(bid) == *market_scope)
			&& state.world.concrete_market_bid_get_commodity_from_concrete_bid_commodity(bid) == commodity)
			bids.push_back({false, bid, 0, state.world.concrete_market_bid_get_created_on(bid),
				economy::causal_order::sequence_for_dcon(state, economy::causal_order::event_kind::goods_bid,
					uint64_t(bid.index())), uint64_t(bid.index())});
	});
	state.world.for_each_market([&](auto market) {
		if(market_scope && market != *market_scope) return;
		for(auto bid : exact_person_goods::active_bids(state, market, commodity))
			bids.push_back({true, {}, bid.id, bid.created_on, bid.causal_sequence, bid.id});
	});
	state.world.for_each_concrete_market_ask([&](auto ask) {
		if(state.world.concrete_market_ask_get_status(ask) == active
			&& (!market_scope || state.world.concrete_market_ask_get_market_from_concrete_ask_market(ask) == *market_scope)
			&& state.world.concrete_market_ask_get_commodity_from_concrete_ask_commodity(ask) == commodity)
			asks.push_back(ask);
	});
	std::sort(bids.begin(), bids.end(), [&](auto const& a, auto const& b) {
		if(economy::causal_order::before({a.created_on, a.causal_sequence},
			{b.created_on, b.causal_sequence})) return true;
		if(economy::causal_order::before({b.created_on, b.causal_sequence},
			{a.created_on, a.causal_sequence})) return false;
		return a.stable_id < b.stable_id;
	});
	std::sort(asks.begin(), asks.end(), [&](auto a, auto b) { auto ap = state.world.concrete_market_ask_get_minimum_price(a), bp = state.world.concrete_market_ask_get_minimum_price(b); return ap == bp ? a.index() < b.index() : ap < bp; });
	std::vector<dcon::concrete_trade_fill_id> result;
	for(auto const& candidate : bids) {
		if(candidate.exact) {
			auto exact_bid = exact_person_goods::bid(state, candidate.exact_bid);
			if(!exact_bid) continue;
			std::vector<ask_candidate> exact_asks;
			for(auto ask : asks) {
				auto source = state.world.concrete_market_ask_get_site_from_concrete_ask_site(ask);
				auto landed = landed_unit_cost(state, source, exact_bid->destination, commodity,
					state.world.concrete_market_ask_get_minimum_price(ask));
				exact_asks.push_back({ask, landed,
					std::max(0.0f, landed - state.world.concrete_market_ask_get_minimum_price(ask))});
			}
			std::sort(exact_asks.begin(), exact_asks.end(), [&](auto const& left, auto const& right) {
				if(left.landed_price != right.landed_price) return left.landed_price < right.landed_price;
				return left.ask.index() < right.ask.index();
			});
			for(auto const& candidate_ask : exact_asks) {
				auto ask = candidate_ask.ask;
				if(state.world.concrete_market_ask_get_remaining_quantity(ask) <= epsilon) continue;
				if(candidate_ask.landed_price > exact_bid->limit_price) break;
				(void)exact_person_goods::try_fill(state, candidate.exact_bid, ask, date);
				if(!exact_person_goods::bid(state, candidate.exact_bid) || exact_person_goods::bid(state, candidate.exact_bid)->status != exact_person_goods::order_status::active) break;
			}
			continue;
		}
		auto bid = candidate.dcon_bid;
		std::vector<ask_candidate> ranked_asks;
		auto buyer = state.world.concrete_market_bid_get_economic_actor_from_concrete_bid_buyer(bid);
		auto destination = state.world.concrete_market_bid_get_site_from_concrete_bid_destination(bid);
		auto bid_limit = state.world.concrete_market_bid_get_limit_price(bid);
		for(auto ask : asks) {
			auto seller = state.world.concrete_market_ask_get_economic_actor_from_concrete_ask_seller(ask);
			auto source = state.world.concrete_market_ask_get_site_from_concrete_ask_site(ask);
			if(!seller || seller == buyer || !source || !destination) continue;
			auto goods_price = state.world.concrete_market_ask_get_minimum_price(ask);
			auto landed = landed_unit_cost(state, source, destination, commodity, goods_price);
			if(!std::isfinite(landed)) continue;
			ranked_asks.push_back({ask, landed, std::max(0.0f, landed - goods_price)});
		}
		std::sort(ranked_asks.begin(), ranked_asks.end(), [&](auto const& left, auto const& right) {
			if(left.landed_price != right.landed_price) return left.landed_price < right.landed_price;
			if(left.ask.index() != right.ask.index()) return left.ask.index() < right.ask.index();
			return left.transport_price < right.transport_price;
		});
		for(auto const& ranked : ranked_asks) {
			auto ask = ranked.ask;
			if(state.world.concrete_market_bid_get_remaining_quantity(bid) <= epsilon || state.world.concrete_market_ask_get_remaining_quantity(ask) <= epsilon) continue;
			auto seller = state.world.concrete_market_ask_get_economic_actor_from_concrete_ask_seller(ask);
			if(!buyer || !seller || buyer == seller) continue;
			auto price = state.world.concrete_market_ask_get_minimum_price(ask);
			if(ranked.landed_price > bid_limit) break;
			auto source = state.world.concrete_market_ask_get_site_from_concrete_ask_site(ask);
			auto account = state.world.concrete_market_bid_get_monetary_account_from_concrete_bid_account(bid);
			auto funding = state.world.concrete_market_bid_get_deposit_account_from_concrete_bid_deposit(bid);
			auto quantity = std::min(state.world.concrete_market_bid_get_remaining_quantity(bid), state.world.concrete_market_ask_get_remaining_quantity(ask));
			quantity = std::min(quantity, inventory::quantity(state, source, commodity, seller));
			auto free_funds = funding
				? economy::banking::deposit_balance(state, funding) - reserved_deposit_funds(state, funding)
				: accounts::balance(state, account) - reserved_funds(state, account);
			quantity = std::min(quantity, std::max(0.0f, (free_funds + state.world.concrete_market_bid_get_reserved_amount(bid))
				/ std::max(ranked.landed_price, price)));
			if(!valid(quantity)) continue;
			auto transaction = funding
				? exchange::purchase_with_deposit(state, source, commodity, seller, buyer, funding, quantity, price, date)
				: exchange::purchase_with_account(state, source, commodity, seller, buyer, account, quantity, price, date);
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

std::vector<dcon::concrete_trade_fill_id> match(sys::state& state, dcon::market_id market,
	dcon::commodity_id commodity, sys::date date) {
	return match_impl(state, market, commodity, date);
}

std::vector<dcon::concrete_trade_fill_id> match_all(sys::state& state,
	dcon::commodity_id commodity, sys::date date) {
	return match_impl(state, std::nullopt, commodity, date);
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
	auto exact = exact_person_goods::observation_for_date(state, market, commodity, date);
	quantity += exact.quantity;
	value += exact.value;
	return quantity > epsilon ? value / quantity : fallback;
}

float observed_sell_through(sys::state const& state, dcon::market_id market,
	dcon::commodity_id commodity, sys::date date, float fallback) {
	float offered = 0.0f;
	state.world.for_each_concrete_market_ask([&](auto ask) {
		if(state.world.concrete_market_ask_get_created_on(ask) != date || state.world.concrete_market_ask_get_market_from_concrete_ask_market(ask) != market || state.world.concrete_market_ask_get_commodity_from_concrete_ask_commodity(ask) != commodity)
			return;
		offered += std::max(0.0f, state.world.concrete_market_ask_get_original_quantity(ask));
	});
	float sold = 0.0f;
	state.world.for_each_concrete_trade_fill([&](auto fill) {
		if(state.world.concrete_trade_fill_get_occurred_on(fill) != date) return;
		auto ask = state.world.concrete_trade_fill_get_concrete_market_ask_from_concrete_fill_ask(fill);
		if(!ask || state.world.concrete_market_ask_get_market_from_concrete_ask_market(ask) != market || state.world.concrete_market_ask_get_commodity_from_concrete_ask_commodity(ask) != commodity) return;
		sold += std::max(0.0f, state.world.concrete_trade_fill_get_quantity(fill));
	});
	return offered > epsilon ? std::clamp(sold / offered, 0.0f, 1.0f) : fallback;
}

float concrete_reference_price(sys::state const& state, dcon::market_id market,
	dcon::commodity_id commodity, sys::date date, float fallback) {
	float quantity = 0.0f, value = 0.0f;
	std::optional<sys::date> latest;
	state.world.for_each_concrete_trade_fill([&](auto fill) {
		auto occurred = state.world.concrete_trade_fill_get_occurred_on(fill);
		auto bid = state.world.concrete_trade_fill_get_concrete_market_bid_from_concrete_fill_bid(fill);
		if(!bid || occurred >= date || state.world.concrete_market_bid_get_market_from_concrete_bid_market(bid) != market || state.world.concrete_market_bid_get_commodity_from_concrete_bid_commodity(bid) != commodity) return;
		if(!latest || occurred > *latest) { latest = occurred; quantity = 0.0f; value = 0.0f; }
		if(occurred == *latest) {
			quantity += state.world.concrete_trade_fill_get_quantity(fill);
			value += state.world.concrete_trade_fill_get_quantity(fill) * state.world.concrete_trade_fill_get_execution_price(fill);
		}
	});
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

void expire(sys::state& state, sys::date date) {
	exact_person_goods::expire(state, date);
	state.world.for_each_concrete_market_bid([&](auto bid) { if(state.world.concrete_market_bid_get_status(bid) == active && state.world.concrete_market_bid_get_created_on(bid) < date) { state.world.concrete_market_bid_set_status(bid, uint8_t(order_status::canceled)); state.world.concrete_market_bid_set_reserved_amount(bid, 0.0f); } });
	state.world.for_each_concrete_market_ask([&](auto ask) { if(state.world.concrete_market_ask_get_status(ask) == active && state.world.concrete_market_ask_get_created_on(ask) < date) { state.world.concrete_market_ask_set_status(ask, uint8_t(order_status::canceled)); state.world.concrete_market_ask_set_reserved_quantity(ask, 0.0f); } });
}

void project_to_legacy_markets(sys::state& state) {
	struct projection_record {
		float demand = 0.0f;
		float intermediate_demand = 0.0f;
		float supply = 0.0f;
		float traded = 0.0f;
		float consumed = 0.0f;
		float imports = 0.0f;
		float exports = 0.0f;
		float trade_value = 0.0f;
		float stockpile = 0.0f;
	};
	std::unordered_map<uint64_t, projection_record> values;
	auto key_for = [](dcon::market_id market, dcon::commodity_id commodity) {
		return (uint64_t(market.index()) << 32) | uint64_t(commodity.index());
	};
	auto value_for = [&](dcon::market_id market, dcon::commodity_id commodity) -> projection_record* {
		if(!market || !state.world.market_is_valid(market) || !commodity || !state.world.commodity_is_valid(commodity)) return nullptr;
		return &values[key_for(market, commodity)];
	};

	state.world.for_each_concrete_market_bid([&](auto bid) {
		if(state.world.concrete_market_bid_get_created_on(bid) != state.current_date) return;
		auto market = state.world.concrete_market_bid_get_market_from_concrete_bid_market(bid);
		auto commodity = state.world.concrete_market_bid_get_commodity_from_concrete_bid_commodity(bid);
		if(auto record = value_for(market, commodity)) {
			auto quantity = std::max(0.0f, state.world.concrete_market_bid_get_original_quantity(bid));
			record->demand += quantity;
			if(state.world.concrete_market_bid_get_purpose(bid) == uint8_t(order_purpose::factory_input))
				record->intermediate_demand += quantity;
		}
	});
	state.world.for_each_concrete_market_ask([&](auto ask) {
		if(state.world.concrete_market_ask_get_created_on(ask) != state.current_date) return;
		auto market = state.world.concrete_market_ask_get_market_from_concrete_ask_market(ask);
		auto commodity = state.world.concrete_market_ask_get_commodity_from_concrete_ask_commodity(ask);
		if(auto record = value_for(market, commodity))
			record->supply += std::max(0.0f, state.world.concrete_market_ask_get_original_quantity(ask));
	});
	state.world.for_each_concrete_trade_fill([&](auto fill) {
		if(state.world.concrete_trade_fill_get_occurred_on(fill) != state.current_date) return;
		auto bid = state.world.concrete_trade_fill_get_concrete_market_bid_from_concrete_fill_bid(fill);
		auto ask = state.world.concrete_trade_fill_get_concrete_market_ask_from_concrete_fill_ask(fill);
		if(!bid || !ask) return;
		auto buyer_market = state.world.concrete_market_bid_get_market_from_concrete_bid_market(bid);
		auto origin_market = state.world.concrete_market_ask_get_market_from_concrete_ask_market(ask);
		auto commodity = state.world.concrete_market_bid_get_commodity_from_concrete_bid_commodity(bid);
		auto quantity = std::max(0.0f, state.world.concrete_trade_fill_get_quantity(fill));
		auto price = std::max(0.0f, state.world.concrete_trade_fill_get_execution_price(fill));
		if(auto record = value_for(buyer_market, commodity)) {
			record->traded += quantity;
			record->trade_value += quantity * price;
			if(origin_market && origin_market != buyer_market) record->imports += quantity;
		}
		if(origin_market && origin_market != buyer_market)
			if(auto record = value_for(origin_market, commodity)) record->exports += quantity;
	});
	for(auto const& activity : exact_person_goods::market_activity_for_date(state, state.current_date)) {
		if(auto record = value_for(activity.market, activity.commodity)) {
			record->demand += activity.submitted_demand;
			record->traded += activity.traded_quantity;
			record->imports += activity.imports;
			record->exports += activity.exports;
			record->consumed += activity.consumed_quantity;
			record->trade_value += activity.trade_value;
		}
	}
	state.world.for_each_physical_stock([&](auto stock) {
		auto site = state.world.physical_stock_get_site_from_physical_stock_site(stock);
		auto commodity = state.world.physical_stock_get_commodity_from_physical_stock_commodity(stock);
		auto market = market_for_site(state, site);
		if(auto record = value_for(market, commodity))
			record->stockpile += std::max(0.0f, state.world.physical_stock_get_quantity(stock));
	});
	for(auto const& stock : exact_person_goods::export_snapshot(state).stocks) {
		auto market = market_for_site(state, stock.site);
		if(auto record = value_for(market, stock.commodity))
			record->stockpile += std::max(0.0f, stock.quantity);
	}

	state.world.for_each_market([&](auto market) {
		state.world.for_each_commodity([&](auto commodity) {
			auto record_it = values.find(key_for(market, commodity));
			projection_record empty;
			auto const& record = record_it == values.end() ? empty : record_it->second;
			auto const is_money = state.world.commodity_get_money_rgo(commodity);
			state.world.market_set_supply(market, commodity, record.supply);
			state.world.market_set_demand(market, commodity, record.demand);
			state.world.market_set_intermediate_demand(market, commodity, record.intermediate_demand);
			state.world.market_set_consumption(market, commodity, record.consumed);
			state.world.market_set_import(market, commodity, record.imports);
			state.world.market_set_export(market, commodity, record.exports);
			state.world.market_set_stockpile(market, commodity, is_money ? 0.0f : record.stockpile);
			state.world.market_set_aggregated_supply_history(market, commodity, record.supply);
			state.world.market_set_aggregated_demand_history(market, commodity, record.demand);
			auto buy_probability = record.demand > epsilon
				? std::clamp(record.traded / record.demand, 0.0f, 1.0f) : 0.0f;
			auto sell_probability = record.supply > epsilon
				? std::clamp(record.traded / record.supply, 0.0f, 1.0f) : 0.0f;
			state.world.market_set_actual_probability_to_buy(market, commodity, buy_probability);
			state.world.market_set_expected_probability_to_buy(market, commodity, buy_probability);
			state.world.market_set_actual_probability_to_sell(market, commodity, sell_probability);
			state.world.market_set_expected_probability_to_sell(market, commodity, sell_probability);
			auto price = record.traded > epsilon ? record.trade_value / record.traded
				: canonical_reference_price(state, market, commodity, state.current_date);
			if(valid(price)) state.world.market_set_price(market, commodity, price);
		});
	});
}
} // namespace economy::physical::concrete_market
