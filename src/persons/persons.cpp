#include "persons.hpp"

#include "actors/ownership.hpp"
#include "economy/exact_person_economy.hpp"
#include "economy/physical/labor_dynamics.hpp"
#include "governance/governance.hpp"
#include "military/land_forces.hpp"
#include "persons/exact_population.hpp"
#include "system_state.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace persons {

namespace {

bool tenure_closable_by(sys::state const& state, dcon::person_id person, sys::date date) {
	for(auto office : active_offices_of(state, person)) {
		auto tenure = active_tenure_for(state, office);
		if(tenure && state.world.office_tenure_get_started_on(tenure) > date) return false;
	}
	return true;
}

bool mark_profile_dead(sys::state& state, dcon::person_id profile, sys::date date) {
	if(!profile || !state.world.person_is_valid(profile) || !state.world.person_get_alive(profile)
		|| !born_on_or_before(state, profile, date) || !tenure_closable_by(state, profile, date)) return false;
	auto offices = active_offices_of(state, profile);
	state.world.person_set_alive(profile, uint8_t(0));
	state.world.person_set_death_date(profile, date);
	for(auto office : offices) (void)remove_from_office(state, office, date);
	return true;
}

bool contains_key(std::vector<exact_population::person_range> const& ranges, person_key key) {
	return std::any_of(ranges.begin(), ranges.end(), [&](auto const& range) {
		return range.source_population_cell == key.source_population_cell
			&& key.ordinal >= range.first_ordinal && key.ordinal - range.first_ordinal < range.count;
	});
}

void checksum_byte(uint64_t& hash, uint8_t value) {
	hash ^= value;
	hash *= 1099511628211ULL;
}

template<typename T>
void checksum_integer(uint64_t& hash, T value) {
	using unsigned_t = std::make_unsigned_t<T>;
	auto bits = uint64_t(static_cast<unsigned_t>(value));
	for(std::size_t i = 0; i < sizeof(T); ++i) {
		checksum_byte(hash, uint8_t(bits & uint64_t(0xff)));
		bits >>= 8;
	}
}

void checksum_key(uint64_t& hash, person_key key) {
	checksum_integer(hash, key.source_population_cell);
	checksum_integer(hash, key.ordinal);
}

bool key_less(person_key a, person_key b) {
	return a.source_population_cell == b.source_population_cell ? a.ordinal < b.ordinal
		: a.source_population_cell < b.source_population_cell;
}

} // namespace

bool can_kill_person(sys::state const& state, person_key key, sys::date date, death_cause cause) {
	if(!exact_population::exists(state, key) || !exact_population::alive(state, key)
		|| !date || uint8_t(cause) > uint8_t(death_cause::attrition)
		|| int32_t(date.to_raw_value()) - 1 < exact_population::birth_day_index(state, key)) return false;
	auto profile = exact_population::profile_for_person(state, key);
	return !profile || (born_on_or_before(state, profile, date) && tenure_closable_by(state, profile, date));
}

bool kill_person(sys::state& state, person_key key, sys::date date, death_cause cause,
	bool project_population) {
	if(!can_kill_person(state, key, date, cause)) return false;
	auto profile = exact_population::profile_for_person(state, key);
	if(!military::land_forces::close_person_assignment_on_death(state, key)) return false;
	economy::physical::labor_dynamics::close_person_relations_on_death(state, key, date);
	if(!exact_population::retire_person(state, key, int32_t(date.to_raw_value()) - 1, uint8_t(cause))) return false;
	if(profile && !mark_profile_dead(state, profile, date)) return false;
	if(project_population && !exact_population::project_population_membership(state)) return false;
	return true;
}

dcon::person_id create_person_with_birth_day(sys::state& s, birth_day_index_t birth_day) {
	return exact_population::create_individual_person(s, birth_day);
}

namespace detail {

dcon::person_id create_profile_record_with_birth_day(sys::state& s, birth_day_index_t birth_day) {
	auto p = s.world.create_person();
	s.world.person_set_birth_day_index(p, birth_day);
	s.world.person_set_birth_day_known(p, uint8_t(1));
	// sys::date is retained as a save/API compatibility mirror. It cannot
	// represent dates before the simulation base, so the signed index above is
	// authoritative for all age-sensitive behavior.
	if(birth_day >= 0 && birth_day <= int32_t(std::numeric_limits<uint16_t>::max() - 1))
		s.world.person_set_birth_date(p, sys::date{uint16_t(birth_day)});
	else
		s.world.person_set_birth_date(p, sys::date{});
	s.world.person_set_alive(p, uint8_t(1));
	auto actor = s.world.create_economic_actor();
	s.world.economic_actor_set_kind(actor, uint8_t(actors::ownership::actor_kind::person));
	s.world.force_create_person_actor(p, actor);
	return p;
}

} // namespace detail

