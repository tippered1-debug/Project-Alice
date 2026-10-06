#include "catch.hpp"

#include "actors/organizations/organizations.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/collective_labor.hpp"
#include "economy/exact_person_economy.hpp"
#include "economy/industrial_production.hpp"
#include "economy/relations/relations.hpp"

namespace collective_labor_tests {

using primary_production_tests::fixture;
namespace labor = economy::collective_labor;
namespace exact = economy::exact_person_economy;

uint64_t first_contract(fixture const& f) {
	auto contracts = exact::active_contracts_for_factory(*f.state, f.plant);
	return contracts.empty() ? 0 : contracts.front();
}

} // namespace collective_labor_tests

TEST_CASE("union dues are transferred from the exact worker wallet into union treasury", "[economy][labor][union]") {
	using namespace collective_labor_tests;
	fixture f;
	f.hire();
	auto contract_id = first_contract(f);
	REQUIRE(contract_id != 0);
	auto worker_contract_result = exact::contract(*f.state, contract_id);
	REQUIRE(worker_contract_result.has_value());
	auto worker_contract = *worker_contract_result;
	auto wallet = exact::open_account(*f.state, worker_contract.worker, f.settlement);
	REQUIRE(exact::set_balance(*f.state, wallet, 20.0f));

	auto union_id = labor::create_union(*f.state, f.nation, 0.10f, 0.50f);
	REQUIRE(union_id);
	REQUIRE(labor::join_union(*f.state, union_id, worker_contract.worker, f.state->current_date));
	REQUIRE(labor::member_count(*f.state, union_id) == 1);
	REQUIRE(labor::is_union_member(*f.state, union_id, worker_contract.worker));

	auto expected_dues = exact::wage_due(*f.state, contract_id) * 0.10f;
	labor::process(*f.state);
	float dues_paid = 0.0f;
	for(auto const& transaction : exact::transaction_records(*f.state))
		if(transaction.kind == economy::relations::transaction_kind::union_dues
			&& transaction.timestamp == f.state->current_date)
			dues_paid += transaction.amount;
	REQUIRE(dues_paid == Approx(expected_dues));
	REQUIRE(exact::balance(*f.state, wallet) == Approx(20.0f - expected_dues));
	REQUIRE(labor::leave_union(*f.state, union_id, worker_contract.worker, f.state->current_date));
	REQUIRE_FALSE(labor::is_union_member(*f.state, union_id, worker_contract.worker));
}

