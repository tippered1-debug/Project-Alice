#pragma once

#include <array>
#include <chrono>

#include "system_state.hpp"
#include "gui_element_types.hpp"
#include "gui_containers.hpp"
#include "serialization.hpp"
#include "commands.hpp"
#include "game_scene_constants.hpp"
#include "game_scene_title.hpp"
#include "alice_ui.hpp"

namespace game_scene {
void switch_scene(sys::state& state, scene_id ui_scene);
}

namespace ui {

// Shared drawing vocabulary of the KATECHON shell: tracked serif capitals in
// ivory and gold over dark washes. Screens derive from it so they cannot drift
// apart in type, colour or spacing.
class katechon_canvas : public element_base {
protected:
	struct text_run {
		text::stored_glyphs glyphs;
		float width = 0.f;

		text_run(text::stored_glyphs&& g) : glyphs(std::move(g)) { }
	};

	inline static constexpr ogl::color3f ivory{ 0.80f, 0.78f, 0.73f };
	inline static constexpr ogl::color3f gold{ 0.95f, 0.76f, 0.38f };
	inline static constexpr ogl::color3f dim{ 0.40f, 0.39f, 0.37f };
	inline static constexpr ogl::color3f muted{ 0.58f, 0.56f, 0.52f };

	ankerl::unordered_dense::map<std::string, std::unique_ptr<text_run>> text_cache;
	float cache_scale = 0.f;
	int32_t cache_height = 0;
	int32_t cache_locale = -1;
	bool cache_classic = false;

	static std::string localized(sys::state& state, std::string_view key, std::string_view fallback) {
		if(state.key_is_localized(key))
			return text::produce_simple_string(state, key);
		return std::string(fallback);
	}

	static void play_click(sys::state& state) {
		sound::play_interface_sound(state, sound::get_click_sound(state), state.user_settings.interface_volume * state.user_settings.master_volume);
	}

	text_run const& shaped(sys::state& state, std::string_view content, int32_t size, float tracking) {
		auto const locale = state.font_collection.get_current_locale().index();
		auto const classic = state.user_settings.use_classic_fonts;
		if(cache_scale != state.user_settings.ui_scale || cache_height != state.y_size || cache_locale != locale || cache_classic != classic || text_cache.size() > 512) {
			text_cache.clear();
			cache_scale = state.user_settings.ui_scale;
			cache_height = state.y_size;
			cache_locale = locale;
			cache_classic = classic;
		}

		auto key = std::string(content) + '\x1f' + std::to_string(size) + '\x1f' + std::to_string(int32_t(tracking * 1000.f));
		if(auto it = text_cache.find(key); it != text_cache.end())
			return *it->second;

		auto utf16 = simple_fs::utf8_to_utf16(content);
		auto span = std::span<uint16_t>(reinterpret_cast<uint16_t*>(utf16.data()), utf16.size());
		if(classic) {
			auto const font_id = state.ui_state.default_header_font;
			auto run = std::make_unique<text_run>(text::stored_glyphs(state, text::size_from_font_id(font_id), text::font_selection::header_font, span, 0, nullptr, font_id));
			run->width = state.font_collection.text_extent(state, run->glyphs, 0, uint32_t(run->glyphs.glyph_info.size()), font_id);
			return *text_cache.emplace(std::move(key), std::move(run)).first->second;
		}
		auto run = std::make_unique<text_run>(text::stored_glyphs(state, size, text::font_selection::body_font, span, text::stored_glyphs::no_bidi{}));
		{
			auto const extra = int32_t(tracking * float(size) * state.user_settings.ui_scale * text::fixed_to_fp);
			float total = 0.f;
			for(auto& g : run->glyphs.glyph_info) {
				g.x_advance += extra;
				total += float(g.x_advance) / text::fixed_to_fp;
			}
			if(!run->glyphs.glyph_info.empty())
				total -= float(extra) / text::fixed_to_fp;
			run->width = total / state.user_settings.ui_scale;
		}
		return *text_cache.emplace(std::move(key), std::move(run)).first->second;
	}

	float draw_text(sys::state& state, std::string_view content, float x, float center_y, int32_t size, float tracking, ogl::color3f color, bool align_right = false) {
		if(content.empty() || size <= 0)
			return 0.f;
		auto const& run = shaped(state, content, size, tracking);
		auto const left = align_right ? x - run.width : x;
		if(state.user_settings.use_classic_fonts) {
			auto const font_id = state.ui_state.default_header_font;
			ogl::render_text(state, run.glyphs, ogl::color_modification::none, left, center_y - state.font_collection.line_height(state, font_id) * 0.5f, color, font_id);
		} else {
			auto& font = state.font_collection.get_font(state, text::font_selection::body_font);
			ogl::render_new_text(state, run.glyphs, ogl::color_modification::none, left, center_y - float(size) * 0.66f, float(size), color, font);
		}
		return run.width;
	}

	static ogl::color3f mix(ogl::color3f a, ogl::color3f b, float t) {
		return ogl::color3f{ a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t };
	}

	void draw_rule(sys::state& state, float x, float y, float width, float alpha) {
		ogl::render_alpha_colored_rect(state, x, y, width, 1.f, gold.r * 0.8f, gold.g * 0.8f, gold.b * 0.8f, alpha);
	}

	// Darkens a vertical band; alpha runs from alpha_left to alpha_right.
	void draw_wash(sys::state& state, float x, float width, float h, float alpha_left, float alpha_right) {
		constexpr int32_t strips = 24;
		auto const strip_w = width / float(strips);
		for(int32_t i = 0; i < strips; ++i) {
			auto const t = (float(i) + 0.5f) / float(strips);
			ogl::render_alpha_colored_rect(state, x + float(i) * strip_w, 0.f, strip_w + 1.f, h, 0.02f, 0.025f, 0.04f, alpha_left + (alpha_right - alpha_left) * t);
		}
	}

	// The gold bar and glow that mark the active row.
	void draw_row_marker(sys::state& state, float x, float center_y, float height, float u, float amount) {
		if(amount <= 0.01f)
			return;
		auto const top = center_y - height * 0.5f;
		constexpr int32_t strips = 70;
		auto const strip_w = 1.f * u;
		for(int32_t i = 0; i < strips; ++i) {
			auto const t = 1.f - float(i) / float(strips);
			ogl::render_alpha_colored_rect(state, x + float(i) * strip_w, top, strip_w, height, gold.r, gold.g, gold.b, 0.10f * t * t * amount);
		}
		ogl::render_alpha_colored_rect(state, x, top, std::max(1.5f, 2.f * u), height, gold.r, gold.g, gold.b, amount);
	}
};

// The KATECHON title screen. It is a single element that fills the window and
// draws itself: the hall artwork, the wordmark, and a column of text entries.
// All positions are given in the coordinate space of the 1672 x 941 artwork and
// scaled by the window height, so the composition holds at any resolution.
class title_screen_element : public katechon_canvas {
	enum class page : uint8_t { main, load, scenarios, multiplayer, credits };
	enum class action : uint8_t {
		none, continue_game, new_game, open_load, open_multiplayer, open_scenarios, open_settings, open_credits, exit, back, load_entry
	};

	struct menu_item {
		std::string_view key;
		std::string_view fallback;
		action act = action::none;
	};
	struct save_entry {
		native_string file_name;
		std::string name;
		std::string detail;
		uint64_t timestamp = 0;
		bool is_new_game = false;
	};
	struct hit_target {
		float x = 0.f;
		float y = 0.f;
		float w = 0.f;
		float h = 0.f;
		action act = action::none;
		int32_t entry = -1;
		bool enabled = true;
	};

