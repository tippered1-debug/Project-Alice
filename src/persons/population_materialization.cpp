#include "population_materialization.hpp"

#include "persons.hpp"
#include "system_state.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace persons::population_materialization {
namespace {

constexpr uint32_t synthetic_minimum_age_days = 5u * 365u;
constexpr uint32_t synthetic_age_span_days = 80u * 365u;

template<typename Id>
void sort_ids(std::vector<Id>& ids) {
	std::sort(ids.begin(), ids.end(), [](auto left, auto right) {
		return left.index() < right.index();
	});
}

uint32_t source_cell_key(dcon::pop_id pop) {
	return uint32_t(pop.index()) + 1u;
}

bool literal_count(sys::state const& state, dcon::pop_id pop, uint32_t& count) {
	auto size = state.world.pop_get_size(pop);
	if(!std::isfinite(size) || size < 0.0f) return false;
	auto integral = std::floor(double(size));
	if(integral > double(std::numeric_limits<uint32_t>::max())) return false;
	count = uint32_t(integral);
	return true;
}

dcon::population_materialization_id marker_for(sys::state const& state, dcon::pop_id pop) {
	if(!pop || !state.world.pop_is_valid(pop)) return {};
	return state.world.pop_get_population_materialization_from_population_materialization_source(pop);
}

std::vector<dcon::person_id> persons_for_marker(sys::state const& state,
	dcon::population_materialization_id marker) {
	std::vector<dcon::person_id> result;
	if(!marker || !state.world.population_materialization_is_valid(marker)) return result;
	state.world.population_materialization_for_each_population_materialization_person(marker,
		[&](dcon::population_materialization_person_id relation) {
			auto person = state.world.population_materialization_person_get_person(relation);
			if(person && state.world.person_is_valid(person)) result.push_back(person);
		});
	std::sort(result.begin(), result.end(), [&](auto left, auto right) {
		auto left_ordinal = state.world.person_get_source_population_ordinal(left);
		auto right_ordinal = state.world.person_get_source_population_ordinal(right);
		return left_ordinal == right_ordinal ? left.index() < right.index() : left_ordinal < right_ordinal;
	});
	return result;
}

dcon::site_id first_site_in_province(sys::state const& state, dcon::province_id province) {
	std::vector<dcon::site_id> sites;
	if(!province) return {};
	state.world.province_for_each_site_location_as_province(province, [&](auto relation) {
		auto site = state.world.site_location_get_site(relation);
		if(site && state.world.site_is_valid(site)) sites.push_back(site);
	});
	sort_ids(sites);
	return sites.empty() ? dcon::site_id{} : sites.front();
}

dcon::site_id deterministic_home_site(sys::state& state, dcon::pop_id pop, dcon::site_id requested) {
	if(requested && state.world.site_is_valid(requested)) return requested;
	auto province = state.world.pop_get_province_from_pop_location(pop);
	if(!province || !state.world.province_is_valid(province)) return {};
	if(auto existing = first_site_in_province(state, province)) return existing;
	auto site = state.world.create_site();
	state.world.force_create_site_location(site, province);
	state.world.site_set_position(site, state.world.province_get_mid_point(province));
	return site;
}

void initialize_person(sys::state& state, dcon::person_id person, dcon::pop_id pop,
	uint32_t source_cell, uint32_t ordinal, dcon::site_id home_site) {
	state.world.person_set_source_population_cell(person, source_cell);
	state.world.person_set_source_population_ordinal(person, ordinal);
	state.world.person_set_source_pop_type(person, state.world.pop_get_poptype(pop));
	state.world.person_set_source_culture(person, state.world.pop_get_culture(pop));
	state.world.person_set_source_religion(person, state.world.pop_get_religion(pop));
	state.world.force_create_person_home_site(person, home_site);
}

uint64_t stable_age_hash(person_key key) {
	uint64_t value = uint64_t(key.source_population_cell) * 0x9e3779b97f4a7c15ULL;
	value ^= uint64_t(key.ordinal) + 0x517cc1b727220a95ULL + (value << 6) + (value >> 2);
	return value;
}

}

sys::date bootstrap_birth_date(sys::date current_date, person_key key) {
	if(!current_date) return {};
	auto age_days = synthetic_minimum_age_days + uint32_t(stable_age_hash(key) % synthetic_age_span_days);
	if(current_date.to_raw_value() <= int32_t(age_days)) return {};
	return current_date - int32_t(age_days);
}

cell_materialization_result materialize_population_cell_with_status(sys::state& state, dcon::pop_id pop,
	dcon::site_id home_site) {
	cell_materialization_result result;
	if(!pop || !state.world.pop_is_valid(pop)) return result;
	auto marker = marker_for(state, pop);
	if(marker) {
		result.persons = persons_for_marker(state, marker);
		result.status = state.world.population_materialization_get_bootstrap_version(marker)
			== bootstrap_semantics_version
			? materialization_status::already_materialized
			: materialization_status::version_mismatch;
		return result;
	}
	uint32_t count = 0;
	if(!literal_count(state, pop, count)) {
		result.status = materialization_status::overflow;
		return result;
	}
	if(home_site && !state.world.site_is_valid(home_site)) {
		result.status = materialization_status::invalid_home_site;
		return result;
	}
	auto site = deterministic_home_site(state, pop, home_site);
	if(count != 0 && !site) {
		result.status = materialization_status::missing_home_province;
		return result;
	}
	auto source_cell = source_cell_key(pop);
	marker = state.world.create_population_materialization();
	state.world.population_materialization_set_source_population_cell(marker, source_cell);
	state.world.population_materialization_set_materialized_person_count(marker, count);
	state.world.population_materialization_set_bootstrap_version(marker, bootstrap_semantics_version);
	state.world.force_create_population_materialization_source(marker, pop);
	result.persons.reserve(count);
	for(uint32_t ordinal = 0; ordinal < count; ++ordinal) {
		person_key key{source_cell, ordinal};
		auto person = create_person(state, bootstrap_birth_date(state.current_date, key));
		initialize_person(state, person, pop, source_cell, ordinal, site);
		state.world.force_create_population_materialization_person(marker, person);
		result.persons.push_back(person);
	}
	result.status = materialization_status::created;
	return result;
}