TEST_CASE("a union strike withholds its exact workers, stops output and pays benefits from union cash", "[economy][labor][strike]") {
	using namespace collective_labor_tests;
	fixture f;
	f.hire();
	f.hire();
	auto contracts = exact::active_contracts_for_factory(*f.state, f.plant);
	REQUIRE(contracts.size() == 2);
	auto contract_id = contracts.front();
	auto worker_contract_result = exact::contract(*f.state, contract_id);
	REQUIRE(worker_contract_result.has_value());
	auto worker_contract = *worker_contract_result;
	auto other_id = contracts.back();
	auto union_id = labor::create_union(*f.state, f.nation, 0.0f, 0.50f);
	REQUIRE(union_id);
	REQUIRE(labor::join_union(*f.state, union_id, worker_contract.worker, f.state->current_date));

	// An employer association can bargain for its member firms as one unit.
	auto association = labor::create_employer_association(*f.state, f.nation);
	REQUIRE(association);
	REQUIRE(labor::add_employer(*f.state, association, f.company));
	REQUIRE(labor::employers(*f.state, association) == std::vector<dcon::organization_id>{f.company});
	auto unit = labor::request_association_bargaining(*f.state, union_id, association, 2.0f, 2);
	REQUIRE(unit);
	REQUIRE(labor::recognize_by_employer(*f.state, unit));
	REQUIRE(labor::submit_employer_offer(*f.state, unit, 1.0f, 1));
	auto action = labor::call_strike(*f.state, unit, f.state->current_date);
	REQUIRE(action);
	REQUIRE(labor::contract_is_withheld(*f.state, contract_id, f.state->current_date));
	REQUIRE_FALSE(labor::contract_is_withheld(*f.state, other_id, f.state->current_date));
	REQUIRE(exact::wage_due(*f.state, contract_id) == Approx(0.0f));
	REQUIRE(exact::wage_due(*f.state, other_id) > 0.0f);
	REQUIRE(exact::labor_supplied_to_factory(*f.state, f.plant) == Approx(1.0f));

	auto union_org = f.state->world.labor_union_get_organization_from_labor_union_organization(union_id);
	auto union_actor = actors::organizations::actor_for_organization(*f.state, union_org);
	auto treasury = economy::accounts::open_account(*f.state, union_actor, f.settlement);
	REQUIRE(economy::accounts::bootstrap_set_balance(*f.state, treasury, 5.0f));
	labor::process(*f.state);
	REQUIRE(f.state->world.strike_action_get_withheld_labor_today(action) == Approx(1.0f));
	REQUIRE(f.state->world.strike_action_get_cumulative_lost_labor(action) == Approx(1.0f));
	REQUIRE(economy::industrial_production::produce_factory(*f.state, f.plant) == Approx(2.0f));
	// The remaining worker supplies labor; the union member's capacity is absent.
	float benefits = 0.0f;
	for(auto const& transaction : exact::transaction_records(*f.state))
		if(transaction.kind == economy::relations::transaction_kind::strike_benefit
			&& transaction.timestamp == f.state->current_date)
			benefits += transaction.amount;
	REQUIRE(benefits == Approx(0.5f));
	REQUIRE(economy::accounts::balance(*f.state, treasury) == Approx(4.5f));

	auto agreement = labor::resolve_conflict(*f.state, action, 1.5f, 2, f.state->current_date);
	REQUIRE(agreement);
	REQUIRE(f.state->world.strike_action_get_status(action)
		== uint8_t(labor::conflict_status::resolved));
	f.state->current_date += 1;
	labor::process(*f.state);
	REQUIRE(labor::wage_floor_for_contract(*f.state, contract_id, f.state->current_date) == Approx(1.5f));
	REQUIRE(exact::wage_due(*f.state, contract_id) == Approx(1.5f));
	REQUIRE(exact::labor_supplied_to_factory(*f.state, f.plant) == Approx(2.0f));
}

TEST_CASE("an employer lockout withholds every contract in the bargaining unit", "[economy][labor][lockout]") {
	using namespace collective_labor_tests;
	fixture f;
	f.hire();
	f.hire();
	auto contracts = exact::active_contracts_for_factory(*f.state, f.plant);
	REQUIRE(contracts.size() == 2);
	auto member_result = exact::contract(*f.state, contracts.front());
	REQUIRE(member_result.has_value());
	auto member = *member_result;
	auto union_id = labor::create_union(*f.state, f.nation, 0.0f, 0.0f);
	REQUIRE(union_id);
	REQUIRE(labor::join_union(*f.state, union_id, member.worker, f.state->current_date));
	auto unit = labor::request_bargaining(*f.state, union_id, f.company, 2.0f, 1);
	REQUIRE(unit);
	REQUIRE(labor::recognize_by_employer(*f.state, unit));
	REQUIRE(labor::submit_employer_offer(*f.state, unit, 1.0f, 1));
	auto action = labor::declare_lockout(*f.state, unit, f.state->current_date);
	REQUIRE(action);
	for(auto id : contracts) {
		REQUIRE(labor::contract_is_withheld(*f.state, id, f.state->current_date));
		REQUIRE(exact::wage_due(*f.state, id) == Approx(0.0f));
	}
	REQUIRE(exact::labor_supplied_to_factory(*f.state, f.plant) == Approx(0.0f));
	REQUIRE(f.state->world.strike_action_get_kind(action) == uint8_t(labor::conflict_kind::lockout));
}
