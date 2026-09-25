#include "strategic_statecraft.hpp"

#include "system_state.hpp"
#include "military/military.hpp"
#include "provinces/province.hpp"
#include "nations.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace nations::strategic_statecraft {
namespace {

constexpr uint16_t max_memory = std::numeric_limits<uint16_t>::max();
constexpr int32_t recent_war_window_days = 5 * 365;

float unit(float value) {
	return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}

float sat_add(float a, float b) {
	return std::clamp(a + b, 0.0f, 1.0f);
}

void increment(uint16_t& counter) {
	if(counter != max_memory)
		++counter;
}

uint64_t pair_key(dcon::nation_id observer, dcon::nation_id subject) {
	return (uint64_t(observer.index()) << 32) | uint64_t(subject.index());
}

uint32_t day(sys::state const& state) {
	return uint32_t(std::max(state.current_date.value, 0));
}

belief* find_belief(sys::state& state, dcon::nation_id observer, dcon::nation_id subject) {
	if(!state.strategic_statecraft_initialized || !observer || !subject)
		return nullptr;
	auto const key = pair_key(observer, subject);
	auto it = std::lower_bound(state.strategic_beliefs.begin(), state.strategic_beliefs.end(), key,
		[](belief const& item, uint64_t lookup) { return item.key < lookup; });
	return it != state.strategic_beliefs.end() && it->key == key ? &*it : nullptr;
}

belief const* find_belief(sys::state const& state, dcon::nation_id observer, dcon::nation_id subject) {
	if(!state.strategic_statecraft_initialized || !observer || !subject)
		return nullptr;
	auto const key = pair_key(observer, subject);
	auto it = std::lower_bound(state.strategic_beliefs.begin(), state.strategic_beliefs.end(), key,
		[](belief const& item, uint64_t lookup) { return item.key < lookup; });
	return it != state.strategic_beliefs.end() && it->key == key ? &*it : nullptr;
}

interests* profile(sys::state& state, dcon::nation_id nation) {
	if(!uses_model(state, nation))
		return nullptr;
	return &state.strategic_interests[nation.index()];
}

float estimate_military(sys::state const& state, dcon::nation_id nation) {
	if(!nation || !state.world.nation_is_valid(nation))
		return 0.0f;
	return std::max(0.0f, float(state.world.nation_get_military_score(nation)));
}

float estimate_economy(sys::state const& state, dcon::nation_id nation) {
	if(!nation || !state.world.nation_is_valid(nation))
		return 0.0f;
	return std::max(0.0f, float(state.world.nation_get_industrial_score(nation)));
}

bool knows_well(sys::state& state, dcon::nation_id observer, dcon::nation_id subject) {
	if(observer == subject || nations::are_allied(state, observer, subject)
		|| military::are_at_war(state, observer, subject)
		|| state.world.get_nation_adjacency_by_nation_adjacency_pair(observer, subject))
		return true;
	if(state.world.nation_get_ai_rival(observer) == subject
		|| state.world.nation_get_ai_rival(subject) == observer)
		return true;
	return state.world.nation_get_in_sphere_of(subject) == observer
		|| state.world.nation_get_is_great_power(observer)
		|| state.world.nation_get_is_great_power(subject);
}

bool knows_crisis_event(sys::state& state, dcon::nation_id observer, dcon::nation_id actor) {
	if(!observer || !actor)
		return false;
	if(knows_well(state, observer, actor))
		return true;
	for(auto const& participant : state.crisis_participants) {
		if(!participant.id)
			break;
		if(participant.id == observer || participant.id == actor)
			return true;
	}
	return observer == state.primary_crisis_attacker || observer == state.primary_crisis_defender;
}

float observation_quality(sys::state& state, dcon::nation_id observer, dcon::nation_id subject) {
	if(knows_well(state, observer, subject))
		return 0.42f;
	return 0.0f;
}

float relative_share(float subject, float observer) {
	if(subject <= 0.0f && observer <= 0.0f)
		return 0.5f;
	return unit(subject / (subject + observer + 0.01f));
}

void refresh_belief_values(sys::state& state, dcon::nation_id observer,
	dcon::nation_id subject, belief& view) {
	auto const quality = observation_quality(state, observer, subject);
	if(observer == subject) {
		view.military_power = 0.5f;
		view.economic_power = 0.5f;
	} else {
		if(quality > 0.0f) {
			auto const observed_military = relative_share(
				estimate_military(state, subject), estimate_military(state, observer));
			auto const observed_economic = relative_share(
				estimate_economy(state, subject), estimate_economy(state, observer));
			view.military_power += (observed_military - view.military_power) * quality;
			view.economic_power += (observed_economic - view.economic_power) * quality;
		}
		// Stale intelligence drifts toward uncertainty rather than the world's
		// current true values.
		if(quality < 0.1f) {
			view.military_power += (0.5f - view.military_power) * 0.025f;
			view.economic_power += (0.5f - view.economic_power) * 0.025f;
		}
	}
	view.military_power = unit(view.military_power);
	view.economic_power = unit(view.economic_power);
	if(quality > 0.0f)
		view.last_observed_day = int32_t(day(state));
	if(view.last_war_day + recent_war_window_days < int32_t(day(state)))
		view.recent_wars = 0;
	bool const at_war = observer != subject && quality > 0.0f
		&& military::are_at_war(state, observer, subject);
	if(at_war && view.currently_at_war == 0) {
		increment(view.recent_wars);
		view.last_war_day = int32_t(day(state));
	}
	view.currently_at_war = uint8_t(at_war);
	view.reliability = unit(0.5f
		+ 0.065f * float(std::min<uint16_t>(view.fulfilled_commitments, 5))
		- 0.11f * float(std::min<uint16_t>(view.broken_commitments, 4)));
}

void refresh_belief(sys::state& state, dcon::nation_id observer, dcon::nation_id subject) {
	if(auto* view = find_belief(state, observer, subject))
		refresh_belief_values(state, observer, subject, *view);
}

bool holds_core_of(sys::state const& state, dcon::nation_id holder, dcon::nation_id claimant) {
	if(!holder || !claimant)
		return false;
	auto identity = state.world.nation_get_identity_from_identity_holder(claimant);
	if(!identity)
		return false;
	for(auto ownership : state.world.nation_get_province_ownership(holder)) {
		for(auto core : ownership.get_province().get_core_as_province()) {
			if(core.get_identity() == identity)
				return true;
		}
	}
	return false;
}

float claim_conflict(sys::state const& state, dcon::nation_id a, dcon::nation_id b) {
	float conflict = 0.0f;
	if(holds_core_of(state, b, a))
		conflict += 0.65f;
	if(holds_core_of(state, a, b))
		conflict += 0.65f;
	return unit(conflict);
}

float reach(sys::state const& state, dcon::nation_id a, dcon::nation_id b) {
	if(state.world.get_nation_adjacency_by_nation_adjacency_pair(a, b))
		return 1.0f;
	auto pa = state.world.nation_get_capital(a);
	auto pb = state.world.nation_get_capital(b);
	if(!pa || !pb)
		return 0.0f;
	if(state.world.province_get_continent(pa) == state.world.province_get_continent(pb))
		return 0.72f;
	if(state.world.nation_get_total_ports(a) > 0 && state.world.nation_get_total_ports(b) > 0)
		return 0.42f;
	return 0.12f;
}

float common_threat(sys::state& state, dcon::nation_id a, dcon::nation_id b) {
	float value = 0.0f;
	auto ra = state.world.nation_get_ai_rival(a);
	auto rb = state.world.nation_get_ai_rival(b);
	if(ra && ra == rb)
		value += 0.8f;
	if(ra && nations::are_allied(state, b, ra))
		value -= 0.55f;
	if(rb && nations::are_allied(state, a, rb))
		value -= 0.55f;
	return unit(value);
}

float own_claim_interest(sys::state const& state, dcon::nation_id nation, dcon::nation_id holder) {
	auto const* weights = state.strategic_statecraft_initialized
		&& nation.index() < state.strategic_interests.size()
		? &state.strategic_interests[nation.index()] : nullptr;
	if(!weights || !weights->enabled || !holds_core_of(state, holder, nation))
		return 0.0f;
	return 0.65f + 0.35f * unit(weights->territorial_claims);
}

void set_decision(sys::state& state, dcon::nation_id nation, decision_code code, float value) {
	if(auto* weights = profile(state, nation)) {
		weights->last_decision = uint8_t(code);
		weights->last_decision_value = std::clamp(value, -1.0f, 1.0f);
	}
}

void set_objective(sys::state& state, dcon::nation_id nation, objective_kind kind,
	dcon::nation_id target, float value) {
	if(auto* weights = profile(state, nation)) {
		weights->objective = uint8_t(kind);
		weights->objective_target = target ? target.index() : interests::no_nation;
		weights->objective_value = std::clamp(value, -1.0f, 1.0f);
	}
}

float belief_power(sys::state const& state, dcon::nation_id observer, dcon::nation_id subject) {
	if(observer == subject)
		return 1.0f;
	if(auto const* view = find_belief(state, observer, subject)) {
		auto const share = std::clamp(view->military_power, 0.05f, 0.95f);
		return share / (1.0f - share);
	}
	return 1.0f;
}

float coalition_power(sys::state const& state, dcon::nation_id observer, bool attacker) {
	float total = 0.0f;
	if(attacker) {
		total += belief_power(state, observer, state.primary_crisis_attacker);
		if(state.crisis_attacker && state.crisis_attacker != state.primary_crisis_attacker)
			total += belief_power(state, observer, state.crisis_attacker);
	} else {
		total += belief_power(state, observer, state.primary_crisis_defender);
		if(state.crisis_defender && state.crisis_defender != state.primary_crisis_defender)
			total += belief_power(state, observer, state.crisis_defender);
	}
	for(auto const& participant : state.crisis_participants) {
		if(!participant.id)
			break;
		if(!participant.merely_interested && participant.supports_attacker == attacker
			&& participant.id != state.primary_crisis_attacker
			&& participant.id != state.primary_crisis_defender
			&& participant.id != state.crisis_attacker
			&& participant.id != state.crisis_defender)
			total += belief_power(state, observer, participant.id);
	}
	return total;
}

float expected_loss(sys::state const& state, dcon::nation_id observer, bool own_side_attacker) {
	auto const own = coalition_power(state, observer, own_side_attacker);
	auto const opponent = coalition_power(state, observer, !own_side_attacker);
	float const loss_probability = unit(0.5f + 0.42f * (opponent - own) / (own + opponent + 0.1f));
	float const claim_at_stake = own_side_attacker
		? own_claim_interest(state, observer, state.crisis_defender)
		: own_claim_interest(state, observer, state.crisis_attacker);
	float const exhaustion = unit(state.world.nation_get_war_exhaustion(observer) / 100.0f);
	float const readiness = 0.12f + exhaustion * 0.28f
		+ (state.world.nation_get_is_mobilized(observer) ? 0.12f : 0.0f);
	float const national_stake = observer.index() < state.strategic_interests.size()
		? 0.18f * state.strategic_interests[observer.index()].territorial_claims
			+ 0.12f * state.strategic_interests[observer.index()].security
		: 0.0f;
	return loss_probability * (0.3f + 0.45f * claim_at_stake + national_stake + readiness);
}

float distance_interest(sys::state const& state, dcon::nation_id a, dcon::nation_id b) {
	return reach(state, a, b);
}

bool valid_crisis_leaders(sys::state const& state) {
	return state.current_crisis_state == sys::crisis_state::heating_up
		&& state.primary_crisis_attacker && state.primary_crisis_defender;
}

dcon::nation_id crisis_side_actor(sys::state const& state, bool attacker) {
	auto primary = attacker ? state.primary_crisis_attacker : state.primary_crisis_defender;
	if(primary)
		return primary;
	auto actor = attacker ? state.crisis_attacker : state.crisis_defender;
	if(actor)
		return actor;
	if(state.crisis_attacker_wargoals.empty())
		return dcon::nation_id{};
	auto const& first = state.crisis_attacker_wargoals.front();
	if(attacker && first.wg_tag)
		return state.world.national_identity_get_nation_from_identity_holder(first.wg_tag);
	return attacker ? dcon::nation_id{} : first.target_nation;
}

void observe_public_crisis(sys::state& state) {
	if(!valid_crisis_leaders(state))
		return;
	for(auto const& participant : state.crisis_participants) {
		if(!participant.id)
			break;
		for(auto const& other : state.crisis_participants) {
			if(!other.id)
				break;
			if(participant.id != other.id)
				refresh_belief(state, participant.id, other.id);
		}
	}
}

void update_profile_for_new_country(sys::state& state, dcon::nation_id nation) {
	if(!nation || !state.world.nation_is_valid(nation)
		|| nation.index() >= state.strategic_interests.size())
		return;
	auto& weights = state.strategic_interests[nation.index()];
	if(weights.enabled)
		return;
	weights = interests{};
	weights.enabled = 1;
	weights.security = 0.62f;
	weights.territorial_claims = 0.18f;
	weights.market_access = 0.24f;
	weights.resource_access = 0.28f;
	weights.route_access = 0.2f;
	weights.ally_subject_protection = 0.3f;
	weights.prestige_influence = 0.2f;
	if(state.world.nation_get_is_great_power(nation)) {
		weights.security = 0.72f;
		weights.territorial_claims = 0.28f;
		weights.market_access = 0.42f;
		weights.resource_access = 0.4f;
		weights.route_access = 0.45f;
		weights.ally_subject_protection = 0.58f;
		weights.prestige_influence = 0.65f;
	}
	auto capital = state.world.nation_get_capital(nation);
	if(capital && state.world.nation_get_total_ports(nation) > 0) {
		weights.route_access = unit(weights.route_access + 0.12f);
		weights.market_access = unit(weights.market_access + 0.08f);
	}
	if(state.world.nation_get_overlord_as_subject(nation)) {
		weights.ally_subject_protection = unit(weights.ally_subject_protection + 0.22f);
		weights.security = unit(weights.security + 0.1f);
	}
	uint32_t held_cores = 0;
	for(auto other : state.world.in_nation) {
		if(other.id != nation && holds_core_of(state, other.id, nation))
			++held_cores;
	}
	weights.territorial_claims = unit(weights.territorial_claims + 0.1f * float(std::min(held_cores, 3u)));
}

void ensure_belief_rows(sys::state& state) {
	std::size_t subject_count = 0;
	std::size_t observer_count = 0;
	for(auto observer : state.world.in_nation) {
		++subject_count;
		observer_count += uses_model(state, observer.id) ? 1u : 0u;
	}
	auto const expected = observer_count * (subject_count == 0 ? 0 : subject_count - 1);
	if(state.strategic_beliefs.size() == expected)
		return;

	std::vector<belief> rebuilt;
	rebuilt.reserve(expected);
	for(auto observer : state.world.in_nation) {
		if(!uses_model(state, observer.id))
			continue;
		for(auto subject : state.world.in_nation) {
			if(observer.id == subject.id)
				continue;
			auto const key = pair_key(observer.id, subject.id);
			auto const it = std::lower_bound(state.strategic_beliefs.begin(), state.strategic_beliefs.end(), key,
				[](belief const& item, uint64_t lookup) { return item.key < lookup; });
			if(it != state.strategic_beliefs.end() && it->key == key) {
				rebuilt.push_back(*it);
			} else {
				belief fresh;
				fresh.key = key;
				rebuilt.push_back(fresh);
			}
		}
	}
	std::sort(rebuilt.begin(), rebuilt.end(),
		[](belief const& a, belief const& b) { return a.key < b.key; });
	state.strategic_beliefs.swap(rebuilt);
}

float crisis_stake(sys::state const& state, dcon::nation_id nation) {
	if(nation.index() < state.strategic_interests.size()) {
		auto const& weights = state.strategic_interests[nation.index()];
		if(weights.enabled)
			return unit(0.45f * weights.security
				+ 0.35f * weights.territorial_claims
				+ 0.2f * weights.prestige_influence);
	}
	return 0.0f;
}

void raise_resolve(sys::state& state, dcon::nation_id observer, dcon::nation_id subject, float delta) {
	if(state.current_crisis_state != sys::crisis_state::inactive && !knows_well(state, observer, subject)) {
		bool involved = observer == state.primary_crisis_attacker || observer == state.primary_crisis_defender
			|| subject == state.primary_crisis_attacker || subject == state.primary_crisis_defender;
		for(auto const& participant : state.crisis_participants) {
			if(!participant.id)
				break;
			involved = involved || observer == participant.id || subject == participant.id;
		}
		if(!involved)
			return;
	}
	if(auto* view = find_belief(state, observer, subject))
		view->resolve = unit(view->resolve + delta);
}

void resolve_commitment(sys::state& state, dcon::nation_id promisor, dcon::nation_id beneficiary,
	commitment_kind kind, bool fulfilled) {
	for(auto it = state.strategic_commitments.rbegin(); it != state.strategic_commitments.rend(); ++it) {
		if(it->active && it->promisor == promisor.index() && it->beneficiary == beneficiary.index()
			&& it->kind == uint8_t(kind)) {
			it->active = 0;
			it->resolved_day = int32_t(day(state));
			if(auto* view = find_belief(state, beneficiary, promisor)) {
				if(fulfilled) {
					increment(view->fulfilled_commitments);
					view->reliability = sat_add(view->reliability, 0.04f);
				} else {
					increment(view->broken_commitments);
					view->reliability = unit(view->reliability - 0.12f);
				}
				view->last_commitment_day = int32_t(day(state));
			}
			return;
		}
	}
}

void assess_active_commitment(sys::state& state, dcon::nation_id promisor, dcon::nation_id beneficiary,
	commitment_kind kind, bool fulfilled) {
	for(auto it = state.strategic_commitments.rbegin(); it != state.strategic_commitments.rend(); ++it) {
		if(it->active && it->promisor == promisor.index() && it->beneficiary == beneficiary.index()
			&& it->kind == uint8_t(kind)) {
			if(auto* view = find_belief(state, beneficiary, promisor)) {
				if(fulfilled) {
					increment(view->fulfilled_commitments);
					view->reliability = sat_add(view->reliability, 0.04f);
				} else {
					increment(view->broken_commitments);
					view->reliability = unit(view->reliability - 0.12f);
				}
				view->last_commitment_day = int32_t(day(state));
			}
			return;
		}
	}
}

void add_commitment(sys::state& state, dcon::nation_id promisor,
	dcon::nation_id beneficiary, commitment_kind kind) {
	if(!promisor || !beneficiary || promisor == beneficiary)
		return;
	for(auto const& item : state.strategic_commitments) {
		if(item.active && item.promisor == promisor.index()
			&& item.beneficiary == beneficiary.index() && item.kind == uint8_t(kind))
			return;
	}
	state.strategic_commitments.push_back(commitment{
		promisor.index(), beneficiary.index(), uint8_t(kind), 1, 0,
		int32_t(day(state)), 0
	});
}

void clear_temporary_offer_goal(sys::state& state) {
	auto const slot = state.strategic_crisis.temporary_offer_goal_slot;
	if(slot >= 0 && std::size_t(slot) < state.crisis_attacker_wargoals.size()) {
		state.crisis_attacker_wargoals[std::size_t(slot)] = sys::full_wg{};
	} else if(slot <= -2) {
		auto const defender_slot = std::size_t(-int64_t(slot) - 2);
		if(defender_slot < state.crisis_defender_wargoals.size())
			state.crisis_defender_wargoals[defender_slot] = sys::full_wg{};
	}
	state.strategic_crisis.temporary_offer_goal_slot = -1;
	state.strategic_crisis.partial_concession = 0;
}

bool crisis_side_of(sys::state const& state, dcon::nation_id nation, bool& attacker_side) {
	if(!nation)
		return false;
	if(nation == state.primary_crisis_attacker || nation == state.crisis_attacker) {
		attacker_side = true;
		return true;
	}
	if(nation == state.primary_crisis_defender || nation == state.crisis_defender) {
		attacker_side = false;
		return true;
	}
	for(auto const& participant : state.crisis_participants) {
		if(!participant.id)
			break;
		if(participant.id == nation && !participant.merely_interested) {
			attacker_side = participant.supports_attacker;
			return true;
		}
	}
	return false;
}

bool knows_war_event(sys::state& state, dcon::nation_id observer, dcon::nation_id actor,
	dcon::war_id war) {
	if(knows_well(state, observer, actor))
		return true;
	for(auto participant : state.world.war_get_war_participant(war)) {
		if(participant.get_nation() == observer)
			return true;
	}
	return false;
}

} // namespace

