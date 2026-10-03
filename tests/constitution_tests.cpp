#include "catch.hpp"

#include "canonical_consumer_fixture.hpp"
#include "economy/banking/banking.hpp"
#include "economy/capital_projects.hpp"
#include "economy/monetary_policy.hpp"
#include "governance/constitution.hpp"
#include "governance/finance/finance.hpp"
#include "governance/governance.hpp"
#include "governance/law/law.hpp"
#include "governance/legislature.hpp"
#include "governance/offices.hpp"
#include "governance/policy_inputs.hpp"
#include "persons/exact_population.hpp"
#include "persons/persons.hpp"

namespace constitution_tests {

using namespace governance;
using constitution::founded;

struct fixture : capital_allocation_tests::fixture {
	dcon::state_definition_id region_definition{};
	dcon::pop_id residents{};

	fixture() {
		region_definition = state->world.create_state_definition();
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
		// Residents of a real population row: their profiles survive save and load.
		residents = state->world.create_pop();
		state->world.force_create_pop_location(residents, province);
		state->world.pop_set_size(residents, 400.0f);
		state->world.pop_set_poptype(residents, state->world.create_pop_type());
		state->world.pop_set_culture(residents, state->world.create_culture());
		state->world.pop_set_religion(residents, state->world.create_religion());
		REQUIRE(persons::register_population_cell(*state, residents, site));
	}

	dcon::person_id resident(uint64_t ordinal) {
		auto person = persons::materialize_profile(*state, persons::person_key{ uint32_t(residents.index()) + 1u, ordinal });
		REQUIRE(person);
		return person;
	}

	dcon::person_id citizen(uint64_t ordinal) {
		auto person = persons::materialize_profile(*state, persons::person_key{ 950, ordinal });
		REQUIRE(person);
		return person;
	}

	founded constitute(std::string_view executive, std::string_view territorial = "unitary") {
		constitution::document doc;
		std::string error;
		auto modelled = constitution::model(executive, territorial, doc, error);
		INFO(error);
		REQUIRE(modelled);
		founded result;
		auto built = constitution::found(*state, nation, doc, state->current_date, result, error);
		INFO(error);
		REQUIRE(built);
		return result;
	}

	dcon::person_id seat(founded const& c, std::string const& office, uint64_t ordinal) {
		auto person = citizen(ordinal);
		REQUIRE(offices::install(*state, person, c.office(office), state->current_date));
		return person;
	}

	dcon::monetary_account_id treasury(dcon::institution_id institution, float cash) {
		auto account = finance::open_treasury_account(*state, institution, settlement);
		REQUIRE(account);
		REQUIRE(economy::accounts::bootstrap_set_balance(*state, account, cash));
		return account;
	}
};

} // namespace constitution_tests

using constitution_tests::fixture;
namespace gv = governance;

TEST_CASE("a dated grant is found on its first day", "[governance][constitution]") {
	fixture f;
	auto date = f.state->current_date;
	auto ministry = gv::create_institution(*f.state, f.nation, gv::institution_kind::finance_ministry);
	gv::grant_terms terms{};
	terms.valid_from = date;
	auto grant = gv::grant_authority_to_institution(*f.state, ministry, gv::authority_kind::levy_tax, gv::national(f.nation), terms);
	REQUIRE(grant);
	CHECK(f.state->world.authority_grant_get_kind(grant) == uint8_t(gv::authority_kind::levy_tax));
	CHECK(gv::jurisdiction_of(*f.state, grant) == gv::national(f.nation));
	CHECK(gv::covers(*f.state, gv::jurisdiction_of(*f.state, grant), gv::national(f.nation)));
	CHECK(gv::grant_valid(*f.state, grant, date));
	CHECK(f.state->world.authority_grant_get_valid_from(grant) == date);
	CHECK_FALSE(bool(f.state->world.authority_grant_get_valid_until(grant)));
	REQUIRE(gv::authority_grant_for(*f.state, ministry, gv::authority_kind::levy_tax, gv::national(f.nation), date) == grant);
}

