#include "transformation_laws.hpp"

#include "culture.hpp"
#include "system_state.hpp"

#include <string_view>

namespace politics::transformation::laws {
namespace {

bool selected_option_is(sys::state const& state, dcon::nation_id nation,
		std::string_view option_key) {
	auto const key = state.lookup_key(option_key);
	if(!key)
		return false;
	bool selected = false;
	state.world.for_each_issue([&](dcon::issue_id issue) {
		auto const option = state.world.nation_get_issues(nation, issue);
		if(option && state.world.issue_option_get_name(option) == key)
			selected = true;
	});
	return selected;
}

} // namespace

snapshot for_nation(sys::state const& state, dcon::nation_id nation) {
	snapshot result;
	if(!nation || !state.world.nation_is_valid(nation))
		return result;

	// Existing economic-policy rules are retained only where they literally
	// grant the right to build factories or invest abroad. Franchise and social
	// spending deliberately have no effect on property law here.
	auto const rules = state.world.nation_get_combined_issue_rules(nation);
	auto const private_building = (rules & issue_rule::pop_build_factory) != 0;
	auto const state_building = (rules & issue_rule::build_factory) != 0;
	if(state_building && !private_building)
		result.industry = industry_regime::nationalizing;
	else if(private_building && !state_building)
		result.industry = industry_regime::privatizing;
	result.foreign_capital = (rules & issue_rule::allow_foreign_investment) != 0
		? foreign_capital_regime::permitted : foreign_capital_regime::prohibited;

	if(selected_option_is(state, nation, "alice_estates_concentration_limit"))
		result.estates = estate_regime::concentration_limit;
	if(selected_option_is(state, nation, "alice_tenants_regulated_rent"))
		result.tenants = tenant_regime::regulated_rent;
	else if(selected_option_is(state, nation, "alice_tenants_secure_tenure"))
		result.tenants = tenant_regime::secure_tenure;
	else if(selected_option_is(state, nation, "alice_tenants_right_to_buy"))
		result.tenants = tenant_regime::right_to_buy;
	if(selected_option_is(state, nation, "alice_industry_nationalizing"))
		result.industry = industry_regime::nationalizing;
	else if(selected_option_is(state, nation, "alice_industry_privatizing"))
		result.industry = industry_regime::privatizing;
	else if(selected_option_is(state, nation, "alice_industry_open_market"))
		result.industry = industry_regime::open_market;
	if(selected_option_is(state, nation, "alice_worker_ownership_buyout"))
		result.worker_ownership = worker_ownership_regime::buyout_right;
	if(selected_option_is(state, nation, "alice_foreign_capital_permitted"))
		result.foreign_capital = foreign_capital_regime::permitted;
	else if(selected_option_is(state, nation, "alice_foreign_capital_prohibited"))
		result.foreign_capital = foreign_capital_regime::prohibited;
	if(selected_option_is(state, nation, "alice_profit_tax_low"))
		result.profit_tax = profit_tax_regime::low;
	else if(selected_option_is(state, nation, "alice_profit_tax_standard"))
		result.profit_tax = profit_tax_regime::standard;
	else if(selected_option_is(state, nation, "alice_profit_tax_high"))
		result.profit_tax = profit_tax_regime::high;
	if(selected_option_is(state, nation, "alice_land_tax_low"))
		result.land_tax = land_tax_regime::low;
	else if(selected_option_is(state, nation, "alice_land_tax_standard"))
		result.land_tax = land_tax_regime::standard;
	else if(selected_option_is(state, nation, "alice_land_tax_high"))
		result.land_tax = land_tax_regime::high;
	if(selected_option_is(state, nation, "alice_collective_bargaining_recognized")
		|| selected_option_is(state, nation, "state_controlled"))
		result.collective_bargaining = collective_bargaining_regime::recognized;
	else if(selected_option_is(state, nation, "alice_collective_bargaining_protected")
		|| selected_option_is(state, nation, "non_socialist")
		|| selected_option_is(state, nation, "all_trade_unions"))
		result.collective_bargaining = collective_bargaining_regime::protected_right;
	return result;
}

float annual_profit_tax_rate(profit_tax_regime regime) {
	switch(regime) {
	case profit_tax_regime::low: return 0.05f;
	case profit_tax_regime::standard: return 0.15f;
	case profit_tax_regime::high: return 0.30f;
	case profit_tax_regime::none: break;
	}
	return 0.0f;
}

float annual_land_tax_rate(land_tax_regime regime) {
	switch(regime) {
	case land_tax_regime::low: return 0.05f;
	case land_tax_regime::standard: return 0.15f;
	case land_tax_regime::high: return 0.30f;
	case land_tax_regime::none: break;
	}
	return 0.0f;
}

} // namespace politics::transformation::laws
