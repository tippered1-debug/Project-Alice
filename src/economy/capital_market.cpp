#include "capital_market.hpp"

#include "system_state.hpp"
#include "actors/organizations/organizations.hpp"
#include "actors/ownership.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/banking/banking.hpp"
#include "economy/dividends.hpp"
#include "economy/exact_person_economy.hpp"
#include "economy/households.hpp"
#include "economy/liquidity.hpp"
#include "economy/physical/concrete_market.hpp"
#include "economy/physical/factory_inputs.hpp"
#include "persons/persons.hpp"
#include "world/site.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace economy::capital_market {
namespace {
constexpr float epsilon = 1.0e-4f;
using actors::ownership::actor_kind;

actor_kind kind_of(sys::state const& state, dcon::economic_actor_id actor) {
	return actor && state.world.economic_actor_is_valid(actor)
		? actor_kind(state.world.economic_actor_get_kind(actor)) : actor_kind::invalid;
}

bool saves_in_bank(actor_kind kind) {
	return kind == actor_kind::person || kind == actor_kind::household;
}

bool invests_from_operations(actor_kind kind) {
	return kind == actor_kind::company || kind == actor_kind::fund || kind == actor_kind::cooperative;
}

float free_deposit(sys::state const& state, dcon::deposit_account_id deposit) {
	if(!deposit) return 0.0f;
	auto bank = state.world.deposit_account_get_organization_from_deposit_account_bank(deposit);
	// Savings at an insolvent bank cannot be committed.
	if(economy::banking::status_of(state, bank) == economy::banking::bank_status::insolvent) return 0.0f;
	auto result = economy::banking::deposit_balance(state, deposit)
		- economy::physical::concrete_market::reserved_deposit_amount(state, deposit);
	return std::isfinite(result) ? std::max(0.0f, result) : 0.0f;
}
}

dcon::commodity_id settlement_of(sys::state const& state, dcon::economic_actor_id actor) {
	if(!actor) return {};
	if(auto settlement = economy::accounts::first_settlement_for(state, actor)) return settlement;
	if(auto profile = state.world.economic_actor_get_person_from_person_actor(actor)) {
		auto key = persons::canonical_key(state, profile);
		if(key.source_population_cell != 0)
			for(auto account : economy::exact_person_economy::accounts_for_person(state, key))
				if(auto settlement = economy::exact_person_economy::settlement_of(state, account)) return settlement;
	}
	dcon::commodity_id result{};
	state.world.economic_actor_for_each_deposit_account_owner_as_economic_actor(actor, [&](auto relation) {
		auto deposit = state.world.deposit_account_owner_get_deposit_account(relation);
		if(!result) result = state.world.deposit_account_get_commodity_from_deposit_account_settlement(deposit);
	});
	return result;
}

dcon::nation_id nation_of(sys::state const& state, dcon::economic_actor_id actor, dcon::commodity_id settlement) {
	if(auto nation = economy::liquidity::nation_for(state, actor)) return nation;
	auto deposit = economy::banking::deposit_account_for(state, actor, settlement);
	auto bank = deposit ? state.world.deposit_account_get_organization_from_deposit_account_bank(deposit) : dcon::organization_id{};
	return bank ? state.world.organization_get_bank_jurisdiction(bank) : dcon::nation_id{};
}

float required_return(sys::state const& state, dcon::economic_actor_id actor, dcon::commodity_id settlement) {
	auto deposit = economy::banking::deposit_account_for(state, actor, settlement);
	auto bank = deposit ? state.world.deposit_account_get_organization_from_deposit_account_bank(deposit)
		: economy::liquidity::bank_for(state, nation_of(state, actor, settlement), settlement);
	if(!bank) return default_required_return;
	auto rate = state.world.organization_get_lending_base_rate(bank);
	return std::isfinite(rate) ? std::max(0.0f, rate) + equity_risk_premium : default_required_return;
}

