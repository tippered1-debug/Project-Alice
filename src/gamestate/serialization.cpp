#include "dcon_generated_ids.hpp"
#include "system_state.hpp"
#include "serialization.hpp"
#include "actors/ownership.hpp"
#include "economy/banking/banking.hpp"
#include "economy/causal_order.hpp"
#include "economy/exact_person_economy.hpp"
#include "economy/physical/exact_person_freight.hpp"
#include "economy/physical/exact_person_goods.hpp"
#include "economy/physical/labor_dynamics.hpp"
#include "gamerule/gamerule.hpp"
#include "persons/exact_population.hpp"
#include "military/land_forces.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <random>
#include <ctime>
#include <type_traits>

#define ZSTD_STATIC_LINKING_ONLY
#define XXH_NAMESPACE ZSTD_

#include "blake2.h"
#include "zstd.h"

namespace sys {

namespace {

bool readable_scenario_version(uint32_t version) {
	return version == sys::scenario_file_version;
}

bool readable_save_version(uint32_t version) {
	return version == sys::save_file_version;
}

// Canonical handwritten runtime sections carry an explicit schema and length.
// Save loading accepts only the exact schema written by this runtime.
constexpr uint32_t transformation_politics_save_magic = 0x414F5450u; // AOTP
constexpr uint16_t transformation_politics_save_version = 2;
constexpr std::size_t transformation_politics_save_header_size =
	sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint16_t) + sizeof(uint32_t) + sizeof(uint32_t);
constexpr std::size_t transformation_politics_save_record_size =
	sizeof(uint32_t) + sizeof(int32_t)
	+ politics::transformation::interest_group_count * sizeof(float) + sizeof(float);

constexpr uint32_t transformation_legislation_save_magic = 0x414F424Cu; // AOBL
constexpr uint16_t transformation_legislation_save_version = 2;
constexpr std::size_t transformation_legislation_save_record_size =
	sizeof(uint8_t) + 2 * sizeof(uint8_t) + sizeof(uint16_t) + sizeof(int32_t)
	+ 3 * sizeof(uint32_t) + 5 * sizeof(float);

constexpr uint32_t strategic_statecraft_save_magic = 0x414F5353u; // AOSS
constexpr uint16_t strategic_statecraft_save_version = 1;
constexpr std::size_t strategic_statecraft_save_header_size =
	sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint16_t) + sizeof(uint32_t);

constexpr uint32_t exact_runtime_save_magic = 0x414F4558u; // AOEX
constexpr uint16_t exact_runtime_save_version = 11;
constexpr std::size_t exact_runtime_save_header_size =
	sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint16_t) + sizeof(uint32_t);
constexpr uint32_t exact_runtime_max_records = 64'000'000u;

struct exact_runtime_snapshot {
	persons::exact_population::catalog_snapshot population;
	economy::exact_person_economy::economy_snapshot economy;
	economy::physical::exact_person_goods::goods_snapshot goods;
	economy::physical::exact_person_freight::freight_snapshot freight;
	economy::physical::labor_dynamics::snapshot labor;
	economy::causal_order::snapshot causal_order;
	military::land_forces::snapshot land_forces;
	uint16_t extension_version = 0;
	bool extension_found = false;
	bool present = false;
	bool transformation_politics_loaded = false;
	bool transformation_legislation_loaded = false;
};

struct legacy_casualty_event_record_v10 {
	military::land_forces::stable_id event_id = 0;
	military::land_forces::stable_id formation_id = 0;
	uint64_t personnel_losses = 0;
	int32_t day = 0;
};
static_assert(sizeof(legacy_casualty_event_record_v10) == sizeof(military::land_forces::casualty_event_record));

void disable_strategic_statecraft(sys::state& state) {
	state.strategic_statecraft_initialized = false;
	state.strategic_interests.clear();
	state.strategic_beliefs.clear();
	state.strategic_commitments.clear();
	state.strategic_crisis = nations::strategic_statecraft::crisis_memory{};
}

std::size_t strategic_statecraft_save_size(sys::state const& state) {
	assert(state.strategic_statecraft_initialized);
	if(!state.strategic_statecraft_initialized) std::abort();
	auto const payload_size = sizeof(uint8_t) + sizeof(state.strategic_crisis)
		+ serialize_size(state.strategic_interests)
		+ serialize_size(state.strategic_beliefs)
		+ serialize_size(state.strategic_commitments);
	return strategic_statecraft_save_header_size + payload_size;
}

uint8_t* write_strategic_statecraft_save(uint8_t* ptr, sys::state const& state) {
	assert(state.strategic_statecraft_initialized);
	if(!state.strategic_statecraft_initialized) std::abort();
	auto const payload_size = uint32_t(sizeof(uint8_t) + sizeof(state.strategic_crisis)
		+ serialize_size(state.strategic_interests)
		+ serialize_size(state.strategic_beliefs)
		+ serialize_size(state.strategic_commitments));
	ptr = memcpy_serialize(ptr, strategic_statecraft_save_magic);
	ptr = memcpy_serialize(ptr, strategic_statecraft_save_version);
	uint16_t reserved = 0;
	ptr = memcpy_serialize(ptr, reserved);
	ptr = memcpy_serialize(ptr, payload_size);
	ptr = memcpy_serialize(ptr, uint8_t(1));
	ptr = memcpy_serialize(ptr, state.strategic_crisis);
	ptr = serialize(ptr, state.strategic_interests);
	ptr = serialize(ptr, state.strategic_beliefs);
	ptr = serialize(ptr, state.strategic_commitments);
	return ptr;
}

template<typename T>
bool read_framed_vector(uint8_t const*& ptr, uint8_t const* payload_end,
	std::vector<T>& output, uint32_t max_count) {
	if(std::size_t(payload_end - ptr) < sizeof(uint32_t))
		return false;
	uint32_t count = 0;
	ptr = memcpy_deserialize(ptr, count);
	if(count > max_count || std::size_t(payload_end - ptr) < sizeof(T) * std::size_t(count))
		return false;
	output.resize(count);
	if(count != 0)
		std::memcpy(output.data(), ptr, sizeof(T) * std::size_t(count));
	ptr += sizeof(T) * std::size_t(count);
	return true;
}

template<typename T>
std::size_t pod_vector_size(std::vector<T> const& values) {
	static_assert(std::is_trivially_copyable_v<T>);
	return sizeof(uint32_t) + sizeof(T) * values.size();
}

template<typename T>
uint8_t* write_pod_vector(uint8_t* ptr, std::vector<T> const& values) {
	static_assert(std::is_trivially_copyable_v<T>);
	return serialize(ptr, values);
}

template<typename T>
bool read_pod_vector(uint8_t const*& ptr, uint8_t const* end,
	std::vector<T>& values) {
	static_assert(std::is_trivially_copyable_v<T>);
	return read_framed_vector(ptr, end, values, exact_runtime_max_records);
}

using exact_person_key = persons::person_key;

uint8_t* write_person_key(uint8_t* ptr, exact_person_key key) {
	ptr = memcpy_serialize(ptr, key.source_population_cell);
	return memcpy_serialize(ptr, key.ordinal);
}

bool read_person_key(uint8_t const*& ptr, uint8_t const* end, exact_person_key& key) {
	if(std::size_t(end - ptr) < sizeof(key.source_population_cell) + sizeof(key.ordinal)) return false;
	ptr = memcpy_deserialize(ptr, key.source_population_cell);
	ptr = memcpy_deserialize(ptr, key.ordinal);
	return true;
}

std::size_t person_key_vector_size(std::size_t count) {
	return sizeof(uint32_t) + count * (sizeof(uint32_t) + sizeof(uint64_t));
}

uint8_t* write_person_key_vector(uint8_t* ptr, std::vector<exact_person_key> const& values) {
	ptr = memcpy_serialize(ptr, uint32_t(values.size()));
	for(auto key : values) ptr = write_person_key(ptr, key);
	return ptr;
}

bool read_person_key_vector(uint8_t const*& ptr, uint8_t const* end,
	std::vector<exact_person_key>& values) {
	if(std::size_t(end - ptr) < sizeof(uint32_t)) return false;
	uint32_t count = 0;
	ptr = memcpy_deserialize(ptr, count);
	if(count > exact_runtime_max_records || std::size_t(end - ptr) < std::size_t(count) * (sizeof(uint32_t) + sizeof(uint64_t))) return false;
	values.resize(count);
	for(auto& key : values) if(!read_person_key(ptr, end, key)) return false;
	return true;
}

std::size_t exact_runtime_payload_size(exact_runtime_snapshot const& snapshot) {
	auto const& population = snapshot.population;
	auto const& economy = snapshot.economy;
	auto const& goods = snapshot.goods;
	auto const& freight = snapshot.freight;
	auto const& labor = snapshot.labor;
	auto const& causal = snapshot.causal_order;
	auto const& land_forces = snapshot.land_forces;
	std::size_t size = 6 * sizeof(uint32_t);
	size += pod_vector_size(population.cells) + pod_vector_size(population.bridges)
		+ pod_vector_size(population.source_bindings)
		+ pod_vector_size(population.retired_source_cells)
		+ pod_vector_size(population.retired_people)
		+ pod_vector_size(population.deaths)
		+ pod_vector_size(population.birth_cohorts)
		+ pod_vector_size(population.membership_ranges)
		+ pod_vector_size(population.military_assignments)
		+ pod_vector_size(population.transitions)
		+ pod_vector_size(population.transfer_remainders);
	size += sizeof(uint32_t) + population.overrides.size() *
		(sizeof(uint32_t) + sizeof(uint64_t) + 3 * sizeof(uint8_t) + sizeof(dcon::site_id));
	size += pod_vector_size(economy.accounts) + pod_vector_size(economy.applications)
		+ pod_vector_size(economy.contracts) + pod_vector_size(economy.transactions);
	size += sizeof(uint32_t) + economy.last_separation_dates.size() *
		(sizeof(uint32_t) + sizeof(uint64_t) + sizeof(sys::date));
	size += person_key_vector_size(economy.displaced_workers.size());
	size += pod_vector_size(goods.stocks) + pod_vector_size(goods.needs)
		+ pod_vector_size(goods.bids) + pod_vector_size(goods.fills);
	size += pod_vector_size(freight.requests) + pod_vector_size(freight.contracts)
		+ pod_vector_size(freight.shipment_owners);
	size += sizeof(uint64_t) + pod_vector_size(labor.events);
	size += sizeof(uint64_t) + pod_vector_size(causal.dcon_sequences);
	size += sizeof(uint32_t) + pod_vector_size(land_forces.equipment_models)
		+ pod_vector_size(land_forces.templates) + pod_vector_size(land_forces.template_equipment)
		+ pod_vector_size(land_forces.template_consumables) + pod_vector_size(land_forces.formations)
		+ pod_vector_size(land_forces.equipment)
		+ pod_vector_size(land_forces.consumables) + pod_vector_size(land_forces.stockpiles)
		+ pod_vector_size(land_forces.shipments) + pod_vector_size(land_forces.person_losses)
		+ pod_vector_size(land_forces.equipment_losses) + pod_vector_size(land_forces.casualty_events);
	return size;
}

exact_runtime_snapshot capture_exact_runtime_snapshot(sys::state const& state) {
	exact_runtime_snapshot result;
	result.population = persons::exact_population::export_snapshot(state);
	result.economy = economy::exact_person_economy::export_snapshot(state);
	result.goods = economy::physical::exact_person_goods::export_snapshot(state);
	result.freight = economy::physical::exact_person_freight::export_snapshot(state);
	result.labor = economy::physical::labor_dynamics::export_snapshot(state);
	result.causal_order = economy::causal_order::export_snapshot(state);
	result.land_forces = military::land_forces::export_snapshot(state);
	result.present = true;
	return result;
}

std::size_t exact_runtime_save_size(sys::state const& state) {
	auto snapshot = capture_exact_runtime_snapshot(state);
	auto const payload_size = exact_runtime_payload_size(snapshot);
	if(payload_size > std::numeric_limits<uint32_t>::max()) return 0;
	return exact_runtime_save_header_size + payload_size;
}

uint8_t* write_exact_runtime_save(uint8_t* ptr, sys::state const& state) {
	auto snapshot = capture_exact_runtime_snapshot(state);
	auto const payload_size = exact_runtime_payload_size(snapshot);
	if(payload_size > std::numeric_limits<uint32_t>::max()) return ptr;
	ptr = memcpy_serialize(ptr, exact_runtime_save_magic);
	ptr = memcpy_serialize(ptr, exact_runtime_save_version);
	ptr = memcpy_serialize(ptr, uint16_t(0));
	ptr = memcpy_serialize(ptr, uint32_t(payload_size));
	auto const payload_start = ptr;

	auto const& population = snapshot.population;
	auto const& economy = snapshot.economy;
	auto const& goods = snapshot.goods;
	auto const& freight = snapshot.freight;
	auto const& labor = snapshot.labor;
	auto const& causal = snapshot.causal_order;
	auto const& land_forces = snapshot.land_forces;
	ptr = memcpy_serialize(ptr, population.bootstrap_version);
	ptr = write_pod_vector(ptr, population.cells);
	ptr = memcpy_serialize(ptr, uint32_t(population.overrides.size()));
	for(auto const& record : population.overrides) {
		ptr = write_person_key(ptr, record.key);
		ptr = memcpy_serialize(ptr, uint8_t(record.has_alive));
		ptr = memcpy_serialize(ptr, uint8_t(record.alive));
		ptr = memcpy_serialize(ptr, uint8_t(record.has_home_site));
		ptr = memcpy_serialize(ptr, record.home_site);
	}
	ptr = write_pod_vector(ptr, population.bridges);
	ptr = write_pod_vector(ptr, population.source_bindings);
	ptr = write_pod_vector(ptr, population.retired_source_cells);
	ptr = write_pod_vector(ptr, population.retired_people);
	ptr = write_pod_vector(ptr, population.deaths);
	ptr = write_pod_vector(ptr, population.birth_cohorts);
	ptr = write_pod_vector(ptr, population.membership_ranges);
	ptr = write_pod_vector(ptr, population.military_assignments);
	ptr = write_pod_vector(ptr, population.transitions);
	ptr = write_pod_vector(ptr, population.transfer_remainders);

	ptr = memcpy_serialize(ptr, economy.version);
	ptr = memcpy_serialize(ptr, uint32_t(economy.participation_overrides.size()));
	for(auto const& [key, enabled] : economy.participation_overrides) {
		ptr = write_person_key(ptr, key);
		ptr = memcpy_serialize(ptr, uint8_t(enabled));
	}
	ptr = write_pod_vector(ptr, economy.accounts);
	ptr = write_pod_vector(ptr, economy.applications);
	ptr = write_pod_vector(ptr, economy.contracts);
	ptr = write_pod_vector(ptr, economy.transactions);
	ptr = memcpy_serialize(ptr, uint32_t(economy.last_separation_dates.size()));
	for(auto const& [key, date] : economy.last_separation_dates) {
		ptr = write_person_key(ptr, key);
		ptr = memcpy_serialize(ptr, date);
	}
	ptr = write_person_key_vector(ptr, economy.displaced_workers);

	ptr = memcpy_serialize(ptr, goods.version);
	ptr = write_pod_vector(ptr, goods.stocks);
	ptr = write_pod_vector(ptr, goods.needs);
	ptr = write_pod_vector(ptr, goods.bids);
	ptr = write_pod_vector(ptr, goods.fills);

	ptr = memcpy_serialize(ptr, freight.version);
	ptr = write_pod_vector(ptr, freight.requests);
	ptr = write_pod_vector(ptr, freight.contracts);
	ptr = write_pod_vector(ptr, freight.shipment_owners);

	ptr = memcpy_serialize(ptr, labor.version);
	ptr = memcpy_serialize(ptr, labor.next_event_id);
	ptr = write_pod_vector(ptr, labor.events);

	ptr = memcpy_serialize(ptr, causal.version);
	ptr = memcpy_serialize(ptr, causal.next_sequence);
	ptr = write_pod_vector(ptr, causal.dcon_sequences);
	ptr = memcpy_serialize(ptr, land_forces.version);
	ptr = write_pod_vector(ptr, land_forces.equipment_models);
	ptr = write_pod_vector(ptr, land_forces.templates);
	ptr = write_pod_vector(ptr, land_forces.template_equipment);
	ptr = write_pod_vector(ptr, land_forces.template_consumables);
	ptr = write_pod_vector(ptr, land_forces.formations);
	ptr = write_pod_vector(ptr, land_forces.equipment);
	ptr = write_pod_vector(ptr, land_forces.consumables);
	ptr = write_pod_vector(ptr, land_forces.stockpiles);
	ptr = write_pod_vector(ptr, land_forces.shipments);
	ptr = write_pod_vector(ptr, land_forces.person_losses);
	ptr = write_pod_vector(ptr, land_forces.equipment_losses);
	ptr = write_pod_vector(ptr, land_forces.casualty_events);
	assert(std::size_t(ptr - payload_start) == payload_size);
	return ptr;
}

