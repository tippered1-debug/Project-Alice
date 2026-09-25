#pragma once

#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

#include "dcon_generated_ids.hpp"

namespace sys {
class state;
struct full_wg;
}

namespace nations::strategic_statecraft {

enum class commitment_kind : uint8_t {
	alliance = 0,
	crisis_support = 1,
	guarantee = 2
};

enum class crisis_phase : uint8_t {
	inactive = 0,
	demand = 1,
	threat = 2,
	coalition = 3,
	backing = 4,
	bargaining = 5,
	brinkmanship = 6,
	settled = 7,
	war = 8,
	withdrawn = 9
};

enum class decision_code : uint8_t {
	none = 0,
	alliance_offer = 1,
	alliance_decline = 2,
	crisis_support_attacker = 3,
	crisis_support_defender = 4,
	crisis_stand_aside = 5,
	crisis_demand = 6,
	crisis_counteroffer = 7,
	crisis_accept = 8,
	crisis_reject = 9,
	crisis_mobilize = 10,
	crisis_withdraw = 11,
	crisis_war = 12
};

enum class objective_kind : uint8_t {
	none = 0,
	security = 1,
	territorial_claim = 2,
	market_access = 3,
	resource_access = 4,
	route_access = 5,
	protect_partner = 6,
	prestige = 7,
	balance_threat = 8,
	alliance = 9
};

// Persistent national preferences. The enabled flag is per country so older
// saves can use the legacy diplomacy AI until a country is migrated.
struct interests {
	static constexpr uint32_t no_nation = std::numeric_limits<uint32_t>::max();
	float security = 0.5f;
	float territorial_claims = 0.25f;
	float market_access = 0.25f;
	float resource_access = 0.25f;
	float route_access = 0.25f;
	float ally_subject_protection = 0.25f;
	float prestige_influence = 0.25f;
	uint8_t enabled = 0;
	uint8_t last_decision = uint8_t(decision_code::none);
	uint8_t objective = uint8_t(objective_kind::none);
	uint8_t reserved = 0;
	float last_decision_value = 0.0f;
	uint32_t objective_target = no_nation;
	float objective_value = 0.0f;
};

// One country's imperfect and persistent view of another country. Key packs
// observer and subject indices so the vector can stay sorted and searchable.
struct belief {
	uint64_t key = 0;
	float military_power = 0.5f;
	float economic_power = 0.5f;
	float resolve = 0.5f;
	float reliability = 0.5f;
	uint16_t fulfilled_commitments = 0;
	uint16_t broken_commitments = 0;
	uint16_t recent_wars = 0;
	uint16_t threats = 0;
	uint16_t crisis_support = 0;
	uint16_t abandonment = 0;
	uint16_t concessions = 0;
	uint8_t currently_at_war = 0;
	uint8_t reserved = 0;
	int32_t last_observed_day = 0;
	int32_t last_war_day = 0;
	int32_t last_threat_day = 0;
	int32_t last_commitment_day = 0;
};

struct commitment {
	uint32_t promisor = 0;
	uint32_t beneficiary = 0;
	uint8_t kind = uint8_t(commitment_kind::alliance);
	uint8_t active = 0;
	uint16_t reserved = 0;
	int32_t signed_day = 0;
	int32_t resolved_day = 0;
};

struct crisis_memory {
	static constexpr uint32_t no_nation = std::numeric_limits<uint32_t>::max();
	uint8_t phase = uint8_t(crisis_phase::inactive);
	uint8_t outcome = uint8_t(crisis_phase::inactive);
	uint8_t offers_made = 0;
	uint8_t mobilized_by_attacker = 0;
	uint8_t mobilized_by_defender = 0;
	uint8_t partial_concession = 0;
	uint8_t reserved[2] = {};
	uint32_t claimant = no_nation;
	uint32_t target = no_nation;
	uint32_t outcome_actor = no_nation;
	int32_t opened_day = 0;
	int32_t phase_day = 0;
	int32_t last_offer_day = 0;
	int32_t temporary_offer_goal_slot = -1;
	uint8_t resolution_decision = uint8_t(decision_code::none);
	uint8_t reserved_outcome[3] = {};
	float demand_value = 0.0f;
	float offered_value = 0.0f;
	float readiness_cost = 0.0f;
};

enum class bargaining_action : uint8_t {
	none = 0,
	attacker_demand = 1,
	attacker_concede = 2,
	defender_demand = 3,
	defender_concede = 4
};

bool uses_model(sys::state const& state, dcon::nation_id nation);
bool uses_model_for_current_crisis(sys::state const& state);
belief const* perception_of(sys::state const& state, dcon::nation_id observer, dcon::nation_id subject);
std::string_view phase_name(crisis_phase phase);
std::string_view decision_name(decision_code decision);
std::string_view objective_name(objective_kind objective);
void initialize(sys::state& state);
void update_monthly(sys::state& state);
void update_crisis(sys::state& state);

float alliance_value(sys::state& state, dcon::nation_id observer, dcon::nation_id partner);
dcon::nation_id best_alliance_partner(sys::state& state, dcon::nation_id nation);
bool accepts_alliance(sys::state& state, dcon::nation_id target, dcon::nation_id proposer);
void alliance_formed(sys::state& state, dcon::nation_id a, dcon::nation_id b);
void alliance_broken(sys::state& state, dcon::nation_id a, dcon::nation_id b);
void guarantee_formed(sys::state& state, dcon::nation_id guarantor, dcon::nation_id beneficiary);
void guarantee_broken(sys::state& state, dcon::nation_id guarantor, dcon::nation_id beneficiary);

bool wants_crisis_support(sys::state& state, dcon::nation_id nation, bool supports_attacker);
bool choose_crisis_side(sys::state& state, dcon::nation_id nation, bool& supports_attacker);
bool accepts_crisis_side_offer(sys::state& state, dcon::nation_id nation,
	dcon::nation_id proposer, sys::full_wg const& offer);
bool accepts_crisis_peace_offer(sys::state& state, dcon::nation_id from,
	dcon::nation_id to, dcon::peace_offer_id offer);
bargaining_action decide_bargaining_action(sys::state& state);
bool may_escalate_to_war(sys::state& state);
void record_crisis_commitment(sys::state& state, dcon::nation_id nation, bool supports_attacker);
void record_crisis_outcome(sys::state& state, crisis_phase outcome,
	dcon::nation_id conceding_party = dcon::nation_id{});
void crisis_offer_rejected(sys::state& state, dcon::nation_id proposer);
void record_crisis_war_outcome(sys::state& state, dcon::war_id war, bool attacker_won, bool draw);

} // namespace nations::strategic_statecraft
