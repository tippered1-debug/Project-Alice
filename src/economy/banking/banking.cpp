#include "banking.hpp"
#include "persons/persons.hpp"

#include "actors/organizations/organizations.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/consent/consent.hpp"
#include "economy/money/ontology.hpp"
#include "economy/relations/relations.hpp"
#include "system_state.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace economy::banking {

namespace {

using actors::ownership::actor_kind;

bool valid_bank(sys::state const& state, dcon::organization_id bank) {
	if(!bank || !state.world.organization_is_valid(bank)) return false;
	if(state.world.organization_get_kind(bank) != uint8_t(actor_kind::bank)) return false;
	auto actor = actors::organizations::actor_for_organization(state, bank);
	return actor && state.world.economic_actor_get_kind(actor) == uint8_t(actor_kind::bank);
}

bool valid_nonnegative_amount(float amount) {
	return std::isfinite(amount) && amount >= 0.0f;
}

bool valid_positive_amount(float amount) {
	return std::isfinite(amount) && amount > 0.0f;
}

constexpr float balance_tolerance = 1.0e-4f;

uint64_t stable_hash(std::string_view key) {
	uint64_t hash = 14695981039346656037ULL;
	for(unsigned char c : key) {
		hash ^= uint64_t(c);
		hash *= 1099511628211ULL;
	}
	return hash == 0 ? 1 : hash;
}

std::string id_key(char const* kind, uint64_t first, uint64_t second = 0, uint64_t third = 0) {
	return std::string(kind) + ":" + std::to_string(first) + ":"
		+ std::to_string(second) + ":" + std::to_string(third);
}

bool fraction(float value) {
	return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
}

dcon::organization_id bank_for_loan(sys::state const& state, dcon::obligation_id loan) {
	if(!loan || !state.world.obligation_is_valid(loan)) return {};
	return actors::organizations::organization_for_actor(state,
		state.world.obligation_get_economic_actor_from_obligation_creditor(loan));
}

bool valid_active_loan_for_bank(sys::state const& state, dcon::obligation_id loan,
	dcon::organization_id bank) {
	return loan && state.world.obligation_is_valid(loan)
		&& state.world.obligation_get_kind(loan) == uint8_t(relations::obligation_kind::loan)
		&& state.world.obligation_get_status(loan) == uint8_t(relations::obligation_status::active)
		&& bank_for_loan(state, loan) == bank
		&& valid_bank(state, bank);
}

float bank_base_rate(sys::state const& state, dcon::organization_id bank) {
	return state.world.organization_get_lending_base_rate(bank);
}

bool policy_configured(sys::state const& state, dcon::organization_id bank) {
	return valid_bank(state, bank) && state.world.organization_get_bank_policy_configured(bank) != 0
		&& state.world.organization_get_bank_settlement_currency(bank)
		&& state.world.commodity_is_valid(state.world.organization_get_bank_settlement_currency(bank))
		&& state.world.organization_get_bank_jurisdiction(bank)
		&& state.world.nation_is_valid(state.world.organization_get_bank_jurisdiction(bank));
}

float required_reserves(balance_sheet const& sheet, sys::state const& state, dcon::organization_id bank) {
	auto policy_target = std::max(state.world.organization_get_bank_liquidity_target(bank),
		state.world.organization_get_bank_reserve_requirement(bank));
	return sheet.deposit_liabilities * policy_target;
}

void sync_bank_equity(sys::state& state, dcon::organization_id bank) {
	if(!policy_configured(state, bank)
		|| !reserve_account_for(state, bank, state.world.organization_get_bank_settlement_currency(bank))) return;
	auto sheet = bank_balance_sheet(state, bank, state.world.organization_get_bank_settlement_currency(bank));
	if(!std::isfinite(sheet.net_worth)) return;
	auto paid_in = state.world.organization_get_paid_in_equity(bank);
	auto retained = double(sheet.net_worth) - double(paid_in);
	if(!std::isfinite(paid_in) || !std::isfinite(retained)
		|| std::abs(retained) > std::numeric_limits<float>::max()) return;
	state.world.organization_set_retained_earnings(bank, float(retained));
	auto equity_asset = actors::organizations::equity_asset_for_organization(state, bank);
	if(equity_asset && state.world.asset_is_valid(equity_asset))
		state.world.asset_set_appraised_value(equity_asset, std::max(0.0f, sheet.net_worth));
}

float borrower_exposure(sys::state const& state, dcon::organization_id bank,
	dcon::economic_actor_id borrower, dcon::commodity_id settlement) {
	std::vector<dcon::obligation_id> loans;
	state.world.economic_actor_for_each_obligation_creditor_as_economic_actor(
		actors::organizations::actor_for_organization(state, bank), [&](dcon::obligation_creditor_id relation) {
		auto loan = state.world.obligation_creditor_get_obligation(relation);
		if(!loan || state.world.obligation_get_kind(loan) != uint8_t(relations::obligation_kind::loan)
			|| state.world.obligation_get_economic_actor_from_obligation_debtor(loan) != borrower
			|| state.world.obligation_get_settlement_commodity(loan) != settlement) return;
		auto status = state.world.obligation_get_status(loan);
		if(status == uint8_t(relations::obligation_status::active)
			|| status == uint8_t(relations::obligation_status::defaulted)) loans.push_back(loan);
	});
	std::sort(loans.begin(), loans.end(), [&](auto left, auto right) {
		return state.world.obligation_get_canonical_id(left) < state.world.obligation_get_canonical_id(right);
	});
	double result = 0.0;
	for(auto loan : loans) result += relations::total_due(state, loan);
	return result <= std::numeric_limits<float>::max() ? float(result) : std::numeric_limits<float>::max();
}

float bank_credit_capacity(sys::state const& state, dcon::organization_id bank,
	dcon::economic_actor_id borrower, dcon::commodity_id settlement, bool creates_deposit) {
	if(!policy_configured(state, bank) || status_of(state, bank) != bank_status::solvent
		|| settlement != state.world.organization_get_bank_settlement_currency(bank)) return 0.0f;
	auto sheet = bank_balance_sheet(state, bank, settlement);
	if(sheet.net_worth <= 0.0f || sheet.capital_ratio + balance_tolerance
		< state.world.organization_get_bank_minimum_capital_ratio(bank)) return 0.0f;
	auto capital_ratio = state.world.organization_get_bank_minimum_capital_ratio(bank);
	auto capital_headroom = capital_ratio > 0.0f
		? std::max(0.0f, sheet.net_worth / capital_ratio - sheet.total_assets)
		: std::numeric_limits<float>::max();
	auto max_single = state.world.organization_get_bank_max_single_borrower_exposure(bank);
	auto single_headroom = std::max(0.0f, sheet.net_worth * max_single
		- borrower_exposure(state, bank, borrower, settlement));
	auto reserve = reserve_account_for(state, bank, settlement);
	if(!reserve) return 0.0f;
	auto reserve_balance = accounts::balance(state, reserve);
	auto liquidity_ratio = state.world.organization_get_bank_liquidity_target(bank);
	auto reserve_ratio = state.world.organization_get_bank_reserve_requirement(bank);
	auto required_ratio = std::max(liquidity_ratio, reserve_ratio);
	auto liability_headroom = required_ratio > 0.0f
		? std::max(0.0f, reserve_balance / required_ratio - sheet.deposit_liabilities)
		: std::numeric_limits<float>::max();
	auto available_withdrawal = std::max(0.0f, reserve_balance - sheet.required_liquidity);
	auto liquidity_headroom = creates_deposit ? liability_headroom : available_withdrawal;
	return std::min({ capital_headroom, single_headroom, liquidity_headroom });
}

void refresh_bank_state(sys::state& state, dcon::organization_id bank) {
	if(!valid_bank(state, bank)) return;
	if(!policy_configured(state, bank)) {
		state.world.organization_set_bank_status(bank, uint8_t(bank_status::constrained));
		return;
	}
	auto settlement = state.world.organization_get_bank_settlement_currency(bank);
	auto sheet = bank_balance_sheet(state, bank, settlement);
	sync_bank_equity(state, bank);
	if(state.world.organization_get_bank_status(bank) == uint8_t(bank_status::insolvent)) return;
	if(!std::isfinite(sheet.total_assets) || !std::isfinite(sheet.total_liabilities)
		|| !std::isfinite(sheet.net_worth) || !std::isfinite(sheet.capital_ratio)
		|| !std::isfinite(sheet.liquidity_ratio) || sheet.net_worth < -balance_tolerance) {
		state.world.organization_set_bank_status(bank, uint8_t(bank_status::insolvent));
		return;
	}
	bool capital_breach = sheet.capital_ratio + balance_tolerance
		< state.world.organization_get_bank_minimum_capital_ratio(bank);
	bool liquidity_breach = sheet.liquidity_ratio + balance_tolerance
		< std::max(state.world.organization_get_bank_liquidity_target(bank),
			state.world.organization_get_bank_reserve_requirement(bank))
		|| sheet.settlement_assets + balance_tolerance < sheet.required_liquidity;
	state.world.organization_set_bank_status(bank, uint8_t(capital_breach || liquidity_breach
		? bank_status::constrained : bank_status::solvent));
	if(!capital_breach && !liquidity_breach)
		state.world.organization_set_bank_capital_breach_days(bank, 0);
}

} // namespace

dcon::organization_id create_bank(sys::state& state) {
	auto bank = actors::organizations::create_organization(state, actor_kind::bank);
	if(bank) {
		auto actor = actors::organizations::actor_for_organization(state, bank);
		auto org_id = stable_hash(id_key("bank-runtime", bank.index()));
		state.world.organization_set_canonical_id(bank, org_id);
		state.world.economic_actor_set_canonical_id(actor, stable_hash(id_key("bank-actor-runtime", org_id)));
		state.world.asset_set_canonical_id(actors::organizations::equity_asset_for_organization(state, bank),
			stable_hash(id_key("bank-equity-runtime", org_id)));
		state.world.organization_set_bank_status(bank, uint8_t(bank_status::constrained));
	}
	return bank;
}

bool set_bank_lending_base_rate(sys::state& state, dcon::organization_id bank, float annual_rate) {
	if(!valid_bank(state, bank) || !std::isfinite(annual_rate) || annual_rate < 0.0f || annual_rate > 1.0f
		|| (policy_configured(state, bank)
			&& annual_rate + state.world.organization_get_bank_lending_spread(bank) > 1.0f)) return false;
	state.world.organization_set_lending_base_rate(bank, annual_rate);
	refresh_bank_state(state, bank);
	return true;
}

bool configure_bank_policy(sys::state& state, dcon::organization_id bank, bank_policy const& policy) {
	if(!valid_bank(state, bank) || !policy.jurisdiction || !state.world.nation_is_valid(policy.jurisdiction)
		|| !policy.settlement || !state.world.commodity_is_valid(policy.settlement)
		|| !std::isfinite(policy.lending_base_rate) || policy.lending_base_rate < 0.0f || policy.lending_base_rate > 1.0f
		|| policy.lending_base_rate + policy.lending_spread > 1.0f
		|| !fraction(policy.minimum_capital_ratio) || !fraction(policy.liquidity_target)
		|| !fraction(policy.risk_appetite) || !fraction(policy.lending_spread)
		|| !fraction(policy.max_single_borrower_exposure) || !fraction(policy.reserve_requirement)) return false;
	if(policy_configured(state, bank)
		&& state.world.organization_get_bank_settlement_currency(bank) != policy.settlement) return false;
	state.world.organization_set_bank_jurisdiction(bank, policy.jurisdiction);
	state.world.organization_set_bank_settlement_currency(bank, policy.settlement);
	state.world.organization_set_lending_base_rate(bank, policy.lending_base_rate);
	state.world.organization_set_bank_minimum_capital_ratio(bank, policy.minimum_capital_ratio);
	state.world.organization_set_bank_liquidity_target(bank, policy.liquidity_target);
	state.world.organization_set_bank_risk_appetite(bank, policy.risk_appetite);
	state.world.organization_set_bank_lending_spread(bank, policy.lending_spread);
	state.world.organization_set_bank_max_single_borrower_exposure(bank, policy.max_single_borrower_exposure);
	state.world.organization_set_bank_reserve_requirement(bank, policy.reserve_requirement);
	state.world.organization_set_bank_capital_breach_grace_days(bank, policy.capital_breach_grace_days);
	state.world.organization_set_bank_policy_configured(bank, 1);
	if(state.world.organization_get_bank_status(bank) != uint8_t(bank_status::insolvent))
		state.world.organization_set_bank_status(bank, uint8_t(bank_status::constrained));
	refresh_bank_state(state, bank);
	return true;
}

bank_status status_of(sys::state const& state, dcon::organization_id bank) {
	if(!valid_bank(state, bank) || !policy_configured(state, bank)) return bank_status::constrained;
	auto value = state.world.organization_get_bank_status(bank);
	if(value > uint8_t(bank_status::insolvent)) return bank_status::insolvent;
	return bank_status(value);
}

