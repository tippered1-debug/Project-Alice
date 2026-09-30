#include "catch.hpp"

#include "actors/organizations/organizations.hpp"
#include "compat/alice/legacy_bridge.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/causal_order.hpp"
#include "economy/economy_stats.hpp"
#include "economy/exact_person_economy.hpp"
#include "economy/firm_agency.hpp"
#include "economy/industrial_production.hpp"
#include "economy/physical/factory_inputs.hpp"
#include "economy/physical/exact_person_freight.hpp"
#include "economy/physical/exact_person_goods.hpp"
#include "economy/physical/labor_dynamics.hpp"
#include "economy/price.hpp"
#include "gamestate/serialization.hpp"
#include "military/land_forces.hpp"
#include "nations/strategic_statecraft.hpp"
#include "persons/exact_population.hpp"
#include "persons/persons.hpp"
#include "system_state.hpp"
#include "technology/technology_kernel.hpp"
#include "system_state.hpp"

#include <algorithm>
#include <vector>

namespace technology_kernel_tests {

void initialize_runtime(sys::state& state) {
	if(!state.exact_population) persons::exact_population::initialize_empty_store(state);
	if(!state.causal_order) economy::causal_order::initialize_empty_store(state);
	if(!state.exact_person_economy) economy::exact_person_economy::initialize_empty_store(state);
	if(!state.exact_person_goods) economy::physical::exact_person_goods::initialize_empty_store(state);
	if(!state.exact_person_freight) economy::physical::exact_person_freight::initialize_empty_store(state);
	if(!state.labor_dynamics) economy::physical::labor_dynamics::initialize_empty_store(state);
	if(!military::land_forces::initialized(state)) military::land_forces::initialize_empty_store(state);
}

struct fixture {
	std::unique_ptr<sys::state> state = std::make_unique<sys::state>();
	dcon::province_id province{};
	dcon::state_instance_id zone{};
	dcon::market_id market{};
	dcon::commodity_id settlement{};
	dcon::commodity_id input{};
	dcon::commodity_id output{};
	dcon::factory_type_id type{};
	dcon::factory_id factory{};
	dcon::site_id first_site{};
	dcon::economic_actor_id first_actor{};
	dcon::monetary_account_id first_account{};
	dcon::nation_id nation{};
	dcon::organization_id first_firm{};
	dcon::organization_id second_firm{};
	dcon::factory_id second_factory{};
	dcon::organization_id laboratory{};
	dcon::site_id laboratory_site{};
	dcon::monetary_account_id laboratory_account{};
	dcon::pop_id researchers{};
	persons::person_key researcher{};
	dcon::technology_id legacy_technology{};
	technology::kernel::stable_id first_firm_id = 0;
	technology::kernel::stable_id second_firm_id = 0;
	technology::kernel::stable_id laboratory_id = 0;
	technology::kernel::stable_id capability_id = 0;
	technology::kernel::stable_id program_id = 0;

