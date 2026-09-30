#include "economy_pops.hpp"
#include "economy/physical/exact_person_goods.hpp"
#include "economy/exact_person_economy.hpp"
#include "economy_constants.hpp"
#include "economy_stats.hpp"
#include "money.hpp"
#include "advanced_province_buildings.hpp"
#include "demographics.hpp"
#include "governance/public_administration.hpp"
#include "province_templates.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace economy {
namespace pops {
namespace {
float finite_nonnegative(float x) { return std::isfinite(x) ? std::max(0.0f, x) : 0.0f; }
bool belongs_to(sys::state const& state, persons::person_key key, dcon::pop_id pop) {
	return persons::alive(state, key) && persons::current_population(state, key) == pop;
}
std::array<consumption_category_projection*, 3> categories(population_consumption_projection& p) {
	return {&p.life_needs, &p.everyday_needs, &p.luxury_needs};
}
}

std::array<float, 3> compatibility_category_shares(sys::state const& state,
	persons::person_key owner, dcon::commodity_id commodity) {
	auto type = persons::source_pop_type(state, owner);
	std::array<float, 3> shares{};
	if(type && state.world.pop_type_is_valid(type)) {
		shares = {finite_nonnegative(state.world.pop_type_get_life_needs(type, commodity)),
			finite_nonnegative(state.world.pop_type_get_everyday_needs(type, commodity)),
			finite_nonnegative(state.world.pop_type_get_luxury_needs(type, commodity))};
	}
	auto total = double(shares[0]) + shares[1] + shares[2];
	if(total > 0.0) for(auto& share : shares) share = float(share / total);
	else shares[0] = 1.0f; // Unmapped exact needs remain visible in the basic bar.
	return shares;
}

population_consumption_projection project_consumption(sys::state const& state, dcon::pop_id pop) {
	population_consumption_projection result;
	if(!pop || !state.world.pop_is_valid(pop) || !state.exact_person_goods || !state.exact_person_economy) return result;
	using namespace economy::physical;
	auto positions = categories(result);
	for(auto const& n : exact_person_goods::need_records(state)) {
		if(!belongs_to(state, n.owner, pop)) continue;
		auto shares = compatibility_category_shares(state, n.owner, n.commodity);
		auto price = finite_nonnegative(state.world.commodity_get_cost(n.commodity));
		for(size_t i = 0; i < 3; ++i)
			positions[i]->required += n.desired_quantity_per_period * price * shares[i];
	}
	auto ratios = exact_person_goods::population_consumption_satisfaction(state, pop);
	for(size_t i = 0; i < 3; ++i) positions[i]->physical_consumption_ratio = ratios[i];
	for(auto const& fill : exact_person_goods::fill_records(state)) {
		if(fill.occurred_on != state.current_date) continue;
		auto transaction = exact_person_economy::transaction(state, fill.exact_transaction_id);
		if(!transaction || transaction->settlement != economy::money) continue;
		auto bid = exact_person_goods::bid(state, fill.exact_bid_id);
		if(!bid || !belongs_to(state, bid->buyer, pop)) continue;
		auto shares = compatibility_category_shares(state, bid->buyer, fill.commodity);
		auto value = fill.quantity * fill.execution_price;
		for(size_t i = 0; i < 3; ++i) positions[i]->spent += value * shares[i];
		result.spent_total += value;
	}
	result.cash = exact_person_economy::population_cash_balance(state, pop, economy::money);
	for(auto const& tx : exact_person_economy::transaction_records(state)) {
		if(tx.timestamp != state.current_date || tx.settlement != economy::money) continue;
		auto incoming = tx.destination.kind == exact_person_economy::account_kind::exact
			&& belongs_to(state, exact_person_economy::owner_of(state, tx.destination), pop);
		auto outgoing = tx.source.kind == exact_person_economy::account_kind::exact
			&& belongs_to(state, exact_person_economy::owner_of(state, tx.source), pop);
		if(incoming && tx.kind == relations::transaction_kind::payroll) result.payroll_received += tx.amount;
		if(incoming && tx.kind == relations::transaction_kind::public_spending) result.public_transfers_received += tx.amount;
		if(outgoing && tx.kind == relations::transaction_kind::tax_payment) result.tax_paid += tx.amount;
		if(outgoing && tx.kind == relations::transaction_kind::equity_contribution) result.capital_contributed += tx.amount;
	}
	return result;
}

float projected_spending(sys::state const& state, dcon::pop_id pop,
	dcon::commodity_id commodity, uint8_t category) {
	if(category >= 3 || !state.exact_person_goods || !pop) return 0.0f;
	float total = 0.0f;
	for(auto const& fill : physical::exact_person_goods::fill_records(state)) {
		if(fill.commodity != commodity || fill.occurred_on != state.current_date) continue;
		auto transaction = exact_person_economy::transaction(state, fill.exact_transaction_id);
		if(!transaction || transaction->settlement != economy::money) continue;
		auto bid = physical::exact_person_goods::bid(state, fill.exact_bid_id);
		if(bid && belongs_to(state, bid->buyer, pop))
			total += fill.quantity * fill.execution_price * compatibility_category_shares(state, bid->buyer, commodity)[category];
	}
	return total;
}

float education_access(sys::state const& state, dcon::pop_id pop) {
	if(!pop || !state.world.pop_is_valid(pop) || !state.exact_person_economy) return 0.0f;
	auto province = state.world.pop_get_province_from_pop_location(pop);
	if(!province) return 0.0f;
	auto nation = state.world.province_get_nation_from_province_ownership(province);
	auto ministry = governance::public_administration::institution_for(state, nation, governance::institution_kind::education_ministry);
	if(!ministry) return 0.0f;
	double teachers = 0.0;
	for(auto id : exact_person_economy::active_contracts_for_institution(state, ministry)) {
		auto contract = exact_person_economy::contract(state, id);
		if(!contract || !persons::alive(state, contract->worker) || !contract->workplace) continue;
		if(state.world.site_get_province_from_site_location(contract->workplace) == province)
			teachers += finite_nonnegative(contract->labor_capacity);
	}
	double population = 0.0;
	for(auto location : state.world.province_get_pop_location(province))
		population += finite_nonnegative(state.world.pop_get_size(location.get_pop()));
	// Same staffing target as the public job planner: one teacher per 1000 people.
	return population > 0.0 ? float(std::clamp(teachers / (population * 0.001), 0.0, 1.0)) : 0.0f;
}

std::vector<labor_ratio_wage> projected_payroll(sys::state const& state, dcon::pop_id pop) {
	auto paid = project_consumption(state, pop).payroll_received;
	return paid > 0.0f ? std::vector<labor_ratio_wage>{{labor::no_education, 1.0f, paid}} : std::vector<labor_ratio_wage>{};
}

std::vector<labor_ratio_wage> compatibility_wage_opportunities(sys::state const& state, dcon::province_id pid, dcon::pop_type_id ptid, bool accepted, float size) {
	float no_education_wage =
		state.world.province_get_labor_price(pid, labor::no_education)
		* state.world.province_get_labor_supply_sold(pid, labor::no_education);
	float basic_education_wage =
		state.world.province_get_labor_price(pid, labor::basic_education)
		* state.world.province_get_labor_supply_sold(pid, labor::basic_education); // craftsmen
	float high_education_wage =
		state.world.province_get_labor_price(pid, labor::high_education)
		* state.world.province_get_labor_supply_sold(pid, labor::high_education); // clerks, clergy and bureaucrats
	float guild_education_wage =
		state.world.province_get_labor_price(pid, labor::guild_education)
		* state.world.province_get_labor_supply_sold(pid, labor::guild_education); // artisans
	float high_education_and_accepted_wage =
		state.world.province_get_labor_price(pid, labor::high_education_and_accepted)
		* state.world.province_get_labor_supply_sold(pid, labor::high_education_and_accepted); // clerks, clergy and bureaucrats of accepted culture

	if(state.world.pop_type_get_is_paid_rgo_worker(ptid)) {
		auto no_education = state.world.province_get_pop_labor_distribution(pid, pop_labor::rgo_worker_no_education);
		return { {labor::no_education, no_education, no_education * size * no_education_wage } };
	} else if(state.culture_definitions.primary_factory_worker == ptid) {
		auto no_education = state.world.province_get_pop_labor_distribution(pid, pop_labor::primary_no_education);
		auto basic_education = state.world.province_get_pop_labor_distribution(pid, pop_labor::primary_basic_education);
		return {
			{labor::no_education, no_education, no_education * size * no_education_wage },
			{labor::basic_education, basic_education, basic_education * size * basic_education_wage }
		};
	} else if(state.culture_definitions.secondary_factory_worker == ptid || state.culture_definitions.bureaucrat == ptid || state.culture_definitions.clergy == ptid) {
		if(accepted) {
			auto no_education = state.world.province_get_pop_labor_distribution(pid, pop_labor::high_education_accepted_no_education);
			auto basic_education = state.world.province_get_pop_labor_distribution(pid, pop_labor::high_education_accepted_basic_education);
			auto high_education = state.world.province_get_pop_labor_distribution(pid, pop_labor::high_education_accepted_high_education);
			auto high_education_accepted = state.world.province_get_pop_labor_distribution(pid, pop_labor::high_education_accepted_high_education_accepted);
			return {
				{labor::no_education, no_education, no_education * size * no_education_wage },
				{labor::basic_education, basic_education, basic_education * size * basic_education_wage },
				{labor::high_education, high_education, high_education * size * high_education_wage },
				{labor::high_education_and_accepted, high_education_accepted, high_education_accepted * size * high_education_and_accepted_wage }
			};
		} else {
			auto no_education = state.world.province_get_pop_labor_distribution(pid, pop_labor::high_education_not_accepted_no_education);
			auto basic_education = state.world.province_get_pop_labor_distribution(pid, pop_labor::high_education_not_accepted_basic_education);
			auto high_education = state.world.province_get_pop_labor_distribution(pid, pop_labor::high_education_not_accepted_high_education);
			return {
				{labor::no_education, no_education, no_education * size * no_education_wage },
				{labor::basic_education, basic_education, basic_education * size * basic_education_wage },
				{labor::high_education, high_education, high_education * size * high_education_wage },
			};
		}
	}
	return {};
}


}

