#include "catch.hpp"

#include "canonical_consumer_fixture.hpp"
#include "actors/organizations/organizations.hpp"
#include "actors/ownership.hpp"
#include "economy/banking/banking.hpp"
#include "economy/consent/consent.hpp"
#include "economy/dividends.hpp"
#include "economy/physical/deposits.hpp"
#include "economy/physical/exchange.hpp"
#include "economy/physical/extraction.hpp"
#include "economy/physical/inventory.hpp"
#include "economy/relations/relations.hpp"
#include "governance/constitution.hpp"
#include "governance/finance/finance.hpp"
#include "governance/governance.hpp"
#include "governance/law/law.hpp"
#include "governance/legislature.hpp"
#include "governance/offices.hpp"
#include "governance/policy.hpp"
#include "governance/public_administration.hpp"
#include "persons/exact_population.hpp"
#include "persons/persons.hpp"

namespace capital_regulation_tests {

namespace gv = governance;
using gv::constitution::founded;

struct fixture : capital_allocation_tests::fixture {
	dcon::pop_id residents{};

	fixture() {
		auto region_definition = state->world.create_state_definition();
		state->world.force_create_abstract_state_membership(province, region_definition);
		state->world.nation_set_capital(nation, province);
		persons::exact_population::cell_descriptor citizens;
		citizens.source_population_cell = 950;
		citizens.literal_count = 120;
		citizens.bootstrap_base_day = state->current_date.to_raw_value() - 1;
		citizens.demographic_seed = 950;
		citizens.home_site = site;
		REQUIRE(persons::exact_population::register_synthetic_population_cell(*state, citizens).result
			== persons::exact_population::status::created);
		residents = state->world.create_pop();
		state->world.force_create_pop_location(residents, province);
		state->world.pop_set_size(residents, 400.0f);
		state->world.pop_set_poptype(residents, state->world.create_pop_type());
		state->world.pop_set_culture(residents, state->world.create_culture());
		state->world.pop_set_religion(residents, state->world.create_religion());
		REQUIRE(persons::register_population_cell(*state, residents, site));
	}

	dcon::person_id citizen(uint64_t ordinal) {
		auto profile = persons::materialize_profile(*state, persons::person_key{ 950, ordinal });
		REQUIRE(profile);
		return profile;
	}

	dcon::person_id resident(uint64_t ordinal) {
		auto key = persons::person_key{ uint32_t(residents.index()) + 1u, ordinal };
		auto profile = persons::materialize_profile(*state, key);
		REQUIRE(profile);
		return profile;
	}

	founded constitute() {
		gv::constitution::document document;
		std::string error;
		REQUIRE(gv::constitution::model("parliamentary_republic", "unitary", document, error));
		founded result;
		REQUIRE(gv::constitution::found(*state, nation, document, state->current_date, result, error));
		return result;
	}

	dcon::person_id seat(founded const& c, char const* office, uint64_t ordinal) {
		auto person = citizen(ordinal);
		REQUIRE(gv::offices::install(*state, person, c.office(office), state->current_date));
		return person;
	}
};

dcon::legal_instrument_id enact_topics(fixture& f, founded const& c,
	std::initializer_list<std::pair<gv::policy::topic_id, float>> topics) {
	auto date = f.state->current_date;
	auto instrument = gv::law::create_draft_instrument(*f.state,
		gv::law::legal_instrument_kind::statute, f.nation);
	REQUIRE(instrument);
	for(auto const& [topic, value] : topics)
		REQUIRE(gv::law::add_topic_rule(*f.state, instrument, topic, value));
	auto lower = c.institution("lower_chamber");
	auto upper = c.institution("upper_chamber");
	std::vector<dcon::person_id> lower_members;
	uint64_t next_person = 10;
	for(auto chamber : { lower, upper }) {
		for(auto seat : gv::legislature::seats_of(*f.state, chamber)) {
			auto member = f.citizen(next_person++);
			REQUIRE(gv::offices::install(*f.state, member, seat, date));
			if(chamber == lower) lower_members.push_back(member);
			REQUIRE(gv::legislature::vote_on_instrument(*f.state, member, chamber, instrument,
				gv::legislature::motion_kind::enact, true, date));
		}
	}
	auto head = f.seat(c, "head_of_state", 1);
	REQUIRE(gv::law::authorized_assent(*f.state, head, instrument, date));
	REQUIRE_FALSE(lower_members.empty());
	REQUIRE(gv::law::authorized_enact(*f.state, lower_members.front(), instrument, date, date));
	return instrument;
}

} // namespace capital_regulation_tests

