#include "catch.hpp"

#include "economy/causal_order.hpp"
#include "economy/exact_person_economy.hpp"
#include "economy/payroll.hpp"
#include "economy/physical/exact_person_freight.hpp"
#include "economy/physical/exact_person_goods.hpp"
#include "economy/physical/job_market.hpp"
#include "economy/physical/labor_dynamics.hpp"
#include "governance/governance.hpp"
#include "gamestate/serialization.hpp"
#include "military/land_forces.hpp"
#include "nations/strategic_statecraft.hpp"
#include "persons/exact_population.hpp"
#include "persons/persons.hpp"
#include "system_state.hpp"

#include <algorithm>
#include <limits>

namespace person_kernel_tests {

void initialize_runtime(sys::state& state) {
	if(!state.exact_population) persons::exact_population::initialize_empty_store(state);
	if(!state.causal_order) economy::causal_order::initialize_empty_store(state);
	if(!state.exact_person_economy) economy::exact_person_economy::initialize_empty_store(state);
	if(!state.exact_person_goods) economy::physical::exact_person_goods::initialize_empty_store(state);
	if(!state.exact_person_freight) economy::physical::exact_person_freight::initialize_empty_store(state);
	if(!state.labor_dynamics) economy::physical::labor_dynamics::initialize_empty_store(state);
	if(!military::land_forces::initialized(state)) military::land_forces::initialize_empty_store(state);
}

dcon::pop_id make_population(sys::state& state, dcon::province_id province,
	dcon::site_id home, dcon::pop_type_id type, dcon::culture_id culture,
	dcon::religion_id religion, float size = 0.25f) {
	auto pop = state.world.create_pop();
	state.world.force_create_pop_location(pop, province);
	state.world.pop_set_size(pop, size);
	state.world.pop_set_poptype(pop, type);
	state.world.pop_set_culture(pop, culture);
	state.world.pop_set_religion(pop, religion);
	REQUIRE(persons::register_population_cell(state, pop, home));
	return pop;
}

}

TEST_CASE("person kernel bootstraps POP input into canonical keys and sparse profiles",
	"[population][persons][kernel]") {
	population_materialization_tests::fixture f;
	person_kernel_tests::initialize_runtime(*f.state);
	auto bootstrap = persons::exact_population::bootstrap_from_current_pops(*f.state);
	REQUIRE(bootstrap.complete);
	REQUIRE(bootstrap.registered_cells == 1);
	REQUIRE(bootstrap.logical_people == 12);

	persons::person_key key{uint32_t(f.pop.index()) + 1u, 0};
	REQUIRE(persons::exists(*f.state, key));
	REQUIRE(persons::alive(*f.state, key));
	REQUIRE(persons::current_population(*f.state, key) == f.pop);
	REQUIRE(persons::home_site(*f.state, key) == f.home);
	auto profile = persons::materialize_profile(*f.state, key);
	REQUIRE(profile);
	REQUIRE(persons::materialize_profile(*f.state, key) == profile);
	REQUIRE(persons::canonical_key(*f.state, profile) == key);
	REQUIRE(persons::materialized_profile_count(*f.state) == 1);
	REQUIRE(persons::validate_person_kernel(*f.state));
}