dcon::person_id create_person(sys::state& s, sys::date birth_date) {
	return create_person_with_birth_day(s, birth_date ? birth_date.to_raw_value() - 1 : 0);
}

dcon::economic_actor_id actor_for_person(sys::state const& s, dcon::person_id p) {
	return p ? s.world.person_get_economic_actor_from_person_actor(p) : dcon::economic_actor_id{};
}

bool alive(sys::state const& state, dcon::person_id person) {
	if(!person || !state.world.person_is_valid(person)) return false;
	if(state.exact_population) {
		auto key = exact_population::person_for_profile(state, person);
		if(key.source_population_cell != 0) return exact_population::alive(state, key);
		if(state.world.person_get_source_population_cell(person) != 0) return false;
	}
	return state.world.person_get_alive(person) != 0;
}

birth_day_index_t birth_day_index(sys::state const& s, dcon::person_id person) {
	if(!person || !s.world.person_is_valid(person)) return 0;
	if(s.exact_population) {
		auto key = exact_population::person_for_profile(s, person);
		if(key.source_population_cell != 0) return exact_population::birth_day_index(s, key);
		if(s.world.person_get_source_population_cell(person) != 0) return 0;
	}
	if(s.world.person_get_birth_day_known(person)) return s.world.person_get_birth_day_index(person);
	// Legacy saves may predate the signed field. Derive it once from the
	// compatibility mirror; new persons always use the signed field above.
	auto legacy_date = s.world.person_get_birth_date(person);
	return legacy_date ? legacy_date.to_raw_value() - 1 : 0;
}

bool has_birth_day(sys::state const& s, dcon::person_id person) {
	if(!person || !s.world.person_is_valid(person)) return false;
	if(s.exact_population && s.world.person_get_source_population_cell(person) != 0)
		return exact_population::person_for_profile(s, person).source_population_cell != 0;
	return s.world.person_get_birth_day_known(person) || bool(s.world.person_get_birth_date(person));
}

int32_t age_days(sys::state const& s, dcon::person_id person) {
	if(!has_birth_day(s, person) || !s.current_date) return -1;
	return (s.current_date.to_raw_value() - 1) - birth_day_index(s, person);
}

int32_t age_years(sys::state const& s, dcon::person_id person) {
	auto days = age_days(s, person);
	return days < 0 ? -1 : days / 365;
}

bool born_on_or_before(sys::state const& s, dcon::person_id person, sys::date date) {
	return has_birth_day(s, person) && bool(date)
		&& (date.to_raw_value() - 1) >= birth_day_index(s, person);
}

bool is_work_eligible(sys::state const& s, dcon::person_id person) {
	if(!alive(s, person)) return false;
	auto age = age_days(s, person);
	return age >= policy::minimum_working_age_days && age < policy::maximum_working_age_days;
}

dcon::office_tenure_id active_tenure_for(sys::state const& s, dcon::office_id office) {
	dcon::office_tenure_id result{};
	if(!office) return result;
	s.world.office_for_each_office_tenure_office_as_office(office, [&](dcon::office_tenure_office_id relation) {
		auto tenure = s.world.office_tenure_office_get_tenure(relation);
		if(s.world.office_tenure_get_active(tenure)) result = tenure;
	});
	return result;
}

dcon::office_tenure_id appoint_person(sys::state& s, dcon::person_id person, dcon::office_id office, sys::date date) {
	if(!person || !office || !alive(s, person) || !born_on_or_before(s, person, date)) return {};
	if(auto active = active_tenure_for(s, office)) {
		auto occupant = s.world.office_tenure_get_person_from_office_tenure_person(active);
		return occupant == person ? active : dcon::office_tenure_id{};
	}
	auto tenure = s.world.create_office_tenure();
	s.world.office_tenure_set_started_on(tenure, date);
	s.world.office_tenure_set_active(tenure, uint8_t(1));
	s.world.force_create_office_tenure_person(tenure, person);
	s.world.force_create_office_tenure_office(tenure, office);
	return tenure;
}