dcon::monetary_account_id reserve_account_for(sys::state const& state,
	dcon::organization_id bank, dcon::commodity_id settlement) {
	if(!valid_bank(state, bank) || !settlement) return {};
	dcon::monetary_account_id result{};
	bool duplicate = false;
	state.world.organization_for_each_monetary_account_reserve_bank_as_organization(bank,
		[&](dcon::monetary_account_reserve_bank_id relation) {
			auto account = state.world.monetary_account_reserve_bank_get_monetary_account(relation);
			economy::monetary::ontology::account_view view;
			if(!account || economy::accounts::settlement_of(state, account) != settlement
				|| !economy::monetary::ontology::describe(state, economy::monetary::ontology::account_ref::from_monetary(account), view)
				|| view.instrument != economy::monetary::ontology::instrument_kind::base_money_reserve) return;
			if(result) duplicate = true;
			else result = account;
		});
	return duplicate ? dcon::monetary_account_id{} : result;
}

dcon::monetary_account_id open_reserve_account(sys::state& state, dcon::organization_id bank,
	dcon::commodity_id settlement, uint64_t canonical_id) {
	if(!policy_configured(state, bank) || !settlement || !state.world.commodity_is_valid(settlement)
		|| state.world.organization_get_bank_settlement_currency(bank) != settlement) return {};
	if(auto existing = reserve_account_for(state, bank, settlement))
		return canonical_id == 0 || state.world.monetary_account_get_canonical_id(existing) == canonical_id
			? existing : dcon::monetary_account_id{};
	uint32_t matching_reserves = 0;
	state.world.organization_for_each_monetary_account_reserve_bank_as_organization(bank,
		[&](dcon::monetary_account_reserve_bank_id relation) {
			auto account = state.world.monetary_account_reserve_bank_get_monetary_account(relation);
			if(account && economy::accounts::settlement_of(state, account) == settlement) ++matching_reserves;
	});
	if(matching_reserves != 0) return {};
	if(canonical_id == 0)
		canonical_id = stable_hash(id_key("bank-reserve", state.world.organization_get_canonical_id(bank), settlement.index()));
	bool duplicate_id = false;
	state.world.for_each_monetary_account([&](dcon::monetary_account_id other) {
		if(state.world.monetary_account_get_canonical_id(other) == canonical_id)
			duplicate_id = true;
	});
	if(duplicate_id) return {};
	auto actor = actors::organizations::actor_for_organization(state, bank);
	auto account = economy::accounts::find_account(state, actor, settlement);
	if(!account) account = economy::accounts::open_account(state, actor, settlement);
	if(!account) return {};
	state.world.force_create_monetary_account_reserve_bank(account, bank);
	state.world.monetary_account_set_canonical_id(account, canonical_id);
	return account;
}

bool bootstrap_set_reserve_balance(sys::state& state, dcon::monetary_account_id account,
	float amount) {
	economy::monetary::ontology::account_view view;
	if(!account || !state.world.monetary_account_is_valid(account)
		|| !economy::monetary::ontology::describe(state, economy::monetary::ontology::account_ref::from_monetary(account), view)
		|| view.instrument != economy::monetary::ontology::instrument_kind::base_money_reserve
		|| !valid_nonnegative_amount(amount)
		|| !state.world.monetary_account_get_organization_from_monetary_account_reserve_bank(account)
		|| !policy_configured(state, state.world.monetary_account_get_organization_from_monetary_account_reserve_bank(account))
		|| status_of(state, state.world.monetary_account_get_organization_from_monetary_account_reserve_bank(account)) == bank_status::insolvent) return false;
	state.world.monetary_account_set_balance(account, amount);
	sync_bank_equity(state,
		state.world.monetary_account_get_organization_from_monetary_account_reserve_bank(account));
	return true;
}

dcon::deposit_account_id open_deposit_account(sys::state& state, dcon::organization_id bank,
	dcon::economic_actor_id owner, dcon::commodity_id settlement, uint64_t canonical_id) {
	if(!policy_configured(state, bank) || status_of(state, bank) == bank_status::insolvent
		|| !owner || !state.world.economic_actor_is_valid(owner)
		|| state.world.economic_actor_get_canonical_id(owner) == 0
		|| !settlement || !state.world.commodity_is_valid(settlement)
		|| settlement != state.world.organization_get_bank_settlement_currency(bank)) return {};
	dcon::deposit_account_id existing{};
	state.world.organization_for_each_deposit_account_bank_as_organization(bank,
		[&](dcon::deposit_account_bank_id relation) {
			auto candidate = state.world.deposit_account_bank_get_deposit_account(relation);
			if(candidate && state.world.deposit_account_get_economic_actor_from_deposit_account_owner(candidate) == owner
				&& state.world.deposit_account_get_commodity_from_deposit_account_settlement(candidate) == settlement)
				existing = candidate;
		});
	if(existing) return canonical_id == 0 || state.world.deposit_account_get_canonical_id(existing) == canonical_id
		? existing : dcon::deposit_account_id{};
	if(canonical_id == 0)
		canonical_id = stable_hash(id_key("deposit", state.world.organization_get_canonical_id(bank),
			state.world.economic_actor_get_canonical_id(owner), settlement.index()));
	bool duplicate_id = false;
	state.world.for_each_deposit_account([&](dcon::deposit_account_id account) {
		if(state.world.deposit_account_get_canonical_id(account) == canonical_id) duplicate_id = true;
	});
	if(duplicate_id) return {};
	auto account = state.world.create_deposit_account();
	state.world.deposit_account_set_balance(account, 0.0f);
	state.world.force_create_deposit_account_bank(account, bank);
	state.world.force_create_deposit_account_owner(account, owner);
	state.world.force_create_deposit_account_settlement(account, settlement);
	state.world.deposit_account_set_canonical_id(account, canonical_id);
	return account;
}

float deposit_balance(sys::state const& state, dcon::deposit_account_id account) {
	return account && state.world.deposit_account_is_valid(account)
		? state.world.deposit_account_get_balance(account) : 0.0f;
}

bool bootstrap_set_deposit_balance(sys::state& state, dcon::deposit_account_id account,
	float amount) {
	economy::monetary::ontology::account_view view;
	if(!account || !state.world.deposit_account_is_valid(account)
		|| !economy::monetary::ontology::describe(state, economy::monetary::ontology::account_ref::from_deposit(account), view)
		|| view.instrument != economy::monetary::ontology::instrument_kind::bank_deposit
		|| !valid_nonnegative_amount(amount)
		|| !policy_configured(state, state.world.deposit_account_get_organization_from_deposit_account_bank(account))
		|| status_of(state, state.world.deposit_account_get_organization_from_deposit_account_bank(account)) == bank_status::insolvent
		|| !state.world.deposit_account_get_economic_actor_from_deposit_account_owner(account)
		|| !state.world.deposit_account_get_commodity_from_deposit_account_settlement(account)
		|| state.world.deposit_account_get_canonical_id(account) == 0
		|| state.world.deposit_account_get_commodity_from_deposit_account_settlement(account)
			!= state.world.organization_get_bank_settlement_currency(
				state.world.deposit_account_get_organization_from_deposit_account_bank(account))) return false;
	state.world.deposit_account_set_balance(account, amount);
	sync_bank_equity(state,
		state.world.deposit_account_get_organization_from_deposit_account_bank(account));
	return true;
}

namespace {
dcon::obligation_id execute_loan_raw(sys::state& state, dcon::organization_id bank,
	dcon::deposit_account_id borrower_account, float principal, sys::date creation_date,
	sys::date due_date, float annual_interest_rate, uint64_t canonical_id = 0,
	float collateral_value = 0.0f) {
	if(!policy_configured(state, bank) || status_of(state, bank) != bank_status::solvent || !borrower_account
		|| !state.world.deposit_account_is_valid(borrower_account)
		|| state.world.deposit_account_get_organization_from_deposit_account_bank(borrower_account) != bank
		|| !creation_date || !due_date || due_date < creation_date
		|| !valid_positive_amount(principal) || !std::isfinite(annual_interest_rate)
		|| annual_interest_rate < 0.0f || !valid_nonnegative_amount(collateral_value)) return {};
	auto borrower = state.world.deposit_account_get_economic_actor_from_deposit_account_owner(borrower_account);
	auto settlement = state.world.deposit_account_get_commodity_from_deposit_account_settlement(borrower_account);
	auto lender = actors::organizations::actor_for_organization(state, bank);
	if(!borrower || !settlement || !lender || borrower == lender
		|| settlement != state.world.organization_get_bank_settlement_currency(bank)
		|| !valid_nonnegative_amount(deposit_balance(state, borrower_account))
		|| annual_interest_rate + balance_tolerance < bank_base_rate(state, bank)
			+ state.world.organization_get_bank_lending_spread(bank)
		|| annual_interest_rate > 1.0f) return {};
	auto projected_deposit = double(deposit_balance(state, borrower_account)) + double(principal);
	if(!std::isfinite(projected_deposit) || projected_deposit > std::numeric_limits<float>::max()) return {};
	if(principal > bank_credit_capacity(state, bank, borrower, settlement, true) + balance_tolerance) return {};
	if(canonical_id == 0)
		canonical_id = stable_hash(id_key("loan-runtime", state.world.organization_get_canonical_id(bank),
			state.world.economic_actor_get_canonical_id(borrower), creation_date.to_raw_value())
			+ ":" + std::to_string(std::bit_cast<uint32_t>(principal)));
	state.world.for_each_obligation([&](dcon::obligation_id existing) {
		if(state.world.obligation_get_canonical_id(existing) == canonical_id) canonical_id = 0;
	});
	if(canonical_id == 0) return {};
	auto loan = relations::create_obligation(state, borrower, lender, principal, settlement,
		creation_date, due_date, annual_interest_rate, relations::obligation_kind::loan);
	if(!loan) return {};
	state.world.obligation_set_canonical_id(loan, canonical_id);
	state.world.obligation_set_collateral_value(loan, collateral_value);
	auto issuance = relations::record_transaction(state, lender, borrower, principal, settlement,
		relations::transaction_kind::loan_issuance, creation_date);
	if(!issuance) return {};
	state.world.deposit_account_set_balance(borrower_account, deposit_balance(state, borrower_account) + principal);
	refresh_bank_state(state, bank);
	return loan;
}

dcon::obligation_id originate_factory_loan_to_operating_account(sys::state& state,
	dcon::organization_id bank, dcon::factory_id factory, dcon::monetary_account_id operating_account,
	float principal, float annual_interest_rate, uint64_t canonical_id, float collateral_value) {
	if(!policy_configured(state, bank) || status_of(state, bank) != bank_status::solvent
		|| !factory || !operating_account
		|| !state.world.monetary_account_is_valid(operating_account)) return {};
	economy::monetary::ontology::account_view operating_view;
	if(!economy::monetary::ontology::describe(state, economy::monetary::ontology::account_ref::from_monetary(operating_account), operating_view)
		|| operating_view.instrument != economy::monetary::ontology::instrument_kind::operating_account) return {};
	auto borrower = accounts::owner_of(state, operating_account);
	auto settlement = accounts::settlement_of(state, operating_account);
	auto lender = actors::organizations::actor_for_organization(state, bank);
	auto reserve = reserve_account_for(state, bank, settlement);
	if(!borrower || borrower == lender || borrower != actors::organizations::operator_actor_for_factory(state, factory)
		|| !settlement || !reserve || settlement != state.world.organization_get_bank_settlement_currency(bank)
		|| !valid_positive_amount(principal) || !std::isfinite(annual_interest_rate)
		|| annual_interest_rate + balance_tolerance < bank_base_rate(state, bank)
			+ state.world.organization_get_bank_lending_spread(bank)
		|| annual_interest_rate > 1.0f
		|| !valid_nonnegative_amount(collateral_value)
		|| principal > bank_credit_capacity(state, bank, borrower, settlement, false) + balance_tolerance) return {};
	auto projected_cash = double(accounts::balance(state, operating_account)) + double(principal);
	if(!std::isfinite(projected_cash) || projected_cash > std::numeric_limits<float>::max()) return {};
	if(canonical_id == 0)
		canonical_id = stable_hash(id_key("loan-factory-runtime", state.world.organization_get_canonical_id(bank),
			state.world.factory_get_canonical_id(factory), state.current_date.to_raw_value()));
	state.world.for_each_obligation([&](dcon::obligation_id existing) {
		if(state.world.obligation_get_canonical_id(existing) == canonical_id) canonical_id = 0;
	});
	if(canonical_id == 0) return {};
	auto loan = relations::create_obligation(state, borrower, lender, principal, settlement,
		state.current_date, state.current_date + 365, annual_interest_rate, relations::obligation_kind::loan);
	if(!loan) return {};
	state.world.obligation_set_canonical_id(loan, canonical_id);
	state.world.obligation_set_collateral_value(loan, collateral_value);
	state.world.force_create_obligation_factory(loan, factory);
	auto issuance = relations::record_transaction(state, lender, borrower, principal, settlement,
		relations::transaction_kind::loan_issuance, state.current_date);
	if(!issuance) return {};
	// Operating cash is held outside commercial bank deposit liabilities. This
	// loan is paid out from the bank's authored reserve settlement asset: loan
	// principal replaces reserves, and the operating cash account receives it.
	state.world.monetary_account_set_balance(reserve, accounts::balance(state, reserve) - principal);
	state.world.monetary_account_set_balance(operating_account,
		accounts::balance(state, operating_account) + principal);
	state.world.obligation_set_last_interest_accrual_date(loan, state.current_date);
	refresh_bank_state(state, bank);
	return loan;
}
}