float liquid_funds(sys::state const& state, dcon::economic_actor_id actor, dcon::commodity_id settlement) {
	auto wallet = wallets::account_for(state, actor, settlement);
	return wallets::spendable(state, wallet)
		+ free_deposit(state, economy::banking::deposit_account_for(state, actor, settlement));
}

float investable_funds(sys::state const& state, dcon::economic_actor_id actor, dcon::commodity_id settlement) {
	auto kind = kind_of(state, actor);
	float free = 0.0f;
	if(saves_in_bank(kind)) {
		free = free_deposit(state, economy::banking::deposit_account_for(state, actor, settlement));
	} else if(invests_from_operations(kind)) {
		auto organization = actors::organizations::organization_for_actor(state, actor);
		free = liquid_funds(state, actor, settlement)
			- (organization ? economy::dividends::operating_reserve(state, organization) : 0.0f);
	}
	auto result = std::max(0.0f, free) * (1.0f - liquidity_preference);
	return std::isfinite(result) ? result : 0.0f;
}

bool pay(sys::state& state, dcon::economic_actor_id payer, dcon::commodity_id settlement,
	wallets::account_ref destination, float amount, relations::transaction_kind kind) {
	if(!payer || !destination || !std::isfinite(amount) || amount <= 0.0f) return false;
	auto deposit = economy::banking::deposit_account_for(state, payer, settlement);
	auto wallet = wallets::account_for(state, payer, settlement);
	auto from_deposit = std::min(amount, free_deposit(state, deposit));
	auto from_wallet = amount - from_deposit;
	if(from_wallet > wallets::spendable(state, wallet) + epsilon) return false;
	if(from_deposit > epsilon && !economy::banking::pay_from_deposit(state, deposit, {}, destination,
		from_deposit, kind, state.current_date)) {
		// The bank could not pay out; fall back to the wallet alone.
		from_wallet = amount;
		if(from_wallet > wallets::spendable(state, wallet) + epsilon) return false;
	}
	if(from_wallet > epsilon && !wallets::pay(state, wallet, destination, std::min(from_wallet, wallets::spendable(state, wallet)), kind))
		return false;
	return true;
}

pool build_pool(sys::state const& state) {
	std::map<std::pair<uint32_t, uint32_t>, bool> candidates;
	state.world.for_each_deposit_account([&](dcon::deposit_account_id deposit) {
		auto owner = state.world.deposit_account_get_economic_actor_from_deposit_account_owner(deposit);
		auto settlement = state.world.deposit_account_get_commodity_from_deposit_account_settlement(deposit);
		if(owner && settlement) candidates[{owner.index(), settlement.index()}] = true;
	});
	state.world.for_each_monetary_account([&](dcon::monetary_account_id account) {
		auto owner = economy::accounts::owner_of(state, account);
		if(!invests_from_operations(kind_of(state, owner))) return;
		auto settlement = economy::accounts::settlement_of(state, account);
		if(settlement && economy::accounts::find_account(state, owner, settlement) == account)
			candidates[{owner.index(), settlement.index()}] = true;
	});
	pool result;
	for(auto const& [key, unused] : candidates) {
		(void)unused;
		auto actor = dcon::economic_actor_id{ dcon::economic_actor_id::value_base_t(key.first) };
		auto settlement = dcon::commodity_id{ dcon::commodity_id::value_base_t(key.second) };
		auto investable = investable_funds(state, actor, settlement);
		if(investable <= epsilon) continue;
		result.investors.push_back({ actor, nation_of(state, actor, settlement), settlement, investable,
			required_return(state, actor, settlement) });
	}
	return result;
}

void commit(pool& investors, dcon::economic_actor_id actor, dcon::commodity_id settlement, float amount) {
	for(auto& entry : investors.investors)
		if(entry.actor == actor && entry.settlement == settlement)
			entry.investable = std::max(0.0f, entry.investable - amount);
}

