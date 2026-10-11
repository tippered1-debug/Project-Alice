#pragma once

namespace ui {

// True while the KATECHON map HUD replaces the Victoria 2 chrome on the map
// screen (top bar, province window, minimap bar). The old elements are still
// created, because they own the country windows and the hotkeys, but they stop
// drawing themselves. Set KATECHON_CLASSIC_HUD=1 to get the old chrome back.
inline bool map_hud_enabled = true;

}