dcon::obligation_id originate_loan_with_consent(sys::state& state, dcon::organization_id bank,
	dcon::deposit_account_id borrower_account, float principal, sys::date creation_date,
	sys::date due_date, float annual_interest_rate, dcon::economic_proposal_id proposal) {
	if(!proposal || !state.world.economic_proposal_is_valid(proposal)
		|| state.world.economic_proposal_get_kind(proposal) != uint8_t(economy::consent::proposal_kind::loan)
		|| !borrower_account || !state.world.deposit_account_is_valid(borrower_account)) return {};
	auto bank_actor = actors::organizations::actor_for_organization(state, bank);
	auto borrower = state.world.deposit_account_get_economic_actor_from_deposit_account_owner(borrower_account);
	auto settlement = state.world.deposit_account_get_commodity_from_deposit_account_settlement(borrower_account);
	if(state.world.economic_proposal_get_economic_actor_from_economic_proposal_actor_a(proposal) != bank_actor
		|| state.world.economic_proposal_get_economic_actor_from_economic_proposal_actor_b(proposal) != borrower
		|| state.world.economic_proposal_get_settlement(proposal) != settlement
		|| state.world.economic_proposal_get_amount(proposal) != principal
		|| state.world.economic_proposal_get_due_date(proposal) != due_date
		|| state.world.economic_proposal_get_annual_interest_rate(proposal) != annual_interest_rate
		|| !economy::consent::proposal_fully_accepted(state, proposal, creation_date)) return {};
	auto stable_id = stable_hash(id_key("loan-consent", state.world.organization_get_canonical_id(bank),
		state.world.economic_actor_get_canonical_id(borrower), proposal.index())
		+ ":" + std::to_string(creation_date.to_raw_value()));
	auto loan = execute_loan_raw(state, bank, borrower_account, principal, creation_date, due_date,
		annual_interest_rate, stable_id);
	if(!loan || !economy::consent::mark_executed(state, proposal)) return {};
	return loan;
}

bool transfer_deposit(sys::state& state, dcon::deposit_account_id source,
	dcon::deposit_account_id destination, float amount, sys::date timestamp) {
	economy::monetary::ontology::account_view source_view, destination_view;
	if(!source || !destination || source == destination || !valid_positive_amount(amount)
		|| !economy::monetary::ontology::describe(state, economy::monetary::ontology::account_ref::from_deposit(source), source_view)
		|| !economy::monetary::ontology::describe(state, economy::monetary::ontology::account_ref::from_deposit(destination), destination_view)
		|| source_view.instrument != economy::monetary::ontology::instrument_kind::bank_deposit
		|| destination_view.instrument != economy::monetary::ontology::instrument_kind::bank_deposit) return false;
	auto source_bank = state.world.deposit_account_get_organization_from_deposit_account_bank(source);
	auto destination_bank = state.world.deposit_account_get_organization_from_deposit_account_bank(destination);
	auto settlement = state.world.deposit_account_get_commodity_from_deposit_account_settlement(source);
	if(!policy_configured(state, source_bank) || !policy_configured(state, destination_bank)
		|| !settlement || state.world.deposit_account_get_commodity_from_deposit_account_settlement(destination) != settlement
		|| settlement != state.world.organization_get_bank_settlement_currency(source_bank)
		|| settlement != state.world.organization_get_bank_settlement_currency(destination_bank)) return false;
	if(source_bank != destination_bank) {
		auto source_id = state.world.deposit_account_get_canonical_id(source);
		auto destination_id = state.world.deposit_account_get_canonical_id(destination);
		if(source_id == 0 || destination_id == 0) return false;
		auto key = stable_hash(id_key("interbank-payment", source_id, destination_id,
			timestamp.to_raw_value()) + ":" + std::to_string(std::bit_cast<uint32_t>(amount)));
		return queue_interbank_payment(state, source, destination, amount, timestamp, key);
	}
	if(deposit_balance(state, source) + balance_tolerance < amount) return false;
	auto source_owner = state.world.deposit_account_get_economic_actor_from_deposit_account_owner(source);
	auto destination_owner = state.world.deposit_account_get_economic_actor_from_deposit_account_owner(destination);
	if(!source_owner || !destination_owner) return false;
	auto projected_source = double(deposit_balance(state, source)) - double(amount);
	auto projected_destination = double(deposit_balance(state, destination)) + double(amount);
	if(!std::isfinite(projected_source) || !std::isfinite(projected_destination)
		|| projected_source < -balance_tolerance || projected_destination > std::numeric_limits<float>::max()) return false;
	auto transaction = relations::record_transaction(state, source_owner, destination_owner, amount,
		settlement, relations::transaction_kind::transfer, timestamp);
	if(!transaction) return false;
	state.world.deposit_account_set_balance(source, deposit_balance(state, source) - amount);
	state.world.deposit_account_set_balance(destination, deposit_balance(state, destination) + amount);
	return true;
}

namespace {
// The wallet belongs to the deposit's owner: the same DCON actor, or the exact
// person whose profile actor owns the deposit.
bool wallet_of_owner(sys::state const& state, dcon::deposit_account_id deposit,
	economy::exact_person_economy::account_ref wallet) {
	auto owner = state.world.deposit_account_get_economic_actor_from_deposit_account_owner(deposit);
	if(!owner || !wallet) return false;
	if(wallet.kind == economy::exact_person_economy::account_kind::dcon)
		return accounts::owner_of(state, wallet.dcon_account) == owner;
	auto profile = state.world.economic_actor_get_person_from_person_actor(owner);
	return profile && persons::canonical_key(state, profile) == economy::exact_person_economy::owner_of(state, wallet);
}

bool deposit_usable(sys::state const& state, dcon::deposit_account_id deposit) {
	if(!deposit || !state.world.deposit_account_is_valid(deposit)) return false;
	auto bank = state.world.deposit_account_get_organization_from_deposit_account_bank(deposit);
	auto settlement = state.world.deposit_account_get_commodity_from_deposit_account_settlement(deposit);
	return policy_configured(state, bank) && settlement
		&& settlement == state.world.organization_get_bank_settlement_currency(bank)
		&& reserve_account_for(state, bank, settlement);
}
}

dcon::deposit_account_id deposit_account_for(sys::state const& state, dcon::economic_actor_id owner,
	dcon::commodity_id settlement, dcon::organization_id bank) {
	dcon::deposit_account_id result{};
	if(!owner || !settlement) return result;
	state.world.economic_actor_for_each_deposit_account_owner_as_economic_actor(owner, [&](auto relation) {
		auto account = state.world.deposit_account_owner_get_deposit_account(relation);
		if(state.world.deposit_account_get_commodity_from_deposit_account_settlement(account) != settlement) return;
		auto at_bank = state.world.deposit_account_get_organization_from_deposit_account_bank(account) == bank;
		auto current_at_bank = result && state.world.deposit_account_get_organization_from_deposit_account_bank(result) == bank;
		if(!result || (at_bank && !current_at_bank) || (at_bank == current_at_bank && account.index() < result.index()))
			result = account;
	});
	return result;
}

float deposit_cash(sys::state& state, dcon::deposit_account_id deposit, economy::exact_person_economy::account_ref wallet,
	float amount, sys::date timestamp) {
	if(!deposit_usable(state, deposit) || !wallet_of_owner(state, deposit, wallet) || !valid_positive_amount(amount)) return 0.0f;
	auto bank = state.world.deposit_account_get_organization_from_deposit_account_bank(deposit);
	if(status_of(state, bank) == bank_status::insolvent) return 0.0f;
	auto reserve = reserve_account_for(state, bank, state.world.organization_get_bank_settlement_currency(bank));
	auto projected = double(deposit_balance(state, deposit)) + double(amount);
	if(projected > std::numeric_limits<float>::max()) return 0.0f;
	if(!economy::exact_person_economy::settle_with_reserve(state, reserve, wallet, -amount,
		relations::transaction_kind::deposit_placement, timestamp).success) return 0.0f;
	state.world.deposit_account_set_balance(deposit, float(projected));
	refresh_bank_state(state, bank);
	return amount;
}

float withdraw_cash(sys::state& state, dcon::deposit_account_id deposit, economy::exact_person_economy::account_ref wallet,
	float amount, sys::date timestamp) {
	if(!deposit_usable(state, deposit) || !wallet_of_owner(state, deposit, wallet) || !valid_positive_amount(amount)) return 0.0f;
	auto bank = state.world.deposit_account_get_organization_from_deposit_account_bank(deposit);
	auto reserve = reserve_account_for(state, bank, state.world.organization_get_bank_settlement_currency(bank));
	// A bank cannot pay out more base money than it holds.
	auto paid = std::min({amount, deposit_balance(state, deposit), std::max(0.0f, accounts::balance(state, reserve))});
	if(!valid_positive_amount(paid)) return 0.0f;
	if(!economy::exact_person_economy::settle_with_reserve(state, reserve, wallet, paid,
		relations::transaction_kind::withdrawal, timestamp).success) return 0.0f;
	state.world.deposit_account_set_balance(deposit, std::max(0.0f, deposit_balance(state, deposit) - paid));
	refresh_bank_state(state, bank);
	return paid;
}

dcon::transaction_id pay_from_deposit(sys::state& state, dcon::deposit_account_id source,
	dcon::deposit_account_id destination_deposit, economy::exact_person_economy::account_ref destination_wallet,
	float amount, relations::transaction_kind kind, sys::date timestamp) {
	if(!deposit_usable(state, source) || !valid_positive_amount(amount)
		|| deposit_balance(state, source) + balance_tolerance < amount) return {};
	auto source_bank = state.world.deposit_account_get_organization_from_deposit_account_bank(source);
	auto settlement = state.world.deposit_account_get_commodity_from_deposit_account_settlement(source);
	auto source_owner = state.world.deposit_account_get_economic_actor_from_deposit_account_owner(source);
	auto source_reserve = reserve_account_for(state, source_bank, settlement);
	if(status_of(state, source_bank) == bank_status::insolvent || !source_owner) return {};
	if(destination_deposit) {
		if(destination_deposit == source || !deposit_usable(state, destination_deposit)
			|| state.world.deposit_account_get_commodity_from_deposit_account_settlement(destination_deposit) != settlement) return {};
		auto destination_bank = state.world.deposit_account_get_organization_from_deposit_account_bank(destination_deposit);
		auto destination_owner = state.world.deposit_account_get_economic_actor_from_deposit_account_owner(destination_deposit);
		auto projected = double(deposit_balance(state, destination_deposit)) + double(amount);
		if(!destination_owner || projected > std::numeric_limits<float>::max()) return {};
		dcon::monetary_account_id destination_reserve{};
		if(destination_bank != source_bank) {
			// Gross settlement: the payer's bank pays the payee's bank in reserves now.
			destination_reserve = reserve_account_for(state, destination_bank, settlement);
			if(!destination_reserve || accounts::balance(state, source_reserve) + balance_tolerance < amount) return {};
		}
		auto transaction = relations::record_transaction(state, source_owner, destination_owner, amount, settlement, kind, timestamp);
		if(!transaction) return {};
		state.world.deposit_account_set_balance(source, std::max(0.0f, deposit_balance(state, source) - amount));
		state.world.deposit_account_set_balance(destination_deposit, float(projected));
		if(destination_reserve) {
			state.world.monetary_account_set_balance(source_reserve, accounts::balance(state, source_reserve) - amount);
			state.world.monetary_account_set_balance(destination_reserve, accounts::balance(state, destination_reserve) + amount);
			refresh_bank_state(state, destination_bank);
		}
		refresh_bank_state(state, source_bank);
		return transaction;
	}
	if(!destination_wallet || accounts::balance(state, source_reserve) + balance_tolerance < amount) return {};
	// Paying a wallet takes base money out of the payer's bank.
	// The ledger entry names the depositor as payer; the bank only settles.
	auto settled = economy::exact_person_economy::settle_with_reserve(state, source_reserve, destination_wallet,
		amount, kind, timestamp, source_owner);
	if(!settled.success) return {};
	state.world.deposit_account_set_balance(source, std::max(0.0f, deposit_balance(state, source) - amount));
	refresh_bank_state(state, source_bank);
	return settled.dcon_transaction_id;
}

