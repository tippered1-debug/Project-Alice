#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

#include "dcon_generated.hpp"

// The lines the KATECHON trade and diplomacy modes draw on the map. The HUD
// fills them: the trade mode from the cargo on the way (gui_map_hud_trade.hpp),
// the diplomacy sheet from the ties of the country on it, capital to capital
// (gui_map_hud_diplomacy.hpp). map_state turns them into lines for the
// trade-route shader, which colours each kind.
namespace map::katechon_trade {

enum class lane_kind : uint8_t {
	outgoing = 1, incoming = 2, foreign = 3,
	// the ties of a country, drawn as arcs
	tie_alliance = 4, tie_war = 5, tie_subject = 6, tie_hostile = 7, tie_cordial = 8
};

struct lane {
	dcon::province_id from;
	dcon::province_id to;
	bool sea = false;
	lane_kind kind = lane_kind::foreign;
	float value = 0.f;
	bool arc = false; // a gentle arc from one place to the other, not a route over land or sea
};

inline bool active = false; // the trade or diplomacy mode owns the map's trade lines
inline std::vector<lane> lanes;
inline std::atomic<bool> dirty{ false }; // the lanes changed since they were last built

}