TEST_CASE("canonical identity survives migration promotion and demotion",
	"[population][persons][kernel][membership]") {
	population_materialization_tests::fixture f;
	person_kernel_tests::initialize_runtime(*f.state);
	REQUIRE(persons::register_population_cell(*f.state, f.pop, f.home));
	persons::person_key key{uint32_t(f.pop.index()) + 1u, 0};
	auto profile = persons::materialize_profile(*f.state, key);
	REQUIRE(profile);

	auto destination_province = f.state->world.create_province();
	auto destination_home = f.state->world.create_site();
	f.state->world.force_create_site_location(destination_home, destination_province);
	auto promoted_type = f.state->world.create_pop_type();
	auto promoted_pop = person_kernel_tests::make_population(*f.state,
		destination_province, destination_home, promoted_type, f.culture, f.religion);
	auto demoted_type = f.state->world.create_pop_type();
	auto demoted_pop = person_kernel_tests::make_population(*f.state,
		f.province, f.home, demoted_type, f.culture, f.religion);

	REQUIRE(persons::transfer_population_membership(*f.state, key, promoted_pop,
		persons::population_transition_cause::internal_migration));
	REQUIRE(persons::canonical_key(*f.state, profile) == key);
	REQUIRE(persons::current_population(*f.state, key) == promoted_pop);
	REQUIRE(persons::home_site(*f.state, key) == destination_home);
	REQUIRE(persons::pop_type(*f.state, key) == promoted_type);
	REQUIRE(f.state->world.person_get_source_pop_type(profile) == f.pop_type);
	REQUIRE(f.state->world.person_get_site_from_person_home_site(profile) == destination_home);

	REQUIRE(persons::transfer_population_membership(*f.state, key, demoted_pop,
		persons::population_transition_cause::promotion));
	REQUIRE(persons::canonical_key(*f.state, profile) == key);
	REQUIRE(persons::current_population(*f.state, key) == demoted_pop);
	REQUIRE(persons::home_site(*f.state, key) == f.home);
	REQUIRE(persons::pop_type(*f.state, key) == demoted_type);

	REQUIRE(persons::transfer_population_membership(*f.state, key, f.pop,
		persons::population_transition_cause::demotion));
	REQUIRE(persons::canonical_key(*f.state, profile) == key);
	REQUIRE(persons::current_population(*f.state, key) == f.pop);
	REQUIRE(persons::pop_type(*f.state, key) == f.pop_type);
	REQUIRE(persons::validate_person_kernel(*f.state));
}

TEST_CASE("canonical death closes labor relations once and preserves account history",
	"[population][persons][kernel][labor][death]") {
	individual_concrete_labor_tests::fixture f;
	person_kernel_tests::initialize_runtime(*f.state);
	auto worker = exact_person_economy_tests::register_anchor(f);
	auto offer = f.offer(1, 25.0f);
	auto application = economy::exact_person_economy::submit_application(*f.state,
		worker, offer, f.state->current_date);
	REQUIRE(application != 0);
	economy::exact_person_economy::process_pending_applications(*f.state);
	REQUIRE(economy::exact_person_economy::application(*f.state, application)->status
		== economy::exact_person_economy::application_status::accepted);
	REQUIRE(economy::exact_person_economy::active_contracts_for_person(*f.state, worker).size() == 1);
	economy::payroll::settle_factory(*f.state, f.factory, 1.0f, 1.0f);
	auto contract_id = economy::exact_person_economy::active_contracts_for_person(*f.state, worker).front();
	auto contract = economy::exact_person_economy::contract(*f.state, contract_id);
	REQUIRE(contract);
	auto worker_account = economy::exact_person_economy::account_ref::from_exact(contract->worker_account_id);
	auto transaction = economy::exact_person_economy::latest_transaction(*f.state);
	REQUIRE(transaction);
	REQUIRE(transaction->destination == worker_account);
	REQUIRE(economy::exact_person_economy::balance(*f.state, worker_account) == Approx(25.0f));

	auto pending_worker = exact_person_economy_tests::register_anchor(f);
	auto pending_offer = f.offer(1, 5.0f);
	auto pending_application = economy::exact_person_economy::submit_application(*f.state,
		pending_worker, pending_offer, f.state->current_date);
	REQUIRE(pending_application != 0);
	REQUIRE(persons::kill_person(*f.state, pending_worker, f.state->current_date,
		persons::death_cause::natural));
	REQUIRE(economy::exact_person_economy::application(*f.state, pending_application)->status
		== economy::exact_person_economy::application_status::withdrawn);

	REQUIRE(persons::kill_person(*f.state, worker, f.state->current_date,
		persons::death_cause::natural));
	REQUIRE_FALSE(persons::alive(*f.state, worker));
	REQUIRE_FALSE(persons::current_population(*f.state, worker));
	REQUIRE_FALSE(persons::kill_person(*f.state, worker, f.state->current_date,
		persons::death_cause::natural));
	REQUIRE(economy::exact_person_economy::active_contracts_for_person(*f.state, worker).empty());
	auto ended_contract = economy::exact_person_economy::contract(*f.state, contract_id);
	REQUIRE(ended_contract);
	REQUIRE(ended_contract->status == economy::exact_person_economy::contract_status::terminated);
	REQUIRE(ended_contract->end_date == f.state->current_date);
	REQUIRE(economy::physical::labor_dynamics::separation_event_count(*f.state) == 1);
	REQUIRE(economy::exact_person_economy::account_exists(*f.state, worker_account));
	REQUIRE(economy::exact_person_economy::owner_of(*f.state, worker_account) == worker);
	REQUIRE(economy::exact_person_economy::balance(*f.state, worker_account) == Approx(25.0f));
	REQUIRE(economy::exact_person_economy::transaction(*f.state, transaction->id));
	REQUIRE(economy::exact_person_economy::transaction_count(*f.state) == 1);
	REQUIRE(persons::validate_person_kernel(*f.state));
}