float expected_daily_profit(sys::state const& state, dcon::factory_id factory) {
	if(!factory || !state.world.factory_is_valid(factory)) return 0.0f;
	auto type = state.world.factory_get_building_type(factory);
	auto market = physical::concrete_market::market_for_site(state,
		world::site::site_for_factory(state, factory));
	if(!type || !market) return 0.0f;
	auto capacity = std::max(0.0f, state.world.factory_get_productive_capacity(factory));
	auto output = state.world.factory_type_get_output(type);
	auto price = physical::concrete_market::canonical_reference_price(state, market, output,
		state.current_date, 0.0f);
	auto output_units = std::max(0.0f, state.world.factory_type_get_output_amount(type)) * capacity;
	auto sell_through = state.world.factory_get_agency_expected_sell_through(factory);
	if(!std::isfinite(sell_through) || sell_through <= epsilon) sell_through = 0.65f;
	float materials = 0.0f;
	auto const& inputs = state.world.factory_type_get_inputs(type);
	for(uint32_t i = 0; i < economy::commodity_set::set_size; ++i) {
		auto commodity = inputs.commodity_type[i];
		if(!commodity) break;
		if(physical::factory_inputs::ordinary_physical_input(state, commodity))
			materials += std::max(0.0f, inputs.commodity_amounts[i]) * capacity
				* physical::concrete_market::canonical_reference_price(state, market, commodity,
					state.current_date, 0.0f);
	}
	auto revenue = output_units * std::max(0.0f, price) * std::clamp(sell_through, 0.05f, 1.0f);
	auto wages = exact_person_economy::wage_due_for_factory(state, factory);
	if(wages <= epsilon) wages = revenue * 0.25f;
	auto observed = state.world.factory_get_agency_recent_profit(factory);
	auto result = std::isfinite(observed) && std::abs(observed) > epsilon
		? observed * 0.5f + (revenue - materials - wages) * 0.5f
		: revenue - materials - wages;
	return std::isfinite(result) ? result : 0.0f;
}

float expected_annual_earnings(sys::state const& state, dcon::organization_id organization) {
	float result = 0.0f;
	for(auto factory : actors::organizations::factories_operated_by(state, organization))
		if(state.world.factory_get_agency_lifecycle_status(factory) < 2)
			result += expected_daily_profit(state, factory) * 365.0f;
	return std::isfinite(result) ? result : 0.0f;
}

float book_value(sys::state const& state, dcon::organization_id organization) {
	if(!organization) return 0.0f;
	auto result = state.world.organization_get_paid_in_equity(organization)
		+ state.world.organization_get_retained_earnings(organization);
	return std::isfinite(result) ? std::max(0.0f, result) : 0.0f;
}

float earnings_yield(offering const& terms) {
	auto value = std::max(0.0f, terms.pre_money) + std::max(0.0f, terms.amount);
	auto result = value > epsilon ? terms.annual_earnings / value : 0.0f;
	return std::isfinite(result) ? result : 0.0f;
}

std::vector<subscription> subscriptions(sys::state const& state, pool const& investors, offering const& terms) {
	std::vector<subscription> result;
	if(!terms.settlement || !std::isfinite(terms.amount) || terms.amount <= epsilon) return result;
	// A company still to be founded can be planned for before it exists.
	auto issuer = terms.issuer ? actors::organizations::actor_for_organization(state, terms.issuer) : dcon::economic_actor_id{};
	auto yield = earnings_yield(terms);
	double capacity = 0.0;
	for(uint32_t i = 0; i < investors.investors.size(); ++i) {
		auto const& entry = investors.investors[i];
		if((issuer && entry.actor == issuer) || entry.settlement != terms.settlement
			|| (terms.nation && entry.nation != terms.nation)
			|| entry.investable <= epsilon || entry.required_return > yield) continue;
		auto offer = entry.investable * concentration_limit;
		result.push_back({ i, offer });
		capacity += double(offer);
	}
	if(capacity <= double(epsilon)) return {};
	auto taken = std::min(double(terms.amount), capacity);
	for(auto& entry : result) entry.amount = float(double(entry.amount) * taken / capacity);
	std::erase_if(result, [](subscription const& entry) { return entry.amount <= epsilon; });
	return result;
}

