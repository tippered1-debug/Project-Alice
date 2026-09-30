#include "catch.hpp"

#include "economy/causal_order.hpp"
#include "economy/exact_person_economy.hpp"
#include "economy/physical/exact_person_freight.hpp"
#include "economy/physical/exact_person_goods.hpp"
#include "economy/physical/inventory.hpp"
#include "economy/physical/shipments.hpp"
#include "economy/physical/labor_dynamics.hpp"
#include "military/land_forces.hpp"
#include "nations/strategic_statecraft.hpp"
#include "persons/exact_population.hpp"
#include "serialization.hpp"
#include "system_state.hpp"
#include "economy/economy_constants.hpp"
#include "world/spatial_runtime.hpp"

#include <algorithm>
#include <array>
#include <memory>
#include <vector>

namespace land_forces_tests {

constexpr uint64_t tank_model = 301;
constexpr uint64_t rifle_model = 302;
constexpr uint64_t line_template = 401;
constexpr uint64_t a_first = 501;
constexpr uint64_t a_second = 502;
constexpr uint64_t a_isolated = 503;
constexpr uint64_t b_first = 504;
constexpr uint64_t b_second = 505;
constexpr uint64_t a_warehouse = 601;
constexpr uint64_t a_depot = 602;
constexpr uint64_t b_warehouse = 603;
constexpr uint64_t b_depot = 604;

struct miniature {
	std::unique_ptr<sys::state> state = std::make_unique<sys::state>();
	dcon::nation_id nation_a{};
	dcon::nation_id nation_b{};
	dcon::province_id a_capital{};
	dcon::province_id a_connected{};
	dcon::province_id a_isolated_province{};
	dcon::province_id b_capital{};
	dcon::province_id b_connected{};
	dcon::site_id a_capital_site{};
	dcon::site_id a_connected_site{};
	dcon::site_id a_second_site{};
	dcon::site_id a_isolated_site{};
	dcon::site_id b_capital_site{};
	dcon::site_id b_connected_site{};
	dcon::pop_id pop_a{};
	dcon::pop_id pop_b{};
	uint32_t cell_a = 0;
	uint32_t cell_b = 0;
	dcon::commodity_id tank_commodity{};
	dcon::commodity_id rifle_commodity{};
	dcon::commodity_id food_commodity{};
	dcon::commodity_id fuel_commodity{};
	dcon::commodity_id ammunition_commodity{};

	explicit miniature(bool reverse_authoring = false) {
		state->current_date = sys::date{30000};
		state->province_definitions.first_sea_province = dcon::province_id{100};
		state->world.province_resize_building_level(economy::max_building_types);
		nation_a = state->world.create_nation();
		nation_b = state->world.create_nation();
		a_capital = make_province(nation_a);
		a_connected = make_province(nation_a);
		a_isolated_province = make_province(nation_a);
		b_capital = make_province(nation_b);
		b_connected = make_province(nation_b);
		a_capital_site = make_site(a_capital);
		a_connected_site = make_site(a_connected);
		a_second_site = make_site(a_connected);
		a_isolated_site = make_site(a_isolated_province);
		b_capital_site = make_site(b_capital);
		b_connected_site = make_site(b_connected);
		tank_commodity = state->world.create_commodity();
		rifle_commodity = state->world.create_commodity();
		food_commodity = state->world.create_commodity();
		fuel_commodity = state->world.create_commodity();
		ammunition_commodity = state->world.create_commodity();
		make_capital_state(nation_a, a_capital, a_connected);
		make_capital_state(nation_b, b_capital, b_connected);
		connect(a_capital, a_connected, 100.0f);
		connect(b_capital, b_connected, 100.0f);
		pop_a = make_population(a_capital);
		pop_b = make_population(b_capital);
		cell_a = uint32_t(pop_a.index()) + 1u;
		cell_b = uint32_t(pop_b.index()) + 1u;
		auto people = persons::exact_population::bootstrap_from_current_pops(*state);
		REQUIRE(people.complete);
		REQUIRE(people.logical_people == 400);
		REQUIRE(persons::exact_population::project_population_membership(*state));
		auto spatial = world::spatial_runtime::bootstrap(*state);
		REQUIRE(spatial.status != world::spatial_runtime::bootstrap_status::invalid_world);
		military::land_forces::initialize_empty_store(*state);
		economy::causal_order::initialize_empty_store(*state);
		economy::exact_person_economy::initialize_empty_store(*state);
		economy::physical::exact_person_goods::initialize_empty_store(*state);
		economy::physical::exact_person_freight::initialize_empty_store(*state);
		economy::physical::labor_dynamics::initialize_empty_store(*state);
		nations::strategic_statecraft::initialize(*state);
		state->transformation_government_state.resize(state->world.nation_size());
		state->transformation_legislation_state.resize(state->world.nation_size());
		add_equipment(reverse_authoring);
		add_templates_and_equipment(reverse_authoring);
		add_formations(reverse_authoring);
		add_people(reverse_authoring);
		add_equipment_and_supply();
		add_stockpiles(reverse_authoring);
		REQUIRE(military::land_forces::validate_canonical_land_forces(*state).valid);
	}

