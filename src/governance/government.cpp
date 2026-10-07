#include "government.hpp"

#include "system_state.hpp"
#include "economy/households.hpp"
#include "governance/electorate.hpp"
#include "governance/elections.hpp"
#include "governance/governance.hpp"
#include "governance/law/law.hpp"
#include "governance/legislature.hpp"
#include "governance/offices.hpp"
#include "governance/parties.hpp"
#include "governance/power_topology.hpp"
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

uint32_t total_seats(sys::state const&, dcon::organization_id, dcon::nation_id, sys::date);

policy::policy_value stored_platform_value(sys::state const& state, dcon::platform_position_id item,
	policy::topic_id topic) {
	auto spec = policy::definition(topic);
	if(!spec) return 0.0f;
	switch(spec->kind) {
	case policy::value_kind::continuous: return state.world.platform_position_get_value(item);
	case policy::value_kind::ordinal: return int32_t(state.world.platform_position_get_value(item));
	case policy::value_kind::categorical: return policy::category_value{state.world.platform_position_get_category_value(item)};
	case policy::value_kind::binary: return state.world.platform_position_get_boolean_value(item) != 0;
	case policy::value_kind::structured: return policy::structured_value{state.world.platform_position_get_value(item), state.world.platform_position_get_auxiliary(item)};
	}
	return 0.0f;
}

void save_agreement_value(sys::state& state, dcon::coalition_agreement_id agreement,
	policy::topic_id topic, policy::policy_value const& value) {
	auto spec = policy::definition(topic);
	if(!spec) return;
	auto item = state.world.create_platform_position();
	state.world.platform_position_set_dimension(item, uint16_t(topic));
	state.world.platform_position_set_value_kind(item, uint8_t(spec->kind));
	switch(spec->kind) {
	case policy::value_kind::continuous: state.world.platform_position_set_value(item, std::get<float>(value)); break;
	case policy::value_kind::ordinal: state.world.platform_position_set_value(item, float(std::get<int32_t>(value))); break;
	case policy::value_kind::categorical: state.world.platform_position_set_category_value(item, std::get<policy::category_value>(value).value); break;
	case policy::value_kind::binary: state.world.platform_position_set_boolean_value(item, std::get<bool>(value) ? 1 : 0); break;
	case policy::value_kind::structured: {
		auto parameters = std::get<policy::structured_value>(value);
		state.world.platform_position_set_value(item, parameters.first);
		state.world.platform_position_set_auxiliary(item, parameters.second);
		break;
	}
	}
	state.world.force_create_coalition_agreement_position(item, agreement);
}

dcon::coalition_agreement_id latest_agreement(sys::state const& state, dcon::nation_id nation) {
	dcon::coalition_agreement_id result{};
	state.world.nation_for_each_coalition_agreement_nation_as_nation(nation, [&](auto relation) {
		auto candidate = state.world.coalition_agreement_nation_get_agreement(relation);
		if(!result || state.world.coalition_agreement_get_started_on(result) < state.world.coalition_agreement_get_started_on(candidate)
			|| (state.world.coalition_agreement_get_started_on(result) == state.world.coalition_agreement_get_started_on(candidate)
				&& candidate.index() < result.index())) result = candidate;
	});
	return result;
}

std::vector<dcon::organization_id> agreement_members(sys::state const& state, dcon::coalition_agreement_id agreement) {
	std::vector<dcon::organization_id> result;
	if(!agreement) return result;
	state.world.coalition_agreement_for_each_coalition_agreement_member_as_agreement(agreement, [&](auto relation) {
		result.push_back(state.world.coalition_agreement_member_get_party(relation));
	});
	std::sort(result.begin(), result.end(), [](auto a, auto b) { return a.index() < b.index(); });
	return result;
}

policy::position agreement_position(sys::state const& state, dcon::coalition_agreement_id agreement) {
	policy::position result;
	if(!agreement) return result;
	state.world.coalition_agreement_for_each_coalition_agreement_position_as_agreement(agreement, [&](auto relation) {
		auto item = state.world.coalition_agreement_position_get_platform_position(relation);
		auto topic = policy::topic_id(state.world.platform_position_get_dimension(item));
		(void)policy::set(result, topic, stored_platform_value(state, item, topic));
	});
	return result;
}

