#include "exact_population.hpp"

#include "actors/ownership.hpp"
#include "system_state.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <unordered_map>

namespace persons {

struct exact_population_store {
	std::vector<exact_population::cell_descriptor> cells;
	std::unordered_map<uint32_t, std::size_t> cell_by_source;
	std::unordered_map<exact_population::person_key, exact_population::exact_person_override,
		exact_population::person_key_hash> overrides;
	std::unordered_map<exact_population::person_key, dcon::person_id,
		exact_population::person_key_hash> bridges;
};

} // namespace persons

namespace persons::exact_population {
namespace {

constexpr uint32_t anchor_minimum_age_days = 18u * 365u;
constexpr uint32_t anchor_age_span_days = (65u - 18u) * 365u;
constexpr uint32_t dependent_age_span_days = 91u * 365u;

uint64_t mix(uint64_t value) {
	value ^= value >> 30;
	value *= 0xbf58476d1ce4e5b9ULL;
	value ^= value >> 27;
	value *= 0x94d049bb133111ebULL;
	return value ^ (value >> 31);
}

uint64_t deterministic_hash(cell_descriptor const& cell, person_key key) {
	return mix(cell.demographic_seed
		^ (uint64_t(key.source_population_cell) * 0x9e3779b97f4a7c15ULL)
		^ (key.ordinal + 0x517cc1b727220a95ULL));
}

std::shared_ptr<exact_population_store> ensure_store(sys::state& state) {
	if(!state.exact_population) state.exact_population = std::make_shared<exact_population_store>();
	return state.exact_population;
}

std::shared_ptr<exact_population_store> ensure_store(sys::state const& state) {
	return ensure_store(const_cast<sys::state&>(state));
}

cell_descriptor const* find_cell(sys::state const& state, uint32_t source_population_cell) {
	if(source_population_cell == 0) return nullptr;
	auto store = ensure_store(state);
	auto it = store->cell_by_source.find(source_population_cell);
	return it == store->cell_by_source.end() ? nullptr : &store->cells[it->second];
}

std::size_t find_cell_index(exact_population_store const& store, uint32_t source_population_cell) {
	auto it = store.cell_by_source.find(source_population_cell);
	return it == store.cell_by_source.end() ? std::numeric_limits<std::size_t>::max() : it->second;
}

cell_descriptor const* find_cell(sys::state const& state, person_key key) {
	auto cell = find_cell(state, key.source_population_cell);
	return cell && key.ordinal < cell->literal_count ? cell : nullptr;
}

bool literal_count_for_pop(sys::state const& state, dcon::pop_id pop, uint64_t& count) {
	if(!pop || !state.world.pop_is_valid(pop)) return false;
	auto size = state.world.pop_get_size(pop);
	if(!std::isfinite(size) || size < 0.0f) return false;
	auto literal = std::floor(double(size) * 4.0);
	if(literal > double(std::numeric_limits<uint64_t>::max())) return false;
	count = uint64_t(literal);
	return true;
}

dcon::site_id first_site_in_province(sys::state const& state, dcon::province_id province) {
	std::vector<dcon::site_id> sites;
	if(!province || !state.world.province_is_valid(province)) return {};
	state.world.province_for_each_site_location_as_province(province, [&](auto relation) {
		auto site = state.world.site_location_get_site(relation);
		if(site && state.world.site_is_valid(site)) sites.push_back(site);
	});
	std::sort(sites.begin(), sites.end(), [](auto left, auto right) { return left.index() < right.index(); });
	return sites.empty() ? dcon::site_id{} : sites.front();
}

dcon::site_id deterministic_home_site(sys::state& state, dcon::pop_id pop, dcon::site_id requested) {
	if(requested && state.world.site_is_valid(requested)) return requested;
	auto province = state.world.pop_get_province_from_pop_location(pop);
	if(auto existing = first_site_in_province(state, province)) return existing;
	if(!province || !state.world.province_is_valid(province)) return {};
	auto site = state.world.create_site();
	state.world.force_create_site_location(site, province);
	state.world.site_set_position(site, state.world.province_get_mid_point(province));
	return site;
}

bool valid_descriptor(sys::state const& state, cell_descriptor const& descriptor) {
	if(descriptor.source_population_cell == 0
		|| descriptor.bootstrap_version != bootstrap_semantics_version
		|| descriptor.demographic_policy != demographic_policy_version)
		return false;
	if(descriptor.home_site && !state.world.site_is_valid(descriptor.home_site)) return false;
	if(descriptor.source_culture && !state.world.culture_is_valid(descriptor.source_culture)) return false;
	if(descriptor.source_religion && !state.world.religion_is_valid(descriptor.source_religion)) return false;
	if(descriptor.source_pop_type && !state.world.pop_type_is_valid(descriptor.source_pop_type)) return false;
	return true;
}

registration_result add_descriptor(sys::state& state, cell_descriptor descriptor) {
	registration_result result;
	result.descriptor = descriptor;
	if(!valid_descriptor(state, descriptor)) {
		result.result = descriptor.bootstrap_version != bootstrap_semantics_version
			? status::version_mismatch : status::invalid_descriptor;
		return result;
	}
	auto store = ensure_store(state);
	if(auto existing = store->cell_by_source.find(descriptor.source_population_cell);
		existing != store->cell_by_source.end()) {
		result.result = status::already_registered;
		result.descriptor = store->cells[existing->second];
		return result;
	}
	store->cell_by_source.emplace(descriptor.source_population_cell, store->cells.size());
	store->cells.push_back(descriptor);
	result.result = status::created;
	result.descriptor = descriptor;
	return result;
}

exact_person_override* mutable_override(sys::state& state, person_key key) {
	auto& override = ensure_store(state)->overrides[key];
	override.key = key;
	return &override;
}

exact_person_override const* find_override(sys::state const& state, person_key key) {
	auto store = ensure_store(state);
	auto it = store->overrides.find(key);
	return it == store->overrides.end() ? nullptr : &it->second;
}

void remove_empty_override(sys::state& state, person_key key) {
	auto store = ensure_store(state);
	auto it = store->overrides.find(key);
	if(it != store->overrides.end() && !it->second.has_alive && !it->second.has_home_site)
		store->overrides.erase(it);
}

int32_t clamp_day(int64_t day) {
	if(day < std::numeric_limits<int32_t>::min()) return std::numeric_limits<int32_t>::min();
	if(day > std::numeric_limits<int32_t>::max()) return std::numeric_limits<int32_t>::max();
	return int32_t(day);
}

uint32_t bootstrap_age_days(cell_descriptor const& cell, person_key key) {
	auto hash = deterministic_hash(cell, key);
	if((key.ordinal % 4u) == 0u)
		return anchor_minimum_age_days + uint32_t(hash % anchor_age_span_days);
	return uint32_t(hash % dependent_age_span_days);
}

} // namespace

std::size_t person_key_hash::operator()(person_key key) const noexcept {
	return std::size_t(mix(uint64_t(key.source_population_cell) * 0x9e3779b97f4a7c15ULL ^ key.ordinal));
}

registration_result register_population_cell(sys::state& state, dcon::pop_id pop, dcon::site_id requested_home_site) {
	registration_result result;
	if(!pop || !state.world.pop_is_valid(pop)) return result;
	auto source_cell = uint32_t(pop.index()) + 1u;
	if(auto existing = descriptor_for_cell(state, source_cell)) {
		result.result = status::already_registered;
		result.descriptor = *existing;
		return result;
	}
	uint64_t count = 0;
	if(!literal_count_for_pop(state, pop, count)) {
		result.result = status::overflow;
		return result;
	}
	if(requested_home_site && !state.world.site_is_valid(requested_home_site)) {
		result.result = status::invalid_home_site;
		return result;
	}
	auto home = deterministic_home_site(state, pop, requested_home_site);
	if(count != 0 && !home) {
		result.result = status::missing_home_province;
		return result;
	}
	cell_descriptor descriptor;
	descriptor.source_population_cell = source_cell;
	descriptor.literal_count = count;
	descriptor.bootstrap_base_day = state.current_date ? state.current_date.to_raw_value() - 1 : 0;
	descriptor.demographic_seed = uint64_t(source_cell) * 0x9e3779b97f4a7c15ULL;
	descriptor.home_site = home;
	descriptor.source_culture = state.world.pop_get_culture(pop);
	descriptor.source_religion = state.world.pop_get_religion(pop);
	descriptor.source_pop_type = state.world.pop_get_poptype(pop);
	return add_descriptor(state, descriptor);
}

registration_result register_synthetic_population_cell(sys::state& state, cell_descriptor descriptor) {
	return add_descriptor(state, descriptor);
}

bool source_cell_registered(sys::state const& state, uint32_t source_population_cell) {
	return find_cell(state, source_population_cell) != nullptr;
}

uint64_t literal_count_for_cell(sys::state const& state, uint32_t source_population_cell) {
	auto descriptor = find_cell(state, source_population_cell);
	return descriptor ? descriptor->literal_count : 0;
}

uint64_t logical_person_count(sys::state const& state) {
	uint64_t result = 0;
	for(auto const& descriptor : ensure_store(state)->cells) {
		if(std::numeric_limits<uint64_t>::max() - result < descriptor.literal_count)
			return std::numeric_limits<uint64_t>::max();
		result += descriptor.literal_count;
	}
	return result;
}

uint64_t cell_count(sys::state const& state) {
	return uint64_t(ensure_store(state)->cells.size());
}

uint64_t override_count(sys::state const& state) {
	return uint64_t(ensure_store(state)->overrides.size());
}

uint64_t bridge_count(sys::state const& state) {
	return uint64_t(ensure_store(state)->bridges.size());
}

bool exists(sys::state const& state, person_key key) {
	return find_cell(state, key) != nullptr;
}

bool alive(sys::state const& state, person_key key) {
	if(!exists(state, key)) return false;
	if(auto override = find_override(state, key); override && override->has_alive) return override->alive;
	return true;
}

bool set_alive(sys::state& state, person_key key, bool value) {
	if(!exists(state, key)) return false;
	auto override = mutable_override(state, key);
	if(value) {
		override->has_alive = false;
		override->alive = true;
	} else {
		override->has_alive = true;
		override->alive = false;
	}
	remove_empty_override(state, key);
	return true;
}

persons::birth_day_index_t birth_day_index(sys::state const& state, person_key key) {
	auto descriptor = find_cell(state, key);
	if(!descriptor) return 0;
	return persons::birth_day_index_t(clamp_day(int64_t(descriptor->bootstrap_base_day)
		- int64_t(bootstrap_age_days(*descriptor, key))));
}

int32_t age_days(sys::state const& state, person_key key, sys::date current_day) {
	if(!exists(state, key) || !current_day) return -1;
	return clamp_day(int64_t(current_day.to_raw_value() - 1) - int64_t(birth_day_index(state, key)));
}

int32_t age_years(sys::state const& state, person_key key, sys::date current_day) {
	auto days = age_days(state, key, current_day);
	return days < 0 ? -1 : days / 365;
}

dcon::site_id home_site(sys::state const& state, person_key key) {
	auto descriptor = find_cell(state, key);
	if(!descriptor) return {};
	if(auto override = find_override(state, key); override && override->has_home_site)
		return override->home_site;
	return descriptor->home_site;
}

bool set_home_site(sys::state& state, person_key key, dcon::site_id site) {
	if(!exists(state, key) || !site || !state.world.site_is_valid(site)) return false;
	auto override = mutable_override(state, key);
	auto descriptor = find_cell(state, key);
	if(site == descriptor->home_site) {
		override->has_home_site = false;
		override->home_site = {};
	} else {
		override->has_home_site = true;
		override->home_site = site;
	}
	remove_empty_override(state, key);
	return true;
}

dcon::culture_id source_culture(sys::state const& state, person_key key) {
	auto descriptor = find_cell(state, key);
	return descriptor ? descriptor->source_culture : dcon::culture_id{};
}

dcon::religion_id source_religion(sys::state const& state, person_key key) {
	auto descriptor = find_cell(state, key);
	return descriptor ? descriptor->source_religion : dcon::religion_id{};
}

dcon::pop_type_id source_pop_type(sys::state const& state, person_key key) {
	auto descriptor = find_cell(state, key);
	return descriptor ? descriptor->source_pop_type : dcon::pop_type_id{};
}

bool is_source_workforce_anchor(sys::state const& state, person_key key) {
	return exists(state, key) && key.ordinal % 4u == 0u;
}

std::optional<cell_descriptor> descriptor_for_cell(sys::state const& state, uint32_t source_population_cell) {
	auto descriptor = find_cell(state, source_population_cell);
	return descriptor ? std::optional<cell_descriptor>(*descriptor) : std::nullopt;
}

dcon::person_id legacy_person_for_exact_person(sys::state const& state, person_key key) {
	if(!exists(state, key)) return {};
	auto store = ensure_store(state);
	auto it = store->bridges.find(key);
	return it != store->bridges.end() && state.world.person_is_valid(it->second) ? it->second : dcon::person_id{};
}

dcon::person_id materialize_legacy_person_bridge(sys::state& state, person_key key) {
	if(!exists(state, key)) return {};
	auto store = ensure_store(state);
	if(auto existing = store->bridges.find(key); existing != store->bridges.end())
		return state.world.person_is_valid(existing->second) ? existing->second : dcon::person_id{};
	if(key.ordinal > uint64_t(std::numeric_limits<uint32_t>::max())) return {};
	auto descriptor = find_cell(state, key);
	auto person = persons::create_person_with_birth_day(state, birth_day_index(state, key));
	state.world.person_set_source_population_cell(person, key.source_population_cell);
	state.world.person_set_source_population_ordinal(person, uint32_t(key.ordinal));
	state.world.person_set_source_pop_type(person, descriptor->source_pop_type);
	state.world.person_set_source_culture(person, descriptor->source_culture);
	state.world.person_set_source_religion(person, descriptor->source_religion);
	if(auto site = home_site(state, key)) state.world.force_create_person_home_site(person, site);
	if(!alive(state, key)) state.world.person_set_alive(person, uint8_t(0));
	store->bridges.emplace(key, person);
	return person;
}

catalog_snapshot export_snapshot(sys::state const& state) {
	catalog_snapshot result;
	auto store = ensure_store(state);
	result.cells = store->cells;
	result.overrides.reserve(store->overrides.size());
	for(auto const& [key, override] : store->overrides) result.overrides.push_back(override);
	result.bridges.reserve(store->bridges.size());
	for(auto const& [key, person] : store->bridges) result.bridges.push_back({key, person});
	std::sort(result.overrides.begin(), result.overrides.end(), [](auto const& left, auto const& right) {
		return left.key.source_population_cell == right.key.source_population_cell
			? left.key.ordinal < right.key.ordinal
			: left.key.source_population_cell < right.key.source_population_cell;
	});
	std::sort(result.bridges.begin(), result.bridges.end(), [](auto const& left, auto const& right) {
		return left.key.source_population_cell == right.key.source_population_cell
			? left.key.ordinal < right.key.ordinal
			: left.key.source_population_cell < right.key.source_population_cell;
	});
	return result;
}

bool import_snapshot(sys::state& state, catalog_snapshot const& snapshot) {
	if(snapshot.bootstrap_version != bootstrap_semantics_version) return false;
	auto candidate = std::make_shared<exact_population_store>();
	for(auto const& descriptor : snapshot.cells) {
		if(!valid_descriptor(state, descriptor)
			|| candidate->cell_by_source.contains(descriptor.source_population_cell)) return false;
		candidate->cell_by_source.emplace(descriptor.source_population_cell, candidate->cells.size());
		candidate->cells.push_back(descriptor);
	}
	for(auto const& override : snapshot.overrides) {
		if(!candidate->cell_by_source.contains(override.key.source_population_cell)
			|| override.key.ordinal >= candidate->cells[candidate->cell_by_source.at(override.key.source_population_cell)].literal_count
			|| candidate->overrides.contains(override.key)) return false;
		if(override.has_home_site && (!override.home_site || !state.world.site_is_valid(override.home_site))) return false;
		candidate->overrides.emplace(override.key, override);
	}
	for(auto const& bridge : snapshot.bridges) {
		if(!candidate->cell_by_source.contains(bridge.key.source_population_cell)
			|| bridge.key.ordinal >= candidate->cells[candidate->cell_by_source.at(bridge.key.source_population_cell)].literal_count
			|| !bridge.legacy_person || !state.world.person_is_valid(bridge.legacy_person)
			|| candidate->bridges.contains(bridge.key)) return false;
		candidate->bridges.emplace(bridge.key, bridge.legacy_person);
	}
	state.exact_population = std::move(candidate);
	return true;
}

void clear_store(sys::state& state) {
	state.exact_population.reset();
}

} // namespace persons::exact_population