bool uses_model(sys::state const& state, dcon::nation_id nation) {
	return state.strategic_statecraft_initialized && nation
		&& state.world.nation_is_valid(nation)
		&& nation.index() < state.strategic_interests.size()
		&& state.strategic_interests[nation.index()].enabled != 0;
}

bool uses_model_for_current_crisis(sys::state const& state) {
	return state.strategic_statecraft_initialized
		&& uses_model(state, state.primary_crisis_attacker)
		&& uses_model(state, state.primary_crisis_defender);
}

belief const* perception_of(sys::state const& state, dcon::nation_id observer, dcon::nation_id subject) {
	return find_belief(state, observer, subject);
}

std::string_view phase_name(crisis_phase phase) {
	switch(phase) {
	case crisis_phase::inactive: return "inactive";
	case crisis_phase::demand: return "demand";
	case crisis_phase::threat: return "threat";
	case crisis_phase::coalition: return "coalition";
	case crisis_phase::backing: return "backing";
	case crisis_phase::bargaining: return "bargaining";
	case crisis_phase::brinkmanship: return "brinkmanship";
	case crisis_phase::settled: return "settled";
	case crisis_phase::war: return "war";
	case crisis_phase::withdrawn: return "withdrawn";
	}
	return "inactive";
}

std::string_view decision_name(decision_code decision) {
	switch(decision) {
	case decision_code::none: return "none";
	case decision_code::alliance_offer: return "alliance_offer";
	case decision_code::alliance_decline: return "alliance_decline";
	case decision_code::crisis_support_attacker: return "support_attacker";
	case decision_code::crisis_support_defender: return "support_defender";
	case decision_code::crisis_stand_aside: return "stand_aside";
	case decision_code::crisis_demand: return "demand";
	case decision_code::crisis_counteroffer: return "counteroffer";
	case decision_code::crisis_accept: return "accept";
	case decision_code::crisis_reject: return "reject";
	case decision_code::crisis_mobilize: return "mobilize";
	case decision_code::crisis_withdraw: return "withdraw";
	case decision_code::crisis_war: return "escalate_to_war";
	}
	return "none";
}

