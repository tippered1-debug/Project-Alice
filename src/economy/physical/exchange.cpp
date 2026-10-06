#include "exchange.hpp"
#include "economy/banking/banking.hpp"
#include "economy/physical/extraction.hpp"
#include "concrete_market.hpp"
#include "inventory.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/economy_stats.hpp"
#include "economy/relations/relations.hpp"
#include "governance/finance/finance.hpp"
#include "governance/policy.hpp"
#include "system_state.hpp"
#include "money.hpp"

#include <algorithm>
#include <cmath>

namespace economy::physical::exchange {
namespace {

dcon::nation_id customs_jurisdiction(sys::state const& state, dcon::site_id site) {
	if(!site || !state.world.site_is_valid(site)) return {};
	auto province = state.world.site_get_province_from_site_location(site);
	if(!province || !state.world.province_is_valid(province)) return {};
	auto controller = state.world.province_get_nation_from_province_control(province);
	if(controller) return controller;
	auto zone = state.world.province_get_state_membership(province);
	return zone ? state.world.state_instance_get_nation_from_state_ownership(zone)
		: dcon::nation_id{};
}

bool customs_duty_applies(sys::state const& state, dcon::site_id source,
	dcon::site_id destination, bool export_duty) {
	auto source_market = concrete_market::market_for_site(state, source);
	auto destination_market = concrete_market::market_for_site(state, destination);
	if(!source_market || !destination_market) return true;
	auto route = state.world.get_trade_route_by_province_pair(source_market,
		destination_market);
	if(!route || !state.world.trade_route_is_valid(route)) return true;
	auto market_0 = state.world.trade_route_get_connected_markets(route, 0);
	auto market_1 = state.world.trade_route_get_connected_markets(route, 1);
	if(source_market == market_0)
		return export_duty
			? state.world.trade_route_get_is_tariff_applied_0(route)
			: state.world.trade_route_get_is_tariff_applied_1(route);
	if(source_market == market_1)
		return export_duty
			? state.world.trade_route_get_is_tariff_applied_1(route)
			: state.world.trade_route_get_is_tariff_applied_0(route);
	return true;
}

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

void assess_customs_duties(sys::state& state, dcon::economic_actor_id buyer,
	dcon::economic_actor_id seller, dcon::site_id source, dcon::site_id destination,
	dcon::commodity_id commodity, float declared_value,
	economy::exact_person_economy::account_ref buyer_wallet,
	dcon::deposit_account_id buyer_deposit,
	economy::exact_person_economy::account_ref seller_wallet,
	dcon::deposit_account_id seller_deposit, sys::date date) {
	if(!buyer || !seller || buyer == seller || !source || !destination || !commodity
		|| !state.world.site_is_valid(source) || !state.world.site_is_valid(destination)
		|| !state.world.commodity_is_valid(commodity)
		|| !std::isfinite(declared_value) || declared_value <= 0.0f) return;
	auto exporter = customs_jurisdiction(state, source);
	auto importer = customs_jurisdiction(state, destination);
	if(!exporter || !importer || exporter == importer) return;
	auto export_market = concrete_market::market_for_site(state, source);
	auto import_market = concrete_market::market_for_site(state, destination);
	if(export_market && customs_duty_applies(state, source, destination, true)) {
		auto rate = economy::effective_tariff_export_rate(state, exporter, export_market);
		(void)governance::finance::assess_and_collect_tax_at_rate(state, exporter, seller,
			declared_value, rate, seller_wallet, seller_deposit, date);
	}
	if(import_market && customs_duty_applies(state, source, destination, false)) {
		auto rate = economy::effective_tariff_import_rate(state, importer, import_market);
		(void)governance::finance::assess_and_collect_tax_at_rate(state, importer, buyer,
			declared_value, rate, buyer_wallet, buyer_deposit, date);
	}
}

dcon::commodity_id settlement_for_purchase(sys::state const& state, dcon::economic_actor_id buyer) {
	return accounts::first_settlement_for(state, buyer);
}

purchase_quote quote_purchase_with_account(sys::state const& state,
	dcon::economic_actor_id seller, dcon::monetary_account_id buyer_account,
	float seller_amount, float max_source_amount) {
	purchase_quote result;
	if(!seller || !buyer_account || !std::isfinite(seller_amount) || seller_amount <= 0.0f
		|| !std::isfinite(max_source_amount) || max_source_amount <= 0.0f) return result;
	auto buyer = accounts::owner_of(state, buyer_account);
	auto source_currency = accounts::settlement_of(state, buyer_account);
	if(!buyer || buyer == seller || !source_currency) return result;
	result.seller_account = accounts::find_account(state, seller, source_currency);
	result.settlement = source_currency;
	if(result.seller_account) {
		result.seller_amount = seller_amount;
		result.buyer_source_amount = seller_amount;
		result.available = seller_amount <= max_source_amount + 1.0e-5f;
		return result;
	}
	result.settlement = accounts::first_settlement_for(state, seller);
	if(!result.settlement || result.settlement == source_currency) return {};
	result.seller_account = accounts::find_account(state, seller, result.settlement);
	if(!result.seller_account) return {};
	auto quote = foreign_exchange::quote_conversion(state, source_currency,
		result.settlement, seller_amount, max_source_amount,
		economy::exact_person_economy::account_ref::from_dcon(buyer_account));
	if(!quote.available) return {};
	result.available = true;
	result.seller_amount = seller_amount;
	result.buyer_source_amount = quote.source_amount;
	result.requires_conversion = true;
	return result;
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
	float quantity, float unit_price, float max_source_amount, sys::date timestamp) {
	if(!site || !commodity || !seller || !buyer || seller == buyer || !buyer_account || accounts::owner_of(state, buyer_account) != buyer || !std::isfinite(quantity) || quantity <= 0.0f || !std::isfinite(unit_price) || unit_price <= 0.0f)
		return {};
	auto cost = quantity * unit_price;
	if(!std::isfinite(cost) || inventory::quantity(state, site, commodity, seller) < quantity)
		return {};
	auto payment = quote_purchase_with_account(state, seller, buyer_account, cost,
		max_source_amount);
	if(!payment.available || !payment.seller_account
		|| !state.world.commodity_is_valid(payment.settlement)) return {};
	auto payment_account = buyer_account;
	if(payment.requires_conversion) {
		payment_account = accounts::find_account(state, buyer, payment.settlement);
		if(!payment_account) payment_account = accounts::open_account(state, buyer, payment.settlement);
		if(!payment_account) return {};
		auto payment_position = foreign_exchange::account_position(state,
			economy::exact_person_economy::account_ref::from_dcon(payment_account));
		if(payment_position.reserved > payment_position.balance + 1.0e-5f) return {};
		auto converted = foreign_exchange::convert(state,
			economy::exact_person_economy::account_ref::from_dcon(buyer_account),
			economy::exact_person_economy::account_ref::from_dcon(payment_account),
			cost, max_source_amount, timestamp);
		if(!converted.available) return {};
	}
	if(accounts::balance(state, payment_account) < cost) return {};
	if(!inventory::transfer(state, site, commodity, seller, buyer, quantity)) return {};
	auto transaction = accounts::transfer(state, payment_account, payment.seller_account, cost,
		relations::transaction_kind::purchase, timestamp);
	if(transaction) {
		assess_resource_royalty(state, site, commodity, seller, cost,
			economy::exact_person_economy::account_ref::from_dcon(payment.seller_account), {}, timestamp);
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
