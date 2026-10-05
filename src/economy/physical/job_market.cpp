#include "job_market.hpp"

#include "accounts/accounts.hpp"
#include "actors/organizations/organizations.hpp"
#include "economy/firm_agency.hpp"
#include "economy/exact_person_economy.hpp"
#include "economy/households.hpp"
#include "economy/economy_stats.hpp"
#include "economy/price.hpp"
#include "household_mobility.hpp"
#include "labor_dynamics.hpp"
#include "persons/exact_population.hpp"
#include "governance/governance.hpp"
#include "governance/law/law.hpp"
#include "governance/finance/finance.hpp"
#include "system_state.hpp"
#include "world/site.hpp"
#include "world/spatial_runtime.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace economy::physical::job_market {
namespace {
constexpr float epsilon = 1.0e-6f;
constexpr uint8_t general_occupation = 0;
constexpr uint8_t skilled_occupation = 1;
constexpr uint8_t professional_occupation = 2;
constexpr float voluntary_quit_wage_gain = 1.12f;
constexpr float reservation_premium = 1.1f;

float minimum_daily_wage(sys::state const& state, dcon::site_id workplace, sys::date date) {
	if(!workplace || !state.world.site_is_valid(workplace)) return 0.0f;
	auto province = state.world.site_get_province_from_site_location(workplace);
	auto nation = province ? state.world.province_get_nation_from_province_ownership(province) : dcon::nation_id{};
	if(!nation) return 0.0f;
	auto value = governance::law::effective_topic(state, governance::national(nation),
		governance::policy::topic_id::minimum_wage, date);
	return value ? std::max(0.0f, std::get<float>(*value)) : 0.0f;
}

sys::date normalized_date(sys::state const& state, sys::date date) {
	if(date) return date;
	if(state.current_date) return state.current_date;
	return sys::date{0};
}

bool expired_on(sys::state const& state, dcon::job_offer_id offer) {
	if(!offer || !state.world.job_offer_is_valid(offer)) return true;
	auto expires = state.world.job_offer_get_expires_on(offer);
	return expires && state.current_date && expires < state.current_date;
}

void refresh_offer(sys::state& state, dcon::job_offer_id offer) {
	if(offer && state.world.job_offer_is_valid(offer) && state.world.job_offer_get_status(offer) == uint8_t(offer_status::open) && expired_on(state, offer))
		state.world.job_offer_set_status(offer, uint8_t(offer_status::expired));
}

bool open_for_application(sys::state const& state, dcon::job_offer_id offer) {
	return offer && state.world.job_offer_is_valid(offer)
		&& state.world.job_offer_get_status(offer) == uint8_t(offer_status::open)
		&& !expired_on(state, offer);
}

bool exact_worker_qualifies_for_offer(sys::state const& state,
	economy::exact_person_economy::person_key worker, dcon::job_offer_id offer) {
	return offer && state.world.job_offer_get_occupation(offer)
		<= household_mobility::qualification_rank(state,
			persons::pop_type(state, worker));
}

template<typename Id>
void sort_ids(std::vector<Id>& ids) {
	std::sort(ids.begin(), ids.end(), [](auto left, auto right) { return left.index() < right.index(); });
}

std::vector<dcon::job_offer_id> all_offers(sys::state const& state) {
	std::vector<dcon::job_offer_id> result;
	state.world.for_each_job_offer([&](auto offer) { result.push_back(offer); });
	sort_ids(result);
	return result;
}

struct wage_offer_terms {
	float wage_rate = 1.0f;
	uint16_t pay_period_days = 1;
};

wage_offer_terms wage_offer_for_factory(sys::state const& state, dcon::factory_id factory, uint8_t occupation) {
	for(auto contract_id : economy::exact_person_economy::active_contracts_for_factory(state, factory)) {
		auto contract = economy::exact_person_economy::contract(state, contract_id);
		if(!contract || contract->occupation != occupation) continue;
		if(std::isfinite(contract->wage_rate) && contract->wage_rate >= 0.0f && contract->pay_period_days != 0)
			return {contract->wage_rate, contract->pay_period_days};
	}
	auto expected_revenue = firm_agency::decide_factory(state, factory).expected_unit_revenue;
	float occupation_multiplier = occupation == professional_occupation ? 1.40f
		: occupation == skilled_occupation ? 1.10f : 0.75f;
	auto bootstrap = std::isfinite(expected_revenue)
		? expected_revenue * 0.10f * occupation_multiplier : 0.0f;
	return {std::max(1.0f, bootstrap), 1};
}
}

