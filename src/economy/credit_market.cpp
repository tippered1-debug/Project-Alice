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

void daily_flows::reset(uint32_t nation_count) {
	extended.assign(std::size_t(nation_count), 0.f);
	interest.assign(std::size_t(nation_count), 0.f);
	producer_extended.assign(std::size_t(nation_count), 0.f);
	producer_unfunded.assign(std::size_t(nation_count), 0.f);
	producer_availability.assign(std::size_t(nation_count), 1.f);
	producer_interest_paid.assign(std::size_t(nation_count), 0.f);
	producer_principal_repaid.assign(std::size_t(nation_count), 0.f);
	producer_writeoff.assign(std::size_t(nation_count), 0.f);
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

float producer_debt_after_shortfall(float outstanding_debt, float shortfall) {
	return std::max(nonnegative(outstanding_debt), nonnegative(shortfall));
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

float producer_employment_scale(sys::state const& state, dcon::nation_id nation) {
	if(!gamerule::age_of_transformation_enabled(state) || !nation)
		return 1.f;
	auto const index = std::size_t(nation.index());
	if(index >= state.credit_daily_flows.producer_availability.size())
		return 1.f;
	auto const value = state.credit_daily_flows.producer_availability[index];
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

market calculate(inputs raw_inputs) {
	market result;
	result.policy_annual_rate = nonnegative(raw_inputs.base_annual_rate);
	if(!raw_inputs.enabled)
		return result;

	result.enabled = true;
	auto const base_rate = nonnegative(raw_inputs.base_annual_rate);
	auto const reserves = nonnegative(raw_inputs.banking_reserves);
	auto const government_debt = nonnegative(raw_inputs.government_debt);
	auto const private_investment = nonnegative(raw_inputs.private_investment);
	auto const producer_debt = nonnegative(raw_inputs.producer_debt);
	auto const health = unit(raw_inputs.credit_health);
	auto const shortfall = nonnegative(raw_inputs.private_credit_shortfall);

	auto const committed = government_debt + private_investment + producer_debt;
	if(reserves > epsilon) {
		result.utilization = committed / reserves;
		if(!std::isfinite(result.utilization))
			result.utilization = utilization_cap;
	} else {
		// No reserves at all: any commitment is total exhaustion.
		result.utilization = committed > epsilon ? utilization_cap : 0.f;
	}
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

	// Lending capacity. Government debt encumbers the stock: money already lent
	// to the treasury cannot be lent to a factory as well. That is the whole
	// crowding-out mechanism, and it needs no separate rule.
	auto const lendable = reserves * (1.f - reserve_requirement);
	auto const free_reserves = std::max(0.f, lendable - government_debt);
	result.lending_capacity = free_reserves * maximum_daily_lending_share * health;
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

			// The market value is deliberately lagged, so do not treat the first
			// zero observation as a bankruptcy before the first profit has been
			// recorded. Once a valuation exists, however, a loss-making industry
			// has value zero and an outstanding claim is exactly a bad loan.
			auto const industry_value = nonnegative(
				state.world.province_get_industry_market_value(province));
			auto const smoothed_profit = state.world.province_get_smoothed_factory_profit(province);
				auto const has_factories = state.world.province_get_factory_location(province).begin()
					!= state.world.province_get_factory_location(province).end();
			auto const valuation_ready = industry_value > epsilon
				|| (std::isfinite(smoothed_profit) && std::abs(smoothed_profit) > epsilon);
			if(producer_debt_is_unrecoverable(debt, industry_value, has_factories,
				valuation_ready)) {
				// The bank already advanced the cash when the debt was originated.
				// Losing the claim therefore shows up as a loss of reserves, not as
				// newly created money. A negative till remains an accounting claim
				// against the failed producer until the normal factory cleanup.
				reserves = std::max(0.f, reserves - debt);
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
			// The whole negative till is the producer's liability. Bank
			// financing below only decides how much of it gets live cash;
			// the unfunded remainder must not remain an interest-free shadow
			// balance outside the loan book.
			if(province_shortfall > epsilon) {
				// The same negative till is observed again tomorrow. Only the
				// portion not already represented in the book is new borrowing.
				auto const outstanding = nonnegative(
					state.world.province_get_producer_debt(province));
				auto const updated = producer_debt_after_shortfall(
					outstanding, province_shortfall);
				if(updated > outstanding + epsilon)
					state.world.province_set_producer_debt(province, updated);
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
		if(extended > epsilon) {
			// Top the tills up in proportion to how short each one is, so the
			// distribution does not depend on province order beyond rounding.
			auto const share = extended / shortfall;
			for(auto ownership : state.world.nation_get_province_ownership(nation)) {
				auto const province = ownership.get_province();
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

	// Ownership transfer is a secured-credit event. The old bank cannot follow
	// its collateral across a sovereign border, so the claim is realized as a
	// bad loan instead of becoming an invisible liability of the new owner.
	auto const reserves = nonnegative(state.world.nation_get_national_bank(old_owner));
	state.world.nation_set_national_bank(old_owner, std::max(0.f, reserves - debt));
	state.world.province_set_producer_debt(province, 0.f);
	state.credit_daily_flows.record_writeoff(old_owner, debt);
}

} // namespace economy::credit
