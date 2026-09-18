#include "individual_consumption.hpp"

#include "accounts/accounts.hpp"
#include "persons/persons.hpp"
#include "system_state.hpp"
#include "economy/physical/concrete_market.hpp"
#include "economy/physical/inventory.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace economy::physical::individual_consumption {
namespace {
constexpr float epsilon = 1.0e-5f;

template<typename Id>
void sort_ids(std::vector<Id>& ids) {
	std::sort(ids.begin(), ids.end(), [](auto left, auto right) { return left.index() < right.index(); });
}

dcon::person_commodity_need_id need_for(sys::state const& state, dcon::person_id person,
	dcon::commodity_id commodity) {
	dcon::person_commodity_need_id result{};
	if(!person || !commodity) return result;
	state.world.person_for_each_person_commodity_need_person_as_person(person, [&](auto relation) {
		auto need = state.world.person_commodity_need_person_get_person_commodity_need(relation);
		if(!result && need && state.world.person_commodity_need_get_commodity_from_person_commodity_need_commodity(need) == commodity)
			result = need;
	});
	return result;
}

std::vector<dcon::person_commodity_need_id> needs_for(sys::state const& state, dcon::person_id person) {
	std::vector<dcon::person_commodity_need_id> result;
	if(!person) return result;
	state.world.person_for_each_person_commodity_need_person_as_person(person, [&](auto relation) {
		auto need = state.world.person_commodity_need_person_get_person_commodity_need(relation);
		if(need) result.push_back(need);
	});
	sort_ids(result);
	return result;
}

dcon::market_id market_for_site(sys::state const& state, dcon::site_id site) {
	if(!site || !state.world.site_is_valid(site)) return {};
	auto province = state.world.site_get_province_from_site_location(site);
	if(!province || !state.world.province_is_valid(province)) return {};
	auto zone = state.world.province_get_state_membership(province);
	return zone ? state.world.state_instance_get_market_from_local_market(zone) : dcon::market_id{};
}

float free_cash(sys::state const& state, dcon::monetary_account_id account) {
	return std::max(0.0f, accounts::balance(state, account)
		- concrete_market::reserved_bid_amount(state, account));
}

bool has_active_bid(sys::state const& state, dcon::economic_actor_id actor,
	dcon::site_id destination, dcon::commodity_id commodity) {
	bool result = false;
	state.world.economic_actor_for_each_concrete_bid_buyer_as_economic_actor(actor, [&](auto relation) {
		if(result) return;
		auto bid = state.world.concrete_bid_buyer_get_concrete_market_bid(relation);
		if(bid && state.world.concrete_market_bid_get_status(bid)
			== uint8_t(concrete_market::order_status::active)
			&& state.world.concrete_market_bid_get_site_from_concrete_bid_destination(bid) == destination
			&& state.world.concrete_market_bid_get_commodity_from_concrete_bid_commodity(bid) == commodity)
			result = true;
	});
	return result;
}

void refresh_unmet(sys::state& state, dcon::person_commodity_need_id need,
	dcon::person_id person, dcon::commodity_id commodity, float desired) {
	auto owned = owned_consumable_quantity(state, person, commodity);
	auto consumed = std::max(0.0f, state.world.person_commodity_need_get_consumed_this_period(need));
	state.world.person_commodity_need_set_unmet_quantity(need, std::max(0.0f, desired - consumed - owned));
}

std::vector<dcon::commodity_id> seller_settlements(sys::state const& state,
	dcon::market_id market, dcon::commodity_id commodity) {
	std::vector<dcon::commodity_id> result;
	state.world.for_each_concrete_market_ask([&](auto ask) {
		if(state.world.concrete_market_ask_get_status(ask)
			!= uint8_t(concrete_market::order_status::active)
			|| state.world.concrete_market_ask_get_market_from_concrete_ask_market(ask) != market
			|| state.world.concrete_market_ask_get_commodity_from_concrete_ask_commodity(ask) != commodity) return;
		auto seller = state.world.concrete_market_ask_get_economic_actor_from_concrete_ask_seller(ask);
		state.world.economic_actor_for_each_monetary_account_owner_as_economic_actor(seller,
			[&](auto relation) {
				auto account = state.world.monetary_account_owner_get_monetary_account(relation);
				auto settlement = accounts::settlement_of(state, account);
				if(settlement && state.world.commodity_is_valid(settlement)) result.push_back(settlement);
			});
	});
	sort_ids(result);
	result.erase(std::unique(result.begin(), result.end()), result.end());
	return result;
}
}

bool set_home_site(sys::state& state, dcon::person_id person, dcon::site_id site) {
	if(!person || !state.world.person_is_valid(person) || !site || !state.world.site_is_valid(site)) return false;
	state.world.person_remove_person_home_site_as_person(person);
	state.world.force_create_person_home_site(person, site);
	return true;
}