bool queue_interbank_payment(sys::state& state, dcon::deposit_account_id source,
	dcon::deposit_account_id destination, float amount, sys::date timestamp,
	uint64_t stable_transaction_key) {
	economy::monetary::ontology::account_view source_view, destination_view;
	if(!source || !destination || source == destination || stable_transaction_key == 0
		|| !valid_positive_amount(amount)
		|| !economy::monetary::ontology::describe(state, economy::monetary::ontology::account_ref::from_deposit(source), source_view)
		|| !economy::monetary::ontology::describe(state, economy::monetary::ontology::account_ref::from_deposit(destination), destination_view)
		|| source_view.instrument != economy::monetary::ontology::instrument_kind::bank_deposit
		|| destination_view.instrument != economy::monetary::ontology::instrument_kind::bank_deposit) return false;
	auto source_bank = state.world.deposit_account_get_organization_from_deposit_account_bank(source);
	auto destination_bank = state.world.deposit_account_get_organization_from_deposit_account_bank(destination);
	auto settlement = state.world.deposit_account_get_commodity_from_deposit_account_settlement(source);
	if(!policy_configured(state, source_bank) || !policy_configured(state, destination_bank)
		|| source_bank == destination_bank || !settlement
		|| state.world.deposit_account_get_commodity_from_deposit_account_settlement(destination) != settlement
		|| state.world.organization_get_bank_settlement_currency(source_bank) != settlement
		|| state.world.organization_get_bank_settlement_currency(destination_bank) != settlement
		|| !state.world.deposit_account_get_economic_actor_from_deposit_account_owner(source)
		|| !state.world.deposit_account_get_economic_actor_from_deposit_account_owner(destination)
		|| state.world.deposit_account_get_canonical_id(source) == 0
		|| state.world.deposit_account_get_canonical_id(destination) == 0
		|| state.world.economic_actor_get_canonical_id(
			state.world.deposit_account_get_economic_actor_from_deposit_account_owner(source)) == 0
		|| state.world.economic_actor_get_canonical_id(
			state.world.deposit_account_get_economic_actor_from_deposit_account_owner(destination)) == 0) return false;
	bool duplicate = false;
	state.world.for_each_bank_payment_instruction([&](dcon::bank_payment_instruction_id instruction) {
		if(state.world.bank_payment_instruction_get_canonical_id(instruction) == stable_transaction_key)
			duplicate = true;
	});
	if(duplicate) return false;
	auto instruction = state.world.create_bank_payment_instruction();
	state.world.bank_payment_instruction_set_canonical_id(instruction, stable_transaction_key);
	state.world.bank_payment_instruction_set_amount(instruction, amount);
	state.world.bank_payment_instruction_set_requested_on(instruction, timestamp);
	state.world.bank_payment_instruction_set_status(instruction, 0); // pending
	state.world.force_create_bank_payment_instruction_source(instruction, source);
	state.world.force_create_bank_payment_instruction_destination(instruction, destination);
	return true;
}

bank_clearing_result clear_interbank_payments(sys::state& state, sys::date today) {
	bank_clearing_result result{};
	std::vector<dcon::bank_payment_instruction_id> pending;
	state.world.for_each_bank_payment_instruction([&](dcon::bank_payment_instruction_id instruction) {
		if(state.world.bank_payment_instruction_get_status(instruction) == 0
			&& state.world.bank_payment_instruction_get_requested_on(instruction) <= today)
			pending.push_back(instruction);
	});
	std::sort(pending.begin(), pending.end(), [&](auto left, auto right) {
		return state.world.bank_payment_instruction_get_canonical_id(left)
			< state.world.bank_payment_instruction_get_canonical_id(right);
	});

	std::map<uint32_t, double> account_delta;
	std::map<uint32_t, dcon::deposit_account_id> accounts_by_index;
	std::map<uint32_t, double> reserve_delta;
	std::map<uint32_t, dcon::organization_id> banks_by_index;
	std::map<uint32_t, double> reserve_opening;
	std::vector<dcon::bank_payment_instruction_id> accepted;
	for(auto instruction : pending) {
		auto source_relation = state.world.bank_payment_instruction_get_bank_payment_instruction_source(instruction);
		auto destination_relation = state.world.bank_payment_instruction_get_bank_payment_instruction_destination(instruction);
		auto source = source_relation ? state.world.bank_payment_instruction_source_get_deposit_account(source_relation) : dcon::deposit_account_id{};
		auto destination = destination_relation ? state.world.bank_payment_instruction_destination_get_deposit_account(destination_relation) : dcon::deposit_account_id{};
		auto amount = state.world.bank_payment_instruction_get_amount(instruction);
		bool valid = source && destination && source != destination
			&& state.world.deposit_account_is_valid(source) && state.world.deposit_account_is_valid(destination)
			&& valid_positive_amount(amount);
		dcon::organization_id source_bank{}, destination_bank{};
		dcon::commodity_id settlement{};
		if(valid) {
			source_bank = state.world.deposit_account_get_organization_from_deposit_account_bank(source);
			destination_bank = state.world.deposit_account_get_organization_from_deposit_account_bank(destination);
			settlement = state.world.deposit_account_get_commodity_from_deposit_account_settlement(source);
			valid = policy_configured(state, source_bank) && policy_configured(state, destination_bank)
				&& source_bank != destination_bank && settlement
				&& settlement == state.world.deposit_account_get_commodity_from_deposit_account_settlement(destination)
				&& settlement == state.world.organization_get_bank_settlement_currency(source_bank)
				&& settlement == state.world.organization_get_bank_settlement_currency(destination_bank)
				&& state.world.deposit_account_get_canonical_id(source) != 0
				&& state.world.deposit_account_get_canonical_id(destination) != 0;
		}
		if(!valid) {
			state.world.bank_payment_instruction_set_status(instruction, 2); // invalid/rejected
			++result.rejected;
			continue;
		}
		accepted.push_back(instruction);
		account_delta[source.index()] -= double(amount);
		account_delta[destination.index()] += double(amount);
		accounts_by_index[source.index()] = source;
		accounts_by_index[destination.index()] = destination;
		reserve_delta[source_bank.index()] -= double(amount);
		reserve_delta[destination_bank.index()] += double(amount);
		banks_by_index[source_bank.index()] = source_bank;
		banks_by_index[destination_bank.index()] = destination_bank;
		result.reserves_settled += amount;
	}
	if(accepted.empty()) return result;

	bool source_balances_valid = true;
	for(auto const& [index, delta] : account_delta) {
		auto account = accounts_by_index.at(index);
		auto after = double(deposit_balance(state, account)) + delta;
		if(!std::isfinite(after) || after < -double(balance_tolerance)
			|| after > std::numeric_limits<float>::max()) source_balances_valid = false;
	}
	if(!source_balances_valid) {
		// Stable ordering makes the accepted prefix independent of DCON traversal.
		std::map<uint32_t, double> used;
		std::unordered_set<uint32_t> accepted_accounts;
		for(auto instruction : accepted) {
			auto source_rel = state.world.bank_payment_instruction_get_bank_payment_instruction_source(instruction);
			auto source = state.world.bank_payment_instruction_source_get_deposit_account(source_rel);
			auto amount = state.world.bank_payment_instruction_get_amount(instruction);
			auto const next = used[source.index()] + double(amount);
			if(next > double(deposit_balance(state, source)) + balance_tolerance) {
				state.world.bank_payment_instruction_set_status(instruction, 2);
				++result.rejected;
				continue;
			}
			used[source.index()] = next;
			accepted_accounts.insert(instruction.index());
		}
		std::erase_if(accepted, [&](auto instruction) {
			return !accepted_accounts.contains(instruction.index());
		});
		account_delta.clear();
		reserve_delta.clear();
		result.reserves_settled = 0.0f;
		for(auto instruction : accepted) {
			if(!accepted_accounts.contains(instruction.index())) continue;
			auto source_rel = state.world.bank_payment_instruction_get_bank_payment_instruction_source(instruction);
			auto destination_rel = state.world.bank_payment_instruction_get_bank_payment_instruction_destination(instruction);
			auto source = state.world.bank_payment_instruction_source_get_deposit_account(source_rel);
			auto destination = state.world.bank_payment_instruction_destination_get_deposit_account(destination_rel);
			auto amount = state.world.bank_payment_instruction_get_amount(instruction);
			auto source_bank = state.world.deposit_account_get_organization_from_deposit_account_bank(source);
			auto destination_bank = state.world.deposit_account_get_organization_from_deposit_account_bank(destination);
			account_delta[source.index()] -= amount;
			account_delta[destination.index()] += amount;
			reserve_delta[source_bank.index()] -= amount;
			reserve_delta[destination_bank.index()] += amount;
			result.reserves_settled += amount;
		}
	}

	bool reserves_valid = true;
	for(auto const& [index, delta] : reserve_delta) {
		auto bank = banks_by_index.at(index);
		auto settlement = state.world.organization_get_bank_settlement_currency(bank);
		auto reserve = reserve_account_for(state, bank, settlement);
		if(!reserve) { reserves_valid = false; break; }
		reserve_opening[index] = accounts::balance(state, reserve);
		auto projected = reserve_opening[index] + delta;
		auto sheet = bank_balance_sheet(state, bank, settlement);
		double projected_deposits = sheet.deposit_liabilities;
		for(auto const& [account_index, account_delta_value] : account_delta) {
			auto account = accounts_by_index.at(account_index);
			if(state.world.deposit_account_get_organization_from_deposit_account_bank(account) == bank)
				projected_deposits += account_delta_value;
		}
		auto required = projected_deposits * std::max(
			double(state.world.organization_get_bank_liquidity_target(bank)),
			double(state.world.organization_get_bank_reserve_requirement(bank)));
		if(!std::isfinite(projected) || projected < 0.0
			|| projected > std::numeric_limits<float>::max()
			|| projected + balance_tolerance < required) {
			reserves_valid = false;
			break;
		}
	}
	if(!reserves_valid) {
		result.deferred = uint32_t(accepted.size());
		return result;
	}

	// All source deposits and net reserve movements have passed before the first
	// ledger write. Instruction order is canonical stable ID order.
	for(auto instruction : accepted) {
		auto source_rel = state.world.bank_payment_instruction_get_bank_payment_instruction_source(instruction);
		auto destination_rel = state.world.bank_payment_instruction_get_bank_payment_instruction_destination(instruction);
		auto source = state.world.bank_payment_instruction_source_get_deposit_account(source_rel);
		auto destination = state.world.bank_payment_instruction_destination_get_deposit_account(destination_rel);
		auto amount = state.world.bank_payment_instruction_get_amount(instruction);
		auto payer = state.world.deposit_account_get_economic_actor_from_deposit_account_owner(source);
		auto payee = state.world.deposit_account_get_economic_actor_from_deposit_account_owner(destination);
		auto settlement = state.world.deposit_account_get_commodity_from_deposit_account_settlement(source);
		auto transaction = relations::record_transaction(state, payer, payee, amount, settlement,
			relations::transaction_kind::transfer, today);
		assert(transaction && "validated interbank instructions must have recordable transactions");
	}
	for(auto const& [index, delta] : account_delta)
		state.world.deposit_account_set_balance(accounts_by_index.at(index),
			float(std::max(0.0, double(deposit_balance(state, accounts_by_index.at(index))) + delta)));
	for(auto const& [index, delta] : reserve_delta) {
		auto bank = banks_by_index.at(index);
		auto reserve = reserve_account_for(state, bank,
			state.world.organization_get_bank_settlement_currency(bank));
		state.world.monetary_account_set_balance(reserve,
			float(double(accounts::balance(state, reserve)) + delta));
	}
	for(auto instruction : accepted) {
		state.world.bank_payment_instruction_set_status(instruction, 1); // settled
		++result.settled;
	}
	for(auto const& [index, delta] : reserve_delta) {
		(void)delta;
		refresh_bank_state(state, banks_by_index.at(index));
	}
	return result;
}

float repay_loan(sys::state& state, dcon::obligation_id loan,
	dcon::deposit_account_id borrower_account, float amount, sys::date timestamp) {
	if(!valid_positive_amount(amount) || !borrower_account
		|| !state.world.deposit_account_is_valid(borrower_account)
		|| !valid_active_loan_for_bank(state, loan,
			state.world.deposit_account_get_organization_from_deposit_account_bank(borrower_account))) return 0.0f;
	auto borrower = state.world.deposit_account_get_economic_actor_from_deposit_account_owner(borrower_account);
	auto settlement = state.world.deposit_account_get_commodity_from_deposit_account_settlement(borrower_account);
	if(!borrower || !settlement
		|| state.world.obligation_get_economic_actor_from_obligation_debtor(loan) != borrower
		|| state.world.obligation_get_settlement_commodity(loan) != settlement
		|| settlement != state.world.organization_get_bank_settlement_currency(
			bank_for_loan(state, loan))) return 0.0f;
	auto total_due = relations::total_due(state, loan);
	auto available_balance = deposit_balance(state, borrower_account);
	if(!valid_nonnegative_amount(total_due) || !valid_nonnegative_amount(available_balance)) return 0.0f;
	auto accepted = std::min(amount, total_due);
	if(accepted <= 0.0f || available_balance < accepted) return 0.0f;
	auto transaction = relations::record_transaction(state, borrower,
		state.world.obligation_get_economic_actor_from_obligation_creditor(loan), accepted, settlement,
		relations::transaction_kind::repayment, timestamp);
	if(!transaction) return 0.0f;
	state.world.deposit_account_set_balance(borrower_account, deposit_balance(state, borrower_account) - accepted);
	auto paid = relations::repay_obligation(state, loan, accepted);
	refresh_bank_state(state, bank_for_loan(state, loan));
	return paid;
}

