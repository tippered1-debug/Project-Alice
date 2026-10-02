#include "monetary_policy.hpp"

#include "system_state.hpp"
#include "actors/organizations/organizations.hpp"
#include "actors/ownership.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/banking/banking.hpp"
#include "economy/physical/concrete_market.hpp"
#include "economy/physical/exact_person_goods.hpp"
#include "economy/relations/relations.hpp"
#include "economy/wallets.hpp"
#include "governance/governance.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace economy::monetary_policy {
namespace {
constexpr float epsilon = 1.0e-4f;
using actors::ownership::actor_kind;

uint64_t loan_id(uint64_t bank, uint32_t date, uint32_t sequence) {
	uint64_t hash = 1469598103934665603ULL;
	for(uint64_t value : { 0x646973636f756e74ULL, bank, uint64_t(date), uint64_t(sequence) }) {
		hash ^= value;
		hash *= 1099511628211ULL;
	}
	return hash == 0 ? 1 : hash;
}

bool configured_bank_of(sys::state const& state, dcon::organization_id bank, dcon::nation_id nation, dcon::commodity_id settlement) {
	return state.world.organization_get_kind(bank) == uint8_t(actor_kind::bank)
		&& state.world.organization_get_bank_policy_configured(bank) != 0
		&& state.world.organization_get_bank_jurisdiction(bank) == nation
		&& state.world.organization_get_bank_settlement_currency(bank) == settlement;
}

std::vector<dcon::obligation_id> discount_loans(sys::state const& state, dcon::organization_id bank) {
	std::vector<dcon::obligation_id> result;
	auto actor = actors::organizations::actor_for_organization(state, bank);
	state.world.economic_actor_for_each_obligation_debtor_as_economic_actor(actor, [&](dcon::obligation_debtor_id relation) {
		auto loan = state.world.obligation_debtor_get_obligation(relation);
		if(!loan || state.world.obligation_get_status(loan) != uint8_t(relations::obligation_status::active)
			|| state.world.obligation_get_kind(loan) != uint8_t(relations::obligation_kind::loan)) return;
		auto creditor = actors::organizations::organization_for_actor(state,
			state.world.obligation_get_economic_actor_from_obligation_creditor(loan));
		if(is_central_bank(state, creditor)) result.push_back(loan);
	});
	std::sort(result.begin(), result.end(), [](auto a, auto b) { return a.index() < b.index(); });
	return result;
}

void set_issued(sys::state& state, dcon::organization_id central_bank, float amount) {
	state.world.organization_set_central_bank_issued(central_bank, std::max(0.0f, amount));
}
}

bool is_central_bank(sys::state const& state, dcon::organization_id organization) {
	return organization && state.world.organization_is_valid(organization)
		&& state.world.organization_get_kind(organization) == uint8_t(actor_kind::state_entity)
		&& state.world.organization_get_bank_jurisdiction(organization);
}

dcon::organization_id central_bank_for(sys::state const& state, dcon::nation_id nation, dcon::commodity_id settlement) {
	dcon::organization_id result{};
	if(!nation || !settlement) return result;
	state.world.for_each_organization([&](dcon::organization_id organization) {
		if(!result && is_central_bank(state, organization)
			&& state.world.organization_get_bank_jurisdiction(organization) == nation
			&& state.world.organization_get_bank_settlement_currency(organization) == settlement)
			result = organization;
	});
	return result;
}

