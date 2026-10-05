#include "social_entitlements.hpp"

#include "economy/accounts/accounts.hpp"
#include "economy/exact_person_economy.hpp"
#include "economy/physical/labor_dynamics.hpp"
#include "economy/money.hpp"
#include "governance/finance/finance.hpp"
#include "governance/governance.hpp"
#include "governance/law/law.hpp"
#include "governance/policy.hpp"
#include "governance/public_administration.hpp"
#include "persons/persons.hpp"
#include "system_state.hpp"
#include "world/site.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace economy::social_entitlements {
namespace {

constexpr uint8_t unemployment_claim = 0;
constexpr uint8_t active_claim = 1;
constexpr uint8_t ended_claim = 2;
constexpr float epsilon = 1.0e-5f;

dcon::nation_id nation_for(sys::state const& state, economy::physical::labor_dynamics::separation_event const& event) {
	if(!event.factory || !state.world.factory_is_valid(event.factory)) return {};
	auto workplace = world::site::site_for_factory(state, event.factory);
	auto province = workplace ? state.world.site_get_province_from_site_location(workplace) : dcon::province_id{};
	return province ? state.world.province_get_nation_from_province_ownership(province) : dcon::nation_id{};
}

dcon::institution_id fund_for(sys::state const& state, dcon::nation_id nation, sys::date date) {
	auto configured = governance::law::effective_topic_target(state, governance::national(nation),
		governance::policy::topic_id::unemployment_replacement, date);
	return configured ? configured : governance::public_administration::tax_authority_for(state, nation);
}

bool claim_for_event(sys::state const& state, uint64_t event_id) {
	bool found = false;
	state.world.for_each_social_entitlement_claim([&](dcon::social_entitlement_claim_id claim) {
		found = found || state.world.social_entitlement_claim_get_separation_event_id(claim) == event_id;
	});
	return found;
}

void establish_claims(sys::state& state) {
	for(uint64_t id = 1; id <= economy::physical::labor_dynamics::separation_event_count(state); ++id) {
		if(claim_for_event(state, id)) continue;
		auto event = economy::physical::labor_dynamics::separation_event_at(state, id);
		if(!event || (event->reason != economy::physical::labor_dynamics::separation_reason::employer_layoff
			&& event->reason != economy::physical::labor_dynamics::separation_reason::worker_quit_arrears)) continue;
		auto nation = nation_for(state, *event);
		if(!nation || !event->date) continue;
		auto scope = governance::national(nation);
		auto replacement = governance::law::effective_topic(state, scope,
			governance::policy::topic_id::unemployment_replacement, event->date);
		auto duration = governance::law::effective_topic(state, scope,
			governance::policy::topic_id::unemployment_duration, event->date);
		if(!replacement || !duration) continue;
		auto rate = std::get<float>(*replacement);
		auto days = std::get<int32_t>(*duration);
		if(!(rate > 0.0f) || days <= 0) continue;
		auto contract = economy::exact_person_economy::contract(state, event->contract_id);
		if(!contract || contract->pay_period_days == 0) continue;
		auto settlement = event->factory && state.world.factory_is_valid(event->factory)
			? state.world.factory_get_payroll_settlement(event->factory) : economy::money;
		if(!settlement) settlement = economy::money;
		auto daily_wage = contract->wage_rate * contract->labor_capacity / float(contract->pay_period_days);
		if(!std::isfinite(daily_wage) || daily_wage <= epsilon) continue;
		auto fund = fund_for(state, nation, event->date);
		auto claim = state.world.create_social_entitlement_claim();
		state.world.social_entitlement_claim_set_kind(claim, unemployment_claim);
		state.world.social_entitlement_claim_set_source_population_cell(claim, event->exact_worker.source_population_cell);
		state.world.social_entitlement_claim_set_person_ordinal(claim, event->exact_worker.ordinal);
		state.world.social_entitlement_claim_set_separation_event_id(claim, event->id);
		state.world.social_entitlement_claim_set_established_on(claim, event->date);
		state.world.social_entitlement_claim_set_last_assessed_on(claim, event->date);
		state.world.social_entitlement_claim_set_expires_on(claim, event->date + days);
		state.world.social_entitlement_claim_set_daily_benefit(claim, daily_wage * rate);
		state.world.social_entitlement_claim_set_settlement(claim, settlement);
		state.world.social_entitlement_claim_set_amount_due(claim, 0.0f);
		state.world.social_entitlement_claim_set_amount_paid(claim, 0.0f);
		state.world.social_entitlement_claim_set_status(claim, active_claim);
		state.world.force_create_social_entitlement_claim_nation(claim, nation);
		if(fund) state.world.force_create_social_entitlement_claim_institution(claim, fund);
	}
}

void pay_claim(sys::state& state, dcon::social_entitlement_claim_id claim) {
	if(state.world.social_entitlement_claim_get_kind(claim) != unemployment_claim
		|| state.world.social_entitlement_claim_get_status(claim) == ended_claim) return;
	persons::person_key recipient{
		state.world.social_entitlement_claim_get_source_population_cell(claim),
		state.world.social_entitlement_claim_get_person_ordinal(claim)};
	if(!persons::exists(state, recipient) || !persons::alive(state, recipient)) {
		state.world.social_entitlement_claim_set_status(claim, ended_claim);
		return;
	}
	auto event = economy::physical::labor_dynamics::separation_event_at(state,
		state.world.social_entitlement_claim_get_separation_event_id(claim));
	if(!event) { state.world.social_entitlement_claim_set_status(claim, ended_claim); return; }
	auto fund = state.world.social_entitlement_claim_get_institution_from_social_entitlement_claim_institution(claim);
	if(!fund) fund = governance::public_administration::tax_authority_for(state,
		state.world.social_entitlement_claim_get_nation_from_social_entitlement_claim_nation(claim));
	auto settlement = state.world.social_entitlement_claim_get_settlement(claim);
	if(!settlement) settlement = economy::money;
	auto expires = state.world.social_entitlement_claim_get_expires_on(claim);
	auto last = state.world.social_entitlement_claim_get_last_assessed_on(claim);
	if(!economy::exact_person_economy::person_has_active_contract(state, recipient) && state.current_date > last) {
		auto through = state.current_date < expires ? state.current_date : expires;
		if(through > last) {
			auto accrued = float(through.to_raw_value() - last.to_raw_value())
				* state.world.social_entitlement_claim_get_daily_benefit(claim);
			auto due = state.world.social_entitlement_claim_get_amount_due(claim) + accrued;
			if(std::isfinite(due)) state.world.social_entitlement_claim_set_amount_due(claim, due);
			state.world.social_entitlement_claim_set_last_assessed_on(claim, through);
		}
	} else if(economy::exact_person_economy::person_has_active_contract(state, recipient)) {
		state.world.social_entitlement_claim_set_last_assessed_on(claim, state.current_date);
	}
	auto due = state.world.social_entitlement_claim_get_amount_due(claim);
	if(fund && due > epsilon) {
		auto treasury = governance::finance::treasury_account_for(state, fund, settlement);
		if(!treasury) treasury = governance::finance::open_treasury_account(state, fund, settlement);
		auto available = treasury ? economy::accounts::balance(state, treasury) : 0.0f;
		auto amount = std::min(due, std::max(0.0f, available));
		if(amount > epsilon) {
			auto account = economy::exact_person_economy::find_account(state, recipient, settlement);
			if(!account) account = economy::exact_person_economy::open_account(state, recipient, settlement);
			auto paid = account ? governance::finance::authorized_spend_exact_person_by_institution(state,
				fund, treasury, recipient, account, amount, state.current_date) : dcon::fiscal_action_id{};
			if(paid) {
				state.world.social_entitlement_claim_set_amount_due(claim, due - amount);
				state.world.social_entitlement_claim_set_amount_paid(claim,
					state.world.social_entitlement_claim_get_amount_paid(claim) + amount);
			}
		}
	}
	if(state.current_date >= expires && state.world.social_entitlement_claim_get_amount_due(claim) <= epsilon)
		state.world.social_entitlement_claim_set_status(claim, ended_claim);
}

} // namespace

void process(sys::state& state) {
	establish_claims(state);
	std::vector<dcon::social_entitlement_claim_id> claims;
	state.world.for_each_social_entitlement_claim([&](auto claim) { claims.push_back(claim); });
	std::sort(claims.begin(), claims.end(), [](auto a, auto b) { return a.index() < b.index(); });
	for(auto claim : claims) pay_claim(state, claim);
}

} // namespace economy::social_entitlements
