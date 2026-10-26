#include "catch.hpp"
#include "canonical_consumer_fixture.hpp"
#include "economy/capital_projects.hpp"
#include "economy/construction_industry.hpp"
#include "economy/firm_agency.hpp"
#include "economy/physical/deposits.hpp"
#include "economy/physical/extraction.hpp"
#include "actors/ownership.hpp"
#include "economy/physical/inventory.hpp"
#include "economy/physical/concrete_market.hpp"
#include "economy/physical/shipments.hpp"
#include "gamestate/serialization.hpp"
#include "governance/governance.hpp"
#include "governance/finance/finance.hpp"
#include "economy/physical/freight_market.hpp"
#include "persons/exact_population.hpp"
#include "world/spatial_runtime.hpp"
#include "economy/industrial_production.hpp"
#include "economy/economy_stats.hpp"
#include <cmath>
#include <limits>
namespace construction_tests {
using namespace economy;
using namespace economy::physical;
namespace industry = economy::construction_industry;
struct fixture : canonical_consumer_tests::fixture {
 uint64_t next_worker = 0;
 fixture() {
  state->world.province_resize_building_level(economy::max_building_types);
  state->world.province_resize_labor_price(economy::labor::total);
  auto node=state->world.create_infrastructure_node();
  state->world.force_create_infrastructure_node_location(node,province);
  // Residents the job market can hire as exact workers.
  persons::exact_population::cell_descriptor residents;
  residents.source_population_cell = 900;
  residents.literal_count = 400;
  residents.bootstrap_base_day = state->current_date.to_raw_value() - 1;
  residents.demographic_seed = 900;
  residents.home_site = site;
  REQUIRE(persons::exact_population::register_synthetic_population_cell(*state, residents).result
   == persons::exact_population::status::created);
 }
 dcon::organization_id company() { return actors::organizations::operator_organization_for_factory(*state, factory); }
 // A construction company with its own works and cash.
 dcon::factory_id contractor(float cash = 0.0f) {
  auto organization = actors::organizations::create_company(*state);
  auto works = industry::open_works(*state, organization, province, settlement);
  REQUIRE(works);
  if(cash > 0.0f) REQUIRE(accounts::bootstrap_set_balance(*state,
   actors::organizations::operating_account_for(*state, organization, settlement), cash));
  return works;
 }
 dcon::monetary_account_id till(dcon::factory_id works) {
  return actors::organizations::operating_account_for(*state, actors::organizations::operator_organization_for_factory(*state, works), settlement);
 }
 // Hires exact workers through an ordinary job offer, contract and payroll.
 void hire(dcon::factory_id works, uint32_t count) {
  auto employer = actors::organizations::operator_actor_for_factory(*state, works);
  auto offer = economy::physical::job_market::post_job_offer(*state, employer, works,
   state->world.factory_get_site_from_factory_site(works), 0, 1.0f, 1.0f, 1, till(works), count, state->current_date);
  REQUIRE(offer);
  auto before = exact_person_economy::active_contracts_for_factory(*state, works).size();
  for(uint32_t hired = 0; hired < count && next_worker < 400; next_worker += 4)
   if(exact_person_economy::submit_application(*state, {900, next_worker}, offer, state->current_date)) ++hired;
  exact_person_economy::process_pending_applications(*state);
  REQUIRE(exact_person_economy::active_contracts_for_factory(*state, works).size() == before + count);
 }
 void lay_off(dcon::factory_id works) {
  for(auto id : exact_person_economy::active_contracts_for_factory(*state, works))
   REQUIRE(economy::physical::labor_dynamics::quit_exact_employment(*state, id));
 }
 void day() { capital_projects::process_projects(*state); state->current_date += 1; }
 // Every unit of money in the world, in firm, treasury, project and personal accounts.
 double money() {
  double total = 0.0;
  state->world.for_each_monetary_account([&](auto account) { total += accounts::balance(*state, account); });
  for(auto const& account : exact_person_economy::export_snapshot(*state).accounts) total += account.balance;
  return total;
 }
};
dcon::capital_project_id project(fixture& f, capital_projects::project_kind kind = capital_projects::project_kind::factory) {
 return capital_projects::create(*f.state, kind, f.employer, f.company(), f.site, f.settlement, f.type, f.output);
}
dcon::site_id yard(fixture& f, dcon::capital_project_id p) { return f.state->world.capital_project_get_site_from_capital_project_site(p); }
dcon::monetary_account_id account(fixture& f, dcon::capital_project_id p) { return f.state->world.capital_project_get_monetary_account_from_capital_project_account(p); }
float stock(fixture& f, dcon::capital_project_id p) { return inventory::quantity(*f.state, yard(f,p), f.output, f.employer); }
float consumed(fixture& f, dcon::capital_project_id p) { return capital_projects::consumed_materials(*f.state, p).commodity_amounts[0]; }
float work(fixture& f, dcon::capital_project_id p) { return capital_projects::work_done(*f.state, p); }
// A funded project with its materials already at the yard.
dcon::capital_project_id stocked(fixture& f, float materials = 10.0f, float cash = 1000.0f) {
 auto p = project(f); REQUIRE(p);
 REQUIRE(capital_projects::add_requirement(*f.state, p, f.output, materials));
 REQUIRE(capital_projects::fund(*f.state, p, f.payer, cash));
 REQUIRE(inventory::add(*f.state, yard(f,p), f.output, materials, f.employer) == Approx(materials));
 return p;
}
// Staffs a local contractor and works the project until it is complete.
void build(fixture& f, dcon::capital_project_id p, uint32_t crew = 4) {
 if(capital_projects::accepts_work(*f.state, p) == false) REQUIRE(capital_projects::fund(*f.state, p, f.payer, 1000));
 auto works = f.contractor();
 REQUIRE(industry::sign(*f.state, p, works));
 f.hire(works, crew);
 for(int i = 0; i < 400 && f.state->world.capital_project_get_status(p) != uint8_t(capital_projects::status::completed); ++i) f.day();
 REQUIRE(f.state->world.capital_project_get_status(p) == uint8_t(capital_projects::status::completed));
}
}
using namespace construction_tests;