dcon::organization_id open_central_bank(sys::state& state, dcon::nation_id nation, dcon::commodity_id settlement) {
	if(auto existing = central_bank_for(state, nation, settlement)) return existing;
	if(!nation || !settlement) return {};
	double rates = 0.0;
	int32_t banks = 0;
	state.world.for_each_organization([&](dcon::organization_id bank) {
		if(!configured_bank_of(state, bank, nation, settlement)) return;
		rates += double(state.world.organization_get_lending_base_rate(bank));
		++banks;
	});
	auto central_bank = actors::organizations::create_organization(state, actor_kind::state_entity);
	if(!central_bank) return {};
	actors::ownership::assign_runtime_canonical_id(state, central_bank);
	auto actor = actors::organizations::actor_for_organization(state, central_bank);
	actors::ownership::assign_runtime_canonical_id(state, actor);
	state.world.organization_set_bank_jurisdiction(central_bank, nation);
	state.world.organization_set_bank_settlement_currency(central_bank, settlement);
	state.world.organization_set_lending_base_rate(central_bank,
		banks > 0 ? float(rates / double(banks)) : neutral_real_rate + inflation_target);
	state.world.organization_set_central_bank_price_index(central_bank, price_index(state, nation));
	state.world.organization_set_central_bank_inflation(central_bank, 0.0f);
	set_issued(state, central_bank, 0.0f);
	// The central bank belongs to the state: its income is public income.
	auto government = governance::actor_for_institution(state, governance::central_government_for(state, nation));
	if(auto equity = actors::organizations::equity_asset_for_organization(state, central_bank); equity && government)
		(void)actors::ownership::create_stake(state, government, equity, 1.0f, 1.0f, 1.0f);
	(void)accounts::open_account(state, actor, settlement);
	return central_bank;
}

float policy_rate(sys::state const& state, dcon::organization_id central_bank) {
	return central_bank ? state.world.organization_get_lending_base_rate(central_bank) : 0.0f;
}

float issued_money(sys::state const& state, dcon::organization_id central_bank) {
	return central_bank ? state.world.organization_get_central_bank_issued(central_bank) : 0.0f;
}

float price_index(sys::state const& state, dcon::nation_id nation) {
	if(!nation) return 1.0f;
	std::unordered_set<uint32_t> markets;
	state.world.for_each_province([&](dcon::province_id province) {
		if(state.world.province_get_nation_from_province_ownership(province) != nation) return;
		auto zone = state.world.province_get_state_membership(province);
		auto market = zone ? state.world.state_instance_get_market_from_local_market(zone) : dcon::market_id{};
		if(market) markets.insert(market.index());
	});
	std::unordered_map<uint64_t, float> demand;
	auto from = state.current_date - review_period_days;
	state.world.for_each_concrete_market_bid([&](dcon::concrete_market_bid_id bid) {
		auto created = state.world.concrete_market_bid_get_created_on(bid);
		auto market = state.world.concrete_market_bid_get_market_from_concrete_bid_market(bid);
		auto commodity = state.world.concrete_market_bid_get_commodity_from_concrete_bid_commodity(bid);
		if(created <= from || state.current_date < created || !market || !commodity || !markets.contains(market.index())) return;
		demand[(uint64_t(market.index()) << 32) | uint64_t(commodity.index())]
			+= std::max(0.0f, state.world.concrete_market_bid_get_original_quantity(bid));
	});
	physical::exact_person_goods::add_submitted_demand(state, from, state.current_date, demand);
	double current = 0.0, base = 0.0;
	for(auto const& [key, quantity] : demand) {
		auto market = dcon::market_id{ dcon::market_id::value_base_t(key >> 32) };
		auto commodity = dcon::commodity_id{ dcon::commodity_id::value_base_t(key & 0xffffffffULL) };
		auto cost = state.world.commodity_get_cost(commodity);
		if(!markets.contains(market.index()) || !(cost > 0.0f) || !(quantity > 0.0f)) continue;
		current += double(quantity) * double(physical::concrete_market::canonical_reference_price(state, market, commodity, state.current_date, 0.0f));
		base += double(quantity) * double(cost);
	}
	auto result = base > 0.0 ? float(current / base) : 1.0f;
	return std::isfinite(result) && result > 0.0f ? result : 1.0f;
}

