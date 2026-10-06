#include "intelligence.hpp"

#include "system_state.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/demographics.hpp"
#include "economy/exact_person_economy.hpp"
#include "economy/money.hpp"
#include "governance/finance/finance.hpp"

#include <algorithm>
#include <cmath>

namespace governance::intelligence {

dcon::institution_id agency_for(sys::state const& state, dcon::nation_id nation, service_kind kind) {
	if(!nation || !state.world.nation_is_valid(nation)
		|| (kind != service_kind::intelligence && kind != service_kind::counterintelligence)) return {};
	dcon::institution_id result{};
	for(auto institution : institutions_of(state, nation))
		if(state.world.institution_get_kind(institution) == uint8_t(institution_kind::agency)
			&& service_kind(state.world.institution_get_service(institution)) == kind) {
			if(!result || institution.index() < result.index()) result = institution;
		}
	return result;
}

dcon::office_id director_office_for(sys::state const& state, dcon::institution_id agency) {
	if(!agency || !state.world.institution_is_valid(agency)
		|| state.world.institution_get_kind(agency) != uint8_t(institution_kind::agency)) return {};
	dcon::office_id result{};
	for(auto office : offices_of(state, agency))
		if(state.world.office_get_kind(office) == uint8_t(office_kind::agency_director)) {
			if(!result || office.index() < result.index()) result = office;
		}
	return result;
}

float operational_readiness(sys::state const& state, dcon::institution_id agency) {
	if(!agency || !state.world.institution_is_valid(agency)
		|| state.world.institution_get_kind(agency) != uint8_t(institution_kind::agency)) return 0.0f;
	auto nation = nation_of(state, agency);
	auto target = state.world.nation_get_demographics(nation, demographics::total)
		* state.world.institution_get_staffing_per_capita(agency);
	if(!(target > 0.0f) || !std::isfinite(target)) return 0.0f;
	float staffed = 0.0f;
	for(auto contract_id : economy::exact_person_economy::active_contracts_for_institution(state, agency)) {
		auto record = economy::exact_person_economy::contract(state, contract_id);
		if(record && std::isfinite(record->labor_capacity)) staffed += std::max(0.0f, record->labor_capacity);
	}
	auto monthly_cost = economy::exact_person_economy::wage_due_for_institution(state, agency) * 30.0f;
	auto treasury = finance::treasury_account_for(state, agency, economy::money);
	auto cash = economy::accounts::balance(state, treasury);
	if(!(staffed > 0.0f) || !(monthly_cost > 0.0f)
		|| !std::isfinite(monthly_cost) || !std::isfinite(cash)) return 0.0f;
	auto staffing_ratio = std::clamp(staffed / target, 0.0f, 1.0f);
	auto funding_ratio = std::clamp(cash / monthly_cost, 0.0f, 1.0f);
	return std::sqrt(staffing_ratio * funding_ratio);
}

dcon::information_report_id publish_assessment(sys::state& state,
	dcon::nation_id observer, dcon::nation_id subject,
	economy::information::information_fact_kind kind, float reported_value,
	float confidence, sys::date fact_date, sys::date received_on) {
	if(!observer || !subject || observer == subject || !fact_date || received_on < fact_date
		|| !state.world.nation_is_valid(observer) || !state.world.nation_is_valid(subject)) return {};
	auto agency = agency_for(state, observer, service_kind::intelligence);
	auto recipient = find_institution(state, observer, institution_kind::central_government);
	if(!agency || !recipient) return {};
	auto source_actor = actor_for_institution(state, agency);
	auto recipient_actor = actor_for_institution(state, recipient);
	dcon::information_report_id existing{};
	state.world.economic_actor_for_each_information_report_source_as_economic_actor(source_actor,
		[&](dcon::information_report_source_id relation) {
			auto report = state.world.information_report_source_get_information_report(relation);
			if(!report || state.world.information_report_get_fact_kind(report) != uint8_t(kind)
				|| state.world.information_report_get_nation_from_information_report_subject(report) != subject
				|| state.world.information_report_get_fact_date(report) != fact_date
				|| state.world.information_report_get_economic_actor_from_information_report_recipient(report) != recipient_actor)
				return;
			existing = report;
		});
	if(existing)
		return existing;
	return economy::information::publish_report(state, kind, source_actor,
		recipient_actor, subject, economy::money, reported_value,
		fact_date, fact_date, received_on, confidence);
}

} // namespace governance::intelligence
