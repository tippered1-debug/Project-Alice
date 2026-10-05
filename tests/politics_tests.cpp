#include "catch.hpp"

#include "canonical_consumer_fixture.hpp"
#include "economy/households.hpp"
#include "governance/electorate.hpp"
#include "governance/elections.hpp"
#include "governance/government.hpp"
#include "governance/legislature.hpp"
#include "governance/offices.hpp"
#include "governance/parties.hpp"
#include "governance/policy.hpp"

namespace politics_tests {

namespace gv = governance;

// A nation of peasants, townspeople and a landed elite, each a household
// cohort with people of its own to stand for office.
struct nation_fixture : constitution_tests::fixture {
	dcon::organization_id peasants{}, townsfolk{}, gentry{};

	dcon::pop_id population(dcon::pop_type_id type, float size) {
		auto pop = state->world.create_pop();
		state->world.force_create_pop_location(pop, province);
		state->world.pop_set_size(pop, size);
		state->world.pop_set_poptype(pop, type);
		state->world.pop_set_culture(pop, state->world.create_culture());
		state->world.pop_set_religion(pop, state->world.create_religion());
		REQUIRE(persons::register_population_cell(*state, pop, site));
		return pop;
	}

	dcon::organization_id cohort(economy::households::role role, float members, float reservation_wage) {
		auto household = economy::households::create(*state, province, role, settlement);
		state->world.organization_set_household_members(household, members);
		state->world.organization_set_household_workers(household, members * economy::households::workers_per_member);
		state->world.organization_set_household_reservation_wage(household, reservation_wage);
		return household;
	}

	nation_fixture() {
		state->culture_definitions.farmers = state->world.create_pop_type();
		state->culture_definitions.aristocrat = state->world.create_pop_type();
		(void)population(state->culture_definitions.farmers, 600.0f);
		(void)population(state->culture_definitions.aristocrat, 200.0f);
		peasants = cohort(economy::households::role::peasant, 1000.0f, 0.5f);
		townsfolk = cohort(economy::households::role::urban, 600.0f, 2.0f);
		gentry = cohort(economy::households::role::landed, 40.0f, 30.0f);
	}

	gv::constitution::founded constitute_and_elect(std::string_view model) {
		auto c = constitute(model);
		gv::government::bootstrap(*state, nation, state->current_date);
		return c;
	}

	uint32_t party_seats(dcon::institution_id chamber) {
		uint32_t total = 0;
		for(auto party : gv::parties::parties_of(*state, nation)) total += gv::government::seats_of(*state, party, chamber, state->current_date);
		return total;
	}
};

} // namespace politics_tests

namespace gv = governance;
using politics_tests::nation_fixture;

TEST_CASE("seat allocation follows highest averages and largest remainders", "[governance][politics]") {
	REQUIRE(gv::elections::highest_averages({ 100.0f, 80.0f, 30.0f, 20.0f }, 8) == std::vector<uint32_t>{ 4, 3, 1, 0 });
	REQUIRE(gv::elections::largest_remainders({ 50.0f, 30.0f, 20.0f }, 7) == std::vector<uint32_t>{ 4, 2, 1 });
}

TEST_CASE("voters want what their economic position gives them reason to want", "[governance][politics]") {
	nation_fixture f;
	auto voters = gv::electorate::voters(*f.state, f.nation);
	REQUIRE(voters.size() == 3);
	auto ideal_of = [&](dcon::organization_id cohort) {
		for(auto const& value : voters) if(value.cohort == cohort) return value.ideal;
		return gv::policy::position{};
	};
	auto tax = gv::policy::topic_id::income_tax, spread = gv::policy::topic_id::progressivity,
		works = gv::policy::topic_id::public_works_appropriation;
	// The poorer want higher and more progressive taxes; the rural want public works.
	REQUIRE(std::get<float>(*gv::policy::get(ideal_of(f.peasants), tax)) > std::get<float>(*gv::policy::get(ideal_of(f.gentry), tax)));
	REQUIRE(std::get<float>(*gv::policy::get(ideal_of(f.townsfolk), spread)) > std::get<float>(*gv::policy::get(ideal_of(f.gentry), spread)));
	REQUIRE(std::get<float>(*gv::policy::get(ideal_of(f.peasants), works)) > std::get<float>(*gv::policy::get(ideal_of(f.townsfolk), works)));
	// The peasants are the median adults.
	REQUIRE(gv::electorate::median_income(voters) == Approx(0.5f * 250.0f / 600.0f));
}

