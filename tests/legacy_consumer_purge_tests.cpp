#define CATCH_CONFIG_DISABLE_EXCEPTIONS 1
#include "catch2/catch.hpp"
#include "economy/payroll.hpp"
#include "economy/physical/inventory.hpp"
#include "economy/physical/concrete_market.hpp"
#include "governance/finance/finance.hpp"
#include "canonical_consumer_fixture.hpp"
#include "economy/economy_pops.hpp"
#include "economy/demographics_templates.hpp"
#include "economy/physical/household_mobility.hpp"
#include "scripting/fif_common.hpp"
#include "gamestate/serialization.hpp"

namespace legacy_consumer_purge_tests {
using namespace economy;
using namespace economy::physical;

struct fixture : canonical_consumer_tests::fixture {
	persons::person_key consumer{};
	dcon::pop_id population{};
	dcon::pop_type_id profile{};
	dcon::economic_actor_id seller{};
	dcon::monetary_account_id seller_account{};
	exact_person_economy::account_ref cash{};
	std::array<dcon::commodity_id, 3> goods{};
	fixture() {
		profile = state->world.create_pop_type();
		goods = {output, state->world.create_commodity(), state->world.create_commodity()};
		state->world.pop_type_resize_life_needs(state->world.commodity_size());
		state->world.pop_type_resize_everyday_needs(state->world.commodity_size());
		state->world.pop_type_resize_luxury_needs(state->world.commodity_size());
		state->world.market_resize_life_needs_weights(state->world.commodity_size());
		state->world.market_resize_everyday_needs_weights(state->world.commodity_size());
		state->world.market_resize_luxury_needs_weights(state->world.commodity_size());
		state->world.market_resize_expected_probability_to_buy(state->world.commodity_size());
		state->world.pop_type_set_life_needs(profile, goods[0], 1.0f);
		state->world.pop_type_set_everyday_needs(profile, goods[1], 1.0f);
		state->world.pop_type_set_luxury_needs(profile, goods[2], 1.0f);
		population = state->world.create_pop();
		state->world.pop_set_poptype(population, profile);
		state->world.pop_set_size(population, 0.25f);
		state->world.force_create_pop_location(population, province);
		REQUIRE(persons::register_population_cell(*state, population, site));
		consumer = {uint32_t(population.index()) + 1u, 0};
		cash = exact_person_economy::open_account(*state, consumer, settlement);
		seller = state->world.create_economic_actor();
		seller_account = accounts::open_account(*state, seller, settlement);
		REQUIRE(accounts::bootstrap_set_balance(*state, seller_account, 0.0f));
		for(auto c : goods) {
			state->world.commodity_set_cost(c, 10.0f);
			REQUIRE(exact_person_goods::set_need(*state, consumer, c, 1.0f));
			state->world.market_set_life_needs_weights(market, c, 1.0f);
			state->world.market_set_everyday_needs_weights(market, c, 1.0f);
			state->world.market_set_luxury_needs_weights(market, c, 1.0f);
		}
		REQUIRE(exact_person_goods::mark_need_profile_imported(*state, consumer));
	}
	void stock_and_offer(dcon::commodity_id c, float amount = 1.0f) {
		REQUIRE(inventory::add(*state, site, c, amount, seller) == Approx(amount));
		REQUIRE(concrete_market::post_ask(*state, seller, site, market, c, amount, 10.0f, {}));
	}
	void corrupt(int fields) {
		if(fields & 1) state->world.pop_set_savings(population, -999999.0f);
		if(fields & 2) state->world.pop_set_satisfaction(population, std::numeric_limits<float>::quiet_NaN());
		if(fields & 4) for(auto c : goods) {
			state->world.market_set_life_needs_weights(market, c, std::numeric_limits<float>::quiet_NaN());
			state->world.market_set_everyday_needs_weights(market, c, -999999.0f);
			state->world.market_set_luxury_needs_weights(market, c, std::numeric_limits<float>::infinity());
			state->world.market_set_expected_probability_to_buy(market, c, 0.0f);
		}
	}
	uint64_t hire(float wage) {
		auto vacancy = offer(1, wage);
		REQUIRE(vacancy);
		REQUIRE(exact_person_economy::is_work_eligible(*state, consumer));
		REQUIRE(exact_person_economy::is_labor_force_participant(*state, consumer));
		REQUIRE(exact_person_economy::submit_application(*state, consumer, vacancy, state->current_date));
		exact_person_economy::process_pending_applications(*state);
		REQUIRE(exact_person_economy::person_has_active_contract(*state, consumer));
		return exact_person_economy::active_contracts_for_person(*state, consumer).front();
	}
};
}

