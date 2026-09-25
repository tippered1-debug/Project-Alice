#include "public_administration.hpp"

#include "actors/ownership.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/economy_stats.hpp"
#include "economy/money.hpp"
#include "economy/physical/concrete_market.hpp"
#include "economy/physical/concrete_labor.hpp"
#include "economy/physical/job_market.hpp"
#include "economy/payroll.hpp"
#include "economy/physical/inventory.hpp"
#include "economy/exact_person_economy.hpp"
#include "economy/demographics.hpp"
#include "governance/finance/finance.hpp"
#include "system_state.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <vector>

namespace governance::public_administration {
namespace {

dcon::institution_id find_institution(sys::state const& state, dcon::nation_id nation,
	governance::institution_kind kind) {
	dcon::institution_id result{};
	for(auto institution : governance::institutions_of(state, nation)) {
		if(state.world.institution_get_kind(institution) == uint8_t(kind)) {
			result = institution;
			break;
		}
	}
	return result;
}

dcon::institution_id ensure_institution(sys::state& state, dcon::nation_id nation,
	governance::institution_kind kind) {
	auto institution = find_institution(state, nation, kind);
	if(!institution) {
		institution = governance::create_institution(state, nation, kind);
		auto central = governance::central_government_for(state, nation);
		if(institution != central) (void)governance::set_parent(state, institution, central);
	}
	return institution;
}

dcon::local_government_id local_government_for(sys::state& state, dcon::nation_id nation,
	dcon::institution_id institution) {
	dcon::local_government_id result{};
	state.world.for_each_local_government([&](dcon::local_government_id local_government) {
		if(state.world.local_government_get_institution_from_local_government_institution(local_government)
			== institution) result = local_government;
	});
	if(!result) {
		result = state.world.create_local_government();
		(void)governance::bind_local_government(state, result, institution);
	}
	if(!state.world.local_government_get_local_government_jurisdiction(result)) {
		auto territory = state.world.create_territorial_unit();
		state.world.force_create_local_government_jurisdiction(result, territory);
	}
	if(!state.world.local_government_get_settlement_from_local_government_seat(result)) {
		auto capital = state.world.nation_get_capital(nation);
		dcon::settlement_id seat{};
		state.world.province_for_each_settlement_location_as_province(capital,
			[&](dcon::settlement_location_id relation) {
				if(!seat) seat = state.world.settlement_location_get_settlement(relation);
			});
		if(seat) state.world.force_create_local_government_seat(result, seat);
	}
	return result;
}

dcon::territorial_unit_id territory_for(sys::state const& state, dcon::local_government_id local_government) {
	auto relation = state.world.local_government_get_local_government_jurisdiction(local_government);
	return relation ? state.world.local_government_jurisdiction_get_territorial_unit(relation)
		: dcon::territorial_unit_id{};
}

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

dcon::monetary_account_id treasury_for_kind(sys::state const& state, dcon::nation_id nation,
	governance::institution_kind kind) {
	auto institution = find_institution(state, nation, kind);
	return institution ? finance::treasury_account_for(state, institution, economy::money)
		: dcon::monetary_account_id{};
}

float positive(float value) { return std::isfinite(value) ? std::max(0.0f, value) : 0.0f; }

float monthly_wage_cost(sys::state const& state, dcon::employment_contract_id contract) {
	if(state.world.employment_contract_get_status(contract) != uint8_t(economy::physical::concrete_labor::contract_status::active))
		return 0.0f;
	auto wage = state.world.employment_contract_get_wage_rate(contract);
	auto capacity = state.world.employment_contract_get_labor_capacity(contract);
	return std::isfinite(wage) && wage > 0.0f && std::isfinite(capacity) && capacity > 0.0f
		? wage * capacity : 0.0f;
}

float active_monthly_payroll(sys::state const& state, dcon::institution_id institution) {
	float total = 0.0f;
	for(auto contract : economy::physical::concrete_labor::active_contracts_for_institution(state, institution))
		total += monthly_wage_cost(state, contract);
	for(auto id : economy::exact_person_economy::active_contracts_for_institution(state, institution)) {
		auto contract = economy::exact_person_economy::contract(state, id);
		if(contract && std::isfinite(contract->wage_rate) && contract->wage_rate > 0.0f
			&& std::isfinite(contract->labor_capacity) && contract->labor_capacity > 0.0f)
			total += contract->wage_rate * contract->labor_capacity;
	}
	return positive(total);
}

float active_capacity_at(sys::state const& state, dcon::institution_id institution,
	dcon::province_id province, uint8_t occupation) {
	float total = 0.0f;
	for(auto contract : economy::physical::concrete_labor::active_contracts_for_institution(state, institution)) {
		if(state.world.employment_contract_get_occupation(contract) != occupation) continue;
		auto site = state.world.employment_contract_get_site_from_employment_contract_site(contract);
		if(!site || state.world.site_get_province_from_site_location(site) != province) continue;
		total += positive(state.world.employment_contract_get_labor_capacity(contract));
	}
	for(auto id : economy::exact_person_economy::active_contracts_for_institution(state, institution)) {
		auto contract = economy::exact_person_economy::contract(state, id);
		if(!contract || contract->occupation != occupation || !contract->workplace
			|| state.world.site_get_province_from_site_location(contract->workplace) != province) continue;
		total += positive(contract->labor_capacity);
	}
	return total;
}

float open_capacity_at(sys::state const& state, dcon::institution_id institution,
	dcon::province_id province, uint8_t occupation) {
	float total = 0.0f;
	state.world.institution_for_each_job_offer_institution_as_institution(institution, [&](auto relation) {
		auto offer = state.world.job_offer_institution_get_job_offer(relation);
		if(!offer || state.world.job_offer_get_status(offer) != uint8_t(economy::physical::job_market::offer_status::open)
			|| state.world.job_offer_get_occupation(offer) != occupation
			|| (state.world.job_offer_get_expires_on(offer)
				&& state.world.job_offer_get_expires_on(offer) < state.current_date)) return;
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
		if(!offer || state.world.job_offer_get_status(offer) != uint8_t(economy::physical::job_market::offer_status::open)
			|| (state.world.job_offer_get_expires_on(offer)
				&& state.world.job_offer_get_expires_on(offer) < state.current_date)) return;
		auto wage = state.world.job_offer_get_wage_rate(offer);
		auto capacity = state.world.job_offer_get_labor_capacity(offer);
		if(!std::isfinite(wage) || wage <= 0.0f || !std::isfinite(capacity) || capacity <= 0.0f) return;
		total += wage * capacity * float(state.world.job_offer_get_openings(offer));
	});
	return positive(total);
}

void post_public_vacancies(sys::state& state, dcon::nation_id nation,
	dcon::institution_id institution, dcon::monetary_account_id account,
	uint8_t occupation, float positions_per_person, float wage_multiplier,
	std::vector<float> const& population_by_state,
	std::vector<dcon::province_id> const& province_by_state,
	float& available_monthly_funds) {
	if(!institution || !account || positions_per_person <= 0.0f) return;
	auto capital = state.world.nation_get_capital(nation);
	auto capital_state = state.world.province_get_state_membership(capital);
	auto reference_province = capital_state ? state.world.state_instance_get_capital(capital_state) : capital;
	auto lane = occupation == 0 ? economy::labor::no_education
		: economy::labor::high_education_and_accepted;
	float const monthly_cost = active_monthly_payroll(state, institution) + open_monthly_payroll(state, institution);
	available_monthly_funds = std::max(0.0f,
		economy::accounts::balance(state, account) - monthly_cost
		- economy::physical::concrete_market::reserved_bid_amount(state, account));
	for(uint32_t index = 0; index < population_by_state.size(); ++index) {
		auto population = population_by_state[index];
		auto province = province_by_state[index];
		if(population <= 0.0f || !province) continue;
		auto site = site_for_province(state, province);
		if(!site) continue;
		auto target = population * positions_per_person;
		auto supplied = active_capacity_at(state, institution, province, occupation);
		auto advertised = open_capacity_at(state, institution, province, occupation);
		auto shortage = std::max(0.0f, target - supplied - advertised);
		if(shortage < 0.5f) continue;
		auto wage_province = state.world.province_get_state_membership(province);
		auto wage_location = wage_province ? state.world.state_instance_get_capital(wage_province) : reference_province;
		auto daily_wage = std::max(0.01f, state.world.province_get_labor_price(wage_location, lane)
			* wage_multiplier);
		auto monthly_wage = daily_wage * 30.0f;
		if(!std::isfinite(monthly_wage) || monthly_wage <= 0.0f || available_monthly_funds < monthly_wage) continue;
		auto openings = std::min(uint32_t(std::ceil(shortage)),
			uint32_t(std::min<double>(double(std::numeric_limits<uint32_t>::max()),
				std::floor(double(available_monthly_funds / monthly_wage)))));
		if(openings == 0) continue;
		if(economy::physical::job_market::post_institution_job_offer(state, institution, site,
			occupation, 1.0f, monthly_wage, 30, account, openings, state.current_date))
			available_monthly_funds -= float(openings) * monthly_wage;
	}
}

} // namespace

dcon::institution_id institution_for(sys::state const& state, dcon::nation_id nation,
	governance::institution_kind kind) {
	return find_institution(state, nation, kind);
}

dcon::institution_id tax_authority_for(sys::state const& state, dcon::nation_id nation) {
	return find_institution(state, nation, governance::institution_kind::tax_authority);
}

dcon::monetary_account_id tax_treasury_for(sys::state const& state, dcon::nation_id nation) {
	return treasury_for_kind(state, nation, governance::institution_kind::tax_authority);
}

dcon::economic_actor_id household_sector_actor(sys::state const& state, dcon::nation_id nation) {
	auto institution = find_institution(state, nation, governance::institution_kind::household_sector);
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
		auto tax_authority = ensure_institution(state, nation, governance::institution_kind::tax_authority);
		auto finance_ministry = ensure_institution(state, nation, governance::institution_kind::finance_ministry);
		auto education_ministry = ensure_institution(state, nation, governance::institution_kind::education_ministry);
		auto interior_ministry = ensure_institution(state, nation, governance::institution_kind::interior_ministry);
		auto public_works = ensure_institution(state, nation, governance::institution_kind::public_works_ministry);
		auto municipality = ensure_institution(state, nation, governance::institution_kind::municipality);
		auto household_sector = ensure_institution(state, nation, governance::institution_kind::household_sector);
		(void)central;

		for(auto institution : {tax_authority, finance_ministry, education_ministry, interior_ministry,
			public_works, municipality}) {
			(void)finance::open_treasury_account(state, institution, economy::money);
			(void)governance::grant_authority_to_institution(state, institution,
				governance::authority_kind::administer, nation);
			(void)governance::grant_authority_to_institution(state, institution,
				governance::authority_kind::spend_public_funds, nation);
		}
		(void)governance::grant_authority_to_institution(state, central,
			governance::authority_kind::administer, nation);
		(void)governance::grant_authority_to_institution(state, tax_authority,
			governance::authority_kind::levy_tax, nation);
		(void)governance::grant_authority_to_institution(state, tax_authority,
			governance::authority_kind::spend_public_funds, nation);
		auto household_actor = governance::actor_for_institution(state, household_sector);
		if(household_actor)
			state.world.economic_actor_set_kind(household_actor, uint8_t(actors::ownership::actor_kind::other));
		if(household_actor && !economy::accounts::find_account(state, household_actor, economy::money))
			(void)economy::accounts::open_account(state, household_actor, economy::money);
		auto local_government = local_government_for(state, nation, municipality);
		auto territory = territory_for(state, local_government);
		if(territory) {
			(void)governance::grant_authority_to_institution(state, municipality,
				governance::authority_kind::administer, territory);
			(void)governance::grant_authority_to_institution(state, municipality,
				governance::authority_kind::spend_public_funds, territory);
		}
	});
	synchronize_local_governments(state);
}

