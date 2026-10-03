#include "policy.hpp"

#include "system_state.hpp"
#include "governance/governance.hpp"
#include "governance/law/law.hpp"
#include "governance/offices.hpp"

#include <algorithm>
#include <cmath>

namespace governance::policy {
namespace {

constexpr float tolerance = 1.0e-4f;

bool fiscal_instrument(sys::state const& state, dcon::legal_instrument_id instrument) {
	bool result = false;
	state.world.legal_instrument_for_each_legal_instrument_policy_rule_as_legal_instrument(instrument, [&](auto relation) {
		auto kind = law::policy_rule_kind(state.world.policy_rule_get_kind(state.world.legal_instrument_policy_rule_get_policy_rule(relation)));
		if(kind == law::policy_rule_kind::income_tax_rate || kind == law::policy_rule_kind::appropriation_share
			|| kind == law::policy_rule_kind::disbursement_rate) result = true;
	});
	return result;
}

} // namespace

float lower_bound(dimension) { return 0.0f; }
float upper_bound(dimension which) { return which == dimension::tax_level ? maximum_tax_level : 1.0f; }

position clamp(position value) {
	for(size_t i = 0; i < dimension_count; ++i) {
		auto which = dimension(i);
		value[i] = std::isfinite(value[i]) ? std::clamp(value[i], lower_bound(which), upper_bound(which)) : lower_bound(which);
	}
	return value;
}

float distance(position const& a, position const& b) {
	float result = 0.0f;
	for(size_t i = 0; i < dimension_count; ++i) {
		auto range = upper_bound(dimension(i)) - lower_bound(dimension(i));
		auto delta = (a[i] - b[i]) / range;
		result += delta * delta;
	}
	return result;
}

fiscal_rules rules_for(sys::state const& state, dcon::nation_id nation, position const& raw) {
	auto value = clamp(raw);
	fiscal_rules result{};
	auto base = value[size_t(dimension::tax_level)];
	auto spread = value[size_t(dimension::progressivity)];
	result.tax_rates[0] = std::clamp(base * (1.0f - 0.6f * spread), 0.0f, 1.0f);
	result.tax_rates[1] = std::clamp(base, 0.0f, 1.0f);
	result.tax_rates[2] = std::clamp(base * (1.0f + spread), 0.0f, 1.0f);
	std::vector<dcon::institution_id> territorial;
	for(auto institution : institutions_of(state, nation)) {
		if(territory_of(state, institution)) {
			if(state.world.institution_get_staffing_per_capita(institution) > 0.0f) territorial.push_back(institution);
			continue;
		}
		auto service = service_kind(state.world.institution_get_service(institution));
		float share = -1.0f;
		if(service == service_kind::education) share = value[size_t(dimension::education)];
		else if(service == service_kind::policing) share = value[size_t(dimension::policing)];
		else if(service == service_kind::construction) share = value[size_t(dimension::public_works)];
		if(share > 0.0f) result.shares.push_back({ institution, share });
	}
	auto local = value[size_t(dimension::local_government)];
	if(local > 0.0f && !territorial.empty())
		for(auto institution : territorial) result.shares.push_back({ institution, local / float(territorial.size()) });
	return result;
}

bool current(sys::state const& state, dcon::nation_id nation, sys::date date, position& result) {
	auto scope = national(nation);
	auto middle = law::effective_amount(state, scope, law::policy_rule_kind::income_tax_rate, date, 1);
	auto rich = law::effective_amount(state, scope, law::policy_rule_kind::income_tax_rate, date, 2);
	if(!middle || !rich) return false;
	result = {};
	result[size_t(dimension::tax_level)] = *middle;
	result[size_t(dimension::progressivity)] = *middle > tolerance ? *rich / *middle - 1.0f : 0.0f;
	for(auto const& entry : law::appropriations(state, scope, date)) {
		if(territory_of(state, entry.institution)) { result[size_t(dimension::local_government)] += entry.share; continue; }
		auto service = service_kind(state.world.institution_get_service(entry.institution));
		if(service == service_kind::education) result[size_t(dimension::education)] = entry.share;
		else if(service == service_kind::policing) result[size_t(dimension::policing)] = entry.share;
		else if(service == service_kind::construction) result[size_t(dimension::public_works)] = entry.share;
	}
	result = clamp(result);
	return true;
}

bool law_matches(sys::state const& state, dcon::nation_id nation, position const& value, sys::date date) {
	auto wanted = rules_for(state, nation, value);
	auto scope = national(nation);
	for(uint8_t bucket = 0; bucket < 3; ++bucket) {
		auto rate = law::effective_amount(state, scope, law::policy_rule_kind::income_tax_rate, date, bucket);
		if(!rate || std::abs(*rate - wanted.tax_rates[bucket]) > tolerance) return false;
	}
	auto disbursement = law::effective_amount(state, scope, law::policy_rule_kind::disbursement_rate, date);
	if(!disbursement || std::abs(*disbursement - disbursement_rate) > tolerance) return false;
	auto shares = law::appropriations(state, scope, date);
	if(shares.size() != wanted.shares.size()) return false;
	for(auto const& [institution, share] : wanted.shares) {
		bool found = false;
		for(auto const& entry : shares)
			if(entry.institution == institution && std::abs(entry.share - share) <= tolerance) found = true;
		if(!found) return false;
	}
	return true;
}

dcon::person_id fiscal_regulator(sys::state const& state, dcon::nation_id nation, sys::date date) {
	dcon::person_id fallback{};
	for(auto institution : institutions_of(state, nation))
		for(auto office : offices_of(state, institution)) {
			auto person = offices::holder(state, office);
			if(!person || !offices::tenure_of(state, person, office, date)
				|| !has_authority(state, office, authority_kind::regulate, national(nation), date)) continue;
			if(state.world.office_get_kind(office) == uint8_t(office_kind::finance_minister)) return person;
			if(!fallback) fallback = person;
		}
	return fallback;
}

dcon::legal_instrument_id enact(sys::state& state, dcon::nation_id nation, dcon::person_id person, position const& value, sys::date date) {
	auto rules = rules_for(state, nation, value);
	auto instrument = law::create_draft_instrument(state, law::legal_instrument_kind::regulation, nation);
	if(!instrument) return {};
	bool ok = true;
	for(uint8_t bucket = 0; bucket < 3; ++bucket)
		ok = ok && law::add_rule(state, instrument, { law::policy_rule_kind::income_tax_rate, {}, rules.tax_rates[bucket], bucket });
	ok = ok && law::add_rule(state, instrument, { law::policy_rule_kind::disbursement_rate, {}, disbursement_rate });
	for(auto const& [institution, share] : rules.shares)
		ok = ok && law::add_rule(state, instrument, { law::policy_rule_kind::appropriation_share, {}, share, 0, institution });
	std::vector<dcon::legal_instrument_id> superseded;
	for(auto existing : law::effective_instruments(state, national(nation), date))
		if(existing != instrument && fiscal_instrument(state, existing)
			&& state.world.legal_instrument_get_kind(existing) == uint8_t(law::legal_instrument_kind::regulation)) superseded.push_back(existing);
	if(!ok || !law::authorized_enact(state, person, instrument, date, date)) {
		// A refused draft leaves nothing behind.
		std::vector<dcon::policy_rule_id> drafted;
		state.world.legal_instrument_for_each_legal_instrument_policy_rule_as_legal_instrument(instrument, [&](auto relation) {
			drafted.push_back(state.world.legal_instrument_policy_rule_get_policy_rule(relation));
		});
		state.world.delete_legal_instrument(instrument);
		for(auto rule : drafted) state.world.delete_policy_rule(rule);
		return {};
	}
	for(auto existing : superseded) (void)law::authorized_repeal(state, person, existing, date);
	return instrument;
}

} // namespace governance::policy
