#include "catch.hpp"

#include "actors/organizations/organizations.hpp"
#include "actors/ownership.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/economy_stats.hpp"
#include "economy/firm_agency.hpp"
#include "economy/households.hpp"
#include "economy/physical/exchange.hpp"
#include "economy/physical/inventory.hpp"
#include "economy/physical/land.hpp"
#include "economy/relations/relations.hpp"
#include "economy/wallets.hpp"
#include "governance/defense.hpp"
#include "governance/finance/finance.hpp"
#include "governance/governance.hpp"
#include "governance/policy.hpp"
#include "governance/public_administration.hpp"
#include "military/land_forces.hpp"
#include "persons/exact_population.hpp"
#include "persons/persons.hpp"

namespace public_finance_tests {

namespace gv = governance;
using gv::policy::topic_id;

constexpr uint64_t line_template = 701;
constexpr uint64_t line_formation = 801;
constexpr uint64_t food_store = 901;

struct fixture : capital_regulation_tests::fixture {
	gv::constitution::founded constitution;
	dcon::monetary_account_id tax_treasury{};

	fixture() {
		state->world.province_set_nation_from_province_control(province, nation);
		constitution = constitute();
		auto authority = gv::public_administration::tax_authority_for(*state, nation);
		REQUIRE(authority);
		tax_treasury = gv::finance::open_treasury_account(*state, authority, settlement);
		REQUIRE(tax_treasury);
	}

	void enact(std::initializer_list<std::pair<topic_id, float>> topics) {
		capital_regulation_tests::enact_topics(*this, constitution, topics);
	}

	float revenue() { return economy::accounts::balance(*state, tax_treasury); }

	void move_to_first_of_month() {
		for(int32_t i = 0; i < 32 && state->current_date.to_ymd(state->start_date).day != 1; ++i)
			state->current_date = state->current_date + 1;
		REQUIRE(state->current_date.to_ymd(state->start_date).day == 1);
	}

	dcon::economic_actor_id funded_company(float cash) {
		auto company = actors::organizations::create_company(*state);
		auto actor = actors::organizations::actor_for_organization(*state, company);
		auto account = economy::accounts::open_account(*state, actor, settlement);
		REQUIRE(economy::accounts::bootstrap_set_balance(*state, account, cash));
		return actor;
	}

	dcon::economic_actor_id funded_cohort(float cash) {
		auto household = economy::households::create(*state, province, economy::households::role::urban, settlement);
		REQUIRE(household);
		auto actor = actors::organizations::actor_for_organization(*state, household);
		auto wallet = economy::wallets::open_for(*state, actor, settlement);
		REQUIRE(wallet);
		REQUIRE(economy::accounts::bootstrap_set_balance(*state, wallet.dcon_account, cash));
		return actor;
	}

	float cash(dcon::economic_actor_id actor) {
		return economy::wallets::spendable(*state, economy::wallets::account_for(*state, actor, settlement));
	}

	dcon::institution_id command() {
		auto result = gv::defense::military_command_for(*state, nation);
		REQUIRE(result);
		return result;
	}

	dcon::monetary_account_id fund_command(float amount) {
		auto treasury = gv::finance::open_treasury_account(*state, command(), settlement);
		REQUIRE(treasury);
		REQUIRE(economy::accounts::bootstrap_set_balance(*state, treasury, amount));
		return treasury;
	}

	// One formation of four soldiers from the residents' cell, with daily food.
	dcon::commodity_id raise_army() {
		auto food = state->world.create_commodity();
		state->world.commodity_set_cost(food, 5.0f);
		state->world.market_resize_price(state->world.commodity_size());
		state->world.market_set_price(market, food, 5.0f);
		REQUIRE(military::land_forces::add_template(*state, { line_template, 10 }));
		military::land_forces::template_consumable_requirement ration{};
		ration.template_id = line_template;
		ration.kind = military::land_forces::consumable_kind::food;
		ration.commodity = food;
		ration.per_person = 1.0;
		REQUIRE(military::land_forces::set_template_consumable_requirement(*state, ration));
		military::land_forces::formation unit{};
		unit.id = line_formation;
		unit.template_id = line_template;
		unit.owner = nation;
		unit.location = site;
		REQUIRE(military::land_forces::create_formation(*state, unit));
		auto cell = uint32_t(residents.index()) + 1u;
		std::vector<persons::person_key> soldiers;
		for(uint64_t ordinal = 0; ordinal < 4; ++ordinal) soldiers.push_back({ cell, ordinal });
		REQUIRE(military::land_forces::assign_personnel(*state, line_formation, soldiers, 0));
		REQUIRE(military::land_forces::personnel_count(*state, line_formation) == 4);
		state->world.province_set_labor_price(province, economy::labor::no_education, 2.0f);
		return food;
	}
};

} // namespace public_finance_tests

