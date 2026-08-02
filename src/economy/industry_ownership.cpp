#include "industry_ownership.hpp"

#include "demographics.hpp"
#include "demographics_templates.hpp"
#include "gamerule.hpp"
#include "money.hpp"
#include "nations.hpp"
#include "system_state.hpp"

#include <algorithm>
#include <vector>
#include <cmath>

namespace economy::industry_ownership {
namespace {

constexpr float epsilon = 0.000001f;

float finite_nonnegative(float value) {
	return std::isfinite(value) ? std::max(0.f, value) : 0.f;
}

float unit(float value) {
	return std::isfinite(value) ? std::clamp(value, 0.f, 1.f) : 0.f;
}

constexpr std::size_t index(owner_group group) {
	return std::size_t(group);
}

// Must match nations::tag_to_int, which packs the tag big-endian. Getting the
// byte order wrong makes every lookup silently miss and every profile fall back
// to unassigned.
constexpr uint32_t country_tag(char first, char second, char third) {
	return (uint32_t(uint8_t(first)) << 16)
		| (uint32_t(uint8_t(second)) << 8)
		| uint32_t(uint8_t(third));
}

} // namespace

std::array<float, owner_group_count> shares(distribution value) {
	std::array<float, owner_group_count> result{};
	result[index(owner_group::capitalists)] = value.capitalists;
	result[index(owner_group::landed_elites)] = value.landed_elites;
	result[index(owner_group::state)] = value.state;
	result[index(owner_group::foreign)] = value.foreign;
	result[index(owner_group::workers)] = value.workers;
	return result;
}

distribution from_shares(std::array<float, owner_group_count> const& value) {
	return distribution{
		value[index(owner_group::capitalists)],
		value[index(owner_group::landed_elites)],
		value[index(owner_group::state)],
		value[index(owner_group::foreign)],
		value[index(owner_group::workers)]};
}

distribution normalize(distribution value) {
	auto parts = shares(value);
	auto total = 0.f;
	for(auto& part : parts) {
		part = finite_nonnegative(part);
		total += part;
	}
	if(total <= epsilon) {
		// Nothing recorded at all: industry belongs to its capitalists. This is
		// also what an old save loads as, which is why no migration pass exists.
		return distribution{};
	}
	for(auto& part : parts)
		part /= total;
	return from_shares(parts);
}

float update_smoothed_profit(float previous_daily_profit, float current_daily_profit,
		float window_days) {
	auto const window = std::isfinite(window_days) ? std::max(1.f, window_days) : 270.f;
	auto const previous = std::isfinite(previous_daily_profit) ? previous_daily_profit : 0.f;
	auto const current = std::isfinite(current_daily_profit) ? current_daily_profit : 0.f;
	auto const weight = 1.f / window;
	return previous * (1.f - weight) + current * weight;
}

float capitalized_value(float smoothed_daily_profit, float capitalization_years) {
	auto const years = std::isfinite(capitalization_years)
		? std::clamp(capitalization_years, 1.f, 50.f) : 10.f;
	// A business that loses money is not worth a negative sum; it is worth
	// nothing, and its owners are the ones who want out.
	return finite_nonnegative(smoothed_daily_profit) * 365.f * years;
}

historical_profile profile_for_tag(uint32_t identifying_int) {
	switch(identifying_int) {
	case country_tag('R', 'U', 'S'):
	case country_tag('J', 'A', 'P'):
	case country_tag('T', 'U', 'R'):
		// State arsenals, state railways, and a private sector built behind them.
		return historical_profile::state_led;
	case country_tag('P', 'R', 'U'):
	case country_tag('G', 'E', 'R'):
	case country_tag('A', 'U', 'S'):
		// Landed families that carried their capital into industry.
		return historical_profile::junker_industrial;
	case country_tag('P', 'E', 'R'):
	case country_tag('E', 'G', 'Y'):
	case country_tag('C', 'H', 'I'):
		// Concession economies: the works exist, the owners are elsewhere.
		return historical_profile::foreign_concession;
	case country_tag('U', 'S', 'A'):
	case country_tag('E', 'N', 'G'):
	case country_tag('B', 'E', 'L'):
		return historical_profile::liberal_private;
	default:
		return historical_profile::unassigned;
	}
}

historical_profile profile_for(sys::state const& state, dcon::province_id province) {
	auto const stored = state.world.province_get_industry_profile(province);
	if(stored != uint8_t(historical_profile::unassigned))
		return historical_profile(stored);
	auto const nation = state.world.province_get_nation_from_province_ownership(province);
	if(!nation)
		return historical_profile::unassigned;
	auto const identity = state.world.nation_get_identity_from_identity_holder(nation);
	if(!identity)
		return historical_profile::unassigned;
	return profile_for_tag(state.world.national_identity_get_identifying_int(identity));
}

std::string_view profile_localization_key(historical_profile profile) {
	switch(profile) {
	case historical_profile::liberal_private: return "alice_industry_profile_liberal_private";
	case historical_profile::state_led: return "alice_industry_profile_state_led";
	case historical_profile::foreign_concession: return "alice_industry_profile_foreign_concession";
	case historical_profile::junker_industrial: return "alice_industry_profile_junker_industrial";
	case historical_profile::cooperative: return "alice_industry_profile_cooperative";
	case historical_profile::unassigned: break;
	}
	return "alice_industry_profile_unassigned";
}

std::string_view ownership_law_localization_key(ownership_law law) {
	switch(law) {
	case ownership_law::nationalizing: return "alice_industry_law_nationalizing";
	case ownership_law::privatizing: return "alice_industry_law_privatizing";
	case ownership_law::open_market: break;
	}
	return "alice_industry_law_open_market";
}

distribution historical_initial_distribution(historical_profile profile) {
	switch(profile) {
	case historical_profile::liberal_private:
		return normalize(distribution{0.88f, 0.06f, 0.04f, 0.02f, 0.f});
	case historical_profile::state_led:
		return normalize(distribution{0.45f, 0.10f, 0.40f, 0.05f, 0.f});
	case historical_profile::foreign_concession:
		return normalize(distribution{0.25f, 0.10f, 0.10f, 0.55f, 0.f});
	case historical_profile::junker_industrial:
		return normalize(distribution{0.55f, 0.32f, 0.10f, 0.03f, 0.f});
	case historical_profile::cooperative:
		return normalize(distribution{0.60f, 0.05f, 0.15f, 0.02f, 0.18f});
	case historical_profile::unassigned:
		break;
	}
	return distribution{};
}

void initialize_historical_profiles(sys::state& state) {
	if(!gamerule::age_of_transformation_enabled(state))
		return;
	state.world.for_each_province([&](dcon::province_id province) {
		if(state.world.province_get_industry_profile(province)
			!= uint8_t(historical_profile::unassigned))
			return;
		auto const profile = profile_for(state, province);
		state.world.province_set_industry_profile(province, uint8_t(profile));
		if(profile == historical_profile::unassigned)
			return;
		store_distribution(state, province, historical_initial_distribution(profile));
	});
}

distribution current_distribution(sys::state const& state, dcon::province_id province) {
	auto const state_share = finite_nonnegative(
		state.world.province_get_industry_state_share(province));
	auto const foreign_share = finite_nonnegative(
		state.world.province_get_industry_foreign_share(province));
	auto const worker_share = finite_nonnegative(
		state.world.province_get_industry_worker_share(province));
	auto const landed_share = finite_nonnegative(
		state.world.province_get_industry_landed_share(province));
	auto const recorded = state_share + foreign_share + worker_share + landed_share;
	// Capitalists are the residual, so zeroed fields mean fully private
	// industry rather than industry that belongs to nobody.
	auto const capitalists = std::max(0.f, 1.f - recorded);
	return normalize(distribution{capitalists, landed_share, state_share,
		foreign_share, worker_share});
}

void store_distribution(sys::state& state, dcon::province_id province, distribution value) {
	auto const normalized = normalize(value);
	state.world.province_set_industry_state_share(province, normalized.state);
	state.world.province_set_industry_foreign_share(province, normalized.foreign);
	state.world.province_set_industry_worker_share(province, normalized.workers);
	state.world.province_set_industry_landed_share(province, normalized.landed_elites);
}

market_config configuration_for(sys::state const& state, dcon::province_id province) {
	market_config config;
	config.enabled = gamerule::age_of_transformation_enabled(state);
	auto const nation = state.world.province_get_nation_from_province_ownership(province);
	auto const rules = nation ? state.world.nation_get_combined_issue_rules(nation) : 0u;
	auto const allows_private_building = (rules & issue_rule::pop_build_factory) != 0;
	auto const allows_state_building = (rules & issue_rule::build_factory) != 0;
	config.foreign_investment_allowed = (rules & issue_rule::allow_foreign_investment) != 0;

	// The same legal signals that drive the land reform ladder drive this one,
	// so a player reads one political position rather than two.
	if(allows_state_building && !allows_private_building) {
		config.regime = ownership_law::nationalizing;
		config.nationalization_rate = 0.002f;
		config.compensation_rate = 0.5f;
	} else if(allows_private_building && !allows_state_building) {
		config.regime = ownership_law::privatizing;
		config.privatization_rate = 0.001f;
	}
	if(config.foreign_investment_allowed)
		config.foreign_investment_rate = 0.002f;
	else
		config.foreign_divestment_rate = 0.002f;
	if((rules & issue_rule::all_voting) != 0) {
		// Industrial democracy arrives with the franchise, on the same footing
		// as the right to buy in the land ladder.
		config.worker_buyout_rate = 0.0008f;
		config.compensation_rate = std::max(config.compensation_rate, 0.75f);
	}
	config.annual_profit_tax_rate = nation
		? 0.05f * nations::tax_efficiency(state, nation)
			* float(state.world.nation_get_rich_tax(nation)) / 100.f
		: 0.f;
	config.implementation_efficiency = nation
		? std::clamp(0.25f + 0.75f * nations::tax_efficiency(state, nation), 0.25f, 1.f)
		: 0.25f;
	config.available_public_funds = nation && state.world.commodity_size() > 0
		? 0.02f * finite_nonnegative(state.world.nation_get_stockpiles(nation, economy::money))
		: 0.f;
	return config;
}

dividend_split split_dividend(distribution owners, float dividend) {
	dividend_split result;
	auto const amount = finite_nonnegative(dividend);
	if(amount <= 0.f)
		return result;
	result.enabled = true;
	auto const normalized = shares(normalize(owners));
	auto distributed = 0.f;
	for(std::size_t group = 0; group < owner_group_count; ++group) {
		result.payout[group] = amount * normalized[group];
		distributed += result.payout[group];
	}
	result.retained = std::max(0.f, amount - distributed);
	return result;
}

market_result clear_market(distribution current,
		std::array<group_finance, owner_group_count> finances,
		float industry_value, market_config const& config) {
	market_result result;
	result.before = normalize(current);
	result.after = result.before;
	if(!config.enabled)
		return result;

	result.enabled = true;
	result.industry_value = finite_nonnegative(industry_value);

	auto holdings = shares(result.before);
	auto cash = std::array<float, owner_group_count>{};

	// Legal transfers first: they do not depend on anyone's willingness, only on
	// how much of the entitlement the administration can actually execute.
	auto const efficiency = unit(config.implementation_efficiency);
	auto public_funds = finite_nonnegative(config.available_public_funds);

	auto transfer = [&](owner_group from, owner_group to, float fraction, bool compensated) {
		auto const available = holdings[index(from)];
		auto moved = std::min(available, finite_nonnegative(fraction) * efficiency);
		if(moved <= epsilon)
			return;
		auto payment = 0.f;
		if(compensated && result.industry_value > 0.f) {
			payment = moved * result.industry_value * unit(config.compensation_rate);
			if(payment > public_funds) {
				// Only what the treasury can pay actually changes hands.
				auto const affordable = payment > 0.f ? public_funds / payment : 0.f;
				moved *= affordable;
				payment = public_funds;
			}
			public_funds -= payment;
			result.compensation += payment;
			result.public_cost += payment;
		}
		if(moved <= epsilon)
			return;
		holdings[index(from)] -= moved;
		holdings[index(to)] += moved;
		cash[index(from)] += payment;
		result.reform_turnover += moved;
	};

	if(config.nationalization_rate > 0.f) {
		transfer(owner_group::capitalists, owner_group::state,
			config.nationalization_rate, true);
		transfer(owner_group::landed_elites, owner_group::state,
			config.nationalization_rate, true);
	}
	if(config.privatization_rate > 0.f) {
		// The treasury sells: it receives rather than pays.
		auto const available = holdings[index(owner_group::state)];
		auto const moved = std::min(available, config.privatization_rate * efficiency);
		if(moved > epsilon) {
			holdings[index(owner_group::state)] -= moved;
			holdings[index(owner_group::capitalists)] += moved;
			auto const proceeds = moved * result.industry_value;
			cash[index(owner_group::state)] += proceeds;
			cash[index(owner_group::capitalists)] -= proceeds;
			result.reform_turnover += moved;
		}
	}
	if(config.worker_buyout_rate > 0.f) {
		transfer(owner_group::capitalists, owner_group::workers,
			config.worker_buyout_rate, true);
	}
	if(config.foreign_divestment_rate > 0.f) {
		transfer(owner_group::foreign, owner_group::capitalists,
			config.foreign_divestment_rate, true);
	}
	if(config.foreign_investment_allowed && config.foreign_investment_rate > 0.f) {
		// Foreign capital buys in rather than being granted a share: the local
		// owners are paid, and the flow stops once the holding matches what was
		// actually invested.
		auto const target = unit(config.foreign_investment_target);
		auto const held = holdings[index(owner_group::foreign)];
		if(target > held) {
			auto const step = std::min(target - held,
				finite_nonnegative(config.foreign_investment_rate));
			auto const moved = std::min(holdings[index(owner_group::capitalists)], step);
			if(moved > epsilon) {
				holdings[index(owner_group::capitalists)] -= moved;
				holdings[index(owner_group::foreign)] += moved;
				auto const payment = moved * result.industry_value;
				cash[index(owner_group::capitalists)] += payment;
				cash[index(owner_group::foreign)] -= payment;
				result.reform_turnover += moved;
			}
		}
	}

	// Then the voluntary market. Bids are cash-backed; asks are a small
	// voluntary listing plus distress selling by holders under hardship.
	auto bids = std::array<float, owner_group_count>{};
	auto asks = std::array<float, owner_group_count>{};
	auto total_bid = 0.f;
	auto total_ask = 0.f;

	for(std::size_t group = 0; group < owner_group_count; ++group) {
		auto const& finance = finances[group];
		auto const reserve = finite_nonnegative(finance.monthly_essential_needs)
			* std::max(0.f, config.reserve_months);
		auto const committable = std::max(0.f,
			finite_nonnegative(finance.liquid_savings) - reserve);
		// The state and foreign owners do not bid out of household savings;
		// their acquisitions run through the legal channel above.
		if(group == index(owner_group::state))
			bids[group] = 0.f;
		else if(group == index(owner_group::foreign))
			bids[group] = config.foreign_investment_allowed ? committable : 0.f;
		else
			bids[group] = committable;
		total_bid += bids[group];

		auto const voluntary = holdings[group] * finite_nonnegative(config.voluntary_ask_rate);
		auto const distress = holdings[group] * unit(finance.hardship);
		asks[group] = voluntary + distress;
		result.distress_asks += distress;
		total_ask += asks[group];
	}

	auto const value = result.industry_value;
	auto const demanded_share = value > epsilon ? total_bid / value : 0.f;
	auto matched = std::min(demanded_share, total_ask);
	matched = std::min(matched, std::max(0.f, config.maximum_monthly_turnover));
	if(matched > epsilon && total_ask > epsilon && total_bid > epsilon) {
		for(std::size_t group = 0; group < owner_group_count; ++group) {
			auto const sold = matched * (asks[group] / total_ask);
			auto const bought = matched * (bids[group] / total_bid);
			holdings[group] += bought - sold;
			cash[group] += (sold - bought) * value;
		}
		result.turnover = matched;
	}
	result.bids = bids;
	result.asks = asks;

	// The profit tax falls on the private owners of industry.
	if(config.annual_profit_tax_rate > 0.f && value > 0.f) {
		auto const monthly = value * unit(config.annual_profit_tax_rate) / 12.f;
		auto const private_share = holdings[index(owner_group::capitalists)]
			+ holdings[index(owner_group::landed_elites)];
		result.profit_tax = monthly * private_share;
	}

	for(auto& holding : holdings)
		holding = finite_nonnegative(holding);
	result.after = normalize(from_shares(holdings));
	result.cash_delta = cash;
	return result;
}


namespace {

owner_group pop_owner_group(sys::state const& state, dcon::pop_id pop, bool& participates) {
	auto const type = state.world.pop_get_poptype(pop);
	participates = true;
	if(type == state.culture_definitions.capitalists)
		return owner_group::capitalists;
	if(type == state.culture_definitions.aristocrat)
		return owner_group::landed_elites;
	if(type == state.culture_definitions.primary_factory_worker
		|| type == state.culture_definitions.secondary_factory_worker)
		return owner_group::workers;
	participates = false;
	return owner_group::capitalists;
}

// Seller proceeds follow each POP's existing savings, the best already-persisted
// proxy for its share of the class holding, so a numerous class cannot collect
// another owner's sale.
void apply_pop_cash(sys::state& state, dcon::province_id province, owner_group group,
		float cash_delta, float group_savings) {
	if(!std::isfinite(cash_delta) || std::abs(cash_delta) <= epsilon)
		return;
	std::vector<dcon::pop_id> recipients;
	for(auto location : state.world.province_get_pop_location(province)) {
		auto const pop = location.get_pop().id;
		bool participates = false;
		auto const pop_group = pop_owner_group(state, pop, participates);
		if(participates && pop_group == group)
			recipients.push_back(pop);
	}
	if(recipients.empty()) {
		// A class with no POPs here cannot be paid; the proceeds stay in the
		// banking system rather than evaporating.
		if(cash_delta > 0.f) {
			auto const nation = state.world.province_get_nation_from_province_ownership(province);
			if(nation)
				state.world.nation_set_national_bank(nation,
					finite_nonnegative(state.world.nation_get_national_bank(nation)) + cash_delta);
		}
		return;
	}
	auto remaining = std::abs(cash_delta);
	for(std::size_t i = 0; i < recipients.size(); ++i) {
		auto const pop = recipients[i];
		auto const current = finite_nonnegative(state.world.pop_get_savings(pop));
		auto weight = group_savings > 0.f
			? current / group_savings : 1.f / float(recipients.size());
		auto amount = i + 1 == recipients.size()
			? remaining : std::min(remaining, std::abs(cash_delta) * weight);
		if(cash_delta < 0.f)
			amount = std::min(amount, current);
		state.world.pop_set_savings(pop, cash_delta < 0.f ? current - amount : current + amount);
		remaining = std::max(0.f, remaining - amount);
	}
}

void apply_treasury(sys::state& state, dcon::nation_id nation, float delta) {
	if(!nation || state.world.commodity_size() == 0 || !std::isfinite(delta))
		return;
	auto const treasury = finite_nonnegative(state.world.nation_get_stockpiles(nation, economy::money));
	state.world.nation_set_stockpiles(nation, economy::money, std::max(0.f, treasury + delta));
}

} // namespace

// Share of a nation's industry that recorded foreign investment would justify
// owning. Computed once per nation: doing it per province would be quadratic.
std::vector<float> foreign_investment_targets(sys::state const& state) {
	std::vector<float> targets(std::size_t(state.world.nation_size()), 0.f);
	for(auto nation : state.world.in_nation) {
		double domestic_value = 0.0;
		for(auto ownership : state.world.nation_get_province_ownership(nation)) {
			domestic_value += double(finite_nonnegative(
				state.world.province_get_industry_market_value(ownership.get_province())));
		}
		double invested = 0.0;
		for(auto relation : state.world.nation_get_unilateral_relationship_as_target(nation)) {
			invested += double(std::max(0.f, relation.get_foreign_investment()));
		}
		auto const total = domestic_value + invested;
		if(total > 0.0)
			targets[std::size_t(nation.id.index())] = unit(float(invested / total));
	}
	return targets;
}

void update_markets(sys::state& state) {
	if(!gamerule::age_of_transformation_enabled(state))
		return;
	// Ownership is a stock and the rates here are monthly, matching the land
	// market. Clearing it daily would apply every legal flow thirty times over.
	auto const date = state.current_date.to_ymd(state.start_date);
	if(date.day != 1)
		return;

	auto const investment_targets = foreign_investment_targets(state);

	state.world.for_each_province([&](dcon::province_id province) {
		auto const nation = state.world.province_get_nation_from_province_ownership(province);
		if(!nation)
			return;

		auto config = configuration_for(state, province);
		if(auto const slot = std::size_t(nation.index()); slot < investment_targets.size())
			config.foreign_investment_target = investment_targets[slot];
		auto const value = capitalized_value(
			state.world.province_get_smoothed_factory_profit(province));
		state.world.province_set_industry_market_value(province, value);

		std::array<group_finance, owner_group_count> finances{};
		std::array<float, owner_group_count> group_savings{};
		for(auto location : state.world.province_get_pop_location(province)) {
			auto const pop = location.get_pop().id;
			bool participates = false;
			auto const group = pop_owner_group(state, pop, participates);
			if(!participates)
				continue;
			auto const slot = std::size_t(group);
			auto const savings = finite_nonnegative(state.world.pop_get_savings(pop));
			finances[slot].liquid_savings += savings;
			group_savings[slot] += savings;
			auto const size = finite_nonnegative(state.world.pop_get_size(pop));
			auto const market = state.world.state_instance_get_market_from_local_market(
				state.world.province_get_state_membership(province));
			if(market) {
				auto const type = state.world.pop_get_poptype(pop);
				finances[slot].monthly_essential_needs +=
					finite_nonnegative(state.world.market_get_life_needs_costs(market, type))
					* size / state.defines.alice_needs_scaling_factor;
			}
			auto const employment = pop_demographics::get_employment(state, pop);
			auto const unemployed = size > 0.f ? std::max(0.f, 1.f - employment / size) : 0.f;
			auto const unmet = 1.f - unit(pop_demographics::get_life_needs(state, pop));
			finances[slot].hardship = std::max(finances[slot].hardship,
				unit(0.5f * unemployed + 0.5f * unmet) * 0.01f);
		}

		auto const before = current_distribution(state, province);
		auto const result = clear_market(before, finances, value, config);
		if(!result.enabled)
			return;

		store_distribution(state, province, result.after);
		state.world.province_set_industry_market_turnover(province, result.turnover);

		for(std::size_t group = 0; group < owner_group_count; ++group) {
			auto const delta = result.cash_delta[group];
			if(std::abs(delta) <= epsilon)
				continue;
			if(group == std::size_t(owner_group::state)) {
				apply_treasury(state, nation, delta);
			} else if(group == std::size_t(owner_group::foreign)) {
				// Foreign proceeds leave the domestic circuit through the bank
				// rather than being handed to a POP that does not live here.
				state.world.nation_set_national_bank(nation,
					std::max(0.f, finite_nonnegative(
						state.world.nation_get_national_bank(nation)) + delta));
			} else {
				apply_pop_cash(state, province, owner_group(group), delta, group_savings[group]);
			}
		}
		if(result.public_cost > 0.f)
			apply_treasury(state, nation, -result.public_cost);
		if(result.profit_tax > 0.f)
			apply_treasury(state, nation, result.profit_tax);
	});
}

} // namespace economy::industry_ownership
