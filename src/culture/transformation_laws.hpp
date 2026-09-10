#pragma once

#include "dcon_generated_ids.hpp"

#include <cstdint>

namespace sys {
struct state;
}

namespace politics::transformation::laws {

// These are legal institutions, not generic bonuses. Scenario and mod authors
// opt into them with issue-option keys documented beside each enum value.
enum class estate_regime : uint8_t {
	unrestricted,       // alice_estates_unrestricted
	concentration_limit // alice_estates_concentration_limit
};

enum class tenant_regime : uint8_t {
	free_contract, // alice_tenants_free_contract
	regulated_rent, // alice_tenants_regulated_rent
	secure_tenure, // alice_tenants_secure_tenure
	right_to_buy, // alice_tenants_right_to_buy
};

enum class industry_regime : uint8_t {
	open_market, // alice_industry_open_market
	nationalizing, // alice_industry_nationalizing
	privatizing, // alice_industry_privatizing
};

enum class worker_ownership_regime : uint8_t {
	none, // alice_worker_ownership_none
	buyout_right, // alice_worker_ownership_buyout
};

enum class foreign_capital_regime : uint8_t {
	prohibited, // alice_foreign_capital_prohibited
	permitted, // alice_foreign_capital_permitted
};

enum class profit_tax_regime : uint8_t {
	none, // alice_profit_tax_none
	low, // alice_profit_tax_low (5 percent)
	standard, // alice_profit_tax_standard (15 percent)
	high, // alice_profit_tax_high (30 percent)
};

enum class land_tax_regime : uint8_t {
	none, // alice_land_tax_none
	low, // alice_land_tax_low (5 percent)
	standard, // alice_land_tax_standard (15 percent)
	high, // alice_land_tax_high (30 percent)
};

enum class collective_bargaining_regime : uint8_t {
	prohibited, // alice_collective_bargaining_prohibited
	recognized, // alice_collective_bargaining_recognized
	protected_right, // alice_collective_bargaining_protected
};

struct snapshot {
	estate_regime estates = estate_regime::unrestricted;
	tenant_regime tenants = tenant_regime::free_contract;
	industry_regime industry = industry_regime::open_market;
	worker_ownership_regime worker_ownership = worker_ownership_regime::none;
	foreign_capital_regime foreign_capital = foreign_capital_regime::prohibited;
	profit_tax_regime profit_tax = profit_tax_regime::none;
	land_tax_regime land_tax = land_tax_regime::none;
	collective_bargaining_regime collective_bargaining =
		collective_bargaining_regime::prohibited;
};

[[nodiscard]] snapshot for_nation(sys::state const& state, dcon::nation_id nation);
[[nodiscard]] float annual_profit_tax_rate(profit_tax_regime regime);
[[nodiscard]] float annual_land_tax_rate(land_tax_regime regime);

} // namespace politics::transformation::laws