std::string_view objective_name(objective_kind objective) {
	switch(objective) {
	case objective_kind::none: return "none";
	case objective_kind::security: return "security";
	case objective_kind::territorial_claim: return "territorial_claim";
	case objective_kind::market_access: return "market_access";
	case objective_kind::resource_access: return "resource_access";
	case objective_kind::route_access: return "route_access";
	case objective_kind::protect_partner: return "protect_partner";
	case objective_kind::prestige: return "prestige";
	case objective_kind::balance_threat: return "balance_threat";
	case objective_kind::alliance: return "alliance";
	}
	return "none";
}

void initialize(sys::state& state) {
	state.strategic_statecraft_initialized = true;
	state.strategic_interests.assign(state.world.nation_size(), interests{});
	state.strategic_beliefs.clear();
	state.strategic_commitments.clear();
	state.strategic_crisis = crisis_memory{};
	for(auto nation : state.world.in_nation)
		update_profile_for_new_country(state, nation.id);
	ensure_belief_rows(state);
	for(auto nation : state.world.in_nation) {
		for(auto relation : nation.get_diplomatic_relation()) {
			if(relation.get_are_allied()) {
				auto const a = relation.get_related_nations(0);
				auto const b = relation.get_related_nations(1);
				if(a && b && a.index() < b.index())
					alliance_formed(state, a, b);
			}
		}
		auto const overlord_relation = state.world.nation_get_overlord_as_subject(nation.id);
		auto const overlord = state.world.overlord_get_ruler(overlord_relation);
		if(overlord)
			guarantee_formed(state, overlord, nation.id);
	}
	update_monthly(state);
}