TEST_CASE("dividend tax creates a real liability and withholds cash from exact shareholders", "[governance][capital-income]") {
	capital_regulation_tests::fixture f;
	auto c = f.constitute();
	auto date = f.state->current_date;
	capital_regulation_tests::enact_topics(f, c, {
		{ governance::policy::topic_id::dividend_tax, 0.25f }
	});
	auto tax_authority = governance::public_administration::tax_authority_for(*f.state, f.nation);
	REQUIRE(tax_authority);
	auto treasury = governance::finance::open_treasury_account(*f.state, tax_authority, f.settlement);
	REQUIRE(treasury);
	auto treasury_before = economy::accounts::balance(*f.state, treasury);

	auto firm = actors::organizations::create_company(*f.state);
	auto firm_actor = actors::organizations::actor_for_organization(*f.state, firm);
	auto firm_cash = economy::accounts::open_account(*f.state, firm_actor, f.settlement);
	REQUIRE(economy::accounts::bootstrap_set_balance(*f.state, firm_cash, 1000.0f));
	f.state->world.organization_set_retained_earnings(firm, 1000.0f);
	auto owner_key = persons::person_key{ 950, 1 };
	auto owner_profile = persons::materialize_profile(*f.state, owner_key);
	REQUIRE(owner_profile);
	auto owner = persons::actor_for_person(*f.state, owner_profile);
	REQUIRE(actors::ownership::create_stake(*f.state, owner,
		actors::organizations::equity_asset_for_organization(*f.state, firm), 1.0f, 1.0f, 1.0f));
	REQUIRE(economy::exact_person_economy::open_account(*f.state, owner_key, f.settlement));

	REQUIRE(economy::dividends::pay(*f.state, firm) == Approx(1000.0f));
	CHECK(economy::exact_person_economy::balance(*f.state,
	economy::exact_person_economy::find_account(*f.state, owner_key, f.settlement)) == Approx(750.0f));
	CHECK(economy::accounts::balance(*f.state, treasury) == Approx(treasury_before + 250.0f));
	auto tax_due = 0.0f;
	f.state->world.economic_actor_for_each_obligation_debtor_as_economic_actor(owner,
		[&](dcon::obligation_debtor_id relation) {
			auto obligation = f.state->world.obligation_debtor_get_obligation(relation);
			if(f.state->world.obligation_get_kind(obligation) == uint8_t(economy::relations::obligation_kind::tax))
				tax_due += economy::relations::total_due(*f.state, obligation);
		});
	CHECK(tax_due == Approx(0.0f));
}

TEST_CASE("resource royalty is charged on a mine output sale and not a resale", "[governance][resource-royalty]") {
	capital_regulation_tests::fixture f;
	auto c = f.constitute();
	capital_regulation_tests::enact_topics(f, c, {
		{ governance::policy::topic_id::resource_royalty, 0.20f }
	});
	auto tax_authority = governance::public_administration::tax_authority_for(*f.state, f.nation);
	REQUIRE(tax_authority);
	auto treasury = governance::finance::open_treasury_account(*f.state, tax_authority, f.settlement);
	REQUIRE(treasury);
	auto treasury_before = economy::accounts::balance(*f.state, treasury);

	auto ore = f.state->world.create_commodity();
	auto mine_type = f.state->world.create_factory_type();
	f.state->world.factory_type_set_output(mine_type, ore);
	f.state->world.factory_type_set_output_amount(mine_type, 1.0f);
	f.state->world.factory_type_set_base_workforce(mine_type, 1);
	f.state->world.factory_type_set_extracts_deposit(mine_type, true);
	auto deposit = economy::physical::deposits::create_deposit(*f.state, f.site, ore,
		100.0f, 100.0f, 1.0f, 10.0f, 10.0f, 0);
	REQUIRE(deposit);
	auto miner = actors::organizations::create_company(*f.state);
	auto miner_actor = actors::organizations::actor_for_organization(*f.state, miner);
	REQUIRE(economy::physical::extraction::create_enterprise(*f.state, deposit, mine_type, miner));
	auto miner_cash = economy::accounts::open_account(*f.state, miner_actor, f.settlement);
	REQUIRE(economy::accounts::bootstrap_set_balance(*f.state, miner_cash, 0.0f));
	auto hub = f.state->world.create_site();
	f.state->world.force_create_site_location(hub, f.province);
	f.state->world.force_create_market_hub_site(f.market, hub);
	REQUIRE(economy::physical::inventory::add(*f.state, hub, ore, 1.0f, miner_actor) == Approx(1.0f));
	REQUIRE(economy::physical::exchange::purchase(*f.state, hub, ore, miner_actor, f.employer,
		1.0f, 100.0f, f.settlement, f.state->current_date));
	CHECK(economy::accounts::balance(*f.state, miner_cash) == Approx(80.0f));
	CHECK(economy::accounts::balance(*f.state, treasury) == Approx(treasury_before + 20.0f));

	auto reseller = actors::organizations::create_company(*f.state);
	auto reseller_actor = actors::organizations::actor_for_organization(*f.state, reseller);
	auto reseller_cash = economy::accounts::open_account(*f.state, reseller_actor, f.settlement);
	REQUIRE(economy::accounts::bootstrap_set_balance(*f.state, reseller_cash, 0.0f));
	REQUIRE(economy::physical::inventory::add(*f.state, hub, ore, 1.0f, reseller_actor) == Approx(1.0f));
	REQUIRE(economy::physical::exchange::purchase(*f.state, hub, ore, reseller_actor, f.employer,
		1.0f, 100.0f, f.settlement, f.state->current_date));
	CHECK(economy::accounts::balance(*f.state, reseller_cash) == Approx(100.0f));
	CHECK(economy::accounts::balance(*f.state, treasury) == Approx(treasury_before + 20.0f));
}