	inline static constexpr float design_w = 1672.f;
	inline static constexpr float design_h = 941.f;
	inline static constexpr float menu_x = 122.f;
	inline static constexpr float marker_x = 88.f;
	inline static constexpr float menu_top = 338.f;
	inline static constexpr float menu_pitch = 46.f;
	inline static constexpr float list_top = 388.f;
	inline static constexpr float list_pitch = 44.f;
	inline static constexpr float list_right = 560.f;
	inline static constexpr int32_t list_rows = 6;
	inline static constexpr float back_y = 660.f;


	inline static constexpr std::array<menu_item, 8> main_items{
		menu_item{ "katechon_menu_continue", "CONTINUE", action::continue_game },
		menu_item{ "katechon_menu_new_game", "NEW GAME", action::new_game },
		menu_item{ "katechon_menu_load_game", "LOAD GAME", action::open_load },
		menu_item{ "katechon_menu_multiplayer", "MULTIPLAYER", action::open_multiplayer },
		menu_item{ "katechon_menu_scenarios", "SCENARIOS", action::open_scenarios },
		menu_item{ "katechon_menu_settings", "SETTINGS", action::open_settings },
		menu_item{ "katechon_menu_credits", "CREDITS", action::open_credits },
		menu_item{ "katechon_menu_exit", "EXIT", action::exit }
	};

	GLuint bg_texture = 0;
	GLuint logo_texture = 0;
	GLuint crest_texture = 0;
	float bg_w = design_w;
	float bg_h = design_h;
	bool textures_loaded = false;

	page current_page = page::main;
	int32_t selected = 1;
	int32_t main_selected = 1;
	int32_t scroll = 0;
	std::vector<save_entry> entries;
	save_entry latest_save;
	bool has_latest_save = false;
	bool saves_scanned = false;

	std::vector<hit_target> targets;
	std::array<float, 16> glow{};
	float last_mouse_x = -1.f;
	float last_mouse_y = -1.f;
	float fade_in = 0.f;
	std::chrono::steady_clock::time_point last_frame{};