TEST_CASE("death closes a materialized office holder tenure and profile mirror",
	"[population][persons][kernel][governance][death]") {
	population_materialization_tests::fixture f;
	person_kernel_tests::initialize_runtime(*f.state);
	REQUIRE(persons::register_population_cell(*f.state, f.pop, f.home));
	persons::person_key key{uint32_t(f.pop.index()) + 1u, 0};
	auto profile = persons::materialize_profile(*f.state, key);
	auto nation = f.state->world.create_nation();
	auto institution = governance::create_institution(*f.state, nation,
		governance::institution_kind::central_government);
	auto office = governance::create_office(*f.state, institution,
		governance::office_kind::finance_minister);
	auto tenure = persons::appoint_person(*f.state, profile, office, f.state->current_date);
	REQUIRE(tenure);

	REQUIRE(persons::kill_person(*f.state, key, f.state->current_date,
		persons::death_cause::scripted));
	REQUIRE_FALSE(persons::alive(*f.state, key));
	REQUIRE_FALSE(persons::alive(*f.state, profile));
	REQUIRE(f.state->world.person_get_death_date(profile) == f.state->current_date);
	REQUIRE_FALSE(f.state->world.office_tenure_get_active(tenure));
	REQUIRE(f.state->world.office_tenure_get_ended_on(tenure) == f.state->current_date);
	REQUIRE(persons::active_tenure_for(*f.state, office) == dcon::office_tenure_id{});
	REQUIRE(persons::validate_person_kernel(*f.state));
}

TEST_CASE("person snapshot round trip preserves profile mapping and lifecycle checksum",
	"[population][persons][kernel][persistence]") {
	population_materialization_tests::fixture f;
	person_kernel_tests::initialize_runtime(*f.state);
	REQUIRE(persons::register_population_cell(*f.state, f.pop, f.home));
	persons::person_key key{uint32_t(f.pop.index()) + 1u, 0};
	auto moved_home = f.state->world.create_site();
	f.state->world.force_create_site_location(moved_home, f.province);
	REQUIRE(persons::set_home_site(*f.state, key, moved_home));
	auto profile = persons::materialize_profile(*f.state, key);
	REQUIRE(persons::kill_person(*f.state, key, f.state->current_date,
		persons::death_cause::demographic));
	auto checksum = persons::person_kernel_checksum(*f.state);
	auto snapshot = persons::exact_population::export_snapshot(*f.state);

	persons::exact_population::clear_store(*f.state);
	REQUIRE(persons::exact_population::import_snapshot(*f.state, snapshot));
	REQUIRE(persons::person_kernel_checksum(*f.state) == checksum);
	REQUIRE(persons::canonical_key(*f.state, profile) == key);
	REQUIRE(persons::materialized_profile(*f.state, key) == profile);
	REQUIRE_FALSE(persons::alive(*f.state, key));
	REQUIRE_FALSE(persons::alive(*f.state, profile));
	REQUIRE(persons::death_day_index(*f.state, key) == int32_t(f.state->current_date.to_raw_value()) - 1);
	REQUIRE(persons::home_site(*f.state, key) == moved_home);
	REQUIRE(f.state->world.person_get_death_date(profile) == f.state->current_date);
	REQUIRE(persons::validate_person_kernel(*f.state));
}

