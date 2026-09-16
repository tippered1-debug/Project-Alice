#include "exchange.hpp"
#include "inventory.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/relations/relations.hpp"
#include "system_state.hpp"
#include "money.hpp"

#include <algorithm>
#include <cmath>

namespace economy::physical::exchange {

dcon::commodity_id settlement_for_purchase(sys::state const& state, dcon::economic_actor_id buyer) {
	return accounts::first_settlement_for(state, buyer);
}

std::vector<dcon::physical_stock_id> seller_stocks(sys::state const& state, dcon::site_id site,
	dcon::commodity_id commodity, dcon::economic_actor_id buyer) {
	std::vector<dcon::physical_stock_id> result;
	state.world.site_for_each_physical_stock_site_as_site(site, [&](dcon::physical_stock_site_id relation) {
		auto stock = state.world.physical_stock_site_get_physical_stock(relation);
		auto owner_relation = state.world.physical_stock_get_physical_stock_owner(stock);
		auto owner = owner_relation ? state.world.physical_stock_owner_get_economic_actor(owner_relation) : dcon::economic_actor_id{};
		if(owner && owner != buyer && state.world.physical_stock_get_commodity_from_physical_stock_commodity(stock) == commodity
			&& std::isfinite(state.world.physical_stock_get_quantity(stock))
			&& state.world.physical_stock_get_quantity(stock) > 0.0f)
			result.push_back(stock);
	});
	std::sort(result.begin(), result.end(), [](auto a, auto b) { return a.index() < b.index(); });
	return result;
}

dcon::transaction_id purchase(sys::state& state, dcon::site_id site, dcon::commodity_id commodity,
	dcon::economic_actor_id seller, dcon::economic_actor_id buyer, float quantity, float unit_price,
	dcon::commodity_id settlement, sys::date timestamp) {
	if(!site || !commodity || !seller || !buyer || seller == buyer || !std::isfinite(quantity) || quantity <= 0.0f
		|| !std::isfinite(unit_price) || unit_price <= 0.0f || !settlement || !state.world.commodity_is_valid(settlement))
		return {};
	auto buyer_account = accounts::find_account(state, buyer, settlement);
	auto seller_account = accounts::find_account(state, seller, settlement);
	if(!buyer_account || !seller_account || inventory::quantity(state, site, commodity, seller) < quantity)
		return {};
	auto cost = quantity * unit_price;
	if(!std::isfinite(cost) || accounts::balance(state, buyer_account) < cost)
		return {};
	// Move the physical stock first, then commit money and its transaction. All
	// preconditions above make the reverse operation safe if account settlement
	// unexpectedly fails; failed exchanges therefore leave no Transaction.
	if(!inventory::transfer(state, site, commodity, seller, buyer, quantity)) return {};
	auto transaction = accounts::transfer(state, buyer_account, seller_account, cost,
		relations::transaction_kind::purchase, timestamp);
	if(transaction) return transaction;
	inventory::transfer(state, site, commodity, buyer, seller, quantity);
	return {};
}

} // namespace economy::physical::exchange