TEST_CASE("materials at the yard without workers build nothing", "[construction-industry]") {
 fixture f; auto p = stocked(f);
 for(int i = 0; i < 20; ++i) f.day();
 // The sponsor's own works was contracted as builder of last resort, but it has no crew.
 auto works = industry::contractor_for(*f.state, p); REQUIRE(works);
 REQUIRE(industry::crew(*f.state, works) == 0);
 REQUIRE(work(f,p) == 0); REQUIRE(consumed(f,p) == 0); REQUIRE(stock(f,p) == 10);
 REQUIRE(capital_projects::physical_progress(*f.state, p) == 0);
 REQUIRE_FALSE(capital_projects::complete(*f.state, p));
 REQUIRE_FALSE(f.state->world.capital_project_get_factory_from_capital_project_factory(p));
}

TEST_CASE("workers without materials build nothing and are still paid", "[construction-industry]") {
 fixture f; auto p = project(f); REQUIRE(capital_projects::add_requirement(*f.state, p, f.output, 10));
 REQUIRE(capital_projects::fund(*f.state, p, f.payer, 1000));
 auto works = f.contractor(100); REQUIRE(industry::sign(*f.state, p, works)); f.hire(works, 3);
 auto before = accounts::balance(*f.state, f.till(works));
 for(int i = 0; i < 5; ++i) f.day();
 REQUIRE(work(f,p) == 0); REQUIRE(consumed(f,p) == 0);
 REQUIRE(f.state->world.capital_project_get_status(p) == uint8_t(capital_projects::status::funded));
 // The idle crew draws its wages from the contractor through ordinary payroll.
 REQUIRE(accounts::balance(*f.state, f.till(works)) < before);
 auto contract = industry::active_contract(*f.state, p);
 REQUIRE(f.state->world.construction_contract_get_idle_days(contract) == 0);
 REQUIRE(f.state->world.construction_contract_get_stalled_days(contract) == 5);
}

TEST_CASE("workers and materials together build at a limited daily rate", "[construction-industry]") {
 fixture f; auto p = stocked(f, 10); auto works = f.contractor();
 REQUIRE(industry::sign(*f.state, p, works)); f.hire(works, 1);
 auto required = capital_projects::required_work(*f.state, p); REQUIRE(required > 2);
 f.day();
 // One unequipped worker performs one reference worker-day and installs the
 // materials that day's work needs, no more.
 REQUIRE(work(f,p) == Approx(1));
 REQUIRE(consumed(f,p) == Approx(10 / required));
 REQUIRE(stock(f,p) == Approx(10 - 10 / required));
 REQUIRE(f.state->world.capital_project_get_status(p) == uint8_t(capital_projects::status::active));
 SECTION("equipment mechanizes the crew up to the site's planned crew") {
  auto machines = f.state->world.create_commodity(); f.state->world.commodity_set_construction_equipment(machines, true);
  REQUIRE(inventory::add(*f.state, f.state->world.factory_get_site_from_factory_site(works), machines, 5,
   actors::organizations::operator_actor_for_factory(*f.state, works)) == 5);
  REQUIRE(industry::daily_work_capacity(*f.state, works) == Approx(2));
  f.day();
  REQUIRE(work(f,p) == Approx(3));
  REQUIRE(industry::equipment_stock(*f.state, works) < 5);
 }
 SECTION("progress needs every day of work, never elapsed time") {
  auto contract = industry::active_contract(*f.state, p); REQUIRE(contract);
  int days = 1;
  while(f.state->world.capital_project_get_status(p) != uint8_t(capital_projects::status::completed) && days < 400) { f.day(); ++days; }
  REQUIRE(days == int(std::ceil(required - 1e-3f)));
  REQUIRE(consumed(f,p) == 10); REQUIRE(stock(f,p) == Approx(0).margin(1e-4));
  REQUIRE(f.state->world.capital_project_get_factory_from_capital_project_factory(p));
  REQUIRE_FALSE(industry::active_contract(*f.state, p));
  REQUIRE(f.state->world.construction_contract_get_status(contract) == uint8_t(industry::contract_status::completed));
  REQUIRE(f.state->world.construction_contract_get_work_performed(contract) == Approx(required));
 }
 SECTION("a labor shortage stretches the schedule") {
  f.lay_off(works);
  for(int i = 0; i < 10; ++i) f.day();
  REQUIRE(work(f,p) == Approx(1));
 }
}