TEST_CASE("head of state, head of government and ministers are distinct offices in distinct institutions", "[governance][constitution]") {
	fixture f;
	auto c = f.constitute("parliamentary_republic");
	auto head = c.office("head_of_state"), premier = c.office("chief_executive"), minister = c.office("finance_minister");
	REQUIRE(head);
	REQUIRE(premier);
	REQUIRE(minister);
	REQUIRE(gv::kind_of(*f.state, gv::institution_for_office(*f.state, head)) == gv::institution_kind::head_of_state);
	REQUIRE(gv::kind_of(*f.state, gv::institution_for_office(*f.state, premier)) == gv::institution_kind::cabinet);
	REQUIRE(gv::kind_of(*f.state, gv::institution_for_office(*f.state, minister)) == gv::institution_kind::finance_ministry);
	REQUIRE(gv::parent_of(*f.state, c.institution("finance")) == c.institution("cabinet"));
	REQUIRE(gv::parent_of(*f.state, c.institution("tax")) == c.institution("finance"));
	REQUIRE(gv::parent_of(*f.state, c.institution("cabinet")) == gv::central_government_for(*f.state, f.nation));
	REQUIRE(gv::offices::rules_of(*f.state, minister).appointer == premier);
	REQUIRE(gv::offices::rules_of(*f.state, premier).appointer == head);
	REQUIRE(gv::offices::rules_of(*f.state, premier).confirmer == c.institution("lower_chamber"));
	REQUIRE(gv::law::constitution_of(*f.state, f.nation) == c.constitution);

	// The same ontology describes a presidential system: one office leads the
	// state and the government, and there is no prime minister.
	fixture p;
	auto presidential = p.constitute("presidential");
	REQUIRE(p.state->world.office_get_kind(presidential.office("chief_executive")) == uint8_t(gv::office_kind::head_of_state));
	REQUIRE_FALSE(presidential.office("head_of_state"));
	REQUIRE(gv::offices::rules_of(*p.state, presidential.office("finance_minister")).appointer == presidential.office("chief_executive"));
	REQUIRE(gv::offices::rules_of(*p.state, presidential.office("chief_executive")).successor == presidential.office("vice_president"));
}

TEST_CASE("only the constitutional appointer with authority appoints a minister", "[governance][constitution]") {
	fixture f;
	auto c = f.constitute("parliamentary_republic");
	auto date = f.state->current_date;
	auto premier = f.seat(c, "chief_executive", 1);
	auto head = f.seat(c, "head_of_state", 2);
	auto stranger = f.citizen(3);
	auto candidate = f.citizen(4);
	REQUIRE_FALSE(gv::offices::appoint(*f.state, stranger, candidate, c.office("finance_minister"), date));
	// The head of state can appoint, but the constitution gives ministers to the premier.
	REQUIRE_FALSE(gv::offices::appoint(*f.state, head, candidate, c.office("finance_minister"), date));
	REQUIRE(gv::offices::appoint(*f.state, premier, candidate, c.office("finance_minister"), date));
	REQUIRE(gv::offices::holder(*f.state, c.office("finance_minister")) == candidate);
	// An occupied office is not appointed over.
	REQUIRE_FALSE(gv::offices::appoint(*f.state, premier, f.citizen(5), c.office("finance_minister"), date));
}

TEST_CASE("dismissal ends the tenure and the successor acts", "[governance][constitution]") {
	fixture f;
	auto c = f.constitute("parliamentary_republic");
	auto date = f.state->current_date;
	auto premier = f.seat(c, "chief_executive", 1);
	auto minister = f.citizen(2);
	REQUIRE(gv::offices::appoint(*f.state, premier, minister, c.office("finance_minister"), date));
	auto tenure = gv::offices::current_tenure(*f.state, c.office("finance_minister"));
	auto later = date + 10;
	REQUIRE(gv::offices::dismiss(*f.state, premier, c.office("finance_minister"), later));
	REQUIRE_FALSE(f.state->world.office_tenure_get_active(tenure));
	REQUIRE(f.state->world.office_tenure_get_ended_on(tenure) == later);
	REQUIRE(gv::offices::vacant(*f.state, c.office("finance_minister")));
	// The premier acts as finance minister until a new one is appointed.
	REQUIRE(gv::offices::holder(*f.state, c.office("finance_minister")) == premier);
	REQUIRE(gv::offices::acting(*f.state, gv::offices::current_tenure(*f.state, c.office("finance_minister"))));
	// The dismissed minister no longer exercises the office's powers.
	REQUIRE_FALSE(gv::offices::exercising(*f.state, minister, gv::authority_kind::regulate, gv::national(f.nation), later));
	auto replacement = f.citizen(3);
	REQUIRE(gv::offices::appoint(*f.state, premier, replacement, c.office("finance_minister"), later));
	REQUIRE(gv::offices::holder(*f.state, c.office("finance_minister")) == replacement);
	REQUIRE_FALSE(gv::offices::acting(*f.state, gv::offices::current_tenure(*f.state, c.office("finance_minister"))));
}

