#include "policy_inputs.hpp"

#include "system_state.hpp"
#include "governance/governance.hpp"
#include "governance/law/law.hpp"
#include "governance/offices.hpp"
#include "persons/persons.hpp"

#include <cmath>

namespace governance::policy_inputs {
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

fiscal_inputs read(sys::state const& state, dcon::nation_id nation) {
	fiscal_inputs result{};
	result.tax_rates[0] = float(state.world.nation_get_poor_tax(nation)) / 100.0f;
	result.tax_rates[1] = float(state.world.nation_get_middle_tax(nation)) / 100.0f;
	result.tax_rates[2] = float(state.world.nation_get_rich_tax(nation)) / 100.0f;
	for(auto& rate : result.tax_rates) rate = std::isfinite(rate) ? std::clamp(rate, 0.0f, 1.0f) : 0.0f;
	auto weight = [&](int8_t slider, float base) { return base + std::max(0.0f, float(slider) / 100.0f); };
	std::vector<dcon::institution_id> territorial;
	for(auto institution : institutions_of(state, nation)) {
		if(governance::territory_of(state, institution)) {
			if(state.world.institution_get_staffing_per_capita(institution) > 0.0f) territorial.push_back(institution);
			continue;
		}
		switch(kind_of(state, institution)) {
		case institution_kind::education_ministry: result.shares.push_back({ institution, weight(state.world.nation_get_education_spending(nation), 0.05f) }); break;
		case institution_kind::interior_ministry: result.shares.push_back({ institution, weight(state.world.nation_get_administrative_spending(nation), 0.05f) }); break;
		case institution_kind::public_works_ministry: result.shares.push_back({ institution, weight(state.world.nation_get_construction_spending(nation), 0.05f) }); break;
		default: break;
		}
	}
	if(!territorial.empty()) {
		auto local_share = weight(state.world.nation_get_social_spending(nation), 0.10f) / float(territorial.size());
		for(auto institution : territorial) result.shares.push_back({ institution, local_share });
	}
	return result;
}

bool law_matches(sys::state const& state, dcon::nation_id nation, fiscal_inputs const& inputs, sys::date date) {
	auto scope = national(nation);
	for(uint8_t bucket = 0; bucket < 3; ++bucket) {
		auto rate = law::effective_amount(state, scope, law::policy_rule_kind::income_tax_rate, date, bucket);
		if(!rate || std::abs(*rate - inputs.tax_rates[bucket]) > tolerance) return false;
	}
	auto disbursement = law::effective_amount(state, scope, law::policy_rule_kind::disbursement_rate, date);
	if(!disbursement || std::abs(*disbursement - inputs.disbursement) > tolerance) return false;
	auto shares = law::appropriations(state, scope, date);
	if(shares.size() != inputs.shares.size()) return false;
	for(auto const& [institution, share] : inputs.shares) {
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

dcon::legal_instrument_id enact(sys::state& state, dcon::nation_id nation, dcon::person_id person, fiscal_inputs const& inputs, sys::date date) {
	auto instrument = law::create_draft_instrument(state, law::legal_instrument_kind::regulation, nation);
	if(!instrument) return {};
	bool ok = true;
	for(uint8_t bucket = 0; bucket < 3; ++bucket)
		ok = ok && law::add_rule(state, instrument, { law::policy_rule_kind::income_tax_rate, {}, inputs.tax_rates[bucket], bucket });
	ok = ok && law::add_rule(state, instrument, { law::policy_rule_kind::disbursement_rate, {}, inputs.disbursement });
	for(auto const& [institution, share] : inputs.shares)
		ok = ok && law::add_rule(state, instrument, { law::policy_rule_kind::appropriation_share, {}, share, 0, institution });
	std::vector<dcon::legal_instrument_id> superseded;
	for(auto existing : law::effective_instruments(state, national(nation), date))
		if(existing != instrument && fiscal_instrument(state, existing)
			&& state.world.legal_instrument_get_kind(existing) == uint8_t(law::legal_instrument_kind::regulation)) superseded.push_back(existing);
	if(!ok || !law::authorized_enact(state, person, instrument, date, date)) {
		// A refused draft leaves nothing behind.
		std::vector<dcon::policy_rule_id> rules;
		state.world.legal_instrument_for_each_legal_instrument_policy_rule_as_legal_instrument(instrument, [&](auto relation) {
			rules.push_back(state.world.legal_instrument_policy_rule_get_policy_rule(relation));
		});
		state.world.delete_legal_instrument(instrument);
		for(auto rule : rules) state.world.delete_policy_rule(rule);
		return {};
	}
	for(auto existing : superseded) (void)law::authorized_repeal(state, person, existing, date);
	return instrument;
}

void apply(sys::state& state) {
	auto date = state.current_date;
	state.world.for_each_nation([&](dcon::nation_id nation) {
		if(!law::constitution_of(state, nation)) return;
		auto inputs = read(state, nation);
		if(law_matches(state, nation, inputs, date)) return;
		if(auto person = fiscal_regulator(state, nation, date)) (void)enact(state, nation, person, inputs, date);
	});
}

} // namespace governance::policy_inputs
