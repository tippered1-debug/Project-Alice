#include "catch.hpp"

#include "canonical_consumer_fixture.hpp"
#include "actors/ownership.hpp"
#include "economy/dividends.hpp"
#include "economy/households.hpp"
#include "economy/wallets.hpp"
#include "economy/banking/banking.hpp"
#include "economy/physical/household_mobility.hpp"
#include "governance/governance.hpp"
#include "governance/finance/finance.hpp"
#include "persons/persons.hpp"

namespace capital_income_tests {

// Total cash in every DCON account and every exact account.
double total_money(sys::state const& state) {
	double total = 0.0;
	state.world.for_each_monetary_account([&](dcon::monetary_account_id account) {
		total += double(state.world.monetary_account_get_balance(account));
	});
	for(auto const& account : economy::exact_person_economy::export_snapshot(state).accounts) total += double(account.balance);
	return total;
}

struct fixture : canonical_consumer_tests::fixture {
	dcon::nation_id nation{};
	dcon::institution_id government{};
	dcon::economic_actor_id government_actor{};
	dcon::organization_id firm{};
	dcon::economic_actor_id firm_actor{};
	dcon::monetary_account_id firm_account{};
	persons::person_key founder_key{ 800, 0 };
	dcon::economic_actor_id founder_actor{};
	dcon::organization_id cohort{};
	dcon::economic_actor_id cohort_actor{};

	fixture() {
		nation = state->world.province_get_nation_from_province_ownership(province);
		government = governance::central_government_for(*state, nation);
		government_actor = governance::actor_for_institution(*state, government);
		firm = actors::organizations::create_company(*state);
		firm_actor = actors::organizations::actor_for_organization(*state, firm);
		firm_account = economy::accounts::open_account(*state, firm_actor, settlement);
		REQUIRE(economy::accounts::bootstrap_set_balance(*state, firm_account, 1000.0f));
		state->world.organization_set_retained_earnings(firm, 400.0f);
		state->world.organization_set_paid_in_equity(firm, 600.0f);

		persons::exact_population::cell_descriptor capitalists;
		capitalists.source_population_cell = 800;
		capitalists.literal_count = 8;
		capitalists.bootstrap_base_day = state->current_date.to_raw_value() - 1;
		capitalists.demographic_seed = 800;
		capitalists.home_site = site;
		REQUIRE(persons::exact_population::register_synthetic_population_cell(*state, capitalists).result
			== persons::exact_population::status::created);
		founder_actor = persons::actor_for_person(*state, persons::materialize_profile(*state, founder_key));
		REQUIRE(founder_actor);

		cohort = economy::households::create(*state, province, economy::households::role::peasant, settlement);
		cohort_actor = actors::organizations::actor_for_organization(*state, cohort);

		// Equity: 50% the founder, 25% the state, 15% the cohort, 10% another company.
		auto equity = actors::organizations::equity_asset_for_organization(*state, firm);
		REQUIRE(actors::ownership::create_stake(*state, founder_actor, equity, 0.5f, 0.5f, 0.5f));
		REQUIRE(actors::ownership::create_stake(*state, government_actor, equity, 0.25f, 0.25f, 0.25f));
		REQUIRE(actors::ownership::create_stake(*state, cohort_actor, equity, 0.15f, 0.15f, 0.15f));
		REQUIRE(actors::ownership::create_stake(*state, employer, equity, 0.1f, 0.1f, 0.1f));
	}
};

} // namespace capital_income_tests

TEST_CASE("each actor has exactly one cash ledger", "[economy][capital-income]") {
	capital_income_tests::fixture f;
	auto person = economy::wallets::open_for(*f.state, f.founder_actor, f.settlement);
	REQUIRE(person.kind == economy::exact_person_economy::account_kind::exact);
	REQUIRE(economy::exact_person_economy::owner_of(*f.state, person) == f.founder_key);
	auto treasury = economy::wallets::open_for(*f.state, f.government_actor, f.settlement);
	REQUIRE(treasury.kind == economy::exact_person_economy::account_kind::dcon);
	REQUIRE(governance::finance::treasury_institution_for(*f.state, treasury.dcon_account) == f.government);
	auto company = economy::wallets::account_for(*f.state, f.firm_actor, f.settlement);
	REQUIRE(company.dcon_account == f.firm_account);
	REQUIRE(economy::wallets::open_for(*f.state, f.founder_actor, f.settlement) == person);
}

