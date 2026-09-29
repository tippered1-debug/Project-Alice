#include "land_forces.hpp"

#include "military.hpp"
#include "persons/exact_population.hpp"
#include "system_state.hpp"
#include "world/spatial_runtime.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <set>
#include <tuple>

namespace military {

struct land_force_store {
	std::vector<land_forces::equipment_model> equipment_models;
	std::vector<land_forces::formation_template> templates;
	std::vector<land_forces::template_equipment_authorization> template_equipment;
	std::vector<land_forces::template_consumable_requirement> template_consumables;
	std::vector<land_forces::formation> formations;
	std::vector<land_forces::equipment_holding> equipment;
	std::vector<land_forces::formation_consumable_inventory> consumables;
	std::vector<land_forces::stockpile> stockpiles;
	std::vector<land_forces::physical_shipment> shipments;
	std::vector<land_forces::casualty_person_record> person_losses;
	std::vector<land_forces::casualty_equipment_record> equipment_losses;
	std::vector<land_forces::casualty_event_record> casualty_events;
};

} // namespace military

namespace military::land_forces {
namespace {

uint64_t mix(uint64_t value) noexcept {
	value ^= value >> 30;
	value *= 0xbf58476d1ce4e5b9ULL;
	value ^= value >> 27;
	value *= 0x94d049bb133111ebULL;
	return value ^ (value >> 31);
}

uint64_t hash_combine(uint64_t hash, uint64_t value) noexcept {
	return mix(hash ^ (value + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2)));
}

uint64_t hash_float(uint64_t hash, float value) noexcept {
	return hash_combine(hash, std::bit_cast<uint32_t>(value));
}

uint64_t hash_double(uint64_t hash, double value) noexcept {
	return hash_combine(hash, std::bit_cast<uint64_t>(value));
}

std::shared_ptr<land_force_store> ensure(sys::state& state) {
	assert(state.land_forces && "canonical land forces must be initialized before use");
	if(!state.land_forces) std::abort();
	return state.land_forces;
}

std::shared_ptr<land_force_store const> ensure(sys::state const& state) {
	assert(state.land_forces && "canonical land forces must be initialized before lookup");
	if(!state.land_forces) std::abort();
	return state.land_forces;
}

template<typename T, typename Id, typename GetId>
T const* find_by_id(std::vector<T> const& values, Id id, GetId get_id) {
	auto found = std::lower_bound(values.begin(), values.end(), id,
		[&](T const& value, Id target) { return get_id(value) < target; });
	return found != values.end() && get_id(*found) == id ? &*found : nullptr;
}

template<typename T, typename Id, typename GetId>
T* find_by_id(std::vector<T>& values, Id id, GetId get_id) {
	auto found = std::lower_bound(values.begin(), values.end(), id,
		[&](T const& value, Id target) { return get_id(value) < target; });
	return found != values.end() && get_id(*found) == id ? &*found : nullptr;
}

template<typename T, typename Id, typename GetId>
bool insert_by_id(std::vector<T>& values, T const& value, Id id, GetId get_id) {
	auto found = std::lower_bound(values.begin(), values.end(), id,
		[&](T const& existing, Id target) { return get_id(existing) < target; });
	if(found != values.end() && get_id(*found) == id) return false;
	values.insert(found, value);
	return true;
}

bool stable_id_in_use(sys::state const& state, stable_id id) {
	if(id == 0 || !state.land_forces) return id == 0;
	auto const& store = *state.land_forces;
	return find_by_id(store.equipment_models, id, [](auto const& item) { return item.id; })
		|| find_by_id(store.templates, id, [](auto const& item) { return item.id; })
		|| find_by_id(store.formations, id, [](auto const& item) { return item.id; })
		|| find_by_id(store.stockpiles, id, [](auto const& item) { return item.id; })
		|| find_by_id(store.shipments, id, [](auto const& item) { return item.id; })
		|| std::any_of(store.casualty_events.begin(), store.casualty_events.end(),
			[&](auto const& item) { return item.event_id == id; });
}

bool valid_site(sys::state const& state, dcon::site_id site) {
	if(!site || !state.world.site_is_valid(site)) return false;
	auto province = state.world.site_get_province_from_site_location(site);
	return province && state.world.province_is_valid(province);
}

formation_template const* find_template(sys::state const& state, stable_id id) {
	return find_by_id(ensure(state)->templates, id, [](auto const& value) { return value.id; });
}

equipment_holding* mutable_holding(sys::state& state, stable_id formation_id, stable_id model_id) {
	auto store = ensure(state);
	auto it = std::lower_bound(store->equipment.begin(), store->equipment.end(),
		std::pair{formation_id, model_id}, [](equipment_holding const& value, auto const& target) {
			return std::pair{value.formation_id, value.equipment_model_id} < target;
		});
	if(it != store->equipment.end() && it->formation_id == formation_id && it->equipment_model_id == model_id)
		return &*it;
	return nullptr;
}

formation_consumable_inventory* mutable_consumable(sys::state& state, stable_id formation_id,
	consumable_kind kind) {
	auto store = ensure(state);
	auto key = std::pair{formation_id, uint8_t(kind)};
	auto it = std::lower_bound(store->consumables.begin(), store->consumables.end(), key,
		[](formation_consumable_inventory const& value, auto const& target) {
			return std::pair{value.formation_id, uint8_t(value.kind)} < target;
		});
	if(it != store->consumables.end() && it->formation_id == formation_id && it->kind == kind) return &*it;
	return nullptr;
}

bool assigned(sys::state const& state, persons::person_key key) {
	return persons::exact_population::has_military_assignment(state, key);
}

std::vector<personnel_assignment_range> ranges_for_formation(sys::state const& state,
	stable_id formation_id) {
	std::vector<personnel_assignment_range> result;
	for(auto const& range : persons::exact_population::military_assignments_for_formation(state, formation_id))
		result.push_back({range.formation_id, range.source_population_cell, range.first_ordinal,
			range.count, range.ordinal_stride, range.training_days_remaining, 0});
	std::sort(result.begin(), result.end(), [](auto const& left, auto const& right) {
		return std::tie(left.source_population_cell, left.first_ordinal, left.ordinal_stride)
			< std::tie(right.source_population_cell, right.first_ordinal, right.ordinal_stride);
	});
	return result;
}

uint64_t template_equipment_count(sys::state const& state, stable_id template_id,
	stable_id equipment_model_id) {
	uint64_t result = 0;
	for(auto const& authorization : ensure(state)->template_equipment)
		if(authorization.template_id == template_id && authorization.equipment_model_id == equipment_model_id)
			result = authorization.quantity;
	return result;
}

bool has_template_model(sys::state const& state, stable_id template_id, stable_id equipment_model_id) {
	for(auto const& authorization : ensure(state)->template_equipment)
		if(authorization.template_id == template_id && authorization.equipment_model_id == equipment_model_id) return true;
	return false;
}

uint64_t equipment_count_in_state(sys::state const& state, stable_id formation_id,
	stable_id equipment_model_id) {
	for(auto const& holding : ensure(state)->equipment)
		if(holding.formation_id == formation_id && holding.equipment_model_id == equipment_model_id)
			return holding.quantity;
	return 0;
}

void add_equipment(sys::state& state, stable_id formation_id, stable_id model_id, uint64_t quantity) {
	auto store = ensure(state);
	if(auto existing = mutable_holding(state, formation_id, model_id)) existing->quantity += quantity;
	else {
		store->equipment.push_back({formation_id, model_id, quantity});
		std::sort(store->equipment.begin(), store->equipment.end(), [](auto const& left, auto const& right) {
			return std::tie(left.formation_id, left.equipment_model_id)
				< std::tie(right.formation_id, right.equipment_model_id);
		});
	}
}

void add_consumable(sys::state& state, stable_id formation_id, consumable_kind kind, double quantity) {
	auto store = ensure(state);
	if(auto existing = mutable_consumable(state, formation_id, kind)) existing->quantity += quantity;
	else {
		store->consumables.push_back({formation_id, kind, {}, quantity});
		std::sort(store->consumables.begin(), store->consumables.end(), [](auto const& left, auto const& right) {
			return std::pair{left.formation_id, uint8_t(left.kind)}
				< std::pair{right.formation_id, uint8_t(right.kind)};
		});
	}
}

persons::person_key person_at_offset(
	std::vector<personnel_assignment_range> const& ranges, uint64_t offset) {
	for(auto const& range : ranges) {
		if(offset < range.count)
			return {range.source_population_cell,
				range.first_ordinal + offset * uint64_t(range.ordinal_stride)};
		offset -= range.count;
	}
	return {};
}

bool transfer_route_is_valid(sys::state& state, dcon::nation_id owner,
	dcon::site_id origin, dcon::site_id destination, double cargo_weight,
	army_supply_access_data* route_data = nullptr) {
	if(!valid_site(state, origin) || !valid_site(state, destination)
		|| !std::isfinite(cargo_weight) || cargo_weight < 0.0) return false;
	auto const source_province = state.world.site_get_province_from_site_location(origin);
	auto const destination_province = state.world.site_get_province_from_site_location(destination);
	if(source_province == destination_province) {
		if(state.world.province_get_nation_from_province_ownership(source_province) != owner
			|| state.world.province_get_nation_from_province_control(source_province) != owner) return false;
		if(route_data) *route_data = army_supply_access_data{};
		return true;
	}
	auto access = military::calculate_army_supply_access_from_source(state, owner,
		source_province, destination_province);
	if(route_data) *route_data = access;
	return access.reachable && access.source == source_province
		&& std::isfinite(access.route_capacity) && access.route_capacity > 0.0f
		&& cargo_weight <= double(access.route_capacity);
}

uint16_t transit_days(army_supply_access_data const& route, dcon::site_id origin,
	dcon::site_id destination, sys::state const& state) {
	if(state.world.site_get_province_from_site_location(origin)
		== state.world.site_get_province_from_site_location(destination)) return 0;
	auto const km = std::max(1.0f, route.distance_km);
	auto const days = std::ceil(km / 100.0f);
	return uint16_t(std::clamp(days, 1.0f, float(std::numeric_limits<uint16_t>::max())));
}

double cargo_mass(sys::state const& state, stockpile const& pile, double quantity) {
	if(pile.cargo == cargo_kind::consumable) return quantity;
	auto model = find_equipment_model(state, pile.equipment_model_id);
	return model ? double(model->mass) * quantity : std::numeric_limits<double>::infinity();
}

double formation_daily_demand(sys::state const& state, formation const& unit,
	consumable_kind kind) {
	double demand = 0.0;
	double equipment_tonnes = 0.0;
	for(auto const& holding : ensure(state)->equipment) {
		if(holding.formation_id != unit.id) continue;
		auto model = find_equipment_model(state, holding.equipment_model_id);
		if(model) equipment_tonnes += double(model->mass) * double(holding.quantity);
	}
	for(auto const& requirement : ensure(state)->template_consumables) {
		if(requirement.template_id != unit.template_id || requirement.kind != kind) continue;
		demand += double(personnel_count(state, unit.id)) * requirement.per_person
			+ (equipment_tonnes / 1000.0) * requirement.per_equipment_tonne;
	}
	auto tempo_factor = 0.25 + 0.75 * std::clamp(double(unit.operational_tempo), 0.0, 1.0);
	if(unit.moving) tempo_factor *= 1.25;
	if(unit.in_combat) tempo_factor *= 1.5;
	return demand * tempo_factor;
}

} // namespace

