#pragma once

#include "dcon_generated.hpp"
#include "economy/exact_person_economy.hpp"
#include "economy/foreign_exchange.hpp"
#include <vector>

namespace sys { class state; }

namespace economy::physical::exchange {

struct purchase_quote {
	bool available = false;
	dcon::monetary_account_id seller_account{};
	dcon::commodity_id settlement{};
	float seller_amount = 0.0f;
	float buyer_source_amount = 0.0f;
	bool requires_conversion = false;
};

std::vector<dcon::physical_stock_id> seller_stocks(sys::state const&, dcon::site_id,
	dcon::commodity_id, dcon::economic_actor_id buyer);
dcon::transaction_id purchase(sys::state&, dcon::site_id, dcon::commodity_id,
	dcon::economic_actor_id seller, dcon::economic_actor_id buyer, float quantity,
	float unit_price, dcon::commodity_id settlement, sys::date timestamp);
dcon::transaction_id purchase_with_account(sys::state&, dcon::site_id, dcon::commodity_id,
	dcon::economic_actor_id seller, dcon::economic_actor_id buyer,
	dcon::monetary_account_id buyer_account, float quantity, float unit_price,
	float max_source_amount, sys::date timestamp);
purchase_quote quote_purchase_with_account(sys::state const&, dcon::economic_actor_id seller,
	dcon::monetary_account_id buyer_account, float seller_amount, float max_source_amount);
// Buys with a bank deposit: goods move to the buyer, and the seller is paid
// into their deposit at the buyer's bank, into a deposit at another bank, or
// into their operating wallet, in that order of preference.
dcon::transaction_id purchase_with_deposit(sys::state&, dcon::site_id, dcon::commodity_id,
	dcon::economic_actor_id seller, dcon::economic_actor_id buyer, dcon::deposit_account_id buyer_deposit,
	float quantity, float unit_price, sys::date);
dcon::commodity_id settlement_for_purchase(sys::state const&, dcon::economic_actor_id buyer);

// Assesses duties on a completed cross-border purchase and attempts immediate
// payment through each party's actual wallet or bank deposit.
void assess_customs_duties(sys::state&, dcon::economic_actor_id buyer,
	dcon::economic_actor_id seller, dcon::site_id source, dcon::site_id destination,
	dcon::commodity_id commodity, float declared_value,
	economy::exact_person_economy::account_ref buyer_wallet,
	dcon::deposit_account_id buyer_deposit,
	economy::exact_person_economy::account_ref seller_wallet,
	dcon::deposit_account_id seller_deposit, sys::date date);

} // namespace economy::physical::exchange