dcon::site_id home_site(sys::state const& state, dcon::person_id person) {
	return person && state.world.person_is_valid(person)
		? state.world.person_get_site_from_person_home_site(person) : dcon::site_id{};
}

dcon::person_commodity_need_id set_need(sys::state& state, dcon::person_id person,
	dcon::commodity_id commodity, float desired_quantity_per_period) {
	if(!person || !state.world.person_is_valid(person) || !commodity
		|| !state.world.commodity_is_valid(commodity) || !std::isfinite(desired_quantity_per_period)
		|| desired_quantity_per_period < 0.0f) return {};
	auto need = need_for(state, person, commodity);
	if(!need) {
		need = state.world.create_person_commodity_need();
		state.world.force_create_person_commodity_need_person(need, person);
		state.world.force_create_person_commodity_need_commodity(need, commodity);
		state.world.person_commodity_need_set_last_consumed_quantity(need, 0.0f);
		state.world.person_commodity_need_set_last_consumed_on(need, {});
		state.world.person_commodity_need_set_consumed_this_period(need, 0.0f);
		state.world.person_commodity_need_set_consumption_period_start(need, state.current_date);
	}
	state.world.person_commodity_need_set_desired_quantity_per_period(need, desired_quantity_per_period);
	refresh_unmet(state, need, person, commodity, desired_quantity_per_period);
	return need;
}

dcon::person_commodity_need_id add_need(sys::state& state, dcon::person_id person,
	dcon::commodity_id commodity, float quantity) {
	if(!std::isfinite(quantity) || quantity < 0.0f) return {};
	auto current = need_for(state, person, commodity);
	auto desired = current ? state.world.person_commodity_need_get_desired_quantity_per_period(current) : 0.0f;
	return set_need(state, person, commodity, desired + quantity);
}

float owned_consumable_quantity(sys::state const& state, dcon::person_id person, dcon::commodity_id commodity) {
	auto actor = persons::actor_for_person(state, person);
	auto site = home_site(state, person);
	return actor && site && commodity ? inventory::quantity(state, site, commodity, actor) : 0.0f;
}

float unmet_need(sys::state const& state, dcon::person_id person, dcon::commodity_id commodity) {
	auto need = need_for(state, person, commodity);
	if(!need) return 0.0f;
	return std::max(0.0f, state.world.person_commodity_need_get_desired_quantity_per_period(need)
		- std::max(0.0f, state.world.person_commodity_need_get_consumed_this_period(need))
		- owned_consumable_quantity(state, person, commodity));
}

void begin_period(sys::state& state, sys::date period) {
	std::vector<dcon::person_commodity_need_id> needs;
	state.world.for_each_person_commodity_need([&](auto need) { needs.push_back(need); });
	sort_ids(needs);
	for(auto need : needs) {
		auto started = state.world.person_commodity_need_get_consumption_period_start(need);
		if(period <= started) continue;
		state.world.person_commodity_need_set_consumed_this_period(need, 0.0f);
		state.world.person_commodity_need_set_consumption_period_start(need, period);
		auto person = state.world.person_commodity_need_get_person_from_person_commodity_need_person(need);
		auto commodity = state.world.person_commodity_need_get_commodity_from_person_commodity_need_commodity(need);
		refresh_unmet(state, need, person, commodity,
			state.world.person_commodity_need_get_desired_quantity_per_period(need));
	}
}

dcon::monetary_account_id spending_account(sys::state const& state, dcon::person_id person,
	dcon::commodity_id settlement) {
	auto actor = persons::actor_for_person(state, person);
	if(!actor) return {};
	std::vector<dcon::monetary_account_id> accounts_for_person;
	state.world.economic_actor_for_each_monetary_account_owner_as_economic_actor(actor, [&](auto relation) {
		auto account = state.world.monetary_account_owner_get_monetary_account(relation);
		if(account && state.world.monetary_account_is_valid(account)) accounts_for_person.push_back(account);
	});
	sort_ids(accounts_for_person);
	dcon::monetary_account_id result{};
	float best_cash = -std::numeric_limits<float>::infinity();
	for(auto account : accounts_for_person) {
		if(settlement && accounts::settlement_of(state, account) != settlement) continue;
		auto cash = free_cash(state, account);
		if(cash > best_cash || (cash == best_cash && (!result || account.index() < result.index()))) {
			result = account;
			best_cash = cash;
		}
	}
	return result;
}

float spendable_cash(sys::state const& state, dcon::person_id person, dcon::commodity_id settlement) {
	return free_cash(state, spending_account(state, person, settlement));
}

