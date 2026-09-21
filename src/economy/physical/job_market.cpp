#include "job_market.hpp"

#include "accounts/accounts.hpp"
#include "actors/organizations/organizations.hpp"
#include "concrete_labor.hpp"
#include "economy/firm_agency.hpp"
#include "economy/exact_person_economy.hpp"
#include "economy/causal_order.hpp"
#include "labor_dynamics.hpp"
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
	if(!persons::is_work_eligible(state, person)
		|| !open_for_application(state, offer)) return false;
	auto factory = state.world.job_offer_get_factory_from_job_offer_factory(offer);
	auto workplace = state.world.job_offer_get_site_from_job_offer_site(offer);
	return factory && state.world.factory_is_valid(factory) && workplace && state.world.site_is_valid(workplace)
		&& !concrete_labor::person_has_active_contract(state, person)
		&& !labor_dynamics::legacy_separated_on_date(state, person, state.current_date);
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

struct wage_offer_terms {
	float wage_rate = 1.0f;
	uint16_t pay_period_days = 1;
};

wage_offer_terms wage_offer_for_factory(sys::state const& state, dcon::factory_id factory, uint8_t occupation) {
	for(auto contract : concrete_labor::active_contracts_for_factory(state, factory)) {
		if(state.world.employment_contract_get_occupation(contract) != occupation) continue;
		auto wage = state.world.employment_contract_get_wage_rate(contract);
		auto period = state.world.employment_contract_get_pay_period_days(contract);
		if(std::isfinite(wage) && wage >= 0.0f && period != 0) return {wage, period};
	}
	for(auto contract_id : economy::exact_person_economy::active_contracts_for_factory(state, factory)) {
		auto contract = economy::exact_person_economy::contract(state, contract_id);
		if(!contract || contract->occupation != occupation) continue;
		if(std::isfinite(contract->wage_rate) && contract->wage_rate >= 0.0f && contract->pay_period_days != 0)
			return {contract->wage_rate, contract->pay_period_days};
	}

	// A new concrete vacancy has no contract from which to inherit terms. Use a
	// deterministic factory-specific bootstrap: ten percent of expected unit
	// revenue, with a small concrete floor, paid daily. This is intentionally
	// independent of provincial aggregate labor wages and can be replaced by a
	// richer firm wage policy later.
	auto expected_revenue = firm_agency::decide_factory(state, factory).expected_unit_revenue;
	auto bootstrap = std::isfinite(expected_revenue) ? expected_revenue * 0.10f : 0.0f;
	return {std::max(1.0f, bootstrap), 1};
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
	if(economy::causal_order::sequence_for_dcon(state, economy::causal_order::event_kind::job_application,
		uint64_t(application.index())) == 0) return {};
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
	struct candidate {
		bool exact = false;
		dcon::job_application_id legacy{};
		uint64_t exact_id = 0;
		dcon::job_offer_id offer{};
		sys::date applied_on{};
		uint64_t causal_sequence = 0;
		uint64_t stable_id = 0;
	};
	for(auto offer : all_offers(state)) refresh_offer(state, offer);
	std::vector<candidate> pending;
	for(auto offer : all_offers(state)) {
		for(auto application : pending_applications(state, offer)) {
			pending.push_back({false, application, 0, offer,
				state.world.job_application_get_applied_on(application),
				economy::causal_order::sequence_for_dcon(state, economy::causal_order::event_kind::job_application,
					uint64_t(application.index())), uint64_t(application.index())});
		}
		for(auto id : economy::exact_person_economy::applications_for_offer(state, offer)) {
			auto application = economy::exact_person_economy::application(state, id);
			if(application && application->status == economy::exact_person_economy::application_status::pending)
				pending.push_back({true, {}, id, offer, application->applied_on, application->causal_sequence, id});
		}
	}
	std::sort(pending.begin(), pending.end(), [](auto const& left, auto const& right) {
		if(economy::causal_order::before({left.applied_on, left.causal_sequence},
			{right.applied_on, right.causal_sequence})) return true;
		if(economy::causal_order::before({right.applied_on, right.causal_sequence},
			{left.applied_on, left.causal_sequence})) return false;
		return left.stable_id < right.stable_id;
	});
	for(auto const& item : pending) {
		if(!item.offer || !open_for_application(state, item.offer) || state.world.job_offer_get_openings(item.offer) == 0) continue;
		if(item.exact) {
			(void)economy::exact_person_economy::accept_pending_application(state, item.exact_id);
			continue;
		}
		auto application = item.legacy;
		auto person = state.world.job_application_get_person_from_job_application_person(application);
		if(!accepts_worker(state, person, item.offer)) {
			state.world.job_application_set_status(application, uint8_t(application_status::rejected));
			continue;
		}
		auto payer = state.world.job_offer_get_monetary_account_from_job_offer_payer_account(item.offer);
		auto worker_account = worker_account_for(state, person, accounts::settlement_of(state, payer));
		auto contract = concrete_labor::create_employment_contract(state, person,
			state.world.job_offer_get_economic_actor_from_job_offer_employer(item.offer),
			state.world.job_offer_get_factory_from_job_offer_factory(item.offer),
			state.world.job_offer_get_site_from_job_offer_site(item.offer),
			state.world.job_offer_get_occupation(item.offer), state.world.job_offer_get_labor_capacity(item.offer),
			state.world.job_offer_get_wage_rate(item.offer), state.world.job_offer_get_pay_period_days(item.offer),
			payer, worker_account, state.current_date);
		if(!contract) state.world.job_application_set_status(application, uint8_t(application_status::rejected));
		else {
			state.world.job_application_set_status(application, uint8_t(application_status::accepted));
			state.world.job_offer_set_openings(item.offer, state.world.job_offer_get_openings(item.offer) - 1);
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
		auto terms = wage_offer_for_factory(state, factory, 0);
		(void)post_job_offer(state, employer, factory, site, 0, 1.0f, terms.wage_rate, terms.pay_period_days,
			payer, openings, state.current_date);
	});
}