float total(std::vector<subscription> const& planned) {
	double result = 0.0;
	for(auto const& entry : planned) result += double(entry.amount);
	return float(result);
}

float execute(sys::state& state, pool& investors, offering const& terms, std::vector<subscription> const& planned) {
	auto issuer = actors::organizations::actor_for_organization(state, terms.issuer);
	auto equity = actors::organizations::equity_asset_for_organization(state, terms.issuer);
	auto destination = issuer ? wallets::open_for(state, issuer, terms.settlement) : wallets::account_ref{};
	if(!equity || !destination) return 0.0f;
	// Every subscriber buys at the same price: the pre-money value grows by
	// each subscription already issued.
	auto pre_money = std::max(0.0f, terms.pre_money);
	float raised = 0.0f;
	for(auto const& entry : planned) {
		if(entry.investor >= investors.investors.size()) continue;
		auto& buyer = investors.investors[entry.investor];
		auto amount = std::min(entry.amount, buyer.investable);
		if(amount <= epsilon || !pay(state, buyer.actor, terms.settlement, destination, amount,
			relations::transaction_kind::equity_contribution)) continue;
		actors::ownership::assign_runtime_canonical_id(state, buyer.actor);
		if(!actors::ownership::issue_equity(state, equity, buyer.actor, amount, pre_money)) std::abort();
		pre_money += amount;
		buyer.investable = std::max(0.0f, buyer.investable - amount);
		raised += amount;
	}
	return raised;
}

float raise(sys::state& state, pool& investors, offering const& terms, float minimum) {
	auto planned = subscriptions(state, investors, terms);
	if(planned.empty() || total(planned) + epsilon < minimum) return 0.0f;
	return execute(state, investors, terms, planned);
}

bool transfer_stake(sys::state& state, dcon::ownership_stake_id from, dcon::economic_actor_id to, float fraction) {
	if(!from || !state.world.ownership_stake_is_valid(from) || !to || !std::isfinite(fraction) || fraction <= 0.0f) return false;
	auto asset = state.world.ownership_stake_get_asset_from_ownership_stake_asset(from);
	auto held = state.world.ownership_stake_get_economic_fraction(from);
	if(!asset || fraction > held + 1.0e-6f) return false;
	fraction = std::min(fraction, held);
	auto share = held > 0.0f ? fraction / held : 1.0f;
	auto ownership = state.world.ownership_stake_get_ownership_fraction(from) * share;
	auto voting = state.world.ownership_stake_get_voting_fraction(from) * share;
	dcon::ownership_stake_id target{};
	state.world.asset_for_each_ownership_stake_asset_as_asset(asset, [&](dcon::ownership_stake_asset_id relation) {
		auto stake = state.world.ownership_stake_asset_get_ownership_stake(relation);
		if(state.world.ownership_stake_get_economic_actor_from_ownership_stake_owner(stake) == to) target = stake;
	});
	actors::ownership::assign_runtime_canonical_id(state, to);
	// The seller gives up its fractions first: an asset never holds more than 1.
	auto seller_ownership = state.world.ownership_stake_get_ownership_fraction(from);
	auto seller_voting = state.world.ownership_stake_get_voting_fraction(from);
	if(!actors::ownership::set_stake_fractions(state, from, std::max(0.0f, seller_ownership - ownership),
		std::max(0.0f, seller_voting - voting), std::max(0.0f, held - fraction))) return false;
	auto restore = [&]() { (void)actors::ownership::set_stake_fractions(state, from, seller_ownership, seller_voting, held); };
	if(!target) {
		target = actors::ownership::create_stake(state, to, asset, 0.0f, 0.0f, 0.0f);
		if(!target) {
			restore();
			return false;
		}
		actors::ownership::assign_runtime_canonical_id(state, target);
	}
	if(!actors::ownership::set_stake_fractions(state, target,
		std::min(1.0f, state.world.ownership_stake_get_ownership_fraction(target) + ownership),
		std::min(1.0f, state.world.ownership_stake_get_voting_fraction(target) + voting),
		std::min(1.0f, state.world.ownership_stake_get_economic_fraction(target) + fraction))) {
		restore();
		return false;
	}
	if(fraction + 1.0e-6f >= held) state.world.delete_ownership_stake(from);
	return true;
}

