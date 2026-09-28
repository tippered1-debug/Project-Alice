#include "exact_population.hpp"

#include "actors/ownership.hpp"
#include "system_state.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace persons {

struct exact_population_store {
	std::vector<exact_population::cell_descriptor> cells;
	std::unordered_map<uint32_t, std::size_t> cell_by_source;
	std::unordered_map<uint32_t, uint32_t> source_by_pop_slot;
	std::unordered_map<uint32_t, uint32_t> pop_slot_by_source;
	std::unordered_set<uint32_t> retired_source_cells;
	std::vector<exact_population::person_range> retired_people;
	std::vector<exact_population::birth_cohort> birth_cohorts;
	std::unordered_map<uint32_t, double> observed_size_by_source;
	uint64_t observed_world_literal_count = 0;
	bool has_lifecycle_checkpoint = false;
	std::unordered_map<exact_population::person_key, exact_population::exact_person_override,
		exact_population::person_key_hash> overrides;
	std::unordered_map<exact_population::person_key, dcon::person_id,
		exact_population::person_key_hash> bridges;
	bool legacy_unbound_catalog = false;
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
	if(source_population_cell == 0 || !state.exact_population) return nullptr;
	auto store = state.exact_population;
	auto it = store->cell_by_source.find(source_population_cell);
	return it == store->cell_by_source.end() ? nullptr : &store->cells[it->second];
}

bool retired_ordinal(exact_population_store const& store, person_key key) {
	for(auto const& range : store.retired_people)
		if(range.source_population_cell == key.source_population_cell
			&& key.ordinal >= range.first_ordinal
			&& key.ordinal - range.first_ordinal < range.count) return true;
	return false;
}

birth_cohort const* birth_cohort_for(exact_population_store const& store, person_key key) {
	auto found = std::upper_bound(store.birth_cohorts.begin(), store.birth_cohorts.end(), key,
		[](person_key value, birth_cohort const& cohort) {
			return value.source_population_cell == cohort.source_population_cell
				? value.ordinal < cohort.first_ordinal
				: value.source_population_cell < cohort.source_population_cell;
		});
	if(found == store.birth_cohorts.begin()) return nullptr;
	--found;
	return found->source_population_cell == key.source_population_cell
		&& key.ordinal >= found->first_ordinal
		&& key.ordinal - found->first_ordinal < found->count ? &*found : nullptr;
}

void normalize_retired_ranges(exact_population_store& store) {
	std::sort(store.retired_people.begin(), store.retired_people.end(), [](auto const& left, auto const& right) {
		return left.source_population_cell == right.source_population_cell
			? left.first_ordinal < right.first_ordinal
			: left.source_population_cell < right.source_population_cell;
	});
	std::vector<person_range> merged;
	merged.reserve(store.retired_people.size());
	for(auto const& range : store.retired_people) {
		if(range.count == 0) continue;
		if(!merged.empty() && merged.back().source_population_cell == range.source_population_cell
			&& range.first_ordinal <= merged.back().first_ordinal + merged.back().count) {
			auto end = std::max(merged.back().first_ordinal + merged.back().count,
				range.first_ordinal + range.count);
			merged.back().count = end - merged.back().first_ordinal;
		} else {
			merged.push_back(range);
		}
	}
	store.retired_people = std::move(merged);
}

uint64_t live_count_for_cell(exact_population_store const& store, cell_descriptor const& descriptor) {
	uint64_t live = descriptor.literal_count;
	for(auto const& range : store.retired_people)
		if(range.source_population_cell == descriptor.source_population_cell)
			live -= std::min(live, range.count);
	for(auto const& [key, override] : store.overrides) {
		if(key.source_population_cell != descriptor.source_population_cell || !override.has_alive) continue;
		auto default_alive = !retired_ordinal(store, key);
		if(override.alive && !default_alive && live != std::numeric_limits<uint64_t>::max()) ++live;
		else if(!override.alive && default_alive && live != 0) --live;
	}
	return live;
}