TEST_CASE("an officeholder's death opens the vacancy the succession rule fills", "[governance][constitution]") {
	fixture monarchy;
	auto c = monarchy.constitute("parliamentary_monarchy");
	auto king = monarchy.seat(c, "head_of_state", 1);
	auto heir = monarchy.seat(c, "heir", 2);
	auto day = monarchy.state->current_date + 5;
	REQUIRE(persons::mark_dead(*monarchy.state, king, day));
	// Full succession: the heir becomes the monarch and the heir's office empties.
	REQUIRE(gv::offices::holder(*monarchy.state, c.office("head_of_state")) == heir);
	REQUIRE_FALSE(gv::offices::acting(*monarchy.state, gv::offices::current_tenure(*monarchy.state, c.office("head_of_state"))));
	REQUIRE(gv::offices::vacant(*monarchy.state, c.office("heir")));
	REQUIRE_FALSE(gv::offices::exercising(*monarchy.state, king, gv::authority_kind::assent, gv::national(monarchy.nation), day));

	fixture republic;
	auto r = republic.constitute("parliamentary_republic");
	auto president = republic.seat(r, "head_of_state", 1);
	auto speaker = republic.seat(r, "speaker", 2);
	REQUIRE(persons::mark_dead(*republic.state, president, republic.state->current_date + 5));
	// Acting succession: the speaker acts as head of state.
	REQUIRE(gv::offices::holder(*republic.state, r.office("head_of_state")) == speaker);
	REQUIRE(gv::offices::acting(*republic.state, gv::offices::current_tenure(*republic.state, r.office("head_of_state"))));
}

TEST_CASE("terms end tenures and exclusive offices are not combined", "[governance][constitution]") {
	fixture f;
	auto c = f.constitute("parliamentary_republic");
	auto office = c.office("prosecutor_general");
	auto rules = gv::offices::rules_of(*f.state, office);
	rules.term_days = 30;
	REQUIRE(gv::offices::set_rules(*f.state, office, rules));
	auto prosecutor = f.seat(c, "prosecutor_general", 1);
	gv::offices::expire_terms(*f.state, f.state->current_date + 29);
	REQUIRE(gv::offices::holder(*f.state, office) == prosecutor);
	gv::offices::expire_terms(*f.state, f.state->current_date + 30);
	REQUIRE(gv::offices::vacant(*f.state, office));
	REQUIRE_FALSE(gv::offices::holder(*f.state, office));
	// The bank governor's office is exclusive.
	auto governor = f.seat(c, "bank_governor", 2);
	REQUIRE_FALSE(gv::offices::install(*f.state, governor, c.office("interior_minister"), f.state->current_date));
}

TEST_CASE("revoked authority forbids the action, and delegated powers fall with it", "[governance][constitution]") {
	fixture f;
	auto c = f.constitute("parliamentary_republic");
	auto date = f.state->current_date;
	auto premier = f.seat(c, "chief_executive", 1);
	auto grant = gv::authority_grant_for(*f.state, c.office("chief_executive"), gv::authority_kind::appoint, gv::national(f.nation), date);
	REQUIRE(grant);
	REQUIRE(f.state->world.authority_grant_get_legal_instrument_from_authority_grant_source(grant) == c.constitution);
	REQUIRE(gv::revoke_authority(*f.state, grant, date));
	REQUIRE_FALSE(gv::offices::appoint(*f.state, premier, f.citizen(2), c.office("finance_minister"), date));

	// The tax authority levies taxes under the finance ministry's power.
	auto tax = c.institution("tax");
	REQUIRE(gv::has_authority(*f.state, tax, gv::authority_kind::levy_tax, gv::national(f.nation), date));
	auto finance_power = gv::authority_grant_for(*f.state, c.institution("finance"), gv::authority_kind::levy_tax, gv::national(f.nation), date);
	REQUIRE(gv::revoke_authority(*f.state, finance_power, date + 1));
	REQUIRE(gv::has_authority(*f.state, tax, gv::authority_kind::levy_tax, gv::national(f.nation), date));
	REQUIRE_FALSE(gv::has_authority(*f.state, tax, gv::authority_kind::levy_tax, gv::national(f.nation), date + 1));
	// A parent institution's powers are not inherited without a delegation.
	REQUIRE_FALSE(gv::has_authority(*f.state, c.institution("tax"), gv::authority_kind::appropriate, gv::national(f.nation), date));
}