TEST_CASE("two projects compete for one contractor's crew", "[construction-industry]") {
 fixture f; auto a = stocked(f, 10), b = stocked(f, 10); auto works = f.contractor();
 REQUIRE(industry::sign(*f.state, a, works));
 f.state->current_date += 1;
 REQUIRE(industry::sign(*f.state, b, works));
 REQUIRE(industry::committed_crew(*f.state, works) == Approx(2));
 f.hire(works, 1);
 for(int i = 0; i < 3; ++i) f.day();
 // The crew serves the earlier contract first; the later one waits.
 REQUIRE(work(f,a) == Approx(3)); REQUIRE(work(f,b) == 0);
 REQUIRE(f.state->world.construction_contract_get_idle_days(industry::active_contract(*f.state, b)) == 3);
 f.hire(works, 2);
 f.day();
 // Three workers: the first site absorbs at most its planned crew working
 // fully equipped, the rest goes to the second site.
 REQUIRE(work(f,a) == Approx(5)); REQUIRE(work(f,b) == Approx(1));
 REQUIRE(industry::decide_works(*f.state, works).desired_units == Approx(2));
}

TEST_CASE("wages and contractor payments move money without creating it", "[construction-industry]") {
 fixture f; auto p = stocked(f, 10, 200); auto works = f.contractor(50);
 REQUIRE(industry::sign(*f.state, p, works)); f.hire(works, 2);
 auto total = f.money();
 auto project_cash = accounts::balance(*f.state, account(f,p));
 auto contractor_cash = accounts::balance(*f.state, f.till(works));
 for(int i = 0; i < 6; ++i) f.day();
 REQUIRE(f.money() == Approx(total));
 auto contract = industry::active_contract(*f.state, p); REQUIRE(contract);
 auto paid = f.state->world.construction_contract_get_amount_paid(contract);
 REQUIRE(paid == Approx(work(f,p) * f.state->world.construction_contract_get_unit_price(contract)));
 REQUIRE(accounts::balance(*f.state, account(f,p)) == Approx(project_cash - paid));
 // The contractor received the payments and paid its crew from them.
 auto wages = contractor_cash + paid - accounts::balance(*f.state, f.till(works));
 REQUIRE(wages > 0); REQUIRE(wages <= 12.0001);
 REQUIRE(f.state->world.construction_contract_get_unit_price(contract) > 1);
}

TEST_CASE("cancellation keeps the sponsor's stock and cash and the contractor's earnings", "[construction-industry]") {
 fixture f; auto p = stocked(f, 10, 300); auto works = f.contractor();
 REQUIRE(industry::sign(*f.state, p, works)); f.hire(works, 1);
 for(int i = 0; i < 3; ++i) f.day();
 auto contract = industry::active_contract(*f.state, p);
 auto total = f.money(); auto left = stock(f,p); auto cash = accounts::balance(*f.state, account(f,p));
 auto earned = accounts::balance(*f.state, f.till(works)); auto built = work(f,p);
 REQUIRE(capital_projects::cancel(*f.state, p));
 REQUIRE(f.state->world.construction_contract_get_status(contract) == uint8_t(industry::contract_status::cancelled));
 REQUIRE(stock(f,p) == left); REQUIRE(accounts::balance(*f.state, account(f,p)) == cash);
 REQUIRE(accounts::balance(*f.state, f.till(works)) == earned); REQUIRE(f.money() == Approx(total));
 for(int i = 0; i < 5; ++i) f.day();
 REQUIRE(work(f,p) == built); REQUIRE(stock(f,p) == left);
 REQUIRE_FALSE(f.state->world.capital_project_get_factory_from_capital_project_factory(p));
 REQUIRE_FALSE(capital_projects::fund(*f.state, p, f.payer, 1));
}

TEST_CASE("a defaulting contractor completes nothing and a new one resumes the physical work", "[construction-industry]") {
 fixture f; auto p = stocked(f, 10); auto first = f.contractor();
 REQUIRE(industry::sign(*f.state, p, first)); f.hire(first, 1);
 for(int i = 0; i < 3; ++i) f.day();
 auto built = work(f,p); auto installed = consumed(f,p); REQUIRE(built == Approx(3));
 auto broken = industry::active_contract(*f.state, p);
 SECTION("non-performance") {
  f.lay_off(first);
  for(int i = 0; i < int(industry::non_performance_days); ++i) f.day();
  REQUIRE(f.state->world.construction_contract_get_status(broken) == uint8_t(industry::contract_status::active));
  f.day();
  REQUIRE(f.state->world.construction_contract_get_status(broken) == uint8_t(industry::contract_status::defaulted));
 }
 SECTION("bankruptcy") {
  f.state->world.factory_set_agency_lifecycle_status(first, 2);
  f.day();
  REQUIRE(f.state->world.construction_contract_get_status(broken) == uint8_t(industry::contract_status::defaulted));
 }
 REQUIRE(work(f,p) == Approx(built)); REQUIRE(consumed(f,p) == Approx(installed));
 REQUIRE(f.state->world.capital_project_get_status(p) != uint8_t(capital_projects::status::completed));
 REQUIRE_FALSE(f.state->world.capital_project_get_factory_from_capital_project_factory(p));
 // The sponsor re-tenders; the defaulter is never chosen again for this project.
 auto second = industry::contractor_for(*f.state, p); REQUIRE(second); REQUIRE(second != first);
 f.hire(second, 1);
 f.day();
 REQUIRE(work(f,p) == Approx(built + 1));
 while(f.state->world.capital_project_get_status(p) != uint8_t(capital_projects::status::completed) && work(f,p) < 1000) f.day();
 REQUIRE(consumed(f,p) == 10);
 REQUIRE(f.state->world.capital_project_get_factory_from_capital_project_factory(p));
}

