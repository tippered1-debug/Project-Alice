#pragma once

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

#include "system_state.hpp"
#include "economy/physical/concrete_market.hpp"
#include "economy/physical/exact_person_goods.hpp"

// A country's foreign trade as the KATECHON trade mode shows it. Read-only:
// nothing here changes the world, and every figure comes from what the
// running simulation already records.
//
// The trade is the deals themselves, over a span of days: the fills of the
// households' markets (exact_person_goods) and of the firms' (concrete trade
// fills). A deal crosses a border when the place the goods were taken from
// and the place they go to lie in different countries; it is valued at the
// price it was struck at. Self-sufficiency over the same span: of all a
// country bought, the part that was not covered by goods from abroad net of
// what went abroad. The span before it is summed too, so that a figure can be
// set against the one before it once the game has run that long.
namespace economy::trade_picture {

struct commodity_line {
	dcon::commodity_id commodity;
	float exported = 0.f; // value sent abroad over the span
	float imported = 0.f; // value brought from abroad over the span
	float bought = 0.f; // quantity bought by the country over the span
	float sold = 0.f; // quantity the country's own places sold, at home or abroad
	float imported_quantity = 0.f;
	float exported_quantity = 0.f;
	float net_import = 0.f; // quantity, net of what went abroad; negative for a net exporter
	int32_t suppliers = 0; // countries it was bought from
	dcon::nation_id main_source; // where most of what came from abroad came from
	float main_source_share = 0.f; // of the imports of this good, by value
	dcon::nation_id main_buyer;
	float main_buyer_share = 0.f;

