#include "population_materialization.hpp"

#include "persons.hpp"
#include "system_state.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace persons::population_materialization {
namespace {

template<typename Id>
void sort_ids(std::vector<Id>& ids) {
	std::sort(ids.begin(), ids.end(), [](auto left, auto right) {
		return left.index() < right.index();
	});
}

uint32_t source_cell_key(dcon::pop_id pop) {
	return uint32_t(pop.index()) + 1u;
}

uint32_t materialization_count(sys::state const& state, dcon::pop_id pop) {
	auto size = state.world.pop_get_size(pop);
	if(!std::isfinite(size) || size <= 0.0f) return 0;
	auto bounded = std::min<double>(std::floor(double(size)),
		std::numeric_limits<uint32_t>::max());
	return uint32_t(bounded);
}

dcon::population_materialization_id marker_for(sys::state const& state, uint32_t source_cell) {
	dcon::population_materialization_id result{};
	state.world.for_each_population_materialization([&](auto marker) {
		if(!result && state.world.population_materialization_get_source_population_cell(marker) == source_cell)
			result = marker;
	});
	return result;
}

std::vector<dcon::person_id> persons_for_cell(sys::state const& state, uint32_t source_cell) {
	std::vector<dcon::person_id> result;
	state.world.for_each_person([&](auto person) {
		if(state.world.person_get_source_population_cell(person) == source_cell)
			result.push_back(person);
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

}

std::vector<dcon::person_id> materialize_population_cell(sys::state& state, dcon::pop_id pop,
	dcon::site_id home_site) {
	std::vector<dcon::person_id> result;
	if(!pop || !state.world.pop_is_valid(pop)) return result;
	auto source_cell = source_cell_key(pop);
	if(auto marker = marker_for(state, source_cell)) {
		return persons_for_cell(state, source_cell);
	}
	auto count = materialization_count(state, pop);
	auto site = deterministic_home_site(state, pop, home_site);
	if(count != 0 && !site) return result;
	result.reserve(count);
	for(uint32_t ordinal = 0; ordinal < count; ++ordinal) {
		auto person = create_person(state, state.current_date);
		initialize_person(state, person, pop, source_cell, ordinal, site);
		result.push_back(person);
	}
	auto marker = state.world.create_population_materialization();
	state.world.population_materialization_set_source_population_cell(marker, source_cell);
	state.world.population_materialization_set_materialized_person_count(marker, count);
	return result;
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

materialization_measurement measure_initial_population_materialization(sys::state& state) {
	auto persons_before = state.world.person_size();
	auto started = std::chrono::steady_clock::now();
	(void)materialize_initial_population(state);
	auto finished = std::chrono::steady_clock::now();
	materialization_measurement result;
	result.elapsed_microseconds = uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(finished - started).count());
	result.persons_created = state.world.person_size() - persons_before;
	result.persons_total = state.world.person_size();
	result.economic_actors_total = state.world.economic_actor_size();
	result.materialization_markers = state.world.population_materialization_size();
	return result;
}

person_key key_for_person(sys::state const& state, dcon::person_id person) {
	if(!person || !state.world.person_is_valid(person)) return {};
	return {
		state.world.person_get_source_population_cell(person),
		state.world.person_get_source_population_ordinal(person)
	};
}

bool is_materialized_person(sys::state const& state, dcon::person_id person) {
	return key_for_person(state, person).source_population_cell != 0;
}

dcon::site_id home_site_for_materialized_person(sys::state const& state, dcon::person_id person) {
	return is_materialized_person(state, person)
		? state.world.person_get_site_from_person_home_site(person) : dcon::site_id{};
}

} // namespace persons::population_materialization
