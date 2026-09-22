#include "market_clearing.hpp"

#include "gamerule.hpp"
#include "system_state.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace economy::market_clearing {
namespace {

float finite_nonnegative(float value) noexcept {
	return std::isfinite(value) && value > 0.0f ? value : 0.0f;
}

float finite_price(float value, float fallback = 0.0f) noexcept {
	return std::isfinite(value) && value >= 0.0f
		? value : finite_nonnegative(fallback);
}

size_t flat_index(uint32_t commodity_count, dcon::market_id market,
		dcon::commodity_id commodity) noexcept {
	return size_t(market.index()) * size_t(commodity_count)
		+ size_t(commodity.index());
}

float reservation_multiplier(demand_class category) noexcept {
	switch(category) {
	case demand_class::life_needs: return 2.00f;
	case demand_class::government: return 1.80f;
	case demand_class::intermediate: return 1.65f;
	case demand_class::everyday_needs: return 1.50f;
	case demand_class::construction: return 1.30f;
	case demand_class::inventory: return 1.15f;
	case demand_class::trade: return 1.05f;
	case demand_class::other: return 1.00f;
	case demand_class::luxury_needs: return 0.85f;
	case demand_class::count: break;
	}
	return 1.0f;
}

} // namespace

auction_result clear_call_auction(
		std::vector<bid_order> const& raw_bids,
		std::vector<ask_order> const& raw_asks,
		float fallback_price) {
	auto bids = raw_bids;
	auto asks = raw_asks;
	for(auto& bid : bids) {
		bid.quantity = finite_nonnegative(bid.quantity);
		bid.limit_price = finite_price(bid.limit_price);
	}
	for(auto& ask : asks) {
		ask.quantity = finite_nonnegative(ask.quantity);
		ask.limit_price = finite_price(ask.limit_price);
	}

	std::stable_sort(bids.begin(), bids.end(), [](auto const& left, auto const& right) {
		if(left.limit_price != right.limit_price)
			return left.limit_price > right.limit_price;
		return left.stable_order < right.stable_order;
	});
	std::stable_sort(asks.begin(), asks.end(), [](auto const& left, auto const& right) {
		if(left.limit_price != right.limit_price)
			return left.limit_price < right.limit_price;
		return left.stable_order < right.stable_order;
	});

	auction_result result{};
	result.clearing_price = finite_price(fallback_price);
	for(auto const& bid : bids)
		result.quantity_requested += bid.quantity;
	for(auto const& ask : asks)
		result.quantity_offered += ask.quantity;

	size_t bid_index = 0;
	size_t ask_index = 0;
	float bid_remaining = bids.empty() ? 0.0f : bids.front().quantity;
	float ask_remaining = asks.empty() ? 0.0f : asks.front().quantity;
	float marginal_bid = result.clearing_price;
	float marginal_ask = result.clearing_price;
	while(bid_index < bids.size() && ask_index < asks.size()) {
		auto const& bid = bids[bid_index];
		auto const& ask = asks[ask_index];
		if(bid.limit_price < ask.limit_price)
			break;
		if(bid_remaining <= 0.0f) {
			++bid_index;
			bid_remaining = bid_index < bids.size() ? bids[bid_index].quantity : 0.0f;
			continue;
		}
		if(ask_remaining <= 0.0f) {
			++ask_index;
			ask_remaining = ask_index < asks.size() ? asks[ask_index].quantity : 0.0f;
			continue;
		}

		auto const quantity = std::min(bid_remaining, ask_remaining);
		if(quantity <= 0.0f)
			break;
		auto const category = static_cast<size_t>(bid.category);
		if(category < result.bought.size())
			result.bought[category] += quantity;
		result.quantity_traded += quantity;
		bid_remaining -= quantity;
		ask_remaining -= quantity;
		marginal_bid = bid.limit_price;
		marginal_ask = ask.limit_price;
	}

	if(result.quantity_traded > 0.0f) {
		result.clearing_price = finite_price(
			0.5f * (marginal_bid + marginal_ask), result.clearing_price);
	}
	for(auto const& bid : raw_bids) {
		auto const category = static_cast<size_t>(bid.category);
		if(category < result.fill.size())
			result.fill[category] += finite_nonnegative(bid.quantity);
	}
	for(size_t category = 0; category < result.fill.size(); ++category) {
		result.fill[category] = result.fill[category] > 0.0f
			? std::clamp(result.bought[category] / result.fill[category], 0.0f, 1.0f)
			: 1.0f;
	}
	return result;
}

void account::reset(uint32_t markets, uint32_t commodities, bool active) {
	enabled = active;
	market_count = markets;
	commodity_count = commodities;
	auto const size = size_t(markets) * size_t(commodities);
	for(auto& values : demand)
		values.assign(size, 0.0f);
	for(auto& values : fill)
		values.assign(size, 1.0f);
	clearing_price.assign(size, 0.0f);
	quantity_traded.assign(size, 0.0f);
}

void account::record(dcon::market_id market, dcon::commodity_id commodity,
		demand_class category, float amount) noexcept {
	if(!enabled || !market || !commodity)
		return;
	auto const category_index = static_cast<size_t>(category);
	auto const index = flat_index(commodity_count, market, commodity);
	if(category_index >= demand.size() || index >= demand[category_index].size())
		return;
	demand[category_index][index] += finite_nonnegative(amount);
}

