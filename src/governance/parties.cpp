#include "parties.hpp"

#include "system_state.hpp"
#include "actors/organizations/organizations.hpp"
#include "actors/ownership.hpp"
#include "economy/households.hpp"
#include "governance/offices.hpp"
#include "governance/power_topology.hpp"
#include "persons/exact_population.hpp"
#include "persons/persons.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>

namespace governance::parties {
namespace {

constexpr int32_t adult_age_days = 25 * 365;
constexpr int32_t old_age_days = 75 * 365;

float weight_of(electorate::voter const& value) { return value.adults * value.turnout; }

policy::position weighted_mean(std::vector<electorate::voter> const& voters, std::vector<size_t> const& chosen) {
	std::vector<std::pair<policy::position const*, float>> values;
	values.reserve(chosen.size());
	for(auto index : chosen) values.push_back({ &voters[index].ideal, weight_of(voters[index]) });
	return policy::weighted_mean(values);
}

policy::policy_value read_value(sys::state const& state, dcon::platform_position_id id, policy::topic_id topic) {
	auto spec = policy::definition(topic);
	if(!spec) return 0.0f;
	switch(spec->kind) {
	case policy::value_kind::continuous: return state.world.platform_position_get_value(id);
	case policy::value_kind::ordinal: return int32_t(state.world.platform_position_get_value(id));
	case policy::value_kind::categorical: return policy::category_value{state.world.platform_position_get_category_value(id)};
	case policy::value_kind::binary: return state.world.platform_position_get_boolean_value(id) != 0;
	case policy::value_kind::structured: return policy::structured_value{state.world.platform_position_get_value(id), state.world.platform_position_get_auxiliary(id)};
	}
	return 0.0f;
}

void write_value(sys::state& state, dcon::platform_position_id id, policy::topic_id topic, policy::policy_value const& value) {
	auto spec = policy::definition(topic);
	if(!spec) return;
	state.world.platform_position_set_value_kind(id, uint8_t(spec->kind));
	switch(spec->kind) {
	case policy::value_kind::continuous: state.world.platform_position_set_value(id, std::get<float>(value)); break;
	case policy::value_kind::ordinal: state.world.platform_position_set_value(id, float(std::get<int32_t>(value))); break;
	case policy::value_kind::categorical: state.world.platform_position_set_category_value(id, std::get<policy::category_value>(value).value); break;
	case policy::value_kind::binary: state.world.platform_position_set_boolean_value(id, std::get<bool>(value) ? 1 : 0); break;
	case policy::value_kind::structured: {
		auto parameters = std::get<policy::structured_value>(value);
		state.world.platform_position_set_value(id, parameters.first);
		state.world.platform_position_set_auxiliary(id, parameters.second);
		break;
	}
	}
}

bool recruitable(sys::state const& state, persons::person_key key, sys::date date) {
	if(!persons::exact_population::alive(state, key)) return false;
	auto age = (int32_t(date.to_raw_value()) - 1) - persons::exact_population::birth_day_index(state, key);
	if(age < adult_age_days || age > old_age_days) return false;
	// Office holders are not recruited: a monarch, judge or central banker
	// stays outside party politics.
	auto profile = persons::exact_population::profile_for_person(state, key);
	return !profile || (!state.world.person_get_organization_from_party_membership(profile)
		&& persons::active_offices_of(state, profile).empty());
}

dcon::person_id enlist(sys::state& state, dcon::organization_id party, persons::person_key key, sys::date date) {
	if(!recruitable(state, key, date)) return {};
	auto person = persons::materialize_profile(state, key);
	return person && join(state, person, party, date) ? person : dcon::person_id{};
}

// Adults of a cohort: people of the cohort's role living in its province.
uint32_t enlist_from_cohort(sys::state& state, dcon::organization_id party, dcon::organization_id cohort, uint32_t wanted, sys::date date) {
	uint32_t enlisted = 0;
	auto province = economy::households::home_of(state, cohort);
	auto role = economy::households::role_of(state, cohort);
	for(auto location : state.world.province_get_pop_location(province)) {
		auto pop = location.get_pop();
		if(enlisted >= wanted) break;
		if(economy::households::role_for_pop_type(state, pop.get_poptype()) != role) continue;
		auto cell = persons::source_population_cell_for_population(state, pop);
		auto count = cell ? persons::exact_population::literal_count_for_cell(state, cell) : 0;
		for(uint64_t ordinal = 0; ordinal < count && enlisted < wanted; ++ordinal)
			if(enlist(state, party, persons::person_key{ cell, ordinal }, date)) ++enlisted;
	}
	return enlisted;
}

} // namespace

dcon::organization_id create(sys::state& state, dcon::nation_id nation, policy::position const& position, sys::date date) {
	if(!nation) return {};
	auto party = actors::organizations::create_organization(state, actors::ownership::actor_kind::party);
	if(!party) return {};
	actors::ownership::assign_runtime_canonical_id(state, party);
	state.world.force_create_party_nation(party, nation);
	state.world.organization_set_party_founded_on(party, date);
	set_platform(state, party, position);
	return party;
}

bool is_party(sys::state const& state, dcon::organization_id organization) {
	return organization && state.world.organization_is_valid(organization)
		&& state.world.organization_get_kind(organization) == uint8_t(actors::ownership::actor_kind::party)
		&& bool(state.world.organization_get_nation_from_party_nation(organization));
}

std::vector<dcon::organization_id> parties_of(sys::state const& state, dcon::nation_id nation) {
	std::vector<dcon::organization_id> result;
	if(!nation) return result;
	state.world.nation_for_each_party_nation_as_nation(nation, [&](auto relation) {
		auto party = state.world.party_nation_get_organization(relation);
		if(is_party(state, party)) result.push_back(party);
	});
	std::sort(result.begin(), result.end(), [](auto a, auto b) { return a.index() < b.index(); });
	return result;
}

dcon::nation_id nation_of(sys::state const& state, dcon::organization_id party) {
	return party ? state.world.organization_get_nation_from_party_nation(party) : dcon::nation_id{};
}

policy::position platform(sys::state const& state, dcon::organization_id party) {
	policy::position result;
	state.world.organization_for_each_platform_position_party_as_organization(party, [&](auto relation) {
		auto item = state.world.platform_position_party_get_platform_position(relation);
		auto topic = policy::topic_id(state.world.platform_position_get_dimension(item));
		(void)policy::set(result, topic, read_value(state, item, topic));
	});
	return result;
}

void set_platform(sys::state& state, dcon::organization_id party, policy::position const& raw) {
	auto value = policy::clamp(raw);
	std::map<uint16_t, std::pair<dcon::platform_position_party_id, dcon::platform_position_id>> existing;
	state.world.organization_for_each_platform_position_party_as_organization(party, [&](auto relation) {
		auto item = state.world.platform_position_party_get_platform_position(relation);
		existing[state.world.platform_position_get_dimension(item)] = {relation, item};
	});
	for(auto const& [topic, ids] : existing) {
		if(policy::get(value, policy::topic_id(topic))) continue;
		state.world.delete_platform_position_party(ids.first);
		state.world.delete_platform_position(ids.second);
	}
	for(auto const& item : value.entries) {
		auto found = existing.find(uint16_t(item.topic));
		auto id = found == existing.end() ? dcon::platform_position_id{} : found->second.second;
		if(!id) {
			id = state.world.create_platform_position();
			state.world.platform_position_set_dimension(id, uint16_t(item.topic));
			state.world.force_create_platform_position_party(id, party);
		}
		write_value(state, id, item.topic, item.value);
	}
}

dcon::organization_id party_of(sys::state const& state, dcon::person_id person) {
	return person ? state.world.person_get_organization_from_party_membership(person) : dcon::organization_id{};
}

bool join(sys::state& state, dcon::person_id person, dcon::organization_id party, sys::date date) {
	if(!person || !persons::alive(state, person) || !is_party(state, party)) return false;
	if(party_of(state, person) == party) return true;
	leave(state, person);
	uint16_t rank = 0;
	state.world.organization_for_each_party_membership_as_organization(party, [&](auto relation) {
		rank = std::max<uint16_t>(rank, uint16_t(state.world.party_membership_get_rank(relation) + 1));
	});
	state.world.force_create_party_membership(person, party);
	auto relation = state.world.person_get_party_membership(person);
	state.world.party_membership_set_joined_on(relation, date);
	state.world.party_membership_set_rank(relation, rank);
	return true;
}

void leave(sys::state& state, dcon::person_id person) {
	if(!party_of(state, person)) return;
	state.world.delete_party_membership(state.world.person_get_party_membership(person));
}

std::vector<dcon::person_id> members(sys::state const& state, dcon::organization_id party) {
	std::vector<std::pair<uint16_t, dcon::person_id>> ranked;
	state.world.organization_for_each_party_membership_as_organization(party, [&](auto relation) {
		auto person = state.world.party_membership_get_person(relation);
		if(persons::alive(state, person)) ranked.push_back({ state.world.party_membership_get_rank(relation), person });
	});
	std::sort(ranked.begin(), ranked.end(), [](auto const& a, auto const& b) {
		return a.first != b.first ? a.first < b.first : a.second.index() < b.second.index();
	});
	std::vector<dcon::person_id> result;
	for(auto const& [rank, person] : ranked) result.push_back(person);
	return result;
}

dcon::person_id leader(sys::state const& state, dcon::organization_id party) {
	return leader(state, party, state.current_date);
}

dcon::person_id leader(sys::state const& state, dcon::organization_id party, sys::date date) {
	auto list = members(state, party);
	if(list.empty()) return {};
	auto actor = actors::organizations::actor_for_organization(state, party);
	return power_topology::nominated_member(state, power_topology::actor_node(actor), list, date);
}

void dissolve(sys::state& state, dcon::organization_id party) {
	std::vector<dcon::person_id> enrolled;
	state.world.organization_for_each_party_membership_as_organization(party, [&](auto relation) {
		enrolled.push_back(state.world.party_membership_get_person(relation));
	});
	for(auto person : enrolled) leave(state, person);
	state.world.organization_set_party_governing(party, 0);
	if(auto relation = state.world.organization_get_party_nation(party)) state.world.delete_party_nation(relation);
}

dcon::organization_id nearest(sys::state const& state, std::vector<dcon::organization_id> const& candidates, policy::position const& ideal) {
	dcon::organization_id best{};
	float best_distance = std::numeric_limits<float>::infinity();
	for(auto party : candidates) {
		auto d = policy::distance(ideal, platform(state, party));
		if(d < best_distance) {
			best = party;
			best_distance = d;
		}
	}
	return best;
}

uint32_t recruit(sys::state& state, dcon::organization_id party, std::vector<electorate::voter> const& voters, uint32_t wanted, sys::date date) {
	auto have = uint32_t(members(state, party).size());
	if(have >= wanted) return 0;
	auto target = platform(state, party);
	std::vector<size_t> order(voters.size());
	std::iota(order.begin(), order.end(), size_t(0));
	std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
		return policy::distance(voters[a].ideal, target, voters[a].issue_salience)
			< policy::distance(voters[b].ideal, target, voters[b].issue_salience);
	});
	auto rivals = parties_of(state, nation_of(state, party));
	uint32_t enlisted = 0;
	// The party's own supporters join first; then those nearest it. People with
	// an individual budget join themselves; cohorts send their adults.
	for(int pass = 0; pass < 2 && have + enlisted < wanted; ++pass)
		for(auto index : order) {
			if(have + enlisted >= wanted) break;
			auto const& value = voters[index];
		auto supporter_party = dcon::organization_id{};
		float supporter_distance = std::numeric_limits<float>::infinity();
		for(auto rival : rivals) {
			auto candidate_distance = policy::distance(value.ideal, platform(state, rival), value.issue_salience);
			if(candidate_distance < supporter_distance) { supporter_party = rival; supporter_distance = candidate_distance; }
		}
		bool supporter = supporter_party == party;
			if((pass == 0) != supporter) continue;
			auto room = wanted - have - enlisted;
			if(value.cohort) enlisted += enlist_from_cohort(state, party, value.cohort, pass == 0 ? room : std::min<uint32_t>(3, room), date);
			else if(enlist(state, party, value.person, date)) ++enlisted;
		}
	return enlisted;
}

