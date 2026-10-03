#include "public_administration.hpp"

#include "actors/ownership.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/economy_stats.hpp"
#include "economy/money.hpp"
#include "economy/physical/concrete_market.hpp"
#include "economy/physical/job_market.hpp"
#include "economy/payroll.hpp"
#include "economy/physical/inventory.hpp"
#include "economy/exact_person_economy.hpp"
#include "economy/demographics.hpp"
#include "governance/constitution.hpp"
#include "governance/finance/finance.hpp"
#include "governance/law/law.hpp"
#include "system_state.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <vector>

namespace governance::public_administration {
namespace {

dcon::site_id site_for_province(sys::state const& state, dcon::province_id province) {
	dcon::site_id result{};
	state.world.province_for_each_site_location_as_province(province,
		[&](dcon::site_location_id relation) {
			if(!result) result = state.world.site_location_get_site(relation);
		});
	return result;
}

dcon::commodity_id commodity_named(sys::state const& state, char const* name) {
	dcon::commodity_id result{};
	state.world.for_each_commodity([&](dcon::commodity_id commodity) {
		if(!result && state.to_string_view(state.world.commodity_get_name(commodity)) == name)
			result = commodity;
	});
	return result;
}

float positive(float value) { return std::isfinite(value) ? std::max(0.0f, value) : 0.0f; }

float active_monthly_payroll(sys::state const& state, dcon::institution_id institution) {
	float total = 0.0f;
	for(auto id : economy::exact_person_economy::active_contracts_for_institution(state, institution)) {
		auto contract = economy::exact_person_economy::contract(state, id);
		if(contract && std::isfinite(contract->wage_rate) && contract->wage_rate > 0.0f && std::isfinite(contract->labor_capacity) && contract->labor_capacity > 0.0f)
			total += contract->wage_rate * contract->labor_capacity;
	}
	return positive(total);
}

float active_capacity_at(sys::state const& state, dcon::institution_id institution,
	dcon::province_id province, uint8_t occupation) {
	float total = 0.0f;
	for(auto id : economy::exact_person_economy::active_contracts_for_institution(state, institution)) {
		auto contract = economy::exact_person_economy::contract(state, id);
		if(!contract || contract->occupation != occupation || !contract->workplace || state.world.site_get_province_from_site_location(contract->workplace) != province) continue;
		total += positive(contract->labor_capacity);
	}
	return total;
}

float open_capacity_at(sys::state const& state, dcon::institution_id institution,
	dcon::province_id province, uint8_t occupation) {
	float total = 0.0f;
	state.world.institution_for_each_job_offer_institution_as_institution(institution, [&](auto relation) {
		auto offer = state.world.job_offer_institution_get_job_offer(relation);
		if(!offer || state.world.job_offer_get_status(offer) != uint8_t(economy::physical::job_market::offer_status::open) || state.world.job_offer_get_occupation(offer) != occupation || state.world.job_offer_get_expires_on(offer) && state.world.job_offer_get_expires_on(offer) < state.current_date) return;
		auto site = state.world.job_offer_get_site_from_job_offer_site(offer);
		if(!site || state.world.site_get_province_from_site_location(site) != province) return;
		total += positive(state.world.job_offer_get_labor_capacity(offer))
			* float(state.world.job_offer_get_openings(offer));
	});
	return total;
}

float open_monthly_payroll(sys::state const& state, dcon::institution_id institution) {
	float total = 0.0f;
	state.world.institution_for_each_job_offer_institution_as_institution(institution, [&](auto relation) {
		auto offer = state.world.job_offer_institution_get_job_offer(relation);
		if(!offer || state.world.job_offer_get_status(offer) != uint8_t(economy::physical::job_market::offer_status::open) || state.world.job_offer_get_expires_on(offer) && state.world.job_offer_get_expires_on(offer) < state.current_date) return;
		auto wage = state.world.job_offer_get_wage_rate(offer);
		auto capacity = state.world.job_offer_get_labor_capacity(offer);
		if(!std::isfinite(wage) || wage <= 0.0f || !std::isfinite(capacity) || capacity <= 0.0f) return;
		total += wage * capacity * float(state.world.job_offer_get_openings(offer));
	});
	return positive(total);
}

// Where an institution's staff work: population per state market inside its
// jurisdiction, counted at the market's capital province (or the first
// province of the jurisdiction in that market).
struct staffing_area {
	dcon::province_id province{};
	float population = 0.0f;
};

std::vector<staffing_area> staffing_areas(sys::state const& state, dcon::nation_id nation, dcon::institution_id institution) {
	std::map<uint32_t, staffing_area> by_state;
	auto scope = governance::jurisdiction_of(state, institution);
	for(auto province : governance::provinces_in(state, scope)) {
		if(state.world.province_get_nation_from_province_control(province) != nation) continue;
		auto state_instance = state.world.province_get_state_membership(province);
		if(!state_instance) continue;
		auto& area = by_state[state_instance.id.index()];
		area.population += std::max(0.0f, state.world.province_get_demographics(province, demographics::total));
		auto capital = state.world.state_instance_get_capital(state_instance);
		if(governance::contains(state, scope, capital)) area.province = capital;
		else if(!area.province) area.province = province;
	}
	std::vector<staffing_area> result;
	for(auto const& [index, area] : by_state)
		if(area.population > 0.0f && area.province) result.push_back(area);
	return result;
}

// An institution recruits the staff its mandate funds, where it governs, as
// far as its own treasury pays for a month of wages.
void post_public_vacancies(sys::state& state, dcon::nation_id nation, dcon::institution_id institution,
	dcon::monetary_account_id account) {
	auto positions_per_person = state.world.institution_get_staffing_per_capita(institution);
	auto occupation = state.world.institution_get_staff_occupation(institution);
	auto wage_multiplier = state.world.institution_get_staff_wage_multiplier(institution);
	if(!account || !(positions_per_person > 0.0f)) return;
	auto lane = occupation == 0 ? economy::labor::no_education : economy::labor::high_education_and_accepted;
	float const monthly_cost = active_monthly_payroll(state, institution) + open_monthly_payroll(state, institution);
	auto available_monthly_funds = std::max(0.0f, economy::accounts::balance(state, account) - monthly_cost
		- economy::physical::concrete_market::reserved_bid_amount(state, account));
	for(auto const& area : staffing_areas(state, nation, institution)) {
		auto site = site_for_province(state, area.province);
		if(!site) continue;
		auto target = area.population * positions_per_person;
		auto shortage = std::max(0.0f, target - active_capacity_at(state, institution, area.province, occupation)
			- open_capacity_at(state, institution, area.province, occupation));
		if(shortage < 0.5f) continue;
		auto daily_wage = std::max(0.01f, state.world.province_get_labor_price(area.province, lane) * std::max(0.01f, wage_multiplier));
		auto monthly_wage = daily_wage * 30.0f;
		if(!std::isfinite(monthly_wage) || monthly_wage <= 0.0f || available_monthly_funds < monthly_wage) continue;
		auto openings = std::min(uint32_t(std::ceil(shortage)),
			uint32_t(std::min<double>(double(std::numeric_limits<uint32_t>::max()), std::floor(double(available_monthly_funds / monthly_wage)))));
		if(openings == 0) continue;
		if(economy::physical::job_market::post_institution_job_offer(state, institution, site, occupation, 1.0f,
			monthly_wage, 30, account, openings, state.current_date))
			available_monthly_funds -= float(openings) * monthly_wage;
	}
}

// The seat of a territorial government: the first province of its territory.
dcon::site_id seat_of(sys::state const& state, dcon::institution_id institution) {
	auto provinces = governance::provinces_in(state, governance::jurisdiction_of(state, institution));
	return provinces.empty() ? dcon::site_id{} : site_for_province(state, provinces.front());
}

} // namespace

dcon::institution_id institution_for(sys::state const& state, dcon::nation_id nation, governance::institution_kind kind) {
	return governance::find_institution(state, nation, kind);
}

dcon::institution_id tax_authority_for(sys::state const& state, dcon::nation_id nation) {
	return governance::find_institution(state, nation, governance::institution_kind::tax_authority);
}

dcon::monetary_account_id tax_treasury_for(sys::state const& state, dcon::nation_id nation) {
	auto institution = tax_authority_for(state, nation);
	return institution ? finance::treasury_account_for(state, institution, economy::money) : dcon::monetary_account_id{};
}

dcon::economic_actor_id household_sector_actor(sys::state const& state, dcon::nation_id nation) {
	auto institution = governance::find_institution(state, nation, governance::institution_kind::household_sector);
	return institution ? governance::actor_for_institution(state, institution) : dcon::economic_actor_id{};
}

dcon::monetary_account_id household_sector_account(sys::state& state, dcon::nation_id nation) {
	auto actor = household_sector_actor(state, nation);
	if(!actor) return {};
	auto account = economy::accounts::find_account(state, actor, economy::money);
	return account ? account : economy::accounts::open_account(state, actor, economy::money);
}

void bootstrap(sys::state& state) {
	state.world.for_each_nation([&](dcon::nation_id nation) {
		auto central = governance::central_government_for(state, nation);
		// The household sector is the pass-through account wage taxes are
		// collected through; it holds no public power.
		auto household_sector = governance::find_institution(state, nation, governance::institution_kind::household_sector);
		if(!household_sector) {
			household_sector = governance::create_institution(state, nation, governance::institution_kind::household_sector);
			(void)governance::set_parent(state, household_sector, central);
		}
		auto household_actor = governance::actor_for_institution(state, household_sector);
		if(household_actor) state.world.economic_actor_set_kind(household_actor, uint8_t(actors::ownership::actor_kind::other));
		if(household_actor && !economy::accounts::find_account(state, household_actor, economy::money))
			(void)economy::accounts::open_account(state, household_actor, economy::money);
		for(auto institution : governance::institutions_of(state, nation))
			if(institution != household_sector) (void)finance::open_treasury_account(state, institution, economy::money);
	});
	synchronize_local_governments(state);
}

void synchronize_local_governments(sys::state& state) {
	governance::constitution::synchronize(state);
	state.world.for_each_nation([&](dcon::nation_id nation) {
		for(auto institution : governance::institutions_of(state, nation))
			if(governance::territory_of(state, institution) && state.world.commodity_is_valid(economy::money))
				(void)finance::open_treasury_account(state, institution, economy::money);
	});
}

float treasury_cash(sys::state const& state, dcon::nation_id nation) {
	float total = 0.0f;
	for(auto institution : governance::institutions_of(state, nation)) {
		if(governance::kind_of(state, institution) == governance::institution_kind::household_sector) continue;
		total += economy::accounts::balance(state, finance::treasury_account_for(state, institution, economy::money));
	}
	return positive(total);
}

float daily_budget(sys::state const& state, dcon::nation_id nation) {
	// A projection for the interface: a thirty-day envelope of public cash.
	return treasury_cash(state, nation) / 30.0f;
}

void allocate_budget(sys::state& state, dcon::nation_id nation) {
	if(state.current_date.to_ymd(state.start_date).day != 1) return;
	auto date = state.current_date;
	auto finance_ministry = governance::find_institution(state, nation, governance::institution_kind::finance_ministry);
	auto treasury = finance_ministry ? finance::treasury_account_for(state, finance_ministry, economy::money) : dcon::monetary_account_id{};
	if(!treasury) return;
	// Revenue collected by the tax authority goes to the national treasury.
	auto tax_authority = tax_authority_for(state, nation);
	auto tax_treasury = tax_treasury_for(state, nation);
	if(tax_authority && tax_treasury && tax_treasury != treasury) {
		auto collected = economy::accounts::balance(state, tax_treasury) - economy::physical::concrete_market::reserved_bid_amount(state, tax_treasury);
		if(collected > 0.001f) (void)finance::authorized_spend_by_institution(state, tax_authority, tax_treasury, treasury, collected, date);
	}
	// The national treasury disburses what the fiscal law appropriates.
	auto scope = governance::jurisdiction_of(state, finance_ministry);
	auto rate = governance::law::effective_amount(state, scope, governance::law::policy_rule_kind::disbursement_rate, date);
	auto shares = governance::law::appropriations(state, scope, date);
	if(!rate || shares.empty()) return;
	auto available = std::max(0.0f, economy::accounts::balance(state, treasury)
		- economy::physical::concrete_market::reserved_bid_amount(state, treasury)) * *rate;
	float total = 0.0f;
	for(auto const& entry : shares) if(entry.institution != finance_ministry) total += entry.share;
	if(available <= 0.001f || total <= 0.0f) return;
	for(auto const& entry : shares) {
		if(entry.institution == finance_ministry) continue;
		(void)finance::authorized_allocate(state, finance_ministry, entry.institution, economy::money, available * entry.share / total, date);
	}
}

void plan_public_staffing(sys::state& state, dcon::nation_id nation) {
	if(!nation || !state.world.nation_is_valid(nation)) return;
	for(auto ownership : state.world.nation_get_province_ownership(nation)) {
		auto province = ownership.get_province().id;
		state.world.province_set_public_bureaucrat_staffing(province, 0.0f);
		state.world.province_set_public_teacher_staffing(province, 0.0f);
		state.world.province_set_public_police_staffing(province, 0.0f);
	}
	std::map<uint32_t, std::array<float, 3>> staffing; // bureaucrats, teachers, police
	std::map<uint32_t, dcon::province_id> provinces;
	for(auto institution : governance::institutions_of(state, nation)) {
		if(!(state.world.institution_get_staffing_per_capita(institution) > 0.0f)) continue;
		auto account = finance::treasury_account_for(state, institution, economy::money);
		post_public_vacancies(state, nation, institution, account);
		auto service = governance::service_kind(state.world.institution_get_service(institution));
		int32_t lane = service == governance::service_kind::education ? 1
			: service == governance::service_kind::policing ? 2
			: service == governance::service_kind::administration ? 0 : -1;
		if(lane < 0) continue;
		auto occupation = state.world.institution_get_staff_occupation(institution);
		for(auto const& area : staffing_areas(state, nation, institution)) {
			staffing[area.province.index()][size_t(lane)] += active_capacity_at(state, institution, area.province, occupation);
			provinces[area.province.index()] = area.province;
		}
	}
	// Filled public jobs create the labor demand behind school output and
	// policy execution in each market.
	for(auto const& [index, staff] : staffing) {
		auto province = provinces.at(index);
		state.world.province_set_public_bureaucrat_staffing(province, staff[0]);
		state.world.province_set_public_teacher_staffing(province, staff[1]);
		state.world.province_set_public_police_staffing(province, staff[2]);
		state.world.province_set_administration_employment_target(province, staff[0]);
		state.world.province_set_labor_demand(province, economy::labor::high_education_and_accepted,
			state.world.province_get_labor_demand(province, economy::labor::high_education_and_accepted) + staff[0] + staff[1]);
		state.world.province_set_labor_demand(province, economy::labor::no_education,
			state.world.province_get_labor_demand(province, economy::labor::no_education) + staff[2]);
	}
	auto capital = state.world.nation_get_capital(nation);
	auto capital_state = capital ? state.world.province_get_state_membership(capital) : dcon::state_instance_id{};
	dcon::province_id capital_province = capital_state ? state.world.state_instance_get_capital(capital_state) : capital;
	auto entry = capital_province ? staffing.find(capital_province.index()) : staffing.end();
	state.world.nation_set_administration_employment_target_in_capital(nation, entry == staffing.end() ? 0.0f : entry->second[0]);
}

void settle_public_payroll(sys::state& state) {
	struct daily_public_payroll {
		dcon::province_id province{};
		dcon::commodity_id settlement{};
		float due_no = 0.0f;
		float due_basic = 0.0f;
		float due_high = 0.0f;
		float paid_no = 0.0f;
		float paid_basic = 0.0f;
		float paid_high = 0.0f;
	};
	std::map<std::pair<uint32_t, uint32_t>, daily_public_payroll> by_province;
	auto accrue = [&](dcon::province_id province, dcon::commodity_id settlement,
		uint8_t occupation, float due, float paid) {
		if(!province || !settlement || due <= 0.000001f) return;
		auto& totals = by_province[{uint32_t(province.index()), uint32_t(settlement.index())}];
		totals.province = province;
		totals.settlement = settlement;
		if(occupation == 0) { totals.due_no += due; totals.paid_no += std::min(due, paid); }
		else if(occupation == 1) { totals.due_basic += due; totals.paid_basic += std::min(due, paid); }
		else { totals.due_high += due; totals.paid_high += std::min(due, paid); }
	};
	state.world.for_each_nation([&](dcon::nation_id nation) {
		for(auto institution : governance::institutions_of(state, nation)) {
			for(auto contract_id : economy::exact_person_economy::contracts_for_institution(state, institution)) {
				auto record = economy::exact_person_economy::contract(state, contract_id);
				if(!record) continue;
				auto due = economy::exact_person_economy::wage_due(state, contract_id);
				auto result = economy::exact_person_economy::settle_contract_wage(state, contract_id);
				auto province = record->workplace
					? state.world.site_get_province_from_site_location(record->workplace) : dcon::province_id{};
				accrue(province, economy::accounts::settlement_of(state, record->payer_account),
					record->occupation, due, result.current_paid);
			}
		}
	});
	for(auto const& [key, totals] : by_province) {
		(void)key;
		economy::payroll::record_public_payroll(state, totals.province, totals.settlement,
			totals.due_no, totals.due_basic, totals.due_high,
			totals.paid_no, totals.paid_basic, totals.paid_high);
	}
}

void post_procurement_bids(sys::state& state) {
	auto paper = commodity_named(state, "paper");
	auto furniture = commodity_named(state, "furniture");
	if(!paper && !furniture) return;
	// Municipal administrations buy office supplies at their seat with their own cash.
	state.world.for_each_institution([&](dcon::institution_id institution) {
		if(governance::kind_of(state, institution) != governance::institution_kind::municipality) return;
		auto account = finance::treasury_account_for(state, institution, economy::money);
		auto actor = governance::actor_for_institution(state, institution);
		auto site = seat_of(state, institution);
		auto market = site ? economy::physical::concrete_market::market_for_site(state, site) : dcon::market_id{};
		if(!account || !actor || !site || !market) return;
		for(auto commodity : {paper, furniture}) {
			if(!commodity) continue;
			state.world.for_each_physical_stock([&](dcon::physical_stock_id stock) {
				if(state.world.physical_stock_get_commodity_from_physical_stock_commodity(stock) != commodity) return;
				auto source_site = state.world.physical_stock_get_site_from_physical_stock_site(stock);
				auto owner_relation = state.world.physical_stock_get_physical_stock_owner(stock);
				auto seller = owner_relation ? state.world.physical_stock_owner_get_economic_actor(owner_relation) : dcon::economic_actor_id{};
				auto available = seller ? economy::physical::inventory::quantity(state, source_site, commodity, seller) : 0.0f;
				auto source_market = seller ? economy::physical::concrete_market::market_for_site(state, source_site) : dcon::market_id{};
				auto source_price = source_market ? economy::physical::concrete_market::canonical_reference_price(state, source_market,
					commodity, state.current_date, state.world.commodity_get_cost(commodity)) : 0.0f;
				if(seller && seller != actor && source_market && available > 0.0f && std::isfinite(source_price) && source_price > 0.0f)
					(void)economy::physical::concrete_market::post_ask(state, seller, source_site, source_market, commodity, available,
						source_price, economy::physical::concrete_market::order_purpose::general);
			});
		}
		auto cash = std::max(0.0f, economy::accounts::balance(state, account) - economy::physical::concrete_market::reserved_bid_amount(state, account));
		if(cash <= 0.01f) return;
		for(auto commodity : {paper, furniture}) {
			if(!commodity) continue;
			auto price = economy::physical::concrete_market::canonical_reference_price(state, market, commodity, state.current_date,
				state.world.commodity_get_cost(commodity));
			if(!std::isfinite(price) || price <= 0.0f) continue;
			auto per_order_budget = std::min(cash * 0.02f, economy::accounts::balance(state, account) * 0.01f);
			if(per_order_budget < price * 0.02f) continue;
			auto quantity = std::min(1.0f, per_order_budget / price);
			if(economy::physical::concrete_market::post_bid(state, actor, account, site, market, commodity, quantity, price * 1.05f,
				economy::physical::concrete_market::order_purpose::general))
				cash -= per_order_budget;
		}
	});
}

void deliver_public_services(sys::state& state) {
	// Office supplies are consumed by municipal administrations after market
	// settlement, so this demand draws real inventory and treasury cash.
	auto paper = commodity_named(state, "paper");
	auto furniture = commodity_named(state, "furniture");
	if(!paper && !furniture) return;
	state.world.for_each_institution([&](dcon::institution_id institution) {
		if(governance::kind_of(state, institution) != governance::institution_kind::municipality) return;
		auto actor = governance::actor_for_institution(state, institution);
		auto site = seat_of(state, institution);
		if(!actor || !site) return;
		if(paper) (void)economy::physical::inventory::remove(state, site, paper, 0.01f, actor);
		if(furniture) (void)economy::physical::inventory::remove(state, site, furniture, 0.001f, actor);
	});
}

float education_staff_at(sys::state const& state, dcon::province_id province) {
	auto nation = state.world.province_get_nation_from_province_ownership(province);
	double teachers = 0.0;
	for(auto institution : governance::institutions_of(state, nation)) {
		if(governance::service_kind(state.world.institution_get_service(institution)) != governance::service_kind::education
			|| !governance::contains(state, governance::jurisdiction_of(state, institution), province)) continue;
		for(auto id : economy::exact_person_economy::active_contracts_for_institution(state, institution)) {
			auto contract = economy::exact_person_economy::contract(state, id);
			if(!contract || !persons::alive(state, contract->worker) || !contract->workplace) continue;
			if(state.world.site_get_province_from_site_location(contract->workplace) == province)
				teachers += std::max(0.0f, contract->labor_capacity);
		}
	}
	return float(teachers);
}

} // namespace governance::public_administration