TEST_CASE("unfinished physical work and the contract survive save and load", "[construction-industry]") {
 fixture f; auto p = stocked(f, 10); auto works = f.contractor();
 REQUIRE(industry::sign(*f.state, p, works)); f.hire(works, 1);
 for(int i = 0; i < 4; ++i) f.day();
 REQUIRE(work(f,p) == Approx(4));
 std::vector<uint8_t> bytes(sys::sizeof_save_section(*f.state)); auto end=sys::write_save_section(bytes.data(),*f.state); REQUIRE(end==bytes.data()+bytes.size());
 fixture loaded; REQUIRE(sys::read_save_section(bytes.data(),end,*loaded.state)==end);
 REQUIRE(work(loaded,p) == Approx(4)); REQUIRE(consumed(loaded,p) == Approx(consumed(f,p)));
 REQUIRE(capital_projects::required_work(*loaded.state,p) == capital_projects::required_work(*f.state,p));
 REQUIRE(industry::contractor_for(*loaded.state,p) == works);
 REQUIRE(industry::crew(*loaded.state,works) == 1);
 for(auto* x:{&f,&loaded}) { x->day(); REQUIRE(work(*x,p) == Approx(5)); }
 REQUIRE(stock(loaded,p) == Approx(stock(f,p)));
}

TEST_CASE("construction never completes on cached progress, elapsed time, or materials alone", "[construction-industry]") {
 fixture f; auto p = stocked(f);
 f.state->world.capital_project_set_progress(p, 1);
 f.state->current_date += 3000;
 REQUIRE(capital_projects::physical_progress(*f.state,p) == 0);
 REQUIRE_FALSE(capital_projects::complete(*f.state,p));
 // Every material installed by hand still lacks the work.
 f.state->world.capital_project_for_each_capital_project_requirement_project_as_capital_project(p, [&](auto rel) {
  f.state->world.capital_project_requirement_set_consumed_quantity(f.state->world.capital_project_requirement_project_get_capital_project_requirement(rel), 10);
 });
 REQUIRE_FALSE(capital_projects::complete(*f.state,p));
 REQUIRE(capital_projects::physical_progress(*f.state,p) < 1);
 REQUIRE_FALSE(f.state->world.capital_project_get_factory_from_capital_project_factory(p));
}

TEST_CASE("empty and malformed construction recipes never complete", "[construction-cut]") {
 fixture f; auto p = project(f); REQUIRE(p);
 f.state->world.capital_project_set_progress(p,1);
 REQUIRE_FALSE(capital_projects::complete(*f.state,p));
 REQUIRE_FALSE(capital_projects::seal_required_work(*f.state,p));
 REQUIRE_FALSE(capital_projects::add_requirement(*f.state,p,f.output,std::numeric_limits<float>::quiet_NaN()));
 auto req=capital_projects::add_requirement(*f.state,p,f.output,10); REQUIRE(req);
 f.state->world.capital_project_requirement_set_consumed_quantity(req,std::numeric_limits<float>::infinity());
 REQUIRE(capital_projects::physical_progress(*f.state,p) == 0);
 REQUIRE(capital_projects::workable(*f.state,p) == 0);
 REQUIRE_FALSE(capital_projects::complete(*f.state,p));
}

TEST_CASE("projects isolate both accounts and stock for the same sponsor and province", "[construction-cut]") {
 fixture f; auto a=project(f), b=project(f); REQUIRE(a); REQUIRE(b);
 REQUIRE(yard(f,a) != yard(f,b)); REQUIRE(account(f,a) != account(f,b));
 REQUIRE(capital_projects::add_requirement(*f.state,a,f.output,5)); REQUIRE(capital_projects::add_requirement(*f.state,b,f.output,5));
 REQUIRE(inventory::add(*f.state,yard(f,a),f.output,5,f.employer) == 5);
 REQUIRE(capital_projects::fund(*f.state,a,f.payer,20)); REQUIRE(capital_projects::fund(*f.state,b,f.payer,20));
 REQUIRE(capital_projects::seal_required_work(*f.state,a)); REQUIRE(capital_projects::seal_required_work(*f.state,b));
 REQUIRE(capital_projects::workable(*f.state,b) == 0);
 REQUIRE(capital_projects::workable(*f.state,a) == Approx(capital_projects::required_work(*f.state,a)));
 REQUIRE_FALSE(capital_projects::add_requirement(*f.state,a,f.output,1));
}