template<typename T, typename ReadOne>
bool read_custom_vector(uint8_t const*& ptr, uint8_t const* end,
	std::vector<T>& values, std::size_t record_size, ReadOne read_one) {
	if(std::size_t(end - ptr) < sizeof(uint32_t)) return false;
	uint32_t count = 0;
	ptr = memcpy_deserialize(ptr, count);
	if(count > exact_runtime_max_records || std::size_t(end - ptr) < std::size_t(count) * record_size) return false;
	values.resize(count);
	for(auto& value : values) if(!read_one(ptr, end, value)) return false;
	return true;
}

uint8_t const* read_exact_runtime_save(uint8_t const* ptr,
	uint8_t const* section_end, exact_runtime_snapshot& result) {
	result = exact_runtime_snapshot{};
	if(std::size_t(section_end - ptr) < exact_runtime_save_header_size) return ptr;
	auto const* header_start = ptr;
	uint32_t magic = 0;
	uint16_t version = 0;
	uint16_t reserved = 0;
	uint32_t payload_size = 0;
	ptr = memcpy_deserialize(ptr, magic);
	ptr = memcpy_deserialize(ptr, version);
	ptr = memcpy_deserialize(ptr, reserved);
	ptr = memcpy_deserialize(ptr, payload_size);
	(void)reserved;
	if(magic != exact_runtime_save_magic) return header_start;
	result.extension_found = true;
	result.extension_version = version;
	if(std::size_t(section_end - ptr) < payload_size) return section_end;
	auto const* payload_end = ptr + payload_size;
	if(version < 10 || version > exact_runtime_save_version) return payload_end;

	auto& population = result.population;
	auto& economy = result.economy;
	auto& goods = result.goods;
	auto& freight = result.freight;
	auto& labor = result.labor;
	auto& causal = result.causal_order;
	auto& land_forces = result.land_forces;
	bool valid = std::size_t(payload_end - ptr) >= 6 * sizeof(uint32_t);
	if(valid) ptr = memcpy_deserialize(ptr, population.bootstrap_version);
	if(valid) valid = read_pod_vector(ptr, payload_end, population.cells);
	if(valid) valid = read_custom_vector(ptr, payload_end, population.overrides,
		sizeof(uint32_t) + sizeof(uint64_t) + 3 * sizeof(uint8_t) + sizeof(dcon::site_id),
		[](uint8_t const*& input, uint8_t const* end, persons::exact_population::exact_person_override& record) {
			uint8_t has_alive = 0, alive = 0, has_home_site = 0;
			if(!read_person_key(input, end, record.key) || std::size_t(end - input) < 3 * sizeof(uint8_t) + sizeof(record.home_site)) return false;
			input = memcpy_deserialize(input, has_alive);
			input = memcpy_deserialize(input, alive);
			input = memcpy_deserialize(input, has_home_site);
			input = memcpy_deserialize(input, record.home_site);
			if(has_alive > 1 || alive > 1 || has_home_site > 1) return false;
			record.has_alive = has_alive != 0;
			record.alive = alive != 0;
			record.has_home_site = has_home_site != 0;
			return true;
		});
	if(valid) valid = read_pod_vector(ptr, payload_end, population.bridges);
	if(valid) valid = read_pod_vector(ptr, payload_end, population.source_bindings);
	population.has_source_bindings = valid;
	if(valid) valid = read_pod_vector(ptr, payload_end, population.retired_source_cells);
	if(valid) valid = read_pod_vector(ptr, payload_end, population.retired_people);
	if(valid) valid = read_pod_vector(ptr, payload_end, population.deaths);
	if(valid) valid = read_pod_vector(ptr, payload_end, population.birth_cohorts);
	if(valid) valid = read_pod_vector(ptr, payload_end, population.membership_ranges);
	if(valid) valid = read_pod_vector(ptr, payload_end, population.military_assignments);
	if(valid) valid = read_pod_vector(ptr, payload_end, population.transitions);
	if(valid) valid = read_pod_vector(ptr, payload_end, population.transfer_remainders);
	population.has_current_membership = valid;

	if(valid) ptr = memcpy_deserialize(ptr, economy.version);
	if(valid) valid = read_custom_vector(ptr, payload_end, economy.participation_overrides,
		sizeof(uint32_t) + sizeof(uint64_t) + sizeof(uint8_t),
		[](uint8_t const*& input, uint8_t const* end, std::pair<exact_person_key, bool>& record) {
			uint8_t enabled = 0;
			if(!read_person_key(input, end, record.first) || std::size_t(end - input) < sizeof(enabled)) return false;
			input = memcpy_deserialize(input, enabled);
			if(enabled > 1) return false;
			record.second = enabled != 0;
			return true;
		});
	if(valid) valid = read_pod_vector(ptr, payload_end, economy.accounts);
	if(valid) valid = read_pod_vector(ptr, payload_end, economy.applications);
	if(valid) valid = read_pod_vector(ptr, payload_end, economy.contracts);
	if(valid) valid = read_pod_vector(ptr, payload_end, economy.transactions);
	if(valid) valid = read_custom_vector(ptr, payload_end, economy.last_separation_dates,
		sizeof(uint32_t) + sizeof(uint64_t) + sizeof(sys::date),
		[](uint8_t const*& input, uint8_t const* end, std::pair<exact_person_key, sys::date>& record) {
			if(!read_person_key(input, end, record.first) || std::size_t(end - input) < sizeof(record.second)) return false;
			input = memcpy_deserialize(input, record.second);
			return true;
		});
	if(valid) valid = read_person_key_vector(ptr, payload_end, economy.displaced_workers);

	if(valid) ptr = memcpy_deserialize(ptr, goods.version);
	if(valid) valid = read_pod_vector(ptr, payload_end, goods.stocks);
	if(valid) valid = read_pod_vector(ptr, payload_end, goods.needs);
	if(valid) valid = read_pod_vector(ptr, payload_end, goods.bids);
	if(valid) valid = read_pod_vector(ptr, payload_end, goods.fills);

	if(valid) ptr = memcpy_deserialize(ptr, freight.version);
	if(valid) valid = read_pod_vector(ptr, payload_end, freight.requests);
	if(valid) valid = read_pod_vector(ptr, payload_end, freight.contracts);
	if(valid) valid = read_pod_vector(ptr, payload_end, freight.shipment_owners);

	if(valid) ptr = memcpy_deserialize(ptr, labor.version);
	if(valid && std::size_t(payload_end - ptr) >= sizeof(labor.next_event_id))
		ptr = memcpy_deserialize(ptr, labor.next_event_id);
	else if(valid) valid = false;
	if(valid) valid = read_pod_vector(ptr, payload_end, labor.events);

	if(valid) ptr = memcpy_deserialize(ptr, causal.version);
	if(valid && std::size_t(payload_end - ptr) >= sizeof(causal.next_sequence))
		ptr = memcpy_deserialize(ptr, causal.next_sequence);
	else if(valid) valid = false;
	if(valid) valid = read_pod_vector(ptr, payload_end, causal.dcon_sequences);
	if(valid && std::size_t(payload_end - ptr) >= sizeof(land_forces.version))
		ptr = memcpy_deserialize(ptr, land_forces.version);
	else if(valid) valid = false;
	if(valid) valid = read_pod_vector(ptr, payload_end, land_forces.equipment_models);
	if(valid) valid = read_pod_vector(ptr, payload_end, land_forces.templates);
	if(valid) valid = read_pod_vector(ptr, payload_end, land_forces.template_equipment);
	if(valid) valid = read_pod_vector(ptr, payload_end, land_forces.template_consumables);
	if(valid) valid = read_pod_vector(ptr, payload_end, land_forces.formations);
	if(valid) valid = read_pod_vector(ptr, payload_end, land_forces.equipment);
	if(valid) valid = read_pod_vector(ptr, payload_end, land_forces.consumables);
	if(valid) valid = read_pod_vector(ptr, payload_end, land_forces.stockpiles);
	if(valid) valid = read_pod_vector(ptr, payload_end, land_forces.shipments);
	if(valid) valid = read_pod_vector(ptr, payload_end, land_forces.person_losses);
	if(valid) valid = read_pod_vector(ptr, payload_end, land_forces.equipment_losses);
	if(valid && version == 10) {
		valid = read_custom_vector(ptr, payload_end, land_forces.casualty_events,
			sizeof(legacy_casualty_event_record_v10),
			[](uint8_t const*& input, uint8_t const* end, military::land_forces::casualty_event_record& record) {
				legacy_casualty_event_record_v10 old_record{};
				if(std::size_t(end - input) < sizeof(old_record)) return false;
				auto const* record_start = input;
				input = memcpy_deserialize(input, old_record.event_id);
				input = memcpy_deserialize(input, old_record.formation_id);
				input = memcpy_deserialize(input, old_record.personnel_losses);
				input = memcpy_deserialize(input, old_record.day);
				input = record_start + sizeof(old_record);
				record = {old_record.event_id, old_record.formation_id,
					old_record.personnel_losses, old_record.day,
					persons::death_cause::combat, {}};
				return true;
			});
	} else if(valid) valid = read_pod_vector(ptr, payload_end, land_forces.casualty_events);
	valid = valid && ptr == payload_end;
	if(valid) result.present = true;
	else {
		auto const found_extension = result.extension_found;
		auto const found_version = result.extension_version;
		result = exact_runtime_snapshot{};
		result.extension_found = found_extension;
		result.extension_version = found_version;
	}
	return payload_end;
}

void clear_exact_runtime_state(sys::state& state) {
	persons::exact_population::clear_store(state);
	economy::exact_person_economy::clear_store(state);
	economy::physical::exact_person_goods::clear_store(state);
	economy::physical::exact_person_freight::clear_store(state);
	economy::physical::labor_dynamics::clear_store(state);
	economy::causal_order::clear_store(state);
	military::land_forces::clear_store(state);
}

bool restore_exact_runtime_state(sys::state& state, exact_runtime_snapshot const& snapshot) {
	clear_exact_runtime_state(state);
	if(!snapshot.present) return false;
	auto clear_on_failure = [&] { clear_exact_runtime_state(state); };
	bool restored = persons::exact_population::import_snapshot(state, snapshot.population)
		&& economy::causal_order::import_snapshot(state, snapshot.causal_order)
		&& economy::exact_person_economy::import_snapshot(state, snapshot.economy)
		&& economy::physical::exact_person_goods::import_snapshot(state, snapshot.goods)
		&& economy::physical::exact_person_freight::import_snapshot(state, snapshot.freight)
		&& economy::physical::labor_dynamics::import_snapshot(state, snapshot.labor);
	if(restored) restored = military::land_forces::import_snapshot(state, snapshot.land_forces);
	if(!restored) clear_on_failure();
	return restored;
}