bool remove_from_office(sys::state& s, dcon::office_id office, sys::date date) {
	auto tenure = active_tenure_for(s, office);
	if(!tenure || date < s.world.office_tenure_get_started_on(tenure)) return false;
	s.world.office_tenure_set_active(tenure, uint8_t(0));
	s.world.office_tenure_set_ended_on(tenure, date);
	return true;
}

dcon::person_id occupant_of(sys::state const& s, dcon::office_id office) {
	auto tenure = active_tenure_for(s, office);
	return tenure ? s.world.office_tenure_get_person_from_office_tenure_person(tenure) : dcon::person_id{};
}

std::vector<dcon::office_id> active_offices_of(sys::state const& s, dcon::person_id person) {
	std::vector<dcon::office_id> result;
	if(!person) return result;
	s.world.person_for_each_office_tenure_person_as_person(person, [&](dcon::office_tenure_person_id relation) {
		auto tenure = s.world.office_tenure_person_get_tenure(relation);
		if(s.world.office_tenure_get_active(tenure)) result.push_back(s.world.office_tenure_get_office_from_office_tenure_office(tenure));
	});
	return result;
}

template<typename Scope>
bool has_authority_via_offices(sys::state const& s, dcon::person_id person, governance::authority_kind kind, Scope scope) {
	for(auto office : active_offices_of(s, person)) {
		if(governance::has_authority(s, office, kind, scope)) return true;
	}
	return false;
}

bool person_has_authority(sys::state const& s, dcon::person_id person, governance::authority_kind kind, dcon::nation_id nation) {
	return has_authority_via_offices(s, person, kind, nation);
}

dcon::office_tenure_id authority_tenure_on_or_before(sys::state const& s, dcon::person_id person,
	governance::authority_kind kind, dcon::nation_id nation, sys::date date) {
	for(auto office : active_offices_of(s, person)) {
		auto tenure = active_tenure_for(s, office);
		if(tenure && s.world.office_tenure_get_started_on(tenure) <= date && governance::has_authority(s, office, kind, nation)) return tenure;
	}
	return {};
}

bool person_has_authority(sys::state const& s, dcon::person_id person, governance::authority_kind kind, dcon::territorial_unit_id territorial_unit) {
	return has_authority_via_offices(s, person, kind, territorial_unit);
}

bool mark_dead(sys::state& s, dcon::person_id person, sys::date date) {
	auto key = s.exact_population ? exact_population::person_for_profile(s, person) : person_key{};
	if(key.source_population_cell != 0) return kill_person(s, key, date, death_cause::unspecified);
	if(person && s.world.person_is_valid(person)
		&& s.world.person_get_source_population_cell(person) != 0) return false;
	return mark_profile_dead(s, person, date);
}

bool exists(sys::state const& state, person_key key) {
	return exact_population::exists(state, key);
}

bool alive(sys::state const& state, person_key key) {
	return exact_population::alive(state, key);
}

birth_day_index_t birth_day_index(sys::state const& state, person_key key) {
	return exact_population::birth_day_index(state, key);
}

int32_t age_days(sys::state const& state, person_key key, sys::date date) {
	return exact_population::age_days(state, key, date);
}

int32_t age_years(sys::state const& state, person_key key, sys::date date) {
	return exact_population::age_years(state, key, date);
}

int32_t death_day_index(sys::state const& state, person_key key) {
	return exact_population::death_day_index(state, key);
}

dcon::pop_id current_population(sys::state const& state, person_key key) {
	if(!alive(state, key)) return {};
	return exact_population::current_population_for_person(state, key);
}

uint32_t current_population_cell(sys::state const& state, person_key key) {
	if(!alive(state, key)) return 0;
	return exact_population::current_population_cell(state, key);
}

dcon::site_id home_site(sys::state const& state, person_key key) {
	return exact_population::home_site(state, key);
}

dcon::site_id home_site(sys::state const& state, dcon::person_id profile) {
	if(!profile || !state.world.person_is_valid(profile)) return {};
	if(state.exact_population) {
		auto key = exact_population::person_for_profile(state, profile);
		if(key.source_population_cell != 0) return exact_population::home_site(state, key);
		if(state.world.person_get_source_population_cell(profile) != 0) return {};
	}
	return state.world.person_get_site_from_person_home_site(profile);
}