float accrue_loan_interest(sys::state& state, dcon::obligation_id loan, uint32_t days) {
	if(!loan || !state.world.obligation_is_valid(loan)
		|| state.world.obligation_get_kind(loan) != uint8_t(relations::obligation_kind::loan)
		|| !valid_bank(state, bank_for_loan(state, loan))
		|| state.world.obligation_get_status(loan) != uint8_t(relations::obligation_status::active)) return 0.0f;
	auto principal = state.world.obligation_get_principal_outstanding(loan);
	auto accrued = state.world.obligation_get_accrued_interest(loan);
	auto rate = state.world.obligation_get_annual_interest_rate(loan);
	auto projected = double(accrued) + double(principal) * double(rate) * double(days) / 365.0;
	if(!valid_nonnegative_amount(principal) || !valid_nonnegative_amount(accrued)
		|| !valid_nonnegative_amount(rate) || !std::isfinite(projected)
		|| projected > std::numeric_limits<float>::max()) return 0.0f;
	auto interest = relations::accrue_interest(state, loan, days);
	refresh_bank_state(state, bank_for_loan(state, loan));
	return interest;
}

bool mark_loan_defaulted(sys::state& state, dcon::obligation_id loan) {
	if(!loan || !state.world.obligation_is_valid(loan)
		|| state.world.obligation_get_kind(loan) != uint8_t(relations::obligation_kind::loan)
		|| !valid_bank(state, bank_for_loan(state, loan))
		|| state.world.obligation_get_status(loan) != uint8_t(relations::obligation_status::active)) return false;
	state.world.obligation_set_status(loan, uint8_t(relations::obligation_status::defaulted));
	refresh_bank_state(state, bank_for_loan(state, loan));
	return true;
}

bool write_off_loan(sys::state& state, dcon::obligation_id loan) {
	if(!loan || !state.world.obligation_is_valid(loan)
		|| state.world.obligation_get_kind(loan) != uint8_t(relations::obligation_kind::loan)
		|| !valid_bank(state, bank_for_loan(state, loan))) return false;
	auto status = state.world.obligation_get_status(loan);
	if(status != uint8_t(relations::obligation_status::defaulted)) return false;
	auto exposure = relations::total_due(state, loan);
	state.world.obligation_set_written_off_amount(loan, exposure);
	state.world.obligation_set_recovered_amount(loan, 0.0f);
	state.world.obligation_set_principal_outstanding(loan, 0.0f);
	state.world.obligation_set_accrued_interest(loan, 0.0f);
	state.world.obligation_set_status(loan, uint8_t(relations::obligation_status::written_off));
	refresh_bank_state(state, bank_for_loan(state, loan));
	return true;
}

float resolve_defaulted_loan(sys::state& state, dcon::obligation_id loan,
	dcon::monetary_account_id recovery_source, float requested_recovery, sys::date timestamp) {
	if(!loan || !state.world.obligation_is_valid(loan) || !recovery_source
		|| !state.world.monetary_account_is_valid(recovery_source)
		|| !valid_nonnegative_amount(requested_recovery)
		|| state.world.obligation_get_kind(loan) != uint8_t(relations::obligation_kind::loan)
		|| state.world.obligation_get_status(loan) != uint8_t(relations::obligation_status::defaulted)) return 0.0f;
	auto bank = bank_for_loan(state, loan);
	auto borrower = state.world.obligation_get_economic_actor_from_obligation_debtor(loan);
	auto lender = state.world.obligation_get_economic_actor_from_obligation_creditor(loan);
	auto settlement = state.world.obligation_get_settlement_commodity(loan);
	if(!valid_bank(state, bank) || accounts::owner_of(state, recovery_source) != borrower
		|| accounts::settlement_of(state, recovery_source) != settlement
		|| settlement != state.world.organization_get_bank_settlement_currency(bank)) return 0.0f;
	auto reserve = reserve_account_for(state, bank, settlement);
	if(!reserve) return 0.0f;
	auto exposure = relations::total_due(state, loan);
	auto source_balance = accounts::balance(state, recovery_source);
	auto reserve_balance = accounts::balance(state, reserve);
	if(!valid_nonnegative_amount(exposure) || !valid_nonnegative_amount(source_balance)
		|| !valid_nonnegative_amount(reserve_balance)) return 0.0f;
	auto recovery = std::min({ requested_recovery, exposure, source_balance });
	if(double(reserve_balance) + double(recovery) > std::numeric_limits<float>::max()) return 0.0f;
	if(recovery > 0.0f) {
		auto transaction = relations::record_transaction(state, borrower, lender, recovery, settlement,
			relations::transaction_kind::repayment, timestamp);
		if(!transaction) return 0.0f;
		state.world.monetary_account_set_balance(recovery_source,
			accounts::balance(state, recovery_source) - recovery);
		state.world.monetary_account_set_balance(reserve,
			accounts::balance(state, reserve) + recovery);
	}
	auto loss = std::max(0.0f, exposure - recovery);
	state.world.obligation_set_recovered_amount(loan, recovery);
	state.world.obligation_set_written_off_amount(loan, loss);
	state.world.obligation_set_principal_outstanding(loan, 0.0f);
	state.world.obligation_set_accrued_interest(loan, 0.0f);
	state.world.obligation_set_status(loan, loss <= balance_tolerance
		? uint8_t(relations::obligation_status::recovered)
		: uint8_t(relations::obligation_status::written_off));
	refresh_bank_state(state, bank);
	return recovery;
}

float indicative_factory_loan_rate(sys::state const& state, dcon::factory_id factory,
	dcon::commodity_id settlement, float requested_amount, float collateral_value) {
	if(!factory || !state.world.factory_is_valid(factory) || !settlement
		|| !valid_positive_amount(requested_amount) || !valid_nonnegative_amount(collateral_value))
		return std::numeric_limits<float>::infinity();
	auto borrower = actors::organizations::operator_actor_for_factory(state, factory);
	if(!borrower) return std::numeric_limits<float>::infinity();
	float best_rate = std::numeric_limits<float>::infinity();
	uint64_t best_bank_id = std::numeric_limits<uint64_t>::max();
	std::vector<dcon::organization_id> banks;
	state.world.for_each_organization([&](dcon::organization_id bank) {
		if(policy_configured(state, bank)
			&& state.world.organization_get_bank_settlement_currency(bank) == settlement)
			banks.push_back(bank);
	});
	std::sort(banks.begin(), banks.end(), [&](auto left, auto right) {
		return state.world.organization_get_canonical_id(left) < state.world.organization_get_canonical_id(right);
	});
	for(auto bank : banks) {
		if(bank_credit_capacity(state, bank, borrower, settlement, false) + balance_tolerance < requested_amount) continue;
		auto rate = bank_base_rate(state, bank) + state.world.organization_get_bank_lending_spread(bank);
		auto stable_id = state.world.organization_get_canonical_id(bank);
		if(rate < best_rate || (rate == best_rate && stable_id < best_bank_id)) {
			best_rate = rate;
			best_bank_id = stable_id;
		}
	}
	return best_rate;
}

factory_credit_result underwrite_factory_credit(sys::state& state, dcon::factory_id factory,
	dcon::monetary_account_id operating_account, uint8_t request_kind, float requested_amount,
	float expected_annual_return, float collateral_value, dcon::capital_project_id project) {
	factory_credit_result result{};
	if(!factory || !state.world.factory_is_valid(factory) || !operating_account
		|| !state.world.monetary_account_is_valid(operating_account)
		|| !valid_positive_amount(requested_amount) || !std::isfinite(expected_annual_return)
		|| !valid_nonnegative_amount(collateral_value)) return result;
	auto borrower = actors::organizations::operator_actor_for_factory(state, factory);
	auto settlement = accounts::settlement_of(state, operating_account);
	if(!borrower || accounts::owner_of(state, operating_account) != borrower || !settlement) return result;
	result.requested_amount = requested_amount;
	auto request_key = stable_hash(id_key("firm-capital-request",
		state.world.factory_get_canonical_id(factory), state.current_date.to_raw_value(), request_kind));
	bool duplicate_request = false;
	state.world.for_each_firm_capital_request([&](dcon::firm_capital_request_id existing) {
		if(state.world.firm_capital_request_get_canonical_id(existing) == request_key)
			duplicate_request = true;
	});
	if(duplicate_request) return result;
	auto request = state.world.create_firm_capital_request();
	result.request = request;
	state.world.firm_capital_request_set_canonical_id(request, request_key);
	state.world.firm_capital_request_set_request_kind(request, request_kind);
	state.world.firm_capital_request_set_status(request, 0);
	state.world.firm_capital_request_set_settlement(request, settlement);
	state.world.firm_capital_request_set_requested_amount(request, requested_amount);
	state.world.firm_capital_request_set_funded_amount(request, 0.0f);
	state.world.firm_capital_request_set_annual_interest_rate(request, 0.0f);
	state.world.firm_capital_request_set_expected_annual_return(request, expected_annual_return);
	state.world.firm_capital_request_set_collateral_value(request, collateral_value);
	state.world.firm_capital_request_set_underwriting_score(request, 0.0f);
	state.world.firm_capital_request_set_requested_on(request, state.current_date);
	state.world.firm_capital_request_set_maturity_date(request, state.current_date + 365);
	state.world.force_create_firm_capital_request_factory(request, factory);
	if(project) state.world.force_create_firm_capital_request_project(request, project);

	double existing_debt = 0.0;
	uint32_t defaulted_loans = 0;
	std::vector<dcon::obligation_id> borrower_loans;
	state.world.economic_actor_for_each_obligation_debtor_as_economic_actor(borrower,
		[&](dcon::obligation_debtor_id relation) {
			auto loan = state.world.obligation_debtor_get_obligation(relation);
			if(loan && state.world.obligation_get_kind(loan) == uint8_t(relations::obligation_kind::loan)
				&& state.world.obligation_get_settlement_commodity(loan) == settlement
				&& state.world.obligation_get_factory_from_obligation_factory(loan) == factory)
				borrower_loans.push_back(loan);
		});
	std::sort(borrower_loans.begin(), borrower_loans.end(), [&](auto left, auto right) {
		return state.world.obligation_get_canonical_id(left) < state.world.obligation_get_canonical_id(right);
	});
	for(auto loan : borrower_loans) {
		auto status = state.world.obligation_get_status(loan);
		if(status == uint8_t(relations::obligation_status::active)
			|| status == uint8_t(relations::obligation_status::defaulted)) {
			auto due = relations::total_due(state, loan);
			if(!valid_nonnegative_amount(due)) {
				state.world.firm_capital_request_set_status(request, 3);
				return result;
			}
			existing_debt += double(due);
		}
		if(status == uint8_t(relations::obligation_status::defaulted)
			|| status == uint8_t(relations::obligation_status::written_off)) ++defaulted_loans;
	}
	auto realized_cashflow = state.world.factory_get_agency_cashflow(factory);
	auto recent_profit = state.world.factory_get_agency_recent_profit(factory);
	if(!std::isfinite(realized_cashflow) || !std::isfinite(recent_profit)) {
		state.world.firm_capital_request_set_status(request, 3);
		return result;
	}
	double daily_cashflow = std::max(0.0, std::min(double(realized_cashflow), double(recent_profit)));
	double annual_cashflow = daily_cashflow * 365.0;
	float cashflow_coverage = float(std::clamp(annual_cashflow / double(requested_amount), 0.0, 1.0));
	float collateral_coverage = float(std::clamp(double(collateral_value) / double(requested_amount), 0.0, 1.0));
	float repayment_history = defaulted_loans == 0 ? 1.0f : 0.0f;
	float business_health = daily_cashflow > 0.0
		&& state.world.factory_get_agency_distress_days(factory) == 0 ? 1.0f : 0.0f;
	double borrower_headroom_exact = std::max(0.0, annual_cashflow + double(collateral_value) - existing_debt);
	float borrower_headroom = float(std::min(borrower_headroom_exact,
		double(std::numeric_limits<float>::max())));

	struct bank_offer { dcon::organization_id bank{}; float amount = 0.0f; float rate = 0.0f; float score = 0.0f; uint64_t id = 0; };
	std::vector<dcon::organization_id> banks;
	state.world.for_each_organization([&](dcon::organization_id candidate) {
		if(policy_configured(state, candidate)
			&& state.world.organization_get_bank_settlement_currency(candidate) == settlement)
			banks.push_back(candidate);
	});
	std::sort(banks.begin(), banks.end(), [&](auto left, auto right) {
		return state.world.organization_get_canonical_id(left) < state.world.organization_get_canonical_id(right);
	});
	std::vector<bank_offer> offers;
	for(auto candidate : banks) {
		auto rate = bank_base_rate(state, candidate)
			+ state.world.organization_get_bank_lending_spread(candidate);
		float return_coverage = expected_annual_return > rate ? 1.0f : 0.0f;
		float score = std::min({ cashflow_coverage, collateral_coverage, repayment_history,
			business_health, return_coverage });
		auto appetite = state.world.organization_get_bank_risk_appetite(candidate);
		if(status_of(state, candidate) != bank_status::solvent
			|| score + balance_tolerance < 1.0f - appetite) continue;
		auto bank_headroom = bank_credit_capacity(state, candidate, borrower, settlement, false);
		auto approved = std::min({ requested_amount, borrower_headroom, bank_headroom });
		if(approved <= balance_tolerance) continue;
		offers.push_back({ candidate, approved, rate, score,
			state.world.organization_get_canonical_id(candidate) });
	}
	std::sort(offers.begin(), offers.end(), [](bank_offer const& left, bank_offer const& right) {
		if(left.amount != right.amount) return left.amount > right.amount;
		if(left.rate != right.rate) return left.rate < right.rate;
		return left.id < right.id;
	});
	if(offers.empty()) {
		state.world.firm_capital_request_set_status(request, 3);
		return result;
	}
	auto const best = offers.front();
	state.world.force_create_firm_capital_request_bank(request, best.bank);
	state.world.firm_capital_request_set_underwriting_score(request, best.score);
	state.world.firm_capital_request_set_annual_interest_rate(request, best.rate);
	result.underwriting_score = best.score;
	result.annual_interest_rate = best.rate;
	auto approved = best.amount;
	auto obligation = originate_factory_loan_to_operating_account(state, best.bank, factory,
		operating_account, approved, best.rate,
		stable_hash(id_key("loan-request", request_key)), collateral_value);
	if(!obligation) {
		state.world.firm_capital_request_set_status(request, 3);
		return result;
	}
	state.world.force_create_firm_capital_request_obligation(request, obligation);
	state.world.firm_capital_request_set_funded_amount(request, approved);
	state.world.firm_capital_request_set_status(request,
		approved + 1.0e-5f < requested_amount ? 2 : 1); // partial / full
	result.obligation = obligation;
	result.funded_amount = approved;
	return result;
}