TEST_CASE("legacy POP corruption cannot change the canonical household checksum over multiple days",
	"[economy][exact][consumer-purge][determinism]") {
	using namespace legacy_consumer_purge_tests;
	int fields = 0;
	SECTION("POP savings") { fields = 1; }
	SECTION("all legacy satisfaction bars") { fields = 2; }
	SECTION("all market need weights and utility probability") { fields = 4; }
	SECTION("critical combined hard cut") { fields = 7; }
	fixture control, poisoned;
	control.hire(12.0f); poisoned.hire(12.0f);
	for(int day = 0; day < 12; ++day) {
		control.state->current_date += 1; poisoned.state->current_date += 1;
		poisoned.corrupt(fields);
		for(auto f : {&control, &poisoned}) {
			payroll::settle_factory(*f->state, f->factory, 1.0f, 1.0f);
			household_mobility::update_employed_households(*f->state);
			for(auto c : f->goods) f->stock_and_offer(c);
			exact_person_goods::process_daily(*f->state);
			REQUIRE(exact_person_economy::project_population_cash_balances(*f->state));
			REQUIRE(exact_person_goods::validate_canonical_household_economy(*f->state));
		}
		REQUIRE(exact_person_goods::canonical_household_checksum(*control.state)
			== exact_person_goods::canonical_household_checksum(*poisoned.state));
		REQUIRE(control.state->world.pop_get_savings(control.population)
			== Approx(exact_person_economy::balance(*control.state, control.cash)));
	}
}

TEST_CASE("exact balances bound quantities and concrete wages restore purchasing power",
	"[economy][exact][consumer-purge][causal]") {
	using namespace legacy_consumer_purge_tests;
	fixture f;
	REQUIRE(exact_person_economy::set_balance(*f.state, f.cash, 2.5f));
	f.corrupt(7);
	f.stock_and_offer(f.output, 3.0f);
	exact_person_goods::process_daily(*f.state);
	REQUIRE(exact_person_goods::fill_count(*f.state) == 1);
	REQUIRE(exact_person_goods::fill(*f.state, 1)->quantity == Approx(0.25f));
	REQUIRE(exact_person_economy::balance(*f.state, f.cash) == Approx(0.0f));
	REQUIRE(exact_person_goods::consumed_this_period(*f.state, f.consumer, f.output) == Approx(0.25f));
	REQUIRE(exact_person_goods::stock_quantity(*f.state, f.consumer, f.site, f.output)
		+ inventory::quantity(*f.state, f.site, f.output, f.seller)
		+ exact_person_goods::consumed_this_period(*f.state, f.consumer, f.output) == Approx(3.0f));
	REQUIRE(exact_person_economy::project_population_cash_balances(*f.state));
	REQUIRE(f.state->world.pop_get_savings(f.population) == Approx(0.0f));
	auto contract = f.hire(5.0f);
	f.state->current_date += 1;
	REQUIRE(exact_person_economy::settle_contract_wage(*f.state, contract).paid == Approx(5.0f));
	exact_person_goods::process_daily(*f.state);
	REQUIRE(exact_person_goods::fill_count(*f.state) == 2);
	REQUIRE(exact_person_goods::fill(*f.state, 2)->quantity == Approx(0.5f));
	REQUIRE(accounts::balance(*f.state, f.seller_account) == Approx(7.5f));
	REQUIRE(exact_person_economy::balance(*f.state, f.cash) == Approx(0.0f));
	REQUIRE(exact_person_goods::validate_canonical_household_economy(*f.state));
}

