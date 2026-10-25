// The atlas: the visual language of KATECHON's trade screens. This file is
// included inside the body of ui::map_hud_element (gui_map_hud.hpp), before
// the screens that use it; it is not a header of its own.
//
// Imperial statecraft, set with restraint. A dark body stands over the map;
// the work is laid on a surface one step lighter, and what is sunk into it
// (column heads, wells, tracks) one step darker. Areas are told apart by
// light, type and room, not by frames: a line is never more than a pixel and
// never strong. Brass marks what can be handled and what is chosen, umber and
// steel are the two directions of a flow, and red is spent only on a real
// shortage, never on a whole area.
//
// Two typefaces: the serif of the body font for the names of states, goods
// and sections; the condensed sans of the header font for everything read in
// passing. Figures are set in tabular cells and aligned on the right.

// ---- the colours ----
// The neutrals are graphite, not blue: the colour on a screen belongs to the figures. Three steps of
// type, far enough apart that what is read first and what is read in passing never look alike.
inline static constexpr ogl::color3f atlas_body{ 0.078f, 0.084f, 0.094f }; // #141518: the body of a window
inline static constexpr ogl::color3f atlas_work{ 0.106f, 0.114f, 0.125f }; // #1B1D20: where the work is laid
inline static constexpr ogl::color3f atlas_deep{ 0.059f, 0.064f, 0.071f }; // #0F1012: sunk into the work
inline static constexpr ogl::color3f atlas_hover{ 0.137f, 0.149f, 0.165f }; // #23262A: under the mouse
inline static constexpr ogl::color3f atlas_chosen{ 0.173f, 0.184f, 0.200f }; // #2C2F33: chosen
inline static constexpr ogl::color3f atlas_type{ 0.953f, 0.941f, 0.914f }; // #F3F0E9
inline static constexpr ogl::color3f atlas_type_soft{ 0.624f, 0.643f, 0.667f }; // #9FA4AA
inline static constexpr ogl::color3f atlas_type_faint{ 0.427f, 0.455f, 0.486f }; // #6D747C
inline static constexpr ogl::color3f atlas_accent{ 0.808f, 0.667f, 0.416f }; // #CEAA6A: brass, for what is chosen and what acts
inline static constexpr ogl::color3f atlas_accent_lit{ 0.886f, 0.769f, 0.553f };
inline static constexpr ogl::color3f atlas_export{ 0.749f, 0.537f, 0.420f }; // #BF896B: old copper, a hue away from the brass
inline static constexpr ogl::color3f atlas_import{ 0.478f, 0.608f, 0.718f }; // #7A9BB7: slate
inline static constexpr ogl::color3f atlas_risk{ 0.878f, 0.380f, 0.380f }; // #E06161: a real shortage
inline static constexpr ogl::color3f atlas_caution{ 0.784f, 0.588f, 0.569f }; // a shortage worth watching: the red, quietened
inline static constexpr ogl::color3f atlas_gain{ 0.553f, 0.733f, 0.588f }; // what improves, where it matters
inline static constexpr ogl::color3f atlas_home{ 0.800f, 0.784f, 0.745f }; // supply from home, on a bar
inline static constexpr ogl::color3f atlas_track{ 0.165f, 0.176f, 0.192f }; // the empty part of a bar

// The parts of a whole, in the order of their size: brass, steel, sage, clay, plum, teal; the rest in grey.
inline static constexpr ogl::color3f atlas_parts[] = { { 0.722f, 0.604f, 0.400f }, { 0.471f, 0.627f, 0.694f }, { 0.529f, 0.627f, 0.525f }, { 0.698f, 0.490f, 0.427f },
	{ 0.580f, 0.502f, 0.627f }, { 0.435f, 0.612f, 0.580f } };
inline static constexpr ogl::color3f atlas_parts_rest{ 0.380f, 0.416f, 0.451f };

// A hairline: the type at a low opacity, so that it reads the same on every surface.
void atlas_hair(sys::state& state, float x, float y, float w, float h, float alpha = 0.08f) {
	fill(state, x, y, w, h, atlas_type, alpha);
}
void atlas_rule(sys::state& state, float left, float right, float y, float alpha = 0.08f) {
	atlas_hair(state, left, y, right - left, 1.f, alpha);
}
void atlas_outline(sys::state& state, float x, float y, float w, float h, float alpha = 0.08f) {
	frame(state, x, y, w, h, atlas_type, alpha);
}