dcon::job_offer_id post_job_offer(sys::state& state, dcon::economic_actor_id employer,
	dcon::factory_id factory, dcon::site_id workplace, uint8_t occupation,
	float labor_capacity, float wage_rate, uint16_t pay_period_days,
	dcon::monetary_account_id payer_account, uint32_t openings,
	sys::date created_on, sys::date expires_on) {
	if(!employer || !factory || !state.world.factory_is_valid(factory) || !std::isfinite(labor_capacity) || labor_capacity <= 0.0f || !std::isfinite(wage_rate) || wage_rate < 0.0f || pay_period_days == 0 || !payer_account || accounts::owner_of(state, payer_account) != employer) return {};
	auto operator_actor = actors::organizations::operator_actor_for_factory(state, factory);
	if(operator_actor && operator_actor != employer) return {};
	if(!workplace) workplace = world::site::site_for_factory(state, factory);
	if(!workplace || !state.world.site_is_valid(workplace)) return {};
	created_on = normalized_date(state, created_on);
	if(wage_rate / float(pay_period_days) + epsilon < minimum_daily_wage(state, workplace, created_on)) return {};
	auto settlement = accounts::settlement_of(state, payer_account);
	auto factory_settlement = state.world.factory_get_payroll_settlement(factory);
	if(!settlement || factory_settlement && factory_settlement != settlement) return {};
	if(expires_on && expires_on < created_on) return {};
	auto offer = state.world.create_job_offer();
	state.world.job_offer_set_occupation(offer, occupation);
	state.world.job_offer_set_labor_capacity(offer, labor_capacity);
	state.world.job_offer_set_wage_rate(offer, wage_rate);
	state.world.job_offer_set_pay_period_days(offer, pay_period_days);
	state.world.job_offer_set_openings(offer, openings);
	state.world.job_offer_set_created_on(offer, created_on);
	state.world.job_offer_set_expires_on(offer, expires_on);
	state.world.job_offer_set_status(offer, uint8_t(offer_status::open));
	state.world.force_create_job_offer_employer(offer, employer);
	state.world.force_create_job_offer_factory(offer, factory);
	state.world.force_create_job_offer_site(offer, workplace);
	state.world.force_create_job_offer_payer_account(offer, payer_account);
	return offer;
}

dcon::job_offer_id post_institution_job_offer(sys::state& state, dcon::institution_id institution,
	dcon::site_id workplace, uint8_t occupation, float labor_capacity, float wage_rate,
	uint16_t pay_period_days, dcon::monetary_account_id payer_account, uint32_t openings,
	sys::date created_on, sys::date expires_on) {
	if(!institution || !state.world.institution_is_valid(institution) || !workplace || !state.world.site_is_valid(workplace) || !std::isfinite(labor_capacity) || labor_capacity <= 0.0f || !std::isfinite(wage_rate) || wage_rate < 0.0f || pay_period_days == 0 || !payer_account || governance::finance::treasury_institution_for(state, payer_account) != institution)
		return {};
	auto employer = governance::actor_for_institution(state, institution);
	if(!employer || accounts::owner_of(state, payer_account) != employer) return {};
	created_on = normalized_date(state, created_on);
	if(!expires_on) expires_on = created_on + int32_t(30);
	if(expires_on && expires_on < created_on) return {};
	if(wage_rate / float(pay_period_days) + epsilon < minimum_daily_wage(state, workplace, created_on)) return {};
	auto offer = state.world.create_job_offer();
	state.world.job_offer_set_occupation(offer, occupation);
	state.world.job_offer_set_labor_capacity(offer, labor_capacity);
	state.world.job_offer_set_wage_rate(offer, wage_rate);
	state.world.job_offer_set_pay_period_days(offer, pay_period_days);
	state.world.job_offer_set_openings(offer, openings);
	state.world.job_offer_set_created_on(offer, created_on);
	state.world.job_offer_set_expires_on(offer, expires_on);
	state.world.job_offer_set_status(offer, uint8_t(offer_status::open));
	state.world.force_create_job_offer_employer(offer, employer);
	state.world.force_create_job_offer_institution(offer, institution);
	state.world.force_create_job_offer_site(offer, workplace);
	state.world.force_create_job_offer_payer_account(offer, payer_account);
	return offer;
}

bool close_job_offer(sys::state& state, dcon::job_offer_id offer) {
	if(!offer || !state.world.job_offer_is_valid(offer) || state.world.job_offer_get_status(offer) != uint8_t(offer_status::open)) return false;
	state.world.job_offer_set_status(offer, uint8_t(offer_status::closed));
	return true;
}

