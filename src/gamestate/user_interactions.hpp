#pragma once

namespace sys {
void on_mouse_wheel(sys::state& state, int32_t x, int32_t y, key_modifiers mod, float amount); // an amount of 1.0 is one "click" of the wheel
void process_dialog_boxes(sys::state& state);
void process_errorpopup_boxes(sys::state& state);
void open_diplomacy_window(sys::state& state, dcon::nation_id target);
// The diplomacy screen of the KATECHON shell. When it is set, every way into
// diplomacy (the ribbon, a flag, a country's name) opens it instead of the old window.
inline void (*diplomacy_screen)(sys::state& state, dcon::nation_id target) = nullptr;
// The state finances sheet of the map HUD opens or closes; set while the HUD exists.
inline void (*finance_screen)(sys::state& state) = nullptr;
inline bool (*finance_screen_open)() = nullptr;
// A right press on a country of the map with nothing chosen to command: when
// set, it offers what can be done with that country there and then.
inline void (*diplomacy_context)(sys::state& state, dcon::nation_id target, dcon::province_id where) = nullptr;
}
