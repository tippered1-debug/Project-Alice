#include "job_market.hpp"

#include "accounts/accounts.hpp"
#include "actors/organizations/organizations.hpp"
#include "concrete_labor.hpp"
#include "economy/firm_agency.hpp"
#include "persons/persons.hpp"
#include "system_state.hpp"
#include "world/site.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace economy::physical::job_market {
namespace {
constexpr float epsilon = 1.0e-6f;

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
	if(offer && state.world.job_offer_is_valid(offer)
		&& state.world.job_offer_get_status(offer) == uint8_t(offer_status::open)
		&& expired_on(state, offer))
		state.world.job_offer_set_status(offer, uint8_t(offer_status::expired));
}

bool open_for_application(sys::state const& state, dcon::job_offer_id offer) {
	return offer && state.world.job_offer_is_valid(offer)
		&& state.world.job_offer_get_status(offer) == uint8_t(offer_status::open)
		&& !expired_on(state, offer);
}

bool accepts_worker(sys::state const& state, dcon::person_id person, dcon::job_offer_id offer) {
	if(!person || !state.world.person_is_valid(person) || !state.world.person_get_alive(person)
		|| !open_for_application(state, offer)) return false;
	auto factory = state.world.job_offer_get_factory_from_job_offer_factory(offer);
	auto workplace = state.world.job_offer_get_site_from_job_offer_site(offer);
	return factory && state.world.factory_is_valid(factory) && workplace && state.world.site_is_valid(workplace)
		&& !concrete_labor::person_has_active_contract(state, person);
}

dcon::monetary_account_id worker_account_for(sys::state& state, dcon::person_id person,
	dcon::commodity_id settlement) {
	auto actor = persons::actor_for_person(state, person);
	if(!actor || !settlement) return {};
	std::vector<dcon::monetary_account_id> candidates;
	state.world.economic_actor_for_each_monetary_account_owner_as_economic_actor(actor, [&](auto relation) {
		auto account = state.world.monetary_account_owner_get_monetary_account(relation);
		if(account && accounts::settlement_of(state, account) == settlement) candidates.push_back(account);
	});
	std::sort(candidates.begin(), candidates.end(), [](auto left, auto right) { return left.index() < right.index(); });
	if(!candidates.empty()) return candidates.front();
	return accounts::open_account(state, actor, settlement);
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

std::vector<dcon::job_application_id> pending_applications(sys::state const& state, dcon::job_offer_id offer) {
	std::vector<dcon::job_application_id> result;
	state.world.job_offer_for_each_job_application_offer_as_job_offer(offer, [&](auto relation) {
		auto application = state.world.job_application_offer_get_job_application(relation);
		if(application && state.world.job_application_get_status(application) == uint8_t(application_status::pending))
			result.push_back(application);
	});
	sort_ids(result);
	return result;
}

bool duplicate_application(sys::state const& state, dcon::person_id person, dcon::job_offer_id offer) {
	bool result = false;
	state.world.person_for_each_job_application_person_as_person(person, [&](auto relation) {
		if(result) return;
		auto application = state.world.job_application_person_get_job_application(relation);
		if(application && state.world.job_application_get_job_offer_from_job_application_offer(application) == offer
			&& state.world.job_application_get_status(application) == uint8_t(application_status::pending)) result = true;
	});
	return result;
}
}

