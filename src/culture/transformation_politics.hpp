#pragma once

#include "dcon_generated_ids.hpp"
#include "system_state_forward.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace politics::transformation {

// Stable numeric values are intentional: coalition masks and deterministic
// tie-breaking use them. These are runtime results, not saved DCON entities.
enum class interest_group_id : uint8_t {
	landed_elites = 0,
	industrialists = 1,
	intelligentsia = 2,
	organized_labor = 3,
	rural_communities = 4,
	state_services = 5,
	count = 6,
};

constexpr inline std::size_t interest_group_count = std::size_t(interest_group_id::count);
using interest_group_mask = uint32_t;

constexpr inline interest_group_mask group_bit(interest_group_id id) {
	return interest_group_mask(1u) << uint8_t(id);
}

constexpr inline interest_group_mask all_group_bits =
	(interest_group_mask(1u) << uint8_t(interest_group_id::count)) - 1u;

// Roles are deliberately more detailed than interest groups. A POP can lend
// support to several groups, while exact Project Alice POP types are mapped to
// one of these roles by population_role_for_pop_type().
enum class population_role : uint8_t {
	landowner,
	capital_owner,
	artisan,
	intellectual,
	administrator,
	officer,
	soldier,
	industrial_worker,
	farmer,
	agricultural_laborer,
	enslaved,
	other_poor,
	other_middle,
	other_rich,
};

struct population_sample {
	population_role role = population_role::other_poor;
	float population = 0.0f;
	float savings_per_capita = 0.0f;
	// Realized needs satisfaction is used by the state adapter as an income
	// security proxy. Pure callers may provide a better [0, 1] estimate.
	float income_security = 0.0f;
	// A bounded [0, 1] ownership proxy. The adapter derives it from POP role;
	// wealth remains separate so newly prosperous workers can gain power too.
	float property_ownership = 0.0f;
	float literacy = 0.0f;
	// Normalized [0, 1]. Project Alice's native 0..10 value is scaled by the
	// adapter.
	float consciousness = 0.0f;
	// Political rights and organization are deliberately separate from mass
	// support. A disenfranchised population can still create unrest and popular
	// pressure, but should not automatically receive the same institutional power
	// as an organized electorate.
	float political_rights = 1.0f;
	float political_organization = 1.0f;
};

struct ruleset_config {
	// This is the compatibility boundary. When false, every high-level entry
	// point returns enabled=false and no caller should alter legacy politics.
	bool enabled = false;

	float wealth_reference = 1.0f;
	float base_power = 0.25f;
	float wealth_power_weight = 1.25f;
	float income_power_weight = 0.35f;
	float property_power_weight = 1.50f;
	float literacy_power_weight = 0.35f;
	float consciousness_power_weight = 0.30f;
	float maximum_power_per_capita = 4.0f;

	float coalition_power_target = 0.50f;
	uint8_t maximum_coalition_groups = 3;
	float coalition_power_weight = 0.45f;
	float coalition_support_weight = 0.15f;
	float coalition_cohesion_weight = 0.20f;
	float coalition_majority_bonus = 0.25f;
	float coalition_size_penalty = 0.025f;
	// A challenger must beat the incumbent by more than this raw-score margin.
	// Passing the previous mask is optional, so callers control persistence.
	float coalition_hysteresis_margin = 0.03f;
	// Maximum legitimacy points a wholly foreign-owned economy can cost.
	float maximum_foreign_ownership_penalty = 12.0f;
};

struct interest_group_aggregate {
	interest_group_id id = interest_group_id::landed_elites;
	float population_support = 0.0f;
	float political_power = 0.0f;
	float support_share = 0.0f;
	float political_power_share = 0.0f;
	float mean_wealth_signal = 0.0f;
	float mean_income_security = 0.0f;
	float mean_property_ownership = 0.0f;
	float mean_literacy = 0.0f;
	float mean_consciousness = 0.0f;
	float mean_power_per_capita = 0.0f;
};

struct interest_group_snapshot {
	bool enabled = false;
	float represented_population = 0.0f;
	float electorate_population = 0.0f;
	interest_group_mask active_groups = 0;
	std::array<interest_group_aggregate, interest_group_count> groups{};
};

struct coalition_result {
	interest_group_mask groups = 0;
	uint8_t group_count = 0;
	float power_share = 0.0f;
	float support_share = 0.0f;
	float cohesion = 0.0f;
	float score = 0.0f;
	bool has_working_majority = false;
	bool retained_incumbent = false;

	interest_group_mask best_challenger = 0;
	float best_challenger_score = 0.0f;
	float incumbent_score = 0.0f;
	float challenger_advantage = 0.0f;
};