loan_service_result service_actor_loans(sys::state& state, dcon::economic_actor_id borrower,
	sys::date today, uint32_t default_grace_days) {
	loan_service_result result{};
	if(!borrower || !state.world.economic_actor_is_valid(borrower)) return result;
	std::vector<dcon::obligation_id> loans;
	state.world.economic_actor_for_each_obligation_debtor_as_economic_actor(borrower,
		[&](dcon::obligation_debtor_id relation) {
			auto loan = state.world.obligation_debtor_get_obligation(relation);
			if(loan && state.world.obligation_is_valid(loan)
				&& state.world.obligation_get_kind(loan) == uint8_t(relations::obligation_kind::loan)
				&& state.world.obligation_get_economic_actor_from_obligation_debtor(loan) == borrower)
				loans.push_back(loan);
		});
	std::sort(loans.begin(), loans.end(), [&](auto left, auto right) {
		return state.world.obligation_get_canonical_id(left) < state.world.obligation_get_canonical_id(right);
	});
	for(auto loan : loans) {
		auto status = state.world.obligation_get_status(loan);
		if(status == uint8_t(relations::obligation_status::defaulted)) {
			++result.defaulted_loans;
			auto factory = state.world.obligation_get_factory_from_obligation_factory(loan);
			if(factory) result.defaulted_factories.push_back(factory);
			else result.has_unscoped_default = true;
			continue;
		}
		if(status != uint8_t(relations::obligation_status::active)) continue;
		auto bank = bank_for_loan(state, loan);
		if(!policy_configured(state, bank)) continue;
		auto last_accrual = state.world.obligation_get_last_interest_accrual_date(loan);
		if(!last_accrual) last_accrual = state.world.obligation_get_creation_date(loan);
		auto elapsed = today.to_raw_value() > last_accrual.to_raw_value()
			? uint32_t(today.to_raw_value() - last_accrual.to_raw_value()) : 0u;
		if(elapsed > 0) {
			result.interest_accrued += accrue_loan_interest(state, loan, elapsed);
			state.world.obligation_set_last_interest_accrual_date(loan, today);
		}
		auto due_date = state.world.obligation_get_due_date(loan);
		if(!due_date || today < due_date) continue;
		auto settlement = state.world.obligation_get_settlement_commodity(loan);
		if(!settlement || settlement != state.world.organization_get_bank_settlement_currency(bank)) continue;

		std::vector<dcon::deposit_account_id> deposits;
		state.world.organization_for_each_deposit_account_bank_as_organization(bank,
			[&](dcon::deposit_account_bank_id relation) {
				auto account = state.world.deposit_account_bank_get_deposit_account(relation);
				if(account && state.world.deposit_account_get_economic_actor_from_deposit_account_owner(account) == borrower
					&& state.world.deposit_account_get_commodity_from_deposit_account_settlement(account) == settlement)
					deposits.push_back(account);
			});
		std::sort(deposits.begin(), deposits.end(), [&](auto left, auto right) {
			return state.world.deposit_account_get_canonical_id(left) < state.world.deposit_account_get_canonical_id(right);
		});
		for(auto account : deposits) {
			if(relations::total_due(state, loan) <= balance_tolerance) break;
			auto payment = std::min(deposit_balance(state, account), relations::total_due(state, loan));
			if(payment > balance_tolerance) result.amount_repaid += repay_loan(state, loan, account, payment, today);
		}

		if(relations::total_due(state, loan) > balance_tolerance) {
			auto operating = accounts::find_account(state, borrower, settlement);
			auto reserve = reserve_account_for(state, bank, settlement);
			if(operating && reserve) {
				auto payment = std::min(accounts::balance(state, operating), relations::total_due(state, loan));
				if(payment > balance_tolerance && accounts::settle_obligation_payment(state, loan,
					operating, reserve, payment, today)) {
					result.amount_repaid += payment;
					refresh_bank_state(state, bank);
				}
			}
		}
		if(relations::total_due(state, loan) > balance_tolerance
			&& today.to_raw_value() - due_date.to_raw_value() >= default_grace_days) {
			if(mark_loan_defaulted(state, loan)) {
				++result.defaulted_loans;
				auto factory = state.world.obligation_get_factory_from_obligation_factory(loan);
				if(factory) result.defaulted_factories.push_back(factory);
				else result.has_unscoped_default = true;
			}
		}
	}
	std::sort(result.defaulted_factories.begin(), result.defaulted_factories.end(), [&](auto left, auto right) {
		return state.world.factory_get_canonical_id(left) < state.world.factory_get_canonical_id(right);
	});
	result.defaulted_factories.erase(std::unique(result.defaulted_factories.begin(), result.defaulted_factories.end()),
		result.defaulted_factories.end());
	return result;
}

balance_sheet bank_balance_sheet(sys::state const& state, dcon::organization_id bank,
	dcon::commodity_id settlement) {
	balance_sheet result{};
	if(!valid_bank(state, bank) || !settlement || !state.world.commodity_is_valid(settlement)
		|| (policy_configured(state, bank)
			&& state.world.organization_get_bank_settlement_currency(bank) != settlement)) return result;
	std::vector<dcon::monetary_account_id> reserves;
	state.world.organization_for_each_monetary_account_reserve_bank_as_organization(bank,
		[&](dcon::monetary_account_reserve_bank_id relation) {
			auto account = state.world.monetary_account_reserve_bank_get_monetary_account(relation);
			if(account && economy::accounts::settlement_of(state, account) == settlement) reserves.push_back(account);
		});
	std::sort(reserves.begin(), reserves.end(), [&](auto left, auto right) {
		return state.world.monetary_account_get_canonical_id(left) < state.world.monetary_account_get_canonical_id(right);
	});
	double reserve_total = 0.0;
	for(auto account : reserves) reserve_total += economy::accounts::balance(state, account);
	result.settlement_assets = float(reserve_total);
	std::vector<dcon::deposit_account_id> deposits;
	state.world.organization_for_each_deposit_account_bank_as_organization(bank,
		[&](dcon::deposit_account_bank_id relation) {
			auto account = state.world.deposit_account_bank_get_deposit_account(relation);
			if(account && state.world.deposit_account_get_commodity_from_deposit_account_settlement(account) == settlement)
				deposits.push_back(account);
		});
	std::sort(deposits.begin(), deposits.end(), [&](auto left, auto right) {
		return state.world.deposit_account_get_canonical_id(left) < state.world.deposit_account_get_canonical_id(right);
	});
	double deposit_total = 0.0;
	for(auto account : deposits) deposit_total += deposit_balance(state, account);
	result.deposit_liabilities = float(deposit_total);
	std::vector<dcon::obligation_id> creditor_obligations;
	auto bank_actor = actors::organizations::actor_for_organization(state, bank);
	state.world.economic_actor_for_each_obligation_creditor_as_economic_actor(bank_actor,
		[&](dcon::obligation_creditor_id relation) {
			auto loan = state.world.obligation_creditor_get_obligation(relation);
			if(loan && state.world.obligation_get_settlement_commodity(loan) == settlement)
				creditor_obligations.push_back(loan);
		});
	std::sort(creditor_obligations.begin(), creditor_obligations.end(), [&](auto left, auto right) {
		return state.world.obligation_get_canonical_id(left) < state.world.obligation_get_canonical_id(right);
	});
	double performing_principal = 0.0, interest_receivable = 0.0, public_debt = 0.0;
	for(auto obligation : creditor_obligations) {
		auto status = state.world.obligation_get_status(obligation);
		if(status != uint8_t(relations::obligation_status::active)) continue;
		auto principal = state.world.obligation_get_principal_outstanding(obligation);
		auto interest = state.world.obligation_get_accrued_interest(obligation);
		if(state.world.obligation_get_kind(obligation) == uint8_t(relations::obligation_kind::loan)) {
			performing_principal += principal;
			interest_receivable += interest;
		} else if(state.world.obligation_get_kind(obligation) == uint8_t(relations::obligation_kind::public_debt)) {
			public_debt += principal + interest;
		}
	}
	result.loan_assets = float(performing_principal);
	result.accrued_interest_receivable = float(interest_receivable);
	result.public_debt_assets = float(public_debt);
	std::vector<dcon::obligation_id> bank_debts;
	state.world.economic_actor_for_each_obligation_debtor_as_economic_actor(bank_actor,
		[&](dcon::obligation_debtor_id relation) {
			auto obligation = state.world.obligation_debtor_get_obligation(relation);
			if(obligation && state.world.obligation_get_settlement_commodity(obligation) == settlement)
				bank_debts.push_back(obligation);
		});
	std::sort(bank_debts.begin(), bank_debts.end(), [&](auto left, auto right) {
		return state.world.obligation_get_canonical_id(left) < state.world.obligation_get_canonical_id(right);
	});
	double liabilities = 0.0;
	for(auto obligation : bank_debts) {
		auto status = state.world.obligation_get_status(obligation);
		if(status == uint8_t(relations::obligation_status::active)
			|| status == uint8_t(relations::obligation_status::defaulted))
			liabilities += relations::total_due(state, obligation);
	}
	result.other_financial_liabilities = float(liabilities);
	result.total_assets = result.settlement_assets + result.loan_assets
		+ result.accrued_interest_receivable + result.public_debt_assets;
	result.total_liabilities = result.deposit_liabilities + result.other_financial_liabilities;
	result.net_worth = result.total_assets - result.total_liabilities;
	result.capital_ratio = result.total_assets > 0.0f ? result.net_worth / result.total_assets : 0.0f;
	result.liquidity_ratio = result.deposit_liabilities > 0.0f
		? result.settlement_assets / result.deposit_liabilities : 1.0f;
	result.required_liquidity = policy_configured(state, bank) ? required_reserves(result, state, bank) : 0.0f;
	return result;
}