	fixture() {
		province = state->world.create_province();
		zone = state->world.create_state_instance();
		market = state->world.create_market();
		state->world.state_instance_set_market_from_local_market(zone, market);
		state->world.market_set_zone_from_local_market(market, zone);
		state->world.province_set_state_membership(province, zone);
		settlement = state->world.create_commodity();
		input = state->world.create_commodity();
		output = state->world.create_commodity();
		for(auto commodity : {input, output}) {
			state->world.commodity_set_is_local(commodity, false);
			state->world.commodity_set_money_rgo(commodity, false);
		}
		state->world.market_resize_price(state->world.commodity_size());
		state->world.commodity_set_cost(input, 2.0f);
		state->world.commodity_set_cost(output, 10.0f);
		state->world.market_set_price(market, input, 2.0f);
		state->world.market_set_price(market, output, 10.0f);
		type = state->world.create_factory_type();
		state->world.factory_type_set_output(type, output);
		state->world.factory_type_set_output_amount(type, 1.0f);
		state->world.factory_type_set_base_workforce(type, 10.0f);
		economy::commodity_set recipe{};
		recipe.commodity_type[0] = input;
		recipe.commodity_amounts[0] = 1.0f;
		state->world.factory_type_set_inputs(type, recipe);
		factory = state->world.create_factory();
		state->world.factory_set_building_type(factory, type);
		state->world.factory_set_size(factory, 10.0f);
		state->world.factory_set_productive_capacity(factory, 10.0f);
		state->world.factory_set_productivity_factor(factory, 1.0f);
		state->world.factory_set_unqualified_employment(factory, 10.0f);
		state->world.force_create_factory_location(factory, province);
		compat::alice::bootstrap_factory_sites(*state);
		first_site = world::site::site_for_factory(*state, factory);
		first_firm = actors::organizations::create_company(*state);
		REQUIRE(actors::organizations::bind_factory_operator(*state, first_firm, factory));
		first_actor = actors::organizations::actor_for_organization(*state, first_firm);
		first_account = economy::accounts::open_account(*state, first_actor, settlement);
		REQUIRE(economy::accounts::bootstrap_set_balance(*state, first_account, 1000.0f));
		state->world.factory_set_payroll_settlement(factory, settlement);
		state->world.province_resize_labor_price(economy::labor::total);
		state->world.province_resize_labor_demand_satisfaction(economy::labor::total);
		state->world.province_set_labor_price(province, economy::labor::no_education, 1.0f);
		state->world.province_set_labor_price(province, economy::labor::basic_education, 1.0f);
		state->world.province_set_labor_price(province, economy::labor::high_education, 1.0f);

		state->current_date = sys::date{30000};
		initialize_runtime(*state);
		nations::strategic_statecraft::initialize(*state);
		technology::kernel::initialize_empty_store(*state);

		nation = state->world.create_nation();
		state->world.force_create_province_ownership(province, nation);
		legacy_technology = state->world.create_technology();
		state->world.nation_resize_active_technologies(state->world.technology_size());
		state->world.nation_set_active_technologies(nation, legacy_technology, true);

		first_firm_id = technology::kernel::stable_id_for("organization", "first_firm");
		state->world.organization_set_canonical_id(first_firm, first_firm_id);

		second_firm = actors::organizations::create_company(*state);
		second_firm_id = technology::kernel::stable_id_for("organization", "second_firm");
		state->world.organization_set_canonical_id(second_firm, second_firm_id);
		second_factory = state->world.create_factory();
		state->world.factory_set_building_type(second_factory, state->world.factory_get_building_type(factory));
		state->world.factory_set_size(second_factory, 10.0f);
		state->world.factory_set_productive_capacity(second_factory, 10.0f);
		state->world.factory_set_productivity_factor(second_factory, 1.0f);
		state->world.factory_set_unqualified_employment(second_factory, 10.0f);
		state->world.force_create_factory_location(second_factory, province);
		REQUIRE(actors::organizations::bind_factory_operator(*state, second_firm, second_factory));
		compat::alice::bootstrap_factory_sites(*state);
		state->world.factory_set_payroll_settlement(second_factory, settlement);
		auto second_account = economy::accounts::open_account(*state,
			actors::organizations::actor_for_organization(*state, second_firm), settlement);
		economy::accounts::bootstrap_set_balance(*state, second_account, 1000.0f);

		laboratory = actors::organizations::create_company(*state);
		laboratory_id = technology::kernel::stable_id_for("organization", "public_laboratory");
		state->world.organization_set_canonical_id(laboratory, laboratory_id);
		laboratory_site = state->world.create_site();
		state->world.force_create_site_location(laboratory_site, province);
		state->world.site_set_canonical_id(laboratory_site,
			technology::kernel::stable_id_for("site", "public_laboratory_site"));
		laboratory_account = economy::accounts::open_account(*state,
			actors::organizations::actor_for_organization(*state, laboratory), settlement);
		state->world.monetary_account_set_canonical_id(laboratory_account,
			technology::kernel::stable_id_for("account", "public_laboratory_funding"));
		economy::accounts::bootstrap_set_balance(*state, laboratory_account, 0.0f);

		capability_id = technology::kernel::stable_id_for("capability", "test_process_knowledge");
		program_id = technology::kernel::stable_id_for("research-program", "test_program");

		researchers = state->world.create_pop();
		state->world.force_create_pop_location(researchers, province);
		state->world.pop_set_poptype(researchers, state->world.create_pop_type());
		state->world.pop_set_culture(researchers, state->world.create_culture());
		state->world.pop_set_religion(researchers, state->world.create_religion());
		state->world.pop_set_size(researchers, 3.0f);
		REQUIRE(persons::register_population_cell(*state, researchers, laboratory_site));
		for(uint64_t ordinal = 0; ordinal < 12; ++ordinal) {
			persons::person_key candidate{uint32_t(researchers.index()) + 1u, ordinal};
			if(economy::exact_person_economy::is_work_eligible(*state, candidate)) {
				researcher = candidate;
				break;
			}
		}
		REQUIRE(researcher.source_population_cell != 0);
	}