TEST_CASE("ministries spend real money only under their spending authority", "[governance][constitution]") {
	fixture f;
	auto c = f.constitute("parliamentary_republic");
	auto date = f.state->current_date;
	auto education = f.treasury(c.institution("education"), 1000.0f);
	auto works = f.treasury(c.institution("works"), 0.0f);
	auto courts = f.treasury(c.institution("judiciary"), 1000.0f);
	auto money = capital_allocation_tests::base_money(*f.state);
	// The judiciary holds no power to spend public funds.
	REQUIRE_FALSE(gv::finance::authorized_spend_by_institution(*f.state, c.institution("judiciary"), courts, works, 100.0f, date));
	REQUIRE(gv::finance::authorized_spend_by_institution(*f.state, c.institution("education"), education, works, 100.0f, date));
	REQUIRE(economy::accounts::balance(*f.state, education) == Approx(900.0f));
	REQUIRE(economy::accounts::balance(*f.state, works) == Approx(100.0f));
	REQUIRE(capital_allocation_tests::base_money(*f.state) == Approx(money));
	auto power = gv::authority_grant_for(*f.state, c.institution("education"), gv::authority_kind::spend_public_funds, gv::national(f.nation), date);
	REQUIRE(gv::revoke_authority(*f.state, power, date));
	REQUIRE_FALSE(gv::finance::authorized_spend_by_institution(*f.state, c.institution("education"), education, works, 100.0f, date));
}

TEST_CASE("the finance minister sets fiscal law and the ministry allocates only what it appropriates", "[governance][constitution]") {
	fixture f;
	auto c = f.constitute("parliamentary_republic");
	auto date = f.state->current_date;
	auto national = f.treasury(c.institution("finance"), 1000.0f);
	auto education = f.treasury(c.institution("education"), 0.0f);
	auto works = f.treasury(c.institution("works"), 0.0f);
	auto minister = f.seat(c, "finance_minister", 1);
	auto interior_minister = f.seat(c, "interior_minister", 2);
	gv::policy_inputs::fiscal_inputs inputs{};
	inputs.tax_rates[0] = 0.1f;
	inputs.tax_rates[1] = 0.2f;
	inputs.tax_rates[2] = 0.3f;
	inputs.shares.push_back({ c.institution("education"), 0.5f });
	// Another minister holds no power to regulate public finance.
	REQUIRE_FALSE(gv::policy_inputs::enact(*f.state, f.nation, interior_minister, inputs, date));
	auto regulation = gv::policy_inputs::enact(*f.state, f.nation, minister, inputs, date);
	REQUIRE(regulation);
	REQUIRE(f.state->world.legal_instrument_get_office_from_legal_instrument_authorizing_office(regulation) == c.office("finance_minister"));
	REQUIRE(f.state->world.legal_instrument_get_institution_from_legal_instrument_issuing_institution(regulation) == c.institution("finance"));
	REQUIRE(gv::law::effective_amount(*f.state, gv::national(f.nation), gv::law::policy_rule_kind::income_tax_rate, date, 2) == Approx(0.3f));
	auto money = capital_allocation_tests::base_money(*f.state);
	REQUIRE(gv::finance::authorized_allocate(*f.state, c.institution("finance"), c.institution("education"), f.settlement, 200.0f, date));
	REQUIRE_FALSE(gv::finance::authorized_allocate(*f.state, c.institution("finance"), c.institution("works"), f.settlement, 200.0f, date));
	REQUIRE(economy::accounts::balance(*f.state, national) == Approx(800.0f));
	REQUIRE(economy::accounts::balance(*f.state, education) == Approx(200.0f));
	REQUIRE(economy::accounts::balance(*f.state, works) == Approx(0.0f));
	REQUIRE(capital_allocation_tests::base_money(*f.state) == Approx(money));
}