float consume_owned_goods(sys::state& state, dcon::person_id person, dcon::commodity_id commodity, float quantity) {
	if(!person || !state.world.person_is_valid(person) || !state.world.person_get_alive(person)
		|| !commodity || !std::isfinite(quantity) || quantity <= 0.0f)
		return 0.0f;
	auto actor = persons::actor_for_person(state, person);
	auto site = home_site(state, person);
	if(!actor || !site) return 0.0f;
	auto consumed = inventory::remove(state, site, commodity, quantity, actor);
	auto need = need_for(state, person, commodity);
	if(need) {
		state.world.person_commodity_need_set_consumed_this_period(need,
			std::max(0.0f, state.world.person_commodity_need_get_consumed_this_period(need)) + consumed);
		state.world.person_commodity_need_set_last_consumed_quantity(need, consumed);
		state.world.person_commodity_need_set_last_consumed_on(need, state.current_date);
		refresh_unmet(state, need, person, commodity,
			state.world.person_commodity_need_get_desired_quantity_per_period(need));
	}
	return consumed;
}

float last_consumed_amount(sys::state const& state, dcon::person_id person, dcon::commodity_id commodity) {
	auto need = need_for(state, person, commodity);
	return need ? std::max(0.0f, state.world.person_commodity_need_get_last_consumed_quantity(need)) : 0.0f;
}

void process_purchase_decisions(sys::state& state) {
	std::vector<dcon::person_id> people;
	state.world.for_each_person([&](auto person) {
		if(!needs_for(state, person).empty()) people.push_back(person);
	});
	sort_ids(people);
	std::vector<std::pair<dcon::market_id, dcon::commodity_id>> markets_to_match;
	for(auto person : people) {
		if(!state.world.person_get_alive(person)) continue;
		auto site = home_site(state, person);
		auto market = market_for_site(state, site);
		auto actor = persons::actor_for_person(state, person);
		if(!site || !market || !actor) continue;
		for(auto need : needs_for(state, person)) {
			auto commodity = state.world.person_commodity_need_get_commodity_from_person_commodity_need_commodity(need);
			auto desired = state.world.person_commodity_need_get_desired_quantity_per_period(need);
			if(!commodity || !std::isfinite(desired) || desired <= 0.0f) {
				state.world.person_commodity_need_set_unmet_quantity(need, 0.0f);
				continue;
			}
			refresh_unmet(state, need, person, commodity, desired);
			auto unmet = state.world.person_commodity_need_get_unmet_quantity(need);
			if(unmet <= epsilon || has_active_bid(state, actor, site, commodity)) continue;
			auto fallback = std::max(0.01f, state.world.commodity_get_cost(commodity));
			auto price = concrete_market::concrete_reference_price(state, market, commodity, state.current_date, fallback);
			if(!std::isfinite(price) || price <= 0.0f) continue;
			auto account = dcon::monetary_account_id{};
			auto best_cash = -std::numeric_limits<float>::infinity();
			for(auto settlement : seller_settlements(state, market, commodity)) {
				auto candidate = spending_account(state, person, settlement);
				if(!candidate) continue;
				auto cash = free_cash(state, candidate);
				if(cash > best_cash || (cash == best_cash && (!account || candidate.index() < account.index()))) {
					account = candidate;
					best_cash = cash;
				}
			}
			auto cash = free_cash(state, account);
			auto quantity = std::min(unmet, cash / price);
			if(!std::isfinite(quantity) || quantity <= epsilon) continue;
			auto bid = concrete_market::post_bid(state, actor, account, site, market, commodity,
				quantity, price, concrete_market::order_purpose::general);
			if(bid) markets_to_match.emplace_back(market, commodity);
		}
	}
	std::sort(markets_to_match.begin(), markets_to_match.end(), [](auto const& left, auto const& right) {
		return left.first.index() == right.first.index()
			? left.second.index() < right.second.index()
			: left.first.index() < right.first.index();
	});
	markets_to_match.erase(std::unique(markets_to_match.begin(), markets_to_match.end()), markets_to_match.end());
	for(auto const& [market, commodity] : markets_to_match)
		(void)concrete_market::match(state, market, commodity, state.current_date);
}

void process_consumption(sys::state& state) {
	std::vector<dcon::person_id> people;
	state.world.for_each_person([&](auto person) {
		if(!needs_for(state, person).empty()) people.push_back(person);
	});
	sort_ids(people);
	for(auto person : people) {
		if(!state.world.person_get_alive(person)) continue;
		for(auto need : needs_for(state, person)) {
			auto commodity = state.world.person_commodity_need_get_commodity_from_person_commodity_need_commodity(need);
			auto desired = state.world.person_commodity_need_get_desired_quantity_per_period(need);
			if(commodity && desired > 0.0f) (void)consume_owned_goods(state, person, commodity, desired);
		}
	}
}

void process(sys::state& state) {
	begin_period(state, state.current_date);
	process_purchase_decisions(state);
	process_consumption(state);
}

} // namespace economy::physical::individual_consumption
