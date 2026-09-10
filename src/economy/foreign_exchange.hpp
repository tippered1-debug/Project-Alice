#pragma once

namespace economy::foreign_exchange {

struct inputs {
	bool enabled = false;
	float import_bill = 0.0f;
	float export_receipts = 0.0f;
	float liquid_reserves = 0.0f;
	float daily_reserve_release_share = 0.02f;
	// Bills of exchange and merchant overdrafts can bridge part of a daily
	// import bill before export receipts arrive. The market cash account is
	// signed, so this is financed trade credit rather than a free transfer.
	float minimum_trade_credit_share = 0.20f;
};

struct result {
	bool enabled = false;
	float available_settlement = 0.0f;
	float settled_imports = 0.0f;
	float liquidity_gap = 0.0f;
	float settlement_fraction = 1.0f;
	float exchange_rate_multiplier = 1.0f;
};

result evaluate(inputs const& inputs) noexcept;

} // namespace economy::foreign_exchange
