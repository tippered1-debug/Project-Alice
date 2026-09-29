#include "ai_alliances.hpp"
#include "ai_campaign_values.hpp"
#include "system_state.hpp"
#include "commands.hpp"
#include "nations/strategic_statecraft.hpp"

namespace ai {

constexpr inline float ally_overestimate = 2.f;

void form_alliances(sys::state& state) {
	for(auto n : state.world.in_nation) {
		if(n.get_is_player_controlled() || n.get_overlord_as_subject().get_ruler())
			continue;
		auto partner = nations::strategic_statecraft::best_alliance_partner(state, n.id);
		bool const accepted = partner && (state.world.nation_get_is_player_controlled(partner)
			? state.world.unilateral_relationship_get_interested_in_alliance(
				state.world.get_unilateral_relationship_by_unilateral_pair(n.id, partner))
			: nations::strategic_statecraft::accepts_alliance(state, partner, n.id));
		if(accepted)
			nations::make_alliance(state, n.id, partner);
	}
}

void prune_alliances(sys::state& state) {
	static std::vector<dcon::nation_id> prune_targets;
	for(auto n : state.world.in_nation) {
		if(n.get_is_player_controlled() || n.get_overlord_as_subject().get_ruler())
			continue;
		prune_targets.clear();
		for(auto relation : n.get_diplomatic_relation()) {
			if(!relation.get_are_allied())
				continue;
			auto other = relation.get_related_nations(0) != n
				? relation.get_related_nations(0) : relation.get_related_nations(1);
			if(other.get_in_sphere_of() == n || military::are_allied_in_war(state, n, other))
				continue;
			auto const mutual_value = nations::strategic_statecraft::alliance_value(state, n.id, other.id)
				+ nations::strategic_statecraft::alliance_value(state, other.id, n.id);
			if(mutual_value < 0.12f || state.world.nation_get_infamy(other) >= state.defines.badboy_limit)
				prune_targets.push_back(other.id);
		}
		for(auto target : prune_targets) {
			if(command::can_cancel_alliance(state, n.id, target, true))
				command::execute_cancel_alliance(state, n.id, target);
		}
	}
}


bool ai_is_close_enough(sys::state& state, dcon::nation_id target, dcon::nation_id from) {
	auto target_continent = state.world.province_get_continent(state.world.nation_get_capital(target));
	auto source_continent = state.world.province_get_continent(state.world.nation_get_capital(from));
	return (target_continent == source_continent) || bool(state.world.get_nation_adjacency_by_nation_adjacency_pair(target, from));
}

static bool ai_has_mutual_enemy(sys::state& state, dcon::nation_id from, dcon::nation_id target) {
	auto rival_a = state.world.nation_get_ai_rival(target);
	auto rival_b = state.world.nation_get_ai_rival(from);
	// Same rival equates to instantaneous alliance (we benefit from more allies against a common enemy)
	if(rival_a && rival_a == rival_b)
		return true;
	// // Our rivals are allied?
	// if(rival_a && rival_b && rival_a != rival_b && nations::are_allied(state, rival_a, rival_b))
	// 	return true;

	// // One of the allies of our rivals can be declared on?
	// for(auto n : state.world.in_nation) {
	// 	if(n.id != target && n.id != from && n.id != rival_a && n.id != rival_b) {
	// 		if(nations::are_allied(state, rival_a, n.id) || nations::are_allied(state, rival_b, n.id)) {
	// 			bool enemy_a = military::can_use_cb_against(state, from, n.id);
	// 			bool enemy_b = military::can_use_cb_against(state, target, n.id);
	// 			if(enemy_a || enemy_b)
	// 				return true;
	// 		}
	// 	}
	// }
	return false;
}



bool ai_will_accept_alliance(sys::state& state, dcon::nation_id target, dcon::nation_id from) {
	return nations::strategic_statecraft::accepts_alliance(state, target, from);
}

void explain_ai_alliance_reasons(sys::state& state, dcon::nation_id target, text::layout_base& contents, int32_t indent) {

	text::add_line_with_condition(state, contents, "ai_alliance_1", state.world.nation_get_ai_is_threatened(target), indent);

	text::add_line(state, contents, "kierkegaard_1", indent);

	text::add_line_with_condition(state, contents, "ai_alliance_5", ai_has_mutual_enemy(state, state.local_player_nation, target), indent + 15);

	text::add_line(state, contents, "kierkegaard_2", indent);

	text::add_line_with_condition(state, contents, "ai_alliance_2", ai_is_close_enough(state, target, state.local_player_nation), indent + 15);

	text::add_line_with_condition(state, contents, "ai_alliance_3", state.world.nation_get_ai_rival(target) != state.local_player_nation && state.world.nation_get_ai_rival(state.local_player_nation) != target, indent + 15);

	auto target_score = estimate_strength(state, target);
	auto source_score = estimate_strength(state, state.local_player_nation);
	text::add_line_with_condition(state, contents, "ai_alliance_4", std::max<float>(source_score, 1.f) * ally_overestimate >= target_score, indent + 15);

	auto holdscores = false;
	auto natid = state.world.nation_get_identity_from_identity_holder(state.local_player_nation);
	for(auto prov_owner : state.world.nation_get_province_ownership(target)) {
		auto prov = prov_owner.get_province();

		for(auto core : prov.get_core_as_province()) {
			if(core.get_identity() == natid) {
				holdscores = true; // holds our cores
			}
		}
	}

	text::add_line_with_condition(state, contents, "ai_alliance_5", !holdscores, indent + 15);
}

}