void update_bank_statuses(sys::state& state, sys::date today) {
	(void)today;
	std::vector<dcon::organization_id> banks;
	state.world.for_each_organization([&](dcon::organization_id bank) {
		if(valid_bank(state, bank)) banks.push_back(bank);
	});
	std::sort(banks.begin(), banks.end(), [&](auto left, auto right) {
		return state.world.organization_get_canonical_id(left) < state.world.organization_get_canonical_id(right);
	});
	for(auto bank : banks) {
		sync_bank_equity(state, bank);
		if(state.world.organization_get_bank_status(bank) == uint8_t(bank_status::insolvent)) continue;
		if(!policy_configured(state, bank)) {
			state.world.organization_set_bank_status(bank, uint8_t(bank_status::constrained));
			continue;
		}
		auto settlement = state.world.organization_get_bank_settlement_currency(bank);
		auto sheet = bank_balance_sheet(state, bank, settlement);
		if(!std::isfinite(sheet.total_assets) || !std::isfinite(sheet.total_liabilities)
			|| !std::isfinite(sheet.net_worth) || sheet.net_worth < -balance_tolerance) {
			state.world.organization_set_bank_status(bank, uint8_t(bank_status::insolvent));
			continue;
		}
		bool capital_breach = sheet.capital_ratio + balance_tolerance
			< state.world.organization_get_bank_minimum_capital_ratio(bank);
		bool liquidity_breach = sheet.liquidity_ratio + balance_tolerance
			< std::max(state.world.organization_get_bank_liquidity_target(bank),
				state.world.organization_get_bank_reserve_requirement(bank))
			|| sheet.settlement_assets + balance_tolerance < sheet.required_liquidity;
		if(!capital_breach && !liquidity_breach) {
			state.world.organization_set_bank_capital_breach_days(bank, 0);
			state.world.organization_set_bank_status(bank, uint8_t(bank_status::solvent));
			continue;
		}
		auto grace = state.world.organization_get_bank_capital_breach_grace_days(bank);
		auto days = state.world.organization_get_bank_capital_breach_days(bank);
		if(days < std::numeric_limits<uint16_t>::max()) ++days;
		state.world.organization_set_bank_capital_breach_days(bank, days);
		state.world.organization_set_bank_status(bank,
			grace == 0 || days >= grace ? uint8_t(bank_status::insolvent)
				: uint8_t(bank_status::constrained));
	}
}

bool validate_canonical_banking_state(sys::state const& state, std::vector<std::string>& errors) {
	errors.clear();
	auto add_error = [&](std::string message) { errors.push_back(std::move(message)); };
	std::unordered_set<uint64_t> bank_ids, bank_actor_ids, bank_equity_ids, reserve_ids,
		deposit_ids, obligation_ids, instruction_ids;
	std::set<std::tuple<uint32_t, uint32_t, uint32_t>> deposit_keys;
	std::unordered_set<uint32_t> linked_reserve_accounts, linked_deposit_accounts;
	std::vector<dcon::organization_id> banks;
	state.world.for_each_organization([&](dcon::organization_id bank) {
		if(state.world.organization_get_kind(bank) == uint8_t(actor_kind::bank)) banks.push_back(bank);
	});
	std::sort(banks.begin(), banks.end(), [&](auto left, auto right) {
		return state.world.organization_get_canonical_id(left) < state.world.organization_get_canonical_id(right);
	});
	for(auto bank : banks) {
		auto bank_id = state.world.organization_get_canonical_id(bank);
		auto actor = actors::organizations::actor_for_organization(state, bank);
		if(!valid_bank(state, bank)) add_error("bank dcon:" + std::to_string(bank.index()) + " has no valid bank actor");
		if(bank_id == 0 || !bank_ids.insert(bank_id).second)
			add_error("bank dcon:" + std::to_string(bank.index()) + " has a missing or duplicated stable ID");
		auto bank_actor_id = actor ? state.world.economic_actor_get_canonical_id(actor) : 0;
		if(bank_actor_id == 0 || !bank_actor_ids.insert(bank_actor_id).second)
			add_error("bank dcon:" + std::to_string(bank.index()) + " has no stable actor ID");
		auto equity_asset = actors::organizations::equity_asset_for_organization(state, bank);
		auto equity_id = equity_asset && state.world.asset_is_valid(equity_asset)
			? state.world.asset_get_canonical_id(equity_asset) : 0;
		if(equity_id == 0 || !bank_equity_ids.insert(equity_id).second)
			add_error("bank " + std::to_string(bank_id) + " has no stable equity asset");
		if(equity_asset && state.world.asset_is_valid(equity_asset)) {
			auto value = state.world.asset_get_appraised_value(equity_asset);
			if(!valid_nonnegative_amount(value))
				add_error("bank " + std::to_string(bank_id) + " has an invalid equity asset valuation");
		}
		if(!policy_configured(state, bank)) {
			add_error("bank dcon:" + std::to_string(bank.index()) + " has no authored settlement currency or jurisdiction policy");
			continue;
		}
		auto policy = bank_policy{
			state.world.organization_get_bank_jurisdiction(bank),
			state.world.organization_get_bank_settlement_currency(bank),
			state.world.organization_get_lending_base_rate(bank),
			state.world.organization_get_bank_minimum_capital_ratio(bank),
			state.world.organization_get_bank_liquidity_target(bank),
			state.world.organization_get_bank_risk_appetite(bank),
			state.world.organization_get_bank_lending_spread(bank),
			state.world.organization_get_bank_max_single_borrower_exposure(bank),
			state.world.organization_get_bank_reserve_requirement(bank),
			state.world.organization_get_bank_capital_breach_grace_days(bank)
		};
		if(!std::isfinite(policy.lending_base_rate) || policy.lending_base_rate < 0.0f || policy.lending_base_rate > 1.0f
			|| policy.lending_base_rate + policy.lending_spread > 1.0f
			|| !fraction(policy.minimum_capital_ratio) || !fraction(policy.liquidity_target)
			|| !fraction(policy.risk_appetite) || !fraction(policy.lending_spread)
			|| !fraction(policy.max_single_borrower_exposure) || !fraction(policy.reserve_requirement))
			add_error("bank " + std::to_string(bank_id) + " has invalid authored lending policy values");
		auto status = state.world.organization_get_bank_status(bank);
		if(status > uint8_t(bank_status::insolvent)) add_error("bank " + std::to_string(bank_id) + " has an invalid insolvency state");
		auto sheet = bank_balance_sheet(state, bank, policy.settlement);
		if(!std::isfinite(sheet.settlement_assets) || !std::isfinite(sheet.loan_assets)
			|| !std::isfinite(sheet.accrued_interest_receivable) || !std::isfinite(sheet.public_debt_assets)
			|| !std::isfinite(sheet.deposit_liabilities) || !std::isfinite(sheet.other_financial_liabilities)
			|| !std::isfinite(sheet.total_assets) || !std::isfinite(sheet.total_liabilities)
			|| !std::isfinite(sheet.net_worth) || !std::isfinite(sheet.capital_ratio)
			|| !std::isfinite(sheet.liquidity_ratio) || !std::isfinite(sheet.required_liquidity))
			add_error("bank " + std::to_string(bank_id) + " has a non-finite balance sheet value");
		if(sheet.settlement_assets < -balance_tolerance || sheet.loan_assets < -balance_tolerance
			|| sheet.accrued_interest_receivable < -balance_tolerance || sheet.public_debt_assets < -balance_tolerance
			|| sheet.deposit_liabilities < -balance_tolerance || sheet.other_financial_liabilities < -balance_tolerance)
			add_error("bank " + std::to_string(bank_id) + " has a negative asset or liability balance");
		auto identity_error = std::abs(sheet.total_assets - sheet.total_liabilities - sheet.net_worth);
		auto tolerance = balance_tolerance * std::max(1.0f, std::abs(sheet.total_assets));
		if(identity_error > tolerance) add_error("bank " + std::to_string(bank_id) + " fails assets = liabilities + equity");
		if(sheet.net_worth < -balance_tolerance && status != uint8_t(bank_status::insolvent))
			add_error("bank " + std::to_string(bank_id) + " has negative equity but is not insolvent");
		uint32_t reserve_count = 0;
		state.world.organization_for_each_monetary_account_reserve_bank_as_organization(bank,
			[&](dcon::monetary_account_reserve_bank_id relation) {
				auto account = state.world.monetary_account_reserve_bank_get_monetary_account(relation);
				if(!account || !state.world.monetary_account_is_valid(account)) {
					add_error("bank " + std::to_string(bank_id) + " has an orphan reserve account relation");
					return;
				}
				++reserve_count;
			if(!linked_reserve_accounts.insert(account.index()).second)
				add_error("reserve account dcon:" + std::to_string(account.index()) + " is linked to multiple banks");
				auto id = state.world.monetary_account_get_canonical_id(account);
				if(id == 0 || !reserve_ids.insert(id).second)
					add_error("reserve account dcon:" + std::to_string(account.index()) + " has a missing or duplicated stable ID");
				if(economy::accounts::owner_of(state, account) != actor
					|| economy::accounts::settlement_of(state, account) != policy.settlement)
					add_error("reserve account dcon:" + std::to_string(account.index()) + " has an invalid bank owner or currency");
				auto balance = economy::accounts::balance(state, account);
				if(!std::isfinite(balance) || balance < 0.0f)
					add_error("reserve account dcon:" + std::to_string(account.index()) + " has an invalid balance");
			});
		if(reserve_count != 1) add_error("bank " + std::to_string(bank_id) + " must have exactly one reserve account in its settlement currency");
		state.world.organization_for_each_deposit_account_bank_as_organization(bank,
			[&](dcon::deposit_account_bank_id relation) {
				auto account = state.world.deposit_account_bank_get_deposit_account(relation);
				if(!account || !state.world.deposit_account_is_valid(account)) {
					add_error("bank " + std::to_string(bank_id) + " has an orphan customer deposit relation");
					return;
				}
				if(!linked_deposit_accounts.insert(account.index()).second)
					add_error("deposit account dcon:" + std::to_string(account.index()) + " is linked to multiple banks");
				auto id = state.world.deposit_account_get_canonical_id(account);
				if(id == 0 || !deposit_ids.insert(id).second)
					add_error("deposit account dcon:" + std::to_string(account.index()) + " has a missing or duplicated stable ID");
				auto owner = state.world.deposit_account_get_economic_actor_from_deposit_account_owner(account);
				auto currency = state.world.deposit_account_get_commodity_from_deposit_account_settlement(account);
				if(!owner || !state.world.economic_actor_is_valid(owner)
					|| !currency || !state.world.commodity_is_valid(currency)
					|| currency != policy.settlement)
					add_error("deposit account dcon:" + std::to_string(account.index()) + " has an invalid owner or settlement currency");
				else if(state.world.economic_actor_get_canonical_id(owner) == 0)
					add_error("deposit account dcon:" + std::to_string(account.index()) + " owner has no stable actor ID");
				auto balance = deposit_balance(state, account);
				if(!std::isfinite(balance) || balance < 0.0f)
					add_error("deposit account dcon:" + std::to_string(account.index()) + " has an invalid balance");
				if(owner && currency && !deposit_keys.emplace(bank.index(), owner.index(), currency.index()).second)
					add_error("bank " + std::to_string(bank_id) + " has duplicate customer accounts for one owner and currency");
			});
	}
	state.world.for_each_monetary_account([&](dcon::monetary_account_id account) {
		if(state.world.monetary_account_get_organization_from_monetary_account_reserve_bank(account)
			&& !linked_reserve_accounts.contains(account.index()))
			add_error("reserve account dcon:" + std::to_string(account.index()) + " is not linked to a configured bank");
	});
	state.world.for_each_deposit_account([&](dcon::deposit_account_id account) {
		if(!linked_deposit_accounts.contains(account.index()))
			add_error("deposit account dcon:" + std::to_string(account.index()) + " is not linked to a configured bank");
	});
	state.world.for_each_obligation([&](dcon::obligation_id loan) {
		auto debtor = state.world.obligation_get_economic_actor_from_obligation_debtor(loan);
		auto creditor = state.world.obligation_get_economic_actor_from_obligation_creditor(loan);
		auto settlement = state.world.obligation_get_settlement_commodity(loan);
		auto status = state.world.obligation_get_status(loan);
		auto kind = state.world.obligation_get_kind(loan);
		if(!debtor || !state.world.economic_actor_is_valid(debtor)
			|| !creditor || !state.world.economic_actor_is_valid(creditor)
			|| !settlement || !state.world.commodity_is_valid(settlement))
			add_error("obligation dcon:" + std::to_string(loan.index()) + " has an orphan debtor, creditor, or settlement currency");
		if(status > uint8_t(relations::obligation_status::recovered))
			add_error("obligation dcon:" + std::to_string(loan.index()) + " has an invalid status");
		if(!valid_nonnegative_amount(state.world.obligation_get_original_principal(loan))
			|| state.world.obligation_get_original_principal(loan) <= 0.0f
			|| !valid_nonnegative_amount(state.world.obligation_get_principal_outstanding(loan))
			|| !valid_nonnegative_amount(state.world.obligation_get_accrued_interest(loan))
			|| !std::isfinite(state.world.obligation_get_annual_interest_rate(loan))
			|| state.world.obligation_get_annual_interest_rate(loan) < 0.0f)
			add_error("obligation dcon:" + std::to_string(loan.index()) + " has invalid principal, interest, or rate");
		if(kind != uint8_t(relations::obligation_kind::loan)) return;
		if(!valid_nonnegative_amount(relations::total_due(state, loan)))
			add_error("loan dcon:" + std::to_string(loan.index()) + " has a non-finite total exposure");
		if(!state.world.obligation_get_creation_date(loan)
			|| !state.world.obligation_get_due_date(loan)
			|| state.world.obligation_get_due_date(loan) < state.world.obligation_get_creation_date(loan)
			|| (state.world.obligation_get_last_interest_accrual_date(loan)
				&& state.world.obligation_get_last_interest_accrual_date(loan)
					< state.world.obligation_get_creation_date(loan)))
			add_error("loan dcon:" + std::to_string(loan.index()) + " has invalid origination, accrual, or maturity dates");
		if(state.world.economic_actor_get_canonical_id(debtor) == 0
			|| state.world.economic_actor_get_canonical_id(creditor) == 0)
			add_error("loan dcon:" + std::to_string(loan.index()) + " debtor or creditor has no stable actor ID");
		auto bank = actors::organizations::organization_for_actor(state, creditor);
		if(!valid_bank(state, bank) || !policy_configured(state, bank)
			|| settlement != state.world.organization_get_bank_settlement_currency(bank))
			add_error("loan dcon:" + std::to_string(loan.index()) + " has no valid creditor bank or matching settlement currency");
		if(!valid_nonnegative_amount(state.world.obligation_get_collateral_value(loan))
			|| !valid_nonnegative_amount(state.world.obligation_get_recovered_amount(loan))
			|| !valid_nonnegative_amount(state.world.obligation_get_written_off_amount(loan)))
			add_error("loan dcon:" + std::to_string(loan.index()) + " has invalid collateral, recovery, or write-off values");
		if(status == uint8_t(relations::obligation_status::active)
			&& (state.world.obligation_get_recovered_amount(loan) > balance_tolerance
				|| state.world.obligation_get_written_off_amount(loan) > balance_tolerance))
			add_error("performing loan dcon:" + std::to_string(loan.index()) + " already records recovery or write-off");
		if((status == uint8_t(relations::obligation_status::written_off)
			|| status == uint8_t(relations::obligation_status::recovered))
			&& (state.world.obligation_get_principal_outstanding(loan) > balance_tolerance
				|| state.world.obligation_get_accrued_interest(loan) > balance_tolerance))
			add_error("resolved loan dcon:" + std::to_string(loan.index()) + " still carries principal or interest");
		auto id = state.world.obligation_get_canonical_id(loan);
		if(id == 0 || !obligation_ids.insert(id).second)
			add_error("bank loan dcon:" + std::to_string(loan.index()) + " has a missing or duplicated stable ID");
	});
	state.world.for_each_bank_payment_instruction([&](dcon::bank_payment_instruction_id instruction) {
		auto id = state.world.bank_payment_instruction_get_canonical_id(instruction);
		if(id == 0 || !instruction_ids.insert(id).second)
			add_error("interbank instruction dcon:" + std::to_string(instruction.index()) + " has a missing or duplicated stable ID");
		if(state.world.bank_payment_instruction_get_status(instruction) > 2
			|| !valid_positive_amount(state.world.bank_payment_instruction_get_amount(instruction)))
			add_error("interbank instruction dcon:" + std::to_string(instruction.index()) + " has invalid status or amount");
		auto source = state.world.bank_payment_instruction_get_deposit_account_from_bank_payment_instruction_source(instruction);
		auto destination = state.world.bank_payment_instruction_get_deposit_account_from_bank_payment_instruction_destination(instruction);
		if(!source || !destination || !state.world.deposit_account_is_valid(source)
			|| !state.world.deposit_account_is_valid(destination)
			|| state.world.deposit_account_get_organization_from_deposit_account_bank(source)
				== state.world.deposit_account_get_organization_from_deposit_account_bank(destination)
			|| state.world.deposit_account_get_commodity_from_deposit_account_settlement(source)
				!= state.world.deposit_account_get_commodity_from_deposit_account_settlement(destination))
			add_error("interbank instruction dcon:" + std::to_string(instruction.index()) + " has invalid or orphan settlement endpoints");
		else if(state.world.deposit_account_get_canonical_id(source) == 0
			|| state.world.deposit_account_get_canonical_id(destination) == 0
			|| state.world.economic_actor_get_canonical_id(
				state.world.deposit_account_get_economic_actor_from_deposit_account_owner(source)) == 0
			|| state.world.economic_actor_get_canonical_id(
				state.world.deposit_account_get_economic_actor_from_deposit_account_owner(destination)) == 0)
			add_error("interbank instruction dcon:" + std::to_string(instruction.index() + 1) + " has an endpoint without stable identity");
	});
	return errors.empty();
}

