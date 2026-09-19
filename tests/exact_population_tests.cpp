#include "catch.hpp"

#include "persons/exact_population.hpp"

#include <set>

using persons::exact_population::cell_descriptor;
using persons::exact_population::person_key;

namespace exact_population_tests {

cell_descriptor synthetic(uint32_t cell, uint64_t count) {
	cell_descriptor descriptor;
	descriptor.source_population_cell = cell;
	descriptor.literal_count = count;
	descriptor.demographic_seed = uint64_t(cell) * 101;
	return descriptor;
}

}

TEST_CASE("exact population registers 4N identities without DCON mass allocation", "[population][exact]") {
	population_materialization_tests::fixture f;
	auto persons_before = f.state->world.person_size();
	auto actors_before = f.state->world.economic_actor_size();
	auto accounts_before = f.state->world.monetary_account_size();
	auto inventories_before = f.state->world.physical_stock_size();
	auto contracts_before = f.state->world.employment_contract_size();

	auto result = persons::exact_population::register_population_cell(*f.state, f.pop, f.home);
	REQUIRE(result.result == persons::exact_population::status::created);
	REQUIRE(persons::exact_population::literal_count_for_cell(*f.state, uint32_t(f.pop.index()) + 1u) == 12);
	REQUIRE(persons::exact_population::logical_person_count(*f.state) == 12);
	REQUIRE(f.state->world.person_size() == persons_before);
	REQUIRE(f.state->world.economic_actor_size() == actors_before);
	REQUIRE(f.state->world.monetary_account_size() == accounts_before);
	REQUIRE(f.state->world.physical_stock_size() == inventories_before);
	REQUIRE(f.state->world.employment_contract_size() == contracts_before);

	std::set<person_key, bool(*)(person_key, person_key)> keys([](auto left, auto right) {
		return left.source_population_cell == right.source_population_cell
			? left.ordinal < right.ordinal
			: left.source_population_cell < right.source_population_cell;
	});
	uint32_t anchors = 0;
	for(uint64_t ordinal = 0; ordinal < 12; ++ordinal) {
		person_key key{uint32_t(f.pop.index()) + 1u, ordinal};
		REQUIRE(persons::exact_population::exists(*f.state, key));
		REQUIRE(keys.insert(key).second);
		anchors += persons::exact_population::is_source_workforce_anchor(*f.state, key) ? 1u : 0u;
	}
	REQUIRE(anchors == 3);
	REQUIRE_FALSE(persons::exact_population::exists(*f.state, person_key{uint32_t(f.pop.index()) + 1u, 12}));
}

TEST_CASE("exact population bootstrap attributes are deterministic and sealed", "[population][exact]") {
	population_materialization_tests::fixture first;
	population_materialization_tests::fixture second;
	REQUIRE(persons::exact_population::register_population_cell(*first.state, first.pop, first.home).result
		== persons::exact_population::status::created);
	REQUIRE(persons::exact_population::register_population_cell(*second.state, second.pop, second.home).result
		== persons::exact_population::status::created);

	for(uint64_t ordinal = 0; ordinal < 12; ++ordinal) {
		person_key first_key{uint32_t(first.pop.index()) + 1u, ordinal};
		person_key second_key{uint32_t(second.pop.index()) + 1u, ordinal};
		REQUIRE(persons::exact_population::birth_day_index(*first.state, first_key)
			== persons::exact_population::birth_day_index(*second.state, second_key));
		REQUIRE(persons::exact_population::age_days(*first.state, first_key, first.state->current_date)
			== persons::exact_population::age_days(*second.state, second_key, second.state->current_date));
		REQUIRE(persons::exact_population::home_site(*first.state, first_key) == first.home);
		REQUIRE(persons::exact_population::source_culture(*first.state, first_key) == first.culture);
		REQUIRE(persons::exact_population::source_religion(*first.state, first_key) == first.religion);
		REQUIRE(persons::exact_population::source_pop_type(*first.state, first_key) == first.pop_type);
	}

	auto changed_culture = first.state->world.create_culture();
	auto changed_religion = first.state->world.create_religion();
	auto changed_pop_type = first.state->world.create_pop_type();
	first.state->world.pop_set_size(first.pop, 99.0f);
	first.state->world.pop_set_culture(first.pop, changed_culture);
	first.state->world.pop_set_religion(first.pop, changed_religion);
	first.state->world.pop_set_poptype(first.pop, changed_pop_type);
	person_key key{uint32_t(first.pop.index()) + 1u, 1};
	REQUIRE(persons::exact_population::literal_count_for_cell(*first.state, key.source_population_cell) == 12);
	REQUIRE(persons::exact_population::source_culture(*first.state, key) == first.culture);
	REQUIRE(persons::exact_population::source_religion(*first.state, key) == first.religion);
	REQUIRE(persons::exact_population::source_pop_type(*first.state, key) == first.pop_type);
}

TEST_CASE("exact workforce anchors receive working-age bootstrap ages", "[population][exact]") {
	population_materialization_tests::fixture f;
	REQUIRE(persons::exact_population::register_population_cell(*f.state, f.pop, f.home).result
		== persons::exact_population::status::created);
	person_key first_key{uint32_t(f.pop.index()) + 1u, 0};
	auto age_before = persons::exact_population::age_days(*f.state, first_key, f.state->current_date);
	f.state->current_date = sys::date{30365};
	REQUIRE(persons::exact_population::age_days(*f.state, first_key, f.state->current_date) == age_before + 365);
	for(uint64_t ordinal = 0; ordinal < 12; ++ordinal) {
		person_key key{uint32_t(f.pop.index()) + 1u, ordinal};
		if(persons::exact_population::is_source_workforce_anchor(*f.state, key)) {
			REQUIRE(persons::exact_population::age_years(*f.state, key, f.state->current_date) >= 18);
			REQUIRE(persons::exact_population::age_years(*f.state, key, f.state->current_date) < 65);
		}
	}
}