	dcon::province_id make_province(dcon::nation_id owner) {
		auto province = state->world.create_province();
		state->world.province_set_nation_from_province_ownership(province, owner);
		state->world.province_set_nation_from_province_control(province, owner);
		state->world.province_set_control_ratio(province, 1.0f);
		return province;
	}

	dcon::site_id make_site(dcon::province_id province) {
		auto site = state->world.create_site();
		state->world.force_create_site_location(site, province);
		return site;
	}

	dcon::pop_id make_population(dcon::province_id province) {
		auto type = state->world.create_pop_type();
		auto culture = state->world.create_culture();
		auto religion = state->world.create_religion();
		auto pop = state->world.create_pop();
		state->world.force_create_pop_location(pop, province);
		state->world.pop_set_poptype(pop, type);
		state->world.pop_set_culture(pop, culture);
		state->world.pop_set_religion(pop, religion);
		state->world.pop_set_size(pop, 50.0f);
		return pop;
	}

	void make_capital_state(dcon::nation_id, dcon::province_id capital, dcon::province_id neighbor) {
		auto state_instance = state->world.create_state_instance();
		state->world.state_instance_set_capital(state_instance, capital);
		state->world.province_set_state_membership(capital, state_instance);
		state->world.province_set_state_membership(neighbor, state_instance);
	}

	void connect(dcon::province_id from, dcon::province_id to, float distance) {
		auto edge = state->world.force_create_province_adjacency(from, to);
		state->world.province_adjacency_set_distance_km(edge, distance);
		state->world.province_adjacency_set_type(edge, 0);
	}

	void add_equipment(bool reverse) {
		military::land_forces::equipment_model tank{tank_model, 1, tank_commodity, 1.0f, 0.8f, 8.0f, 7.0f, 500.0f};
		military::land_forces::equipment_model rifle{rifle_model, 2, rifle_commodity, 0.02f, 0.9f, 1.0f, 0.5f, 0.5f};
		if(reverse) {
			REQUIRE(military::land_forces::add_equipment_model(*state, rifle));
			REQUIRE(military::land_forces::add_equipment_model(*state, tank));
		} else {
			REQUIRE(military::land_forces::add_equipment_model(*state, tank));
			REQUIRE(military::land_forces::add_equipment_model(*state, rifle));
		}
	}

