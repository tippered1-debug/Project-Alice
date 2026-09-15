#include "law.hpp"

#include "governance/governance.hpp"
#include "persons/persons.hpp"
#include "system_state.hpp"

#include <cmath>

namespace governance::law {

namespace {
bool finite_nonnegative(float value) { return std::isfinite(value) && value >= 0.0f; }

authority_kind required_authority(sys::state const& state, dcon::legal_instrument_id instrument) {
	return state.world.legal_instrument_get_kind(instrument) == uint8_t(legal_instrument_kind::statute)
		? authority_kind::legislate : authority_kind::regulate;
}

dcon::nation_id nation_scope(sys::state const& state, dcon::legal_instrument_id instrument) {
	return state.world.legal_instrument_get_nation_from_legal_instrument_nation_jurisdiction(instrument);
}

dcon::territorial_unit_id territorial_scope(sys::state const& state, dcon::legal_instrument_id instrument) {
	return state.world.legal_instrument_get_territorial_unit_from_legal_instrument_territorial_jurisdiction(instrument);
}

dcon::legal_instrument_id instrument_for_rule(sys::state const& state, dcon::policy_rule_id rule) {
	dcon::legal_instrument_id result{};
	state.world.policy_rule_for_each_legal_instrument_policy_rule_as_policy_rule(rule,
		[&](dcon::legal_instrument_policy_rule_id relation) { result = state.world.legal_instrument_policy_rule_get_legal_instrument(relation); });
	return result;
}

bool is_draft(sys::state const& state, dcon::legal_instrument_id instrument) {
	return instrument && state.world.legal_instrument_is_valid(instrument)
		&& state.world.legal_instrument_get_status(instrument) == uint8_t(legal_status::draft);
}

bool has_rule(sys::state const& state, dcon::legal_instrument_id instrument, policy_rule_kind kind, dcon::commodity_id settlement) {
	for(auto rule : state.world.in_policy_rule)
		if(instrument_for_rule(state, rule) == instrument
			&& state.world.policy_rule_get_kind(rule) == uint8_t(kind)
			&& state.world.policy_rule_get_settlement(rule) == settlement) return true;
	return false;
}

bool interval_overlap(sys::date first_start, std::optional<sys::date> first_end,
	sys::date second_start, std::optional<sys::date> second_end) {
	return (!first_end || second_start < *first_end) && (!second_end || first_start < *second_end);
}

bool same_rule_conflict(sys::state const& state, dcon::legal_instrument_id candidate,
	policy_rule_kind kind, dcon::commodity_id settlement, sys::date effective_from) {
	auto nation = nation_scope(state, candidate);
	auto check = [&](dcon::legal_instrument_id instrument) {
		if(instrument == candidate || !state.world.legal_instrument_is_valid(instrument)
			|| state.world.legal_instrument_get_status(instrument) == uint8_t(legal_status::draft)) return false;
		auto existing_start = state.world.legal_instrument_get_effective_from(instrument);
		std::optional<sys::date> existing_end;
		if(state.world.legal_instrument_get_status(instrument) == uint8_t(legal_status::repealed))
			existing_end = state.world.legal_instrument_get_repealed_on(instrument);
		if(!interval_overlap(effective_from, std::nullopt, existing_start, existing_end)) return false;
		bool conflict = false;
	for(auto rule : state.world.in_policy_rule) {
		if(instrument_for_rule(state, rule) != instrument) continue;
			if(rule && state.world.policy_rule_get_kind(rule) == uint8_t(kind)
				&& state.world.policy_rule_get_settlement(rule) == settlement) conflict = true;
	}
		return conflict;
	};
	bool result = false;
	if(nation) state.world.nation_for_each_legal_instrument_nation_jurisdiction_as_nation(nation,
		[&](dcon::legal_instrument_nation_jurisdiction_id relation) { result = result || check(state.world.legal_instrument_nation_jurisdiction_get_legal_instrument(relation)); });
	else {
		auto territorial = territorial_scope(state, candidate);
		state.world.territorial_unit_for_each_legal_instrument_territorial_jurisdiction_as_territorial_unit(territorial,
			[&](dcon::legal_instrument_territorial_jurisdiction_id relation) { result = result || check(state.world.legal_instrument_territorial_jurisdiction_get_legal_instrument(relation)); });
	}
	return result;
}

dcon::legal_action_id record_action(sys::state& state, legal_action_kind kind, dcon::person_id person,
	dcon::office_id office, dcon::institution_id institution, dcon::legal_instrument_id instrument, sys::date date) {
	auto action = state.world.create_legal_action();
	state.world.legal_action_set_kind(action, uint8_t(kind));
	state.world.legal_action_set_occurred_on(action, date);
	state.world.force_create_legal_action_initiator(action, person);
	state.world.force_create_legal_action_authorizing_office(action, office);
	state.world.force_create_legal_action_issuing_institution(action, institution);
	state.world.force_create_legal_action_instrument(action, instrument);
	return action;
}
}

dcon::legal_instrument_id create_draft_instrument(sys::state& state, legal_instrument_kind kind,
	dcon::nation_id nation, dcon::territorial_unit_id territorial_unit) {
	if((nation && territorial_unit) || (!nation && !territorial_unit)
		|| (nation && !state.world.nation_is_valid(nation))
		|| (territorial_unit && !state.world.territorial_unit_is_valid(territorial_unit))
		|| uint8_t(kind) > uint8_t(legal_instrument_kind::regulation)) return {};
	auto instrument = state.world.create_legal_instrument();
	state.world.legal_instrument_set_kind(instrument, uint8_t(kind));
	state.world.legal_instrument_set_status(instrument, uint8_t(legal_status::draft));
	if(nation) state.world.force_create_legal_instrument_nation_jurisdiction(instrument, nation);
	else state.world.force_create_legal_instrument_territorial_jurisdiction(instrument, territorial_unit);
	return instrument;
}

bool add_public_debt_ceiling_rule(sys::state& state, dcon::legal_instrument_id instrument,
	dcon::commodity_id settlement, float amount) {
	if(!is_draft(state, instrument) || !settlement || !state.world.commodity_is_valid(settlement) || !finite_nonnegative(amount)
		|| has_rule(state, instrument, policy_rule_kind::public_debt_ceiling, settlement)) return false;
	auto rule = state.world.create_policy_rule();
	state.world.policy_rule_set_kind(rule, uint8_t(policy_rule_kind::public_debt_ceiling));
	state.world.policy_rule_set_settlement(rule, settlement);
	state.world.policy_rule_set_amount(rule, amount);
	state.world.force_create_legal_instrument_policy_rule(instrument, rule);
	return true;
}

bool add_public_debt_prohibition_rule(sys::state& state, dcon::legal_instrument_id instrument, dcon::commodity_id settlement) {
	if(!is_draft(state, instrument) || !settlement || !state.world.commodity_is_valid(settlement)
		|| has_rule(state, instrument, policy_rule_kind::public_debt_issuance_prohibited, settlement)) return false;
	auto rule = state.world.create_policy_rule();
	state.world.policy_rule_set_kind(rule, uint8_t(policy_rule_kind::public_debt_issuance_prohibited));
	state.world.policy_rule_set_settlement(rule, settlement);
	state.world.policy_rule_set_amount(rule, 0.0f);
	state.world.force_create_legal_instrument_policy_rule(instrument, rule);
	return true;
}

bool instrument_is_effective(sys::state const& state, dcon::legal_instrument_id instrument, sys::date date) {
	if(!instrument || !state.world.legal_instrument_is_valid(instrument)
		|| state.world.legal_instrument_get_status(instrument) == uint8_t(legal_status::draft)) return false;
	auto enacted = state.world.legal_instrument_get_enacted_on(instrument);
	auto effective = state.world.legal_instrument_get_effective_from(instrument);
	if(date < enacted || date < effective) return false;
	auto repealed = state.world.legal_instrument_get_repealed_on(instrument);
	return state.world.legal_instrument_get_status(instrument) != uint8_t(legal_status::repealed) || date < repealed;
}

dcon::legal_action_id authorized_enact(sys::state& state, dcon::person_id initiator,
	dcon::legal_instrument_id instrument, sys::date enacted_on, sys::date effective_from) {
	if(!is_draft(state, instrument) || effective_from < enacted_on || !initiator
		|| !state.world.person_is_valid(initiator) || !state.world.person_get_alive(initiator)) return {};
	auto nation = nation_scope(state, instrument);
	auto territorial = territorial_scope(state, instrument);
	dcon::office_tenure_id tenure{};
	if(nation) tenure = persons::authority_tenure_on_or_before(state, initiator, required_authority(state, instrument), nation, enacted_on);
	else {
		for(auto office : persons::active_offices_of(state, initiator)) {
			auto candidate = persons::active_tenure_for(state, office);
			if(candidate && state.world.office_tenure_get_started_on(candidate) <= enacted_on
				&& governance::has_authority(state, office, required_authority(state, instrument), territorial)) { tenure = candidate; break; }
		}
	}
	if(!tenure) return {};
	auto office = state.world.office_tenure_get_office_from_office_tenure_office(tenure);
	auto institution = governance::institution_for_office(state, office);
	if(!institution) return {};
	bool has_rule = false;
	bool valid_rules = true;
	for(auto first : state.world.in_policy_rule) {
		if(instrument_for_rule(state, first) != instrument) continue;
		for(auto second : state.world.in_policy_rule) {
			if(first != second && instrument_for_rule(state, second) == instrument
				&& state.world.policy_rule_get_kind(first) == state.world.policy_rule_get_kind(second)
				&& state.world.policy_rule_get_settlement(first) == state.world.policy_rule_get_settlement(second)) valid_rules = false;
		}
	}
	for(auto relation : state.world.in_policy_rule) {
		if(instrument_for_rule(state, relation) != instrument) continue;
		{
			has_rule = true;
			auto rule = relation;
			if(!rule || !state.world.policy_rule_get_settlement(rule) || !state.world.commodity_is_valid(state.world.policy_rule_get_settlement(rule))
				|| !finite_nonnegative(state.world.policy_rule_get_amount(rule))) valid_rules = false;
			if(rule && (state.world.policy_rule_get_kind(rule) == uint8_t(policy_rule_kind::public_debt_ceiling)
				|| state.world.policy_rule_get_kind(rule) == uint8_t(policy_rule_kind::public_debt_issuance_prohibited))
				&& same_rule_conflict(state, instrument, policy_rule_kind(state.world.policy_rule_get_kind(rule)), state.world.policy_rule_get_settlement(rule), effective_from)) valid_rules = false;
		}
	}
	if(!has_rule || !valid_rules) return {};
	state.world.legal_instrument_set_enacted_on(instrument, enacted_on);
	state.world.legal_instrument_set_effective_from(instrument, effective_from);
	state.world.legal_instrument_set_status(instrument, uint8_t(legal_status::enacted));
	state.world.force_create_legal_instrument_enactor(instrument, initiator);
	state.world.force_create_legal_instrument_authorizing_office(instrument, office);
	state.world.force_create_legal_instrument_issuing_institution(instrument, institution);
	return record_action(state, legal_action_kind::enactment, initiator, office, institution, instrument, enacted_on);
}

dcon::legal_action_id authorized_repeal(sys::state& state, dcon::person_id initiator,
	dcon::legal_instrument_id instrument, sys::date date) {
	if(!instrument || !state.world.legal_instrument_is_valid(instrument)
		|| state.world.legal_instrument_get_status(instrument) != uint8_t(legal_status::enacted)
		|| date < state.world.legal_instrument_get_enacted_on(instrument)
		|| date < state.world.legal_instrument_get_effective_from(instrument)
		|| !initiator || !state.world.person_is_valid(initiator) || !state.world.person_get_alive(initiator)) return {};
	auto office = state.world.legal_instrument_get_office_from_legal_instrument_authorizing_office(instrument);
	auto institution = state.world.legal_instrument_get_institution_from_legal_instrument_issuing_institution(instrument);
	auto nation = nation_scope(state, instrument);
	auto territorial = territorial_scope(state, instrument);
	dcon::office_tenure_id tenure{};
	if(nation) tenure = persons::authority_tenure_on_or_before(state, initiator, required_authority(state, instrument), nation, date);
	else {
		for(auto current : persons::active_offices_of(state, initiator)) {
			auto candidate = persons::active_tenure_for(state, current);
			if(candidate && state.world.office_tenure_get_started_on(candidate) <= date
				&& governance::has_authority(state, current, required_authority(state, instrument), territorial)) { tenure = candidate; break; }
		}
	}
	if(!tenure) return {};
	auto actual_office = state.world.office_tenure_get_office_from_office_tenure_office(tenure);
	auto actual_institution = governance::institution_for_office(state, actual_office);
	if(!actual_office || !actual_institution) return {};
	state.world.legal_instrument_set_status(instrument, uint8_t(legal_status::repealed));
	state.world.legal_instrument_set_repealed_on(instrument, date);
	return record_action(state, legal_action_kind::repeal, initiator, actual_office, actual_institution, instrument, date);
}

public_debt_policy public_debt_policy_for(sys::state const& state, dcon::nation_id nation,
	dcon::commodity_id settlement, sys::date date) {
	public_debt_policy result{};
	if(!nation || !settlement) return result;
	state.world.nation_for_each_legal_instrument_nation_jurisdiction_as_nation(nation, [&](dcon::legal_instrument_nation_jurisdiction_id jurisdiction) {
		auto instrument = state.world.legal_instrument_nation_jurisdiction_get_legal_instrument(jurisdiction);
		if(!instrument_is_effective(state, instrument, date)) return;
		for(auto relation : state.world.in_policy_rule) {
			if(instrument_for_rule(state, relation) != instrument) continue;
			{
				auto rule = relation;
				if(rule && state.world.policy_rule_get_settlement(rule) == settlement) {
					if(state.world.policy_rule_get_kind(rule) == uint8_t(policy_rule_kind::public_debt_issuance_prohibited)) result.issuance_allowed = false;
					if(state.world.policy_rule_get_kind(rule) == uint8_t(policy_rule_kind::public_debt_ceiling)) result.ceiling = state.world.policy_rule_get_amount(rule);
				}
			}
		}
	});
	return result;
}

} // namespace governance::law