TEST_CASE("construction bids fit available project cash and suspend and cancel release it", "[construction-cut]") {
 fixture f; auto p=project(f); REQUIRE(capital_projects::add_requirement(*f.state,p,f.output,100));
 REQUIRE(capital_projects::fund(*f.state,p,f.payer,12));
 capital_projects::process_projects(*f.state);
 auto reserved=concrete_market::reserved_bid_amount(*f.state,account(f,p));
 // Part of the cash is kept for the crew's work.
 REQUIRE(reserved > 0); REQUIRE(reserved < 12);
 capital_projects::process_projects(*f.state);
 REQUIRE(concrete_market::reserved_bid_amount(*f.state,account(f,p)) == Approx(reserved));
 REQUIRE(capital_projects::suspend(*f.state,p));
 REQUIRE(concrete_market::reserved_bid_amount(*f.state,account(f,p)) == 0);
 capital_projects::process_projects(*f.state);
 REQUIRE(concrete_market::reserved_bid_amount(*f.state,account(f,p)) == 0);
 REQUIRE(capital_projects::cancel(*f.state,p));
 REQUIRE_FALSE(capital_projects::fund(*f.state,p,f.payer,1));
 REQUIRE(accounts::balance(*f.state,f.payer) + accounts::balance(*f.state,account(f,p)) == Approx(10000));
}

TEST_CASE("delivery pays from project cash and cannot be worked before physical arrival", "[construction-cut]") {
 fixture f; auto p=project(f); REQUIRE(capital_projects::add_requirement(*f.state,p,f.output,5));
 auto seller=actors::organizations::actor_for_organization(*f.state,actors::organizations::create_company(*f.state));
 auto seller_account=accounts::open_account(*f.state,seller,f.settlement);
 REQUIRE(inventory::add(*f.state,f.site,f.output,5,seller) == 5);
 REQUIRE_FALSE(capital_projects::deliver_material(*f.state,p,seller_account,f.site,f.output,5,10));
 REQUIRE(capital_projects::fund(*f.state,p,f.payer,10));
 REQUIRE(capital_projects::seal_required_work(*f.state,p));
 REQUIRE(capital_projects::deliver_material(*f.state,p,seller_account,f.site,f.output,5,10));
 REQUIRE(accounts::balance(*f.state,seller_account) == 10);
 REQUIRE(accounts::balance(*f.state,account(f,p)) == 0);
 REQUIRE(capital_projects::workable(*f.state,p) == 0);
 for(int i=0;i<5;++i) { shipments::advance(*f.state); shipments::process_arrivals(*f.state); f.state->current_date += 1; }
 auto received=stock(f,p);
 REQUIRE(received < 5); REQUIRE(received > 4.99);
 // Transport loss leaves a sliver of work that the materials cannot carry.
 REQUIRE(capital_projects::workable(*f.state,p) < capital_projects::required_work(*f.state,p));
 REQUIRE(capital_projects::workable(*f.state,p) > 0);
}

TEST_CASE("every result kind is produced only by contracted work", "[construction-cut]") {
 fixture f; auto company=f.company();
 SECTION("extraction") {
  // The plant is built on an authored deposit; construction never creates one.
  auto ore=f.state->world.create_commodity();
  auto mine=f.state->world.create_factory_type();
  f.state->world.factory_type_set_output(mine,ore);
  f.state->world.factory_type_set_output_amount(mine,1.0f);
  f.state->world.factory_type_set_base_workforce(mine,1);
  f.state->world.factory_type_set_extracts_deposit(mine,true);
  auto& bill=f.state->world.factory_type_get_construction_costs(mine); bill.commodity_type[0]=f.output; bill.commodity_amounts[0]=5;
  auto deposit=physical::deposits::create_deposit(*f.state,f.site,ore,50,50,1,2,2,0); REQUIRE(deposit);
  REQUIRE(actors::organizations::bind_deposit_operator(*f.state,company,deposit));
  auto subsoil=f.state->world.create_asset(); f.state->world.force_create_resource_deposit_asset(deposit,subsoil);
  REQUIRE(actors::ownership::create_stake(*f.state,f.employer,subsoil,1,1,1));
  auto deposits_before=f.state->world.resource_deposit_size();
  auto p=capital_projects::create_extraction_plant(*f.state,f.employer,company,deposit,mine,f.settlement); REQUIRE(p);
  REQUIRE(inventory::add(*f.state,yard(f,p),f.output,5,f.employer) == 5);
  capital_projects::process_projects(*f.state);
  REQUIRE_FALSE(f.state->world.capital_project_get_factory_from_capital_project_factory(p));
  build(f,p);
  auto plant=f.state->world.capital_project_get_factory_from_capital_project_factory(p); REQUIRE(plant);
  REQUIRE(physical::extraction::deposit_for_enterprise(*f.state,plant)==deposit);
  REQUIRE(f.state->world.resource_deposit_size()==deposits_before);
 }
 SECTION("infrastructure") {
  auto& recipe=f.state->economy_definitions.building_definitions[0].cost; recipe.commodity_type[0]=f.output; recipe.commodity_amounts[0]=5;
  auto p=capital_projects::create_infrastructure(*f.state,f.employer,company,f.site,f.settlement,0); REQUIRE(p);
  REQUIRE(inventory::add(*f.state,yard(f,p),f.output,5,f.employer) == 5);
  capital_projects::process_projects(*f.state);
  REQUIRE(f.state->world.province_get_building_level(f.province,0) == 0);
  build(f,p);
  REQUIRE(f.state->world.province_get_building_level(f.province,0) == 1);
  f.day();
  REQUIRE(f.state->world.province_get_building_level(f.province,0) == 1);
 }
 SECTION("expansion") {
  auto p=capital_projects::create_factory_expansion(*f.state,f.factory,2,f.settlement); REQUIRE(p);
  REQUIRE(capital_projects::add_requirement(*f.state,p,f.output,5));
  REQUIRE(inventory::add(*f.state,yard(f,p),f.output,5,f.employer) == 5);
  build(f,p);
  REQUIRE(f.state->world.factory_get_productive_capacity(f.factory) == 3);
  f.day();
  REQUIRE(f.state->world.factory_get_productive_capacity(f.factory) == 3);
 }
 SECTION("naval") {
  f.state->world.nation_resize_modifier_values(sys::national_mod_offsets::count);
  f.state->military_definitions.unit_base_definitions.resize(3);
  auto unit=dcon::unit_type_id{1};
  f.state->military_definitions.unit_base_definitions[unit].is_land=false;
  auto p=project(f,capital_projects::project_kind::naval_construction); REQUIRE(p);
  REQUIRE(capital_projects::add_requirement(*f.state,p,f.output,5));
  capital_projects::request_record row{}; row.project=p; row.unit=unit; row.nation=f.state->world.province_get_nation_from_province_ownership(f.province);
  REQUIRE(capital_projects::import_requests(*f.state,{row}));
  REQUIRE(inventory::add(*f.state,yard(f,p),f.output,5,f.employer)==5);
  capital_projects::process_projects(*f.state); REQUIRE(f.state->world.ship_size()==0);
  build(f,p);
  REQUIRE(f.state->world.ship_size()==1); REQUIRE(capital_projects::export_requests(*f.state)[0].ship);
  f.day(); REQUIRE(f.state->world.ship_size()==1);
 }
 SECTION("military equipment is not a construction project") {
  REQUIRE_FALSE(capital_projects::create(*f.state,capital_projects::project_kind::retired_military_production,f.employer,company,f.site,f.settlement,{},f.output,2));
 }
}