dcon::job_offer_id post_job_offer(sys::state& state, dcon::economic_actor_id employer,
	dcon::factory_id factory, dcon::site_id workplace, uint8_t occupation,
	float labor_capacity, float wage_rate, uint16_t pay_period_days,
	dcon::monetary_account_id payer_account, uint32_t openings,
	sys::date created_on, sys::date expires_on) {
	if(!employer || !factory || !state.world.factory_is_valid(factory)
		|| !std::isfinite(labor_capacity) || labor_capacity <= 0.0f
		|| !std::isfinite(wage_rate) || wage_rate < 0.0f || pay_period_days == 0
		|| !payer_account || accounts::owner_of(state, payer_account) != employer) return {};
	auto operator_actor = actors::organizations::operator_actor_for_factory(state, factory);
	if(operator_actor && operator_actor != employer) return {};
	if(!workplace) workplace = world::site::site_for_factory(state, factory);
	if(!workplace || !state.world.site_is_valid(workplace)) return {};
	auto settlement = accounts::settlement_of(state, payer_account);
	auto factory_settlement = state.world.factory_get_payroll_settlement(factory);
	if(!settlement || (factory_settlement && factory_settlement != settlement)) return {};
	created_on = normalized_date(state, created_on);
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

bool close_job_offer(sys::state& state, dcon::job_offer_id offer) {
	if(!offer || !state.world.job_offer_is_valid(offer)
		|| state.world.job_offer_get_status(offer) != uint8_t(offer_status::open)) return false;
	state.world.job_offer_set_status(offer, uint8_t(offer_status::closed));
	return true;
}

bool expire_job_offer(sys::state& state, dcon::job_offer_id offer) {
	if(!offer || !state.world.job_offer_is_valid(offer)
		|| state.world.job_offer_get_status(offer) != uint8_t(offer_status::open)) return false;
	state.world.job_offer_set_status(offer, uint8_t(offer_status::expired));
	return true;
}

bool add_job_offer_openings(sys::state& state, dcon::job_offer_id offer, uint32_t openings) {
	if(!offer || !state.world.job_offer_is_valid(offer)
		|| state.world.job_offer_get_status(offer) != uint8_t(offer_status::open)
		|| openings > std::numeric_limits<uint32_t>::max() - state.world.job_offer_get_openings(offer)) return false;
	if(expired_on(state, offer)) {
		state.world.job_offer_set_status(offer, uint8_t(offer_status::expired));
		return false;
	}
	state.world.job_offer_set_openings(offer, state.world.job_offer_get_openings(offer) + openings);
	return true;
}

dcon::job_application_id submit_job_application(sys::state& state, dcon::person_id person,
	dcon::job_offer_id offer, sys::date applied_on) {
	if(!accepts_worker(state, person, offer) || duplicate_application(state, person, offer)) return {};
	applied_on = normalized_date(state, applied_on);
	if(applied_on < state.world.job_offer_get_created_on(offer)) return {};
	auto application = state.world.create_job_application();
	state.world.job_application_set_applied_on(application, applied_on);
	state.world.job_application_set_status(application, uint8_t(application_status::pending));
	state.world.force_create_job_application_person(application, person);
	state.world.force_create_job_application_offer(application, offer);
	state.world.force_create_job_offer_application(offer, application);
	return application;
}

bool withdraw_job_application(sys::state& state, dcon::job_application_id application) {
	if(!application || !state.world.job_application_is_valid(application)
		|| state.world.job_application_get_status(application) != uint8_t(application_status::pending)) return false;
	state.world.job_application_set_status(application, uint8_t(application_status::withdrawn));
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

std::vector<dcon::job_offer_id> open_offers_for_person(sys::state const& state, dcon::person_id person) {
	std::vector<dcon::job_offer_id> result;
	for(auto offer : all_offers(state))
		if(open_for_application(state, offer) && state.world.job_offer_get_openings(offer) > 0
			&& accepts_worker(state, person, offer)) result.push_back(offer);
	return result;
}

std::vector<dcon::job_application_id> applications_for_offer(sys::state const& state, dcon::job_offer_id offer) {
	std::vector<dcon::job_application_id> result;
	if(!offer) return result;
	state.world.job_offer_for_each_job_application_offer_as_job_offer(offer, [&](auto relation) {
		auto application = state.world.job_application_offer_get_job_application(relation);
		if(application) result.push_back(application);
	});
	sort_ids(result);
	return result;
}

std::vector<dcon::job_application_id> applications_for_person(sys::state const& state, dcon::person_id person) {
	std::vector<dcon::job_application_id> result;
	if(!person) return result;
	state.world.person_for_each_job_application_person_as_person(person, [&](auto relation) {
		auto application = state.world.job_application_person_get_job_application(relation);
		if(application) result.push_back(application);
	});
	sort_ids(result);
	return result;
}

void process_pending_applications(sys::state& state) {
	for(auto offer : all_offers(state)) {
		refresh_offer(state, offer);
		if(!open_for_application(state, offer) || state.world.job_offer_get_openings(offer) == 0) continue;
		for(auto application : pending_applications(state, offer)) {
			if(state.world.job_offer_get_openings(offer) == 0) break;
			auto person = state.world.job_application_get_person_from_job_application_person(application);
			if(!accepts_worker(state, person, offer)) {
				state.world.job_application_set_status(application, uint8_t(application_status::rejected));
				continue;
			}
			auto payer = state.world.job_offer_get_monetary_account_from_job_offer_payer_account(offer);
			auto worker_account = worker_account_for(state, person, accounts::settlement_of(state, payer));
			auto contract = concrete_labor::create_employment_contract(state, person,
				state.world.job_offer_get_economic_actor_from_job_offer_employer(offer),
				state.world.job_offer_get_factory_from_job_offer_factory(offer),
				state.world.job_offer_get_site_from_job_offer_site(offer),
				state.world.job_offer_get_occupation(offer), state.world.job_offer_get_labor_capacity(offer),
				state.world.job_offer_get_wage_rate(offer), state.world.job_offer_get_pay_period_days(offer),
				payer, worker_account, state.current_date);
			if(!contract) {
				state.world.job_application_set_status(application, uint8_t(application_status::rejected));
				continue;
			}
			state.world.job_application_set_status(application, uint8_t(application_status::accepted));
			state.world.job_offer_set_openings(offer, state.world.job_offer_get_openings(offer) - 1);
		}
	}
}

void process_factory_vacancies(sys::state& state) {
	state.world.for_each_factory([&](dcon::factory_id factory) {
		if(!state.world.factory_get_canonical_production(factory)) return;
		auto employer = actors::organizations::operator_actor_for_factory(state, factory);
		auto settlement = state.world.factory_get_payroll_settlement(factory);
		auto payer = settlement ? accounts::find_account(state, employer, settlement) : dcon::monetary_account_id{};
		if(!employer || !payer) return;
		auto desired = firm_agency::decide_factory(state, factory).desired_units;
		auto supplied = concrete_labor::labor_supplied_to_factory(state, factory);
		if(!std::isfinite(desired) || desired <= supplied + epsilon) return;
		auto site = world::site::site_for_factory(state, factory);
		if(!site) return;
		float open_capacity = 0.0f;
		for(auto offer : open_offers_for_factory(state, factory))
			open_capacity += state.world.job_offer_get_labor_capacity(offer) * state.world.job_offer_get_openings(offer);
		auto shortage = desired - supplied - open_capacity;
		if(!std::isfinite(shortage) || shortage <= epsilon) return;
		auto openings = uint32_t(std::ceil(shortage));
		if(openings == 0) return;
		(void)post_job_offer(state, employer, factory, site, 0, 1.0f, 1.0f, 1,
			payer, openings, state.current_date);
	});
}

void process(sys::state& state) {
	process_factory_vacancies(state);
	process_pending_applications(state);
}

} // namespace economy::physical::job_market
