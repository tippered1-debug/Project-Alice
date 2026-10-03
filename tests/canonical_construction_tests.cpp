#include "catch.hpp"
#include "canonical_consumer_fixture.hpp"
#include "economy/capital_projects.hpp"
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
#include "world/spatial_runtime.hpp"
#include <limits>
namespace construction_tests {
using namespace economy;
using namespace economy::physical;
struct fixture : canonical_consumer_tests::fixture {
 fixture() {
  state->world.province_resize_building_level(economy::max_building_types);
  auto node=state->world.create_infrastructure_node();
  state->world.force_create_infrastructure_node_location(node,province);
 }
};
dcon::capital_project_id project(fixture& f, capital_projects::project_kind kind = capital_projects::project_kind::factory) {
 auto company = actors::organizations::operator_organization_for_factory(*f.state, f.factory);
 return capital_projects::create(*f.state, kind, f.employer, company, f.site, f.settlement, f.type, f.output);
}
dcon::site_id yard(fixture& f, dcon::capital_project_id p) { return f.state->world.capital_project_get_site_from_capital_project_site(p); }
dcon::monetary_account_id account(fixture& f, dcon::capital_project_id p) { return f.state->world.capital_project_get_monetary_account_from_capital_project_account(p); }
}
using namespace construction_tests;
TEST_CASE("construction requires consumed physical materials, never cached progress or elapsed time", "[construction-cut]") {
 fixture f; auto p = project(f); REQUIRE(p);
 auto req = capital_projects::add_requirement(*f.state, p, f.output, 10); REQUIRE(req);
 f.state->world.capital_project_set_progress(p, 1);
 f.state->current_date += 3000;
 REQUIRE(capital_projects::material_progress(*f.state,p) == 0);
 REQUIRE_FALSE(capital_projects::complete(*f.state,p));
 REQUIRE(inventory::add(*f.state, yard(f,p), f.output, 10, f.employer) == 10);
 REQUIRE(capital_projects::material_progress(*f.state,p) == 0);
 REQUIRE(capital_projects::consume(*f.state,req,4) == 4);
 REQUIRE(capital_projects::material_progress(*f.state,p) == Approx(.4));
 REQUIRE_FALSE(capital_projects::complete(*f.state,p));
 REQUIRE(capital_projects::consume(*f.state,req,100) == 6);
 auto factory = f.state->world.capital_project_get_factory_from_capital_project_factory(p); REQUIRE(factory);
 REQUIRE(inventory::quantity(*f.state,yard(f,p),f.output,f.employer) == 0);
 auto count = f.state->world.factory_size();
 REQUIRE_FALSE(capital_projects::complete(*f.state,p));
 REQUIRE(f.state->world.factory_size() == count);
}
TEST_CASE("empty and malformed construction recipes never complete", "[construction-cut]") {
 fixture f; auto p = project(f); REQUIRE(p);
 f.state->world.capital_project_set_progress(p,1);
 REQUIRE_FALSE(capital_projects::complete(*f.state,p));
 REQUIRE_FALSE(capital_projects::add_requirement(*f.state,p,f.output,std::numeric_limits<float>::quiet_NaN()));
 auto req=capital_projects::add_requirement(*f.state,p,f.output,10); REQUIRE(req);
 f.state->world.capital_project_requirement_set_consumed_quantity(req,std::numeric_limits<float>::infinity());
 REQUIRE(capital_projects::material_progress(*f.state,p) == 0);
 REQUIRE_FALSE(capital_projects::complete(*f.state,p));
}
TEST_CASE("projects isolate both accounts and stock for the same sponsor and province", "[construction-cut]") {
 fixture f; auto a=project(f), b=project(f); REQUIRE(a); REQUIRE(b);
 REQUIRE(yard(f,a) != yard(f,b)); REQUIRE(account(f,a) != account(f,b));
 auto ra=capital_projects::add_requirement(*f.state,a,f.output,5); auto rb=capital_projects::add_requirement(*f.state,b,f.output,5);
 REQUIRE(inventory::add(*f.state,yard(f,a),f.output,5,f.employer) == 5);
 REQUIRE(capital_projects::consume(*f.state,rb,5) == 0);
 REQUIRE(capital_projects::fund(*f.state,a,f.payer,20));
 REQUIRE(accounts::balance(*f.state,account(f,b)) == 0);
 REQUIRE_FALSE(capital_projects::add_requirement(*f.state,a,f.output,1));
 REQUIRE(capital_projects::consume(*f.state,ra,5) == 5);
 REQUIRE(capital_projects::material_progress(*f.state,b) == 0);
}
TEST_CASE("construction bids fit available project cash and suspend and cancel release it", "[construction-cut]") {
 fixture f; auto p=project(f); REQUIRE(capital_projects::add_requirement(*f.state,p,f.output,100));
 REQUIRE(capital_projects::fund(*f.state,p,f.payer,12));
 capital_projects::process_projects(*f.state);
 auto reserved=concrete_market::reserved_bid_amount(*f.state,account(f,p));
 REQUIRE(reserved > 0); REQUIRE(reserved <= 12.0001f);
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
TEST_CASE("delivery pays from project cash and cannot complete before physical arrival", "[construction-cut]") {
 fixture f; auto p=project(f); auto req=capital_projects::add_requirement(*f.state,p,f.output,5); REQUIRE(req);
 auto seller=actors::organizations::actor_for_organization(*f.state,actors::organizations::create_company(*f.state));
 auto seller_account=accounts::open_account(*f.state,seller,f.settlement);
 REQUIRE(inventory::add(*f.state,f.site,f.output,5,seller) == 5);
 REQUIRE_FALSE(capital_projects::deliver_material(*f.state,p,seller_account,f.site,f.output,5,10));
 REQUIRE(inventory::quantity(*f.state,f.site,f.output,seller) == 5);
 REQUIRE(capital_projects::fund(*f.state,p,f.payer,10));
 auto delivery=capital_projects::deliver_material(*f.state,p,seller_account,f.site,f.output,5,10); REQUIRE(delivery);
 REQUIRE(accounts::balance(*f.state,seller_account) == 10);
 REQUIRE(accounts::balance(*f.state,account(f,p)) == 0);
 REQUIRE(capital_projects::consume(*f.state,req,5) == 0);
 for(int i=0;i<5;++i) { shipments::advance(*f.state); shipments::process_arrivals(*f.state); f.state->current_date += 1; }
 auto received=inventory::quantity(*f.state,yard(f,p),f.output,f.employer);
 REQUIRE(received < 5); REQUIRE(received > 4.99);
 REQUIRE(capital_projects::consume(*f.state,req,5)==Approx(received)); REQUIRE_FALSE(capital_projects::complete(*f.state,p));
 REQUIRE(inventory::add(*f.state,f.site,f.output,.01f,seller)==Approx(.01f));
 REQUIRE(capital_projects::deliver_material(*f.state,p,seller_account,f.site,f.output,.01f,0));
 shipments::process_arrivals(*f.state);
 REQUIRE(capital_projects::consume(*f.state,req,1)>0); REQUIRE(capital_projects::material_progress(*f.state,p)==1);
}
TEST_CASE("daily project processing supports deposits infrastructure expansion and military equipment", "[construction-cut]") {
 fixture f; auto company=actors::organizations::operator_organization_for_factory(*f.state,f.factory);
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
  auto plant=f.state->world.capital_project_get_factory_from_capital_project_factory(p); REQUIRE(plant);
  REQUIRE(physical::extraction::deposit_for_enterprise(*f.state,plant)==deposit);
  REQUIRE(f.state->world.resource_deposit_size()==deposits_before);
 }
 SECTION("infrastructure") {
  auto& recipe=f.state->economy_definitions.building_definitions[0].cost; recipe.commodity_type[0]=f.output; recipe.commodity_amounts[0]=5;
  auto p=capital_projects::create_infrastructure(*f.state,f.employer,company,f.site,f.settlement,0); REQUIRE(p);
  REQUIRE(f.state->world.province_get_building_level(f.province,0) == 0);
  REQUIRE(inventory::add(*f.state,yard(f,p),f.output,5,f.employer) == 5);
  capital_projects::process_projects(*f.state);
  REQUIRE(f.state->world.province_get_building_level(f.province,0) == 1);
  capital_projects::process_projects(*f.state);
  REQUIRE(f.state->world.province_get_building_level(f.province,0) == 1);
 }
 SECTION("expansion") {
  auto p=capital_projects::create_factory_expansion(*f.state,f.factory,2,f.settlement); REQUIRE(p);
  REQUIRE(capital_projects::add_requirement(*f.state,p,f.output,5));
  REQUIRE(inventory::add(*f.state,yard(f,p),f.output,5,f.employer) == 5);
  capital_projects::process_projects(*f.state);
  REQUIRE(f.state->world.factory_get_productive_capacity(f.factory) == 3);
  capital_projects::process_projects(*f.state);
  REQUIRE(f.state->world.factory_get_productive_capacity(f.factory) == 3);
 }
 SECTION("military production") {
  auto equipment=f.state->world.create_commodity(); f.state->world.commodity_set_cost(equipment,20);
  commodity_set recipe{}; recipe.commodity_type[0]=f.output; recipe.commodity_amounts[0]=5;
  auto p=capital_projects::create_military_production(*f.state,f.employer,company,f.site,f.settlement,equipment,2,recipe); REQUIRE(p);
  REQUIRE(inventory::quantity(*f.state,yard(f,p),equipment,f.employer) == 0);
  REQUIRE(inventory::add(*f.state,yard(f,p),f.output,5,f.employer) == 5);
  capital_projects::process_projects(*f.state);
  REQUIRE(inventory::quantity(*f.state,yard(f,p),equipment,f.employer) == 2);
  REQUIRE(inventory::quantity(*f.state,yard(f,p),f.output,f.employer) == 0);
  REQUIRE(f.state->world.regiment_size() == 0);
  capital_projects::process_projects(*f.state);
  REQUIRE(inventory::quantity(*f.state,yard(f,p),equipment,f.employer) == 2);
 }
}
TEST_CASE("construction binding and consumed ledger survive normal save load and replay", "[construction-cut]") {
 fixture f; auto company=actors::organizations::operator_organization_for_factory(*f.state,f.factory);
 auto& recipe=f.state->economy_definitions.building_definitions[0].cost; recipe.commodity_type[0]=f.output; recipe.commodity_amounts[0]=5;
 auto p=capital_projects::create_infrastructure(*f.state,f.employer,company,f.site,f.settlement,0); REQUIRE(p);
 REQUIRE(inventory::add(*f.state,yard(f,p),f.output,2,f.employer) == 2);
 capital_projects::process_projects(*f.state); REQUIRE(capital_projects::material_progress(*f.state,p) == Approx(.4));
 std::vector<uint8_t> bytes(sys::sizeof_save_section(*f.state)); auto end=sys::write_save_section(bytes.data(),*f.state); REQUIRE(end==bytes.data()+bytes.size());
 fixture loaded; REQUIRE(sys::read_save_section(bytes.data(),end,*loaded.state)==end);
 REQUIRE(capital_projects::export_requests(*loaded.state).size()==1);
 REQUIRE(capital_projects::material_progress(*loaded.state,p)==Approx(.4));
 for(auto* x:{&f,&loaded}) {
  REQUIRE(inventory::add(*x->state,yard(*x,p),x->output,3,x->employer)==3);
  capital_projects::process_projects(*x->state);
  REQUIRE(x->state->world.province_get_building_level(x->province,0)==1);
  REQUIRE(capital_projects::material_progress(*x->state,p)==1);
 }
}
TEST_CASE("construction procurement is independent of requirement insertion order", "[construction-cut]") {
 fixture a,b;
 auto second_a=a.state->world.create_commodity(), second_b=b.state->world.create_commodity(); REQUIRE(second_a==second_b);
 a.state->world.commodity_set_cost(second_a,20); b.state->world.commodity_set_cost(second_b,20);
 auto pa=project(a), pb=project(b);
 REQUIRE(capital_projects::add_requirement(*a.state,pa,a.output,10)); REQUIRE(capital_projects::add_requirement(*a.state,pa,second_a,5));
 REQUIRE(capital_projects::add_requirement(*b.state,pb,second_b,5)); REQUIRE(capital_projects::add_requirement(*b.state,pb,b.output,10));
 REQUIRE(capital_projects::fund(*a.state,pa,a.payer,24)); REQUIRE(capital_projects::fund(*b.state,pb,b.payer,24));
 capital_projects::process_projects(*a.state); capital_projects::process_projects(*b.state);
 std::vector<std::pair<int,float>> orders_a,orders_b;
 auto collect=[](fixture& f, auto& orders) { f.state->world.for_each_concrete_market_bid([&](auto bid) { orders.push_back({f.state->world.concrete_market_bid_get_commodity_from_concrete_bid_commodity(bid).index(),f.state->world.concrete_market_bid_get_remaining_quantity(bid)}); }); };
 collect(a,orders_a); collect(b,orders_b); REQUIRE(orders_a==orders_b); REQUIRE(orders_a.size()==2);
 REQUIRE(orders_a[0].second==Approx(1)); REQUIRE(orders_a[1].second==Approx(.5));
}
TEST_CASE("legacy purchased goods demand progress and build time cannot advance canonical construction", "[construction-cut]") {
 fixture a,b;
 for(auto* f:{&a,&b}) {
  auto& recipe=f->state->economy_definitions.building_definitions[0].cost; recipe.commodity_type[0]=f->output; recipe.commodity_amounts[0]=10;
  auto company=actors::organizations::operator_organization_for_factory(*f->state,f->factory);
  auto p=capital_projects::create_infrastructure(*f->state,f->employer,company,f->site,f->settlement,0); REQUIRE(p);
  auto nation=f->state->world.province_get_nation_from_province_ownership(f->province);
  auto legacy=f->state->world.force_create_province_building_construction(f->province,nation);
  f->state->world.province_building_construction_set_type(legacy,0);
  auto rows=capital_projects::export_requests(*f->state); rows[0].building=legacy; REQUIRE(capital_projects::import_requests(*f->state,rows));
  f->state->world.market_resize_construction_demand(f->state->world.commodity_size());
  f->state->world.market_resize_private_construction_demand(f->state->world.commodity_size());
 }
 auto pa=capital_projects::export_requests(*a.state)[0].project, pb=capital_projects::export_requests(*b.state)[0].project;
 for(int day=0;day<8;++day) {
  auto legacy=capital_projects::export_requests(*b.state)[0].building;
  if(legacy) b.state->world.province_building_construction_get_purchased_goods(legacy).commodity_amounts[0]=1000000;
  b.state->world.capital_project_set_progress(pb,1);
  b.state->world.market_set_construction_demand(b.market,b.output,1000000);
  b.state->world.market_set_private_construction_demand(b.market,b.output,1000000);
  b.state->economy_definitions.building_definitions[0].time=0;
  for(auto* f:{&a,&b}) { auto p=f==&a?pa:pb; REQUIRE(inventory::add(*f->state,yard(*f,p),f->output,1.25,f->employer)==1.25); capital_projects::process_projects(*f->state); f->state->current_date+=1; }
  REQUIRE(capital_projects::material_progress(*a.state,pa)==capital_projects::material_progress(*b.state,pb));
  REQUIRE(a.state->world.province_get_building_level(a.province,0)==b.state->world.province_get_building_level(b.province,0));
  REQUIRE(accounts::balance(*a.state,account(a,pa))==accounts::balance(*b.state,account(b,pb)));
 }
 REQUIRE(a.state->world.province_get_building_level(a.province,0)==1);
}
TEST_CASE("naval orders create a ship once after actual construction consumption", "[construction-cut]") {
 fixture f;
 f.state->world.nation_resize_modifier_values(sys::national_mod_offsets::count);
 f.state->military_definitions.unit_base_definitions.resize(3);
 auto unit=dcon::unit_type_id{1};
 f.state->military_definitions.unit_base_definitions[unit].is_land=false;
 auto p=project(f,capital_projects::project_kind::naval_construction); REQUIRE(p);
 REQUIRE(capital_projects::add_requirement(*f.state,p,f.output,5));
 capital_projects::request_record row{}; row.project=p; row.unit=unit; row.nation=f.state->world.province_get_nation_from_province_ownership(f.province);
 REQUIRE(capital_projects::import_requests(*f.state,{row}));
 REQUIRE_FALSE(capital_projects::complete(*f.state,p)); REQUIRE(f.state->world.ship_size()==0);
 REQUIRE(inventory::add(*f.state,yard(f,p),f.output,5,f.employer)==5);
 capital_projects::process_projects(*f.state);
 REQUIRE(f.state->world.ship_size()==1); REQUIRE(capital_projects::export_requests(*f.state)[0].ship);
 capital_projects::process_projects(*f.state); REQUIRE(f.state->world.ship_size()==1);
}
TEST_CASE("public construction spends real treasury cash only with fiscal authority", "[construction-cut]") {
 fixture f; auto nation=f.state->world.province_get_nation_from_province_ownership(f.province);
 auto& recipe=f.state->economy_definitions.building_definitions[0].cost; recipe.commodity_type[0]=f.output; recipe.commodity_amounts[0]=5;
 auto authority=governance::create_institution(*f.state,nation,governance::institution_kind::public_works_ministry);
 auto treasury=governance::finance::open_treasury_account(*f.state,authority,f.settlement); REQUIRE(treasury);
 REQUIRE(accounts::bootstrap_set_balance(*f.state,treasury,100)); f.state->world.nation_set_construction_spending(nation,100);
 auto order=f.state->world.force_create_province_building_construction(f.province,nation); f.state->world.province_building_construction_set_type(order,0);
 f.state->world.province_building_construction_get_purchased_goods(order).commodity_amounts[0]=1000000;
 capital_projects::adopt_legacy_requests(*f.state); auto p=capital_projects::project_for(*f.state,order); REQUIRE(p);
 REQUIRE(capital_projects::material_progress(*f.state,p)==0);
 REQUIRE(governance::finance::treasury_account_for(*f.state,authority,f.settlement)==treasury);
 REQUIRE(governance::finance::open_treasury_account(*f.state,authority,f.settlement)==treasury);
 REQUIRE(accounts::balance(*f.state,treasury)==100); REQUIRE(accounts::balance(*f.state,account(f,p))==0);
 REQUIRE(governance::grant_authority_to_institution(*f.state,authority,governance::authority_kind::spend_public_funds,nation));
 capital_projects::adopt_legacy_requests(*f.state);
 REQUIRE(accounts::balance(*f.state,account(f,p))==Approx(60)); REQUIRE(accounts::balance(*f.state,treasury)==Approx(40));
 REQUIRE(accounts::balance(*f.state,treasury)+accounts::balance(*f.state,account(f,p))==100);
 REQUIRE_FALSE(capital_projects::complete(*f.state,p));
}
TEST_CASE("paid materials await a real carrier without duplicate procurement and replay index zero orders", "[construction-cut]") {
 fixture f; auto p=project(f); REQUIRE(capital_projects::add_requirement(*f.state,p,f.output,2)); REQUIRE(capital_projects::fund(*f.state,p,f.payer,100));
 auto seller=actors::organizations::actor_for_organization(*f.state,actors::organizations::create_company(*f.state));
 auto seller_account=accounts::open_account(*f.state,seller,f.settlement); REQUIRE(inventory::add(*f.state,f.site,f.output,2,seller)==2);
 capital_projects::process_projects(*f.state); auto fills=concrete_market::match_all(*f.state,f.output,f.state->current_date); REQUIRE(fills.size()==1);
 REQUIRE(accounts::balance(*f.state,seller_account)==20); REQUIRE(accounts::balance(*f.state,account(f,p))==80); REQUIRE(capital_projects::material_progress(*f.state,p)==0);
 capital_projects::process_projects(*f.state);
 float active=0; f.state->world.for_each_concrete_market_bid([&](auto bid) { if(f.state->world.concrete_market_bid_get_status(bid)==0) active+=f.state->world.concrete_market_bid_get_remaining_quantity(bid); }); REQUIRE(active==0);
 REQUIRE(inventory::quantity(*f.state,yard(f,p),f.output,f.employer)==0);
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
  for(int i=0;i<3;++i) { shipments::advance(*x->state); shipments::process_arrivals(*x->state); x->state->current_date+=1; }
  capital_projects::process_projects(*x->state); REQUIRE(capital_projects::material_progress(*x->state,p)<1);
  // Replenish genuine transport loss through the same paid market and carrier.
  REQUIRE(inventory::add(*x->state,x->site,x->output,.01f,seller)==Approx(.01f));
  for(int i=0;i<4 && capital_projects::material_progress(*x->state,p)<1;++i) {
   capital_projects::process_projects(*x->state); concrete_market::match_all(*x->state,x->output,x->state->current_date);
   freight_market::process_pending_requests(*x->state); shipments::process_arrivals(*x->state); x->state->current_date+=1;
  }
  capital_projects::process_projects(*x->state); REQUIRE(capital_projects::material_progress(*x->state,p)==1);
  REQUIRE(x->state->world.capital_project_get_factory_from_capital_project_factory(p));
  REQUIRE(accounts::balance(*x->state,x->payer)+accounts::balance(*x->state,account(*x,p))+accounts::balance(*x->state,seller_account)+accounts::balance(*x->state,carrier_account)==Approx(10000));
 }
}
TEST_CASE("a small missing material cannot disappear through progress rounding", "[construction-cut]") {
 fixture f; auto p=project(f); auto component=f.state->world.create_commodity();
 auto bulk=capital_projects::add_requirement(*f.state,p,f.output,1000000); REQUIRE(bulk);
 REQUIRE(capital_projects::add_requirement(*f.state,p,component,.001f));
 REQUIRE(inventory::add(*f.state,yard(f,p),f.output,1000000,f.employer)==1000000);
 REQUIRE(capital_projects::consume(*f.state,bulk,1000000)==1000000);
 REQUIRE(capital_projects::material_progress(*f.state,p)<1); REQUIRE_FALSE(capital_projects::complete(*f.state,p));
 REQUIRE_FALSE(f.state->world.capital_project_get_factory_from_capital_project_factory(p));
}
TEST_CASE("delivered in kind construction does not require a legacy market zone", "[construction-cut]") {
 fixture f; auto p=project(f); REQUIRE(capital_projects::add_requirement(*f.state,p,f.output,2));
 f.state->world.province_set_state_membership(f.province,{});
 REQUIRE(inventory::add(*f.state,yard(f,p),f.output,2,f.employer)==2);
 capital_projects::process_projects(*f.state); REQUIRE(capital_projects::material_progress(*f.state,p)==1);
 REQUIRE(f.state->world.capital_project_get_factory_from_capital_project_factory(p));
}
TEST_CASE("completed rail construction changes physical edge capacity", "[construction-cut]") {
 fixture f; auto other=f.state->world.create_province(); f.state->world.province_set_building_level(other,0,1);
 auto other_node=f.state->world.create_infrastructure_node(); f.state->world.force_create_infrastructure_node_location(other_node,other);
 auto node=world::spatial_runtime::node_for_site(*f.state,f.site);
 auto edge=f.state->world.create_infrastructure_edge(); f.state->world.force_create_infrastructure_edge_from(edge,node); f.state->world.force_create_infrastructure_edge_to(edge,other_node); f.state->world.infrastructure_edge_set_distance(edge,1);
 auto before=world::spatial_runtime::effective_capacity(*f.state,edge);
 auto& recipe=f.state->economy_definitions.building_definitions[0].cost; recipe.commodity_type[0]=f.output; recipe.commodity_amounts[0]=2;
 auto company=actors::organizations::operator_organization_for_factory(*f.state,f.factory);
 auto p=capital_projects::create_infrastructure(*f.state,f.employer,company,f.site,f.settlement,0); REQUIRE(p);
 REQUIRE(f.state->world.infrastructure_edge_get_type(edge)==0);
 REQUIRE(inventory::add(*f.state,yard(f,p),f.output,2,f.employer)==2); capital_projects::process_projects(*f.state);
 REQUIRE(f.state->world.infrastructure_edge_get_type(edge)==1); REQUIRE(world::spatial_runtime::effective_capacity(*f.state,edge)>before);
}
TEST_CASE("construction funding cannot spend cash reserved by another concrete order", "[construction-cut]") {
 fixture f; auto p=project(f); REQUIRE(capital_projects::add_requirement(*f.state,p,f.output,2));
 REQUIRE(concrete_market::post_bid(*f.state,f.employer,f.payer,f.site,f.market,f.output,999,10,{}));
 REQUIRE_FALSE(capital_projects::fund(*f.state,p,f.payer,20)); REQUIRE(accounts::balance(*f.state,account(f,p))==0);
 REQUIRE(capital_projects::fund(*f.state,p,f.payer,10));
 REQUIRE(accounts::balance(*f.state,f.payer)==9990); REQUIRE(concrete_market::reserved_bid_amount(*f.state,f.payer)==9990);
}