bool expire_job_offer(sys::state& state, dcon::job_offer_id offer) {
	if(!offer || !state.world.job_offer_is_valid(offer) || state.world.job_offer_get_status(offer) != uint8_t(offer_status::open)) return false;
	state.world.job_offer_set_status(offer, uint8_t(offer_status::expired));
	return true;
}

bool add_job_offer_openings(sys::state& state, dcon::job_offer_id offer, uint32_t openings) {
	if(!offer || !state.world.job_offer_is_valid(offer) || state.world.job_offer_get_status(offer) != uint8_t(offer_status::open) || openings > std::numeric_limits<uint32_t>::max() - state.world.job_offer_get_openings(offer)) return false;
	if(expired_on(state, offer)) {
		state.world.job_offer_set_status(offer, uint8_t(offer_status::expired));
		return false;
	}
	state.world.job_offer_set_openings(offer, state.world.job_offer_get_openings(offer) + openings);
	return true;
}

std::vector<dcon::job_offer_id> open_offers_for_factory(sys::state const& state, dcon::factory_id factory) {
	std::vector<dcon::job_offer_id> result;
	if(!factory) return result;
	state.world.factory_for_each_job_offer_factory_as_factory(factory, [&](auto relation) {
		auto offer = state.world.job_offer_factory_get_job_offer(relation);
		if(offer && open_for_application(state, offer) && state.world.job_offer_get_openings(offer) > 0)
			result.push_back(offer);
	});
	sort_ids(result);
	return result;
}

void process_factory_vacancies(sys::state& state) {
	state.world.for_each_factory([&](dcon::factory_id factory) {
		// A peasant cohort staffs its own farm with its members.
		if(economy::households::self_working_operator(state, factory)) return;
		auto employer = actors::organizations::operator_actor_for_factory(state, factory);
		auto settlement = state.world.factory_get_payroll_settlement(factory);
		auto payer = settlement ? accounts::find_account(state, employer, settlement) : dcon::monetary_account_id{};
		if(!employer || !payer) return;
		auto decision = firm_agency::decide_factory(state, factory);
		auto desired = decision.desired_units;
		auto site = world::site::site_for_factory(state, factory);
		if(!site || !std::isfinite(desired) || desired <= epsilon) return;
		float staffing_mix[3]{};
		for(auto contract_id : economy::exact_person_economy::active_contracts_for_factory(state, factory)) {
			auto contract = economy::exact_person_economy::contract(state, contract_id);
			if(contract && contract->occupation < 3)
				staffing_mix[contract->occupation] += std::max(0.0f, contract->labor_capacity);
		}
		auto mix_total = staffing_mix[0] + staffing_mix[1] + staffing_mix[2];
		if(!std::isfinite(mix_total) || mix_total <= epsilon) {
			staffing_mix[0] = 1.0f;
			mix_total = 1.0f;
		}
		for(uint8_t occupation = general_occupation; occupation <= professional_occupation; ++occupation) {
			auto target = desired * staffing_mix[occupation] / mix_total;
			if(target <= epsilon) continue;
			float supplied = 0.0f;
			for(auto contract_id : economy::exact_person_economy::active_contracts_for_factory(state, factory)) {
				auto contract = economy::exact_person_economy::contract(state, contract_id);
				if(contract && contract->occupation == occupation)
					supplied += std::max(0.0f, contract->labor_capacity);
			}
			float open_capacity = 0.0f;
			for(auto offer : open_offers_for_factory(state, factory))
				if(state.world.job_offer_get_occupation(offer) == occupation)
					open_capacity += state.world.job_offer_get_labor_capacity(offer)
						* float(state.world.job_offer_get_openings(offer));
			auto shortage = target - supplied - open_capacity;
			if(!std::isfinite(shortage) || shortage <= epsilon) continue;
			auto openings = uint32_t(std::ceil(shortage));
			if(openings == 0) continue;
			auto terms = wage_offer_for_factory(state, factory, occupation);
			auto minimum = minimum_daily_wage(state, site, state.current_date);
		if(terms.pay_period_days == 0) continue;
		auto minimum_rate = minimum * float(terms.pay_period_days);
		if(minimum_rate > terms.wage_rate + epsilon) {
			// Hiring at the statutory rate is worthwhile only while expected
			// contribution per worker covers that real payroll obligation.
			if(desired <= 0.0f || decision.expected_unit_revenue - decision.expected_variable_cost < minimum) continue;
			terms.wage_rate = minimum_rate;
		}
			(void)post_job_offer(state, employer, factory, site, occupation, 1.0f,
				terms.wage_rate, terms.pay_period_days, payer, openings, state.current_date);
		}
	});
}

