#include "individual_consumption.hpp"

#include "economy/physical/exact_person_goods.hpp"
#include "persons/persons.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace economy::physical::individual_consumption {
namespace {

persons::person_key key_for(sys::state const& state, dcon::person_id person) {
	return person && persons::alive(state, person)
		? persons::canonical_key(state, person) : persons::person_key{};
}

float free_cash(sys::state const& state, economy::exact_person_economy::account_ref account) {
	if(!account) return 0.0f;
	auto available = economy::exact_person_economy::balance(state, account);
	if(account.kind == economy::exact_person_economy::account_kind::exact)
		available -= exact_person_goods::reserved_bid_amount(state, account.exact_account_id);
	return std::max(0.0f, available);
}

} // namespace

bool set_home_site(sys::state& state, dcon::person_id person, dcon::site_id site) {
	auto key = key_for(state, person);
	if(key.source_population_cell == 0 || !persons::set_home_site(state, key, site)) return false;
	exact_person_goods::refresh_unmet_needs(state, key);
	return true;
}

dcon::site_id home_site(sys::state const& state, dcon::person_id person) {
	auto key = key_for(state, person);
	return key.source_population_cell != 0 ? persons::home_site(state, key) : dcon::site_id{};
}

bool set_need(sys::state& state, dcon::person_id person, dcon::commodity_id commodity,
	float desired_quantity_per_period) {
	auto key = key_for(state, person);
	return key.source_population_cell != 0
		&& exact_person_goods::set_need(state, key, commodity, desired_quantity_per_period);
}

bool add_need(sys::state& state, dcon::person_id person, dcon::commodity_id commodity,
	float quantity) {
	if(!std::isfinite(quantity) || quantity < 0.0f) return false;
	auto key = key_for(state, person);
	if(key.source_population_cell == 0) return false;
	auto current = exact_person_goods::need(state, key, commodity);
	auto desired = current ? current->desired_quantity_per_period : 0.0f;
	return exact_person_goods::set_need(state, key, commodity, desired + quantity);
}

float unmet_need(sys::state const& state, dcon::person_id person, dcon::commodity_id commodity) {
	auto key = key_for(state, person);
	return key.source_population_cell != 0 ? exact_person_goods::unmet_need(state, key, commodity) : 0.0f;
}

void begin_period(sys::state& state, sys::date period) {
	exact_person_goods::begin_period(state, period);
}

economy::exact_person_economy::account_ref spending_account(sys::state const& state,
	dcon::person_id person, dcon::commodity_id settlement) {
	auto key = key_for(state, person);
	if(key.source_population_cell == 0) return {};
	auto accounts = economy::exact_person_economy::accounts_for_person(state, key);
	economy::exact_person_economy::account_ref selected{};
	float best_cash = -std::numeric_limits<float>::infinity();
	for(auto account : accounts) {
		if(settlement && economy::exact_person_economy::settlement_of(state, account) != settlement) continue;
		auto cash = free_cash(state, account);
		auto const account_settlement = economy::exact_person_economy::settlement_of(state, account);
		auto const selected_settlement = selected
			? economy::exact_person_economy::settlement_of(state, selected)
			: dcon::commodity_id{};
		if(cash > best_cash || (cash == best_cash
			&& (!selected || account_settlement.index() < selected_settlement.index()))) {
			selected = account;
			best_cash = cash;
		}
	}
	return selected;
}

float spendable_cash(sys::state const& state, dcon::person_id person,
	dcon::commodity_id settlement) {
	return free_cash(state, spending_account(state, person, settlement));
}

float owned_consumable_quantity(sys::state const& state, dcon::person_id person,
	dcon::commodity_id commodity) {
	auto key = key_for(state, person);
	auto site = key.source_population_cell != 0 ? persons::home_site(state, key) : dcon::site_id{};
	return site ? exact_person_goods::stock_quantity(state, key, site, commodity) : 0.0f;
}

float consume_owned_goods(sys::state& state, dcon::person_id person,
	dcon::commodity_id commodity, float quantity) {
	auto key = key_for(state, person);
	return key.source_population_cell != 0
		? exact_person_goods::consume_stock(state, key, commodity, quantity) : 0.0f;
}

float last_consumed_amount(sys::state const& state, dcon::person_id person,
	dcon::commodity_id commodity) {
	auto key = key_for(state, person);
	auto record = key.source_population_cell != 0
		? exact_person_goods::need(state, key, commodity) : std::nullopt;
	return record ? record->last_consumed_quantity : 0.0f;
}

void process_purchase_decisions(sys::state& state) {
	auto snapshot = exact_person_goods::export_snapshot(state);
	std::vector<persons::person_key> people;
	people.reserve(snapshot.needs.size());
	for(auto const& need : snapshot.needs) people.push_back(need.owner);
	std::sort(people.begin(), people.end(), [](auto left, auto right) {
		return left.source_population_cell == right.source_population_cell
			? left.ordinal < right.ordinal
			: left.source_population_cell < right.source_population_cell;
	});
	people.erase(std::unique(people.begin(), people.end()), people.end());
	for(auto person : people) exact_person_goods::process_purchase_decisions(state, person);
}

void process_consumption(sys::state& state) {
	auto snapshot = exact_person_goods::export_snapshot(state);
	std::sort(snapshot.needs.begin(), snapshot.needs.end(), [](auto const& left, auto const& right) {
		if(left.owner.source_population_cell != right.owner.source_population_cell)
			return left.owner.source_population_cell < right.owner.source_population_cell;
		if(left.owner.ordinal != right.owner.ordinal) return left.owner.ordinal < right.owner.ordinal;
		return left.commodity.index() < right.commodity.index();
	});
	for(auto const& need : snapshot.needs)
		if(persons::alive(state, need.owner))
			(void)exact_person_goods::process_consumption(state, need.owner, need.commodity);
}

void process(sys::state& state) {
	exact_person_goods::process_daily(state);
}

} // namespace economy::physical::individual_consumption
