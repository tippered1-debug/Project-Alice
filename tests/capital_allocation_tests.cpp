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
#include "economy/physical/concrete_market.hpp"
#include "economy/physical/deposits.hpp"
#include "economy/relations/relations.hpp"
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

	// Buyers bid for `per_day` of the commodity on this market every day of the demand window.
	void demand(dcon::commodity_id commodity, float per_day) {
		auto bid = state->world.create_concrete_market_bid();
		state->world.concrete_market_bid_set_original_quantity(bid, per_day * float(economy::industrial_dynamics::demand_window_days));
		state->world.concrete_market_bid_set_created_on(bid, state->current_date);
		state->world.concrete_market_bid_set_status(bid, uint8_t(economy::physical::concrete_market::order_status::filled));
		state->world.force_create_concrete_bid_market(bid, market);
		state->world.force_create_concrete_bid_commodity(bid, commodity);
	}

	// A plant recipe that needs only labor and `construction` of material per unit.
	dcon::factory_type_id recipe(float price, float construction, dcon::commodity_id* output = nullptr) {
		auto product = state->world.create_commodity();
		state->world.commodity_set_cost(product, price);
		state->world.market_resize_price(state->world.commodity_size());
		auto plant = state->world.create_factory_type();
		state->world.factory_type_set_output(plant, product);
		state->world.factory_type_set_output_amount(plant, 1.0f);
		state->world.factory_type_set_base_workforce(plant, 1.0f);
		set_construction(plant, construction);
		if(output) *output = product;
		return plant;
	}

	std::vector<dcon::capital_project_id> projects_of(dcon::factory_type_id plant) {
		std::vector<dcon::capital_project_id> result;
		state->world.for_each_capital_project([&](dcon::capital_project_id project) {
			if(state->world.capital_project_get_factory_type(project) == plant
				&& state->world.capital_project_get_status(project) < uint8_t(economy::capital_projects::status::completed))
				result.push_back(project);
		});
		return result;
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
	REQUIRE(economy::industrial_dynamics::asking_price_for(*f.state, f.factory) == Approx(30.0f).epsilon(0.001));
	f.state->current_date = f.state->current_date + 60;
	// The plant wore a little during the first review.
	auto price = economy::industrial_dynamics::asking_price_for(*f.state, f.factory);
	REQUIRE(price == Approx(20.0f).epsilon(0.001));

	auto money = base_money(*f.state);
	f.quiet_everyone();
	f.make_due(buyer);
	economy::industrial_dynamics::process(*f.state);
	REQUIRE(actors::organizations::operator_actor_for_factory(*f.state, f.factory) == buyer);
	REQUIRE(capital_allocation_tests::stake_of(*f.state, plant, buyer) == Approx(1.0f));
	REQUIRE(economy::accounts::balance(*f.state, buyer_wallet) == Approx(1000.0f - price).epsilon(0.0001));
	// The plant's owners share the price: 60% to the landlord, 40% stays with the operator.
	REQUIRE(economy::accounts::balance(*f.state, economy::accounts::find_account(*f.state, landlord, f.settlement)) == Approx(0.6f * price).epsilon(0.001));
	REQUIRE(economy::accounts::balance(*f.state, f.payer) == Approx(10000.0f + 0.4f * price).epsilon(0.0001));
	REQUIRE(base_money(*f.state) == Approx(money));
}

TEST_CASE("a saver founds a company that the public funds", "[economy][capital-allocation]") {
	capital_allocation_tests::fixture f;
	dcon::commodity_id product{};
	auto works = f.recipe(20.0f, 20.0f, &product); // capital cost 50, budget 75
	f.demand(product, 10.0f);
	auto founder = f.person(0, 40.0f);
	auto public_savings = f.cohort(economy::households::role::urban, 2000.0f);

	f.quiet_everyone();
	f.make_due(founder);
	auto money = base_money(*f.state);
	economy::industrial_dynamics::process(*f.state);

	auto projects = f.projects_of(works);
	REQUIRE(projects.size() == 1);
	auto project = projects.front();
	REQUIRE(f.state->world.capital_project_get_status(project) == uint8_t(economy::capital_projects::status::funded));
	auto company = f.state->world.capital_project_get_economic_actor_from_capital_project_sponsor(project);
	auto organization = actors::organizations::organization_for_actor(*f.state, company);
	auto equity = actors::organizations::equity_asset_for_organization(*f.state, organization);
	// The founder holds a share and outside savers hold the rest.
	auto founder_share = capital_allocation_tests::stake_of(*f.state, equity, founder);
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
}