uint8_t const* read_strategic_statecraft_save(
	uint8_t const* ptr, uint8_t const* section_end, sys::state& state) {
	if(std::size_t(section_end - ptr) < strategic_statecraft_save_header_size) {
		disable_strategic_statecraft(state);
		return ptr;
	}
	auto const* header_start = ptr;
	uint32_t magic = 0;
	uint16_t version = 0;
	uint16_t reserved = 0;
	uint32_t payload_size = 0;
	ptr = memcpy_deserialize(ptr, magic);
	ptr = memcpy_deserialize(ptr, version);
	ptr = memcpy_deserialize(ptr, reserved);
	ptr = memcpy_deserialize(ptr, payload_size);
	(void)reserved;
	if(magic != strategic_statecraft_save_magic) {
		disable_strategic_statecraft(state);
		return header_start;
	}
	if(std::size_t(section_end - ptr) < payload_size) {
		disable_strategic_statecraft(state);
		return section_end;
	}
	auto const* payload_end = ptr + payload_size;
	if(version != strategic_statecraft_save_version) {
		disable_strategic_statecraft(state);
		return payload_end;
	}
	uint8_t initialized = 0;
	if(std::size_t(payload_end - ptr) < sizeof(initialized) + sizeof(state.strategic_crisis)) {
		disable_strategic_statecraft(state);
		return payload_end;
	}
	ptr = memcpy_deserialize(ptr, initialized);
	ptr = memcpy_deserialize(ptr, state.strategic_crisis);
	// The handwritten save extension is parsed before the DCON world payload.
	// Validate structural bounds here; nation-index checks wait until that
	// payload has been loaded below.
	constexpr uint32_t maximum_nations = 100'000u;
	constexpr uint32_t maximum_beliefs = 4'000'000u;
	bool const vectors_ok = read_framed_vector(ptr, payload_end, state.strategic_interests,
		maximum_nations)
		&& read_framed_vector(ptr, payload_end, state.strategic_beliefs, maximum_beliefs)
		&& read_framed_vector(ptr, payload_end, state.strategic_commitments, 65'536u);
	bool records_ok = initialized <= 1 && vectors_ok && ptr == payload_end
		&& state.strategic_interests.size() <= maximum_nations;
	auto const bounded_unit = [](float value) {
		return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
	};
	if(records_ok) {
		for(auto const& profile : state.strategic_interests) {
			if(profile.enabled > 1 || profile.last_decision > uint8_t(nations::strategic_statecraft::decision_code::crisis_war) || profile.objective > uint8_t(nations::strategic_statecraft::objective_kind::alliance) || !bounded_unit(profile.security) || !bounded_unit(profile.territorial_claims) || !bounded_unit(profile.market_access) || !bounded_unit(profile.resource_access) || !bounded_unit(profile.route_access) || !bounded_unit(profile.ally_subject_protection) || !bounded_unit(profile.prestige_influence) || !std::isfinite(profile.last_decision_value) || profile.last_decision_value < -1.0f || profile.last_decision_value > 1.0f || !std::isfinite(profile.objective_value) || profile.objective_value < -1.0f || profile.objective_value > 1.0f || profile.objective_target != nations::strategic_statecraft::interests::no_nation && profile.objective_target >= maximum_nations) {
				records_ok = false;
				break;
			}
		}
	}
	if(records_ok) {
		uint64_t previous_key = 0;
		bool first_belief = true;
		for(auto const& view : state.strategic_beliefs) {
			auto const observer = uint32_t(view.key >> 32);
			auto const subject = uint32_t(view.key);
			if(observer >= maximum_nations || subject >= maximum_nations || observer == subject || !first_belief && view.key <= previous_key || !bounded_unit(view.military_power) || !bounded_unit(view.economic_power) || !bounded_unit(view.resolve) || !bounded_unit(view.reliability) || view.currently_at_war > 1) {
				records_ok = false;
				break;
			}
			first_belief = false;
			previous_key = view.key;
		}
	}
	if(records_ok) {
		for(auto const& promise : state.strategic_commitments) {
			if(promise.promisor >= maximum_nations || promise.beneficiary >= maximum_nations || promise.promisor == promise.beneficiary || promise.kind > uint8_t(nations::strategic_statecraft::commitment_kind::guarantee) || promise.active > 1) {
				records_ok = false;
				break;
			}
		}
	}
	if(records_ok) {
		auto const valid_index = [](uint32_t value) {
			return value == nations::strategic_statecraft::crisis_memory::no_nation || value < maximum_nations;
		};
		auto const& crisis = state.strategic_crisis;
		if(crisis.phase > uint8_t(nations::strategic_statecraft::crisis_phase::withdrawn) || crisis.outcome > uint8_t(nations::strategic_statecraft::crisis_phase::withdrawn) || crisis.resolution_decision > uint8_t(nations::strategic_statecraft::decision_code::crisis_war) || crisis.partial_concession > 1 || !valid_index(crisis.claimant) || !valid_index(crisis.target) || !valid_index(crisis.outcome_actor) || !bounded_unit(crisis.demand_value) || !bounded_unit(crisis.offered_value) || !bounded_unit(crisis.readiness_cost))
			records_ok = false;
		else if(crisis.temporary_offer_goal_slot >= 0) {
			auto const slot = std::size_t(crisis.temporary_offer_goal_slot);
			if(slot >= state.crisis_attacker_wargoals.size() || !state.crisis_attacker_wargoals[slot].cb)
				records_ok = false;
		} else if(crisis.temporary_offer_goal_slot <= -2) {
			auto const slot = std::size_t(-int64_t(crisis.temporary_offer_goal_slot) - 2);
			if(slot >= state.crisis_defender_wargoals.size() || !state.crisis_defender_wargoals[slot].cb)
				records_ok = false;
		}
	}
	if(!records_ok) {
		disable_strategic_statecraft(state);
		return payload_end;
	}
	state.strategic_statecraft_initialized = initialized != 0;
	if(!state.strategic_statecraft_initialized) {
		disable_strategic_statecraft(state);
	}
	return payload_end;
}

std::size_t transformation_politics_save_size(sys::state const& state) {
	assert(state.transformation_government_state.size() == state.world.nation_size());
	if(state.transformation_government_state.size() != state.world.nation_size()) std::abort();
	auto const count = state.transformation_government_state.size();
	return transformation_politics_save_header_size
		+ count * transformation_politics_save_record_size;
}

uint8_t* write_transformation_politics_save(uint8_t* ptr, sys::state const& state) {
	assert(state.transformation_government_state.size() == state.world.nation_size());
	if(state.transformation_government_state.size() != state.world.nation_size()) std::abort();
	auto const count = state.transformation_government_state.size();

	auto const payload_size = uint32_t(count * transformation_politics_save_record_size);
	auto const count_u32 = uint32_t(count);
	ptr = memcpy_serialize(ptr, transformation_politics_save_magic);
	ptr = memcpy_serialize(ptr, transformation_politics_save_version);
	uint16_t reserved = 0;
	ptr = memcpy_serialize(ptr, reserved);
	ptr = memcpy_serialize(ptr, payload_size);
	ptr = memcpy_serialize(ptr, count_u32);
	for(std::size_t index = 0; index < count; ++index) {
		auto const& government = state.transformation_government_state[index];
		ptr = memcpy_serialize(ptr, government.groups);
		ptr = memcpy_serialize(ptr, government.established_on);
		for(auto const confidence : government.confidence)
			ptr = memcpy_serialize(ptr, confidence);
		ptr = memcpy_serialize(ptr, government.party_mandate);
	}
	return ptr;
}

uint8_t const* read_transformation_politics_save(
	uint8_t const* ptr, uint8_t const* section_end, sys::state& state, bool& loaded) {
	loaded = false;
	if(std::size_t(section_end - ptr) < transformation_politics_save_header_size)
		return ptr;

	uint32_t magic = 0;
	uint16_t version = 0;
	uint16_t reserved = 0;
	uint32_t payload_size = 0;
	uint32_t count = 0;
	ptr = memcpy_deserialize(ptr, magic);
	ptr = memcpy_deserialize(ptr, version);
	ptr = memcpy_deserialize(ptr, reserved);
	ptr = memcpy_deserialize(ptr, payload_size);
	ptr = memcpy_deserialize(ptr, count);
	(void)reserved;

	if(magic != transformation_politics_save_magic)
		return ptr - transformation_politics_save_header_size;
	if(std::size_t(section_end - ptr) < payload_size)
		return section_end;
	if(version != transformation_politics_save_version)
		return ptr + payload_size;
	if(count != state.world.nation_size()
		|| payload_size != count * transformation_politics_save_record_size)
		return ptr + payload_size;

	auto const* payload_start = ptr;
	state.transformation_government_state.assign(state.world.nation_size(), {});
	bool records_valid = true;
	for(uint32_t index = 0; index < count; ++index) {
		uint32_t groups = 0;
		int32_t established_on = 0;
		std::array<float, politics::transformation::interest_group_count> confidence{};
		ptr = memcpy_deserialize(ptr, groups);
		ptr = memcpy_deserialize(ptr, established_on);
		for(auto& value : confidence)
			ptr = memcpy_deserialize(ptr, value);
		float party_mandate = -1.0f;
		ptr = memcpy_deserialize(ptr, party_mandate);
		bool confidence_valid = true;
		for(auto const value : confidence)
			confidence_valid = confidence_valid && std::isfinite(value) && value >= 0.0f && value <= 1.0f;
		bool const mandate_valid = std::isfinite(party_mandate)
			&& (party_mandate == -1.0f || party_mandate >= 0.0f && party_mandate <= 1.0f);
		if(!confidence_valid || !mandate_valid)
			records_valid = false;
		auto& government = state.transformation_government_state[index];
		government.groups = groups;
		government.established_on = established_on;
		government.confidence = confidence;
		government.party_mandate = party_mandate;
	}
	loaded = records_valid && ptr == payload_start + payload_size;
	return payload_start + payload_size;
}

std::size_t transformation_legislation_save_size(sys::state const& state) {
	assert(state.transformation_legislation_state.size() == state.world.nation_size());
	if(state.transformation_legislation_state.size() != state.world.nation_size()) std::abort();
	auto const count = state.transformation_legislation_state.size();
	return transformation_politics_save_header_size
		+ count * transformation_legislation_save_record_size;
}

uint8_t* write_transformation_legislation_save(uint8_t* ptr, sys::state const& state) {
	assert(state.transformation_legislation_state.size() == state.world.nation_size());
	if(state.transformation_legislation_state.size() != state.world.nation_size()) std::abort();
	auto const count = state.transformation_legislation_state.size();

	auto const payload_size = uint32_t(count * transformation_legislation_save_record_size);
	ptr = memcpy_serialize(ptr, transformation_legislation_save_magic);
	ptr = memcpy_serialize(ptr, transformation_legislation_save_version);
	uint16_t reserved = 0;
	ptr = memcpy_serialize(ptr, reserved);
	ptr = memcpy_serialize(ptr, payload_size);
	ptr = memcpy_serialize(ptr, uint32_t(count));
	for(std::size_t index = 0; index < count; ++index) {
		auto const& bill = state.transformation_legislation_state[index];
		ptr = memcpy_serialize(ptr, uint8_t(bill.active ? 1 : 0));
		ptr = memcpy_serialize(ptr, uint8_t(bill.target));
		ptr = memcpy_serialize(ptr, uint8_t(bill.stage));
		ptr = memcpy_serialize(ptr, bill.stage_days);
		ptr = memcpy_serialize(ptr, bill.proposed_on);
		ptr = memcpy_serialize(ptr, uint32_t(bill.option ? bill.option.index() : 0));
		ptr = memcpy_serialize(ptr, uint32_t(bill.reform ? bill.reform.index() : 0));
		ptr = memcpy_serialize(ptr, uint32_t(bill.sponsor ? bill.sponsor.index() : 0));
		ptr = memcpy_serialize(ptr, bill.party_support);
		ptr = memcpy_serialize(ptr, bill.compromise);
		ptr = memcpy_serialize(ptr, bill.mandate);
		ptr = memcpy_serialize(ptr, bill.execution);
		ptr = memcpy_serialize(ptr, bill.coalition_support);
	}
	return ptr;
}

uint8_t const* read_transformation_legislation_save(
	uint8_t const* ptr, uint8_t const* section_end, sys::state& state, bool& loaded) {
	loaded = false;
	if(std::size_t(section_end - ptr) < transformation_politics_save_header_size)
		return ptr;

	uint32_t magic = 0;
	uint16_t version = 0;
	uint16_t reserved = 0;
	uint32_t payload_size = 0;
	uint32_t count = 0;
	ptr = memcpy_deserialize(ptr, magic);
	ptr = memcpy_deserialize(ptr, version);
	ptr = memcpy_deserialize(ptr, reserved);
	ptr = memcpy_deserialize(ptr, payload_size);
	ptr = memcpy_deserialize(ptr, count);
	(void)reserved;
	if(magic != transformation_legislation_save_magic)
		return ptr - transformation_politics_save_header_size;
	if(std::size_t(section_end - ptr) < payload_size)
		return section_end;
	if(version != transformation_legislation_save_version)
		return ptr + payload_size;
	if(count != state.world.nation_size()
		|| payload_size != count * transformation_legislation_save_record_size)
		return ptr + payload_size;

	auto const* payload_start = ptr;
	state.transformation_legislation_state.assign(state.world.nation_size(), {});
	bool records_valid = true;
	for(uint32_t index = 0; index < count; ++index) {
		uint8_t active = 0;
		uint8_t target = uint8_t(politics::transformation::legislation_target::issue);
		uint8_t stage = 0;
		uint16_t stage_days = 0;
		int32_t proposed_on = 0;
		uint32_t option_index = 0;
		uint32_t reform_index = 0;
		uint32_t sponsor_index = 0;
		float party_support = 0.5f;
		float compromise = 0.0f;
		float mandate = 0.0f;
		float execution = 0.0f;
		float coalition_support = 0.0f;
		ptr = memcpy_deserialize(ptr, active);
		ptr = memcpy_deserialize(ptr, target);
		ptr = memcpy_deserialize(ptr, stage);
		ptr = memcpy_deserialize(ptr, stage_days);
		ptr = memcpy_deserialize(ptr, proposed_on);
		ptr = memcpy_deserialize(ptr, option_index);
		ptr = memcpy_deserialize(ptr, reform_index);
		ptr = memcpy_deserialize(ptr, sponsor_index);
		ptr = memcpy_deserialize(ptr, party_support);
		ptr = memcpy_deserialize(ptr, compromise);
		ptr = memcpy_deserialize(ptr, mandate);
		ptr = memcpy_deserialize(ptr, execution);
		ptr = memcpy_deserialize(ptr, coalition_support);
		bool const issue_target = target == uint8_t(politics::transformation::legislation_target::issue);
		bool const reform_target = target == uint8_t(politics::transformation::legislation_target::reform);
		bool const support_valid = std::isfinite(party_support) && party_support >= 0.0f && party_support <= 1.0f
			&& std::isfinite(compromise) && std::isfinite(mandate)
			&& std::isfinite(execution) && std::isfinite(coalition_support);
		bool const active_bill_valid = active == 0
			|| (issue_target && option_index < state.world.issue_option_size())
			|| (reform_target && reform_index < state.world.reform_option_size());
		if(active > 1 || (!issue_target && !reform_target)
			|| stage > uint8_t(politics::transformation::legislation_stage::implementation)
			|| !support_valid || !active_bill_valid)
			records_valid = false;
		if(active != 0 && active_bill_valid) {
			auto& bill = state.transformation_legislation_state[index];
			bill.active = true;
			bill.target = reform_target
				? politics::transformation::legislation_target::reform
				: politics::transformation::legislation_target::issue;
			if(issue_target) {
				bill.option = dcon::issue_option_id{
					dcon::issue_option_id::value_base_t(option_index)};
			} else {
				bill.reform = dcon::reform_option_id{
					dcon::reform_option_id::value_base_t(reform_index)};
			}
			if(sponsor_index < state.world.political_party_size())
				bill.sponsor = dcon::political_party_id{
					dcon::political_party_id::value_base_t(sponsor_index)};
			bill.stage = politics::transformation::legislation_stage(
				std::min<uint8_t>(stage, uint8_t(politics::transformation::legislation_stage::implementation)));
			bill.stage_days = stage_days;
			bill.proposed_on = proposed_on;
			bill.party_support = party_support;
			bill.compromise = compromise;
			bill.mandate = mandate;
			bill.execution = execution;
			bill.coalition_support = coalition_support;
		}
	}
	loaded = records_valid && ptr == payload_start + payload_size;
	return payload_start + payload_size;
}

} // namespace

uint8_t const* read_scenario_header(uint8_t const* ptr_in, scenario_header& header_out) {
	uint32_t length = 0;
	memcpy(&length, ptr_in, sizeof(uint32_t));
	memcpy(&header_out, ptr_in + sizeof(uint32_t), std::min(length, uint32_t(sizeof(scenario_header))));
	return ptr_in + sizeof(uint32_t) + length;
}

uint8_t const* read_save_header(uint8_t const* ptr_in, save_header& header_out) {
	uint32_t length = 0;
	memcpy(&length, ptr_in, sizeof(uint32_t));
	memcpy(&header_out, ptr_in + sizeof(uint32_t), std::min(length, uint32_t(sizeof(save_header))));
	return ptr_in + sizeof(uint32_t) + length;
}

uint8_t* write_scenario_header(uint8_t* ptr_in, scenario_header const& header_in) {
	uint32_t length = uint32_t(sizeof(scenario_header));
	memcpy(ptr_in, &length, sizeof(uint32_t));
	memcpy(ptr_in + sizeof(uint32_t), &header_in, sizeof(scenario_header));
	return ptr_in + sizeof_scenario_header(header_in);
}
uint8_t* write_save_header(uint8_t* ptr_in, save_header const& header_in) {
	uint32_t length = uint32_t(sizeof(save_header));
	memcpy(ptr_in, &length, sizeof(uint32_t));
	memcpy(ptr_in + sizeof(uint32_t), &header_in, sizeof(save_header));
	return ptr_in + sizeof_save_header(header_in);
}

size_t sizeof_scenario_header(scenario_header const& header_in) {
	return sizeof(uint32_t) + sizeof(scenario_header);
}

size_t sizeof_save_header(save_header const& header_in) {
	return sizeof(uint32_t) + sizeof(save_header);
}