TEST_CASE("the enacted reserve requirement overrides a bank's opening policy and reduces credit", "[governance][banking]") {
	capital_regulation_tests::fixture f;
	auto c = f.constitute();
	auto depositor = f.company(0.0f);
	(void)f.save(depositor, 10000.0f);
	auto before = economy::banking::bank_balance_sheet(*f.state, f.bank, f.settlement);
	CHECK(economy::banking::reserve_requirement_for(*f.state, f.bank, f.state->current_date) == Approx(0.05f));
	CHECK(before.required_liquidity == Approx(500.0f));
	auto bank_actor = actors::organizations::actor_for_organization(*f.state, f.bank);
	auto officer = persons::create_person(*f.state, f.state->current_date);
	REQUIRE(economy::consent::create_mandate(*f.state, f.bank, officer,
		economy::consent::decision_kind::lend, f.state->current_date));
	auto lend_to_person = [&](uint64_t ordinal) {
		auto borrower_person = f.resident(ordinal);
		auto borrower = persons::actor_for_person(*f.state, borrower_person);
		actors::ownership::assign_runtime_canonical_id(*f.state, borrower);
		auto account = economy::banking::open_deposit_account(*f.state, f.bank, borrower, f.settlement);
		REQUIRE(account);
		auto due = f.state->current_date + 365;
		auto proposal = economy::consent::create_proposal(*f.state,
			economy::consent::proposal_kind::loan, bank_actor, borrower, f.settlement,
			10000.0f, due, 0.10f, f.state->current_date);
		REQUIRE(proposal);
		REQUIRE(economy::consent::accept_proposal(*f.state, proposal, bank_actor, officer, f.state->current_date));
		REQUIRE(economy::consent::accept_proposal(*f.state, proposal, borrower, borrower_person, f.state->current_date));
		return economy::banking::originate_loan_with_consent(*f.state, f.bank, account,
			10000.0f, f.state->current_date, due, 0.10f, proposal);
	};
	for(uint64_t ordinal = 1; ordinal <= 3; ++ordinal) REQUIRE(lend_to_person(ordinal));
	auto quote_before = economy::banking::quote_project_credit(*f.state, depositor, f.settlement,
		100000.0f, 100000.0f, 100000.0f, 100000.0f);
	REQUIRE(quote_before.amount == Approx(10000.0f));

	capital_regulation_tests::enact_topics(f, c, {
		{ governance::policy::topic_id::bank_reserve_requirement, 0.50f }
	});
	CHECK(economy::banking::reserve_requirement_for(*f.state, f.bank, f.state->current_date) == Approx(0.50f));
	economy::banking::update_bank_statuses(*f.state, f.state->current_date);
	auto after = economy::banking::bank_balance_sheet(*f.state, f.bank, f.settlement);
	CHECK(after.required_liquidity == Approx(20000.0f));
	CHECK(economy::banking::status_of(*f.state, f.bank) == economy::banking::bank_status::solvent);
	auto quote_after = economy::banking::quote_project_credit(*f.state, depositor, f.settlement,
		100000.0f, 100000.0f, 100000.0f, 100000.0f);
	CHECK(quote_after.amount == Approx(0.0f));
	CHECK_FALSE(lend_to_person(4));
}