bool canonical_banking_checksum(sys::state const& state, uint64_t& checksum) {
	std::vector<std::string> errors;
	if(!validate_canonical_banking_state(state, errors)) return false;
	uint64_t hash = 14695981039346656037ULL;
	auto add_u64 = [&](uint64_t value) {
		for(unsigned shift = 0; shift < 64; shift += 8) {
			hash ^= uint8_t(value >> shift);
			hash *= 1099511628211ULL;
		}
	};
	auto add_float = [&](float value) { add_u64(std::bit_cast<uint32_t>(value)); };
	std::vector<dcon::organization_id> banks;
	state.world.for_each_organization([&](dcon::organization_id bank) {
		if(state.world.organization_get_kind(bank) == uint8_t(actor_kind::bank)) banks.push_back(bank);
	});
	std::sort(banks.begin(), banks.end(), [&](auto left, auto right) {
		return state.world.organization_get_canonical_id(left) < state.world.organization_get_canonical_id(right);
	});
	for(auto bank : banks) {
		auto id = state.world.organization_get_canonical_id(bank);
		add_u64(0x42414E4BULL); // BANK
		add_u64(id);
		add_u64(state.world.economic_actor_get_canonical_id(
			actors::organizations::actor_for_organization(state, bank)));
		add_u64(state.world.asset_get_canonical_id(
			actors::organizations::equity_asset_for_organization(state, bank)));
		add_u64(state.world.organization_get_bank_policy_configured(bank));
		add_u64(state.world.organization_get_bank_jurisdiction(bank).id.index());
		add_u64(state.world.organization_get_bank_settlement_currency(bank).id.index());
		add_float(bank_base_rate(state, bank));
		add_float(state.world.organization_get_bank_minimum_capital_ratio(bank));
		add_float(state.world.organization_get_bank_liquidity_target(bank));
		add_float(state.world.organization_get_bank_risk_appetite(bank));
		add_float(state.world.organization_get_bank_lending_spread(bank));
		add_float(state.world.organization_get_bank_max_single_borrower_exposure(bank));
		add_float(state.world.organization_get_bank_reserve_requirement(bank));
		add_u64(state.world.organization_get_bank_capital_breach_grace_days(bank));
		add_u64(state.world.organization_get_bank_capital_breach_days(bank));
		add_u64(state.world.organization_get_bank_status(bank));
		auto sheet = bank_balance_sheet(state, bank, state.world.organization_get_bank_settlement_currency(bank));
		add_float(sheet.settlement_assets); add_float(sheet.loan_assets); add_float(sheet.accrued_interest_receivable);
		add_float(sheet.public_debt_assets); add_float(sheet.deposit_liabilities);
		add_float(sheet.other_financial_liabilities); add_float(sheet.net_worth);
		add_float(sheet.capital_ratio); add_float(sheet.liquidity_ratio); add_float(sheet.required_liquidity);
	}
	std::vector<dcon::monetary_account_id> reserve_accounts;
	state.world.for_each_monetary_account([&](dcon::monetary_account_id account) {
		if(state.world.monetary_account_get_organization_from_monetary_account_reserve_bank(account))
			reserve_accounts.push_back(account);
	});
	std::sort(reserve_accounts.begin(), reserve_accounts.end(), [&](auto left, auto right) {
		return state.world.monetary_account_get_canonical_id(left) < state.world.monetary_account_get_canonical_id(right);
	});
	for(auto account : reserve_accounts) {
		add_u64(0x52455345525645ULL); // RESERVE
		add_u64(state.world.monetary_account_get_canonical_id(account));
		add_u64(state.world.organization_get_canonical_id(
			state.world.monetary_account_get_organization_from_monetary_account_reserve_bank(account)));
		add_u64(economy::accounts::settlement_of(state, account).index());
		add_float(economy::accounts::balance(state, account));
	}
	std::vector<dcon::deposit_account_id> deposits;
	state.world.for_each_deposit_account([&](dcon::deposit_account_id account) { deposits.push_back(account); });
	std::sort(deposits.begin(), deposits.end(), [&](auto left, auto right) {
		return state.world.deposit_account_get_canonical_id(left) < state.world.deposit_account_get_canonical_id(right);
	});
	for(auto account : deposits) {
		add_u64(0x4445504F534954ULL); // DEPOSIT
		add_u64(state.world.deposit_account_get_canonical_id(account));
		add_u64(state.world.organization_get_canonical_id(
			state.world.deposit_account_get_organization_from_deposit_account_bank(account)));
		add_u64(state.world.economic_actor_get_canonical_id(
			state.world.deposit_account_get_economic_actor_from_deposit_account_owner(account)));
		add_u64(state.world.deposit_account_get_commodity_from_deposit_account_settlement(account).index());
		add_float(deposit_balance(state, account));
	}
	std::vector<dcon::obligation_id> loans;
	state.world.for_each_obligation([&](dcon::obligation_id loan) {
		if(state.world.obligation_get_kind(loan) != uint8_t(relations::obligation_kind::loan)) return;
		auto bank = bank_for_loan(state, loan);
		if(bank) loans.push_back(loan);
	});
	std::sort(loans.begin(), loans.end(), [&](auto left, auto right) {
		return state.world.obligation_get_canonical_id(left) < state.world.obligation_get_canonical_id(right);
	});
	for(auto loan : loans) {
		add_u64(0x4C4F414EULL); // LOAN
		add_u64(state.world.obligation_get_canonical_id(loan));
		add_u64(state.world.economic_actor_get_canonical_id(
			state.world.obligation_get_economic_actor_from_obligation_debtor(loan)));
		add_u64(state.world.economic_actor_get_canonical_id(
			state.world.obligation_get_economic_actor_from_obligation_creditor(loan)));
		add_u64(state.world.obligation_get_settlement_commodity(loan).id.index());
		add_float(state.world.obligation_get_principal_outstanding(loan));
		add_float(state.world.obligation_get_original_principal(loan));
		add_float(state.world.obligation_get_accrued_interest(loan));
		add_float(state.world.obligation_get_annual_interest_rate(loan));
		add_float(state.world.obligation_get_collateral_value(loan));
		add_float(state.world.obligation_get_recovered_amount(loan));
		add_float(state.world.obligation_get_written_off_amount(loan));
		add_u64(state.world.obligation_get_status(loan));
		add_u64(uint32_t(state.world.obligation_get_creation_date(loan).to_raw_value()));
		add_u64(uint32_t(state.world.obligation_get_due_date(loan).to_raw_value()));
		add_u64(uint32_t(state.world.obligation_get_last_interest_accrual_date(loan).to_raw_value()));
		auto factory = state.world.obligation_get_factory_from_obligation_factory(loan);
		add_u64(factory ? state.world.factory_get_canonical_id(factory) : 0);
	}
	std::vector<dcon::bank_payment_instruction_id> instructions;
	state.world.for_each_bank_payment_instruction([&](dcon::bank_payment_instruction_id instruction) { instructions.push_back(instruction); });
	std::sort(instructions.begin(), instructions.end(), [&](auto left, auto right) {
		return state.world.bank_payment_instruction_get_canonical_id(left)
			< state.world.bank_payment_instruction_get_canonical_id(right);
	});
	for(auto instruction : instructions) {
		add_u64(0x5041594D454E54ULL); // PAYMENT
		add_u64(state.world.bank_payment_instruction_get_canonical_id(instruction));
		add_float(state.world.bank_payment_instruction_get_amount(instruction));
		add_u64(uint32_t(state.world.bank_payment_instruction_get_requested_on(instruction).to_raw_value()));
		add_u64(state.world.bank_payment_instruction_get_status(instruction));
		auto source = state.world.bank_payment_instruction_get_deposit_account_from_bank_payment_instruction_source(instruction);
		auto destination = state.world.bank_payment_instruction_get_deposit_account_from_bank_payment_instruction_destination(instruction);
		add_u64(state.world.deposit_account_get_canonical_id(source));
		add_u64(state.world.deposit_account_get_canonical_id(destination));
	}
	checksum = hash;
	return true;
}

} // namespace economy::banking
