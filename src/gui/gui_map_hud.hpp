#pragma once

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>

#include "system_state.hpp"
#include "gui_element_types.hpp"
#include "gui_common_elements.hpp"
#include "gui_title_screen.hpp"
#include "gui_topbar.hpp"
#include "gui_map_hud_state.hpp"
#include "demographics.hpp"
#include "map_modes.hpp"

namespace ui {

// The KATECHON map HUD: a slim top bar, one card for the selected province and
// its owner, and a navigation strip with zoom and map layers. Like the title
// screen it is a single element that draws itself; the country windows and
// their rules still live in the old top bar and menu bar, which this element
// drives by pressing their (now undrawn) buttons.
class map_hud_element : public katechon_canvas {
	enum class action : uint8_t {
		none, press, toggle_layers, set_layer, zoom_in, zoom_out, capital, close_province, province_details,
		center_province, diplomacy, toggle_outliner, toggle_log
	};
	struct hit_target {
		float x = 0.f;
		float y = 0.f;
		float w = 0.f;
		float h = 0.f;
		action act = action::none;
		button_element_base* button = nullptr;
		int32_t value = 0;
	};
	struct rect {
		float x = 0.f;
		float y = 0.f;
		float w = 0.f;
		float h = 0.f;
	};
	struct labelled {
		std::string label;
		std::string value;
		bool warning = false;
	};
	struct tab {
		std::string_view element;
		std::string_view key;
		std::string_view fallback;
	};
	struct score {
		std::string label;
		std::string value;
		std::string rank;
	};
	// Everything read from the world, gathered when the game state updates so
	// that drawing never walks the simulation's data mid-tick.
	struct snapshot {
		bool ready = false;
		dcon::nation_id player;
		std::string player_name;
		std::string player_status;
		std::string date;
		std::vector<labelled> stats;

		dcon::province_id province;
		std::string province_name;
		std::string province_state;
		std::vector<labelled> province_rows;

		dcon::nation_id owner;
		std::string owner_name;
		std::string owner_status;
		std::string owner_government;
		std::array<score, 4> scores;
		std::vector<labelled> owner_rows;
	};

	inline static constexpr float bar_h = 34.f;
	inline static constexpr float strip_h = 22.f;
	inline static constexpr float top_h = bar_h + strip_h;
	inline static constexpr float edge = 12.f;
	inline static constexpr float card_w = 292.f;
	inline static constexpr float pad = 16.f;
	inline static constexpr float row_pitch = 24.f;
	inline static constexpr float nav_h = 30.f;
	inline static constexpr float zoom_w = 30.f;
	inline static constexpr int32_t layer_count = 45;
	inline static constexpr int32_t layer_rows = 15;
	inline static constexpr float layer_col_w = 172.f;
	inline static constexpr float layer_pitch = 22.f;
	inline static constexpr ogl::color3f alarm{ 0.86f, 0.44f, 0.38f };

	inline static constexpr std::array<tab, 8> tabs{
		tab{ "topbarbutton_production", "katechon_hud_production", "PRODUCTION" },
		tab{ "topbarbutton_budget", "katechon_hud_budget", "BUDGET" },
		tab{ "topbarbutton_tech", "katechon_hud_technology", "TECHNOLOGY" },
		tab{ "topbarbutton_politics", "katechon_hud_politics", "POLITICS" },
		tab{ "topbarbutton_pops", "katechon_hud_population", "POPULATION" },
		tab{ "topbarbutton_trade", "katechon_hud_trade", "TRADE" },
		tab{ "topbarbutton_diplomacy", "katechon_hud_diplomacy", "DIPLOMACY" },
		tab{ "topbarbutton_military", "katechon_hud_military", "MILITARY" }
	};

	// A country's name as a landmark: where its largest landmass sits on the
	// map and how much room it offers, in map pixels.
	struct map_label {
		std::string text;
		glm::vec2 anchor{ 0.f, 0.f };
		float span_x = 0.f;
		float span_y = 0.f;
	};
	struct place {
		std::string name;
		glm::vec2 position{ 0.f, 0.f };
		bool capital = true;
		float weight = 0.f;
	};

	snapshot snap;
	std::vector<map_label> labels;
	std::vector<place> capitals; // by national rank; used when the scenario lists no cities
	std::vector<place> cities; // most important first
	std::vector<place> regions;
	bool cities_read = false;
	std::vector<rect> placed;
	float people_per_unit = 1.f;
	int32_t frames = 0;
	std::chrono::steady_clock::time_point fps_mark{};
	bool report_fps = false;
	std::vector<hit_target> targets;
	std::vector<rect> panels;
	std::array<button_element_base*, 8> tab_buttons{};
	button_element_base* pause_button = nullptr;
	button_element_base* faster_button = nullptr;
	button_element_base* slower_button = nullptr;
	button_element_base* menu_button = nullptr;
	button_element_base* search_button = nullptr;
	button_element_base* ledger_button = nullptr;
	button_element_base* economy_button = nullptr;
	bool resolved = false;
	bool layers_open = false;

	static float width(sys::state& state) {
		return float(state.x_size) / state.user_settings.ui_scale;
	}
	static float height(sys::state& state) {
		return float(state.y_size) / state.user_settings.ui_scale;
	}
	static bool over(hit_target const& t, float mx, float my) {
		return mx >= t.x && mx < t.x + t.w && my >= t.y && my < t.y + t.h;
	}

	static button_element_base* child_button(sys::state& state, element_base* owner, std::string_view name) {
		return owner ? static_cast<button_element_base*>(owner->get_child_by_name(state, name)) : nullptr;
	}

