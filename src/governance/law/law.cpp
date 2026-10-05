#include "law.hpp"

#include "governance/governance.hpp"
#include "governance/legislature.hpp"
#include "governance/offices.hpp"
#include "persons/persons.hpp"
#include "system_state.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

namespace governance::law {
namespace {

bool finite_nonnegative(float value) { return std::isfinite(value) && value >= 0.0f; }

legal_instrument_kind kind_of(sys::state const& state, dcon::legal_instrument_id instrument) {
	return legal_instrument_kind(state.world.legal_instrument_get_kind(instrument));
}

std::vector<dcon::policy_rule_id> rules_of(sys::state const& state, dcon::legal_instrument_id instrument) {
	std::vector<dcon::policy_rule_id> result;
	if(!instrument) return result;
	state.world.legal_instrument_for_each_legal_instrument_policy_rule_as_legal_instrument(instrument, [&](auto relation) {
		result.push_back(state.world.legal_instrument_policy_rule_get_policy_rule(relation));
	});
	return result;
}

bool is_draft(sys::state const& state, dcon::legal_instrument_id instrument) {
	return instrument && state.world.legal_instrument_is_valid(instrument)
		&& state.world.legal_instrument_get_status(instrument) == uint8_t(legal_status::draft);
}

bool same_rule(sys::state const& state, dcon::policy_rule_id existing, rule const& candidate) {
	if(state.world.policy_rule_get_kind(existing) != uint8_t(candidate.kind)) return false;
	if(candidate.kind == policy_rule_kind::substantive_policy)
		return state.world.policy_rule_get_topic_id(existing) == uint16_t(candidate.topic);
	return state.world.policy_rule_get_kind(existing) == uint8_t(candidate.kind)
		&& state.world.policy_rule_get_settlement(existing) == candidate.settlement
		&& state.world.policy_rule_get_parameter(existing) == candidate.parameter
		&& state.world.policy_rule_get_institution_from_policy_rule_target_institution(existing) == candidate.target;
}

rule read_rule(sys::state const& state, dcon::policy_rule_id id) {
	rule result{};
	result.kind = policy_rule_kind(state.world.policy_rule_get_kind(id));
	result.settlement = state.world.policy_rule_get_settlement(id);
	result.amount = state.world.policy_rule_get_amount(id);
	result.parameter = state.world.policy_rule_get_parameter(id);
	result.target = state.world.policy_rule_get_institution_from_policy_rule_target_institution(id);
	if(result.kind == policy_rule_kind::substantive_policy) {
		result.topic = policy::topic_id(state.world.policy_rule_get_topic_id(id));
		auto spec = policy::definition(result.topic);
		if(spec) switch(spec->kind) {
		case policy::value_kind::continuous: result.topic_value = state.world.policy_rule_get_amount(id); break;
		case policy::value_kind::ordinal: result.topic_value = int32_t(state.world.policy_rule_get_amount(id)); break;
		case policy::value_kind::categorical: result.topic_value = policy::category_value{state.world.policy_rule_get_category_value(id)}; break;
		case policy::value_kind::binary: result.topic_value = state.world.policy_rule_get_boolean_value(id) != 0; break;
		case policy::value_kind::structured: result.topic_value = policy::structured_value{state.world.policy_rule_get_amount(id), state.world.policy_rule_get_auxiliary(id)}; break;
		}
	}
	return result;
}

bool valid_rule(sys::state const& state, dcon::legal_instrument_id instrument, rule const& value) {
	auto settlement_ok = value.settlement && state.world.commodity_is_valid(value.settlement);
	switch(value.kind) {
	case policy_rule_kind::public_debt_ceiling: return settlement_ok && finite_nonnegative(value.amount);
	case policy_rule_kind::public_debt_issuance_prohibited: return settlement_ok;
	case policy_rule_kind::income_tax_rate:
		return value.parameter <= 2 && std::isfinite(value.amount) && value.amount >= 0.0f && value.amount <= 1.0f;
	case policy_rule_kind::appropriation_share: {
		auto scope = jurisdiction_of(state, instrument);
		auto nation = scope.nation ? scope.nation : governance::nation_of(state, scope.territory);
		return value.target && state.world.institution_is_valid(value.target)
			&& governance::nation_of(state, value.target) == nation && finite_nonnegative(value.amount);
	}
	case policy_rule_kind::inflation_target:
		return std::isfinite(value.amount) && value.amount >= -0.1f && value.amount <= 1.0f;
	case policy_rule_kind::disbursement_rate:
		return std::isfinite(value.amount) && value.amount > 0.0f && value.amount <= 1.0f;
	case policy_rule_kind::substantive_policy: {
		auto scope = jurisdiction_of(state, instrument);
		auto spec = policy::definition(value.topic);
		if(!spec || spec->jurisdiction != policy::jurisdiction_kind::national || !scope.nation) return false;
		return policy::valid(value.topic, value.topic_value)
			&& (!value.target || state.world.institution_is_valid(value.target)
				&& governance::nation_of(state, value.target) == scope.nation);
	}
	}
	return false;
}

bool interval_overlap(sys::date first_start, std::optional<sys::date> first_end,
	sys::date second_start, std::optional<sys::date> second_end) {
	return (!first_end || second_start < *first_end) && (!second_end || first_start < *second_end);
}

std::vector<dcon::legal_instrument_id> instruments_in(sys::state const& state, jurisdiction scope) {
	std::vector<dcon::legal_instrument_id> result;
	if(scope.nation)
		state.world.nation_for_each_legal_instrument_nation_jurisdiction_as_nation(scope.nation, [&](auto relation) {
			result.push_back(state.world.legal_instrument_nation_jurisdiction_get_legal_instrument(relation));
		});
	else if(scope.territory)
		state.world.territorial_unit_for_each_legal_instrument_territorial_jurisdiction_as_territorial_unit(scope.territory, [&](auto relation) {
			result.push_back(state.world.legal_instrument_territorial_jurisdiction_get_legal_instrument(relation));
		});
	return result;
}

// A public-debt rule may not overlap an effective rule of the same kind and
// settlement in another instrument of the same jurisdiction.
bool debt_rule_conflict(sys::state const& state, dcon::legal_instrument_id candidate, rule const& value, sys::date effective_from) {
	if(value.kind != policy_rule_kind::public_debt_ceiling && value.kind != policy_rule_kind::public_debt_issuance_prohibited) return false;
	for(auto instrument : instruments_in(state, jurisdiction_of(state, candidate))) {
		if(instrument == candidate || !state.world.legal_instrument_is_valid(instrument)
			|| state.world.legal_instrument_get_status(instrument) == uint8_t(legal_status::draft)) continue;
		std::optional<sys::date> existing_end;
		if(state.world.legal_instrument_get_status(instrument) == uint8_t(legal_status::repealed))
			existing_end = state.world.legal_instrument_get_repealed_on(instrument);
		if(!interval_overlap(effective_from, std::nullopt, state.world.legal_instrument_get_effective_from(instrument), existing_end)) continue;
		for(auto existing : rules_of(state, instrument))
			if(same_rule(state, existing, value)) return true;
	}
	return false;
}

dcon::legal_action_id record_action(sys::state& state, legal_action_kind kind, dcon::person_id person,
	dcon::office_id office, dcon::institution_id institution, dcon::legal_instrument_id instrument, sys::date date) {
	auto action = state.world.create_legal_action();
	state.world.legal_action_set_kind(action, uint8_t(kind));
	state.world.legal_action_set_occurred_on(action, date);
	if(person) state.world.force_create_legal_action_initiator(action, person);
	if(office) state.world.force_create_legal_action_authorizing_office(action, office);
	if(institution) state.world.force_create_legal_action_issuing_institution(action, institution);
	state.world.force_create_legal_action_instrument(action, instrument);
	return action;
}

struct authorization {
	dcon::office_id office{};
	dcon::institution_id institution{};
	explicit operator bool() const noexcept { return bool(office) && bool(institution); }
};

bool assent_required(sys::state const& state, jurisdiction scope, sys::date date) {
	auto nation = scope.nation ? scope.nation : governance::nation_of(state, scope.territory);
	for(auto institution : institutions_of(state, nation))
		for(auto office : offices_of(state, institution))
			if(has_authority(state, office, authority_kind::assent, scope, date)) return true;
	return false;
}

bool assent_given(sys::state const& state, dcon::legal_instrument_id instrument, sys::date date) {
	bool result = false;
	state.world.legal_instrument_for_each_legal_action_instrument_as_legal_instrument(instrument, [&](auto relation) {
		auto action = state.world.legal_action_instrument_get_legal_action(relation);
		if(state.world.legal_action_get_kind(action) == uint8_t(legal_action_kind::assent)
			&& !(date < state.world.legal_action_get_occurred_on(action))) result = true;
	});
	return result;
}

// Who may act on the instrument on `date`, per the constitution.
authorization authorize(sys::state const& state, dcon::person_id person, dcon::legal_instrument_id instrument,
	legislature::motion_kind motion, sys::date date) {
	if(!person || !state.world.person_is_valid(person) || !persons::alive(state, person)) return {};
	auto kind = kind_of(state, instrument);
	auto power = required_authority(kind);
	auto scope = jurisdiction_of(state, instrument);
	auto threshold = kind == legal_instrument_kind::constitution ? legislature::constitutional_majority : legislature::simple_majority;
	auto chambers = legislature::deciding_chambers(state, power, scope, date);
	if(!chambers.empty()) {
		if(!legislature::instrument_passed(state, instrument, motion, power, scope, date, threshold)) return {};
		if(motion == legislature::motion_kind::enact && assent_required(state, scope, date) && !assent_given(state, instrument, date)) return {};
		for(auto chamber : chambers)
			for(auto office : offices_of(state, chamber))
				if(offices::tenure_of(state, person, office, date)) {
					auto legislature_body = parent_of(state, chamber);
					return { office, legislature_body ? legislature_body : chamber };
				}
		return {};
	}
	auto exercised = offices::exercising(state, person, power, scope, date);
	if(!exercised) return {};
	return { exercised.office, institution_for_office(state, exercised.office) };
}

} // namespace

authority_kind required_authority(legal_instrument_kind kind) {
	switch(kind) {
	case legal_instrument_kind::constitution: return authority_kind::amend_constitution;
	case legal_instrument_kind::statute: return authority_kind::legislate;
	case legal_instrument_kind::regulation: return authority_kind::regulate;
	case legal_instrument_kind::administrative_action: return authority_kind::administer;
	}
	return authority_kind::legislate;
}

jurisdiction jurisdiction_of(sys::state const& state, dcon::legal_instrument_id instrument) {
	if(!instrument) return {};
	if(auto nation = state.world.legal_instrument_get_nation_from_legal_instrument_nation_jurisdiction(instrument)) return national(nation);
	return local(state.world.legal_instrument_get_territorial_unit_from_legal_instrument_territorial_jurisdiction(instrument));
}

dcon::legal_instrument_id create_draft_instrument(sys::state& state, legal_instrument_kind kind,
	dcon::nation_id nation, dcon::territorial_unit_id territorial_unit) {
	if(bool(nation) == bool(territorial_unit) || (nation && !state.world.nation_is_valid(nation))
		|| (territorial_unit && !state.world.territorial_unit_is_valid(territorial_unit))
		|| uint8_t(kind) > uint8_t(legal_instrument_kind::administrative_action)
		|| (kind == legal_instrument_kind::constitution && !nation)) return {};
	auto instrument = state.world.create_legal_instrument();
	state.world.legal_instrument_set_kind(instrument, uint8_t(kind));
	state.world.legal_instrument_set_status(instrument, uint8_t(legal_status::draft));
	if(nation) state.world.force_create_legal_instrument_nation_jurisdiction(instrument, nation);
	else state.world.force_create_legal_instrument_territorial_jurisdiction(instrument, territorial_unit);
	return instrument;
}

bool add_rule(sys::state& state, dcon::legal_instrument_id instrument, rule const& value) {
	if(!is_draft(state, instrument) || !valid_rule(state, instrument, value)) return false;
	for(auto existing : rules_of(state, instrument))
		if(same_rule(state, existing, value)) return false;
	auto id = state.world.create_policy_rule();
	state.world.policy_rule_set_kind(id, uint8_t(value.kind));
	state.world.policy_rule_set_settlement(id, value.settlement);
	state.world.policy_rule_set_amount(id, value.amount);
	state.world.policy_rule_set_parameter(id, value.parameter);
	if(value.kind == policy_rule_kind::substantive_policy) {
		state.world.policy_rule_set_topic_id(id, uint16_t(value.topic));
		auto spec = policy::definition(value.topic);
		if(spec) {
			state.world.policy_rule_set_value_kind(id, uint8_t(spec->kind));
			switch(spec->kind) {
			case policy::value_kind::continuous: state.world.policy_rule_set_amount(id, std::get<float>(value.topic_value)); break;
			case policy::value_kind::ordinal: state.world.policy_rule_set_amount(id, float(std::get<int32_t>(value.topic_value))); break;
			case policy::value_kind::categorical: state.world.policy_rule_set_category_value(id, std::get<policy::category_value>(value.topic_value).value); break;
			case policy::value_kind::binary: state.world.policy_rule_set_boolean_value(id, std::get<bool>(value.topic_value) ? 1 : 0); break;
			case policy::value_kind::structured: {
				auto parameters = std::get<policy::structured_value>(value.topic_value);
				state.world.policy_rule_set_amount(id, parameters.first);
				state.world.policy_rule_set_auxiliary(id, parameters.second);
				break;
			}
			}
		}
	}
	if(value.target) state.world.force_create_policy_rule_target_institution(id, value.target);
	state.world.force_create_legal_instrument_policy_rule(id, instrument);
	return true;
}

bool add_public_debt_ceiling_rule(sys::state& state, dcon::legal_instrument_id instrument, dcon::commodity_id settlement, float amount) {
	return add_rule(state, instrument, { policy_rule_kind::public_debt_ceiling, settlement, amount });
}

bool add_public_debt_prohibition_rule(sys::state& state, dcon::legal_instrument_id instrument, dcon::commodity_id settlement) {
	return add_rule(state, instrument, { policy_rule_kind::public_debt_issuance_prohibited, settlement, 0.0f });
}

bool add_topic_rule(sys::state& state, dcon::legal_instrument_id instrument, policy::topic_id topic,
	policy::policy_value const& value, dcon::institution_id target) {
	rule result{};
	result.kind = policy_rule_kind::substantive_policy;
	result.topic = topic;
	result.topic_value = value;
	result.target = target;
	return add_rule(state, instrument, result);
}

bool instrument_is_effective(sys::state const& state, dcon::legal_instrument_id instrument, sys::date date) {
	if(!instrument || !state.world.legal_instrument_is_valid(instrument)
		|| state.world.legal_instrument_get_status(instrument) == uint8_t(legal_status::draft)) return false;
	if(date < state.world.legal_instrument_get_enacted_on(instrument) || date < state.world.legal_instrument_get_effective_from(instrument)) return false;
	return state.world.legal_instrument_get_status(instrument) != uint8_t(legal_status::repealed)
		|| date < state.world.legal_instrument_get_repealed_on(instrument);
}

dcon::legal_action_id authorized_enact(sys::state& state, dcon::person_id initiator,
	dcon::legal_instrument_id instrument, sys::date enacted_on, sys::date effective_from) {
	if(!is_draft(state, instrument) || effective_from < enacted_on) return {};
	auto rules = rules_of(state, instrument);
	// Only a constitution may consist of structure alone.
	if(rules.empty() && kind_of(state, instrument) != legal_instrument_kind::constitution) return {};
	for(auto id : rules) {
		auto value = read_rule(state, id);
		if(!valid_rule(state, instrument, value) || debt_rule_conflict(state, instrument, value, effective_from)) return {};
	}
	auto authorized = authorize(state, initiator, instrument, legislature::motion_kind::enact, enacted_on);
	if(!authorized) return {};
	state.world.legal_instrument_set_enacted_on(instrument, enacted_on);
	state.world.legal_instrument_set_effective_from(instrument, effective_from);
	state.world.legal_instrument_set_status(instrument, uint8_t(legal_status::enacted));
	state.world.force_create_legal_instrument_enactor(instrument, initiator);
	state.world.force_create_legal_instrument_authorizing_office(instrument, authorized.office);
	state.world.force_create_legal_instrument_issuing_institution(instrument, authorized.institution);
	return record_action(state, legal_action_kind::enactment, initiator, authorized.office, authorized.institution, instrument, enacted_on);
}

dcon::legal_action_id authorized_repeal(sys::state& state, dcon::person_id initiator,
	dcon::legal_instrument_id instrument, sys::date date) {
	if(!instrument || !state.world.legal_instrument_is_valid(instrument)
		|| state.world.legal_instrument_get_status(instrument) != uint8_t(legal_status::enacted)
		|| date < state.world.legal_instrument_get_enacted_on(instrument)
		|| date < state.world.legal_instrument_get_effective_from(instrument)) return {};
	auto authorized = authorize(state, initiator, instrument, legislature::motion_kind::repeal, date);
	if(!authorized) return {};
	state.world.legal_instrument_set_status(instrument, uint8_t(legal_status::repealed));
	state.world.legal_instrument_set_repealed_on(instrument, date);
	return record_action(state, legal_action_kind::repeal, initiator, authorized.office, authorized.institution, instrument, date);
}

dcon::legal_action_id authorized_assent(sys::state& state, dcon::person_id person, dcon::legal_instrument_id instrument, sys::date date) {
	if(!is_draft(state, instrument)) return {};
	auto exercised = offices::exercising(state, person, authority_kind::assent, jurisdiction_of(state, instrument), date);
	if(!exercised) return {};
	return record_action(state, legal_action_kind::assent, person, exercised.office,
		institution_for_office(state, exercised.office), instrument, date);
}

std::vector<dcon::legal_instrument_id> effective_instruments(sys::state const& state, jurisdiction scope, sys::date date) {
	std::vector<dcon::legal_instrument_id> result;
	for(auto instrument : instruments_in(state, scope))
		if(instrument_is_effective(state, instrument, date)) result.push_back(instrument);
	// Oldest first: later instruments override earlier ones.
	std::sort(result.begin(), result.end(), [&](auto a, auto b) {
		return std::tuple(state.world.legal_instrument_get_effective_from(a).to_raw_value(), state.world.legal_instrument_get_enacted_on(a).to_raw_value(), a.index())
			< std::tuple(state.world.legal_instrument_get_effective_from(b).to_raw_value(), state.world.legal_instrument_get_enacted_on(b).to_raw_value(), b.index());
	});
	return result;
}

std::optional<float> effective_amount(sys::state const& state, jurisdiction scope, policy_rule_kind kind, sys::date date,
	uint8_t parameter, dcon::institution_id target, dcon::commodity_id settlement) {
	std::optional<float> result;
	rule wanted{ kind, settlement, 0.0f, parameter, target };
	for(auto instrument : effective_instruments(state, scope, date))
		for(auto id : rules_of(state, instrument))
			if(same_rule(state, id, wanted)) result = state.world.policy_rule_get_amount(id);
	return result;
}

std::optional<policy::policy_value> effective_topic(sys::state const& state, jurisdiction scope,
	policy::topic_id topic, sys::date date) {
	std::optional<policy::policy_value> result;
	for(auto instrument : effective_instruments(state, scope, date))
		for(auto id : rules_of(state, instrument))
			if(state.world.policy_rule_get_kind(id) == uint8_t(policy_rule_kind::substantive_policy)
				&& state.world.policy_rule_get_topic_id(id) == uint16_t(topic)) {
				auto value = read_rule(state, id);
				result = value.topic_value;
			}
	return result;
}

dcon::institution_id effective_topic_target(sys::state const& state, jurisdiction scope,
	policy::topic_id topic, sys::date date) {
	dcon::institution_id result{};
	for(auto instrument : effective_instruments(state, scope, date))
		for(auto id : rules_of(state, instrument))
			if(state.world.policy_rule_get_kind(id) == uint8_t(policy_rule_kind::substantive_policy)
				&& state.world.policy_rule_get_topic_id(id) == uint16_t(topic))
				result = state.world.policy_rule_get_institution_from_policy_rule_target_institution(id);
	return result;
}

std::vector<appropriation> appropriations(sys::state const& state, jurisdiction scope, sys::date date) {
	std::map<uint32_t, float> shares;
	for(auto instrument : effective_instruments(state, scope, date))
		for(auto id : rules_of(state, instrument))
			if(state.world.policy_rule_get_kind(id) == uint8_t(policy_rule_kind::appropriation_share))
				if(auto target = state.world.policy_rule_get_institution_from_policy_rule_target_institution(id))
					shares[target.index()] = state.world.policy_rule_get_amount(id);
	std::vector<appropriation> result;
	for(auto const& [index, share] : shares)
		if(share > 0.0f) result.push_back({ dcon::institution_id{ dcon::institution_id::value_base_t(index) }, share });
	return result;
}

public_debt_policy public_debt_policy_for(sys::state const& state, dcon::nation_id nation,
	dcon::commodity_id settlement, sys::date date) {
	public_debt_policy result{};
	if(!nation || !settlement) return result;
	for(auto instrument : effective_instruments(state, national(nation), date))
		for(auto id : rules_of(state, instrument)) {
			if(state.world.policy_rule_get_settlement(id) != settlement) continue;
			if(state.world.policy_rule_get_kind(id) == uint8_t(policy_rule_kind::public_debt_issuance_prohibited)) result.issuance_allowed = false;
			if(state.world.policy_rule_get_kind(id) == uint8_t(policy_rule_kind::public_debt_ceiling)) result.ceiling = state.world.policy_rule_get_amount(id);
		}
	return result;
}

dcon::legal_instrument_id found_constitution(sys::state& state, dcon::nation_id nation, dcon::institution_id issuer, sys::date date) {
	if(!nation || !issuer || governance::nation_of(state, issuer) != nation || constitution_of(state, nation)) return {};
	auto instrument = create_draft_instrument(state, legal_instrument_kind::constitution, nation);
	if(!instrument) return {};
	state.world.legal_instrument_set_enacted_on(instrument, date);
	state.world.legal_instrument_set_effective_from(instrument, date);
	state.world.legal_instrument_set_status(instrument, uint8_t(legal_status::enacted));
	state.world.force_create_legal_instrument_issuing_institution(instrument, issuer);
	(void)record_action(state, legal_action_kind::founding, {}, {}, issuer, instrument, date);
	return instrument;
}

dcon::legal_instrument_id constitution_of(sys::state const& state, dcon::nation_id nation) {
	dcon::legal_instrument_id result{};
	for(auto instrument : instruments_in(state, national(nation)))
		if(kind_of(state, instrument) == legal_instrument_kind::constitution
			&& state.world.legal_instrument_get_status(instrument) != uint8_t(legal_status::draft)) result = instrument;
	return result;
}

} // namespace governance::law