TEST_CASE("legacy purchased goods demand progress and build time cannot advance canonical construction", "[construction-cut]") {
 fixture a,b;
 dcon::capital_project_id pa{}, pb{};
 for(auto* f:{&a,&b}) {
  auto& recipe=f->state->economy_definitions.building_definitions[0].cost; recipe.commodity_type[0]=f->output; recipe.commodity_amounts[0]=10;
  auto p=capital_projects::create_infrastructure(*f->state,f->employer,f->company(),f->site,f->settlement,0); REQUIRE(p);
  auto nation=f->state->world.province_get_nation_from_province_ownership(f->province);
  auto legacy=f->state->world.force_create_province_building_construction(f->province,nation);
  f->state->world.province_building_construction_set_type(legacy,0);
  auto rows=capital_projects::export_requests(*f->state); rows[0].building=legacy; REQUIRE(capital_projects::import_requests(*f->state,rows));
  f->state->world.market_resize_construction_demand(f->state->world.commodity_size());
  f->state->world.market_resize_private_construction_demand(f->state->world.commodity_size());
  REQUIRE(capital_projects::fund(*f->state,p,f->payer,1000));
  auto works=f->contractor(); REQUIRE(industry::sign(*f->state,p,works)); f->hire(works,1);
  (f==&a ? pa : pb)=p;
 }
 for(int day=0;day<8;++day) {
  auto legacy=capital_projects::export_requests(*b.state)[0].building;
  if(legacy) b.state->world.province_building_construction_get_purchased_goods(legacy).commodity_amounts[0]=1000000;
  b.state->world.capital_project_set_progress(pb,1);
  b.state->world.market_set_construction_demand(b.market,b.output,1000000);
  b.state->world.market_set_private_construction_demand(b.market,b.output,1000000);
  b.state->economy_definitions.building_definitions[0].time=0;
  for(auto* f:{&a,&b}) { auto p=f==&a?pa:pb; REQUIRE(inventory::add(*f->state,yard(*f,p),f->output,1.25,f->employer)==1.25); f->day(); }
  REQUIRE(capital_projects::physical_progress(*a.state,pa)==capital_projects::physical_progress(*b.state,pb));
  REQUIRE(work(a,pa)==work(b,pb));
  REQUIRE(a.state->world.province_get_building_level(a.province,0)==b.state->world.province_get_building_level(b.province,0));
  REQUIRE(accounts::balance(*a.state,account(a,pa))==accounts::balance(*b.state,account(b,pb)));
 }
 REQUIRE(a.state->world.province_get_building_level(a.province,0)==0);
}