void initialize_empty_store(sys::state& state) {
	assert(!state.land_forces && "canonical land forces initialized more than once");
	if(state.land_forces) std::abort();
	state.land_forces = std::make_shared<land_force_store>();
}

bool initialized(sys::state const& state) { return bool(state.land_forces); }

bool add_equipment_model(sys::state& state, equipment_model const& model) {
	if(stable_id_in_use(state, model.id) || !std::isfinite(model.mass) || model.mass <= 0.0f
		|| !std::isfinite(model.reliability) || model.reliability < 0.0f || model.reliability > 1.0f
		|| !std::isfinite(model.attack) || model.attack < 0.0f
		|| !std::isfinite(model.defense) || model.defense < 0.0f
		|| !std::isfinite(model.range_km) || model.range_km < 0.0f) return false;
	return insert_by_id(ensure(state)->equipment_models, model, model.id,
		[](auto const& value) { return value.id; });
}

bool add_template(sys::state& state, formation_template const& value) {
	if(stable_id_in_use(state, value.id) || value.personnel_authorization == 0) return false;
	return insert_by_id(ensure(state)->templates, value, value.id,
		[](auto const& item) { return item.id; });
}

bool authorize_template_equipment(sys::state& state, template_equipment_authorization const& value) {
	if(value.template_id == 0 || value.equipment_model_id == 0 || value.quantity == 0
		|| !find_template(state, value.template_id)
		|| !find_equipment_model(state, value.equipment_model_id)) return false;
	auto store = ensure(state);
	auto key = std::pair{value.template_id, value.equipment_model_id};
	auto it = std::lower_bound(store->template_equipment.begin(), store->template_equipment.end(), key,
		[](auto const& current, auto const& target) {
			return std::pair{current.template_id, current.equipment_model_id} < target;
		});
	if(it != store->template_equipment.end() && it->template_id == value.template_id
		&& it->equipment_model_id == value.equipment_model_id) return false;
	store->template_equipment.insert(it, value);
	return true;
}

bool set_template_consumable_requirement(sys::state& state,
	template_consumable_requirement const& value) {
	if(value.template_id == 0 || uint8_t(value.kind) > uint8_t(consumable_kind::ammunition)
		|| !find_template(state, value.template_id) || !std::isfinite(value.per_person)
		|| value.per_person < 0.0 || !std::isfinite(value.per_equipment_tonne)
		|| value.per_equipment_tonne < 0.0 || (value.per_person == 0.0 && value.per_equipment_tonne == 0.0)) return false;
	auto store = ensure(state);
	auto key = std::pair{value.template_id, uint8_t(value.kind)};
	auto it = std::lower_bound(store->template_consumables.begin(), store->template_consumables.end(), key,
		[](auto const& current, auto const& target) {
			return std::pair{current.template_id, uint8_t(current.kind)} < target;
		});
	if(it != store->template_consumables.end() && it->template_id == value.template_id
		&& it->kind == value.kind) return false;
	else store->template_consumables.insert(it, value);
	return true;
}

bool create_formation(sys::state& state, formation const& value) {
	if(stable_id_in_use(state, value.id) || value.template_id == 0 || !state.world.nation_is_valid(value.owner)
		|| !valid_site(state, value.location) || uint8_t(value.status) > uint8_t(formation_status::destroyed)
		|| !std::isfinite(value.operational_tempo) || value.operational_tempo < 0.0f
		|| value.operational_tempo > 1.0f || !find_template(state, value.template_id)) return false;
	if(value.parent_id != 0) {
		auto parent = find_formation(state, value.parent_id);
		if(!parent || parent->owner != value.owner || parent->id == value.id) return false;
	}
	if(find_formation(state, value.id)) return false;
	auto created = value;
	created.personnel_authorization = find_template(state, value.template_id)->personnel_authorization;
	return insert_by_id(ensure(state)->formations, created, created.id,
		[](auto const& item) { return item.id; });
}

bool create_stockpile(sys::state& state, stockpile const& value) {
	if(stable_id_in_use(state, value.id) || !state.world.nation_is_valid(value.owner) || !valid_site(state, value.location)
		|| uint8_t(value.kind) > uint8_t(stockpile_kind::depot)
		|| uint8_t(value.cargo) > uint8_t(cargo_kind::consumable)
		|| !std::isfinite(value.quantity) || value.quantity < 0.0) return false;
	if(value.cargo == cargo_kind::equipment) {
		if(value.equipment_model_id == 0 || !find_equipment_model(state, value.equipment_model_id)
			|| std::floor(value.quantity) != value.quantity) return false;
	} else if(uint8_t(value.consumable) > uint8_t(consumable_kind::ammunition)
		|| value.equipment_model_id != 0) return false;
	return insert_by_id(ensure(state)->stockpiles, value, value.id,
		[](auto const& item) { return item.id; });
}

bool set_initial_equipment_holding(sys::state& state, stable_id formation_id,
	stable_id equipment_model_id, uint64_t quantity) {
	auto unit = find_formation(state, formation_id);
	if(!unit || !find_equipment_model(state, equipment_model_id)
		|| !has_template_model(state, unit->template_id, equipment_model_id)
		|| quantity > equipment_authorization(state, formation_id, equipment_model_id)
		|| mutable_holding(state, formation_id, equipment_model_id)) return false;
	add_equipment(state, formation_id, equipment_model_id, quantity);
	return true;
}

