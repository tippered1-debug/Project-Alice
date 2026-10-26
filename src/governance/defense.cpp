#include "defense.hpp"

#include "actors/organizations/organizations.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/economy_stats.hpp"
#include "economy/households.hpp"
#include "economy/money.hpp"
#include "economy/physical/concrete_market.hpp"
#include "economy/physical/inventory.hpp"
#include "economy/relations/relations.hpp"
#include "economy/wallets.hpp"
#include "governance/finance/finance.hpp"
#include "governance/governance.hpp"
#include "military/land_forces.hpp"
#include "persons/exact_population.hpp"
#include "persons/persons.hpp"
#include "system_state.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <tuple>
#include <vector>

namespace governance::defense {
namespace {

constexpr float epsilon = 1.0e-5f;

using economy::relations::obligation_kind;
using economy::relations::obligation_status;
using military::land_forces::stockpile;

// Serving soldiers whose families belong to one household cohort.
struct family_group {
	dcon::organization_id household{};
	dcon::province_id home{};
	uint64_t soldiers = 0;
	std::vector<persons::exact_population::military_assignment_range> ranges;
};

std::map<uint32_t, family_group> family_groups(sys::state const& state, dcon::nation_id nation) {
	std::map<uint32_t, family_group> result;
	if(!state.exact_population || !military::land_forces::initialized(state)) return result;
	auto formations = military::land_forces::formations_of(state, nation);
	if(formations.empty()) return result;
	std::set<uint64_t> owned(formations.begin(), formations.end());
	for(auto const& range : persons::exact_population::all_military_assignments(state)) {
		if(!owned.contains(range.formation_id)) continue;
		auto population = persons::population_for_source_cell(state, range.source_population_cell);
		auto home = population ? state.world.pop_get_province_from_pop_location(population) : dcon::province_id{};
		auto role = population ? economy::households::role_for_pop_type(state, state.world.pop_get_poptype(population))
			: economy::households::role::none;
		auto household = home ? economy::households::household_for(state, home, role) : dcon::organization_id{};
		// Bonded soldiers have no free cohort and draw no pay.
		if(!household) continue;
		auto living = persons::exact_population::living_people_in_person_range(state,
			range.source_population_cell, range.first_ordinal, range.count, range.ordinal_stride);
		if(living == 0) continue;
		auto& group = result[uint32_t(household.index())];
		group.household = household;
		group.home = home;
		group.soldiers += living;
		group.ranges.push_back(range);
	}
	return result;
}

dcon::obligation_id arrears_obligation(sys::state const& state, dcon::economic_actor_id command,
	dcon::economic_actor_id household) {
	dcon::obligation_id result{};
	if(!command || !household) return result;
	state.world.economic_actor_for_each_obligation_debtor_as_economic_actor(command,
		[&](dcon::obligation_debtor_id relation) {
			auto obligation = state.world.obligation_debtor_get_obligation(relation);
			if(result || !obligation
				|| state.world.obligation_get_kind(obligation) != uint8_t(obligation_kind::payroll)
				|| state.world.obligation_get_status(obligation) != uint8_t(obligation_status::active)
				|| state.world.obligation_get_economic_actor_from_obligation_creditor(obligation) != household) return;
			result = obligation;
		});
	return result;
}

void add_arrears(sys::state& state, dcon::economic_actor_id command, dcon::economic_actor_id household,
	float amount) {
	if(!(amount > epsilon) || !std::isfinite(amount)) return;
	if(auto obligation = arrears_obligation(state, command, household)) {
		state.world.obligation_set_original_principal(obligation,
			state.world.obligation_get_original_principal(obligation) + amount);
		state.world.obligation_set_principal_outstanding(obligation,
			state.world.obligation_get_principal_outstanding(obligation) + amount);
		state.world.obligation_set_due_date(obligation, state.current_date);
		return;
	}
	(void)economy::relations::create_obligation(state, command, household, amount, economy::money,
		state.current_date, state.current_date, 0.0f, obligation_kind::payroll);
}

float free_cash(sys::state const& state, dcon::monetary_account_id account) {
	if(!account) return 0.0f;
	auto cash = economy::accounts::balance(state, account)
		- economy::physical::concrete_market::reserved_bid_amount(state, account);
	return std::isfinite(cash) ? std::max(0.0f, cash) : 0.0f;
}

dcon::monetary_account_id command_treasury(sys::state const& state, dcon::institution_id command) {
	return command ? finance::treasury_account_for(state, command, economy::money) : dcon::monetary_account_id{};
}

// The most recently recruited soldiers of a group leave first.
uint64_t release(sys::state& state, family_group const& group, uint64_t leaving) {
	auto ranges = group.ranges;
	std::sort(ranges.begin(), ranges.end(), [](auto const& left, auto const& right) {
		return std::tie(left.formation_id, left.source_population_cell, left.first_ordinal)
			> std::tie(right.formation_id, right.source_population_cell, right.first_ordinal);
	});
	uint64_t released = 0;
	for(auto const& range : ranges) {
		for(uint64_t offset = range.count; offset > 0 && released < leaving; --offset) {
			persons::person_key person{ range.source_population_cell,
				range.first_ordinal + (offset - 1) * uint64_t(range.ordinal_stride) };
			if(!persons::alive(state, person)) continue;
			if(persons::exact_population::unassign_military_person(state, person, range.formation_id)) ++released;
		}
		if(released >= leaving) break;
	}
	return released;
}

double in_transit_to(sys::state const& state, dcon::economic_actor_id buyer, dcon::site_id site,
	dcon::commodity_id commodity) {
	double result = economy::physical::inventory::quantity(state, site, commodity, buyer);
	state.world.site_for_each_shipment_destination_as_site(site, [&](dcon::shipment_destination_id relation) {
		auto shipment = state.world.shipment_destination_get_shipment(relation);
		auto owner = shipment ? state.world.shipment_get_shipment_owner(shipment) : dcon::shipment_owner_id{};
		if(!owner || state.world.shipment_owner_get_economic_actor(owner) != buyer
			|| state.world.shipment_get_commodity(shipment) != commodity) return;
		result += std::max(0.0f, state.world.shipment_get_remaining_quantity(shipment));
	});
	return result;
}

std::vector<stockpile> preferred_destinations(std::vector<stockpile> const& stores) {
	std::vector<stockpile> result;
	for(auto const& pile : stores)
		if(pile.kind == military::land_forces::stockpile_kind::warehouse) result.push_back(pile);
	return result.empty() ? stores : result;
}

} // namespace

dcon::institution_id military_command_for(sys::state const& state, dcon::nation_id nation) {
	return nation ? governance::find_institution(state, nation, institution_kind::military_command) : dcon::institution_id{};
}

float monthly_pay(sys::state const& state, dcon::nation_id nation, dcon::province_id home) {
	auto command = military_command_for(state, nation);
	auto multiplier = command ? state.world.institution_get_staff_wage_multiplier(command) : 1.0f;
	if(!std::isfinite(multiplier) || multiplier <= 0.0f) multiplier = 1.0f;
	auto wage = home ? state.world.province_get_labor_price(home, economy::labor::no_education) : 0.0f;
	if(!std::isfinite(wage)) wage = 0.0f;
	return pay_period_days * std::max(0.01f, wage) * multiplier;
}

float monthly_payroll(sys::state const& state, dcon::nation_id nation) {
	double total = 0.0;
	for(auto const& [index, group] : family_groups(state, nation))
		total += double(group.soldiers) * monthly_pay(state, nation, group.home);
	return float(total);
}

float pay_arrears(sys::state const& state, dcon::nation_id nation) {
	auto command = military_command_for(state, nation);
	auto actor = command ? governance::actor_for_institution(state, command) : dcon::economic_actor_id{};
	float total = 0.0f;
	if(!actor) return total;
	state.world.economic_actor_for_each_obligation_debtor_as_economic_actor(actor,
		[&](dcon::obligation_debtor_id relation) {
			auto obligation = state.world.obligation_debtor_get_obligation(relation);
			if(obligation && state.world.obligation_get_kind(obligation) == uint8_t(obligation_kind::payroll)
				&& state.world.obligation_get_status(obligation) == uint8_t(obligation_status::active))
				total += economy::relations::total_due(state, obligation);
		});
	return total;
}

payroll_result pay_armed_forces(sys::state& state, dcon::nation_id nation) {
	payroll_result result{};
	auto command = military_command_for(state, nation);
	auto actor = command ? governance::actor_for_institution(state, command) : dcon::economic_actor_id{};
	auto treasury = command_treasury(state, command);
	if(!actor || !treasury) return result;
	auto groups = family_groups(state, nation);
	struct claim {
		family_group const* group = nullptr;
		dcon::economic_actor_id household{};
		float arrears = 0.0f;
		float due = 0.0f;
	};
	std::vector<claim> claims;
	double total = 0.0;
	for(auto const& [index, group] : groups) {
		auto household = actors::organizations::actor_for_organization(state, group.household);
		if(!household) continue;
		auto obligation = arrears_obligation(state, actor, household);
		auto arrears = obligation ? economy::relations::total_due(state, obligation) : 0.0f;
		auto due = float(group.soldiers) * monthly_pay(state, nation, group.home);
		claims.push_back({ &group, household, arrears, due });
		total += double(arrears) + double(due);
		result.soldiers += group.soldiers;
		result.due += due;
	}
	auto available = double(free_cash(state, treasury));
	// Shortfalls are shared pro rata; a small margin keeps the last payment
	// within the cash that rounding leaves.
	auto ratio = total > 0.0 ? (available >= total ? 1.0 : available / total * 0.999999) : 0.0;
	for(auto const& item : claims) {
		auto payment = float((double(item.arrears) + double(item.due)) * ratio);
		float paid = 0.0f;
		if(payment > epsilon) {
			auto wallet = economy::wallets::open_for(state, item.household, economy::money);
			if(wallet && finance::authorized_pay_by_institution(state, command, treasury, wallet, payment,
				economy::relations::transaction_kind::payroll, state.current_date)) paid = payment;
		}
		float to_arrears = 0.0f;
		if(auto obligation = arrears_obligation(state, actor, item.household); obligation && paid > epsilon && item.arrears > 0.0f)
			to_arrears = economy::relations::repay_obligation(state, obligation, std::min(paid, item.arrears));
		result.arrears_paid += to_arrears;
		result.paid += paid - to_arrears;
		add_arrears(state, actor, item.household, item.due - (paid - to_arrears));
		auto owed = arrears_obligation(state, actor, item.household);
		auto owed_amount = owed ? economy::relations::total_due(state, owed) : 0.0f;
		if(item.due > epsilon && owed_amount > desertion_arrears_months * item.due) {
			auto leaving = uint64_t(std::ceil(double(item.group->soldiers) * double(monthly_desertion_share)));
			result.deserted += release(state, *item.group, leaving);
		}
	}
	result.arrears = pay_arrears(state, nation);
	return result;
}

void process_payroll(sys::state& state) {
	if(state.current_date.to_ymd(state.start_date).day != 1) return;
	state.world.for_each_nation([&](dcon::nation_id nation) { (void)pay_armed_forces(state, nation); });
}

void post_procurement_bids(sys::state& state) {
	if(!military::land_forces::initialized(state)) return;
	using military::land_forces::consumable_kind;
	using military::land_forces::cargo_kind;
	state.world.for_each_nation([&](dcon::nation_id nation) {
		auto piles = military::land_forces::stockpiles_of(state, nation);
		if(piles.empty()) return;
		auto command = military_command_for(state, nation);
		auto actor = command ? governance::actor_for_institution(state, command) : dcon::economic_actor_id{};
		auto treasury = command_treasury(state, command);
		if(!actor || !treasury) return;
		// Pay comes before stores: only cash beyond the next payroll and the
		// arrears is spent.
		auto reserve = monthly_payroll(state, nation) + pay_arrears(state, nation);
		auto budget = std::max(0.0f, free_cash(state, treasury) - reserve) * daily_procurement_share;
		if(budget <= epsilon) return;
		auto formations = military::land_forces::formations_of(state, nation);

		auto held_in_stores = [&](std::vector<stockpile> const& stores, dcon::commodity_id commodity) {
			double held = 0.0;
			std::set<uint32_t> sites;
			for(auto const& pile : stores) {
				held += military::land_forces::stockpile_stock(state, pile);
				if(sites.insert(uint32_t(pile.location.index())).second)
					held += in_transit_to(state, actor, pile.location, commodity);
			}
			return held;
		};
		auto buy = [&](dcon::commodity_id commodity, double shortfall, std::vector<stockpile> const& stores) {
			if(!commodity || !(shortfall > epsilon) || stores.empty()) return;
			auto destinations = preferred_destinations(stores);
			auto share = shortfall / double(destinations.size());
			for(auto const& pile : destinations) {
				auto market = economy::physical::concrete_market::market_for_site(state, pile.location);
				if(!market) continue;
				auto price = economy::physical::concrete_market::canonical_reference_price(state, market, commodity,
					state.current_date, state.world.commodity_get_cost(commodity));
				if(!std::isfinite(price) || price <= 0.0f) continue;
				auto limit = price * procurement_markup;
				auto quantity = float(std::min(share, double(budget / limit)));
				if(!(quantity > epsilon)) continue;
				if(economy::physical::concrete_market::post_bid(state, actor, treasury, pile.location, market,
					commodity, quantity, limit, economy::physical::concrete_market::order_purpose::general))
					budget -= quantity * limit;
			}
		};

		for(auto kind : { consumable_kind::food, consumable_kind::fuel, consumable_kind::ammunition }) {
			std::vector<stockpile> stores;
			for(auto const& pile : piles)
				if(pile.cargo == cargo_kind::consumable && pile.consumable == kind
					&& military::land_forces::stockpile_commodity(state, pile)) stores.push_back(pile);
			if(stores.empty()) continue;
			auto commodity = military::land_forces::stockpile_commodity(state, stores.front());
			double demand = 0.0, held = held_in_stores(stores, commodity);
			for(auto formation : formations) {
				demand += military::land_forces::daily_consumable_demand(state, formation, kind);
				held += military::land_forces::consumable_quantity(state, formation, kind);
			}
			if(demand > 0.0) buy(commodity, double(supply_target_days) * demand - held, stores);
		}

		std::map<uint64_t, std::vector<stockpile>> by_model;
		for(auto const& pile : piles)
			if(pile.cargo == cargo_kind::equipment && military::land_forces::stockpile_commodity(state, pile))
				by_model[pile.equipment_model_id].push_back(pile);
		for(auto const& [model, stores] : by_model) {
			auto commodity = military::land_forces::stockpile_commodity(state, stores.front());
			double authorized = 0.0, held = held_in_stores(stores, commodity);
			for(auto formation : formations) {
				authorized += double(military::land_forces::equipment_authorization(state, formation, model));
				held += double(military::land_forces::equipment_count(state, formation, model));
			}
			buy(commodity, std::floor(authorized - held), stores);
		}
	});
}

void take_delivery(sys::state& state) {
	if(!military::land_forces::initialized(state)) return;
	state.world.for_each_nation([&](dcon::nation_id nation) {
		auto piles = military::land_forces::stockpiles_of(state, nation);
		if(piles.empty()) return;
		auto command = military_command_for(state, nation);
		auto actor = command ? governance::actor_for_institution(state, command) : dcon::economic_actor_id{};
		auto custodian = military::land_forces::stockpile_custodian(state, nation);
		if(!actor || !custodian || actor == custodian) return;
		for(auto const& pile : piles) {
			auto commodity = military::land_forces::stockpile_commodity(state, pile);
			if(!commodity) continue;
			auto quantity = economy::physical::inventory::quantity(state, pile.location, commodity, actor);
			if(pile.cargo == military::land_forces::cargo_kind::equipment) quantity = std::floor(quantity);
			if(quantity > epsilon)
				(void)economy::physical::inventory::transfer(state, pile.location, commodity, actor, custodian, quantity);
		}
	});
}

} // namespace governance::defense