void update_monthly(sys::state& state) {
	if(!state.strategic_statecraft_initialized)
		return;
	if(state.strategic_interests.size() < state.world.nation_size())
		state.strategic_interests.resize(state.world.nation_size());
	for(auto nation : state.world.in_nation)
		update_profile_for_new_country(state, nation.id);
	ensure_belief_rows(state);
	for(auto& view : state.strategic_beliefs) {
		auto const observer = dcon::nation_id{ dcon::nation_id::value_base_t(uint32_t(view.key >> 32)) };
		auto const subject = dcon::nation_id{ dcon::nation_id::value_base_t(uint32_t(view.key)) };
		refresh_belief_values(state, observer, subject, view);
	}
	if(state.strategic_commitments.size() > 16384) {
		state.strategic_commitments.erase(
			std::remove_if(state.strategic_commitments.begin(), state.strategic_commitments.end(),
				[&](commitment const& item) {
					return !item.active && item.resolved_day + 20 * 365 < int32_t(day(state));
				}),
			state.strategic_commitments.end());
	}
}

float alliance_value(sys::state& state, dcon::nation_id observer, dcon::nation_id partner) {
	if(!uses_model(state, observer) || !partner || !state.world.nation_is_valid(partner)
		|| observer == partner)
		return -1.0f;
	auto const* view = find_belief(state, observer, partner);
	float const reliability = view ? view->reliability : 0.5f;
	float const threat = common_threat(state, observer, partner);
	float const distance = distance_interest(state, observer, partner);
	float const conflict = claim_conflict(state, observer, partner);
	float const other_power = view ? view->military_power : 0.5f;
	auto const& weights = state.strategic_interests[observer.index()];
	float const strategic_value = unit(0.42f * threat + 0.22f * other_power
		+ 0.18f * distance + 0.18f * float(state.world.nation_get_is_great_power(partner))
		+ 0.08f * weights.market_access
			* (view ? view->economic_power : 0.5f)
		+ 0.08f * weights.resource_access
			* (view ? view->economic_power : 0.5f)
		+ 0.1f * weights.route_access * distance
		+ 0.07f * weights.prestige_influence
			* float(state.world.nation_get_is_great_power(partner)));
	float const entanglement = unit(0.04f * float(state.world.nation_get_allies_count(partner))
		+ 0.06f * float(state.world.nation_get_is_at_war(partner))
		+ 0.18f * conflict);
	return std::clamp(0.3f * threat + 0.27f * strategic_value
		+ 0.25f * reliability + 0.18f * distance
		- 0.2f * entanglement - 0.48f * conflict, -1.0f, 1.0f);
}

dcon::nation_id best_alliance_partner(sys::state& state, dcon::nation_id nation) {
	if(!uses_model(state, nation))
		return dcon::nation_id{};
	dcon::nation_id best{};
	float best_value = 0.35f;
	for(auto candidate : state.world.in_nation) {
		if(candidate.id == nation || candidate.get_is_player_controlled()
			|| nations::are_allied(state, nation, candidate.id)
			|| military::are_at_war(state, nation, candidate.id)
			|| candidate.get_overlord_as_subject().get_ruler())
			continue;
		auto const proposer_value = alliance_value(state, nation, candidate.id);
		if(proposer_value <= best_value)
			continue;
		if(uses_model(state, candidate.id)
			&& alliance_value(state, candidate.id, nation) < 0.28f)
			continue;
		best_value = proposer_value;
		best = candidate.id;
	}
	set_decision(state, nation, best ? decision_code::alliance_offer : decision_code::alliance_decline, best_value);
	if(best)
		set_objective(state, nation, objective_kind::alliance, best, best_value);
	return best;
}

bool accepts_alliance(sys::state& state, dcon::nation_id target, dcon::nation_id proposer) {
	if(!uses_model(state, target))
		return false;
	auto const value = alliance_value(state, target, proposer);
	bool const accepted = value >= 0.28f
		&& state.world.nation_get_infamy(target) < state.defines.badboy_limit * 0.85f
		&& claim_conflict(state, target, proposer) < 0.75f;
	set_decision(state, target, accepted ? decision_code::alliance_offer : decision_code::alliance_decline, value);
	if(accepted)
		set_objective(state, target, objective_kind::alliance, proposer, value);
	return accepted;
}

void alliance_formed(sys::state& state, dcon::nation_id a, dcon::nation_id b) {
	if(!state.strategic_statecraft_initialized)
		return;
	add_commitment(state, a, b, commitment_kind::alliance);
	add_commitment(state, b, a, commitment_kind::alliance);
}

void alliance_broken(sys::state& state, dcon::nation_id a, dcon::nation_id b) {
	resolve_commitment(state, a, b, commitment_kind::alliance, false);
	resolve_commitment(state, b, a, commitment_kind::alliance, false);
}

void guarantee_formed(sys::state& state, dcon::nation_id guarantor, dcon::nation_id beneficiary) {
	if(!state.strategic_statecraft_initialized)
		return;
	add_commitment(state, guarantor, beneficiary, commitment_kind::guarantee);
}

void guarantee_broken(sys::state& state, dcon::nation_id guarantor, dcon::nation_id beneficiary) {
	resolve_commitment(state, guarantor, beneficiary, commitment_kind::guarantee, false);
}

bool wants_crisis_support(sys::state& state, dcon::nation_id nation, bool supports_attacker) {
	if(!uses_model(state, nation) || state.current_crisis_state == sys::crisis_state::inactive)
		return false;
	auto const side = crisis_side_actor(state, supports_attacker);
	auto const opponent = crisis_side_actor(state, !supports_attacker);
	if(!side || !opponent)
		return false;
	auto const secondary_side = supports_attacker ? state.crisis_attacker : state.crisis_defender;
	auto const secondary_opponent = supports_attacker ? state.crisis_defender : state.crisis_attacker;
	auto const* weights = profile(state, nation);
	auto const* side_view = find_belief(state, nation, side);
	auto const* other_view = find_belief(state, nation, opponent);
	float const reach_score = distance_interest(state, nation, side);
	float const claim_stake = supports_attacker
		? own_claim_interest(state, nation, state.crisis_defender)
		: own_claim_interest(state, nation, state.crisis_attacker);
	float common_threat_score = common_threat(state, nation, opponent);
	auto const rival = state.world.nation_get_ai_rival(nation);
	if(rival == opponent || (secondary_opponent && rival == secondary_opponent))
		common_threat_score = std::max(common_threat_score, 0.92f);
	if(rival == side || (secondary_side && rival == secondary_side))
		common_threat_score = unit(common_threat_score - 0.5f);
	float const ally_link = (nations::are_allied(state, nation, side)
		|| state.world.nation_get_in_sphere_of(side) == nation
		|| nations::is_nation_subject_of(state, nation, side)
		|| nations::is_nation_subject_of(state, side, nation)) ? 0.95f : 0.0f;
	float const protection_value = weights ? weights->ally_subject_protection : 0.0f;
	float const leader_reliability = side_view ? side_view->reliability : 0.5f;
	float const opponent_reliability = other_view ? other_view->reliability : 0.5f;
	float const known_balance = (supports_attacker ? coalition_power(state, nation, true)
		: coalition_power(state, nation, false))
		/ (coalition_power(state, nation, supports_attacker)
			+ coalition_power(state, nation, !supports_attacker) + 0.1f);
	float const access_opportunity = weights
		? unit(0.36f * weights->market_access * (side_view ? side_view->economic_power : 0.5f)
			+ 0.34f * weights->resource_access * (side_view ? side_view->economic_power : 0.5f)
			+ 0.3f * weights->route_access * reach_score)
		: 0.0f;
	float const interest_score = unit(0.32f * ally_link
		+ 0.24f * claim_stake
		+ 0.38f * common_threat_score
		+ 0.12f * access_opportunity
		+ 0.08f * (weights ? weights->prestige_influence : 0.0f)
		+ 0.06f * (secondary_side ? float(nations::are_allied(state, nation, secondary_side)) : 0.0f));
	float const expected_cost = unit(0.1f + 0.34f * (1.0f - known_balance)
		+ 0.12f * state.world.nation_get_war_exhaustion(nation) / 100.0f
		+ 0.12f * float(state.world.nation_get_is_mobilized(nation))
		+ 0.12f * opponent_reliability
		+ 0.08f * (1.0f - reach_score)
		- 0.14f * protection_value);
	float const commitment_value = ally_link * (0.2f + leader_reliability * 0.14f);
	float const utility = interest_score + commitment_value - expected_cost;
	bool const wants = utility > 0.02f;
	if(wants) {
		auto objective = objective_kind::security;
		if(claim_stake > 0.2f)
			objective = objective_kind::territorial_claim;
		else if(ally_link > 0.5f)
			objective = objective_kind::protect_partner;
		else if(common_threat_score > 0.45f)
			objective = objective_kind::balance_threat;
		else if(weights && weights->route_access >= weights->market_access
			&& weights->route_access >= weights->resource_access)
			objective = objective_kind::route_access;
		else if(weights && weights->resource_access >= weights->market_access)
			objective = objective_kind::resource_access;
		else if(weights && weights->market_access > 0.0f)
			objective = objective_kind::market_access;
		set_objective(state, nation, objective, side, utility);
	}
	set_decision(state, nation,
		wants ? (supports_attacker ? decision_code::crisis_support_attacker : decision_code::crisis_support_defender)
			: decision_code::crisis_stand_aside,
		utility);
	return wants;
}