	void add_templates_and_equipment(bool reverse) {
		REQUIRE(military::land_forces::add_template(*state, {line_template, 100}));
		military::land_forces::template_equipment_authorization tank{line_template, tank_model, 20};
		military::land_forces::template_equipment_authorization rifle{line_template, rifle_model, 100};
		if(reverse) {
			REQUIRE(military::land_forces::authorize_template_equipment(*state, rifle));
			REQUIRE(military::land_forces::authorize_template_equipment(*state, tank));
		} else {
			REQUIRE(military::land_forces::authorize_template_equipment(*state, tank));
			REQUIRE(military::land_forces::authorize_template_equipment(*state, rifle));
		}
		REQUIRE(military::land_forces::set_template_consumable_requirement(*state,
			{line_template, military::land_forces::consumable_kind::food, food_commodity, {}, 0.1, 0.0}));
		REQUIRE(military::land_forces::set_template_consumable_requirement(*state,
			{line_template, military::land_forces::consumable_kind::fuel, fuel_commodity, {}, 0.0, 0.1}));
		REQUIRE(military::land_forces::set_template_consumable_requirement(*state,
			{line_template, military::land_forces::consumable_kind::ammunition, ammunition_commodity, {}, 0.01, 0.0}));
	}

	void add_formations(bool reverse) {
		std::vector<military::land_forces::formation> units = {
			{a_first, 0, line_template, 0, nation_a, a_capital_site, 0, military::land_forces::formation_status::active, 0, 0, 0, 1.0f},
			{a_second, a_first, line_template, 0, nation_a, a_second_site, 0, military::land_forces::formation_status::active, 0, 0, 0, 0.75f},
			{a_isolated, 0, line_template, 0, nation_a, a_isolated_site, 0, military::land_forces::formation_status::active, 0, 0, 0, 0.5f},
			{b_first, 0, line_template, 0, nation_b, b_capital_site, 0, military::land_forces::formation_status::active, 0, 0, 0, 1.0f},
			{b_second, b_first, line_template, 0, nation_b, b_connected_site, 0, military::land_forces::formation_status::active, 0, 0, 0, 0.75f}
		};
		if(reverse) std::reverse(units.begin(), units.end());
		// Create roots before their children, reversing country insertion order.
		for(auto id : reverse ? std::vector<uint64_t>{b_first, a_first, a_isolated}
			: std::vector<uint64_t>{a_first, a_isolated, b_first}) {
			auto unit = *std::find_if(units.begin(), units.end(), [&](auto const& item) { return item.id == id; });
			REQUIRE(military::land_forces::create_formation(*state, unit));
		}
		for(auto id : reverse ? std::vector<uint64_t>{b_second, a_second}
			: std::vector<uint64_t>{a_second, b_second}) {
			auto unit = *std::find_if(units.begin(), units.end(), [&](auto const& item) { return item.id == id; });
			REQUIRE(military::land_forces::create_formation(*state, unit));
		}
	}

	void add_people(bool reverse) {
		std::vector<persons::exact_population::military_assignment_range> ranges = {
			{cell_a, 1, 0, 100, a_first, 7, 0}, {cell_a, 1, 100, 100, a_second, 7, 0},
			{cell_b, 1, 0, 100, b_first, 7, 0}, {cell_b, 1, 100, 100, b_second, 7, 0}
		};
		if(reverse) std::reverse(ranges.begin(), ranges.end());
		for(auto const& range : ranges)
			REQUIRE(persons::exact_population::assign_military_range(*state, range));
	}

	void add_equipment_and_supply() {
		for(auto id : {a_first, b_first}) {
			REQUIRE(military::land_forces::set_initial_equipment_holding(*state, id, tank_model, 20));
			REQUIRE(military::land_forces::set_initial_equipment_holding(*state, id, rifle_model, 100));
		}
		for(auto id : {a_second, b_second, a_isolated})
			REQUIRE(military::land_forces::set_initial_equipment_holding(*state, id, rifle_model, 100));
		for(auto id : {a_first, a_second, a_isolated, b_first, b_second}) {
			REQUIRE(military::land_forces::set_initial_consumable_inventory(*state, id,
				military::land_forces::consumable_kind::food, 10000.0));
			REQUIRE(military::land_forces::set_initial_consumable_inventory(*state, id,
				military::land_forces::consumable_kind::fuel, 10000.0));
			REQUIRE(military::land_forces::set_initial_consumable_inventory(*state, id,
				military::land_forces::consumable_kind::ammunition, 10000.0));
		}
	}

