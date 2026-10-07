#include "elections.hpp"

#include "system_state.hpp"
#include "governance/governance.hpp"
#include "governance/legislature.hpp"
#include "governance/offices.hpp"
#include "governance/parties.hpp"
#include "governance/power_topology.hpp"
#include "governance/political_resources.hpp"
#include "persons/persons.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <set>

namespace governance::elections {
namespace {

std::vector<dcon::organization_id> contenders(sys::state const& state, dcon::nation_id nation, sys::date date) {
	std::vector<dcon::organization_id> result;
	for(auto party : parties::parties_of(state, nation))
		if(parties::leader(state, party, date)) result.push_back(party);
	return result;
}

dcon::election_id record(sys::state& state, tally const& counted, std::map<uint32_t, uint16_t> const& seats, sys::date date) {
	auto election = state.world.create_election();
	state.world.election_set_held_on(election, date);
	state.world.election_set_electorate(election, counted.electorate);
	state.world.election_set_turnout(election, counted.electorate > 0.0f ? counted.cast / counted.electorate : 0.0f);
	std::set<uint32_t> parties;
	for(auto const& [index, votes] : counted.votes) parties.insert(index);
	for(auto const& [index, won] : seats) parties.insert(index);
	for(auto index : parties) {
		auto result = state.world.create_election_result();
		auto votes = counted.votes.find(index);
		auto won = seats.find(index);
		state.world.election_result_set_votes(result, votes == counted.votes.end() ? 0.0f : votes->second);
		state.world.election_result_set_seats(result, won == seats.end() ? uint16_t(0) : won->second);
		state.world.force_create_election_result_election(result, election);
		state.world.force_create_election_result_party(result, dcon::organization_id{ dcon::organization_id::value_base_t(index) });
	}
	return election;
}

void add(tally& total, tally const& part) {
	for(auto const& [index, votes] : part.votes) total.votes[index] += votes;
	total.electorate += part.electorate;
	total.cast += part.cast;
}

bool holds_seat(sys::state const& state, dcon::person_id person) {
	for(auto office : persons::active_offices_of(state, person))
		if(state.world.office_get_kind(office) == uint8_t(office_kind::legislator)) return true;
	return false;
}

// Seats a person in an office, first resigning the offices an exclusive
// office cannot be combined with.
bool seat(sys::state& state, dcon::person_id person, dcon::office_id office, sys::date date) {
	if(!person || !power_topology::appointment_candidate_eligible(state, office, person, date)) return false;
	if(state.world.office_get_exclusive(office))
		for(auto held : persons::active_offices_of(state, person))
			if(held != office) (void)offices::resign(state, person, held, date);
	return bool(offices::install(state, person, office, date));
}

dcon::organization_id largest_party(sys::state const& state, std::map<uint32_t, uint16_t> const& seats) {
	dcon::organization_id best{};
	uint16_t most = 0;
	for(auto const& [index, won] : seats)
		if(won > most) { most = won; best = dcon::organization_id{ dcon::organization_id::value_base_t(index) }; }
	return best;
}

std::map<uint32_t, uint16_t> seats_held(sys::state const& state, dcon::institution_id chamber, sys::date date) {
	std::map<uint32_t, uint16_t> result;
	for(auto office : legislature::seats_of(state, chamber)) {
		auto person = offices::holder(state, office);
		auto party = person ? parties::party_of(state, person) : dcon::organization_id{};
		if(party && offices::tenure_of(state, person, office, date)) ++result[party.index()];
	}
	return result;
}

void schedule(sys::state& state, dcon::institution_id chamber, sys::date date) {
	auto term = state.world.institution_get_election_term_days(chamber);
	state.world.institution_set_next_election(chamber, term > 0 ? date + int32_t(term) : sys::date{});
}

void schedule(sys::state& state, dcon::office_id office, sys::date date) {
	auto term = state.world.office_get_election_term_days(office);
	state.world.office_set_next_election(office, term > 0 ? date + int32_t(term) : sys::date{});
}

} // namespace

void set_rule(sys::state& state, dcon::institution_id chamber, electoral_system system, district_rule districts, uint16_t term_days, sys::date first) {
	state.world.institution_set_electoral_system(chamber, uint8_t(system));
	state.world.institution_set_election_districts(chamber, uint8_t(districts));
	state.world.institution_set_election_term_days(chamber, term_days);
	state.world.institution_set_next_election(chamber, first);
}

void set_rule(sys::state& state, dcon::office_id office, electoral_system system, district_rule districts, uint16_t term_days, sys::date first) {
	state.world.office_set_electoral_system(office, uint8_t(system));
	state.world.office_set_election_districts(office, uint8_t(districts));
	state.world.office_set_election_term_days(office, term_days);
	state.world.office_set_next_election(office, first);
}

namespace {

tally count_impl(sys::state const& state, dcon::nation_id nation, std::vector<electorate::voter> const& voters,
	std::vector<dcon::organization_id> const& candidates, dcon::territorial_unit_id district, float incumbent,
	sys::date election_date, std::map<uint32_t, float> const& campaign_amounts) {
	tally result;
	if(candidates.empty()) return result;
	std::vector<policy::position> platforms;
	std::vector<float> bonus;
	double electorate_size = 0.0;
	for(auto const& value : voters) electorate_size += std::max(0.0f, value.adults);
	auto median_income = electorate::median_income(voters);
	// Twenty organizers can make personal contact with roughly fifty adults
	// apiece in one day; normalize payroll against that level of campaign reach.
	auto campaign_scale = std::max(1.0, double(std::max(0.0f, median_income)) * electorate_size / 50.0);
	for(auto party : candidates) {
		platforms.push_back(parties::platform(state, party));
		auto governing_bonus = state.world.organization_get_party_governing(party) ? retrospective_weight * incumbent : 0.0f;
		auto campaign_bonus = 0.0f;
		if(election_date) {
			auto it = campaign_amounts.find(party.index());
			auto spending = it == campaign_amounts.end() ? 0.0f : it->second;
			if(spending > 0.0f && electorate_size > 0.0)
				campaign_bonus = std::clamp(0.25f * std::log1p(float(double(spending) / campaign_scale)), 0.0f, 0.75f);
		}
		bonus.push_back(governing_bonus + campaign_bonus);
	}
	std::vector<double> votes(candidates.size(), 0.0);
	for(auto const& value : voters) {
		if(district && !contains(state, local(district), value.province)) continue;
		result.electorate += value.adults;
		auto cast = value.adults * value.turnout;
		result.cast += cast;
		std::vector<double> utility(candidates.size());
		double best = -1.0e30;
		for(size_t j = 0; j < candidates.size(); ++j) {
			utility[j] = -choice_sensitivity * policy::distance(value.ideal, platforms[j], value.issue_salience) + bonus[j];
			best = std::max(best, utility[j]);
		}
		double total = 0.0;
		for(auto& u : utility) { u = std::exp(u - best); total += u; }
		for(size_t j = 0; j < candidates.size(); ++j) votes[j] += cast * utility[j] / total;
	}
	for(size_t j = 0; j < candidates.size(); ++j) result.votes[candidates[j].index()] = float(votes[j]);
	(void)nation;
	return result;
}

} // namespace

tally count(sys::state const& state, dcon::nation_id nation, std::vector<electorate::voter> const& voters,
	std::vector<dcon::organization_id> const& candidates, dcon::territorial_unit_id district, float incumbent,
	sys::date election_date) {
	auto campaign_amounts = election_date
		? political_resources::campaign_spending_by_party(state, candidates, election_date - 90, election_date)
		: std::map<uint32_t, float>{};
	return count_impl(state, nation, voters, candidates, district, incumbent, election_date, campaign_amounts);
}

std::vector<uint32_t> highest_averages(std::vector<float> const& votes, uint32_t seats) {
	std::vector<uint32_t> result(votes.size(), 0);
	for(uint32_t seat = 0; seat < seats && !votes.empty(); ++seat) {
		size_t best = 0;
		double best_quotient = -1.0;
		for(size_t i = 0; i < votes.size(); ++i) {
			auto quotient = double(votes[i]) / double(result[i] + 1);
			if(quotient > best_quotient) { best = i; best_quotient = quotient; }
		}
		if(best_quotient <= 0.0) break;
		++result[best];
	}
	return result;
}

std::vector<uint32_t> largest_remainders(std::vector<float> const& weights, uint32_t seats) {
	std::vector<uint32_t> result(weights.size(), 0);
	double total = std::accumulate(weights.begin(), weights.end(), 0.0);
	if(!(total > 0.0)) return result;
	std::vector<std::pair<double, size_t>> remainders;
	uint32_t given = 0;
	for(size_t i = 0; i < weights.size(); ++i) {
		auto quota = double(weights[i]) * double(seats) / total;
		result[i] = uint32_t(std::floor(quota));
		given += result[i];
		remainders.push_back({ quota - std::floor(quota), i });
	}
	std::stable_sort(remainders.begin(), remainders.end(), [](auto const& a, auto const& b) { return a.first > b.first; });
	for(size_t i = 0; given < seats && i < remainders.size(); ++i, ++given) ++result[remainders[i].second];
	return result;
}

float incumbent_growth(sys::state const& state, dcon::nation_id nation, float median_income) {
	auto state_institution = find_institution(state, nation, institution_kind::central_government);
	auto baseline = state_institution ? state.world.institution_get_political_baseline_income(state_institution) : 0.0f;
	if(!(baseline > 0.0f) || !std::isfinite(median_income)) return 0.0f;
	return std::clamp(median_income / baseline - 1.0f, -0.2f, 0.2f);
}

dcon::election_id hold(sys::state& state, dcon::institution_id chamber, std::vector<electorate::voter> const& voters, sys::date date) {
	auto nation = nation_of(state, chamber);
	auto system = electoral_system(state.world.institution_get_electoral_system(chamber));
	schedule(state, chamber, date);
	auto candidates = contenders(state, nation, date);
	auto seat_offices = legislature::seats_of(state, chamber);
	std::sort(seat_offices.begin(), seat_offices.end(), [](auto a, auto b) { return a.index() < b.index(); });
	if(candidates.empty() || seat_offices.empty() || (system != electoral_system::proportional && system != electoral_system::plurality)) return {};
	auto growth = incumbent_growth(state, nation, electorate::median_income(voters));
	auto campaign_amounts = political_resources::campaign_spending_by_party(state, candidates, date - 90, date);
	auto allocate = [&](tally const& counted, uint32_t seats, std::map<uint32_t, uint16_t>& won) {
		std::vector<float> votes;
		for(auto party : candidates) votes.push_back(counted.votes.at(party.index()));
		std::vector<uint32_t> split;
		if(system == electoral_system::proportional) split = highest_averages(votes, seats);
		else {
			split.assign(candidates.size(), 0);
			auto winner = size_t(std::max_element(votes.begin(), votes.end()) - votes.begin());
			if(!votes.empty() && votes[winner] > 0.0f) split[winner] = seats;
		}
		for(size_t j = 0; j < candidates.size(); ++j) if(split[j]) won[candidates[j].index()] += uint16_t(split[j]);
	};
	tally total;
	std::map<uint32_t, uint16_t> won;
	if(district_rule(state.world.institution_get_election_districts(chamber)) == district_rule::regional) {
		std::map<uint32_t, float> adults;
		for(auto const& value : voters) if(value.region) adults[value.region.index()] += value.adults;
		std::vector<dcon::territorial_unit_id> regions;
		std::vector<float> weights;
		for(auto const& [index, count] : adults) {
			regions.push_back(dcon::territorial_unit_id{ dcon::territorial_unit_id::value_base_t(index) });
			weights.push_back(count);
		}
		auto apportioned = largest_remainders(weights, uint32_t(seat_offices.size()));
		for(size_t r = 0; r < regions.size(); ++r) {
			auto counted = count_impl(state, nation, voters, candidates, regions[r], growth, date, campaign_amounts);
			add(total, counted);
			if(apportioned[r] > 0) allocate(counted, apportioned[r], won);
		}
	} else {
		total = count_impl(state, nation, voters, candidates, {}, growth, date, campaign_amounts);
		allocate(total, uint32_t(seat_offices.size()), won);
	}
	// The old members' terms end; the winners take the seats in list order.
	for(auto office : seat_offices) offices::vacate(state, office, date);
	std::vector<std::pair<uint16_t, dcon::organization_id>> order;
	for(auto const& [index, seats] : won) order.push_back({ seats, dcon::organization_id{ dcon::organization_id::value_base_t(index) } });
	std::stable_sort(order.begin(), order.end(), [](auto const& a, auto const& b) { return a.first > b.first; });
	size_t next_seat = 0;
	for(auto const& [seats, party] : order) {
		uint16_t filled = 0;
		for(int attempt = 0; attempt < 2 && filled < seats; ++attempt) {
			for(auto person : parties::members(state, party)) {
				if(filled >= seats || next_seat >= seat_offices.size()) break;
				if(holds_seat(state, person)) continue;
				if(!power_topology::appointment_candidate_eligible(state, seat_offices[next_seat], person, date)) continue;
				if(offices::install(state, person, seat_offices[next_seat], date)) { ++next_seat; ++filled; }
			}
			if(filled < seats) (void)parties::recruit(state, party, voters, uint32_t(parties::members(state, party).size()) + seats - filled, date);
		}
		// A topology veto or cadre restriction can leave a legally won seat
		// vacant. Reserve its list position so another party cannot inherit it.
		auto unfilled = uint32_t(seats - filled);
		next_seat = std::min(seat_offices.size(), next_seat + unfilled);
	}
	auto election = record(state, total, won, date);
	state.world.force_create_election_institution(election, chamber);
	for(auto office : offices_of(state, chamber))
		if(state.world.office_get_electoral_system(office) == uint8_t(electoral_system::presiding)) offices::vacate(state, office, date);
	fill_presiding(state, chamber, date);
	return election;
}

dcon::election_id hold(sys::state& state, dcon::office_id office, std::vector<electorate::voter> const& voters, sys::date date) {
	auto institution = institution_for_office(state, office);
	auto nation = nation_of(state, institution);
	auto system = electoral_system(state.world.office_get_electoral_system(office));
	schedule(state, office, date);
	auto candidates = contenders(state, nation, date);
	if(candidates.empty()) return {};
	tally counted;
	std::map<uint32_t, uint16_t> won;
	dcon::organization_id winner{};
	if(system == electoral_system::direct) {
		auto district = district_rule(state.world.office_get_election_districts(office)) == district_rule::own
			? territory_of(state, institution) : dcon::territorial_unit_id{};
		auto growth = incumbent_growth(state, nation, electorate::median_income(voters));
		auto campaign_amounts = political_resources::campaign_spending_by_party(state, candidates, date - 90, date);
		counted = count_impl(state, nation, voters, candidates, district, growth, date, campaign_amounts);
		std::vector<std::pair<float, dcon::organization_id>> ranking;
		for(auto party : candidates) ranking.push_back({ counted.votes[party.index()], party });
		std::stable_sort(ranking.begin(), ranking.end(), [](auto const& a, auto const& b) { return a.first > b.first; });
		winner = ranking.front().second;
		// Without a majority the top two meet in a runoff.
		if(ranking.size() > 1 && ranking.front().first <= 0.5f * counted.cast) {
			auto runoff_parties = std::vector{ranking[0].second, ranking[1].second};
			auto runoff = count_impl(state, nation, voters, runoff_parties, district, growth, date, campaign_amounts);
			winner = runoff.votes[ranking[1].second.index()] > runoff.votes[ranking[0].second.index()] ? ranking[1].second : ranking[0].second;
		}
	} else if(system == electoral_system::legislative) {
		std::map<uint32_t, uint16_t> seats;
		for(auto chamber : institutions_of(state, nation))
			if(legislature::is_chamber(state, chamber))
				for(auto const& [index, held] : seats_held(state, chamber, date)) seats[index] += held;
		winner = largest_party(state, seats);
		won = seats;
	} else {
		return {};
	}
	if(!winner) return {};
	won[winner.index()] = std::max<uint16_t>(won[winner.index()], 1);
	offices::vacate(state, office, date);
	// A directly elected office goes to the party's leader; one the legislature
	// fills goes to a senior member, the leader staying for the government.
	auto elected = system == electoral_system::direct ? parties::leader(state, winner, date)
		: senior_member(state, winner, false, office, date);
	if(!power_topology::appointment_candidate_eligible(state, office, elected, date))
		elected = senior_member(state, winner, false, office, date);
	(void)seat(state, elected, office, date);
	// A running mate comes from the winner's list.
	for(auto other : offices_of(state, institution)) {
		if(other == office || state.world.office_get_electoral_system(other) != uint8_t(electoral_system::running_mate)) continue;
		offices::vacate(state, other, date);
		auto list = parties::members(state, winner);
		for(auto person : list)
			if(person != offices::holder(state, office) && seat(state, person, other, date)) break;
	}
	auto election = record(state, counted, won, date);
	state.world.force_create_election_office(election, office);
	return election;
}

dcon::person_id senior_member(sys::state const& state, dcon::organization_id party, bool seated_only) {
	auto list = parties::members(state, party);
	for(size_t i = 1; i < list.size(); ++i) {
		bool seated = holds_seat(state, list[i]);
		auto held = persons::active_offices_of(state, list[i]);
		if(seated_only ? (seated && held.size() == 1) : held.empty()) return list[i];
	}
	return list.empty() ? dcon::person_id{} : list.front();
}

dcon::person_id senior_member(sys::state const& state, dcon::organization_id party, bool seated_only,
	dcon::office_id office, sys::date date) {
	auto list = parties::members(state, party);
	for(auto person : list) {
		bool seated = holds_seat(state, person);
		auto held = persons::active_offices_of(state, person);
		if((seated_only ? (seated && held.size() == 1) : held.empty())
			&& power_topology::appointment_candidate_eligible(state, office, person, date)) return person;
	}
	for(auto person : list)
		if(power_topology::appointment_candidate_eligible(state, office, person, date)) return person;
	return {};
}

void fill_presiding(sys::state& state, dcon::institution_id chamber, sys::date date) {
	auto largest = largest_party(state, seats_held(state, chamber, date));
	if(!largest) return;
	for(auto office : offices_of(state, chamber)) {
		if(state.world.office_get_electoral_system(office) != uint8_t(electoral_system::presiding) || !offices::vacant(state, office)) continue;
		(void)seat(state, senior_member(state, largest, true, office, date), office, date);
	}
}

std::map<uint32_t, float> last_shares(sys::state const& state, dcon::nation_id nation) {
	dcon::election_id latest{};
	state.world.for_each_election([&](dcon::election_id election) {
		auto chamber = state.world.election_get_institution_from_election_institution(election);
		if(!chamber || nation_of(state, chamber) != nation) return;
		if(!latest || state.world.election_get_held_on(latest) < state.world.election_get_held_on(election)
			|| (state.world.election_get_held_on(latest) == state.world.election_get_held_on(election) && latest.index() < election.index()))
			latest = election;
	});
	std::map<uint32_t, float> result;
	if(!latest) return result;
	double total = 0.0;
	state.world.election_for_each_election_result_election_as_election(latest, [&](auto relation) {
		auto entry = state.world.election_result_election_get_election_result(relation);
		total += state.world.election_result_get_votes(entry);
	});
	state.world.election_for_each_election_result_election_as_election(latest, [&](auto relation) {
		auto entry = state.world.election_result_election_get_election_result(relation);
		auto party = state.world.election_result_get_organization_from_election_result_party(entry);
		if(total > 0.0) result[party.index()] = float(state.world.election_result_get_votes(entry) / total);
	});
	return result;
}

bool process(sys::state& state, dcon::nation_id nation, sys::date date) {
	auto due_chamber = [&](dcon::institution_id chamber) {
		auto next = state.world.institution_get_next_election(chamber);
		return state.world.institution_get_electoral_system(chamber) != uint8_t(electoral_system::none) && next && !(date < next);
	};
	auto due_office = [&](dcon::office_id office, electoral_system system) {
		auto next = state.world.office_get_next_election(office);
		return state.world.office_get_electoral_system(office) == uint8_t(system) && next && !(date < next);
	};
	std::vector<dcon::institution_id> chambers;
	std::vector<dcon::office_id> direct, legislative;
	for(auto institution : institutions_of(state, nation)) {
		if(legislature::is_chamber(state, institution) && due_chamber(institution)) chambers.push_back(institution);
		for(auto office : offices_of(state, institution)) {
			if(due_office(office, electoral_system::direct)) direct.push_back(office);
			if(due_office(office, electoral_system::legislative)) legislative.push_back(office);
		}
	}
	if(chambers.empty() && direct.empty() && legislative.empty()) return false;
	auto voters = electorate::voters(state, nation);
	political_resources::prepare_campaigns(state, nation, voters, date);
	for(auto chamber : chambers) (void)hold(state, chamber, voters, date);
	for(auto office : direct) (void)hold(state, office, voters, date);
	for(auto office : legislative) (void)hold(state, office, voters, date);
	return !chambers.empty();
}

} // namespace governance::elections