void read_mod_path(uint8_t const* ptr_in, uint8_t const* lim, native_string& path_out) {
	uint32_t length = 0;
	if(size_t(lim - ptr_in) < sizeof(uint32_t))
		return;

	memcpy(&length, ptr_in, sizeof(uint32_t));
	ptr_in += sizeof(uint32_t);

	if(size_t(lim - ptr_in) < sizeof(uint32_t) + length * sizeof(char))
		return;

	const char* path_char_ptr = reinterpret_cast<const char*>(ptr_in);
	auto path_str = std::string{ path_char_ptr, length };
	path_out =  simple_fs::utf8_to_native(path_str);
}
uint8_t const* load_mod_path(uint8_t const* ptr_in, sys::state& state) {
	uint32_t length = 0;
	memcpy(&length, ptr_in, sizeof(uint32_t));
	ptr_in += sizeof(uint32_t);
	const char* path_char_ptr = reinterpret_cast<const char *>(ptr_in);
	auto path_str = std::string{ path_char_ptr, length };
	auto native_str = simple_fs::utf8_to_native(path_str);
	auto str_view = native_string_view{ native_str };

	simple_fs::restore_state(state.common_fs, str_view);
	return ptr_in + length * sizeof(char);
}
uint8_t* write_mod_path(uint8_t* ptr_in, native_string const& path_in) {
	auto uf8_path = simple_fs::native_to_utf8(path_in);
	uint32_t length = uint32_t(uf8_path.length());
	memcpy(ptr_in, &length, sizeof(uint32_t));
	ptr_in += sizeof(uint32_t);
	memcpy(ptr_in, uf8_path.c_str(), length * sizeof(char));
	ptr_in += length * sizeof(char);
	return ptr_in;
}
size_t sizeof_mod_path(native_string const& path_in) {
	auto uf8_path = simple_fs::native_to_utf8(path_in);
	size_t sz = 0;
	uint32_t length = uint32_t(uf8_path.length());
	sz += sizeof(uint32_t);
	sz += length * sizeof(char);
	return sz;
}

mod_identifier extract_mod_information(uint8_t const* ptr_in, uint64_t file_size) {
	scenario_header h;

	auto file_end = ptr_in + file_size;

	if(file_size > sizeof_scenario_header(h)) {
		ptr_in = read_scenario_header(ptr_in, h);
	}

	if(!readable_scenario_version(h.version)) {
		return mod_identifier{ native_string{}, 0, 0 };
	}

	native_string mod_path;
	read_mod_path(ptr_in, file_end, mod_path);

	return mod_identifier{ mod_path, h.timestamp, h.count };
}

uint8_t* write_compressed_section(uint8_t* ptr_out, uint8_t const* ptr_in, uint32_t uncompressed_size) {
	uint32_t decompressed_length = uncompressed_size;

	uint32_t section_length = uint32_t(ZSTD_compress(ptr_out + sizeof(uint32_t) * 2, ZSTD_compressBound(uncompressed_size), ptr_in,
		uncompressed_size, 0)); // write compressed data

	memcpy(ptr_out, &section_length, sizeof(uint32_t));
	memcpy(ptr_out + sizeof(uint32_t), &decompressed_length, sizeof(uint32_t));

	return ptr_out + sizeof(uint32_t) * 2 + section_length;
}

template<typename T>
uint8_t const* with_decompressed_section(uint8_t const* ptr_in, T const& function) {
	uint32_t section_length = 0;
	uint32_t decompressed_length = 0;
	memcpy(&section_length, ptr_in, sizeof(uint32_t));
	memcpy(&decompressed_length, ptr_in + sizeof(uint32_t), sizeof(uint32_t));

	uint8_t* temp_buffer = new uint8_t[decompressed_length];
	// TODO: allocate memory for decompression and decompress into it

	ZSTD_decompress(temp_buffer, decompressed_length, ptr_in + sizeof(uint32_t) * 2, section_length);

	// function(ptr_in + sizeof(uint32_t) * 2, decompressed_length);
	function(temp_buffer, decompressed_length);

	delete[] temp_buffer;
	return ptr_in + sizeof(uint32_t) * 2 + section_length;
}


uint8_t const* read_handwritten_scenario_section(uint8_t const* ptr_in, uint8_t const* section_end, sys::state& state, bool exclude_local_handwritten_fields = false) {
	// hand-written contribution
	{  // lua script
		ptr_in = deserialize(ptr_in, state.lua_combined_script);
		ptr_in = deserialize(ptr_in, state.lua_game_loop_script);
		ptr_in = deserialize(ptr_in, state.lua_ui_script);
	}
	{ // map
		ptr_in = memcpy_deserialize(ptr_in, state.map_state.map_data.size_x);
		ptr_in = memcpy_deserialize(ptr_in, state.map_state.map_data.size_y);
		ptr_in = memcpy_deserialize(ptr_in, state.map_state.map_data.world_circumference);
		if(!exclude_local_handwritten_fields) {
			ptr_in = deserialize(ptr_in, state.map_state.map_data.river_vertices);
			ptr_in = deserialize(ptr_in, state.map_state.map_data.river_starts);
			ptr_in = deserialize(ptr_in, state.map_state.map_data.river_counts);
			ptr_in = deserialize(ptr_in, state.map_state.map_data.coastal_vertices);
			ptr_in = deserialize(ptr_in, state.map_state.map_data.coastal_starts);
			ptr_in = deserialize(ptr_in, state.map_state.map_data.coastal_counts);
			ptr_in = deserialize(ptr_in, state.map_state.map_data.border_vertices);
			ptr_in = deserialize(ptr_in, state.map_state.map_data.borders);
		}
		ptr_in = deserialize(ptr_in, state.map_state.map_data.terrain_id_map);
		ptr_in = deserialize(ptr_in, state.map_state.map_data.province_id_map);
		ptr_in = deserialize(ptr_in, state.map_state.map_data.province_area);
		ptr_in = deserialize(ptr_in, state.map_state.map_data.province_area_km2);
		ptr_in = deserialize(ptr_in, state.map_state.map_data.diagonal_borders);
	}
	{
		memcpy(&(state.defines), ptr_in, sizeof(parsing::defines));
		ptr_in += sizeof(parsing::defines);
	}
	{
		memcpy(&(state.economy_definitions), ptr_in, sizeof(economy::global_economy_state));
		ptr_in += sizeof(economy::global_economy_state);
	}
	{ // culture definitions
		ptr_in = deserialize(ptr_in, state.culture_definitions.party_issues);
		ptr_in = deserialize(ptr_in, state.culture_definitions.political_issues);
		ptr_in = deserialize(ptr_in, state.culture_definitions.social_issues);
		ptr_in = deserialize(ptr_in, state.culture_definitions.military_issues);
		ptr_in = deserialize(ptr_in, state.culture_definitions.economic_issues);
		ptr_in = deserialize(ptr_in, state.culture_definitions.tech_folders);
		ptr_in = deserialize(ptr_in, state.culture_definitions.crimes);
		ptr_in = memcpy_deserialize(ptr_in, state.culture_definitions.artisans);
		ptr_in = memcpy_deserialize(ptr_in, state.culture_definitions.capitalists);
		ptr_in = memcpy_deserialize(ptr_in, state.culture_definitions.farmers);
		ptr_in = memcpy_deserialize(ptr_in, state.culture_definitions.laborers);
		ptr_in = memcpy_deserialize(ptr_in, state.culture_definitions.clergy);
		ptr_in = memcpy_deserialize(ptr_in, state.culture_definitions.soldiers);
		ptr_in = memcpy_deserialize(ptr_in, state.culture_definitions.officers);
		ptr_in = memcpy_deserialize(ptr_in, state.culture_definitions.slaves);
		ptr_in = memcpy_deserialize(ptr_in, state.culture_definitions.bureaucrat);
		ptr_in = memcpy_deserialize(ptr_in, state.culture_definitions.aristocrat);
		ptr_in = memcpy_deserialize(ptr_in, state.culture_definitions.primary_factory_worker);
		ptr_in = memcpy_deserialize(ptr_in, state.culture_definitions.secondary_factory_worker);
		ptr_in = memcpy_deserialize(ptr_in, state.culture_definitions.officer_leadership_points);
		ptr_in = memcpy_deserialize(ptr_in, state.culture_definitions.bureaucrat_tax_efficiency);
		ptr_in = memcpy_deserialize(ptr_in, state.culture_definitions.conservative);
		ptr_in = memcpy_deserialize(ptr_in, state.culture_definitions.jingoism);
		ptr_in = memcpy_deserialize(ptr_in, state.culture_definitions.promotion_chance);
		ptr_in = memcpy_deserialize(ptr_in, state.culture_definitions.demotion_chance);
		ptr_in = memcpy_deserialize(ptr_in, state.culture_definitions.migration_chance);
		ptr_in = memcpy_deserialize(ptr_in, state.culture_definitions.colonialmigration_chance);
		ptr_in = memcpy_deserialize(ptr_in, state.culture_definitions.emigration_chance);
		ptr_in = memcpy_deserialize(ptr_in, state.culture_definitions.assimilation_chance);
		ptr_in = memcpy_deserialize(ptr_in, state.culture_definitions.conversion_chance);
	}
	{ // military definitions
		ptr_in = memcpy_deserialize(ptr_in, state.military_definitions.first_background_trait);
		ptr_in = deserialize(ptr_in, state.military_definitions.unit_base_definitions);
		ptr_in = memcpy_deserialize(ptr_in, state.military_definitions.base_army_unit);
		ptr_in = memcpy_deserialize(ptr_in, state.military_definitions.base_naval_unit);
		ptr_in = memcpy_deserialize(ptr_in, state.military_definitions.standard_civil_war);
		ptr_in = memcpy_deserialize(ptr_in, state.military_definitions.standard_great_war);
		ptr_in = memcpy_deserialize(ptr_in, state.military_definitions.standard_status_quo);
		ptr_in = memcpy_deserialize(ptr_in, state.military_definitions.liberate);
		ptr_in = memcpy_deserialize(ptr_in, state.military_definitions.uninstall_communist_gov);
		ptr_in = memcpy_deserialize(ptr_in, state.military_definitions.crisis_colony);
		ptr_in = memcpy_deserialize(ptr_in, state.military_definitions.crisis_liberate);
		ptr_in = memcpy_deserialize(ptr_in, state.military_definitions.irregular);
	}
	{ // national definitions
		if(!exclude_local_handwritten_fields) {
			ptr_in = deserialize(ptr_in, state.national_definitions.flag_variable_names);
			ptr_in = deserialize(ptr_in, state.national_definitions.global_flag_variable_names);
			ptr_in = deserialize(ptr_in, state.national_definitions.variable_names);
		}
		ptr_in = deserialize(ptr_in, state.national_definitions.triggered_modifiers);
		ptr_in = memcpy_deserialize(ptr_in, state.national_definitions.rebel_id);
		ptr_in = memcpy_deserialize(ptr_in, state.national_definitions.very_easy_player);
		ptr_in = memcpy_deserialize(ptr_in, state.national_definitions.easy_player);
		ptr_in = memcpy_deserialize(ptr_in, state.national_definitions.hard_player);
		ptr_in = memcpy_deserialize(ptr_in, state.national_definitions.very_hard_player);
		ptr_in = memcpy_deserialize(ptr_in, state.national_definitions.very_easy_ai);
		ptr_in = memcpy_deserialize(ptr_in, state.national_definitions.easy_ai);
		ptr_in = memcpy_deserialize(ptr_in, state.national_definitions.hard_ai);
		ptr_in = memcpy_deserialize(ptr_in, state.national_definitions.very_hard_ai);
		ptr_in = memcpy_deserialize(ptr_in, state.national_definitions.overseas);
		ptr_in = memcpy_deserialize(ptr_in, state.national_definitions.coastal);
		ptr_in = memcpy_deserialize(ptr_in, state.national_definitions.non_coastal);
		ptr_in = memcpy_deserialize(ptr_in, state.national_definitions.coastal_sea);
		ptr_in = memcpy_deserialize(ptr_in, state.national_definitions.sea_zone);
		ptr_in = memcpy_deserialize(ptr_in, state.national_definitions.land_province);
		ptr_in = memcpy_deserialize(ptr_in, state.national_definitions.blockaded);
		ptr_in = memcpy_deserialize(ptr_in, state.national_definitions.no_adjacent_controlled);
		ptr_in = memcpy_deserialize(ptr_in, state.national_definitions.core);
		ptr_in = memcpy_deserialize(ptr_in, state.national_definitions.has_siege);
		ptr_in = memcpy_deserialize(ptr_in, state.national_definitions.occupied);
		ptr_in = memcpy_deserialize(ptr_in, state.national_definitions.nationalism);
		ptr_in = memcpy_deserialize(ptr_in, state.national_definitions.infrastructure);
		ptr_in = memcpy_deserialize(ptr_in, state.national_definitions.base_values);
		ptr_in = memcpy_deserialize(ptr_in, state.national_definitions.war);
		ptr_in = memcpy_deserialize(ptr_in, state.national_definitions.peace);
		ptr_in = memcpy_deserialize(ptr_in, state.national_definitions.disarming);
		ptr_in = memcpy_deserialize(ptr_in, state.national_definitions.war_exhaustion);
		ptr_in = memcpy_deserialize(ptr_in, state.national_definitions.badboy);
		ptr_in = memcpy_deserialize(ptr_in, state.national_definitions.debt_default_to);
		ptr_in = memcpy_deserialize(ptr_in, state.national_definitions.bad_debter);
		ptr_in = memcpy_deserialize(ptr_in, state.national_definitions.great_power);
		ptr_in = memcpy_deserialize(ptr_in, state.national_definitions.second_power);
		ptr_in = memcpy_deserialize(ptr_in, state.national_definitions.civ_nation);
		ptr_in = memcpy_deserialize(ptr_in, state.national_definitions.unciv_nation);
		ptr_in = memcpy_deserialize(ptr_in, state.national_definitions.average_literacy);
		ptr_in = memcpy_deserialize(ptr_in, state.national_definitions.plurality);
		ptr_in = memcpy_deserialize(ptr_in, state.national_definitions.generalised_debt_default);
		ptr_in = memcpy_deserialize(ptr_in, state.national_definitions.total_occupation);
		ptr_in = memcpy_deserialize(ptr_in, state.national_definitions.total_blockaded);
		ptr_in = memcpy_deserialize(ptr_in, state.national_definitions.in_bankrupcy);
		ptr_in = memcpy_deserialize(ptr_in, state.national_definitions.num_allocated_national_variables);
		ptr_in = memcpy_deserialize(ptr_in, state.national_definitions.num_allocated_national_flags);
		ptr_in = memcpy_deserialize(ptr_in, state.national_definitions.num_allocated_global_flags);
		ptr_in = memcpy_deserialize(ptr_in, state.national_definitions.flashpoint_focus);
		ptr_in = memcpy_deserialize(ptr_in, state.national_definitions.flashpoint_amount);
		ptr_in = deserialize(ptr_in, state.national_definitions.on_yearly_pulse);
		ptr_in = deserialize(ptr_in, state.national_definitions.on_quarterly_pulse);
		ptr_in = deserialize(ptr_in, state.national_definitions.on_battle_won);
		ptr_in = deserialize(ptr_in, state.national_definitions.on_battle_lost);
		ptr_in = deserialize(ptr_in, state.national_definitions.on_surrender);
		ptr_in = deserialize(ptr_in, state.national_definitions.on_new_great_nation);
		ptr_in = deserialize(ptr_in, state.national_definitions.on_lost_great_nation);
		ptr_in = deserialize(ptr_in, state.national_definitions.on_election_tick);
		ptr_in = deserialize(ptr_in, state.national_definitions.on_colony_to_state);
		ptr_in = deserialize(ptr_in, state.national_definitions.on_state_conquest);
		ptr_in = deserialize(ptr_in, state.national_definitions.on_colony_to_state_free_slaves);
		ptr_in = deserialize(ptr_in, state.national_definitions.on_debtor_default);
		ptr_in = deserialize(ptr_in, state.national_definitions.on_debtor_default_small);
		ptr_in = deserialize(ptr_in, state.national_definitions.on_debtor_default_second);
		ptr_in = deserialize(ptr_in, state.national_definitions.on_civilize);
		ptr_in = deserialize(ptr_in, state.national_definitions.on_my_factories_nationalized);
		ptr_in = deserialize(ptr_in, state.national_definitions.on_crisis_declare_interest);
		ptr_in = deserialize(ptr_in, state.national_definitions.on_election_started);
		ptr_in = deserialize(ptr_in, state.national_definitions.on_election_finished);
	}
	{ // provincial definitions
		ptr_in = deserialize(ptr_in, state.province_definitions.canals);
		ptr_in = deserialize(ptr_in, state.province_definitions.canal_provinces);
		if(!exclude_local_handwritten_fields) {
			ptr_in = deserialize(ptr_in, state.province_definitions.terrain_to_gfx_map);
		}
		ptr_in = memcpy_deserialize(ptr_in, state.province_definitions.first_sea_province);
		ptr_in = memcpy_deserialize(ptr_in, state.province_definitions.europe);
		ptr_in = memcpy_deserialize(ptr_in, state.province_definitions.asia);
		ptr_in = memcpy_deserialize(ptr_in, state.province_definitions.africa);
		ptr_in = memcpy_deserialize(ptr_in, state.province_definitions.north_america);
		ptr_in = memcpy_deserialize(ptr_in, state.province_definitions.south_america);
		ptr_in = memcpy_deserialize(ptr_in, state.province_definitions.oceania);
	}
	ptr_in = memcpy_deserialize(ptr_in, state.start_date);
	ptr_in = memcpy_deserialize(ptr_in, state.end_date);
	ptr_in = deserialize(ptr_in, state.trigger_data);
	ptr_in = deserialize(ptr_in, state.trigger_data_indices);
	ptr_in = deserialize(ptr_in, state.effect_data);
	ptr_in = deserialize(ptr_in, state.effect_data_indices);
	ptr_in = deserialize(ptr_in, state.value_modifier_segments);
	ptr_in = deserialize(ptr_in, state.value_modifiers);
	if(!exclude_local_handwritten_fields) {
		ptr_in = deserialize(ptr_in, state.key_data);
		simple_fs::fileseperators_from_standard_to_native(state.key_data);
		ptr_in = deserialize(ptr_in, state.untrans_key_to_text_sequence);
	}
	ptr_in = memcpy_deserialize(ptr_in, state.hardcoded_gamerules);

	if(!exclude_local_handwritten_fields){ // ui definitions
		ptr_in = deserialize(ptr_in, state.ui_defs.gfx);
		ptr_in = deserialize(ptr_in, state.ui_defs.textures);
		ptr_in = deserialize(ptr_in, state.ui_defs.gui);
		ptr_in = deserialize(ptr_in, state.font_collection.font_names);
		ptr_in = deserialize(ptr_in, state.ui_defs.extensions);
	}
	return ptr_in;
}