// The last press on the HUD, so that what was pressed can be seen to sink.
rect atlas_press_rect;
std::chrono::steady_clock::time_point atlas_press_time{};
void atlas_note_press(hit_target const& t) {
	atlas_press_rect = rect{ t.x, t.y, t.w, t.h };
	atlas_press_time = std::chrono::steady_clock::now();
}
bool atlas_pressed(hit_target const& t) const {
	return std::chrono::steady_clock::now() - atlas_press_time < std::chrono::milliseconds(170) && std::abs(atlas_press_rect.x - t.x) < 0.5f
		&& std::abs(atlas_press_rect.y - t.y) < 0.5f && std::abs(atlas_press_rect.w - t.w) < 0.5f;
}

// ---- surfaces ----

// A window: the body over the map and one faint edge, a shade lighter along its top. It claims the mouse.
void atlas_window(sys::state& state, float x, float y, float w, float h) {
	fill(state, x, y, w, h, atlas_body, 0.985f);
	atlas_outline(state, x, y, w, h, 0.10f);
	fill(state, x, y, w, 1.f, atlas_type, 0.16f);
	panels.push_back(rect{ x, y, w, h });
}
// The working surface, a step lighter than the body; no frame.
void atlas_panel(sys::state& state, float x, float y, float w, float h, ogl::color3f face = atlas_work) {
	fill(state, x, y, w, h, face, 1.f);
}

// ---- type ----
float atlas_serif(sys::state& state, std::string_view content, float x, float cy, int32_t size, ogl::color3f colour, bool align_right = false) {
	return draw_text(state, content, x, cy, size, 0.004f, colour, align_right);
}
// A small heading of capitals, tracked, for the label over a figure or a group.
float atlas_label(sys::state& state, std::string const& content, float x, float cy, ogl::color3f colour = atlas_type_faint, bool align_right = false) {
	return sans(state, upper(content), x, cy, 13, colour, align_right, 0.06f);
}

// Figures in tabular cells: every digit takes the width of the widest, so that columns of figures line
// up digit under digit; set from the right edge unless told otherwise. Returns the width.
static size_t atlas_char_len(uint8_t c) {
	return c >= 0xF0 ? 4 : (c >= 0xE0 ? 3 : (c >= 0xC0 ? 2 : 1));
}
float atlas_figure_w(sys::state& state, std::string_view content, int32_t size) {
	auto const cell = sans_w(state, "0", size, 0.f);
	auto w = 0.f;
	for(size_t i = 0; i < content.size();) {
		auto const c = uint8_t(content[i]);
		auto const len = std::min(atlas_char_len(c), content.size() - i);
		w += c >= '0' && c <= '9' ? cell : sans_w(state, content.substr(i, len), size, 0.f);
		i += len;
	}
	return w;
}
float atlas_figure(sys::state& state, std::string_view content, float x, float cy, int32_t size, ogl::color3f colour, bool align_right = true) {
	if(content.empty())
		return 0.f;
	auto const cell = sans_w(state, "0", size, 0.f);
	auto const total = atlas_figure_w(state, content, size);
	auto at = align_right ? x - total : x;
	for(size_t i = 0; i < content.size();) {
		auto const c = uint8_t(content[i]);
		auto const len = std::min(atlas_char_len(c), content.size() - i);
		auto const piece = content.substr(i, len);
		if(c >= '0' && c <= '9') {
			sans(state, piece, std::round(at + (cell - sans_w(state, piece, size, 0.f)) * 0.5f), cy, size, colour, false, 0.f);
			at += cell;
		} else {
			at += sans(state, piece, at, cy, size, colour, false, 0.f);
		}
		i += len;
	}
	return total;
}

// ---- marks ----
void atlas_flag(sys::state& state, dcon::nation_id n, float x, float y, float w, float h) {
	if(auto const handle = flag_of(state, n); handle)
		ogl::render_textured_rect(state, ogl::color_modification::none, x, y, w, h, handle, ui::rotation::upright, false, false);
	atlas_outline(state, x, y, w, h, 0.14f);
}

// A small solid diamond: the mark of a finding that asks for attention.
void atlas_lozenge(sys::state& state, float cx, float cy, int32_t r, ogl::color3f colour) {
	for(int32_t i = -r; i <= r; ++i) {
		auto const half = float(r - std::abs(i));
		fill(state, std::round(cx - half), std::round(cy) + float(i), 2.f * half + 1.f, 1.f, colour, 1.f);
	}
}