policy::position negotiate_program(sys::state const& state, dcon::nation_id nation,
	std::vector<dcon::organization_id> const& governing) {
	policy::position result;
	auto chamber = confidence_chamber(state, nation);
	std::map<uint32_t, float> leverage;
	uint32_t filled = chamber ? legislature::filled_seats(state, chamber, state.current_date) : 0;
	uint32_t held = 0;
	for(auto party : governing) held += chamber ? seats_of(state, party, chamber, state.current_date) : total_seats(state, party, nation, state.current_date);
	for(auto party : governing) {
		auto seats = chamber ? seats_of(state, party, chamber, state.current_date) : total_seats(state, party, nation, state.current_date);
		auto critical = chamber && float(held - std::min(held, seats)) <= legislature::simple_majority * float(filled);
		leverage[party.index()] = float(std::max<uint32_t>(seats, 1)) * (critical ? 1.5f : 1.0f);
	}
	for(auto const& spec : policy::topics()) {
		std::vector<std::pair<policy::policy_value, float>> options;
		for(auto party : governing)
			if(auto value = policy::get(parties::platform(state, party), spec.id))
				options.push_back({*value, leverage[party.index()]});
		if(options.empty()) continue;
		auto score = [&](policy::policy_value const& candidate) {
			policy::position point;
			(void)policy::set(point, spec.id, candidate);
			double total = 0.0;
			for(auto const& option : options) {
				policy::position other;
				(void)policy::set(other, spec.id, option.first);
				total += option.second * policy::distance(point, other);
			}
			return total;
		};
		auto best = options.front().first;
		auto best_score = score(best);
		for(auto const& option : options) {
			auto candidate_score = score(option.first);
			if(candidate_score < best_score) { best = option.first; best_score = candidate_score; }
		}
		(void)policy::set(result, spec.id, best);
	}
	return result;
}