std::vector<dcon::organization_id> found(sys::state& state, dcon::nation_id nation, std::vector<electorate::voter> const& voters,
	uint32_t members_each, sys::date date) {
	std::vector<dcon::organization_id> result;
	std::vector<size_t> all;
	for(size_t i = 0; i < voters.size(); ++i) if(weight_of(voters[i]) > 0.0f) all.push_back(i);
	if(all.empty()) return result;
	auto k = std::min(founding_parties, all.size());
	// Deterministic seeding: the weighted mean, then the voter weight that
	// lies farthest from the centroids chosen so far.
	std::vector<policy::position> centroids{ weighted_mean(voters, all) };
	while(centroids.size() < k) {
		size_t best = all.front();
		float best_score = -1.0f;
		for(auto index : all) {
			float nearest_distance = std::numeric_limits<float>::infinity();
			for(auto const& centroid : centroids) nearest_distance = std::min(nearest_distance,
				policy::distance(voters[index].ideal, centroid, voters[index].issue_salience));
			auto score = weight_of(voters[index]) * nearest_distance;
			if(score > best_score) { best = index; best_score = score; }
		}
		if(best_score <= 0.0f) break;
		centroids.push_back(voters[best].ideal);
	}
	std::vector<std::vector<size_t>> clusters(centroids.size());
	for(int iteration = 0; iteration < 25; ++iteration) {
		for(auto& cluster : clusters) cluster.clear();
		for(auto index : all) {
			size_t best = 0;
			for(size_t c = 1; c < centroids.size(); ++c)
				if(policy::distance(voters[index].ideal, centroids[c], voters[index].issue_salience)
					< policy::distance(voters[index].ideal, centroids[best], voters[index].issue_salience)) best = c;
			clusters[best].push_back(index);
		}
		for(size_t c = 0; c < centroids.size(); ++c)
			if(!clusters[c].empty()) centroids[c] = weighted_mean(voters, clusters[c]);
	}
	// All parties exist before any recruits, so each draws on its own supporters.
	for(size_t c = 0; c < centroids.size(); ++c)
		if(!clusters[c].empty())
			if(auto party = create(state, nation, centroids[c], date)) result.push_back(party);
	for(auto party : result) (void)recruit(state, party, voters, members_each, date);
	return result;
}