float liquidity_premium(sys::state const& state, dcon::organization_id bank) {
	auto sheet = economy::banking::bank_balance_sheet(state, bank, state.world.organization_get_bank_settlement_currency(bank));
	if(!(sheet.required_liquidity > epsilon)) return 0.0f;
	auto coverage = sheet.settlement_assets / sheet.required_liquidity;
	auto result = maximum_liquidity_premium * std::clamp((2.0f - coverage) / 2.0f, 0.0f, 1.0f);
	return std::isfinite(result) ? result : maximum_liquidity_premium;
}

float deposit_rate(sys::state const& state, dcon::organization_id bank) {
	auto settlement = state.world.organization_get_bank_settlement_currency(bank);
	auto sheet = economy::banking::bank_balance_sheet(state, bank, settlement);
	if(!(sheet.deposit_liabilities > epsilon)) return 0.0f;
	double income = 0.0;
	state.world.economic_actor_for_each_obligation_creditor_as_economic_actor(
		actors::organizations::actor_for_organization(state, bank), [&](dcon::obligation_creditor_id relation) {
			auto loan = state.world.obligation_creditor_get_obligation(relation);
			if(!loan || state.world.obligation_get_kind(loan) != uint8_t(relations::obligation_kind::loan)
				|| state.world.obligation_get_status(loan) != uint8_t(relations::obligation_status::active)
				|| state.world.obligation_get_settlement_commodity(loan) != settlement) return;
			income += double(state.world.obligation_get_annual_interest_rate(loan))
				* double(state.world.obligation_get_principal_outstanding(loan));
		});
	auto yield = float(income / double(sheet.deposit_liabilities));
	auto result = std::clamp(deposit_pass_through * yield, 0.0f, std::max(0.0f, state.world.organization_get_lending_base_rate(bank)));
	return std::isfinite(result) ? result : 0.0f;
}

void review(sys::state& state, dcon::organization_id central_bank) {
	if(!is_central_bank(state, central_bank)) return;
	auto nation = state.world.organization_get_bank_jurisdiction(central_bank);
	auto settlement = state.world.organization_get_bank_settlement_currency(central_bank);
	auto index = price_index(state, nation);
	auto previous = state.world.organization_get_central_bank_price_index(central_bank);
	auto inflation = state.world.organization_get_central_bank_inflation(central_bank);
	if(previous > 0.0f && index > 0.0f) {
		auto annual = std::clamp(std::pow(index / previous, 365.0f / float(review_period_days)) - 1.0f, -0.9f, 5.0f);
		if(std::isfinite(annual)) inflation += inflation_smoothing * (annual - inflation);
	}
	state.world.organization_set_central_bank_price_index(central_bank, index);
	state.world.organization_set_central_bank_inflation(central_bank, inflation);
	auto rule = std::clamp(neutral_real_rate + inflation + inflation_response * (inflation - inflation_target),
		0.0f, maximum_policy_rate);
	auto current = policy_rate(state, central_bank);
	auto rate = std::clamp(current + rate_smoothing * (rule - current), 0.0f, maximum_policy_rate);
	state.world.organization_set_lending_base_rate(central_bank, rate);
	state.world.for_each_organization([&](dcon::organization_id bank) {
		if(!configured_bank_of(state, bank, nation, settlement)) return;
		auto lending = std::min(rate + liquidity_premium(state, bank),
			1.0f - state.world.organization_get_bank_lending_spread(bank));
		(void)economy::banking::set_bank_lending_base_rate(state, bank, std::max(0.0f, lending));
	});
	// The central bank's interest income is the government's.
	auto actor = actors::organizations::actor_for_organization(state, central_bank);
	auto income = wallets::account_for(state, actor, settlement);
	auto treasury = wallets::open_for(state, governance::actor_for_institution(state, governance::central_government_for(state, nation)), settlement);
	auto amount = wallets::spendable(state, income);
	if(income && treasury && amount > epsilon)
		(void)wallets::pay(state, income, treasury, amount, relations::transaction_kind::public_spending);
}