TEST_CASE("person kernel mapping and lifecycle survive a normal save load",
	"[population][persons][kernel][persistence][serialization]") {
	population_materialization_tests::fixture f;
	person_kernel_tests::initialize_runtime(*f.state);
	nations::strategic_statecraft::initialize(*f.state);
	REQUIRE(persons::register_population_cell(*f.state, f.pop, f.home));
	persons::person_key key{uint32_t(f.pop.index()) + 1u, 0};
	auto profile = persons::materialize_profile(*f.state, key);
	REQUIRE(profile);
	REQUIRE(persons::kill_person(*f.state, key, f.state->current_date,
		persons::death_cause::scripted));
	auto const checksum = persons::person_kernel_checksum(*f.state);

	std::vector<uint8_t> bytes(sys::sizeof_save_section(*f.state));
	auto const* end = sys::write_save_section(bytes.data(), *f.state);
	REQUIRE(end == bytes.data() + bytes.size());

	population_materialization_tests::fixture loaded;
	person_kernel_tests::initialize_runtime(*loaded.state);
	nations::strategic_statecraft::initialize(*loaded.state);
	REQUIRE(persons::register_population_cell(*loaded.state, loaded.pop, loaded.home));
	REQUIRE(persons::materialize_profile(*loaded.state, key));
	sys::read_save_section(bytes.data(), end, *loaded.state);

	auto loaded_profile = persons::materialized_profile(*loaded.state, key);
	REQUIRE(loaded_profile);
	REQUIRE(persons::canonical_key(*loaded.state, loaded_profile) == key);
	REQUIRE_FALSE(persons::alive(*loaded.state, key));
	REQUIRE_FALSE(persons::alive(*loaded.state, loaded_profile));
	REQUIRE(persons::death_day_index(*loaded.state, key)
		== int32_t(loaded.state->current_date.to_raw_value()) - 1);
	REQUIRE(persons::person_kernel_checksum(*loaded.state) == checksum);
	REQUIRE(persons::validate_person_kernel(*loaded.state));
}

TEST_CASE("person checksum normalizes descriptor and profile insertion order",
	"[population][persons][kernel][determinism]") {
	population_materialization_tests::fixture first;
	population_materialization_tests::fixture second;
	person_kernel_tests::initialize_runtime(*first.state);
	person_kernel_tests::initialize_runtime(*second.state);
	auto descriptor = [](sys::state const& state, uint32_t cell) {
		persons::exact_population::cell_descriptor value;
		value.source_population_cell = cell;
		value.literal_count = 4;
		value.bootstrap_base_day = state.current_date.to_raw_value() - 1;
		value.demographic_seed = uint64_t(cell) * 101;
		return value;
	};
	for(auto cell : {50u, 51u})
		REQUIRE(persons::exact_population::register_synthetic_population_cell(*first.state,
			descriptor(*first.state, cell)).result == persons::exact_population::status::created);
	for(auto cell : {51u, 50u})
		REQUIRE(persons::exact_population::register_synthetic_population_cell(*second.state,
			descriptor(*second.state, cell)).result == persons::exact_population::status::created);

	for(auto key : {persons::person_key{50, 1}, persons::person_key{51, 2}})
		REQUIRE(persons::materialize_profile(*first.state, key));
	for(auto key : {persons::person_key{51, 2}, persons::person_key{50, 1}})
		REQUIRE(persons::materialize_profile(*second.state, key));
	REQUIRE(persons::person_kernel_checksum(*first.state)
		== persons::person_kernel_checksum(*second.state));
}