	void resolve(sys::state& state) {
		resolved = true;
		auto* bar = state.ui_state.topbar_window;
		for(size_t i = 0; i < tabs.size(); ++i)
			tab_buttons[i] = child_button(state, bar, tabs[i].element);
		pause_button = child_button(state, bar, "pause_bg");
		faster_button = child_button(state, bar, "button_speedup");
		slower_button = child_button(state, bar, "button_speeddown");
		// Visual QA hooks, as elsewhere: open the HUD in a given state without
		// synthetic clicks. KATECHON_HUD_PROVINCE=<id>, KATECHON_HUD_LAYERS=1.
		if(auto const* id = std::getenv("KATECHON_HUD_PROVINCE"); id && id[0]) {
			auto const prov = dcon::province_id{ dcon::province_id::value_base_t(std::atoi(id)) };
			if(prov.index() >= 0 && uint32_t(prov.index()) < state.world.province_size()) {
				state.set_selected_province(prov);
				state.map_state.center_map_on_province(state, prov);
			}
		}
		if(auto const* open = std::getenv("KATECHON_HUD_LAYERS"); open && open[0] == '1')
			layers_open = true;
		// KATECHON_HUD_ZOOM=<factor> sets the camera depth, KATECHON_HUD_FPS=1
		// reports the frame rate on stderr.
		if(auto const* depth = std::getenv("KATECHON_HUD_ZOOM"); depth && depth[0])
			state.map_state.zoom = std::clamp(float(std::atof(depth)), map::min_zoom, map::max_zoom);
		if(auto const* fps = std::getenv("KATECHON_HUD_FPS"); fps && fps[0] == '1')
			report_fps = true;
		// The outliner is one switch away; it does not sit on the map by default.
		if(auto* outliner = state.ui_state.outliner_window; outliner)
			outliner->set_visible(state, false);
		auto* menu = state.ui_state.menubar_window;
		menu_button = child_button(state, menu, "menu_button");
		search_button = child_button(state, menu, "button_goto");
		ledger_button = child_button(state, menu, "ledger_button");
		economy_button = child_button(state, menu, "economy-scene-enter-button");
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

	void fill(sys::state& state, float x, float y, float w, float h, ogl::color3f c, float alpha) {
		ogl::render_alpha_colored_rect(state, x, y, w, h, c.r, c.g, c.b, alpha);
	}

	// A dark plate with a gold top edge; it also claims the mouse.
	void draw_panel(sys::state& state, float x, float y, float w, float h) {
		ogl::render_alpha_colored_rect(state, x, y, w, h, 0.035f, 0.043f, 0.063f, 0.94f);
		fill(state, x, y, w, 1.f, gold, 0.55f);
		fill(state, x, y + h - 1.f, w, 1.f, gold, 0.16f);
		fill(state, x, y, 1.f, h, gold, 0.16f);
		fill(state, x + w - 1.f, y, 1.f, h, gold, 0.16f);
		panels.push_back(rect{ x, y, w, h });
	}

	// Shortens a string to the given width, ending it with dots.
	std::string fit(sys::state& state, std::string content, int32_t size, float tracking, float max_w) {
		if(content.empty() || shaped(state, content, size, tracking).width <= max_w)
			return content;
		while(!content.empty()) {
			auto const removed = uint8_t(content.back());
			content.pop_back();
			if((removed & 0xC0) == 0x80)
				continue; // still inside a multi-byte character
			while(!content.empty() && content.back() == ' ')
				content.pop_back();
			if(shaped(state, content + "...", size, tracking).width <= max_w)
				return content + "...";
		}
		return content;
	}

	float centered(sys::state& state, std::string_view content, float x, float w, float center_y, int32_t size, float tracking, ogl::color3f color) {
		auto const text_w = shaped(state, content, size, tracking).width;
		draw_text(state, content, x + (w - text_w) * 0.5f, center_y, size, tracking, color);
		return text_w;
	}

	void text_button(sys::state& state, float x, float y, float w, float h, std::string_view label, hit_target t, bool hovering, float mx, float my) {
		t.x = x;
		t.y = y;
		t.w = w;
		t.h = h;
		auto const hovered = hovering && over(t, mx, my);
		fill(state, x, y, w, h, gold, hovered ? 0.22f : 0.09f);
		fill(state, x, y, w, 1.f, gold, 0.55f);
		fill(state, x, y + h - 1.f, w, 1.f, gold, 0.55f);
		centered(state, label, x, w, y + h * 0.5f, 12, 0.16f, hovered ? ivory : gold);
		targets.push_back(t);
	}

	void label_row(sys::state& state, labelled const& row, float x, float right, float center_y) {
		auto const label_w = draw_text(state, row.label, x, center_y, 11, 0.12f, muted);
		auto const value = fit(state, row.value, 14, 0.02f, right - x - label_w - 12.f);
		draw_text(state, value, right, center_y, 14, 0.02f, row.warning ? alarm : ivory, true);
		fill(state, x, center_y + row_pitch * 0.5f, right - x, 1.f, gold, 0.10f);
	}

	static std::string rank_text(int32_t rank) {
		return "#" + std::to_string(rank);
	}

	static bool paused(sys::state& state) {
		return state.actual_game_speed.load(std::memory_order::acquire) <= 0 || state.internally_paused || state.ui_pause.load(std::memory_order::acquire);
	}

	static bool country_window_open(sys::state& state) {
		return state.ui_state.topbar_subwindow && state.ui_state.topbar_subwindow->is_visible();
	}

	static std::string capitals_of(std::string value) {
		for(auto& c : value) {
			if(uint8_t(c) < 128)
				c = char(std::toupper(uint8_t(c)));
		}
		return value;
	}

	// The scenario may state how many people one unit of population stands
	// for (the simulation works on a sample); figures are shown in people.
	std::string people(float units) const {
		return text::prettify(int64_t(double(units) * double(people_per_unit)));
	}

	// map/katechon_cities.csv: province;x;y;capital;population;name, with the
	// position in pixels of the province map.
	void read_cities(sys::state& state) {
		cities_read = true;
		auto root = simple_fs::get_root(state.common_fs);
		auto map_dir = simple_fs::open_directory(root, NATIVE("map"));
		auto file = simple_fs::open_file(map_dir, NATIVE("katechon_cities.csv"));
		if(!file)
			return;
		auto const content = simple_fs::view_contents(*file);
		auto const text = std::string_view(content.data, content.file_size);
		auto const map_h = float(state.map_state.map_data.size_y);
		size_t start = text.find('\n');
		while(start != std::string_view::npos && start + 1 < text.size()) {
			auto const end = text.find('\n', start + 1);
			auto line = text.substr(start + 1, end == std::string_view::npos ? std::string_view::npos : end - start - 1);
			start = end;
			std::array<std::string_view, 6> field{};
			size_t count = 0;
			while(count < 5) {
				auto const cut = line.find(';');
				if(cut == std::string_view::npos)
					break;
				field[count++] = line.substr(0, cut);
				line.remove_prefix(cut + 1);
			}
			if(count < 5 || line.empty())
				continue;
			while(!line.empty() && (line.back() == '\r' || line.back() == ' '))
				line.remove_suffix(1);
			auto const number = [](std::string_view v) { return float(std::atof(std::string(v).c_str())); };
			auto const map_id = int32_t(number(field[0]));
			if(map_id <= 0 || uint32_t(map_id) > state.world.province_size())
				continue;
			auto const province = province::from_map_id(uint16_t(map_id));
			place city;
			city.name = std::string(line);
			city.capital = number(field[3]) > 0.f;
			city.weight = number(field[4]) * (city.capital ? 4.f : 1.f) + (city.capital ? 2000000.f : 0.f);
			// The file counts rows from the top of the picture; take whichever
			// reading lands in the city's own province.
			auto const mid = state.world.province_get_mid_point(province);
			auto const y = number(field[2]);
			city.position = glm::vec2{ number(field[1]), std::abs(mid.y - y) <= std::abs(mid.y - (map_h - y)) ? y : map_h - y };
			cities.push_back(std::move(city));
		}
		std::sort(cities.begin(), cities.end(), [](place const& a, place const& b) { return a.weight > b.weight; });
	}

	void survey_map(sys::state& state) {
		if(!cities_read)
			read_cities(state);
		labels.clear();
		capitals.clear();
		if(regions.empty()) {
			province::for_each_land_province(state, [&](dcon::province_id p) {
				regions.push_back(place{ text::get_name_as_string(state, p), state.world.province_get_mid_point(p) });
			});
		}

		struct mass {
			double x = 0.0;
			double y = 0.0;
			double xx = 0.0;
			double yy = 0.0;
			int32_t count = 0;
		};
		std::vector<std::pair<int32_t, dcon::nation_id>> ranked;
		for(auto n : state.world.in_nation) {
			if(n.get_owned_province_count() == 0)
				continue;
			ranked.emplace_back(int32_t(n.get_rank()), n.id);
			// The largest landmass carries the name.
			ankerl::unordered_dense::map<uint16_t, mass> masses;
			for(auto own : n.get_province_ownership()) {
				auto const p = own.get_province();
				auto const mid = p.get_mid_point();
				auto& m = masses[uint16_t(p.get_connected_region_id())];
				m.x += mid.x;
				m.y += mid.y;
				m.xx += double(mid.x) * mid.x;
				m.yy += double(mid.y) * mid.y;
				++m.count;
			}
			mass const* best = nullptr;
			for(auto const& entry : masses) {
				if(!best || entry.second.count > best->count)
					best = &entry.second;
			}
			if(!best)
				continue;
			auto const cx = best->x / best->count;
			auto const cy = best->y / best->count;
			auto const sx = std::sqrt(std::max(0.0, best->xx / best->count - cx * cx));
			auto const sy = std::sqrt(std::max(0.0, best->yy / best->count - cy * cy));
			auto const floor_span = 16.f + 9.f * std::sqrt(float(best->count));
			labels.push_back(map_label{ capitals_of(text::get_name_as_string(state, n)), glm::vec2{ float(cx), float(cy) },
				std::max(float(3.0 * sx), floor_span), std::max(float(3.0 * sy), floor_span * 0.6f) });
		}
		std::sort(labels.begin(), labels.end(), [](map_label const& a, map_label const& b) {
			return a.span_x * a.span_y > b.span_x * b.span_y;
		});
		std::sort(ranked.begin(), ranked.end(), [](auto const& a, auto const& b) {
			return a.first != b.first ? a.first < b.first : a.second.index() < b.second.index();
		});
		for(auto const& entry : ranked) {
			if(auto const capital = state.world.nation_get_capital(entry.second); capital)
				capitals.push_back(place{ text::get_name_as_string(state, capital), state.world.province_get_mid_point(capital) });
		}
	}

	void refresh(sys::state& state) {
		snap = snapshot{};
		snap.ready = true;
		people_per_unit = std::max(1.f, float(std::atof(localized(state, "katechon_people_per_unit", "1").c_str())));
		survey_map(state);
		snap.date = text::date_to_string(state, state.current_date);

		auto const player = state.local_player_nation;
		snap.player = player;
		if(player) {
			snap.player_name = text::get_name_as_string(state, dcon::fatten(state.world, player));
			snap.player_status = get_status_text(state, player) + "  " + rank_text(int32_t(state.world.nation_get_rank(player)));
			snap.stats.push_back(labelled{ localized(state, "katechon_hud_treasury", "TREASURY"), text::format_money(nations::get_treasury(state, player)) });
			snap.stats.push_back(labelled{ localized(state, "katechon_hud_people", "PEOPLE"), people(state.world.nation_get_demographics(player, demographics::total)) });
			snap.stats.push_back(labelled{ localized(state, "katechon_hud_literacy", "LITERACY"), text::format_percentage(nations::get_avg_non_colonial_literacy(state, player), 1) });
			snap.stats.push_back(labelled{ localized(state, "katechon_hud_research", "RESEARCH"), text::format_float(nations::daily_research_points(state, player), 1) });
			snap.stats.push_back(labelled{ localized(state, "katechon_hud_prestige", "PRESTIGE"), std::to_string(int32_t(nations::prestige_score(state, player))) });
			snap.stats.push_back(labelled{ localized(state, "katechon_hud_industry", "INDUSTRY"), std::to_string(int32_t(state.world.nation_get_industrial_score(player))) });
			snap.stats.push_back(labelled{ localized(state, "katechon_hud_army", "ARMY"), text::format_ratio(int32_t(state.world.nation_get_active_regiments(player)), int32_t(state.world.nation_get_recruitable_regiments(player))) });
			snap.stats.push_back(labelled{ localized(state, "katechon_hud_influence", "DIPLOMATS"), text::format_float(nations::diplomatic_points(state, player), 1) });
			auto const infamy = state.world.nation_get_infamy(player);
			snap.stats.push_back(labelled{ localized(state, "katechon_hud_infamy", "INFAMY"), text::format_float(infamy, 1), infamy >= state.defines.badboy_limit });
		}

		auto const prov = state.map_state.selected_province;
		snap.province = prov;
		if(!prov)
			return;
		snap.province_name = text::get_name_as_string(state, prov);
		if(prov.index() >= state.province_definitions.first_sea_province.index())
			return;

		if(auto const definition = state.world.province_get_state_from_abstract_state_membership(prov); definition)
			snap.province_state = text::get_name_as_string(state, definition);
		snap.province_rows.push_back(labelled{ localized(state, "katechon_hud_people", "PEOPLE"), people(state.world.province_get_demographics(prov, demographics::total)) });
		if(auto const terrain = state.world.province_get_terrain(prov); terrain)
			snap.province_rows.push_back(labelled{ localized(state, "katechon_hud_terrain", "TERRAIN"), text::get_name_as_string(state, terrain) });
		snap.province_rows.push_back(labelled{ localized(state, "katechon_hud_life_rating", "LIFE RATING"), std::to_string(int32_t(state.world.province_get_life_rating(prov))) });

		auto const owner = state.world.province_get_nation_from_province_ownership(prov);
		auto const controller = state.world.province_get_nation_from_province_control(prov);
		if(controller && controller != owner)
			snap.province_rows.push_back(labelled{ localized(state, "katechon_hud_occupied", "OCCUPIED BY"), text::get_name_as_string(state, dcon::fatten(state.world, controller)), true });

		snap.owner = owner;
		if(!owner)
			return;
		snap.owner_name = text::get_name_as_string(state, dcon::fatten(state.world, owner));
		snap.owner_status = get_status_text(state, owner);
		snap.owner_government = text::produce_simple_string(state, state.world.government_type_get_name(state.world.nation_get_government_type(owner)));
		if(auto const party = state.world.nation_get_ruling_party(owner); party)
			snap.owner_government += "  ·  " + text::get_name_as_string(state, party);

		auto const prestige = nations::prestige_score(state, owner);
		auto const industry = state.world.nation_get_industrial_score(owner);
		auto const military = state.world.nation_get_military_score(owner);
		snap.scores[0] = score{ localized(state, "katechon_hud_prestige", "PRESTIGE"), std::to_string(int32_t(prestige)), rank_text(int32_t(state.world.nation_get_prestige_rank(owner))) };
		snap.scores[1] = score{ localized(state, "katechon_hud_industry", "INDUSTRY"), std::to_string(int32_t(industry)), rank_text(int32_t(state.world.nation_get_industrial_rank(owner))) };
		snap.scores[2] = score{ localized(state, "katechon_hud_military", "MILITARY"), std::to_string(int32_t(military)), rank_text(int32_t(state.world.nation_get_military_rank(owner))) };
		snap.scores[3] = score{ localized(state, "katechon_hud_total", "TOTAL"), std::to_string(int32_t(prestige + industry + military)), rank_text(int32_t(state.world.nation_get_rank(owner))) };

		snap.owner_rows.push_back(labelled{ localized(state, "katechon_hud_people", "PEOPLE"), people(state.world.nation_get_demographics(owner, demographics::total)) });
		if(auto const leader = state.world.nation_get_in_sphere_of(owner); leader)
			snap.owner_rows.push_back(labelled{ localized(state, "katechon_hud_sphere", "SPHERE OF"), text::get_name_as_string(state, dcon::fatten(state.world, leader)) });
		if(auto const overlord = state.world.overlord_get_ruler(state.world.nation_get_overlord_as_subject(owner)); overlord)
			snap.owner_rows.push_back(labelled{ localized(state, "katechon_hud_overlord", "SUBJECT OF"), text::get_name_as_string(state, dcon::fatten(state.world, overlord)) });

		std::string allies;
		std::string enemies;
		for(auto other : state.world.in_nation) {
			if(other.id == owner || other.get_owned_province_count() == 0)
				continue;
			if(military::are_at_war(state, owner, other)) {
				if(!enemies.empty())
					enemies += ", ";
				enemies += text::get_name_as_string(state, other);
			} else if(nations::are_allied(state, owner, other)) {
				if(!allies.empty())
					allies += ", ";
				allies += text::get_name_as_string(state, other);
			}
		}
		if(!allies.empty())
			snap.owner_rows.push_back(labelled{ localized(state, "katechon_hud_allies", "ALLIES"), allies });
		if(!enemies.empty())
			snap.owner_rows.push_back(labelled{ localized(state, "katechon_hud_at_war", "AT WAR WITH"), enemies, true });
	}

	void press(sys::state& state, button_element_base* button) {
		if(!button || button->disabled)
			return;
		sound::play_interface_sound(state, button->get_click_sound(state), state.user_settings.interface_volume * state.user_settings.master_volume);
		button->button_action(state);
	}

	void zoom_by(sys::state& state, float factor) {
		state.map_state.zoom = std::clamp(state.map_state.zoom * factor, map::min_zoom, map::max_zoom);
	}

	void activate(sys::state& state, hit_target const& t) {
		switch(t.act) {
		case action::press:
			press(state, t.button);
			return;
		case action::toggle_layers:
			layers_open = !layers_open;
			break;
		case action::set_layer:
			map_mode::set_map_mode(state, map_mode::mode(uint8_t(t.value)));
			state.ui_state.root->impl_on_update(state);
			break;
		case action::zoom_in:
			zoom_by(state, 1.6f);
			break;
		case action::zoom_out:
			zoom_by(state, 1.f / 1.6f);
			break;
		case action::capital:
			if(auto const capital = state.world.nation_get_capital(state.local_player_nation); capital)
				state.map_state.center_map_on_province(state, capital);
			break;
		case action::close_province:
			state.set_selected_province(dcon::province_id{});
			break;
		case action::province_details:
			if(auto* window = state.ui_state.province_window; window) {
				window->set_visible(state, true);
				window->impl_on_update(state);
				state.ui_state.root->move_child_to_front(window);
			}
			break;
		case action::center_province:
			if(snap.province)
				state.map_state.center_map_on_province(state, snap.province);
			break;
		case action::diplomacy:
			if(snap.owner)
				state.open_diplomacy(snap.owner);
			break;
		case action::toggle_outliner:
			if(auto* outliner = state.ui_state.outliner_window; outliner)
				outliner->set_visible(state, !outliner->is_visible());
			break;
		case action::toggle_log:
			if(auto* log = state.ui_state.msg_log_window; log)
				log->set_visible(state, !log->is_visible());
			break;
		case action::none:
			return;
		}
		play_click(state);
	}

	bool project(sys::state& state, glm::vec2 map_position, glm::vec2& out) {
		screen_space::point_ui screen_position;
		if(!state.map_state.map_to_screen(state.map_state.normalize_map_coord(map_position), glm::vec2{ width(state), height(state) },
			state.user_settings.map_is_globe, screen_position, glm::vec2{ 60.f, 60.f }))
			return false;
		out = screen_position.data;
		return true;
	}

	bool claim(rect r) {
		if(r.y < top_h)
			return false;
		for(auto const& other : placed) {
			if(r.x < other.x + other.w && other.x < r.x + r.w && r.y < other.y + other.h && other.y < r.y + r.h)
				return false;
		}
		placed.push_back(r);
		return true;
	}

	// Lettering on the map: light type on a dark offset copy, so that it holds
	// on any country colour.
	void map_text(sys::state& state, std::string_view content, float x, float center_y, int32_t size, float tracking, ogl::color3f color) {
		auto const offset = std::max(1.f, float(size) / 16.f);
		draw_text(state, content, x + offset, center_y + offset, size, tracking, ogl::color3f{ 0.03f, 0.04f, 0.06f });
		draw_text(state, content, x, center_y, size, tracking, color);
	}

	// Names follow the scale: countries at world level, capitals as the camera
	// comes down, regions at the closest levels. A name that would collide
	// with a more important one is left out.
	void draw_map_names(sys::state& state, float w, float h) {
		placed.clear();
		auto const zoom = state.map_state.zoom;
		constexpr float tracking = 0.28f;
		constexpr int32_t reference = 20;

		for(auto const& label : labels) {
			glm::vec2 centre;
			glm::vec2 beside;
			if(!project(state, label.anchor, centre) || !project(state, label.anchor + glm::vec2{ 8.f, 0.f }, beside))
				continue;
			auto const scale = glm::distance(centre, beside) / 8.f;
			auto const room_w = label.span_x * scale;
			auto const room_h = label.span_y * scale;
			if(room_w > w * 1.5f)
				continue; // deep inside the country its name is no longer a landmark
			auto const reference_w = shaped(state, label.text, reference, tracking).width;
			if(reference_w <= 0.f)
				continue;
			auto const size = int32_t(std::min({ float(reference) * 0.78f * room_w / reference_w, 0.55f * room_h, 40.f }));
			if(size < 9)
				continue;
			auto const text_w = shaped(state, label.text, size, tracking).width;
			if(!claim(rect{ centre.x - text_w * 0.5f - 3.f, centre.y - float(size) * 0.65f, text_w + 6.f, float(size) * 1.3f }))
				continue;
			map_text(state, label.text, centre.x - text_w * 0.5f, centre.y, size, tracking, ivory);
		}

		if(zoom >= 2.2f) {
			auto const& shown_places = cities.empty() ? capitals : cities;
			auto const shown = zoom < 3.5f ? size_t(36) : (zoom < 6.f ? size_t(120) : (zoom < 10.f ? size_t(320) : shown_places.size()));
			for(size_t i = 0; i < shown_places.size() && i < shown; ++i) {
				auto const& city = shown_places[i];
				glm::vec2 at;
				if(!project(state, city.position, at))
					continue;
				auto const size = city.capital ? 12 : 11;
				auto const text_w = shaped(state, city.name, size, 0.04f).width;
				if(!claim(rect{ at.x - 5.f, at.y - 8.f, text_w + 16.f, 16.f }))
					continue;
				if(city.capital) {
					ogl::render_alpha_colored_rect(state, at.x - 3.5f, at.y - 3.5f, 7.f, 7.f, 0.03f, 0.04f, 0.06f, 0.95f);
					ogl::render_alpha_colored_rect(state, at.x - 2.f, at.y - 2.f, 4.f, 4.f, gold.r, gold.g, gold.b, 1.f);
				} else {
					ogl::render_alpha_colored_rect(state, at.x - 2.5f, at.y - 2.5f, 5.f, 5.f, 0.03f, 0.04f, 0.06f, 0.95f);
					ogl::render_alpha_colored_rect(state, at.x - 1.5f, at.y - 1.5f, 3.f, 3.f, ivory.r, ivory.g, ivory.b, 1.f);
				}
				map_text(state, city.name, at.x + 8.f, at.y, size, 0.04f, city.capital ? ivory : mix(ivory, muted, 0.3f));
			}
		}

		if(zoom >= 9.f) {
			for(auto const& region : regions) {
				glm::vec2 at;
				if(!project(state, region.position, at) || at.x < 0.f || at.x > w || at.y < top_h || at.y > h)
					continue;
				auto const text_w = shaped(state, region.name, 11, 0.06f).width;
				if(!claim(rect{ at.x - text_w * 0.5f - 4.f, at.y - 8.f, text_w + 8.f, 16.f }))
					continue;
				map_text(state, region.name, at.x - text_w * 0.5f, at.y, 11, 0.06f, mix(ivory, muted, 0.35f));
			}
		}
	}

	void draw_plus(sys::state& state, float cx, float cy, bool minus, ogl::color3f color) {
		fill(state, cx - 5.f, cy - 0.75f, 10.f, 1.5f, color, 1.f);
		if(!minus)
			fill(state, cx - 0.75f, cy - 5.f, 1.5f, 10.f, color, 1.f);
	}

	void draw_top_bar(sys::state& state, float w, bool hovering, float mx, float my) {
		ogl::render_alpha_colored_rect(state, 0.f, 0.f, w, bar_h, 0.030f, 0.037f, 0.055f, 0.97f);
		ogl::render_alpha_colored_rect(state, 0.f, bar_h, w, strip_h, 0.050f, 0.060f, 0.085f, 0.94f);
		fill(state, 0.f, bar_h, w, 1.f, gold, 0.14f);
		fill(state, 0.f, top_h, w, 1.f, gold, 0.50f);
		panels.push_back(rect{ 0.f, 0.f, w, top_h + 1.f });

		auto const mid = bar_h * 0.5f;

		// Left: the player's nation; a click brings the capital into view.
		auto left_end = edge;
		if(snap.player) {
			draw_flag(state, snap.player, edge, mid - 11.f, 33.f, 22.f);
			auto const name = fit(state, snap.player_name, 16, 0.06f, 220.f);
			auto const name_w = draw_text(state, name, edge + 45.f, mid, 16, 0.06f, ivory);
			left_end = edge + 45.f + name_w;
			targets.push_back(hit_target{ 0.f, 0.f, left_end + 10.f, bar_h, action::capital });
		}

		// Right: date and game speed. The date is the pause switch.
		auto const is_paused = paused(state);
		auto x = w - edge - 24.f;
		{
			hit_target t{ x, 0.f, 24.f, bar_h, action::press, faster_button };
			auto const hovered = hovering && over(t, mx, my);
			if(hovered)
				fill(state, t.x, 5.f, t.w, bar_h - 10.f, gold, 0.12f);
			draw_plus(state, x + 12.f, mid, false, hovered ? gold : ivory);
			targets.push_back(t);
		}
		auto const speed = state.actual_game_speed.load(std::memory_order::acquire) > 0 ? state.actual_game_speed.load(std::memory_order::acquire) : state.ui_state.held_game_speed;
		x -= 4.f + 5.f * 8.f;
		for(int32_t i = 0; i < 5; ++i) {
			auto const lit = i < speed;
			fill(state, x + float(i) * 8.f, mid - 6.f, 5.f, 12.f, lit ? gold : dim, lit ? (is_paused ? 0.35f : 0.95f) : 0.45f);
		}
		x -= 4.f + 24.f;
		{
			hit_target t{ x, 0.f, 24.f, bar_h, action::press, slower_button };
			auto const hovered = hovering && over(t, mx, my);
			if(hovered)
				fill(state, t.x, 5.f, t.w, bar_h - 10.f, gold, 0.12f);
			draw_plus(state, x + 12.f, mid, true, hovered ? gold : ivory);
			targets.push_back(t);
		}
		x -= 14.f;
		auto const date_w = shaped(state, snap.date, 15, 0.05f).width;
		auto const date_x = x - date_w;
		auto right_start = date_x - 26.f;
		{
			hit_target t{ right_start - 6.f, 0.f, x - right_start + 12.f, bar_h, action::press, pause_button };
			auto const hovered = hovering && over(t, mx, my);
			if(hovered)
				fill(state, t.x, 5.f, t.w, bar_h - 10.f, gold, 0.10f);
			if(is_paused) {
				fill(state, right_start + 2.f, mid - 6.f, 3.5f, 12.f, gold, 1.f);
				fill(state, right_start + 9.f, mid - 6.f, 3.5f, 12.f, gold, 1.f);
			} else {
				for(int32_t i = 0; i < 10; ++i) {
					auto const half = 6.f * (1.f - float(i) / 10.f);
					fill(state, right_start + 2.f + float(i), mid - half, 1.1f, half * 2.f, ivory, 0.9f);
				}
			}
			draw_text(state, snap.date, date_x, mid, 15, 0.05f, is_paused ? gold : ivory);
			targets.push_back(t);
			right_start = t.x;
		}

		// Centre: the country windows.
		std::array<std::string, 8> labels;
		auto const lane_x = left_end + 30.f;
		auto const lane_w = right_start - 20.f - lane_x;
		auto size = 13;
		auto tracking = 0.16f;
		auto total = 0.f;
		for(int32_t attempt = 0; attempt < 3; ++attempt) {
			total = 0.f;
			for(size_t i = 0; i < tabs.size(); ++i) {
				labels[i] = localized(state, tabs[i].key, tabs[i].fallback);
				total += shaped(state, labels[i], size, tracking).width;
			}
			if(total + float(tabs.size()) * 14.f <= lane_w)
				break;
			size -= 1;
			tracking -= 0.05f;
		}
		auto const gap = std::clamp((lane_w - total) / float(tabs.size()), 8.f, 44.f);
		auto tab_x = lane_x + std::max(0.f, (lane_w - total - gap * float(tabs.size())) * 0.5f);
		for(size_t i = 0; i < tabs.size(); ++i) {
			auto const label_w = shaped(state, labels[i], size, tracking).width;
			hit_target t{ tab_x, 0.f, label_w + gap, bar_h, action::press, tab_buttons[i] };
			auto const active = tab_buttons[i] && static_cast<topbar_tab_button*>(tab_buttons[i])->is_active(state);
			auto const hovered = hovering && over(t, mx, my);
			if(hovered && !active)
				fill(state, t.x, 0.f, t.w, bar_h, gold, 0.07f);
			if(active) {
				fill(state, t.x, 0.f, t.w, bar_h, gold, 0.12f);
				fill(state, t.x, bar_h - 2.f, t.w, 2.f, gold, 1.f);
			}
			draw_text(state, labels[i], tab_x + gap * 0.5f, mid, size, tracking, active ? gold : (hovered ? mix(ivory, gold, 0.45f) : ivory));
			if(tab_buttons[i])
				targets.push_back(t);
			tab_x += t.w;
		}

		// Second line: the figures a player checks without opening anything.
		auto const strip_mid = bar_h + strip_h * 0.5f + 0.5f;
		auto const status_w = snap.player_status.empty() ? 0.f : shaped(state, snap.player_status, 11, 0.14f).width;
		if(status_w > 0.f)
			draw_text(state, snap.player_status, w - edge, strip_mid, 11, 0.14f, gold, true);
		auto stat_x = edge;
		auto const stat_limit = w - edge - status_w - 24.f;
		for(auto const& stat : snap.stats) {
			auto const label_w = shaped(state, stat.label, 10, 0.14f).width;
			auto const value_w = shaped(state, stat.value, 13, 0.02f).width;
			if(stat_x + label_w + 7.f + value_w > stat_limit)
				break;
			draw_text(state, stat.label, stat_x, strip_mid, 10, 0.14f, muted);
			draw_text(state, stat.value, stat_x + label_w + 7.f, strip_mid, 13, 0.02f, stat.warning ? alarm : ivory);
			stat_x += label_w + 7.f + value_w + 24.f;
		}
	}

	void draw_card(sys::state& state, bool hovering, float mx, float my) {
		auto const sea = snap.province_rows.empty();
		auto const has_owner = bool(snap.owner);
		auto const foreign = has_owner && snap.owner != snap.player;

		auto h = 64.f;
		if(!sea)
			h += float(snap.province_rows.size()) * row_pitch + 12.f;
		h += 26.f + pad;
		if(has_owner) {
			h += 14.f + 34.f + 26.f + 62.f + float(snap.owner_rows.size()) * row_pitch + 8.f;
			if(foreign)
				h += 26.f + 12.f;
		}

		auto const x = edge;
		auto const top = top_h + edge;
		auto const right = x + card_w - pad;
		auto const left = x + pad;
		draw_panel(state, x, top, card_w, h);

		// The province.
		{
			hit_target close{ x + card_w - 36.f, top + 8.f, 28.f, 28.f, action::close_province };
			auto const hovered = hovering && over(close, mx, my);
			centered(state, "×", close.x, close.w, top + 23.f, 20, 0.f, hovered ? gold : muted);
			targets.push_back(close);
		}
		draw_text(state, fit(state, snap.province_name, 18, 0.05f, card_w - 2.f * pad - 28.f), left, top + 24.f, 18, 0.05f, ivory);
		if(!snap.province_state.empty())
			draw_text(state, fit(state, snap.province_state, 12, 0.10f, card_w - 2.f * pad), left, top + 46.f, 12, 0.10f, gold);
		draw_rule(state, left, top + 62.f, right - left, 0.40f);

		auto y = top + 64.f;
		if(!sea) {
			for(auto const& row : snap.province_rows) {
				label_row(state, row, left, right, y + row_pitch * 0.5f);
				y += row_pitch;
			}
			y += 12.f;
			auto const half = (right - left - 8.f) * 0.5f;
			text_button(state, left, y, half, 26.f, localized(state, "katechon_hud_details", "DETAILS"), hit_target{ 0.f, 0.f, 0.f, 0.f, action::province_details }, hovering, mx, my);
			text_button(state, left + half + 8.f, y, half, 26.f, localized(state, "katechon_hud_center", "CENTER"), hit_target{ 0.f, 0.f, 0.f, 0.f, action::center_province }, hovering, mx, my);
		} else {
			text_button(state, left, y, right - left, 26.f, localized(state, "katechon_hud_center", "CENTER"), hit_target{ 0.f, 0.f, 0.f, 0.f, action::center_province }, hovering, mx, my);
		}
		y += 26.f + pad;
		if(!has_owner)
			return;

		// The state that owns it.
		ogl::render_alpha_colored_rect(state, x + 1.f, y, card_w - 2.f, top + h - y - 1.f, 0.060f, 0.072f, 0.100f, 0.55f);
		fill(state, x, y, card_w, 1.f, gold, 0.30f);
		y += 14.f;
		draw_flag(state, snap.owner, left, y + 1.f, 48.f, 32.f);
		draw_text(state, fit(state, snap.owner_name, 16, 0.04f, right - left - 60.f), left + 60.f, y + 9.f, 16, 0.04f, ivory);
		draw_text(state, fit(state, snap.owner_status, 11, 0.14f, right - left - 60.f), left + 60.f, y + 27.f, 11, 0.14f, gold);
		y += 34.f;
		draw_text(state, fit(state, snap.owner_government, 12, 0.03f, right - left), left, y + 14.f, 12, 0.03f, muted);
		y += 26.f;

		auto const cell = (right - left) / float(snap.scores.size());
		fill(state, left, y, right - left, 1.f, gold, 0.14f);
		for(size_t i = 0; i < snap.scores.size(); ++i) {
			auto const cx = left + float(i) * cell;
			if(i > 0)
				fill(state, cx, y + 8.f, 1.f, 46.f, gold, 0.14f);
			centered(state, fit(state, snap.scores[i].label, 9, 0.10f, cell - 4.f), cx, cell, y + 13.f, 9, 0.10f, muted);
			centered(state, snap.scores[i].value, cx, cell, y + 31.f, 17, 0.02f, ivory);
			centered(state, snap.scores[i].rank, cx, cell, y + 49.f, 11, 0.04f, gold);
		}
		y += 62.f;
		fill(state, left, y - 1.f, right - left, 1.f, gold, 0.14f);

		for(auto const& row : snap.owner_rows) {
			label_row(state, row, left, right, y + row_pitch * 0.5f);
			y += row_pitch;
		}
		y += 8.f;
		if(foreign)
			text_button(state, left, y, right - left, 26.f, localized(state, "katechon_hud_open_diplomacy", "DIPLOMACY"), hit_target{ 0.f, 0.f, 0.f, 0.f, action::diplomacy }, hovering, mx, my);
	}

	void draw_navigation(sys::state& state, float w, float h, bool hovering, float mx, float my) {
		struct entry {
			std::string label;
			hit_target target;
			bool lit = false;
		};
		auto bottom = h - edge;
		auto* log = state.ui_state.msg_log_window;
		auto const log_open = log && log->is_visible();
		if(log_open)
			bottom = std::min(bottom, float(ui::get_absolute_location(state, *log).y) - 8.f);
		auto* outliner = state.ui_state.outliner_window;

		auto const mode = uint8_t(state.map_state.active_map_mode);
		auto const mode_name = localized(state, "mapmode_" + std::to_string(int32_t(mode)), "");
		auto const layers_label = localized(state, "katechon_hud_layers", "LAYERS");

		std::vector<entry> entries;
		entries.push_back(entry{ localized(state, "katechon_hud_capital", "CAPITAL"), hit_target{ 0.f, 0.f, 0.f, 0.f, action::capital } });
		if(search_button)
			entries.push_back(entry{ localized(state, "katechon_hud_search", "SEARCH"), hit_target{ 0.f, 0.f, 0.f, 0.f, action::press, search_button } });
		if(ledger_button)
			entries.push_back(entry{ localized(state, "katechon_hud_ledger", "LEDGER"), hit_target{ 0.f, 0.f, 0.f, 0.f, action::press, ledger_button } });
		if(economy_button)
			entries.push_back(entry{ localized(state, "katechon_hud_economy", "ECONOMY"), hit_target{ 0.f, 0.f, 0.f, 0.f, action::press, economy_button } });
		if(outliner)
			entries.push_back(entry{ localized(state, "katechon_hud_outliner", "OUTLINER"), hit_target{ 0.f, 0.f, 0.f, 0.f, action::toggle_outliner }, outliner->is_visible() });
		if(log)
			entries.push_back(entry{ localized(state, "katechon_hud_log", "LOG"), hit_target{ 0.f, 0.f, 0.f, 0.f, action::toggle_log }, log_open });
		if(menu_button)
			entries.push_back(entry{ localized(state, "katechon_hud_menu", "MENU"), hit_target{ 0.f, 0.f, 0.f, 0.f, action::press, menu_button } });

		constexpr float inset = 13.f;
		auto const layers_w = shaped(state, layers_label, 12, 0.16f).width + (mode_name.empty() ? 0.f : 9.f + shaped(state, mode_name, 13, 0.03f).width) + 2.f * inset + 4.f;
		auto strip_w = layers_w;
		for(auto const& e : entries)
			strip_w += shaped(state, e.label, 12, 0.16f).width + 2.f * inset;

		auto const strip_x = w - edge - strip_w;
		auto const strip_y = bottom - nav_h;
		auto const mid = strip_y + nav_h * 0.5f;
		draw_panel(state, strip_x, strip_y, strip_w, nav_h);

		// Layers: the current one is named on the switch itself.
		{
			hit_target t{ strip_x, strip_y, layers_w, nav_h, action::toggle_layers };
			auto const hovered = hovering && over(t, mx, my);
			if(layers_open || hovered)
				fill(state, t.x, t.y + 1.f, t.w, t.h - 2.f, gold, layers_open ? 0.16f : 0.08f);
			auto const label_w = draw_text(state, layers_label, strip_x + inset, mid, 12, 0.16f, layers_open ? gold : muted);
			draw_text(state, mode_name, strip_x + inset + label_w + 9.f, mid, 13, 0.03f, gold);
			targets.push_back(t);
		}
		auto x = strip_x + layers_w;
		for(auto& e : entries) {
			auto const label_w = shaped(state, e.label, 12, 0.16f).width;
			e.target.x = x;
			e.target.y = strip_y;
			e.target.w = label_w + 2.f * inset;
			e.target.h = nav_h;
			auto const hovered = hovering && over(e.target, mx, my);
			fill(state, x, strip_y + 8.f, 1.f, nav_h - 16.f, gold, 0.22f);
			if(hovered)
				fill(state, x + 1.f, strip_y + 1.f, e.target.w - 1.f, nav_h - 2.f, gold, 0.08f);
			if(e.lit)
				fill(state, x + inset, strip_y + nav_h - 5.f, label_w, 2.f, gold, 1.f);
			draw_text(state, e.label, x + inset, mid, 12, 0.16f, e.lit ? gold : (hovered ? mix(ivory, gold, 0.45f) : ivory));
			targets.push_back(e.target);
			x += e.target.w;
		}

		// Zoom: a rail with the current depth marked on it.
		constexpr float rail = 66.f;
		auto const zoom_h = zoom_w * 2.f + rail;
		auto const zoom_x = w - edge - zoom_w;
		auto const zoom_y = strip_y - 8.f - zoom_h;
		draw_panel(state, zoom_x, zoom_y, zoom_w, zoom_h);
		{
			hit_target in{ zoom_x, zoom_y, zoom_w, zoom_w, action::zoom_in };
			hit_target out{ zoom_x, zoom_y + zoom_w + rail, zoom_w, zoom_w, action::zoom_out };
			auto const in_hovered = hovering && over(in, mx, my);
			auto const out_hovered = hovering && over(out, mx, my);
			if(in_hovered)
				fill(state, in.x + 1.f, in.y + 1.f, in.w - 2.f, in.h - 1.f, gold, 0.12f);
			if(out_hovered)
				fill(state, out.x + 1.f, out.y, out.w - 2.f, out.h - 1.f, gold, 0.12f);
			draw_plus(state, zoom_x + zoom_w * 0.5f, in.y + zoom_w * 0.5f, false, in_hovered ? gold : ivory);
			draw_plus(state, zoom_x + zoom_w * 0.5f, out.y + zoom_w * 0.5f, true, out_hovered ? gold : ivory);
			auto const depth = std::clamp(std::log(state.map_state.zoom / map::min_zoom) / std::log(map::max_zoom / map::min_zoom), 0.f, 1.f);
			auto const rail_x = zoom_x + zoom_w * 0.5f;
			fill(state, rail_x - 0.5f, zoom_y + zoom_w + 4.f, 1.f, rail - 8.f, gold, 0.30f);
			fill(state, rail_x - 5.f, zoom_y + zoom_w + 4.f + (rail - 10.f) * (1.f - depth), 10.f, 2.f, gold, 1.f);
			targets.push_back(in);
			targets.push_back(out);
		}

		if(!layers_open)
			return;

		// The layer list opens above the strip.
		constexpr int32_t columns = (layer_count + layer_rows - 1) / layer_rows;
		auto const list_w = float(columns) * layer_col_w + 2.f * 10.f;
		auto const list_h = 40.f + float(layer_rows) * layer_pitch + 10.f;
		auto const list_x = std::max(edge, std::min(strip_x, zoom_x - 8.f - list_w));
		auto const list_y = std::max(top_h + edge, strip_y - 8.f - list_h);
		draw_panel(state, list_x, list_y, list_w, list_h);
		draw_text(state, localized(state, "katechon_hud_layers_title", "MAP LAYERS"), list_x + pad, list_y + 20.f, 12, 0.20f, gold);
		draw_rule(state, list_x + pad, list_y + 36.f, list_w - 2.f * pad, 0.35f);
		for(int32_t i = 0; i < layer_count; ++i) {
			auto const id = i + 1;
			auto const name = localized(state, "mapmode_" + std::to_string(id), "");
			if(name.empty())
				continue;
			hit_target t{ list_x + 10.f + float(i / layer_rows) * layer_col_w, list_y + 40.f + float(i % layer_rows) * layer_pitch, layer_col_w, layer_pitch, action::set_layer, nullptr, id };
			auto const active = id == int32_t(mode);
			auto const hovered = hovering && over(t, mx, my);
			if(active) {
				fill(state, t.x, t.y + 1.f, t.w - 4.f, t.h - 2.f, gold, 0.14f);
				fill(state, t.x, t.y + 3.f, 2.f, t.h - 6.f, gold, 1.f);
			} else if(hovered) {
				fill(state, t.x, t.y + 1.f, t.w - 4.f, t.h - 2.f, gold, 0.07f);
			}
			draw_text(state, fit(state, name, 13, 0.02f, layer_col_w - 22.f), t.x + 10.f, t.y + layer_pitch * 0.5f, 13, 0.02f, active ? gold : (hovered ? mix(ivory, gold, 0.45f) : ivory));
			targets.push_back(t);
		}
	}

public:
	void on_update(sys::state& state) noexcept override {
		refresh(state);
	}

	mouse_probe impl_probe_mouse(sys::state& state, int32_t x, int32_t y, mouse_probe_type type) noexcept override {
		for(auto const& p : panels) {
			if(float(x) >= p.x && float(x) < p.x + p.w && float(y) >= p.y && float(y) < p.y + p.h)
				return mouse_probe{ this, xy_pair{ int16_t(x), int16_t(y) } };
		}
		return mouse_probe{ nullptr, xy_pair{ int16_t(x), int16_t(y) } };
	}

	message_result on_lbutton_down(sys::state& state, int32_t x, int32_t y, sys::key_modifiers mods) noexcept override {
		// Later targets are drawn on top, so they are asked first.
		for(size_t i = targets.size(); i-- > 0;) {
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
		return message_result::consumed;
	}

	message_result on_key_down(sys::state& state, sys::virtual_key key, sys::key_modifiers mods) noexcept override {
		if(key == sys::virtual_key::ESCAPE && layers_open) {
			layers_open = false;
			return message_result::consumed;
		}
		return message_result::unseen;
	}

	void render(sys::state& state, int32_t x, int32_t y) noexcept override {
		if(!resolved)
			resolve(state);
		if(!snap.ready || snap.province != state.map_state.selected_province || snap.player != state.local_player_nation)
			refresh(state);

		auto const w = width(state);
		auto const h = height(state);
		auto const mx = float(state.mouse_x_position) / state.user_settings.ui_scale;
		auto const my = float(state.mouse_y_position) / state.user_settings.ui_scale;
		auto const hovering = state.ui_state.under_mouse == this;
		targets.clear();
		panels.clear();

		if(report_fps) {
			auto const now = std::chrono::steady_clock::now();
			if(++frames == 120) {
				std::fprintf(stderr, "HUD fps %.1f at zoom %.2f\n", 120.f / std::chrono::duration<float>(now - fps_mark).count(), state.map_state.zoom);
				frames = 0;
			}
			if(frames == 0)
				fps_mark = now;
		}

		// A country window takes the whole screen; only the bar stays with it.
		if(!country_window_open(state)) {
			draw_map_names(state, w, h);
			auto* details = state.ui_state.province_window;
			if(snap.province && !(details && details->is_visible()))
				draw_card(state, hovering, mx, my);
			draw_navigation(state, w, h, hovering, mx, my);
		}
		draw_top_bar(state, w, hovering, mx, my);
	}
};

}