	static void set_linear_filtering(GLuint texture) {
		if(!texture)
			return;
		glBindTexture(GL_TEXTURE_2D, texture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	void load_textures(sys::state& state) {
		textures_loaded = true;
		auto root = simple_fs::get_root(state.common_fs);
		auto assets = simple_fs::open_directory(root, NATIVE("assets"));
		bg_texture = ogl::make_gl_texture(assets, NATIVE("katechon_title_bg.png"));
		logo_texture = ogl::make_gl_texture(assets, NATIVE("katechon_title_logo.png"));
		crest_texture = ogl::make_gl_texture(assets, NATIVE("katechon_title_crest.png"));
		set_linear_filtering(bg_texture);
		set_linear_filtering(logo_texture);
		set_linear_filtering(crest_texture);
		if(bg_texture) {
			GLint w = 0;
			GLint h = 0;
			glBindTexture(GL_TEXTURE_2D, bg_texture);
			glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
			glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
			glBindTexture(GL_TEXTURE_2D, 0);
			if(w > 0 && h > 0) {
				bg_w = float(w);
				bg_h = float(h);
			}
		}
	}

	void scan_saves(sys::state& state, bool bookmarks) {
		entries.clear();
		has_latest_save = false;
		saves_scanned = true;

		if(bookmarks) {
			entries.push_back(save_entry{ NATIVE(""), localized(state, "katechon_scenarios_campaign", "Grand campaign"), std::string(), 0, true });
		}

		auto sdir = simple_fs::get_or_create_save_game_directory(state.mod_save_dir);
		for(auto& f : simple_fs::list_files(sdir, NATIVE(".bin"))) {
			auto of = simple_fs::open_file(f);
			if(!of)
				continue;
			auto content = simple_fs::view_contents(*of);
			sys::save_header h;
			if(content.file_size <= sys::sizeof_save_header(h))
				continue;
			sys::read_save_header(reinterpret_cast<uint8_t const*>(content.data), h);
			if(!h.checksum.is_equal(state.scenario_checksum))
				continue;

			save_entry e;
			e.file_name = simple_fs::get_file_name(f);
			e.timestamp = h.timestamp;
			e.detail = text::date_to_string(state, h.d);
			auto const is_bookmark = e.file_name.starts_with(NATIVE("bookmark_"));
			auto const stored_name = std::string(h.save_name);
			if(is_bookmark) {
				e.name = text::produce_simple_string(state, stored_name);
			} else if(stored_name == simple_fs::native_to_utf8(e.file_name)) {
				// A default file name carries no meaning; show the country instead.
				auto name = text::get_name(state, state.world.national_identity_get_nation_from_identity_holder(h.tag));
				if(auto gov_name = state.world.national_identity_get_government_name(h.tag, h.cgov); state.key_is_localized(gov_name)) {
					name = gov_name;
				}
				e.name = text::produce_simple_string(state, name);
			} else {
				e.name = simple_fs::native_to_utf8(simple_fs::remove_file_extension(e.file_name));
			}

			if(!is_bookmark && (!has_latest_save || e.timestamp > latest_save.timestamp)) {
				latest_save = e;
				has_latest_save = true;
			}
			if(is_bookmark == bookmarks)
				entries.push_back(std::move(e));
		}

		auto first_sorted = entries.begin() + (bookmarks ? 1 : 0);
		std::sort(first_sorted, entries.end(), [bookmarks](save_entry const& a, save_entry const& b) {
			if(bookmarks)
				return a.name < b.name;
			return a.timestamp > b.timestamp;
		});
	}

	bool overlay_open() const {
		if(!parent)
			return false;
		for(auto& c : static_cast<container_base*>(parent)->children) {
			if(c.get() != this && c->is_visible())
				return true;
		}
		return false;
	}

	void close_overlays(sys::state& state) {
		if(!parent)
			return;
		for(auto& c : static_cast<container_base*>(parent)->children) {
			if(c.get() != this && c->is_visible())
				c->set_visible(state, false);
		}
	}


	void open_page(sys::state& state, page p) {
		if(current_page == page::main)
			main_selected = selected;
		current_page = p;
		scroll = 0;
		selected = 0;
		glow.fill(0.f);
		if(p == page::main) {
			selected = main_selected;
			scan_saves(state, false);
		} else if(p == page::load) {
			scan_saves(state, false);
		} else if(p == page::scenarios) {
			scan_saves(state, true);
		}
	}

	void load_entry(sys::state& state, save_entry const& e, bool start_immediately) {
		// Saves are loaded by the lobby's own command path, which only accepts
		// them while the lobby scene is active.
		game_scene::switch_scene(state, game_scene::scene_id::pick_nation);
		command::load_save_game(state, simple_fs::native_to_utf8(e.file_name), e.is_new_game);
		if(start_immediately)
			command::notify_start_game(state);
	}

	void activate(sys::state& state, hit_target const& t) {
		if(!t.enabled || t.act == action::none)
			return;
		play_click(state);
		switch(t.act) {
		case action::continue_game:
			if(has_latest_save)
				load_entry(state, latest_save, true);
			break;
		case action::new_game:
			game_scene::switch_scene(state, game_scene::scene_id::pick_nation);
			// Neither a loaded save nor an abandoned session may leak into a
			// new game.
			if(!state.loaded_save_file.empty() || game_scene::world_played_since_load) {
				game_scene::world_played_since_load = false;
				command::load_save_game(state, std::string(), true);
			}
			break;
		case action::open_load:
			open_page(state, page::load);
			break;
		case action::open_multiplayer:
			open_page(state, page::multiplayer);
			break;
		case action::open_scenarios:
			open_page(state, page::scenarios);
			break;
		case action::open_settings:
			alice_ui::display_at_front<alice_ui::make_main_menu_base>(state);
			break;
		case action::open_credits:
			open_page(state, page::credits);
			break;
		case action::exit:
			window::close_window(state);
			break;
		case action::back:
			open_page(state, page::main);
			break;
		case action::load_entry:
			if(t.entry >= 0 && t.entry < int32_t(entries.size()))
				load_entry(state, entries[size_t(t.entry)], false);
			break;
		case action::none:
			break;
		}
	}

	void move_selection(int32_t direction) {
		auto const count = int32_t(targets.size());
		if(count == 0)
			return;
		auto next = selected;
		for(int32_t i = 0; i < count; ++i) {
			next = (next + direction + count) % count;
			if(targets[size_t(next)].enabled) {
				selected = next;
				return;
			}
		}
	}

	void scroll_by(int32_t rows) {
		auto const max_scroll = std::max(0, int32_t(entries.size()) - list_rows);
		scroll = std::clamp(scroll + rows, 0, max_scroll);
	}


	// Draws one line with its capitals centred on center_y. Returns the width.


	void draw_background(sys::state& state, float w, float h) {
		if(!bg_texture) {
			ogl::render_alpha_colored_rect(state, 0.f, 0.f, w, h, 0.02f, 0.025f, 0.04f, 1.f);
			return;
		}
		// Cover the window, keeping the left edge of the artwork in place.
		auto const scale = std::max(w / bg_w, h / bg_h);
		auto const src_w = std::min(1.f, w / (scale * bg_w));
		auto const src_h = std::min(1.f, h / (scale * bg_h));
		glBindVertexArray(state.open_gl.global_square_vao);
		ogl::render_subrect(state, 0.f, 0.f, w, h, 0.f, (1.f - src_h) * 0.5f, src_w, src_h, bg_texture);
	}

	// Sub-pages reach further right than the menu, over brighter parts of the
	// painting, so they get a darkening wash that fades out towards the hall.
	void draw_reading_wash(sys::state& state, float u, float h, float strength) {
		constexpr int32_t strips = 28;
		auto const width = 760.f * u;
		auto const strip_w = width / float(strips);
		for(int32_t i = 0; i < strips; ++i) {
			auto const t = float(i) / float(strips - 1);
			auto const alpha = strength * (1.f - t * t);
			ogl::render_alpha_colored_rect(state, float(i) * strip_w, 0.f, strip_w + 1.f, h, 0.015f, 0.018f, 0.03f, alpha);
		}
	}

	void draw_marker(sys::state& state, float u, float center_y, float amount) {
		if(amount <= 0.01f)
			return;
		auto const height = 43.f * u;
		auto const top = center_y - height * 0.5f;
		constexpr int32_t strips = 70;
		auto const strip_w = 1.f * u;
		for(int32_t i = 0; i < strips; ++i) {
			auto const t = 1.f - float(i) / float(strips);
			ogl::render_alpha_colored_rect(state, marker_x * u + float(i) * strip_w, top, strip_w, height, gold.r, gold.g, gold.b, 0.10f * t * t * amount);
		}
		ogl::render_alpha_colored_rect(state, marker_x * u, top, std::max(1.5f, 2.f * u), height, gold.r, gold.g, gold.b, amount);
	}


	void draw_page_header(sys::state& state, float u, std::string_view key, std::string_view fallback) {
		draw_text(state, localized(state, key, fallback), menu_x * u, 326.f * u, int32_t(19.f * u), 0.22f, gold);
		draw_rule(state, menu_x * u, 350.f * u, (list_right - menu_x) * u, 0.45f);
	}

	void add_entry_target(float u, float center_y, float pitch, float width, action act, int32_t entry, bool enabled) {
		targets.push_back(hit_target{ (marker_x - 10.f) * u, center_y - pitch * u * 0.5f, width + (menu_x - marker_x + 40.f) * u, pitch * u, act, entry, enabled });
	}

	void draw_main(sys::state& state, float u) {
		auto const size = int32_t(23.f * u);
		for(size_t i = 0; i < main_items.size(); ++i) {
			auto const& item = main_items[i];
			auto const enabled = item.act != action::continue_game || has_latest_save;
			auto const center_y = (menu_top + float(i) * menu_pitch) * u;
			auto const amount = glow[i];
			draw_marker(state, u, center_y, amount);
			auto const color = enabled ? mix(ivory, gold, amount) : dim;
			auto const width = draw_text(state, localized(state, item.key, item.fallback), menu_x * u, center_y, size, 0.16f, color);
			add_entry_target(u, center_y, menu_pitch, width, item.act, -1, enabled);
		}
	}

	void draw_back(sys::state& state, float u) {
		auto const index = targets.size();
		auto const center_y = back_y * u;
		auto const amount = index < glow.size() ? glow[index] : 0.f;
		draw_marker(state, u, center_y, amount);
		auto const width = draw_text(state, localized(state, "katechon_menu_back", "BACK"), menu_x * u, center_y, int32_t(23.f * u), 0.16f, mix(ivory, gold, amount));
		add_entry_target(u, center_y, menu_pitch, width, action::back, -1, true);
	}

	void draw_list(sys::state& state, float u, std::string_view empty_key, std::string_view empty_fallback) {
		if(entries.empty()) {
			draw_text(state, localized(state, empty_key, empty_fallback), menu_x * u, list_top * u, int32_t(18.f * u), 0.02f, muted);
		}
		scroll_by(0);
		auto const visible = std::min(list_rows, int32_t(entries.size()) - scroll);
		for(int32_t row = 0; row < visible; ++row) {
			auto const index = scroll + row;
			auto const& e = entries[size_t(index)];
			auto const center_y = (list_top + float(row) * list_pitch) * u;
			auto const amount = size_t(row) < glow.size() ? glow[size_t(row)] : 0.f;
			draw_marker(state, u, center_y, amount);
			draw_text(state, e.name, menu_x * u, center_y, int32_t(20.f * u), 0.03f, mix(ivory, gold, amount));
			draw_text(state, e.detail, list_right * u, center_y, int32_t(16.f * u), 0.03f, mix(muted, ivory, amount), true);
			add_entry_target(u, center_y, list_pitch, (list_right - menu_x) * u, action::load_entry, index, true);
		}
		if(int32_t(entries.size()) > list_rows) {
			// A slim position indicator; the wheel and arrow keys do the scrolling.
			auto const track_top = (list_top - list_pitch * 0.5f) * u;
			auto const track_h = float(list_rows) * list_pitch * u;
			auto const thumb_h = track_h * float(list_rows) / float(entries.size());
			auto const thumb_y = track_top + (track_h - thumb_h) * float(scroll) / float(int32_t(entries.size()) - list_rows);
			auto const x = (list_right + 18.f) * u;
			ogl::render_alpha_colored_rect(state, x, track_top, 1.f, track_h, gold.r, gold.g, gold.b, 0.18f);
			ogl::render_alpha_colored_rect(state, x - 1.f, thumb_y, 3.f, thumb_h, gold.r, gold.g, gold.b, 0.75f);
		}
	}

	void draw_paragraph(sys::state& state, float u, std::initializer_list<std::pair<std::string_view, std::string_view>> lines) {
		auto y = list_top;
		for(auto const& line : lines) {
			if(!line.first.empty())
				draw_text(state, localized(state, line.first, line.second), menu_x * u, y * u, int32_t(18.f * u), 0.02f, ivory);
			y += 30.f;
		}
	}

	void update_selection_from_mouse(sys::state& state, float mouse_x, float mouse_y) {
		if(mouse_x == last_mouse_x && mouse_y == last_mouse_y)
			return;
		last_mouse_x = mouse_x;
		last_mouse_y = mouse_y;
		if(state.ui_state.under_mouse != this)
			return;
		for(size_t i = 0; i < targets.size(); ++i) {
			auto const& t = targets[i];
			if(t.enabled && mouse_x >= t.x && mouse_x <= t.x + t.w && mouse_y >= t.y && mouse_y < t.y + t.h) {
				selected = int32_t(i);
				return;
			}
		}
	}

public:
	// The page is rebuilt from scratch each time the scene is entered.
	void reset(sys::state& state) {
		current_page = page::main;
		selected = 1;
		main_selected = 1;
		scroll = 0;
		glow.fill(0.f);
		fade_in = 0.f;
		last_frame = std::chrono::steady_clock::time_point{};
		scan_saves(state, false);
		if(has_latest_save) {
			selected = 0;
			main_selected = 0;
		}
	}

	mouse_probe impl_probe_mouse(sys::state& state, int32_t x, int32_t y, mouse_probe_type type) noexcept override {
		return mouse_probe{ this, xy_pair{ int16_t(x), int16_t(y) } };
	}

	message_result on_lbutton_down(sys::state& state, int32_t x, int32_t y, sys::key_modifiers mods) noexcept override {
		// A click beside an open window must not reach the menu underneath it.
		if(overlay_open())
			return message_result::consumed;
		for(size_t i = 0; i < targets.size(); ++i) {
			auto const t = targets[i];
			if(float(x) >= t.x && float(x) <= t.x + t.w && float(y) >= t.y && float(y) < t.y + t.h) {
				if(t.enabled)
					selected = int32_t(i);
				activate(state, t);
				break;
			}
		}
		return message_result::consumed;
	}

	message_result on_rbutton_down(sys::state& state, int32_t x, int32_t y, sys::key_modifiers mods) noexcept override {
		if(current_page != page::main && !overlay_open())
			open_page(state, page::main);
		return message_result::consumed;
	}

	message_result on_scroll(sys::state& state, int32_t x, int32_t y, float amount, sys::key_modifiers mods) noexcept override {
		if(current_page == page::load || current_page == page::scenarios)
			scroll_by(amount > 0.f ? -1 : 1);
		return message_result::consumed;
	}

	message_result on_key_down(sys::state& state, sys::virtual_key key, sys::key_modifiers mods) noexcept override {
		if(overlay_open()) {
			if(key == sys::virtual_key::ESCAPE)
				close_overlays(state);
			return message_result::consumed;
		}
		auto const is_list = current_page == page::load || current_page == page::scenarios;
		switch(key) {
		case sys::virtual_key::UP:
			if(is_list && selected == 0 && scroll > 0)
				scroll_by(-1);
			else
				move_selection(-1);
			break;
		case sys::virtual_key::DOWN:
			if(is_list && selected == list_rows - 1 && scroll + list_rows < int32_t(entries.size()))
				scroll_by(1);
			else
				move_selection(1);
			break;
		case sys::virtual_key::RETURN:
			if(selected >= 0 && selected < int32_t(targets.size())) {
				auto const t = targets[size_t(selected)];
				activate(state, t);
			}
			break;
		case sys::virtual_key::ESCAPE:
			if(current_page != page::main)
				open_page(state, page::main);
			break;
		default:
			return message_result::unseen;
		}
		return message_result::consumed;
	}

	void render(sys::state& state, int32_t x, int32_t y) noexcept override {
		if(!textures_loaded)
			load_textures(state);
		if(!saves_scanned || state.ui_state.title_screen_entered) {
			state.ui_state.title_screen_entered = false;
			reset(state);
			// Visual QA hook, like ALICE_TRADE_DASHBOARD_PREVIEW: open the system
			// menu at start so it can be captured without synthetic clicks.
			if(auto const* open_menu = std::getenv("KATECHON_OPEN_SYSTEM_MENU"); open_menu && open_menu[0] == '1')
				alice_ui::display_at_front<alice_ui::make_main_menu_base>(state);
		}

		auto const now = std::chrono::steady_clock::now();
		auto dt = 0.f;
		if(last_frame != std::chrono::steady_clock::time_point{})
			dt = std::min(0.1f, std::chrono::duration<float>(now - last_frame).count());
		last_frame = now;
		fade_in = std::min(1.f, fade_in + dt / 0.9f);

		auto const w = float(state.x_size) / state.user_settings.ui_scale;
		auto const h = float(state.y_size) / state.user_settings.ui_scale;
		auto const u = h / design_h;

		// Map scenes finish by returning to the window's framebuffer. This scene
		// draws no map, so it has to select it itself; otherwise the frame lands
		// in whichever offscreen buffer start-up left bound.
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glViewport(0, 0, state.x_size, state.y_size);
		draw_background(state, w, h);
		if(current_page != page::main)
			draw_reading_wash(state, u, h, 0.62f);
		if(logo_texture)
			ogl::render_textured_rect(state, ogl::color_modification::none, 66.f * u, 58.f * u, 506.f * u, 220.f * u, logo_texture, ui::rotation::upright, false, false);
		if(crest_texture)
			ogl::render_textured_rect(state, ogl::color_modification::none, 70.f * u, 688.f * u, 212.f * u, 234.f * u, crest_texture, ui::rotation::upright, false, false);

		targets.clear();
		switch(current_page) {
		case page::main:
			draw_main(state, u);
			break;
		case page::load:
			draw_page_header(state, u, "katechon_menu_load_game", "LOAD GAME");
			draw_list(state, u, "katechon_load_empty", "No saved games yet.");
			draw_back(state, u);
			break;
		case page::scenarios:
			draw_page_header(state, u, "katechon_menu_scenarios", "SCENARIOS");
			draw_list(state, u, "katechon_scenarios_empty", "No scenarios installed.");
			draw_text(state, localized(state, "katechon_scenarios_desc", "Historical and alternative starting conditions."), menu_x * u, 366.f * u, int32_t(15.f * u), 0.02f, muted);
			draw_back(state, u);
			break;
		case page::multiplayer:
			draw_page_header(state, u, "katechon_menu_multiplayer", "MULTIPLAYER");
			draw_paragraph(state, u, {
				{ "katechon_mp_line_1", "A multiplayer session is chosen when the game starts." },
				{ "katechon_mp_line_2", "Host:  launch with  -host -name YourName" },
				{ "katechon_mp_line_3", "Join:  launch with  -join address -name YourName" },
				{ "", "" },
				{ "katechon_mp_line_4", "The lobby opens directly in either mode." }
			});
			draw_back(state, u);
			break;
		case page::credits:
			draw_page_header(state, u, "katechon_menu_credits", "CREDITS");
			draw_paragraph(state, u, {
				{ "katechon_credits_line_1", "KATECHON" },
				{ "katechon_credits_line_2", "Built on the Project Alice engine by Schombert and its contributors," },
				{ "katechon_credits_line_3", "a continuation of the Open V2 codebase." },
				{ "", "" },
				{ "katechon_credits_line_4", "Typefaces: Libre Caslon Text, Source Serif 4, Noto Serif." },
				{ "katechon_credits_line_5", "Released under the GNU General Public License, version 3." }
			});
			draw_back(state, u);
			break;
		}

		if(selected >= int32_t(targets.size()))
			selected = std::max(0, int32_t(targets.size()) - 1);
		update_selection_from_mouse(state, float(state.mouse_x_position) / state.user_settings.ui_scale, float(state.mouse_y_position) / state.user_settings.ui_scale);
		auto const overlay = overlay_open();
		for(size_t i = 0; i < glow.size(); ++i) {
			auto const target = (!overlay && int32_t(i) == selected && i < targets.size() && targets[i].enabled) ? 1.f : 0.f;
			glow[i] += (target - glow[i]) * std::min(1.f, dt * 14.f);
		}

		if(overlay)
			ogl::render_alpha_colored_rect(state, 0.f, 0.f, w, h, 0.f, 0.f, 0.f, 0.55f);
		if(fade_in < 1.f)
			ogl::render_alpha_colored_rect(state, 0.f, 0.f, w, h, 0.f, 0.f, 0.f, (1.f - fade_in) * (1.f - fade_in));
	}
};

// Country selection in the KATECHON shell: the map stays in the middle, a
// ranked list of nations sits on the left and the chosen nation's summary on
// the right. It replaces the single-player lobby controls; the map itself
// still selects nations by click.
class country_select_element : public katechon_canvas {
	enum class action : uint8_t { none, pick, back, play };
	struct hit_target {
		float x = 0.f;
		float y = 0.f;
		float w = 0.f;
		float h = 0.f;
		action act = action::none;
		dcon::nation_id nation;
	};

	inline static constexpr float left_width = 430.f;
	inline static constexpr float right_width = 400.f;
	inline static constexpr float margin = 48.f;
	inline static constexpr float list_top = 168.f;
	inline static constexpr float row_pitch = 38.f;
	inline static constexpr float footer = 120.f;

	std::vector<dcon::nation_id> nations;
	std::vector<hit_target> targets;
	int32_t scroll = 0;
	int32_t visible_rows = 1;
	dcon::nation_id followed_nation;
	bool gliding = false;
	glm::vec2 glide_target{ 0.f, 0.f };
	std::chrono::steady_clock::time_point glide_last{};
	bool autostart_pending = true;

	static float unit(sys::state& state) {
		return (float(state.y_size) / state.user_settings.ui_scale) / 941.f;
	}
	static float width(sys::state& state) {
		return float(state.x_size) / state.user_settings.ui_scale;
	}
	static float height(sys::state& state) {
		return float(state.y_size) / state.user_settings.ui_scale;
	}

	void rebuild(sys::state& state) {
		nations.clear();
		for(auto n : state.world.in_nation) {
			if(n.get_owned_province_count() > 0)
				nations.push_back(n);
		}
		std::sort(nations.begin(), nations.end(), [&](dcon::nation_id a, dcon::nation_id b) {
			auto const ra = state.world.nation_get_rank(a);
			auto const rb = state.world.nation_get_rank(b);
			if(ra != rb)
				return ra < rb;
			return a.index() < b.index();
		});
	}

	static GLuint flag_of(sys::state& state, dcon::nation_id n) {
		auto const identity = state.world.nation_get_identity_from_identity_holder(n);
		auto const government = state.world.nation_get_government_type(n);
		dcon::government_flag_id flag_type{};
		if(government) {
			auto const own = state.world.national_identity_get_government_flag_type(identity, government);
			flag_type = own ? own : state.world.government_type_get_flag(government);
		}
		return ogl::get_flag_handle(state, identity, flag_type);
	}

	void draw_flag(sys::state& state, dcon::nation_id n, float x, float y, float w, float h) {
		ogl::render_alpha_colored_rect(state, x - 1.f, y - 1.f, w + 2.f, h + 2.f, gold.r * 0.6f, gold.g * 0.6f, gold.b * 0.6f, 0.9f);
		if(auto const handle = flag_of(state, n); handle)
			ogl::render_textured_rect(state, ogl::color_modification::none, x, y, w, h, handle, ui::rotation::upright, false, false);
	}

	void scroll_by(int32_t rows) {
		auto const max_scroll = std::max(0, int32_t(nations.size()) - visible_rows);
		scroll = std::clamp(scroll + rows, 0, max_scroll);
	}

	void follow_selection(sys::state& state) {
		// Bring a nation chosen on the map into view in the list, once.
		if(state.local_player_nation == followed_nation)
			return;
		followed_nation = state.local_player_nation;
		// and turn the map towards it; the glide gives way to the player's own hand
		if(auto capital = state.world.nation_get_capital(followed_nation); capital) {
			map_space::point map_pos = { state.world.province_get_mid_point(capital) };
			glide_target = map_space::inverted_from_point(map_pos, float(state.map_state.map_data.size_x), float(state.map_state.map_data.size_y)).data;
			gliding = true;
			glide_last = std::chrono::steady_clock::time_point{};
		}
		for(int32_t i = 0; i < int32_t(nations.size()); ++i) {
			if(nations[size_t(i)] == followed_nation) {
				if(i < scroll)
					scroll = i;
				else if(i >= scroll + visible_rows)
					scroll = i - visible_rows + 1;
				break;
			}
		}
	}

	bool over(hit_target const& t, float mx, float my) const {
		return mx >= t.x && mx <= t.x + t.w && my >= t.y && my < t.y + t.h;
	}

	void pick(sys::state& state, dcon::nation_id n) {
		if(command::can_notify_player_picks_nation(state, state.local_player_nation, n, state.local_player_id))
			command::notify_player_picks_nation(state, state.local_player_nation, n);
	}

	void activate(sys::state& state, hit_target const& t) {
		play_click(state);
		switch(t.act) {
		case action::pick:
			pick(state, t.nation);
			break;
		case action::back:
			game_scene::switch_scene(state, game_scene::scene_id::title_screen);
			break;
		case action::play:
			if(command::can_notify_start_game(state, state.local_player_nation))
				command::notify_start_game(state);
			break;
		case action::none:
			break;
		}
	}

	void step_selection(sys::state& state, int32_t direction) {
		if(nations.empty())
			return;
		int32_t current = -1;
		for(int32_t i = 0; i < int32_t(nations.size()); ++i) {
			if(nations[size_t(i)] == state.local_player_nation)
				current = i;
		}
		auto const next = std::clamp(current + direction, 0, int32_t(nations.size()) - 1);
		pick(state, nations[size_t(next)]);
	}

	// Visual QA hook: KATECHON_AUTOSTART=<tag> picks that nation and starts the
	// game, so that the map screen can be reached without synthetic clicks.
	void autostart(sys::state& state) {
		auto const* tag = std::getenv("KATECHON_AUTOSTART");
		if(!tag || !tag[0]) {
			autostart_pending = false;
			return;
		}
		auto const tag_of = [&](dcon::nation_id n) {
			return nations::int_to_tag(state.world.national_identity_get_identifying_int(state.world.nation_get_identity_from_identity_holder(n)));
		};
		if(state.local_player_nation && tag_of(state.local_player_nation) == tag) {
			if(command::can_notify_start_game(state, state.local_player_nation)) {
				command::notify_start_game(state);
				autostart_pending = false;
			}
			return;
		}
		for(auto n : nations) {
			if(tag_of(n) == tag) {
				pick(state, n);
				return;
			}
		}
		autostart_pending = false;
	}

	// A scenario may model one pop unit as many real people; it says how many in
	// its localisation so that the panel shows real head counts.
	static double people_per_pop_unit(sys::state& state) {
		if(state.key_is_localized("katechon_people_per_pop_unit")) {
			auto const value = std::atof(text::produce_simple_string(state, "katechon_people_per_pop_unit").c_str());
			if(value >= 1.0)
				return value;
		}
		return 4.0;
	}

	// ---- drawing ----
	// The screen is the hall of the title screen with three things set in it:
	// a framed board holding the list of nations and a window onto the map, a
	// card for the chosen nation, and the way on. Positions are in the units of
	// the 941-point-high artwork, as on the title screen.
	GLuint hall = 0;
	GLuint icons = 0;
	GLuint eagle = 0;
	GLuint play_art = 0;
	bool art_loaded = false;
	hit_target map_window; // everything right of the list; the map shows through wherever no panel lies
	std::vector<hit_target> panels;
	inline static constexpr int32_t icon_cells = 30; // assets/katechon_hud_icons.png
	inline static constexpr ogl::color3f aged{ 0.89f, 0.75f, 0.49f };
	inline static constexpr ogl::color3f cream{ 0.94f, 0.88f, 0.74f };

	void load_art(sys::state& state) {
		art_loaded = true;
		auto root = simple_fs::get_root(state.common_fs);
		auto assets = simple_fs::open_directory(root, NATIVE("assets"));
		hall = ogl::make_gl_texture(assets, NATIVE("katechon_title_bg.png"));
		icons = ogl::make_gl_texture(assets, NATIVE("katechon_hud_icons.png"));
		eagle = ogl::make_gl_texture(assets, NATIVE("katechon_shell_eagle.png"));
		play_art = ogl::make_gl_texture(assets, NATIVE("katechon_pick_play.png"));
		for(auto const texture : { hall, icons, eagle, play_art }) {
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

	void fill(sys::state& state, float x, float y, float w, float h, ogl::color3f c, float alpha) {
		ogl::render_alpha_colored_rect(state, x, y, w, h, c.r, c.g, c.b, alpha);
	}

	// A part of the hall, where it falls when the artwork covers the window.
	void hall_piece(sys::state& state, float x, float y, float w, float h, float screen_w, float screen_h, float dark) {
		if(w <= 0.f || h <= 0.f)
			return;
		if(hall) {
			constexpr float art_ratio = 1672.f / 941.f;
			auto const art_w = std::max(screen_w, screen_h * art_ratio);
			auto const art_h = art_w / art_ratio;
			auto const ox = (screen_w - art_w) * 0.5f;
			auto const oy = (screen_h - art_h) * 0.5f;
			ogl::render_subrect(state, x, y, w, h, (x - ox) / art_w, (y - oy) / art_h, w / art_w, h / art_h, hall);
		}
		fill(state, x, y, w, h, ogl::color3f{ 0.035f, 0.022f, 0.018f }, hall ? dark : 1.f);
	}

	static std::string capitals(std::string value) {
		for(auto& c : value) {
			if(uint8_t(c) < 128)
				c = char(std::toupper(uint8_t(c)));
		}
		return value;
	}

	std::string fit(sys::state& state, std::string content, int32_t size, float tracking, float max_w) {
		while(content.size() > 3 && shaped(state, content, size, tracking).width > max_w) {
			content.pop_back();
			while(!content.empty() && (uint8_t(content.back()) & 0xC0) == 0x80)
				content.pop_back();
		}
		return content;
	}

	void lozenge(sys::state& state, float cx, float cy, int32_t r, ogl::color3f c) {
		for(int32_t i = -r; i <= r; ++i)
			fill(state, cx - float(r - std::abs(i)) - 0.5f, cy + float(i), float(2 * (r - std::abs(i)) + 1), 1.f, c, 1.f);
	}

	// A double gilt line with a lozenge at each corner and at the middle of
	// its top and bottom.
	void gilt_frame(sys::state& state, float x, float y, float w, float h) {
		for(int32_t k = 0; k < 2; ++k) {
			auto const in = k == 0 ? 0.f : 5.f;
			auto const a = k == 0 ? 0.90f : 0.40f;
			fill(state, x + in, y + in, w - 2.f * in, 1.f, aged, a);
			fill(state, x + in, y + h - in - 1.f, w - 2.f * in, 1.f, aged, a);
			fill(state, x + in, y + in, 1.f, h - 2.f * in, aged, a);
			fill(state, x + w - in - 1.f, y + in, 1.f, h - 2.f * in, aged, a);
		}
		for(auto const cx : { x, x + w - 1.f }) {
			for(auto const cy : { y, y + h - 1.f })
				lozenge(state, cx, cy, 4, aged);
		}
		lozenge(state, std::round(x + w * 0.5f), y, 3, aged);
		lozenge(state, std::round(x + w * 0.5f), y + h - 1.f, 3, aged);
	}

	// A rule fading to both ends, with a lozenge (or the eagle) at its middle.
	void ornament_rule(sys::state& state, float x, float right, float y, float gap) {
		auto const centre = std::round((x + right) * 0.5f);
		constexpr int32_t steps = 14;
		auto const reach = (right - x) * 0.5f - gap;
		for(int32_t i = 0; i < steps; ++i) {
			auto const a = 0.85f * (1.f - float(i) / float(steps));
			fill(state, centre + gap + reach * float(i) / float(steps), y, reach / float(steps) + 0.5f, 1.f, aged, a);
			fill(state, centre - gap - reach * float(i + 1) / float(steps), y, reach / float(steps) + 0.5f, 1.f, aged, a);
		}
	}

	void draw_icon(sys::state& state, int32_t cell, float cx, float cy, float size, ogl::color3f c) {
		if(icons)
			ogl::render_tinted_subsprite(state, cell, icon_cells, std::round(cx - size * 0.5f), std::round(cy - size * 0.5f), size, size, c.r, c.g, c.b, icons, ui::rotation::upright, false, false);
	}

	void stat_row(sys::state& state, int32_t cell, float x, float right, float y, float u, std::string_view key, std::string_view fallback, std::string const& value) {
		draw_icon(state, cell, x + 11.f * u, y, 22.f * u, aged);
		draw_text(state, localized(state, key, fallback), x + 40.f * u, y, int32_t(14.f * u), 0.16f, ogl::color3f{ 0.72f, 0.66f, 0.56f });
		draw_text(state, value, right, y, int32_t(21.f * u), 0.02f, cream, true);
		fill(state, x, y + 21.f * u, right - x, 1.f, aged, 0.16f);
	}

public:
	void on_update(sys::state& state) noexcept override {
		rebuild(state);
	}

	// Everything but the window onto the map belongs to this screen.
	mouse_probe impl_probe_mouse(sys::state& state, int32_t x, int32_t y, mouse_probe_type type) noexcept override {
		// Only the panels take the mouse; everywhere else it belongs to the map.
		for(auto const& panel : panels) {
			if(over(panel, float(x), float(y)))
				return mouse_probe{ this, xy_pair{ int16_t(x), int16_t(y) } };
		}
		return mouse_probe{ nullptr, xy_pair{ int16_t(x), int16_t(y) } };
	}

	message_result on_lbutton_down(sys::state& state, int32_t x, int32_t y, sys::key_modifiers mods) noexcept override {
		for(size_t i = 0; i < targets.size(); ++i) {
			auto const t = targets[i];
			if(over(t, float(x), float(y))) {
				activate(state, t);
				break;
			}
		}
		return message_result::consumed;
	}

	message_result on_rbutton_down(sys::state& state, int32_t x, int32_t y, sys::key_modifiers mods) noexcept override {
		return message_result::consumed;
	}

	message_result on_scroll(sys::state& state, int32_t x, int32_t y, float amount, sys::key_modifiers mods) noexcept override {
		if(float(x) < map_window.x)
			scroll_by(amount > 0.f ? -3 : 3);
		return message_result::consumed;
	}

	message_result on_key_down(sys::state& state, sys::virtual_key key, sys::key_modifiers mods) noexcept override {
		switch(key) {
		case sys::virtual_key::ESCAPE:
			game_scene::switch_scene(state, game_scene::scene_id::title_screen);
			return message_result::consumed;
		case sys::virtual_key::RETURN:
			if(command::can_notify_start_game(state, state.local_player_nation))
				command::notify_start_game(state);
			return message_result::consumed;
		case sys::virtual_key::UP:
			step_selection(state, -1);
			return message_result::consumed;
		case sys::virtual_key::DOWN:
			step_selection(state, 1);
			return message_result::consumed;
		default:
			return message_result::unseen;
		}
	}

	void render(sys::state& state, int32_t x, int32_t y) noexcept override {
		if(nations.empty())
			rebuild(state);
		if(autostart_pending)
			autostart(state);
		if(!art_loaded)
			load_art(state);

		auto const w = width(state);
		auto const h = height(state);
		auto const u = unit(state);
		auto const mx = float(state.mouse_x_position) / state.user_settings.ui_scale;
		auto const my = float(state.mouse_y_position) / state.user_settings.ui_scale;
		auto const hovering = state.ui_state.under_mouse == this;
		targets.clear();

		if(gliding) {
			auto const now = std::chrono::steady_clock::now();
			auto const dt = glide_last == std::chrono::steady_clock::time_point{} ? 0.016f : std::min(0.1f, std::chrono::duration<float>(now - glide_last).count());
			glide_last = now;
			if(state.map_state.is_dragging) {
				gliding = false;
			} else {
				auto const current = state.map_state.pos.data;
				auto dx = glide_target.x - current.x;
				dx -= std::round(dx); // the short way round the world
				auto const dy = glide_target.y - current.y;
				auto const step = 1.f - std::exp(-dt * 4.5f);
				state.map_state.set_pos(map_space::point_normalized_inverted_y{ glm::vec2{ current.x + dx * step, current.y + dy * step } });
				state.map_state.last_map_movement = now;
				state.map_state.last_map_movement_handled = false;
				if(std::abs(dx) + std::abs(dy) < 0.0006f)
					gliding = false;
			}
		}

		// ---- where things stand ----
		auto const card_w = std::round(330.f * u);
		auto const card_x = std::round(w - card_w - 30.f * u);
		auto const card_y = std::round(150.f * u);
		auto const play_h = std::round(72.f * u);
		auto const play_y = std::round(h - 104.f * u - play_h);
		// the card is as tall as what it says, not as tall as the screen
		auto const card_h = std::min(play_y - 22.f * u - card_y, std::round(566.f * u));
		auto const board_x = std::round(30.f * u);
		auto const board_y = std::round(178.f * u);
		auto const board_w = card_x - 24.f * u - board_x;
		auto const board_h = std::round(h - 118.f * u) - board_y;
		auto const list_w = std::round(280.f * u);
		map_window = hit_target{ board_x + list_w, 0.f, w - board_x - list_w, h, action::none, dcon::nation_id{} };

		// ---- the map fills the screen; only the panels lie over it ----
		fill(state, 0.f, 0.f, 0.f, 0.f, ogl::color3f{ 0.f, 0.f, 0.f }, 0.f); // sets up the state the artwork relies on
		panels.clear();
		panels.push_back(hit_target{ board_x, board_y, list_w, board_h, action::none, dcon::nation_id{} });
		panels.push_back(hit_target{ 40.f * u, h - 92.f * u, 200.f * u, 52.f * u, action::none, dcon::nation_id{} });
		if(state.local_player_nation) {
			panels.push_back(hit_target{ card_x, card_y, card_w, card_h, action::none, dcon::nation_id{} });
			panels.push_back(hit_target{ card_x, play_y, card_w, play_h, action::none, dcon::nation_id{} });
		}
		// The panels have no boxes: the map darkens towards the left and right
		// edges of the screen, enough for type to stand on, and clears in between.
		{
			constexpr int32_t strips = 64;
			auto const left_reach = board_x + list_w + 150.f * u;
			auto const right_reach = card_w + 30.f * u + 170.f * u;
			for(int32_t i = 0; i < strips; ++i) {
				auto const t = (float(i) + 0.5f) / float(strips);
				// hold the shade under the text, then let it go in a smooth curve
				auto const hold = [](float v, float solid) { auto const k = std::clamp((v - solid) / (1.f - solid), 0.f, 1.f); return 1.f - k * k * (3.f - 2.f * k); };
				// strips meet on whole pixels: an overlap would show as a darker line, a gap as a lighter one
				auto const l0 = std::round(left_reach * float(i) / float(strips));
				auto const l1 = std::round(left_reach * float(i + 1) / float(strips));
				fill(state, l0, 0.f, l1 - l0, h, ogl::color3f{ 0.045f, 0.026f, 0.030f }, 0.90f * hold(t, 0.62f));
				if(state.local_player_nation) {
					auto const r0 = std::round(right_reach * float(i) / float(strips));
					auto const r1 = std::round(right_reach * float(i + 1) / float(strips));
					fill(state, w - r1, 0.f, r1 - r0, h, ogl::color3f{ 0.045f, 0.026f, 0.030f }, 0.90f * hold(t, 0.60f));
				}
			}
		}

		// ---- the title ----
		{
			auto const tx = 66.f * u;
			auto const title = localized(state, "katechon_pick_header", "CHOOSE A NATION");
			auto const title_w = draw_text(state, title, tx, 104.f * u, int32_t(30.f * u), 0.16f, aged);
			ornament_rule(state, tx, tx + title_w, 140.f * u, 12.f * u);
			lozenge(state, std::round(tx + title_w * 0.5f), 140.f * u, 4, aged);
			auto const date = capitals(text::date_to_string(state, state.current_date));
			auto const date_w = shaped(state, date, int32_t(13.f * u), 0.30f).width;
			draw_text(state, date, tx + (title_w - date_w) * 0.5f, 168.f * u - 4.f * u, int32_t(13.f * u), 0.30f, muted);
		}

		// ---- the board: the list of nations beside the window ----
		// a single gold hairline closes the list instead of a frame
		fill(state, board_x + list_w + 8.f * u, board_y + 10.f * u, 1.f, board_h - 20.f * u, aged, 0.28f);
		{
			auto const pitch = 36.5f * u;
			auto const top = board_y + 20.f * u;
			auto const row_x = board_x + 12.f * u;
			auto const row_w = list_w - 36.f * u;
			visible_rows = std::max(1, int32_t((board_h - 40.f * u) / pitch));
			scroll_by(0);
			follow_selection(state);
			auto const rows = std::min(visible_rows, int32_t(nations.size()) - scroll);
			for(int32_t row = 0; row < rows; ++row) {
				auto const n = nations[size_t(scroll + row)];
				hit_target t{ row_x, top + float(row) * pitch, row_w, pitch, action::pick, n };
				auto const center_y = t.y + pitch * 0.5f;
				auto const selected = n == state.local_player_nation;
				auto const hovered = hovering && over(t, mx, my);
				if(selected) {
					// the chosen nation lies on a strip of the crimson of the shell
					fill(state, t.x, t.y + 1.f, t.w, t.h - 2.f, ogl::color3f{ 0.40f, 0.09f, 0.11f }, 0.85f);
					fill(state, t.x, t.y + 1.f, t.w, 1.f, aged, 0.80f);
					fill(state, t.x, t.y + t.h - 2.f, t.w, 1.f, aged, 0.80f);
					fill(state, t.x, t.y + 1.f, 2.f, t.h - 2.f, aged, 1.f);
				} else if(hovered) {
					fill(state, t.x, t.y + 1.f, t.w, t.h - 2.f, aged, 0.09f);
				} else {
					fill(state, t.x + 14.f * u, t.y + t.h - 1.f, t.w - 28.f * u, 1.f, aged, 0.07f);
				}
				draw_flag(state, n, t.x + 16.f * u, center_y - 10.f * u, 30.f * u, 20.f * u);
				draw_text(state, text::get_name_as_string(state, dcon::fatten(state.world, n)), t.x + 66.f * u, center_y, int32_t(18.f * u), 0.03f, selected ? cream : (hovered ? cream : ivory));
				draw_text(state, std::to_string(int32_t(state.world.nation_get_rank(n))), t.x + t.w - 16.f * u, center_y, int32_t(14.f * u), 0.02f, selected ? aged : muted, true);
				targets.push_back(t);
			}
			if(int32_t(nations.size()) > visible_rows) {
				auto const track_x = board_x + list_w - 14.f * u;
				auto const track_top = top;
				auto const track_h = float(visible_rows) * pitch;
				auto const thumb_h = std::max(24.f * u, track_h * float(visible_rows) / float(nations.size()));
				auto const thumb_y = track_top + (track_h - thumb_h) * float(scroll) / float(int32_t(nations.size()) - visible_rows);
				fill(state, track_x, track_top, 1.f, track_h, aged, 0.30f);
				lozenge(state, track_x, track_top, 3, aged);
				lozenge(state, track_x, track_top + track_h, 3, aged);
				fill(state, track_x - 2.f, thumb_y, 5.f, thumb_h, aged, 0.85f);
			}
		}

		// ---- the way back ----
		{
			auto const center_y = h - 66.f * u;
			auto const label = localized(state, "katechon_menu_back", "BACK");
			hit_target t{ 40.f * u, center_y - 26.f * u, 200.f * u, 52.f * u, action::back, dcon::nation_id{} };
			auto const hovered = hovering && over(t, mx, my);
			draw_icon(state, 21, 66.f * u, center_y, 24.f * u, hovered ? cream : aged); // chevron_left
			draw_text(state, label, 100.f * u, center_y, int32_t(21.f * u), 0.26f, hovered ? cream : aged);
			targets.push_back(t);
		}

		// ---- the chosen nation ----
		auto const chosen = state.local_player_nation;
		if(!chosen) {
			auto const hint = localized(state, "katechon_pick_hint", "Select a nation from the list or on the map.");
			draw_text(state, hint, card_x, card_y + 40.f * u, int32_t(17.f * u), 0.04f, cream);
			return;
		}
		// the dossier stands directly on the shaded map; a hairline marks where it begins
		fill(state, card_x - 8.f * u, card_y + 10.f * u, 1.f, card_h - 20.f * u, aged, 0.28f);
		{
			auto const cx = card_x + 40.f * u;
			auto const right = card_x + card_w - 40.f * u;
			auto y0 = card_y + 44.f * u;
			draw_flag(state, chosen, cx, y0, 132.f * u, 88.f * u);
			y0 += 88.f * u + 38.f * u;
			draw_text(state, fit(state, text::get_name_as_string(state, dcon::fatten(state.world, chosen)), int32_t(30.f * u), 0.03f, right - cx), cx, y0, int32_t(30.f * u), 0.03f, cream);
			draw_text(state, get_status_text(state, chosen), cx, y0 + 38.f * u, int32_t(19.f * u), 0.10f, aged);
			draw_text(state, text::produce_simple_string(state, state.world.government_type_get_name(state.world.nation_get_government_type(chosen))), cx, y0 + 66.f * u, int32_t(15.f * u), 0.10f, muted);
			y0 += 98.f * u;
			ornament_rule(state, cx - 14.f * u, right + 14.f * u, y0, 30.f * u);
			if(eagle)
				ogl::render_textured_rect(state, ogl::color_modification::none, std::round((cx + right) * 0.5f - 16.f * u), y0 - 18.f * u, 32.f * u, 36.f * u, eagle, ui::rotation::upright, false, false);
			y0 += 50.f * u;
			auto const pitch = 46.f * u;
			// cells of the icon strip: capital (a star), politics, production, military, society
			stat_row(state, 1, cx, right, y0, u, "katechon_pick_rank", "OVERALL RANK", std::to_string(int32_t(state.world.nation_get_rank(chosen))));
			stat_row(state, 26, cx, right, y0 + pitch, u, "katechon_pick_prestige", "PRESTIGE", std::to_string(int32_t(state.world.nation_get_prestige_rank(chosen))));
			stat_row(state, 23, cx, right, y0 + 2.f * pitch, u, "katechon_pick_industry", "INDUSTRY", std::to_string(int32_t(state.world.nation_get_industrial_rank(chosen))));
			stat_row(state, 29, cx, right, y0 + 3.f * pitch, u, "katechon_pick_military", "MILITARY", std::to_string(int32_t(state.world.nation_get_military_rank(chosen))));
			stat_row(state, 14, cx, right, y0 + 4.f * pitch, u, "katechon_pick_population", "POPULATION", text::prettify(int64_t(double(state.world.nation_get_demographics(chosen, demographics::total)) * people_per_pop_unit(state))));
		}

		// ---- the way on ----
		{
			auto const enabled = command::can_notify_start_game(state, chosen);
			hit_target t{ card_x, play_y, card_w, play_h, enabled ? action::play : action::none, dcon::nation_id{} };
			auto const hovered = enabled && hovering && over(t, mx, my);
			if(play_art) {
				ogl::render_tinted_subsprite(state, hovered ? 1 : 0, 2, card_x - 3.f * u, play_y, card_w + 6.f * u, play_h, enabled ? 1.f : 0.5f, enabled ? 1.f : 0.5f, enabled ? 1.f : 0.5f, play_art, ui::rotation::upright, false, false);
			} else {
				fill(state, card_x, play_y, card_w, play_h, ogl::color3f{ 0.40f, 0.09f, 0.11f }, 1.f);
				gilt_frame(state, card_x, play_y, card_w, play_h);
			}
			auto const label = localized(state, "katechon_pick_play", "PLAY");
			auto const size = int32_t(28.f * u);
			auto const label_w = shaped(state, label, size, 0.30f).width;
			auto const lx = card_x + (card_w - label_w - 34.f * u) * 0.5f;
			auto const tone = enabled ? (hovered ? ogl::color3f{ 1.f, 0.95f, 0.80f } : ogl::color3f{ 0.96f, 0.82f, 0.52f }) : dim;
			draw_text(state, label, lx + 1.f, play_y + play_h * 0.5f + 1.f, size, 0.30f, ogl::color3f{ 0.10f, 0.02f, 0.03f });
			draw_text(state, label, lx, play_y + play_h * 0.5f - 1.f, size, 0.30f, tone);
			draw_icon(state, 10, lx + label_w + 26.f * u, play_y + play_h * 0.5f - 1.f, 24.f * u, tone); // chevron_right
			if(enabled)
				targets.push_back(t);
		}
	}
};

}