uint8_t const* read_scenario_section(uint8_t const* ptr_in, uint8_t const* section_end, sys::state& state, bool exclude_local_handwritten_fields) {
	ptr_in = read_handwritten_scenario_section(ptr_in, section_end, state, exclude_local_handwritten_fields);

	// data container

	dcon::load_record loaded;
	std::byte const* start = reinterpret_cast<std::byte const*>(ptr_in);
	state.world.deserialize(start, reinterpret_cast<std::byte const*>(section_end), loaded);

	return section_end;
}


uint8_t* write_handwritten_scenario_section(uint8_t* ptr_in, sys::state& state, bool exclude_local_handwritten_fields = false) {
	// hand-written contribution
	{  // lua script
		ptr_in = serialize(ptr_in, state.lua_combined_script);
		ptr_in = serialize(ptr_in, state.lua_game_loop_script);
		ptr_in = serialize(ptr_in, state.lua_ui_script);
	}
	{ // map
		ptr_in = memcpy_serialize(ptr_in, state.map_state.map_data.size_x);
		ptr_in = memcpy_serialize(ptr_in, state.map_state.map_data.size_y);
		ptr_in = memcpy_serialize(ptr_in, state.map_state.map_data.world_circumference);
		if(!exclude_local_handwritten_fields) {
			ptr_in = serialize(ptr_in, state.map_state.map_data.river_vertices);
			ptr_in = serialize(ptr_in, state.map_state.map_data.river_starts);
			ptr_in = serialize(ptr_in, state.map_state.map_data.river_counts);
			ptr_in = serialize(ptr_in, state.map_state.map_data.coastal_vertices);
			ptr_in = serialize(ptr_in, state.map_state.map_data.coastal_starts);
			ptr_in = serialize(ptr_in, state.map_state.map_data.coastal_counts);
			ptr_in = serialize(ptr_in, state.map_state.map_data.border_vertices);
			ptr_in = serialize(ptr_in, state.map_state.map_data.borders);
		}
		ptr_in = serialize(ptr_in, state.map_state.map_data.terrain_id_map);
		ptr_in = serialize(ptr_in, state.map_state.map_data.province_id_map);
		ptr_in = serialize(ptr_in, state.map_state.map_data.province_area);
		ptr_in = serialize(ptr_in, state.map_state.map_data.province_area_km2);
		ptr_in = serialize(ptr_in, state.map_state.map_data.diagonal_borders);
	}
	{
		memcpy(ptr_in, &(state.defines), sizeof(parsing::defines));
		ptr_in += sizeof(parsing::defines);
	}
	{
		memcpy(ptr_in, &(state.economy_definitions), sizeof(economy::global_economy_state));
		ptr_in += sizeof(economy::global_economy_state);
	}
	{ // culture definitions
		ptr_in = serialize(ptr_in, state.culture_definitions.party_issues);
		ptr_in = serialize(ptr_in, state.culture_definitions.political_issues);
		ptr_in = serialize(ptr_in, state.culture_definitions.social_issues);
		ptr_in = serialize(ptr_in, state.culture_definitions.military_issues);
		ptr_in = serialize(ptr_in, state.culture_definitions.economic_issues);
		ptr_in = serialize(ptr_in, state.culture_definitions.tech_folders);
		ptr_in = serialize(ptr_in, state.culture_definitions.crimes);
		ptr_in = memcpy_serialize(ptr_in, state.culture_definitions.artisans);
		ptr_in = memcpy_serialize(ptr_in, state.culture_definitions.capitalists);
		ptr_in = memcpy_serialize(ptr_in, state.culture_definitions.farmers);
		ptr_in = memcpy_serialize(ptr_in, state.culture_definitions.laborers);
		ptr_in = memcpy_serialize(ptr_in, state.culture_definitions.clergy);
		ptr_in = memcpy_serialize(ptr_in, state.culture_definitions.soldiers);
		ptr_in = memcpy_serialize(ptr_in, state.culture_definitions.officers);
		ptr_in = memcpy_serialize(ptr_in, state.culture_definitions.slaves);
		ptr_in = memcpy_serialize(ptr_in, state.culture_definitions.bureaucrat);
		ptr_in = memcpy_serialize(ptr_in, state.culture_definitions.aristocrat);
		ptr_in = memcpy_serialize(ptr_in, state.culture_definitions.primary_factory_worker);
		ptr_in = memcpy_serialize(ptr_in, state.culture_definitions.secondary_factory_worker);
		ptr_in = memcpy_serialize(ptr_in, state.culture_definitions.officer_leadership_points);
		ptr_in = memcpy_serialize(ptr_in, state.culture_definitions.bureaucrat_tax_efficiency);
		ptr_in = memcpy_serialize(ptr_in, state.culture_definitions.conservative);
		ptr_in = memcpy_serialize(ptr_in, state.culture_definitions.jingoism);
		ptr_in = memcpy_serialize(ptr_in, state.culture_definitions.promotion_chance);
		ptr_in = memcpy_serialize(ptr_in, state.culture_definitions.demotion_chance);
		ptr_in = memcpy_serialize(ptr_in, state.culture_definitions.migration_chance);
		ptr_in = memcpy_serialize(ptr_in, state.culture_definitions.colonialmigration_chance);
		ptr_in = memcpy_serialize(ptr_in, state.culture_definitions.emigration_chance);
		ptr_in = memcpy_serialize(ptr_in, state.culture_definitions.assimilation_chance);
		ptr_in = memcpy_serialize(ptr_in, state.culture_definitions.conversion_chance);
	}
	{ // military definitions
		ptr_in = memcpy_serialize(ptr_in, state.military_definitions.first_background_trait);
		ptr_in = serialize(ptr_in, state.military_definitions.unit_base_definitions);
		ptr_in = memcpy_serialize(ptr_in, state.military_definitions.base_army_unit);
		ptr_in = memcpy_serialize(ptr_in, state.military_definitions.base_naval_unit);
		ptr_in = memcpy_serialize(ptr_in, state.military_definitions.standard_civil_war);
		ptr_in = memcpy_serialize(ptr_in, state.military_definitions.standard_great_war);
		ptr_in = memcpy_serialize(ptr_in, state.military_definitions.standard_status_quo);
		ptr_in = memcpy_serialize(ptr_in, state.military_definitions.liberate);
		ptr_in = memcpy_serialize(ptr_in, state.military_definitions.uninstall_communist_gov);
		ptr_in = memcpy_serialize(ptr_in, state.military_definitions.crisis_colony);
		ptr_in = memcpy_serialize(ptr_in, state.military_definitions.crisis_liberate);
		ptr_in = memcpy_serialize(ptr_in, state.military_definitions.irregular);
	}
	{ // national definitions
		if(!exclude_local_handwritten_fields) {
			ptr_in = serialize(ptr_in, state.national_definitions.flag_variable_names);
			ptr_in = serialize(ptr_in, state.national_definitions.global_flag_variable_names);
			ptr_in = serialize(ptr_in, state.national_definitions.variable_names);
		}
		ptr_in = serialize(ptr_in, state.national_definitions.triggered_modifiers);
		ptr_in = memcpy_serialize(ptr_in, state.national_definitions.rebel_id);
		ptr_in = memcpy_serialize(ptr_in, state.national_definitions.very_easy_player);
		ptr_in = memcpy_serialize(ptr_in, state.national_definitions.easy_player);
		ptr_in = memcpy_serialize(ptr_in, state.national_definitions.hard_player);
		ptr_in = memcpy_serialize(ptr_in, state.national_definitions.very_hard_player);
		ptr_in = memcpy_serialize(ptr_in, state.national_definitions.very_easy_ai);
		ptr_in = memcpy_serialize(ptr_in, state.national_definitions.easy_ai);
		ptr_in = memcpy_serialize(ptr_in, state.national_definitions.hard_ai);
		ptr_in = memcpy_serialize(ptr_in, state.national_definitions.very_hard_ai);
		ptr_in = memcpy_serialize(ptr_in, state.national_definitions.overseas);
		ptr_in = memcpy_serialize(ptr_in, state.national_definitions.coastal);
		ptr_in = memcpy_serialize(ptr_in, state.national_definitions.non_coastal);
		ptr_in = memcpy_serialize(ptr_in, state.national_definitions.coastal_sea);
		ptr_in = memcpy_serialize(ptr_in, state.national_definitions.sea_zone);
		ptr_in = memcpy_serialize(ptr_in, state.national_definitions.land_province);
		ptr_in = memcpy_serialize(ptr_in, state.national_definitions.blockaded);
		ptr_in = memcpy_serialize(ptr_in, state.national_definitions.no_adjacent_controlled);
		ptr_in = memcpy_serialize(ptr_in, state.national_definitions.core);
		ptr_in = memcpy_serialize(ptr_in, state.national_definitions.has_siege);
		ptr_in = memcpy_serialize(ptr_in, state.national_definitions.occupied);
		ptr_in = memcpy_serialize(ptr_in, state.national_definitions.nationalism);
		ptr_in = memcpy_serialize(ptr_in, state.national_definitions.infrastructure);
		ptr_in = memcpy_serialize(ptr_in, state.national_definitions.base_values);
		ptr_in = memcpy_serialize(ptr_in, state.national_definitions.war);
		ptr_in = memcpy_serialize(ptr_in, state.national_definitions.peace);
		ptr_in = memcpy_serialize(ptr_in, state.national_definitions.disarming);
		ptr_in = memcpy_serialize(ptr_in, state.national_definitions.war_exhaustion);
		ptr_in = memcpy_serialize(ptr_in, state.national_definitions.badboy);
		ptr_in = memcpy_serialize(ptr_in, state.national_definitions.debt_default_to);
		ptr_in = memcpy_serialize(ptr_in, state.national_definitions.bad_debter);
		ptr_in = memcpy_serialize(ptr_in, state.national_definitions.great_power);
		ptr_in = memcpy_serialize(ptr_in, state.national_definitions.second_power);
		ptr_in = memcpy_serialize(ptr_in, state.national_definitions.civ_nation);
		ptr_in = memcpy_serialize(ptr_in, state.national_definitions.unciv_nation);
		ptr_in = memcpy_serialize(ptr_in, state.national_definitions.average_literacy);
		ptr_in = memcpy_serialize(ptr_in, state.national_definitions.plurality);
		ptr_in = memcpy_serialize(ptr_in, state.national_definitions.generalised_debt_default);
		ptr_in = memcpy_serialize(ptr_in, state.national_definitions.total_occupation);
		ptr_in = memcpy_serialize(ptr_in, state.national_definitions.total_blockaded);
		ptr_in = memcpy_serialize(ptr_in, state.national_definitions.in_bankrupcy);
		ptr_in = memcpy_serialize(ptr_in, state.national_definitions.num_allocated_national_variables);
		ptr_in = memcpy_serialize(ptr_in, state.national_definitions.num_allocated_national_flags);
		ptr_in = memcpy_serialize(ptr_in, state.national_definitions.num_allocated_global_flags);
		ptr_in = memcpy_serialize(ptr_in, state.national_definitions.flashpoint_focus);
		ptr_in = memcpy_serialize(ptr_in, state.national_definitions.flashpoint_amount);
		ptr_in = serialize(ptr_in, state.national_definitions.on_yearly_pulse);
		ptr_in = serialize(ptr_in, state.national_definitions.on_quarterly_pulse);
		ptr_in = serialize(ptr_in, state.national_definitions.on_battle_won);
		ptr_in = serialize(ptr_in, state.national_definitions.on_battle_lost);
		ptr_in = serialize(ptr_in, state.national_definitions.on_surrender);
		ptr_in = serialize(ptr_in, state.national_definitions.on_new_great_nation);
		ptr_in = serialize(ptr_in, state.national_definitions.on_lost_great_nation);
		ptr_in = serialize(ptr_in, state.national_definitions.on_election_tick);
		ptr_in = serialize(ptr_in, state.national_definitions.on_colony_to_state);
		ptr_in = serialize(ptr_in, state.national_definitions.on_state_conquest);
		ptr_in = serialize(ptr_in, state.national_definitions.on_colony_to_state_free_slaves);
		ptr_in = serialize(ptr_in, state.national_definitions.on_debtor_default);
		ptr_in = serialize(ptr_in, state.national_definitions.on_debtor_default_small);
		ptr_in = serialize(ptr_in, state.national_definitions.on_debtor_default_second);
		ptr_in = serialize(ptr_in, state.national_definitions.on_civilize);
		ptr_in = serialize(ptr_in, state.national_definitions.on_my_factories_nationalized);
		ptr_in = serialize(ptr_in, state.national_definitions.on_crisis_declare_interest);
		ptr_in = serialize(ptr_in, state.national_definitions.on_election_started);
		ptr_in = serialize(ptr_in, state.national_definitions.on_election_finished);
	}
	{ // provincial definitions
		ptr_in = serialize(ptr_in, state.province_definitions.canals);
		ptr_in = serialize(ptr_in, state.province_definitions.canal_provinces);
		if(!exclude_local_handwritten_fields) {
			ptr_in = serialize(ptr_in, state.province_definitions.terrain_to_gfx_map);
		}
		ptr_in = memcpy_serialize(ptr_in, state.province_definitions.first_sea_province);
		ptr_in = memcpy_serialize(ptr_in, state.province_definitions.europe);
		ptr_in = memcpy_serialize(ptr_in, state.province_definitions.asia);
		ptr_in = memcpy_serialize(ptr_in, state.province_definitions.africa);
		ptr_in = memcpy_serialize(ptr_in, state.province_definitions.north_america);
		ptr_in = memcpy_serialize(ptr_in, state.province_definitions.south_america);
		ptr_in = memcpy_serialize(ptr_in, state.province_definitions.oceania);
	}
	ptr_in = memcpy_serialize(ptr_in, state.start_date);
	ptr_in = memcpy_serialize(ptr_in, state.end_date);
	ptr_in = serialize(ptr_in, state.trigger_data);
	ptr_in = serialize(ptr_in, state.trigger_data_indices);
	ptr_in = serialize(ptr_in, state.effect_data);
	ptr_in = serialize(ptr_in, state.effect_data_indices);
	ptr_in = serialize(ptr_in, state.value_modifier_segments);
	ptr_in = serialize(ptr_in, state.value_modifiers);
	if(!exclude_local_handwritten_fields) {
		ptr_in = serialize(ptr_in, simple_fs::fileseperators_from_native_to_standard_copy(state.key_data));
		ptr_in = serialize(ptr_in, state.untrans_key_to_text_sequence);
	}
	ptr_in = memcpy_serialize(ptr_in, state.hardcoded_gamerules);

	if(!exclude_local_handwritten_fields){ // ui definitions
		ptr_in = serialize(ptr_in, state.ui_defs.gfx);
		ptr_in = serialize(ptr_in, state.ui_defs.textures);
		ptr_in = serialize(ptr_in, state.ui_defs.gui);
		ptr_in = serialize(ptr_in, state.font_collection.font_names);
		ptr_in = serialize(ptr_in, state.ui_defs.extensions);
	}
	return ptr_in;
}