TEST_CASE("sparse profiles map canonical ordinals above the DCON mirror width",
	"[population][persons][kernel][scale]") {
	population_materialization_tests::fixture f;
	person_kernel_tests::initialize_runtime(*f.state);
	persons::exact_population::cell_descriptor descriptor;
	descriptor.source_population_cell = 90;
	descriptor.literal_count = uint64_t(std::numeric_limits<uint32_t>::max()) + 2u;
	descriptor.bootstrap_base_day = f.state->current_date.to_raw_value() - 1;
	descriptor.demographic_seed = 90;
	REQUIRE(persons::exact_population::register_synthetic_population_cell(*f.state,
		descriptor).result == persons::exact_population::status::created);
	persons::person_key key{90, uint64_t(std::numeric_limits<uint32_t>::max()) + 1u};
	auto profile = persons::materialize_profile(*f.state, key);
	REQUIRE(profile);
	REQUIRE(persons::canonical_key(*f.state, profile) == key);
	REQUIRE(persons::materialized_profile(*f.state, key) == profile);
	REQUIRE(persons::validate_person_kernel(*f.state));
	auto checksum = persons::person_kernel_checksum(*f.state);
	auto snapshot = persons::exact_population::export_snapshot(*f.state);
	persons::exact_population::clear_store(*f.state);
	REQUIRE(persons::exact_population::import_snapshot(*f.state, snapshot));
	REQUIRE(persons::canonical_key(*f.state, profile) == key);
	REQUIRE(persons::person_kernel_checksum(*f.state) == checksum);
}

TEST_CASE("orphan canonical-looking DCON profiles never restore identity from stale mirrors",
	"[population][persons][kernel][validation]") {
	population_materialization_tests::fixture f;
	person_kernel_tests::initialize_runtime(*f.state);
	auto orphan = f.state->world.create_person();
	f.state->world.person_set_alive(orphan, uint8_t(1));
	f.state->world.person_set_source_population_cell(orphan, 90);
	f.state->world.person_set_source_population_ordinal(orphan, 1);
	f.state->world.person_set_birth_day_known(orphan, uint8_t(1));
	f.state->world.person_set_birth_day_index(orphan, -100);
	REQUIRE(persons::canonical_key(*f.state, orphan).source_population_cell == 0);
	REQUIRE_FALSE(persons::alive(*f.state, orphan));
	REQUIRE_FALSE(persons::has_birth_day(*f.state, orphan));
	REQUIRE(persons::birth_day_index(*f.state, orphan) == 0);
	REQUIRE_FALSE(persons::mark_dead(*f.state, orphan, f.state->current_date));
}

TEST_CASE("person snapshot rejects missing membership and duplicate profile bridges",
	"[population][persons][kernel][validation]") {
	population_materialization_tests::fixture f;
	person_kernel_tests::initialize_runtime(*f.state);
	REQUIRE(persons::register_population_cell(*f.state, f.pop, f.home));
	persons::person_key first_key{uint32_t(f.pop.index()) + 1u, 0};
	persons::person_key second_key{uint32_t(f.pop.index()) + 1u, 1};
	REQUIRE(persons::materialize_profile(*f.state, first_key));
	REQUIRE(persons::materialize_profile(*f.state, second_key));
	auto valid = persons::exact_population::export_snapshot(*f.state);
	REQUIRE(persons::validate_person_kernel(*f.state));

	auto missing_membership = valid;
	missing_membership.membership_ranges.clear();
	persons::exact_population::clear_store(*f.state);
	REQUIRE_FALSE(persons::exact_population::import_snapshot(*f.state, missing_membership));
	REQUIRE_FALSE(f.state->exact_population);

	auto duplicate_bridge = valid;
	REQUIRE(duplicate_bridge.bridges.size() == 2);
	duplicate_bridge.bridges[1].profile = duplicate_bridge.bridges[0].profile;
	REQUIRE_FALSE(persons::exact_population::import_snapshot(*f.state, duplicate_bridge));
	REQUIRE_FALSE(f.state->exact_population);
}
