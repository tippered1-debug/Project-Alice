#include "exact_population.hpp"

#include "actors/ownership.hpp"
#include "system_state.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <memory>
#include <numeric>
#include <tuple>
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
	std::vector<exact_population::death_range> deaths;
	std::vector<exact_population::birth_cohort> birth_cohorts;
	std::vector<exact_population::population_membership_range> membership_ranges;
	std::vector<exact_population::military_assignment_range> military_assignments;
	std::vector<exact_population::population_transition_record> transitions;
	std::vector<exact_population::population_transfer_remainder> transfer_remainders;
	std::unordered_map<exact_population::person_key, exact_population::exact_person_override,
		exact_population::person_key_hash> overrides;
	std::unordered_map<exact_population::person_key, dcon::person_id,
		exact_population::person_key_hash> bridges;
	std::unordered_map<uint32_t, exact_population::person_key> person_by_profile;
};

std::size_t person_key_hash::operator()(person_key key) const noexcept {
	uint64_t value = uint64_t(key.source_population_cell) * 0x9e3779b97f4a7c15ULL ^ key.ordinal;
	value ^= value >> 30;
	value *= 0xbf58476d1ce4e5b9ULL;
	value ^= value >> 27;
	value *= 0x94d049bb133111ebULL;
	value ^= value >> 31;
	return std::size_t(value);
}

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
	assert(state.exact_population && "exact population store must be initialized before simulation");
	if(!state.exact_population) std::abort();
	return state.exact_population;
}

std::shared_ptr<exact_population_store> ensure_store(sys::state const& state) {
	assert(state.exact_population && "exact population store must be initialized before lookup");
	if(!state.exact_population) std::abort();
	return state.exact_population;
}

cell_descriptor const* find_cell(sys::state const& state, uint32_t source_population_cell) {
	assert(state.exact_population && "exact population store must be initialized before cell lookup");
	if(!state.exact_population) std::abort();
	if(source_population_cell == 0) return nullptr;
	auto store = state.exact_population;
	auto it = store->cell_by_source.find(source_population_cell);
	return it == store->cell_by_source.end() ? nullptr : &store->cells[it->second];
}

bool retired_ordinal(exact_population_store const& store, person_key key) {
	for(auto const& range : store.retired_people)
		if(range.source_population_cell == key.source_population_cell && key.ordinal >= range.first_ordinal && key.ordinal - range.first_ordinal < range.count) return true;
	return false;
}

uint64_t living_count_strided(exact_population_store const& store, uint32_t source_cell,
	uint64_t first_ordinal, uint64_t count, uint32_t stride) {
	if(count == 0 || stride == 0) return 0;
	auto cell = store.cell_by_source.find(source_cell);
	if(cell == store.cell_by_source.end()) return 0;
	auto const literal_count = store.cells[cell->second].literal_count;
	if(first_ordinal >= literal_count || count - 1 > (literal_count - 1 - first_ordinal) / stride) return 0;
	auto const last_ordinal = first_ordinal + (count - 1) * uint64_t(stride);
	uint64_t dead = 0;
	for(auto const& range : store.retired_people) {
		if(range.source_population_cell != source_cell) continue;
		auto const range_end = range.first_ordinal + range.count;
		if(range_end <= first_ordinal || range.first_ordinal > last_ordinal) continue;
		auto const first_match = range.first_ordinal <= first_ordinal ? 0
			: (range.first_ordinal - first_ordinal + stride - 1) / stride;
		auto const last_match = std::min<uint64_t>(count - 1,
			(range_end - 1 - first_ordinal) / stride);
		if(last_match >= first_match) dead += last_match - first_match + 1;
	}
	uint64_t live = count - std::min(count, dead);
	for(auto const& [key, override] : store.overrides) {
		if(key.source_population_cell != source_cell || !override.has_alive
			|| key.ordinal < first_ordinal || key.ordinal > last_ordinal
			|| (key.ordinal - first_ordinal) % stride != 0) continue;
		auto const default_alive = !retired_ordinal(store, key);
		if(override.alive && !default_alive) ++live;
		else if(!override.alive && default_alive && live != 0) --live;
	}
	return live;
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
		if(!merged.empty() && merged.back().source_population_cell == range.source_population_cell && range.first_ordinal <= merged.back().first_ordinal + merged.back().count) {
			auto end = std::max(merged.back().first_ordinal + merged.back().count,
				range.first_ordinal + range.count);
			merged.back().count = end - merged.back().first_ordinal;
		} else {
			merged.push_back(range);
		}
	}
	store.retired_people = std::move(merged);
}

void normalize_death_ranges(exact_population_store& store) {
	std::sort(store.deaths.begin(), store.deaths.end(), [](auto const& left, auto const& right) {
		if(left.source_population_cell != right.source_population_cell)
			return left.source_population_cell < right.source_population_cell;
		if(left.first_ordinal != right.first_ordinal)
			return left.first_ordinal < right.first_ordinal;
		if(left.death_day != right.death_day) return left.death_day < right.death_day;
		return left.cause < right.cause;
	});
	std::vector<death_range> merged;
	merged.reserve(store.deaths.size());
	for(auto const& range : store.deaths) {
		if(range.count == 0) continue;
		if(!merged.empty() && merged.back().source_population_cell == range.source_population_cell
			&& merged.back().death_day == range.death_day && merged.back().cause == range.cause
			&& range.first_ordinal == merged.back().first_ordinal + merged.back().count) {
			merged.back().count += range.count;
		} else {
			merged.push_back(range);
		}
	}
	store.deaths = std::move(merged);
}

void normalize_membership_ranges(exact_population_store& store) {
	std::sort(store.membership_ranges.begin(), store.membership_ranges.end(), [](auto const& left, auto const& right) {
		return left.identity_population_cell == right.identity_population_cell
			? left.first_ordinal < right.first_ordinal
			: left.identity_population_cell < right.identity_population_cell;
	});
	std::vector<population_membership_range> merged;
	merged.reserve(store.membership_ranges.size());
	for(auto const& range : store.membership_ranges) {
		if(range.count == 0) continue;
		if(!merged.empty() && merged.back().identity_population_cell == range.identity_population_cell && merged.back().current_population_cell == range.current_population_cell && range.first_ordinal == merged.back().first_ordinal + merged.back().count) {
			merged.back().count += range.count;
		} else {
			merged.push_back(range);
		}
	}
	store.membership_ranges = std::move(merged);
}

void normalize_military_assignments(exact_population_store& store) {
	std::sort(store.military_assignments.begin(), store.military_assignments.end(), [](auto const& left, auto const& right) {
		return std::tie(left.source_population_cell, left.first_ordinal, left.ordinal_stride,
			left.formation_id, left.training_days_remaining)
			< std::tie(right.source_population_cell, right.first_ordinal, right.ordinal_stride,
				right.formation_id, right.training_days_remaining);
	});
	std::vector<exact_population::military_assignment_range> merged;
	merged.reserve(store.military_assignments.size());
	for(auto const& range : store.military_assignments) {
		if(range.count == 0) continue;
		if(!merged.empty()) {
			auto& previous = merged.back();
			auto const can_multiply = previous.ordinal_stride != 0
				&& previous.count <= std::numeric_limits<uint64_t>::max() / previous.ordinal_stride;
			bool const compatible = can_multiply
				&& previous.source_population_cell == range.source_population_cell
				&& previous.ordinal_stride == range.ordinal_stride
				&& previous.formation_id == range.formation_id
				&& previous.training_days_remaining == range.training_days_remaining
				&& previous.first_ordinal <= std::numeric_limits<uint64_t>::max()
					- previous.count * uint64_t(previous.ordinal_stride)
				&& previous.first_ordinal + previous.count * uint64_t(previous.ordinal_stride)
					== range.first_ordinal;
			if(compatible) {
				previous.count += range.count;
				continue;
			}
		}
		merged.push_back(range);
	}
	store.military_assignments = std::move(merged);
}