uint8_t* write_scenario_section(uint8_t* ptr_in, sys::state& state, bool exclude_local_handwritten_fields) {
	ptr_in = write_handwritten_scenario_section(ptr_in, state, exclude_local_handwritten_fields);

	dcon::load_record result = state.world.make_serialize_record_store_scenario();
	std::byte* start = reinterpret_cast<std::byte*>(ptr_in);
	state.world.serialize(start, result);

	return reinterpret_cast<uint8_t*>(start);
}

size_t sizeof_handwritten_scenario_section(sys::state& state, bool exclude_local_handwritten_fields = false) {
	size_t sz = 0;

	// hand-written contribution
	{
		sz += serialize_size(state.lua_combined_script);
		sz += serialize_size(state.lua_game_loop_script);
		sz += serialize_size(state.lua_ui_script);
	}
	{ // map
		sz += sizeof(state.map_state.map_data.size_x);
		sz += sizeof(state.map_state.map_data.size_y);
		sz += sizeof(state.map_state.map_data.world_circumference);
		if(!exclude_local_handwritten_fields) {
			sz += serialize_size(state.map_state.map_data.river_vertices);
			sz += serialize_size(state.map_state.map_data.river_starts);
			sz += serialize_size(state.map_state.map_data.river_counts);
			sz += serialize_size(state.map_state.map_data.coastal_vertices);
			sz += serialize_size(state.map_state.map_data.coastal_starts);
			sz += serialize_size(state.map_state.map_data.coastal_counts);
			sz += serialize_size(state.map_state.map_data.border_vertices);
			sz += serialize_size(state.map_state.map_data.borders);
		}
		sz += serialize_size(state.map_state.map_data.terrain_id_map);
		sz += serialize_size(state.map_state.map_data.province_id_map);
		sz += serialize_size(state.map_state.map_data.province_area);
		sz += serialize_size(state.map_state.map_data.province_area_km2);
		sz += serialize_size(state.map_state.map_data.diagonal_borders);
	}
	{
		sz += sizeof(parsing::defines);
	}
	{
		sz += sizeof(economy::global_economy_state);
	}
	{ // culture definitions
		sz += serialize_size(state.culture_definitions.party_issues);
		sz += serialize_size(state.culture_definitions.political_issues);
		sz += serialize_size(state.culture_definitions.social_issues);
		sz += serialize_size(state.culture_definitions.military_issues);
		sz += serialize_size(state.culture_definitions.economic_issues);
		sz += serialize_size(state.culture_definitions.tech_folders);
		sz += serialize_size(state.culture_definitions.crimes);
		sz += sizeof(state.culture_definitions.artisans);
		sz += sizeof(state.culture_definitions.capitalists);
		sz += sizeof(state.culture_definitions.farmers);
		sz += sizeof(state.culture_definitions.laborers);
		sz += sizeof(state.culture_definitions.clergy);
		sz += sizeof(state.culture_definitions.soldiers);
		sz += sizeof(state.culture_definitions.officers);
		sz += sizeof(state.culture_definitions.slaves);
		sz += sizeof(state.culture_definitions.bureaucrat);
		sz += sizeof(state.culture_definitions.aristocrat);
		sz += sizeof(state.culture_definitions.primary_factory_worker);
		sz += sizeof(state.culture_definitions.secondary_factory_worker);
		sz += sizeof(state.culture_definitions.officer_leadership_points);
		sz += sizeof(state.culture_definitions.bureaucrat_tax_efficiency);
		sz += sizeof(state.culture_definitions.conservative);
		sz += sizeof(state.culture_definitions.jingoism);
		sz += sizeof(state.culture_definitions.promotion_chance);
		sz += sizeof(state.culture_definitions.demotion_chance);
		sz += sizeof(state.culture_definitions.migration_chance);
		sz += sizeof(state.culture_definitions.colonialmigration_chance);
		sz += sizeof(state.culture_definitions.emigration_chance);
		sz += sizeof(state.culture_definitions.assimilation_chance);
		sz += sizeof(state.culture_definitions.conversion_chance);
	}
	{ // military definitions
		sz += sizeof(state.military_definitions.first_background_trait);
		sz += serialize_size(state.military_definitions.unit_base_definitions);
		sz += sizeof(state.military_definitions.base_army_unit);
		sz += sizeof(state.military_definitions.base_naval_unit);
		sz += sizeof(state.military_definitions.standard_civil_war);
		sz += sizeof(state.military_definitions.standard_great_war);
		sz += sizeof(state.military_definitions.standard_status_quo);
		sz += sizeof(state.military_definitions.liberate);
		sz += sizeof(state.military_definitions.uninstall_communist_gov);
		sz += sizeof(state.military_definitions.crisis_colony);
		sz += sizeof(state.military_definitions.crisis_liberate);
		sz += sizeof(state.military_definitions.irregular);
	}
	{ // national definitions
		if(!exclude_local_handwritten_fields) {
			sz += serialize_size(state.national_definitions.flag_variable_names);
			sz += serialize_size(state.national_definitions.global_flag_variable_names);
			sz += serialize_size(state.national_definitions.variable_names);
		}
		sz += serialize_size(state.national_definitions.triggered_modifiers);
		sz += sizeof(state.national_definitions.rebel_id);
		sz += sizeof(state.national_definitions.very_easy_player);
		sz += sizeof(state.national_definitions.easy_player);
		sz += sizeof(state.national_definitions.hard_player);
		sz += sizeof(state.national_definitions.very_hard_player);
		sz += sizeof(state.national_definitions.very_easy_ai);
		sz += sizeof(state.national_definitions.easy_ai);
		sz += sizeof(state.national_definitions.hard_ai);
		sz += sizeof(state.national_definitions.very_hard_ai);
		sz += sizeof(state.national_definitions.overseas);
		sz += sizeof(state.national_definitions.coastal);
		sz += sizeof(state.national_definitions.non_coastal);
		sz += sizeof(state.national_definitions.coastal_sea);
		sz += sizeof(state.national_definitions.sea_zone);
		sz += sizeof(state.national_definitions.land_province);
		sz += sizeof(state.national_definitions.blockaded);
		sz += sizeof(state.national_definitions.no_adjacent_controlled);
		sz += sizeof(state.national_definitions.core);
		sz += sizeof(state.national_definitions.has_siege);
		sz += sizeof(state.national_definitions.occupied);
		sz += sizeof(state.national_definitions.nationalism);
		sz += sizeof(state.national_definitions.infrastructure);
		sz += sizeof(state.national_definitions.base_values);
		sz += sizeof(state.national_definitions.war);
		sz += sizeof(state.national_definitions.peace);
		sz += sizeof(state.national_definitions.disarming);
		sz += sizeof(state.national_definitions.war_exhaustion);
		sz += sizeof(state.national_definitions.badboy);
		sz += sizeof(state.national_definitions.debt_default_to);
		sz += sizeof(state.national_definitions.bad_debter);
		sz += sizeof(state.national_definitions.great_power);
		sz += sizeof(state.national_definitions.second_power);
		sz += sizeof(state.national_definitions.civ_nation);
		sz += sizeof(state.national_definitions.unciv_nation);
		sz += sizeof(state.national_definitions.average_literacy);
		sz += sizeof(state.national_definitions.plurality);
		sz += sizeof(state.national_definitions.generalised_debt_default);
		sz += sizeof(state.national_definitions.total_occupation);
		sz += sizeof(state.national_definitions.total_blockaded);
		sz += sizeof(state.national_definitions.in_bankrupcy);
		sz += sizeof(state.national_definitions.num_allocated_national_variables);
		sz += sizeof(state.national_definitions.num_allocated_national_flags);
		sz += sizeof(state.national_definitions.num_allocated_global_flags);
		sz += sizeof(state.national_definitions.flashpoint_focus);
		sz += sizeof(state.national_definitions.flashpoint_amount);
		sz += serialize_size(state.national_definitions.on_yearly_pulse);
		sz += serialize_size(state.national_definitions.on_quarterly_pulse);
		sz += serialize_size(state.national_definitions.on_battle_won);
		sz += serialize_size(state.national_definitions.on_battle_lost);
		sz += serialize_size(state.national_definitions.on_surrender);
		sz += serialize_size(state.national_definitions.on_new_great_nation);
		sz += serialize_size(state.national_definitions.on_lost_great_nation);
		sz += serialize_size(state.national_definitions.on_election_tick);
		sz += serialize_size(state.national_definitions.on_colony_to_state);
		sz += serialize_size(state.national_definitions.on_state_conquest);
		sz += serialize_size(state.national_definitions.on_colony_to_state_free_slaves);
		sz += serialize_size(state.national_definitions.on_debtor_default);
		sz += serialize_size(state.national_definitions.on_debtor_default_small);
		sz += serialize_size(state.national_definitions.on_debtor_default_second);
		sz += serialize_size(state.national_definitions.on_civilize);
		sz += serialize_size(state.national_definitions.on_my_factories_nationalized);
		sz += serialize_size(state.national_definitions.on_crisis_declare_interest);
		sz += serialize_size(state.national_definitions.on_election_started);
		sz += serialize_size(state.national_definitions.on_election_finished);
	}
	{ // provincial definitions
		sz += serialize_size(state.province_definitions.canals);
		sz += serialize_size(state.province_definitions.canal_provinces);
		if(!exclude_local_handwritten_fields) {
			sz += serialize_size(state.province_definitions.terrain_to_gfx_map);
		}
		sz += sizeof(state.province_definitions.first_sea_province);
		sz += sizeof(state.province_definitions.europe);
		sz += sizeof(state.province_definitions.asia);
		sz += sizeof(state.province_definitions.africa);
		sz += sizeof(state.province_definitions.north_america);
		sz += sizeof(state.province_definitions.south_america);
		sz += sizeof(state.province_definitions.oceania);
	}
	sz += sizeof(state.start_date);
	sz += sizeof(state.end_date);
	sz += serialize_size(state.trigger_data);
	sz += serialize_size(state.trigger_data_indices);
	sz += serialize_size(state.effect_data);
	sz += serialize_size(state.effect_data_indices);
	sz += serialize_size(state.value_modifier_segments);
	sz += serialize_size(state.value_modifiers);
	if(!exclude_local_handwritten_fields) {
		sz += serialize_size(state.key_data);
		sz += serialize_size(state.untrans_key_to_text_sequence);
	}
	sz += sizeof(state.hardcoded_gamerules);

	if(!exclude_local_handwritten_fields){ // ui definitions
		sz += serialize_size(state.ui_defs.gfx);
		sz += serialize_size(state.ui_defs.textures);
		sz += serialize_size(state.ui_defs.gui);
		sz += serialize_size(state.font_collection.font_names);
		sz += serialize_size(state.ui_defs.extensions);
	}
	return sz;

}

scenario_size sizeof_scenario_section(sys::state& state, bool exclude_local_handwritten_fields) {
	size_t sz = 0;

	// hand-written contribution
	sz += sizeof_handwritten_scenario_section(state, exclude_local_handwritten_fields);

	// data container contribution
	dcon::load_record loaded = state.world.make_serialize_record_store_scenario();
	// dcon::load_record loaded;
	auto szb = state.world.serialize_size(loaded);

	return scenario_size{ sz + szb, sz };
}

