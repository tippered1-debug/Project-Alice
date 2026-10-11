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

	void stat_row(sys::state& state, float x, float right, float y, float u, std::string_view key, std::string_view fallback, std::string const& value) {
		draw_text(state, localized(state, key, fallback), x, y, int32_t(16.f * u), 0.10f, muted);
		draw_text(state, value, right, y, int32_t(19.f * u), 0.02f, ivory, true);
		draw_rule(state, x, y + 17.f * u, right - x, 0.18f);
	}

public:
	void on_update(sys::state& state) noexcept override {
		rebuild(state);
	}

	mouse_probe impl_probe_mouse(sys::state& state, int32_t x, int32_t y, mouse_probe_type type) noexcept override {
		auto const u = unit(state);
		auto const over_left = float(x) < left_width * u;
		auto const over_right = bool(state.local_player_nation) && float(x) > width(state) - right_width * u;
		if(over_left || over_right)
			return mouse_probe{ this, xy_pair{ int16_t(x), int16_t(y) } };
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
		if(float(x) < left_width * unit(state))
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

		auto const w = width(state);
		auto const h = height(state);
		auto const u = unit(state);
		auto const mx = float(state.mouse_x_position) / state.user_settings.ui_scale;
		auto const my = float(state.mouse_y_position) / state.user_settings.ui_scale;
		auto const hovering = state.ui_state.under_mouse == this;
		targets.clear();

		// Left: the list of nations.
		auto const lw = left_width * u;
		draw_wash(state, 0.f, lw, h, 0.93f, 0.90f);
		draw_wash(state, lw, 70.f * u, h, 0.90f, 0.f);
		ogl::render_alpha_colored_rect(state, lw, 0.f, 1.f, h, gold.r * 0.8f, gold.g * 0.8f, gold.b * 0.8f, 0.35f);

		auto const mg = margin * u;
		draw_text(state, localized(state, "katechon_pick_header", "CHOOSE A NATION"), mg, 70.f * u, int32_t(21.f * u), 0.20f, gold);
		draw_text(state, text::date_to_string(state, state.current_date), mg, 104.f * u, int32_t(15.f * u), 0.06f, muted);
		draw_rule(state, mg, 128.f * u, lw - 2.f * mg, 0.45f);

		visible_rows = std::max(1, int32_t((h - (list_top + footer) * u) / (row_pitch * u)) + 1);
		scroll_by(0);
		follow_selection(state);
		auto const rows = std::min(visible_rows, int32_t(nations.size()) - scroll);
		for(int32_t row = 0; row < rows; ++row) {
			auto const n = nations[size_t(scroll + row)];
			auto const center_y = (list_top + float(row) * row_pitch) * u;
			hit_target t{ 0.f, center_y - row_pitch * u * 0.5f, lw, row_pitch * u, action::pick, n };
			auto const selected = n == state.local_player_nation;
			auto const hovered = hovering && over(t, mx, my);
			if(selected)
				draw_row_marker(state, mg - 18.f * u, center_y, 30.f * u, u, 1.f);
			else if(hovered)
				ogl::render_alpha_colored_rect(state, 0.f, t.y, lw, t.h, gold.r, gold.g, gold.b, 0.07f);
			draw_flag(state, n, mg, center_y - 10.f * u, 30.f * u, 20.f * u);
			auto const color = selected ? gold : (hovered ? mix(ivory, gold, 0.45f) : ivory);
			draw_text(state, text::get_name_as_string(state, dcon::fatten(state.world, n)), mg + 46.f * u, center_y, int32_t(18.f * u), 0.03f, color);
			draw_text(state, std::to_string(int32_t(state.world.nation_get_rank(n))), lw - mg, center_y, int32_t(15.f * u), 0.02f, selected ? gold : muted, true);
			targets.push_back(t);
		}
		if(int32_t(nations.size()) > visible_rows) {
			auto const track_top = (list_top - row_pitch * 0.5f) * u;
			auto const track_h = float(visible_rows) * row_pitch * u;
			auto const thumb_h = std::max(18.f * u, track_h * float(visible_rows) / float(nations.size()));
			auto const thumb_y = track_top + (track_h - thumb_h) * float(scroll) / float(int32_t(nations.size()) - visible_rows);
			ogl::render_alpha_colored_rect(state, lw - 14.f * u, track_top, 1.f, track_h, gold.r, gold.g, gold.b, 0.18f);
			ogl::render_alpha_colored_rect(state, lw - 14.f * u - 1.f, thumb_y, 3.f, thumb_h, gold.r, gold.g, gold.b, 0.75f);
		}

		{
			auto const center_y = h - 56.f * u;
			auto const label = localized(state, "katechon_menu_back", "BACK");
			hit_target t{ 0.f, center_y - 26.f * u, lw * 0.6f, 52.f * u, action::back, dcon::nation_id{} };
			auto const hovered = hovering && over(t, mx, my);
			if(hovered)
				draw_row_marker(state, mg - 18.f * u, center_y, 40.f * u, u, 1.f);
			draw_text(state, label, mg, center_y, int32_t(22.f * u), 0.16f, hovered ? gold : ivory);
			targets.push_back(t);
		}

		// Right: the chosen nation.
		auto const chosen = state.local_player_nation;
		if(!chosen) {
			draw_text(state, localized(state, "katechon_pick_hint", "Select a nation from the list or on the map."), lw + 90.f * u, h - 56.f * u, int32_t(17.f * u), 0.04f, ivory);
			return;
		}
		auto const rw = right_width * u;
		auto const rx = w - rw;
		draw_wash(state, rx - 70.f * u, 70.f * u, h, 0.f, 0.90f);
		draw_wash(state, rx, rw, h, 0.90f, 0.93f);
		ogl::render_alpha_colored_rect(state, rx, 0.f, 1.f, h, gold.r * 0.8f, gold.g * 0.8f, gold.b * 0.8f, 0.35f);

		auto const cx = rx + mg;
		auto const right = w - mg;
		draw_flag(state, chosen, cx, 58.f * u, 132.f * u, 88.f * u);
		draw_text(state, text::get_name_as_string(state, dcon::fatten(state.world, chosen)), cx, 186.f * u, int32_t(25.f * u), 0.04f, ivory);
		draw_text(state, get_status_text(state, chosen), cx, 220.f * u, int32_t(16.f * u), 0.10f, gold);
		draw_text(state, text::produce_simple_string(state, state.world.government_type_get_name(state.world.nation_get_government_type(chosen))), cx, 246.f * u, int32_t(15.f * u), 0.04f, muted);
		draw_rule(state, cx, 272.f * u, right - cx, 0.45f);

		stat_row(state, cx, right, 310.f * u, u, "katechon_pick_rank", "OVERALL RANK", std::to_string(int32_t(state.world.nation_get_rank(chosen))));
		stat_row(state, cx, right, 354.f * u, u, "katechon_pick_prestige", "PRESTIGE", std::to_string(int32_t(state.world.nation_get_prestige_rank(chosen))));
		stat_row(state, cx, right, 398.f * u, u, "katechon_pick_industry", "INDUSTRY", std::to_string(int32_t(state.world.nation_get_industrial_rank(chosen))));
		stat_row(state, cx, right, 442.f * u, u, "katechon_pick_military", "MILITARY", std::to_string(int32_t(state.world.nation_get_military_rank(chosen))));
		stat_row(state, cx, right, 486.f * u, u, "katechon_pick_population", "POPULATION", text::prettify(int64_t(state.world.nation_get_demographics(chosen, demographics::total))));

		{
			auto const center_y = h - 60.f * u;
			auto const enabled = command::can_notify_start_game(state, chosen);
			hit_target t{ rx, center_y - 34.f * u, rw, 68.f * u, enabled ? action::play : action::none, dcon::nation_id{} };
			auto const hovered = enabled && hovering && over(t, mx, my);
			ogl::render_alpha_colored_rect(state, cx, center_y - 30.f * u, right - cx, 60.f * u, gold.r, gold.g, gold.b, hovered ? 0.22f : 0.10f);
			ogl::render_alpha_colored_rect(state, cx, center_y - 30.f * u, right - cx, 1.f, gold.r, gold.g, gold.b, 0.8f);
			ogl::render_alpha_colored_rect(state, cx, center_y + 30.f * u, right - cx, 1.f, gold.r, gold.g, gold.b, 0.8f);
			auto const label = localized(state, "katechon_pick_play", "PLAY");
			auto const size = int32_t(26.f * u);
			auto const label_w = shaped(state, label, size, 0.30f).width;
			draw_text(state, label, cx + (right - cx - label_w) * 0.5f, center_y, size, 0.30f, enabled ? gold : dim);
			if(enabled)
				targets.push_back(t);
		}
	}
};

}