bool set_initial_consumable_inventory(sys::state& state, stable_id formation_id,
	consumable_kind kind, double quantity) {
	if(!find_formation(state, formation_id) || uint8_t(kind) > uint8_t(consumable_kind::ammunition)
		|| !std::isfinite(quantity) || quantity < 0.0
		|| mutable_consumable(state, formation_id, kind)) return false;
	add_consumable(state, formation_id, kind, quantity);
	return true;
}

formation const* find_formation(sys::state const& state, stable_id id) {
	if(!state.land_forces || id == 0) return nullptr;
	return find_by_id(state.land_forces->formations, id, [](auto const& value) { return value.id; });
}

equipment_model const* find_equipment_model(sys::state const& state, stable_id id) {
	if(!state.land_forces || id == 0) return nullptr;
	return find_by_id(state.land_forces->equipment_models, id, [](auto const& value) { return value.id; });
}

uint64_t personnel_count(sys::state const& state, stable_id formation_id) {
	return persons::exact_population::military_personnel_count(state, formation_id);
}

namespace {
uint64_t trained_personnel_count(sys::state const& state, stable_id formation_id) {
	uint64_t result = 0;
	for(auto const& assignment : persons::exact_population::military_assignments_for_formation(state, formation_id))
		if(assignment.training_days_remaining == 0) result += assignment.count;
	return result;
}
}

uint64_t equipment_count(sys::state const& state, stable_id formation_id, stable_id equipment_model_id) {
	return equipment_count_in_state(state, formation_id, equipment_model_id);
}

uint64_t equipment_authorization(sys::state const& state, stable_id formation_id,
	stable_id equipment_model_id) {
	auto unit = find_formation(state, formation_id);
	return unit ? template_equipment_count(state, unit->template_id, equipment_model_id) : 0;
}

double consumable_quantity(sys::state const& state, stable_id formation_id, consumable_kind kind) {
	for(auto const& item : ensure(state)->consumables)
		if(item.formation_id == formation_id && item.kind == kind) return item.quantity;
	return 0.0;
}

readiness derive_readiness(sys::state const& state, stable_id formation_id) {
	readiness result;
	auto unit = find_formation(state, formation_id);
	if(!unit || unit->status == formation_status::destroyed) return result;
	auto const personnel_auth = unit->personnel_authorization;
	result.personnel_fill = personnel_auth == 0 ? 0.0f : std::clamp(
		float(trained_personnel_count(state, formation_id)) / float(personnel_auth), 0.0f, 1.0f);
	double actual_weight = 0.0;
	double authorized_weight = 0.0;
	for(auto const& authorization : ensure(state)->template_equipment) {
		if(authorization.template_id != unit->template_id) continue;
		auto model = find_equipment_model(state, authorization.equipment_model_id);
		if(!model) continue;
		authorized_weight += double(model->mass) * double(authorization.quantity);
		actual_weight += double(model->mass) * double(equipment_count(state,
			formation_id, authorization.equipment_model_id));
	}
	result.equipment_fill = authorized_weight <= 0.0 ? 1.0f : std::clamp(
		float(actual_weight / authorized_weight), 0.0f, 1.0f);
	double days = std::numeric_limits<double>::infinity();
	bool has_demand = false;
	for(auto const& requirement : ensure(state)->template_consumables) {
		if(requirement.template_id != unit->template_id) continue;
		auto demand = formation_daily_demand(state, *unit, requirement.kind);
		if(demand <= 0.0) continue;
		has_demand = true;
		days = std::min(days, consumable_quantity(state, formation_id, requirement.kind) / demand);
	}
	if(!has_demand) days = 3650.0;
	result.supply_days = float(std::min(days, 3650.0));
	auto supply_ratio = std::clamp(result.supply_days / 3.0f, 0.0f, 1.0f);
	result.operational_readiness = std::clamp(result.personnel_fill * result.equipment_fill
		* supply_ratio, 0.0f, 1.0f);
	return result;
}

bool assign_personnel(sys::state& state, stable_id formation_id,
	std::span<persons::person_key const> people, uint16_t training_days) {
	auto unit = find_formation(state, formation_id);
	if(!unit || unit->status == formation_status::destroyed || people.empty()
		|| people.size() > unit->personnel_authorization - std::min<uint64_t>(
			personnel_count(state, formation_id), unit->personnel_authorization)) return false;
	std::vector<persons::person_key> ordered(people.begin(), people.end());
	std::sort(ordered.begin(), ordered.end(), [](auto left, auto right) {
		return std::tie(left.source_population_cell, left.ordinal)
			< std::tie(right.source_population_cell, right.ordinal);
	});
	std::map<uint32_t, std::pair<dcon::site_id, uint64_t>> source_loads;
	for(size_t i = 0; i < ordered.size(); ++i) {
		auto key = ordered[i];
		if(key.source_population_cell == 0 || !persons::exists(state, key)
			|| !persons::alive(state, key) || assigned(state, key)
			|| (i != 0 && ordered[i - 1] == key)) return false;
		auto pop = persons::current_population(state, key);
		auto province = pop ? state.world.pop_get_province_from_pop_location(pop) : dcon::province_id{};
		auto owner = province ? state.world.province_get_nation_from_province_ownership(province) : dcon::nation_id{};
		auto site = province ? world::spatial_runtime::site_for_province(state, province) : dcon::site_id{};
		if(owner != unit->owner || !site) return false;
		auto& load = source_loads[uint32_t(site.index())];
		load.first = site;
		++load.second;
	}
	for(auto const& [index, source] : source_loads)
		if(!personnel_route_is_valid(state, unit->owner, source.first, unit->location, source.second)) return false;
	for(size_t i = 0; i < ordered.size();) {
		auto const source = ordered[i].source_population_cell;
		auto first = ordered[i].ordinal;
		auto count = uint64_t(1);
		while(i + count < ordered.size() && ordered[i + count].source_population_cell == source
			&& ordered[i + count].ordinal == first + count) ++count;
		if(!persons::exact_population::assign_military_range(state,
			{source, 1, first, count, formation_id, training_days, 0})) return false;
		i += size_t(count);
	}
	return true;
}

uint64_t recruit_personnel(sys::state& state, stable_id formation_id, dcon::pop_id source_pop,
	uint64_t requested, uint16_t training_days) {
	if(requested == 0 || !source_pop || !state.world.pop_is_valid(source_pop)
		|| !state.exact_population) return 0;
	auto unit = find_formation(state, formation_id);
	if(!unit || unit->status == formation_status::destroyed) return 0;
	requested = std::min<uint64_t>(requested,
		unit->personnel_authorization > personnel_count(state, formation_id)
			? unit->personnel_authorization - personnel_count(state, formation_id) : 0);
	if(requested == 0) return 0;
	auto province = state.world.pop_get_province_from_pop_location(source_pop);
	if(!province || state.world.province_get_nation_from_province_ownership(province) != unit->owner) return 0;
	auto source_site = world::spatial_runtime::site_for_province(state, province);
	if(!source_site || !personnel_route_is_valid(state, unit->owner, source_site, unit->location, requested)) {
		auto const destination = state.world.site_get_province_from_site_location(unit->location);
		if(!source_site || !destination || province == destination) return 0;
		auto route = military::calculate_army_supply_access_from_source(state, unit->owner, province, destination);
		if(!route.reachable || route.source != province || !std::isfinite(route.route_capacity)
			|| route.route_capacity <= 0.0f) return 0;
		requested = std::min<uint64_t>(requested,
			uint64_t(std::floor(double(route.route_capacity) * 1000.0)));
		if(requested == 0) return 0;
	}
	auto const source_cell = persons::exact_population::source_cell_for_population(state, source_pop);
	auto descriptor = persons::exact_population::descriptor_for_cell(state, source_cell);
	if(source_cell == 0 || !descriptor) return 0;
	std::vector<personnel_assignment_range> recruited;
	uint64_t actual = 0;
	uint64_t run_first = 0;
	uint64_t run_count = 0;
	auto flush = [&] {
		if(run_count != 0) recruited.push_back({formation_id, source_cell, run_first,
			run_count, 4, training_days, 0});
		run_count = 0;
	};
	for(uint64_t ordinal = 0; ordinal < descriptor->literal_count && actual < requested; ordinal += 4) {
		persons::person_key key{source_cell, ordinal};
		bool eligible = persons::alive(state, key)
			&& persons::current_population_cell(state, key) == source_cell
			&& persons::age_years(state, key, state.current_date) >= 18
			&& persons::age_years(state, key, state.current_date) < 65
			&& !assigned(state, key);
		if(!eligible) {
			flush();
			continue;
		}
		if(run_count == 0) run_first = ordinal;
		if(run_count != 0 && run_first + run_count * 4 != ordinal) flush(), run_first = ordinal;
		++run_count;
		++actual;
	}
	flush();
	if(actual == 0) return 0;
	for(auto const& range : recruited)
		if(!persons::exact_population::assign_military_range(state,
			{range.source_population_cell, range.ordinal_stride, range.first_ordinal,
				range.count, range.formation_id, range.training_days_remaining, 0})) return 0;
	return actual;
}