void process_job_search(sys::state& state) {
	auto offers = all_offers(state);
	for(auto offer : offers) refresh_offer(state, offer);

	std::vector<dcon::job_offer_id> candidates;
	for(auto offer : offers) {
		if(open_for_application(state, offer) && state.world.job_offer_get_openings(offer) > 0)
			candidates.push_back(offer);
	}

	std::vector<dcon::person_id> people;
	state.world.for_each_person([&](auto person) { people.push_back(person); });
	sort_ids(people);
	for(auto person : people) {
		if(!state.world.person_get_alive(person) || concrete_labor::person_has_active_contract(state, person)) continue;

		bool blocked_by_pending = false;
		for(auto application : applications_for_person(state, person)) {
			if(state.world.job_application_get_status(application) != uint8_t(application_status::pending)) continue;
			auto offer = state.world.job_application_get_job_offer_from_job_application_offer(application);
			if(open_for_application(state, offer)) {
				blocked_by_pending = true;
				continue;
			}
			state.world.job_application_set_status(application, uint8_t(application_status::rejected));
		}
		if(blocked_by_pending) continue;

		dcon::job_offer_id best{};
		for(auto offer : candidates) {
			if(!accepts_worker(state, person, offer)) continue;
			if(!best) {
				best = offer;
				continue;
			}
			auto wage = state.world.job_offer_get_wage_rate(offer);
			auto best_wage = state.world.job_offer_get_wage_rate(best);
			if(wage > best_wage
				|| (wage == best_wage && offer.index() < best.index())) best = offer;
		}
		if(best) (void)submit_job_application(state, person, best, state.current_date);
	}
}

void process(sys::state& state) {
	for(auto offer : all_offers(state)) refresh_offer(state, offer);
	process_factory_vacancies(state);
	// Vacancies must exist before either representation searches. The exact
	// search remains sparse: it only visits the displaced-worker queue.
	labor_dynamics::process_displaced_job_search(state);
	process_job_search(state);
	process_pending_applications(state);
}

} // namespace economy::physical::job_market