bool choose_crisis_side(sys::state& state, dcon::nation_id nation, bool& supports_attacker) {
	bool const attacker = wants_crisis_support(state, nation, true);
	float const attacker_value = nation.index() < state.strategic_interests.size()
		? state.strategic_interests[nation.index()].last_decision_value : -1.0f;
	bool const defender = wants_crisis_support(state, nation, false);
	float const defender_value = nation.index() < state.strategic_interests.size()
		? state.strategic_interests[nation.index()].last_decision_value : -1.0f;
	if(!attacker && !defender) {
		auto const value = std::max(attacker_value, defender_value);
		set_decision(state, nation, decision_code::crisis_stand_aside, value);
		set_objective(state, nation, objective_kind::security,
			state.primary_crisis_attacker, value);
		return false;
	}
	if(attacker && defender) {
		// Prefer the side whose leader the state believes is more reliable; an
		// exact tie is resolved deterministically in favor of the claimant.
		auto const av = find_belief(state, nation, state.primary_crisis_attacker);
		auto const dv = find_belief(state, nation, state.primary_crisis_defender);
		float const attacker_score = attacker_value + (av ? 0.04f * av->reliability : 0.0f);
		float const defender_score = defender_value + (dv ? 0.04f * dv->reliability : 0.0f);
		supports_attacker = attacker_score >= defender_score;
		set_decision(state, nation, supports_attacker
			? decision_code::crisis_support_attacker : decision_code::crisis_support_defender,
			supports_attacker ? attacker_score : defender_score);
	} else {
		supports_attacker = attacker;
		set_decision(state, nation, supports_attacker
			? decision_code::crisis_support_attacker : decision_code::crisis_support_defender,
			supports_attacker ? attacker_value : defender_value);
	}
	auto const selected_side = supports_attacker
		? state.primary_crisis_attacker : state.primary_crisis_defender;
	auto const selected_opponent = supports_attacker
		? state.primary_crisis_defender : state.primary_crisis_attacker;
	auto const claim_stake = supports_attacker
		? own_claim_interest(state, nation, state.crisis_defender)
		: own_claim_interest(state, nation, state.crisis_attacker);
	auto const* weights = profile(state, nation);
	objective_kind objective = objective_kind::security;
	if(claim_stake > 0.2f)
		objective = objective_kind::territorial_claim;
	else if(nations::are_allied(state, nation, selected_side)
		|| nations::is_nation_subject_of(state, selected_side, nation)
		|| nations::is_nation_subject_of(state, nation, selected_side))
		objective = objective_kind::protect_partner;
	else if(state.world.nation_get_ai_rival(nation) == selected_opponent)
		objective = objective_kind::balance_threat;
	else if(weights && weights->route_access >= weights->market_access
		&& weights->route_access >= weights->resource_access)
		objective = objective_kind::route_access;
	else if(weights && weights->resource_access >= weights->market_access)
		objective = objective_kind::resource_access;
	else if(weights && weights->market_access > 0.0f)
		objective = objective_kind::market_access;
	set_objective(state, nation, objective, selected_side,
		supports_attacker ? attacker_value : defender_value);
	return true;
}

bool accepts_crisis_side_offer(sys::state& state, dcon::nation_id nation,
	dcon::nation_id proposer, sys::full_wg const& offer) {
	if(!uses_model(state, nation) || !valid_crisis_leaders(state))
		return false;
	bool const attacker_side = proposer == state.primary_crisis_attacker;
	if(proposer != state.primary_crisis_attacker && proposer != state.primary_crisis_defender)
		return false;
	auto const benefit = attacker_side
		? (state.crisis_defender == nation || holds_core_of(state, state.crisis_defender, nation))
		: (state.crisis_attacker == nation || holds_core_of(state, state.crisis_attacker, nation));
	float const offer_value = unit((benefit ? 0.48f : 0.12f)
		+ float((state.world.cb_type_get_type_bits(offer.cb) & military::cb_flag::po_unequal_treaty) != 0) * 0.18f
		+ float((state.world.cb_type_get_type_bits(offer.cb) & military::cb_flag::po_demand_state) != 0) * 0.2f);
	float const cost = expected_loss(state, nation, attacker_side);
	bool const already_wants_to_support = wants_crisis_support(state, nation, attacker_side);
	bool const accepted = already_wants_to_support && offer_value > 0.18f + 0.35f * cost
		&& state.world.nation_get_war_exhaustion(nation) < 95.0f;
	set_decision(state, nation, accepted
		? (attacker_side ? decision_code::crisis_support_attacker : decision_code::crisis_support_defender)
		: decision_code::crisis_reject, offer_value - 0.18f - 0.35f * cost);
	return accepted;
}

void record_crisis_commitment(sys::state& state, dcon::nation_id nation, bool supports_attacker) {
	if(!uses_model(state, nation) || !valid_crisis_leaders(state))
		return;
	if(state.strategic_crisis.phase <= uint8_t(crisis_phase::coalition)) {
		state.strategic_crisis.phase = uint8_t(crisis_phase::backing);
		state.strategic_crisis.phase_day = int32_t(day(state));
	}
	auto const side_leader = supports_attacker ? state.primary_crisis_attacker : state.primary_crisis_defender;
	if(nation == side_leader) {
		// A crisis leader commits to carrying its declared position through.
		// Record that promise against the opposing actor; a self-commitment would
		// be discarded and would leave retreats invisible to later crises.
		auto const opponent = supports_attacker
			? (state.crisis_defender ? state.crisis_defender : state.primary_crisis_defender)
			: (state.crisis_attacker ? state.crisis_attacker : state.primary_crisis_attacker);
		add_commitment(state, nation, opponent, commitment_kind::crisis_support);
	} else {
		// Coalition support is reciprocal: the supporter backs the leader, and
		// the leader undertakes to protect the supporter if the crisis escalates.
		add_commitment(state, nation, side_leader, commitment_kind::crisis_support);
		add_commitment(state, side_leader, nation, commitment_kind::crisis_support);
	}
	for(auto observer : state.world.in_nation) {
		if(!knows_crisis_event(state, observer.id, nation))
			continue;
		if(auto* view = find_belief(state, observer.id, nation)) {
			increment(view->crisis_support);
			view->last_commitment_day = int32_t(day(state));
		}
	}
}