TEST_CASE("firms keep entering a market until competition removes the excess return", "[economy][capital-allocation]") {
	using economy::industrial_dynamics::expected_entry;
	// An entrant sells what demand leaves and expects a lower price as capacity outgrows it.
	REQUIRE(expected_entry(0.0f, 0.0f, 1.0f).sell_through == Approx(0.0f));
	REQUIRE(expected_entry(4.0f, 0.0f, 1.0f).sell_through == Approx(0.95f));
	REQUIRE(expected_entry(4.0f, 0.0f, 1.0f).price_factor == Approx(1.25f));
	REQUIRE(expected_entry(1.0f, 1.0f, 1.0f).sell_through == Approx(0.5f));
	REQUIRE(expected_entry(1.0f, 1.0f, 1.0f).price_factor == Approx(std::pow(0.5f, 1.0f / economy::industrial_dynamics::demand_elasticity)));

	capital_allocation_tests::fixture f;
	dcon::commodity_id product{};
	// Output worth 1, capital cost 1500 per plant: the first two entrants earn
	// more than they require; the third would not.
	auto works = f.recipe(1.0f, 600.0f, &product);
	f.demand(product, 1.2f);
	std::vector<dcon::economic_actor_id> firms;
	for(int i = 0; i < 4; ++i) {
		auto firm = f.company(10000.0f);
		(void)f.save(firm, 0.0f);
		firms.push_back(firm);
	}
	f.quiet_everyone();
	for(auto firm : firms) f.make_due(firm);
	economy::industrial_dynamics::process(*f.state);
	REQUIRE(f.projects_of(works).size() == 2);
	// Two plants of the same recipe in one province, run by different firms.
	auto projects = f.projects_of(works);
	REQUIRE(f.state->world.capital_project_get_economic_actor_from_capital_project_sponsor(projects[0])
		!= f.state->world.capital_project_get_economic_actor_from_capital_project_sponsor(projects[1]));

	// A month later the plants are still being built and count as supply.
	f.state->current_date = f.state->current_date + 30;
	f.demand(product, 1.2f);
	f.quiet_everyone();
	for(auto firm : firms) f.make_due(firm);
	economy::industrial_dynamics::process(*f.state);
	REQUIRE(f.projects_of(works).size() == 2);

	// Demand grows: entry resumes.
	f.state->current_date = f.state->current_date + 30;
	f.demand(product, 3.0f);
	f.quiet_everyone();
	for(auto firm : firms) f.make_due(firm);
	economy::industrial_dynamics::process(*f.state);
	REQUIRE(f.projects_of(works).size() > 2);
}