	void add_stockpiles(bool reverse) {
		std::vector<military::land_forces::stockpile> piles = {
			{a_warehouse, nation_a, a_capital_site, military::land_forces::stockpile_kind::warehouse,
				military::land_forces::cargo_kind::equipment, military::land_forces::consumable_kind::food, {}, tank_model, 20.0},
			{a_depot, nation_a, a_connected_site, military::land_forces::stockpile_kind::depot,
				military::land_forces::cargo_kind::equipment, military::land_forces::consumable_kind::food, {}, tank_model, 10.0},
			{b_warehouse, nation_b, b_capital_site, military::land_forces::stockpile_kind::warehouse,
				military::land_forces::cargo_kind::equipment, military::land_forces::consumable_kind::food, {}, tank_model, 20.0},
			{b_depot, nation_b, b_connected_site, military::land_forces::stockpile_kind::depot,
				military::land_forces::cargo_kind::equipment, military::land_forces::consumable_kind::food, {}, tank_model, 10.0}
		};
		if(reverse) std::reverse(piles.begin(), piles.end());
		for(auto const& pile : piles) REQUIRE(military::land_forces::create_stockpile(*state, pile));
	}
};

void advance(miniature& world, int days) {
	for(int i = 0; i < days; ++i) {
		economy::physical::shipments::process_arrivals(*world.state);
		military::land_forces::update_daily(*world.state);
		world.state->current_date += 1;
	}
}

double tank_material(miniature const& world) {
	double total = 0.0;
	world.state->world.for_each_physical_stock([&](dcon::physical_stock_id stock) {
		if(world.state->world.physical_stock_get_commodity_from_physical_stock_commodity(stock)
			== world.tank_commodity)
			total += std::max(0.0f, world.state->world.physical_stock_get_quantity(stock));
	});
	world.state->world.for_each_shipment([&](dcon::shipment_id shipment) {
		if(world.state->world.shipment_get_commodity(shipment) == world.tank_commodity)
			total += std::max(0.0f, world.state->world.shipment_get_remaining_quantity(shipment));
	});
	return total;
}

}