exact_population::military_assignment_range const* assignment_range_for(
	exact_population_store const& store, exact_population::person_key key) {
	auto found = std::upper_bound(store.military_assignments.begin(), store.military_assignments.end(), key,
		[](exact_population::person_key value, exact_population::military_assignment_range const& range) {
			return std::tie(value.source_population_cell, value.ordinal)
				< std::tie(range.source_population_cell, range.first_ordinal);
		});
	if(found == store.military_assignments.begin()) return nullptr;
	--found;
	if(found->source_population_cell != key.source_population_cell || found->ordinal_stride == 0
		|| key.ordinal < found->first_ordinal) return nullptr;
	auto const delta = key.ordinal - found->first_ordinal;
	return delta % found->ordinal_stride == 0 && delta / found->ordinal_stride < found->count
		? &*found : nullptr;
}

uint64_t living_count_in_range(exact_population_store const& store, uint32_t identity_cell,
	uint64_t first_ordinal, uint64_t count) {
	auto const end_ordinal = first_ordinal + count;
	uint64_t live = count;
	for(auto const& range : store.retired_people) {
		if(range.source_population_cell != identity_cell) continue;
		auto const range_end = range.first_ordinal + range.count;
		auto const overlap_start = std::max(first_ordinal, range.first_ordinal);
		auto const overlap_end = std::min(end_ordinal, range_end);
		if(overlap_end > overlap_start) live -= overlap_end - overlap_start;
	}
	for(auto const& [key, override] : store.overrides) {
		if(key.source_population_cell != identity_cell || !override.has_alive || key.ordinal < first_ordinal || key.ordinal >= end_ordinal) continue;
		auto default_alive = !retired_ordinal(store, key);
		if(override.alive && !default_alive && live != std::numeric_limits<uint64_t>::max()) ++live;
		else if(!override.alive && default_alive && live != 0) --live;
	}
	return live;
}

population_membership_range const* membership_range_for(exact_population_store const& store, person_key key) {
	auto found = std::upper_bound(store.membership_ranges.begin(), store.membership_ranges.end(), key,
		[](person_key value, population_membership_range const& range) {
			return value.source_population_cell == range.identity_population_cell
				? value.ordinal < range.first_ordinal
				: value.source_population_cell < range.identity_population_cell;
		});
	if(found == store.membership_ranges.begin()) return nullptr;
	--found;
	return found->identity_population_cell == key.source_population_cell
		&& key.ordinal >= found->first_ordinal
		&& key.ordinal - found->first_ordinal < found->count ? &*found : nullptr;
}

void append_default_membership(exact_population_store& store, cell_descriptor const& descriptor,
	uint64_t first_ordinal, uint64_t count) {
	if(count == 0) return;
	store.membership_ranges.push_back({descriptor.source_population_cell,
		descriptor.source_population_cell, first_ordinal, count});
	normalize_membership_ranges(store);
}

uint64_t living_count_in_population_cell(exact_population_store const& store, uint32_t current_cell) {
	uint64_t result = 0;
	for(auto const& range : store.membership_ranges) {
		if(range.current_population_cell != current_cell) continue;
		auto count = living_count_in_range(store, range.identity_population_cell,
			range.first_ordinal, range.count);
		if(std::numeric_limits<uint64_t>::max() - result < count)
			return std::numeric_limits<uint64_t>::max();
		result += count;
	}
	return result;
}

std::vector<person_range> living_ranges_in_interval(exact_population_store const& store,
	uint32_t identity_cell, uint64_t first_ordinal, uint64_t count) {
	std::vector<person_range> blocked;
	auto const end_ordinal = first_ordinal + count;
	for(auto const& range : store.retired_people) {
		if(range.source_population_cell != identity_cell) continue;
		auto start = std::max(first_ordinal, range.first_ordinal);
		auto end = std::min(end_ordinal, range.first_ordinal + range.count);
		if(end > start) blocked.push_back({identity_cell, 0, start, end - start});
	}
	for(auto const& [key, override] : store.overrides) {
		if(key.source_population_cell == identity_cell && override.has_alive && !override.alive && key.ordinal >= first_ordinal && key.ordinal < end_ordinal && !retired_ordinal(store, key))
			blocked.push_back({identity_cell, 0, key.ordinal, 1});
	}
	for(auto const& assignment : store.military_assignments) {
		if(assignment.source_population_cell != identity_cell || assignment.count == 0
			|| assignment.ordinal_stride == 0) continue;
		auto const last = assignment.first_ordinal
			+ (assignment.count - 1) * uint64_t(assignment.ordinal_stride);
		if(last < first_ordinal || assignment.first_ordinal >= end_ordinal) continue;
		auto const first_index = assignment.first_ordinal < first_ordinal
			? (first_ordinal - assignment.first_ordinal + assignment.ordinal_stride - 1) / assignment.ordinal_stride : 0;
		for(auto index = first_index; index < assignment.count; ++index) {
			auto ordinal = assignment.first_ordinal + index * uint64_t(assignment.ordinal_stride);
			if(ordinal >= end_ordinal) break;
			if(ordinal >= first_ordinal && !retired_ordinal(store, {identity_cell, ordinal}))
				blocked.push_back({identity_cell, 0, ordinal, 1});
		}
	}
	std::sort(blocked.begin(), blocked.end(), [](auto const& left, auto const& right) {
		return left.first_ordinal < right.first_ordinal;
	});
	std::vector<person_range> merged_blocked;
	for(auto const& range : blocked) {
		if(!merged_blocked.empty() && range.first_ordinal <= merged_blocked.back().first_ordinal + merged_blocked.back().count) {
			auto end = std::max(merged_blocked.back().first_ordinal + merged_blocked.back().count,
				range.first_ordinal + range.count);
			merged_blocked.back().count = end - merged_blocked.back().first_ordinal;
		} else {
			merged_blocked.push_back(range);
		}
	}
	std::vector<person_range> result;
	auto cursor = first_ordinal;
	for(auto const& range : merged_blocked) {
		if(range.first_ordinal > cursor)
			result.push_back({identity_cell, 0, cursor, range.first_ordinal - cursor});
		cursor = std::max(cursor, range.first_ordinal + range.count);
	}
	if(cursor < end_ordinal) result.push_back({identity_cell, 0, cursor, end_ordinal - cursor});
	return result;
}