uint64_t retire_oldest_ordinals(exact_population_store& store, cell_descriptor const& descriptor, uint64_t requested) {
	if(requested == 0 || descriptor.literal_count == 0) return 0;
	std::vector<person_range> existing;
	for(auto const& range : store.retired_people)
		if(range.source_population_cell == descriptor.source_population_cell) existing.push_back(range);
	for(auto const& [key, override] : store.overrides)
		if(key.source_population_cell == descriptor.source_population_cell
			&& override.has_alive && !override.alive && !retired_ordinal(store, key))
			existing.push_back({key.source_population_cell, 0, key.ordinal, 1});
	std::sort(existing.begin(), existing.end(), [](auto const& left, auto const& right) {
		return left.first_ordinal < right.first_ordinal;
	});
	auto cursor = uint64_t(0);
	auto remaining = std::min(requested, live_count_for_cell(store, descriptor));
	auto const requested_to_retire = remaining;
	for(auto const& range : existing) {
		if(remaining == 0 || cursor >= descriptor.literal_count) break;
		if(range.first_ordinal > cursor) {
			auto count = std::min(remaining, range.first_ordinal - cursor);
			store.retired_people.push_back({descriptor.source_population_cell, 0, cursor, count});
			remaining -= count;
			cursor += count;
		}
		cursor = std::max(cursor, range.first_ordinal + range.count);
	}
	if(remaining != 0) {
		auto count = std::min(remaining, descriptor.literal_count - cursor);
		store.retired_people.push_back({descriptor.source_population_cell, 0, cursor, count});
		remaining -= count;
	}
	normalize_retired_ranges(store);
	return requested_to_retire - remaining;
}

std::size_t find_cell_index(exact_population_store const& store, uint32_t source_population_cell) {
	auto it = store.cell_by_source.find(source_population_cell);
	return it == store.cell_by_source.end() ? std::numeric_limits<std::size_t>::max() : it->second;
}

uint32_t pop_slot(dcon::pop_id pop) {
	return pop ? uint32_t(pop.index()) + 1u : 0u;
}

bool bind_population(exact_population_store& store, dcon::pop_id pop, uint32_t source_cell) {
	auto slot = pop_slot(pop);
	if(slot == 0 || source_cell == 0 || store.retired_source_cells.contains(source_cell)) return false;
	if(auto existing = store.source_by_pop_slot.find(slot);
		existing != store.source_by_pop_slot.end() && existing->second != source_cell) return false;
	if(auto existing = store.pop_slot_by_source.find(source_cell);
		existing != store.pop_slot_by_source.end() && existing->second != slot) return false;
	store.source_by_pop_slot.insert_or_assign(slot, source_cell);
	store.pop_slot_by_source.insert_or_assign(source_cell, slot);
	return true;
}

uint32_t maximum_source_cell(exact_population_store const& store) {
	uint32_t result = 0;
	for(auto const& cell : store.cells) result = std::max(result, cell.source_population_cell);
	for(auto const& [source, slot] : store.pop_slot_by_source) result = std::max(result, source);
	for(auto source : store.retired_source_cells) result = std::max(result, source);
	return result;
}