TEST_CASE("parties arise from interests and win seats that real people take", "[governance][politics]") {
	nation_fixture f;
	auto c = f.constitute_and_elect("parliamentary_republic");
	auto parties = gv::parties::parties_of(*f.state, f.nation);
	REQUIRE(parties.size() >= 2);
	for(auto party : parties) REQUIRE(gv::parties::leader(*f.state, party));
	REQUIRE(gv::policy::distance(gv::parties::platform(*f.state, parties[0]), gv::parties::platform(*f.state, parties[1])) > 0.0f);
	auto lower = c.institution("lower_chamber");
	// Every seat is held by a member of the party that won it.
	REQUIRE(gv::legislature::filled_seats(*f.state, lower, f.state->current_date) == 7);
	REQUIRE(f.party_seats(lower) == 7);
	REQUIRE(f.party_seats(c.institution("upper_chamber")) == 5);
	REQUIRE(gv::offices::holder(*f.state, c.office("speaker")));
	REQUIRE(gv::offices::holder(*f.state, c.office("head_of_state")));
	REQUIRE(gv::offices::holder(*f.state, c.office("mayor")));
	auto shares = gv::elections::last_shares(*f.state, f.nation);
	float total = 0.0f;
	for(auto const& [party, share] : shares) total += share;
	REQUIRE(total == Approx(1.0f));
	// Next elections are scheduled by term.
	REQUIRE(f.state->world.institution_get_next_election(lower) == f.state->current_date + 1460);
}

TEST_CASE("the largest party forms a confirmed majority government and shares the ministries", "[governance][politics]") {
	nation_fixture f;
	auto c = f.constitute_and_elect("parliamentary_republic");
	auto date = f.state->current_date;
	auto lower = c.institution("lower_chamber");
	auto premier = gv::offices::holder(*f.state, c.office("chief_executive"));
	REQUIRE(premier);
	auto coalition = gv::government::coalition(*f.state, f.nation);
	REQUIRE(!coalition.empty());
	// The premier leads the largest party of the confidence chamber.
	uint32_t most = 0;
	dcon::organization_id largest{};
	for(auto party : gv::parties::parties_of(*f.state, f.nation)) {
		auto seats = gv::government::seats_of(*f.state, party, lower, date);
		if(seats > most) { most = seats; largest = party; }
	}
	REQUIRE(gv::parties::leader(*f.state, largest) == premier);
	uint32_t held = 0;
	for(auto party : coalition) held += gv::government::seats_of(*f.state, party, lower, date);
	REQUIRE(float(held) > 0.5f * 7.0f);
	// Ministers belong to governing parties; the central bank governor to none.
	auto minister = gv::offices::holder(*f.state, c.office("finance_minister"));
	REQUIRE(minister);
	REQUIRE(f.state->world.organization_get_party_governing(gv::parties::party_of(*f.state, minister)));
	auto governor = gv::offices::holder(*f.state, c.office("bank_governor"));
	REQUIRE(governor);
	REQUIRE_FALSE(gv::parties::party_of(*f.state, governor));
	// The programme is the fiscal law, enacted by the finance minister.
	REQUIRE(gv::policy::law_matches(*f.state, f.nation, gv::government::program(*f.state, f.nation), date));
	auto regulation = gv::law::effective_instruments(*f.state, gv::national(f.nation), date).back();
	REQUIRE(f.state->world.legal_instrument_get_person_from_legal_instrument_enactor(regulation) == minister);
}