TEST_CASE("public construction is commissioned and paid by the public works ministry", "[governance][constitution]") {
	fixture f;
	f.state->world.province_resize_building_level(economy::max_building_types);
	f.state->world.force_create_infrastructure_node_location(f.state->world.create_infrastructure_node(), f.province);
	auto c = f.constitute("parliamentary_republic");
	auto works = c.institution("works");
	auto treasury = f.treasury(works, 100.0f);
	auto state_treasury = f.treasury(gv::central_government_for(*f.state, f.nation), 500.0f);
	f.state->world.nation_set_construction_spending(f.nation, 0);
	f.state->economy_definitions.building_definitions[0].cost.commodity_type[0] = f.material;
	f.state->economy_definitions.building_definitions[0].cost.commodity_amounts[0] = 5;
	auto order = f.state->world.force_create_province_building_construction(f.province, f.nation);
	f.state->world.province_building_construction_set_type(order, 0);
	economy::capital_projects::adopt_legacy_requests(*f.state);
	auto project = economy::capital_projects::project_for(*f.state, order);
	REQUIRE(project);
	REQUIRE(f.state->world.capital_project_get_economic_actor_from_capital_project_sponsor(project) == gv::actor_for_institution(*f.state, works));
	auto account = f.state->world.capital_project_get_monetary_account_from_capital_project_account(project);
	// The ministry funds the materials (5 at 10, plus 20%) from its own
	// appropriation, whatever the legacy construction slider says.
	REQUIRE(economy::accounts::balance(*f.state, account) == Approx(60.0f));
	REQUIRE(economy::accounts::balance(*f.state, treasury) == Approx(40.0f));
	REQUIRE(economy::accounts::balance(*f.state, state_treasury) == Approx(500.0f));
}

TEST_CASE("national and territorial powers do not reach each other's jurisdiction", "[governance][constitution]") {
	fixture f;
	auto c = f.constitute("parliamentary_republic");
	auto date = f.state->current_date;
	auto region = c.institution("region");
	auto city = c.institution("capital");
	REQUIRE(region);
	REQUIRE(city);
	auto region_territory = gv::territory_of(*f.state, region);
	auto city_territory = gv::territory_of(*f.state, city);
	REQUIRE(region_territory);
	REQUIRE(gv::parent_of(*f.state, city_territory) == region_territory);
	REQUIRE(gv::territory_of(*f.state, f.province) == city_territory);
	REQUIRE(gv::has_authority(*f.state, region, gv::authority_kind::spend_public_funds, gv::local(region_territory), date));
	REQUIRE_FALSE(gv::has_authority(*f.state, region, gv::authority_kind::spend_public_funds, gv::national(f.nation), date));
	REQUIRE_FALSE(gv::has_authority(*f.state, c.institution("finance"), gv::authority_kind::spend_public_funds, gv::local(region_territory), date));
	// A region's power covers the city inside it; the city's does not cover the region.
	REQUIRE(gv::has_authority(*f.state, region, gv::authority_kind::administer, gv::local(city_territory), date));
	REQUIRE_FALSE(gv::has_authority(*f.state, city, gv::authority_kind::administer, gv::local(region_territory), date));
	auto regional_treasury = f.treasury(region, 100.0f);
	auto city_treasury = f.treasury(city, 0.0f);
	REQUIRE(gv::finance::authorized_spend_by_institution(*f.state, region, regional_treasury, city_treasury, 40.0f, date));
	REQUIRE(economy::accounts::balance(*f.state, city_treasury) == Approx(40.0f));
}

TEST_CASE("a federal region legislates for its own territory only", "[governance][constitution]") {
	fixture f;
	auto c = f.constitute("parliamentary_republic", "federal");
	auto date = f.state->current_date;
	auto governor = f.seat(c, "governor", 1);
	auto finance_minister = f.seat(c, "finance_minister", 2);
	auto territory = gv::territory_of(*f.state, c.institution("region"));
	auto regional = gv::law::create_draft_instrument(*f.state, gv::law::legal_instrument_kind::regulation, {}, territory);
	REQUIRE(gv::law::add_rule(*f.state, regional, { gv::law::policy_rule_kind::income_tax_rate, {}, 0.05f, 0 }));
	// The national finance minister's power does not reach a regional rule.
	REQUIRE_FALSE(gv::law::authorized_enact(*f.state, finance_minister, regional, date, date));
	REQUIRE(gv::law::authorized_enact(*f.state, governor, regional, date, date));
	REQUIRE(gv::law::jurisdiction_of(*f.state, regional) == gv::local(territory));
	// In a unitary state the governor administers but does not regulate.
	fixture u;
	auto unitary = u.constitute("parliamentary_republic", "unitary");
	auto appointed = u.seat(unitary, "governor", 1);
	auto draft = gv::law::create_draft_instrument(*u.state, gv::law::legal_instrument_kind::regulation, {}, gv::territory_of(*u.state, unitary.institution("region")));
	REQUIRE(gv::law::add_rule(*u.state, draft, { gv::law::policy_rule_kind::income_tax_rate, {}, 0.05f, 0 }));
	REQUIRE_FALSE(gv::law::authorized_enact(*u.state, appointed, draft, u.state->current_date, u.state->current_date));
}

