#include "government.hpp"

#include "system_state.hpp"
#include "governance/electorate.hpp"
#include "governance/elections.hpp"
#include "governance/governance.hpp"
#include "governance/law/law.hpp"
#include "governance/legislature.hpp"
#include "governance/offices.hpp"
#include "governance/parties.hpp"
#include "persons/exact_population.hpp"
#include "persons/persons.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <unordered_set>

namespace governance::government {
namespace {

// Offices outside party politics: the judiciary, the central bank, the general staff.
bool nonpartisan(sys::state const& state, dcon::office_id office) {
	switch(office_kind(state.world.office_get_kind(office))) {
	case office_kind::central_bank_governor: case office_kind::chief_justice: case office_kind::judge_seat:
	case office_kind::chief_of_general_staff:
		return true;
	default:
		return false;
	}
}

bool ministerial(sys::state const& state, dcon::office_id office) {
	auto kind = office_kind(state.world.office_get_kind(office));
	return kind == office_kind::finance_minister || kind == office_kind::minister;
}

dcon::person_id holder_on(sys::state const& state, dcon::office_id office, sys::date date) {
	auto person = offices::holder(state, office);
	return person && offices::tenure_of(state, person, office, date) ? person : dcon::person_id{};
}

// Adults of the nation who belong to no party and hold no office.
dcon::person_id independent_adult(sys::state& state, dcon::nation_id nation, sys::date date, std::unordered_set<uint32_t>& tried) {
	auto capital = state.world.nation_get_capital(nation);
	std::vector<dcon::province_id> provinces;
	if(capital) provinces.push_back(capital);
	for(auto ownership : state.world.nation_get_province_ownership(nation))
		if(ownership.get_province().id != capital) provinces.push_back(ownership.get_province().id);
	for(auto province : provinces)
		for(auto location : state.world.province_get_pop_location(province)) {
			auto cell = persons::source_population_cell_for_population(state, location.get_pop());
			auto count = cell ? persons::exact_population::literal_count_for_cell(state, cell) : 0;
			for(uint64_t ordinal = 0; ordinal < count; ++ordinal) {
				persons::person_key key{ cell, ordinal };
				if(!persons::exact_population::alive(state, key)) continue;
				auto age = (int32_t(date.to_raw_value()) - 1) - persons::exact_population::birth_day_index(state, key);
				if(age < 30 * 365 || age > 70 * 365) continue;
				auto existing = persons::exact_population::profile_for_person(state, key);
				if(existing && (parties::party_of(state, existing) || !persons::active_offices_of(state, existing).empty())) continue;
				auto person = existing ? existing : persons::materialize_profile(state, key);
				if(person && tried.insert(person.index()).second) return person;
			}
		}
	return {};
}

dcon::person_id last_holder(sys::state const& state, dcon::office_id office) {
	dcon::office_tenure_id latest{};
	state.world.office_for_each_office_tenure_office_as_office(office, [&](auto relation) {
		auto tenure = state.world.office_tenure_office_get_tenure(relation);
		if(!latest || state.world.office_tenure_get_ended_on(latest) < state.world.office_tenure_get_ended_on(tenure)) latest = tenure;
	});
	return latest ? state.world.office_tenure_get_person_from_office_tenure_person(latest) : dcon::person_id{};
}

void set_governing(sys::state& state, dcon::nation_id nation, std::vector<dcon::organization_id> const& governing) {
	for(auto party : parties::parties_of(state, nation)) state.world.organization_set_party_governing(party, 0);
	for(auto party : governing) state.world.organization_set_party_governing(party, 1);
}

uint32_t total_seats(sys::state const& state, dcon::organization_id party, dcon::nation_id nation, sys::date date) {
	uint32_t result = 0;
	for(auto institution : institutions_of(state, nation))
		if(legislature::is_chamber(state, institution)) result += seats_of(state, party, institution, date);
	return result;
}

// Appoints, as the appointer's holder, the first candidate the constitution
// lets take the office; a confirming chamber votes on each by party line.
bool appoint_first(sys::state& state, dcon::office_id office, dcon::person_id appointer, std::vector<dcon::person_id> const& candidates, sys::date date) {
	auto confirmer = offices::rules_of(state, office).confirmer;
	for(auto candidate : candidates) {
		if(!candidate || candidate == appointer && state.world.office_get_exclusive(office)) continue;
		if(confirmer) whip_confirmation(state, office, candidate, date);
		if(offices::appoint(state, appointer, candidate, office, date)) return true;
	}
	return false;
}

} // namespace

dcon::office_id chief_office(sys::state const& state, dcon::nation_id nation) {
	dcon::office_id head_of_government{}, head_of_state{};
	for(auto institution : institutions_of(state, nation))
		for(auto office : offices_of(state, institution)) {
			auto kind = office_kind(state.world.office_get_kind(office));
			if(kind == office_kind::finance_minister)
				if(auto appointer = offices::rules_of(state, office).appointer) return appointer;
			if(kind == office_kind::head_of_government && !head_of_government) head_of_government = office;
			if(kind == office_kind::head_of_state && !head_of_state) head_of_state = office;
		}
	return head_of_government ? head_of_government : head_of_state;
}

dcon::institution_id confidence_chamber(sys::state const& state, dcon::nation_id nation) {
	auto chief = chief_office(state, nation);
	return chief ? offices::rules_of(state, chief).confirmer : dcon::institution_id{};
}

std::vector<dcon::organization_id> coalition(sys::state const& state, dcon::nation_id nation) {
	std::vector<dcon::organization_id> result;
	for(auto party : parties::parties_of(state, nation))
		if(state.world.organization_get_party_governing(party)) result.push_back(party);
	return result;
}

uint32_t seats_of(sys::state const& state, dcon::organization_id party, dcon::institution_id chamber, sys::date date) {
	uint32_t result = 0;
	for(auto seat : legislature::seats_of(state, chamber)) {
		auto person = holder_on(state, seat, date);
		if(person && parties::party_of(state, person) == party) ++result;
	}
	return result;
}

policy::position program(sys::state const& state, dcon::nation_id nation) {
	auto governing = coalition(state, nation);
	policy::position result{};
	if(!governing.empty()) {
		auto chamber = confidence_chamber(state, nation);
		double total = 0.0;
		for(auto party : governing) {
			double weight = chamber ? seats_of(state, party, chamber, state.current_date) : total_seats(state, party, nation, state.current_date);
			if(weight <= 0.0) weight = 1.0;
			auto platform = parties::platform(state, party);
			for(size_t i = 0; i < policy::dimension_count; ++i) result[i] += float(weight) * platform[i];
			total += weight;
		}
		for(auto& value : result) value = float(value / total);
		return policy::clamp(result);
	}
	// A government without a party base serves the wealthiest tenth.
	auto voters = electorate::voters(state, nation);
	std::sort(voters.begin(), voters.end(), [](auto const& a, auto const& b) { return a.wealth > b.wealth; });
	double adults = 0.0, all = 0.0;
	for(auto const& value : voters) all += value.adults;
	for(auto const& value : voters) {
		if(adults >= 0.1 * all && adults > 0.0) break;
		for(size_t i = 0; i < policy::dimension_count; ++i) result[i] += value.adults * value.ideal[i];
		adults += value.adults;
	}
	if(adults > 0.0) for(auto& value : result) value = float(value / adults);
	return policy::clamp(result);
}

void whip_confirmation(sys::state& state, dcon::office_id office, dcon::person_id candidate, sys::date date) {
	auto chamber = offices::rules_of(state, office).confirmer;
	for(auto seat : legislature::seats_of(state, chamber))
		if(auto member = holder_on(state, seat, date)) {
			auto party = parties::party_of(state, member);
			(void)legislature::vote_on_confirmation(state, member, chamber, office, candidate,
				party && state.world.organization_get_party_governing(party), date);
		}
}

void whip_no_confidence(sys::state& state, dcon::office_id office, sys::date date) {
	auto chamber = offices::rules_of(state, office).confirmer;
	for(auto seat : legislature::seats_of(state, chamber))
		if(auto member = holder_on(state, seat, date)) {
			auto party = parties::party_of(state, member);
			(void)legislature::vote_on_no_confidence(state, member, office,
				!(party && state.world.organization_get_party_governing(party)), date);
		}
}

bool form(sys::state& state, dcon::nation_id nation, sys::date date) {
	auto chief = chief_office(state, nation);
	if(!chief) return false;
	auto rules = offices::rules_of(state, chief);
	std::vector<dcon::organization_id> governing;
	if(!rules.appointer) {
		// An elected or hereditary chief executive governs with their own party.
		if(auto party = parties::party_of(state, holder_on(state, chief, date))) governing.push_back(party);
		set_governing(state, nation, governing);
	} else {
		auto chamber = rules.confirmer;
		std::vector<std::pair<uint32_t, dcon::organization_id>> ranked;
		for(auto party : parties::parties_of(state, nation)) {
			auto seats = chamber ? seats_of(state, party, chamber, date) : total_seats(state, party, nation, date);
			if(seats > 0) ranked.push_back({ seats, party });
		}
		std::stable_sort(ranked.begin(), ranked.end(), [](auto const& a, auto const& b) { return a.first > b.first; });
		if(ranked.empty()) return false;
		auto formateur = ranked.front().second;
		governing.push_back(formateur);
		if(chamber) {
			// The formateur adds the nearest parties until the coalition holds a majority.
			auto filled = legislature::filled_seats(state, chamber, date);
			uint32_t held = ranked.front().first;
			auto platform = parties::platform(state, formateur);
			std::vector<std::pair<float, std::pair<uint32_t, dcon::organization_id>>> partners;
			for(size_t i = 1; i < ranked.size(); ++i)
				partners.push_back({ policy::distance(platform, parties::platform(state, ranked[i].second)), ranked[i] });
			std::stable_sort(partners.begin(), partners.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
			for(auto const& [distance, partner] : partners) {
				if(float(held) > legislature::simple_majority * float(filled)) break;
				governing.push_back(partner.second);
				held += partner.first;
			}
		}
		set_governing(state, nation, governing);
		auto candidate = parties::leader(state, formateur);
		auto current = holder_on(state, chief, date);
		if(current != candidate) {
			// The outgoing chief executive resigns for the new government.
			if(current) (void)offices::resign(state, current, chief, date);
			auto appointer = holder_on(state, rules.appointer, date);
			if(!appointer || !appoint_first(state, chief, appointer, { candidate }, date)) return false;
		}
	}
	auto chief_person = holder_on(state, chief, date);
	if(auto state_institution = find_institution(state, nation, institution_kind::central_government))
		state.world.institution_set_political_baseline_income(state_institution, electorate::median_income(electorate::voters(state, nation)));
	// Ministries are shared among the governing parties by seats.
	if(chief_person && !governing.empty()) {
		std::vector<dcon::office_id> ministries;
		for(auto institution : institutions_of(state, nation))
			for(auto office : offices_of(state, institution))
				if(ministerial(state, office) && offices::rules_of(state, office).appointer == chief) ministries.push_back(office);
		std::sort(ministries.begin(), ministries.end(), [](auto a, auto b) { return a.index() < b.index(); });
		std::vector<float> weights;
		for(auto party : governing) weights.push_back(float(std::max<uint32_t>(1, total_seats(state, party, nation, date))));
		auto shares = elections::highest_averages(weights, uint32_t(ministries.size()));
		size_t next = 0;
		for(size_t p = 0; p < governing.size(); ++p)
			for(uint32_t taken = 0; taken < shares[p] && next < ministries.size(); ++taken, ++next) {
				auto office = ministries[next];
				auto minister = holder_on(state, office, date);
				if(minister && !offices::acting(state, offices::current_tenure(state, office)) && parties::party_of(state, minister) == governing[p]) continue;
				if(minister && !offices::acting(state, offices::current_tenure(state, office))) (void)offices::dismiss(state, chief_person, office, date);
				std::vector<dcon::person_id> candidates;
				for(auto member : parties::members(state, governing[p]))
					if(member != chief_person) candidates.push_back(member);
				(void)appoint_first(state, office, chief_person, candidates, date);
			}
	}
	implement(state, nation, date);
	return bool(chief_person);
}

bool test_confidence(sys::state& state, dcon::nation_id nation, sys::date date) {
	auto chief = chief_office(state, nation);
	auto rules = offices::rules_of(state, chief);
	if(!chief || rules.removal != offices::removal_rule::by_no_confidence || !rules.confirmer || !holder_on(state, chief, date)) return false;
	uint32_t held = 0;
	for(auto party : coalition(state, nation)) held += seats_of(state, party, rules.confirmer, date);
	if(float(held) > legislature::simple_majority * float(legislature::filled_seats(state, rules.confirmer, date))) return false;
	whip_no_confidence(state, chief, date);
	if(!offices::remove_by_no_confidence(state, chief, date)) return false;
	(void)form(state, nation, date);
	return true;
}

void fill_vacancies(sys::state& state, dcon::nation_id nation, sys::date date) {
	std::unordered_set<uint32_t> tried;
	for(auto institution : institutions_of(state, nation))
		if(legislature::is_chamber(state, institution)) elections::fill_presiding(state, institution, date);
	for(auto institution : institutions_of(state, nation))
		for(auto office : offices_of(state, institution)) {
			if(!offices::vacant(state, office)) continue;
			auto rules = offices::rules_of(state, office);
			// A seat filled by election passes to the next member of the late holder's list.
			if(!rules.appointer && state.world.office_get_kind(office) == uint8_t(office_kind::legislator)) {
				auto party = parties::party_of(state, last_holder(state, office));
				if(!party) continue;
				for(auto member : parties::members(state, party)) {
					bool seated = false;
					for(auto held : persons::active_offices_of(state, member))
						if(state.world.office_get_kind(held) == uint8_t(office_kind::legislator)) seated = true;
					if(!seated && offices::install(state, member, office, date)) break;
				}
				continue;
			}
			if(!rules.appointer) continue;
			auto appointer = holder_on(state, rules.appointer, date);
			if(!appointer) continue;
			std::vector<dcon::person_id> candidates;
			auto party = parties::party_of(state, appointer);
			if(!nonpartisan(state, office) && party) {
				for(auto member : parties::members(state, party)) if(member != appointer) candidates.push_back(member);
				for(auto partner : coalition(state, nation))
					if(partner != party) for(auto member : parties::members(state, partner)) candidates.push_back(member);
			}
			for(int i = 0; i < 4; ++i) candidates.push_back(independent_adult(state, nation, date, tried));
			(void)appoint_first(state, office, appointer, candidates, date);
		}
}

void implement(sys::state& state, dcon::nation_id nation, sys::date date) {
	auto target = program(state, nation);
	if(policy::law_matches(state, nation, target, date)) return;
	if(auto regulator = policy::fiscal_regulator(state, nation, date)) (void)policy::enact(state, nation, regulator, target, date);
}

void bootstrap(sys::state& state, dcon::nation_id nation, sys::date date) {
	if(!law::constitution_of(state, nation)) return;
	uint32_t seats = 0;
	for(auto institution : institutions_of(state, nation))
		if(legislature::is_chamber(state, institution)) seats += uint32_t(legislature::seats_of(state, institution).size());
	auto voters = electorate::voters(state, nation);
	if(parties::parties_of(state, nation).empty())
		(void)parties::found(state, nation, voters, std::max<uint32_t>(seats, 4) + parties::list_margin, date);
	(void)elections::process(state, nation, date);
	(void)form(state, nation, date);
	fill_vacancies(state, nation, date);
	implement(state, nation, date);
}

void process(sys::state& state, sys::date date) {
	state.world.for_each_nation([&](dcon::nation_id nation) {
		if(!law::constitution_of(state, nation)) return;
		// Parties evolve on the eve of a chamber election so that new ones can stand.
		bool chamber_due = false;
		for(auto institution : institutions_of(state, nation)) {
			auto next = state.world.institution_get_next_election(institution);
			if(legislature::is_chamber(state, institution) && next && !(date < next)
				&& state.world.institution_get_electoral_system(institution) != uint8_t(elections::electoral_system::none)) chamber_due = true;
		}
		if(chamber_due) {
			uint32_t seats = 0;
			for(auto institution : institutions_of(state, nation))
				if(legislature::is_chamber(state, institution)) seats += uint32_t(legislature::seats_of(state, institution).size());
			parties::evolve(state, nation, electorate::voters(state, nation), elections::last_shares(state, nation),
				std::max<uint32_t>(seats, 4) + parties::list_margin, date);
		}
		if(elections::process(state, nation, date)) (void)form(state, nation, date);
		else if(state.current_date.to_ymd(state.start_date).day == 1) (void)test_confidence(state, nation, date);
		fill_vacancies(state, nation, date);
		implement(state, nation, date);
	});
}

} // namespace governance::government