uint64_t demobilize(sys::state& state, stable_id formation_id) {
	return persons::exact_population::unassign_all_military_personnel(state, formation_id);
}

bool close_person_assignment_on_death(sys::state& state, persons::person_key person) {
	uint64_t formation_id = 0;
	if(!persons::exact_population::has_military_assignment(state, person, &formation_id)) return true;
	return persons::exact_population::unassign_military_person(state, person, formation_id);
}

void advance_training(sys::state& state) {
	persons::exact_population::advance_military_training(state);
	advance_shipments(state);
}

void update_daily(sys::state& state) {
	advance_training(state);
	std::vector<std::pair<stable_id, consumable_kind>> needs;
	for(auto const& unit : ensure(state)->formations) {
		if(unit.status == formation_status::destroyed) continue;
		for(auto const& requirement : ensure(state)->template_consumables)
			if(requirement.template_id == unit.template_id)
				needs.emplace_back(unit.id, requirement.kind);
	}
	for(auto const& [formation_id, kind] : needs)
		(void)consume_daily_supply(state, formation_id, kind);
}

bool move_formation(sys::state& state, stable_id formation_id, dcon::site_id destination) {
	auto unit = find_formation(state, formation_id);
	if(!unit || unit->status == formation_status::destroyed || !valid_site(state, destination)) return false;
	auto origin_province = state.world.site_get_province_from_site_location(unit->location);
	auto destination_province = state.world.site_get_province_from_site_location(destination);
	if(origin_province != destination_province
		&& !world::spatial_runtime::route_for_sites(state, unit->location, destination).connected) return false;
	for(auto const& shipment : ensure(state)->shipments)
		if(shipment.status == 0 && shipment.destination_formation_id == formation_id) return false;
	find_by_id(ensure(state)->formations, formation_id,
		[](auto const& value) { return value.id; })->location = destination;
	return true;
}

bool personnel_route_is_valid(sys::state& state, dcon::nation_id owner,
	dcon::site_id origin, dcon::site_id destination, uint64_t personnel) {
	return transfer_route_is_valid(state, owner, origin, destination,
		double(personnel) / 1000.0);
}

void sync_legacy_adapter_state(sys::state& state) {
	if(!state.land_forces) return;
	for(auto& unit : ensure(state)->formations) {
		if(unit.legacy_regiment_index_plus_one == 0) continue;
		auto index = unit.legacy_regiment_index_plus_one - 1u;
		if(index >= state.world.regiment_size()) continue;
		auto regiment = dcon::regiment_id{dcon::regiment_id::value_base_t(index)};
		if(!state.world.regiment_is_valid(regiment)) continue;
		auto army = state.world.regiment_get_army_from_army_membership(regiment);
		if(!army || !state.world.army_is_valid(army)) continue;
		auto province = state.world.army_get_location_from_army_location(army);
		auto site = province ? world::spatial_runtime::site_for_province(state, province) : dcon::site_id{};
		if(valid_site(state, site)) unit.location = site;
		unit.moving = uint8_t(state.world.army_get_path(army).size() != 0
			|| bool(state.world.army_get_arrival_time(army)));
		unit.in_combat = uint8_t(bool(state.world.army_get_battle_from_army_battle_participation(army)));
	}
}

bool map_legacy_regiment(sys::state& state, stable_id formation_id, dcon::regiment_id regiment) {
	auto unit = find_by_id(ensure(state)->formations, formation_id,
		[](auto const& value) { return value.id; });
	if(!unit || !regiment || !state.world.regiment_is_valid(regiment)
		|| unit->legacy_regiment_index_plus_one != 0) return false;
	for(auto const& other : ensure(state)->formations)
		if(other.legacy_regiment_index_plus_one == uint32_t(regiment.index()) + 1u) return false;
	unit->legacy_regiment_index_plus_one = uint32_t(regiment.index()) + 1u;
	return true;
}

bool clear_legacy_regiment_mapping(sys::state& state, dcon::regiment_id regiment) {
	if(!regiment || !state.land_forces) return false;
	auto key = uint32_t(regiment.index()) + 1u;
	for(auto& unit : ensure(state)->formations) {
		if(unit.legacy_regiment_index_plus_one != key) continue;
		unit.legacy_regiment_index_plus_one = 0;
		return true;
	}
	return false;
}

stable_id formation_for_legacy_regiment(sys::state const& state, dcon::regiment_id regiment) {
	if(!state.land_forces || !regiment) return 0;
	auto key = uint32_t(regiment.index()) + 1u;
	for(auto const& unit : state.land_forces->formations)
		if(unit.legacy_regiment_index_plus_one == key) return unit.id;
	return 0;
}

float projected_regiment_strength(sys::state const& state, stable_id formation_id) {
	return derive_readiness(state, formation_id).operational_readiness;
}

float formation_combat_stat(sys::state const& state, dcon::regiment_id regiment, bool attacking) {
	auto formation_id = formation_for_legacy_regiment(state, regiment);
	if(!formation_id) return 0.0f;
	double weighted = 0.0;
	double quantity = 0.0;
	for(auto const& holding : ensure(state)->equipment) {
		if(holding.formation_id != formation_id || holding.quantity == 0) continue;
		auto model = find_equipment_model(state, holding.equipment_model_id);
		if(!model) continue;
		auto stat = attacking ? model->attack : model->defense;
		weighted += double(stat) * double(holding.quantity);
		quantity += double(holding.quantity);
	}
	return quantity > 0.0 ? float(weighted / quantity) : 0.0f;
}

float apply_legacy_regiment_damage(sys::state& state, dcon::regiment_id regiment,
	float damage, stable_id event_id, int32_t day, persons::death_cause cause) {
	auto formation_id = formation_for_legacy_regiment(state, regiment);
	if(!formation_id || !std::isfinite(damage) || damage <= 0.0f) return 0.0f;
	auto const before = projected_regiment_strength(state, formation_id);
	auto const applied_damage = std::min(before, damage);
	if(applied_damage <= 0.0f) return 0.0f;
	auto const ratio = std::clamp(applied_damage / before, 0.0f, 1.0f);
	std::vector<casualty_request> equipment_losses;
	for(auto const& holding : ensure(state)->equipment) {
		if(holding.formation_id != formation_id || holding.quantity == 0) continue;
		auto loss = uint64_t(std::llround(double(holding.quantity) * double(ratio)));
		if(loss != 0) equipment_losses.push_back({holding.equipment_model_id, loss});
	}
	auto personnel = personnel_count(state, formation_id);
	auto personnel_losses = uint64_t(std::llround(double(personnel) * double(ratio)));
	if(ratio >= 1.0f) personnel_losses = personnel;
	auto result = apply_losses(state, formation_id, personnel_losses, equipment_losses, event_id, day, cause);
	if(!result.applied) return 0.0f;
	auto unit = find_formation(state, formation_id);
	if(personnel_count(state, formation_id) == 0 && unit && unit->status != formation_status::destroyed)
		(void)destroy_formation(state, formation_id);
	state.world.regiment_set_strength(regiment, projected_regiment_strength(state, formation_id));
	return applied_damage;
}

bool sync_legacy_regiment_projection(sys::state& state, dcon::regiment_id regiment) {
	auto formation_id = formation_for_legacy_regiment(state, regiment);
	if(!formation_id || !state.world.regiment_is_valid(regiment)) return false;
	state.world.regiment_set_strength(regiment, projected_regiment_strength(state, formation_id));
	return true;
}