void update_crisis(sys::state& state) {
	if(!state.strategic_statecraft_initialized)
		return;
	if(state.current_crisis_state == sys::crisis_state::inactive) {
		if(state.strategic_crisis.phase != uint8_t(crisis_phase::inactive)
			&& state.strategic_crisis.phase != uint8_t(crisis_phase::settled)
			&& state.strategic_crisis.phase != uint8_t(crisis_phase::war)
			&& state.strategic_crisis.phase != uint8_t(crisis_phase::withdrawn))
			state.strategic_crisis.phase = uint8_t(crisis_phase::inactive);
		return;
	}
	if(state.strategic_crisis.claimant == crisis_memory::no_nation
		|| state.strategic_crisis.phase == uint8_t(crisis_phase::settled)
		|| state.strategic_crisis.phase == uint8_t(crisis_phase::war)
		|| state.strategic_crisis.phase == uint8_t(crisis_phase::withdrawn)) {
		auto const first = state.crisis_attacker_wargoals.empty()
			? sys::full_wg{} : state.crisis_attacker_wargoals.front();
		state.strategic_crisis = crisis_memory{};
		state.strategic_crisis.phase = uint8_t(crisis_phase::demand);
		auto const claimant = state.crisis_attacker ? state.crisis_attacker : state.primary_crisis_attacker;
		auto const target = first.target_nation ? first.target_nation
			: (state.crisis_defender ? state.crisis_defender : state.primary_crisis_defender);
		state.strategic_crisis.claimant = claimant ? claimant.index() : crisis_memory::no_nation;
		state.strategic_crisis.target = target ? target.index() : crisis_memory::no_nation;
		state.strategic_crisis.opened_day = int32_t(day(state));
		state.strategic_crisis.phase_day = int32_t(day(state));
		state.strategic_crisis.demand_value = 0.55f;
		if(uses_model(state, claimant)) {
			state.strategic_crisis.demand_value = unit(0.38f
				+ 0.34f * state.strategic_interests[claimant.index()].territorial_claims
				+ 0.18f * state.strategic_interests[claimant.index()].prestige_influence);
			set_objective(state, claimant, objective_kind::territorial_claim, target,
				state.strategic_crisis.demand_value);
		}
		if(uses_model(state, state.primary_crisis_defender))
			set_objective(state, state.primary_crisis_defender, objective_kind::security, claimant, 0.5f);
		if(claimant) {
			for(auto observer : state.world.in_nation) {
				if(auto* view = find_belief(state, observer.id, claimant)) {
					if(!knows_crisis_event(state, observer.id, claimant))
						continue;
					increment(view->threats);
					view->last_threat_day = int32_t(day(state));
				}
			}
		}
	}
	if(!valid_crisis_leaders(state))
		return;
	observe_public_crisis(state);
	if(state.current_crisis_state != sys::crisis_state::heating_up)
		return;
	auto& crisis = state.strategic_crisis;
	auto const elapsed = int32_t(day(state)) - crisis.opened_day;
	if(elapsed >= 90 && crisis.phase < uint8_t(crisis_phase::brinkmanship)) {
		crisis.phase = uint8_t(crisis_phase::brinkmanship);
		crisis.phase_day = int32_t(day(state));
	} else if(elapsed >= 35 && crisis.phase < uint8_t(crisis_phase::bargaining)) {
		crisis.phase = uint8_t(crisis_phase::bargaining);
		crisis.phase_day = int32_t(day(state));
	} else if(elapsed >= 8 && crisis.phase < uint8_t(crisis_phase::coalition)) {
		crisis.phase = uint8_t(crisis_phase::coalition);
		crisis.phase_day = int32_t(day(state));
	} else if(elapsed >= 2 && crisis.phase < uint8_t(crisis_phase::threat)) {
		crisis.phase = uint8_t(crisis_phase::threat);
		crisis.phase_day = int32_t(day(state));
	}
	if(elapsed >= 28) {
		for(auto& participant : state.crisis_participants) {
			if(!participant.id)
				break;
			if(!participant.merely_interested
				|| state.world.nation_get_is_player_controlled(participant.id)
				|| !uses_model(state, participant.id))
				continue;
			bool supports_attacker = false;
			if(!choose_crisis_side(state, participant.id, supports_attacker))
				continue;
			participant.merely_interested = false;
			participant.supports_attacker = supports_attacker;
			record_crisis_commitment(state, participant.id, supports_attacker);
			notification::post(state, notification::message{
				[source = participant.id, supports_attacker](sys::state& state, text::layout_base& contents) {
					text::add_line(state, contents,
						supports_attacker ? "msg_crisis_vol_join_1" : "msg_crisis_vol_join_2",
						text::variable_type::x, source);
				},
				"msg_crisis_vol_join_title",
				participant.id, dcon::nation_id{}, dcon::nation_id{},
				sys::message_base_type::crisis_voluntary_join,
				dcon::province_id{}
			});
		}
	}
	if(elapsed >= 24 && !crisis.mobilized_by_attacker && uses_model(state, state.primary_crisis_attacker)
		&& state.world.nation_get_is_player_controlled(state.primary_crisis_attacker) == false) {
		auto const attacker_power = coalition_power(state, state.primary_crisis_attacker, true);
		auto const defender_power = coalition_power(state, state.primary_crisis_attacker, false);
		auto const interest = crisis_stake(state, state.primary_crisis_attacker);
		float const cost = 0.08f + 0.18f * state.world.nation_get_war_exhaustion(state.primary_crisis_attacker) / 100.0f;
		crisis.readiness_cost = unit(crisis.readiness_cost + cost);
		if(interest > cost && attacker_power + 0.55f >= defender_power
			&& !state.world.nation_get_is_mobilized(state.primary_crisis_attacker)) {
			military::start_mobilization(state, state.primary_crisis_attacker);
			crisis.mobilized_by_attacker = 1;
			crisis.phase = uint8_t(crisis_phase::brinkmanship);
			crisis.phase_day = int32_t(day(state));
			set_decision(state, state.primary_crisis_attacker, decision_code::crisis_mobilize, interest - cost);
			for(auto observer : state.world.in_nation) {
				raise_resolve(state, observer.id, state.primary_crisis_attacker, 0.12f);
			}
		} else {
			crisis.readiness_cost = unit(crisis.readiness_cost - cost);
		}
	}
	if(elapsed >= 38 && !crisis.mobilized_by_defender && uses_model(state, state.primary_crisis_defender)
		&& state.world.nation_get_is_player_controlled(state.primary_crisis_defender) == false) {
		auto const defender_power = coalition_power(state, state.primary_crisis_defender, false);
		auto const attacker_power = coalition_power(state, state.primary_crisis_defender, true);
		auto const interest = crisis_stake(state, state.primary_crisis_defender);
		float const cost = 0.07f + 0.2f * state.world.nation_get_war_exhaustion(state.primary_crisis_defender) / 100.0f;
		if(interest > cost && defender_power < attacker_power + 0.4f
			&& !state.world.nation_get_is_mobilized(state.primary_crisis_defender)) {
			military::start_mobilization(state, state.primary_crisis_defender);
			crisis.mobilized_by_defender = 1;
			set_decision(state, state.primary_crisis_defender, decision_code::crisis_mobilize, interest - cost);
			for(auto observer : state.world.in_nation)
				raise_resolve(state, observer.id, state.primary_crisis_defender, 0.1f);
		}
	}
}