TEST_CASE("a firm without operations returns all of its cash to its owners", "[economy][capital-income]") {
	capital_income_tests::fixture f;
	REQUIRE(economy::dividends::winding_up(*f.state, f.firm));
	auto before = capital_income_tests::total_money(*f.state);
	auto paid = economy::dividends::pay(*f.state, f.firm);
	REQUIRE(paid == Approx(1000.0f));
	REQUIRE(capital_income_tests::total_money(*f.state) == Approx(before));
	REQUIRE(economy::accounts::balance(*f.state, f.firm_account) == Approx(0.0f));
	REQUIRE(economy::exact_person_economy::balance(*f.state,
		economy::wallets::account_for(*f.state, f.founder_actor, f.settlement)) == Approx(500.0f));
	REQUIRE(economy::accounts::balance(*f.state,
		governance::finance::treasury_account_for(*f.state, f.government, f.settlement)) == Approx(250.0f));
	REQUIRE(economy::accounts::balance(*f.state,
		economy::accounts::find_account(*f.state, f.cohort_actor, f.settlement)) == Approx(150.0f));
	REQUIRE(economy::accounts::balance(*f.state, f.payer) == Approx(10000.0f + 100.0f));
	// 400 came out of earnings and 600 returned paid-in capital.
	REQUIRE(f.state->world.organization_get_retained_earnings(f.firm) == Approx(0.0f));
	REQUIRE(f.state->world.organization_get_paid_in_equity(f.firm) == Approx(0.0f));
}

TEST_CASE("an operating firm pays half its earnings above a cash buffer", "[economy][capital-income]") {
	capital_income_tests::fixture f;
	REQUIRE(actors::organizations::transfer_factory_operator(*f.state, f.firm, f.factory));
	REQUIRE_FALSE(economy::dividends::winding_up(*f.state, f.firm));
	f.state->world.factory_set_agency_recent_costs(f.factory, 5.0f); // a 450 buffer
	// Half of 400 earnings is 200, and 1000 - 450 leaves room for it.
	REQUIRE(economy::dividends::payable(*f.state, f.firm) == Approx(200.0f));
	f.state->world.factory_set_agency_recent_costs(f.factory, 10.0f); // a 900 buffer
	REQUIRE(economy::dividends::payable(*f.state, f.firm) == Approx(100.0f));
	f.state->world.organization_set_retained_earnings(f.firm, -50.0f);
	REQUIRE(economy::dividends::payable(*f.state, f.firm) == Approx(0.0f));
}

TEST_CASE("a person's dividend lands in the ledger they consume from", "[economy][capital-income]") {
	capital_income_tests::fixture f;
	REQUIRE(economy::physical::exact_person_goods::needs_for_person(*f.state, f.founder_key).empty());
	(void)economy::dividends::pay(*f.state, f.firm);
	auto ledger = economy::wallets::account_for(*f.state, f.founder_actor, f.settlement);
	REQUIRE(economy::exact_person_economy::balance(*f.state, ledger) == Approx(500.0f));
	// Living on their own cash, the founder is an individual consumer.
	economy::physical::household_mobility::update_employed_households(*f.state);
	REQUIRE(economy::physical::exact_person_goods::need_profile_imported(*f.state, f.founder_key));
}

TEST_CASE("dividends run once per period for each organization", "[economy][capital-income]") {
	capital_income_tests::fixture f;
	auto before = capital_income_tests::total_money(*f.state);
	for(int day = 0; day < economy::dividends::payout_period_days; ++day) {
		economy::dividends::process(*f.state);
		f.state->current_date += 1;
	}
	REQUIRE(economy::accounts::balance(*f.state, f.firm_account) == Approx(0.0f));
	REQUIRE(capital_income_tests::total_money(*f.state) == Approx(before));
}

TEST_CASE("an ownerless or unconfigured bank pays nothing", "[economy][capital-income]") {
	capital_income_tests::fixture f;
	auto bank = economy::banking::create_bank(*f.state);
	REQUIRE(bank);
	REQUIRE(economy::dividends::payable(*f.state, bank) == Approx(0.0f));
}