TEST_CASE("public construction spends real treasury cash only with fiscal authority and uses the same contracts", "[construction-cut]") {
 fixture f; auto nation=f.state->world.province_get_nation_from_province_ownership(f.province);
 auto& recipe=f.state->economy_definitions.building_definitions[0].cost; recipe.commodity_type[0]=f.output; recipe.commodity_amounts[0]=5;
 auto authority=governance::create_institution(*f.state,nation,governance::institution_kind::public_works_ministry);
 auto treasury=governance::finance::open_treasury_account(*f.state,authority,f.settlement); REQUIRE(treasury);
 REQUIRE(accounts::bootstrap_set_balance(*f.state,treasury,1000)); f.state->world.nation_set_construction_spending(nation,100);
 auto order=f.state->world.force_create_province_building_construction(f.province,nation); f.state->world.province_building_construction_set_type(order,0);
 f.state->world.province_building_construction_get_purchased_goods(order).commodity_amounts[0]=1000000;
 capital_projects::adopt_legacy_requests(*f.state); auto p=capital_projects::project_for(*f.state,order); REQUIRE(p);
 REQUIRE(capital_projects::physical_progress(*f.state,p)==0);
 REQUIRE(accounts::balance(*f.state,treasury)==1000); REQUIRE(accounts::balance(*f.state,account(f,p))==0);
 REQUIRE(governance::grant_authority_to_institution(*f.state,authority,governance::authority_kind::spend_public_funds,nation));
 capital_projects::adopt_legacy_requests(*f.state);
 // The appropriation covers materials and the crew's work.
 auto materials=5*10*1.2f;
 REQUIRE(accounts::balance(*f.state,account(f,p)) == Approx(materials + capital_projects::remaining_labor_cost(*f.state,p)));
 REQUIRE(accounts::balance(*f.state,treasury)+accounts::balance(*f.state,account(f,p))==Approx(1000));
 // The state's own enterprise is the builder of last resort, through the same works and payroll.
 REQUIRE(inventory::add(*f.state,yard(f,p),f.output,5,f.state->world.capital_project_get_economic_actor_from_capital_project_sponsor(p))==5);
 f.day();
 auto works=industry::contractor_for(*f.state,p); REQUIRE(works);
 REQUIRE(actors::organizations::operator_organization_for_factory(*f.state,works)==industry::public_works_enterprise(*f.state,nation));
 REQUIRE(work(f,p)==0);
 f.hire(works,2);
 while(f.state->world.capital_project_get_status(p)!=uint8_t(capital_projects::status::completed) && work(f,p)<10000) f.day();
 REQUIRE(f.state->world.province_get_building_level(f.province,0)==1);
 auto asset=actors::ownership::asset_for_factory(*f.state,works); REQUIRE(asset);
 REQUIRE(f.state->world.factory_get_canonical_id(works)!=0);
 REQUIRE(f.state->world.organization_get_canonical_id(industry::public_works_enterprise(*f.state,nation))!=0);
}

TEST_CASE("paid materials await a real carrier without duplicate procurement and replay index zero orders", "[construction-cut]") {
 fixture f; auto p=project(f); REQUIRE(capital_projects::add_requirement(*f.state,p,f.output,2)); REQUIRE(capital_projects::fund(*f.state,p,f.payer,100));
 auto seller=actors::organizations::actor_for_organization(*f.state,actors::organizations::create_company(*f.state));
 auto seller_account=accounts::open_account(*f.state,seller,f.settlement); REQUIRE(inventory::add(*f.state,f.site,f.output,2,seller)==2);
 capital_projects::process_projects(*f.state); auto fills=concrete_market::match_all(*f.state,f.output,f.state->current_date); REQUIRE(fills.size()==1);
 REQUIRE(accounts::balance(*f.state,seller_account)==20); REQUIRE(accounts::balance(*f.state,account(f,p))==80); REQUIRE(capital_projects::physical_progress(*f.state,p)==0);
 capital_projects::process_projects(*f.state);
 float active=0; f.state->world.for_each_concrete_market_bid([&](auto bid) { if(f.state->world.concrete_market_bid_get_status(bid)==0) active+=f.state->world.concrete_market_bid_get_remaining_quantity(bid); }); REQUIRE(active==0);
 REQUIRE(stock(f,p)==0);
 REQUIRE_FALSE(concrete_market::post_ask(*f.state,f.employer,f.site,f.market,f.output,2,10,{}));
 std::vector<uint8_t> bytes(sys::sizeof_save_section(*f.state)); auto end=sys::write_save_section(bytes.data(),*f.state);
 fixture loaded; REQUIRE(sys::read_save_section(bytes.data(),end,*loaded.state)==end); REQUIRE(loaded.state->causal_order);
 auto original=economy::causal_order::export_snapshot(*f.state), restored=economy::causal_order::export_snapshot(*loaded.state);
 REQUIRE(original.dcon_sequences.size()==2); REQUIRE(restored.dcon_sequences.size()==2);
 for(auto const& row:restored.dcon_sequences) { REQUIRE(row.stable_id==0); REQUIRE(row.sequence>0); }
 for(auto* x:{&f,&loaded}) {
  auto carrier_owner=actors::organizations::actor_for_organization(*x->state,actors::organizations::create_company(*x->state));
  auto carrier_account=accounts::open_account(*x->state,carrier_owner,x->settlement);
  auto carrier=freight_market::create_carrier(*x->state,carrier_owner,carrier_account,100,255,x->market); REQUIRE(carrier);
  REQUIRE(freight_market::create_offer(*x->state,carrier,x->market,x->market,255,100,1,0));
  freight_market::process_pending_requests(*x->state);
  REQUIRE(accounts::balance(*x->state,carrier_account)>0); REQUIRE_FALSE(capital_projects::complete(*x->state,p));
  for(int i=0;i<10 && stock(*x,p)==0;++i) { shipments::advance(*x->state); shipments::process_arrivals(*x->state); x->state->current_date+=1; }
  // Arrived cargo waits at the yard for a crew; nothing is installed on arrival.
  REQUIRE(stock(*x,p) > 1.9); REQUIRE(stock(*x,p) <= 2); REQUIRE(consumed(*x,p) == 0);
  REQUIRE(accounts::balance(*x->state,x->payer)+accounts::balance(*x->state,account(*x,p))+accounts::balance(*x->state,seller_account)+accounts::balance(*x->state,carrier_account)==Approx(10000));
 }
}