std::vector<dcon::person_id> materialize_population_cell(sys::state& state, dcon::pop_id pop,
	dcon::site_id home_site) {
	return materialize_population_cell_with_status(state, pop, home_site).persons;
}

std::vector<dcon::person_id> materialize_initial_population(sys::state& state) {
	std::vector<dcon::pop_id> pops;
	state.world.for_each_pop([&](auto pop) { pops.push_back(pop); });
	sort_ids(pops);
	std::vector<dcon::person_id> result;
	for(auto pop : pops) {
		auto persons = materialize_population_cell(state, pop);
		result.insert(result.end(), persons.begin(), persons.end());
	}
	return result;
}

population_estimate estimate_initial_population(sys::state const& state) {
	population_estimate result;
	state.world.for_each_pop([&](auto pop) {
		++result.source_population_cells;
		auto size = state.world.pop_get_size(pop);
		if(!std::isfinite(size) || size < 0.0f) {
			result.overflow = true;
			return;
		}
		result.source_population_units += double(size);
		auto literal = std::floor(double(size));
		if(literal > double(std::numeric_limits<uint64_t>::max())
			|| result.intended_literal_persons > std::numeric_limits<uint64_t>::max() - uint64_t(literal)) {
			result.overflow = true;
			return;
		}
		auto count = uint64_t(literal);
		result.intended_literal_persons += count;
		result.intended_economic_actors += count;
		result.largest_source_cell = std::max(result.largest_source_cell, count);
	});
	return result;
}

materialization_measurement measure_initial_population_materialization(sys::state& state) {
	auto persons_before = state.world.person_size();
	auto actors_before = state.world.economic_actor_size();
	auto started = std::chrono::steady_clock::now();
	(void)materialize_initial_population(state);
	auto finished = std::chrono::steady_clock::now();
	materialization_measurement result;
	result.elapsed_microseconds = uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(finished - started).count());
	result.persons_created = state.world.person_size() - persons_before;
	result.economic_actors_created = state.world.economic_actor_size() - actors_before;
	result.persons_total = state.world.person_size();
	result.economic_actors_total = state.world.economic_actor_size();
	result.materialization_markers = state.world.population_materialization_size();
	if(result.elapsed_microseconds != 0)
		result.persons_per_second = double(result.persons_created) * 1'000'000.0 / double(result.elapsed_microseconds);
	return result;
}

materialization_measurement measure_synthetic_population_materialization(sys::state& state, uint32_t count) {
	auto province = state.world.create_province();
	auto site = state.world.create_site();
	state.world.force_create_site_location(site, province);
	auto pop = state.world.create_pop();
	state.world.force_create_pop_location(pop, province);
	state.world.pop_set_size(pop, float(count));
	auto persons_before = state.world.person_size();
	auto actors_before = state.world.economic_actor_size();
	auto started = std::chrono::steady_clock::now();
	(void)materialize_population_cell(state, pop, site);
	auto finished = std::chrono::steady_clock::now();
	materialization_measurement result;
	result.elapsed_microseconds = uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(finished - started).count());
	result.persons_created = state.world.person_size() - persons_before;
	result.economic_actors_created = state.world.economic_actor_size() - actors_before;
	result.persons_total = state.world.person_size();
	result.economic_actors_total = state.world.economic_actor_size();
	result.materialization_markers = state.world.population_materialization_size();
	if(result.elapsed_microseconds != 0)
		result.persons_per_second = double(result.persons_created) * 1'000'000.0 / double(result.elapsed_microseconds);
	return result;
}

materialization_status materialization_status_for_population_cell(sys::state const& state, dcon::pop_id pop) {
	if(!pop || !state.world.pop_is_valid(pop)) return materialization_status::invalid_source;
	auto marker = marker_for(state, pop);
	if(!marker) return materialization_status::invalid_source;
	return state.world.population_materialization_get_bootstrap_version(marker)
		== bootstrap_semantics_version ? materialization_status::already_materialized : materialization_status::version_mismatch;
}

person_key key_for_person(sys::state const& state, dcon::person_id person) {
	if(!person || !state.world.person_is_valid(person)) return {};
	return {state.world.person_get_source_population_cell(person), state.world.person_get_source_population_ordinal(person)};
}

bool is_materialized_person(sys::state const& state, dcon::person_id person) {
	return key_for_person(state, person).source_population_cell != 0;
}

dcon::site_id home_site_for_materialized_person(sys::state const& state, dcon::person_id person) {
	return is_materialized_person(state, person)
		? state.world.person_get_site_from_person_home_site(person) : dcon::site_id{};
}

} // namespace persons::population_materialization