TEST_CASE("a government without a majority falls to a motion of no confidence and a new one forms", "[governance][politics]") {
	nation_fixture f;
	auto c = f.constitute_and_elect("parliamentary_republic");
	auto date = f.state->current_date + 1;
	auto chief = c.office("chief_executive");
	auto first_tenure = gv::offices::current_tenure(*f.state, chief);
	// The coalition breaks up: no party governs any more.
	for(auto party : gv::parties::parties_of(*f.state, f.nation)) f.state->world.organization_set_party_governing(party, 0);
	REQUIRE(gv::government::test_confidence(*f.state, f.nation, date));
	REQUIRE_FALSE(f.state->world.office_tenure_get_active(first_tenure));
	REQUIRE(gv::legislature::no_confidence_passed(*f.state, chief, date) == false); // the new premier faces no motion yet
	REQUIRE(gv::offices::holder(*f.state, chief));
	REQUIRE(!gv::government::coalition(*f.state, f.nation).empty());
	// A majority government survives the test.
	REQUIRE_FALSE(gv::government::test_confidence(*f.state, f.nation, date + 1));
}

TEST_CASE("a falling economy costs the governing parties votes", "[governance][politics]") {
	nation_fixture f;
	auto c = f.constitute_and_elect("parliamentary_republic");
	auto voters = gv::electorate::voters(*f.state, f.nation);
	auto parties = gv::parties::parties_of(*f.state, f.nation);
	// One party governs alone.
	auto governing = std::vector<dcon::organization_id>{ parties.front() };
	for(auto party : parties) f.state->world.organization_set_party_governing(party, party == parties.front() ? 1 : 0);
	auto share = [&](float growth) {
		auto counted = gv::elections::count(*f.state, f.nation, voters, parties, {}, growth);
		float mine = 0.0f;
		for(auto party : governing) mine += counted.votes[party.index()];
		return mine / counted.cast;
	};
	REQUIRE(share(-0.2f) < share(0.0f));
	REQUIRE(share(0.2f) > share(0.0f));
	(void)c;
}

TEST_CASE("parties drift to their voters, new blocs found parties, and failures dissolve", "[governance][politics]") {
	nation_fixture f;
	(void)f.constitute("parliamentary_republic");
	auto date = f.state->current_date;
	gv::policy::position left, right, far;
	(void)gv::policy::set(left, gv::policy::topic_id::income_tax, 0.3f);
	(void)gv::policy::set(right, gv::policy::topic_id::income_tax, 0.05f);
	(void)gv::policy::set(far, gv::policy::topic_id::income_tax, 0.4f);
	(void)gv::policy::set(far, gv::policy::topic_id::progressivity, 1.0f);
	(void)gv::policy::set(far, gv::policy::topic_id::education_appropriation, 1.0f);
	(void)gv::policy::set(far, gv::policy::topic_id::policing_appropriation, 1.0f);
	(void)gv::policy::set(far, gv::policy::topic_id::public_works_appropriation, 1.0f);
	(void)gv::policy::set(far, gv::policy::topic_id::local_government_appropriation, 1.0f);
	auto a = gv::parties::create(*f.state, f.nation, left, date);
	auto b = gv::parties::create(*f.state, f.nation, right, date);
	std::vector<gv::electorate::voter> voters(3);
	voters[0].adults = 100.0f; voters[0].turnout = 1.0f; voters[0].ideal = left; (void)gv::policy::set(voters[0].ideal, gv::policy::topic_id::income_tax, 0.25f);
	voters[1].adults = 100.0f; voters[1].turnout = 1.0f; voters[1].ideal = right;
	voters[2].adults = 100.0f; voters[2].turnout = 1.0f; voters[2].ideal = far;
	std::map<uint32_t, float> shares{ { a.index(), 0.5f }, { b.index(), 0.01f } };
	gv::parties::evolve(*f.state, f.nation, voters, shares, 0, date);
	// The left party moved 30% toward the centre of the voters nearest it
	// (the first and the far voter): from 0.3 toward 0.325.
	REQUIRE(std::get<float>(*gv::policy::get(gv::parties::platform(*f.state, a), gv::policy::topic_id::income_tax)) == Approx(0.3075f));
	REQUIRE(std::get<float>(*gv::policy::get(gv::parties::platform(*f.state, b), gv::policy::topic_id::income_tax)) == Approx(0.05f));
	// The unrepresented third founded a party.
	REQUIRE(gv::parties::parties_of(*f.state, f.nation).size() == 3);
	gv::parties::evolve(*f.state, f.nation, voters, shares, 0, date);
	// The party below 2% in two elections is gone.
	auto remaining = gv::parties::parties_of(*f.state, f.nation);
	REQUIRE(std::find(remaining.begin(), remaining.end(), b) == remaining.end());
}

