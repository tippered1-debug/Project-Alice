#include "catch.hpp"

#include "canonical_consumer_fixture.hpp"
#include "actors/ownership.hpp"
#include "economy/banking/banking.hpp"
#include "economy/capital_market.hpp"
#include "economy/capital_projects.hpp"
#include "economy/households.hpp"
#include "economy/industrial_dynamics.hpp"
#include "economy/liquidity.hpp"
#include "economy/wallets.hpp"
#include "economy/physical/inventory.hpp"
#include "persons/exact_population.hpp"
#include "persons/persons.hpp"

namespace capital_allocation_tests {

// Base money: every operating and reserve account plus every exact account.
double base_money(sys::state const& state) {
	double total = 0.0;
	state.world.for_each_monetary_account([&](dcon::monetary_account_id account) {
		total += double(state.world.monetary_account_get_balance(account));
	});
	for(auto const& account : economy::exact_person_economy::export_snapshot(state).accounts) total += double(account.balance);
	return total;
}

float stake_of(sys::state const& state, dcon::asset_id asset, dcon::economic_actor_id owner) {
	float result = 0.0f;
	state.world.asset_for_each_ownership_stake_asset_as_asset(asset, [&](dcon::ownership_stake_asset_id relation) {
		auto stake = state.world.ownership_stake_asset_get_ownership_stake(relation);
		if(state.world.ownership_stake_get_economic_actor_from_ownership_stake_owner(stake) == owner)
			result += state.world.ownership_stake_get_economic_fraction(stake);
	});
	return result;
}

float total_stakes(sys::state const& state, dcon::asset_id asset) {
	float result = 0.0f;
	state.world.asset_for_each_ownership_stake_asset_as_asset(asset, [&](dcon::ownership_stake_asset_id relation) {
		result += state.world.ownership_stake_get_economic_fraction(state.world.ownership_stake_asset_get_ownership_stake(relation));
	});
	return result;
}

struct fixture : canonical_consumer_tests::fixture {
	dcon::nation_id nation{};
	dcon::organization_id bank{};
	dcon::commodity_id material{};

	fixture() {
		nation = state->world.province_get_nation_from_province_ownership(province);
		bank = economy::banking::create_bank(*state);
		economy::banking::bank_policy policy{};
		policy.jurisdiction = nation;
		policy.settlement = settlement;
		policy.lending_base_rate = 0.03f;
		policy.minimum_capital_ratio = 0.08f;
		policy.liquidity_target = 0.05f;
		policy.risk_appetite = 1.0f;
		policy.max_single_borrower_exposure = 1.0f;
		policy.reserve_requirement = 0.05f;
		policy.capital_breach_grace_days = 30;
		REQUIRE(economy::banking::configure_bank_policy(*state, bank, policy));
		auto reserve = economy::banking::open_reserve_account(*state, bank, settlement);
		REQUIRE(economy::banking::bootstrap_set_reserve_balance(*state, reserve, 20000.0f));
		economy::banking::update_bank_statuses(*state, state->current_date);
		actors::ownership::assign_runtime_canonical_id(*state, bank);

		material = state->world.create_commodity();
		state->world.commodity_set_cost(material, 10.0f);
		state->world.market_resize_price(state->world.commodity_size());
		// Building one unit of the fixture plant takes 10 of material: its
		// replacement value is 10 * 1 * 0.5 * 10 = 50.
		set_construction(type, 10.0f);

		persons::exact_population::cell_descriptor people;
		people.source_population_cell = 900;
		people.literal_count = 8;
		people.bootstrap_base_day = state->current_date.to_raw_value() - 1;
		people.demographic_seed = 900;
		people.home_site = site;
		REQUIRE(persons::exact_population::register_synthetic_population_cell(*state, people).result
			== persons::exact_population::status::created);
	}

	void set_construction(dcon::factory_type_id plant, float amount) {
		economy::commodity_set costs{};
		costs.commodity_type[0] = material;
		costs.commodity_amounts[0] = amount;
		state->world.factory_type_set_construction_costs(plant, costs);
	}

	dcon::deposit_account_id save(dcon::economic_actor_id actor, float amount) {
		actors::ownership::assign_runtime_canonical_id(*state, actor);
		auto deposit = economy::banking::open_deposit_account(*state, bank, actor, settlement);
		REQUIRE(deposit);
		REQUIRE(economy::banking::bootstrap_set_deposit_balance(*state, deposit, amount));
		return deposit;
	}

	dcon::economic_actor_id cohort(economy::households::role role, float savings) {
		auto household = economy::households::create(*state, province, role, settlement);
		auto actor = actors::organizations::actor_for_organization(*state, household);
		(void)save(actor, savings);
		return actor;
	}

