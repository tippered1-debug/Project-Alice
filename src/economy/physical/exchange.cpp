#include "exchange.hpp"
#include "economy/banking/banking.hpp"
#include "economy/physical/extraction.hpp"
#include "concrete_market.hpp"
#include "inventory.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/economy_stats.hpp"
#include "economy/households.hpp"
#include "economy/wallets.hpp"
#include "economy/relations/relations.hpp"
#include "governance/finance/finance.hpp"
#include "governance/governance.hpp"
#include "governance/policy.hpp"
#include "governance/public_administration.hpp"
#include "persons/exact_population.hpp"
#include "persons/persons.hpp"
#include "system_state.hpp"
#include "money.hpp"
#include "nations/nations.hpp"

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

bool direct_no_tariffs(sys::state const& state, dcon::nation_id source, dcon::nation_id target) {
	if(!source || !target) return false;
	if(source == target) return true;
	auto relation = state.world.get_unilateral_relationship_by_unilateral_pair(target, source);
	if(relation && state.world.unilateral_relationship_get_no_tariffs_until(relation))
		return true;
	if(state.world.nation_get_in_sphere_of(source) == target) return true;
	auto overlord = state.world.nation_get_overlord_as_subject(source);
	return overlord && state.world.overlord_get_ruler(overlord) == target;
}

bool no_tariffs_between(sys::state const& state, dcon::nation_id source, dcon::nation_id target) {
	auto source_leader = nations::get_market_leader(state, source);
	auto target_leader = nations::get_market_leader(state, target);
	return direct_no_tariffs(state, source, target)
		|| direct_no_tariffs(state, source_leader, target_leader)
		|| direct_no_tariffs(state, source_leader, target)
		|| direct_no_tariffs(state, source, target_leader);
}

bool customs_duty_applies(sys::state const& state, dcon::nation_id exporter,
	dcon::nation_id importer, bool export_duty) {
	return export_duty
		? !no_tariffs_between(state, exporter, importer)
		: !no_tariffs_between(state, importer, exporter);
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
	// Rates are those of each nation's enacted tariff law; without one, nothing is due.
	if(export_market && customs_duty_applies(state, exporter, importer, true))
		(void)governance::finance::assess_and_collect_topic_tax(state,
			governance::policy::topic_id::export_tariff, exporter, seller,
			declared_value, seller_wallet, seller_deposit, date);
	if(import_market && customs_duty_applies(state, exporter, importer, false))
		(void)governance::finance::assess_and_collect_topic_tax(state,
			governance::policy::topic_id::import_tariff, importer, buyer,
			declared_value, buyer_wallet, buyer_deposit, date);
}

void assess_sales_tax(sys::state& state, dcon::economic_actor_id buyer, dcon::site_id destination,
	float purchase_value, economy::exact_person_economy::account_ref buyer_wallet,
	dcon::deposit_account_id buyer_deposit, sys::date date) {
	if(!buyer || !state.world.economic_actor_is_valid(buyer)
		|| !std::isfinite(purchase_value) || purchase_value <= 0.0f) return;
	auto final_consumer = bool(state.world.economic_actor_get_person_from_person_actor(buyer))
		|| economy::households::is_household(state, buyer);
	if(!final_consumer) return;
	auto nation = customs_jurisdiction(state, destination);
	if(!nation) return;
	(void)governance::finance::assess_and_collect_topic_tax(state,
		governance::policy::topic_id::sales_tax, nation, buyer, purchase_value,
		buyer_wallet, buyer_deposit, date);
}

void assess_person_sales_tax(sys::state& state, persons::person_key buyer,
	economy::exact_person_economy::account_ref wallet, dcon::site_id destination,
	float purchase_value, sys::date date) {
	if(!std::isfinite(purchase_value) || purchase_value <= 0.0f || !wallet) return;
	if(auto profile = persons::exact_population::profile_for_person(state, buyer))
		if(auto actor = persons::actor_for_person(state, profile)) {
			assess_sales_tax(state, actor, destination, purchase_value, wallet, {}, date);
			return;
		}
	// A person without an economic profile pays through the household sector,
	// as the wage tax is collected.
	auto nation = customs_jurisdiction(state, destination);
	if(!nation) return;
	auto rate = governance::finance::enacted_rate(state, governance::policy::topic_id::sales_tax, nation, date);
	if(!(rate > 0.0f)) return;
	auto due = purchase_value * rate;
	auto authority = governance::public_administration::tax_authority_for(state, nation);
	auto treasury = governance::public_administration::tax_treasury_for(state, nation);
	auto household_actor = governance::public_administration::household_sector_actor(state, nation);
	auto household_account = governance::public_administration::household_sector_account(state, nation);
	if(!authority || !treasury || !household_actor || !household_account || !(due > 1.0e-6f)) return;
	auto assessment = governance::finance::authorized_assess_tax_by_institution(state, authority,
		household_actor, treasury, due, date, date);
	auto obligation = assessment
		? state.world.fiscal_action_get_obligation_from_fiscal_action_resulting_obligation(assessment)
		: dcon::obligation_id{};
	if(!obligation) return;
	auto paid = std::min(due, economy::wallets::spendable(state, wallet));
	if(paid <= 1.0e-6f || !economy::exact_person_economy::transfer(state, wallet,
		economy::exact_person_economy::account_ref::from_dcon(household_account), paid,
		relations::transaction_kind::tax_payment, date)) return;
	(void)governance::finance::pay_tax(state, obligation, household_account, treasury, paid, date,
		governance::finance::source_of_topic(governance::policy::topic_id::sales_tax));
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