TEST_CASE("a small missing material cannot disappear through progress rounding", "[construction-cut]") {
 fixture f; auto p=project(f); auto component=f.state->world.create_commodity();
 REQUIRE(capital_projects::add_requirement(*f.state,p,f.output,1000000));
 REQUIRE(capital_projects::add_requirement(*f.state,p,component,.001f));
 REQUIRE(capital_projects::fund(*f.state,p,f.payer,100)); REQUIRE(capital_projects::seal_required_work(*f.state,p));
 REQUIRE(inventory::add(*f.state,yard(f,p),f.output,1000000,f.employer)==1000000);
 // Without the component nothing at all may be installed.
 REQUIRE(capital_projects::workable(*f.state,p)==0);
 REQUIRE(capital_projects::apply_work(*f.state,p,capital_projects::required_work(*f.state,p))==0);
 REQUIRE(capital_projects::physical_progress(*f.state,p)==0); REQUIRE_FALSE(capital_projects::complete(*f.state,p));
 REQUIRE_FALSE(f.state->world.capital_project_get_factory_from_capital_project_factory(p));
}

TEST_CASE("completed rail construction changes physical edge capacity", "[construction-cut]") {
 fixture f; auto other=f.state->world.create_province(); f.state->world.province_set_building_level(other,0,1);
 auto other_node=f.state->world.create_infrastructure_node(); f.state->world.force_create_infrastructure_node_location(other_node,other);
 auto node=world::spatial_runtime::node_for_site(*f.state,f.site);
 auto edge=f.state->world.create_infrastructure_edge(); f.state->world.force_create_infrastructure_edge_from(edge,node); f.state->world.force_create_infrastructure_edge_to(edge,other_node); f.state->world.infrastructure_edge_set_distance(edge,1);
 auto before=world::spatial_runtime::effective_capacity(*f.state,edge);
 auto& recipe=f.state->economy_definitions.building_definitions[0].cost; recipe.commodity_type[0]=f.output; recipe.commodity_amounts[0]=2;
 auto p=capital_projects::create_infrastructure(*f.state,f.employer,f.company(),f.site,f.settlement,0); REQUIRE(p);
 REQUIRE(f.state->world.infrastructure_edge_get_type(edge)==0);
 REQUIRE(inventory::add(*f.state,yard(f,p),f.output,2,f.employer)==2);
 build(f,p);
 REQUIRE(f.state->world.infrastructure_edge_get_type(edge)==1); REQUIRE(world::spatial_runtime::effective_capacity(*f.state,edge)>before);
}

TEST_CASE("construction funding cannot spend cash reserved by another concrete order", "[construction-cut]") {
 fixture f; auto p=project(f); REQUIRE(capital_projects::add_requirement(*f.state,p,f.output,2));
 REQUIRE(concrete_market::post_bid(*f.state,f.employer,f.payer,f.site,f.market,f.output,999,10,{}));
 REQUIRE_FALSE(capital_projects::fund(*f.state,p,f.payer,20)); REQUIRE(accounts::balance(*f.state,account(f,p))==0);
 REQUIRE(capital_projects::fund(*f.state,p,f.payer,10));
 REQUIRE(accounts::balance(*f.state,f.payer)==9990); REQUIRE(concrete_market::reserved_bid_amount(*f.state,f.payer)==9990);
}

TEST_CASE("contractor works are staffed by the job market for their contracts", "[construction-industry]") {
 fixture f; auto p=stocked(f,40); auto works=f.contractor(100);
 REQUIRE(industry::sign(*f.state,p,works));
 REQUIRE(industry::decide_works(*f.state,works).desired_units==Approx(2));
 REQUIRE(firm_agency::decide_factory(*f.state,works).desired_units==Approx(2));
 economy::physical::job_market::process_factory_vacancies(*f.state);
 auto offers=economy::physical::job_market::open_offers_for_factory(*f.state,works); REQUIRE(offers.size()==1);
 REQUIRE(f.state->world.job_offer_get_openings(offers[0])==2);
 // A works has no product: production and recipe planning leave it alone.
 REQUIRE(economy::industrial_production::produce_factory(*f.state,works)==0);
 REQUIRE_FALSE(f.state->world.factory_get_building_type(works));
 REQUIRE(capital_projects::is_construction_site(*f.state,f.state->world.factory_get_site_from_factory_site(works)));
 auto asset=actors::ownership::asset_for_factory(*f.state,works); REQUIRE(asset);
 REQUIRE(f.state->world.asset_get_canonical_id(asset)!=0);
}