float estimate_pops_consumption(sys::state const& state, dcon::commodity_id c, dcon::province_id p) {
	if(!p || !state.world.province_is_valid(p) || !c
		|| !state.world.commodity_is_valid(c)) return 0.0f;
	auto needs = economy::physical::exact_person_goods::need_records(state);
	std::sort(needs.begin(), needs.end(), [](auto const& left, auto const& right) {
		if(left.owner.source_population_cell != right.owner.source_population_cell)
			return left.owner.source_population_cell < right.owner.source_population_cell;
		if(left.owner.ordinal != right.owner.ordinal)
			return left.owner.ordinal < right.owner.ordinal;
		return left.commodity.index() < right.commodity.index();
	});
	double total = 0.0;
	for(auto const& need : needs) {
		if(need.commodity != c || need.last_consumed_on != state.current_date
			|| !persons::alive(state, need.owner)) continue;
		auto home = persons::home_site(state, need.owner);
		if(!home || !state.world.site_is_valid(home)
			|| state.world.site_get_province_from_site_location(home) != p) continue;
		if(std::isfinite(need.last_consumed_quantity) && need.last_consumed_quantity > 0.0f)
			total += need.last_consumed_quantity;
	}
	return std::isfinite(total) && total <= double(std::numeric_limits<float>::max())
		? float(total) : 0.0f;
}
}