float lend_reserves(sys::state& state, dcon::organization_id bank) {
	if(state.world.organization_get_kind(bank) != uint8_t(actor_kind::bank)
		|| state.world.organization_get_bank_policy_configured(bank) == 0
		|| economy::banking::status_of(state, bank) == economy::banking::bank_status::insolvent) return 0.0f;
	auto nation = state.world.organization_get_bank_jurisdiction(bank);
	auto settlement = state.world.organization_get_bank_settlement_currency(bank);
	auto reserve = economy::banking::reserve_account_for(state, bank, settlement);
	auto sheet = economy::banking::bank_balance_sheet(state, bank, settlement);
	if(!reserve || !(sheet.net_worth > 0.0f)) return 0.0f;
	auto shortfall = sheet.required_liquidity - sheet.settlement_assets;
	if(!(shortfall > epsilon)) return 0.0f;
	float borrowed = 0.0f;
	for(auto loan : discount_loans(state, bank)) borrowed += relations::total_due(state, loan);
	auto amount = std::min(shortfall, discount_collateral_share * sheet.loan_assets - borrowed);
	if(!(amount > epsilon)) return 0.0f;
	auto central_bank = open_central_bank(state, nation, settlement);
	auto lender = actors::organizations::actor_for_organization(state, central_bank);
	auto borrower = actors::organizations::actor_for_organization(state, bank);
	if(!central_bank || !lender || !borrower) return 0.0f;
	auto loan = relations::create_obligation(state, borrower, lender, amount, settlement, state.current_date,
		state.current_date + discount_term_days, policy_rate(state, central_bank) + discount_penalty,
		relations::obligation_kind::loan);
	if(!loan) return 0.0f;
	uint32_t sequence = 0;
	auto id = loan_id(state.world.organization_get_canonical_id(bank), uint32_t(state.current_date.to_raw_value()), sequence);
	bool taken = true;
	while(taken) {
		taken = false;
		state.world.for_each_obligation([&](dcon::obligation_id existing) {
			if(existing != loan && state.world.obligation_get_canonical_id(existing) == id) taken = true;
		});
		if(taken) id = loan_id(state.world.organization_get_canonical_id(bank), uint32_t(state.current_date.to_raw_value()), ++sequence);
	}
	state.world.obligation_set_canonical_id(loan, id);
	state.world.obligation_set_last_interest_accrual_date(loan, state.current_date);
	// New base money: the reserves exist because the central bank created them.
	state.world.monetary_account_set_balance(reserve, accounts::balance(state, reserve) + amount);
	set_issued(state, central_bank, issued_money(state, central_bank) + amount);
	(void)relations::record_transaction(state, lender, borrower, amount, settlement,
		relations::transaction_kind::loan_issuance, state.current_date);
	economy::banking::update_bank_statuses(state, state.current_date);
	return amount;
}

float repay_reserves(sys::state& state, dcon::organization_id bank) {
	auto settlement = state.world.organization_get_bank_settlement_currency(bank);
	auto reserve = economy::banking::reserve_account_for(state, bank, settlement);
	if(!reserve) return 0.0f;
	float repaid = 0.0f;
	for(auto loan : discount_loans(state, bank)) {
		auto last = state.world.obligation_get_last_interest_accrual_date(loan);
		if(last && last < state.current_date) {
			(void)relations::accrue_interest(state, loan, uint32_t(state.current_date.to_raw_value() - last.to_raw_value()));
			state.world.obligation_set_last_interest_accrual_date(loan, state.current_date);
		}
		auto sheet = economy::banking::bank_balance_sheet(state, bank, settlement);
		auto free = std::max(0.0f, sheet.settlement_assets - sheet.required_liquidity);
		auto amount = std::min(relations::total_due(state, loan), free);
		auto central_bank = actors::organizations::organization_for_actor(state,
			state.world.obligation_get_economic_actor_from_obligation_creditor(loan));
		auto lender = actors::organizations::actor_for_organization(state, central_bank);
		if(amount > epsilon) {
			auto interest = std::min(amount, state.world.obligation_get_accrued_interest(loan));
			auto paid = relations::repay_obligation(state, loan, amount);
			auto principal = std::max(0.0f, paid - interest);
			state.world.monetary_account_set_balance(reserve, accounts::balance(state, reserve) - paid);
			// Repaid principal is retired base money; interest is central bank income.
			set_issued(state, central_bank, issued_money(state, central_bank) - principal);
			if(interest > 0.0f) {
				auto income = wallets::open_for(state, lender, settlement);
				if(income) state.world.monetary_account_set_balance(income.dcon_account, accounts::balance(state, income.dcon_account) + interest);
			}
			(void)relations::record_transaction(state, actors::organizations::actor_for_organization(state, bank), lender, paid,
				settlement, relations::transaction_kind::repayment, state.current_date);
			repaid += paid;
		}
		// A solvent bank that still cannot repay at maturity rolls the loan over.
		if(state.world.obligation_get_status(loan) == uint8_t(relations::obligation_status::active)
			&& !(state.current_date < state.world.obligation_get_due_date(loan)))
			state.world.obligation_set_due_date(loan, state.current_date + discount_term_days);
	}
	if(repaid > 0.0f) economy::banking::update_bank_statuses(state, state.current_date);
	return repaid;
}