TEST_CASE("the legislature is an institution of chambers with concrete members who enact statutes", "[governance][constitution]") {
	fixture f;
	auto c = f.constitute("parliamentary_republic");
	auto date = f.state->current_date;
	auto chambers = gv::legislature::chambers_of(*f.state, c.institution("legislature"));
	REQUIRE(chambers.size() == 2);
	auto lower = c.institution("lower_chamber"), upper = c.institution("upper_chamber");
	REQUIRE(gv::legislature::seats_of(*f.state, lower).size() == 7);
	REQUIRE(gv::legislature::seats_of(*f.state, upper).size() == 5);
	std::vector<dcon::person_id> lower_members, upper_members;
	uint64_t next = 10;
	for(auto seat : gv::legislature::seats_of(*f.state, lower)) {
		lower_members.push_back(f.citizen(next++));
		REQUIRE(gv::offices::install(*f.state, lower_members.back(), seat, date));
	}
	for(auto seat : gv::legislature::seats_of(*f.state, upper)) {
		upper_members.push_back(f.citizen(next++));
		REQUIRE(gv::offices::install(*f.state, upper_members.back(), seat, date));
	}
	REQUIRE(gv::legislature::filled_seats(*f.state, lower, date) == 7);
	auto head = f.seat(c, "head_of_state", 1);
	auto premier = f.seat(c, "chief_executive", 2);

	auto statute = gv::law::create_draft_instrument(*f.state, gv::law::legal_instrument_kind::statute, f.nation);
	REQUIRE(gv::law::add_public_debt_ceiling_rule(*f.state, statute, f.settlement, 1000.0f));
	// The premier holds no legislative power: statutes belong to the chambers.
	REQUIRE_FALSE(gv::law::authorized_enact(*f.state, premier, statute, date, date));
	// A seat holder cannot enact what the chambers have not passed.
	REQUIRE_FALSE(gv::law::authorized_enact(*f.state, lower_members[0], statute, date, date));
	// Non-members cannot vote.
	REQUIRE_FALSE(gv::legislature::vote_on_instrument(*f.state, premier, lower, statute, gv::legislature::motion_kind::enact, true, date));
	for(int i = 0; i < 4; ++i) REQUIRE(gv::legislature::vote_on_instrument(*f.state, lower_members[i], lower, statute, gv::legislature::motion_kind::enact, true, date));
	for(int i = 0; i < 3; ++i) REQUIRE(gv::legislature::vote_on_instrument(*f.state, upper_members[i], upper, statute, gv::legislature::motion_kind::enact, true, date));
	// Passed, but the head of state has not assented.
	REQUIRE_FALSE(gv::law::authorized_enact(*f.state, lower_members[0], statute, date, date));
	REQUIRE_FALSE(gv::law::authorized_assent(*f.state, premier, statute, date));
	REQUIRE(gv::law::authorized_assent(*f.state, head, statute, date));
	// Only a member promulgates.
	REQUIRE_FALSE(gv::law::authorized_enact(*f.state, premier, statute, date, date));
	auto effective = date + 7;
	REQUIRE(gv::law::authorized_enact(*f.state, lower_members[0], statute, date, effective));
	REQUIRE(f.state->world.legal_instrument_get_status(statute) == uint8_t(gv::law::legal_status::enacted));
	REQUIRE(f.state->world.legal_instrument_get_institution_from_legal_instrument_issuing_institution(statute) == c.institution("legislature"));
	REQUIRE(f.state->world.legal_instrument_get_office_from_legal_instrument_authorizing_office(statute) == gv::legislature::seat_of(*f.state, lower_members[0], lower, date));
	REQUIRE(f.state->world.legal_instrument_get_person_from_legal_instrument_enactor(statute) == lower_members[0]);
	REQUIRE(gv::law::jurisdiction_of(*f.state, statute) == gv::national(f.nation));
	REQUIRE(f.state->world.legal_instrument_get_enacted_on(statute) == date);
	REQUIRE(f.state->world.legal_instrument_get_effective_from(statute) == effective);
	REQUIRE_FALSE(gv::law::public_debt_policy_for(*f.state, f.nation, f.settlement, date).ceiling);
	REQUIRE(*gv::law::public_debt_policy_for(*f.state, f.nation, f.settlement, effective).ceiling == Approx(1000.0f));
}

