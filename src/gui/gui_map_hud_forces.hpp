#pragma once

#include <cstdint>
#include <vector>

namespace ui {

// The canonical land formations the player has chosen, on the map or in the
// military panel of the KATECHON HUD, by stable id. The right button on the
// map sends them where it is pressed, each with everything under it.
inline std::vector<uint64_t> chosen_formations;

}