// Serialized independently from the derived politics cache. It gives a
// country an actual incumbent: a coalition can survive a weak month, acquire
// a tenure, and be replaced only by a credible challenger.
struct governing_coalition_state {
	interest_group_mask groups = 0;
	int32_t established_on = 0;
	std::array<float, interest_group_count> confidence{};
	// Negative means that no election result has been observed yet. Otherwise
	// this is the winning party's actual vote/seat share from the last election.
	float party_mandate = -1.0f;
};

enum class legislation_stage : uint8_t {
	none = 0,
	negotiation = 1,
	voting = 2,
	implementation = 3,
};

enum class legislation_target : uint8_t {
	issue = 0,
	reform = 1,
};

// One active bill per nation keeps the first implementation vertical slice
// legible: the player can see what is being negotiated, why it is delayed, and
// what still has to happen before the law reaches the country.
struct legislation_state {
	bool active = false;
	legislation_target target = legislation_target::issue;
	dcon::issue_option_id option{};
	dcon::reform_option_id reform{};
	dcon::political_party_id sponsor{};
	legislation_stage stage = legislation_stage::none;
	int32_t proposed_on = 0;
	uint16_t stage_days = 0;
	float compromise = 0.0f;
	float mandate = 0.0f;
	float execution = 0.0f;
	float coalition_support = 0.0f;
	// Position of the sponsoring/ruling party on this issue. A party that has
	// no explicit position contributes a neutral 50%, not a hidden veto.
	float party_support = 0.5f;
};

// Read-only presentation of the active bill's current gate. Keeping this in
// the simulation layer prevents the UI from explaining different thresholds
// than advance_legislation() actually applies.
struct legislation_progress {
	bool active = false;
	uint16_t minimum_stage_days = 0;
	uint16_t maximum_stage_days = 0;
	float required_mandate = 0.0f;
	float required_coalition_support = 0.0f;
	float required_execution = 0.0f;
	float effective_coalition_support = 0.0f;
	float concession_pressure = 0.0f;
	bool time_ready = false;
	bool mandate_ready = false;
	bool coalition_ready = false;
	bool execution_ready = false;
};

struct government_snapshot {
	interest_group_mask groups = 0;
	int32_t established_on = 0;
	float stability = 0.0f;
	// Electoral mandate of the current ruling party, separate from the
	// interest-group coalition that keeps the cabinet in office.
	float party_mandate = 0.0f;
	// Confidence is deliberately exposed to the UI: the player needs to see
	// which members of the cabinet are its weak link, not just one opaque score.
	std::array<float, interest_group_count> confidence{};
	bool changed_this_refresh = false;
};

struct legitimacy_breakdown {
	float power_mandate = 0.0f;
	float popular_support = 0.0f;
	float coalition_cohesion = 0.0f;
	float social_breadth = 0.0f;
	float continuity = 0.0f;
	float majority_bonus = 0.0f;
	float minority_penalty = 0.0f;
	float representation_gap_penalty = 0.0f;
	float fragmentation_penalty = 0.0f;
	// A government presiding over an economy owned from abroad answers to
	// people who did not put it there. Bounded, and zero when nothing is
	// foreign-owned, which is every classic game.
	float foreign_ownership_penalty = 0.0f;
	float total = 0.0f;
};

struct nation_result {
	bool enabled = false;
	interest_group_snapshot interest_groups{};
	coalition_result coalition{};
	government_snapshot government{};
	legitimacy_breakdown legitimacy{};
};

// A reform has two distinct constituencies: people and political power. The
// second can diverge sharply when ownership and wealth are concentrated.
struct issue_support_result {
	bool enabled = false;
	std::array<float, interest_group_count> group_support{};
	float popular_support = 0.0f;
	float electoral_support = 0.0f;
	float political_power_support = 0.0f;
	float coalition_support = 0.0f;
	float opposition_support = 0.0f;
};

struct movement_pressure_inputs {
	bool enabled = false;
	float political_power_support = 0.0f;
	float coalition_support = 0.0f;
	float legitimacy = 1.0f;
	float economic_hardship = 0.0f;
	float implementation_gap = 0.0f;
	// National legitimacy can look healthy while a movement is concentrated
	// in provinces where the state has little control or administrative reach.
	float regional_control = 1.0f;
	float regional_execution = 1.0f;
};

struct movement_pressure_breakdown {
	bool enabled = false;
	float political_power_pressure = 0.0f;
	float coalition_opposition_pressure = 0.0f;
	float legitimacy_pressure = 0.0f;
	float hardship_pressure = 0.0f;
	float implementation_pressure = 0.0f;
	float regional_control_pressure = 0.0f;
	float regional_implementation_pressure = 0.0f;
	float legitimacy_relief = 0.0f;
	float total_adjustment = 0.0f;
};