TEST_CASE("exact population mutable overlays are sparse and person-specific", "[population][exact]") {
	population_materialization_tests::fixture f;
	REQUIRE(persons::exact_population::register_population_cell(*f.state, f.pop, f.home).result
		== persons::exact_population::status::created);
	auto other_site = f.state->world.create_site();
	f.state->world.force_create_site_location(other_site, f.province);
	person_key changed{uint32_t(f.pop.index()) + 1u, 1};
	person_key neighbor{uint32_t(f.pop.index()) + 1u, 2};
	REQUIRE(persons::exact_population::set_alive(*f.state, changed, false));
	REQUIRE(persons::exact_population::set_home_site(*f.state, changed, other_site));
	REQUIRE(persons::exact_population::override_count(*f.state) == 1);
	REQUIRE_FALSE(persons::exact_population::alive(*f.state, changed));
	REQUIRE(persons::exact_population::home_site(*f.state, changed) == other_site);
	REQUIRE(persons::exact_population::alive(*f.state, neighbor));
	REQUIRE(persons::exact_population::home_site(*f.state, neighbor) == f.home);
}

TEST_CASE("synthetic exact catalogs scale by ranges rather than DCON objects", "[population][exact][scale]") {
	population_materialization_tests::fixture f;
	auto persons_before = f.state->world.person_size();
	auto actors_before = f.state->world.economic_actor_size();
	auto accounts_before = f.state->world.monetary_account_size();
	auto inventory_before = f.state->world.physical_stock_size();
	auto contracts_before = f.state->world.employment_contract_size();
	REQUIRE(persons::exact_population::register_synthetic_population_cell(*f.state,
		exact_population_tests::synthetic(100, 1'000'000)).result == persons::exact_population::status::created);
	REQUIRE(persons::exact_population::register_synthetic_population_cell(*f.state,
		exact_population_tests::synthetic(101, 100'000'000)).result == persons::exact_population::status::created);
	REQUIRE(persons::exact_population::register_synthetic_population_cell(*f.state,
		exact_population_tests::synthetic(102, 1'000'000'000)).result == persons::exact_population::status::created);
	REQUIRE(persons::exact_population::logical_person_count(*f.state) == 1'101'000'012);
	for(auto key : {person_key{100, 0}, person_key{100, 999'999}, person_key{101, 50'000'000},
		person_key{102, 999'999'999}})
		REQUIRE(persons::exact_population::exists(*f.state, key));
	REQUIRE_FALSE(persons::exact_population::exists(*f.state, person_key{102, 1'000'000'000}));
	REQUIRE(f.state->world.person_size() == persons_before);
	REQUIRE(f.state->world.economic_actor_size() == actors_before);
	REQUIRE(f.state->world.monetary_account_size() == accounts_before);
	REQUIRE(f.state->world.physical_stock_size() == inventory_before);
	REQUIRE(f.state->world.employment_contract_size() == contracts_before);
}

TEST_CASE("legacy exact-person bridge is explicit and idempotent", "[population][exact][bridge]") {
	population_materialization_tests::fixture f;
	REQUIRE(persons::exact_population::register_population_cell(*f.state, f.pop, f.home).result
		== persons::exact_population::status::created);
	person_key key{uint32_t(f.pop.index()) + 1u, 1};
	auto persons_before = f.state->world.person_size();
	auto actors_before = f.state->world.economic_actor_size();
	REQUIRE_FALSE(persons::exact_population::legacy_person_for_exact_person(*f.state, key));
	REQUIRE(f.state->world.person_size() == persons_before);
	REQUIRE(f.state->world.economic_actor_size() == actors_before);
	auto bridge = persons::exact_population::materialize_legacy_person_bridge(*f.state, key);
	REQUIRE(bridge);
	REQUIRE(persons::exact_population::legacy_person_for_exact_person(*f.state, key) == bridge);
	REQUIRE(persons::exact_population::materialize_legacy_person_bridge(*f.state, key) == bridge);
	REQUIRE(persons::exact_population::bridge_count(*f.state) == 1);
	REQUIRE(f.state->world.person_size() == persons_before + 1);
	REQUIRE(f.state->world.economic_actor_size() == actors_before + 1);
	REQUIRE(persons::birth_day_index(*f.state, bridge) == persons::exact_population::birth_day_index(*f.state, key));
	REQUIRE(f.state->world.person_get_source_population_cell(bridge) == key.source_population_cell);
	REQUIRE(f.state->world.person_get_source_population_ordinal(bridge) == key.ordinal);
}

TEST_CASE("exact catalog snapshot restores sealed descriptors and overlays", "[population][exact][persistence]") {
	population_materialization_tests::fixture f;
	REQUIRE(persons::exact_population::register_population_cell(*f.state, f.pop, f.home).result
		== persons::exact_population::status::created);
	person_key key{uint32_t(f.pop.index()) + 1u, 1};
	REQUIRE(persons::exact_population::set_alive(*f.state, key, false));
	auto bridge = persons::exact_population::materialize_legacy_person_bridge(*f.state, key);
	auto snapshot = persons::exact_population::export_snapshot(*f.state);
	persons::exact_population::clear_store(*f.state);
	REQUIRE(persons::exact_population::cell_count(*f.state) == 0);
	REQUIRE(persons::exact_population::import_snapshot(*f.state, snapshot));
	REQUIRE(persons::exact_population::exists(*f.state, key));
	REQUIRE_FALSE(persons::exact_population::alive(*f.state, key));
	REQUIRE(persons::exact_population::legacy_person_for_exact_person(*f.state, key) == bridge);
}