float trade_shares(sys::state& state, pool& investors) {
	auto day = state.current_date.to_raw_value();
	std::vector<dcon::ownership_stake_id> due;
	state.world.for_each_ownership_stake([&](dcon::ownership_stake_id stake) {
		if((day + int32_t(stake.index())) % share_review_days == 0) due.push_back(stake);
	});
	float traded = 0.0f;
	for(auto stake : due) {
		if(!state.world.ownership_stake_is_valid(stake)) continue;
		auto asset = state.world.ownership_stake_get_asset_from_ownership_stake_asset(stake);
		auto issuer = asset ? state.world.asset_get_organization_from_organization_equity_asset(asset) : dcon::organization_id{};
		auto holder = state.world.ownership_stake_get_economic_actor_from_ownership_stake_owner(stake);
		auto kind = kind_of(state, holder);
		// Only firms' shares trade, and only private holders trade them.
		if(!issuer || households::is_household(state, issuer)
			|| kind_of(state, actors::organizations::actor_for_organization(state, issuer)) != actor_kind::company
			|| !(saves_in_bank(kind) || invests_from_operations(kind))) continue;
		auto settlement = settlement_of(state, holder);
		auto nation = nation_of(state, holder, settlement);
		auto held = state.world.ownership_stake_get_economic_fraction(stake);
		auto earnings = expected_annual_earnings(state, issuer);
		if(!settlement || !nation || !(held > 0.0f) || !(earnings > 0.0f)) continue;
		auto seller_required = required_return(state, holder, settlement);
		auto in_need = liquid_funds(state, holder, settlement) < cash_need_threshold;
		auto issuer_actor = actors::organizations::actor_for_organization(state, issuer);
		investor const* buyer = nullptr;
		for(auto const& entry : investors.investors) {
			if(entry.actor == holder || entry.actor == issuer_actor || entry.settlement != settlement || entry.nation != nation
				|| entry.investable <= epsilon || !(entry.required_return > 0.0f)) continue;
			if(!in_need && entry.required_return + 1.0e-6f >= seller_required) continue;
			if(!buyer || entry.required_return < buyer->required_return) buyer = &entry;
		}
		if(!buyer) continue;
		// Value of the whole firm to each side.
		auto buyer_value = earnings / buyer->required_return;
		auto seller_value = seller_required > 0.0f ? earnings / seller_required : 0.0f;
		auto price_per_unit = in_need ? buyer_value * (1.0f - distressed_sale_discount) : 0.5f * (buyer_value + seller_value);
		if(!(price_per_unit > 0.0f) || !std::isfinite(price_per_unit)) continue;
		auto fraction = std::min(held, buyer->investable * concentration_limit / price_per_unit);
		auto price = fraction * price_per_unit;
		auto destination = wallets::open_for(state, holder, settlement);
		if(price <= epsilon || !destination) continue;
		auto buyer_actor = buyer->actor;
		if(!pay(state, buyer_actor, settlement, destination, price, relations::transaction_kind::purchase)) continue;
		if(!transfer_stake(state, stake, buyer_actor, fraction)) std::abort();
		commit(investors, buyer_actor, settlement, price);
		traded += price;
	}
	return traded;
}

} // namespace economy::capital_market
