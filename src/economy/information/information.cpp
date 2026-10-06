#include "information.hpp"

#include "system_state.hpp"

#include <cmath>

namespace economy::information {

namespace {
bool finite_unit(float value) { return std::isfinite(value) && value >= 0.0f && value <= 1.0f; }
bool valid_kind(information_fact_kind kind) {
	return uint8_t(kind) <= uint8_t(information_fact_kind::national_economic_power);
}

bool report_has_belief(sys::state const& state, dcon::information_report_id report) {
	bool result = false;
	state.world.information_report_for_each_belief_source_as_information_report(report,
		[&](dcon::belief_source_id relation) {
			result = result || bool(state.world.belief_source_get_belief(relation));
		});
	return result;
}
}

dcon::information_report_id publish_report(sys::state& state, information_fact_kind kind,
	dcon::economic_actor_id source, dcon::economic_actor_id recipient, dcon::nation_id subject,
	dcon::commodity_id settlement, float reported_value, sys::date fact_date,
	sys::date published_on, sys::date received_on, float confidence) {
	if(!valid_kind(kind) || !source || !recipient || !state.world.economic_actor_is_valid(source) || !state.world.economic_actor_is_valid(recipient) || !subject || !state.world.nation_is_valid(subject) || !settlement || !state.world.commodity_is_valid(settlement) || !std::isfinite(reported_value) || !finite_unit(confidence) || fact_date > published_on || published_on > received_on) return {};
	auto report = state.world.create_information_report();
	state.world.information_report_set_fact_kind(report, uint8_t(kind));
	state.world.information_report_set_reported_value(report, reported_value);
	state.world.information_report_set_fact_date(report, fact_date);
	state.world.information_report_set_published_on(report, published_on);
	state.world.information_report_set_received_on(report, received_on);
	state.world.information_report_set_confidence(report, confidence);
	state.world.force_create_information_report_source(report, source);
	state.world.force_create_information_report_recipient(report, recipient);
	state.world.force_create_information_report_subject(report, subject);
	state.world.force_create_information_report_settlement(report, settlement);
	return report;
}

dcon::belief_id adopt_report_as_belief(sys::state& state, dcon::economic_actor_id holder,
	dcon::information_report_id report, sys::date date) {
	if(!holder || !report || !state.world.economic_actor_is_valid(holder)
		|| !state.world.information_report_is_valid(report)
		|| !valid_kind(information_fact_kind(state.world.information_report_get_fact_kind(report)))
		|| state.world.information_report_get_economic_actor_from_information_report_recipient(report) != holder
		|| state.world.information_report_get_received_on(report) > date) return {};
	auto belief = state.world.create_belief();
	state.world.belief_set_fact_kind(belief, state.world.information_report_get_fact_kind(report));
	state.world.belief_set_estimated_value(belief, state.world.information_report_get_reported_value(report));
	state.world.belief_set_confidence(belief, state.world.information_report_get_confidence(report));
	state.world.belief_set_formed_on(belief, date);
	state.world.force_create_belief_holder(belief, holder);
	state.world.force_create_belief_subject(belief, state.world.information_report_get_nation_from_information_report_subject(report));
	state.world.force_create_belief_settlement(belief, state.world.information_report_get_commodity_from_information_report_settlement(report));
	state.world.force_create_belief_source(belief, report);
	return belief;
}

std::optional<dcon::belief_id> latest_belief(sys::state const& state, dcon::economic_actor_id holder,
	information_fact_kind kind, dcon::nation_id subject, dcon::commodity_id settlement, sys::date date) {
	if(!holder || !subject || !settlement || !state.world.economic_actor_is_valid(holder)) return std::nullopt;
	std::optional<dcon::belief_id> result;
	sys::date latest{};
	state.world.economic_actor_for_each_belief_holder_as_economic_actor(holder, [&](dcon::belief_holder_id relation) {
		auto belief = state.world.belief_holder_get_belief(relation);
		if(!belief || state.world.belief_get_fact_kind(belief) != uint8_t(kind) || state.world.belief_get_nation_from_belief_subject(belief) != subject || state.world.belief_get_commodity_from_belief_settlement(belief) != settlement || state.world.belief_get_formed_on(belief) > date) return;
		if(!result || state.world.belief_get_formed_on(belief) > latest || state.world.belief_get_formed_on(belief) == latest && belief.index() > result->index()) {
			result = belief; latest = state.world.belief_get_formed_on(belief);
		}
	});
	return result;
}

uint32_t process_received_reports(sys::state& state, sys::date date) {
	if(!date) return 0;
	std::vector<dcon::information_report_id> ready;
	state.world.for_each_information_report([&](dcon::information_report_id report) {
		if(!report_has_belief(state, report)
			&& state.world.information_report_get_received_on(report) <= date)
			ready.push_back(report);
	});
	uint32_t adopted = 0;
	for(auto report : ready) {
		auto recipient = state.world.information_report_get_economic_actor_from_information_report_recipient(report);
		if(adopt_report_as_belief(state, recipient, report, date)) ++adopted;
	}
	return adopted;
}

uint32_t expire_old_intelligence_reports(sys::state& state, sys::date date, int32_t retention_days) {
	if(!date || retention_days < 0) return 0;
	auto cutoff = date - retention_days;
	std::vector<dcon::information_report_id> expired;
	state.world.for_each_information_report([&](dcon::information_report_id report) {
		auto kind = state.world.information_report_get_fact_kind(report);
		if(kind != uint8_t(information_fact_kind::national_military_power)
			&& kind != uint8_t(information_fact_kind::national_economic_power)) return;
		if(state.world.information_report_get_received_on(report) < cutoff) expired.push_back(report);
	});
	for(auto report : expired) {
		std::vector<dcon::belief_id> beliefs;
		state.world.information_report_for_each_belief_source_as_information_report(report,
			[&](dcon::belief_source_id relation) {
				auto belief = state.world.belief_source_get_belief(relation);
				if(belief) beliefs.push_back(belief);
			});
		for(auto belief : beliefs) state.world.delete_belief(belief);
		state.world.delete_information_report(report);
	}
	return uint32_t(expired.size());
}

} // namespace economy::information