dcon::pop_type_id pop_type(sys::state const& state, person_key key) {
	return exact_population::current_pop_type(state, key);
}

dcon::culture_id current_culture(sys::state const& state, person_key key) {
	return exact_population::current_culture(state, key);
}

dcon::religion_id current_religion(sys::state const& state, person_key key) {
	return exact_population::current_religion(state, key);
}

dcon::culture_id source_culture(sys::state const& state, person_key key) {
	return exact_population::source_culture(state, key);
}

dcon::religion_id source_religion(sys::state const& state, person_key key) {
	return exact_population::source_religion(state, key);
}

dcon::pop_type_id source_pop_type(sys::state const& state, person_key key) {
	return exact_population::source_pop_type(state, key);
}

bool is_source_workforce_anchor(sys::state const& state, person_key key) {
	return exact_population::is_source_workforce_anchor(state, key);
}

person_key first_living_person_in_population(sys::state const& state, dcon::pop_id population) {
	return exact_population::first_living_person_in_population(state, population);
}

bool register_population_cell(sys::state& state, dcon::pop_id population, dcon::site_id home) {
	auto result = exact_population::register_population_cell(state, population, home).result;
	return result == exact_population::status::created || result == exact_population::status::already_registered;
}

uint32_t source_population_cell_for_population(sys::state const& state, dcon::pop_id population) {
	return exact_population::source_cell_for_population(state, population);
}

dcon::pop_id population_for_source_cell(sys::state const& state, uint32_t source_cell) {
	return exact_population::population_for_source_cell(state, source_cell);
}

uint64_t living_people_in_population_cell(sys::state const& state, uint32_t source_cell) {
	return exact_population::living_people_in_population_cell(state, source_cell);
}

bool project_population_membership(sys::state& state) {
	return exact_population::project_population_membership(state);
}

void retire_population_cell(sys::state& state, dcon::pop_id population) {
	exact_population::retire_population_cell(state, population);
}

population_transfer_result transfer_population(sys::state& state, dcon::pop_id source,
	dcon::pop_id destination, float amount, population_transition_cause cause) {
	auto result = exact_population::transfer_population_membership(state, source, destination, amount, cause);
	return {result.people_moved, result.complete};
}

bool set_home_site(sys::state& state, person_key key, dcon::site_id site) {
	if(!exact_population::set_home_site_storage(state, key, site)) return false;
	auto profile = exact_population::profile_for_person(state, key);
	if(profile && state.world.person_is_valid(profile)) {
		auto relation = dcon::person_home_site_id{dcon::person_home_site_id::value_base_t(profile.index())};
		if(state.world.person_home_site_is_valid(relation))
			state.world.person_set_site_from_person_home_site(profile, site);
		else
			state.world.force_create_person_home_site(profile, site);
	}
	return true;
}

bool transfer_population_membership(sys::state& state, person_key key, dcon::pop_id destination,
	population_transition_cause cause) {
	if(!exact_population::exists(state, key)) return false;
	if(!exact_population::transfer_population_person_membership(state, key, destination, cause)) return false;
	auto profile = exact_population::profile_for_person(state, key);
	if(profile) {
		auto home = exact_population::home_site(state, key);
		auto relation = dcon::person_home_site_id{dcon::person_home_site_id::value_base_t(profile.index())};
		if(home && state.world.person_home_site_is_valid(relation))
			state.world.person_set_site_from_person_home_site(profile, home);
		else if(home)
			state.world.force_create_person_home_site(profile, home);
		else if(state.world.person_home_site_is_valid(relation))
			state.world.person_remove_person_home_site(profile);
	}
	return true;
}