uint32_t source_cell_for_population(sys::state& state, dcon::pop_id pop) {
	if(!pop || !state.world.pop_is_valid(pop)) return 0;
	auto store = ensure_store(state);
	auto slot = pop_slot(pop);
	if(auto found = store->source_by_pop_slot.find(slot); found != store->source_by_pop_slot.end())
		return found->second;

	// Saves written before lifetime bindings used index + 1 as the source cell
	// identity. Preserve those keys once while importing that old catalog.
	auto legacy_candidate = slot;
	if(store->legacy_unbound_catalog
		&& !store->pop_slot_by_source.contains(legacy_candidate)
		&& bind_population(*store, pop, legacy_candidate)) return legacy_candidate;

	// On a fresh world, keep the historical deterministic numbering when free.
	if(!store->legacy_unbound_catalog
		&& !store->cell_by_source.contains(legacy_candidate)
		&& !store->pop_slot_by_source.contains(legacy_candidate)
		&& bind_population(*store, pop, legacy_candidate)) return legacy_candidate;

	auto maximum = maximum_source_cell(*store);
	if(maximum == std::numeric_limits<uint32_t>::max()) return 0;
	auto allocated = maximum + 1u;
	return bind_population(*store, pop, allocated) ? allocated : 0;
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

uint64_t world_literal_count(sys::state const& state) {
	double total_size = 0.0;
	bool overflow = false;
	state.world.for_each_pop([&](auto pop) {
		if(!pop || !state.world.pop_is_valid(pop)) return;
		auto size = double(state.world.pop_get_size(pop));
		if(!std::isfinite(size) || size < 0.0
			|| size > double(std::numeric_limits<uint64_t>::max()) / 4.0 - total_size) {
			overflow = true;
			return;
		}
		total_size += size;
	});
	if(overflow) return std::numeric_limits<uint64_t>::max();
	return uint64_t(std::floor(total_size * 4.0));
}

dcon::site_id deterministic_home_site(sys::state&, dcon::pop_id, dcon::site_id);
registration_result add_descriptor(sys::state&, cell_descriptor);

void capture_population_baseline(sys::state const& state, exact_population_store& store) {
	store.observed_size_by_source.clear();
	for(auto const& [source, slot] : store.pop_slot_by_source) {
		auto pop = dcon::pop_id{dcon::pop_id::value_base_t(slot - 1u)};
		if(state.world.pop_is_valid(pop)) {
			auto size = double(state.world.pop_get_size(pop));
			if(std::isfinite(size) && size >= 0.0)
				store.observed_size_by_source.insert_or_assign(source, size);
		}
	}
	store.observed_world_literal_count = world_literal_count(state);
	store.has_lifecycle_checkpoint = true;
}

bool create_empty_population_cell(sys::state& state, dcon::pop_id pop, uint32_t source_cell) {
	if(!pop || !state.world.pop_is_valid(pop) || source_cell == 0) return false;
	if(find_cell(state, source_cell)) return true;
	auto home = deterministic_home_site(state, pop, {});
	if(!home) return false;
	cell_descriptor descriptor;
	descriptor.source_population_cell = source_cell;
	descriptor.literal_count = 0;
	descriptor.bootstrap_base_day = state.current_date ? state.current_date.to_raw_value() - 1 : 0;
	descriptor.demographic_seed = uint64_t(source_cell) * 0x9e3779b97f4a7c15ULL;
	descriptor.home_site = home;
	descriptor.source_culture = state.world.pop_get_culture(pop);
	descriptor.source_religion = state.world.pop_get_religion(pop);
	descriptor.source_pop_type = state.world.pop_get_poptype(pop);
	return add_descriptor(state, descriptor).result == status::created;
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
	auto source_cell = source_cell_for_population(state, pop);
	if(source_cell == 0) {
		result.result = status::overflow;
		return result;
	}
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

world_bootstrap_result bootstrap_from_current_pops(sys::state& state) {
	world_bootstrap_result result;
	auto store = ensure_store(state);
	auto const had_catalog = !store->cells.empty();
	auto const migrating_legacy_catalog = store->legacy_unbound_catalog;
	for(auto it = store->source_by_pop_slot.begin(); it != store->source_by_pop_slot.end();) {
		auto slot = it->first;
		auto pop = dcon::pop_id{dcon::pop_id::value_base_t(slot - 1u)};
		if(!state.world.pop_is_valid(pop)) {
			store->retired_source_cells.insert(it->second);
			store->pop_slot_by_source.erase(it->second);
			it = store->source_by_pop_slot.erase(it);
		} else {
			++it;
		}
	}
	std::vector<dcon::pop_id> pops;
	state.world.for_each_pop([&](auto pop) { pops.push_back(pop); });
	std::sort(pops.begin(), pops.end(), [](auto left, auto right) {
		return left.index() < right.index();
	});
	for(auto pop : pops) {
		auto slot = pop_slot(pop);
		if(had_catalog && !store->source_by_pop_slot.contains(slot)) {
			auto legacy_cell = slot;
			if(!migrating_legacy_catalog
				|| !store->cell_by_source.contains(legacy_cell)
				|| !bind_population(*store, pop, legacy_cell)) {
				++result.unbound_populations;
				continue;
			}
		}
		auto registration = register_population_cell(state, pop);
		if(registration.result != status::created
			&& registration.result != status::already_registered) {
			if(result.complete) {
				result.failed_population = pop;
				result.failure = registration.result;
			}
			result.complete = false;
			continue;
		}
		if(registration.result == status::created) ++result.registered_cells;
		if(std::numeric_limits<uint64_t>::max() - result.logical_people
			< registration.descriptor.literal_count) {
			result.logical_people = std::numeric_limits<uint64_t>::max();
			result.complete = false;
			result.failed_population = pop;
			result.failure = status::overflow;
		} else {
			result.logical_people += registration.descriptor.literal_count;
		}
	}
	store->legacy_unbound_catalog = false;
	if(result.complete && result.unbound_populations == 0)
		capture_population_baseline(state, *store);
	return result;
}

uint64_t synchronize_current_pop_bindings(sys::state& state) {
	if(!state.exact_population) return 0;
	auto store = ensure_store(state);
	for(auto it = store->source_by_pop_slot.begin(); it != store->source_by_pop_slot.end();) {
		auto pop = dcon::pop_id{dcon::pop_id::value_base_t(it->first - 1u)};
		if(!state.world.pop_is_valid(pop)) {
			store->retired_source_cells.insert(it->second);
			store->pop_slot_by_source.erase(it->second);
			it = store->source_by_pop_slot.erase(it);
		} else {
			++it;
		}
	}
	std::vector<dcon::pop_id> pops;
	state.world.for_each_pop([&](auto pop) { pops.push_back(pop); });
	std::sort(pops.begin(), pops.end(), [](auto left, auto right) {
		return left.index() < right.index();
	});
	uint64_t unbound = 0;
	for(auto pop : pops) {
		if(source_cell_for_population(state, pop) == 0) ++unbound;
	}
	return unbound;
}

population_reconciliation_result reconcile_population_lifecycle(sys::state& state) {
	population_reconciliation_result result;
	(void)synchronize_current_pop_bindings(state);
	auto store = ensure_store(state);
	struct row_state {
		uint32_t source = 0;
		dcon::pop_id pop{};
		double size = 0.0;
	};
	struct row_delta {
		uint32_t source = 0;
		dcon::pop_id pop{};
		double size_change = 0.0;
	};
	std::vector<row_state> current_rows;
	state.world.for_each_pop([&](auto pop) {
		auto source = source_cell_for_population(state, pop);
		uint64_t count = 0;
		if(source != 0 && literal_count_for_pop(state, pop, count))
			current_rows.push_back({source, pop, double(state.world.pop_get_size(pop))});
		else
			++result.unbound_populations;
	});
	std::sort(current_rows.begin(), current_rows.end(), [](auto const& left, auto const& right) {
		return left.source < right.source;
	});
	if(!store->has_lifecycle_checkpoint) {
		if(result.unbound_populations != 0) {
			result.complete = false;
			return result;
		}
		capture_population_baseline(state, *store);
		result.initialized_checkpoint = true;
		return result;
	}
	if(result.unbound_populations != 0) {
		result.complete = false;
		return result;
	}
	auto const current_world_count = world_literal_count(state);
	std::vector<row_delta> growing_rows;
	std::vector<row_delta> shrinking_rows;
	std::unordered_set<uint32_t> current_sources;
	for(auto const& row : current_rows) {
		current_sources.insert(row.source);
		auto previous = store->observed_size_by_source.find(row.source);
		auto old_size = previous == store->observed_size_by_source.end() ? 0.0 : previous->second;
		if(row.size > old_size) growing_rows.push_back({row.source, row.pop, row.size - old_size});
		else if(old_size > row.size) shrinking_rows.push_back({row.source, row.pop, old_size - row.size});
	}
	for(auto const& [source, old_size] : store->observed_size_by_source)
		if(!current_sources.contains(source) && old_size > 0.0)
			shrinking_rows.push_back({source, {}, old_size});
	auto births_remaining = current_world_count > store->observed_world_literal_count
		? current_world_count - store->observed_world_literal_count : 0u;
	auto deaths_remaining = store->observed_world_literal_count > current_world_count
		? store->observed_world_literal_count - current_world_count : 0u;
	double total_growing_size = 0.0;
	for(auto const& row : growing_rows) total_growing_size += row.size_change;
	auto const initial_births = births_remaining;
	for(std::size_t i = 0; i < growing_rows.size(); ++i) {
		if(births_remaining == 0) break;
		auto const& row = growing_rows[i];
		auto amount = i + 1 == growing_rows.size() || total_growing_size <= 0.0
			? births_remaining
			: std::min(births_remaining, uint64_t(std::floor(
				double(initial_births) * row.size_change / total_growing_size)));
		if(amount == 0) continue;
		if(!create_empty_population_cell(state, row.pop, row.source)) {
			result.complete = false;
			continue;
		}
		auto index = find_cell_index(*store, row.source);
		if(index == std::numeric_limits<std::size_t>::max()
			|| amount > std::numeric_limits<uint64_t>::max() - store->cells[index].literal_count) {
			result.complete = false;
			continue;
		}
		auto& cell = store->cells[index];
		auto first_ordinal = cell.literal_count;
		cell.literal_count += amount;
		birth_cohort cohort;
		cohort.source_population_cell = row.source;
		cohort.first_ordinal = first_ordinal;
		cohort.count = amount;
		cohort.birth_day = state.current_date ? state.current_date.to_raw_value() - 1 : cell.bootstrap_base_day;
		cohort.home_site = deterministic_home_site(state, row.pop, {});
		if(!cohort.home_site) cohort.home_site = cell.home_site;
		cohort.culture = state.world.pop_get_culture(row.pop);
		cohort.religion = state.world.pop_get_religion(row.pop);
		cohort.pop_type = state.world.pop_get_poptype(row.pop);
		store->birth_cohorts.push_back(cohort);
		result.births += amount;
		births_remaining -= amount;
	}
	std::sort(store->birth_cohorts.begin(), store->birth_cohorts.end(), [](auto const& left, auto const& right) {
		return left.source_population_cell == right.source_population_cell
			? left.first_ordinal < right.first_ordinal
			: left.source_population_cell < right.source_population_cell;
	});
	if(births_remaining != 0) result.complete = false;
	double total_shrinking_size = 0.0;
	for(auto const& row : shrinking_rows) total_shrinking_size += row.size_change;
	auto const initial_deaths = deaths_remaining;
	for(std::size_t i = 0; i < shrinking_rows.size(); ++i) {
		if(deaths_remaining == 0) break;
		auto const& row = shrinking_rows[i];
		auto amount = i + 1 == shrinking_rows.size() || total_shrinking_size <= 0.0
			? deaths_remaining
			: std::min(deaths_remaining, uint64_t(std::floor(
				double(initial_deaths) * row.size_change / total_shrinking_size)));
		if(amount == 0) continue;
		auto index = find_cell_index(*store, row.source);
		if(index == std::numeric_limits<std::size_t>::max()) continue;
		auto retired = retire_oldest_ordinals(*store, store->cells[index], amount);
		result.deaths += retired;
		deaths_remaining -= retired;
	}
	if(deaths_remaining != 0) {
		for(auto const& cell : store->cells) {
			if(deaths_remaining == 0) break;
			auto retired = retire_oldest_ordinals(*store, cell, deaths_remaining);
			result.deaths += retired;
			deaths_remaining -= retired;
		}
	}
	if(deaths_remaining != 0) result.complete = false;
	for(auto& [source, size] : store->observed_size_by_source) size = 0.0;
	for(auto const& row : current_rows)
		store->observed_size_by_source.insert_or_assign(row.source, row.size);
	store->observed_world_literal_count = current_world_count;
	store->has_lifecycle_checkpoint = true;
	return result;
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
	auto store = ensure_store(state);
	for(auto const& descriptor : store->cells) {
		auto live = live_count_for_cell(*store, descriptor);
		if(std::numeric_limits<uint64_t>::max() - result < live)
			return std::numeric_limits<uint64_t>::max();
		result += live;
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
	return !state.exact_population || !retired_ordinal(*state.exact_population, key);
}

bool set_alive(sys::state& state, person_key key, bool value) {
	if(!exists(state, key)) return false;
	auto store = ensure_store(state);
	auto existing_override = find_override(state, key);
	if(value) {
		std::vector<person_range> remaining;
		remaining.reserve(store->retired_people.size() + 1);
		for(auto const& range : store->retired_people) {
			if(range.source_population_cell != key.source_population_cell
				|| key.ordinal < range.first_ordinal
				|| key.ordinal - range.first_ordinal >= range.count) {
				remaining.push_back(range);
				continue;
			}
			auto before = key.ordinal - range.first_ordinal;
			auto after = range.count - before - 1;
			if(before != 0) remaining.push_back({range.source_population_cell, 0, range.first_ordinal, before});
			if(after != 0) remaining.push_back({range.source_population_cell, 0, key.ordinal + 1, after});
		}
		store->retired_people = std::move(remaining);
		normalize_retired_ranges(*store);
		if(existing_override && existing_override->has_alive) {
			auto override = mutable_override(state, key);
			override->has_alive = false;
			override->alive = true;
		}
	} else {
		store->retired_people.push_back({key.source_population_cell, 0, key.ordinal, 1});
		normalize_retired_ranges(*store);
		if(existing_override && existing_override->has_alive) {
			auto override = mutable_override(state, key);
			override->has_alive = false;
			override->alive = true;
		}
	}
	remove_empty_override(state, key);
	return true;
}

persons::birth_day_index_t birth_day_index(sys::state const& state, person_key key) {
	auto descriptor = find_cell(state, key);
	if(!descriptor) return 0;
	if(state.exact_population)
		if(auto cohort = birth_cohort_for(*state.exact_population, key))
			return persons::birth_day_index_t(cohort->birth_day);
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
	if(state.exact_population)
		if(auto cohort = birth_cohort_for(*state.exact_population, key)) return cohort->home_site;
	return descriptor->home_site;
}

bool set_home_site(sys::state& state, person_key key, dcon::site_id site) {
	if(!exists(state, key) || !site || !state.world.site_is_valid(site)) return false;
	auto override = mutable_override(state, key);
	if(site == home_site(state, key)) {
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
	if(descriptor && state.exact_population)
		if(auto cohort = birth_cohort_for(*state.exact_population, key)) return cohort->culture;
	return descriptor ? descriptor->source_culture : dcon::culture_id{};
}

dcon::religion_id source_religion(sys::state const& state, person_key key) {
	auto descriptor = find_cell(state, key);
	if(descriptor && state.exact_population)
		if(auto cohort = birth_cohort_for(*state.exact_population, key)) return cohort->religion;
	return descriptor ? descriptor->source_religion : dcon::religion_id{};
}

dcon::pop_type_id source_pop_type(sys::state const& state, person_key key) {
	auto descriptor = find_cell(state, key);
	if(descriptor && state.exact_population)
		if(auto cohort = birth_cohort_for(*state.exact_population, key)) return cohort->pop_type;
	return descriptor ? descriptor->source_pop_type : dcon::pop_type_id{};
}

bool is_source_workforce_anchor(sys::state const& state, person_key key) {
	return exists(state, key) && key.ordinal % 4u == 0u;
}

std::optional<cell_descriptor> descriptor_for_cell(sys::state const& state, uint32_t source_population_cell) {
	auto descriptor = find_cell(state, source_population_cell);
	return descriptor ? std::optional<cell_descriptor>(*descriptor) : std::nullopt;
}

uint32_t source_cell_for_population(sys::state const& state, dcon::pop_id pop) {
	if(!pop || !state.world.pop_is_valid(pop) || !state.exact_population) return 0;
	auto store = state.exact_population;
	auto found = store->source_by_pop_slot.find(pop_slot(pop));
	return found == store->source_by_pop_slot.end() ? 0u : found->second;
}

dcon::pop_id population_for_source_cell(sys::state const& state, uint32_t source_population_cell) {
	if(source_population_cell == 0 || !state.exact_population) return {};
	auto store = state.exact_population;
	auto found = store->pop_slot_by_source.find(source_population_cell);
	if(found == store->pop_slot_by_source.end()) return {};
	auto pop = dcon::pop_id{dcon::pop_id::value_base_t(found->second - 1u)};
	return state.world.pop_is_valid(pop) ? pop : dcon::pop_id{};
}

void retire_population_cell(sys::state& state, dcon::pop_id pop) {
	if(!pop || !state.exact_population) return;
	auto store = state.exact_population;
	auto slot = pop_slot(pop);
	auto found = store->source_by_pop_slot.find(slot);
	if(found == store->source_by_pop_slot.end()) return;
	store->retired_source_cells.insert(found->second);
	store->pop_slot_by_source.erase(found->second);
	store->source_by_pop_slot.erase(found);
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
	state.world.person_set_source_pop_type(person, source_pop_type(state, key));
	state.world.person_set_source_culture(person, source_culture(state, key));
	state.world.person_set_source_religion(person, source_religion(state, key));
	if(auto site = home_site(state, key)) state.world.force_create_person_home_site(person, site);
	if(!alive(state, key)) state.world.person_set_alive(person, uint8_t(0));
	store->bridges.emplace(key, person);
	return person;
}

catalog_snapshot export_snapshot(sys::state const& state) {
	catalog_snapshot result;
	auto store = ensure_store(state);
	result.cells = store->cells;
	result.has_source_bindings = true;
	result.retired_source_cells.assign(store->retired_source_cells.begin(), store->retired_source_cells.end());
	std::sort(result.retired_source_cells.begin(), result.retired_source_cells.end());
	result.retired_people = store->retired_people;
	result.birth_cohorts = store->birth_cohorts;
	result.observed_world_literal_count = store->observed_world_literal_count;
	result.has_lifecycle_checkpoint = store->has_lifecycle_checkpoint;
	result.population_observations.reserve(store->observed_size_by_source.size());
	for(auto const& [source, size] : store->observed_size_by_source)
		result.population_observations.push_back({source, 0, size});
	std::sort(result.population_observations.begin(), result.population_observations.end(), [](auto const& left, auto const& right) {
		return left.source_population_cell < right.source_population_cell;
	});
	result.source_bindings.reserve(store->pop_slot_by_source.size());
	for(auto const& [source, slot] : store->pop_slot_by_source)
		result.source_bindings.push_back({source, slot});
	std::sort(result.source_bindings.begin(), result.source_bindings.end(), [](auto const& left, auto const& right) {
		return left.source_population_cell < right.source_population_cell;
	});
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
	for(auto source : snapshot.retired_source_cells) {
		if(source != 0) candidate->retired_source_cells.insert(source);
	}
	for(auto const& range : snapshot.retired_people) {
		auto descriptor = find_cell_index(*candidate, range.source_population_cell);
		if(range.reserved != 0 || descriptor == std::numeric_limits<std::size_t>::max() || range.count == 0
			|| range.first_ordinal >= candidate->cells[descriptor].literal_count
			|| range.count > candidate->cells[descriptor].literal_count - range.first_ordinal) return false;
		candidate->retired_people.push_back(range);
	}
	normalize_retired_ranges(*candidate);
	if(candidate->retired_people.size() != snapshot.retired_people.size()) return false;
	for(auto const& cohort : snapshot.birth_cohorts) {
		auto descriptor = find_cell_index(*candidate, cohort.source_population_cell);
		if(cohort.reserved != 0 || cohort.reserved_tail != 0
			|| descriptor == std::numeric_limits<std::size_t>::max() || cohort.count == 0
			|| cohort.first_ordinal >= candidate->cells[descriptor].literal_count
			|| cohort.count > candidate->cells[descriptor].literal_count - cohort.first_ordinal
			|| (state.current_date && cohort.birth_day > state.current_date.to_raw_value() - 1)
			|| (cohort.home_site && !state.world.site_is_valid(cohort.home_site))
			|| (cohort.culture && !state.world.culture_is_valid(cohort.culture))
			|| (cohort.religion && !state.world.religion_is_valid(cohort.religion))
			|| (cohort.pop_type && !state.world.pop_type_is_valid(cohort.pop_type))) return false;
		candidate->birth_cohorts.push_back(cohort);
	}
	std::sort(candidate->birth_cohorts.begin(), candidate->birth_cohorts.end(), [](auto const& left, auto const& right) {
		return left.source_population_cell == right.source_population_cell
			? left.first_ordinal < right.first_ordinal
			: left.source_population_cell < right.source_population_cell;
	});
	for(std::size_t i = 1; i < candidate->birth_cohorts.size(); ++i) {
		auto const& previous = candidate->birth_cohorts[i - 1];
		auto const& current = candidate->birth_cohorts[i];
		if(previous.source_population_cell == current.source_population_cell
			&& current.first_ordinal < previous.first_ordinal + previous.count) return false;
	}
	for(auto const& observation : snapshot.population_observations) {
		if(observation.source_population_cell == 0 || observation.reserved != 0
			|| !std::isfinite(observation.population_size) || observation.population_size < 0.0
			|| candidate->observed_size_by_source.contains(observation.source_population_cell)) return false;
		candidate->observed_size_by_source.emplace(observation.source_population_cell, observation.population_size);
	}
	candidate->observed_world_literal_count = snapshot.observed_world_literal_count;
	candidate->has_lifecycle_checkpoint = snapshot.has_lifecycle_checkpoint;
	if(snapshot.has_source_bindings) {
		for(auto const& binding : snapshot.source_bindings) {
			if(binding.source_population_cell == 0 || binding.dcon_pop_index_plus_one == 0) continue;
			auto pop = dcon::pop_id{dcon::pop_id::value_base_t(binding.dcon_pop_index_plus_one - 1u)};
			if(state.world.pop_is_valid(pop))
				(void)bind_population(*candidate, pop, binding.source_population_cell);
		}
	} else {
		candidate->legacy_unbound_catalog = true;
	}
	for(auto const& override : snapshot.overrides) {
		if(!candidate->cell_by_source.contains(override.key.source_population_cell)
			|| override.key.ordinal >= candidate->cells[candidate->cell_by_source.at(override.key.source_population_cell)].literal_count
			|| (override.has_alive && override.alive && retired_ordinal(*candidate, override.key))
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
