#include "economy/foreign_exchange.hpp"

#include "catch2/catch.hpp"

#include <limits>

namespace foreign_exchange = economy::foreign_exchange;

TEST_CASE("foreign settlement uses exports before releasing reserves",
		"[economy][trade][foreign-exchange]") {
	foreign_exchange::inputs inputs{};
	inputs.enabled = true;
	inputs.import_bill = 100.0f;
	inputs.export_receipts = 50.0f;
	inputs.liquid_reserves = 1000.0f;
	inputs.daily_reserve_release_share = 0.02f;
	auto const result = foreign_exchange::evaluate(inputs);
	REQUIRE(result.available_settlement == Approx(70.0f));
	REQUIRE(result.settled_imports == Approx(70.0f));
	REQUIRE(result.settlement_fraction == Approx(0.70f));
	REQUIRE(result.exchange_rate_multiplier == Approx(1.60f));
}

TEST_CASE("balanced external trade clears without an exchange premium",
		"[economy][trade][foreign-exchange]") {
	foreign_exchange::inputs inputs{};
	inputs.enabled = true;
	inputs.import_bill = 100.0f;
	inputs.export_receipts = 100.0f;
	auto const result = foreign_exchange::evaluate(inputs);
	REQUIRE(result.settlement_fraction == Approx(1.0f));
	REQUIRE(result.exchange_rate_multiplier == Approx(1.0f));
	REQUIRE(result.liquidity_gap == Approx(0.0f));
}

TEST_CASE("merchant bills provide bounded short-term trade credit",
		"[economy][trade][foreign-exchange]") {
	foreign_exchange::inputs inputs{};
	inputs.enabled = true;
	inputs.import_bill = 100.0f;
	inputs.minimum_trade_credit_share = 0.20f;
	auto const result = foreign_exchange::evaluate(inputs);
	REQUIRE(result.available_settlement == Approx(20.0f));
	REQUIRE(result.settled_imports == Approx(20.0f));
	REQUIRE(result.settlement_fraction == Approx(0.20f));
	REQUIRE(result.exchange_rate_multiplier == Approx(2.60f));
}

TEST_CASE("classic foreign settlement is an exact no-op",
		"[economy][trade][foreign-exchange]") {
	foreign_exchange::inputs inputs{};
	inputs.enabled = false;
	inputs.import_bill = std::numeric_limits<float>::infinity();
	auto const result = foreign_exchange::evaluate(inputs);
	REQUIRE_FALSE(result.enabled);
	REQUIRE(result.settlement_fraction == Approx(1.0f));
	REQUIRE(result.exchange_rate_multiplier == Approx(1.0f));
}