uint8_t const* read_handwritten_save_section(uint8_t const* ptr_in, uint8_t const* section_end,
	sys::state& state, exact_runtime_snapshot& exact_runtime, bool exclude_local_handwritten_fields = false) {
	// hand-written contribution
	if(!exclude_local_handwritten_fields) {
		ptr_in = deserialize(ptr_in, state.unit_names);
		ptr_in = deserialize(ptr_in, state.unit_names_indices);
		ptr_in = memcpy_deserialize(ptr_in, state.local_player_nation);
	}
	ptr_in = memcpy_deserialize(ptr_in, state.current_date);
	ptr_in = memcpy_deserialize(ptr_in, state.game_seed);
	ptr_in = memcpy_deserialize(ptr_in, state.current_crisis_state);
	ptr_in = deserialize(ptr_in, state.crisis_participants);
	ptr_in = memcpy_deserialize(ptr_in, state.crisis_temperature);
	ptr_in = memcpy_deserialize(ptr_in, state.crisis_attacker);
	ptr_in = memcpy_deserialize(ptr_in, state.crisis_defender);
	ptr_in = memcpy_deserialize(ptr_in, state.primary_crisis_attacker);
	ptr_in = memcpy_deserialize(ptr_in, state.primary_crisis_defender);
	ptr_in = memcpy_deserialize(ptr_in, state.crisis_state_instance);
	ptr_in = memcpy_deserialize(ptr_in, state.crisis_last_checked_gp);
	ptr_in = memcpy_deserialize(ptr_in, state.crisis_war);
	ptr_in = memcpy_deserialize(ptr_in, state.last_crisis_end_date);
	ptr_in = deserialize(ptr_in, state.crisis_defender_wargoals);
	ptr_in = deserialize(ptr_in, state.crisis_attacker_wargoals);
	ptr_in = memcpy_deserialize(ptr_in, state.inflation);
	ptr_in = deserialize(ptr_in, state.great_nations);
	ptr_in = deserialize(ptr_in, state.pending_n_event);
	ptr_in = deserialize(ptr_in, state.pending_f_n_event);
	ptr_in = deserialize(ptr_in, state.pending_p_event);
	ptr_in = deserialize(ptr_in, state.pending_f_p_event);
	ptr_in = memcpy_deserialize(ptr_in, state.pending_messages);
	if(!exclude_local_handwritten_fields) {
		ptr_in = deserialize(ptr_in, state.player_data_cache);
	}
	ptr_in = deserialize(ptr_in, state.future_n_event);
	ptr_in = deserialize(ptr_in, state.future_p_event);

	{ // national definitions
		ptr_in = deserialize(ptr_in, state.national_definitions.global_flag_variables);
	}

	{ // military definitions
		ptr_in = memcpy_deserialize(ptr_in, state.military_definitions.great_wars_enabled);
		ptr_in = memcpy_deserialize(ptr_in, state.military_definitions.world_wars_enabled);
	}
	ptr_in = read_transformation_politics_save(ptr_in, section_end, state,
		exact_runtime.transformation_politics_loaded);
	ptr_in = read_transformation_legislation_save(ptr_in, section_end, state,
		exact_runtime.transformation_legislation_loaded);
	ptr_in = read_strategic_statecraft_save(ptr_in, section_end, state);
	ptr_in = read_exact_runtime_save(ptr_in, section_end, exact_runtime);
	return ptr_in;
}

bool validate_strategic_statecraft_world_state(sys::state& state) {
	if(!state.strategic_statecraft_initialized) return false;
	auto const nation_count = state.world.nation_size();
	bool valid = state.strategic_interests.size() == nation_count;
	for(std::size_t i = 0; valid && i < state.strategic_interests.size(); ++i) {
		auto const& profile = state.strategic_interests[i];
		if(state.world.nation_is_valid(dcon::nation_id{dcon::nation_id::value_base_t(uint32_t(i))}) && profile.enabled == 0) {
			valid = false;
			break;
		}
		if(profile.objective_target != nations::strategic_statecraft::interests::no_nation && profile.objective_target >= nation_count) {
			valid = false;
			break;
		}
	}
	if(valid) {
		for(auto const& view : state.strategic_beliefs) {
			if(uint32_t(view.key >> 32) >= nation_count || uint32_t(view.key) >= nation_count) {
				valid = false;
				break;
			}
		}
	}
	if(valid) {
		for(auto const& promise : state.strategic_commitments) {
			if(promise.promisor >= nation_count || promise.beneficiary >= nation_count) {
				valid = false;
				break;
			}
		}
	}
	if(valid) {
		auto const is_valid_index = [nation_count](uint32_t value) {
			return value == nations::strategic_statecraft::crisis_memory::no_nation
				|| value < nation_count;
		};
		auto const& crisis = state.strategic_crisis;
		valid = is_valid_index(crisis.claimant) && is_valid_index(crisis.target)
			&& is_valid_index(crisis.outcome_actor);
	}
	if(!valid)
		disable_strategic_statecraft(state);
	return valid;
}

bool canonical_runtime_loaded(sys::state const& state) {
	std::vector<std::string> banking_errors;
	return economy::banking::validate_canonical_banking_state(state, banking_errors)
		&& bool(state.exact_population) && bool(state.exact_person_economy)
		&& bool(state.exact_person_goods) && bool(state.exact_person_freight)
		&& bool(state.labor_dynamics) && bool(state.causal_order)
		&& military::land_forces::initialized(state)
		&& state.strategic_statecraft_initialized
		&& state.strategic_interests.size() == state.world.nation_size()
		&& state.transformation_government_state.size() == state.world.nation_size()
		&& state.transformation_legislation_state.size() == state.world.nation_size()
		&& actors::ownership::canonical_ownership_is_valid(state);
}

uint8_t const* read_save_section(uint8_t const* ptr_in, uint8_t const* section_end, sys::state& state, bool exclude_local_handwritten_fields) {
	exact_runtime_snapshot exact_runtime;
	ptr_in = read_handwritten_save_section(ptr_in, section_end, state, exact_runtime,
		exclude_local_handwritten_fields);

	// data container contribution

	dcon::load_record loaded;

	if(state.network_mode == sys::network_mode_type::single_player) {
		std::byte const* start = reinterpret_cast<std::byte const*>(ptr_in);
		state.world.deserialize(start, reinterpret_cast<std::byte const*>(section_end), loaded);
	} else {
		dcon::load_record loadmask = state.world.make_serialize_record_store_save();
		std::byte const* start = reinterpret_cast<std::byte const*>(ptr_in);
		state.world.deserialize(start, reinterpret_cast<std::byte const*>(section_end), loaded, loadmask);
	}
	bool const politics_state_matches_world = state.transformation_government_state.size() == state.world.nation_size()
		&& state.transformation_legislation_state.size() == state.world.nation_size();
	bool runtime_restored = politics_state_matches_world
		&& exact_runtime.transformation_politics_loaded
		&& exact_runtime.transformation_legislation_loaded;
	if(runtime_restored) runtime_restored = restore_exact_runtime_state(state, exact_runtime);
	if(!runtime_restored) {
		clear_exact_runtime_state(state);
	}
	validate_strategic_statecraft_world_state(state);
	state.clear_army_supply_derived_data();
	return section_end;
}

uint8_t* write_handwritten_save_section(uint8_t* ptr_in, sys::state& state, bool exclude_local_handwritten_fields = false) {
	// hand-written contribution
	ptr_in = serialize(ptr_in, state.unit_names);
	ptr_in = serialize(ptr_in, state.unit_names_indices);
	if(!exclude_local_handwritten_fields) {
		ptr_in = memcpy_serialize(ptr_in, state.local_player_nation);
	}
	ptr_in = memcpy_serialize(ptr_in, state.current_date);
	ptr_in = memcpy_serialize(ptr_in, state.game_seed);
	ptr_in = memcpy_serialize(ptr_in, state.current_crisis_state);
	ptr_in = serialize(ptr_in, state.crisis_participants);
	ptr_in = memcpy_serialize(ptr_in, state.crisis_temperature);
	ptr_in = memcpy_serialize(ptr_in, state.crisis_attacker);
	ptr_in = memcpy_serialize(ptr_in, state.crisis_defender);
	ptr_in = memcpy_serialize(ptr_in, state.primary_crisis_attacker);
	ptr_in = memcpy_serialize(ptr_in, state.primary_crisis_defender);
	ptr_in = memcpy_serialize(ptr_in, state.crisis_state_instance);
	ptr_in = memcpy_serialize(ptr_in, state.crisis_last_checked_gp);
	ptr_in = memcpy_serialize(ptr_in, state.crisis_war);
	ptr_in = memcpy_serialize(ptr_in, state.last_crisis_end_date);
	ptr_in = serialize(ptr_in, state.crisis_defender_wargoals);
	ptr_in = serialize(ptr_in, state.crisis_attacker_wargoals);
	ptr_in = memcpy_serialize(ptr_in, state.inflation);
	ptr_in = serialize(ptr_in, state.great_nations);
	ptr_in = serialize(ptr_in, state.pending_n_event);
	ptr_in = serialize(ptr_in, state.pending_f_n_event);
	ptr_in = serialize(ptr_in, state.pending_p_event);
	ptr_in = serialize(ptr_in, state.pending_f_p_event);
	ptr_in = memcpy_serialize(ptr_in, state.pending_messages);
	if(!exclude_local_handwritten_fields) {
		ptr_in = serialize(ptr_in, state.player_data_cache);
	}
	ptr_in = serialize(ptr_in, state.future_n_event);
	ptr_in = serialize(ptr_in, state.future_p_event);

	{ // national definitions
		ptr_in = serialize(ptr_in, state.national_definitions.global_flag_variables);
	}
	{ // military definitions
		ptr_in = memcpy_serialize(ptr_in, state.military_definitions.great_wars_enabled);
		ptr_in = memcpy_serialize(ptr_in, state.military_definitions.world_wars_enabled);
	}
	ptr_in = write_transformation_politics_save(ptr_in, state);
	ptr_in = write_transformation_legislation_save(ptr_in, state);
	ptr_in = write_strategic_statecraft_save(ptr_in, state);
	ptr_in = write_exact_runtime_save(ptr_in, state);
	return ptr_in;
}

uint8_t* write_save_section(uint8_t* ptr_in, sys::state& state, bool exclude_local_handwritten_fields) {
	ptr_in = write_handwritten_save_section(ptr_in, state, exclude_local_handwritten_fields);

	// data container contribution
	dcon::load_record loaded = state.world.make_serialize_record_store_save();
	std::byte* start = reinterpret_cast<std::byte*>(ptr_in);
	state.world.serialize(start, loaded);

	return reinterpret_cast<uint8_t*>(start);
}

size_t sizeof_handwritten_save_section(sys::state& state, bool exclude_local_handwritten_fields = false) {
	size_t sz = 0;

	// hand-written contribution

	sz += serialize_size(state.unit_names);
	sz += serialize_size(state.unit_names_indices);
	if(!exclude_local_handwritten_fields) {
		sz += sizeof(state.local_player_nation);
	}
	sz += sizeof(state.current_date);
	sz += sizeof(state.game_seed);
	sz += sizeof(state.current_crisis_state);
	sz += serialize_size(state.crisis_participants);
	sz += sizeof(state.crisis_temperature);
	sz += sizeof(state.crisis_attacker);
	sz += sizeof(state.crisis_defender);
	sz += sizeof(state.primary_crisis_attacker);
	sz += sizeof(state.primary_crisis_defender);
	sz += sizeof(state.crisis_state_instance);
	sz += sizeof(state.crisis_last_checked_gp);
	sz += sizeof(state.crisis_war);
	sz += sizeof(state.last_crisis_end_date);
	sz += serialize_size(state.crisis_defender_wargoals);
	sz += serialize_size(state.crisis_attacker_wargoals);
	sz += sizeof(state.inflation);
	sz += serialize_size(state.great_nations);
	sz += serialize_size(state.pending_n_event);
	sz += serialize_size(state.pending_f_n_event);
	sz += serialize_size(state.pending_p_event);
	sz += serialize_size(state.pending_f_p_event);
	sz += sizeof(state.pending_messages);
	if(!exclude_local_handwritten_fields) {
		sz += serialize_size(state.player_data_cache);
	}
	sz += serialize_size(state.future_n_event);
	sz += serialize_size(state.future_p_event);

	{ // national definitions
		sz += serialize_size(state.national_definitions.global_flag_variables);
	}
	{ // military definitions
		sz += sizeof(state.military_definitions.great_wars_enabled);
		sz += sizeof(state.military_definitions.world_wars_enabled);
	}
	sz += transformation_politics_save_size(state);
	sz += transformation_legislation_save_size(state);
	sz += strategic_statecraft_save_size(state);
	sz += exact_runtime_save_size(state);
	return sz;
}

size_t sizeof_save_section(sys::state& state, bool exclude_local_handwritten_fields) {
	size_t sz = 0;

	sz += sizeof_handwritten_save_section(state, exclude_local_handwritten_fields);

	// data container contribution
	dcon::load_record loaded = state.world.make_serialize_record_store_save();
	sz += state.world.serialize_size(loaded);

	return sz;
}

uint8_t const* read_entire_mp_state(uint8_t const* ptr_in, uint8_t const* section_end, sys::state& state, bool exclude_local_handwritten_fields) {

	ptr_in = read_handwritten_scenario_section(ptr_in, section_end, state, exclude_local_handwritten_fields);
	exact_runtime_snapshot exact_runtime;
	ptr_in = read_handwritten_save_section(ptr_in, section_end, state, exact_runtime,
		exclude_local_handwritten_fields);
	dcon::load_record loaded;
	std::byte const* start = reinterpret_cast<std::byte const*>(ptr_in);
	state.world.deserialize(start, reinterpret_cast<std::byte const*>(section_end), loaded);
	bool const politics_state_matches_world = state.transformation_government_state.size() == state.world.nation_size()
		&& state.transformation_legislation_state.size() == state.world.nation_size();
	bool runtime_restored = politics_state_matches_world
		&& exact_runtime.transformation_politics_loaded
		&& exact_runtime.transformation_legislation_loaded;
	if(runtime_restored) runtime_restored = restore_exact_runtime_state(state, exact_runtime);
	if(!runtime_restored || !validate_strategic_statecraft_world_state(state)) {
		clear_exact_runtime_state(state);
		std::abort();
	}
	state.clear_army_supply_derived_data();
	return section_end;
}
uint8_t* write_entire_mp_state(uint8_t* ptr_in, sys::state& state, bool exclude_local_handwritten_fields) {
	ptr_in = write_handwritten_scenario_section(ptr_in, state, exclude_local_handwritten_fields);
	ptr_in = write_handwritten_save_section(ptr_in, state, exclude_local_handwritten_fields);


	dcon::load_record result = state.world.make_serialize_record_store_mp_checksum_excluded();
	std::byte* start = reinterpret_cast<std::byte*>(ptr_in);
	state.world.serialize(start, result);

	return reinterpret_cast<uint8_t*>(start);
}
size_t sizeof_entire_mp_state(sys::state& state, bool exclude_local_handwritten_fields) {
	size_t sz = 0;
	sz += sizeof_handwritten_scenario_section(state, exclude_local_handwritten_fields);
	sz += sizeof_handwritten_save_section(state, exclude_local_handwritten_fields);
	dcon::load_record loaded = state.world.make_serialize_record_store_mp_checksum_excluded();
	sz += state.world.serialize_size(loaded);

	return sz;
}


size_t sizeof_mp_data(sys::state& state) {
	size_t sz = 0;

	// data container contribution
	dcon::load_record loaded = state.world.make_serialize_record_store_mp_data();
	sz += state.world.serialize_size(loaded);

	return sz;
}


uint8_t* write_mp_data(uint8_t* ptr_in, sys::state& state) {

	// data container contribution
	dcon::load_record loaded = state.world.make_serialize_record_store_mp_data();
	std::byte* start = reinterpret_cast<std::byte*>(ptr_in);
	state.world.serialize(start, loaded);

	return reinterpret_cast<uint8_t*>(start);
}

uint8_t const* read_mp_data(uint8_t const* ptr_in, uint8_t const* section_end, sys::state& state) {
	dcon::load_record loaded;
	std::byte const* start = reinterpret_cast<std::byte const*>(ptr_in);
	state.world.deserialize(start, reinterpret_cast<std::byte const*>(section_end), loaded);

	return section_end;
}