bool set_operational_state(sys::state& state, stable_id formation_id, bool moving,
	bool in_combat, float operational_tempo) {
	auto unit = find_by_id(ensure(state)->formations, formation_id,
		[](auto const& value) { return value.id; });
	if(!unit || unit->status == formation_status::destroyed || !std::isfinite(operational_tempo)
		|| operational_tempo < 0.0f || operational_tempo > 1.0f) return false;
	unit->moving = uint8_t(moving);
	unit->in_combat = uint8_t(in_combat);
	unit->operational_tempo = operational_tempo;
	return true;
}

bool dispatch_to_stockpile(sys::state& state, stable_id shipment_id, stable_id source_stockpile_id,
	stable_id destination_stockpile_id, double quantity) {
	if(stable_id_in_use(state, shipment_id) || !std::isfinite(quantity) || quantity <= 0.0) return false;
	auto store = ensure(state);
	if(find_by_id(store->shipments, shipment_id, [](auto const& value) { return value.id; })) return false;
	auto source = find_by_id(store->stockpiles, source_stockpile_id, [](auto const& value) { return value.id; });
	auto destination = find_by_id(store->stockpiles, destination_stockpile_id, [](auto const& value) { return value.id; });
	if(!source || !destination || source->id == destination->id || source->owner != destination->owner
		|| destination->kind != stockpile_kind::depot || source->cargo != destination->cargo
		|| source->equipment_model_id != destination->equipment_model_id
		|| source->consumable != destination->consumable || source->quantity < quantity) return false;
	if(source->cargo == cargo_kind::equipment && std::floor(quantity) != quantity) return false;
	auto mass = cargo_mass(state, *source, quantity);
	army_supply_access_data route{};
	if(!transfer_route_is_valid(state, source->owner, source->location, destination->location, mass, &route)) return false;
	auto days = transit_days(route, source->location, destination->location, state);
	auto mutable_source = find_by_id(store->stockpiles, source_stockpile_id,
		[](auto const& value) { return value.id; });
	mutable_source->quantity -= quantity;
	store->shipments.push_back({shipment_id, source_stockpile_id, destination_stockpile_id,
		0, destination->location, source->cargo, source->consumable, 0,
		{}, source->equipment_model_id, quantity, days, 0});
	std::sort(store->shipments.begin(), store->shipments.end(), [](auto const& left, auto const& right) {
		return left.id < right.id;
	});
	if(days == 0) advance_shipments(state);
	return true;
}

bool dispatch_to_formation(sys::state& state, stable_id shipment_id, stable_id source_stockpile_id,
	stable_id destination_formation_id, double quantity) {
	if(stable_id_in_use(state, shipment_id) || !std::isfinite(quantity) || quantity <= 0.0) return false;
	auto store = ensure(state);
	if(find_by_id(store->shipments, shipment_id, [](auto const& value) { return value.id; })) return false;
	auto source = find_by_id(store->stockpiles, source_stockpile_id, [](auto const& value) { return value.id; });
	auto unit = find_formation(state, destination_formation_id);
	if(!source || !unit || source->kind != stockpile_kind::depot || source->owner != unit->owner
		|| source->quantity < quantity || unit->status == formation_status::destroyed) return false;
	if(source->cargo == cargo_kind::equipment
		&& !has_template_model(state, unit->template_id, source->equipment_model_id)) return false;
	if(source->cargo == cargo_kind::equipment
		&& (std::floor(quantity) != quantity
			|| equipment_count(state, unit->id, source->equipment_model_id) + uint64_t(quantity)
				> equipment_authorization(state, unit->id, source->equipment_model_id))) return false;
	auto mass = cargo_mass(state, *source, quantity);
	army_supply_access_data route{};
	if(!transfer_route_is_valid(state, source->owner, source->location, unit->location, mass, &route)) return false;
	auto days = transit_days(route, source->location, unit->location, state);
	auto mutable_source = find_by_id(store->stockpiles, source_stockpile_id,
		[](auto const& value) { return value.id; });
	mutable_source->quantity -= quantity;
	store->shipments.push_back({shipment_id, source_stockpile_id, 0,
		destination_formation_id, unit->location, source->cargo, source->consumable,
		0, {}, source->equipment_model_id, quantity, days, 0});
	std::sort(store->shipments.begin(), store->shipments.end(), [](auto const& left, auto const& right) {
		return left.id < right.id;
	});
	if(days == 0) advance_shipments(state);
	return true;
}

void advance_shipments(sys::state& state) {
	auto store = ensure(state);
	for(auto& shipment : store->shipments) {
		if(shipment.status != 0) continue;
		if(shipment.days_remaining != 0) --shipment.days_remaining;
		if(shipment.days_remaining != 0) continue;
		if(shipment.destination_stockpile_id != 0) {
			auto destination = find_by_id(store->stockpiles, shipment.destination_stockpile_id,
				[](auto const& value) { return value.id; });
			if(!destination) continue;
			destination->quantity += shipment.quantity;
		} else {
			auto unit = find_by_id(store->formations, shipment.destination_formation_id,
				[](auto const& value) { return value.id; });
			if(!unit || unit->status == formation_status::destroyed || unit->location != shipment.destination_site) continue;
			if(shipment.cargo == cargo_kind::equipment) {
			auto quantity = uint64_t(std::floor(shipment.quantity));
			if(double(quantity) != shipment.quantity) continue;
			add_equipment(state, unit->id, shipment.equipment_model_id, quantity);
			} else add_consumable(state, unit->id, shipment.consumable, shipment.quantity);
		}
		shipment.status = 1;
	}
}

double daily_consumable_demand(sys::state const& state, stable_id formation_id, consumable_kind kind) {
	auto unit = find_formation(state, formation_id);
	return unit ? formation_daily_demand(state, *unit, kind) : 0.0;
}

double logistics_demand(sys::state const& state, stable_id formation_id) {
	auto unit = find_formation(state, formation_id);
	if(!unit || unit->status == formation_status::destroyed) return 0.0;
	double equipment_mass = 0.0;
	for(auto const& holding : ensure(state)->equipment) {
		if(holding.formation_id != formation_id) continue;
		auto model = find_equipment_model(state, holding.equipment_model_id);
		if(model) equipment_mass += double(model->mass) * double(holding.quantity);
	}
	constexpr std::array<consumable_kind, 3> consumables = {
		consumable_kind::food, consumable_kind::fuel, consumable_kind::ammunition
	};
	double daily_consumables = 0.0;
	for(auto kind : consumables) daily_consumables += formation_daily_demand(state, *unit, kind);
	auto tempo = 0.25 + 0.75 * std::clamp(double(unit->operational_tempo), 0.0, 1.0);
	if(unit->moving) tempo *= 1.25;
	if(unit->in_combat) tempo *= 1.5;
	// Route demand is a stable logistics score built from actual people, held
	// equipment mass, and the template's authored daily consumables.
	auto personnel_load = double(personnel_count(state, formation_id)) / 1000.0;
	auto equipment_load = equipment_mass / 10.0;
	return std::max(0.25, (personnel_load + equipment_load) * tempo + daily_consumables);
}

double replacement_load(sys::state const& state, stable_id formation_id) {
	auto unit = find_formation(state, formation_id);
	if(!unit || unit->status == formation_status::destroyed) return 0.0;
	auto personnel_deficit = unit->personnel_authorization > personnel_count(state, formation_id)
		? unit->personnel_authorization - personnel_count(state, formation_id) : 0;
	double result = double(personnel_deficit) / 1000.0;
	for(auto const& authorization : ensure(state)->template_equipment) {
		if(authorization.template_id != unit->template_id) continue;
		auto current = equipment_count(state, formation_id, authorization.equipment_model_id);
		if(current >= authorization.quantity) continue;
		auto model = find_equipment_model(state, authorization.equipment_model_id);
		if(model) result += double(authorization.quantity - current) * double(model->mass) / 10.0;
	}
	return result;
}

double consume_daily_supply(sys::state& state, stable_id formation_id, consumable_kind kind) {
	auto unit = find_formation(state, formation_id);
	if(!unit || unit->status == formation_status::destroyed) return 0.0;
	auto inventory = mutable_consumable(state, formation_id, kind);
	if(!inventory) return 0.0;
	auto const demand = formation_daily_demand(state, *unit, kind);
	auto const consumed = std::min(inventory->quantity, demand);
	inventory->quantity -= consumed;
	return consumed;
}