TEST_CASE("a bank lends to a new entrepreneur against the project itself", "[economy][capital-allocation]") {
	capital_allocation_tests::fixture f;
	// No outside savers: the founder and the bank finance the plant.
	REQUIRE(economy::accounts::bootstrap_set_balance(*f.state, f.payer, 0.0f));
	f.state->world.organization_set_bank_risk_appetite(f.bank, 0.0f);
	dcon::commodity_id product{};
	auto works = f.recipe(20.0f, 20.0f, &product);
	f.demand(product, 10.0f);
	// Commits 75 of a capital need near 96: well over 30% equity, and a loan
	// within half the plant's value of 50.
	auto founder = f.person(0, 150.0f);
	f.quiet_everyone();
	f.make_due(founder);
	auto money = base_money(*f.state);
	economy::industrial_dynamics::process(*f.state);
	auto projects = f.projects_of(works);
	REQUIRE(projects.size() == 1);
	auto project = projects.front();
	auto company = f.state->world.capital_project_get_economic_actor_from_capital_project_sponsor(project);
	dcon::obligation_id loan{};
	f.state->world.capital_project_for_each_firm_capital_request_project_as_capital_project(project, [&](auto relation) {
		auto request = f.state->world.firm_capital_request_project_get_firm_capital_request(relation);
		loan = f.state->world.firm_capital_request_get_obligation_from_firm_capital_request_obligation(request);
	});
	REQUIRE(loan);
	REQUIRE(f.state->world.obligation_get_economic_actor_from_obligation_debtor(loan) == company);
	REQUIRE(f.state->world.obligation_get_principal_outstanding(loan) > 0.0f);
	REQUIRE(f.state->world.obligation_get_principal_outstanding(loan) <= 25.0f + 1.0e-3f);
	REQUIRE(f.state->world.obligation_get_due_date(loan) == f.state->current_date + economy::banking::project_loan_term_days);
	REQUIRE_FALSE(f.state->world.obligation_get_factory_from_obligation_factory(loan));
	// The founder owns the whole company: the bank holds debt, not shares.
	auto organization = actors::organizations::organization_for_actor(*f.state, company);
	REQUIRE(capital_allocation_tests::stake_of(*f.state, actors::organizations::equity_asset_for_organization(*f.state, organization), founder) == Approx(1.0f));
	REQUIRE(base_money(*f.state) == Approx(money));

	// Once built, the loan follows the plant.
	auto yard = f.state->world.capital_project_get_site_from_capital_project_site(project);
	dcon::capital_project_requirement_id requirement{};
	f.state->world.capital_project_for_each_capital_project_requirement_project_as_capital_project(project, [&](auto relation) {
		requirement = f.state->world.capital_project_requirement_project_get_capital_project_requirement(relation);
	});
	auto needed = f.state->world.capital_project_requirement_get_required_quantity(requirement);
	REQUIRE(economy::physical::inventory::add(*f.state, yard, f.material, needed, company) == Approx(needed));
	(void)economy::capital_projects::consume(*f.state, requirement, needed);
	auto plant = f.state->world.capital_project_get_factory_from_capital_project_factory(project);
	REQUIRE(plant);
	REQUIRE(f.state->world.obligation_get_factory_from_obligation_factory(loan) == plant);
}

TEST_CASE("a bank refuses a project its sponsor has too little stake in", "[economy][capital-allocation]") {
	capital_allocation_tests::fixture f;
	REQUIRE(economy::accounts::bootstrap_set_balance(*f.state, f.payer, 0.0f));
	f.state->world.organization_set_bank_risk_appetite(f.bank, 0.0f);
	dcon::commodity_id product{};
	auto works = f.recipe(20.0f, 20.0f, &product);
	f.demand(product, 10.0f);
	// Commits 30: a loan for the rest would exceed half the plant's value.
	auto founder = f.person(0, 60.0f);
	f.quiet_everyone();
	f.make_due(founder);
	auto money = base_money(*f.state);
	economy::industrial_dynamics::process(*f.state);
	REQUIRE(f.projects_of(works).empty());
	REQUIRE(base_money(*f.state) == Approx(money));
	REQUIRE(economy::banking::deposit_balance(*f.state, economy::banking::deposit_account_for(*f.state, founder, f.settlement)) == Approx(60.0f));
}