// A thin bar on a track: a share of a whole at a glance, square-ended, a few pixels high.
void atlas_bar(sys::state& state, float x, float cy, float w, float part, ogl::color3f colour, float h = 6.f, bool track = true) {
	auto const y = std::round(cy - h * 0.5f);
	if(track)
		fill(state, x, y, w, h, atlas_track, 1.f);
	auto const lit = std::round(w * std::clamp(part, 0.f, 1.f));
	if(lit >= 1.f)
		fill(state, x, y, lit, h, colour, 1.f);
}

// A line through points, laid a pixel column at a time.
void atlas_polyline(sys::state& state, std::vector<std::pair<float, float>> const& points, ogl::color3f colour, float thickness) {
	for(size_t k = 0; k + 1 < points.size(); ++k) {
		auto const [ax, ay] = points[k];
		auto const [bx, by] = points[k + 1];
		if(bx <= ax)
			continue;
		for(auto px = std::floor(ax); px < std::ceil(bx); px += 1.f) {
			auto const t0 = std::clamp((px - ax) / (bx - ax), 0.f, 1.f);
			auto const t1 = std::clamp((px + 1.f - ax) / (bx - ax), 0.f, 1.f);
			auto const y0 = ay + (by - ay) * t0;
			auto const y1 = ay + (by - ay) * t1;
			auto const lo = std::min(y0, y1) - thickness * 0.5f;
			auto const hi = std::max(y0, y1) + thickness * 0.5f;
			fill(state, px, lo, 1.f, hi - lo, colour, 1.f);
		}
	}
}

// ---- controls ----

// A row of words to choose between: the chosen one in full type with a brass line under it, the
// others quiet until the mouse comes. No plates and no frames: the serif for the main navigation of a
// screen, the sans for its tools. Returns the width.
float atlas_toggle_w(sys::state& state, std::string const& label, int32_t size, bool serif) {
	return serif ? shaped(state, label, size, 0.004f).width : sans_w(state, label, size);
}
float atlas_toggles_w(sys::state& state, std::vector<std::pair<std::string, int32_t>> const& items, int32_t size, bool serif, float gap) {
	auto w = 0.f;
	for(auto const& [label, value] : items)
		w += std::round(atlas_toggle_w(state, label, size, serif)) + gap;
	return items.empty() ? 0.f : w - gap;
}
float atlas_toggles(sys::state& state, float x, float cy, std::vector<std::pair<std::string, int32_t>> const& items, int32_t chosen, action act, bool hovering, float mx, float my, int32_t size, bool serif,
	float gap) {
	auto at = x;
	for(auto const& [label, value] : items) {
		auto const lw = std::round(atlas_toggle_w(state, label, size, serif));
		hit_target t{ at - gap * 0.5f, cy - float(size) * 0.5f - 9.f, lw + gap, float(size) + 18.f, act, nullptr, value };
		auto const on = value == chosen;
		auto const hovered = !on && hovering && over(t, mx, my);
		auto const colour = on ? atlas_type : (hovered ? atlas_type_soft : atlas_type_faint);
		if(serif)
			atlas_serif(state, label, at, cy, size, colour);
		else
			sans(state, label, at, cy, size, colour);
		auto const line_y = std::round(cy + float(size) * 0.62f + (serif ? 6.f : 4.f));
		if(on)
			fill(state, at, line_y, lw, 2.f, atlas_accent, 1.f);
		else if(hovered)
			fill(state, at, line_y, lw, 1.f, atlas_type_faint, 0.8f);
		if(!on)
			targets.push_back(t);
		at += lw + gap;
	}
	return at - gap - x;
}