TEST_CASE("an elected president governs with their party and a running mate", "[governance][politics]") {
	nation_fixture f;
	auto c = f.constitute_and_elect("presidential");
	auto president = gv::offices::holder(*f.state, c.office("chief_executive"));
	auto deputy = gv::offices::holder(*f.state, c.office("vice_president"));
	REQUIRE(president);
	REQUIRE(deputy);
	auto party = gv::parties::party_of(*f.state, president);
	REQUIRE(party);
	REQUIRE(gv::parties::party_of(*f.state, deputy) == party);
	REQUIRE(gv::government::coalition(*f.state, f.nation) == std::vector<dcon::organization_id>{ party });
	REQUIRE(gv::parties::party_of(*f.state, gv::offices::holder(*f.state, c.office("finance_minister"))) == party);
}

TEST_CASE("an absolute monarchy holds no elections and its monarch appoints outside party politics", "[governance][politics]") {
	nation_fixture f;
	auto c = f.constitute("absolute_monarchy");
	std::string error;
	REQUIRE(gv::constitution::appoint_founders(*f.state, f.nation, c, f.state->current_date, error));
	gv::government::bootstrap(*f.state, f.nation, f.state->current_date);
	auto monarch = gv::offices::holder(*f.state, c.office("chief_executive"));
	REQUIRE(monarch);
	REQUIRE_FALSE(gv::parties::party_of(*f.state, monarch));
	REQUIRE(f.state->world.institution_get_electoral_system(c.institution("lower_chamber")) == 0);
	auto minister = gv::offices::holder(*f.state, c.office("finance_minister"));
	REQUIRE(minister);
	REQUIRE_FALSE(gv::parties::party_of(*f.state, minister));
	REQUIRE(gv::policy::law_matches(*f.state, f.nation, gv::government::program(*f.state, f.nation), f.state->current_date));
}

TEST_CASE("terms end in new elections and a dead member's seat passes down the list", "[governance][politics]") {
	nation_fixture f;
	auto c = f.constitute_and_elect("parliamentary_republic");
	auto lower = c.institution("lower_chamber");
	auto seat = gv::legislature::seats_of(*f.state, lower).front();
	auto member = gv::offices::holder(*f.state, seat);
	auto party = gv::parties::party_of(*f.state, member);
	REQUIRE(persons::mark_dead(*f.state, member, f.state->current_date + 10));
	f.state->current_date = f.state->current_date + 10;
	gv::government::fill_vacancies(*f.state, f.nation, f.state->current_date);
	auto successor = gv::offices::holder(*f.state, seat);
	REQUIRE(successor);
	REQUIRE(successor != member);
	REQUIRE(gv::parties::party_of(*f.state, successor) == party);

	size_t elections_before = 0;
	f.state->world.for_each_election([&](auto) { ++elections_before; });
	f.state->current_date = f.state->world.institution_get_next_election(lower);
	gv::government::process(*f.state, f.state->current_date);
	size_t elections_after = 0;
	f.state->world.for_each_election([&](auto) { ++elections_after; });
	REQUIRE(elections_after > elections_before);
	REQUIRE(f.state->world.institution_get_next_election(lower) == f.state->current_date + 1460);
	REQUIRE(f.party_seats(lower) == 7);
}