double depot_inventory_at(sys::state const& state, dcon::nation_id owner, dcon::province_id province) {
	if(!state.land_forces || !province) return 0.0;
	double result = 0.0;
	for(auto const& pile : ensure(state)->stockpiles)
		if(pile.kind == stockpile_kind::depot && pile.owner == owner
			&& state.world.site_get_province_from_site_location(pile.location) == province)
			result += pile.quantity;
	return result;
}

stable_id next_loss_event_id(sys::state const& state, stable_id formation_id, int32_t day,
	persons::death_cause cause) {
	if(formation_id == 0 || uint8_t(cause) > uint8_t(persons::death_cause::attrition)) return 0;
	uint64_t sequence = 0;
	for(auto const& event : ensure(state)->casualty_events)
		if(event.formation_id == formation_id && event.day == day && event.cause == cause) ++sequence;
	auto seed = hash_combine(0x4c4f53534556454eULL, formation_id);
	seed = hash_combine(seed, uint32_t(day));
	seed = hash_combine(seed, uint8_t(cause));
	for(;; ++sequence) {
		auto candidate = hash_combine(seed, sequence);
		if(candidate != 0 && !stable_id_in_use(state, candidate)) return candidate;
	}
}

casualty_result apply_losses(sys::state& state, stable_id formation_id,
	uint64_t requested_personnel_losses, std::span<casualty_request const> requested_equipment_losses,
	stable_id event_id, int32_t day, persons::death_cause cause) {
	casualty_result result;
	if(stable_id_in_use(state, event_id) || !state.exact_population
		|| uint8_t(cause) > uint8_t(persons::death_cause::attrition)) return result;
	auto unit = find_formation(state, formation_id);
	if(!unit || unit->status == formation_status::destroyed) return result;
	for(auto const& existing : ensure(state)->casualty_events)
		if(existing.event_id == event_id) return result;
	std::vector<casualty_request> equipment_to_remove(requested_equipment_losses.begin(),
		requested_equipment_losses.end());
	std::sort(equipment_to_remove.begin(), equipment_to_remove.end(), [](auto const& left, auto const& right) {
		return left.equipment_model_id < right.equipment_model_id;
	});
	for(size_t i = 0; i < equipment_to_remove.size(); ++i) {
		auto const& request = equipment_to_remove[i];
		if(request.equipment_model_id == 0 || request.quantity == 0
			|| !find_equipment_model(state, request.equipment_model_id)
			|| !mutable_holding(state, formation_id, request.equipment_model_id)
			|| equipment_count(state, formation_id, request.equipment_model_id) < request.quantity
			|| (i && equipment_to_remove[i - 1].equipment_model_id == request.equipment_model_id)) return result;
	}
	auto ranges = ranges_for_formation(state, formation_id);
	uint64_t total_personnel = 0;
	for(auto const& range : ranges) {
		if(std::numeric_limits<uint64_t>::max() - total_personnel < range.count) return result;
		total_personnel += range.count;
	}
	if(requested_personnel_losses > total_personnel) return result;
	if(requested_personnel_losses != 0 && !persons::exact_population::can_project_population_membership(state)) return result;
	for(auto const& range : ranges) {
		if(persons::exact_population::living_people_in_person_range(state,
			range.source_population_cell, range.first_ordinal, range.count, range.ordinal_stride) != range.count) return result;
	}
	uint64_t seed = mix(event_id ^ uint64_t(uint32_t(day))
		^ (formation_id * 0x9e3779b97f4a7c15ULL));
	auto start = total_personnel == 0 ? 0 : seed % total_personnel;
	result.persons_killed.reserve(size_t(requested_personnel_losses));
	for(uint64_t i = 0; i < requested_personnel_losses; ++i) {
		auto offset = (start + i) % total_personnel;
		result.persons_killed.push_back(person_at_offset(ranges, offset));
	}
	sys::date casualty_date{};
	if(day >= 0 && day < int32_t(std::numeric_limits<uint16_t>::max()))
		casualty_date = sys::date{uint16_t(day + 1)};
	if(requested_personnel_losses != 0 && !casualty_date) return casualty_result{};
	for(auto key : result.persons_killed) {
		uint64_t assigned_formation = 0;
		if(!persons::alive(state, key)
			|| !persons::exact_population::has_military_assignment(state, key, &assigned_formation)
			|| assigned_formation != formation_id
			|| !persons::can_kill_person(state, key, casualty_date, cause)) return casualty_result{};
		auto profile = persons::exact_population::profile_for_person(state, key);
		if(profile && !persons::active_offices_of(state, profile).empty()) return casualty_result{};
	}
	auto store = ensure(state);
	for(auto const& key : result.persons_killed) {
		if(!persons::exact_population::unassign_military_person(state, key, formation_id)
			|| !persons::kill_person(state, key, casualty_date, cause, false))
			return casualty_result{};
		store->person_losses.push_back({event_id, formation_id, key, day});
	}
	for(auto const& request : equipment_to_remove) {
		auto holding = mutable_holding(state, formation_id, request.equipment_model_id);
		if(!holding || holding->quantity < request.quantity) return casualty_result{};
		holding->quantity -= request.quantity;
		store->equipment_losses.push_back({event_id, formation_id, request.equipment_model_id,
			request.quantity, day});
	}
	store->casualty_events.push_back({event_id, formation_id, requested_personnel_losses, day, cause});
	std::sort(store->person_losses.begin(), store->person_losses.end(), [](auto const& left, auto const& right) {
		return std::tie(left.event_id, left.person.source_population_cell, left.person.ordinal)
			< std::tie(right.event_id, right.person.source_population_cell, right.person.ordinal);
	});
	std::sort(store->equipment_losses.begin(), store->equipment_losses.end(), [](auto const& left, auto const& right) {
		return std::tie(left.event_id, left.equipment_model_id)
			< std::tie(right.event_id, right.equipment_model_id);
	});
	std::sort(store->casualty_events.begin(), store->casualty_events.end(), [](auto const& left, auto const& right) {
		return left.event_id < right.event_id;
	});
	if(requested_personnel_losses != 0
		&& !persons::exact_population::project_population_membership(state)) return casualty_result{};
	result.applied = true;
	return result;
}

bool destroy_formation(sys::state& state, stable_id formation_id) {
	auto unit = find_by_id(ensure(state)->formations, formation_id,
		[](auto const& value) { return value.id; });
	if(!unit) return false;
	demobilize(state, formation_id);
	unit->status = formation_status::destroyed;
	unit->moving = 0;
	unit->in_combat = 0;
	unit->operational_tempo = 0.0f;
	return true;
}