void combine_load_records(dcon::load_record& affected_record, const dcon::load_record& other_record) {

	uint8_t* write_ptr = reinterpret_cast<uint8_t*>(&affected_record);
	const uint8_t* read_ptr = reinterpret_cast<const uint8_t*>(&other_record);
	for(uint32_t i = 0; i < sizeof(dcon::load_record); i++) {
		write_ptr[i] = write_ptr[i] | read_ptr[i];
	}

}


void write_scenario_file(sys::state& state, native_string_view name, uint32_t count) {
	scenario_header header;
	header.count = count;
	header.timestamp = uint64_t(std::time(nullptr));
	std::string save_dir_utf8 = simple_fs::native_to_utf8(state.mod_save_dir);
	memcpy(header.mod_save_dir, save_dir_utf8.data(), std::min(save_dir_utf8.length(), sizeof(header.mod_save_dir)));
	// Set last character to null incase the mod dir was 128 characters or more, to create the null-terminated string
	header.mod_save_dir[127] = 0;

	auto scenario_space = sizeof_scenario_section(state);
	size_t save_space = sizeof_save_section(state);

	state.scenario_counter = count;
	state.scenario_time_stamp = header.timestamp;


	// this is an upper bound, since compacting the data may require less space
	size_t total_size =
			sizeof_scenario_header(header) + sizeof_mod_path(simple_fs::extract_state(state.common_fs)) + ZSTD_compressBound(scenario_space.total_size) + ZSTD_compressBound(save_space) + sizeof(uint32_t) * 4;

	uint8_t* temp_buffer = new uint8_t[total_size];
	uint8_t* buffer_position = temp_buffer;

	buffer_position = write_scenario_header(buffer_position, header);
	buffer_position = write_mod_path(buffer_position, simple_fs::extract_state(state.common_fs));

	uint8_t* temp_scenario_buffer = new uint8_t[scenario_space.total_size];
	auto last_written = write_scenario_section(temp_scenario_buffer, state);
	auto last_written_count = last_written - temp_scenario_buffer;
	assert(size_t(last_written_count) == scenario_space.total_size);
	// calculate checksum
	checksum_key* checksum = &reinterpret_cast<scenario_header*>(temp_buffer + sizeof(uint32_t))->checksum;
	blake2b(checksum, sizeof(*checksum), temp_scenario_buffer + scenario_space.checksum_offset, scenario_space.total_size - scenario_space.checksum_offset, nullptr, 0);
	state.scenario_checksum = *checksum;

	buffer_position = write_compressed_section(buffer_position, temp_scenario_buffer, uint32_t(scenario_space.total_size));
	delete[] temp_scenario_buffer;

	uint8_t* temp_save_buffer = new uint8_t[save_space];
	auto last_save_written = write_save_section(temp_save_buffer, state);
	auto last_save_written_count = last_save_written - temp_save_buffer;
	assert(size_t(last_save_written_count) == save_space);
	buffer_position = write_compressed_section(buffer_position, temp_save_buffer, uint32_t(save_space));
	delete[] temp_save_buffer;

	auto total_size_used = buffer_position - temp_buffer;

	simple_fs::write_file(simple_fs::get_or_create_scenario_directory(), name, reinterpret_cast<char*>(temp_buffer),
			uint32_t(total_size_used));

	delete[] temp_buffer;
}
bool try_read_scenario_file(sys::state& state, native_string_view name) {
	auto dir = simple_fs::get_or_create_scenario_directory();
	auto save_file = open_file(dir, name);
	if(save_file) {
		scenario_header header;
		header.version = 0;

		auto contents = simple_fs::view_contents(*save_file);
		uint8_t const* buffer_pos = reinterpret_cast<uint8_t const*>(contents.data);
		auto file_end = buffer_pos + contents.file_size;

		if(contents.file_size > sizeof_scenario_header(header)) {
			buffer_pos = read_scenario_header(buffer_pos, header);
		}

		if(!readable_scenario_version(header.version)) {
			return false;
		}

		state.scenario_counter = header.count;
		state.scenario_time_stamp = header.timestamp;
		state.scenario_checksum = header.checksum;
		state.mod_save_dir = simple_fs::utf8_to_native( std::string( header.mod_save_dir));
		state.loaded_save_file = NATIVE("");
		state.loaded_scenario_file = name;

		buffer_pos = load_mod_path(buffer_pos, state);

		buffer_pos = with_decompressed_section(buffer_pos,
				[&](uint8_t const* ptr_in, uint32_t length) { read_scenario_section(ptr_in, ptr_in + length, state); });

		state.on_scenario_load();

		return true;
	} else {
		return false;
	}
}

bool try_read_scenario_and_save_file(sys::state& state, native_string_view name) {
	auto dir = simple_fs::get_or_create_scenario_directory();
	auto save_file = open_file(dir, name);
	if(save_file) {
		scenario_header header;
		header.version = 0;

		auto contents = simple_fs::view_contents(*save_file);
		uint8_t const* buffer_pos = reinterpret_cast<uint8_t const*>(contents.data);
		auto file_end = buffer_pos + contents.file_size;

		if(contents.file_size > sizeof_scenario_header(header)) {
			buffer_pos = read_scenario_header(buffer_pos, header);
		}

		if(!readable_scenario_version(header.version)) {
			return false;
		}

		state.scenario_counter = header.count;
		state.scenario_time_stamp = header.timestamp;
		state.scenario_checksum = header.checksum;
		state.mod_save_dir = simple_fs::utf8_to_native(std::string(header.mod_save_dir));

		state.loaded_save_file = NATIVE("");
		state.loaded_scenario_file = name;

		buffer_pos = load_mod_path(buffer_pos, state);

		buffer_pos = with_decompressed_section(buffer_pos,
				[&](uint8_t const* ptr_in, uint32_t length) { read_scenario_section(ptr_in, ptr_in + length, state); });
		state.on_scenario_load();
		buffer_pos = with_decompressed_section(buffer_pos,
				[&](uint8_t const* ptr_in, uint32_t length) { read_save_section(ptr_in, ptr_in + length, state); });
		if(!canonical_runtime_loaded(state)) return false;

		state.game_seed = uint32_t(std::random_device()());

		// only load gamerule settings if host or singleplayer. A client would have to load the host' settings anyway
		if(state.network_mode == sys::network_mode_type::host || state.network_mode == sys::network_mode_type::single_player) {
			state.load_gamerule_settings();
		}

		return true;
	} else {
		return false;
	}
}

bool try_read_scenario_as_save_file(sys::state& state, native_string_view name) {
	auto dir = simple_fs::get_or_create_scenario_directory();
	auto save_file = open_file(dir, name);
	if(save_file) {
		scenario_header header;
		header.version = 0;

		auto contents = simple_fs::view_contents(*save_file);
		uint8_t const* buffer_pos = reinterpret_cast<uint8_t const*>(contents.data);
		auto file_end = buffer_pos + contents.file_size;

		if(contents.file_size > sizeof_scenario_header(header)) {
			buffer_pos = read_scenario_header(buffer_pos, header);
		}

		if(!readable_scenario_version(header.version)) {
			return false;
		}

		if(!state.scenario_checksum.is_equal(header.checksum))
			return false;

		state.loaded_save_file = NATIVE("");

		buffer_pos = load_mod_path(buffer_pos, state);

		buffer_pos = with_decompressed_section(buffer_pos,
			[&](uint8_t const* ptr_in, uint32_t length) {
				// DO NOTHING -- this skips over reading the scenario section
			});
		buffer_pos = with_decompressed_section(buffer_pos,
			[&](uint8_t const* ptr_in, uint32_t length) {
				read_save_section(ptr_in, ptr_in + length, state);
			});
		if(!canonical_runtime_loaded(state)) return false;

		state.game_seed = uint32_t(std::random_device()());


		// only load gamerule settings if host or singleplayer. A client would have to load the host' settings anyway
		if(state.network_mode == sys::network_mode_type::host || state.network_mode == sys::network_mode_type::single_player) {
			state.load_gamerule_settings();
		}


		return canonical_runtime_loaded(state);
	} else {
		return false;
	}
}

std::string make_time_string(uint64_t value) {
	std::string result;
	for(int32_t i = 64 / 4; i --> 0; ) {
		result += char('a' + (0x0F & (value >> (i * 4))));
	}
	return result;
}

std::string get_default_save_name(sys::state& state, save_type type) {
	auto ymd_date = state.current_date.to_ymd(state.start_date);
	if(type == sys::save_type::autosave) {
		return "autosave_" + std::to_string(state.autosave_counter) + ".bin";
	}
	else if(type == sys::save_type::bookmark) {
		return  "bookmark_" + make_time_string(uint64_t(std::time(nullptr))) + "-" + std::to_string(ymd_date.year) + "-" + std::to_string(ymd_date.month) + "-" + std::to_string(ymd_date.day) + ".bin";
	}
	else {
		auto tag = state.world.nation_get_identity_from_identity_holder(state.local_player_nation);
		return make_time_string(uint64_t(std::time(nullptr))) + "-" + nations::int_to_tag(state.world.national_identity_get_identifying_int(tag)) + "-" + std::to_string(ymd_date.year) + "-" + std::to_string(ymd_date.month) + "-" + std::to_string(ymd_date.day) + ".bin";
	}
}

void write_save_file(sys::state& state, save_type type, std::string const& name, const std::string& file_name) {
	save_header header;
	header.count = state.scenario_counter;
	//header.timestamp = state.scenario_time_stamp;
	auto time_stamp = std::time(nullptr);
	header.timestamp = int64_t(time_stamp);
	header.checksum = state.scenario_checksum;
	header.tag = state.world.nation_get_identity_from_identity_holder(state.local_player_nation);
	header.cgov = state.world.nation_get_government_type(state.local_player_nation);
	header.d = state.current_date;

	auto default_save_name = get_default_save_name(state, type);

	if(!name.empty()) {
		memcpy(header.save_name, name.c_str(), std::min(name.length(), size_t(63)));
		if(name.length() < 63) {
			header.save_name[name.length()] = 0;
		} else {
			header.save_name[63] = 0;
		}
	}
	else {
		memcpy(header.save_name, default_save_name.c_str(), std::min(default_save_name.length(), size_t(63)));
		if(default_save_name.length() < 63) {
			header.save_name[default_save_name.length()] = 0;
		} else {
			header.save_name[63] = 0;
		}
	}


	size_t save_space = sizeof_save_section(state);

	// this is an upper bound, since compacting the data may require less space
	size_t total_size = sizeof_save_header(header) + ZSTD_compressBound(save_space) + sizeof(uint32_t) * 2;

	uint8_t* temp_buffer = new uint8_t[total_size];
	uint8_t* buffer_position = temp_buffer;

	buffer_position = write_save_header(buffer_position, header);

	uint8_t* temp_save_buffer = new uint8_t[save_space];
	write_save_section(temp_save_buffer, state);

	buffer_position = write_compressed_section(buffer_position, temp_save_buffer, uint32_t(save_space));
	delete[] temp_save_buffer;

	auto total_size_used = buffer_position - temp_buffer;

	auto sdir = simple_fs::get_or_create_save_game_directory(state.mod_save_dir);

	if(type == sys::save_type::autosave) {
		simple_fs::write_file(sdir, simple_fs::utf8_to_native(default_save_name), reinterpret_cast<char*>(temp_buffer), uint32_t(total_size_used));
		state.autosave_counter = (state.autosave_counter + 1) % sys::max_autosaves;
	} else if(type == sys::save_type::bookmark) {
		simple_fs::write_file(sdir, simple_fs::utf8_to_native( default_save_name), reinterpret_cast<char*>(temp_buffer), uint32_t(total_size_used));
	} else {
		if(!file_name.empty()) {
			auto base_str = file_name + ".bin";
			simple_fs::write_file(sdir, simple_fs::utf8_to_native(base_str), reinterpret_cast<char*>(temp_buffer), uint32_t(total_size_used));
		}
		else {
			simple_fs::write_file(sdir, simple_fs::utf8_to_native(default_save_name), reinterpret_cast<char*>(temp_buffer), uint32_t(total_size_used));
		}
	}
	delete[] temp_buffer;

	state.save_list_updated.store(true, std::memory_order::release); // update for ui

	/*
	// log count of pressed wargoals
	// can be used as a simple measure of how well AI expands during tests of AI changes
	{
		auto data_dumps_directory = simple_fs::get_or_create_data_dumps_directory();
		auto data = (std::to_string(state.pressed_wargoals) + "\n");
		simple_fs::append_file(
			data_dumps_directory,
			NATIVE("diplomacy_stats.txt"),
			data.c_str(),
			uint32_t(data.size())
		);
	}
	*/


	if(state.cheat_data.ecodump) {
		auto data_dumps_directory = simple_fs::get_or_create_data_dumps_directory();

		simple_fs::append_file(
			data_dumps_directory,
			NATIVE("economy_dump.txt"),
			state.cheat_data.national_economy_dump_buffer.c_str(),
			uint32_t(state.cheat_data.national_economy_dump_buffer.size())
		);
		state.cheat_data.national_economy_dump_buffer.clear();
		simple_fs::append_file(
			data_dumps_directory,
			NATIVE("savings_dump.txt"),
			state.cheat_data.savings_buffer.c_str(),
			uint32_t(state.cheat_data.savings_buffer.size())
		);
		state.cheat_data.savings_buffer.clear();
		simple_fs::append_file(
			data_dumps_directory,
			NATIVE("prices_dump.txt"),
			state.cheat_data.prices_dump_buffer.c_str(),
			uint32_t(state.cheat_data.prices_dump_buffer.size())
		);
		state.cheat_data.prices_dump_buffer.clear();
		simple_fs::append_file(
			data_dumps_directory,
			NATIVE("demand_dump.txt"),
			state.cheat_data.demand_dump_buffer.c_str(),
			uint32_t(state.cheat_data.demand_dump_buffer.size())
		);
		state.cheat_data.demand_dump_buffer.clear();
		simple_fs::append_file(
			data_dumps_directory,
			NATIVE("supply_dump.txt"),
			state.cheat_data.supply_dump_buffer.c_str(),
			uint32_t(state.cheat_data.supply_dump_buffer.size())
		);
		state.cheat_data.supply_dump_buffer.clear();
	}
}
bool try_read_save_file(sys::state& state, native_string_view name, bool ignore_checksum) {
	auto dir = simple_fs::get_or_create_save_game_directory(state.mod_save_dir);
	auto save_file = open_file(dir, name);
	if(save_file) {
		save_header header;
		header.version = 0;

		auto contents = simple_fs::view_contents(*save_file);
		uint8_t const* buffer_pos = reinterpret_cast<uint8_t const*>(contents.data);
		auto file_end = buffer_pos + contents.file_size;

		if(contents.file_size > sizeof_save_header(header)) {
			buffer_pos = read_save_header(buffer_pos, header);
		}

		if(!readable_save_version(header.version)) {
			return false;
		}

		//if(state.scenario_counter != header.count)
		//	return false;
		//if(state.scenario_time_stamp != header.timestamp)
		//	return false;
		// 
		// check the checksum if we dont want to ignore it, and refuse to load if it mismatches
		if(!ignore_checksum) {
			if(!state.scenario_checksum.is_equal(header.checksum))
				return false;
		}
		

		state.loaded_save_file = name;

			buffer_pos = with_decompressed_section(buffer_pos,
				[&](uint8_t const* ptr_in, uint32_t length) { read_save_section(ptr_in, ptr_in + length, state); });

		return canonical_runtime_loaded(state);
	} else {
		return false;
	}
}

} // namespace sys
