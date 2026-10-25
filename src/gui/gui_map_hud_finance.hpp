// The state finances sheet of the KATECHON HUD, included in the body of
// map_hud_element. It takes the whole window right of the ribbon: the books
// of the state (governance::finance::report) as five figures, a chart of the
// period, the tables of revenue and spending, the fiscal law and the public
// debt, with a bar of sections under them. Nothing on it is drawn from the
// old budget fields: every figure is a canonical flow, balance or obligation.
//
// The art is assets/katechon_fin_*.png (tools/katechon_ui/build_finance.py):
// navy plates in one thin gilt line with engraved corners, gilt buttons, the
// medallions, the slider knob and one strip of engraved gold icons. Plates are
// drawn in nine pieces without alpha; icons, knob, glow and corners with it.

// The cells of assets/katechon_fin_icons.png, in the order the tool draws them.
enum class fin_icon : int32_t {
	coin_stack, scales, coins, facade, document, bank_coin, progressivity, person_one, person_two, person_crown,
	book, shield, aqueduct, coin_eagle, coin_gem, seal_scroll, shield_coin, factory, ornament, swords, people,
	coins_doc, percent, calendar, crest, gavel, bars, line_chart, chevron_down, chevron_left, shield_ceiling,
	close, plus, minus, dot, ring, debt_coin, count
};
enum class fin_page_kind : int32_t { overview = 0, revenue, spending, debt, policy, count };

bool finance_open = false;
bool fin_art_read = false;
GLuint fin_icons = 0, fin_panel = 0, fin_card = 0, fin_inset = 0, fin_row = 0, fin_button = 0, fin_button_hover = 0,
	fin_tab = 0, fin_nav = 0, fin_nav_on = 0, fin_medallion = 0, fin_knob = 0, fin_glow = 0, fin_damask = 0,
	fin_header = 0, fin_corner = 0, fin_flourish = 0;
fin_page_kind fin_page = fin_page_kind::overview;
governance::finance::fiscal_period fin_period = governance::finance::fiscal_period::days_30;
governance::finance::fiscal_step fin_step = governance::finance::fiscal_step::daily;
bool fin_bars = true; // the chart as bars; otherwise as lines
bool fin_step_menu = false;
governance::finance::fiscal_report fin_rep;
int32_t fin_rep_day = -1;
int32_t fin_rep_frames = 0;
dcon::nation_id fin_rep_nation;
bool fin_rep_stale = true;
// The proposal on the sliders: base rate, progressivity, and the shares of
// education, public order, infrastructure and local government.
std::array<float, 6> fin_draft{};
bool fin_draft_set = false;
int32_t fin_drag = -1; // the slider held, while the button is down
std::array<rect, 6> fin_tracks{};
int32_t fin_bond_choice = 1; // of fin_bond_shares
int32_t fin_tenor_choice = 1; // of fin_tenors
int32_t fin_notice_frames = 0; // a bill or a debt operation was ordered; its outcome is shown for a while
bool fin_notice_bill = false;
int32_t fin_qa_frames = -1;
inline static constexpr std::array<float, 4> fin_bond_shares{ 0.02f, 0.05f, 0.10f, 0.25f }; // of annual revenue
inline static constexpr std::array<uint16_t, 4> fin_tenors{ 365, 1095, 1825, 3650 };

std::vector<std::pair<uint32_t, std::unique_ptr<ogl::generic_ui_mesh_triangle_strip>>> fin_meshes;
size_t fin_meshes_used = 0;

inline static constexpr ogl::color3f fin_type{ 0.910f, 0.863f, 0.780f }; // #E8DCC7
inline static constexpr ogl::color3f fin_title_type{ 0.875f, 0.800f, 0.745f };
inline static constexpr ogl::color3f fin_muted{ 0.612f, 0.663f, 0.722f }; // #9CA9B8
inline static constexpr ogl::color3f fin_quiet{ 0.420f, 0.470f, 0.530f };
inline static constexpr ogl::color3f fin_gold{ 0.780f, 0.639f, 0.416f }; // #C7A36A
inline static constexpr ogl::color3f fin_gold_light{ 0.878f, 0.765f, 0.557f }; // #E0C38E
inline static constexpr ogl::color3f fin_green{ 0.396f, 0.718f, 0.620f }; // #65B79E
inline static constexpr ogl::color3f fin_green_text{ 0.470f, 0.780f, 0.560f };
inline static constexpr ogl::color3f fin_spend{ 0.788f, 0.620f, 0.380f }; // #C99E61
inline static constexpr ogl::color3f fin_coral{ 0.850f, 0.420f, 0.430f };
inline static constexpr ogl::color3f fin_violet{ 0.718f, 0.514f, 0.827f }; // #B783D3
inline static constexpr ogl::color3f fin_red{ 0.812f, 0.451f, 0.475f }; // #CF7379
inline static constexpr ogl::color3f fin_wall{ 0.086f, 0.082f, 0.118f };
inline static constexpr ogl::color3f fin_deep{ 0.035f, 0.059f, 0.106f }; // #090F1B
inline static constexpr ogl::color3f fin_grid{ 0.200f, 0.250f, 0.320f };
inline static constexpr ogl::color3f fin_track{ 0.090f, 0.118f, 0.165f };
inline static constexpr ogl::color3f fin_line{ 0.690f, 0.549f, 0.337f };
inline static constexpr ogl::color3f fin_black{ 0.012f, 0.016f, 0.027f };

// ---- art ----