TEST_CASE("profit tax is charged on realized profit and booked against retained earnings", "[governance][public-finance]") {
	public_finance_tests::fixture f;
	f.enact({ { governance::policy::topic_id::corporate_profit_tax, 0.2f } });
	auto firm = actors::organizations::create_company(*f.state);
	auto actor = actors::organizations::actor_for_organization(*f.state, firm);
	auto account = economy::accounts::open_account(*f.state, actor, f.settlement);
	REQUIRE(economy::accounts::bootstrap_set_balance(*f.state, account, 1000.0f));
	f.state->world.organization_set_retained_earnings(firm, 500.0f);
	auto before = f.revenue();

	CHECK(economy::firm_agency::assess_profit_tax(*f.state, firm, f.nation, 100.0f) == Approx(20.0f));
	CHECK(economy::accounts::balance(*f.state, account) == Approx(980.0f));
	CHECK(f.revenue() == Approx(before + 20.0f));
	CHECK(f.state->world.organization_get_retained_earnings(firm) == Approx(480.0f));
	// Losses owe nothing, and a household cohort is not a firm.
	CHECK(economy::firm_agency::assess_profit_tax(*f.state, firm, f.nation, -50.0f) == 0.0f);
	auto cohort = economy::households::create(*f.state, f.province, economy::households::role::urban, f.settlement);
	CHECK(economy::firm_agency::assess_profit_tax(*f.state, cohort, f.nation, 100.0f) == 0.0f);
}

TEST_CASE("sales tax falls on final consumers only", "[governance][public-finance]") {
	public_finance_tests::fixture f;
	f.enact({ { governance::policy::topic_id::sales_tax, 0.1f } });
	auto before = f.revenue();

	auto cohort = f.funded_cohort(500.0f);
	economy::physical::exchange::assess_sales_tax(*f.state, cohort, f.site, 100.0f,
		economy::wallets::account_for(*f.state, cohort, f.settlement), {}, f.state->current_date);
	CHECK(f.cash(cohort) == Approx(490.0f));
	CHECK(f.revenue() == Approx(before + 10.0f));

	auto firm = f.funded_company(500.0f);
	economy::physical::exchange::assess_sales_tax(*f.state, firm, f.site, 100.0f,
		economy::wallets::account_for(*f.state, firm, f.settlement), {}, f.state->current_date);
	CHECK(f.cash(firm) == Approx(500.0f));
	CHECK(f.revenue() == Approx(before + 10.0f));
}

TEST_CASE("a person without an economic profile pays sales tax through the household sector", "[governance][public-finance]") {
	public_finance_tests::fixture f;
	auto sector = governance::create_institution(*f.state, f.nation, governance::institution_kind::household_sector);
	REQUIRE(governance::set_parent(*f.state, sector, governance::central_government_for(*f.state, f.nation)));
	f.enact({ { governance::policy::topic_id::sales_tax, 0.1f } });
	auto before = f.revenue();
	auto buyer = persons::person_key{ 950, 5 };
	auto wallet = economy::exact_person_economy::open_account(*f.state, buyer, f.settlement);
	REQUIRE(wallet);
	REQUIRE(economy::exact_person_economy::set_balance(*f.state, wallet, 50.0f));
	REQUIRE_FALSE(persons::exact_population::profile_for_person(*f.state, buyer));

	economy::physical::exchange::assess_person_sales_tax(*f.state, buyer, wallet, f.site, 100.0f,
		f.state->current_date);
	CHECK(economy::exact_person_economy::balance(*f.state, wallet) == Approx(40.0f));
	CHECK(f.revenue() == Approx(before + 10.0f));
}

