#include "credit_market.hpp"

#include "banking_stability.hpp"
#include "gamerule.hpp"
#include "money.hpp"
#include "system_state.hpp"

#include <algorithm>
#include <cmath>

namespace economy::credit {
namespace {

constexpr float epsilon = 0.0001f;

float nonnegative(float value) {
	return std::isfinite(value) ? std::max(0.f, value) : 0.f;
}

float unit(float value) {
	return std::isfinite(value) ? std::clamp(value, 0.f, 1.f) : 0.f;
}

} // namespace

void daily_flows::reset(uint32_t nation_count, uint32_t province_count) {
	extended.assign(std::size_t(nation_count), 0.f);
	interest.assign(std::size_t(nation_count), 0.f);
	private_requested.assign(std::size_t(nation_count), 0.f);
	private_construction_spent.assign(std::size_t(nation_count), 0.f);
	producer_extended.assign(std::size_t(nation_count), 0.f);
	producer_unfunded.assign(std::size_t(nation_count), 0.f);
	producer_availability.assign(std::size_t(nation_count), 1.f);
	producer_availability_by_province.assign(std::size_t(province_count), 1.f);
	producer_interest_paid.assign(std::size_t(nation_count), 0.f);
	producer_principal_repaid.assign(std::size_t(nation_count), 0.f);
	producer_writeoff.assign(std::size_t(nation_count), 0.f);
	bank_distribution.assign(std::size_t(nation_count), 0.f);
	investment_distribution.assign(std::size_t(nation_count), 0.f);
}

void daily_flows::record_private_spending(dcon::nation_id nation, float amount) {
	if(!nation)
		return;
	auto const index = std::size_t(nation.index());
	if(index >= private_construction_spent.size())
		return;
	private_construction_spent[index] = nonnegative(amount);
}

void daily_flows::record_producer_province(dcon::province_id province,
		float availability) {
	if(!province)
		return;
	auto const index = std::size_t(province.index());
	if(index >= producer_availability_by_province.size())
		return;
	producer_availability_by_province[index] = std::isfinite(availability)
		? std::clamp(availability, 0.f, 1.f) : 1.f;
}

void daily_flows::record_debt_service(dcon::nation_id nation, float interest, float principal) {
	if(!nation)
		return;
	auto const index = std::size_t(nation.index());
	if(index >= producer_interest_paid.size())
		return;
	producer_interest_paid[index] = nonnegative(interest);
	producer_principal_repaid[index] = nonnegative(principal);
}

void daily_flows::record_writeoff(dcon::nation_id nation, float amount) {
	if(!nation)
		return;
	auto const index = std::size_t(nation.index());
	if(index >= producer_writeoff.size())
		return;
	producer_writeoff[index] += nonnegative(amount);
}

void daily_flows::record_producer(dcon::nation_id nation, float extended_amount,
		float unfunded_amount, float availability) {
	if(!nation)
		return;
	auto const index = std::size_t(nation.index());
	if(index >= producer_extended.size())
		return;
	producer_extended[index] = nonnegative(extended_amount);
	producer_unfunded[index] = nonnegative(unfunded_amount);
	producer_availability[index] = std::isfinite(availability)
		? std::clamp(availability, 0.f, 1.f) : 1.f;
}

debt_service service_debt(float outstanding_debt, float available_cash, float annual_rate) {
	debt_service result;
	auto const debt = nonnegative(outstanding_debt);
	result.closing_debt = debt;
	if(debt <= epsilon)
		return result;

	auto const daily_rate = nonnegative(annual_rate) / 365.f;
	auto const accrued = debt * daily_rate;
	auto cash = nonnegative(available_cash);

	// Interest first, out of whatever surplus the till holds.
	result.interest = std::min(accrued, cash);
	cash -= result.interest;
	// What could not be paid is added to the principal. This is the mechanism
	// that lets a firm dig itself deeper without anyone deciding to lend it more.
	result.capitalized = accrued - result.interest;

	auto const amortization = debt * principal_repayment_rate;
	result.repayment = std::min(amortization, cash);

	result.closing_debt = std::max(0.f, debt + result.capitalized - result.repayment);
	return result;
}

float producer_debt_after_extension(float outstanding_debt, float extended) {
	auto const updated = nonnegative(outstanding_debt) + nonnegative(extended);
	return std::isfinite(updated) ? updated : nonnegative(outstanding_debt);
}

bool producer_debt_is_unrecoverable(float outstanding_debt, float industry_value,
		bool has_industry, bool valuation_ready) {
	auto const debt = nonnegative(outstanding_debt);
	if(debt <= epsilon || (has_industry && !valuation_ready))
		return false;
	return debt > std::max(epsilon,
		nonnegative(industry_value) * producer_bankruptcy_debt_to_value);
}

float employment_availability(float unfunded, float requested) {
	auto const asked = nonnegative(requested);
	if(asked <= epsilon)
		return 1.f;
	auto const stress = unit(nonnegative(unfunded) / asked);
	return 1.f - maximum_daily_employment_contraction * stress;
}

float producer_employment_scale(sys::state const& state,
		dcon::province_id province) {
	if(!gamerule::age_of_transformation_enabled(state) || !province)
		return 1.f;
	auto const index = std::size_t(province.index());
	if(index >= state.credit_daily_flows.producer_availability_by_province.size())
		return 1.f;
	auto const value =
		state.credit_daily_flows.producer_availability_by_province[index];
	return std::isfinite(value) ? std::clamp(value, 0.f, 1.f) : 1.f;
}

void daily_flows::record(dcon::nation_id nation, float extended_amount, float interest_amount) {
	if(!nation)
		return;
	auto const index = std::size_t(nation.index());
	if(index >= extended.size())
		return;
	extended[index] = nonnegative(extended_amount);
	interest[index] = nonnegative(interest_amount);
}

balance_sheet make_balance_sheet(inputs raw_inputs) {
	balance_sheet result;
	result.cash_reserves = nonnegative(raw_inputs.banking_reserves);
	result.government_bonds = nonnegative(raw_inputs.government_debt);
	result.investment_loans = nonnegative(raw_inputs.private_investment);
	result.producer_loans = nonnegative(raw_inputs.producer_debt);
	result.total_assets = result.cash_reserves + result.government_bonds
		+ result.investment_loans + result.producer_loans;
	return result;
}

market calculate(inputs raw_inputs) {
	market result;
	result.policy_annual_rate = nonnegative(raw_inputs.base_annual_rate);
	if(!raw_inputs.enabled)
		return result;

	result.enabled = true;
	auto const base_rate = nonnegative(raw_inputs.base_annual_rate);
	auto const balance = make_balance_sheet(raw_inputs);
	auto const reserves = balance.cash_reserves;
	auto const government_debt = balance.government_bonds;
	auto const private_investment = balance.investment_loans;
	auto const producer_debt = balance.producer_loans;
	auto const health = unit(raw_inputs.credit_health);
	auto const shortfall = nonnegative(raw_inputs.private_credit_shortfall);

	auto const committed = government_debt + private_investment + producer_debt;
	result.utilization = balance.total_assets > epsilon
		? unit(committed / balance.total_assets) : 0.f;
	result.government_share = committed > epsilon ? unit(government_debt / committed) : 0.f;

	// Scarcity of loanable funds is the price signal. Risk is the second term,
	// and reuses the premium shape banking_stability applied on its own.
	auto const effective_utilization = std::min(result.utilization, utilization_cap);
	auto const scarcity_multiplier =
		1.f + rate_sensitivity * effective_utilization * effective_utilization;
	auto const stress = 1.f - health;
	auto const risk_multiplier = 1.f + maximum_risk_premium * stress * stress;
	result.interest_cost_multiplier = std::min(
		maximum_rate_multiplier, scarcity_multiplier * risk_multiplier);
	result.policy_annual_rate = base_rate * result.interest_cost_multiplier;

	// The cash left the reserve account when each existing loan was originated.
	// Subtracting the claim here a second time would double-count lending.
	auto const lendable_cash = reserves * (1.f - reserve_requirement);
	result.lending_capacity = lendable_cash * maximum_daily_lending_share * health;
	result.private_credit_requested = shortfall;
	result.private_credit_extended = std::min(result.lending_capacity, shortfall);

	// The pool services what it holds. This is what stops the bank from being a
	// sink: deployed capital yields, and the yield comes back as reserves.
	result.private_interest_due = std::min(
		private_investment, private_investment * result.policy_annual_rate / 365.f);
	return result;
}

producer_financing finance_producers(market const& priced, float shortfall) {
	producer_financing result;
	result.requested = nonnegative(shortfall);
	if(!priced.enabled || result.requested <= epsilon) {
		result.unfunded = result.requested;
		return result;
	}
	// Producers draw on the same reserves as the investment pool, so financing a
	// deficit crowds out financing an expansion. That is the intended trade-off,
	// not a separate rule.
	result.extended = std::min(nonnegative(priced.lending_capacity), result.requested);
	result.unfunded = result.requested - result.extended;
	result.interest = result.extended * nonnegative(priced.policy_annual_rate) / 365.f;
	return result;
}

circulation calculate_circulation(circulation_inputs raw_inputs) {
	circulation result;
	if(!raw_inputs.enabled || !raw_inputs.has_claimants)
		return result;

	auto const reserves = nonnegative(raw_inputs.banking_reserves);
	auto const pool = nonnegative(raw_inputs.private_investment);
	auto const realized_interest = nonnegative(raw_inputs.realized_bank_interest);
	auto const credit_demand = nonnegative(raw_inputs.credit_demand);
	auto const construction_spending =
		nonnegative(raw_inputs.private_construction_spending);

	// Only realized cash yield is a dividend. Keeping half in the bank pays for
	// losses and grows its capacity; distributing half stops that yield from
	// becoming a permanent demand sink.
	result.bank_profit_dividend = std::min(reserves,
		realized_interest * realized_interest_distribution_share);
	auto const bank_after_profit = reserves - result.bank_profit_dividend;

	// Retain both the statutory liquidity share and enough cash to cover ninety
	// days of today's observed credit demand. Only the excess is an idle deposit,
	// and even that is returned slowly.
	auto const bank_floor = std::max(
		bank_after_profit * reserve_requirement,
		credit_demand * capital_demand_reserve_days);
	auto const idle_bank = std::max(0.f, bank_after_profit - bank_floor);
	result.bank_idle_withdrawal = std::min(bank_after_profit,
		idle_bank / idle_capital_release_days);

	// A construction fund keeps the same ninety-day operating buffer. When
	// there are no projects, capital is redeemed over three years rather than
	// confiscated or immediately dumped into consumption.
	auto const pool_floor = construction_spending * capital_demand_reserve_days;
	auto const idle_pool = std::max(0.f, pool - pool_floor);
	result.investment_idle_withdrawal = std::min(pool,
		idle_pool / idle_capital_release_days);
	return result;
}

market evaluate_nation(sys::state const& state, dcon::nation_id nation,
		float private_credit_shortfall) {
	inputs derived;
	if(!gamerule::age_of_transformation_enabled(state)
		|| !nation || !state.world.nation_is_valid(nation)) {
		return calculate(derived);
	}

	auto const loan_modifier = state.world.nation_get_modifier_values(
		nation, sys::national_mod_offsets::loan_interest);
	derived.enabled = true;
	derived.base_annual_rate = std::max(
		0.01f, (loan_modifier + 1.f) * state.defines.loan_base_interest);
	derived.banking_reserves = state.world.nation_get_national_bank(nation);
	derived.government_debt = state.world.nation_get_local_loan(nation);
	derived.private_investment = state.world.nation_get_private_investment(nation);
	for(auto ownership : state.world.nation_get_province_ownership(nation)) {
		derived.producer_debt += nonnegative(
			state.world.province_get_producer_debt(ownership.get_province()));
	}
	derived.credit_health = banking_stability::evaluate_nation(state, nation).credit_health;
	derived.private_credit_shortfall = private_credit_shortfall;
	return calculate(derived);
}

void settle_nation(sys::state& state, dcon::nation_id nation, market const& result) {
	if(!result.enabled || !nation)
		return;

	auto reserves = nonnegative(state.world.nation_get_national_bank(nation));
	auto pool = nonnegative(state.world.nation_get_private_investment(nation));

	auto const extended = std::min(nonnegative(result.private_credit_extended), reserves);
	reserves -= extended;
	pool += extended;

	auto const interest = std::min(nonnegative(result.private_interest_due), pool);
	pool -= interest;
	reserves += interest;

	state.world.nation_set_national_bank(nation, reserves);
	state.world.nation_set_private_investment(nation, pool);
	state.credit_daily_flows.record(nation, extended, interest);
	auto const index = std::size_t(nation.index());
	if(index < state.credit_daily_flows.private_requested.size()) {
		state.credit_daily_flows.private_requested[index] =
			nonnegative(result.private_credit_requested);
	}
}

circulation settle_circulation(sys::state& state, dcon::nation_id nation,
		bool has_claimants) {
	circulation_inputs inputs;
	inputs.enabled = gamerule::age_of_transformation_enabled(state);
	inputs.has_claimants = has_claimants;
	if(!nation || !state.world.nation_is_valid(nation))
		return calculate_circulation(inputs);

	inputs.banking_reserves = state.world.nation_get_national_bank(nation);
	inputs.private_investment = state.world.nation_get_private_investment(nation);
	auto const index = std::size_t(nation.index());
	if(index < state.credit_daily_flows.interest.size())
		inputs.realized_bank_interest += state.credit_daily_flows.interest[index];
	if(index < state.credit_daily_flows.producer_interest_paid.size())
		inputs.realized_bank_interest +=
			state.credit_daily_flows.producer_interest_paid[index];
	if(index < state.credit_daily_flows.private_requested.size())
		inputs.credit_demand += state.credit_daily_flows.private_requested[index];
	if(index < state.credit_daily_flows.producer_extended.size())
		inputs.credit_demand += state.credit_daily_flows.producer_extended[index];
	if(index < state.credit_daily_flows.producer_unfunded.size())
		inputs.credit_demand += state.credit_daily_flows.producer_unfunded[index];
	if(index < state.credit_daily_flows.private_construction_spent.size())
		inputs.private_construction_spending =
			state.credit_daily_flows.private_construction_spent[index];

	auto const result = calculate_circulation(inputs);
	auto const bank_paid = std::min(nonnegative(inputs.banking_reserves),
		nonnegative(result.bank_total()));
	auto const investment_paid = std::min(nonnegative(inputs.private_investment),
		nonnegative(result.investment_idle_withdrawal));
	state.world.nation_set_national_bank(nation,
		nonnegative(inputs.banking_reserves) - bank_paid);
	state.world.nation_set_private_investment(nation,
		nonnegative(inputs.private_investment) - investment_paid);
	if(index < state.credit_daily_flows.bank_distribution.size())
		state.credit_daily_flows.bank_distribution[index] = bank_paid;
	if(index < state.credit_daily_flows.investment_distribution.size())
		state.credit_daily_flows.investment_distribution[index] = investment_paid;
	return result;
}

void service_producer_debt(sys::state& state) {
	if(!gamerule::age_of_transformation_enabled(state))
		return;

	for(auto nation : state.world.in_nation) {
		auto const priced = evaluate_nation(state, nation);
		if(!priced.enabled)
			continue;
		auto paid_interest = 0.f;
		auto repaid_principal = 0.f;
		auto reserves = nonnegative(state.world.nation_get_national_bank(nation));

		for(auto ownership : state.world.nation_get_province_ownership(nation)) {
			auto const province = ownership.get_province();
			auto const debt = nonnegative(state.world.province_get_producer_debt(province));
			if(debt <= epsilon)
				continue;

			// A producer loan can finance either factories or the RGO till. Its
			// collateral therefore includes both industrial capital and land. Using
			// factory value alone made every farm/mine loan look unsecured: it was
			// written off, recreated as new debt and hit employment again each day.
			auto const industry_value = nonnegative(
				state.world.province_get_industry_market_value(province));
			auto const land_value = nonnegative(
				state.world.province_get_land_market_value(province));
			auto const collateral_value = industry_value + land_value;
			auto const smoothed_factory_profit =
				state.world.province_get_smoothed_factory_profit(province);
			auto const smoothed_land_rent =
				state.world.province_get_smoothed_land_rent(province);
			auto const has_factories =
				state.world.province_get_factory_location(province).begin()
					!= state.world.province_get_factory_location(province).end();
			auto const has_rgo = nonnegative(
				state.world.province_get_rgo_base_size(province)) > epsilon;
			auto const has_production = has_factories || has_rgo;
			auto const valuation_ready = collateral_value > epsilon
				|| (std::isfinite(smoothed_factory_profit)
					&& std::abs(smoothed_factory_profit) > epsilon)
				|| (std::isfinite(smoothed_land_rent)
					&& std::abs(smoothed_land_rent) > epsilon);
			if(producer_debt_is_unrecoverable(debt, collateral_value,
				has_production,
				valuation_ready)) {
				// The cash left when the loan was originated. A write-off removes the
				// claim from assets; charging cash again would record the loss twice.
				state.world.province_set_producer_debt(province, 0.f);
				state.credit_daily_flows.record_writeoff(nation, debt);
				continue;
			}
			// Only a till in surplus can service anything; an overdrawn one has
			// nothing to pay with and its interest capitalizes.
			auto const factory = state.world.province_get_factory_bank(province);
			auto const rgo = state.world.province_get_rgo_bank(province);
			auto surplus = 0.f;
			if(std::isfinite(factory) && factory > 0.f)
				surplus += factory;
			if(std::isfinite(rgo) && rgo > 0.f)
				surplus += rgo;

			auto const serviced = service_debt(debt, surplus, priced.policy_annual_rate);
			auto const taken = serviced.interest + serviced.repayment;
			if(taken > epsilon && surplus > epsilon) {
				// Draw proportionally from whichever tills are in surplus.
				auto const share = std::min(1.f, taken / surplus);
				if(std::isfinite(factory) && factory > 0.f)
					state.world.province_set_factory_bank(province, factory - factory * share);
				if(std::isfinite(rgo) && rgo > 0.f)
					state.world.province_set_rgo_bank(province, rgo - rgo * share);
				reserves += taken;
			}
			state.world.province_set_producer_debt(province, serviced.closing_debt);
			paid_interest += serviced.interest;
			repaid_principal += serviced.repayment;
		}
		state.world.nation_set_national_bank(nation, reserves);
		state.credit_daily_flows.record_debt_service(nation, paid_interest, repaid_principal);
	}
}

void settle_producer_credit(sys::state& state) {
	if(!gamerule::age_of_transformation_enabled(state))
		return;

	// Serial over nations, and over each nation's provinces in dcon order, so
	// the pro-rata split cannot depend on scheduling.
	for(auto nation : state.world.in_nation) {
		auto shortfall = 0.f;
		struct province_credit_need {
			dcon::province_id province;
			float gross_shortfall = 0.f;
		};
		std::vector<province_credit_need> province_shortfalls;
		for(auto ownership : state.world.nation_get_province_ownership(nation)) {
			auto const province = ownership.get_province();
			auto const rgo = state.world.province_get_rgo_bank(province);
			auto const factory = state.world.province_get_factory_bank(province);
			auto province_shortfall = 0.f;
			if(std::isfinite(rgo) && rgo < 0.f)
				province_shortfall -= rgo;
			if(std::isfinite(factory) && factory < 0.f)
				province_shortfall -= factory;
			shortfall += province_shortfall;
			// A negative till is demand for working capital. It becomes a bank
			// asset only to the extent that cash is actually transferred below;
			// the remainder stays observable as an unfunded operating deficit.
			if(province_shortfall > epsilon) {
				province_shortfalls.push_back({province, province_shortfall});
			}
		}
		if(shortfall <= epsilon) {
			state.credit_daily_flows.record_producer(nation, 0.f, 0.f, 1.f);
			continue;
		}

		auto const priced = evaluate_nation(state, nation);
		auto const financing = finance_producers(priced, shortfall);
		auto reserves = nonnegative(state.world.nation_get_national_bank(nation));
		auto const extended = std::min(financing.extended, reserves);
		auto const funded_share = shortfall > epsilon
			? std::clamp(extended / shortfall, 0.f, 1.f) : 0.f;
		if(extended > epsilon) {
			// Top the tills up in proportion to how short each one is, so the
			// distribution does not depend on province order beyond rounding. The
			// matching cash transfer creates an equally sized claim on each borrower.
			auto const share = extended / shortfall;
			for(auto const& need : province_shortfalls) {
				auto const province = need.province;
				auto const province_extended = need.gross_shortfall * share;
				auto const outstanding = nonnegative(
					state.world.province_get_producer_debt(province));
				state.world.province_set_producer_debt(province,
					producer_debt_after_extension(outstanding, province_extended));
				auto const rgo = state.world.province_get_rgo_bank(province);
				if(std::isfinite(rgo) && rgo < 0.f) {
					state.world.province_set_rgo_bank(province, rgo - rgo * share);
				}
				auto const factory = state.world.province_get_factory_bank(province);
				if(std::isfinite(factory) && factory < 0.f) {
					state.world.province_set_factory_bank(province, factory - factory * share);
				}
			}
			reserves -= extended;
			// Interest on the drawing is paid out of the same reserves the loan
			// came from, so the bank's stock reflects a priced loan book.
			state.world.nation_set_national_bank(nation, reserves);
		}
		for(auto const& need : province_shortfalls) {
			// This is a multiplier on today's newly calculated employment target,
			// not a destructive compounding write to factory capacity. Keeping it
			// active while the till remains negative prevents immediate rehiring on
			// working capital that still has not been financed.
			auto const province_unfunded =
				need.gross_shortfall * (1.f - funded_share);
			state.credit_daily_flows.record_producer_province(need.province,
				employment_availability(province_unfunded, need.gross_shortfall));
		}
		auto const unfunded = shortfall - extended;
		state.credit_daily_flows.record_producer(nation, extended, unfunded,
			employment_availability(unfunded, shortfall));
	}
}

void on_province_owner_changed(sys::state& state, dcon::province_id province,
		dcon::nation_id old_owner, dcon::nation_id new_owner) {
	if(!gamerule::age_of_transformation_enabled(state)
		|| !old_owner || old_owner == new_owner)
		return;

	auto const debt = nonnegative(state.world.province_get_producer_debt(province));
	if(debt <= epsilon)
		return;

	// Ownership transfer is a secured-credit event. Cash was already paid when
	// the loan originated, so realizing the loss removes the claim only.
	state.world.province_set_producer_debt(province, 0.f);
	state.credit_daily_flows.record_writeoff(old_owner, debt);
}

} // namespace economy::credit