float pay_deposit_interest(sys::state& state, dcon::deposit_account_id deposit) {
	auto bank = state.world.deposit_account_get_organization_from_deposit_account_bank(deposit);
	auto owner = state.world.deposit_account_get_economic_actor_from_deposit_account_owner(deposit);
	if(!bank || !owner || economy::banking::status_of(state, bank) == economy::banking::bank_status::insolvent) return 0.0f;
	auto balance = economy::banking::deposit_balance(state, deposit);
	auto interest = balance * deposit_rate(state, bank) * float(review_period_days) / 365.0f;
	if(!std::isfinite(interest) || interest <= epsilon) return 0.0f;
	state.world.deposit_account_set_balance(deposit, balance + interest);
	(void)relations::record_transaction(state, actors::organizations::actor_for_organization(state, bank), owner, interest,
		state.world.deposit_account_get_commodity_from_deposit_account_settlement(deposit),
		relations::transaction_kind::interest, state.current_date);
	return interest;
}

void process(sys::state& state) {
	auto day = state.current_date.to_raw_value();
	std::set<std::pair<uint32_t, uint32_t>> currencies;
	std::vector<dcon::organization_id> banks;
	state.world.for_each_organization([&](dcon::organization_id bank) {
		if(state.world.organization_get_kind(bank) != uint8_t(actor_kind::bank)
			|| state.world.organization_get_bank_policy_configured(bank) == 0) return;
		dcon::nation_id nation = state.world.organization_get_bank_jurisdiction(bank);
		dcon::commodity_id settlement = state.world.organization_get_bank_settlement_currency(bank);
		if(nation && settlement) currencies.insert({ nation.index(), settlement.index() });
		banks.push_back(bank);
	});
	for(auto const& [nation, settlement] : currencies) {
		auto central_bank = open_central_bank(state, dcon::nation_id{ dcon::nation_id::value_base_t(nation) },
			dcon::commodity_id{ dcon::commodity_id::value_base_t(settlement) });
		if(central_bank && (day + int32_t(central_bank.index())) % review_period_days == 0) review(state, central_bank);
	}
	std::sort(banks.begin(), banks.end(), [](auto a, auto b) { return a.index() < b.index(); });
	for(auto bank : banks) {
		(void)repay_reserves(state, bank);
		(void)lend_reserves(state, bank);
	}
	std::vector<dcon::deposit_account_id> deposits;
	state.world.for_each_deposit_account([&](dcon::deposit_account_id deposit) {
		if((day + int32_t(deposit.index())) % review_period_days == 0) deposits.push_back(deposit);
	});
	for(auto deposit : deposits) (void)pay_deposit_interest(state, deposit);
	if(!deposits.empty()) economy::banking::update_bank_statuses(state, state.current_date);
}

} // namespace economy::monetary_policy