void process_worker_choices(sys::state& state) {
	assert(state.exact_population && "exact population must exist for worker choices");
	if(!state.exact_population) std::abort();
	auto offers = all_offers(state);
	for(auto offer : offers) refresh_offer(state, offer);
	auto consider_contract = [&](uint64_t contract_id, dcon::factory_id factory,
		dcon::institution_id institution) {
		auto contract = economy::exact_person_economy::contract(state, contract_id);
		if(!contract || contract->pay_period_days == 0) return;
		auto home = persons::home_site(state, contract->worker);
		if(!home) return;
		auto current_wage = contract->wage_rate * contract->labor_capacity
			/ float(contract->pay_period_days);
		auto current_net = household_mobility::commute_adjusted_daily_wage(state, home,
			contract->workplace, current_wage);
		for(auto offer : offers) {
			if(!open_for_application(state, offer) || state.world.job_offer_get_openings(offer) == 0 || state.world.job_offer_get_factory_from_job_offer_factory(offer) == factory || state.world.job_offer_get_institution_from_job_offer_institution(offer) == institution || !exact_worker_qualifies_for_offer(state, contract->worker, offer)) continue;
			auto net = household_mobility::commute_adjusted_daily_wage(state, home, offer);
			if(net > current_net * voluntary_quit_wage_gain + epsilon) {
				(void)labor_dynamics::quit_exact_employment(state, contract_id,
					labor_dynamics::separation_reason::worker_quit);
				return;
			}
		}
	};
	state.world.for_each_factory([&](dcon::factory_id factory) {
		for(auto contract : economy::exact_person_economy::active_contracts_for_factory(state, factory))
			consider_contract(contract, factory, {});
	});
	state.world.for_each_nation([&](dcon::nation_id nation) {
		for(auto institution : governance::institutions_of(state, nation))
			for(auto contract : economy::exact_person_economy::active_contracts_for_institution(state, institution))
				consider_contract(contract, {}, institution);
	});
}

void project_labor_price_view(sys::state& state) {
	using lane_totals = std::array<double, economy::labor::total>;
	std::vector<lane_totals> wage_sum(state.world.province_size());
	std::vector<lane_totals> capacity_sum(state.world.province_size());
	auto add = [&](dcon::site_id workplace, bool factory_role, uint8_t occupation,
		float wage_rate, uint16_t pay_period_days, float capacity) {
		if(!workplace || !state.world.site_is_valid(workplace) || occupation > 2 || pay_period_days == 0 || !std::isfinite(wage_rate) || wage_rate < 0.0f || !std::isfinite(capacity) || capacity <= 0.0f) return;
		auto province = state.world.site_get_province_from_site_location(workplace);
		if(!province || !state.world.province_is_valid(province) || province.index() >= wage_sum.size()) return;
		int32_t lane = occupation == 0 ? economy::labor::no_education
			: occupation == 1 ? (factory_role ? economy::labor::basic_education
				: economy::labor::high_education_and_accepted)
			: economy::labor::high_education;
		auto const weight = double(capacity);
		wage_sum[province.index()][size_t(lane)] += double(wage_rate)
			/ double(pay_period_days) * weight;
		capacity_sum[province.index()][size_t(lane)] += weight;
	};

	state.world.for_each_factory([&](dcon::factory_id factory) {
		for(auto id : exact_person_economy::active_contracts_for_factory(state, factory)) {
			auto contract = exact_person_economy::contract(state, id);
			if(contract) add(contract->workplace, true, contract->occupation,
				contract->wage_rate, contract->pay_period_days, contract->labor_capacity);
		}
	});
	state.world.for_each_nation([&](dcon::nation_id nation) {
		for(auto institution : governance::institutions_of(state, nation))
			for(auto id : exact_person_economy::active_contracts_for_institution(state, institution)) {
				auto contract = exact_person_economy::contract(state, id);
				if(contract) add(contract->workplace, false, contract->occupation,
					contract->wage_rate, contract->pay_period_days, contract->labor_capacity);
			}
	});
	state.world.for_each_job_offer([&](auto offer) {
		if(state.world.job_offer_get_status(offer) != uint8_t(offer_status::open) || state.world.job_offer_get_expires_on(offer) && state.world.job_offer_get_expires_on(offer) < state.current_date) return;
		auto factory = state.world.job_offer_get_factory_from_job_offer_factory(offer);
		add(state.world.job_offer_get_site_from_job_offer_site(offer), bool(factory),
			state.world.job_offer_get_occupation(offer), state.world.job_offer_get_wage_rate(offer),
			state.world.job_offer_get_pay_period_days(offer),
			state.world.job_offer_get_labor_capacity(offer)
				* float(state.world.job_offer_get_openings(offer)));
	});

	state.world.for_each_province([&](dcon::province_id province) {
		for(int32_t lane = 0; lane < economy::labor::total; ++lane) {
			auto const weight = capacity_sum[province.index()][size_t(lane)];
			auto const projected = weight > 0.0
				? float(wage_sum[province.index()][size_t(lane)] / weight)
				: price_properties::labor::min;
			state.world.province_set_labor_price(province, lane,
				std::isfinite(projected) && projected >= 0.0f
					? projected : price_properties::labor::min);
		}
	});
}