int64_t adjust_population_size(sys::state& state, dcon::pop_id pop, double size_delta, death_cause cause) {
	if(!pop || !state.world.pop_is_valid(pop) || !std::isfinite(size_delta)) return 0;
	auto death_day = state.current_date ? int32_t(state.current_date.to_raw_value()) - 1 : 0;
	auto adjusted = exact_population::adjust_population_size_storage(state, pop, size_delta,
		death_day, uint8_t(cause));
	if(adjusted.deaths.empty()) return adjusted.changed;
	auto date = state.current_date ? state.current_date : sys::date{uint16_t(death_day + 1)};
	auto economy_snapshot = economy::exact_person_economy::export_snapshot(state);
	std::unordered_set<person_key, person_key_hash> affected_workers;
	for(auto const& application : economy_snapshot.applications)
		if(application.status == economy::exact_person_economy::application_status::pending
			&& contains_key(adjusted.deaths, application.worker)) affected_workers.insert(application.worker);
	for(auto const& contract : economy_snapshot.contracts)
		if(contract.status == economy::exact_person_economy::contract_status::active
			&& contains_key(adjusted.deaths, contract.worker)) affected_workers.insert(contract.worker);
	for(auto worker : affected_workers)
		economy::physical::labor_dynamics::close_person_relations_on_death(state, worker, date);
	auto population_snapshot = exact_population::export_snapshot(state);
	for(auto const& bridge : population_snapshot.bridges) {
		if(!contains_key(adjusted.deaths, bridge.key) || !bridge.profile
			|| !state.world.person_is_valid(bridge.profile)) continue;
		auto day = exact_population::death_day_index(state, bridge.key);
		auto profile_date = day >= 0 && day <= int32_t(std::numeric_limits<uint16_t>::max() - 1)
			? sys::date{uint16_t(day + 1)} : sys::date{};
		(void)mark_profile_dead(state, bridge.profile, profile_date);
	}
	return adjusted.changed;
}

dcon::person_id materialized_profile(sys::state const& state, person_key key) {
	return exact_population::profile_for_person(state, key);
}

dcon::person_id materialize_profile(sys::state& state, person_key key) {
	return exact_population::materialize_person_profile(state, key);
}

person_key canonical_key(sys::state const& state, dcon::person_id profile) {
	return state.exact_population ? exact_population::person_for_profile(state, profile) : person_key{};
}

uint64_t materialized_profile_count(sys::state const& state) {
	return exact_population::bridge_count(state);
}

bool validate_person_kernel(sys::state const& state) {
	if(!state.exact_population || !state.exact_person_economy) return false;
	auto catalog = exact_population::export_snapshot(state);
	std::unordered_map<uint32_t, uint64_t> literal_counts;
	for(auto const& cell : catalog.cells)
		if(cell.source_population_cell == 0 || !literal_counts.emplace(cell.source_population_cell, cell.literal_count).second) return false;
	std::unordered_map<uint32_t, uint64_t> next_ordinal;
	for(auto const& range : catalog.membership_ranges) {
		if(range.identity_population_cell == 0 || range.current_population_cell == 0 || range.count == 0
			|| !literal_counts.contains(range.identity_population_cell)
			|| !literal_counts.contains(range.current_population_cell)) return false;
		auto& cursor = next_ordinal[range.identity_population_cell];
		if(range.first_ordinal != cursor || range.count > literal_counts.at(range.identity_population_cell) - cursor) return false;
		cursor += range.count;
	}
	for(auto const& [cell, count] : literal_counts)
		if(next_ordinal[cell] != count) return false;
	for(auto const& bridge : catalog.bridges) {
		auto const profile_ordinal = bridge.key.ordinal <= uint64_t(std::numeric_limits<uint32_t>::max())
			? uint32_t(bridge.key.ordinal) : 0u;
		if(!bridge.profile || !state.world.person_is_valid(bridge.profile)
			|| canonical_key(state, bridge.profile) != bridge.key
			|| state.world.person_get_source_population_cell(bridge.profile) != bridge.key.source_population_cell
			|| state.world.person_get_source_population_ordinal(bridge.profile) != profile_ordinal
			|| state.world.person_get_source_culture(bridge.profile) != exact_population::source_culture(state, bridge.key)
			|| state.world.person_get_source_religion(bridge.profile) != exact_population::source_religion(state, bridge.key)
			|| state.world.person_get_source_pop_type(bridge.profile) != exact_population::source_pop_type(state, bridge.key)
			|| !state.world.person_get_birth_day_known(bridge.profile)
			|| state.world.person_get_birth_day_index(bridge.profile) != birth_day_index(state, bridge.key)
			|| bool(state.world.person_get_alive(bridge.profile)) != alive(state, bridge.key)
			|| state.world.person_get_site_from_person_home_site(bridge.profile) != home_site(state, bridge.key)) return false;
		auto death_day = death_day_index(state, bridge.key);
		if(alive(state, bridge.key)) {
			if(death_day != std::numeric_limits<int32_t>::min()) return false;
		} else {
			if(death_day == std::numeric_limits<int32_t>::min()) return false;
			auto expected = death_day >= 0 && death_day <= int32_t(std::numeric_limits<uint16_t>::max() - 1)
				? sys::date{uint16_t(death_day + 1)} : sys::date{};
			if(state.world.person_get_death_date(bridge.profile) != expected) return false;
		}
	}
	for(auto const& retired : catalog.retired_people) {
		auto cursor = retired.first_ordinal;
		auto end = retired.first_ordinal + retired.count;
		for(auto const& death : catalog.deaths) {
			if(death.source_population_cell != retired.source_population_cell
				|| death.first_ordinal + death.count <= cursor) continue;
			if(death.first_ordinal > cursor) break;
			cursor = std::min(end, death.first_ordinal + death.count);
			if(cursor == end) break;
		}
		if(cursor != end) return false;
	}
	for(auto const& death : catalog.deaths) {
		bool covered = false;
		for(auto const& retired : catalog.retired_people)
			if(retired.source_population_cell == death.source_population_cell
				&& death.first_ordinal >= retired.first_ordinal
				&& death.first_ordinal + death.count <= retired.first_ordinal + retired.count) {
				covered = true;
				break;
			}
		if(!covered) return false;
	}
	auto economy_snapshot = economy::exact_person_economy::export_snapshot(state);
	std::unordered_map<person_key, uint32_t, person_key_hash> active_contract_count;
	for(auto const& account : economy_snapshot.accounts)
		if(!exists(state, account.owner)) return false;
	for(auto const& application : economy_snapshot.applications)
		if(!exists(state, application.worker)
			|| (application.status == economy::exact_person_economy::application_status::pending
				&& !alive(state, application.worker))) return false;
	for(auto const& contract : economy_snapshot.contracts) {
		if(!exists(state, contract.worker)) return false;
		if(contract.status == economy::exact_person_economy::contract_status::active
			&& (!alive(state, contract.worker) || ++active_contract_count[contract.worker] > 1)) return false;
	}
	return true;
}