	technology::kernel::snapshot scenario(bool assign_staff = true, float required_effort = 300.0f) const {
		technology::kernel::snapshot value;
		value.canonical_runtime_active = 1;
		value.capabilities.push_back({capability_id,
			technology::kernel::stable_id_for("domain", "industrial_process"), required_effort, 1, 1});
		value.factory_processes.push_back({capability_id,
			technology::kernel::stable_id_for("factory-process", "test_factory"),
			state->world.factory_get_building_type(factory)});
		value.organizations.push_back({laboratory_id, laboratory, laboratory_site,
			laboratory_account, technology::kernel::research_role::public_laboratory, 1.0f});
		value.programs.push_back({program_id, laboratory_id, capability_id,
			technology::kernel::program_status::planned, {}, 0.0f, required_effort, 1.0f});
		if(assign_staff) value.assignments.push_back({program_id, researcher, 1.0f, 1.0f});
		return value;
	}

	void advance(uint32_t days) {
		for(uint32_t day = 0; day < days; ++day) {
			technology::kernel::update_daily(*state);
			state->current_date += 1;
		}
	}
};

} // namespace technology_kernel_tests

TEST_CASE("funded research requires exact-person staff and pays progress from its account",
	"[technology][research][funding][persons]") {
	technology_kernel_tests::fixture f;
	auto value = f.scenario(false, 2.0f);
	REQUIRE(technology::kernel::install_scenario_state(*f.state, value));
	REQUIRE(economy::accounts::bootstrap_set_balance(*f.state, f.laboratory_account, 10.0f));
	f.advance(1);
	REQUIRE(technology::kernel::export_snapshot(*f.state).programs.front().effort_accumulated == Approx(0.0f));

	value.assignments.push_back({f.program_id, f.researcher, 1.0f, 1.0f});
	REQUIRE(technology::kernel::install_scenario_state(*f.state, value));
	REQUIRE(economy::accounts::bootstrap_set_balance(*f.state, f.laboratory_account, 0.0f));
	f.advance(1);
	REQUIRE(technology::kernel::export_snapshot(*f.state).programs.front().effort_accumulated == Approx(0.0f));

	REQUIRE(economy::accounts::bootstrap_set_balance(*f.state, f.laboratory_account, 10.0f));
	f.advance(2);
	auto result = technology::kernel::export_snapshot(*f.state);
	REQUIRE(result.programs.front().effort_accumulated == Approx(2.0f));
	REQUIRE(result.programs.front().status == technology::kernel::program_status::completed);
	REQUIRE(economy::accounts::balance(*f.state, f.laboratory_account) == Approx(8.0f));
	auto researcher_account = economy::exact_person_economy::find_account(*f.state, f.researcher, f.settlement);
	REQUIRE(researcher_account);
	REQUIRE(economy::exact_person_economy::balance(*f.state, researcher_account) == Approx(2.0f));
	REQUIRE(technology::kernel::organization_holds(*f.state, f.laboratory_id, f.capability_id));
	REQUIRE_FALSE(technology::kernel::organization_holds(*f.state, f.first_firm_id, f.capability_id));
	REQUIRE_FALSE(technology::kernel::organization_holds(*f.state, f.second_firm_id, f.capability_id));
}