void fin_load(sys::state& state) {
	fin_art_read = true;
	auto root = simple_fs::get_root(state.common_fs);
	auto assets = simple_fs::open_directory(root, NATIVE("assets"));
	fin_icons = ogl::make_gl_texture(assets, NATIVE("katechon_fin_icons.png"));
	fin_panel = ogl::make_gl_texture(assets, NATIVE("katechon_fin_panel.png"));
	fin_card = ogl::make_gl_texture(assets, NATIVE("katechon_fin_card.png"));
	fin_inset = ogl::make_gl_texture(assets, NATIVE("katechon_fin_inset.png"));
	fin_row = ogl::make_gl_texture(assets, NATIVE("katechon_fin_row.png"));
	fin_button = ogl::make_gl_texture(assets, NATIVE("katechon_fin_button.png"));
	fin_button_hover = ogl::make_gl_texture(assets, NATIVE("katechon_fin_button_hover.png"));
	fin_tab = ogl::make_gl_texture(assets, NATIVE("katechon_fin_tab.png"));
	fin_nav = ogl::make_gl_texture(assets, NATIVE("katechon_fin_nav.png"));
	fin_nav_on = ogl::make_gl_texture(assets, NATIVE("katechon_fin_nav_on.png"));
	fin_medallion = ogl::make_gl_texture(assets, NATIVE("katechon_fin_medallion.png"));
	fin_knob = ogl::make_gl_texture(assets, NATIVE("katechon_fin_knob.png"));
	fin_glow = ogl::make_gl_texture(assets, NATIVE("katechon_fin_glow.png"));
	fin_damask = ogl::make_gl_texture(assets, NATIVE("katechon_fin_damask.png"));
	fin_header = ogl::make_gl_texture(assets, NATIVE("katechon_fin_header.png"));
	fin_corner = ogl::make_gl_texture(assets, NATIVE("katechon_fin_corner.png"));
	fin_flourish = ogl::make_gl_texture(assets, NATIVE("katechon_fin_flourish.png"));
	// Icons and ornaments are drawn far smaller than they are painted: mipmaps keep their lines clean.
	for(auto const texture : { fin_icons, fin_medallion, fin_knob, fin_glow, fin_corner, fin_flourish }) {
		if(!texture)
			continue;
		glBindTexture(GL_TEXTURE_2D, texture);
		glGenerateMipmap(GL_TEXTURE_2D);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glBindTexture(GL_TEXTURE_2D, 0);
	}
	for(auto const texture : { fin_panel, fin_card, fin_inset, fin_row, fin_button, fin_button_hover, fin_tab, fin_nav, fin_nav_on, fin_damask, fin_header }) {
		if(!texture)
			continue;
		glBindTexture(GL_TEXTURE_2D, texture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glBindTexture(GL_TEXTURE_2D, 0);
	}
}

void fin_draw_icon(sys::state& state, fin_icon which, float cx, float cy, float size, ogl::color3f tint = ogl::color3f{ 1.f, 1.f, 1.f }) {
	if(!fin_icons)
		return;
	ogl::render_tinted_subsprite(state, int(which), int(fin_icon::count), std::round(cx - size * 0.5f), std::round(cy - size * 0.5f), size, size,
		tint.r, tint.g, tint.b, fin_icons, ui::rotation::upright, false, false);
}

void fin_sprite(sys::state& state, GLuint texture, float x, float y, float w, float h, ogl::color3f tint = ogl::color3f{ 1.f, 1.f, 1.f }) {
	if(texture)
		ogl::render_tinted_textured_rect(state, x, y, w, h, tint.r, tint.g, tint.b, texture, ui::rotation::upright, false, false);
}

// A board of the sheet (a panel or a figure) with the shadow it throws.
void fin_board(sys::state& state, GLuint texture, float x, float y, float w, float h) {
	fill(state, x + 2.f, y + 4.f, w, h, fin_black, 0.35f);
	fill(state, x - 1.f, y + h, w + 2.f, 2.f, fin_black, 0.25f);
	nine(state, texture, x, y, w, h, 160.f, 160.f, 24.f, 12.f);
}
void fin_plate(sys::state& state, GLuint texture, float x, float y, float w, float h, float image_w, float image_h, float corner_px, float corner) {
	nine(state, texture, x, y, w, h, image_w, image_h, corner_px, corner);
}

// ---- type ----

float fin_text(sys::state& state, std::string_view content, float x, float cy, int32_t size, ogl::color3f colour, bool align_right = false, float tracking = 0.01f) {
	if(content.empty())
		return 0.f;
	return draw_text(state, content, x, cy, size, tracking, colour, align_right);
}
float fin_w(sys::state& state, std::string_view content, int32_t size, float tracking = 0.01f) {
	return content.empty() ? 0.f : shaped(state, content, size, tracking).width;
}
// Figures and headings that sit on art keep a soft shadow under them.
float fin_lifted(sys::state& state, std::string_view content, float x, float cy, int32_t size, ogl::color3f colour, bool align_right = false, float tracking = 0.01f) {
	fin_text(state, content, x + 1.f, cy + 2.f, size, fin_black, align_right, tracking);
	return fin_text(state, content, x, cy, size, colour, align_right, tracking);
}
float fin_caps(sys::state& state, std::string const& content, float x, float cy, int32_t size, ogl::color3f colour, float tracking = 0.12f) {
	return fin_text(state, upper(content), x, cy, size, colour, false, tracking);
}
float fin_caps_w(sys::state& state, std::string const& content, int32_t size, float tracking = 0.12f) {
	return fin_w(state, upper(content), size, tracking);
}
std::string fin_fit(sys::state& state, std::string content, int32_t size, float max_w) {
	return fit(state, std::move(content), size, 0.01f, max_w);
}

static std::string fin_money(float value, bool sign = false) {
	if(!std::isfinite(value))
		value = 0.f;
	auto const magnitude = std::abs(value);
	char buffer[64];
	if(magnitude < 1000.f)
		std::snprintf(buffer, sizeof(buffer), "%.1f", magnitude);
	else if(magnitude < 1.0e6f)
		std::snprintf(buffer, sizeof(buffer), "%.2fK", magnitude / 1.0e3f);
	else if(magnitude < 1.0e9f)
		std::snprintf(buffer, sizeof(buffer), "%.2fM", magnitude / 1.0e6f);
	else if(magnitude < 1.0e12f)
		std::snprintf(buffer, sizeof(buffer), "%.2fB", magnitude / 1.0e9f);
	else
		std::snprintf(buffer, sizeof(buffer), "%.2fT", magnitude / 1.0e12f);
	std::string result = value < 0.f && magnitude >= 0.05f ? "-" : (sign && magnitude >= 0.05f ? "+" : "");
	return result + buffer + " \xC2\xA3";
}
static std::string fin_pct(float part, int digits = 1, bool sign = false) {
	if(!std::isfinite(part))
		part = 0.f;
	char buffer[32];
	std::snprintf(buffer, sizeof(buffer), digits == 0 ? "%.0f%%" : "%.1f%%", std::abs(part) * 100.f);
	auto const shown = std::abs(part) * 100.f >= (digits == 0 ? 0.5f : 0.05f);
	return std::string(part < 0.f && shown ? "-" : (sign && shown ? "+" : "")) + buffer;
}
// The axis of the chart: whole numbers, then thousands.
static std::string fin_axis(float value) {
	char buffer[32];
	auto const magnitude = std::abs(value);
	if(magnitude >= 1.0e6f)
		std::snprintf(buffer, sizeof(buffer), "%gM", std::round(value / 1.0e5f) / 10.f);
	else if(magnitude >= 1.0e3f)
		std::snprintf(buffer, sizeof(buffer), "%gK", std::round(value / 1.0e2f) / 10.f);
	else if(magnitude >= 10.f || magnitude == 0.f)
		std::snprintf(buffer, sizeof(buffer), "%.0f", value);
	else
		std::snprintf(buffer, sizeof(buffer), "%.1f", value);
	return buffer;
}

static sys::date fin_date(int32_t raw) {
	return raw > 0 ? sys::date{ sys::date::value_base_t(raw - 1) } : sys::date{};
}
static std::string fin_month(sys::state& state, int32_t month, bool full = false) {
	static constexpr std::array<char const*, 12> brief{ "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	static constexpr std::array<char const*, 12> whole{ "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
	auto const m = std::clamp(month, 1, 12) - 1;
	return full ? localized(state, "katechon_fin_month_" + std::to_string(m + 1), whole[m])
		: localized(state, "katechon_fin_mon_" + std::to_string(m + 1), brief[m]);
}
// "Jan 17", or "Jan 17, 2010" with the year.
static std::string fin_day(sys::state& state, int32_t raw, bool year = false) {
	if(raw <= 0)
		return {};
	auto ymd = fin_date(raw).to_ymd(state.start_date);
	auto text = fin_month(state, ymd.month) + " " + std::to_string(ymd.day);
	return year ? text + ", " + std::to_string(ymd.year) : text;
}
static std::string fin_day(sys::state& state, sys::date date, bool year = true) {
	return date ? fin_day(state, date.to_raw_value(), year) : std::string{};
}

// ---- drawing ----

// A strip of triangles along a polyline, `width` thick, alpha-blended; drawn
// twice, the second time wider and fainter, so that its edges are soft.
ogl::generic_ui_mesh_triangle_strip& fin_mesh(uint32_t vertices) {
	for(size_t i = fin_meshes_used; i < fin_meshes.size(); ++i) {
		if(fin_meshes[i].first == vertices) {
			std::swap(fin_meshes[i], fin_meshes[fin_meshes_used]);
			return *fin_meshes[fin_meshes_used++].second;
		}
	}
	fin_meshes.emplace_back(vertices, std::make_unique<ogl::generic_ui_mesh_triangle_strip>(vertices));
	std::swap(fin_meshes.back(), fin_meshes[fin_meshes_used]);
	return *fin_meshes[fin_meshes_used++].second;
}
void fin_draw_strip(sys::state& state, std::vector<float>& coords, rect bounds, ogl::color3f colour, float alpha) {
	auto& mesh = fin_mesh(uint32_t(coords.size() / 2));
	mesh.set_coords(coords.data());
	glBindVertexArray(state.open_gl.global_square_vao);
	mesh.bind_buffer();
	glUniform4f(state.open_gl.ui_shader_d_rect_uniform, bounds.x, bounds.y, bounds.w, bounds.h);
	glUniform2ui(state.open_gl.ui_shader_subroutines_index_uniform, ogl::parameters::enabled, ogl::parameters::alpha_color);
	glUniform3f(state.open_gl.ui_shader_inner_color_uniform, colour.r, colour.g, colour.b);
	glUniform1f(state.open_gl.ui_shader_border_size_uniform, alpha);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, GLsizei(mesh.count));
}
void fin_polyline(sys::state& state, std::vector<std::pair<float, float>> const& points, float width, ogl::color3f colour, float alpha) {
	if(points.size() < 2)
		return;
	float x0 = points[0].first, x1 = x0, y0 = points[0].second, y1 = y0;
	for(auto const& [x, y] : points) {
		x0 = std::min(x0, x);
		x1 = std::max(x1, x);
		y0 = std::min(y0, y);
		y1 = std::max(y1, y);
	}
	rect bounds{ x0 - 4.f, y0 - 4.f, x1 - x0 + 8.f, y1 - y0 + 8.f };
	for(int32_t pass = 0; pass < 2; ++pass) {
		auto const half = (pass == 0 ? width + 1.2f : width) * 0.5f;
		std::vector<float> coords;
		coords.reserve(points.size() * 4);
		for(size_t i = 0; i < points.size(); ++i) {
			auto const& prev = points[i == 0 ? 0 : i - 1];
			auto const& next = points[i + 1 < points.size() ? i + 1 : i];
			auto dx = next.first - prev.first;
			auto dy = next.second - prev.second;
			auto const len = std::max(std::sqrt(dx * dx + dy * dy), 1.0e-4f);
			auto nx = -dy / len, ny = dx / len;
			// keep the band as thick through a bend as on the straight
			if(i > 0 && i + 1 < points.size()) {
				auto ax = points[i].first - prev.first, ay = points[i].second - prev.second;
				auto const al = std::max(std::sqrt(ax * ax + ay * ay), 1.0e-4f);
				auto const cosine = std::abs((-ay / al) * nx + (ax / al) * ny);
				auto const scale = 1.f / std::max(cosine, 0.45f);
				nx *= scale;
				ny *= scale;
			}
			auto const px = points[i].first, py = points[i].second;
			coords.push_back(2.f * (px + nx * half - bounds.x) / bounds.w - 1.f);
			coords.push_back(2.f * (py + ny * half - bounds.y) / bounds.h - 1.f);
			coords.push_back(2.f * (px - nx * half - bounds.x) / bounds.w - 1.f);
			coords.push_back(2.f * (py - ny * half - bounds.y) / bounds.h - 1.f);
		}
		fin_draw_strip(state, coords, bounds, colour, pass == 0 ? alpha * 0.30f : alpha);
	}
}
// The ground under a line, down to `base`; brighter near the line.
void fin_area(sys::state& state, std::vector<std::pair<float, float>> const& points, float base, ogl::color3f colour, float alpha) {
	if(points.size() < 2)
		return;
	float x0 = points.front().first, x1 = points.back().first, top = base;
	for(auto const& p : points)
		top = std::min(top, p.second);
	rect bounds{ x0, top, std::max(x1 - x0, 1.f), std::max(base - top, 1.f) };
	for(int32_t pass = 0; pass < 2; ++pass) {
		std::vector<float> coords;
		coords.reserve(points.size() * 4);
		for(auto const& [x, y] : points) {
			auto const lower = pass == 0 ? base : std::min(base, y + (base - y) * 0.45f);
			coords.push_back(2.f * (x - bounds.x) / bounds.w - 1.f);
			coords.push_back(2.f * (y - bounds.y) / bounds.h - 1.f);
			coords.push_back(2.f * (x - bounds.x) / bounds.w - 1.f);
			coords.push_back(2.f * (lower - bounds.y) / bounds.h - 1.f);
		}
		fin_draw_strip(state, coords, bounds, colour, pass == 0 ? alpha * 0.55f : alpha * 0.8f);
	}
}
void fin_dashed(sys::state& state, float x, float y0, float y1, ogl::color3f colour, float alpha) {
	for(float y = y0; y < y1; y += 6.f)
		fill(state, std::round(x), y, 1.f, std::min(3.f, y1 - y), colour, alpha);
}
void fin_rule(sys::state& state, float x, float y, float w, float alpha = 0.55f) {
	fill(state, x, y, w, 1.f, fin_line, alpha * 0.55f);
	fill(state, x, y + 1.f, w, 1.f, fin_black, alpha * 0.6f);
}
// A bar of a table: a dark groove with its fill, lit along the top.
void fin_groove(sys::state& state, float x, float cy, float w, float h, float part, ogl::color3f colour) {
	auto const y = std::round(cy - h * 0.5f);
	fill(state, x, y, w, h, fin_track, 1.f);
	fill(state, x, y, w, 1.f, fin_black, 0.8f);
	fill(state, x, y + h - 1.f, w, 1.f, fin_grid, 0.35f);
	fill(state, x, y, 1.f, h, fin_grid, 0.30f);
	fill(state, x + w - 1.f, y, 1.f, h, fin_grid, 0.30f);
	auto const fw = std::round(std::clamp(part, 0.f, 1.f) * (w - 2.f));
	if(fw >= 1.f) {
		fill(state, x + 1.f, y + 1.f, fw, h - 2.f, colour, 0.92f);
		fill(state, x + 1.f, y + 1.f, fw, std::max(1.f, (h - 2.f) * 0.45f), mix(colour, ogl::color3f{ 1.f, 1.f, 1.f }, 0.22f), 0.55f);
	}
}

// A button of the sheet: the gilt plate when it acts, the quiet one when it is a choice not taken.
bool fin_button_at(sys::state& state, float x, float y, float w, float h, std::string const& label, fin_icon image, bool gilt, bool enabled,
	hit_target t, bool hovering, float mx, float my, std::string const& tip_text = {}, int32_t size = 15) {
	auto const hovered = hovering && over(t, mx, my);
	auto const texture = gilt ? (hovered && enabled ? fin_button_hover : fin_button) : fin_tab;
	fin_plate(state, texture, x, y, w, h, 96.f, 48.f, 12.f, 6.f);
	if(!gilt && hovered && enabled)
		fill(state, x + 2.f, y + 2.f, w - 4.f, h - 4.f, fin_gold, 0.07f);
	auto const colour = !enabled ? fin_quiet : (gilt ? fin_type : (hovered ? fin_type : fin_muted));
	auto const icon_w = image == fin_icon::count ? 0.f : std::round(h * 0.56f) + 8.f;
	auto const tw = fin_w(state, label, size);
	auto const start = std::round(x + (w - tw - icon_w) * 0.5f);
	if(image != fin_icon::count)
		fin_draw_icon(state, image, start + icon_w * 0.5f - 4.f, y + h * 0.5f, std::round(h * 0.56f), enabled ? ogl::color3f{ 1.f, 1.f, 1.f } : ogl::color3f{ 0.55f, 0.55f, 0.55f });
	fin_text(state, label, start + icon_w, y + h * 0.5f, size, colour);
	t.x = x;
	t.y = y;
	t.w = w;
	t.h = h;
	if(enabled)
		targets.push_back(t);
	if(hovered && !tip_text.empty())
		hint(tip_text, t);
	return hovered;
}

// ---- the books ----

void fin_refresh(sys::state& state, bool force = false) {
	auto const nation = state.local_player_nation;
	auto const today = state.current_date.to_raw_value();
	if(!force && !fin_rep_stale && fin_rep_day == today && fin_rep_nation == nation && --fin_rep_frames > 0)
		return;
	fin_rep = governance::finance::report(state, nation, fin_period, fin_step);
	fin_rep_day = today;
	fin_rep_nation = nation;
	fin_rep_frames = 45;
	fin_rep_stale = false;
	if(!fin_draft_set || (!fin_dirty() && fin_drag < 0)) {
		fin_draft = fin_law();
		fin_draft_set = true;
	}
}
std::array<float, 6> fin_law() const {
	return { fin_rep.base_rate, fin_rep.progressivity, fin_rep.priorities[0], fin_rep.priorities[1], fin_rep.priorities[2], fin_rep.priorities[3] };
}
bool fin_dirty() const {
	auto const law = fin_law();
	for(size_t i = 0; i < law.size(); ++i)
		if(std::abs(law[i] - fin_draft[i]) > 0.0005f)
			return true;
	return false;
}
// One share moves; the others keep their proportions and fill the rest, so the shares stay whole.
void fin_set_share(size_t index, float value) {
	value = std::clamp(value, 0.f, 1.f);
	float others = 0.f;
	for(size_t i = 2; i < 6; ++i)
		if(i != index)
			others += fin_draft[i];
	for(size_t i = 2; i < 6; ++i) {
		if(i == index)
			continue;
		fin_draft[i] = others > 1.0e-5f ? fin_draft[i] * (1.f - value) / others : (1.f - value) / 3.f;
	}
	fin_draft[index] = value;
}
// What the sliders would make the rates of the three strata, by the rules the law uses.
std::array<float, 3> fin_draft_rates(sys::state& state) const {
	governance::policy::position p;
	(void)governance::policy::set(p, governance::policy::topic_id::income_tax, fin_draft[0]);
	(void)governance::policy::set(p, governance::policy::topic_id::progressivity, fin_draft[1]);
	auto rules = governance::policy::rules_for(state, state.local_player_nation, p);
	return { rules.tax_rates[0], rules.tax_rates[1], rules.tax_rates[2] };
}
static constexpr float fin_slider_top(size_t index) {
	return index == 0 ? 0.4f : 1.f; // the base rate of income tax runs from 0 to 40%
}

void open_finance(sys::state& state) {
	close_population(state);
	close_military();
	close_diplomacy(state);
	close_trade(state);
	if(auto* old = state.ui_state.topbar_subwindow; old && old->is_visible())
		old->set_visible(state, false);
	finance_open = true;
	fin_step_menu = false;
	fin_rep_stale = true;
	fin_draft_set = false;
	if(!fin_art_read)
		fin_load(state);
}
void close_finance(sys::state&) {
	finance_open = false;
	fin_step_menu = false;
	fin_drag = -1;
}
void toggle_finance(sys::state& state) {
	if(finance_open)
		close_finance(state);
	else
		open_finance(state);
}

// QA hooks: KATECHON_HUD_FINANCE=<0..4> opens the sheet on that section,
// KATECHON_HUD_FIN_PERIOD=<0..2> on a period, KATECHON_HUD_FIN_LINES=1 the chart as lines,
// KATECHON_HUD_FIN_DRAFT=<base>,<progressivity> moves the tax sliders,
// KATECHON_HUD_FIN_PROPOSE=1 lays the bill, KATECHON_HUD_FIN_BONDS=<choice> offers bonds
// (each after KATECHON_HUD_FIN_AFTER=<frames>, default 60).
void fin_resolve(sys::state& state) {
	sys::finance_screen = [](sys::state& s) {
		if(the_hud)
			the_hud->toggle_finance(s);
	};
	sys::finance_screen_open = []() {
		return the_hud && the_hud->finance_open;
	};
	if(auto const* page = std::getenv("KATECHON_HUD_FINANCE"); page && page[0] >= '0' && page[0] <= '4') {
		fin_page = fin_page_kind(page[0] - '0');
		fin_qa_open = true;
	}
	if(auto const* period = std::getenv("KATECHON_HUD_FIN_PERIOD"); period && period[0] >= '0' && period[0] <= '2') {
		fin_period = governance::finance::fiscal_period(period[0] - '0');
		fin_step = governance::finance::default_step(fin_period);
	}
	if(auto const* lines = std::getenv("KATECHON_HUD_FIN_LINES"); lines && lines[0] == '1')
		fin_bars = false;
	if(auto const* after = std::getenv("KATECHON_HUD_FIN_AFTER"); after && after[0])
		fin_qa_frames = std::atoi(after);
}
bool fin_qa_open = false;
bool fin_qa_done = false;
void fin_qa(sys::state& state) {
	if(fin_qa_open && state.local_player_nation) {
		fin_qa_open = false;
		auto const page = fin_page;
		open_finance(state);
		fin_page = page;
	}
	if(fin_qa_done || !finance_open)
		return;
	if(fin_qa_frames < 0)
		fin_qa_frames = 60;
	if(--fin_qa_frames > 0)
		return;
	fin_qa_done = true;
	if(auto const* draft = std::getenv("KATECHON_HUD_FIN_DRAFT"); draft && draft[0]) {
		float base = fin_draft[0], spread = fin_draft[1];
		std::sscanf(draft, "%f,%f", &base, &spread);
		fin_draft[0] = std::clamp(base, 0.f, 0.4f);
		fin_draft[1] = std::clamp(spread, 0.f, 1.f);
	}
	if(auto const* propose = std::getenv("KATECHON_HUD_FIN_PROPOSE"); propose && propose[0] == '1')
		fin_propose(state);
	if(auto const* bonds = std::getenv("KATECHON_HUD_FIN_BONDS"); bonds && bonds[0] >= '0' && bonds[0] <= '3') {
		fin_bond_choice = bonds[0] - '0';
		fin_offer_bonds(state);
	}
}

void fin_propose(sys::state& state) {
	command::fiscal_bill_data bill{ fin_draft[0], fin_draft[1], fin_draft[2], fin_draft[3], fin_draft[4], fin_draft[5] };
	if(!command::can_propose_fiscal_bill(state, state.local_player_nation, bill))
		return;
	command::propose_fiscal_bill(state, state.local_player_nation, bill);
	fin_notice_frames = 600;
	fin_notice_bill = true;
	fin_rep_stale = true;
}
float fin_bond_amount() const {
	return std::round(std::max(fin_rep.credit.annual_revenue, 0.f) * fin_bond_shares[size_t(std::clamp(fin_bond_choice, 0, 3))] * 10.f) / 10.f;
}
void fin_offer_bonds(sys::state& state) {
	auto const amount = fin_bond_amount();
	auto const tenor = fin_tenors[size_t(std::clamp(fin_tenor_choice, 0, 3))];
	if(!command::can_place_public_bonds(state, state.local_player_nation, amount, tenor))
		return;
	command::place_public_bonds(state, state.local_player_nation, amount, tenor);
	fin_notice_frames = 600;
	fin_notice_bill = false;
	fin_rep_stale = true;
}

bool fin_handles(action a) const {
	switch(a) {
	case action::toggle_finance: case action::fin_page: case action::fin_period: case action::fin_step_menu:
	case action::fin_step: case action::fin_chart: case action::fin_slider: case action::fin_propose:
	case action::fin_reset: case action::fin_bond: case action::fin_tenor: case action::fin_offer: case action::fin_repay:
		return true;
	default:
		return false;
	}
}
// What leaves the sheet: another sheet of the ribbon, or a country window.
bool fin_leaves(action a, button_element_base* button) const {
	switch(a) {
	case action::toggle_population: case action::toggle_military: case action::toggle_diplomacy: case action::toggle_trade:
	case action::population_inspect: case action::diplomacy:
		return true;
	case action::press:
		return std::find(tab_buttons.begin(), tab_buttons.end(), button) != tab_buttons.end();
	default:
		return false;
	}
}

void fin_activate(sys::state& state, hit_target const& t) {
	using governance::finance::fiscal_period;
	using governance::finance::fiscal_step;
	if(t.act != action::fin_step && t.act != action::fin_step_menu)
		fin_step_menu = false;
	switch(t.act) {
	case action::toggle_finance:
		toggle_finance(state);
		break;
	case action::fin_page:
		fin_page = fin_page_kind(std::clamp(t.value, 0, int32_t(fin_page_kind::count) - 1));
		break;
	case action::fin_period:
		fin_period = fiscal_period(std::clamp(t.value, 0, 2));
		if(!governance::finance::step_allowed(fin_period, fin_step))
			fin_step = governance::finance::default_step(fin_period);
		fin_rep_stale = true;
		break;
	case action::fin_step_menu:
		fin_step_menu = !fin_step_menu;
		break;
	case action::fin_step:
		fin_step = fiscal_step(std::clamp(t.value, 0, 2));
		fin_step_menu = false;
		fin_rep_stale = true;
		break;
	case action::fin_chart:
		fin_bars = t.value == 0;
		break;
	case action::fin_slider:
		fin_drag = std::clamp(t.value, 0, 5);
		state.ui_state.drag_target = this;
		fin_drag_to(float(state.mouse_x_position) / state.user_settings.ui_scale);
		break;
	case action::fin_propose:
		fin_propose(state);
		break;
	case action::fin_reset:
		fin_draft = fin_law();
		break;
	case action::fin_bond:
		fin_bond_choice = std::clamp(t.value, 0, 3);
		break;
	case action::fin_tenor:
		fin_tenor_choice = std::clamp(t.value, 0, 3);
		break;
	case action::fin_offer:
		fin_offer_bonds(state);
		break;
	case action::fin_repay:
		command::repay_public_debt(state, state.local_player_nation, dcon::obligation_id{ dcon::obligation_id::value_base_t(t.formation) });
		fin_notice_frames = 600;
		fin_notice_bill = false;
		fin_rep_stale = true;
		break;
	default:
		break;
	}
	play_click(state);
}
void fin_drag_to(float x) {
	if(fin_drag < 0)
		return;
	auto const& track = fin_tracks[size_t(fin_drag)];
	if(track.w <= 1.f)
		return;
	auto const part = std::clamp((x - track.x) / track.w, 0.f, 1.f);
	auto const index = size_t(fin_drag);
	if(index < 2) {
		// the tax sliders step by a tenth of a point (a whole point of progressivity)
		auto const top = fin_slider_top(index);
		auto const step = index == 0 ? 0.001f : 0.01f;
		fin_draft[index] = std::clamp(std::round(part * top / step) * step, 0.f, top);
	} else {
		fin_set_share(index, std::round(part * 100.f) / 100.f);
	}
}
bool fin_escape(sys::state& state) {
	if(!finance_open)
		return false;
	if(fin_step_menu)
		fin_step_menu = false;
	else if(fin_page != fin_page_kind::overview)
		fin_page = fin_page_kind::overview;
	else
		close_finance(state);
	return true;
}

// ---- the sheet ----

struct fin_layout {
	float x0 = 0.f, y0 = 0.f, x1 = 0.f, y1 = 0.f; // the frame
	float left = 0.f, right = 0.f; // inside it
	float cards_top = 0.f, cards_h = 0.f;
	float body_top = 0.f, body_bottom = 0.f;
	float side_x = 0.f, side_w = 0.f; // the right column
	float main_w = 0.f; // the left column
	float nav_top = 0.f, nav_h = 0.f;
};
fin_layout fin_lay(float w, float h) const {
	fin_layout l;
	l.x0 = ribbon_w + 3.f;
	l.y0 = top_h - 1.f;
	l.x1 = w - 3.f;
	l.y1 = h - 3.f;
	l.left = l.x0 + 16.f;
	l.right = l.x1 - 16.f;
	auto const k = std::clamp((l.y1 - l.y0) / 890.f, 0.82f, 1.12f);
	l.cards_top = std::round(l.y0 + 120.f * k);
	l.cards_h = std::round(132.f * k);
	l.body_top = l.cards_top + l.cards_h + 12.f;
	l.body_bottom = l.y1 - 14.f;
	l.side_w = std::round(std::clamp((l.right - l.left) * 0.268f, 380.f, 470.f));
	l.side_x = l.right - l.side_w;
	l.main_w = l.side_x - 12.f - l.left;
	l.nav_h = 46.f;
	l.nav_top = l.body_bottom - l.nav_h;
	return l;
}

void fin_backdrop(sys::state& state, fin_layout const& l, float w, float h) {
	fill(state, ribbon_w, top_h - 3.f, w - ribbon_w, h - top_h + 3.f, fin_wall, 1.f);
	// the wall: a faint damask under everything, darker toward the edges
	if(fin_damask) {
		for(float y = l.y0; y < l.y1; y += 128.f)
			for(float x = l.x0; x < l.x1; x += 128.f)
				fin_sprite(state, fin_damask, x, y, 128.f, 128.f);
	}
	for(int32_t i = 0; i < 12; ++i) {
		auto const t = float(i) / 12.f;
		auto const a = 0.06f * (1.f - t);
		fill(state, l.x0 + t * 60.f, l.y0, 5.f, l.y1 - l.y0, fin_black, a);
		fill(state, l.x1 - t * 60.f - 5.f, l.y0, 5.f, l.y1 - l.y0, fin_black, a);
		fill(state, l.x0, l.y1 - t * 40.f - 4.f, l.x1 - l.x0, 4.f, fin_black, a);
	}
	// the hall over the heading
	auto const band_h = std::round(l.cards_top - l.y0 + 14.f);
	if(fin_header) {
		constexpr float image_w = 3200.f, image_h = 240.f;
		auto const draw_h = band_h;
		auto const draw_w = image_w * draw_h / image_h;
		auto const span = l.x1 - l.x0;
		if(draw_w >= span) {
			auto const part = span / draw_w;
			ogl::render_subrect(state, l.x0, l.y0, span, draw_h, 1.f - part, 0.f, part, 1.f, fin_header);
		} else {
			fill(state, l.x0, l.y0, span - draw_w, draw_h, ogl::color3f{ 0.118f, 0.106f, 0.165f }, 1.f);
			ogl::render_subrect(state, l.x1 - draw_w, l.y0, draw_w, draw_h, 0.f, 0.f, 1.f, 1.f, fin_header);
		}
		// it fades into the wall below
		for(int32_t i = 0; i < 16; ++i) {
			auto const t = float(i + 1) / 16.f;
			fill(state, l.x0, l.y0 + draw_h - 28.f + float(i) * 1.75f, l.x1 - l.x0, 1.75f, fin_wall, t * t * 0.9f);
		}
		fill(state, l.x0, l.y0, (l.x1 - l.x0) * 0.42f, draw_h, fin_black, 0.18f);
	}
	// the frame: one gilt line, a dark one inside it, engraved corners
	fill(state, l.x0, l.y0, l.x1 - l.x0, 1.f, fin_line, 0.85f);
	fill(state, l.x0, l.y1 - 1.f, l.x1 - l.x0, 1.f, fin_line, 0.85f);
	fill(state, l.x0, l.y0, 1.f, l.y1 - l.y0, fin_line, 0.85f);
	fill(state, l.x1 - 1.f, l.y0, 1.f, l.y1 - l.y0, fin_line, 0.85f);
	fill(state, l.x0 + 3.f, l.y0 + 3.f, l.x1 - l.x0 - 6.f, 1.f, fin_line, 0.25f);
	fill(state, l.x0 + 3.f, l.y1 - 4.f, l.x1 - l.x0 - 6.f, 1.f, fin_line, 0.25f);
	fill(state, l.x0 + 3.f, l.y0 + 3.f, 1.f, l.y1 - l.y0 - 6.f, fin_line, 0.25f);
	fill(state, l.x1 - 4.f, l.y0 + 3.f, 1.f, l.y1 - l.y0 - 6.f, fin_line, 0.25f);
	if(fin_corner) {
		constexpr float c = 46.f;
		ogl::render_tinted_subsprite(state, 0, 4, l.x0 + 1.f, l.y0 + 1.f, c, c, 1.f, 1.f, 1.f, fin_corner, ui::rotation::upright, false, false);
		ogl::render_tinted_subsprite(state, 1, 4, l.x1 - c - 1.f, l.y0 + 1.f, c, c, 1.f, 1.f, 1.f, fin_corner, ui::rotation::upright, false, false);
		ogl::render_tinted_subsprite(state, 2, 4, l.x0 + 1.f, l.y1 - c - 1.f, c, c, 1.f, 1.f, 1.f, fin_corner, ui::rotation::upright, false, false);
		ogl::render_tinted_subsprite(state, 3, 4, l.x1 - c - 1.f, l.y1 - c - 1.f, c, c, 1.f, 1.f, 1.f, fin_corner, ui::rotation::upright, false, false);
	}
}

void fin_heading(sys::state& state, fin_layout const& l, bool hovering, float mx, float my) {
	auto const k = (l.cards_top - l.y0) / 120.f;
	auto const x = l.x0 + 48.f;
	// the way back, above the title
	{
		auto const label = localized(state, "katechon_fin_government", "Government");
		auto const cy = l.y0 + 33.f * k;
		hit_target back{ x - 6.f, cy - 10.f, fin_caps_w(state, label, 12, 0.14f) + 30.f, 20.f, action::toggle_finance };
		auto const hovered = hovering && over(back, mx, my);
		fin_draw_icon(state, fin_icon::chevron_left, x + 5.f, cy, 12.f, hovered ? ogl::color3f{ 1.f, 1.f, 1.f } : ogl::color3f{ 0.85f, 0.85f, 0.85f });
		fin_caps(state, label, x + 18.f, cy, 12, hovered ? fin_gold_light : mix(fin_type, fin_gold, 0.25f), 0.14f);
		targets.push_back(back);
		if(hovered)
			hint(localized(state, "katechon_fin_back", "Back to the map"), back);
	}
	auto const title = localized(state, "katechon_fin_title", "State Finances");
	auto const title_size = int32_t(std::round(54.f * std::min(k, 1.05f)));
	auto const title_w = fin_lifted(state, title, x - 2.f, l.y0 + 76.f * k, title_size, fin_title_type, false, 0.004f);
	// the period, beside the title
	{
		static constexpr std::array<std::pair<char const*, char const*>, 3> periods{
			std::pair{ "katechon_fin_30_days", "30 Days" }, std::pair{ "katechon_fin_90_days", "90 Days" }, std::pair{ "katechon_fin_12_months", "12 Months" } };
		auto const tab_w = 127.f;
		auto const tab_h = std::round(38.f * std::min(k, 1.f));
		auto px = std::round(std::max(x + title_w + 96.f, l.x0 + (l.x1 - l.x0) * 0.33f));
		auto const py = std::round(l.y0 + 75.f * k - tab_h * 0.5f);
		for(size_t i = 0; i < periods.size(); ++i) {
			auto const chosen = size_t(fin_period) == i;
			hit_target t{ px, py, tab_w, tab_h, action::fin_period, nullptr, int32_t(i) };
			fin_button_at(state, px, py, tab_w, tab_h, localized(state, periods[i].first, periods[i].second), fin_icon::count, chosen, !chosen, t, hovering, mx, my, {}, 15);
			if(chosen) // a chosen tab still reads as chosen though it does not act
				fin_text(state, localized(state, periods[i].first, periods[i].second), px + tab_w * 0.5f - fin_w(state, localized(state, periods[i].first, periods[i].second), 15) * 0.5f, py + tab_h * 0.5f, 15, fin_type);
			px += tab_w + 2.f;
		}
	}
}

std::string fin_versus(sys::state& state) const {
	switch(fin_period) {
	case governance::finance::fiscal_period::days_30: return localized(state, "katechon_fin_vs_30", "vs prev. 30d");
	case governance::finance::fiscal_period::days_90: return localized(state, "katechon_fin_vs_90", "vs prev. 90d");
	default: return localized(state, "katechon_fin_vs_12", "vs prev. 12m");
	}
}

// A sparkline along the foot of a figure: dots on each recorded step, the ground under them.
void fin_spark(sys::state& state, std::vector<float> const& values, float x, float y, float w, float h, ogl::color3f colour) {
	if(values.size() < 2)
		return;
	auto lo = values.front(), hi = values.front();
	for(auto v : values) {
		lo = std::min(lo, v);
		hi = std::max(hi, v);
	}
	if(hi - lo < 1.0e-6f) {
		lo -= 1.f;
		hi += 1.f;
	}
	auto const pad = (hi - lo) * 0.12f;
	lo -= pad;
	hi += pad;
	std::vector<std::pair<float, float>> points;
	auto const n = float(values.size() - 1);
	for(size_t i = 0; i < values.size(); ++i)
		points.push_back({ x + w * float(i) / n, y + h - (values[i] - lo) / (hi - lo) * h });
	fin_area(state, points, y + h, colour, 0.16f);
	fin_polyline(state, points, 1.4f, colour, 0.95f);
	if(values.size() <= 40)
		for(auto const& [px, py] : points)
			fin_draw_icon(state, fin_icon::dot, px, py, 5.f, colour);
}

void fin_cards(sys::state& state, fin_layout const& l, bool hovering, float mx, float my) {
	auto const& r = fin_rep;
	constexpr int32_t count = 5;
	constexpr float gap = 10.f;
	auto const card_w = std::floor((l.right - l.left - gap * float(count - 1)) / float(count));
	std::vector<float> treasury, balance, revenue, spending, debt;
	for(auto const& b : r.buckets) {
		if(!b.recorded)
			continue;
		treasury.push_back(b.treasury);
		balance.push_back(b.balance);
		revenue.push_back(b.revenue);
		spending.push_back(b.spending);
		debt.push_back(b.debt);
	}
	struct card {
		fin_icon image;
		std::string title;
		std::string value;
		ogl::color3f value_colour;
		std::string note;
		ogl::color3f note_colour;
		std::vector<float> const* series;
		ogl::color3f line;
		std::string tip;
	};
	auto const known = r.recorded_days > 0;
	auto const change = [&](float now, float before, bool more_is_good, ogl::color3f& colour) {
		if(!r.previous_known || before <= 1.0e-4f) {
			colour = fin_quiet;
			return localized(state, "katechon_fin_no_previous", "no earlier period on record");
		}
		auto const part = now / before - 1.f;
		colour = (part >= 0.f) == more_is_good ? fin_green_text : fin_red;
		return fin_pct(part, 1, true) + " " + fin_versus(state);
	};
	std::array<card, count> cards;
	{
		auto colour = r.treasury_per_day >= 0.f ? fin_green_text : fin_red;
		cards[0] = card{ fin_icon::coin_stack, localized(state, "katechon_fin_treasury", "Treasury"), fin_money(r.treasury), fin_type,
			known ? fin_money(r.treasury_per_day, true) + " " + localized(state, "katechon_fin_per_day", "/ day") : localized(state, "katechon_fin_no_days", "no closed day yet"),
			known ? colour : fin_quiet, &treasury, ogl::color3f{ 0.42f, 0.66f, 0.52f },
			filled(localized(state, "katechon_fin_treasury_tip", "Cash in every treasury of the state. The finance ministry holds %1 of it"), fin_money(r.ministry)) };
	}
	{
		auto const share = r.revenue_total > 0.f ? r.balance / r.revenue_total : 0.f;
		auto const good = r.balance >= 0.f;
		cards[1] = card{ fin_icon::scales, localized(state, "katechon_fin_balance", "Budget Balance"), fin_money(r.balance, true), known ? (good ? fin_green_text : fin_red) : fin_type,
			r.revenue_total > 0.f ? filled(localized(state, "katechon_fin_of_revenue", "%1 of revenue"), fin_pct(share, 1, true)) : localized(state, "katechon_fin_no_revenue", "no revenue yet"),
			r.revenue_total > 0.f ? (good ? fin_green_text : fin_red) : fin_quiet, &balance, good ? fin_green_text : fin_red,
			localized(state, "katechon_fin_balance_tip", "Revenue less spending over the period. Borrowing and repaid principal are financing, not revenue or spending") };
	}
	{
		ogl::color3f colour{};
		auto note = change(r.revenue_total, r.previous_revenue, true, colour);
		cards[2] = card{ fin_icon::coins, localized(state, "katechon_fin_revenue_total", "Total Revenue"), fin_money(r.revenue_total), fin_type, note, colour, &revenue, ogl::color3f{ 0.36f, 0.70f, 0.58f },
			localized(state, "katechon_fin_revenue_tip", "Taxes received and other income from outside the state, over the period") };
	}
	{
		ogl::color3f colour{};
		auto note = change(r.spending_total, r.previous_spending, false, colour);
		cards[3] = card{ fin_icon::facade, localized(state, "katechon_fin_spending_total", "Total Spending"), fin_money(r.spending_total), fin_type, note, colour, &spending, fin_coral,
			localized(state, "katechon_fin_spending_tip", "Payroll, purchases, transfers and interest paid outside the state. Money moved between its institutions is not counted") };
	}
	{
		auto const has_debt = r.public_debt > 0.005f;
		cards[4] = card{ fin_icon::document, localized(state, "katechon_fin_public_debt", "Public Debt"), fin_money(r.public_debt), fin_type,
			has_debt ? filled(localized(state, "katechon_fin_interest_rate", "%1 interest rate"), fin_pct(r.average_rate)) : localized(state, "katechon_fin_no_bonds", "no bonds outstanding"),
			has_debt ? fin_violet : fin_quiet, &debt, fin_violet,
			localized(state, "katechon_fin_debt_tip", "Public debt of every institution of the state: principal and interest accrued") };
	}
	auto const k = l.cards_h / 132.f;
	for(int32_t i = 0; i < count; ++i) {
		auto const& c = cards[size_t(i)];
		auto const x = l.left + float(i) * (card_w + gap);
		auto const y = l.cards_top;
		fin_board(state, fin_card, x, y, card_w, l.cards_h);
		auto const med = std::round(62.f * k);
		fin_sprite(state, fin_medallion, x + 14.f, y + 12.f * k, med, med);
		fin_draw_icon(state, c.image, x + 14.f + med * 0.5f, y + 12.f * k + med * 0.5f, std::round(med * 0.70f));
		auto const tx = x + 14.f + med + 18.f;
		auto const room = card_w - (tx - x) - 12.f;
		fin_text(state, fin_fit(state, c.title, 19, room), tx, y + 30.f * k, 19, fin_type);
		fin_lifted(state, fin_fit(state, c.value, 30, room), tx, y + 61.f * k, 30, c.value_colour);
		fin_text(state, fin_fit(state, c.note, 14, room), tx, y + 89.f * k, 14, c.note_colour);
		fin_spark(state, *c.series, x + 14.f, y + 98.f * k, card_w - 28.f, l.cards_h - 98.f * k - 12.f, c.line);
		hit_target t{ x, y, card_w, l.cards_h, action::none };
		if(hovering && over(t, mx, my))
			hint(c.tip, t);
	}
}

// The head of a panel: its name, and what stands at its right.
void fin_panel_head(sys::state& state, std::string const& title, float x, float y, float w, int32_t size = 22) {
	fin_lifted(state, title, x + 20.f, y + 26.f, size, fin_type, false, 0.004f);
}

// ---- the chart ----

struct fin_chart_spec {
	bool revenue = true;
	bool spending = true;
	bool balance = true;
	bool cash = false; // the treasury and the debt in place of the flows
};

void fin_chart(sys::state& state, float x, float y, float w, float h, std::string const& title, fin_chart_spec spec, bool hovering, float mx, float my) {
	using governance::finance::fiscal_step;
	fin_board(state, fin_panel, x, y, w, h);
	fin_panel_head(state, title, x, y, w);
	auto const& buckets = fin_rep.buckets;
	// the tools: bars or lines, and the step
	{
		auto const step_names = std::array<std::string, 3>{ localized(state, "katechon_fin_daily", "Daily"), localized(state, "katechon_fin_weekly", "Weekly"), localized(state, "katechon_fin_monthly", "Monthly") };
		constexpr float th = 30.f;
		auto const ty = y + 12.f;
		auto const menu_w = 150.f;
		auto const mx0 = x + w - 20.f - menu_w;
		if(!spec.cash) {
			for(int32_t i = 0; i < 2; ++i) {
				auto const bx = mx0 - 16.f - float(2 - i) * 34.f;
				auto const on = (i == 0) == fin_bars;
				hit_target t{ bx, ty, 32.f, th, action::fin_chart, nullptr, i };
				fin_plate(state, on ? fin_button : fin_tab, bx, ty, 32.f, th, 96.f, 48.f, 12.f, 6.f);
				fin_draw_icon(state, i == 0 ? fin_icon::bars : fin_icon::line_chart, bx + 16.f, ty + th * 0.5f, 18.f, on ? ogl::color3f{ 1.f, 1.f, 1.f } : ogl::color3f{ 0.62f, 0.62f, 0.62f });
				targets.push_back(t);
				if(hovering && over(t, mx, my))
					hint(i == 0 ? localized(state, "katechon_fin_as_bars", "As bars") : localized(state, "katechon_fin_as_lines", "As lines"), t);
			}
		}
		hit_target menu{ mx0, ty, menu_w, th, action::fin_step_menu };
		fin_plate(state, fin_tab, mx0, ty, menu_w, th, 96.f, 48.f, 12.f, 6.f);
		if(hovering && over(menu, mx, my))
			fill(state, mx0 + 2.f, ty + 2.f, menu_w - 4.f, th - 4.f, fin_gold, 0.07f);
		fin_draw_icon(state, fin_icon::calendar, mx0 + 20.f, ty + th * 0.5f, 16.f);
		fin_text(state, step_names[size_t(fin_rep.step)], mx0 + 42.f, ty + th * 0.5f, 15, fin_type);
		fin_draw_icon(state, fin_icon::chevron_down, mx0 + menu_w - 18.f, ty + th * 0.5f, 11.f, fin_muted);
		targets.push_back(menu);
	}

	auto const plot_x = x + 62.f;
	auto const plot_r = x + w - 24.f;
	auto const plot_y = y + 64.f;
	auto const plot_b = y + h - 62.f;
	auto const plot_w = plot_r - plot_x;
	auto const plot_h = plot_b - plot_y;
	auto const n = buckets.size();
	bool any = false;
	float hi = 0.f, lo = 0.f;
	for(auto const& b : buckets) {
		if(!b.recorded)
			continue;
		any = true;
		if(spec.cash) {
			hi = std::max({ hi, b.treasury, b.debt });
			lo = std::min({ lo, b.treasury, b.debt });
		} else {
			if(spec.revenue)
				hi = std::max(hi, b.revenue);
			if(spec.spending)
				hi = std::max(hi, b.spending);
			if(spec.balance) {
				hi = std::max(hi, b.balance);
				lo = std::min(lo, b.balance);
			}
		}
	}
	if(hi - lo < 1.0e-3f)
		hi = lo + 1.f;
	// a round step, four or five of them over the range
	auto const raw_step = (hi - lo) / 4.f;
	auto const magnitude = std::pow(10.f, std::floor(std::log10(raw_step)));
	auto step = magnitude;
	for(auto m : { 1.f, 1.5f, 2.f, 2.5f, 5.f, 10.f })
		if(magnitude * m >= raw_step) {
			step = magnitude * m;
			break;
		}
	auto const top = std::ceil(hi / step) * step;
	auto const bottom = std::floor(lo / step) * step;
	auto const yv = [&](float v) { return plot_b - (v - bottom) / std::max(top - bottom, 1.0e-6f) * plot_h; };
	for(float v = bottom; v <= top + step * 0.5f; v += step) {
		auto const gy = std::round(yv(v));
		auto const zero = std::abs(v) < step * 0.01f;
		fill(state, plot_x, gy, plot_w, 1.f, zero ? fin_line : fin_grid, zero ? 0.55f : 0.32f);
		fin_text(state, fin_axis(v), plot_x - 12.f, gy, 13, fin_muted, true);
	}
	fill(state, plot_x, plot_y, 1.f, plot_h, fin_grid, 0.32f);
	if(n == 0)
		return;
	auto const slot = plot_w / float(n);
	auto const centre = [&](size_t i) { return plot_x + slot * (float(i) + 0.5f); };
	// the labels of the steps, a dozen at most, each with its faint line
	{
		auto const every = std::max<size_t>(1, (n + 9) / 10);
		for(size_t i = (n - 1) % every; i < n; i += every) {
			auto const& b = buckets[i];
			std::string label;
			if(fin_rep.step == fiscal_step::monthly) {
				auto ymd = fin_date(b.first_day).to_ymd(state.start_date);
				label = fin_month(state, ymd.month);
			} else {
				label = fin_day(state, fin_rep.step == fiscal_step::weekly ? b.first_day : b.last_day);
			}
			auto const cx = centre(i);
			fill(state, std::round(cx), plot_y, 1.f, plot_h, fin_grid, 0.16f);
			auto const lw = fin_w(state, label, 13);
			fin_text(state, label, cx - lw * 0.5f, plot_b + 22.f, 13, fin_muted);
		}
	}
	if(!any) {
		auto const note = filled(localized(state, "katechon_fin_books_open", "The books open on %1. The first day closes at midnight"), fin_day(state, state.current_date));
		fin_text(state, note, plot_x + plot_w * 0.5f - fin_w(state, note, 15) * 0.5f, plot_y + plot_h * 0.5f, 15, fin_muted);
	}
	// what the mouse rests on
	int32_t hovered = -1;
	if(hovering && mx >= plot_x && mx < plot_r && my >= plot_y - 8.f && my < plot_b + 8.f && !fin_step_menu) {
		auto const i = int32_t((mx - plot_x) / slot);
		if(i >= 0 && size_t(i) < n && buckets[size_t(i)].recorded)
			hovered = i;
	}
	if(spec.cash) {
		std::vector<std::pair<float, float>> cash, owed;
		for(size_t i = 0; i < n; ++i) {
			if(!buckets[i].recorded)
				continue;
			cash.push_back({ centre(i), yv(buckets[i].treasury) });
			owed.push_back({ centre(i), yv(buckets[i].debt) });
		}
		fin_area(state, cash, yv(std::max(bottom, 0.f)), fin_green, 0.12f);
		fin_polyline(state, cash, 2.f, fin_green, 0.95f);
		fin_area(state, owed, yv(std::max(bottom, 0.f)), fin_violet, 0.10f);
		fin_polyline(state, owed, 2.f, fin_violet, 0.95f);
	} else if(fin_bars) {
		auto const group = std::min(slot * 0.70f, 30.f);
		auto const both = spec.revenue && spec.spending;
		auto const bw = std::max(1.f, both ? group * 0.5f - 1.f : group);
		for(size_t i = 0; i < n; ++i) {
			auto const& b = buckets[i];
			if(!b.recorded)
				continue;
			auto const lit = int32_t(i) == hovered;
			auto const gx = centre(i) - group * 0.5f;
			auto const bar = [&](float bx, float value, ogl::color3f colour, float alpha) {
				auto const ty0 = std::round(yv(std::max(value, 0.f)));
				auto const by = std::round(yv(std::max(bottom, 0.f)));
				if(by - ty0 < 1.f)
					return;
				fill(state, bx, ty0, bw, by - ty0, colour, alpha * (lit ? 1.15f : 1.f));
				fill(state, bx, ty0, bw, std::min(3.f, by - ty0), mix(colour, ogl::color3f{ 1.f, 1.f, 1.f }, 0.25f), 0.65f);
				fill(state, bx, ty0, 1.f, by - ty0, mix(colour, ogl::color3f{ 1.f, 1.f, 1.f }, 0.15f), 0.25f);
			};
			if(spec.revenue)
				bar(gx, b.revenue, ogl::color3f{ 0.24f, 0.54f, 0.47f }, 0.62f);
			if(spec.spending)
				bar(both ? gx + bw + 2.f : gx, b.spending, ogl::color3f{ 0.69f, 0.53f, 0.31f }, 0.72f);
		}
	} else {
		std::vector<std::pair<float, float>> income, outgo;
		for(size_t i = 0; i < n; ++i) {
			if(!buckets[i].recorded)
				continue;
			income.push_back({ centre(i), yv(buckets[i].revenue) });
			outgo.push_back({ centre(i), yv(buckets[i].spending) });
		}
		if(spec.revenue) {
			fin_area(state, income, yv(std::max(bottom, 0.f)), fin_green, 0.10f);
			fin_polyline(state, income, 2.f, fin_green, 0.95f);
		}
		if(spec.spending) {
			fin_area(state, outgo, yv(std::max(bottom, 0.f)), fin_spend, 0.08f);
			fin_polyline(state, outgo, 2.f, fin_spend, 0.95f);
		}
	}
	if(spec.balance && !spec.cash) {
		std::vector<std::pair<float, float>> line;
		for(size_t i = 0; i < n; ++i)
			if(buckets[i].recorded)
				line.push_back({ centre(i), yv(buckets[i].balance) });
		fin_area(state, line, yv(std::max(bottom, 0.f)), fin_violet, 0.07f);
		fin_polyline(state, line, 1.6f, fin_violet, 0.95f);
		for(auto const& [px, py] : line)
			fin_draw_icon(state, fin_icon::dot, px, py, n > 60 ? 4.f : 6.f, mix(fin_violet, ogl::color3f{ 1.f, 1.f, 1.f }, 0.25f));
	}
	// the legend
	{
		struct key {
			ogl::color3f colour;
			std::string label;
		};
		std::vector<key> keys;
		if(spec.cash) {
			keys.push_back(key{ fin_green, localized(state, "katechon_fin_treasury", "Treasury") });
			keys.push_back(key{ fin_violet, localized(state, "katechon_fin_public_debt", "Public Debt") });
		} else {
			if(spec.revenue)
				keys.push_back(key{ fin_green, localized(state, "katechon_fin_revenue", "Revenue") });
			if(spec.spending)
				keys.push_back(key{ fin_spend, localized(state, "katechon_fin_spending", "Spending") });
			if(spec.balance)
				keys.push_back(key{ fin_violet, localized(state, "katechon_fin_balance_short", "Balance") });
		}
		auto kx = x + 40.f;
		auto const ky = y + h - 22.f;
		for(auto const& k : keys) {
			fin_draw_icon(state, fin_icon::dot, kx, ky, 9.f, k.colour);
			kx += 12.f + fin_text(state, k.label, kx + 12.f, ky, 14, fin_type) + 38.f;
		}
	}
	// the day under the mouse
	if(hovered >= 0) {
		auto const& b = buckets[size_t(hovered)];
		auto const cx = centre(size_t(hovered));
		fin_dashed(state, cx, plot_y, plot_b, fin_type, 0.45f);
		auto const marker_y = spec.cash ? yv(b.treasury) : yv(spec.balance ? b.balance : (spec.revenue ? b.revenue : b.spending));
		fin_sprite(state, fin_glow, cx - 14.f, marker_y - 14.f, 28.f, 28.f, mix(fin_violet, ogl::color3f{ 1.f, 1.f, 1.f }, 0.4f));
		fin_draw_icon(state, fin_icon::ring, cx, marker_y, 10.f, fin_type);
		std::string date;
		if(fin_rep.step == fiscal_step::daily)
			date = fin_day(state, b.last_day, true);
		else if(fin_rep.step == fiscal_step::weekly)
			date = fin_day(state, b.first_day) + " \xE2\x80\x93 " + fin_day(state, b.last_day, true);
		else {
			auto ymd = fin_date(b.first_day).to_ymd(state.start_date);
			date = fin_month(state, ymd.month, true) + " " + std::to_string(ymd.year);
		}
		struct line_item {
			ogl::color3f colour;
			std::string label;
			std::string value;
			ogl::color3f value_colour;
		};
		std::vector<line_item> lines;
		if(spec.cash) {
			lines.push_back(line_item{ fin_green, localized(state, "katechon_fin_treasury", "Treasury"), fin_money(b.treasury), fin_type });
			lines.push_back(line_item{ fin_violet, localized(state, "katechon_fin_public_debt", "Public Debt"), fin_money(b.debt), fin_type });
		} else {
			if(spec.revenue)
				lines.push_back(line_item{ fin_green, localized(state, "katechon_fin_revenue", "Revenue"), fin_money(b.revenue), fin_type });
			if(spec.spending)
				lines.push_back(line_item{ fin_spend, localized(state, "katechon_fin_spending", "Spending"), fin_money(b.spending), fin_type });
			if(spec.balance)
				lines.push_back(line_item{ fin_violet, localized(state, "katechon_fin_balance_short", "Balance"), fin_money(b.balance, true), b.balance >= 0.f ? fin_green_text : fin_red });
		}
		auto box_w = fin_w(state, date, 14) + 28.f;
		for(auto const& li : lines)
			box_w = std::max(box_w, 36.f + fin_w(state, li.label, 14) + 26.f + fin_w(state, li.value, 14) + 14.f);
		auto const box_h = 34.f + float(lines.size()) * 22.f + 6.f;
		auto bx = cx + 16.f;
		if(bx + box_w > x + w - 8.f)
			bx = cx - 16.f - box_w;
		auto const by = std::max(y + 8.f, std::min(marker_y - box_h - 12.f, plot_b - box_h));
		fill(state, bx + 3.f, by + 5.f, box_w, box_h, fin_black, 0.45f);
		fin_plate(state, fin_inset, bx, by, box_w, box_h, 64.f, 64.f, 10.f, 5.f);
		fill(state, bx + 1.f, by + 1.f, box_w - 2.f, 1.f, fin_gold, 0.45f);
		fin_text(state, date, bx + 14.f, by + 18.f, 14, fin_type);
		auto ly = by + 42.f;
		for(auto const& li : lines) {
			fin_draw_icon(state, fin_icon::dot, bx + 20.f, ly, 9.f, li.colour);
			fin_text(state, li.label, bx + 32.f, ly, 14, fin_type);
			fin_text(state, li.value, bx + box_w - 14.f, ly, 14, li.value_colour, true);
			ly += 22.f;
		}
	}
	panels.push_back(rect{ x, y, w, h });
}

// ---- the tables ----

struct fin_line_item {
	fin_icon image;
	std::string name;
	float amount = 0.f;
	std::string tip;
};

void fin_table(sys::state& state, float x, float y, float w, float h, std::string const& title, std::string const& first_column,
	std::vector<fin_line_item> const& rows, float total, ogl::color3f bar, bool per_day, bool hovering, float mx, float my) {
	fin_board(state, fin_panel, x, y, w, h);
	fin_panel_head(state, title, x, y, w);
	auto const rule_y = y + 48.f;
	fin_rule(state, x + 20.f, rule_y, w - 40.f);
	auto const amount_r = x + w * (per_day ? 0.43f : 0.51f);
	auto const day_r = x + w * 0.58f;
	auto const share_r = x + w * (per_day ? 0.71f : 0.645f);
	auto const bar_x = x + w * (per_day ? 0.74f : 0.675f);
	auto const bar_w = x + w - 22.f - bar_x;
	auto const head_y = rule_y + 20.f;
	fin_text(state, first_column, x + 22.f, head_y, 14, fin_muted);
	fin_text(state, localized(state, "katechon_fin_amount", "Amount"), amount_r, head_y, 14, fin_muted, true);
	if(per_day)
		fin_text(state, localized(state, "katechon_fin_a_day", "A day"), day_r, head_y, 14, fin_muted, true);
	fin_text(state, localized(state, "katechon_fin_share", "% of Total"), share_r, head_y, 14, fin_muted, true);
	auto const rows_top = head_y + 14.f;
	auto const pitch = std::clamp((y + h - 10.f - rows_top) / float(std::max<size_t>(rows.size(), 1)), 22.f, 34.f);
	auto const days = std::max(1, fin_rep.recorded_days);
	for(size_t i = 0; i < rows.size(); ++i) {
		auto const& row = rows[i];
		auto const ry = rows_top + float(i) * pitch;
		auto const cy = ry + pitch * 0.5f;
		hit_target t{ x + 14.f, ry + 1.f, w - 28.f, pitch - 2.f, action::none };
		auto const lit = hovering && over(t, mx, my);
		if(i % 2 == 0 || lit)
			fin_plate(state, fin_row, t.x, t.y, t.w, t.h, 64.f, 32.f, 8.f, 4.f);
		fin_draw_icon(state, row.image, x + 34.f, cy, std::round(std::min(pitch - 6.f, 24.f)));
		fin_text(state, fin_fit(state, row.name, 15, amount_r - x - 140.f), x + 56.f, cy, 15, fin_type);
		fin_text(state, fin_money(row.amount), amount_r, cy, 15, fin_type, true);
		if(per_day)
			fin_text(state, fin_money(row.amount / float(days)), day_r, cy, 15, fin_muted, true);
		auto const share = total > 0.f ? row.amount / total : 0.f;
		fin_text(state, fin_pct(share), share_r, cy, 15, fin_type, true);
		fin_groove(state, bar_x, cy, bar_w, std::round(std::min(pitch * 0.42f, 13.f)), share, bar);
		if(lit && !row.tip.empty())
			hint(row.tip, t);
	}
}

std::vector<fin_line_item> fin_revenue_rows(sys::state& state) const {
	using governance::finance::revenue_source;
	auto const& r = fin_rep;
	auto const at = [&](revenue_source s) { return r.revenue[size_t(s)]; };
	return {
		fin_line_item{ fin_icon::coin_eagle, localized(state, "katechon_fin_income_tax", "Income Tax"), at(revenue_source::income_tax),
			localized(state, "katechon_fin_income_tax_tip", "Tax on wages, at the rates of the fiscal law for each stratum") },
		fin_line_item{ fin_icon::coin_gem, localized(state, "katechon_fin_royalties", "Resource Royalties"), at(revenue_source::resource_royalties),
			localized(state, "katechon_fin_royalties_tip", "The royalty on sales of extracted resources, at the rate the law sets") },
		fin_line_item{ fin_icon::seal_scroll, localized(state, "katechon_fin_customs", "Customs Duties"), at(revenue_source::customs_duties),
			localized(state, "katechon_fin_customs_tip", "Duties on goods crossing the border, at the tariffs in force") },
		fin_line_item{ fin_icon::shield_coin, localized(state, "katechon_fin_dividend_tax", "Dividend Tax"), at(revenue_source::dividend_tax),
			localized(state, "katechon_fin_dividend_tax_tip", "Tax on dividends paid to owners") },
		fin_line_item{ fin_icon::factory, localized(state, "katechon_fin_enterprises", "State Enterprises"), at(revenue_source::state_enterprises),
			localized(state, "katechon_fin_enterprises_tip", "Dividends the state receives as owner of companies") },
		fin_line_item{ fin_icon::ornament, localized(state, "katechon_fin_other_revenue", "Other Revenue"), at(revenue_source::other),
			localized(state, "katechon_fin_other_revenue_tip", "Rents, sales, interest and other payments to the state's institutions") },
	};
}
std::vector<fin_line_item> fin_spending_rows(sys::state& state) const {
	using governance::finance::spending_function;
	auto const& r = fin_rep;
	auto const at = [&](spending_function f) { return r.spending[size_t(f)]; };
	return {
		fin_line_item{ fin_icon::facade, localized(state, "katechon_fin_administration", "Administration"), at(spending_function::administration),
			localized(state, "katechon_fin_administration_tip", "The ministries, the tax authority, the legislature and local governments") },
		fin_line_item{ fin_icon::swords, localized(state, "katechon_fin_military", "Military"), at(spending_function::military),
			localized(state, "katechon_fin_military_tip", "The military command: pay and procurement") },
		fin_line_item{ fin_icon::book, localized(state, "katechon_fin_education", "Education"), at(spending_function::education),
			localized(state, "katechon_fin_education_tip", "Schools: teachers' pay and what they need") },
		fin_line_item{ fin_icon::shield, localized(state, "katechon_fin_public_order", "Public Order"), at(spending_function::public_order),
			localized(state, "katechon_fin_public_order_tip", "Police, intelligence, courts and prosecutors") },
		fin_line_item{ fin_icon::aqueduct, localized(state, "katechon_fin_infrastructure", "Infrastructure"), at(spending_function::infrastructure),
			localized(state, "katechon_fin_infrastructure_tip", "Public works: construction paid by the state") },
		fin_line_item{ fin_icon::people, localized(state, "katechon_fin_social", "Social Transfers"), at(spending_function::social_transfers),
			localized(state, "katechon_fin_social_tip", "Benefits paid to people, such as unemployment benefits") },
		fin_line_item{ fin_icon::coins_doc, localized(state, "katechon_fin_debt_service", "Debt Service"), at(spending_function::debt_service),
			localized(state, "katechon_fin_debt_service_tip", "Interest paid on public debt. Repaid principal is financing, not spending") },
	};
}

// ---- fiscal policy ----

void fin_slider_row(sys::state& state, size_t index, float x, float right, float cy, float label_w, fin_icon image, bool boxed,
	std::string const& label, std::string const& value, float part, bool hovering, float mx, float my, std::string const& tip_text) {
	auto const icon_x = x + (boxed ? 18.f : 12.f);
	if(boxed)
		fin_plate(state, fin_inset, x, cy - 15.f, 36.f, 30.f, 64.f, 64.f, 10.f, 5.f);
	fin_draw_icon(state, image, icon_x, cy, boxed ? 22.f : 20.f);
	fin_text(state, label, x + (boxed ? 50.f : 32.f), cy, 15, mix(fin_type, fin_muted, 0.25f));
	auto const tx = std::round(x + label_w);
	auto const tr = std::round(right - 64.f);
	auto const tw = tr - tx;
	fin_tracks[index] = rect{ tx, cy - 3.f, tw, 6.f };
	fill(state, tx, cy - 3.f, tw, 6.f, fin_track, 1.f);
	fill(state, tx, cy - 3.f, tw, 1.f, fin_black, 0.9f);
	fill(state, tx, cy + 2.f, tw, 1.f, fin_grid, 0.45f);
	auto const knob_x = tx + tw * std::clamp(part, 0.f, 1.f);
	if(knob_x - tx >= 1.f) {
		fill(state, tx, cy - 3.f, knob_x - tx, 6.f, ogl::color3f{ 0.70f, 0.55f, 0.34f }, 1.f);
		fill(state, tx, cy - 3.f, knob_x - tx, 2.f, fin_gold_light, 0.85f);
	}
	hit_target t{ tx - 10.f, cy - 12.f, tw + 20.f, 24.f, action::fin_slider, nullptr, int32_t(index) };
	auto const lit = fin_drag == int32_t(index) || (hovering && over(t, mx, my));
	if(lit)
		fin_sprite(state, fin_glow, knob_x - 18.f, cy - 18.f, 36.f, 36.f, fin_gold);
	fin_sprite(state, fin_knob, knob_x - 11.f, cy - 11.f, 22.f, 22.f);
	fin_text(state, value, right - 14.f, cy, 17, fin_type, true);
	targets.push_back(t);
	if(lit && !tip_text.empty() && fin_drag < 0)
		hint(tip_text, t);
}

std::string fin_bill_words(sys::state& state, governance::finance::bill_notice const& n) const {
	using governance::finance::bill_outcome;
	auto const votes = filled(localized(state, "katechon_fin_votes", "%1 for, %2 against"), std::to_string(n.ayes), std::to_string(n.noes));
	switch(n.outcome) {
	case bill_outcome::enacted: return localized(state, "katechon_fin_bill_enacted", "Enacted") + ": " + votes;
	case bill_outcome::rejected: return localized(state, "katechon_fin_bill_rejected", "Rejected by the chambers") + ": " + votes;
	case bill_outcome::no_assent: return localized(state, "katechon_fin_bill_no_assent", "Passed, but assent was withheld") + ": " + votes;
	case bill_outcome::too_soon: return filled(localized(state, "katechon_fin_bill_too_soon", "The chambers take a new fiscal bill from %1"), fin_day(state, fin_rep.next_bill));
	case bill_outcome::no_constitution: return localized(state, "katechon_fin_bill_no_state", "There is no constitutional state to legislate");
	case bill_outcome::no_enactor: return localized(state, "katechon_fin_bill_no_enactor", "Nobody holds an office that could enact it");
	case bill_outcome::unchanged: return localized(state, "katechon_fin_bill_unchanged", "The law already says this");
	case bill_outcome::invalid: return localized(state, "katechon_fin_bill_invalid", "The bill could not be drafted");
	default: return {};
	}
}

void fin_policy(sys::state& state, float x, float y, float w, float h, bool hovering, float mx, float my) {
	fin_board(state, fin_panel, x, y, w, h);
	fin_panel_head(state, localized(state, "katechon_fin_policy", "Fiscal Policy"), x, y, w, 24);
	auto const today = state.current_date;
	auto const dirty = fin_dirty();
	auto const too_soon = fin_rep.next_bill && today < fin_rep.next_bill;
	auto const can = dirty && !too_soon && fin_rep.law_in_force;
	{
		constexpr float bw = 152.f, bh = 32.f;
		auto const bx = x + w - 18.f - bw;
		auto const by = y + 11.f;
		std::string why;
		if(!fin_rep.law_in_force)
			why = localized(state, "katechon_fin_no_law", "No fiscal law is in force to amend");
		else if(!dirty)
			why = localized(state, "katechon_fin_move_first", "Move the sliders to draft a bill");
		else if(too_soon)
			why = filled(localized(state, "katechon_fin_bill_too_soon", "The chambers take a new fiscal bill from %1"), fin_day(state, fin_rep.next_bill));
		else
			why = localized(state, "katechon_fin_propose_tip", "Lay these rates and priorities before the chambers as a fiscal bill");
		fin_button_at(state, bx, by, bw, bh, localized(state, "katechon_fin_propose", "Propose Changes"), fin_icon::gavel, true, can,
			hit_target{ 0.f, 0.f, 0.f, 0.f, action::fin_propose }, hovering, mx, my, why, 14);
		if(!can && hovering && over(hit_target{ bx, by, bw, bh }, mx, my))
			hint(why, hit_target{ bx, by, bw, bh });
	}
	fin_rule(state, x + 20.f, y + 52.f, w - 40.f);
	// the rows share the panel's height
	auto const k = std::clamp((h - 60.f) / 350.f, 0.82f, 1.25f);
	auto cy = y + 52.f + 22.f * k;
	fin_text(state, localized(state, "katechon_fin_income_tax", "Income Tax"), x + 20.f, cy, 18, fin_type);
	cy += 30.f * k;
	auto const label_w = 150.f;
	fin_slider_row(state, 0, x + 14.f, x + w - 4.f, cy, label_w, fin_icon::bank_coin, false, localized(state, "katechon_fin_base_rate", "Base Rate"),
		fin_pct(fin_draft[0], 0), fin_draft[0] / 0.4f, hovering, mx, my, localized(state, "katechon_fin_base_rate_tip", "The rate on the wages of the middle stratum, from 0 to 40%"));
	cy += 30.f * k;
	fin_slider_row(state, 1, x + 14.f, x + w - 4.f, cy, label_w, fin_icon::progressivity, false, localized(state, "katechon_fin_progressivity", "Progressivity"),
		fin_pct(fin_draft[1], 0), fin_draft[1], hovering, mx, my, localized(state, "katechon_fin_progressivity_tip", "At 0 every stratum pays the base rate. At 100% the poor pay 40% of it and the rich twice it"));
	cy += 22.f * k;
	// the rates each stratum would pay
	{
		auto const rates = dirty ? fin_draft_rates(state) : fin_rep.stratum_rates;
		auto const row_h = 24.f * k;
		auto const box_h = 30.f * k + 3.f * row_h + 6.f;
		fin_plate(state, fin_inset, x + 14.f, cy, w - 28.f, box_h, 64.f, 64.f, 10.f, 5.f);
		fin_text(state, localized(state, "katechon_fin_effective", "Effective Rates"), x + 30.f, cy + 16.f * k, 15, fin_muted);
		if(dirty)
			fin_text(state, localized(state, "katechon_fin_as_proposed", "as proposed"), x + w - 30.f, cy + 16.f * k, 13, fin_gold, true);
		static constexpr std::array<std::pair<char const*, char const*>, 3> strata{ std::pair{ "katechon_fin_poor", "Poor" }, std::pair{ "katechon_fin_middle", "Middle" }, std::pair{ "katechon_fin_rich", "Rich" } };
		static constexpr std::array<fin_icon, 3> marks{ fin_icon::person_one, fin_icon::person_two, fin_icon::person_crown };
		auto ry = cy + 30.f * k;
		for(size_t i = 0; i < 3; ++i) {
			fill(state, x + 26.f, ry, w - 52.f, 1.f, fin_grid, 0.35f);
			auto const rcy = ry + row_h * 0.5f;
			fin_draw_icon(state, marks[i], x + 40.f, rcy, 20.f, ogl::color3f{ 0.85f, 0.85f, 0.85f });
			fin_text(state, localized(state, strata[i].first, strata[i].second), x + 62.f, rcy, 15, fin_type);
			fin_text(state, fin_pct(rates[i]), x + w - 30.f, rcy, 15, dirty ? fin_gold_light : fin_type, true);
			ry += row_h;
		}
		cy += box_h;
	}
	cy += 24.f * k;
	fin_text(state, localized(state, "katechon_fin_priorities", "Expenditure Priorities"), x + 20.f, cy, 18, fin_type);
	float total = 0.f;
	for(size_t i = 2; i < 6; ++i)
		total += fin_draft[i];
	fin_text(state, localized(state, "katechon_fin_total", "Total:") + " " + (total > 0.f ? fin_pct(total, 0) : std::string("\xE2\x80\x94")), x + w - 18.f, cy, 15, fin_muted, true);
	cy += 32.f * k;
	static constexpr std::array<std::pair<char const*, char const*>, 4> shares{ std::pair{ "katechon_fin_education", "Education" }, std::pair{ "katechon_fin_public_order", "Public Order" },
		std::pair{ "katechon_fin_infrastructure", "Infrastructure" }, std::pair{ "katechon_fin_local_government", "Local Government" } };
	static constexpr std::array<fin_icon, 4> share_icons{ fin_icon::book, fin_icon::shield, fin_icon::aqueduct, fin_icon::facade };
	auto const pitch = std::clamp((y + h - 18.f - cy) / 3.6f, 26.f, 36.f);
	for(size_t i = 0; i < 4; ++i) {
		fin_slider_row(state, 2 + i, x + 14.f, x + w - 4.f, cy, 196.f, share_icons[i], true, localized(state, shares[i].first, shares[i].second),
			fin_pct(fin_draft[2 + i], 0), fin_draft[2 + i], hovering, mx, my,
			localized(state, "katechon_fin_share_tip", "The share of each monthly disbursement of the national treasury. The other priorities make room"));
		cy += pitch;
	}
	// what became of the bill just laid
	auto const notice = governance::finance::last_bill(state, state.local_player_nation);
	if(fin_notice_frames > 0 && fin_notice_bill && notice.outcome != governance::finance::bill_outcome::none) {
		auto const words = fin_bill_words(state, notice);
		auto const good = notice.outcome == governance::finance::bill_outcome::enacted;
		auto const nw = std::min(w - 28.f, fin_w(state, words, 14) + 36.f);
		auto const nx = x + w - 14.f - nw;
		auto const ny = y + 48.f;
		fill(state, nx + 3.f, ny + 4.f, nw, 30.f, fin_black, 0.5f);
		fin_plate(state, fin_inset, nx, ny, nw, 30.f, 64.f, 64.f, 10.f, 5.f);
		fill(state, nx, ny + 1.f, 3.f, 28.f, good ? fin_green : fin_red, 1.f);
		fin_text(state, fin_fit(state, words, 14, nw - 28.f), nx + 16.f, ny + 15.f, 14, good ? fin_green_text : fin_type);
	}
	panels.push_back(rect{ x, y, w, h });
}

// ---- debt ----

std::string fin_grade_name(governance::finance::credit_grade g) const {
	static constexpr std::array<char const*, 9> names{ "AAA", "AA", "A", "BBB", "BB", "B", "CCC", "CC", "D" };
	return names[std::min<size_t>(size_t(g), names.size() - 1)];
}
std::string fin_debt_words(sys::state& state, governance::finance::debt_notice const& n) const {
	using governance::finance::debt_outcome;
	switch(n.outcome) {
	case debt_outcome::placed: return filled(localized(state, "katechon_fin_debt_placed", "Bonds placed: %1 at %2"), fin_money(n.amount), fin_pct(n.annual_rate, 2));
	case debt_outcome::repaid: return filled(localized(state, "katechon_fin_debt_repaid", "Repaid %1"), fin_money(n.amount));
	case debt_outcome::no_office: return localized(state, "katechon_fin_debt_no_office", "No finance minister holds the power to borrow");
	case debt_outcome::no_bank: return localized(state, "katechon_fin_debt_no_bank", "No solvent domestic bank to buy the bonds");
	case debt_outcome::declined: return n.amount > 0.f ? filled(localized(state, "katechon_fin_debt_declined_part", "The banks would take only %1"), fin_money(n.amount))
		: localized(state, "katechon_fin_debt_declined", "The banks declined: the state's credit is below their risk appetite");
	case debt_outcome::prohibited: return localized(state, "katechon_fin_debt_prohibited", "The law prohibits public borrowing");
	case debt_outcome::ceiling: return localized(state, "katechon_fin_debt_ceiling_hit", "The issue would exceed the debt ceiling in law");
	case debt_outcome::no_cash: return localized(state, "katechon_fin_debt_no_cash", "The treasury has no free cash to repay it");
	case debt_outcome::invalid: return localized(state, "katechon_fin_debt_invalid", "The operation was refused");
	default: return {};
	}
}

void fin_rating(sys::state& state, float right, float cy, governance::finance::credit_assessment const& c) {
	constexpr int32_t boxes = 6;
	constexpr float bw = 14.f, bh = 12.f, gap = 3.f;
	auto const x0 = right - float(boxes) * (bw + gap) + gap;
	auto const filled_boxes = c.assessed ? std::clamp(6 - int32_t(c.grade) * 6 / 8, 0, 6) : 0;
	for(int32_t i = 0; i < boxes; ++i) {
		auto const bx = x0 + float(i) * (bw + gap);
		fill(state, bx, cy - bh * 0.5f, bw, bh, i < filled_boxes ? ogl::color3f{ 0.86f, 0.73f, 0.50f } : fin_track, 1.f);
		fill(state, bx, cy - bh * 0.5f, bw, 1.f, i < filled_boxes ? fin_gold_light : fin_black, 0.8f);
		if(i >= filled_boxes) {
			fill(state, bx, cy + bh * 0.5f - 1.f, bw, 1.f, fin_grid, 0.5f);
			fill(state, bx, cy - bh * 0.5f, 1.f, bh, fin_grid, 0.4f);
			fill(state, bx + bw - 1.f, cy - bh * 0.5f, 1.f, bh, fin_grid, 0.4f);
		}
	}
	auto const grade = c.assessed ? fin_grade_name(c.grade) : localized(state, "katechon_fin_not_assessed", "Not assessed");
	auto const colour = !c.assessed ? fin_quiet : (c.score >= 0.62f ? fin_green_text : (c.score >= 0.38f ? fin_gold_light : fin_red));
	fin_text(state, grade, x0 - 14.f, cy, c.assessed ? 17 : 14, colour, true);
}

void fin_debt(sys::state& state, float x, float y, float w, float h, bool hovering, float mx, float my) {
	auto const& r = fin_rep;
	fin_board(state, fin_panel, x, y, w, h);
	fin_panel_head(state, localized(state, "katechon_fin_debt_title", "Debt & Borrowing"), x, y, w, 22);
	fin_button_at(state, x + w - 18.f - 112.f, y + 12.f, 112.f, 30.f, localized(state, "katechon_fin_manage_debt", "Manage Debt"), fin_icon::count, true, true,
		hit_target{ 0.f, 0.f, 0.f, 0.f, action::fin_page, nullptr, int32_t(fin_page_kind::debt) }, hovering, mx, my,
		localized(state, "katechon_fin_manage_tip", "The obligations of the state, the bonds it may offer and how the banks rate it"), 14);
	fin_rule(state, x + 20.f, y + 52.f, w - 40.f);
	auto const pitch = std::clamp((h - 62.f) / 5.f, 24.f, 34.f);
	auto cy = y + 56.f + pitch * 0.5f;
	auto const right = x + w - 18.f;
	auto const row = [&](fin_icon image, std::string const& label) {
		fin_draw_icon(state, image, x + 32.f, cy, 20.f);
		fin_text(state, label, x + 52.f, cy, 15, mix(fin_type, fin_muted, 0.2f));
	};
	row(fin_icon::scales, localized(state, "katechon_fin_outstanding", "Outstanding Debt"));
	fin_text(state, fin_money(r.public_debt), right, cy, 16, fin_gold_light, true);
	cy += pitch;
	row(fin_icon::percent, localized(state, "katechon_fin_annual_interest", "Annual Interest"));
	fin_text(state, r.debt_principal > 0.f ? fin_money(r.annual_interest) + " (" + fin_pct(r.average_rate) + ")" : fin_money(0.f), right, cy, 16, fin_gold_light, true);
	cy += pitch;
	row(fin_icon::calendar, localized(state, "katechon_fin_next_payment", "Next Payment"));
	if(r.next_payment) {
		auto const amount = fin_money(r.next_payment_amount);
		auto const aw = fin_text(state, amount, right, cy, 16, fin_gold_light, true);
		fin_text(state, fin_day(state, r.next_payment), right - aw - 12.f, cy, 15, fin_type, true);
	} else {
		fin_text(state, localized(state, "katechon_fin_none_due", "None due"), right, cy, 15, fin_quiet, true);
	}
	cy += pitch;
	row(fin_icon::shield_ceiling, localized(state, "katechon_fin_ceiling", "Debt Ceiling"));
	if(!r.issuance_allowed) {
		fin_text(state, localized(state, "katechon_fin_prohibited", "Borrowing prohibited"), right, cy, 15, fin_red, true);
	} else if(r.debt_ceiling && *r.debt_ceiling > 0.f) {
		auto const used = r.public_debt / *r.debt_ceiling;
		auto const words = fin_money(*r.debt_ceiling) + " (" + fin_pct(used, 0) + ")";
		auto const ww = fin_text(state, words, right, cy, 15, fin_type, true);
		auto const bar_r = right - ww - 10.f;
		auto const bar_x = x + 52.f + fin_w(state, localized(state, "katechon_fin_ceiling", "Debt Ceiling"), 15) + 18.f;
		if(bar_r - bar_x > 30.f)
			fin_groove(state, bar_x, cy, bar_r - bar_x, 9.f, used, ogl::color3f{ 0.52f, 0.34f, 0.66f });
	} else {
		fin_text(state, localized(state, "katechon_fin_no_ceiling", "No ceiling in law"), right, cy, 15, fin_quiet, true);
	}
	cy += pitch;
	row(fin_icon::crest, localized(state, "katechon_fin_rating", "Credit Rating"));
	fin_rating(state, right, cy, r.credit);
	{
		hit_target t{ x + 14.f, cy - pitch * 0.5f, w - 28.f, pitch, action::none };
		if(hovering && over(t, mx, my))
			hint(r.credit.assessed ? filled(localized(state, "katechon_fin_rating_tip", "Assessed from the books: the weakest of five ratios scores %1. See Manage Debt"), fin_pct(r.credit.score, 0))
				: filled(localized(state, "katechon_fin_rating_wait", "The banks assess the state after %1 days of revenue on the books"), std::to_string(governance::finance::credit_minimum_days)), t);
	}
	panels.push_back(rect{ x, y, w, h });
}

// ---- the bar of sections ----

void fin_navigation(sys::state& state, float x, float y, float w, float h, bool hovering, float mx, float my) {
	static constexpr std::array<std::pair<char const*, char const*>, 5> names{ std::pair{ "katechon_fin_overview", "Overview" }, std::pair{ "katechon_fin_revenue", "Revenue" },
		std::pair{ "katechon_fin_spending", "Spending" }, std::pair{ "katechon_fin_debt", "Debt" }, std::pair{ "katechon_fin_policy_short", "Policy" } };
	static constexpr std::array<fin_icon, 5> marks{ fin_icon::coin_stack, fin_icon::scales, fin_icon::facade, fin_icon::document, fin_icon::gavel };
	if(fin_flourish) {
		ogl::render_tinted_subsprite(state, 0, 2, x - 70.f, y + h * 0.5f - 22.f, 88.f, 44.f, 1.f, 1.f, 1.f, fin_flourish, ui::rotation::upright, false, false);
		ogl::render_tinted_subsprite(state, 1, 2, x + w - 18.f, y + h * 0.5f - 22.f, 88.f, 44.f, 1.f, 1.f, 1.f, fin_flourish, ui::rotation::upright, false, false);
	}
	fin_plate(state, fin_nav, x, y, w, h, 128.f, 48.f, 12.f, 6.f);
	auto const seg = (w - 8.f) / float(names.size());
	for(size_t i = 0; i < names.size(); ++i) {
		auto const sx = x + 4.f + seg * float(i);
		hit_target t{ sx, y + 3.f, seg, h - 6.f, action::fin_page, nullptr, int32_t(i) };
		auto const on = size_t(fin_page) == i;
		auto const lit = hovering && over(t, mx, my);
		if(on)
			fin_plate(state, fin_nav_on, sx, y + 3.f, seg, h - 6.f, 96.f, 48.f, 12.f, 6.f);
		else if(lit)
			fill(state, sx + 2.f, y + 5.f, seg - 4.f, h - 10.f, fin_gold, 0.07f);
		if(i > 0 && !on && size_t(fin_page) + 1 != i)
			fill(state, sx, y + 10.f, 1.f, h - 20.f, fin_line, 0.30f);
		auto const label = upper(localized(state, names[i].first, names[i].second));
		auto const lw = fin_w(state, label, 14, 0.06f);
		auto const start = std::round(sx + (seg - lw - 30.f) * 0.5f);
		fin_draw_icon(state, marks[i], start + 9.f, y + h * 0.5f, 18.f, on ? ogl::color3f{ 1.f, 1.f, 1.f } : ogl::color3f{ 0.80f, 0.80f, 0.80f });
		fin_text(state, label, start + 30.f, y + h * 0.5f, 14, on ? fin_gold_light : (lit ? fin_type : fin_muted), false, 0.06f);
		if(!on)
			targets.push_back(t);
	}
	panels.push_back(rect{ x, y, w, h });
}

// ---- the detailed sections ----

// A panel of label and value rows; a row with an empty label is a heading.
struct fin_fact {
	fin_icon image = fin_icon::count;
	std::string label;
	std::string value;
	ogl::color3f colour{ 0.910f, 0.863f, 0.780f };
	std::string tip;
};
void fin_facts(sys::state& state, float x, float y, float w, float h, std::string const& title, std::vector<fin_fact> const& facts, bool hovering, float mx, float my) {
	fin_board(state, fin_panel, x, y, w, h);
	fin_panel_head(state, title, x, y, w, 22);
	fin_rule(state, x + 20.f, y + 48.f, w - 40.f);
	auto const pitch = std::clamp((h - 64.f) / float(std::max<size_t>(facts.size(), 1)), 22.f, 32.f);
	auto cy = y + 56.f + pitch * 0.5f;
	for(auto const& f : facts) {
		if(f.label.empty()) {
			fin_text(state, f.value, x + 20.f, cy + 2.f, 16, fin_gold);
			cy += pitch;
			continue;
		}
		auto const tx = f.image == fin_icon::count ? x + 22.f : x + 52.f;
		if(f.image != fin_icon::count)
			fin_draw_icon(state, f.image, x + 32.f, cy, 19.f);
		auto const vw = fin_w(state, f.value, 15);
		fin_text(state, fin_fit(state, f.label, 15, x + w - 20.f - vw - 16.f - tx), tx, cy, 15, mix(fin_type, fin_muted, 0.2f));
		fin_text(state, f.value, x + w - 20.f, cy, 15, f.colour, true);
		hit_target t{ x + 14.f, cy - pitch * 0.5f, w - 28.f, pitch, action::none };
		if(!f.tip.empty() && hovering && over(t, mx, my))
			hint(f.tip, t);
		cy += pitch;
	}
	panels.push_back(rect{ x, y, w, h });
}

void fin_revenue_page(sys::state& state, fin_layout const& l, bool hovering, float mx, float my) {
	auto const& r = fin_rep;
	auto const top = l.body_top;
	auto const bottom = l.nav_top - 10.f;
	auto const chart_h = std::round((bottom - top) * 0.50f);
	fin_chart(state, l.left, top, l.main_w, chart_h, localized(state, "katechon_fin_revenue", "Revenue"), fin_chart_spec{ true, false, false, false }, hovering, mx, my);
	auto const rows = fin_revenue_rows(state);
	fin_table(state, l.left, top + chart_h + 10.f, l.main_w, bottom - top - chart_h - 10.f, localized(state, "katechon_fin_by_source", "Revenue by Source"),
		localized(state, "katechon_fin_source", "Source"), rows, r.revenue_total, ogl::color3f{ 0.31f, 0.64f, 0.53f }, true, hovering, mx, my);
	auto const collected = r.revenue[0] + r.revenue[1] + r.revenue[2] + r.revenue[3];
	std::vector<fin_fact> facts{
		fin_fact{ fin_icon::document, localized(state, "katechon_fin_assessed", "Tax assessed"), fin_money(r.tax_assessed), fin_type,
			localized(state, "katechon_fin_assessed_tip", "Tax charged to taxpayers over the period, paid or not") },
		fin_fact{ fin_icon::coins, localized(state, "katechon_fin_collected", "Tax collected"), fin_money(collected), fin_type,
			localized(state, "katechon_fin_collected_tip", "Tax actually received in the treasuries over the period") },
		fin_fact{ fin_icon::percent, localized(state, "katechon_fin_collection", "Collected of assessed"), r.tax_assessed > 0.f ? fin_pct(collected / r.tax_assessed) : std::string("\xE2\x80\x94"), fin_type,
			localized(state, "katechon_fin_collection_tip", "What part of the tax charged was received. The rest stays owed") },
		fin_fact{ fin_icon::calendar, localized(state, "katechon_fin_receivables", "Tax still owed"), fin_money(r.tax_receivables), r.tax_receivables > 0.f ? fin_gold_light : fin_type,
			localized(state, "katechon_fin_receivables_tip", "Tax obligations outstanding today") },
		fin_fact{ fin_icon::count, {}, localized(state, "katechon_fin_rates_in_force", "Income tax in force") },
		fin_fact{ fin_icon::person_one, localized(state, "katechon_fin_poor", "Poor"), fin_pct(r.stratum_rates[0]) },
		fin_fact{ fin_icon::person_two, localized(state, "katechon_fin_middle", "Middle"), fin_pct(r.stratum_rates[1]) },
		fin_fact{ fin_icon::person_crown, localized(state, "katechon_fin_rich", "Rich"), fin_pct(r.stratum_rates[2]) },
		fin_fact{ fin_icon::count, {}, localized(state, "katechon_fin_not_revenue", "Not revenue") },
		fin_fact{ fin_icon::debt_coin, localized(state, "katechon_fin_borrowed", "Borrowing received"), fin_money(r.borrowed), fin_type,
			localized(state, "katechon_fin_borrowed_tip", "Proceeds of bonds and loans: financing, owed back with interest") },
	};
	fin_facts(state, l.side_x, top, l.side_w, l.body_bottom - top, localized(state, "katechon_fin_assessment", "Assessment & Collection"), facts, hovering, mx, my);
}

void fin_spending_page(sys::state& state, fin_layout const& l, bool hovering, float mx, float my) {
	using governance::finance::spending_function;
	auto const& r = fin_rep;
	auto const top = l.body_top;
	auto const bottom = l.nav_top - 10.f;
	auto const chart_h = std::round((bottom - top) * 0.46f);
	fin_chart(state, l.left, top, l.main_w, chart_h, localized(state, "katechon_fin_spending", "Spending"), fin_chart_spec{ false, true, false, false }, hovering, mx, my);
	fin_table(state, l.left, top + chart_h + 10.f, l.main_w, bottom - top - chart_h - 10.f, localized(state, "katechon_fin_by_function", "Spending by Function"),
		localized(state, "katechon_fin_function", "Function"), fin_spending_rows(state), r.spending_total, ogl::color3f{ 0.72f, 0.56f, 0.33f }, true, hovering, mx, my);
	// the law's priorities against what was spent on them
	float services = r.spending[size_t(spending_function::education)] + r.spending[size_t(spending_function::public_order)]
		+ r.spending[size_t(spending_function::infrastructure)] + r.spending[size_t(spending_function::administration)];
	auto const spent = [&](spending_function f) { return services > 0.f ? fin_pct(r.spending[size_t(f)] / services) : std::string("\xE2\x80\x94"); };
	auto const both = [&](size_t priority, spending_function f) { return fin_pct(r.priorities[priority], 0) + "  /  " + spent(f); };
	std::vector<fin_fact> facts{
		fin_fact{ fin_icon::count, {}, localized(state, "katechon_fin_appropriated_spent", "Appropriated  /  spent") },
		fin_fact{ fin_icon::book, localized(state, "katechon_fin_education", "Education"), both(0, spending_function::education) },
		fin_fact{ fin_icon::shield, localized(state, "katechon_fin_public_order", "Public Order"), both(1, spending_function::public_order) },
		fin_fact{ fin_icon::aqueduct, localized(state, "katechon_fin_infrastructure", "Infrastructure"), both(2, spending_function::infrastructure) },
		fin_fact{ fin_icon::facade, localized(state, "katechon_fin_local_admin", "Local Government / Administration"), both(3, spending_function::administration), fin_type,
			localized(state, "katechon_fin_local_admin_tip", "Local governments spend on their own administration. The national ministries are administration too") },
		fin_fact{ fin_icon::count, {}, localized(state, "katechon_fin_financing", "Financing") },
		fin_fact{ fin_icon::coins_doc, localized(state, "katechon_fin_interest_paid", "Interest paid"), fin_money(r.spending[size_t(spending_function::debt_service)]) },
		fin_fact{ fin_icon::document, localized(state, "katechon_fin_principal_repaid", "Principal repaid"), fin_money(r.principal_repaid), fin_type,
			localized(state, "katechon_fin_principal_tip", "Repaying principal settles debt. It is not spending") },
		fin_fact{ fin_icon::coins, localized(state, "katechon_fin_financial", "Other financial flows"), fin_money(r.financial_net, true), fin_type,
			localized(state, "katechon_fin_financial_tip", "Deposits, withdrawals, exchange, equity and lending, net into the treasuries") },
	};
	fin_facts(state, l.side_x, top, l.side_w, l.body_bottom - top, localized(state, "katechon_fin_priorities_spent", "Priorities & Financing"), facts, hovering, mx, my);
}

std::string fin_holder(sys::state& state, dcon::economic_actor_id holder) const {
	auto const bank = holder ? actors::organizations::organization_for_actor(state, holder) : dcon::organization_id{};
	if(!bank)
		return localized(state, "katechon_fin_holder", "Holder");
	int32_t number = 1;
	auto const nation = state.world.organization_get_bank_jurisdiction(bank);
	state.world.for_each_organization([&](dcon::organization_id other) {
		if(other != bank && state.world.organization_get_bank_jurisdiction(other) == nation
			&& state.world.organization_get_bank_policy_configured(other)
			&& state.world.organization_get_canonical_id(other) < state.world.organization_get_canonical_id(bank))
			++number;
	});
	return filled(localized(state, "katechon_fin_bank_no", "Bank No. %1"), std::to_string(number));
}

void fin_debt_page(sys::state& state, fin_layout const& l, bool hovering, float mx, float my) {
	auto const& r = fin_rep;
	auto const top = l.body_top;
	auto const bottom = l.nav_top - 10.f;
	auto const chart_h = std::round((bottom - top) * 0.44f);
	fin_chart(state, l.left, top, l.main_w, chart_h, localized(state, "katechon_fin_cash_debt", "Treasury & Debt"), fin_chart_spec{ false, false, false, true }, hovering, mx, my);
	// the obligations
	{
		auto const x = l.left;
		auto const y = top + chart_h + 10.f;
		auto const w = l.main_w;
		auto const h = bottom - y;
		fin_board(state, fin_panel, x, y, w, h);
		fin_panel_head(state, localized(state, "katechon_fin_obligations", "Obligations"), x, y, w, 22);
		fin_rule(state, x + 20.f, y + 48.f, w - 40.f);
		std::array<float, 6> const col{ x + 22.f, x + w * 0.40f, x + w * 0.50f, x + w * 0.63f, x + w * 0.76f, x + w - 22.f };
		auto const head_y = y + 68.f;
		fin_text(state, localized(state, "katechon_fin_holder", "Holder"), col[0], head_y, 14, fin_muted);
		fin_text(state, localized(state, "katechon_fin_principal", "Principal"), col[1], head_y, 14, fin_muted, true);
		fin_text(state, localized(state, "katechon_fin_rate", "Rate"), col[2], head_y, 14, fin_muted, true);
		fin_text(state, localized(state, "katechon_fin_issued", "Issued"), col[3], head_y, 14, fin_muted, true);
		fin_text(state, localized(state, "katechon_fin_due", "Due"), col[4], head_y, 14, fin_muted, true);
		auto const pitch = 30.f;
		auto const fits = size_t(std::max(0.f, (y + h - 10.f - (head_y + 14.f)) / pitch));
		if(r.obligations.empty())
			fin_text(state, localized(state, "katechon_fin_no_obligations", "The state owes no public debt"), col[0], head_y + 40.f, 15, fin_quiet);
		for(size_t i = 0; i < r.obligations.size() && i < fits; ++i) {
			auto const& o = r.obligations[i];
			auto const ry = head_y + 14.f + float(i) * pitch;
			auto const cy = ry + pitch * 0.5f;
			if(i % 2 == 0)
				fin_plate(state, fin_row, x + 14.f, ry + 1.f, w - 28.f, pitch - 2.f, 64.f, 32.f, 8.f, 4.f);
			fin_text(state, fin_holder(state, o.holder), col[0], cy, 15, fin_type);
			fin_text(state, fin_money(o.principal + o.interest_due), col[1], cy, 15, fin_type, true);
			fin_text(state, fin_pct(o.annual_rate, 1), col[2], cy, 15, fin_type, true);
			fin_text(state, fin_day(state, o.issued), col[3], cy, 15, fin_muted, true);
			fin_text(state, fin_day(state, o.due), col[4], cy, 15, o.defaulted ? fin_red : (o.overdue ? fin_gold_light : fin_type), true);
			hit_target t{ 0.f, 0.f, 0.f, 0.f, action::fin_repay };
			t.formation = uint64_t(o.obligation.index());
			fin_button_at(state, col[5] - 84.f, ry + 3.f, 84.f, pitch - 6.f, localized(state, "katechon_fin_repay", "Repay"), fin_icon::count, true, r.treasury > 0.f, t, hovering, mx, my,
				filled(localized(state, "katechon_fin_repay_tip", "Repay %1 now from the debtor's treasury, interest first"), fin_money(o.principal + o.interest_due)), 13);
		}
		panels.push_back(rect{ x, y, w, h });
	}
	// how the banks rate the state, and the bonds it may offer
	auto const& c = r.credit;
	auto const factor = [&](governance::finance::credit_factor const& f, bool share) {
		return c.assessed ? (share ? fin_pct(f.value) : fin_axis(f.value)) + "   " + fin_pct(f.score, 0) : std::string("\xE2\x80\x94");
	};
	auto const side_h = l.body_bottom - top;
	auto const rating_h = std::round(side_h * 0.52f);
	std::vector<fin_fact> facts{
		fin_fact{ fin_icon::crest, localized(state, "katechon_fin_rating", "Credit Rating"), c.assessed ? fin_grade_name(c.grade) + "  (" + fin_pct(c.score, 0) + ")" : localized(state, "katechon_fin_not_assessed", "Not assessed"),
			c.assessed ? fin_gold_light : fin_quiet, localized(state, "katechon_fin_rating_model", "The weakest of the five factors below. Banks buy bonds when it clears their risk appetite") },
		fin_fact{ fin_icon::scales, localized(state, "katechon_fin_f_debt", "Debt to revenue"), factor(c.debt_to_revenue, true), fin_type,
			localized(state, "katechon_fin_f_debt_tip", "Full score up to 60% of a year's revenue, none from 300%") },
		fin_fact{ fin_icon::percent, localized(state, "katechon_fin_f_interest", "Interest to revenue"), factor(c.interest_to_revenue, true), fin_type,
			localized(state, "katechon_fin_f_interest_tip", "Full score up to 5%, none from 30%") },
		fin_fact{ fin_icon::calendar, localized(state, "katechon_fin_f_liquidity", "Cash to service due in 90 days"), factor(c.liquidity, false), fin_type,
			localized(state, "katechon_fin_f_liquidity_tip", "Treasury cash over principal and interest falling due within 90 days") },
		fin_fact{ fin_icon::coins, localized(state, "katechon_fin_f_balance", "Balance to revenue"), factor(c.deficit, true), fin_type,
			localized(state, "katechon_fin_f_balance_tip", "Full score for a surplus, none at a deficit of half the revenue") },
		fin_fact{ fin_icon::shield, localized(state, "katechon_fin_f_record", "Payment record"), c.assessed ? fin_pct(c.record.score, 0) : std::string("\xE2\x80\x94"), fin_type,
			localized(state, "katechon_fin_f_record_tip", "Clean, overdue (40%) or in default (0)") },
	};
	fin_facts(state, l.side_x, top, l.side_w, rating_h, localized(state, "katechon_fin_assessment_title", "Credit Assessment"), facts, hovering, mx, my);
	// the offer
	{
		auto const x = l.side_x;
		auto const y = top + rating_h + 10.f;
		auto const w = l.side_w;
		auto const h = l.body_bottom - y;
		fin_board(state, fin_panel, x, y, w, h);
		fin_panel_head(state, localized(state, "katechon_fin_offer_title", "Offer Bonds"), x, y, w, 22);
		fin_rule(state, x + 20.f, y + 48.f, w - 40.f);
		auto cy = y + 72.f;
		fin_text(state, localized(state, "katechon_fin_amount", "Amount"), x + 22.f, cy, 15, fin_muted);
		auto bx = x + 110.f;
		auto const choice_w = (x + w - 20.f - bx - 9.f) / 4.f;
		for(int32_t i = 0; i < 4; ++i) {
			auto const on = fin_bond_choice == i;
			hit_target t{ bx, cy - 13.f, choice_w, 26.f, action::fin_bond, nullptr, i };
			fin_button_at(state, bx, cy - 13.f, choice_w, 26.f, fin_pct(fin_bond_shares[size_t(i)], 0), fin_icon::count, on, true, t, hovering, mx, my,
				localized(state, "katechon_fin_amount_tip", "A share of a year's revenue"), 13);
			bx += choice_w + 3.f;
		}
		cy += 34.f;
		fin_text(state, localized(state, "katechon_fin_tenor", "Term"), x + 22.f, cy, 15, fin_muted);
		bx = x + 110.f;
		for(int32_t i = 0; i < 4; ++i) {
			auto const on = fin_tenor_choice == i;
			hit_target t{ bx, cy - 13.f, choice_w, 26.f, action::fin_tenor, nullptr, i };
			auto const years = std::to_string(fin_tenors[size_t(i)] / 365);
			fin_button_at(state, bx, cy - 13.f, choice_w, 26.f, filled(localized(state, "katechon_fin_years", "%1 y"), years), fin_icon::count, on, true, t, hovering, mx, my, {}, 13);
			bx += choice_w + 3.f;
		}
		cy += 34.f;
		auto const amount = fin_bond_amount();
		auto const quote = economy::banking::quote_sovereign_credit(state,
			governance::actor_for_institution(state, governance::find_institution(state, state.local_player_nation, governance::institution_kind::finance_ministry)),
			state.local_player_nation, economy::money, std::max(amount, 0.01f), c.score);
		fin_text(state, localized(state, "katechon_fin_quote", "Best bank's price"), x + 22.f, cy, 15, fin_muted);
		fin_text(state, quote.bank && quote.amount + 1.0e-3f >= amount && c.assessed
			? filled(localized(state, "katechon_fin_quote_value", "%1 a year"), fin_pct(quote.annual_interest_rate, 2))
			: localized(state, "katechon_fin_no_quote", "no bank would buy it"), x + w - 20.f, cy, 15, quote.bank ? fin_type : fin_quiet, true);
		cy += 30.f;
		auto const can = c.assessed && amount > 0.f && r.issuance_allowed;
		fin_button_at(state, x + 20.f, cy - 15.f, w - 40.f, 32.f, filled(localized(state, "katechon_fin_offer", "Offer %1 in bonds"), fin_money(amount)), fin_icon::document, true, can,
			hit_target{ 0.f, 0.f, 0.f, 0.f, action::fin_offer }, hovering, mx, my,
			localized(state, "katechon_fin_offer_tip", "The finance minister offers the bonds. The bank that underwrites them pays from its reserves"), 14);
		cy += 32.f;
		auto const notice = governance::finance::last_debt(state, state.local_player_nation);
		if(notice.outcome != governance::finance::debt_outcome::none)
			fin_text(state, fin_fit(state, fin_debt_words(state, notice), 14, w - 44.f), x + 22.f, std::min(cy, y + h - 16.f), 14,
				notice.outcome == governance::finance::debt_outcome::placed || notice.outcome == governance::finance::debt_outcome::repaid ? fin_green_text : fin_gold_light);
		panels.push_back(rect{ x, y, w, h });
	}
}

void fin_policy_page(sys::state& state, fin_layout const& l, bool hovering, float mx, float my) {
	auto const& r = fin_rep;
	auto const top = l.body_top;
	auto const bottom = l.nav_top - 10.f;
	auto const policy_w = std::round(l.main_w * 0.55f);
	fin_policy(state, l.left, top, policy_w, bottom - top, hovering, mx, my);
	auto const rates = fin_draft_rates(state);
	auto const arrow = [&](float now, float then, int digits) {
		return std::abs(now - then) > 0.0005f ? fin_pct(now, digits) + "  /  " + fin_pct(then, digits) : fin_pct(now, digits);
	};
	std::vector<fin_fact> law{
		fin_fact{ fin_icon::count, {}, localized(state, "katechon_fin_law_vs_draft", "Law in force  /  proposal") },
		fin_fact{ fin_icon::bank_coin, localized(state, "katechon_fin_base_rate", "Base Rate"), arrow(r.base_rate, fin_draft[0], 1) },
		fin_fact{ fin_icon::progressivity, localized(state, "katechon_fin_progressivity", "Progressivity"), arrow(r.progressivity, fin_draft[1], 0) },
		fin_fact{ fin_icon::person_one, localized(state, "katechon_fin_poor", "Poor"), arrow(r.stratum_rates[0], rates[0], 1) },
		fin_fact{ fin_icon::person_two, localized(state, "katechon_fin_middle", "Middle"), arrow(r.stratum_rates[1], rates[1], 1) },
		fin_fact{ fin_icon::person_crown, localized(state, "katechon_fin_rich", "Rich"), arrow(r.stratum_rates[2], rates[2], 1) },
		fin_fact{ fin_icon::book, localized(state, "katechon_fin_education", "Education"), arrow(r.priorities[0], fin_draft[2], 0) },
		fin_fact{ fin_icon::shield, localized(state, "katechon_fin_public_order", "Public Order"), arrow(r.priorities[1], fin_draft[3], 0) },
		fin_fact{ fin_icon::aqueduct, localized(state, "katechon_fin_infrastructure", "Infrastructure"), arrow(r.priorities[2], fin_draft[4], 0) },
		fin_fact{ fin_icon::facade, localized(state, "katechon_fin_local_government", "Local Government"), arrow(r.priorities[3], fin_draft[5], 0) },
	};
	auto const x = l.left + policy_w + 10.f;
	fin_facts(state, x, top, l.left + l.main_w - x, bottom - top, localized(state, "katechon_fin_the_bill", "The Bill"), law, hovering, mx, my);
	auto const notice = governance::finance::last_bill(state, state.local_player_nation);
	std::vector<fin_fact> process{
		fin_fact{ fin_icon::calendar, localized(state, "katechon_fin_next_bill", "Next fiscal bill"), r.next_bill && state.current_date < r.next_bill ? fin_day(state, r.next_bill) : localized(state, "katechon_fin_now", "now"), fin_type,
			localized(state, "katechon_fin_next_bill_tip", "The chambers take one fiscal bill in thirty days, the government's or the player's") },
		fin_fact{ fin_icon::gavel, localized(state, "katechon_fin_last_bill", "Last bill laid"), notice.outcome == governance::finance::bill_outcome::none ? std::string("\xE2\x80\x94") : fin_day(state, notice.laid_on) },
		fin_fact{ fin_icon::count, {}, notice.outcome == governance::finance::bill_outcome::none ? localized(state, "katechon_fin_no_bill", "No bill laid yet") : fin_bill_words(state, notice) },
		fin_fact{ fin_icon::people, localized(state, "katechon_fin_seats", "Seats in the deciding chambers"), notice.seats ? std::to_string(notice.seats) : std::string("\xE2\x80\x94") },
		fin_fact{ fin_icon::count, {}, localized(state, "katechon_fin_how", "How a bill passes") },
		fin_fact{ fin_icon::count, localized(state, "katechon_fin_how_1", "Each member weighs it against their own income"), {} },
		fin_fact{ fin_icon::count, localized(state, "katechon_fin_how_2", "and their party's platform. Governing parties"), {} },
		fin_fact{ fin_icon::count, localized(state, "katechon_fin_how_3", "back the government. Assent follows the law."), {} },
	};
	fin_facts(state, l.side_x, top, l.side_w, l.body_bottom - top, localized(state, "katechon_fin_legislation", "Legislation"), process, hovering, mx, my);
	if(fin_dirty()) {
		auto const reset_w = 120.f;
		fin_button_at(state, l.left + policy_w - 18.f - reset_w, bottom - 44.f, reset_w, 28.f, localized(state, "katechon_fin_reset", "Keep the law"), fin_icon::count, false, true,
			hit_target{ 0.f, 0.f, 0.f, 0.f, action::fin_reset }, hovering, mx, my, localized(state, "katechon_fin_reset_tip", "Put the sliders back to the law in force"), 13);
	}
}

void draw_finance(sys::state& state, float w, float h, bool hovering, float mx, float my) {
	if(!fin_art_read)
		fin_load(state);
	fin_meshes_used = 0;
	fin_refresh(state);
	if(fin_notice_frames > 0)
		--fin_notice_frames;
	auto const l = fin_lay(w, h);
	panels.push_back(rect{ ribbon_w, top_h - 3.f, w - ribbon_w, h - top_h + 3.f });
	fin_backdrop(state, l, w, h);
	fin_heading(state, l, hovering, mx, my);
	fin_cards(state, l, hovering, mx, my);
	switch(fin_page) {
	case fin_page_kind::revenue:
		fin_revenue_page(state, l, hovering, mx, my);
		break;
	case fin_page_kind::spending:
		fin_spending_page(state, l, hovering, mx, my);
		break;
	case fin_page_kind::debt:
		fin_debt_page(state, l, hovering, mx, my);
		break;
	case fin_page_kind::policy:
		fin_policy_page(state, l, hovering, mx, my);
		break;
	default: {
		auto const bottom = l.nav_top - 10.f;
		auto const tables_h = std::round(std::max(250.f, (bottom - l.body_top) * 0.47f));
		auto const chart_h = bottom - l.body_top - tables_h - 10.f;
		fin_chart(state, l.left, l.body_top, l.main_w, chart_h, localized(state, "katechon_fin_chart", "Revenue & Spending"), fin_chart_spec{}, hovering, mx, my);
		auto const table_w = std::floor((l.main_w - 10.f) * 0.5f);
		auto const ty = l.body_top + chart_h + 10.f;
		fin_table(state, l.left, ty, table_w, tables_h, localized(state, "katechon_fin_by_source", "Revenue by Source"), localized(state, "katechon_fin_source", "Source"),
			fin_revenue_rows(state), fin_rep.revenue_total, ogl::color3f{ 0.31f, 0.64f, 0.53f }, false, hovering, mx, my);
		fin_table(state, l.left + table_w + 10.f, ty, l.main_w - table_w - 10.f, tables_h, localized(state, "katechon_fin_by_function", "Spending by Function"),
			localized(state, "katechon_fin_function", "Function"), fin_spending_rows(state), fin_rep.spending_total, ogl::color3f{ 0.72f, 0.56f, 0.33f }, false, hovering, mx, my);
		auto const policy_h = std::round((l.body_bottom - l.body_top - 10.f) * 0.665f);
		fin_policy(state, l.side_x, l.body_top, l.side_w, policy_h, hovering, mx, my);
		fin_debt(state, l.side_x, l.body_top + policy_h + 10.f, l.side_w, l.body_bottom - l.body_top - policy_h - 10.f, hovering, mx, my);
		break;
	}
	}
	fin_navigation(state, l.left + 70.f, l.nav_top, l.main_w - 140.f, l.nav_h, hovering, mx, my);
	if(fin_step_menu) // the menu of the chart lies over the tables
		fin_step_menu_over(state, l, hovering, mx, my);
}
// The chart's menu of steps opens over the panels under the chart, so it is drawn last.
void fin_step_menu_over(sys::state& state, fin_layout const& l, bool hovering, float mx, float my) {
	if(fin_page == fin_page_kind::policy)
		return;
	using governance::finance::fiscal_step;
	auto const step_names = std::array<std::string, 3>{ localized(state, "katechon_fin_daily", "Daily"), localized(state, "katechon_fin_weekly", "Weekly"), localized(state, "katechon_fin_monthly", "Monthly") };
	auto const menu_w = 150.f;
	auto const mx0 = l.left + l.main_w - 20.f - menu_w;
	auto const my0 = l.body_top + 12.f + 30.f + 4.f;
	std::vector<int32_t> allowed;
	for(int32_t s = 0; s < 3; ++s)
		if(governance::finance::step_allowed(fin_period, fiscal_step(s)))
			allowed.push_back(s);
	auto const mh = float(allowed.size()) * 28.f + 8.f;
	fill(state, mx0 + 3.f, my0 + 4.f, menu_w, mh, fin_black, 0.4f);
	fin_plate(state, fin_inset, mx0, my0, menu_w, mh, 64.f, 64.f, 10.f, 5.f);
	for(size_t i = 0; i < allowed.size(); ++i) {
		hit_target t{ mx0 + 4.f, my0 + 4.f + float(i) * 28.f, menu_w - 8.f, 28.f, action::fin_step, nullptr, allowed[i] };
		auto const on = int32_t(fin_rep.step) == allowed[i];
		if(on || (hovering && over(t, mx, my)))
			fill(state, t.x, t.y, t.w, t.h, fin_gold, on ? 0.14f : 0.07f);
		fin_text(state, step_names[size_t(allowed[i])], t.x + 14.f, t.y + 14.f, 15, on ? fin_gold_light : fin_type);
		targets.push_back(t);
	}
	panels.push_back(rect{ mx0, my0, menu_w, mh });
}