void synchronize_local_governments(sys::state& state) {
	state.world.for_each_nation([&](dcon::nation_id nation) {
		auto municipality = find_institution(state, nation, governance::institution_kind::municipality);
		if(!municipality) return;
		auto local_government = local_government_for(state, nation, municipality);
		auto territory = territory_for(state, local_government);
		if(!territory) return;
		for(auto ownership : state.world.nation_get_province_ownership(nation)) {
			auto province = ownership.get_province().id;
			if(province.index() >= state.province_definitions.first_sea_province.index()) continue;
			auto membership = state.world.province_get_territorial_unit_membership(province);
			if(membership) {
				if(state.world.territorial_unit_membership_get_territorial_unit(membership) != territory)
					state.world.territorial_unit_membership_set_territorial_unit(membership, territory);
			} else {
				state.world.force_create_territorial_unit_membership(province, territory);
			}
		}
	});
}

float treasury_cash(sys::state const& state, dcon::nation_id nation) {
	float total = 0.0f;
	for(auto institution : governance::institutions_of(state, nation)) {
		if(state.world.institution_get_kind(institution) == uint8_t(governance::institution_kind::household_sector))
			continue;
		auto account = finance::treasury_account_for(state, institution, economy::money);
		total += economy::accounts::balance(state, account);
	}
	return positive(total);
}