void evolve(sys::state& state, dcon::nation_id nation, std::vector<electorate::voter> const& voters, std::map<uint32_t, float> const& last_vote_shares,
	uint32_t members_each, sys::date date) {
	auto existing = parties_of(state, nation);
	// Parties that keep failing dissolve.
	for(auto party : existing) {
		auto share = last_vote_shares.find(party.index());
		if(share == last_vote_shares.end()) continue;
		auto weak = share->second < exit_vote_share ? uint8_t(state.world.organization_get_party_weak_elections(party) + 1) : uint8_t(0);
		state.world.organization_set_party_weak_elections(party, weak);
		if(weak >= 2) dissolve(state, party);
	}
	existing = parties_of(state, nation);
	// Platforms drift toward their own voters.
	std::vector<std::vector<size_t>> supporters(existing.size());
	std::vector<size_t> unrepresented;
	double total = 0.0, unrepresented_weight = 0.0;
	for(size_t i = 0; i < voters.size(); ++i) {
		auto weight = weight_of(voters[i]);
		if(!(weight > 0.0f)) continue;
		total += weight;
		size_t best = existing.size();
		float best_distance = std::numeric_limits<float>::infinity();
		for(size_t p = 0; p < existing.size(); ++p) {
			auto d = policy::distance(voters[i].ideal, platform(state, existing[p]), voters[i].issue_salience);
			if(d < best_distance) { best = p; best_distance = d; }
		}
		if(best < existing.size()) supporters[best].push_back(i);
		if(best_distance > representation_distance) {
			unrepresented.push_back(i);
			unrepresented_weight += weight;
		}
	}
	for(size_t p = 0; p < existing.size(); ++p) {
		if(supporters[p].empty()) continue;
		auto current = platform(state, existing[p]);
		auto target = weighted_mean(voters, supporters[p]);
		set_platform(state, existing[p], policy::move_toward(current, target, platform_drift));
	}
	// A large unrepresented bloc founds a party at its centre.
	if(total > 0.0 && unrepresented_weight / total >= entry_share)
		if(auto party = create(state, nation, weighted_mean(voters, unrepresented), date))
			(void)recruit(state, party, voters, members_each, date);
	for(auto party : parties_of(state, nation)) (void)recruit(state, party, voters, members_each, date);
}

} // namespace governance::parties
