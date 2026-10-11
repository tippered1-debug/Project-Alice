#pragma once

#include <atomic>

namespace game_scene {

// Set by the UI when the player leaves a running game for the title screen;
// the stop-game command finishes the trip once the simulation has halted.
inline std::atomic<bool> return_to_title_requested{ false };
// True once a game has been played in this world, so that "New Game" starts
// from the scenario again instead of from wherever the last session stopped.
inline bool world_played_since_load = false;

}