float daily_budget(sys::state const& state, dcon::nation_id nation) {
	// Cash reserves finance a rolling thirty-day operating envelope. This keeps
	// yesterday's full stock from becoming today's spending authorization.
	return treasury_cash(state, nation) / 30.0f;
}

void appropriate_daily_budget(sys::state& state, dcon::nation_id nation) {
	if(state.current_date.to_ymd(state.start_date).day != 1) return;
	auto source_institution = tax_authority_for(state, nation);
	auto source = tax_treasury_for(state, nation);
	if(!source_institution || !source) return;
	auto available = std::min(economy::accounts::balance(state, source), daily_budget(state, nation) * 30.0f * 0.35f);
	if(available <= 0.001f) return;
	struct recipient { governance::institution_kind kind; float weight; dcon::monetary_account_id account; };
	std::array<recipient, 5> recipients{{
		{governance::institution_kind::finance_ministry, 0.10f, {}},
		{governance::institution_kind::education_ministry, 0.05f + float(state.world.nation_get_education_spending(nation)) / 100.0f, {}},
		{governance::institution_kind::interior_ministry, 0.05f + float(state.world.nation_get_administrative_spending(nation)) / 100.0f, {}},
		{governance::institution_kind::public_works_ministry, 0.05f + float(state.world.nation_get_construction_spending(nation)) / 100.0f, {}},
		{governance::institution_kind::municipality, 0.10f + float(state.world.nation_get_social_spending(nation)) / 100.0f, {}},
	}};
	float total_weight = 0.0f;
	for(auto& recipient : recipients) {
		recipient.account = treasury_for_kind(state, nation, recipient.kind);
		recipient.weight = positive(recipient.weight);
		if(recipient.account && recipient.account != source) total_weight += recipient.weight;
		else recipient.weight = 0.0f;
	}
	if(total_weight <= 0.0f) return;
	for(auto const& recipient : recipients) {
		if(recipient.weight <= 0.0f) continue;
		auto amount = available * recipient.weight / total_weight;
		(void)finance::authorized_spend_by_institution(state, source_institution,
			source, recipient.account, amount, state.current_date);
	}
}