	// Production sold against purchases: 1 when the country covers itself,
	// above 1 when it sells abroad on balance, -1 when nothing was bought.
	float self_sufficiency() const {
		return bought > 0.f ? std::max(0.f, 1.f - net_import / bought) : -1.f;
	}
	// Dependence on imports: the part of demand our own production does not meet, 1 - production /
	// demand, never below 0; -1 when there was no demand. Unlike the gross share of imports it agrees
	// with production and demand set side by side.
	float dependence() const {
		return bought > 0.f ? std::clamp(1.f - sold / bought, 0.f, 1.f) : -1.f;
	}
	// Of all the country bought, the part that came from abroad; -1 when nothing was bought.
	float import_share() const {
		return bought > 0.f ? std::min(1.f, imported_quantity / bought) : -1.f;
	}
};

struct partner_line {
	dcon::nation_id nation;
	float exported = 0.f;
	float imported = 0.f;
	float total() const {
		return exported + imported;
	}
};

// One good traded with one country over the span, one way.
struct flow_part {
	dcon::nation_id nation;
	dcon::commodity_id good;
	float value = 0.f;
	float quantity = 0.f;
	bool outgoing = false;
};

struct picture {
	dcon::nation_id nation;
	sys::date as_of; // the day the figures were taken, as the top bar shows it
	int32_t span_days = 0;
	int32_t recorded_days = 0; // of the span, the days the game has run
	float exported = 0.f;
	float imported = 0.f;
	bool has_previous = false; // the game has run through the whole span before this one
	float previous_exported = 0.f;
	float previous_imported = 0.f;
	int32_t deals = 0; // crossing a border, either way
	std::vector<float> exported_series; // oldest first: a day each over 30 days, a month each over a year
	std::vector<float> imported_series;
	size_t first_recorded_bucket = 0; // the buckets before it lie before the start of the game
	// by bucket and good, flat: [bucket * goods_count + good], value
	uint32_t goods_count = 0;
	std::vector<float> exported_by_good_series;
	std::vector<float> imported_by_good_series;
	// by bucket and partner, flat: [bucket * nations_count + nation], value
	uint32_t nations_count = 0;
	std::vector<float> exported_by_nation_series;
	std::vector<float> imported_by_nation_series;
	std::vector<commodity_line> commodities; // every good but money, by value bought, largest first
	std::vector<partner_line> partners; // by value both ways, largest first
	std::vector<float> share_by_nation; // of the country's foreign trade, indexed by nation
	std::vector<flow_part> parts; // every good with every country, largest first
};

// As concrete_market decides who exports and who imports: the country holding
// the place, else the one owning it.
inline dcon::nation_id nation_for_site(sys::state const& state, dcon::site_id site) {
	if(!site || !state.world.site_is_valid(site))
		return {};
	auto const province = state.world.site_get_province_from_site_location(site);
	if(!province || !state.world.province_is_valid(province))
		return {};
	if(auto const controller = state.world.province_get_nation_from_province_control(province); controller)
		return controller;
	auto const zone = state.world.province_get_state_membership(province);
	return zone ? state.world.state_instance_get_nation_from_state_ownership(zone) : dcon::nation_id{};
}

inline float unit_price(sys::state const& state, dcon::market_id market, dcon::commodity_id c) {
	auto const local = market ? state.world.market_get_price(market, c) : 0.f;
	if(std::isfinite(local) && local > 0.f)
		return local;
	auto const median = state.world.commodity_get_median_price(c);
	return std::isfinite(median) && median > 0.f ? median : 0.f;
}

struct deal {
	dcon::site_id from; // where the goods were taken
	dcon::site_id to; // where they go
	dcon::commodity_id commodity;
	float quantity = 0.f;
	float price = 0.f;
	int32_t day = 0; // the raw date it was struck
};

// Every deal struck on or after the given raw date. The households' fills are
// kept in the order they happen, so they are read from the newest back.
template<typename F>
void for_each_deal(sys::state const& state, int32_t since, F&& visit) {
	auto const& fills = economy::physical::exact_person_goods::fill_records(state);
	for(size_t i = fills.size(); i-- > 0;) {
		auto const& f = fills[i];
		auto const day = int32_t(f.occurred_on.to_raw_value());
		if(day < since - 1)
			break;
		if(day < since || !(f.quantity > 0.f))
			continue;
		visit(deal{ f.source, f.destination, f.commodity, f.quantity, f.execution_price, day });
	}
	state.world.for_each_concrete_trade_fill([&](dcon::concrete_trade_fill_id fill) {
		auto const day = int32_t(state.world.concrete_trade_fill_get_occurred_on(fill).to_raw_value());
		if(day < since)
			return;
		auto const bid = state.world.concrete_trade_fill_get_concrete_market_bid_from_concrete_fill_bid(fill);
		auto const ask = state.world.concrete_trade_fill_get_concrete_market_ask_from_concrete_fill_ask(fill);
		if(!bid || !ask)
			return;
		auto const quantity = state.world.concrete_trade_fill_get_quantity(fill);
		if(!(quantity > 0.f))
			return;
		visit(deal{ state.world.concrete_market_ask_get_site_from_concrete_ask_site(ask), state.world.concrete_market_bid_get_site_from_concrete_bid_destination(bid),
			state.world.concrete_market_bid_get_commodity_from_concrete_bid_commodity(bid), quantity, state.world.concrete_trade_fill_get_execution_price(fill), day });
	});
}

inline picture take(sys::state const& state, dcon::nation_id n, int32_t span_days) {
	picture result;
	result.nation = n;
	result.as_of = state.current_date;
	result.span_days = span_days;
	result.share_by_nation.assign(state.world.nation_size(), 0.f);
	auto const buckets = span_days <= 30 ? size_t(span_days) : size_t(12);
	result.exported_series.assign(buckets, 0.f);
	result.imported_series.assign(buckets, 0.f);
	if(!n)
		return result;

	auto const commodity_count = state.world.commodity_size();
	result.goods_count = commodity_count;
	result.exported_by_good_series.assign(buckets * commodity_count, 0.f);
	result.imported_by_good_series.assign(buckets * commodity_count, 0.f);
	result.nations_count = state.world.nation_size();
	result.exported_by_nation_series.assign(buckets * result.nations_count, 0.f);
	result.imported_by_nation_series.assign(buckets * result.nations_count, 0.f);
	std::vector<commodity_line> lines(commodity_count);
	for(uint32_t i = 0; i < commodity_count; ++i)
		lines[i].commodity = dcon::commodity_id{ dcon::commodity_id::value_base_t(i) };
	std::vector<partner_line> partners(state.world.nation_size());
	std::unordered_map<uint64_t, float> by_good_and_partner_in;
	std::unordered_map<uint64_t, float> by_good_and_partner_out;
	auto const key = [](dcon::commodity_id c, dcon::nation_id other) {
		return (uint64_t(c.index()) << 32) | uint64_t(other.index());
	};
	auto const today = int32_t(state.current_date.to_raw_value());
	auto const run = int32_t(state.current_date.value); // days since the start, today included
	result.recorded_days = std::clamp(run, 0, span_days);
	result.has_previous = run >= 2 * span_days;
	if(result.recorded_days > 0) {
		auto const oldest = result.recorded_days - 1; // days ago
		result.first_recorded_bucket = span_days <= 30 ? size_t(span_days - 1 - oldest) : size_t(std::clamp(int32_t(buckets) - 1 - oldest / 30, 0, int32_t(buckets) - 1));
	}
	auto const bucket_of = [&](int32_t day) {
		auto const ago = std::max(0, today - day);
		auto const b = span_days <= 30 ? int32_t(buckets) - 1 - ago : int32_t(buckets) - 1 - ago / 30;
		return size_t(std::clamp(b, 0, int32_t(buckets) - 1));
	};

	std::unordered_map<uint64_t, float> quantity_in;
	std::unordered_map<uint64_t, float> quantity_out;
	auto const since = today - span_days + 1;
	for_each_deal(state, result.has_previous ? since - span_days : since, [&](deal const& d) {
		if(!d.commodity || d.commodity.index() >= int32_t(commodity_count))
			return;
		auto const from = nation_for_site(state, d.from);
		auto const to = nation_for_site(state, d.to);
		if(!from || !to || (from != n && to != n))
			return;
		auto const earlier = d.day < since;
		if(earlier) {
			if(from == to)
				return;
			auto const price = std::isfinite(d.price) && d.price > 0.f ? d.price : unit_price(state, economy::physical::concrete_market::market_for_site(state, d.from), d.commodity);
			(to == n ? result.previous_imported : result.previous_exported) += d.quantity * price;
			return;
		}
		auto& line = lines[d.commodity.index()];
		if(to == n)
			line.bought += d.quantity;
		if(from == n)
			line.sold += d.quantity;
		if(from == to)
			return;
		auto const price = std::isfinite(d.price) && d.price > 0.f ? d.price : unit_price(state, economy::physical::concrete_market::market_for_site(state, d.from), d.commodity);
		auto const value = d.quantity * price;
		++result.deals;
		if(to == n) {
			result.imported += value;
			line.imported += value;
			line.imported_quantity += d.quantity;
			line.net_import += d.quantity;
			partners[from.index()].imported += value;
			by_good_and_partner_in[key(d.commodity, from)] += value;
			quantity_in[key(d.commodity, from)] += d.quantity;
			result.imported_series[bucket_of(d.day)] += value;
			result.imported_by_good_series[bucket_of(d.day) * commodity_count + d.commodity.index()] += value;
			result.imported_by_nation_series[bucket_of(d.day) * result.nations_count + from.index()] += value;
		} else {
			result.exported += value;
			line.exported += value;
			line.exported_quantity += d.quantity;
			line.net_import -= d.quantity;
			partners[to.index()].exported += value;
			by_good_and_partner_out[key(d.commodity, to)] += value;
			quantity_out[key(d.commodity, to)] += d.quantity;
			result.exported_series[bucket_of(d.day)] += value;
			result.exported_by_good_series[bucket_of(d.day) * commodity_count + d.commodity.index()] += value;
			result.exported_by_nation_series[bucket_of(d.day) * result.nations_count + to.index()] += value;
		}
	});

	for(auto const& [k, value] : by_good_and_partner_in) {
		auto& line = lines[size_t(k >> 32)];
		++line.suppliers;
		if(line.imported > 0.f && value / line.imported > line.main_source_share) {
			line.main_source_share = value / line.imported;
			line.main_source = dcon::nation_id{ dcon::nation_id::value_base_t(k & 0xFFFFFFFFull) };
		}
	}
	for(auto const& [k, value] : by_good_and_partner_out) {
		auto& line = lines[size_t(k >> 32)];
		if(line.exported > 0.f && value / line.exported > line.main_buyer_share) {
			line.main_buyer_share = value / line.exported;
			line.main_buyer = dcon::nation_id{ dcon::nation_id::value_base_t(k & 0xFFFFFFFFull) };
		}
	}

	for(auto const& [k, value] : by_good_and_partner_out)
		result.parts.push_back(flow_part{ dcon::nation_id{ dcon::nation_id::value_base_t(k & 0xFFFFFFFFull) }, dcon::commodity_id{ dcon::commodity_id::value_base_t(k >> 32) }, value, quantity_out[k], true });
	for(auto const& [k, value] : by_good_and_partner_in)
		result.parts.push_back(flow_part{ dcon::nation_id{ dcon::nation_id::value_base_t(k & 0xFFFFFFFFull) }, dcon::commodity_id{ dcon::commodity_id::value_base_t(k >> 32) }, value, quantity_in[k], false });
	std::stable_sort(result.parts.begin(), result.parts.end(), [](flow_part const& a, flow_part const& b) { return a.value > b.value; });

	auto const capital = state.world.nation_get_capital(n);
	auto const home_market = capital ? state.world.state_instance_get_market_from_local_market(state.world.province_get_state_membership(capital)) : dcon::market_id{};
	// every good of the simulation, money aside: a good with no trade abroad, or none at all over the
	// span, still belongs to the economy and stays in the picture
	for(auto& line : lines) {
		if(state.world.commodity_get_money_rgo(line.commodity))
			continue;
		result.commodities.push_back(line);
	}
	std::stable_sort(result.commodities.begin(), result.commodities.end(), [&](commodity_line const& a, commodity_line const& b) {
		return a.bought * unit_price(state, home_market, a.commodity) > b.bought * unit_price(state, home_market, b.commodity);
	});

	auto const total = result.exported + result.imported;
	for(uint32_t i = 0; i < partners.size(); ++i) {
		partners[i].nation = dcon::nation_id{ dcon::nation_id::value_base_t(i) };
		if(partners[i].total() <= 0.f)
			continue;
		result.share_by_nation[i] = total > 0.f ? partners[i].total() / total : 0.f;
		result.partners.push_back(partners[i]);
	}
	std::stable_sort(result.partners.begin(), result.partners.end(), [](partner_line const& a, partner_line const& b) { return a.total() > b.total(); });
	return result;
}

} // namespace economy::trade_picture