	dcon::economic_actor_id company(float cash, dcon::monetary_account_id* wallet = nullptr) {
		auto actor = actors::organizations::actor_for_organization(*state, actors::organizations::create_company(*state));
		actors::ownership::assign_runtime_canonical_id(*state, actor);
		auto account = economy::accounts::open_account(*state, actor, settlement);
		REQUIRE(economy::accounts::bootstrap_set_balance(*state, account, cash));
		if(wallet) *wallet = account;
		return actor;
	}

	dcon::economic_actor_id person(uint64_t ordinal, float savings) {
		auto actor = persons::actor_for_person(*state, persons::materialize_profile(*state, persons::person_key{ 900, ordinal }));
		REQUIRE(actor);
		REQUIRE(economy::wallets::open_for(*state, actor, settlement));
		(void)save(actor, savings);
		return actor;
	}

	void make_due(dcon::economic_actor_id actor) {
		state->world.economic_actor_set_industrial_last_decision_date(actor,
			state->current_date - economy::industrial_dynamics::investor_review_days);
	}

	// Every sponsor in the world reviews today only if made due explicitly.
	void quiet_everyone() {
		state->world.for_each_economic_actor([&](dcon::economic_actor_id actor) {
			state->world.economic_actor_set_industrial_last_decision_date(actor, state->current_date);
		});
	}
};

} // namespace capital_allocation_tests

using capital_allocation_tests::base_money;

TEST_CASE("a capital project hands its unspent budget back when it ends", "[economy][capital-allocation]") {
	capital_allocation_tests::fixture f;
	auto money = base_money(*f.state);
	auto cancelled = economy::capital_projects::create_factory_expansion(*f.state, f.factory, 1.0f, f.settlement);
	REQUIRE(cancelled);
	REQUIRE(economy::capital_projects::add_requirement(*f.state, cancelled, f.material, 1.0f));
	REQUIRE(economy::capital_projects::fund(*f.state, cancelled, f.payer, 500.0f));
	REQUIRE(economy::accounts::balance(*f.state, f.payer) == Approx(9500.0f));
	// The project account is earmarked cash, never the sponsor's operating account.
	REQUIRE(economy::accounts::find_account(*f.state, f.employer, f.settlement) == f.payer);
	REQUIRE(economy::capital_projects::cancel(*f.state, cancelled));
	REQUIRE(economy::accounts::balance(*f.state, f.payer) == Approx(10000.0f));
	REQUIRE(economy::accounts::balance(*f.state,
		f.state->world.capital_project_get_monetary_account_from_capital_project_account(cancelled)) == Approx(0.0f));

	auto built = economy::capital_projects::create_factory_expansion(*f.state, f.factory, 1.0f, f.settlement);
	auto requirement = economy::capital_projects::add_requirement(*f.state, built, f.material, 1.0f);
	REQUIRE(economy::capital_projects::fund(*f.state, built, f.payer, 300.0f));
	auto yard = f.state->world.capital_project_get_site_from_capital_project_site(built);
	REQUIRE(economy::physical::inventory::add(*f.state, yard, f.material, 1.0f, f.employer) == Approx(1.0f));
	REQUIRE(economy::capital_projects::consume(*f.state, requirement, 1.0f) == Approx(1.0f));
	REQUIRE(f.state->world.capital_project_get_status(built) == uint8_t(economy::capital_projects::status::completed));
	REQUIRE(economy::accounts::balance(*f.state, f.payer) == Approx(10000.0f));
	REQUIRE(base_money(*f.state) == Approx(money));
}