void plan_public_staffing(sys::state& state, dcon::nation_id nation) {
	if(!nation || !state.world.nation_is_valid(nation)) return;
	std::vector<float> population_by_state(state.world.state_instance_size(), 0.0f);
	std::vector<dcon::province_id> province_by_state(state.world.state_instance_size());
	float total_population = 0.0f;
	for(auto ownership : state.world.nation_get_province_ownership(nation)) {
		auto province = ownership.get_province().id;
		if(province.index() >= state.province_definitions.first_sea_province.index()
			|| state.world.province_get_nation_from_province_control(province) != nation) continue;
		state.world.province_set_public_bureaucrat_staffing(province, 0.0f);
		state.world.province_set_public_teacher_staffing(province, 0.0f);
		state.world.province_set_public_police_staffing(province, 0.0f);
		auto state_instance = state.world.province_get_state_membership(province);
		if(!state_instance || state_instance.id.index() >= population_by_state.size()) continue;
		auto weight = std::max(0.0f, state.world.province_get_demographics(province, demographics::total));
		population_by_state[state_instance.id.index()] += weight;
		province_by_state[state_instance.id.index()] = state.world.state_instance_get_capital(state_instance);
		total_population += weight;
	}
	if(total_population <= 0.0f) return;
	auto finance_institution = find_institution(state, nation, governance::institution_kind::finance_ministry);
	auto education_institution = find_institution(state, nation, governance::institution_kind::education_ministry);
	auto interior_institution = find_institution(state, nation, governance::institution_kind::interior_ministry);
	auto municipality = find_institution(state, nation, governance::institution_kind::municipality);
	auto finance_account = treasury_for_kind(state, nation, governance::institution_kind::finance_ministry);
	auto education_account = treasury_for_kind(state, nation, governance::institution_kind::education_ministry);
	auto interior_account = treasury_for_kind(state, nation, governance::institution_kind::interior_ministry);
	auto municipality_account = treasury_for_kind(state, nation, governance::institution_kind::municipality);
	float available_finance = 0.0f;
	float available_education = 0.0f;
	float available_interior = 0.0f;
	float available_municipal = 0.0f;
	post_public_vacancies(state, nation, finance_institution, finance_account,
		2, 0.00035f, 1.25f, population_by_state, province_by_state, available_finance);
	post_public_vacancies(state, nation, education_institution, education_account,
		2, 0.0010f, 1.15f, population_by_state, province_by_state, available_education);
	post_public_vacancies(state, nation, interior_institution, interior_account,
		0, 0.0015f, 0.85f, population_by_state, province_by_state, available_interior);
	post_public_vacancies(state, nation, municipality, municipality_account,
		2, 0.00015f, 1.0f, population_by_state, province_by_state, available_municipal);

	// Filled government jobs create the labor demand that powers the existing
	// school output and policy-execution lanes in each state market.
	for(uint32_t index = 0; index < population_by_state.size(); ++index) {
		auto province = province_by_state[index];
		if(population_by_state[index] <= 0.0f || !province) continue;
		auto bureaucrat_staff = finance_institution
			? active_capacity_at(state, finance_institution, province, 2) : 0.0f;
		bureaucrat_staff += municipality
			? active_capacity_at(state, municipality, province, 2) : 0.0f;
		auto teacher_staff = education_institution
			? active_capacity_at(state, education_institution, province, 2) : 0.0f;
		auto police_staff = interior_institution
			? active_capacity_at(state, interior_institution, province, 0) : 0.0f;
		state.world.province_set_public_bureaucrat_staffing(province, bureaucrat_staff);
		state.world.province_set_public_teacher_staffing(province, teacher_staff);
		state.world.province_set_public_police_staffing(province, police_staff);
		state.world.province_set_administration_employment_target(province, bureaucrat_staff);
		auto teacher_demand = state.world.province_get_labor_demand(province,
			economy::labor::high_education_and_accepted);
		auto police_demand = state.world.province_get_labor_demand(province, economy::labor::no_education);
		state.world.province_set_labor_demand(province, economy::labor::high_education_and_accepted,
			teacher_demand + bureaucrat_staff + teacher_staff);
		state.world.province_set_labor_demand(province, economy::labor::no_education,
			police_demand + police_staff);
	}
	auto capital = state.world.nation_get_capital(nation);
	auto capital_state = capital ? state.world.province_get_state_membership(capital) : dcon::state_instance_id{};
	auto capital_province = capital_state ? state.world.state_instance_get_capital(capital_state) : capital;
	auto national_bureaucrats = finance_institution && capital_province
		? active_capacity_at(state, finance_institution, capital_province, 2) : 0.0f;
	national_bureaucrats += municipality && capital_province
		? active_capacity_at(state, municipality, capital_province, 2) : 0.0f;
	state.world.nation_set_administration_employment_target_in_capital(nation, national_bureaucrats);
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
			auto kind = governance::institution_kind(state.world.institution_get_kind(institution));
			if(kind != governance::institution_kind::finance_ministry
				&& kind != governance::institution_kind::education_ministry
				&& kind != governance::institution_kind::interior_ministry
				&& kind != governance::institution_kind::municipality
				&& kind != governance::institution_kind::public_works_ministry) continue;
			for(auto contract : economy::physical::concrete_labor::contracts_for_institution(state, institution)) {
				auto due = economy::physical::concrete_labor::wage_due(state, contract);
				auto payer = state.world.employment_contract_get_monetary_account_from_employment_contract_payer_account(contract);
				auto site = state.world.employment_contract_get_site_from_employment_contract_site(contract);
				auto province = site ? state.world.site_get_province_from_site_location(site) : dcon::province_id{};
				auto occupation = state.world.employment_contract_get_occupation(contract);
				auto result = economy::physical::concrete_labor::settle_contract_wage(state, contract);
				accrue(province, economy::accounts::settlement_of(state, payer),
					occupation, due, result.current_paid);
			}
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
	state.world.for_each_nation([&](dcon::nation_id nation) {
		auto institution = find_institution(state, nation, governance::institution_kind::municipality);
		auto account = institution ? finance::treasury_account_for(state, institution, economy::money)
			: dcon::monetary_account_id{};
		if(!institution || !account) return;
		auto actor = governance::actor_for_institution(state, institution);
		auto capital = state.world.nation_get_capital(nation);
		auto site = site_for_province(state, capital);
		auto market = site ? economy::physical::concrete_market::market_for_site(state, site) : dcon::market_id{};
		if(!actor || !site || !market) return;
		for(auto commodity : {paper, furniture}) {
			if(!commodity) continue;
			state.world.for_each_physical_stock([&](dcon::physical_stock_id stock) {
				if(state.world.physical_stock_get_commodity_from_physical_stock_commodity(stock) != commodity)
					return;
				auto source_site = state.world.physical_stock_get_site_from_physical_stock_site(stock);
				auto owner_relation = state.world.physical_stock_get_physical_stock_owner(stock);
				auto seller = owner_relation
					? state.world.physical_stock_owner_get_economic_actor(owner_relation)
					: dcon::economic_actor_id{};
				auto available = seller ? economy::physical::inventory::quantity(state, source_site,
					commodity, seller) : 0.0f;
				auto source_market = seller
					? economy::physical::concrete_market::market_for_site(state, source_site) : dcon::market_id{};
				auto source_price = source_market
					? economy::physical::concrete_market::canonical_reference_price(state, source_market,
						commodity, state.current_date, state.world.commodity_get_cost(commodity)) : 0.0f;
				if(seller && seller != actor && source_market && available > 0.0f
					&& std::isfinite(source_price) && source_price > 0.0f)
					(void)economy::physical::concrete_market::post_ask(state, seller, source_site,
						source_market, commodity, available, source_price,
						economy::physical::concrete_market::order_purpose::general);
			});
		}
		auto cash = std::max(0.0f, economy::accounts::balance(state, account)
			- economy::physical::concrete_market::reserved_bid_amount(state, account));
		if(cash <= 0.01f) return;
		for(auto commodity : {paper, furniture}) {
			if(!commodity) continue;
			auto price = economy::physical::concrete_market::canonical_reference_price(state,
				market, commodity, state.current_date, state.world.commodity_get_cost(commodity));
			if(!std::isfinite(price) || price <= 0.0f) continue;
			auto per_order_budget = std::min(cash * 0.02f, economy::accounts::balance(state, account) * 0.01f);
			if(per_order_budget < price * 0.02f) continue;
			auto quantity = std::min(1.0f, per_order_budget / price);
			if(economy::physical::concrete_market::post_bid(state, actor, account, site,
				market, commodity, quantity, price * 1.05f,
				economy::physical::concrete_market::order_purpose::general))
				cash -= per_order_budget;
		}
	});
}

void deliver_public_services(sys::state& state) {
	// Office supplies are consumed by the public administration after market
	// settlement, so this demand draws real inventory and treasury cash.
	auto paper = commodity_named(state, "paper");
	auto furniture = commodity_named(state, "furniture");
	if(!paper && !furniture) return;
	state.world.for_each_nation([&](dcon::nation_id nation) {
		auto institution = find_institution(state, nation, governance::institution_kind::municipality);
		if(!institution) return;
		auto actor = governance::actor_for_institution(state, institution);
		auto site = site_for_province(state, state.world.nation_get_capital(nation));
		if(!actor || !site) return;
		if(paper) (void)economy::physical::inventory::remove(state, site, paper, 0.01f, actor);
		if(furniture) (void)economy::physical::inventory::remove(state, site, furniture, 0.001f, actor);
	});
}

} // namespace governance::public_administration
