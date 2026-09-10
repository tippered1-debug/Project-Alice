#pragma once

#include "dcon_generated_ids.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace sys {
struct state;
}

namespace economy::industry_ownership {

// Who owns the factories. Project Alice models land ownership but not
// industrial ownership: the factory object carries a type, a size, employment
// and costs, and nothing about who it belongs to. Capitalist income is a
// dividend from an abstract investment pool, so a capitalist owns nothing.
//
// These five shares are the counterpart of the land distribution. They are
// stored per province, like land, because the factory till is also a provincial
// aggregate. Capitalists are the residual so that an old save, whose new fields
// are all zero, loads as fully capitalist industry -- which is the right default
// and needs no migration pass.
struct distribution {
	float capitalists = 1.f;
	float landed_elites = 0.f;
	float state = 0.f;
	float foreign = 0.f;
	float workers = 0.f;

	constexpr distribution() = default;
	constexpr distribution(float capitalist, float landed, float state_owned,
			float foreign_owned, float worker_owned)
		: capitalists(capitalist), landed_elites(landed), state(state_owned),
			foreign(foreign_owned), workers(worker_owned) {
	}
};

enum class owner_group : uint8_t {
	capitalists = 0,
	landed_elites = 1,
	state = 2,
	foreign = 3,
	workers = 4,
	count = 5,
};

constexpr inline std::size_t owner_group_count = std::size_t(owner_group::count);

// Where a country's industry starts. Victoria 2 scenarios record no such thing,
// so the opening position is an explicit, inspectable choice rather than a
// number derived from whoever happens to live in the province.
enum class historical_profile : uint8_t {
	unassigned = 0,
	// Private industry, thin state involvement.
	liberal_private = 1,
	// State arsenals and railways alongside private mills.
	state_led = 2,
	// Industry largely financed and owned from abroad.
	foreign_concession = 3,
	// Landed families converted into industrial owners.
	junker_industrial = 4,
	// Cooperative and municipal ownership present from the start.
	cooperative = 5,
};

enum class ownership_law : uint8_t {
	// Ownership moves only through the market.
	open_market,
	// The state is acquiring industry.
	nationalizing,
	// The state is selling industry.
	privatizing,
};

struct market_config {
	bool enabled = false;
	// A month may turn over at most this fraction of provincial industry.
	float maximum_monthly_turnover = 0.004f;
	// Buyers may commit only cash above this many months of essential needs.
	float reserve_months = 6.f;
	// Holders list a small part of their stake each month.
	float voluntary_ask_rate = 0.001f;
	ownership_law regime = ownership_law::open_market;
	// Policy flows, as a fraction of all provincial industry per month.
	float nationalization_rate = 0.f;
	float privatization_rate = 0.f;
	// Transfer of ownership to the workforce, where law provides for it.
	float worker_buyout_rate = 0.f;
	// The state compensates dispossessed owners at this share of market value.
	float compensation_rate = 0.f;
	// Administrative capacity limits how much of a legal entitlement happens.
	float implementation_efficiency = 1.f;
	float available_public_funds = 0.f;
	bool foreign_investment_allowed = false;
	// Foreign holdings above this are wound down when the law forbids them.
	float foreign_divestment_rate = 0.f;
	// Share of local industry that recorded foreign investment would justify
	// owning. Foreign holdings converge on it while the law permits them, so a
	// concession grows because someone actually paid for it.
	float foreign_investment_target = 0.f;
	float foreign_investment_rate = 0.f;
	float annual_profit_tax_rate = 0.f;
	// Realized operating profit is the tax base. Asset value remains a
	// valuation input for ownership transfers and is never taxed as "profit".
	float monthly_taxable_profit = 0.f;
};

struct group_finance {
	float liquid_savings = 0.f;
	float monthly_essential_needs = 0.f;
	// Bounded 0..1: unmet needs, unemployment and lack of cash.
	float hardship = 0.f;
};

struct market_result {
	bool enabled = false;
	distribution before{};
	distribution after{};
	std::array<float, owner_group_count> cash_delta{};
	std::array<float, owner_group_count> bids{};
	std::array<float, owner_group_count> asks{};
	float industry_value = 0.f;
	float turnover = 0.f;
	float distress_asks = 0.f;
	float profit_tax = 0.f;
	float reform_turnover = 0.f;
	float compensation = 0.f;
	float public_cost = 0.f;
};

[[nodiscard]] distribution normalize(distribution value);

[[nodiscard]] std::array<float, owner_group_count> shares(distribution value);
[[nodiscard]] distribution from_shares(std::array<float, owner_group_count> const& value);

// Nine months of profit, the same window the land price uses, so the two asset
// markets are valued on a comparable basis.
[[nodiscard]] float update_smoothed_profit(float previous_daily_profit,
	float current_daily_profit, float window_days = 270.f);

// Capitalizes the smoothed profit. A loss-making industry is worth nothing
// rather than a negative amount: you cannot be paid to take a factory.
[[nodiscard]] float capitalized_value(float smoothed_daily_profit,
	float capitalization_years = 10.f);

[[nodiscard]] historical_profile profile_for_tag(uint32_t identifying_int);
[[nodiscard]] historical_profile profile_for(sys::state const& state, dcon::province_id province);
[[nodiscard]] std::string_view profile_localization_key(historical_profile profile);
[[nodiscard]] std::string_view ownership_law_localization_key(ownership_law law);
[[nodiscard]] distribution historical_initial_distribution(historical_profile profile);
void initialize_historical_profiles(sys::state& state);

[[nodiscard]] market_config configuration_for(sys::state const& state, dcon::province_id province);

[[nodiscard]] market_result clear_market(distribution current,
	std::array<group_finance, owner_group_count> finances,
	float industry_value, market_config const& config);

// Splits a day's factory profit between the owners. This is what makes the
// shares mean something: a capitalist's income becomes the return on what they
// own rather than a dividend from an abstract pool.
struct dividend_split {
	bool enabled = false;
	std::array<float, owner_group_count> payout{};
	float retained = 0.f;
};

[[nodiscard]] dividend_split split_dividend(distribution owners, float dividend);

// Monthly Project Alice adapter.
void update_markets(sys::state& state);

// Reads the persisted shares of a province, normalized.
[[nodiscard]] distribution current_distribution(sys::state const& state, dcon::province_id province);
void store_distribution(sys::state& state, dcon::province_id province, distribution value);

} // namespace economy::industry_ownership