TEST_CASE("the central bank governor is appointed as the constitution says and cannot be dismissed", "[governance][constitution]") {
	fixture f;
	auto c = f.constitute("parliamentary_republic");
	auto date = f.state->current_date;
	auto head = f.seat(c, "head_of_state", 1);
	auto premier = f.seat(c, "chief_executive", 2);
	auto lower = c.institution("lower_chamber");
	std::vector<dcon::person_id> members;
	uint64_t next = 10;
	for(auto seat : gv::legislature::seats_of(*f.state, lower)) {
		members.push_back(f.citizen(next++));
		REQUIRE(gv::offices::install(*f.state, members.back(), seat, date));
	}
	auto candidate = f.citizen(3);
	auto office = c.office("bank_governor");
	REQUIRE_FALSE(gv::offices::appoint(*f.state, premier, candidate, office, date));
	// The head of state appoints, but only a confirmed candidate.
	REQUIRE_FALSE(gv::offices::appoint(*f.state, head, candidate, office, date));
	for(int i = 0; i < 4; ++i) REQUIRE(gv::legislature::vote_on_confirmation(*f.state, members[i], lower, office, candidate, true, date));
	REQUIRE(gv::offices::appoint(*f.state, head, candidate, office, date));
	REQUIRE(gv::offices::holder(*f.state, office) == candidate);
	REQUIRE_FALSE(gv::offices::dismiss(*f.state, head, office, date + 1));
	REQUIRE(gv::offices::holder(*f.state, office) == candidate);
}

TEST_CASE("an independent central bank is not part of the finance ministry and acts only under its own power", "[governance][constitution]") {
	fixture f;
	auto c = f.constitute("parliamentary_republic");
	auto date = f.state->current_date;
	auto bank = c.institution("central_bank");
	REQUIRE(f.state->world.institution_get_independent(bank) == 1);
	REQUIRE(gv::parent_of(*f.state, bank) == gv::central_government_for(*f.state, f.nation));
	REQUIRE_FALSE(gv::has_authority(*f.state, c.office("finance_minister"), gv::authority_kind::issue_currency, gv::national(f.nation), date));
	REQUIRE(gv::has_authority(*f.state, bank, gv::authority_kind::issue_currency, gv::national(f.nation), date));
	auto organization = economy::monetary_policy::open_central_bank(*f.state, f.nation, f.settlement);
	REQUIRE(organization);
	REQUIRE(economy::monetary_policy::public_institution_of(*f.state, organization) == bank);
	auto rate = economy::monetary_policy::policy_rate(*f.state, organization);
	REQUIRE(gv::revoke_authority(*f.state, gv::authority_grant_for(*f.state, bank, gv::authority_kind::issue_currency, gv::national(f.nation), date), date));
	REQUIRE_FALSE(economy::monetary_policy::authorized(*f.state, organization, date));
	economy::monetary_policy::review(*f.state, organization);
	REQUIRE(economy::monetary_policy::policy_rate(*f.state, organization) == Approx(rate));

	// In a dual monarchy the bank answers to the cabinet: a different
	// constitution, not a different code path.
	fixture d;
	auto dual = d.constitute("dual_monarchy");
	REQUIRE(d.state->world.institution_get_independent(dual.institution("central_bank")) == 0);
	REQUIRE(gv::parent_of(*d.state, dual.institution("central_bank")) == dual.institution("cabinet"));
}

TEST_CASE("legacy sliders change fiscal law only through the empowered office", "[governance][constitution]") {
	fixture f;
	auto c = f.constitute("parliamentary_republic");
	auto date = f.state->current_date;
	f.state->world.nation_set_poor_tax(f.nation, 50);
	f.state->world.nation_set_middle_tax(f.nation, 40);
	f.state->world.nation_set_rich_tax(f.nation, 30);
	// No finance minister: the sliders are wishes nobody can enact.
	gv::policy_inputs::apply(*f.state);
	REQUIRE_FALSE(gv::law::effective_amount(*f.state, gv::national(f.nation), gv::law::policy_rule_kind::income_tax_rate, date, 0));
	auto minister = f.seat(c, "finance_minister", 1);
	gv::policy_inputs::apply(*f.state);
	REQUIRE(gv::law::effective_amount(*f.state, gv::national(f.nation), gv::law::policy_rule_kind::income_tax_rate, date, 0) == Approx(0.5f));
	auto first = gv::law::effective_instruments(*f.state, gv::national(f.nation), date).back();
	REQUIRE(f.state->world.legal_instrument_get_person_from_legal_instrument_enactor(first) == minister);
	// New sliders: a new regulation replaces the old one.
	f.state->world.nation_set_poor_tax(f.nation, 20);
	gv::policy_inputs::apply(*f.state);
	REQUIRE(gv::law::effective_amount(*f.state, gv::national(f.nation), gv::law::policy_rule_kind::income_tax_rate, date, 0) == Approx(0.2f));
	REQUIRE(f.state->world.legal_instrument_get_status(first) == uint8_t(gv::law::legal_status::repealed));
}