TEST_CASE("canonical land forces move and route physical equipment without inventory creation",
	"[military][land-forces][integration]") {
	land_forces_tests::miniature world;
	auto& state = *world.state;
	auto before_people = military::land_forces::personnel_count(state, land_forces_tests::a_second);
	REQUIRE_FALSE(military::land_forces::create_formation(state,
		*military::land_forces::find_formation(state, land_forces_tests::a_first)));
	REQUIRE_FALSE(persons::exact_population::assign_military_range(state,
		{world.cell_a, 1, 0, 100, land_forces_tests::a_first, 7, 0}));
	REQUIRE(military::land_forces::personnel_route_is_valid(state, world.nation_a,
		world.a_capital_site, world.a_connected_site, 100));
	REQUIRE_FALSE(military::land_forces::personnel_route_is_valid(state, world.nation_a,
		world.a_capital_site, world.a_isolated_site, 100));
	REQUIRE(military::land_forces::equipment_count(state, land_forces_tests::a_second, land_forces_tests::tank_model) == 0);
	REQUIRE(military::land_forces::logistics_demand(state, land_forces_tests::a_second) > 0.0);
	REQUIRE(military::land_forces::replacement_load(state, land_forces_tests::a_second) == Approx(2.0));
	auto material_before = land_forces_tests::tank_material(world);
	REQUIRE_FALSE(military::land_forces::dispatch_to_formation(state, 701, land_forces_tests::a_depot,
		land_forces_tests::a_isolated, 1.0));
	REQUIRE(land_forces_tests::tank_material(world) == Approx(material_before));
	REQUIRE(military::land_forces::dispatch_to_stockpile(state, 702, land_forces_tests::a_warehouse,
		land_forces_tests::a_depot, 5.0));
	REQUIRE(land_forces_tests::tank_material(world) == Approx(material_before));
	REQUIRE(military::land_forces::dispatch_to_formation(state, 703, land_forces_tests::a_depot,
		land_forces_tests::a_second, 5.0));
	REQUIRE(land_forces_tests::tank_material(world) == Approx(material_before));
	REQUIRE(military::land_forces::equipment_count(state, land_forces_tests::a_second,
		land_forces_tests::tank_model) == 0);
	REQUIRE(military::land_forces::personnel_count(state, land_forces_tests::a_second) == before_people);
	REQUIRE(military::land_forces::dispatch_to_formation(state, 704, land_forces_tests::a_depot,
		land_forces_tests::a_second, 11.0) == false);
	auto food_before = military::land_forces::consumable_quantity(state, land_forces_tests::a_first,
		military::land_forces::consumable_kind::food);
	advance(world, 1);
	REQUIRE(military::land_forces::equipment_count(state, land_forces_tests::a_second,
		land_forces_tests::tank_model) == 5);
	REQUIRE(military::land_forces::replacement_load(state, land_forces_tests::a_second) == Approx(1.5));
	REQUIRE(military::land_forces::consumable_quantity(state, land_forces_tests::a_first,
		military::land_forces::consumable_kind::food) < food_before);
	REQUIRE(military::land_forces::dispatch_to_formation(state, 705, land_forces_tests::a_depot,
		land_forces_tests::a_second, 10.0));
	advance(world, 1);
	REQUIRE(military::land_forces::equipment_count(state, land_forces_tests::a_second,
		land_forces_tests::tank_model) == 15);
	REQUIRE(military::land_forces::replacement_load(state, land_forces_tests::a_second) == Approx(0.5));
	REQUIRE_FALSE(military::land_forces::dispatch_to_formation(state, 706, land_forces_tests::a_depot,
		land_forces_tests::a_second, 1.0));
	REQUIRE(land_forces_tests::tank_material(world) == Approx(material_before));
	REQUIRE(military::land_forces::move_formation(state, land_forces_tests::a_second, world.a_capital_site));
	REQUIRE(military::land_forces::personnel_count(state, land_forces_tests::a_second) == before_people);
	REQUIRE(military::land_forces::equipment_count(state, land_forces_tests::a_second,
		land_forces_tests::tank_model) == 5);
	auto no_vacancy_candidates = military::land_forces::recruitment_candidates(
		state, land_forces_tests::a_first, 1);
	REQUIRE(military::land_forces::recruit_personnel(state, land_forces_tests::a_first,
		no_vacancy_candidates, 1, 0) == 0);
	REQUIRE(military::land_forces::validate_canonical_land_forces(state).valid);
}