// A flow: a band of one thickness from one place to another along a smooth S, laid as a strip of
// triangles; the S runs flat out of both ends. Its meshes are kept and filled again every frame.
std::vector<std::unique_ptr<ogl::generic_ui_mesh_triangle_strip>> atlas_band_meshes;
size_t atlas_bands_used = 0;
void atlas_band(sys::state& state, float x0, float top0, float x1, float top1, float thickness, ogl::color3f colour, float alpha) {
	if(x1 <= x0 + 1.f || thickness <= 0.f)
		return;
	constexpr uint32_t samples = 40;
	if(atlas_bands_used >= atlas_band_meshes.size())
		atlas_band_meshes.push_back(std::make_unique<ogl::generic_ui_mesh_triangle_strip>(samples * 2));
	auto& mesh = *atlas_band_meshes[atlas_bands_used++];
	auto const thick = std::max(thickness, 1.f);
	auto const by = std::min(top0, top1);
	auto const bh = std::abs(top1 - top0) + thick;
	float v[samples * 4];
	for(uint32_t i = 0; i < samples; ++i) {
		auto const t = float(i) / float(samples - 1);
		auto const s = t * t * t * (t * (t * 6.f - 15.f) + 10.f); // flat at both ends, steep only in the middle
		auto const yt = (top0 + (top1 - top0) * s - by) / bh;
		v[i * 4 + 0] = 2.f * t - 1.f;
		v[i * 4 + 1] = 2.f * yt - 1.f;
		v[i * 4 + 2] = 2.f * t - 1.f;
		v[i * 4 + 3] = 2.f * (yt + thick / bh) - 1.f;
	}
	mesh.set_coords(v);
	glBindVertexArray(state.open_gl.global_square_vao);
	mesh.bind_buffer();
	glUniform4f(state.open_gl.ui_shader_d_rect_uniform, x0, by, x1 - x0, bh);
	glUniform2ui(state.open_gl.ui_shader_subroutines_index_uniform, ogl::parameters::enabled, ogl::parameters::alpha_color);
	glUniform3f(state.open_gl.ui_shader_inner_color_uniform, colour.r, colour.g, colour.b);
	glUniform1f(state.open_gl.ui_shader_border_size_uniform, alpha);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, GLsizei(mesh.count));
}

// A route: a band of one thickness that runs flat out of its start, crosses on one straight slope
// through the middle part of its length and runs flat into its end, its two bends turned on a short
// radius. Drawn on the meshes of atlas_band. `from` and `to` are where the slope starts and ends, as
// parts of the length; `round` is how far each bend is spread, as a part of the length.
void atlas_route(sys::state& state, float x0, float top0, float x1, float top1, float thickness, ogl::color3f colour, float alpha, float from = 0.34f, float to = 0.66f, float round = 0.06f) {
	if(x1 <= x0 + 1.f || thickness <= 0.f)
		return;
	constexpr uint32_t samples = 48;
	if(atlas_bands_used >= atlas_band_meshes.size())
		atlas_band_meshes.push_back(std::make_unique<ogl::generic_ui_mesh_triangle_strip>(samples * 2));
	auto& mesh = *atlas_band_meshes[atlas_bands_used++];
	auto const thick = std::max(thickness, 1.f);
	auto const by = std::min(top0, top1);
	auto const bh = std::abs(top1 - top0) + thick;
	// the slope as a trapezoid of steepness, its edges spread by the bends; its running sum is the route
	auto const steep = [&](float t) {
		auto const rise = std::clamp((t - (from - round)) / (2.f * round), 0.f, 1.f);
		auto const fall = std::clamp(((to + round) - t) / (2.f * round), 0.f, 1.f);
		return std::min(rise, fall);
	};
	float s[samples];
	auto sum = 0.f;
	s[0] = 0.f;
	for(uint32_t i = 1; i < samples; ++i) {
		auto const t = (float(i) - 0.5f) / float(samples - 1);
		sum += steep(t);
		s[i] = sum;
	}
	float v[samples * 4];
	for(uint32_t i = 0; i < samples; ++i) {
		auto const t = float(i) / float(samples - 1);
		auto const k = sum > 0.f ? s[i] / sum : t;
		auto const yt = (top0 + (top1 - top0) * k - by) / bh;
		v[i * 4 + 0] = 2.f * t - 1.f;
		v[i * 4 + 1] = 2.f * yt - 1.f;
		v[i * 4 + 2] = 2.f * t - 1.f;
		v[i * 4 + 3] = 2.f * (yt + thick / bh) - 1.f;
	}
	mesh.set_coords(v);
	glBindVertexArray(state.open_gl.global_square_vao);
	mesh.bind_buffer();
	glUniform4f(state.open_gl.ui_shader_d_rect_uniform, x0, by, x1 - x0, bh);
	glUniform2ui(state.open_gl.ui_shader_subroutines_index_uniform, ogl::parameters::enabled, ogl::parameters::alpha_color);
	glUniform3f(state.open_gl.ui_shader_inner_color_uniform, colour.r, colour.g, colour.b);
	glUniform1f(state.open_gl.ui_shader_border_size_uniform, alpha);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, GLsizei(mesh.count));
}

enum class atlas_kind : uint8_t {
	primary, // the action of a place: the chosen surface, a brass edge, full type
	quiet // a secondary action: words and a mark, a surface only under the mouse
};