TEST_CASE("investors buy new shares only when the earnings yield beats their required return", "[economy][capital-allocation]") {
	capital_allocation_tests::fixture f;
	// Only the two cohorts have savings to commit.
	REQUIRE(economy::accounts::bootstrap_set_balance(*f.state, f.payer, 0.0f));
	auto issuer = actors::organizations::create_company(*f.state);
	auto issuer_actor = actors::organizations::actor_for_organization(*f.state, issuer);
	auto equity = actors::organizations::equity_asset_for_organization(*f.state, issuer);
	auto owner = f.company(0.0f);
	REQUIRE(actors::ownership::create_stake(*f.state, owner, equity, 1.0f, 1.0f, 1.0f));
	f.state->world.organization_set_paid_in_equity(issuer, 1000.0f);
	auto rich = f.cohort(economy::households::role::urban, 4000.0f);
	auto modest = f.cohort(economy::households::role::peasant, 2000.0f);
	// Savers commit half their savings, and no more than a quarter of that to one offering.
	REQUIRE(economy::capital_market::investable_funds(*f.state, rich, f.settlement) == Approx(2000.0f));
	REQUIRE(economy::capital_market::required_return(*f.state, rich, f.settlement) == Approx(0.08f));

	auto pool = economy::capital_market::build_pool(*f.state);
	economy::capital_market::offering terms{};
	terms.issuer = issuer;
	terms.nation = f.nation;
	terms.settlement = f.settlement;
	terms.amount = 300.0f;
	terms.pre_money = economy::capital_market::book_value(*f.state, issuer);
	terms.annual_earnings = 50.0f; // a 3.8% yield on 1300
	auto money = base_money(*f.state);
	REQUIRE(economy::capital_market::raise(*f.state, pool, terms) == Approx(0.0f));
	REQUIRE(capital_allocation_tests::stake_of(*f.state, equity, owner) == Approx(1.0f));

	terms.annual_earnings = 260.0f; // a 20% yield
	REQUIRE(economy::capital_market::raise(*f.state, pool, terms) == Approx(300.0f));
	// Demand of 500 and 250 fills the 300 pro rata.
	REQUIRE(economy::banking::deposit_balance(*f.state, economy::banking::deposit_account_for(*f.state, rich, f.settlement)) == Approx(3800.0f));
	REQUIRE(economy::banking::deposit_balance(*f.state, economy::banking::deposit_account_for(*f.state, modest, f.settlement)) == Approx(1900.0f));
	REQUIRE(economy::wallets::spendable(*f.state, economy::wallets::account_for(*f.state, issuer_actor, f.settlement)) == Approx(300.0f));
	// The old owner is diluted, not called for cash.
	REQUIRE(capital_allocation_tests::stake_of(*f.state, equity, owner) == Approx(1000.0f / 1300.0f));
	REQUIRE(capital_allocation_tests::stake_of(*f.state, equity, rich) == Approx(200.0f / 1300.0f));
	REQUIRE(capital_allocation_tests::stake_of(*f.state, equity, modest) == Approx(100.0f / 1300.0f));
	REQUIRE(capital_allocation_tests::total_stakes(*f.state, equity) == Approx(1.0f));
	REQUIRE(f.state->world.organization_get_paid_in_equity(issuer) == Approx(1300.0f));
	REQUIRE(base_money(*f.state) == Approx(money));
}

TEST_CASE("a sound plant is never sold; a bankrupt plant is auctioned to its owners' benefit", "[economy][capital-allocation]") {
	capital_allocation_tests::fixture f;
	auto plant = f.state->world.create_asset();
	f.state->world.force_create_factory_asset(f.factory, plant);
	auto landlord = f.company(0.0f);
	REQUIRE(actors::ownership::create_stake(*f.state, landlord, plant, 0.6f, 0.6f, 0.6f));
	REQUIRE(actors::ownership::create_stake(*f.state, f.employer, plant, 0.4f, 0.4f, 0.4f));
	f.state->world.factory_set_agency_recent_profit(f.factory, 5.0f);
	dcon::monetary_account_id buyer_wallet{};
	auto buyer = f.company(1000.0f, &buyer_wallet);
	(void)f.save(buyer, 0.0f);

	f.quiet_everyone();
	f.make_due(buyer);
	REQUIRE(economy::industrial_dynamics::asking_price_for(*f.state, f.factory) == Approx(0.0f));
	economy::industrial_dynamics::process(*f.state);
	REQUIRE(actors::organizations::operator_actor_for_factory(*f.state, f.factory) == f.employer);
	REQUIRE(economy::accounts::balance(*f.state, buyer_wallet) == Approx(1000.0f));

	// Bankruptcy opens an auction at 60% of replacement value, falling to 20%.
	f.state->world.factory_set_agency_lifecycle_status(f.factory, 2);
	f.state->world.factory_set_agency_last_lifecycle_date(f.factory, f.state->current_date);
	REQUIRE(economy::industrial_dynamics::asking_price_for(*f.state, f.factory) == Approx(30.0f));
	f.state->current_date = f.state->current_date + 60;
	REQUIRE(economy::industrial_dynamics::asking_price_for(*f.state, f.factory) == Approx(20.0f));

	auto money = base_money(*f.state);
	f.quiet_everyone();
	f.make_due(buyer);
	economy::industrial_dynamics::process(*f.state);
	REQUIRE(actors::organizations::operator_actor_for_factory(*f.state, f.factory) == buyer);
	REQUIRE(capital_allocation_tests::stake_of(*f.state, plant, buyer) == Approx(1.0f));
	REQUIRE(economy::accounts::balance(*f.state, buyer_wallet) == Approx(980.0f));
	// The plant's owners share the price: 60% to the landlord, 40% stays with the operator.
	REQUIRE(economy::accounts::balance(*f.state, economy::accounts::find_account(*f.state, landlord, f.settlement)) == Approx(12.0f));
	REQUIRE(economy::accounts::balance(*f.state, f.payer) == Approx(10008.0f));
	REQUIRE(base_money(*f.state) == Approx(money));
}