std::vector<person_range> retire_oldest_members_in_cell(exact_population_store& store,
	uint32_t current_cell, uint64_t requested) {
	std::vector<person_range> retired_ranges;
	if(requested == 0) return retired_ranges;
	auto remaining = std::min(requested, living_count_in_population_cell(store, current_cell));
	auto const initially_available = remaining;
	std::vector<population_membership_range> ranges;
	for(auto const& range : store.membership_ranges)
		if(range.current_population_cell == current_cell) ranges.push_back(range);
	for(auto const& range : ranges) {
		if(remaining == 0) break;
		for(auto const& live : living_ranges_in_interval(store, range.identity_population_cell,
			range.first_ordinal, range.count)) {
			if(remaining == 0) break;
			auto count = std::min(remaining, live.count);
			person_range newly_retired{live.source_population_cell, 0, live.first_ordinal, count};
			retired_ranges.push_back(newly_retired);
			store.retired_people.push_back(newly_retired);
			remaining -= count;
		}
	}
	normalize_retired_ranges(store);
	if(initially_available != remaining) {
		std::sort(retired_ranges.begin(), retired_ranges.end(), [](auto const& left, auto const& right) {
			return left.source_population_cell == right.source_population_cell
				? left.first_ordinal < right.first_ordinal
				: left.source_population_cell < right.source_population_cell;
		});
	}
	return retired_ranges;
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

bool ensure_population_descriptor(sys::state& state, dcon::pop_id pop, double initial_size);

uint32_t source_cell_for_population(sys::state& state, dcon::pop_id pop) {
	if(!pop || !state.world.pop_is_valid(pop)) return 0;
	assert(state.exact_population && "exact population store must be initialized before population lookup");
	if(!state.exact_population) std::abort();
	auto const& store = *state.exact_population;
	auto slot = pop_slot(pop);
	auto found = store.source_by_pop_slot.find(slot);
	return found == store.source_by_pop_slot.end() ? 0u : found->second;
}

bool bind_new_population(sys::state& state, dcon::pop_id pop) {
	assert(state.exact_population && "exact population store must be initialized before registering a population");
	if(!state.exact_population) std::abort();
	auto store = ensure_store(state);
	auto slot = pop_slot(pop);
	if(slot == 0) return false;
	if(store->source_by_pop_slot.contains(slot)) return true;
	auto const source = slot;
	if(!store->cell_by_source.contains(source) && !store->pop_slot_by_source.contains(source) && bind_population(*store, pop, source)) return true;
	auto maximum = maximum_source_cell(*store);
	return maximum != std::numeric_limits<uint32_t>::max()
		&& bind_population(*store, pop, maximum + 1u);
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

bool create_empty_population_cell(sys::state& state, dcon::pop_id pop, uint32_t source_cell) {
	if(!pop || !state.world.pop_is_valid(pop) || source_cell == 0) return false;
	if(find_cell(state, source_cell)) return true;
	if(source_cell_for_population(state, pop) != source_cell && !bind_new_population(state, pop)) return false;
	if(source_cell_for_population(state, pop) != source_cell) return false;
	return ensure_population_descriptor(state, pop, 0.0);
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
	if(descriptor.source_population_cell == 0 || descriptor.bootstrap_version != bootstrap_semantics_version || descriptor.demographic_policy != demographic_policy_version)
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
	append_default_membership(*store, descriptor, 0, descriptor.literal_count);
	result.result = status::created;
	result.descriptor = descriptor;
	return result;
}

bool ensure_population_descriptor(sys::state& state, dcon::pop_id pop, double initial_size) {
	if(!pop || !state.world.pop_is_valid(pop) || !std::isfinite(initial_size) || initial_size < 0.0)
		return false;
	auto source = source_cell_for_population(state, pop);
	if(source == 0) return false;
	if(find_cell(state, source)) return true;
	auto literal = std::floor(initial_size * 4.0);
	if(literal >= double(std::numeric_limits<uint64_t>::max())) return false;
	cell_descriptor descriptor;
	descriptor.source_population_cell = source;
	descriptor.literal_count = uint64_t(literal);
	descriptor.bootstrap_base_day = state.current_date ? state.current_date.to_raw_value() - 1 : 0;
	descriptor.demographic_seed = uint64_t(source) * 0x9e3779b97f4a7c15ULL;
	descriptor.home_site = deterministic_home_site(state, pop, {});
	descriptor.source_culture = state.world.pop_get_culture(pop);
	descriptor.source_religion = state.world.pop_get_religion(pop);
	descriptor.source_pop_type = state.world.pop_get_poptype(pop);
	return descriptor.home_site && add_descriptor(state, descriptor).result == status::created;
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

registration_result register_population_cell(sys::state& state, dcon::pop_id pop, dcon::site_id requested_home_site) {
	registration_result result;
	if(!pop || !state.world.pop_is_valid(pop)) return result;
	if(!source_cell_for_population(state, pop) && !bind_new_population(state, pop)) {
		result.result = status::overflow;
		return result;
	}
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

void initialize_empty_store(sys::state& state) {
	if(!state.exact_population) state.exact_population = std::make_shared<exact_population_store>();
}

world_bootstrap_result bootstrap_from_current_pops(sys::state& state) {
	world_bootstrap_result result;
	if(!state.exact_population) state.exact_population = std::make_shared<exact_population_store>();
	std::vector<dcon::pop_id> pops;
	state.world.for_each_pop([&](auto pop) { pops.push_back(pop); });
	std::sort(pops.begin(), pops.end(), [](auto left, auto right) {
		return left.index() < right.index();
	});
	for(auto pop : pops) {
		auto registration = register_population_cell(state, pop);
		if(registration.result != status::created && registration.result != status::already_registered) {
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
	return result;
}

population_transfer_result transfer_population_membership(sys::state& state,
	dcon::pop_id source, dcon::pop_id destination, float population_amount,
	population_transition_cause cause) {
	population_transfer_result result;
	if(!source || !destination || source == destination || !state.world.pop_is_valid(source) || !state.world.pop_is_valid(destination) || !std::isfinite(population_amount) || population_amount <= 0.0f) return result;
	auto source_cell = source_cell_for_population(state, source);
	auto destination_cell = source_cell_for_population(state, destination);
	if(source_cell == 0 || destination_cell == 0 || source_cell == destination_cell || !find_cell(state, source_cell) || !find_cell(state, destination_cell)) {
		result.complete = false;
		return result;
	}
	auto store = ensure_store(state);
	auto remainder = std::find_if(store->transfer_remainders.begin(), store->transfer_remainders.end(),
		[&](auto const& record) {
			return record.from_population_cell == source_cell
				&& record.to_population_cell == destination_cell;
		});
	if(remainder == store->transfer_remainders.end()) {
		store->transfer_remainders.push_back({source_cell, destination_cell, 0.0});
		remainder = std::prev(store->transfer_remainders.end());
	}
	auto total_people = double(population_amount) * 4.0 + remainder->pending_people;
	if(!std::isfinite(total_people) || total_people < 0.0 || total_people >= double(std::numeric_limits<uint64_t>::max())) {
		result.complete = false;
		return result;
	}
	auto requested = uint64_t(std::floor(total_people));
	auto unrepresented = total_people - double(requested);
	auto available = living_count_in_population_cell(*store, source_cell);
	if(requested > available) {
		remainder->pending_people = unrepresented;
		result.complete = false;
		return result;
	}
	auto to_move = std::min(requested, available);
	std::vector<population_membership_range> updated;
	updated.reserve(store->membership_ranges.size() + 8);
	auto remaining = to_move;
	std::vector<population_transition_record> new_transitions;
	for(auto const& range : store->membership_ranges) {
		if(range.current_population_cell != source_cell || remaining == 0) {
			updated.push_back(range);
			continue;
		}
		auto cursor = range.first_ordinal;
		auto end = range.first_ordinal + range.count;
		for(auto const& living : living_ranges_in_interval(*store, range.identity_population_cell,
			range.first_ordinal, range.count)) {
			if(living.first_ordinal > cursor)
				updated.push_back({range.identity_population_cell, source_cell, cursor,
					living.first_ordinal - cursor});
			auto moved = std::min(remaining, living.count);
			if(moved != 0) {
				updated.push_back({range.identity_population_cell, destination_cell,
					living.first_ordinal, moved});
				population_transition_record event;
				event.identity_population_cell = range.identity_population_cell;
				event.from_population_cell = source_cell;
				event.to_population_cell = destination_cell;
				event.cause = uint8_t(cause);
				event.first_ordinal = living.first_ordinal;
				event.count = moved;
				event.transition_day = state.current_date ? state.current_date.to_raw_value() - 1 : 0;
				new_transitions.push_back(event);
				remaining -= moved;
			}
			if(moved < living.count)
				updated.push_back({range.identity_population_cell, source_cell,
					living.first_ordinal + moved, living.count - moved});
			cursor = living.first_ordinal + living.count;
		}
		if(cursor < end)
			updated.push_back({range.identity_population_cell, source_cell, cursor, end - cursor});
	}
	store->membership_ranges = std::move(updated);
	normalize_membership_ranges(*store);
	for(auto const& event : new_transitions) {
		if(!store->transitions.empty()) {
			auto& previous = store->transitions.back();
			if(previous.identity_population_cell == event.identity_population_cell && previous.from_population_cell == event.from_population_cell && previous.to_population_cell == event.to_population_cell && previous.cause == event.cause && previous.transition_day == event.transition_day && previous.first_ordinal + previous.count == event.first_ordinal) {
				previous.count += event.count;
				continue;
			}
		}
		store->transitions.push_back(event);
	}
	// Carry only the sub-person fraction forward. If the identity catalog is
	// short, report that mismatch instead of silently borrowing future people.
	remainder->pending_people = unrepresented;
	result.people_moved = to_move;
	result.complete = true;
	if(!project_population_membership(state)) result.complete = false;
	return result;
}

int64_t adjust_population_size(sys::state& state, dcon::pop_id pop, double size_delta) {
	return persons::adjust_population_size(state, pop, size_delta);
}

population_adjustment_result adjust_population_size_storage(sys::state& state, dcon::pop_id pop,
	double size_delta, int32_t death_day, uint8_t cause) {
	population_adjustment_result result;
	assert(state.exact_population && "exact population must be initialized before demographic simulation");
	if(!state.exact_population) std::abort();
	if(!pop || !state.world.pop_is_valid(pop) || !std::isfinite(size_delta)) return result;
	auto const source = source_cell_for_population(state, pop);
	if(source == 0) {
		assert(false && "population row is missing its exact identity binding");
		std::abort();
	}
	auto store = ensure_store(state);
	auto index = find_cell_index(*store, source);
	if(index == std::numeric_limits<std::size_t>::max()) {
		assert(false && "population row is missing its exact population cell");
		std::abort();
	}
	long double const exact_delta = std::round(static_cast<long double>(size_delta) * 4.0L);
	if(exact_delta > static_cast<long double>(std::numeric_limits<int64_t>::max()) || exact_delta < static_cast<long double>(std::numeric_limits<int64_t>::min())) return result;
	auto delta = int64_t(exact_delta);
	if(delta > 0) {
		auto const amount = uint64_t(delta);
		auto& cell = store->cells[index];
		if(amount > std::numeric_limits<uint64_t>::max() - cell.literal_count) return result;
		auto const first = cell.literal_count;
		cell.literal_count += amount;
		append_default_membership(*store, cell, first, amount);
		birth_cohort cohort;
		cohort.source_population_cell = source;
		cohort.first_ordinal = first;
		cohort.count = amount;
		cohort.birth_day = state.current_date ? state.current_date.to_raw_value() - 1 : cell.bootstrap_base_day;
		cohort.home_site = deterministic_home_site(state, pop, {});
		cohort.culture = state.world.pop_get_culture(pop);
		cohort.religion = state.world.pop_get_religion(pop);
		cohort.pop_type = state.world.pop_get_poptype(pop);
		store->birth_cohorts.push_back(cohort);
		std::sort(store->birth_cohorts.begin(), store->birth_cohorts.end(), [](auto const& left, auto const& right) {
			return left.source_population_cell == right.source_population_cell
				? left.first_ordinal < right.first_ordinal
				: left.source_population_cell < right.source_population_cell;
		});
		result.changed = delta;
	} else if(delta < 0) {
		auto const requested = uint64_t(-(delta + 1)) + 1u;
		result.deaths = retire_oldest_members_in_cell(*store, source, requested);
		uint64_t retired = 0;
		for(auto const& range : result.deaths) {
			retired += range.count;
			store->deaths.push_back({range.source_population_cell, 0, range.first_ordinal,
				range.count, death_day, cause, {}});
		}
		normalize_death_ranges(*store);
		result.changed = -int64_t(retired);
	} else {
		result.changed = 0;
	}
	return result;
}

bool transfer_population_person_membership(sys::state& state, person_key key,
	dcon::pop_id destination, population_transition_cause cause) {
	if(!persons::exact_population::exists(state, key) || !persons::exact_population::alive(state, key) || !destination || !state.world.pop_is_valid(destination)) return false;
	assert(state.exact_population && "exact population store must be initialized before transfer");
	auto membership = membership_range_for(*state.exact_population, key);
	auto destination_cell = source_cell_for_population(state, destination);
	if(!membership || membership->current_population_cell == 0 || destination_cell == 0 || membership->current_population_cell == destination_cell || !find_cell(state, destination_cell)) return false;
	auto const old_range = *membership;
	auto const source_cell = old_range.current_population_cell;
	auto store = ensure_store(state);
	std::vector<population_membership_range> updated;
	updated.reserve(store->membership_ranges.size() + 2);
	bool replaced = false;
	for(auto const& range : store->membership_ranges) {
		if(!replaced && range.identity_population_cell == key.source_population_cell && range.current_population_cell == source_cell && key.ordinal >= range.first_ordinal && key.ordinal - range.first_ordinal < range.count) {
			if(key.ordinal > range.first_ordinal)
				updated.push_back({range.identity_population_cell, source_cell,
					range.first_ordinal, key.ordinal - range.first_ordinal});
			updated.push_back({range.identity_population_cell, destination_cell, key.ordinal, 1});
			auto const after = range.first_ordinal + range.count - key.ordinal - 1;
			if(after != 0)
				updated.push_back({range.identity_population_cell, source_cell, key.ordinal + 1, after});
			replaced = true;
		} else {
			updated.push_back(range);
		}
	}
	if(!replaced) return false;
	store->membership_ranges = std::move(updated);
	normalize_membership_ranges(*store);
	if(!project_population_membership(state)) return false;
	population_transition_record event;
	event.identity_population_cell = key.source_population_cell;
	event.from_population_cell = source_cell;
	event.to_population_cell = destination_cell;
	event.cause = uint8_t(cause);
	event.first_ordinal = key.ordinal;
	event.count = 1;
	event.transition_day = state.current_date ? state.current_date.to_raw_value() - 1 : 0;
	if(!store->transitions.empty()) {
		auto& previous = store->transitions.back();
		if(previous.identity_population_cell == event.identity_population_cell && previous.from_population_cell == event.from_population_cell && previous.to_population_cell == event.to_population_cell && previous.cause == event.cause && previous.transition_day == event.transition_day && previous.first_ordinal + previous.count == event.first_ordinal) {
			++previous.count;
			return true;
		}
	}
	store->transitions.push_back(event);
	return true;
}

uint32_t current_population_cell(sys::state const& state, person_key key) {
	assert(state.exact_population && "exact population must exist for person lookup");
	if(!state.exact_population) std::abort();
	assert(persons::exact_population::exists(state, key) && "person key must resolve in canonical population");
	if(!persons::exact_population::exists(state, key)) std::abort();
	auto range = membership_range_for(*state.exact_population, key);
	assert(range && "person must have a canonical membership range");
	if(!range) std::abort();
	return range->current_population_cell;
}

dcon::pop_id current_population_for_person(sys::state const& state, person_key key) {
	return population_for_source_cell(state, persons::exact_population::current_population_cell(state, key));
}

person_key first_living_person_in_population(sys::state const& state, dcon::pop_id population) {
	auto current_cell = source_cell_for_population(state, population);
	if(current_cell == 0) return {};
	auto store = ensure_store(state);
	for(auto const& range : store->membership_ranges) {
		if(range.current_population_cell != current_cell) continue;
		std::vector<person_range> dead;
		auto const end = range.first_ordinal + range.count;
		for(auto const& retired : store->retired_people) {
			if(retired.source_population_cell != range.identity_population_cell) continue;
			auto first = std::max(range.first_ordinal, retired.first_ordinal);
			auto last = std::min(end, retired.first_ordinal + retired.count);
			if(last > first) dead.push_back({range.identity_population_cell, 0, first, last - first});
		}
		for(auto const& [key, override] : store->overrides)
			if(key.source_population_cell == range.identity_population_cell && override.has_alive
				&& !override.alive && key.ordinal >= range.first_ordinal && key.ordinal < end
				&& !retired_ordinal(*store, key))
				dead.push_back({range.identity_population_cell, 0, key.ordinal, 1});
		std::sort(dead.begin(), dead.end(), [](auto const& a, auto const& b) {
			return a.first_ordinal < b.first_ordinal;
		});
		auto cursor = range.first_ordinal;
		for(auto const& blocked : dead) {
			if(blocked.first_ordinal > cursor) break;
			cursor = std::max(cursor, blocked.first_ordinal + blocked.count);
			if(cursor >= end) break;
		}
		if(cursor < end) return {range.identity_population_cell, cursor};
	}
	return {};
}

uint64_t living_people_in_population_cell(sys::state const& state, uint32_t population_cell) {
	assert(state.exact_population && "exact population must exist for count lookup");
	if(!state.exact_population) std::abort();
	assert(find_cell(state, population_cell) && "population cell must exist in canonical population");
	if(!find_cell(state, population_cell)) std::abort();
	return living_count_in_population_cell(*state.exact_population, population_cell);
}

bool project_population_membership(sys::state& state) {
	std::vector<std::pair<dcon::pop_id, float>> projected_sizes;
	bool complete = true;
	state.world.for_each_pop([&](auto pop) {
		auto source = source_cell_for_population(state, pop);
		if(source == 0 || !find_cell(state, source)) {
			complete = false;
			return;
		}
		auto people = living_count_in_population_cell(*ensure_store(state), source);
		auto size = double(people) / 4.0;
		if(!std::isfinite(size) || size > double(std::numeric_limits<float>::max())) {
			complete = false;
			return;
		}
		projected_sizes.emplace_back(pop, float(size));
	});
	if(!complete) return false;
	for(auto const& [pop, size] : projected_sizes) state.world.pop_set_size(pop, size);
	return true;
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
	assert(state.exact_population && "exact population must exist for person lookup");
	if(!state.exact_population) std::abort();
	return find_cell(state, key) != nullptr;
}

bool alive(sys::state const& state, person_key key) {
	assert(state.exact_population && "exact population must exist for person state lookup");
	if(!state.exact_population) std::abort();
	if(!persons::exact_population::exists(state, key)) return false;
	if(auto override = find_override(state, key); override && override->has_alive) return override->alive;
	return !retired_ordinal(*state.exact_population, key);
}

uint64_t living_people_in_person_range(sys::state const& state, uint32_t source_population_cell,
	uint64_t first_ordinal, uint64_t count, uint32_t ordinal_stride) {
	assert(state.exact_population && "exact population must exist for range lookup");
	if(!state.exact_population) std::abort();
	return living_count_strided(*state.exact_population, source_population_cell,
		first_ordinal, count, ordinal_stride);
}

std::vector<military_assignment_range> military_assignments_for_formation(
	sys::state const& state, uint64_t formation_id) {
	std::vector<military_assignment_range> result;
	for(auto const& assignment : ensure_store(state)->military_assignments)
		if(assignment.formation_id == formation_id) result.push_back(assignment);
	return result;
}

uint64_t military_personnel_count(sys::state const& state, uint64_t formation_id) {
	uint64_t result = 0;
	for(auto const& assignment : ensure_store(state)->military_assignments)
		if(assignment.formation_id == formation_id) result += assignment.count;
	return result;
}

std::vector<military_assignment_range> all_military_assignments(sys::state const& state) {
	return ensure_store(state)->military_assignments;
}

bool has_military_assignment(sys::state const& state, person_key key, uint64_t* formation_id) {
	auto assignment = assignment_range_for(*ensure_store(state), key);
	if(assignment && formation_id) *formation_id = assignment->formation_id;
	return assignment != nullptr;
}

bool assign_military_range(sys::state& state, military_assignment_range const& range) {
	if(range.formation_id == 0 || range.source_population_cell == 0 || range.count == 0
		|| (range.ordinal_stride != 1 && range.ordinal_stride != 4)) return false;
	auto descriptor = find_cell(state, range.source_population_cell);
	if(!descriptor || range.first_ordinal >= descriptor->literal_count
		|| range.count - 1 > (descriptor->literal_count - 1 - range.first_ordinal) / range.ordinal_stride
		|| living_people_in_person_range(state, range.source_population_cell, range.first_ordinal,
			range.count, range.ordinal_stride) != range.count) return false;
	auto store = ensure_store(state);
	auto last = range.first_ordinal + (range.count - 1) * uint64_t(range.ordinal_stride);
	for(auto const& existing : store->military_assignments) {
		if(existing.source_population_cell != range.source_population_cell) continue;
		auto existing_last = existing.first_ordinal + (existing.count - 1) * uint64_t(existing.ordinal_stride);
		auto low = std::max(range.first_ordinal, existing.first_ordinal);
		auto high = std::min(last, existing_last);
		if(low > high) continue;
		auto period = std::lcm(range.ordinal_stride, existing.ordinal_stride);
		for(uint64_t candidate = low; candidate <= std::min<uint64_t>(high, low + period); ++candidate)
			if((candidate - range.first_ordinal) % range.ordinal_stride == 0
				&& (candidate - existing.first_ordinal) % existing.ordinal_stride == 0) return false;
	}
	store->military_assignments.push_back(range);
	normalize_military_assignments(*store);
	return true;
}

bool unassign_military_person(sys::state& state, person_key key, uint64_t expected_formation_id) {
	auto store = ensure_store(state);
	auto assignment = assignment_range_for(*store, key);
	if(!assignment || assignment->formation_id != expected_formation_id) return false;
	auto old = *assignment;
	auto const offset = (key.ordinal - old.first_ordinal) / old.ordinal_stride;
	std::vector<military_assignment_range> updated;
	updated.reserve(store->military_assignments.size() + 1);
	for(auto const& range : store->military_assignments) {
		if(&range != assignment) { updated.push_back(range); continue; }
		if(offset != 0) { auto before = old; before.count = offset; updated.push_back(before); }
		if(offset + 1 < old.count) {
			auto after = old;
			after.first_ordinal = key.ordinal + old.ordinal_stride;
			after.count = old.count - offset - 1;
			updated.push_back(after);
		}
	}
	store->military_assignments = std::move(updated);
	normalize_military_assignments(*store);
	return true;
}

uint64_t unassign_all_military_personnel(sys::state& state, uint64_t formation_id) {
	auto store = ensure_store(state);
	uint64_t count = 0;
	for(auto const& assignment : store->military_assignments) {
		if(assignment.formation_id != formation_id) continue;
		if(std::numeric_limits<uint64_t>::max() - count < assignment.count) return 0;
		count += assignment.count;
	}
	std::erase_if(store->military_assignments, [&](auto const& assignment) {
		return assignment.formation_id == formation_id;
	});
	return count;
}

void advance_military_training(sys::state& state) {
	auto store = ensure_store(state);
	for(auto& assignment : store->military_assignments)
		if(assignment.training_days_remaining != 0) --assignment.training_days_remaining;
	normalize_military_assignments(*store);
}

bool set_alive(sys::state& state, person_key key, bool value) {
	if(!persons::exact_population::exists(state, key)) return false;
	if(value) return persons::exact_population::alive(state, key);
	return persons::kill_person(state, key, state.current_date, persons::death_cause::unspecified);
}

bool retire_person(sys::state& state, person_key key, int32_t death_day, uint8_t cause) {
	if(!persons::exact_population::exists(state, key) || !persons::exact_population::alive(state, key) || cause > uint8_t(persons::death_cause::attrition)) return false;
	if(has_military_assignment(state, key)) return false;
	auto store = ensure_store(state);
	auto existing_override = find_override(state, key);
	store->retired_people.push_back({key.source_population_cell, 0, key.ordinal, 1});
	normalize_retired_ranges(*store);
	store->deaths.push_back({key.source_population_cell, 0, key.ordinal, 1, death_day, cause, {}});
	normalize_death_ranges(*store);
	if(existing_override && existing_override->has_alive) {
		auto override = mutable_override(state, key);
		override->has_alive = false;
		override->alive = true;
	}
	remove_empty_override(state, key);
	return true;
}

int32_t death_day_index(sys::state const& state, person_key key) {
	if(!persons::exact_population::exists(state, key)) return std::numeric_limits<int32_t>::min();
	for(auto const& death : ensure_store(state)->deaths)
		if(death.source_population_cell == key.source_population_cell
			&& key.ordinal >= death.first_ordinal && key.ordinal - death.first_ordinal < death.count)
			return death.death_day;
	return std::numeric_limits<int32_t>::min();
}

persons::birth_day_index_t birth_day_index(sys::state const& state, person_key key) {
	auto descriptor = find_cell(state, key);
	if(!descriptor) return 0;
	assert(state.exact_population && "exact population must exist for birth date lookup");
	if(auto cohort = birth_cohort_for(*state.exact_population, key))
		return persons::birth_day_index_t(cohort->birth_day);
	return persons::birth_day_index_t(clamp_day(int64_t(descriptor->bootstrap_base_day)
		- int64_t(bootstrap_age_days(*descriptor, key))));
}

int32_t age_days(sys::state const& state, person_key key, sys::date current_day) {
	if(!persons::exact_population::exists(state, key) || !current_day) return -1;
	return clamp_day(int64_t(current_day.to_raw_value() - 1) - int64_t(persons::exact_population::birth_day_index(state, key)));
}

int32_t age_years(sys::state const& state, person_key key, sys::date current_day) {
	auto days = persons::exact_population::age_days(state, key, current_day);
	return days < 0 ? -1 : days / 365;
}

dcon::site_id home_site(sys::state const& state, person_key key) {
	auto descriptor = find_cell(state, persons::exact_population::current_population_cell(state, key));
	if(!descriptor) return {};
	if(auto override = find_override(state, key); override && override->has_home_site)
		return override->home_site;
	return descriptor->home_site;
}

bool set_home_site(sys::state& state, person_key key, dcon::site_id site) {
	return persons::set_home_site(state, key, site);
}

bool set_home_site_storage(sys::state& state, person_key key, dcon::site_id site) {
	if(!persons::exact_population::exists(state, key) || !site || !state.world.site_is_valid(site)) return false;
	auto override = mutable_override(state, key);
	if(site == persons::exact_population::home_site(state, key)) {
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

dcon::culture_id current_culture(sys::state const& state, person_key key) {
	if(!persons::exact_population::exists(state, key) || !persons::exact_population::alive(state, key)) return {};
	auto descriptor = find_cell(state, persons::exact_population::current_population_cell(state, key));
	return descriptor ? descriptor->source_culture : dcon::culture_id{};
}

dcon::religion_id current_religion(sys::state const& state, person_key key) {
	if(!persons::exact_population::exists(state, key) || !persons::exact_population::alive(state, key)) return {};
	auto descriptor = find_cell(state, persons::exact_population::current_population_cell(state, key));
	return descriptor ? descriptor->source_religion : dcon::religion_id{};
}

dcon::pop_type_id current_pop_type(sys::state const& state, person_key key) {
	if(!persons::exact_population::exists(state, key) || !persons::exact_population::alive(state, key)) return {};
	auto descriptor = find_cell(state, persons::exact_population::current_population_cell(state, key));
	return descriptor ? descriptor->source_pop_type : dcon::pop_type_id{};
}

bool is_source_workforce_anchor(sys::state const& state, person_key key) {
	return persons::exact_population::exists(state, key) && key.ordinal % 4u == 0u;
}

std::optional<cell_descriptor> descriptor_for_cell(sys::state const& state, uint32_t source_population_cell) {
	auto descriptor = find_cell(state, source_population_cell);
	return descriptor ? std::optional<cell_descriptor>(*descriptor) : std::nullopt;
}

uint32_t source_cell_for_population(sys::state const& state, dcon::pop_id pop) {
	if(!pop || !state.world.pop_is_valid(pop)) return 0;
	assert(state.exact_population && "exact population must exist for population lookup");
	if(!state.exact_population) std::abort();
	auto store = state.exact_population;
	auto found = store->source_by_pop_slot.find(pop_slot(pop));
	return found == store->source_by_pop_slot.end() ? 0u : found->second;
}

dcon::pop_id population_for_source_cell(sys::state const& state, uint32_t source_population_cell) {
	if(source_population_cell == 0) return {};
	assert(state.exact_population && "exact population must exist for population projection lookup");
	if(!state.exact_population) std::abort();
	auto store = state.exact_population;
	auto found = store->pop_slot_by_source.find(source_population_cell);
	if(found == store->pop_slot_by_source.end()) return {};
	auto pop = dcon::pop_id{dcon::pop_id::value_base_t(found->second - 1u)};
	return state.world.pop_is_valid(pop) ? pop : dcon::pop_id{};
}

void retire_population_cell(sys::state& state, dcon::pop_id pop) {
	if(!pop) return;
	assert(state.exact_population && "exact population must exist before retiring a population cell");
	if(!state.exact_population) std::abort();
	auto store = state.exact_population;
	auto slot = pop_slot(pop);
	auto found = store->source_by_pop_slot.find(slot);
	if(found == store->source_by_pop_slot.end()) return;
	store->retired_source_cells.insert(found->second);
	store->pop_slot_by_source.erase(found->second);
	store->source_by_pop_slot.erase(found);
}

dcon::person_id profile_for_person(sys::state const& state, person_key key) {
	if(!persons::exact_population::exists(state, key)) return {};
	auto store = ensure_store(state);
	auto it = store->bridges.find(key);
	return it != store->bridges.end() && state.world.person_is_valid(it->second) ? it->second : dcon::person_id{};
}

dcon::person_id materialize_person_profile(sys::state& state, person_key key) {
	if(!persons::exact_population::exists(state, key)) return {};
	auto store = ensure_store(state);
	if(auto existing = store->bridges.find(key); existing != store->bridges.end())
		return state.world.person_is_valid(existing->second) ? existing->second : dcon::person_id{};
	if(!membership_range_for(*store, key)) return {};
	auto person = persons::detail::create_profile_record_with_birth_day(state,
		persons::exact_population::birth_day_index(state, key));
	state.world.person_set_source_population_cell(person, key.source_population_cell);
	state.world.person_set_source_population_ordinal(person,
		key.ordinal <= uint64_t(std::numeric_limits<uint32_t>::max()) ? uint32_t(key.ordinal) : 0u);
	state.world.person_set_source_pop_type(person, persons::exact_population::source_pop_type(state, key));
	state.world.person_set_source_culture(person, persons::exact_population::source_culture(state, key));
	state.world.person_set_source_religion(person, persons::exact_population::source_religion(state, key));
	if(auto site = persons::exact_population::home_site(state, key)) state.world.force_create_person_home_site(person, site);
	if(!persons::exact_population::alive(state, key)) {
		state.world.person_set_alive(person, uint8_t(0));
		auto death_day = persons::exact_population::death_day_index(state, key);
		if(death_day != std::numeric_limits<int32_t>::min() && death_day >= 0
			&& death_day <= int32_t(std::numeric_limits<uint16_t>::max() - 1))
			state.world.person_set_death_date(person, sys::date{uint16_t(death_day + 1)});
	}
	store->bridges.emplace(key, person);
	store->person_by_profile.emplace(uint32_t(person.index()) + 1u, key);
	return person;
}

dcon::person_id create_individual_person(sys::state& state, persons::birth_day_index_t birth_day) {
	if(!state.exact_population) persons::exact_population::initialize_empty_store(state);
	auto store = ensure_store(state);
	auto const maximum = maximum_source_cell(*store);
	if(maximum == std::numeric_limits<uint32_t>::max()) return {};
	cell_descriptor descriptor;
	descriptor.source_population_cell = maximum + 1u;
	descriptor.literal_count = 1;
	descriptor.bootstrap_base_day = birth_day;
	descriptor.demographic_seed = uint64_t(descriptor.source_population_cell) * 0x9e3779b97f4a7c15ULL;
	if(add_descriptor(state, descriptor).result != status::created) return {};
	birth_cohort cohort;
	cohort.source_population_cell = descriptor.source_population_cell;
	cohort.count = 1;
	cohort.birth_day = birth_day;
	store->birth_cohorts.push_back(cohort);
	std::sort(store->birth_cohorts.begin(), store->birth_cohorts.end(), [](auto const& left, auto const& right) {
		return left.source_population_cell == right.source_population_cell
			? left.first_ordinal < right.first_ordinal
			: left.source_population_cell < right.source_population_cell;
	});
	return materialize_person_profile(state, {descriptor.source_population_cell, 0});
}

person_key person_for_profile(sys::state const& state, dcon::person_id profile) {
	if(!profile || !state.world.person_is_valid(profile)) return {};
	auto store = ensure_store(state);
	auto found = store->person_by_profile.find(uint32_t(profile.index()) + 1u);
	return found == store->person_by_profile.end() ? person_key{} : found->second;
}

catalog_snapshot export_snapshot(sys::state const& state) {
	catalog_snapshot result;
	auto store = ensure_store(state);
	result.cells = store->cells;
	result.has_source_bindings = true;
	result.retired_source_cells.assign(store->retired_source_cells.begin(), store->retired_source_cells.end());
	std::sort(result.retired_source_cells.begin(), result.retired_source_cells.end());
	result.retired_people = store->retired_people;
	result.deaths = store->deaths;
	result.birth_cohorts = store->birth_cohorts;
	result.membership_ranges = store->membership_ranges;
	result.military_assignments = store->military_assignments;
	result.transitions = store->transitions;
	result.transfer_remainders = store->transfer_remainders;
	std::sort(result.membership_ranges.begin(), result.membership_ranges.end(), [](auto const& left, auto const& right) {
		return left.identity_population_cell == right.identity_population_cell
			? left.first_ordinal < right.first_ordinal
			: left.identity_population_cell < right.identity_population_cell;
	});
	std::sort(result.cells.begin(), result.cells.end(), [](auto const& left, auto const& right) {
		return left.source_population_cell < right.source_population_cell;
	});
	std::sort(result.retired_people.begin(), result.retired_people.end(), [](auto const& left, auto const& right) {
		return left.source_population_cell == right.source_population_cell
			? left.first_ordinal < right.first_ordinal
			: left.source_population_cell < right.source_population_cell;
	});
	std::sort(result.deaths.begin(), result.deaths.end(), [](auto const& left, auto const& right) {
		return left.source_population_cell == right.source_population_cell
			? left.first_ordinal < right.first_ordinal
			: left.source_population_cell < right.source_population_cell;
	});
	std::sort(result.transfer_remainders.begin(), result.transfer_remainders.end(), [](auto const& left, auto const& right) {
		return left.from_population_cell == right.from_population_cell
			? left.to_population_cell < right.to_population_cell
			: left.from_population_cell < right.from_population_cell;
	});
	result.has_current_membership = true;
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

namespace {

int32_t snapshot_death_day(exact_population_store const& store, person_key key) {
	for(auto const& death : store.deaths)
		if(death.source_population_cell == key.source_population_cell
			&& key.ordinal >= death.first_ordinal && key.ordinal - death.first_ordinal < death.count)
			return death.death_day;
	return std::numeric_limits<int32_t>::min();
}

bool snapshot_alive(exact_population_store const& store, person_key key) {
	auto it = store.overrides.find(key);
	if(it != store.overrides.end() && it->second.has_alive) return it->second.alive;
	return !retired_ordinal(store, key);
}

int32_t snapshot_birth_day(exact_population_store const& store, person_key key) {
	if(auto cohort = birth_cohort_for(store, key)) return cohort->birth_day;
	auto it = store.cell_by_source.find(key.source_population_cell);
	if(it == store.cell_by_source.end()) return 0;
	auto const& descriptor = store.cells[it->second];
	return clamp_day(int64_t(descriptor.bootstrap_base_day)
		- int64_t(bootstrap_age_days(descriptor, key)));
}

dcon::site_id snapshot_home_site(exact_population_store const& store, person_key key) {
	if(auto override = store.overrides.find(key); override != store.overrides.end()
		&& override->second.has_home_site) return override->second.home_site;
	auto membership = membership_range_for(store, key);
	if(!membership) return {};
	auto descriptor = store.cell_by_source.find(membership->current_population_cell);
	return descriptor == store.cell_by_source.end() ? dcon::site_id{} : store.cells[descriptor->second].home_site;
}

bool profile_matches_snapshot(sys::state const& state, exact_population_store const& store,
	person_key key, dcon::person_id profile) {
	auto const profile_ordinal = key.ordinal <= uint64_t(std::numeric_limits<uint32_t>::max())
		? uint32_t(key.ordinal) : 0u;
	if(!profile || !state.world.person_is_valid(profile)
		|| !state.world.person_get_birth_day_known(profile)
		|| state.world.person_get_birth_day_index(profile) != snapshot_birth_day(store, key)
		|| bool(state.world.person_get_alive(profile)) != snapshot_alive(store, key)
		|| state.world.person_get_source_population_cell(profile) != key.source_population_cell
		|| state.world.person_get_source_population_ordinal(profile) != profile_ordinal
		|| state.world.person_get_source_culture(profile) != persons::exact_population::source_culture(state, key)
		|| state.world.person_get_source_religion(profile) != persons::exact_population::source_religion(state, key)
		|| state.world.person_get_source_pop_type(profile) != persons::exact_population::source_pop_type(state, key)
		|| state.world.person_get_site_from_person_home_site(profile) != snapshot_home_site(store, key)) return false;
	auto death_day = snapshot_death_day(store, key);
	if(snapshot_alive(store, key)) return death_day == std::numeric_limits<int32_t>::min();
	auto expected = death_day >= 0 && death_day <= int32_t(std::numeric_limits<uint16_t>::max() - 1)
		? sys::date{uint16_t(death_day + 1)} : sys::date{};
	return death_day != std::numeric_limits<int32_t>::min()
		&& state.world.person_get_death_date(profile) == expected;
}

} // namespace

bool import_snapshot(sys::state& state, catalog_snapshot const& snapshot) {
	if(snapshot.bootstrap_version != bootstrap_semantics_version) return false;
	if(!snapshot.has_source_bindings || !snapshot.has_current_membership) return false;
	auto candidate = std::make_shared<exact_population_store>();
	for(auto const& descriptor : snapshot.cells) {
		if(!valid_descriptor(state, descriptor) || candidate->cell_by_source.contains(descriptor.source_population_cell)) return false;
		candidate->cell_by_source.emplace(descriptor.source_population_cell, candidate->cells.size());
		candidate->cells.push_back(descriptor);
	}
	for(auto source : snapshot.retired_source_cells) {
		if(source != 0) candidate->retired_source_cells.insert(source);
	}
	for(auto const& range : snapshot.retired_people) {
		auto descriptor = find_cell_index(*candidate, range.source_population_cell);
		if(range.reserved != 0 || descriptor == std::numeric_limits<std::size_t>::max() || range.count == 0 || range.first_ordinal >= candidate->cells[descriptor].literal_count || range.count > candidate->cells[descriptor].literal_count - range.first_ordinal) return false;
		candidate->retired_people.push_back(range);
	}
	normalize_retired_ranges(*candidate);
	if(candidate->retired_people.size() != snapshot.retired_people.size()) return false;
	for(auto const& death : snapshot.deaths) {
		auto descriptor = find_cell_index(*candidate, death.source_population_cell);
		if(death.reserved != 0 || death.reserved_tail[0] != 0 || death.reserved_tail[1] != 0
			|| death.reserved_tail[2] != 0 || descriptor == std::numeric_limits<std::size_t>::max()
			|| death.count == 0 || death.first_ordinal >= candidate->cells[descriptor].literal_count
			|| death.count > candidate->cells[descriptor].literal_count - death.first_ordinal
			|| death.cause > uint8_t(persons::death_cause::attrition)
			|| (state.current_date && death.death_day > state.current_date.to_raw_value() - 1)) return false;
		candidate->deaths.push_back(death);
	}
	normalize_death_ranges(*candidate);
	if(candidate->deaths.size() != snapshot.deaths.size()) return false;
	for(std::size_t i = 1; i < candidate->deaths.size(); ++i) {
		auto const& previous = candidate->deaths[i - 1];
		auto const& current = candidate->deaths[i];
		if(previous.source_population_cell == current.source_population_cell
			&& current.first_ordinal < previous.first_ordinal + previous.count) return false;
	}
	for(auto const& retired : candidate->retired_people) {
		auto cursor = retired.first_ordinal;
		auto end = retired.first_ordinal + retired.count;
		for(auto const& death : candidate->deaths) {
			if(death.source_population_cell != retired.source_population_cell
				|| death.first_ordinal + death.count <= cursor) continue;
			if(death.first_ordinal > cursor) break;
			cursor = std::min(end, death.first_ordinal + death.count);
			if(cursor == end) break;
		}
		if(cursor != end) return false;
	}
	for(auto const& death : candidate->deaths) {
		person_range requested{death.source_population_cell, 0, death.first_ordinal, death.count};
		bool covered = false;
		for(auto const& retired : candidate->retired_people)
			if(retired.source_population_cell == requested.source_population_cell
				&& requested.first_ordinal >= retired.first_ordinal
				&& requested.first_ordinal + requested.count <= retired.first_ordinal + retired.count) {
				covered = true;
				break;
			}
		if(!covered) return false;
	}
	for(auto const& cohort : snapshot.birth_cohorts) {
		auto descriptor = find_cell_index(*candidate, cohort.source_population_cell);
		if(cohort.reserved != 0 || cohort.reserved_tail != 0 || descriptor == std::numeric_limits<std::size_t>::max() || cohort.count == 0 || cohort.first_ordinal >= candidate->cells[descriptor].literal_count || cohort.count > candidate->cells[descriptor].literal_count - cohort.first_ordinal || state.current_date && cohort.birth_day > state.current_date.to_raw_value() - 1 || cohort.home_site && !state.world.site_is_valid(cohort.home_site) || cohort.culture && !state.world.culture_is_valid(cohort.culture) || cohort.religion && !state.world.religion_is_valid(cohort.religion) || cohort.pop_type && !state.world.pop_type_is_valid(cohort.pop_type)) return false;
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
		if(previous.source_population_cell == current.source_population_cell && current.first_ordinal < previous.first_ordinal + previous.count) return false;
	}
	candidate->membership_ranges = snapshot.membership_ranges;
	normalize_membership_ranges(*candidate);
	if(candidate->membership_ranges.size() != snapshot.membership_ranges.size()) return false;
	normalize_membership_ranges(*candidate);
	{
		std::vector<cell_descriptor const*> sorted_cells;
		sorted_cells.reserve(candidate->cells.size());
		for(auto const& descriptor : candidate->cells) sorted_cells.push_back(&descriptor);
		std::sort(sorted_cells.begin(), sorted_cells.end(), [](auto left, auto right) {
			return left->source_population_cell < right->source_population_cell;
		});
		std::size_t range_index = 0;
		for(auto descriptor_ptr : sorted_cells) {
			auto const& descriptor = *descriptor_ptr;
			uint64_t next_ordinal = 0;
			while(range_index < candidate->membership_ranges.size()
				&& candidate->membership_ranges[range_index].identity_population_cell == descriptor.source_population_cell) {
				auto const& range = candidate->membership_ranges[range_index++];
				if(range.current_population_cell == 0 || find_cell_index(*candidate, range.current_population_cell) == std::numeric_limits<std::size_t>::max() || range.count == 0 || range.first_ordinal != next_ordinal || range.count > descriptor.literal_count - next_ordinal) return false;
				next_ordinal += range.count;
			}
			if(next_ordinal != descriptor.literal_count) return false;
		}
		if(range_index != candidate->membership_ranges.size()) return false;
	}
	candidate->military_assignments = snapshot.military_assignments;
	normalize_military_assignments(*candidate);
	if(candidate->military_assignments.size() != snapshot.military_assignments.size()) return false;
	for(size_t i = 0; i < candidate->military_assignments.size(); ++i) {
		auto const& assignment = candidate->military_assignments[i];
		auto cell = find_cell_index(*candidate, assignment.source_population_cell);
		if(assignment.reserved != 0 || assignment.formation_id == 0 || assignment.count == 0
			|| (assignment.ordinal_stride != 1 && assignment.ordinal_stride != 4)
			|| cell == std::numeric_limits<std::size_t>::max()
			|| assignment.first_ordinal >= candidate->cells[cell].literal_count
			|| assignment.count - 1 > (candidate->cells[cell].literal_count - 1 - assignment.first_ordinal) / assignment.ordinal_stride
			|| living_count_strided(*candidate, assignment.source_population_cell,
				assignment.first_ordinal, assignment.count, assignment.ordinal_stride) != assignment.count) return false;
		for(size_t j = i + 1; j < candidate->military_assignments.size(); ++j) {
			auto const& other = candidate->military_assignments[j];
			if(assignment.source_population_cell != other.source_population_cell) continue;
			auto last = assignment.first_ordinal + (assignment.count - 1) * uint64_t(assignment.ordinal_stride);
			auto other_last = other.first_ordinal + (other.count - 1) * uint64_t(other.ordinal_stride);
			auto low = std::max(assignment.first_ordinal, other.first_ordinal);
			auto high = std::min(last, other_last);
			if(low > high) continue;
			auto period = std::lcm(assignment.ordinal_stride, other.ordinal_stride);
			for(uint64_t ordinal = low; ordinal <= std::min<uint64_t>(high, low + period); ++ordinal)
				if((ordinal - assignment.first_ordinal) % assignment.ordinal_stride == 0
					&& (ordinal - other.first_ordinal) % other.ordinal_stride == 0) return false;
		}
	}
	for(auto const& event : snapshot.transitions) {
		auto identity_index = find_cell_index(*candidate, event.identity_population_cell);
		if(event.reserved[0] != 0 || event.reserved[1] != 0 || event.reserved[2] != 0 || event.reserved_tail != 0 || identity_index == std::numeric_limits<std::size_t>::max() || event.from_population_cell == 0 || event.to_population_cell == 0 || event.from_population_cell == event.to_population_cell || find_cell_index(*candidate, event.from_population_cell) == std::numeric_limits<std::size_t>::max() || find_cell_index(*candidate, event.to_population_cell) == std::numeric_limits<std::size_t>::max() || event.cause > uint8_t(population_transition_cause::scripted_reclassification) || event.count == 0 || event.first_ordinal >= candidate->cells[identity_index].literal_count || event.count > candidate->cells[identity_index].literal_count - event.first_ordinal || state.current_date && event.transition_day > state.current_date.to_raw_value() - 1) return false;
		candidate->transitions.push_back(event);
	}
	for(auto const& remainder : snapshot.transfer_remainders) {
		if(remainder.from_population_cell == 0 || remainder.to_population_cell == 0 || remainder.from_population_cell == remainder.to_population_cell || find_cell_index(*candidate, remainder.from_population_cell) == std::numeric_limits<std::size_t>::max() || find_cell_index(*candidate, remainder.to_population_cell) == std::numeric_limits<std::size_t>::max() || !std::isfinite(remainder.pending_people) || remainder.pending_people < 0.0 || remainder.pending_people >= 1.0 || std::any_of(candidate->transfer_remainders.begin(), candidate->transfer_remainders.end(), [&](auto const& old) {
				return old.from_population_cell == remainder.from_population_cell
					&& old.to_population_cell == remainder.to_population_cell;
			})) return false;
		candidate->transfer_remainders.push_back(remainder);
	}
	for(auto const& binding : snapshot.source_bindings) {
		if(binding.source_population_cell == 0 || binding.dcon_pop_index_plus_one == 0) return false;
		auto pop = dcon::pop_id{dcon::pop_id::value_base_t(binding.dcon_pop_index_plus_one - 1u)};
		if(!state.world.pop_is_valid(pop) || !bind_population(*candidate, pop, binding.source_population_cell)) return false;
	}
	if(candidate->source_by_pop_slot.size() != snapshot.source_bindings.size()) return false;
	for(auto const& override : snapshot.overrides) {
		if(!candidate->cell_by_source.contains(override.key.source_population_cell) || override.key.ordinal >= candidate->cells[candidate->cell_by_source.at(override.key.source_population_cell)].literal_count || override.has_alive && override.alive && retired_ordinal(*candidate, override.key) || candidate->overrides.contains(override.key)) return false;
		if(override.has_home_site && !override.home_site || !state.world.site_is_valid(override.home_site)) return false;
		candidate->overrides.emplace(override.key, override);
	}
	for(auto const& bridge : snapshot.bridges) {
		if(!candidate->cell_by_source.contains(bridge.key.source_population_cell) || bridge.key.ordinal >= candidate->cells[candidate->cell_by_source.at(bridge.key.source_population_cell)].literal_count || !bridge.profile || !state.world.person_is_valid(bridge.profile) || candidate->bridges.contains(bridge.key) || candidate->person_by_profile.contains(uint32_t(bridge.profile.index()) + 1u) || !profile_matches_snapshot(state, *candidate, bridge.key, bridge.profile)) return false;
		candidate->bridges.emplace(bridge.key, bridge.profile);
		candidate->person_by_profile.emplace(uint32_t(bridge.profile.index()) + 1u, bridge.key);
	}
	state.exact_population = std::move(candidate);
	return true;
}

void clear_store(sys::state& state) {
	state.exact_population.reset();
}

} // namespace persons::exact_population