// A button. It never wears a frame: it is told by its surface, and it sinks a pixel when pressed.
bool atlas_button(sys::state& state, float x, float y, float w, float h, std::string const& label, hit_target t, bool hovering, float mx, float my, atlas_kind kind, icon lead = icon::count) {
	t.x = x;
	t.y = y;
	t.w = w;
	t.h = h;
	auto const hovered = hovering && over(t, mx, my);
	auto const pressed = atlas_pressed(t);
	auto const sink = pressed ? 1.f : 0.f;
	if(kind == atlas_kind::primary) {
		fill(state, x, y + sink, w, h - sink, hovered ? mix(atlas_chosen, atlas_type, 0.06f) : atlas_chosen, 1.f);
		fill(state, x, y + sink, 2.f, h - sink, atlas_accent, 1.f);
	} else if(hovered || pressed) {
		fill(state, x, y + sink, w, h - sink, atlas_hover, 1.f);
	}
	auto const lw = sans_w(state, label, 14);
	auto const icon_w = lead == icon::count ? 0.f : 18.f;
	auto const lx = std::round(x + (w - lw - icon_w) * 0.5f);
	auto const cy = y + sink + h * 0.5f - 1.f;
	if(lead != icon::count)
		draw_icon(state, lead, lx + 6.f, cy + 1.f, 10.f, hovered || kind == atlas_kind::primary ? atlas_accent_lit : atlas_type_faint);
	sans(state, label, lx + icon_w, cy, 14, kind == atlas_kind::primary || hovered ? atlas_type : atlas_type_soft);
	targets.push_back(t);
	return hovered;
}

// A mark that does something: no plate at rest, a surface under the mouse, the chosen surface when on.
bool atlas_icon_button(sys::state& state, float x, float y, float size, icon mark, hit_target t, bool hovering, float mx, float my, std::string const& note = std::string(), bool on = false) {
	t.x = x;
	t.y = y;
	t.w = size;
	t.h = size;
	auto const hovered = hovering && over(t, mx, my);
	auto const pressed = atlas_pressed(t);
	if(on)
		fill(state, x, y, size, size, atlas_chosen, 1.f);
	else if(hovered || pressed)
		fill(state, x, y + (pressed ? 1.f : 0.f), size, size, atlas_hover, 1.f);
	draw_icon(state, mark, x + size * 0.5f, y + size * 0.5f + (pressed ? 1.f : 0.f), std::round(size * 0.42f), on ? atlas_accent_lit : (hovered ? atlas_type : atlas_type_soft));
	if(hovered && !note.empty())
		hint(note, t);
	targets.push_back(t);
	return hovered;
}

// Words that do something, in brass, underlined under the mouse.
bool atlas_link(sys::state& state, std::string const& content, float x, float cy, hit_target t, bool hovering, float mx, float my, bool align_right = false) {
	auto const tw = sans_w(state, content, 14);
	auto const lx = align_right ? x - tw : x;
	t.x = lx - 4.f;
	t.y = cy - 11.f;
	t.w = tw + 8.f;
	t.h = 22.f;
	auto const hovered = hovering && over(t, mx, my);
	sans(state, content, lx, cy, 14, hovered ? atlas_accent_lit : atlas_accent);
	if(hovered)
		fill(state, lx, cy + 8.f, tw, 1.f, atlas_accent_lit, 0.9f);
	targets.push_back(t);
	return hovered;
}

// The close mark in the corner of a window.
void atlas_close(sys::state& state, float x, float y, action act, bool hovering, float mx, float my) {
	atlas_icon_button(state, x, y, 28.f, icon::close, hit_target{ 0.f, 0.f, 0.f, 0.f, act }, hovering, mx, my);
}

// A row of a list: the hover surface under the mouse; chosen, the chosen surface warmed with brass and a
// brass line down its left side; a short brass line alone when it belongs to what is chosen elsewhere.
void atlas_row(sys::state& state, hit_target const& t, bool hovered, bool chosen, bool related) {
	if(chosen) {
		fill(state, t.x, t.y, t.w, t.h, mix(atlas_chosen, atlas_accent, 0.10f), 1.f);
		fill(state, t.x, t.y, 3.f, t.h, atlas_accent, 1.f);
	} else if(hovered || atlas_pressed(t)) {
		fill(state, t.x, t.y, t.w, t.h, atlas_hover, 1.f);
	} else if(related) {
		fill(state, t.x, t.y + 10.f, 2.f, t.h - 20.f, atlas_accent, 0.8f);
	}
}