float account::recorded(dcon::market_id market, dcon::commodity_id commodity,
		demand_class category) const noexcept {
	if(!enabled || !market || !commodity)
		return 0.0f;
	auto const category_index = static_cast<size_t>(category);
	auto const index = flat_index(commodity_count, market, commodity);
	return category_index < demand.size() && index < demand[category_index].size()
		? finite_nonnegative(demand[category_index][index]) : 0.0f;
}

float account::filled(dcon::market_id market, dcon::commodity_id commodity,
		demand_class category, float fallback) const noexcept {
	if(!enabled || !market || !commodity)
		return std::clamp(finite_price(fallback), 0.0f, 1.0f);
	auto const category_index = static_cast<size_t>(category);
	auto const index = flat_index(commodity_count, market, commodity);
	return category_index < fill.size() && index < fill[category_index].size()
		? std::clamp(finite_price(fill[category_index][index]), 0.0f, 1.0f)
		: std::clamp(finite_price(fallback), 0.0f, 1.0f);
}

void begin_day(sys::state& state) {
	state.market_clearing_account.reset(
		state.world.market_size(), state.world.commodity_size(),
		gamerule::age_of_transformation_enabled(state));
}

void record(sys::state& state, dcon::market_id market,
		dcon::commodity_id commodity, demand_class category, float amount) noexcept {
	state.market_clearing_account.record(market, commodity, category, amount);
}

market_result settle(sys::state& state, dcon::market_id market,
		dcon::commodity_id commodity, float raw_supply, float raw_demand,
		float raw_reference_price) {
	market_result result{};
	auto const supply = finite_nonnegative(raw_supply);
	auto const aggregate_demand = finite_nonnegative(raw_demand);
	auto const reference_price = std::max(0.0001f,
		finite_price(raw_reference_price, 1.0f));
	result.quantity_traded = std::min(supply, aggregate_demand);
	result.aggregate_buy_fill = aggregate_demand > 0.0f
		? result.quantity_traded / aggregate_demand : 0.0f;
	result.aggregate_sell_fill = supply > 0.0f
		? result.quantity_traded / supply : 0.0f;
	result.clearing_price = reference_price;
	result.class_fill.fill(result.aggregate_buy_fill);

	auto& account = state.market_clearing_account;
	if(!account.enabled || !market || !commodity)
		return result;

	std::array<float, demand_class_count> classified{};
	float classified_total = 0.0f;
	for(size_t category = 0; category < demand_class_count; ++category) {
		classified[category] = account.recorded(market, commodity,
			static_cast<demand_class>(category));
		classified_total += classified[category];
	}
	// The world aggregate is authoritative. Unclassified or rounding residuals
	// become an ordinary bid; over-recording is normalized conservatively.
	if(classified_total < aggregate_demand) {
		classified[static_cast<size_t>(demand_class::other)] +=
			aggregate_demand - classified_total;
	} else if(classified_total > aggregate_demand && classified_total > 0.0f) {
		auto const scale = aggregate_demand / classified_total;
		for(auto& quantity : classified)
			quantity *= scale;
	}

	std::vector<bid_order> bids;
	bids.reserve(demand_class_count);
	for(size_t category = 0; category < demand_class_count; ++category) {
		bids.push_back({
			classified[category],
			reference_price * reservation_multiplier(
				static_cast<demand_class>(category)),
			static_cast<demand_class>(category),
			uint32_t(category)});
	}
	std::vector<ask_order> asks{{supply, reference_price * 0.75f, 0u}};
	auto const auction = clear_call_auction(bids, asks, reference_price);
	result.quantity_traded = auction.quantity_traded;
	result.aggregate_buy_fill = aggregate_demand > 0.0f
		? std::clamp(auction.quantity_traded / aggregate_demand, 0.0f, 1.0f) : 0.0f;
	result.aggregate_sell_fill = supply > 0.0f
		? std::clamp(auction.quantity_traded / supply, 0.0f, 1.0f) : 0.0f;
	result.clearing_price = auction.clearing_price;
	result.class_fill = auction.fill;

	auto const index = flat_index(account.commodity_count, market, commodity);
	if(index < account.clearing_price.size()) {
		account.clearing_price[index] = result.clearing_price;
		account.quantity_traded[index] = result.quantity_traded;
		for(size_t category = 0; category < demand_class_count; ++category)
			account.fill[category][index] = result.class_fill[category];
	}
	return result;
}

float fill(sys::state const& state, dcon::market_id market,
		dcon::commodity_id commodity, demand_class category) noexcept {
	auto const fallback = market && commodity
		? state.world.market_get_actual_probability_to_buy(market, commodity) : 0.0f;
	return state.market_clearing_account.filled(
		market, commodity, category, fallback);
}

float clearing_price(sys::state const& state, dcon::market_id market,
		dcon::commodity_id commodity, float fallback) noexcept {
	auto const& account = state.market_clearing_account;
	if(!account.enabled || !market || !commodity)
		return fallback;
	auto const index = flat_index(account.commodity_count, market, commodity);
	return index < account.clearing_price.size()
		&& account.clearing_price[index] > 0.0f
		? account.clearing_price[index] : fallback;
}

} // namespace economy::market_clearing