TEST_CASE("land casualties retire exact people and equipment deterministically",
	"[military][land-forces][casualties]") {
	land_forces_tests::miniature first;
	land_forces_tests::miniature shuffled(true);
	std::array<military::land_forces::casualty_request, 1> losses{{{land_forces_tests::tank_model, 10}}};
	auto day = int32_t(first.state->current_date.to_raw_value() - 1);
	auto first_result = military::land_forces::apply_losses(*first.state, land_forces_tests::a_first,
		100, losses, 90001, day);
	auto shuffled_result = military::land_forces::apply_losses(*shuffled.state, land_forces_tests::a_first,
		100, losses, 90001, day);
	REQUIRE(first_result.applied);
	REQUIRE(shuffled_result.applied);
	REQUIRE(first_result.persons_killed == shuffled_result.persons_killed);
	REQUIRE(first_result.persons_killed.size() == 100);
	for(auto person : first_result.persons_killed) REQUIRE_FALSE(persons::alive(*first.state, person));
	REQUIRE(military::land_forces::personnel_count(*first.state, land_forces_tests::a_first) == 0);
	REQUIRE(military::land_forces::equipment_count(*first.state, land_forces_tests::a_first,
		land_forces_tests::tank_model) == 10);
	REQUIRE(first.state->world.pop_get_size(first.pop_a) == Approx(25.0f));
	std::array<military::land_forces::casualty_request, 0> no_equipment_losses{};
	auto attrition_first = military::land_forces::apply_losses(*first.state, land_forces_tests::a_second,
		1, no_equipment_losses, 90002, day, persons::death_cause::attrition);
	auto attrition_shuffled = military::land_forces::apply_losses(*shuffled.state, land_forces_tests::a_second,
		1, no_equipment_losses, 90002, day, persons::death_cause::attrition);
	REQUIRE(attrition_first.applied);
	REQUIRE(attrition_shuffled.applied);
	auto population_after_attrition = persons::exact_population::export_snapshot(*first.state);
	REQUIRE(std::any_of(population_after_attrition.deaths.begin(), population_after_attrition.deaths.end(),
		[](auto const& death) { return death.cause == uint8_t(persons::death_cause::attrition); }));
	REQUIRE(first.state->world.pop_get_size(first.pop_a) == Approx(24.75f));
	auto no_available_people = military::land_forces::recruitment_candidates(
		*first.state, land_forces_tests::a_first, 1);
	REQUIRE(military::land_forces::recruit_personnel(*first.state, land_forces_tests::a_first,
		no_available_people, 1, 0) == 0);
	REQUIRE_FALSE(military::land_forces::apply_losses(*first.state, land_forces_tests::a_first,
		1, losses, 90001, day).applied);
	REQUIRE(military::land_forces::canonical_checksum(*first.state)
		== military::land_forces::canonical_checksum(*shuffled.state));
	REQUIRE(military::land_forces::validate_canonical_land_forces(*first.state).valid);
	REQUIRE(military::land_forces::validate_canonical_land_forces(*shuffled.state).valid);
}

TEST_CASE("canonical land force save and replay preserves daily checksum",
	"[military][land-forces][serialization][determinism]") {
	land_forces_tests::miniature continuous;
	land_forces_tests::miniature split(true);
	for(int day = 0; day < 90; ++day) {
		land_forces_tests::advance(continuous, 1);
		land_forces_tests::advance(split, 1);
		REQUIRE(military::land_forces::validate_canonical_land_forces(*continuous.state).valid);
		REQUIRE(military::land_forces::validate_canonical_land_forces(*split.state).valid);
		REQUIRE(military::land_forces::canonical_checksum(*continuous.state)
			== military::land_forces::canonical_checksum(*split.state));
	}
	REQUIRE(military::land_forces::dispatch_to_formation(*continuous.state, 80001,
		land_forces_tests::a_depot, land_forces_tests::a_second, 1.0));
	REQUIRE(military::land_forces::dispatch_to_formation(*split.state, 80001,
		land_forces_tests::a_depot, land_forces_tests::a_second, 1.0));
	REQUIRE(sys::canonical_runtime_loaded(*split.state));
	std::vector<uint8_t> bytes(sys::sizeof_save_section(*split.state));
	auto const* end = sys::write_save_section(bytes.data(), *split.state);
	REQUIRE(end == bytes.data() + bytes.size());
	land_forces_tests::miniature loaded;
	auto const* loaded_end = sys::read_save_section(bytes.data(), end, *loaded.state);
	REQUIRE(loaded_end == end);
	REQUIRE(sys::canonical_runtime_loaded(*loaded.state));
	for(int day = 0; day < 90; ++day) {
		land_forces_tests::advance(continuous, 1);
		land_forces_tests::advance(loaded, 1);
		REQUIRE(military::land_forces::validate_canonical_land_forces(*continuous.state).valid);
		REQUIRE(military::land_forces::validate_canonical_land_forces(*loaded.state).valid);
		REQUIRE(military::land_forces::canonical_checksum(*continuous.state)
			== military::land_forces::canonical_checksum(*loaded.state));
	}
	REQUIRE(military::land_forces::canonical_checksum(*continuous.state)
		== military::land_forces::canonical_checksum(*loaded.state));
	REQUIRE(military::land_forces::validate_canonical_land_forces(*loaded.state).valid);
}