validation_result validate_canonical_land_forces(sys::state const& state) {
	validation_result result;
	if(!state.land_forces || !state.exact_population) {
		result.valid = false;
		result.errors.push_back("canonical land forces or exact population store is missing");
		return result;
	}
	auto store = ensure(state);
	auto error = [&](std::string message) {
		result.valid = false;
		result.errors.push_back(std::move(message));
	};
	std::set<stable_id> all_ids;
	for(auto const& model : store->equipment_models) {
		if(model.id == 0 || !all_ids.insert(model.id).second) error("duplicate or zero stable ID in equipment models");
		if(!std::isfinite(model.mass) || model.mass <= 0.0f || !std::isfinite(model.reliability)
			|| model.reliability < 0.0f || model.reliability > 1.0f || !std::isfinite(model.attack)
			|| !std::isfinite(model.defense) || !std::isfinite(model.range_km)) error("invalid equipment model properties");
		if(model.commodity && !state.world.commodity_is_valid(model.commodity)) error("equipment model references a missing commodity");
	}
	for(auto const& value : store->templates) {
		if(value.id == 0 || !all_ids.insert(value.id).second) error("duplicate or zero stable ID in templates");
		if(value.personnel_authorization == 0) error("formation template has no personnel authorization");
	}
	std::set<std::pair<stable_id, stable_id>> authorized_equipment;
	for(auto const& value : store->template_equipment) {
		if(!find_template(state, value.template_id) || !find_equipment_model(state, value.equipment_model_id)
			|| value.quantity == 0 || !authorized_equipment.insert({value.template_id, value.equipment_model_id}).second)
			error("orphan, duplicate, or empty template equipment authorization");
	}
	std::set<std::pair<stable_id, uint8_t>> required_consumables;
	for(auto const& value : store->template_consumables)
		if(!find_template(state, value.template_id) || uint8_t(value.kind) > uint8_t(consumable_kind::ammunition)
			|| !std::isfinite(value.per_person) || value.per_person < 0.0
			|| !std::isfinite(value.per_equipment_tonne) || value.per_equipment_tonne < 0.0)
			error("invalid template consumable requirement");
		else if(!required_consumables.insert({value.template_id, uint8_t(value.kind)}).second)
			error("duplicate template consumable requirement");
	for(size_t i = 0; i < store->formations.size(); ++i) {
		auto const& unit = store->formations[i];
		if(unit.id == 0 || !all_ids.insert(unit.id).second) error("duplicate or zero stable ID in formations");
		if(!state.world.nation_is_valid(unit.owner) || !valid_site(state, unit.location)) error("formation has missing owner or site");
		if(!find_template(state, unit.template_id) || !unit.personnel_authorization) error("formation has missing template or authorization");
		if(unit.parent_id != 0) {
			auto parent = find_formation(state, unit.parent_id);
			if(!parent || parent->owner != unit.owner) error("formation has an invalid parent");
		}
		if(uint8_t(unit.status) > uint8_t(formation_status::destroyed)
			|| !std::isfinite(unit.operational_tempo) || unit.operational_tempo < 0.0f
			|| unit.operational_tempo > 1.0f) error("formation status is outside the valid range");
		if(unit.legacy_regiment_index_plus_one != 0) {
			auto index = unit.legacy_regiment_index_plus_one - 1u;
			auto regiment = dcon::regiment_id{dcon::regiment_id::value_base_t(index)};
			if(index >= state.world.regiment_size() || !state.world.regiment_is_valid(regiment))
				error("formation legacy adapter references a missing regiment");
			else {
				auto army = state.world.regiment_get_army_from_army_membership(regiment);
				if(!army || !state.world.army_is_valid(army)
					|| state.world.army_get_controller_from_army_control(army) != unit.owner)
					error("formation legacy adapter owner differs from its regiment army");
			}
			for(size_t j = 0; j < i; ++j)
				if(store->formations[j].legacy_regiment_index_plus_one == unit.legacy_regiment_index_plus_one)
					error("legacy regiment is mapped to multiple formations");
		}
	}
	std::map<stable_id, uint8_t> parent_visit;
	for(auto const& unit : store->formations) {
		std::vector<stable_id> ancestry;
		auto current = &unit;
		while(current) {
			auto& visit = parent_visit[current->id];
			if(visit == 1) {
				error("formation parent hierarchy contains a cycle");
				break;
			}
			if(visit == 2) break;
			visit = 1;
			ancestry.push_back(current->id);
			current = current->parent_id == 0 ? nullptr : find_formation(state, current->parent_id);
		}
		for(auto id : ancestry) parent_visit[id] = 2;
	}
	auto assignments = persons::exact_population::all_military_assignments(state);
	for(size_t i = 0; i < assignments.size(); ++i) {
		auto const& range = assignments[i];
		auto unit = find_formation(state, range.formation_id);
		if(!unit || range.count == 0 || range.source_population_cell == 0
			|| (range.ordinal_stride != 1 && range.ordinal_stride != 4)
			|| persons::exact_population::living_people_in_person_range(state,
				range.source_population_cell, range.first_ordinal, range.count,
				range.ordinal_stride) != range.count) error("personnel assignment range references missing or dead exact people");
		if(unit && unit->status == formation_status::destroyed) error("destroyed formation has assigned personnel");
		if(unit && unit->personnel_authorization != 0
			&& personnel_count(state, unit->id) > unit->personnel_authorization) error("formation exceeds personnel authorization");
		if(range.count == 0 || (range.ordinal_stride != 1 && range.ordinal_stride != 4)
			|| range.count - 1 > (std::numeric_limits<uint64_t>::max() - range.first_ordinal) / range.ordinal_stride) continue;
		auto last = range.first_ordinal + (range.count - 1) * uint64_t(range.ordinal_stride);
		for(size_t j = i + 1; j < assignments.size(); ++j) {
			auto const& other = assignments[j];
			if(other.source_population_cell != range.source_population_cell) break;
			if(other.first_ordinal > last) break;
			if((other.ordinal_stride != 1 && other.ordinal_stride != 4)
				|| other.count == 0
				|| other.count - 1 > (std::numeric_limits<uint64_t>::max() - other.first_ordinal) / other.ordinal_stride) continue;
			auto other_last = other.first_ordinal + (other.count - 1) * uint64_t(other.ordinal_stride);
			auto low = std::max(range.first_ordinal, other.first_ordinal);
			auto high = std::min(last, other_last);
			if(low > high) continue;
			auto period = std::lcm(range.ordinal_stride, other.ordinal_stride);
			auto check_end = low + std::min<uint64_t>(period, high - low);
			for(uint64_t ordinal = low; ordinal <= check_end; ++ordinal)
				if((ordinal - range.first_ordinal) % range.ordinal_stride == 0
					&& (ordinal - other.first_ordinal) % other.ordinal_stride == 0) {
					error("exact person has multiple military assignments");
					break;
				}
		}
	}
	std::set<std::pair<stable_id, stable_id>> formation_equipment_keys;
	for(auto const& holding : store->equipment) {
		if(!formation_equipment_keys.insert({holding.formation_id, holding.equipment_model_id}).second)
			error("duplicate formation equipment holding");
		if(!find_formation(state, holding.formation_id) || !find_equipment_model(state, holding.equipment_model_id)) error("orphan equipment holding");
		if(holding.quantity > 0 && !has_template_model(state,
			find_formation(state, holding.formation_id) ? find_formation(state, holding.formation_id)->template_id : 0,
			holding.equipment_model_id)) error("equipment holding is not authorized by its formation template");
		if(find_formation(state, holding.formation_id)
			&& holding.quantity > equipment_authorization(state, holding.formation_id,
				holding.equipment_model_id)) error("formation equipment exceeds its template authorization");
	}
	std::set<std::pair<stable_id, uint8_t>> formation_consumable_keys;
	for(auto const& inventory : store->consumables) {
		if(!formation_consumable_keys.insert({inventory.formation_id, uint8_t(inventory.kind)}).second)
			error("duplicate formation consumable inventory");
		if(!find_formation(state, inventory.formation_id)
			|| uint8_t(inventory.kind) > uint8_t(consumable_kind::ammunition)
			|| !std::isfinite(inventory.quantity) || inventory.quantity < 0.0)
			error("invalid formation consumable inventory");
	}
	for(auto const& pile : store->stockpiles) {
		if(pile.id == 0 || !all_ids.insert(pile.id).second) error("duplicate or zero stable ID in stockpiles");
		if(!state.world.nation_is_valid(pile.owner) || !valid_site(state, pile.location)
			|| !std::isfinite(pile.quantity) || pile.quantity < 0.0) error("invalid stockpile owner, site, or quantity");
		if(pile.cargo == cargo_kind::equipment && !find_equipment_model(state, pile.equipment_model_id)) error("stockpile references missing equipment model");
	}
	for(auto const& shipment : store->shipments) {
		if(shipment.id == 0 || !all_ids.insert(shipment.id).second) error("duplicate or zero stable ID in shipments");
		if(!find_by_id(store->stockpiles, shipment.source_stockpile_id,
			[](auto const& value) { return value.id; }) || !std::isfinite(shipment.quantity)
			|| shipment.quantity <= 0.0 || shipment.status > 1) error("invalid physical shipment");
		if(shipment.destination_stockpile_id == 0 && shipment.destination_formation_id == 0) error("shipment has no destination");
		if(shipment.destination_stockpile_id != 0 && !find_by_id(store->stockpiles,
			shipment.destination_stockpile_id, [](auto const& value) { return value.id; })) error("shipment references missing depot");
		if(shipment.destination_formation_id != 0 && !find_formation(state,
			shipment.destination_formation_id)) error("shipment references missing formation");
	}
	std::set<stable_id> event_ids;
	std::set<std::pair<uint32_t, uint64_t>> dead_person_keys;
	std::map<stable_id, casualty_event_record const*> events_by_id;
	for(auto const& event : store->casualty_events) {
		if(event.event_id == 0 || !event_ids.insert(event.event_id).second
			|| !find_formation(state, event.formation_id)
			|| uint8_t(event.cause) > uint8_t(persons::death_cause::attrition)
			|| event.reserved[0] != 0 || event.reserved[1] != 0 || event.reserved[2] != 0)
			error("invalid or duplicate casualty event");
		if(!all_ids.insert(event.event_id).second) error("duplicate stable ID in casualty events");
		events_by_id.emplace(event.event_id, &event);
	}
	for(auto const& loss : store->person_losses) {
		auto event = events_by_id.find(loss.event_id);
		if(event == events_by_id.end() || event->second->formation_id != loss.formation_id
			|| !find_formation(state, loss.formation_id)
			|| persons::alive(state, loss.person)
			|| assigned(state, loss.person)) error("personnel loss does not match an exact removal");
		if(!dead_person_keys.insert({loss.person.source_population_cell, loss.person.ordinal}).second) error("exact person appears in multiple casualty records");
	}
	for(auto const& event : store->casualty_events) {
		auto records = uint64_t(std::count_if(store->person_losses.begin(), store->person_losses.end(),
			[&](auto const& loss) { return loss.event_id == event.event_id; }));
		if(records != event.personnel_losses) error("casualty event personnel count differs from exact loss records");
	}
	std::set<std::pair<stable_id, stable_id>> equipment_loss_keys;
	for(auto const& loss : store->equipment_losses) {
		auto event = events_by_id.find(loss.event_id);
		if(event == events_by_id.end() || event->second->formation_id != loss.formation_id
			|| !equipment_loss_keys.insert({loss.event_id, loss.equipment_model_id}).second
			|| !find_formation(state, loss.formation_id)
			|| !find_equipment_model(state, loss.equipment_model_id) || loss.quantity == 0)
			error("equipment loss record is invalid");
	}
	return result;
}