TEST_CASE("land tax is a monthly twelfth of the annual rate on the title's appraised value", "[governance][public-finance]") {
	public_finance_tests::fixture f;
	f.enact({ { governance::policy::topic_id::land_tax, 0.05f } });
	auto owner = f.funded_company(100.0f);
	auto title = economy::physical::land::create_title(*f.state, f.site, 50.0f);
	REQUIRE(title);
	auto asset = f.state->world.create_asset();
	f.state->world.force_create_land_title_asset(title, asset);
	f.state->world.asset_set_appraised_value(asset, 1200.0f);
	REQUIRE(actors::ownership::create_stake(*f.state, owner, asset, 1.0f, 1.0f, 1.0f));
	f.move_to_first_of_month();
	auto before = f.revenue();

	economy::physical::land::assess_land_tax(*f.state);
	CHECK(f.cash(owner) == Approx(95.0f));
	CHECK(f.revenue() == Approx(before + 5.0f));
	// The tax is monthly: the next day owes nothing.
	f.state->current_date = f.state->current_date + 1;
	economy::physical::land::assess_land_tax(*f.state);
	CHECK(f.cash(owner) == Approx(95.0f));
}

TEST_CASE("customs duties follow the tariff law", "[governance][public-finance]") {
	public_finance_tests::fixture f;
	auto foreign = f.state->world.create_nation();
	auto foreign_province = f.state->world.create_province();
	f.state->world.province_set_nation_from_province_ownership(foreign_province, foreign);
	f.state->world.province_set_nation_from_province_control(foreign_province, foreign);
	auto foreign_site = f.state->world.create_site();
	f.state->world.force_create_site_location(foreign_site, foreign_province);
	auto seller = f.funded_company(0.0f);
	auto importer = f.funded_company(1000.0f);
	auto importer_wallet = economy::wallets::account_for(*f.state, importer, f.settlement);
	auto before = f.revenue();

	economy::physical::exchange::assess_customs_duties(*f.state, importer, seller, foreign_site, f.site,
		f.output, 100.0f, importer_wallet, {}, {}, {}, f.state->current_date);
	CHECK(f.cash(importer) == Approx(1000.0f));
	CHECK(economy::effective_tariff_import_rate(*f.state, f.nation) == 0.0f);

	f.enact({ { governance::policy::topic_id::import_tariff, 0.1f } });
	economy::physical::exchange::assess_customs_duties(*f.state, importer, seller, foreign_site, f.site,
		f.output, 100.0f, importer_wallet, {}, {}, {}, f.state->current_date);
	CHECK(f.cash(importer) == Approx(990.0f));
	CHECK(economy::effective_tariff_import_rate(*f.state, f.nation) == Approx(0.1f));
	CHECK(f.revenue() == Approx(before + 10.0f));
}

TEST_CASE("the defense appropriation funds the military command", "[governance][public-finance][defense]") {
	public_finance_tests::fixture f;
	governance::policy::position programme;
	REQUIRE(governance::policy::set(programme, governance::policy::topic_id::defense_appropriation, 0.4f));
	auto rules = governance::policy::rules_for(*f.state, f.nation, programme);
	bool funded = false;
	for(auto const& [institution, share] : rules.shares)
		if(institution == f.command()) funded = share == Approx(0.4f);
	CHECK(funded);
	CHECK(governance::acts_administratively(*f.state, f.command(), governance::authority_kind::spend_public_funds,
		governance::national(f.nation), f.state->current_date));
}