TEST_CASE("knowledge transfer and adoption are organization-specific factory gates",
	"[technology][adoption][industry][legacy]") {
	technology_kernel_tests::fixture f;
	auto value = f.scenario();
	REQUIRE(technology::kernel::install_scenario_state(*f.state, value));
	REQUIRE_FALSE(technology::kernel::organization_can_operate_factory_type(*f.state,
		f.first_firm, f.state->world.factory_get_building_type(f.factory)));
	REQUIRE(technology::kernel::factory_process_has_canonical_requirement(*f.state,
		f.state->world.factory_get_building_type(f.factory)));
	REQUIRE(economy::firm_agency::decide_factory(*f.state, f.factory).desired_units == Approx(0.0f));
	REQUIRE(economy::firm_agency::decide_factory(*f.state, f.second_factory).desired_units == Approx(0.0f));
	REQUIRE(economy::industrial_production::produce_factory(*f.state, f.factory) == Approx(0.0f));
	REQUIRE(f.state->world.factory_get_output(f.factory) == Approx(0.0f));
	REQUIRE_FALSE(technology::kernel::organization_can_operate_factory_type(*f.state,
		f.first_firm, f.state->world.factory_get_building_type(f.factory)));

	REQUIRE(technology::kernel::add_initial_holder(*f.state, {f.laboratory_id, f.capability_id, 1.0f}));
	auto authorization = technology::kernel::stable_id_for("authorized-relation", "license_01");
	REQUIRE(technology::kernel::transfer_capability(*f.state, f.laboratory_id,
		f.second_firm_id, f.capability_id, f.state->current_date, authorization));
	REQUIRE(technology::kernel::adopt_capability(*f.state, f.second_firm_id,
		f.capability_id, f.state->current_date));
	REQUIRE(technology::kernel::organization_can_operate_factory_type(*f.state,
		f.second_firm, f.state->world.factory_get_building_type(f.second_factory)));
	REQUIRE(economy::firm_agency::decide_factory(*f.state, f.second_factory).desired_units > 0.0f);
	REQUIRE_FALSE(technology::kernel::organization_holds(*f.state, f.first_firm_id, f.capability_id));
	REQUIRE_FALSE(technology::kernel::organization_adopted(*f.state, f.first_firm_id, f.capability_id));
}

TEST_CASE("technology snapshots normalize insertion order and reject broken references or allocations",
	"[technology][validation][determinism]") {
	technology_kernel_tests::fixture f;
	auto value = f.scenario();
	auto auxiliary_capability = technology::kernel::stable_id_for("capability", "auxiliary");
	value.capabilities.push_back({auxiliary_capability,
		technology::kernel::stable_id_for("domain", "industrial_process"), 10.0f, 0, 0});
	value.prerequisites.push_back({f.capability_id, auxiliary_capability});
	REQUIRE(technology::kernel::install_scenario_state(*f.state, value));
	auto checksum = technology::kernel::deterministic_checksum(*f.state);
	auto reverse = value;
	std::reverse(reverse.capabilities.begin(), reverse.capabilities.end());
	std::reverse(reverse.prerequisites.begin(), reverse.prerequisites.end());
	std::reverse(reverse.factory_processes.begin(), reverse.factory_processes.end());
	std::reverse(reverse.organizations.begin(), reverse.organizations.end());
	std::reverse(reverse.programs.begin(), reverse.programs.end());
	std::reverse(reverse.assignments.begin(), reverse.assignments.end());
	REQUIRE(technology::kernel::import_snapshot(*f.state, reverse));
	REQUIRE(technology::kernel::deterministic_checksum(*f.state) == checksum);

	auto invalid = value;
	invalid.prerequisites.front().prerequisite = technology::kernel::stable_id_for("capability", "missing");
	REQUIRE_FALSE(technology::kernel::import_snapshot(*f.state, invalid));
	REQUIRE(technology::kernel::install_scenario_state(*f.state, value));
	auto inconsistent_completed = value;
	inconsistent_completed.programs.front().status = technology::kernel::program_status::completed;
	REQUIRE_FALSE(technology::kernel::import_snapshot(*f.state, inconsistent_completed));
	auto full_effort_active = value;
	full_effort_active.programs.front().effort_accumulated = full_effort_active.programs.front().required_effort;
	REQUIRE_FALSE(technology::kernel::import_snapshot(*f.state, full_effort_active));
	REQUIRE(technology::kernel::install_scenario_state(*f.state, value));
	auto overallocated = value;
	auto second_program = overallocated.programs.front();
	second_program.id = technology::kernel::stable_id_for("research-program", "second_program");
	second_program.capability = auxiliary_capability;
	second_program.required_effort = 10.0f;
	overallocated.programs.push_back(second_program);
	overallocated.assignments.push_back({second_program.id, f.researcher, 0.5f, 1.0f});
	overallocated.assignments.front().allocation_fraction = 0.75f;
	REQUIRE_FALSE(technology::kernel::import_snapshot(*f.state, overallocated));
}