// Civil conflict gives a government a second, costly route to reform. This is
// deliberately separate from ordinary support: a concession made under street
// pressure is not the same thing as a parliamentary mandate.
struct concession_pressure_inputs {
	bool enabled = false;
	float matching_movement_population = 0.0f;
	float matching_movement_radicalism = 0.0f;
	float rebel_population = 0.0f;
	float rebel_occupation = 0.0f;
};

struct concession_pressure_result {
	bool enabled = false;
	float movement_pressure = 0.0f;
	float rebellion_pressure = 0.0f;
	float total = 0.0f;
};

// Pure functions. They are deterministic for the same ordered samples and
// config, sanitize non-finite input, and expose every quantity used by the
// coalition and legitimacy calculations.
std::array<float, interest_group_count> affinity_for_role(population_role role);
interest_group_snapshot aggregate_interest_groups(
	std::vector<population_sample> const& samples,
	ruleset_config const& config);
coalition_result select_governing_coalition(
	interest_group_snapshot const& snapshot,
	ruleset_config const& config,
	interest_group_mask previous_coalition = 0);
// foreign_ownership is the [0, 1] share of the nation's productive assets held
// abroad, weighted by value.
legitimacy_breakdown calculate_legitimacy(
	interest_group_snapshot const& snapshot,
	coalition_result const& coalition,
	ruleset_config const& config,
	interest_group_mask previous_coalition = 0,
	float foreign_ownership = 0.0f);
nation_result evaluate_population(
	std::vector<population_sample> const& samples,
	ruleset_config const& config,
	interest_group_mask previous_coalition = 0,
	float foreign_ownership = 0.0f);

// Thin Project Alice adapter. It owns no state and writes nothing, so callers
// may cache or discard results without changing save-file compatibility.
population_role population_role_for_pop_type(sys::state const& state, dcon::pop_type_id pop_type);
population_sample sample_from_pop(sys::state const& state, dcon::pop_id pop);
std::vector<population_sample> collect_nation_population(sys::state const& state, dcon::nation_id nation);
nation_result evaluate_nation(
	sys::state const& state,
	dcon::nation_id nation,
	ruleset_config const& config,
	interest_group_mask previous_coalition = 0);
issue_support_result evaluate_issue_support(sys::state& state,
	dcon::nation_id nation, dcon::issue_option_id option);
issue_support_result evaluate_reform_support(sys::state& state,
	dcon::nation_id nation, dcon::reform_option_id option);
movement_pressure_breakdown calculate_movement_pressure(
	movement_pressure_inputs inputs);
movement_pressure_breakdown movement_pressure_for(sys::state& state,
	dcon::movement_id movement);
concession_pressure_result calculate_concession_pressure(
	concession_pressure_inputs inputs);
concession_pressure_result concession_pressure_for(sys::state& state,
	dcon::nation_id nation, dcon::issue_option_id option);

// Apply the political aftershock of an enacted issue to the incumbent
// cabinet. This is intentionally a small, bounded state transition: groups
// that supported the settlement gain confidence, while a reform enacted by a
// narrow power bloc against the electorate weakens the coalition.
void record_reform_outcome(sys::state& state,
	dcon::nation_id nation, dcon::issue_option_id option);
void record_reform_outcome(sys::state& state,
	dcon::nation_id nation, dcon::reform_option_id option);
void record_ruling_party_change(sys::state& state, dcon::nation_id nation,
	dcon::political_party_id old_party, dcon::political_party_id new_party,
	float election_mandate = -1.0f);

legislation_state const* active_bill(sys::state const& state, dcon::nation_id nation);
legislation_progress bill_progress_for(sys::state& state, dcon::nation_id nation);
bool can_propose_bill(sys::state& state,
	dcon::nation_id nation, dcon::issue_option_id option);
void propose_bill(sys::state& state,
	dcon::nation_id nation, dcon::issue_option_id option);
bool can_propose_bill(sys::state& state,
	dcon::nation_id nation, dcon::reform_option_id option);
void propose_bill(sys::state& state,
	dcon::nation_id nation, dcon::reform_option_id option);
bool can_withdraw_bill(sys::state& state, dcon::nation_id nation);
void withdraw_bill(sys::state& state, dcon::nation_id nation);
void advance_legislation(sys::state& state);

// Derived runtime cache. It is reconstructed from POP/economy state plus the
// serialized incumbent coalition state. Hysteresis therefore survives a save
// and load instead of making a continuous campaign diverge from its reload.
ruleset_config ruleset_config_for(sys::state const& state);
void invalidate_cache(sys::state& state);
void refresh_all_nations(sys::state& state);
nation_result const* cached_nation_result(sys::state& state, dcon::nation_id nation);

} // namespace politics::transformation