TEST_CASE("consumer UI projections read exact fills and independent physical consumption bars",
	"[economy][exact][consumer-purge][projection]") {
	using namespace legacy_consumer_purge_tests;
	fixture f;
	REQUIRE(exact_person_economy::set_balance(*f.state, f.cash, 25.0f));
	for(auto c : f.goods) f.stock_and_offer(c);
	exact_person_goods::process_daily(*f.state);
	auto before = exact_person_goods::canonical_household_checksum(*f.state);
	f.corrupt(7);
	auto view = pops::project_consumption(*f.state, f.population);
	REQUIRE(view.cash == Approx(0.0f));
	REQUIRE(view.life_needs.spent == Approx(10.0f));
	REQUIRE(view.everyday_needs.spent == Approx(10.0f));
	REQUIRE(view.luxury_needs.spent == Approx(5.0f));
	REQUIRE(view.capital_contributed == Approx(0.0f));
	REQUIRE(view.spent_total == Approx(25.0f));
	REQUIRE(pop_demographics::get_life_needs(*f.state, f.population) == Approx(1.0f));
	REQUIRE(pop_demographics::get_everyday_needs(*f.state, f.population) == Approx(1.0f));
	REQUIRE(pop_demographics::get_luxury_needs(*f.state, f.population) == Approx(0.5f));
	REQUIRE(fif::f_consumption_ratio(f.state.get(), 2, f.population.index()) == Approx(0.5f));
	fif::environment env;
	fif::initialize_standard_vocab(env);
	fif::add_import("consumption-ratio", (void*)fif::f_consumption_ratio, fif::f_consumption_ratio_b,
		{fif::fif_i32, fif::fif_i32, fif::fif_opaque_ptr}, {fif::fif_f32}, env);
	fif::interpreter_stack values;
	values.push_back_main(fif::fif_i32, f.population.index(), nullptr);
	values.push_back_main(fif::fif_i32, 2, nullptr);
	values.push_back_main(fif::fif_opaque_ptr, reinterpret_cast<int64_t>(f.state.get()), nullptr);
	fif::run_fif_interpreter(env, "consumption-ratio", values);
	auto data = values.main_data_back(0);
	float scripted_ratio = 0.0f;
	std::memcpy(&scripted_ratio, &data, sizeof(scripted_ratio));
	REQUIRE(scripted_ratio == Approx(0.5f));
	for(int category = 0; category < 3; ++category)
		REQUIRE(pops::projected_spending(*f.state, f.population, f.goods[category], uint8_t(category)) == Approx(category == 2 ? 5.0f : 10.0f));
	REQUIRE(pops::education_access(*f.state, f.population) == Approx(0.0f));
	REQUIRE(exact_person_goods::canonical_household_checksum(*f.state) == before);
	exact_person_goods::project_population_consumption(*f.state);
	REQUIRE(exact_person_economy::project_population_cash_balances(*f.state));
	REQUIRE(exact_person_goods::canonical_household_checksum(*f.state) == before);
	REQUIRE(f.state->world.pop_get_savings(f.population) == Approx(view.cash));
}

TEST_CASE("imported authored needs cannot be reimported after legacy changes",
	"[economy][exact][consumer-purge][bootstrap]") {
	using namespace legacy_consumer_purge_tests;
	fixture f;
	f.state->world.pop_type_set_life_needs(f.profile, f.output, 10000.0f);
	f.state->world.pop_type_set_everyday_needs(f.profile, f.goods[1], 10000.0f);
	f.state->world.pop_type_set_luxury_needs(f.profile, f.goods[2], 10000.0f);
	f.corrupt(7);
	auto before = exact_person_goods::canonical_household_checksum(*f.state);
	household_mobility::update_employed_households(*f.state);
	for(auto c : f.goods)
		REQUIRE(exact_person_goods::need(*f.state, f.consumer, c)->desired_quantity_per_period == Approx(1.0f));
	REQUIRE(exact_person_goods::canonical_household_checksum(*f.state) == before);
}

