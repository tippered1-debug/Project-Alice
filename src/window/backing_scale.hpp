#pragma once

namespace window {

// Framebuffer pixels per window point: 2 on a Retina display, 1 elsewhere.
// user_settings.ui_scale holds the scale in framebuffer pixels, that is the
// scale the player chose times this factor, so that the interface keeps its
// physical size while everything is drawn at the display's native resolution.
inline float backing_scale = 1.f;

}
