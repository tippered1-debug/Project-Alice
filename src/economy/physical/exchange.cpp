#include "exchange.hpp"
#include "economy/banking/banking.hpp"
#include "economy/physical/extraction.hpp"
#include "inventory.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/relations/relations.hpp"
#include "governance/finance/finance.hpp"
#include "governance/policy.hpp"
#include "system_state.hpp"
#include "money.hpp"

#include <algorithm>
#include <cmath>

namespace economy::physical::exchange {
namespace {

void assess_resource_royalty(sys::state& state, dcon::site_id site, dcon::commodity_id commodity,
	dcon::economic_actor_id seller, float gross_receipt,
	economy::exact_person_economy::account_ref seller_wallet,
	dcon::deposit_account_id seller_deposit, sys::date date) {
	auto nation = extraction::royalty_jurisdiction_for_sale(state, seller, site, commodity);
	if(!nation) return;
	(void)governance::finance::assess_and_collect_topic_tax(state,
		governance::policy::topic_id::resource_royalty, nation, seller, gross_receipt,
		seller_wallet, seller_deposit, date);
}

} // namespace

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
		if(owner && owner != buyer && state.world.physical_stock_get_commodity_from_physical_stock_commodity(stock) == commodity && std::isfinite(state.world.physical_stock_get_quantity(stock)) && state.world.physical_stock_get_quantity(stock) > 0.0f)
			result.push_back(stock);
	});
	std::sort(result.begin(), result.end(), [](auto a, auto b) { return a.index() < b.index(); });
	return result;
}

dcon::transaction_id purchase(sys::state& state, dcon::site_id site, dcon::commodity_id commodity,
	dcon::economic_actor_id seller, dcon::economic_actor_id buyer, float quantity, float unit_price,
	dcon::commodity_id settlement, sys::date timestamp) {
	if(!site || !commodity || !seller || !buyer || seller == buyer || !std::isfinite(quantity) || quantity <= 0.0f || !std::isfinite(unit_price) || unit_price <= 0.0f || !settlement || !state.world.commodity_is_valid(settlement))
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
	if(transaction) {
		assess_resource_royalty(state, site, commodity, seller, cost,
			economy::exact_person_economy::account_ref::from_dcon(seller_account), {}, timestamp);
		return transaction;
	}
	inventory::transfer(state, site, commodity, buyer, seller, quantity);
	return {};
}

dcon::transaction_id purchase_with_account(sys::state& state, dcon::site_id site,
	dcon::commodity_id commodity, dcon::economic_actor_id seller,
	dcon::economic_actor_id buyer, dcon::monetary_account_id buyer_account,
	float quantity, float unit_price, sys::date timestamp) {
	if(!site || !commodity || !seller || !buyer || seller == buyer || !buyer_account || accounts::owner_of(state, buyer_account) != buyer || !std::isfinite(quantity) || quantity <= 0.0f || !std::isfinite(unit_price) || unit_price <= 0.0f)
		return {};
	auto settlement = accounts::settlement_of(state, buyer_account);
	auto seller_account = accounts::find_account(state, seller, settlement);
	if(!settlement || !state.world.commodity_is_valid(settlement) || !seller_account || accounts::owner_of(state, seller_account) != seller || accounts::settlement_of(state, seller_account) != settlement || inventory::quantity(state, site, commodity, seller) < quantity)
		return {};
	auto cost = quantity * unit_price;
	if(!std::isfinite(cost) || accounts::balance(state, buyer_account) < cost) return {};
	if(!inventory::transfer(state, site, commodity, seller, buyer, quantity)) return {};
	auto transaction = accounts::transfer(state, buyer_account, seller_account, cost,
		relations::transaction_kind::purchase, timestamp);
	if(transaction) {
		assess_resource_royalty(state, site, commodity, seller, cost,
			economy::exact_person_economy::account_ref::from_dcon(seller_account), {}, timestamp);
		return transaction;
	}
	inventory::transfer(state, site, commodity, buyer, seller, quantity);
	return {};
}

dcon::transaction_id purchase_with_deposit(sys::state& state, dcon::site_id site,
	dcon::commodity_id commodity, dcon::economic_actor_id seller, dcon::economic_actor_id buyer,
	dcon::deposit_account_id buyer_deposit, float quantity, float unit_price, sys::date timestamp) {
	if(!site || !commodity || !seller || !buyer || seller == buyer || !buyer_deposit
		|| state.world.deposit_account_get_economic_actor_from_deposit_account_owner(buyer_deposit) != buyer
		|| !std::isfinite(quantity) || quantity <= 0.0f || !std::isfinite(unit_price) || unit_price <= 0.0f) return {};
	auto settlement = state.world.deposit_account_get_commodity_from_deposit_account_settlement(buyer_deposit);
	auto bank = state.world.deposit_account_get_organization_from_deposit_account_bank(buyer_deposit);
	auto cost = quantity * unit_price;
	if(!std::isfinite(cost) || economy::banking::deposit_balance(state, buyer_deposit) < cost
		|| inventory::quantity(state, site, commodity, seller) < quantity) return {};
	auto payee_deposit = economy::banking::deposit_account_for(state, seller, settlement, bank);
	auto payee_wallet = accounts::find_account(state, seller, settlement);
	if(!payee_deposit && !payee_wallet) return {};
	if(!inventory::transfer(state, site, commodity, seller, buyer, quantity)) return {};
	auto transaction = economy::banking::pay_from_deposit(state, buyer_deposit, payee_deposit,
		payee_deposit ? economy::exact_person_economy::account_ref{} : economy::exact_person_economy::account_ref::from_dcon(payee_wallet),
		cost, relations::transaction_kind::purchase, timestamp);
	if(transaction) {
		assess_resource_royalty(state, site, commodity, seller, cost,
			payee_deposit ? economy::exact_person_economy::account_ref{}
				: economy::exact_person_economy::account_ref::from_dcon(payee_wallet),
			payee_deposit, timestamp);
		return transaction;
	}
	inventory::transfer(state, site, commodity, buyer, seller, quantity);
	return {};
}

} // namespace economy::physical::exchange