TEST_CASE("the operator of a known deposit builds a mine when the ore pays", "[economy][capital-allocation]") {
	capital_allocation_tests::fixture f;
	auto ore = f.state->world.create_commodity();
	f.state->world.commodity_set_cost(ore, 10.0f);
	f.state->world.market_resize_price(f.state->world.commodity_size());
	auto mine = f.state->world.create_factory_type();
	f.state->world.factory_type_set_output(mine, ore);
	f.state->world.factory_type_set_output_amount(mine, 2.0f);
	f.state->world.factory_type_set_base_workforce(mine, 1.0f);
	f.state->world.factory_type_set_extracts_deposit(mine, true);
	f.set_construction(mine, 10.0f);
	auto deposit = economy::physical::deposits::create_deposit(*f.state, f.site, ore, 100000.0f, 100000.0f, 1.0f, 4.0f, 4.0f, 0);
	REQUIRE(deposit);
	dcon::monetary_account_id wallet{};
	auto holder = f.company(1000.0f, &wallet);
	(void)f.save(holder, 0.0f);
	auto holder_org = actors::organizations::organization_for_actor(*f.state, holder);
	REQUIRE(actors::organizations::bind_deposit_operator(*f.state, holder_org, deposit));
	auto subsoil = f.state->world.create_asset();
	f.state->world.force_create_resource_deposit_asset(deposit, subsoil);
	REQUIRE(actors::ownership::create_stake(*f.state, holder, subsoil, 1.0f, 1.0f, 1.0f));
	auto outsider = f.company(100000.0f);
	(void)f.save(outsider, 0.0f);

	// Without buyers for the ore nobody builds.
	f.quiet_everyone();
	f.make_due(holder);
	f.make_due(outsider);
	economy::industrial_dynamics::process(*f.state);
	REQUIRE(f.projects_of(mine).empty());

	f.state->current_date = f.state->current_date + 30;
	f.demand(ore, 20.0f);
	f.quiet_everyone();
	f.make_due(holder);
	f.make_due(outsider);
	economy::industrial_dynamics::process(*f.state);
	auto projects = f.projects_of(mine);
	REQUIRE(projects.size() == 1);
	REQUIRE(f.state->world.capital_project_get_project_kind(projects.front()) == uint8_t(economy::capital_projects::project_kind::extraction_plant));
	REQUIRE(f.state->world.capital_project_get_resource_deposit_from_capital_project_target_deposit(projects.front()) == deposit);
	// Only the deposit's operator may build on it.
	REQUIRE(f.state->world.capital_project_get_economic_actor_from_capital_project_sponsor(projects.front()) == holder);
}