snapshot export_snapshot(sys::state const& state) {
	 snapshot result;
	auto store = ensure(state);
	result.equipment_models = store->equipment_models;
	result.templates = store->templates;
	result.template_equipment = store->template_equipment;
	result.template_consumables = store->template_consumables;
	result.formations = store->formations;
	result.equipment = store->equipment;
	result.consumables = store->consumables;
	result.stockpiles = store->stockpiles;
	result.shipments = store->shipments;
	result.person_losses = store->person_losses;
	result.equipment_losses = store->equipment_losses;
	result.casualty_events = store->casualty_events;
	return result;
}

bool import_snapshot(sys::state& state, snapshot const& value) {
	if(value.version != 1 || !state.exact_population) return false;
	auto candidate = std::make_shared<land_force_store>();
	candidate->equipment_models = value.equipment_models;
	candidate->templates = value.templates;
	candidate->template_equipment = value.template_equipment;
	candidate->template_consumables = value.template_consumables;
	candidate->formations = value.formations;
	candidate->equipment = value.equipment;
	candidate->consumables = value.consumables;
	candidate->stockpiles = value.stockpiles;
	candidate->shipments = value.shipments;
	candidate->person_losses = value.person_losses;
	candidate->equipment_losses = value.equipment_losses;
	candidate->casualty_events = value.casualty_events;
	auto old = state.land_forces;
	state.land_forces = candidate;
	auto valid = validate_canonical_land_forces(state).valid;
	if(!valid) state.land_forces = old;
	return valid;
}

uint64_t canonical_checksum(sys::state const& state) {
	auto value = export_snapshot(state);
	uint64_t hash = 0x4c414e44464f5243ULL;
	for(auto const& model : value.equipment_models) {
		hash = hash_combine(hash, model.id); hash = hash_combine(hash, model.category);
		hash = hash_combine(hash, model.commodity.value); hash = hash_float(hash, model.mass);
		hash = hash_float(hash, model.reliability); hash = hash_float(hash, model.attack);
		hash = hash_float(hash, model.defense); hash = hash_float(hash, model.range_km);
	}
	for(auto const& item : value.templates) { hash = hash_combine(hash, item.id); hash = hash_combine(hash, item.personnel_authorization); }
	for(auto const& item : value.template_equipment) { hash = hash_combine(hash, item.template_id); hash = hash_combine(hash, item.equipment_model_id); hash = hash_combine(hash, item.quantity); }
	for(auto const& item : value.template_consumables) {
		hash = hash_combine(hash, item.template_id); hash = hash_combine(hash, uint8_t(item.kind));
		hash = hash_double(hash, item.per_person); hash = hash_double(hash, item.per_equipment_tonne);
	}
	for(auto const& unit : value.formations) {
		hash = hash_combine(hash, unit.id); hash = hash_combine(hash, unit.parent_id);
		hash = hash_combine(hash, unit.template_id); hash = hash_combine(hash, unit.owner.value);
		hash = hash_combine(hash, unit.legacy_regiment_index_plus_one);
		hash = hash_combine(hash, unit.location.value); hash = hash_combine(hash, unit.personnel_authorization);
		hash = hash_combine(hash, uint8_t(unit.status)); hash = hash_combine(hash, unit.moving);
		hash = hash_combine(hash, unit.in_combat); hash = hash_float(hash, unit.operational_tempo);
	}
	for(auto const& range : persons::exact_population::all_military_assignments(state)) {
		hash = hash_combine(hash, range.formation_id); hash = hash_combine(hash, range.source_population_cell);
		hash = hash_combine(hash, range.first_ordinal); hash = hash_combine(hash, range.count);
		hash = hash_combine(hash, range.ordinal_stride); hash = hash_combine(hash, range.training_days_remaining);
	}
	for(auto const& holding : value.equipment) {
		hash = hash_combine(hash, holding.formation_id); hash = hash_combine(hash, holding.equipment_model_id);
		hash = hash_combine(hash, holding.quantity);
	}
	for(auto const& inventory : value.consumables) {
		hash = hash_combine(hash, inventory.formation_id); hash = hash_combine(hash, uint8_t(inventory.kind));
		hash = hash_double(hash, inventory.quantity);
	}
	for(auto const& pile : value.stockpiles) {
		hash = hash_combine(hash, pile.id); hash = hash_combine(hash, pile.owner.value);
		hash = hash_combine(hash, pile.location.value); hash = hash_combine(hash, uint8_t(pile.kind));
		hash = hash_combine(hash, uint8_t(pile.cargo)); hash = hash_combine(hash, uint8_t(pile.consumable));
		hash = hash_combine(hash, pile.equipment_model_id); hash = hash_double(hash, pile.quantity);
	}
	for(auto const& shipment : value.shipments) {
		hash = hash_combine(hash, shipment.id); hash = hash_combine(hash, shipment.source_stockpile_id);
		hash = hash_combine(hash, shipment.destination_stockpile_id); hash = hash_combine(hash, shipment.destination_formation_id);
		hash = hash_combine(hash, shipment.destination_site.value); hash = hash_combine(hash, uint8_t(shipment.cargo));
		hash = hash_combine(hash, uint8_t(shipment.consumable)); hash = hash_combine(hash, shipment.equipment_model_id);
		hash = hash_double(hash, shipment.quantity); hash = hash_combine(hash, shipment.days_remaining);
		hash = hash_combine(hash, shipment.status);
	}
	for(auto const& loss : value.person_losses) {
		hash = hash_combine(hash, loss.event_id); hash = hash_combine(hash, loss.formation_id);
		hash = hash_combine(hash, loss.person.source_population_cell); hash = hash_combine(hash, loss.person.ordinal);
		hash = hash_combine(hash, uint32_t(loss.day));
	}
	for(auto const& loss : value.equipment_losses) {
		hash = hash_combine(hash, loss.event_id); hash = hash_combine(hash, loss.formation_id);
		hash = hash_combine(hash, loss.equipment_model_id); hash = hash_combine(hash, loss.quantity);
		hash = hash_combine(hash, uint32_t(loss.day));
	}
	for(auto const& event : value.casualty_events) {
		hash = hash_combine(hash, event.event_id); hash = hash_combine(hash, event.formation_id);
		hash = hash_combine(hash, event.personnel_losses); hash = hash_combine(hash, uint32_t(event.day));
		hash = hash_combine(hash, uint8_t(event.cause));
	}
	return hash;
}

void clear_store(sys::state& state) { state.land_forces.reset(); }

} // namespace military::land_forces