TEST_CASE("structurally impossible constitutions fail before founding", "[governance][constitution]") {
	auto check = [](std::string institutions, std::string offices, std::string authorities, std::string_view expected) {
		gv::constitution::document doc;
		std::string error;
		REQUIRE(gv::constitution::parse_institutions("key;kind;parent;scope;independent;service;staffing_per_capita;staff_occupation;wage_multiplier\n" + institutions, doc, error));
		REQUIRE(gv::constitution::parse_offices("key;institution;kind;appointer;confirmer;removal;remover;term_days;succession;successor;exclusive;seats\n" + offices, doc, error));
		REQUIRE(gv::constitution::parse_authorities("holder;kind;scope;delegated_from;source\n" + authorities, doc, error));
		REQUIRE_FALSE(gv::constitution::validate(doc, error));
		INFO(error);
		REQUIRE(error.find(expected) != std::string::npos);
	};
	std::string base = "state;central_government;;national;0;none;0;0;1\nministry;ministry;state;national;0;none;0;0;1\n";
	check(base + "orphan;ministry;nowhere;national;0;none;0;0;1\n", "", "", "is not an institution");
	check(base, "minister;ministry;minister;ghost;;appointer;;0;none;;0;1\n", "", "is not another office");
	check(base, "minister;missing;minister;;;irremovable;;0;none;;0;1\n", "", "is not defined");
	check(base, "minister;ministry;minister;;ministry;irremovable;;0;none;;0;1\n", "", "confirmer must be a legislative chamber");
	check(base, "", "institution:ministry;levy_tax;territory;;constitution\n", "territorial powers");
	check(base, "", "institution:ministry;levy_tax;national;institution:state;\n", "holds no earlier grant");
	check("ministry;ministry;;national;0;none;0;0;1\n", "", "", "only the state may lack a parent");
}

TEST_CASE("save and load keep offices, tenures, powers and hierarchy", "[governance][constitution]") {
	fixture f;
	auto c = f.constitute("parliamentary_republic");
	auto date = f.state->current_date;
	auto premier = f.resident(1);
	REQUIRE(gv::offices::install(*f.state, premier, c.office("chief_executive"), date));
	auto minister = f.resident(2);
	REQUIRE(gv::offices::appoint(*f.state, premier, minister, c.office("finance_minister"), date));
	std::vector<uint8_t> bytes(sys::sizeof_save_section(*f.state));
	auto end = sys::write_save_section(bytes.data(), *f.state);
	REQUIRE(end == bytes.data() + bytes.size());
	fixture loaded;
	REQUIRE(loaded.resident(1) == premier);
	REQUIRE(loaded.resident(2) == minister);
	REQUIRE(sys::read_save_section(bytes.data(), end, *loaded.state) == end);
	REQUIRE(gv::offices::holder(*loaded.state, c.office("finance_minister")) == minister);
	REQUIRE(gv::offices::holder(*loaded.state, c.office("chief_executive")) == premier);
	REQUIRE(gv::offices::rules_of(*loaded.state, c.office("finance_minister")).appointer == c.office("chief_executive"));
	REQUIRE(gv::parent_of(*loaded.state, c.institution("tax")) == c.institution("finance"));
	REQUIRE(gv::has_authority(*loaded.state, c.institution("tax"), gv::authority_kind::levy_tax, gv::national(loaded.nation), date));
	REQUIRE(gv::law::constitution_of(*loaded.state, loaded.nation) == c.constitution);
	REQUIRE(gv::territory_of(*loaded.state, c.institution("region")) == gv::territory_of(*f.state, c.institution("region")));
	// The loaded state acts on its saved powers.
	auto replacement = loaded.resident(3);
	REQUIRE(gv::offices::dismiss(*loaded.state, premier, c.office("finance_minister"), date + 1));
	REQUIRE(gv::offices::appoint(*loaded.state, premier, replacement, c.office("finance_minister"), date + 1));
}