TEST_CASE("an urban cohort takes up a craft the market lacks", "[economy][capital-allocation]") {
	capital_allocation_tests::fixture f;
	auto cloth = f.state->world.create_commodity();
	f.state->world.commodity_set_cost(cloth, 5.0f);
	f.state->world.market_resize_price(f.state->world.commodity_size());
	auto weaving = f.state->world.create_factory_type();
	f.state->world.factory_type_set_output(weaving, cloth);
	f.state->world.factory_type_set_output_amount(weaving, 1.0f);
	f.state->world.factory_type_set_base_workforce(weaving, 1);
	f.state->world.factory_type_set_household_craft(weaving, true);
	auto cohort_actor = f.cohort(economy::households::role::urban, 500.0f);
	auto cohort = actors::organizations::organization_for_actor(*f.state, cohort_actor);
	f.state->world.organization_set_household_members(cohort, 40.0f);
	f.state->world.organization_set_household_workers(cohort, 10.0f);
	auto weaving_shops = [&]() {
		int count = 0;
		for(auto factory : actors::organizations::factories_operated_by(*f.state, cohort))
			if(f.state->world.factory_get_building_type(factory) == weaving) ++count;
		return count;
	};

	// Nobody wants cloth: no workshop.
	f.quiet_everyone();
	f.make_due(cohort_actor);
	economy::industrial_dynamics::process(*f.state);
	REQUIRE(weaving_shops() == 0);

	f.state->current_date = f.state->current_date + 30;
	f.demand(cloth, 20.0f);
	f.quiet_everyone();
	f.make_due(cohort_actor);
	economy::industrial_dynamics::process(*f.state);
	REQUIRE(weaving_shops() == 1);

	// The craft is practised now; the cohort does not open it twice.
	f.state->current_date = f.state->current_date + 30;
	f.demand(cloth, 20.0f);
	f.quiet_everyone();
	f.make_due(cohort_actor);
	economy::industrial_dynamics::process(*f.state);
	REQUIRE(weaving_shops() == 1);
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

TEST_CASE("built plants wear out and their operator books the cost", "[economy][capital-allocation]") {
	capital_allocation_tests::fixture f;
	auto operator_org = actors::organizations::operator_organization_for_factory(*f.state, f.factory);
	f.state->world.organization_set_retained_earnings(operator_org, 100.0f);
	f.quiet_everyone();
	economy::industrial_dynamics::process(*f.state);
	auto daily = 1.0f - std::pow(1.0f - economy::industrial_dynamics::annual_depreciation_rate, 1.0f / 365.0f);
	REQUIRE(f.state->world.factory_get_productive_capacity(f.factory) == Approx(1.0f - daily));
	// Replacement value 50 at full capacity.
	REQUIRE(f.state->world.organization_get_retained_earnings(operator_org) == Approx(100.0f - 50.0f * daily));
	// A year of wear takes the annual rate.
	for(int i = 1; i < 365; ++i) {
		f.state->current_date = f.state->current_date + 1;
		f.quiet_everyone();
		economy::industrial_dynamics::process(*f.state);
	}
	REQUIRE(f.state->world.factory_get_productive_capacity(f.factory) == Approx(1.0f - economy::industrial_dynamics::annual_depreciation_rate).epsilon(0.001));
}

TEST_CASE("a holder sells shares to an investor who values them more", "[economy][capital-allocation]") {
	capital_allocation_tests::fixture f;
	REQUIRE(economy::accounts::bootstrap_set_balance(*f.state, f.payer, 0.0f));
	// A bank that lends dear: its depositors require a higher return from equity.
	auto dear = economy::banking::create_bank(*f.state);
	economy::banking::bank_policy policy{};
	policy.jurisdiction = f.nation;
	policy.settlement = f.settlement;
	policy.lending_base_rate = 0.08f;
	policy.minimum_capital_ratio = 0.08f;
	policy.liquidity_target = 0.05f;
	policy.risk_appetite = 1.0f;
	policy.max_single_borrower_exposure = 1.0f;
	policy.reserve_requirement = 0.05f;
	REQUIRE(economy::banking::configure_bank_policy(*f.state, dear, policy));
	actors::ownership::assign_runtime_canonical_id(*f.state, dear);
	REQUIRE(economy::banking::bootstrap_set_reserve_balance(*f.state,
		economy::banking::open_reserve_account(*f.state, dear, f.settlement), 5000.0f));
	economy::banking::update_bank_statuses(*f.state, f.state->current_date);

	auto issuer = actors::organizations::operator_organization_for_factory(*f.state, f.factory);
	auto equity = actors::organizations::equity_asset_for_organization(*f.state, issuer);
	auto seller = actors::organizations::actor_for_organization(*f.state,
		economy::households::create(*f.state, f.province, economy::households::role::urban, f.settlement));
	actors::ownership::assign_runtime_canonical_id(*f.state, seller);
	auto seller_deposit = economy::banking::open_deposit_account(*f.state, dear, seller, f.settlement);
	REQUIRE(economy::banking::bootstrap_set_deposit_balance(*f.state, seller_deposit, 100.0f));
	auto stake = actors::ownership::create_stake(*f.state, seller, equity, 1.0f, 1.0f, 1.0f);
	REQUIRE(stake);
	auto buyer = f.company(200000.0f);
	(void)f.save(buyer, 0.0f);
	REQUIRE(economy::capital_market::required_return(*f.state, seller, f.settlement) == Approx(0.13f));
	REQUIRE(economy::capital_market::required_return(*f.state, buyer, f.settlement) == Approx(0.08f));

	// Review the stake today.
	auto offset = (economy::capital_market::share_review_days
		- (f.state->current_date.to_raw_value() + int32_t(stake.index())) % economy::capital_market::share_review_days)
		% economy::capital_market::share_review_days;
	f.state->current_date = f.state->current_date + offset;
	auto earnings = economy::capital_market::expected_annual_earnings(*f.state, issuer);
	REQUIRE(earnings > 0.0f);
	auto price = 0.5f * (earnings / 0.08f + earnings / 0.13f);
	auto money = base_money(*f.state);
	auto pool = economy::capital_market::build_pool(*f.state);
	REQUIRE(economy::capital_market::trade_shares(*f.state, pool) == Approx(price));
	REQUIRE(capital_allocation_tests::stake_of(*f.state, equity, buyer) == Approx(1.0f));
	REQUIRE(capital_allocation_tests::stake_of(*f.state, equity, seller) == Approx(0.0f));
	REQUIRE(economy::wallets::spendable(*f.state, economy::wallets::account_for(*f.state, seller, f.settlement)) == Approx(price));
	REQUIRE(base_money(*f.state) == Approx(money));
	REQUIRE(capital_allocation_tests::total_stakes(*f.state, equity) == Approx(1.0f));

	// The new holder requires no more than any other investor: it keeps the shares.
	f.state->current_date = f.state->current_date + economy::capital_market::share_review_days;
	auto again = economy::capital_market::build_pool(*f.state);
	REQUIRE(economy::capital_market::trade_shares(*f.state, again) == Approx(0.0f));
}
