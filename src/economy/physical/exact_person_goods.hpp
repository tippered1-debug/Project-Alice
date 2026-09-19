#pragma once

#include "dcon_generated.hpp"
#include "date_interface.hpp"
#include "economy/exact_person_economy.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace sys { class state; }

namespace economy::physical::exact_person_goods {

using person_key = persons::exact_population::person_key;

enum class order_status : uint8_t { active = 0, canceled = 1, filled = 2 };
enum class order_purpose : uint8_t { general = 0 };

struct stock_record {
	person_key owner{};
	dcon::site_id site{};
	dcon::commodity_id commodity{};
	float quantity = 0.0f;
};

struct need_record {
	person_key owner{};
	dcon::commodity_id commodity{};
	float desired_quantity_per_period = 0.0f;
	float consumed_this_period = 0.0f;
	sys::date consumption_period_start{};
	float unmet_quantity = 0.0f;
	float last_consumed_quantity = 0.0f;
	sys::date last_consumed_on{};
};

struct bid_record {
	uint64_t id = 0;
	person_key buyer{};
	uint64_t exact_account_id = 0;
	dcon::site_id destination{};
	dcon::market_id market{};
	dcon::commodity_id commodity{};
	float original_quantity = 0.0f;
	float remaining_quantity = 0.0f;
	float limit_price = 0.0f;
	float reserved_amount = 0.0f;
	sys::date created_on{};
	order_status status = order_status::active;
	order_purpose purpose = order_purpose::general;
};

struct bid_reference { uint64_t id = 0; sys::date created_on{}; };

struct fill_record {
	uint64_t id = 0;
	uint64_t exact_bid_id = 0;
	dcon::concrete_market_ask_id dcon_ask{};
	uint64_t exact_transaction_id = 0;
	float quantity = 0.0f;
	float execution_price = 0.0f;
	dcon::site_id source{};
	dcon::site_id destination{};
	dcon::market_id market{};
	dcon::commodity_id commodity{};
	sys::date occurred_on{};
};

struct goods_snapshot {
	uint32_t version = 1;
	std::vector<stock_record> stocks;
	std::vector<need_record> needs;
	std::vector<bid_record> bids;
	std::vector<fill_record> fills;
};

float stock_quantity(sys::state const&, person_key, dcon::site_id, dcon::commodity_id);
float add_stock(sys::state&, person_key, dcon::site_id, dcon::commodity_id, float);
float remove_stock(sys::state&, person_key, dcon::site_id, dcon::commodity_id, float);

bool set_need(sys::state&, person_key, dcon::commodity_id, float desired_quantity_per_period);
float unmet_need(sys::state const&, person_key, dcon::commodity_id);
float consumed_this_period(sys::state const&, person_key, dcon::commodity_id);
void begin_period(sys::state&, sys::date period);
float process_consumption(sys::state&, person_key, dcon::commodity_id);

std::optional<need_record> need(sys::state const&, person_key, dcon::commodity_id);
std::optional<bid_record> bid(sys::state const&, uint64_t id);
std::optional<fill_record> fill(sys::state const&, uint64_t id);
uint64_t bid_count(sys::state const&);
uint64_t fill_count(sys::state const&);

uint64_t post_bid(sys::state&, person_key, economy::exact_person_economy::account_ref,
	dcon::site_id destination, dcon::market_id, dcon::commodity_id,
	float quantity, float limit_price, order_purpose = order_purpose::general);
std::vector<bid_reference> active_bids(sys::state const&, dcon::market_id, dcon::commodity_id);
uint64_t try_fill(sys::state&, uint64_t exact_bid_id, dcon::concrete_market_ask_id, sys::date);
void expire(sys::state&, sys::date);

bool process_purchase_decision(sys::state&, person_key, dcon::commodity_id);
void process_purchase_decisions(sys::state&, person_key);

float observed_price(sys::state const&, dcon::market_id, dcon::commodity_id, sys::date);
float concrete_reference_price(sys::state const&, dcon::market_id, dcon::commodity_id, sys::date);

goods_snapshot export_snapshot(sys::state const&);
bool import_snapshot(sys::state&, goods_snapshot const&);
void clear_store(sys::state&);

} // namespace economy::physical::exact_person_goods