bargaining_action decide_bargaining_action(sys::state& state) {
	if(!uses_model_for_current_crisis(state) || !valid_crisis_leaders(state)
		|| state.current_crisis_state != sys::crisis_state::heating_up)
		return bargaining_action::none;
	auto& crisis = state.strategic_crisis;
	if(crisis.offers_made >= 2 || int32_t(day(state)) - crisis.opened_day < 35)
		return bargaining_action::none;
	if(state.world.nation_get_is_player_controlled(state.primary_crisis_attacker)
		|| state.world.nation_get_is_player_controlled(state.primary_crisis_defender))
		return bargaining_action::none;
	if(state.world.nation_get_peace_offer_from_pending_peace_offer(state.primary_crisis_attacker)
		|| state.world.nation_get_peace_offer_from_pending_peace_offer(state.primary_crisis_defender))
		return bargaining_action::none;

	auto const attacker = state.primary_crisis_attacker;
	auto const defender = state.primary_crisis_defender;
	float const attacker_ratio = coalition_power(state, attacker, true)
		/ (coalition_power(state, attacker, false) + 0.1f);
	float const defender_ratio = coalition_power(state, defender, false)
		/ (coalition_power(state, defender, true) + 0.1f);
	auto const* attacker_view_of_self = find_belief(state, defender, attacker);
	float const perceived_resolve = attacker_view_of_self ? attacker_view_of_self->resolve : 0.5f;
	float const defender_holds_claim = own_claim_interest(state, defender, attacker);
	float const expected_attacker_loss = expected_loss(state, attacker, true);
	float const expected_defender_loss = expected_loss(state, defender, false);
	float const demand = std::clamp(crisis.demand_value + crisis.readiness_cost * 0.12f, 0.0f, 1.0f);
	auto const crisis_target = crisis.target == crisis_memory::no_nation ? dcon::nation_id{}
		: dcon::nation_id{ dcon::nation_id::value_base_t(crisis.target) };
	auto const offer_fraction = [](std::vector<sys::full_wg> const& goals) {
		std::size_t count = 0;
		for(auto const& goal : goals) {
			if(!goal.cb)
				break;
			++count;
		}
		if(count <= 1)
			return count == 0 ? 0.0f : 1.0f;
		return float(std::max<std::size_t>(1, count / 2)) / float(count);
	};
	set_objective(state, attacker, objective_kind::territorial_claim, crisis_target,
		demand - expected_attacker_loss);

	// A threatened state can yield when its own estimate of the attacker and
	// coalition makes continued resistance more costly than the demand.
	float const bargaining_pressure = 0.5f * perceived_resolve
		+ 0.28f * defender_ratio + 0.22f * expected_defender_loss;
	float const defender_stake = state.strategic_interests[defender.index()].security * 0.18f
		+ state.strategic_interests[defender.index()].territorial_claims * 0.16f;
	set_objective(state, defender, objective_kind::security, attacker, bargaining_pressure);
	if(defender_holds_claim < 0.78f && bargaining_pressure > 0.66f + defender_stake) {
		crisis.offered_value = demand * offer_fraction(state.crisis_attacker_wargoals);
		set_decision(state, defender, decision_code::crisis_counteroffer,
			perceived_resolve * defender_ratio + expected_defender_loss - defender_holds_claim);
		return bargaining_action::defender_concede;
	}
	if(attacker_ratio > 0.85f && demand > expected_attacker_loss + 0.12f) {
		set_decision(state, attacker, decision_code::crisis_demand, demand - expected_attacker_loss);
		return bargaining_action::attacker_demand;
	}
	if(attacker_ratio > 0.48f && demand > expected_attacker_loss + 0.22f
		&& perceived_resolve < 0.62f && crisis.offers_made == 0) {
		// Deliberate bluff: the demand is valuable enough to try, even though the
		// current estimate does not make war a safe choice.
		set_decision(state, attacker, decision_code::crisis_demand,
			demand - expected_attacker_loss - 0.12f);
		return bargaining_action::attacker_demand;
	}
	if(defender_ratio > 0.9f && expected_attacker_loss > demand * 0.65f) {
		crisis.offered_value = demand * offer_fraction(state.crisis_defender_wargoals);
		set_decision(state, attacker, decision_code::crisis_withdraw,
			expected_attacker_loss - demand * 0.65f);
		return bargaining_action::attacker_concede;
	}
	if(attacker_ratio < 0.72f && expected_defender_loss > defender_holds_claim + 0.12f) {
		set_decision(state, defender, decision_code::crisis_demand,
			expected_defender_loss - defender_holds_claim);
		return bargaining_action::defender_demand;
	}
	return bargaining_action::none;
}

bool accepts_crisis_peace_offer(sys::state& state, dcon::nation_id from,
	dcon::nation_id to, dcon::peace_offer_id offer) {
	if(!uses_model(state, to) || !valid_crisis_leaders(state))
		return false;
	if((to == state.crisis_defender || to == state.primary_crisis_defender)
		&& !state.crisis_attacker_wargoals.empty() && state.crisis_attacker_wargoals.front().cb) {
		auto const bits = state.world.cb_type_get_type_bits(state.crisis_attacker_wargoals.front().cb);
		if((bits & military::cb_flag::po_annex) != 0 && to == state.crisis_defender)
			return false;
	}
	if(from != state.primary_crisis_attacker && from != state.primary_crisis_defender)
		return false;
	auto const is_concession = state.world.peace_offer_get_is_concession(offer);
	float offered_cost = 0.0f;
	uint32_t terms = 0;
	for(auto item : state.world.peace_offer_get_peace_offer_item(offer)) {
		auto goal = item.get_wargoal();
		++terms;
		float value = 0.22f;
		auto const bits = state.world.cb_type_get_type_bits(goal.get_type());
		if((bits & (military::cb_flag::po_demand_state | military::cb_flag::po_annex)) != 0)
			value += 0.21f;
		else if(goal.get_associated_state())
			value += 0.18f;
		if((bits & military::cb_flag::po_annex) != 0)
			value += 0.24f;
		if(goal.get_target_nation() == to || goal.get_added_by() == from)
			offered_cost += value;
		else if(goal.get_target_nation() == from || goal.get_added_by() == to)
			offered_cost -= value;
	}
	if(is_concession)
		offered_cost -= 0.22f;
	if(terms == 0 && is_concession)
		offered_cost -= 0.18f;
	auto const own_attacker = to == state.primary_crisis_attacker;
	float const war_cost = expected_loss(state, to, own_attacker);
	auto const* opponent_view = find_belief(state, to, from);
	auto const opponent_power = coalition_power(state, to, !own_attacker);
	auto const own_power = coalition_power(state, to, own_attacker);
	float const perceived_resolve = opponent_view ? opponent_view->resolve : 0.5f;
	float const threatened_share = unit(opponent_power / (opponent_power + own_power + 0.1f));
	float const escalation_belief = threatened_share * (0.55f + 0.45f * perceived_resolve);
	float const acceptable_cost = 0.38f + 0.42f * escalation_belief
		+ 0.18f * war_cost - 0.12f * crisis_stake(state, to);
	bool const accepted = (is_concession || escalation_belief > 0.46f)
		&& offered_cost <= acceptable_cost
		&& state.world.nation_get_war_exhaustion(to) < 99.0f;
	set_objective(state, to, own_attacker ? objective_kind::territorial_claim : objective_kind::security,
		own_attacker ? state.primary_crisis_defender : state.primary_crisis_attacker,
		war_cost - offered_cost);
	set_decision(state, to, accepted ? decision_code::crisis_accept : decision_code::crisis_reject,
		war_cost - offered_cost);
	return accepted;
}