TEST_CASE("consumer hard cut replays through normal save and load with poisoned legacy fields",
	"[economy][exact][consumer-purge][serialization][determinism]") {
	using namespace legacy_consumer_purge_tests;
	fixture uninterrupted, split;
	uninterrupted.hire(18.0f); split.hire(18.0f);
	REQUIRE(exact_person_economy::set_labor_force_participation(*uninterrupted.state, uninterrupted.consumer, false));
	REQUIRE(exact_person_economy::set_labor_force_participation(*split.state, split.consumer, false));
	REQUIRE(exact_person_economy::participation_override_count(*split.state) == 1);
	auto advance = [](fixture& f) {
		f.state->current_date += 1;
		f.corrupt(7);
		payroll::settle_factory(*f.state, f.factory, 1.0f, 1.0f);
		for(auto c : f.goods) f.stock_and_offer(c);
		exact_person_goods::process_daily(*f.state);
		REQUIRE(exact_person_goods::validate_canonical_household_economy(*f.state));
	};
	for(int i = 0; i < 8; ++i) { advance(uninterrupted); advance(split); }
	split.corrupt(7);
	std::vector<uint8_t> bytes(sys::sizeof_save_section(*split.state));
	auto end = sys::write_save_section(bytes.data(), *split.state);
	REQUIRE(end == bytes.data() + bytes.size());
	fixture loaded;
	REQUIRE(sys::read_save_section(bytes.data(), end, *loaded.state) == end);
	REQUIRE(loaded.state->exact_person_economy);
	REQUIRE(loaded.state->exact_person_goods);
	REQUIRE(exact_person_goods::canonical_household_checksum(*split.state)
		== exact_person_goods::canonical_household_checksum(*loaded.state));
	for(int i = 0; i < 8; ++i) {
		advance(uninterrupted); advance(loaded);
		REQUIRE(exact_person_goods::canonical_household_checksum(*uninterrupted.state)
			== exact_person_goods::canonical_household_checksum(*loaded.state));
	}
}

TEST_CASE("consumer checksum is independent of account and need insertion order",
	"[economy][exact][consumer-purge][determinism][ordering]") {
	using namespace legacy_consumer_purge_tests;
	auto run = [](bool reverse) {
		canonical_consumer_tests::fixture f;
		std::array<persons::person_key, 2> people{{{9001, 0}, {9002, 0}}};
		for(auto key : people) {
			persons::exact_population::cell_descriptor descriptor;
			descriptor.source_population_cell = key.source_population_cell;
			descriptor.literal_count = 1;
			descriptor.bootstrap_base_day = f.state->current_date.to_raw_value() - 1;
			descriptor.home_site = f.site;
			REQUIRE(persons::exact_population::register_synthetic_population_cell(*f.state, descriptor).result
				== persons::exact_population::status::created);
		}
		if(reverse) std::reverse(people.begin(), people.end());
		for(auto key : people) {
			auto cash = exact_person_economy::open_account(*f.state, key, f.settlement);
			REQUIRE(exact_person_economy::set_balance(*f.state, cash, 5.0f));
			REQUIRE(exact_person_goods::set_need(*f.state, key, f.output, 1.0f));
		}
		REQUIRE(inventory::add(*f.state, f.site, f.output, 1.0f, f.employer) == Approx(1.0f));
		REQUIRE(concrete_market::post_ask(*f.state, f.employer, f.site, f.market, f.output, 1.0f, 10.0f, {}));
		exact_person_goods::process_daily(*f.state);
		REQUIRE(exact_person_goods::validate_canonical_household_economy(*f.state));
		return exact_person_goods::canonical_household_checksum(*f.state);
	};
	REQUIRE(run(false) == run(true));
}

TEST_CASE("POP wealth and satisfaction cannot finance education or alter literacy access",
	"[economy][exact][consumer-purge][education]") {
	using namespace legacy_consumer_purge_tests;
	fixture f;
	auto nation = f.state->world.create_nation();
	f.state->world.province_set_nation_from_province_ownership(f.province, nation);
	auto ministry = governance::create_institution(*f.state, nation, governance::institution_kind::education_ministry);
	auto treasury = governance::finance::open_treasury_account(*f.state, ministry, f.settlement);
	REQUIRE(accounts::bootstrap_set_balance(*f.state, treasury, 10.0f));
	auto vacancy = job_market::post_institution_job_offer(*f.state, ministry, f.site, 0,
		0.000125f, 1.0f, 1, treasury, 1, f.state->current_date);
	REQUIRE(vacancy);
	REQUIRE(exact_person_economy::submit_application(*f.state, f.consumer, vacancy, f.state->current_date));
	exact_person_economy::process_pending_applications(*f.state);
	REQUIRE(pops::education_access(*f.state, f.population) == Approx(0.5f));
	auto literacy = demographics::get_estimated_literacy_change(*f.state, f.population);
	auto checksum = exact_person_goods::canonical_household_checksum(*f.state);
	f.corrupt(7);
	REQUIRE(pops::education_access(*f.state, f.population) == Approx(0.5f));
	REQUIRE(demographics::get_estimated_literacy_change(*f.state, f.population) == Approx(literacy));
	REQUIRE(exact_person_goods::canonical_household_checksum(*f.state) == checksum);
}