TEST_CASE("dead assigned researcher stops contributing to a live program",
	"[technology][research][persons][death]") {
	technology_kernel_tests::fixture f;
	REQUIRE(technology::kernel::install_scenario_state(*f.state, f.scenario(true, 100.0f)));
	f.advance(1);
	auto before_death = technology::kernel::export_snapshot(*f.state).programs.front().effort_accumulated;
	REQUIRE(before_death == Approx(1.0f));
	REQUIRE(persons::kill_person(*f.state, f.researcher, f.state->current_date,
		persons::death_cause::natural));
	f.advance(1);
	auto after_death = technology::kernel::export_snapshot(*f.state);
	REQUIRE(after_death.programs.front().effort_accumulated == Approx(before_death));
	REQUIRE(after_death.assignments.empty());
	std::vector<std::string> errors;
	REQUIRE(technology::kernel::validate_canonical_technology_state(*f.state, errors));
}

TEST_CASE("technology save and replay preserve 90 day progress exactly",
	"[technology][persistence][replay][determinism]") {
	auto add_opening_holder = [](technology_kernel_tests::fixture& state_fixture,
		technology::kernel::snapshot& value) {
		auto auxiliary = technology::kernel::stable_id_for("capability", "opening_holder_capability");
		value.capabilities.push_back({auxiliary,
			technology::kernel::stable_id_for("domain", "industrial_process"), 25.0f, 1, 1});
		REQUIRE(technology::kernel::install_scenario_state(*state_fixture.state, value));
		REQUIRE(technology::kernel::add_initial_holder(*state_fixture.state,
			{state_fixture.first_firm_id, auxiliary, 0.75f}));
	};
	technology_kernel_tests::fixture uninterrupted;
	auto uninterrupted_scenario = uninterrupted.scenario(true, 500.0f);
	add_opening_holder(uninterrupted, uninterrupted_scenario);
	REQUIRE(economy::accounts::bootstrap_set_balance(*uninterrupted.state,
		uninterrupted.laboratory_account, 500.0f));
	uninterrupted.advance(180);
	auto uninterrupted_checksum = technology::kernel::deterministic_checksum(*uninterrupted.state);

	technology_kernel_tests::fixture first_half;
	auto first_half_scenario = first_half.scenario(true, 500.0f);
	add_opening_holder(first_half, first_half_scenario);
	REQUIRE(economy::accounts::bootstrap_set_balance(*first_half.state,
		first_half.laboratory_account, 500.0f));
	first_half.advance(90);
	auto mid_snapshot = technology::kernel::export_snapshot(*first_half.state);
	REQUIRE(mid_snapshot.programs.front().effort_accumulated == Approx(90.0f));
	REQUIRE(technology::kernel::organization_holds(*first_half.state, first_half.first_firm_id,
		technology::kernel::stable_id_for("capability", "opening_holder_capability")));

	std::vector<uint8_t> bytes(sys::sizeof_save_section(*first_half.state));
	auto const* end = sys::write_save_section(bytes.data(), *first_half.state);
	REQUIRE(end == bytes.data() + bytes.size());

	technology_kernel_tests::fixture restored;
	// The test scenario and DCON object creation order are identical, so the
	// authored organization/site/account references resolve to the same records.
	auto setup = restored.scenario(true, 500.0f);
	add_opening_holder(restored, setup);
	REQUIRE(economy::accounts::bootstrap_set_balance(*restored.state,
		restored.laboratory_account, 500.0f));
	auto const* read_end = sys::read_save_section(bytes.data(), end, *restored.state);
	REQUIRE(read_end == end);
	restored.state->current_date = first_half.state->current_date;
	REQUIRE(technology::kernel::deterministic_checksum(*restored.state)
		== technology::kernel::deterministic_checksum(*first_half.state));
	REQUIRE(technology::kernel::organization_holds(*restored.state, restored.first_firm_id,
		technology::kernel::stable_id_for("capability", "opening_holder_capability")));
	for(uint32_t day = 0; day < 90; ++day) {
		technology::kernel::update_daily(*restored.state);
		technology::kernel::update_daily(*first_half.state);
		restored.state->current_date += 1;
		first_half.state->current_date += 1;
	}
	REQUIRE(technology::kernel::deterministic_checksum(*restored.state) == uninterrupted_checksum);
	REQUIRE(technology::kernel::deterministic_checksum(*first_half.state) == uninterrupted_checksum);
}