TEST_CASE("soldiers' pay goes to their families, arrears accrue, and unpaid soldiers desert", "[governance][public-finance][defense]") {
	public_finance_tests::fixture f;
	(void)f.raise_army();
	auto family = f.funded_cohort(0.0f);
	auto treasury = f.fund_command(1000.0f);
	auto pay = governance::defense::monthly_pay(*f.state, f.nation, f.province);
	REQUIRE(pay == Approx(60.0f));
	CHECK(governance::defense::monthly_payroll(*f.state, f.nation) == Approx(240.0f));

	auto paid = governance::defense::pay_armed_forces(*f.state, f.nation);
	CHECK(paid.soldiers == 4);
	CHECK(paid.paid == Approx(240.0f));
	CHECK(paid.deserted == 0);
	CHECK(f.cash(family) == Approx(240.0f));
	CHECK(economy::accounts::balance(*f.state, treasury) == Approx(760.0f));

	// Half the payroll: the rest is owed to the family.
	REQUIRE(economy::accounts::bootstrap_set_balance(*f.state, treasury, 120.0f));
	auto short_month = governance::defense::pay_armed_forces(*f.state, f.nation);
	CHECK(short_month.paid == Approx(120.0f).margin(0.01f));
	CHECK(governance::defense::pay_arrears(*f.state, f.nation) == Approx(120.0f).margin(0.01f));

	// Arrears are repaid first once there is money again.
	REQUIRE(economy::accounts::bootstrap_set_balance(*f.state, treasury, 1000.0f));
	auto recovery = governance::defense::pay_armed_forces(*f.state, f.nation);
	CHECK(recovery.arrears_paid == Approx(120.0f).margin(0.01f));
	CHECK(recovery.paid == Approx(240.0f));
	CHECK(governance::defense::pay_arrears(*f.state, f.nation) == Approx(0.0f).margin(0.01f));

	// With an empty treasury, soldiers leave once three months are owed.
	REQUIRE(economy::accounts::bootstrap_set_balance(*f.state, treasury, 0.0f));
	uint64_t deserted = 0;
	int months = 0;
	while(deserted == 0 && months < 6) {
		deserted = governance::defense::pay_armed_forces(*f.state, f.nation).deserted;
		++months;
	}
	CHECK(months == 4);
	CHECK(deserted == 2);
	CHECK(military::land_forces::personnel_count(*f.state, public_finance_tests::line_formation) == 2);
	CHECK(governance::defense::pay_arrears(*f.state, f.nation) == Approx(960.0f).margin(0.05f));
}

TEST_CASE("the military command buys the army's supplies and hands them to its stores", "[governance][public-finance][defense]") {
	public_finance_tests::fixture f;
	auto food = f.raise_army();
	military::land_forces::stockpile store{};
	store.id = public_finance_tests::food_store;
	store.owner = f.nation;
	store.location = f.site;
	store.kind = military::land_forces::stockpile_kind::warehouse;
	store.cargo = military::land_forces::cargo_kind::consumable;
	store.consumable = military::land_forces::consumable_kind::food;
	REQUIRE(military::land_forces::create_stockpile(*f.state, store));
	(void)f.fund_command(100000.0f);
	auto command_actor = governance::actor_for_institution(*f.state, f.command());
	auto demand = military::land_forces::daily_consumable_demand(*f.state, public_finance_tests::line_formation,
		military::land_forces::consumable_kind::food);
	REQUIRE(demand > 0.0);

	governance::defense::post_procurement_bids(*f.state);
	float ordered = 0.0f;
	f.state->world.for_each_concrete_market_bid([&](dcon::concrete_market_bid_id bid) {
		if(f.state->world.concrete_market_bid_get_economic_actor_from_concrete_bid_buyer(bid) == command_actor
			&& f.state->world.concrete_market_bid_get_commodity_from_concrete_bid_commodity(bid) == food)
			ordered += f.state->world.concrete_market_bid_get_remaining_quantity(bid);
	});
	CHECK(ordered == Approx(float(governance::defense::supply_target_days * demand)));

	REQUIRE(economy::physical::inventory::add(*f.state, f.site, food, 10.0f, command_actor) == Approx(10.0f));
	governance::defense::take_delivery(*f.state);
	CHECK(economy::physical::inventory::quantity(*f.state, f.site, food, command_actor) == 0.0f);
	auto stores = military::land_forces::stockpiles_of(*f.state, f.nation);
	REQUIRE(stores.size() == 1);
	CHECK(military::land_forces::stockpile_stock(*f.state, stores.front()) == Approx(10.0));
}