void record_coalition_agreement(sys::state& state, dcon::nation_id nation,
	std::vector<dcon::organization_id> const& governing, sys::date date) {
	if(governing.empty()) return;
	auto agreement = state.world.create_coalition_agreement();
	state.world.coalition_agreement_set_started_on(agreement, date);
	state.world.force_create_coalition_agreement_nation(agreement, nation);
	for(auto party : governing) state.world.force_create_coalition_agreement_member(agreement, party);
	for(auto const& item : negotiate_program(state, nation, governing).entries)
		save_agreement_value(state, agreement, item.topic, item.value);
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
		if(!power_topology::appointment_candidate_eligible(state, office, candidate, date)) continue;
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
	if(!governing.empty()) {
		auto agreement = latest_agreement(state, nation);
		auto recorded_members = agreement_members(state, agreement);
		if(recorded_members == governing) return agreement_position(state, agreement);
		return negotiate_program(state, nation, governing);
	}
	// A government without a party base serves the wealthiest tenth.
	auto voters = electorate::voters(state, nation);
	std::sort(voters.begin(), voters.end(), [](auto const& a, auto const& b) { return a.wealth > b.wealth; });
	double adults = 0.0, all = 0.0;
	for(auto const& value : voters) all += value.adults;
	std::vector<std::pair<policy::position const*, float>> values;
	for(auto const& value : voters) {
		if(adults >= 0.1 * all && adults > 0.0) break;
		values.push_back({&value.ideal, value.adults});
		adults += value.adults;
	}
	return policy::weighted_mean(values);
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
		auto candidate = parties::leader(state, formateur, date);
		auto current = holder_on(state, chief, date);
		if(current != candidate) {
			// The outgoing chief executive resigns for the new government.
			if(current) (void)offices::resign(state, current, chief, date);
			auto appointer = holder_on(state, rules.appointer, date);
			std::vector<dcon::person_id> candidates;
			if(candidate) candidates.push_back(candidate);
			for(auto member : parties::members(state, formateur))
				if(member != candidate) candidates.push_back(member);
			if(!appointer || !appoint_first(state, chief, appointer, candidates, date)) return false;
		}
	}
	if(!governing.empty()) record_coalition_agreement(state, nation, governing, date);
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
					if(!seated && power_topology::appointment_candidate_eligible(state, office, member, date)
						&& offices::install(state, member, office, date)) break;
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
	policy::position target;
	(void)policy::current(state, nation, date, target);
	for(auto const& item : program(state, nation).entries) (void)policy::set(target, item.topic, item.value);
	target = policy::clamp(std::move(target));
	if(policy::law_matches(state, nation, target, date)) return;
	auto next_bill = state.world.nation_get_next_policy_bill_date(nation);
	if(next_bill && date < next_bill) return;

	auto instrument = law::create_draft_instrument(state, law::legal_instrument_kind::statute, nation);
	if(!instrument) return;
	auto fiscal = policy::rules_for(state, nation, target);
	bool valid_bill = true;
	for(uint8_t bucket = 0; bucket < 3; ++bucket)
		valid_bill = law::add_rule(state, instrument, { law::policy_rule_kind::income_tax_rate, {}, fiscal.tax_rates[bucket], bucket }) && valid_bill;
	valid_bill = law::add_rule(state, instrument,
		{ law::policy_rule_kind::disbursement_rate, {}, policy::disbursement_rate }) && valid_bill;
	for(auto const& [recipient, share] : fiscal.shares)
		valid_bill = law::add_rule(state, instrument,
			{ law::policy_rule_kind::appropriation_share, {}, share, 0, recipient }) && valid_bill;
	for(auto const& item : target.entries) {
		auto spec = policy::definition(item.topic);
		if(spec && spec->enacted_by == policy::implementation::statute)
			valid_bill = law::add_topic_rule(state, instrument, item.topic, item.value) && valid_bill;
	}
	if(!valid_bill) return;

	// Each member compares the bill with current law using their own economic
	// exposure. Party and coalition positions add discipline, but ordinary
	// policy motions leave room for deterministic defections.
	auto voters = electorate::voters(state, nation);
	auto prior = policy::position{};
	(void)policy::current(state, nation, date, prior);
	auto agreement = program(state, nation);
	auto chambers = legislature::deciding_chambers(state, authority_kind::legislate, national(nation), date);
	for(auto chamber : chambers)
		for(auto seat : legislature::seats_of(state, chamber)) {
			auto member = holder_on(state, seat, date);
			if(!member) continue;
			policy::position const* own_position = nullptr;
			policy::salience const* own_salience = nullptr;
			auto key = persons::canonical_key(state, member);
			for(auto const& voter : voters)
				if(voter.person.source_population_cell != 0 && voter.person == key) {
					own_position = &voter.ideal; own_salience = &voter.issue_salience; break;
				}
			if(!own_position) {
				auto home = persons::home_site(state, member);
				auto province = home ? state.world.site_get_province_from_site_location(home) : dcon::province_id{};
				auto role = economy::households::role_for_pop_type(state, persons::pop_type(state, key));
				auto household = economy::households::household_for(state, province, role);
				for(auto const& voter : voters)
					if(voter.cohort == household) { own_position = &voter.ideal; own_salience = &voter.issue_salience; break; }
			}
			auto party = parties::party_of(state, member);
			auto platform = party ? parties::platform(state, party) : policy::position{};
			auto support = [&](policy::position const& ideal, policy::salience const& salience) {
				return policy::distance(ideal, prior, salience) - policy::distance(ideal, target, salience);
			};
			float utility = own_position && own_salience ? 0.65f * support(*own_position, *own_salience) : 0.0f;
			if(party) utility += 0.25f * support(platform, {});
			if(party && state.world.organization_get_party_governing(party)) utility += 0.10f * support(agreement, {});
			(void)legislature::vote_on_instrument(state, member, chamber, instrument,
				legislature::motion_kind::enact, utility >= -1.0e-5f, date);
		}

	state.world.nation_set_next_policy_bill_date(nation, date + 30);
	dcon::person_id enactor{};
	for(auto chamber : chambers)
		for(auto seat : legislature::seats_of(state, chamber))
			if(auto candidate = holder_on(state, seat, date)) { enactor = candidate; break; }
	if(!enactor)
		for(auto institution : institutions_of(state, nation))
			for(auto office : offices_of(state, institution))
				if(has_authority(state, office, authority_kind::legislate, national(nation), date))
					if(auto candidate = holder_on(state, office, date)) { enactor = candidate; break; }
	if(!enactor) return;

	bool assent_ok = true;
	for(auto institution : institutions_of(state, nation))
		for(auto office : offices_of(state, institution))
			if(has_authority(state, office, authority_kind::assent, national(nation), date)) {
				auto holder = holder_on(state, office, date);
				if(!holder || !law::authorized_assent(state, holder, instrument, date)) assent_ok = false;
			}
	if(!assent_ok) return;
	(void)law::authorized_enact(state, enactor, instrument, date, date + 1);
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