TEST_CASE("a saver founds a company that the public funds, and nobody duplicates it", "[economy][capital-allocation]") {
	capital_allocation_tests::fixture f;
	// A product that sells at 20 and needs only labor: a strong greenfield case.
	auto product = f.state->world.create_commodity();
	f.state->world.commodity_set_cost(product, 20.0f);
	f.state->world.market_resize_price(f.state->world.commodity_size());
	auto works = f.state->world.create_factory_type();
	f.state->world.factory_type_set_output(works, product);
	f.state->world.factory_type_set_output_amount(works, 1.0f);
	f.state->world.factory_type_set_base_workforce(works, 1.0f);
	f.set_construction(works, 20.0f); // capital cost 50, budget 75
	auto founder = f.person(0, 40.0f);
	auto rival = f.person(1, 400.0f);
	auto public_savings = f.cohort(economy::households::role::urban, 2000.0f);

	f.quiet_everyone();
	f.make_due(founder);
	f.make_due(rival);
	auto money = base_money(*f.state);
	economy::industrial_dynamics::process(*f.state);

	std::vector<dcon::capital_project_id> projects;
	f.state->world.for_each_capital_project([&](dcon::capital_project_id project) {
		if(f.state->world.capital_project_get_factory_type(project) == works) projects.push_back(project);
	});
	// Two sponsors reviewed on the same day; only one plant is built.
	REQUIRE(projects.size() == 1);
	auto project = projects.front();
	REQUIRE(f.state->world.capital_project_get_status(project) == uint8_t(economy::capital_projects::status::funded));
	auto company = f.state->world.capital_project_get_economic_actor_from_capital_project_sponsor(project);
	auto organization = actors::organizations::organization_for_actor(*f.state, company);
	auto equity = actors::organizations::equity_asset_for_organization(*f.state, organization);
	// The founder holds a share and outside savers hold the rest.
	auto founder_share = capital_allocation_tests::stake_of(*f.state, equity, founder)
		+ capital_allocation_tests::stake_of(*f.state, equity, rival);
	REQUIRE(founder_share > 0.0f);
	REQUIRE(founder_share < 1.0f);
	REQUIRE(capital_allocation_tests::stake_of(*f.state, equity, public_savings) > 0.0f);
	REQUIRE(capital_allocation_tests::total_stakes(*f.state, equity) == Approx(1.0f));
	// The project holds its budget; the company keeps working capital.
	REQUIRE(economy::accounts::balance(*f.state,
		f.state->world.capital_project_get_monetary_account_from_capital_project_account(project)) > 0.0f);
	REQUIRE(f.state->world.organization_get_paid_in_equity(organization)
		== Approx(economy::accounts::balance(*f.state, economy::accounts::find_account(*f.state, company, f.settlement))
			+ economy::accounts::balance(*f.state, f.state->world.capital_project_get_monetary_account_from_capital_project_account(project))));
	REQUIRE(base_money(*f.state) == Approx(money));

	// A month later the project is still being built: no second copy.
	f.state->current_date = f.state->current_date + 30;
	f.quiet_everyone();
	f.make_due(rival);
	f.make_due(founder);
	economy::industrial_dynamics::process(*f.state);
	size_t count = 0;
	f.state->world.for_each_capital_project([&](dcon::capital_project_id other) {
		if(f.state->world.capital_project_get_factory_type(other) == works) ++count;
	});
	REQUIRE(count == 1);
}

TEST_CASE("a person holding savings gains a profile and banks them", "[economy][capital-allocation]") {
	capital_allocation_tests::fixture f;
	persons::person_key saver{ 900, 4 };
	persons::person_key spender{ 900, 5 };
	REQUIRE(economy::exact_person_economy::set_balance(*f.state,
		economy::exact_person_economy::open_account(*f.state, saver, f.settlement), 100.0f));
	REQUIRE(economy::exact_person_economy::set_balance(*f.state,
		economy::exact_person_economy::open_account(*f.state, spender, f.settlement), 20.0f));
	auto money = base_money(*f.state);
	economy::liquidity::process(*f.state);
	auto profile = persons::exact_population::profile_for_person(*f.state, saver);
	REQUIRE(profile);
	REQUIRE_FALSE(persons::exact_population::profile_for_person(*f.state, spender));
	auto actor = persons::actor_for_person(*f.state, profile);
	auto deposit = economy::banking::deposit_account_for(*f.state, actor, f.settlement);
	REQUIRE(deposit);
	REQUIRE(economy::banking::deposit_balance(*f.state, deposit) == Approx(70.0f));
	REQUIRE(economy::capital_market::investable_funds(*f.state, actor, f.settlement) == Approx(35.0f));
	REQUIRE(base_money(*f.state) == Approx(money));
}
