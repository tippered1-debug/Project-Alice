#include "inventory.hpp"
#include "system_state.hpp"

#include <algorithm>
#include <cmath>

namespace economy::physical::inventory {

dcon::physical_stock_id find(sys::state const& state, dcon::site_id site, dcon::commodity_id commodity, dcon::economic_actor_id owner) {
	dcon::physical_stock_id result{};
	state.world.site_for_each_physical_stock_site_as_site(site, [&](dcon::physical_stock_site_id relation) {
		auto stock = state.world.physical_stock_site_get_physical_stock(relation);
		if(state.world.physical_stock_get_commodity_from_physical_stock_commodity(stock) == commodity
			&& state.world.physical_stock_get_economic_actor_from_physical_stock_owner(stock) == owner)
			result = stock;
	});
	return result;
}

dcon::physical_stock_id ensure(sys::state& state, dcon::site_id site, dcon::commodity_id commodity, dcon::economic_actor_id owner) {
	if(auto stock = find(state, site, commodity, owner))
		return stock;
	auto stock = state.world.create_physical_stock();
	state.world.force_create_physical_stock_site(stock, site);
	state.world.force_create_physical_stock_commodity(stock, commodity);
	state.world.physical_stock_set_quantity(stock, 0.0f);
	if(owner) state.world.force_create_physical_stock_owner(stock, owner);
	return stock;
}

float quantity(sys::state const& state, dcon::site_id site, dcon::commodity_id commodity, dcon::economic_actor_id owner) {
	auto stock = find(state, site, commodity, owner);
	return stock ? std::max(0.0f, state.world.physical_stock_get_quantity(stock)) : 0.0f;
}

float add(sys::state& state, dcon::site_id site, dcon::commodity_id commodity, float amount, dcon::economic_actor_id owner) {
	if(!std::isfinite(amount) || amount <= 0.0f)
		return 0.0f;
	auto stock = ensure(state, site, commodity, owner);
	auto next = std::max(0.0f, state.world.physical_stock_get_quantity(stock)) + amount;
	if(!std::isfinite(next))
		return 0.0f;
	state.world.physical_stock_set_quantity(stock, next);
	return amount;
}

float remove(sys::state& state, dcon::site_id site, dcon::commodity_id commodity, float amount, dcon::economic_actor_id owner) {
	if(!std::isfinite(amount) || amount <= 0.0f)
		return 0.0f;
	auto stock = find(state, site, commodity, owner);
	if(!stock)
		return 0.0f;
	auto available = std::max(0.0f, state.world.physical_stock_get_quantity(stock));
	auto removed = std::min(available, amount);
	state.world.physical_stock_set_quantity(stock, available - removed);
	return removed;
}

bool transfer(sys::state& state, dcon::site_id site, dcon::commodity_id commodity,
	dcon::economic_actor_id seller, dcon::economic_actor_id buyer, float amount) {
	if(!site || !commodity || !seller || !buyer || seller == buyer || !std::isfinite(amount) || amount <= 0.0f)
		return false;
	auto seller_stock = find(state, site, commodity, seller);
	if(!seller_stock || quantity(state, site, commodity, seller) < amount)
		return false;
	if(!std::isfinite(quantity(state, site, commodity, buyer) + amount))
		return false;
	// Both sides have been validated before either stock is changed.
	if(remove(state, site, commodity, amount, seller) != amount)
		return false;
	if(add(state, site, commodity, amount, buyer) != amount) {
		add(state, site, commodity, amount, seller);
		return false;
	}
	return true;
}

} // namespace economy::physical::inventory