void recruit_local_applicants(sys::state& state) {
	auto engaged = economy::exact_person_economy::engaged_workers(state);
	auto is_engaged = [&](persons::person_key key) {
		return std::binary_search(engaged.begin(), engaged.end(), key, [](auto left, auto right) {
			return left.source_population_cell == right.source_population_cell
				? left.ordinal < right.ordinal : left.source_population_cell < right.source_population_cell;
		});
	};
	std::vector<persons::person_key> applied;
	for(auto offer : all_offers(state)) {
		if(!open_for_application(state, offer)) continue;
		uint32_t pending = 0;
		for(auto id : economy::exact_person_economy::applications_for_offer(state, offer)) {
			auto application = economy::exact_person_economy::application(state, id);
			if(application && application->status == economy::exact_person_economy::application_status::pending) ++pending;
		}
		auto openings = state.world.job_offer_get_openings(offer);
		if(openings <= pending) continue;
		auto needed = openings - pending;
		auto workplace = state.world.job_offer_get_site_from_job_offer_site(offer);
		auto province = workplace ? state.world.site_get_province_from_site_location(workplace) : dcon::province_id{};
		if(!province) continue;
		std::vector<dcon::pop_id> populations;
		for(auto location : state.world.province_get_pop_location(province))
			populations.push_back(location.get_pop());
		sort_ids(populations);
		for(auto population : populations) {
			if(needed == 0) break;
			auto cell = persons::source_population_cell_for_population(state, population);
			if(cell == 0) continue;
			// Cohort members leave their household's land only for a wage worth more
			// than what a worker produces at home.
			if(auto cohort = economy::households::household_for(state, province,
				economy::households::role_for_pop_type(state, state.world.pop_get_poptype(population)))) {
				auto offered = household_mobility::commute_adjusted_daily_wage(state,
					world::spatial_runtime::site_for_province(state, province), offer);
				if(!(offered > reservation_premium * economy::households::reservation_wage(state, cohort))) continue;
			}
			auto count = persons::exact_population::literal_count_for_cell(state, cell);
			// Anchors are the working members of the source cell; ordinals between
			// them are their dependents.
			for(uint64_t ordinal = 0; ordinal < count && needed > 0; ordinal += 4) {
				persons::person_key worker{cell, ordinal};
				if(!persons::alive(state, worker) || persons::current_population(state, worker) != population
					|| is_engaged(worker)
					|| std::find(applied.begin(), applied.end(), worker) != applied.end()
					|| persons::exact_population::has_military_assignment(state, worker)
					|| !economy::exact_person_economy::is_labor_force_participant(state, worker)
					|| !exact_worker_qualifies_for_offer(state, worker, offer)) continue;
				auto home = persons::home_site(state, worker);
				if(!home || state.world.site_get_province_from_site_location(home) != province) continue;
				if(economy::exact_person_economy::submit_application(state, worker, offer, state.current_date)) {
					applied.push_back(worker);
					--needed;
				}
			}
		}
	}
}

void process(sys::state& state) {
	for(auto offer : all_offers(state)) refresh_offer(state, offer);
	process_factory_vacancies(state);
	process_worker_choices(state);
	// Exact persons apply against concrete offers, then hiring creates a
	// person-specific contract and decrements the offer's opening count.
	labor_dynamics::process_displaced_job_search(state);
	recruit_local_applicants(state);
	economy::exact_person_economy::process_pending_applications(state);
}

} // namespace economy::physical::job_market