uint64_t person_kernel_checksum(sys::state const& state) {
	auto catalog = exact_population::export_snapshot(state);
	uint64_t hash = 14695981039346656037ULL;
	checksum_integer(hash, catalog.bootstrap_version);
	std::sort(catalog.cells.begin(), catalog.cells.end(), [](auto const& a, auto const& b) {
		return a.source_population_cell < b.source_population_cell;
	});
	for(auto const& cell : catalog.cells) {
		checksum_integer(hash, cell.source_population_cell);
		checksum_integer(hash, cell.literal_count);
		checksum_integer(hash, cell.bootstrap_version);
		checksum_integer(hash, cell.demographic_policy);
		checksum_integer(hash, cell.bootstrap_base_day);
		checksum_integer(hash, cell.demographic_seed);
		checksum_integer(hash, cell.home_site.index());
		checksum_integer(hash, cell.source_culture.index());
		checksum_integer(hash, cell.source_religion.index());
		checksum_integer(hash, cell.source_pop_type.index());
	}
	std::sort(catalog.retired_people.begin(), catalog.retired_people.end(), [](auto const& a, auto const& b) {
		return std::tie(a.source_population_cell, a.first_ordinal, a.count)
			< std::tie(b.source_population_cell, b.first_ordinal, b.count);
	});
	for(auto const& range : catalog.retired_people) {
		checksum_integer(hash, range.source_population_cell);
		checksum_integer(hash, range.first_ordinal);
		checksum_integer(hash, range.count);
	}
	for(auto source_cell : catalog.retired_source_cells)
		checksum_integer(hash, source_cell);
	std::sort(catalog.deaths.begin(), catalog.deaths.end(), [](auto const& a, auto const& b) {
		return std::tie(a.source_population_cell, a.first_ordinal, a.count, a.death_day, a.cause)
			< std::tie(b.source_population_cell, b.first_ordinal, b.count, b.death_day, b.cause);
	});
	for(auto const& death : catalog.deaths) {
		checksum_integer(hash, death.source_population_cell);
		checksum_integer(hash, death.first_ordinal);
		checksum_integer(hash, death.count);
		checksum_integer(hash, death.death_day);
		checksum_integer(hash, death.cause);
	}
	std::sort(catalog.birth_cohorts.begin(), catalog.birth_cohorts.end(), [](auto const& a, auto const& b) {
		return std::tie(a.source_population_cell, a.first_ordinal)
			< std::tie(b.source_population_cell, b.first_ordinal);
	});
	for(auto const& cohort : catalog.birth_cohorts) {
		checksum_integer(hash, cohort.source_population_cell);
		checksum_integer(hash, cohort.first_ordinal);
		checksum_integer(hash, cohort.count);
		checksum_integer(hash, cohort.birth_day);
		checksum_integer(hash, cohort.home_site.index());
		checksum_integer(hash, cohort.culture.index());
		checksum_integer(hash, cohort.religion.index());
		checksum_integer(hash, cohort.pop_type.index());
	}
	std::sort(catalog.membership_ranges.begin(), catalog.membership_ranges.end(), [](auto const& a, auto const& b) {
		return std::tie(a.identity_population_cell, a.first_ordinal, a.current_population_cell, a.count)
			< std::tie(b.identity_population_cell, b.first_ordinal, b.current_population_cell, b.count);
	});
	for(auto const& range : catalog.membership_ranges) {
		checksum_integer(hash, range.identity_population_cell);
		checksum_integer(hash, range.current_population_cell);
		checksum_integer(hash, range.first_ordinal);
		checksum_integer(hash, range.count);
	}
	std::sort(catalog.overrides.begin(), catalog.overrides.end(), [](auto const& a, auto const& b) { return key_less(a.key, b.key); });
	for(auto const& record : catalog.overrides) {
		checksum_key(hash, record.key);
		checksum_integer(hash, uint8_t(record.has_alive));
		checksum_integer(hash, uint8_t(record.alive));
		checksum_integer(hash, uint8_t(record.has_home_site));
		checksum_integer(hash, record.home_site.index());
	}
	std::sort(catalog.bridges.begin(), catalog.bridges.end(), [](auto const& a, auto const& b) { return key_less(a.key, b.key); });
	for(auto const& bridge : catalog.bridges) {
		checksum_key(hash, bridge.key);
		checksum_integer(hash, uint8_t(alive(state, bridge.key)));
		checksum_integer(hash, birth_day_index(state, bridge.key));
		checksum_integer(hash, death_day_index(state, bridge.key));
		checksum_integer(hash, home_site(state, bridge.key).index());
	}
	auto assignments = catalog.military_assignments;
	std::sort(assignments.begin(), assignments.end(), [](auto const& a, auto const& b) {
		return std::tie(a.source_population_cell, a.first_ordinal, a.ordinal_stride, a.formation_id,
			a.training_days_remaining) < std::tie(b.source_population_cell, b.first_ordinal,
			b.ordinal_stride, b.formation_id, b.training_days_remaining);
	});
	for(auto const& assignment : assignments) {
		checksum_integer(hash, assignment.source_population_cell);
		checksum_integer(hash, assignment.first_ordinal);
		checksum_integer(hash, assignment.ordinal_stride);
		checksum_integer(hash, assignment.count);
		checksum_integer(hash, assignment.formation_id);
		checksum_integer(hash, assignment.training_days_remaining);
	}
	auto transitions = catalog.transitions;
	std::sort(transitions.begin(), transitions.end(), [](auto const& a, auto const& b) {
		return std::tie(a.transition_day, a.identity_population_cell, a.first_ordinal, a.from_population_cell,
			a.to_population_cell, a.cause, a.count) < std::tie(b.transition_day, b.identity_population_cell,
			b.first_ordinal, b.from_population_cell, b.to_population_cell, b.cause, b.count);
	});
	for(auto const& event : transitions) {
		checksum_integer(hash, event.transition_day);
		checksum_integer(hash, event.identity_population_cell);
		checksum_integer(hash, event.from_population_cell);
		checksum_integer(hash, event.to_population_cell);
		checksum_integer(hash, event.cause);
		checksum_integer(hash, event.first_ordinal);
		checksum_integer(hash, event.count);
	}
	// DCON POP slots are projection plumbing, so their allocation order is not
	// part of the canonical person checksum.
	std::sort(catalog.transfer_remainders.begin(), catalog.transfer_remainders.end(), [](auto const& a, auto const& b) {
		return std::tie(a.from_population_cell, a.to_population_cell)
			< std::tie(b.from_population_cell, b.to_population_cell);
	});
	for(auto const& remainder : catalog.transfer_remainders) {
		checksum_integer(hash, remainder.from_population_cell);
		checksum_integer(hash, remainder.to_population_cell);
		checksum_integer(hash, std::bit_cast<uint64_t>(remainder.pending_people));
	}
	return hash;
}

} // namespace persons
