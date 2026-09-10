#include "foreign_exchange.hpp"

#include <algorithm>
#include <cmath>

namespace economy::foreign_exchange {
namespace {

float nonnegative(float value) noexcept {
	return std::isfinite(value) && value > 0.0f ? value : 0.0f;
}

} // namespace

result evaluate(inputs const& raw) noexcept {
	result result{};
	result.enabled = raw.enabled;
	if(!raw.enabled)
		return result;
	auto const import_bill = nonnegative(raw.import_bill);
	auto const exports = nonnegative(raw.export_receipts);
	auto const reserves = nonnegative(raw.liquid_reserves);
	auto const release_share = std::isfinite(raw.daily_reserve_release_share)
		? std::clamp(raw.daily_reserve_release_share, 0.0f, 1.0f) : 0.02f;
	auto const trade_credit_share = std::isfinite(raw.minimum_trade_credit_share)
		? std::clamp(raw.minimum_trade_credit_share, 0.0f, 1.0f) : 0.20f;
	result.available_settlement = std::max(
		exports + reserves * release_share,
		import_bill * trade_credit_share);
	result.settled_imports = std::min(import_bill, result.available_settlement);
	result.liquidity_gap = std::max(0.0f, import_bill - result.available_settlement);
	result.settlement_fraction = import_bill > 0.0f
		? std::clamp(result.settled_imports / import_bill, 0.0f, 1.0f) : 1.0f;
	result.exchange_rate_multiplier = import_bill > 0.0f
		? 1.0f + 2.0f * std::clamp(result.liquidity_gap / import_bill, 0.0f, 1.0f)
		: 1.0f;
	return result;
}

} // namespace economy::foreign_exchange