bool may_escalate_to_war(sys::state& state) {
	if(!uses_model_for_current_crisis(state) || !valid_crisis_leaders(state))
		return false;
	auto const elapsed = int32_t(day(state)) - state.strategic_crisis.opened_day;
	if(elapsed < 150)
		return false;
	float const attacker_ratio = coalition_power(state, state.primary_crisis_attacker, true)
		/ (coalition_power(state, state.primary_crisis_attacker, false) + 0.1f);
	float const defender_ratio = coalition_power(state, state.primary_crisis_defender, false)
		/ (coalition_power(state, state.primary_crisis_defender, true) + 0.1f);
	float const claimant_stake = crisis_stake(state, state.primary_crisis_attacker);
	float const defense_stake = crisis_stake(state, state.primary_crisis_defender);
	auto const crisis_target = state.strategic_crisis.target == crisis_memory::no_nation
		? dcon::nation_id{} : dcon::nation_id{
			dcon::nation_id::value_base_t(state.strategic_crisis.target) };
	set_objective(state, state.primary_crisis_attacker, objective_kind::territorial_claim,
		crisis_target, claimant_stake);
	set_objective(state, state.primary_crisis_defender, objective_kind::security,
		state.primary_crisis_attacker, defense_stake);
	bool const escalation = claimant_stake > 0.25f && attacker_ratio > 0.68f
		&& (attacker_ratio + claimant_stake > defender_ratio + defense_stake * 0.72f);
	if(escalation) {
		set_decision(state, state.primary_crisis_attacker, decision_code::crisis_war,
			attacker_ratio + claimant_stake - defender_ratio);
		state.strategic_crisis.phase = uint8_t(crisis_phase::war);
	} else if(elapsed >= 240
		&& !state.world.nation_get_is_player_controlled(state.primary_crisis_attacker)
		&& !state.world.nation_get_is_player_controlled(state.primary_crisis_defender)) {
		// A threat that no longer pays for its expected war loss is withdrawn;
		// cleanup records the retreat in other states' resolve beliefs.
		nations::cleanup_crisis(state);
	}
	return escalation;
}

void record_crisis_outcome(sys::state& state, crisis_phase outcome, dcon::nation_id conceding_party) {
	if(!state.strategic_statecraft_initialized || state.strategic_crisis.phase == uint8_t(crisis_phase::inactive))
		return;
	state.strategic_crisis.outcome = uint8_t(outcome);
	state.strategic_crisis.phase = uint8_t(outcome);
	auto const attacker = state.primary_crisis_attacker;
	auto const defender = state.primary_crisis_defender;
	state.strategic_crisis.outcome_actor = conceding_party
		? uint32_t(conceding_party.index()) : state.strategic_crisis.claimant;
	state.strategic_crisis.resolution_decision = uint8_t(
		outcome == crisis_phase::settled ? decision_code::crisis_accept
		: outcome == crisis_phase::withdrawn ? decision_code::crisis_withdraw
		: outcome == crisis_phase::war ? decision_code::crisis_war : decision_code::none);
	state.strategic_crisis.temporary_offer_goal_slot = -1;
	auto const claimant = state.strategic_crisis.claimant == crisis_memory::no_nation
		? dcon::nation_id{} : dcon::nation_id{
			dcon::nation_id::value_base_t(state.strategic_crisis.claimant) };
	for(auto observer : state.world.in_nation) {
		if(attacker)
			raise_resolve(state, observer.id, attacker,
				(outcome == crisis_phase::war || conceding_party == defender) ? 0.08f : -0.1f);
		if(defender)
			raise_resolve(state, observer.id, defender,
				(outcome == crisis_phase::war || conceding_party == attacker) ? 0.05f : -0.06f);
	}
	for(auto const& participant : state.crisis_participants) {
		if(!participant.id)
			break;
		if(participant.merely_interested)
			continue;
		auto const beneficiary = participant.supports_attacker ? attacker : defender;
		resolve_commitment(state, participant.id, beneficiary, commitment_kind::crisis_support, true);
		resolve_commitment(state, beneficiary, participant.id, commitment_kind::crisis_support,
			outcome != crisis_phase::withdrawn || beneficiary != claimant);
		if(nations::are_allied(state, participant.id, beneficiary)) {
			assess_active_commitment(state, participant.id, beneficiary, commitment_kind::alliance, true);
			assess_active_commitment(state, beneficiary, participant.id, commitment_kind::alliance, true);
		}
	}
	auto const attacker_target = state.crisis_defender
		? state.crisis_defender : defender;
	auto const defender_target = state.crisis_attacker
		? state.crisis_attacker : attacker;
	resolve_commitment(state, attacker, attacker_target, commitment_kind::crisis_support,
		outcome == crisis_phase::war || (outcome == crisis_phase::settled
			&& conceding_party == attacker_target));
	resolve_commitment(state, defender, defender_target, commitment_kind::crisis_support,
		outcome == crisis_phase::war || (outcome == crisis_phase::settled
			&& conceding_party == defender_target)
			|| (outcome == crisis_phase::withdrawn && conceding_party == state.strategic_crisis.claimant));
	auto const guarantee_count = state.strategic_commitments.size();
	for(std::size_t index = 0; index < guarantee_count; ++index) {
		auto const promise = state.strategic_commitments[index];
		if(!promise.active || promise.kind != uint8_t(commitment_kind::guarantee))
			continue;
		auto const guarantor = dcon::nation_id{ dcon::nation_id::value_base_t(promise.promisor) };
		auto const beneficiary = dcon::nation_id{ dcon::nation_id::value_base_t(promise.beneficiary) };
		if(!state.world.nation_is_valid(guarantor) || !state.world.nation_is_valid(beneficiary)
			|| !nations::is_nation_subject_of(state, beneficiary, guarantor))
			continue;
		bool beneficiary_side = false;
		if(!crisis_side_of(state, beneficiary, beneficiary_side))
			continue;
		bool guarantor_side = false;
		bool const backed = crisis_side_of(state, guarantor, guarantor_side)
			&& guarantor_side == beneficiary_side;
		assess_active_commitment(state, guarantor, beneficiary, commitment_kind::guarantee, backed);
	}
	if(conceding_party) {
		for(auto observer : state.world.in_nation) {
			if(!knows_crisis_event(state, observer.id, conceding_party))
				continue;
			if(auto* view = find_belief(state, observer.id, conceding_party)) {
				increment(view->concessions);
				view->resolve = unit(view->resolve - 0.035f);
			}
		}
	}
	if(outcome == crisis_phase::withdrawn) {
		for(auto observer : state.world.in_nation) {
			if(!knows_crisis_event(state, observer.id, claimant))
				continue;
			if(auto* view = find_belief(state, observer.id, claimant))
				increment(view->abandonment);
		}
	}
}

void crisis_offer_rejected(sys::state& state, dcon::nation_id proposer) {
	if(!uses_model_for_current_crisis(state) || !proposer)
		return;
	clear_temporary_offer_goal(state);
	for(auto observer : state.world.in_nation) {
		if(!knows_crisis_event(state, observer.id, proposer))
			continue;
		if(auto* view = find_belief(state, observer.id, proposer)) {
			increment(view->threats);
			view->last_threat_day = int32_t(day(state));
			view->resolve = unit(view->resolve - 0.025f);
		}
	}
	state.strategic_crisis.readiness_cost = unit(state.strategic_crisis.readiness_cost + 0.025f);
}

void record_crisis_war_outcome(sys::state& state, dcon::war_id war, bool attacker_won, bool draw) {
	if(!state.strategic_statecraft_initialized || !war)
		return;
	auto const attacker = state.world.war_get_original_attacker(war);
	auto const defender = state.world.war_get_original_target(war);
	if(!draw)
		state.strategic_crisis.outcome_actor = uint32_t((attacker_won ? attacker : defender).index());
	for(auto observer : state.world.in_nation) {
		if(!knows_war_event(state, observer.id, attacker, war))
			continue;
		for(auto participant : state.world.war_get_war_participant(war)) {
			if(!knows_war_event(state, observer.id, participant.get_nation(), war))
				continue;
			auto* view = find_belief(state, observer.id, participant.get_nation());
			if(!view)
				continue;
			if(view->currently_at_war == 0) {
				increment(view->recent_wars);
				view->last_war_day = int32_t(day(state));
			}
			view->currently_at_war = 0;
		}
		if(draw) {
			raise_resolve(state, observer.id, attacker, 0.025f);
			raise_resolve(state, observer.id, defender, 0.025f);
		} else {
			raise_resolve(state, observer.id, attacker, attacker_won ? 0.12f : -0.1f);
			raise_resolve(state, observer.id, defender, attacker_won ? -0.1f : 0.12f);
		}
	}
}

} // namespace nations::strategic_statecraft
