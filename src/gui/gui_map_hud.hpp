#pragma once

#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <optional>

#include "system_state.hpp"
#include "gui_element_types.hpp"
#include "gui_common_elements.hpp"
#include "gui_title_screen.hpp"
#include "gui_topbar.hpp"
#include "gui_map_hud_state.hpp"
#include "gui_map_hud_forces.hpp"
#include "demographics.hpp"
#include "map_modes.hpp"
#include "commands.hpp"
#include "governance/command.hpp"
#include "military/land_forces.hpp"
#include "military.hpp"
#include "economy/economy.hpp"
#include "economy/households.hpp"
#include "economy/human_development.hpp"
#include "economy/trade_picture.hpp"
#include "map/katechon_trade_lanes.hpp"
#include "nations/strategic_statecraft_forecast.hpp"
#include "user_interactions.hpp"
#include "stb_image_write.h"
#include "diplomatic_messages.hpp"

namespace ui {

// The KATECHON map HUD: a slim top bar, one card for the selected province and
// its owner, and a navigation strip with zoom and map layers. Like the title
// screen it is a single element that draws itself; the country windows and
// their rules still live in the old top bar and menu bar, which this element
// drives by pressing their (now undrawn) buttons.
class map_hud_element : public katechon_canvas {
	enum class action : uint8_t {
		none, press, toggle_layers, set_layer, zoom_in, zoom_out, capital, close_province, province_details,
		center_province, diplomacy, toggle_outliner, toggle_log, toggle_population, population_metric, population_overview, population_scroll_to, population_inspect,
		population_whole_country, population_home, population_region, toggle_military, pick_formation, military_back, military_order, military_march, military_more, military_mobilize,
		toggle_diplomacy, diplomacy_target, diplomacy_home, diplomacy_deed, dip_deal_toggle, dip_why, dip_item, dip_send, dip_trade, dip_answer, dip_radial, dip_radial_close, dip_tab, dip_review, toggle_trade, trade_details, trade_span, trade_focus, trade_analysis
	};
	// The cells of assets/katechon_hud_icons.png, in the order tools/katechon_ui/build_icons.py draws them.
	enum class icon : int32_t {
		layers, capital, search, ledger, economy, outliner, log, menu, minus, plus, chevron_right, person, work, movement, society, growth,
		chevron_down, pause, play, close, home, chevron_left, chevron_up, production, budget, technology, politics, trade, diplomacy, military, count
	};
	struct hit_target {
		float x = 0.f;
		float y = 0.f;
		float w = 0.f;
		float h = 0.f;
		action act = action::none;
		button_element_base* button = nullptr;
		int32_t value = 0;
		uint64_t formation = 0; // a canonical land formation, by stable id
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
	// What the population pages can paint the map with. Each is also one of
	// the figures the inspector lists, so choosing a figure chooses its map.
	enum class population_metric : uint8_t { density, work, migration, needs, literacy, count };
	struct share {
		std::string label;
		float part = 0.f;
	};
	// What the population pages show, for a country or for one of its regions.
	// Everything comes from the running simulation. A figure it does not keep
	// stays negative and is shown as missing, never as a zero.
	struct population_view {
		bool of_province = false;
		dcon::nation_id nation;
		dcon::province_id province;
		std::string title;
		std::string subtitle;
		float units = 0.f;
		float area = 0.f;
		float part_of_country = -1.f;
		// The month's change and what makes it, in units of population.
		bool drivers_known = false;
		float natural = 0.f; // births over deaths, before the losses below
		float starvation = 0.f;
		float overcrowding = 0.f;
		float transition = 0.f; // fewer children where towns and schooling spread
		float army = 0.f;
		float internal_month = 0.f; // net arrivals from other regions of the country
		float external_month = 0.f; // net arrivals from abroad
		float relocations = 0.f; // people moving between the country's regions, whatever the direction
		bool external_estimated = false; // the month's directed estimate, not today's moves
		float change_month = 0.f; // all of it together
		// How people make a living, as shares of the workforce; negative when not kept.
		float wage_share = -1.f;
		float own_share = -1.f;
		float employed = 0.f;
		float job_access = -1.f;
		float literacy = -1.f;
		float consciousness = -1.f;
		float militancy = -1.f;
		float life = -1.f;
		float everyday = -1.f;
		float luxury = -1.f;
		float poor = 0.f;
		float middle = 0.f;
		float rich = 0.f;
		std::array<std::array<float, 3>, 3> strata_needs{}; // poor, middle, rich by basic, everyday, luxury
		float accepted = -1.f;
		float education_access = -1.f;
		float urbanization = -1.f;
		float housing_access = -1.f;
		float development = -1.f;
		std::vector<share> occupations;
		std::vector<share> cultures;
		std::vector<share> religions;
		std::vector<float> history; // the country's total over the last days, oldest first
		struct region {
			dcon::province_id id;
			std::string name;
			float units = 0.f;
			float growth = 0.f; // natural, a month
			float needs = -1.f;
			float literacy = -1.f;
			float moved = 0.f; // net arrivals, a month
		};
		std::vector<region> regions; // every region of the country, largest first
		std::vector<region> gaining; // most arrivals first
		std::vector<region> losing; // most departures first
		struct partner {
			dcon::nation_id nation;
			float amount = 0.f; // positive: they come from there
		};
		std::vector<partner> abroad; // the countries people leave for or come from, largest flow first
		// The regions that stand out on the map for the figure painted on it.
		std::vector<std::pair<dcon::province_id, float>> highest;
		std::vector<std::pair<dcon::province_id, float>> lowest;
		int32_t rank = 0; // of the region among its country's, for that figure
		int32_t ranked = 0;
	};
	// A land formation as the map and the panel show it: itself, and the sum of
	// itself with everything under it.
	struct force_unit {
		uint64_t id = 0;
		uint64_t parent = 0;
		dcon::nation_id owner;
		bool own = false;
		bool grouping = false; // other formations answer to it
		bool marching = false;
		bool visible = false;
		bool folded = false; // told by its superior's mark
		military::land_forces::echelon level = military::land_forces::echelon::unspecified;
		military::land_forces::march_block blocked = military::land_forces::march_block::none;
		int32_t depth = 0;
		uint16_t days_left = 0;
		float leg = 0.f; // how much of the present leg is behind it
		std::string name;
		std::string tag;
		dcon::province_id at;
		dcon::province_id destination;
		glm::vec2 position{ 0.f, 0.f };
		glm::vec2 towards{ 0.f, 0.f };
		glm::vec2 goal{ 0.f, 0.f };
		glm::vec2 shown{ 0.f, 0.f }; // where its mark stands at the present scale
		military::land_forces::force_summary total;
	};
	struct force_view {
		std::vector<force_unit> units; // by id
		military::land_forces::force_summary national;
		military::land_forces::force_summary focus; // what is chosen; the whole when nothing is
		std::vector<labelled> equipment;
		std::vector<labelled> supply;
		std::vector<labelled> depots;
		bool can_command = false;
		bool can_mobilize = false;
		bool mobilized = false;
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
		// A figure let into the bar: what it is, and which way it is going.
		struct figure {
			icon image = icon::count;
			std::string label;
			std::string value;
			std::string note;
			int32_t way = 0; // rising, falling, or neither
		};
		std::vector<figure> wells;
		std::vector<labelled> alerts; // shown only once they are a concern
		std::string standing; // what the rank is made of

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
	inline static constexpr float hang = 0.f; // between the bar and what is docked under it
	inline static constexpr float ribbon_w = 52.f;
	inline static constexpr float top_h = bar_h + strip_h;
	inline static constexpr float edge = 12.f;
	inline static constexpr float card_w = 300.f;
	inline static constexpr float pad = 18.f;
	inline static constexpr float row_pitch = 24.f;
	inline static constexpr float tool_h = 36.f;
	inline static constexpr float tool_w = 34.f;
	inline static constexpr int32_t layer_count = 45;
	inline static constexpr int32_t layer_rows = 15;
	inline static constexpr float layer_col_w = 172.f;
	inline static constexpr float layer_pitch = 22.f;
	inline static constexpr ogl::color3f alarm{ 0.86f, 0.44f, 0.38f };
	inline static constexpr ogl::color3f bronze{ 0.62f, 0.50f, 0.29f };
	inline static constexpr ogl::color3f ink{ 0.008f, 0.010f, 0.016f };
	// Gold marks the player's own state, what is selected and what can be
	// acted on. Figures and their labels are set in the cooler tones.
	inline static constexpr ogl::color3f bright{ 0.93f, 0.91f, 0.86f };
	inline static constexpr ogl::color3f cream{ 0.94f, 0.88f, 0.74f }; // type on the walnut of the rail
	inline static constexpr ogl::color3f bronze_light{ 0.74f, 0.62f, 0.42f };
	inline static constexpr ogl::color3f slate{ 0.60f, 0.68f, 0.76f };
	inline static constexpr ogl::color3f azure{ 0.40f, 0.59f, 0.73f };
	// The surfaces of the shell. Each kind of thing sits on its own: the
	// analytical panel is cool slate, a card about one place is warm, the
	// tools that float over the map are steel.
	inline static constexpr ogl::color3f panel_base{ 0.094f, 0.108f, 0.128f };
	inline static constexpr ogl::color3f panel_head{ 0.060f, 0.070f, 0.086f };
	inline static constexpr ogl::color3f panel_hero{ 0.128f, 0.148f, 0.176f };
	inline static constexpr ogl::color3f panel_tile{ 0.168f, 0.190f, 0.222f };
	inline static constexpr ogl::color3f panel_sheet{ 0.146f, 0.164f, 0.192f };
	inline static constexpr ogl::color3f panel_well{ 0.062f, 0.070f, 0.084f };
	inline static constexpr ogl::color3f card_base{ 0.098f, 0.128f, 0.170f };
	inline static constexpr ogl::color3f card_head{ 0.140f, 0.180f, 0.232f };
	inline static constexpr ogl::color3f tool_base{ 0.100f, 0.120f, 0.146f };

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
		dcon::nation_id nation;
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
	std::vector<std::string_view> named;
	float people_per_unit = 1.f;
	bool population_open = false; // the inspector beside the map, or the overview over it
	bool population_overview = false; // the overview puts the map aside
	bool population_layer = false; // the population pages paint the map themselves
	population_metric metric = population_metric::density;
	population_view population;
	dcon::nation_id atlas_nation; // the country being studied; the player's own when unset
	dcon::province_id atlas_province; // one of its regions, when a region is studied
	dcon::province_id atlas_seen_province; // a new choice on the map changes the region studied
	float overview_scroll = 0.f;
	float overview_content_h = 0.f;
	float overview_view_h = 0.f;
	std::array<float, 6> overview_anchor{}; // where each part of the overview begins, from its top
	float clip_top = -1e9f; // the part of the window the population pages may answer in
	float clip_bottom = 1e9f;
	ogl::lines population_trend{ 32 };
	std::vector<float> province_area; // km2 by map id; empty when the scenario gives none
	bool areas_read = false;
	int32_t repaint_in = 0;
	int32_t frames = 0;
	std::chrono::steady_clock::time_point fps_mark{};
	bool report_fps = false;
	float qa_zoom = 0.f;
	float qa_width = 0.f;
	float qa_mouse_x = -1.f;
	float qa_mouse_y = -1.f;
	dcon::province_id qa_province;
	std::vector<hit_target> targets;
	std::vector<rect> panels;
	GLuint icons = 0;
	GLuint plate = 0;
	GLuint shell_bar = 0;
	GLuint shell_time = 0;
	GLuint shell_well = 0;
	GLuint shell_raised = 0;
	std::array<std::string, 3> shown_values; // of the figures on the bar, to notice when one changes
	std::array<int32_t, 3> flash{};
	GLuint shell_keystone = 0;
	GLuint shell_flagframe = 0;
	GLuint shell_ribbon = 0;
	GLuint shell_ribbon_tail = 0;
	GLuint shell_rounds = 0;
	GLuint shell_glow = 0;
	std::string tip; // what the control under the mouse does; shown beside it
	rect tip_anchor;
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
	force_view forces;
	std::vector<rect> chips;
	bool military_open = false;
	bool military_more = false;
	bool awaiting_destination = false; // the next press on the map names where the chosen are to march
	bool forces_stale = false;
	bool shift_held = false;
	std::string qa_formation;
	int32_t qa_order = 0;

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
			auto const map_id = std::atoi(id);
			if(map_id > 0 && uint32_t(map_id) <= state.world.province_size()) {
				qa_province = province::from_map_id(uint16_t(map_id));
				// KATECHON_HUD_SELECT=0 only aims the camera at it.
				if(auto const* select = std::getenv("KATECHON_HUD_SELECT"); !(select && select[0] == '0'))
					state.set_selected_province(qa_province);
			}
		}
		if(auto const* open = std::getenv("KATECHON_HUD_LAYERS"); open && open[0] == '1')
			layers_open = true;
		// KATECHON_HUD_ZOOM=<factor> holds the camera at that depth over the
		// chosen province, whatever the mouse does meanwhile; KATECHON_HUD_FPS=1
		// reports the frame rate on stderr.
		if(auto const* depth = std::getenv("KATECHON_HUD_ZOOM"); depth && depth[0])
			qa_zoom = std::clamp(float(std::atof(depth)), map::min_zoom, map::max_zoom);
		if(auto const* fps = std::getenv("KATECHON_HUD_FPS"); fps && fps[0] == '1')
			report_fps = true;
		// KATECHON_HUD_WIDTH=<points> lays the HUD out as on a window that narrow.
		if(auto const* narrow = std::getenv("KATECHON_HUD_WIDTH"); narrow && narrow[0])
			qa_width = float(std::atof(narrow));
		// KATECHON_HUD_MOUSE=<x>,<y> draws the HUD as if the mouse rested there.
		if(auto const* at = std::getenv("KATECHON_HUD_MOUSE"); at && at[0])
			std::sscanf(at, "%f,%f", &qa_mouse_x, &qa_mouse_y);
		// KATECHON_HUD_POPULATION=<0..4> opens the population inspector on that map
		// layer, for the chosen province if there is one. KATECHON_HUD_POPULATION_OVERVIEW=1
		// opens the overview of the country, =2 of the chosen province.
		if(auto const* atlas = std::getenv("KATECHON_HUD_POPULATION"); atlas && atlas[0] >= '0' && atlas[0] <= '4') {
			metric = population_metric(uint8_t(atlas[0] - '0'));
			open_population(state, state.map_state.selected_province, false);
		}
		if(auto const* overview = std::getenv("KATECHON_HUD_POPULATION_OVERVIEW"); overview && (overview[0] == '1' || overview[0] == '2'))
			open_population(state, overview[0] == '2' ? state.map_state.selected_province : dcon::province_id{}, true);
		// KATECHON_HUD_MILITARY=1 opens the military panel; KATECHON_HUD_FORMATION=<name key>
		// chooses a formation; KATECHON_HUD_ORDER=<province> then orders it to march there.
		if(auto const* open = std::getenv("KATECHON_HUD_MILITARY"); open && open[0] == '1')
			open_military(state);
		if(auto const* key = std::getenv("KATECHON_HUD_FORMATION"); key && key[0])
			qa_formation = key;
		if(auto const* destination = std::getenv("KATECHON_HUD_ORDER"); destination && destination[0])
			qa_order = std::atoi(destination);
		// Every way into diplomacy leads to the sheet. KATECHON_HUD_DIPLOMACY=<tag> opens
		// it on that country (1: the player's own); KATECHON_HUD_DIPLOMACY_SECTION=<0..2> on a reading.
		the_hud = this;
		sys::diplomacy_screen = [](sys::state& s, dcon::nation_id target) {
			if(the_hud)
				the_hud->open_diplomacy_panel(s, target ? target : s.local_player_nation);
		};
		// What became of our own proposals is told on the HUD, not in the old popups.
		state.user_settings.self_message_settings[int32_t(sys::message_setting_type::mil_access_declined_on_nation)] = sys::message_response::standard_log;
		state.user_settings.self_message_settings[int32_t(sys::message_setting_type::alliance_declined_on_nation)] = sys::message_response::standard_log;
		sys::diplomacy_context = [](sys::state& s, dcon::nation_id target, dcon::province_id) {
			if(the_hud)
				the_hud->open_radial(s, target);
		};
		if(auto const* tag = std::getenv("KATECHON_HUD_DIPLOMACY"); tag && tag[0])
			qa_diplomacy = tag;
		// KATECHON_HUD_DEAL=<gives>,<asks> opens the table with those items; KATECHON_HUD_RADIAL=1 the ring.
		if(auto const* deal = std::getenv("KATECHON_HUD_DEAL"); deal && deal[0]) {
			unsigned g = 0, a = 0;
			std::sscanf(deal, "%u,%u", &g, &a);
			deal_open = true;
			qa_deal_gives = uint16_t(g);
			qa_deal_asks = uint16_t(a);
		}
		if(auto const* ring = std::getenv("KATECHON_HUD_RADIAL"); ring && ring[0] == '1')
			qa_radial = true;
		// KATECHON_HUD_DIP_TAB=<0..5> opens the sheet's actions on a kind (5: the history).
		if(auto const* tab = std::getenv("KATECHON_HUD_DIP_TAB"); tab && tab[0])
			qa_dip_tab = std::clamp(std::atoi(tab), 0, 5);
		if(auto const* send = std::getenv("KATECHON_HUD_DEAL_SEND"); send && send[0] == '1')
			qa_deal_send = true;
		// KATECHON_HUD_TRADE=1 opens the trade mode, =2 its full analysis; the old dashboard's own preview keeps the old window.
		if(auto const* preview = std::getenv("ALICE_TRADE_DASHBOARD_PREVIEW"); preview && preview[0] == '1')
			legacy_trade = true;
		if(auto const* open = std::getenv("KATECHON_HUD_TRADE"); open && (open[0] == '1' || open[0] == '2')) {
			qa_trade = true;
			qa_trade_analysis = open[0] == '2';
		}
		// KATECHON_HUD_RUN=<1..5> lets time run at that speed.
		if(auto const* speed = std::getenv("KATECHON_HUD_RUN"); speed && speed[0] >= '1' && speed[0] <= '5') {
			for(auto step = '1'; step < speed[0]; ++step)
				press(state, faster_button);
			press(state, pause_button);
		}
		// The outliner is one switch away; it does not sit on the map by default.
		if(auto* outliner = state.ui_state.outliner_window; outliner)
			outliner->set_visible(state, false);
		auto* menu = state.ui_state.menubar_window;
		menu_button = child_button(state, menu, "menu_button");
		search_button = child_button(state, menu, "button_goto");
		ledger_button = child_button(state, menu, "ledger_button");
		economy_button = child_button(state, menu, "economy-scene-enter-button");

		auto root = simple_fs::get_root(state.common_fs);
		auto assets = simple_fs::open_directory(root, NATIVE("assets"));
		icons = ogl::make_gl_texture(assets, NATIVE("katechon_hud_icons.png"));
		plate = ogl::make_gl_texture(assets, NATIVE("katechon_plate.png"));
		shell_bar = ogl::make_gl_texture(assets, NATIVE("katechon_shell_bar.png"));
		shell_time = ogl::make_gl_texture(assets, NATIVE("katechon_shell_time.png"));
		shell_well = ogl::make_gl_texture(assets, NATIVE("katechon_shell_well.png"));
		shell_raised = ogl::make_gl_texture(assets, NATIVE("katechon_shell_raised.png"));
		shell_keystone = ogl::make_gl_texture(assets, NATIVE("katechon_shell_keystone.png"));
		shell_flagframe = ogl::make_gl_texture(assets, NATIVE("katechon_shell_flagframe.png"));
		shell_ribbon = ogl::make_gl_texture(assets, NATIVE("katechon_shell_ribbon.png"));
		shell_ribbon_tail = ogl::make_gl_texture(assets, NATIVE("katechon_shell_ribbon_tail.png"));
		shell_rounds = ogl::make_gl_texture(assets, NATIVE("katechon_shell_rounds.png"));
		shell_glow = ogl::make_gl_texture(assets, NATIVE("katechon_shell_glow.png"));
		for(auto const texture : { icons, plate, shell_bar, shell_time, shell_well, shell_raised, shell_keystone, shell_flagframe, shell_ribbon, shell_ribbon_tail, shell_rounds, shell_glow }) {
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

	void sheet(sys::state& state, float x, float y, float w, float h, ogl::color3f c, float alpha = 1.f) {
		ogl::render_alpha_colored_rect(state, x, y, w, h, c.r, c.g, c.b, alpha);
	}

	void frame(sys::state& state, float x, float y, float w, float h, ogl::color3f c, float alpha) {
		fill(state, x, y, w, 1.f, c, alpha);
		fill(state, x, y + h - 1.f, w, 1.f, c, alpha);
		fill(state, x, y + 1.f, 1.f, h - 2.f, c, alpha);
		fill(state, x + w - 1.f, y + 1.f, 1.f, h - 2.f, c, alpha);
	}

	// The shell stands a little above the map: a short shadow falls from its edges.
	void shadow_below(sys::state& state, float x, float y, float w) {
		for(int32_t i = 0; i < 5; ++i)
			fill(state, x, y + float(i), w, 1.f, ink, 0.34f * (1.f - float(i) / 5.f) * (1.f - float(i) / 5.f));
	}
	void shadow_right(sys::state& state, float x, float y, float h) {
		for(int32_t i = 0; i < 6; ++i)
			fill(state, x + float(i), y, 1.f, h, ink, 0.34f * (1.f - float(i) / 6.f) * (1.f - float(i) / 6.f));
	}

	// A plate that floats over the map (toolbar, lists, hints); it claims the mouse.
	void draw_panel(sys::state& state, float x, float y, float w, float h) {
		sheet(state, x, y, w, h, tool_base, 0.95f);
		frame(state, x, y, w, h, slate, 0.38f);
		fill(state, x + 1.f, y + 1.f, w - 2.f, 1.f, bright, 0.07f);
		shadow_below(state, x + 2.f, y + h, w - 2.f);
		panels.push_back(rect{ x, y, w, h });
	}

	// A plate built into the shell: it hangs from the top bar on the left edge
	// of the window, with a bronze border towards the map.
	void draw_dock(sys::state& state, float w, float h, bool to_the_bottom, ogl::color3f base) {
		auto const x = ribbon_w;
		auto const top = top_h + hang;
		sheet(state, x, top, w, h, base, 0.985f);
		fill(state, x + w - 1.f, top, 1.f, h, bronze, 0.80f);
		fill(state, x + w - 4.f, top, 1.f, h, bronze, 0.14f);
		shadow_right(state, x + w, top, h);
		if(!to_the_bottom) {
			fill(state, x, top + h - 1.f, w, 1.f, bronze, 0.80f);
			shadow_below(state, x, top + h, w);
		}
		panels.push_back(rect{ x, top, w, h });
	}

	// A rule with a small lozenge at its centre, fading towards its ends.
	void draw_ornament(sys::state& state, float centre, float y, float half) {
		constexpr int32_t steps = 12;
		auto const reach = half - 9.f;
		for(int32_t i = 0; i < steps; ++i) {
			auto const alpha = 0.95f * (1.f - float(i) / float(steps));
			auto const piece = reach / float(steps);
			fill(state, centre + 9.f + float(i) * piece, y, piece, 1.f, bronze, alpha);
			fill(state, centre - 9.f - float(i + 1) * piece, y, piece, 1.f, bronze, alpha);
		}
		for(int32_t i = -3; i <= 3; ++i)
			fill(state, centre - float(3 - std::abs(i)) - 0.5f, y + float(i), float(2 * (3 - std::abs(i)) + 1), 1.f, gold, 0.9f);
	}

	void draw_icon(sys::state& state, icon which, float centre_x, float centre_y, float size, ogl::color3f c) {
		if(!icons)
			return;
		ogl::render_tinted_subsprite(state, int(which), int(icon::count), std::round(centre_x - size * 0.5f), std::round(centre_y - size * 0.5f), size, size, c.r, c.g, c.b, icons, ui::rotation::upright, false, false);
	}

	void hint(std::string text, hit_target const& t) {
		tip = std::move(text);
		tip_anchor = rect{ t.x, t.y, t.w, t.h };
	}

	void draw_hint(sys::state& state, float w) {
		if(tip.empty())
			return;
		auto const text_w = shaped(state, tip, 11, 0.14f).width;
		auto const tip_w = text_w + 22.f;
		auto const beside = tip_anchor.y >= top_h - 4.f && tip_anchor.x < ribbon_w;
		auto const x = beside ? tip_anchor.x + tip_anchor.w + 12.f : std::round(std::clamp(tip_anchor.x + (tip_anchor.w - tip_w) * 0.5f, edge, w - edge - tip_w));
		auto const y = beside ? std::round(tip_anchor.y + tip_anchor.h * 0.5f - 12.f) : (tip_anchor.y < top_h ? top_h + 8.f : tip_anchor.y - 32.f);
		sheet(state, x, y, tip_w, 24.f, ogl::color3f{ 0.070f, 0.094f, 0.126f });
		frame(state, x, y, tip_w, 24.f, ogl::color3f{ 0.89f, 0.75f, 0.49f }, 0.75f);
		draw_text(state, tip, x + 11.f, y + 12.f, 11, 0.14f, cream);
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
		// Something the player can do is a solid plate, unlike anything that only reports.
		sheet(state, x, y, w, h, hovered ? ogl::color3f{ 0.80f, 0.63f, 0.31f } : ogl::color3f{ 0.40f, 0.315f, 0.165f });
		fill(state, x, y, w, 1.f, gold, hovered ? 1.f : 0.60f);
		fill(state, x, y + h - 1.f, w, 1.f, ink, 0.45f);
		centered(state, label, x, w, y + h * 0.5f, 12, 0.16f, hovered ? ink : bright);
		targets.push_back(t);
	}

	void label_row(sys::state& state, labelled const& row, float x, float right, float center_y) {
		auto const label_w = draw_text(state, row.label, x, center_y, 11, 0.12f, muted);
		auto const value = fit(state, row.value, 14, 0.02f, right - x - label_w - 12.f);
		draw_text(state, value, right, center_y, 14, 0.02f, row.warning ? alarm : bright, true);
		fill(state, x, center_y + row_pitch * 0.5f, right - x, 1.f, bright, 0.07f);
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
		// The file gives positions in its own picture of the map. How that
		// picture maps onto the game's coordinates (origin, direction of the
		// rows) is fitted from the data: each city lies in a province whose
		// position the game knows.
		struct fit {
			double n = 0.0, a = 0.0, b = 0.0, aa = 0.0, ab = 0.0;
			void add(double from, double to) {
				n += 1.0; a += from; b += to; aa += from * from; ab += from * to;
			}
			glm::dvec2 line() const { // slope, intercept
				auto const d = n * aa - a * a;
				if(n < 8.0 || std::abs(d) < 1e-6)
					return glm::dvec2{ 1.0, 0.0 };
				auto const slope = (n * ab - a * b) / d;
				return glm::dvec2{ slope, (b - slope * a) / n };
			}
		};
		fit fit_x;
		fit fit_y;
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
			auto const mid = state.world.province_get_mid_point(province);
			city.position = glm::vec2{ number(field[1]), number(field[2]) };
			fit_x.add(city.position.x, mid.x);
			fit_y.add(city.position.y, mid.y);
			cities.push_back(std::move(city));
		}
		auto const line_x = fit_x.line();
		auto const line_y = fit_y.line();
		for(auto& city : cities)
			city.position = glm::vec2{ float(line_x.x * city.position.x + line_x.y), float(line_y.x * city.position.y + line_y.y) };
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
			labels.push_back(map_label{ n.id, capitals_of(text::get_name_as_string(state, n)), glm::vec2{ float(cx), float(cy) },
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

	void read_areas(sys::state& state) {
		areas_read = true;
		auto root = simple_fs::get_root(state.common_fs);
		auto map_dir = simple_fs::open_directory(root, NATIVE("map"));
		auto file = simple_fs::open_file(map_dir, NATIVE("katechon_provinces.csv"));
		if(!file)
			return;
		auto const content = simple_fs::view_contents(*file);
		auto const body = std::string_view(content.data, content.file_size);
		province_area.assign(size_t(state.world.province_size()) + 2, 0.f);
		size_t start = body.find('\n');
		while(start != std::string_view::npos && start + 1 < body.size()) {
			auto const end = body.find('\n', start + 1);
			auto const line = body.substr(start + 1, end == std::string_view::npos ? std::string_view::npos : end - start - 1);
			start = end;
			auto const cut = line.find(';');
			if(cut == std::string_view::npos)
				continue;
			auto const id = std::atoi(std::string(line.substr(0, cut)).c_str());
			if(id > 0 && size_t(id) < province_area.size())
				province_area[size_t(id)] = float(std::atof(std::string(line.substr(cut + 1)).c_str()));
		}
	}

	float area_of(dcon::province_id p) const {
		auto const id = size_t(province::to_map_id(p));
		return id < province_area.size() ? province_area[id] : 0.f;
	}

	static float ratio(float part, float whole) {
		return whole > 0.f ? part / whole : -1.f;
	}

	template<typename get_function>
	void gather_shares(sys::state& state, population_view& view, get_function const& get) {
		auto const collect = [&](std::vector<share>& out, size_t keep) {
			std::sort(out.begin(), out.end(), [](share const& a, share const& b) { return a.part > b.part; });
			if(out.size() > keep)
				out.resize(keep);
			while(!out.empty() && out.back().part < 0.0005f)
				out.pop_back();
		};
		for(auto type : state.world.in_pop_type) {
			if(auto const v = get(demographics::to_key(state, type)); v > 0.f)
				view.occupations.push_back(share{ text::produce_simple_string(state, type.get_name()), v / view.units });
		}
		collect(view.occupations, 6);
		for(auto culture : state.world.in_culture) {
			if(auto const v = get(demographics::to_key(state, culture)); v > 0.f)
				view.cultures.push_back(share{ text::produce_simple_string(state, culture.get_name()), v / view.units });
		}
		collect(view.cultures, 4);
		for(auto religion : state.world.in_religion) {
			if(auto const v = get(demographics::to_key(state, religion)); v > 0.f)
				view.religions.push_back(share{ text::produce_simple_string(state, religion.get_name()), v / view.units });
		}
		collect(view.religions, 3);
	}

	// A unit of population is a family of four with one worker in it. The
	// employment the simulation projects counts each person under a wage
	// contract as a quarter of a unit, and a household cohort counts its
	// workers as persons.
	inline static constexpr float persons_per_unit = float(economy::households::persons_per_family);

	// The share of the workforce under a wage contract; negative when not kept.
	static float wage_share_of(sys::state& state, float employed, float employable, float units) {
		if(!state.exact_population)
			return employable > 0.f ? std::clamp(employed / employable, 0.f, 1.f) : -1.f;
		return units > 0.f ? std::clamp(employed * persons_per_unit / units, 0.f, 1.f) : -1.f;
	}

	// The share of the workforce that works its own land and workshops in a household cohort.
	static float own_workers(sys::state& state, dcon::province_id p) {
		auto total = 0.f;
		for(auto const role : { economy::households::role::peasant, economy::households::role::landed, economy::households::role::urban })
			total += economy::households::workers(state, economy::households::household_for(state, p, role));
		return total;
	}

	static float basic_needs_of(sys::state& state, dcon::province_id p) {
		auto const strata = state.world.province_get_demographics(p, demographics::poor_total) + state.world.province_get_demographics(p, demographics::middle_total)
			+ state.world.province_get_demographics(p, demographics::rich_total);
		return ratio(state.world.province_get_demographics(p, demographics::poor_life_needs) + state.world.province_get_demographics(p, demographics::middle_life_needs)
			+ state.world.province_get_demographics(p, demographics::rich_life_needs), strata);
	}

	// One figure of a region, or of a whole country when no region is given;
	// nothing when the simulation does not keep it there.
	std::optional<float> metric_value(sys::state& state, dcon::nation_id n, dcon::province_id p, population_metric m) const {
		auto const get = [&](dcon::demographics_key key) {
			return p ? state.world.province_get_demographics(p, key) : state.world.nation_get_demographics(n, key);
		};
		auto const units = get(demographics::total);
		if(units <= 0.f)
			return std::nullopt;
		switch(m) {
		case population_metric::density: {
			auto area = 0.f;
			if(p) {
				area = area_of(p);
			} else {
				for(auto own : state.world.nation_get_province_ownership(n))
					area += area_of(own.get_province());
			}
			if(area <= 0.f)
				return std::nullopt;
			return units * people_per_unit / area;
		}
		case population_metric::work: {
			auto const share = wage_share_of(state, get(demographics::employed), get(demographics::employable), units);
			if(share < 0.f)
				return std::nullopt;
			return share;
		}
		case population_metric::migration: {
			// net arrivals to the thousand inhabitants, a month
			auto moved = 0.f;
			if(p) {
				moved = (state.world.province_get_daily_net_migration(p) + state.world.province_get_daily_net_immigration(p)) * 30.f;
			} else {
				for(auto own : state.world.nation_get_province_ownership(n))
					moved += own.get_province().get_daily_net_immigration() * 30.f;
			}
			return moved / units * 1000.f;
		}
		case population_metric::needs: {
			auto const strata = get(demographics::poor_total) + get(demographics::middle_total) + get(demographics::rich_total);
			if(strata <= 0.f)
				return std::nullopt;
			return (get(demographics::poor_life_needs) + get(demographics::middle_life_needs) + get(demographics::rich_life_needs)) / strata;
		}
		case population_metric::literacy:
			return get(demographics::literacy) / units;
		case population_metric::count:
			break;
		}
		return std::nullopt;
	}

	// Where a value falls on the colours of its map, 0 to 1 (-1 to 1 for movement).
	static float metric_position(population_metric m, float value) {
		switch(m) {
		case population_metric::density:
			// 0.1 to 3000 people per square kilometre, by powers of ten
			return std::clamp((std::log10(std::clamp(value, 0.1f, 3000.f)) + 1.f) / 4.477f, 0.f, 1.f);
		case population_metric::work:
			return std::clamp(value / 0.5f, 0.f, 1.f);
		case population_metric::migration:
			return std::clamp(value / 5.f, -1.f, 1.f);
		case population_metric::needs:
		case population_metric::literacy:
			return std::clamp(value, 0.f, 1.f);
		case population_metric::count:
			break;
		}
		return 0.f;
	}

	std::string metric_text(sys::state& state, population_metric m, float value) const {
		switch(m) {
		case population_metric::density:
			return filled(localized(state, "katechon_ppl_per_km2", "%1 /km²"), text::format_float(value, value < 10.f ? 1 : 0));
		case population_metric::work:
		case population_metric::needs:
		case population_metric::literacy:
			return text::format_percentage(value, value > 0.f && value < 0.1f ? 1 : 0);
		case population_metric::migration:
			return (value > 0.f ? "+" : (value < 0.f ? "-" : "")) + text::format_float(std::abs(value), 1) + "‰";
		case population_metric::count:
			break;
		}
		return std::string();
	}

	std::string metric_text(sys::state& state, population_metric m, std::optional<float> value) const {
		return value ? metric_text(state, m, *value) : localized(state, "katechon_ppl_no_data", "No data");
	}

	static std::string metric_name(sys::state& state, population_metric m) {
		switch(m) {
		case population_metric::density: return localized(state, "katechon_ppl_m_density", "Density");
		case population_metric::work: return localized(state, "katechon_ppl_m_work", "Wage work");
		case population_metric::migration: return localized(state, "katechon_ppl_m_migration", "Migration");
		case population_metric::needs: return localized(state, "katechon_ppl_m_needs", "Basic needs met");
		case population_metric::literacy: return localized(state, "katechon_ppl_m_literacy", "Literacy");
		case population_metric::count: break;
		}
		return std::string();
	}

	// What a figure means, for the hint over it.
	static std::string metric_about(sys::state& state, population_metric m) {
		switch(m) {
		case population_metric::density: return localized(state, "katechon_ppl_a_density", "People to the square kilometre of land. The map: brighter is more crowded.");
		case population_metric::work: return localized(state, "katechon_ppl_a_work", "Workers under a wage contract, out of all workers. The rest work their own land and workshops or have no work.");
		case population_metric::migration: return localized(state, "katechon_ppl_a_migration", "Net arrivals a month to every thousand inhabitants. The map: blue gains people, red loses them.");
		case population_metric::needs: return localized(state, "katechon_ppl_a_needs", "How much of what people need to live they obtain. Below half, people go hungry and the population shrinks.");
		case population_metric::literacy: return localized(state, "katechon_ppl_a_literacy", "Those who can read and write. It lets people take skilled work and fills the schools of the next generation.");
		case population_metric::count: break;
		}
		return std::string();
	}

	// The two ends of the map's colours, in words.
	static std::pair<std::string, std::string> metric_ends(sys::state& state, population_metric m) {
		switch(m) {
		case population_metric::density: return { localized(state, "katechon_ppl_e_empty", "0.1 /km², empty"), localized(state, "katechon_ppl_e_crowded", "crowded, 1,000+") };
		case population_metric::work: return { "0%", localized(state, "katechon_ppl_e_half", "50% and more") };
		case population_metric::migration: return { localized(state, "katechon_ppl_e_leaving", "-5‰, leaving"), localized(state, "katechon_ppl_e_arriving", "arriving, +5‰") };
		case population_metric::needs: return { localized(state, "katechon_ppl_e_hungry", "0%, hungry"), "100%" };
		case population_metric::literacy: return { "0%", "100%" };
		case population_metric::count: break;
		}
		return {};
	}

	// What the colour of a region on the map says, in a sentence.
	static std::string metric_reading(sys::state& state, population_metric m) {
		switch(m) {
		case population_metric::density: return localized(state, "katechon_ppl_r_density", "The brighter a region, the more people live on each square kilometre of it.");
		case population_metric::work: return localized(state, "katechon_ppl_r_work", "The bluer a region, the more of its workers are hired for wages. Grey regions live off their own land.");
		case population_metric::migration: return localized(state, "katechon_ppl_r_migration", "Blue regions take people in, red ones lose them; grey ones neither, or have not moved yet this month.");
		case population_metric::needs: return localized(state, "katechon_ppl_r_needs", "Red regions go short of food and shelter; green ones have what they need.");
		case population_metric::literacy: return localized(state, "katechon_ppl_r_literacy", "The lighter a region, the more of its people can read and write.");
		case population_metric::count: break;
		}
		return std::string();
	}

	// Whether a value of the figure is a concern (-1), a strength (1), or neither.
	static int32_t metric_tone(population_metric m, float value) {
		switch(m) {
		case population_metric::migration: return value > 0.05f ? 1 : (value < -0.05f ? -1 : 0);
		case population_metric::needs: return value < 0.5f ? -1 : (value >= 0.9f ? 1 : 0);
		case population_metric::literacy: return value < 0.3f ? -1 : 0;
		default: return 0;
		}
	}

	void gather_population(sys::state& state) {
		if(!areas_read)
			read_areas(state);
		population = population_view{};
		auto& view = population;
		auto const player = state.local_player_nation;
		// The region studied must still be land with an owner; the country one that holds land.
		if(atlas_province && !(atlas_province.index() < state.province_definitions.first_sea_province.index() && state.world.province_get_nation_from_province_ownership(atlas_province)))
			atlas_province = dcon::province_id{};
		if(atlas_province)
			atlas_nation = state.world.province_get_nation_from_province_ownership(atlas_province);
		if(atlas_nation == player || (atlas_nation && state.world.nation_get_owned_province_count(atlas_nation) == 0))
			atlas_nation = dcon::nation_id{};
		auto const prov = atlas_province;
		view.of_province = bool(prov);
		view.province = prov;
		view.nation = prov ? state.world.province_get_nation_from_province_ownership(prov) : (atlas_nation ? atlas_nation : player);
		if(!view.nation)
			return;

		auto const get = [&](dcon::demographics_key key) {
			return prov ? state.world.province_get_demographics(prov, key) : state.world.nation_get_demographics(view.nation, key);
		};
		view.units = get(demographics::total);
		if(prov) {
			view.title = text::get_name_as_string(state, prov);
			view.subtitle = text::get_name_as_string(state, dcon::fatten(state.world, view.nation));
			if(auto const definition = state.world.province_get_state_from_abstract_state_membership(prov); definition)
				view.subtitle = text::get_name_as_string(state, definition) + "  ·  " + view.subtitle;
			view.part_of_country = ratio(view.units, state.world.nation_get_demographics(view.nation, demographics::total));
		} else {
			view.title = text::get_name_as_string(state, dcon::fatten(state.world, view.nation));
			view.subtitle = get_status_text(state, view.nation);
		}

		// Every region of the country, for the table, the movements and the map.
		for(auto own : state.world.nation_get_province_ownership(view.nation)) {
			auto const p = own.get_province();
			population_view::region r;
			r.id = p.id;
			r.units = p.get_demographics(demographics::total);
			if(r.units <= 0.f)
				continue;
			r.growth = float(demographics::get_monthly_pop_increase(state, p.id));
			r.needs = basic_needs_of(state, p.id);
			r.literacy = p.get_demographics(demographics::literacy) / r.units;
			r.moved = (p.get_daily_net_migration() + p.get_daily_net_immigration()) * 30.f;
			if(!prov) {
				view.area += area_of(p.id);
				view.relocations += std::max(0.f, p.get_daily_net_migration() * 30.f);
			}
			view.regions.push_back(std::move(r));
		}
		std::sort(view.regions.begin(), view.regions.end(), [](auto const& a, auto const& b) { return a.units > b.units; });
		for(auto& r : view.regions)
			r.name = text::get_name_as_string(state, r.id);
		for(auto const& r : view.regions) {
			if(r.moved > 0.f)
				view.gaining.push_back(r);
			else if(r.moved < 0.f)
				view.losing.push_back(r);
		}
		std::sort(view.gaining.begin(), view.gaining.end(), [](auto const& a, auto const& b) { return a.moved > b.moved; });
		std::sort(view.losing.begin(), view.losing.end(), [](auto const& a, auto const& b) { return a.moved < b.moved; });
		if(view.gaining.size() > 3)
			view.gaining.resize(3);
		if(view.losing.size() > 3)
			view.losing.resize(3);
		gather_standouts(state);

		if(view.units <= 0.f)
			return;

		// What makes the month's change, pop by pop, and what life is like.
		auto weight = 0.f;
		auto job = 0.f, education = 0.f, towns = 0.f, housing = 0.f, development = 0.f;
		auto own = 0.f;
		auto const add_region = [&](dcon::province_id p) {
			if(prov)
				view.area = area_of(p);
			if(state.exact_population)
				own += own_workers(state, p);
			if(prov) {
				view.internal_month += state.world.province_get_daily_net_migration(p) * 30.f;
				view.external_month += state.world.province_get_daily_net_immigration(p) * 30.f;
			} else {
				view.external_month += state.world.province_get_daily_net_immigration(p) * 30.f;
			}
			for(auto location : state.world.province_get_pop_location(p)) {
				auto const pop = location.get_pop();
				auto const size = pop.get_size();
				if(size <= 0.f)
					continue;
				auto const development_here = economy::human_development::evaluate_pop(state, pop);
				auto const legacy = demographics::get_pop_growth_modifiers(state, pop);
				auto const starving = demographics::get_net_pop_starvation_penalty(state, pop, legacy);
				auto const account = economy::human_development::calculate_account(size, legacy, starving, development_here);
				view.natural += account.baseline_natural_growth;
				view.starvation += account.starvation_loss;
				view.overcrowding += account.housing_loss;
				view.transition += account.transition_reduction;
				view.drivers_known = true;
				if(development_here.enabled) {
					weight += size;
					job += size * development_here.factors.job_access;
					education += size * development_here.factors.education_access;
					towns += size * development_here.factors.urbanization;
					housing += size * development_here.factors.housing_access;
					development += size * development_here.human_development_index;
				}
			}
		};
		if(prov) {
			add_region(prov);
		} else {
			for(auto province_own : state.world.nation_get_province_ownership(view.nation))
				add_region(province_own.get_province());
			view.army = std::max(0.f, military::estimated_monthly_army_pop_attrition_loss(state, view.nation));
			// Across the border, the month's directed estimate is what the country's total follows.
			std::vector<float> flows;
			demographics::estimate_directed_immigration(state, view.nation, flows);
			auto net = 0.f;
			for(uint32_t i = 0; i < flows.size(); ++i) {
				if(!std::isfinite(flows[i]) || flows[i] == 0.f)
					continue;
				net += flows[i];
				view.abroad.push_back(population_view::partner{ dcon::nation_id{ dcon::nation_id::value_base_t(i) }, flows[i] });
			}
			std::sort(view.abroad.begin(), view.abroad.end(), [](auto const& a, auto const& b) { return std::abs(a.amount) > std::abs(b.amount); });
			if(view.abroad.size() > 4)
				view.abroad.resize(4);
			view.external_month = net;
			view.external_estimated = true;
		}
		if(weight > 0.f) {
			view.job_access = job / weight;
			view.education_access = education / weight;
			view.urbanization = towns / weight;
			view.housing_access = housing / weight;
			view.development = development / weight;
		}
		auto const natural_net = view.natural - view.starvation - view.overcrowding - view.transition;
		view.change_month = natural_net + view.internal_month + view.external_month - view.army;

		view.employed = get(demographics::employed);
		view.wage_share = wage_share_of(state, view.employed, get(demographics::employable), view.units);
		// a country with no household cohort at all does not keep this record
		view.own_share = state.exact_population && own > 0.f ? std::clamp(own / view.units, 0.f, 1.f) : -1.f;
		view.literacy = get(demographics::literacy) / view.units;
		view.consciousness = get(demographics::consciousness) / view.units;
		view.militancy = get(demographics::militancy) / view.units;
		view.poor = get(demographics::poor_total);
		view.middle = get(demographics::middle_total);
		view.rich = get(demographics::rich_total);
		auto const strata = view.poor + view.middle + view.rich;
		view.life = ratio(get(demographics::poor_life_needs) + get(demographics::middle_life_needs) + get(demographics::rich_life_needs), strata);
		view.everyday = ratio(get(demographics::poor_everyday_needs) + get(demographics::middle_everyday_needs) + get(demographics::rich_everyday_needs), strata);
		view.luxury = ratio(get(demographics::poor_luxury_needs) + get(demographics::middle_luxury_needs) + get(demographics::rich_luxury_needs), strata);
		view.strata_needs[0] = { ratio(get(demographics::poor_life_needs), view.poor), ratio(get(demographics::poor_everyday_needs), view.poor), ratio(get(demographics::poor_luxury_needs), view.poor) };
		view.strata_needs[1] = { ratio(get(demographics::middle_life_needs), view.middle), ratio(get(demographics::middle_everyday_needs), view.middle), ratio(get(demographics::middle_luxury_needs), view.middle) };
		view.strata_needs[2] = { ratio(get(demographics::rich_life_needs), view.rich), ratio(get(demographics::rich_everyday_needs), view.rich), ratio(get(demographics::rich_luxury_needs), view.rich) };
		view.accepted = get(demographics::primary_or_accepted) / view.units;
		gather_shares(state, view, get);

		// The game keeps a record of the player's own total, a day at a time.
		if(!prov && view.nation == player) {
			if(auto* cache = state.find_player_data_cache(view.nation); cache) {
				auto const today = int32_t(state.ui_date.value);
				for(int32_t back = 31; back >= 0; --back) {
					if(today - back < 1)
						continue;
					if(auto const v = cache->population_record[size_t(today - back) % 32]; v > 0.f)
						view.history.push_back(v);
				}
			}
		}
	}

	// The regions of the country highest and lowest on the figure the map shows,
	// and where the region studied stands among them.
	void gather_standouts(sys::state& state) {
		auto& view = population;
		view.highest.clear();
		view.lowest.clear();
		view.rank = 0;
		view.ranked = 0;
		std::vector<std::pair<dcon::province_id, float>> values;
		for(auto const& r : view.regions) {
			if(auto const value = metric_value(state, view.nation, r.id, metric); value)
				values.emplace_back(r.id, *value);
		}
		std::sort(values.begin(), values.end(), [](auto const& a, auto const& b) { return a.second > b.second; });
		view.ranked = int32_t(values.size());
		for(size_t i = 0; i < values.size(); ++i) {
			if(values[i].first == view.province)
				view.rank = int32_t(i) + 1;
		}
		if(values.size() < 2)
			return;
		auto const shown = std::min<size_t>(3, values.size() / 2);
		for(size_t i = 0; i < shown; ++i) {
			view.highest.push_back(values[i]);
			view.lowest.push_back(values[values.size() - 1 - i]);
		}
	}

	// ---- the map layers of the population pages ----
	struct ramp_stop {
		float at;
		ogl::color3f colour;
	};
	template<size_t count>
	static ogl::color3f along(std::array<ramp_stop, count> const& stops, float t) {
		t = std::clamp(t, stops.front().at, stops.back().at);
		for(size_t i = 1; i < count; ++i) {
			if(t <= stops[i].at)
				return mix(stops[i - 1].colour, stops[i].colour, (t - stops[i - 1].at) / (stops[i].at - stops[i - 1].at));
		}
		return stops.back().colour;
	}
	// On a dark map the empty land recedes and the peopled land comes forward.
	inline static constexpr std::array<ramp_stop, 5> density_ramp{ ramp_stop{ 0.f, { 0.29f, 0.32f, 0.35f } }, ramp_stop{ 0.30f, { 0.45f, 0.44f, 0.40f } },
		ramp_stop{ 0.55f, { 0.70f, 0.57f, 0.38f } }, ramp_stop{ 0.78f, { 0.86f, 0.54f, 0.30f } }, ramp_stop{ 1.f, { 0.96f, 0.80f, 0.54f } } };
	// Wage work is no good or evil in itself: it goes from the grey of the
	// countryside to the blue of the towns, never to red.
	inline static constexpr std::array<ramp_stop, 3> wage_ramp{ ramp_stop{ 0.f, { 0.34f, 0.35f, 0.37f } }, ramp_stop{ 0.45f, { 0.36f, 0.50f, 0.62f } },
		ramp_stop{ 1.f, { 0.62f, 0.80f, 0.92f } } };
	inline static constexpr std::array<ramp_stop, 3> movement_ramp{ ramp_stop{ -1.f, { 0.74f, 0.30f, 0.26f } }, ramp_stop{ 0.f, { 0.50f, 0.51f, 0.52f } },
		ramp_stop{ 1.f, { 0.28f, 0.56f, 0.74f } } };
	inline static constexpr std::array<ramp_stop, 4> needs_ramp{ ramp_stop{ 0.f, { 0.66f, 0.24f, 0.22f } }, ramp_stop{ 0.5f, { 0.84f, 0.62f, 0.36f } },
		ramp_stop{ 0.8f, { 0.62f, 0.68f, 0.44f } }, ramp_stop{ 1.f, { 0.38f, 0.66f, 0.48f } } };
	inline static constexpr std::array<ramp_stop, 3> literacy_ramp{ ramp_stop{ 0.f, { 0.36f, 0.30f, 0.40f } }, ramp_stop{ 0.6f, { 0.34f, 0.56f, 0.62f } },
		ramp_stop{ 1.f, { 0.80f, 0.88f, 0.72f } } };
	// Land the figure says nothing about: darker than the darkest of any ramp.
	inline static constexpr ogl::color3f no_reading{ 0.17f, 0.18f, 0.20f };

	static ogl::color3f metric_colour(population_metric m, float t) {
		switch(m) {
		case population_metric::density: return along(density_ramp, t);
		case population_metric::work: return along(wage_ramp, t);
		case population_metric::migration: return along(movement_ramp, t);
		case population_metric::needs: return along(needs_ramp, t);
		case population_metric::literacy: return along(literacy_ramp, t);
		case population_metric::count: break;
		}
		return no_reading;
	}

	void paint_layer(sys::state& state) {
		if(!areas_read)
			read_areas(state);
		auto const province_size = uint32_t(state.world.province_size()) + 1;
		auto const texture_size = province_size + 256 - province_size % 256;
		std::vector<uint32_t> colours(size_t(texture_size) * 2, 0);
		province::for_each_land_province(state, [&](dcon::province_id p) {
			auto const value = metric_value(state, dcon::nation_id{}, p, metric);
			auto const c = value ? metric_colour(metric, metric_position(metric, *value)) : no_reading;
			auto const packed = sys::pack_color(c.r, c.g, c.b);
			auto const i = province::to_map_id(p);
			colours[i] = packed;
			colours[i + texture_size] = packed;
		});
		state.map_state.set_province_color(colours, map_mode::mode::handled_from_outside);
		repaint_in = 20;
	}

	// The population pages open on a country (the player's own unless a region
	// is named) or on one of its regions; as the overview, or beside the map.
	void open_population(sys::state& state, dcon::province_id region, bool overview) {
		close_military();
		close_diplomacy(state);
		close_trade(state);
		if(country_window_open(state))
			state.ui_state.topbar_subwindow->set_visible(state, false);
		population_open = true;
		population_overview = overview;
		overview_scroll = 0.f;
		auto const land = region && region.index() < state.province_definitions.first_sea_province.index();
		atlas_province = land ? region : dcon::province_id{};
		atlas_nation = land ? state.world.province_get_nation_from_province_ownership(region) : dcon::nation_id{};
		atlas_seen_province = state.map_state.selected_province;
		population_layer = true;
		// Any data mode makes the map shader treat the colours as data.
		map_mode::set_map_mode(state, map_mode::mode::population);
		gather_population(state);
		paint_layer(state);
	}

	void close_population(sys::state& state) {
		if(!population_open)
			return;
		population_open = false;
		population_overview = false;
		if(population_layer) {
			population_layer = false;
			map_mode::set_map_mode(state, map_mode::mode::political);
		}
	}

	// Leaves the overview for the map, painted with a figure, on the region studied if there is one.
	void population_to_map(sys::state& state, population_metric chosen) {
		population_overview = false;
		metric = chosen;
		population_layer = true;
		map_mode::set_map_mode(state, map_mode::mode::population);
		if(atlas_province) {
			state.set_selected_province(atlas_province);
			state.map_state.center_map_on_province(state, atlas_province);
		} else if(auto const capital = state.world.nation_get_capital(population.nation); capital) {
			state.map_state.center_map_on_province(state, capital);
		}
		atlas_seen_province = state.map_state.selected_province;
		gather_standouts(state);
		paint_layer(state);
	}

	void refresh(sys::state& state) {
		if(military_open)
			gather_forces(state);
		if(diplomacy_open) {
			gather_diplomacy(state);
			if(diplomacy_layer)
				paint_relations(state);
		} else {
			// requests, the journal and the month of relations are kept whether the sheet is open or not
			record_relations(state);
			gather_requests(state);
			settle_journal(state);
		}
		if(population_open) {
			gather_population(state);
			if(population_layer)
				paint_layer(state);
		}
		trade_tick(state);
		snap = snapshot{};
		snap.ready = true;
		people_per_unit = std::max(1.f, float(std::atof(localized(state, "katechon_people_per_unit", "1").c_str())));
		survey_map(state);
		snap.date = text::date_to_string(state, state.current_date);

		auto const player = state.local_player_nation;
		snap.player = player;
		if(player) {
			snap.player_name = text::get_name_as_string(state, dcon::fatten(state.world, player));
			snap.player_status = capitals_of(get_status_text(state, player)) + "  ·  " + rank_text(int32_t(state.world.nation_get_rank(player)));
			// Treasury: against where it stood a month ago, by the record the game keeps.
			{
				auto const treasury = nations::get_treasury(state, player);
				snapshot::figure money{ icon::budget, localized(state, "katechon_hud_treasury", "TREASURY"), text::format_money(treasury), localized(state, "katechon_hud_treasury", "TREASURY") };
				auto const today = int32_t(state.current_date.value);
				auto const back = std::min(30, today - 1);
				if(auto* cache = state.find_player_data_cache(player); cache && back >= 2) {
					auto const then = cache->treasury_record[size_t(today - back) % 32];
					if(then != 0.f || treasury == 0.f) {
						auto const change = treasury - then;
						money.way = change > 0.f ? 1 : (change < 0.f ? -1 : 0);
						money.note = (change > 0.f ? "+" : (change < 0.f ? "-" : "")) + text::format_money(std::abs(change)) + " " + filled(localized(state, "katechon_hud_in_days", "in $1 days"), std::to_string(back));
					}
				}
				snap.wells.push_back(std::move(money));
			}
			// People: by the growth the simulation works out for the month.
			{
				auto const total = state.world.nation_get_demographics(player, demographics::total);
				snapshot::figure folk{ icon::society, localized(state, "katechon_hud_people", "PEOPLE"), people(total), localized(state, "katechon_hud_people", "PEOPLE") };
				if(total > 0.f) {
					auto const change = float(demographics::get_monthly_pop_increase(state, player)) / total;
					folk.way = change > 0.f ? 1 : (change < 0.f ? -1 : 0);
					folk.note = signed_percent(change) + " " + localized(state, "katechon_pop_per_month", "per month");
				}
				snap.wells.push_back(std::move(folk));
			}
			// Knowledge: what is being learned a day, and how many can read.
			snap.wells.push_back(snapshot::figure{ icon::technology, localized(state, "katechon_hud_research", "RESEARCH"), text::format_float(nations::daily_research_points(state, player), 1),
				localized(state, "katechon_hud_literacy", "LITERACY") + " " + text::format_percentage(nations::get_avg_non_colonial_literacy(state, player), 1) });
			snap.standing = as_a_name(localized(state, "katechon_hud_prestige", "PRESTIGE")) + " " + std::to_string(int32_t(nations::prestige_score(state, player)))
				+ "  ·  " + as_a_name(localized(state, "katechon_hud_industry", "INDUSTRY")) + " " + std::to_string(int32_t(state.world.nation_get_industrial_score(player)))
				+ "  ·  " + as_a_name(localized(state, "katechon_hud_military", "MILITARY")) + " " + std::to_string(int32_t(state.world.nation_get_military_score(player)));
			if(auto const points = nations::diplomatic_points(state, player); points >= 1.f)
				snap.alerts.push_back(labelled{ localized(state, "katechon_hud_influence", "DIPLOMATS"), text::format_float(points, 1) });
			if(auto const infamy = state.world.nation_get_infamy(player); infamy >= 0.5f * state.defines.badboy_limit)
				snap.alerts.push_back(labelled{ localized(state, "katechon_hud_infamy", "INFAMY"), text::format_float(infamy, 1), infamy >= state.defines.badboy_limit });
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
			// A country window and the population atlas do not share the screen.
			if(std::find(tab_buttons.begin(), tab_buttons.end(), t.button) != tab_buttons.end()) {
				close_population(state);
				close_military();
				close_diplomacy(state);
				close_trade(state);
			}
			press(state, t.button);
			return;
		case action::toggle_population:
			// From the state's own navigation the overview opens, on one's own country.
			close_military();
			if(population_open)
				close_population(state);
			else
				open_population(state, dcon::province_id{}, true);
			break;
		case action::population_inspect:
			// From a place on the map the inspector opens, on that place.
			if(auto const region = dcon::province_id{ dcon::province_id::value_base_t(t.value) }; region)
				open_population(state, region, false);
			break;
		case action::toggle_military:
			if(military_open)
				close_military();
			else
				open_military(state);
			break;
		case action::toggle_diplomacy:
			if(diplomacy_open)
				close_diplomacy(state);
			else
				open_diplomacy_panel(state, diplomacy_focus ? diplomacy_focus : state.local_player_nation);
			break;
		case action::dip_deal_toggle:
			// the review closes; nothing is sent
			deal_open = false;
			review_deed = deed::none;
			deal_gives = 0;
			deal_asks = 0;
			break;
		case action::dip_tab:
			dip_tab = t.value;
			break;
		case action::dip_review:
			open_review(state, deed(uint8_t(t.value)), t.formation);
			break;
		case action::dip_why:
			deal_why = !deal_why;
			break;
		case action::dip_item:
			if(t.value & 0x10000)
				deal_gives ^= uint16_t(t.value & 0xFFFF);
			else
				deal_asks ^= uint16_t(t.value & 0xFFFF);
			break;
		case action::dip_send:
			send_deal(state);
			break;
		case action::dip_trade:
			trade_for(state, uint16_t(t.value), uint16_t(t.formation));
			break;
		case action::dip_answer:
			answer_request(state, t.value, t.formation != 0);
			break;
		case action::dip_radial:
			radial_open = false;
			carry_out(state, deed(uint8_t(t.value)), t.formation, radial_nation);
			break;
		case action::dip_radial_close:
			radial_open = false;
			return;
		case action::diplomacy_target:
			if(auto const n = value_nation(t.value); n) {
				open_diplomacy_panel(state, n);
				if(auto const capital = state.world.nation_get_capital(n); capital)
					state.map_state.center_map_on_province(state, capital);
			}
			break;
		case action::diplomacy_home:
			open_diplomacy_panel(state, state.local_player_nation);
			break;
		case action::diplomacy_deed:
			carry_out(state, deed(uint8_t(t.value)), t.formation);
			break;
		case action::toggle_trade:
			if(trade_open)
				close_trade(state);
			else
				open_trade(state);
			break;
		case action::trade_span:
			trade_span = t.value;
			break;
		case action::trade_focus:
			trade_focus = t.value;
			trade_analysis = false; // what was chosen is shown on the map
			break;
		case action::trade_analysis:
			trade_analysis = !trade_analysis;
			break;
		case action::trade_details:
			open_legacy_trade(state);
			break;
		case action::pick_formation:
			choose(state, t.formation, shift_held);
			// chosen from the list, it is also shown on the map
			if(auto const u = unit_of(t.formation); t.value == 1 && u && u->at)
				state.map_state.center_map_on_province(state, u->at);
			break;
		case action::military_back:
			if(t.formation)
				choose(state, t.formation, false);
			else
				chosen_formations.clear();
			forces_stale = true;
			break;
		case action::military_order:
			order(state, command::formation_order_kind(uint8_t(t.value)), dcon::province_id{});
			break;
		case action::military_march:
			awaiting_destination = !awaiting_destination;
			break;
		case action::military_more:
			military_more = !military_more;
			break;
		case action::military_mobilize:
			command::toggle_mobilization(state, state.local_player_nation);
			forces_stale = true;
			break;
		case action::population_metric:
			if(population_overview) {
				population_to_map(state, population_metric(uint8_t(t.value)));
			} else {
				metric = population_metric(uint8_t(t.value));
				population_layer = true;
				map_mode::set_map_mode(state, map_mode::mode::population);
				gather_standouts(state);
				paint_layer(state);
			}
			break;
		case action::population_overview:
			if(t.value != 0) {
				population_overview = true;
				overview_scroll = 0.f;
			} else {
				population_to_map(state, metric);
			}
			break;
		case action::population_scroll_to:
			if(size_t(t.value) < overview_anchor.size())
				overview_scroll = overview_anchor[size_t(t.value)];
			break;
		case action::population_whole_country:
			atlas_province = dcon::province_id{};
			overview_scroll = 0.f;
			gather_population(state);
			break;
		case action::population_home:
			atlas_nation = dcon::nation_id{};
			atlas_province = dcon::province_id{};
			overview_scroll = 0.f;
			gather_population(state);
			break;
		case action::population_region:
			if(auto const region = dcon::province_id{ dcon::province_id::value_base_t(t.value) }; region) {
				atlas_province = region;
				overview_scroll = 0.f;
				if(!population_overview) {
					// beside the map, the region is also chosen on it
					state.set_selected_province(region);
					state.map_state.center_map_on_province(state, region);
					atlas_seen_province = region;
				}
				gather_population(state);
			}
			break;
		case action::toggle_layers:
			layers_open = !layers_open;
			break;
		case action::set_layer:
			population_layer = false; // the player chose a layer; the inspector keeps its figures
			if(diplomacy_layer) {
				map::katechon_trade::lanes.clear();
				map::katechon_trade::dirty.store(true, std::memory_order::release);
			}
			diplomacy_layer = false;
			trade_layer = false;
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
				open_diplomacy_panel(state, snap.owner);
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
		named.clear();
		// This is the first thing drawn after the map. Text relies on the state
		// that a plain rectangle sets up, so one is issued before any lettering.
		ogl::render_alpha_colored_rect(state, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
		auto const zoom = state.map_state.zoom;
		constexpr float tracking = 0.28f;
		constexpr int32_t reference = 20;

		for(auto const& label : labels) {
			// with diplomacy open, only the countries in question are named
			if(diplomacy_open && diplomacy_layer && label.nation && size_t(label.nation.index()) < diplomacy_related.size() && !diplomacy_related[size_t(label.nation.index())])
				continue;
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
				named.push_back(city.name);
			}
		}

		if(zoom >= 9.f) {
			for(auto const& region : regions) {
				glm::vec2 at;
				if(!project(state, region.position, at) || at.x < 0.f || at.x > w || at.y < top_h || at.y > h)
					continue;
				if(std::find(named.begin(), named.end(), std::string_view(region.name)) != named.end())
					continue; // its city already carries the name
				auto const text_w = shaped(state, region.name, 11, 0.06f).width;
				if(!claim(rect{ at.x - text_w * 0.5f - 4.f, at.y - 8.f, text_w + 8.f, 16.f }))
					continue;
				map_text(state, region.name, at.x - text_w * 0.5f, at.y, 11, 0.06f, mix(ivory, muted, 0.35f));
			}
		}
	}

	// A round button of the clock.
	void mini_button(sys::state& state, float x, float mid, icon image, hit_target t, std::string label, bool lit, bool hovering, float mx, float my) {
		t.x = x;
		t.y = mid - 13.f;
		t.w = 26.f;
		t.h = 26.f;
		auto const hovered = hovering && over(t, mx, my);
		if(shell_rounds)
			ogl::render_tinted_subsprite(state, lit ? 4 : (hovered ? 3 : 2), 5, x - 11.f, mid - 24.f, 48.f, 48.f, 1.f, 1.f, 1.f, shell_rounds, ui::rotation::upright, false, false);
		draw_icon(state, image, x + 13.f, mid, 11.f, hovered || lit ? ogl::color3f{ 1.f, 0.95f, 0.80f } : cream);
		targets.push_back(t);
		if(hovered)
			hint(std::move(label), t);
	}

	void art(sys::state& state, GLuint texture, float x, float y, float w, float h) {
		if(texture)
			ogl::render_textured_rect(state, ogl::color_modification::none, x, y, w, h, texture, ui::rotation::upright, false, false);
	}

	// "PRODUCTION" as "Production".
	static std::string as_a_name(std::string value) {
		auto first = true;
		for(size_t i = 0; i < value.size(); ++i) {
			auto const c = uint8_t(value[i]);
			if(c < 128) {
				value[i] = char(first ? std::toupper(c) : std::tolower(c));
				first = c == ' ';
			} else if(c == 0xD0 && i + 1 < value.size()) {
				// Cyrillic capitals: U+0410..U+041F stay in this lead byte, U+0420..U+042F move to the next
				auto const next = uint8_t(value[i + 1]);
				if(!first && next >= 0x90 && next <= 0x9F) {
					value[i + 1] = char(next + 0x20);
				} else if(!first && next >= 0xA0 && next <= 0xAF) {
					value[i] = char(0xD1);
					value[i + 1] = char(next - 0x20);
				}
				first = false;
				++i;
			} else {
				first = false;
			}
		}
		return value;
	}

	// One of the pieces set on the bar: assets/katechon_shell_{raised,well,time}.png,
	// three cells each (left end, middle, right end), forty points high.
	void slab(sys::state& state, GLuint piece, float x, float w) {
		if(!piece)
			return;
		ogl::render_tinted_subsprite(state, 0, 3, x - 8.f, -3.f, 28.f, 56.f, 1.f, 1.f, 1.f, piece, ui::rotation::upright, false, false);
		ogl::render_tinted_subsprite(state, 1, 3, x + 20.f, -3.f, w - 40.f, 56.f, 1.f, 1.f, 1.f, piece, ui::rotation::upright, false, false);
		ogl::render_tinted_subsprite(state, 2, 3, x + w - 20.f, -3.f, 28.f, 56.f, 1.f, 1.f, 1.f, piece, ui::rotation::upright, false, false);
	}

	// Type cut into the material: the letter, and its own shadow just under it.
	float cut(sys::state& state, std::string_view content, float x, float center_y, int32_t size, float tracking, ogl::color3f colour, bool align_right = false) {
		draw_text(state, content, x + 0.7f, center_y + 0.9f, size, tracking, ink, align_right);
		return draw_text(state, content, x, center_y, size, tracking, colour, align_right);
	}

	// A small arrowhead, up or down.
	void arrow(sys::state& state, float x, float center_y, bool up, ogl::color3f colour) {
		for(int32_t i = 0; i < 4; ++i) {
			auto const row = up ? float(i) : float(3 - i);
			fill(state, x + 3.f - float(i), center_y - 2.f + row, float(2 * i + 1), 1.f, colour, 1.f);
		}
	}

	// The bar of the state runs the width of the window in one piece: the
	// walnut of the ribbon over an aged-gold moulding (assets/katechon_shell_*.png,
	// drawn by tools/katechon_ui/build_bar.py). The state stands proud of it on
	// the left; the few figures that matter are let into it, each with which
	// way it is going; the eagle hangs from its middle; the clock is let into
	// its right end. The country's windows are on the ribbon down the left edge.
	void draw_top_bar(sys::state& state, float w, bool hovering, float mx, float my) {
		constexpr float row = 19.f;
		constexpr float under = 36.f;
		constexpr float mid = 25.f;
		auto const centre = std::round(w * 0.5f);
		auto const seconds = std::chrono::duration<float>(std::chrono::steady_clock::now().time_since_epoch()).count();
		ogl::render_alpha_colored_rect(state, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f); // sets up the state the artwork and the type rely on
		art(state, shell_bar, 0.f, 0.f, w, 66.f);
		if(!shell_bar)
			sheet(state, 0.f, 0.f, w, top_h, ogl::color3f{ 0.22f, 0.16f, 0.11f });
		panels.push_back(rect{ 0.f, 0.f, w, top_h });

		// ---- the clock: the date, the year running under it, the speed ----
		auto const is_paused = paused(state);
		auto const speed = state.actual_game_speed.load(std::memory_order::acquire) > 0 ? state.actual_game_speed.load(std::memory_order::acquire) : state.ui_state.held_game_speed;
		auto const date_w = shaped(state, snap.date, 16, 0.04f).width;
		auto const clock_w = std::round(12.f + 26.f + 12.f + date_w + 16.f + 26.f + 7.f + 42.f + 7.f + 26.f + 12.f);
		auto const clock_x = w - 10.f - clock_w;
		slab(state, shell_time, clock_x, clock_w);
		if(is_paused)
			fill(state, clock_x + 2.f, 7.f, clock_w - 4.f, 36.f, ink, 0.22f); // time stands still: the plaque goes dull
		{
			auto x = clock_x + 12.f;
			mini_button(state, x, mid, is_paused ? icon::play : icon::pause, hit_target{ 0.f, 0.f, 0.f, 0.f, action::press, pause_button },
				is_paused ? localized(state, "katechon_hud_resume", "RESUME") : localized(state, "katechon_hud_pause", "PAUSE"), is_paused, hovering, mx, my);
			x += 26.f + 12.f;
			{
				hit_target t{ x - 6.f, 5.f, date_w + 12.f, 40.f, action::press, pause_button };
				cut(state, snap.date, x, row, 16, 0.04f, hovering && over(t, mx, my) ? ogl::color3f{ 1.f, 0.96f, 0.84f } : cream);
				targets.push_back(t);
				// The year as a rule of twelve months, with a mark that moves along
				// it as the days pass.
				auto const today = state.current_date.to_ymd(state.start_date);
				auto const through = std::clamp((float(today.month) - 1.f + (float(today.day) - 1.f) / 31.f) / 12.f, 0.f, 1.f);
				fill(state, x, under + 2.f, date_w, 1.f, ink, 0.55f);
				for(int32_t m = 0; m <= 12; ++m)
					fill(state, std::round(x + date_w * float(m) / 12.f), under - (m % 3 == 0 ? 3.f : 1.f), 1.f, m % 3 == 0 ? 5.f : 3.f, bronze_light, m % 3 == 0 ? 0.85f : 0.50f);
				fill(state, x, under + 1.f, date_w * through, 2.f, is_paused ? bronze : gold, is_paused ? 0.7f : 0.9f);
				fill(state, std::round(x + date_w * through) - 1.f, under - 4.f, 3.f, 9.f, ink, 0.8f);
				fill(state, std::round(x + date_w * through), under - 3.f, 1.f, 7.f, is_paused ? bronze_light : ogl::color3f{ 1.f, 0.90f, 0.62f }, 1.f);
			}
			x += date_w + 16.f;
			mini_button(state, x, mid, icon::minus, hit_target{ 0.f, 0.f, 0.f, 0.f, action::press, slower_button }, localized(state, "katechon_hud_slower", "SLOWER"), false, hovering, mx, my);
			x += 26.f + 7.f;
			// The speed as five lamps. While time runs they breathe, one after another.
			for(int32_t i = 0; i < 5; ++i) {
				auto const lit = i < speed;
				auto const breath = is_paused ? 1.f : 0.80f + 0.20f * std::sin(seconds * 3.2f - float(i) * 0.7f);
				fill(state, x + float(i) * 8.5f, row - 8.f, 6.f, 14.f, ink, 0.80f);
				fill(state, x + float(i) * 8.5f + 1.f, row - 7.f, 4.f, 12.f, ogl::color3f{ 0.16f, 0.11f, 0.08f }, 1.f);
				if(lit)
					fill(state, x + float(i) * 8.5f + 1.f, row - 7.f, 4.f, 12.f, is_paused ? bronze : ogl::color3f{ 1.f, 0.80f, 0.44f }, breath);
			}
			if(is_paused)
				centered(state, localized(state, "katechon_hud_paused", "PAUSED"), x - 6.f, 54.f, under + 1.f, 7, 0.24f, ogl::color3f{ 0.96f, 0.52f, 0.40f });
			x += 42.f + 7.f;
			mini_button(state, x, mid, icon::plus, hit_target{ 0.f, 0.f, 0.f, 0.f, action::press, faster_button }, localized(state, "katechon_hud_faster", "FASTER"), false, hovering, mx, my);
		}

		// ---- the state, standing proud of the bar. A click brings the capital into view ----
		auto x = 16.f;
		if(snap.player) {
			auto const name = fit(state, capitals_of(snap.player_name), 16, 0.16f, 240.f);
			auto const text_w = std::max(shaped(state, name, 16, 0.16f).width, shaped(state, snap.player_status, 9, 0.18f).width);
			auto const block_w = std::round(12.f + 46.f + 14.f + text_w + 16.f);
			hit_target t{ 6.f, 5.f, block_w, 40.f, action::capital };
			auto const hovered = hovering && over(t, mx, my);
			slab(state, shell_raised, 6.f, block_w);
			if(hovered)
				fill(state, 8.f, 7.f, block_w - 4.f, 36.f, cream, 0.06f);
			if(auto const handle = flag_of(state, snap.player); handle)
				ogl::render_textured_rect(state, ogl::color_modification::none, 16.f, 10.f, 46.f, 30.f, handle, ui::rotation::upright, false, false);
			art(state, shell_flagframe, 11.f, 5.f, 56.f, 40.f);
			cut(state, name, 78.f, row, 16, 0.16f, hovered ? ogl::color3f{ 1.f, 0.96f, 0.84f } : cream);
			cut(state, snap.player_status, 78.f, under, 9, 0.18f, ogl::color3f{ 0.96f, 0.80f, 0.48f });
			targets.push_back(t);
			if(hovered)
				hint(snap.standing, t); // what the rank is made of
			x = 6.f + block_w + 14.f;
		}

		// ---- the figures that matter, each let into the bar with which way it is going ----
		for(size_t i = 0; i < snap.wells.size(); ++i) {
			auto const& f = snap.wells[i];
			if(i < shown_values.size()) {
				if(!shown_values[i].empty() && shown_values[i] != f.value)
					flash[i] = 50; // it has just changed: it shows gold for a moment
				shown_values[i] = f.value;
			}
			auto const lit = i < flash.size() && flash[i] > 0 ? float(flash[i]--) / 50.f : 0.f;
			auto const text_w = std::max(shaped(state, f.value, 17, 0.02f).width, shaped(state, f.note, 9, 0.10f).width + (f.way != 0 ? 11.f : 0.f));
			auto const well_w = std::round(12.f + 20.f + 9.f + text_w + 14.f);
			if(x + well_w > centre - 44.f && x < centre + 44.f)
				x = centre + 44.f; // step over what hangs from the middle
			if(x + well_w > clock_x - 12.f)
				break;
			hit_target t{ x, 5.f, well_w, 40.f, action::none };
			slab(state, shell_well, x, well_w);
			draw_icon(state, f.image, x + 12.f + 10.f, mid, 20.f, mix(ogl::color3f{ 0.80f, 0.66f, 0.42f }, ogl::color3f{ 1.f, 0.92f, 0.66f }, lit));
			cut(state, f.value, x + 41.f, row, 17, 0.02f, mix(cream, ogl::color3f{ 1.f, 0.86f, 0.50f }, lit));
			auto const tone = f.way > 0 ? good : (f.way < 0 ? ogl::color3f{ 0.92f, 0.52f, 0.42f } : bronze_light);
			auto note_x = x + 41.f;
			if(f.way != 0) {
				arrow(state, note_x, under, f.way > 0, tone);
				note_x += 11.f;
			}
			draw_text(state, f.note, note_x, under, 9, 0.10f, tone);
			if(hovering && over(t, mx, my))
				hint(f.label, t);
			x += well_w + 8.f;
		}
		// What has become a concern is set beside them, plainly.
		for(auto const& a : snap.alerts) {
			auto const text_w = std::max(shaped(state, a.value, 15, 0.02f).width, shaped(state, a.label, 8, 0.18f).width);
			if(x + 8.f + text_w > centre - 44.f && x < centre + 44.f)
				x = centre + 48.f;
			if(x + 8.f + text_w > clock_x - 12.f)
				break;
			cut(state, a.value, x + 8.f, row, 15, 0.02f, a.warning ? alarm : cream);
			draw_text(state, a.label, x + 8.f, under, 8, 0.18f, a.warning ? alarm : bronze_light);
			x += 8.f + text_w + 16.f;
		}

		art(state, shell_keystone, centre - 33.f, 0.f, 66.f, 70.f);
	}

	// The windows of the country, as a ribbon hung down the left edge under the
	// flag: grouped by what they govern, each an emblem that names itself when
	// the mouse comes to it. The open one sits on a crimson seal.
	void draw_ribbon(sys::state& state, bool hovering, float mx, float my) {
		struct entry {
			size_t tab;
			icon image;
		};
		static constexpr std::array<entry, 8> entries{ entry{ 0, icon::production }, entry{ 1, icon::budget }, entry{ 5, icon::trade },
			entry{ 3, icon::politics }, entry{ 6, icon::diplomacy }, entry{ 7, icon::military }, entry{ 4, icon::society }, entry{ 2, icon::technology } };
		constexpr float pitch = 46.f;
		constexpr float group_gap = 10.f;
		auto const body_h = 8.f + float(entries.size()) * pitch + 2.f * group_gap + 4.f;
		auto const top = top_h - 3.f;
		ogl::render_alpha_colored_rect(state, 0.f, top, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
		art(state, shell_ribbon, 0.f, top, ribbon_w, body_h);
		art(state, shell_ribbon_tail, 0.f, top + body_h, ribbon_w, 30.f);
		if(!shell_ribbon)
			sheet(state, 0.f, top, ribbon_w, body_h, ogl::color3f{ 0.20f, 0.14f, 0.10f });
		panels.push_back(rect{ 0.f, top, ribbon_w, body_h + 16.f });
		constexpr float cx = 23.5f;
		auto y = top + 8.f;
		for(size_t i = 0; i < entries.size(); ++i) {
			if(i == 3 || i == 6) {
				fill(state, 11.f, y + group_gap * 0.5f, 25.f, 1.f, ogl::color3f{ 0.89f, 0.75f, 0.49f }, 0.40f);
				y += group_gap;
			}
			auto const index = entries[i].tab;
			auto const is_population = tabs[index].element == "topbarbutton_pops";
			// The land forces are commanded from the map and one panel, not from the old army window.
			auto const is_military = tabs[index].element == "topbarbutton_military";
			// Foreign policy too has its own sheet; the old diplomacy window is never shown.
			auto const is_diplomacy = tabs[index].element == "topbarbutton_diplomacy";
			// Trade opens its sheet over the map; the old dashboard is a press away from it.
			auto const is_trade = tabs[index].element == "topbarbutton_trade";
			hit_target t{ 0.f, y, ribbon_w - 3.f, pitch, is_population ? action::toggle_population : (is_military ? action::toggle_military : (is_diplomacy ? action::toggle_diplomacy : (is_trade ? action::toggle_trade : action::press))), tab_buttons[index] };
			auto const active = is_population ? population_open : (is_military ? military_open : (is_diplomacy ? diplomacy_open : (is_trade ? trade_open : (tab_buttons[index] && static_cast<topbar_tab_button*>(tab_buttons[index])->is_active(state)))));
			auto const hovered = hovering && over(t, mx, my);
			auto const cy = y + pitch * 0.5f;
			if(shell_rounds && (active || hovered))
				ogl::render_tinted_subsprite(state, active ? 0 : 1, 5, cx - 24.f, cy - 24.f, 48.f, 48.f, 1.f, 1.f, 1.f, shell_rounds, ui::rotation::upright, false, false);
			draw_icon(state, entries[i].image, cx, cy, 22.f, active ? ogl::color3f{ 1.f, 0.93f, 0.76f } : (hovered ? ogl::color3f{ 1.f, 0.88f, 0.58f } : ogl::color3f{ 0.84f, 0.70f, 0.45f }));
			if(tab_buttons[index] || is_population || is_military || is_diplomacy || is_trade)
				targets.push_back(t);
			if(hovered)
				hint(as_a_name(localized(state, tabs[index].key, tabs[index].fallback)), t);
			y += pitch;
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

		auto const x = ribbon_w;
		auto const top = top_h + hang;
		auto const right = x + card_w - pad - 2.f;
		auto const left = x + pad;
		draw_dock(state, card_w, h, false, card_base);
		sheet(state, x, top, card_w - 1.f, 62.f, card_head);

		// The province.
		{
			hit_target close{ x + card_w - 40.f, top + 8.f, 28.f, 28.f, action::close_province };
			draw_icon(state, icon::close, close.x + 14.f, close.y + 14.f, 12.f, hovering && over(close, mx, my) ? gold : muted);
			targets.push_back(close);
		}
		draw_text(state, fit(state, snap.province_name, 18, 0.05f, card_w - 2.f * pad - 28.f), left, top + 24.f, 18, 0.05f, bright);
		if(!snap.province_state.empty())
			draw_text(state, fit(state, snap.province_state, 12, 0.10f, card_w - 2.f * pad), left, top + 46.f, 12, 0.10f, gold);
		draw_rule(state, left, top + 62.f, right - left, 0.40f);

		auto y = top + 64.f;
		if(!sea) {
			for(size_t i = 0; i < snap.province_rows.size(); ++i) {
				auto const& row = snap.province_rows[i];
				if(i == 0) {
					// The people of the place lead to the population inspector, on this place.
					hit_target t{ left - 6.f, y, right - left + 12.f, row_pitch, action::population_inspect, nullptr, int32_t(snap.province.index()) };
					auto const hovered = hovering && over(t, mx, my);
					if(hovered) {
						fill(state, t.x, t.y + 1.f, t.w, t.h - 2.f, gold, 0.08f);
						hint(localized(state, "katechon_ppl_inspect_hint", "The population of this region, on the map"), t);
					}
					label_row(state, row, left, right - 14.f, y + row_pitch * 0.5f);
					draw_icon(state, icon::chevron_right, right - 4.f, y + row_pitch * 0.5f, 9.f, hovered ? gold : muted);
					targets.push_back(t);
				} else {
					label_row(state, row, left, right, y + row_pitch * 0.5f);
				}
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
		// The state that owns it is a different matter from the place itself.
		sheet(state, x, y, card_w - 1.f, top + h - y - 1.f, panel_hero);
		fill(state, x, y, card_w - 1.f, 1.f, ink, 0.50f);
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

	static std::string percent(float part, size_t digits = 0) {
		return part < 0.f ? std::string("n/a") : text::format_percentage(part, digits);
	}
	static std::string signed_percent(float part) {
		return (part > 0.f ? "+" : "") + text::format_percentage(part, 2);
	}
	std::string signed_people(float units) const {
		return (units > 0.f ? "+" : (units < 0.f ? "-" : "")) + people(std::abs(units));
	}

	float dock_w = 344.f; // follows the width of the window, within limits
	inline static constexpr ogl::color3f good{ 0.52f, 0.76f, 0.56f };
	inline static constexpr ogl::color3f label_tone{ 0.76f, 0.64f, 0.44f }; // what a figure is
	inline static constexpr ogl::color3f quiet{ 0.58f, 0.48f, 0.34f }; // remarks, and what is not known
	// The panel is assets/katechon_plate.png (tools/katechon_ui/build_bar.py):
	// a crimson title in the leather of the bar over a calm, warm body. It is drawn at half its
	// pixel size: its top as far down as needed, then its foot.
	inline static constexpr float plate_image_h = 640.f;
	inline static constexpr float plate_foot = 160.f;
	inline static constexpr float plate_margin = 14.f;

	// The place of the value is written %1 in the text files: the game's text
	// reader takes a dollar sign for the start of one of its own variables.
	static std::string filled(std::string pattern, std::string const& value) {
		auto at = pattern.find("%1");
		if(at == std::string::npos)
			at = pattern.find("$1");
		if(at != std::string::npos)
			pattern.replace(at, 2, value);
		return pattern;
	}

	// Running text, broken at the right edge. With draw off it only measures.
	float paragraph(sys::state& state, std::string const& content, float x, float right, float y, int32_t size, ogl::color3f colour, int32_t max_lines, bool draw) {
		auto const pitch = std::round(float(size) * 1.5f);
		std::string line;
		size_t at = 0;
		auto lines = 0;
		auto const put = [&]() {
			if(draw)
				draw_text(state, line, x, y + pitch * 0.5f, size, 0.01f, colour);
			y += pitch;
			++lines;
		};
		while(at <= content.size() && lines < max_lines) {
			auto const next = content.find(' ', at);
			auto const word = content.substr(at, next == std::string::npos ? std::string::npos : next - at);
			auto const candidate = line.empty() ? word : line + " " + word;
			if(!line.empty() && shaped(state, candidate, size, 0.01f).width > right - x) {
				put();
				line = word;
			} else {
				line = candidate;
			}
			if(next == std::string::npos)
				break;
			at = next + 1;
		}
		if(!line.empty() && lines < max_lines)
			put();
		return y;
	}

	void draw_plate(sys::state& state, float x, float y, float w, float h) {
		if(!plate) {
			sheet(state, x, y, w, h, ogl::color3f{ 0.16f, 0.11f, 0.08f });
		} else {
			// a plain rectangle first: it leaves the renderer in the state the image needs
			ogl::render_alpha_colored_rect(state, x, y, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
			auto const upper = h - plate_foot;
			ogl::render_subrect(state, x, y, w, upper, 0.f, 0.f, 1.f, upper / plate_image_h, plate);
			ogl::render_subrect(state, x, y + upper, w, plate_foot, 0.f, 1.f - plate_foot / plate_image_h, 1.f, plate_foot / plate_image_h, plate);
		}
		shadow_below(state, x + 3.f, y + h, w - 3.f);
		shadow_right(state, x + w, y + 3.f, h - 3.f);
		panels.push_back(rect{ x, y, w, h });
	}

	// ---- The population pages ----
	//
	// Two pages read the same view. The inspector stands beside the map while
	// the player works with places: who lives here, the five figures the map
	// can be painted with, and what the colours say. The overview puts the map
	// aside for the questions of state: how many, why the number changes, how
	// people make a living, move and live, who they are and where. Names are set
	// in the serif of the shell; every figure in the sans, which reads at once.

	inline static constexpr ogl::color3f pop_tile{ 0.060f, 0.078f, 0.104f };
	inline static constexpr ogl::color3f pop_own{ 0.56f, 0.66f, 0.44f }; // own land and workshops
	inline static constexpr ogl::color3f pop_wage{ 0.42f, 0.62f, 0.78f }; // wage contracts
	inline static constexpr ogl::color3f pop_other{ 0.34f, 0.36f, 0.40f };
	inline static constexpr ogl::color3f pop_poor{ 0.46f, 0.55f, 0.66f };
	inline static constexpr ogl::color3f pop_middle{ 0.78f, 0.63f, 0.38f };
	inline static constexpr ogl::color3f pop_rich{ 0.94f, 0.86f, 0.66f };

	// Whether the pointer is over a target the population pages can answer in.
	bool pop_over(hit_target const& t, bool hovering, float mx, float my) const {
		return hovering && my >= clip_top && my < clip_bottom && over(t, mx, my);
	}
	// A target, cut to the part of the page that is in view.
	void pop_target(hit_target t) {
		auto const top = std::max(t.y, clip_top);
		auto const bottom = std::min(t.y + t.h, clip_bottom);
		if(bottom <= top)
			return;
		t.y = top;
		t.h = bottom - top;
		targets.push_back(t);
	}
	// A pattern with two places, %1 and %2.
	static std::string filled(std::string pattern, std::string const& first, std::string const& second) {
		pattern = filled(std::move(pattern), first);
		if(auto const at = pattern.find("%2"); at != std::string::npos)
			pattern.replace(at, 2, second);
		return pattern;
	}
	static int32_t region_value(dcon::province_id p) {
		return int32_t(p.index());
	}

	std::string signed_count(float units) const {
		if(std::abs(units) * people_per_unit < 0.5f)
			return "0";
		return signed_people(units);
	}
	ogl::color3f tone_colour(int32_t tone) const {
		return tone > 0 ? dip_yes : (tone < 0 ? dip_no : dip_text);
	}
	// A share when it is kept, a dash when it is not.
	static std::string share_text(float part) {
		if(part < 0.f)
			return "—";
		return text::format_percentage(part, part > 0.f && part < 0.1f ? 1 : 0);
	}

	// The month's change as a part of the whole.
	std::string change_line(sys::state& state, population_view const& v) const {
		if(v.units <= 0.f)
			return std::string();
		auto const rate = v.change_month / v.units;
		return signed_percent(rate) + " " + localized(state, "katechon_pop_per_month", "per month");
	}

	// A heading in tracked capitals with a rule after it; returns where the content starts.
	float pop_heading(sys::state& state, std::string const& label, std::string const& aside, float left, float right, float y) {
		auto const tw = sans(state, upper(label), left, y + 7.f, 10, dip_bronze, false, 0.16f);
		auto const aside_w = aside.empty() ? 0.f : sans(state, aside, right, y + 7.f, 11, dip_faint, true) + 10.f;
		fill(state, left + tw + 10.f, y + 7.f, std::max(0.f, right - left - tw - 20.f - aside_w), 1.f, dip_bronze, 0.30f);
		return y + 24.f;
	}

	// A bar of parts, side by side, filling a width.
	void parts_bar(sys::state& state, float x, float y, float w, float h, std::initializer_list<std::pair<float, ogl::color3f>> parts) {
		fill(state, x, y, w, h, ink, 0.45f);
		auto at = x;
		for(auto const& [part, colour] : parts) {
			if(part <= 0.f)
				continue;
			auto const pw = std::min(x + w - at, std::round(w * part));
			if(pw <= 0.f)
				continue;
			fill(state, at, y, pw, h, colour, 1.f);
			at += pw;
		}
	}

	// A labelled share with a bar under the label: label left, share right.
	float share_row(sys::state& state, std::string const& label, float part, ogl::color3f colour, float left, float right, float y) {
		auto const value = share_text(part);
		auto const vw = sans(state, value, right, y + 8.f, 13, part < 0.f ? dip_faint : dip_text, true);
		sans(state, sans_fit(state, label, 13, right - left - vw - 12.f), left, y + 8.f, 13, dip_muted);
		fill(state, left, y + 19.f, right - left, 3.f, ink, 0.45f);
		if(part > 0.f)
			fill(state, left, y + 19.f, std::max(2.f, std::round((right - left) * std::min(part, 1.f))), 3.f, colour, 1.f);
		return y + 30.f;
	}

	// The place in the bread-crumbs: a region inside a country, a foreign country, one's own.
	float pop_crumbs(sys::state& state, population_view const& v, float x, float y, bool hovering, float mx, float my) {
		auto const foreign = v.nation && v.nation != state.local_player_nation;
		auto const crumb = [&](std::string const& label, action act, float at) {
			hit_target back{ at - 4.f, y - 10.f, sans_w(state, label, 13) + 22.f, 22.f, act };
			auto const hovered = pop_over(back, hovering, mx, my);
			draw_icon(state, icon::chevron_left, at + 4.f, y, 9.f, hovered ? dip_name : dip_bronze);
			sans(state, label, at + 14.f, y, 13, hovered ? dip_name : dip_bronze);
			pop_target(back);
			return back.w + 10.f;
		};
		auto at = x;
		if(v.of_province && v.nation)
			at += crumb(text::get_name_as_string(state, dcon::fatten(state.world, v.nation)), action::population_whole_country, at);
		if(foreign && state.local_player_nation)
			at += crumb(localized(state, "katechon_ppl_your_country", "Your country"), action::population_home, at);
		return at - x;
	}

	// The record of the last days as a line, with its lowest and highest marked.
	void trend_chart(sys::state& state, std::vector<float> const& series, float x, float y, float w, float h, bool labels) {
		fill(state, x, y + h, w, 1.f, dip_rule, 1.f);
		auto lo = series.front();
		auto hi = series.front();
		for(auto const v : series) {
			lo = std::min(lo, v);
			hi = std::max(hi, v);
		}
		if(hi - lo < std::max(1e-6f, hi * 1e-5f)) {
			lo -= std::max(1.f, hi * 0.001f);
			hi += std::max(1.f, hi * 0.001f);
		}
		auto const pad_v = (hi - lo) * 0.12f;
		lo -= pad_v;
		hi += pad_v;
		if(labels) {
			for(int32_t i = 1; i <= 2; ++i) {
				auto const gy = std::round(y + h * float(i) / 3.f);
				fill(state, x, gy, w, 1.f, dip_rule, 0.55f);
			}
			sans(state, people(hi - pad_v), x + 4.f, y + 8.f, 11, dip_faint);
			sans(state, people(lo + pad_v), x + 4.f, y + h - 9.f, 11, dip_faint);
		}
		std::vector<float> points(population_trend.count, 0.5f);
		for(uint32_t i = 0; i < population_trend.count; ++i) {
			auto const at = float(i) / float(population_trend.count - 1) * float(series.size() - 1);
			auto const a = size_t(at);
			auto const b = std::min(series.size() - 1, a + 1);
			auto const value = series[a] + (series[b] - series[a]) * (at - float(a));
			points[i] = (value - lo) / (hi - lo);
		}
		population_trend.set_y(points.data());
		auto const falling = series.back() < series.front();
		auto const colour = series.back() == series.front() ? dip_text : (falling ? dip_no : dip_yes);
		ogl::render_linegraph(state, ogl::color_modification::none, x, y, w, h, colour.r, colour.g, colour.b, 1.f, population_trend);
		fill(state, x + w - 3.f, y + h * (1.f - points.back()) - 3.f, 6.f, 6.f, colour, 1.f);
	}

	void draw_population(sys::state& state, float w, float h, bool hovering, float mx, float my) {
		if(!dip_art_read)
			load_dip_art(state);
		if(population_overview)
			draw_population_overview(state, w, h, hovering, mx, my);
		else
			draw_population_inspector(state, h, hovering, mx, my);
		clip_top = -1e9f;
		clip_bottom = 1e9f;
	}

	// ---- the inspector, beside the map ----
	void draw_population_inspector(sys::state& state, float h, bool hovering, float mx, float my) {
		auto const& v = population;
		constexpr float w = 360.f;
		auto const x0 = ribbon_w + 8.f;
		auto const y0 = top_h + 12.f;
		auto const left = x0 + 22.f;
		auto const right = x0 + w - 22.f;
		auto const foreign = v.nation && v.nation != state.local_player_nation;
		auto const stepped = v.of_province || foreign;
		auto const room = h - y0 - 16.f;
		clip_top = -1e9f;
		clip_bottom = 1e9f;

		// What goes on it decides its height; the standouts give way first.
		auto const shown = population_layer ? metric_reading(state, metric) : std::string();
		auto const reading_h = population_layer ? sans_paragraph(state, shown, left, right, 0.f, 12, dip_muted, 3, false) : 0.f;
		auto const has_history = v.history.size() >= 2;
		constexpr float metric_h = 34.f;
		auto content = 18.f + 22.f + 52.f + (stepped ? 26.f : 0.f) + 12.f + 44.f + (has_history ? 40.f : 8.f)
			+ 24.f + float(uint8_t(population_metric::count)) * metric_h + 10.f
			+ (population_layer ? 40.f + reading_h + 6.f + 26.f : 0.f) + 58.f;
		auto standouts_h = 0.f;
		if(population_layer && v.units > 0.f) {
			standouts_h = v.of_province ? 30.f : (v.highest.empty() ? 0.f : 26.f + float(v.highest.size()) * 22.f + 8.f);
			if(content + standouts_h > room)
				standouts_h = 0.f;
		}
		auto const ph = std::min(room, content + standouts_h);
		dip_frame(state, x0, y0, w, ph);
		auto const floor_y = y0 + ph - 12.f;

		// What this page is.
		auto y = y0 + 18.f;
		{
			auto const scope = v.of_province ? localized(state, "katechon_ppl_scope_region", "Population  ·  region")
				: (foreign ? localized(state, "katechon_ppl_scope_foreign", "Population  ·  foreign country") : localized(state, "katechon_ppl_scope_own", "Population  ·  your country"));
			sans(state, upper(scope), left, y + 6.f, 10, dip_bronze, false, 0.16f);
			hit_target close{ right - 12.f, y - 8.f, 26.f, 26.f, action::toggle_population };
			draw_icon(state, icon::close, close.x + 13.f, close.y + 13.f, 11.f, hovering && over(close, mx, my) ? dip_text : dip_bronze);
			targets.push_back(close);
			if(hovering && over(close, mx, my))
				hint(localized(state, "katechon_ppl_close", "Close"), close);
		}
		y += 22.f;
		// Whose it is.
		if(v.nation)
			dip_flag(state, v.nation, left, y + 4.f, 36.f, 24.f);
		auto const tx = left + (v.nation ? 48.f : 0.f);
		draw_text(state, fit(state, v.title, 24, 0.01f, right - tx), tx, y + 14.f, 24, 0.01f, dip_name);
		sans(state, sans_fit(state, v.subtitle, 13, right - tx), tx, y + 38.f, 13, dip_muted);
		y += 52.f;
		if(stepped) {
			pop_crumbs(state, v, left, y + 10.f, hovering, mx, my);
			y += 26.f;
		}
		dip_rule_line(state, left, right, y + 4.f);
		y += 12.f;

		// How many, and which way it goes.
		if(v.units <= 0.f) {
			sans(state, localized(state, "katechon_pop_empty", "Nobody lives here."), left, y + 18.f, 15, dip_faint);
			y += 44.f;
		} else {
			sans(state, people(v.units), left - 1.f, y + 18.f, 30, dip_name, false, 0.f);
			auto const rate = v.change_month / v.units;
			auto const tone = rate < -0.00005f ? dip_no : (rate > 0.00005f ? dip_yes : dip_muted);
			auto const line = change_line(state, v);
			auto const lw = sans(state, line, right, y + 10.f, 13, tone, true);
			if(std::abs(rate) > 0.00005f)
				arrow(state, right - lw - 10.f, y + 10.f, rate > 0.f, tone);
			sans(state, signed_count(v.change_month) + " " + localized(state, "katechon_ppl_people_word", "people"), right, y + 28.f, 12, dip_muted, true);
			y += 44.f;
		}
		if(has_history) {
			trend_chart(state, v.history, left, y + 2.f, right - left, 26.f, false);
			sans(state, localized(state, "katechon_ppl_last_days", "last 30 days"), right, y + 38.f, 10, dip_faint, true);
			y += 40.f;
		} else {
			y += 8.f;
		}

		// The figures, each the map it can paint.
		y = pop_heading(state, localized(state, "katechon_ppl_figures", "On the map"), std::string(), left, right, y);
		static constexpr std::array<icon, 5> metric_icons{ icon::person, icon::work, icon::movement, icon::society, icon::technology };
		for(uint8_t i = 0; i < uint8_t(population_metric::count); ++i) {
			auto const m = population_metric(i);
			auto const value = metric_value(state, v.nation, v.province, m);
			hit_target t{ left - 10.f, y, right - left + 20.f, metric_h - 2.f, action::population_metric, nullptr, int32_t(i) };
			auto const hovered = pop_over(t, hovering, mx, my);
			auto const chosen = population_layer && metric == m;
			if(chosen) {
				fill(state, t.x, t.y, t.w, t.h, dip_bronze, 0.16f);
				fill(state, t.x, t.y + 3.f, 2.f, t.h - 6.f, gold, 1.f);
			} else if(hovered) {
				fill(state, t.x, t.y, t.w, t.h, dip_bronze, 0.08f);
			}
			auto const mid = y + (metric_h - 2.f) * 0.5f;
			draw_icon(state, metric_icons[i], left + 8.f, mid, 16.f, chosen ? gold : dip_bronze);
			sans(state, metric_name(state, m), left + 26.f, mid, 14, chosen || hovered ? dip_name : dip_muted);
			auto const tone = value ? metric_tone(m, *value) : 0;
			sans(state, metric_text(state, m, value), right, mid, 15, value ? tone_colour(tone) : dip_faint, true);
			pop_target(t);
			if(hovered)
				hint(metric_about(state, m), t);
			y += metric_h;
		}
		y += 10.f;

		// What the colours say, and where this place stands on them.
		if(population_layer) {
			std::optional<float> own_value;
			if(v.units > 0.f)
				own_value = metric_value(state, v.nation, v.province, metric);
			auto const from = metric == population_metric::migration ? -1.f : 0.f;
			constexpr int32_t steps = 64;
			auto const step = (right - left) / float(steps);
			for(int32_t i = 0; i < steps; ++i) {
				auto const c = metric_colour(metric, from + (1.f - from) * float(i) / float(steps - 1));
				fill(state, left + float(i) * step, y + 4.f, step + 0.5f, 6.f, c, 1.f);
			}
			if(own_value) {
				auto const at = (metric_position(metric, *own_value) - from) / (1.f - from);
				auto const mark_x = std::round(left + (right - left) * std::clamp(at, 0.f, 1.f));
				fill(state, mark_x - 2.f, y - 2.f, 4.f, 18.f, ink, 0.9f);
				fill(state, mark_x - 1.f, y - 1.f, 2.f, 16.f, dip_name, 1.f);
			}
			auto const ends = metric_ends(state, metric);
			sans(state, ends.first, left, y + 24.f, 11, dip_faint);
			sans(state, ends.second, right, y + 24.f, 11, dip_faint, true);
			y += 40.f;
			y = sans_paragraph(state, shown, left, right, y, 12, dip_muted, 3, true) + 6.f;

			// Where the place stands among the country's regions, or which regions stand out.
			if(standouts_h > 0.f) {
				if(v.of_province) {
					if(v.rank > 0) {
						auto const country = metric_value(state, v.nation, dcon::province_id{}, metric);
						auto line = filled(localized(state, "katechon_ppl_rank", "%1 of %2 regions"), std::to_string(v.rank), std::to_string(v.ranked));
						if(country)
							line += "  ·  " + filled(localized(state, "katechon_ppl_country_value", "the country %1"), metric_text(state, metric, *country));
						sans(state, sans_fit(state, line, 13, right - left), left, y + 10.f, 13, dip_text);
					}
					y += 30.f;
				} else {
					auto const half = (right - left - 16.f) * 0.5f;
					sans(state, upper(localized(state, "katechon_ppl_highest", "Highest")), left, y + 8.f, 10, dip_bronze, false, 0.14f);
					sans(state, upper(localized(state, "katechon_ppl_lowest", "Lowest")), left + half + 16.f, y + 8.f, 10, dip_bronze, false, 0.14f);
					y += 26.f;
					for(size_t i = 0; i < v.highest.size(); ++i) {
						for(int32_t side = 0; side < 2; ++side) {
							auto const& entry = side == 0 ? v.highest[i] : v.lowest[i];
							auto const cx = side == 0 ? left : left + half + 16.f;
							hit_target t{ cx - 4.f, y, half + 8.f, 22.f, action::population_region, nullptr, region_value(entry.first) };
							auto const hovered = pop_over(t, hovering, mx, my);
							if(hovered)
								fill(state, t.x, t.y, t.w, t.h, dip_bronze, 0.10f);
							auto const value = metric_text(state, metric, entry.second);
							auto const vw = sans(state, value, cx + half, y + 11.f, 12, dip_text, true);
							sans(state, sans_fit(state, text::get_name_as_string(state, entry.first), 12, half - vw - 8.f), cx, y + 11.f, 12, hovered ? dip_name : dip_muted);
							pop_target(t);
						}
						y += 22.f;
					}
					y += 8.f;
				}
			}

			// The region under the pointer, read on the same figure.
			{
				auto const under = state.map_state.under_mouse_province;
				auto const on_map = !hovering && under && under.index() < state.province_definitions.first_sea_province.index();
				if(on_map) {
					auto const value = metric_value(state, dcon::nation_id{}, under, metric);
					auto const line = text::get_name_as_string(state, under) + ":  " + metric_text(state, metric, value);
					sheet(state, left - 8.f, y, right - left + 16.f, 22.f, pop_tile, 0.9f);
					draw_icon(state, icon::search, left + 4.f, y + 11.f, 11.f, dip_bronze);
					sans(state, sans_fit(state, line, 12, right - left - 20.f), left + 16.f, y + 11.f, 12, dip_text);
				} else {
					sans(state, localized(state, "katechon_ppl_point_hint", "Point at a region to read it; press it to study it"), left, y + 11.f, 11, dip_faint);
				}
				y += 26.f;
			}
		}

		// The whole of it, with the map put aside.
		{
			auto const by = std::max(y + 6.f, floor_y - 40.f);
			dip_gold_button(state, left, by, right - left, 34.f, localized(state, "katechon_ppl_open_overview", "Population overview"), true,
				hit_target{ 0.f, 0.f, 0.f, 0.f, action::population_overview, nullptr, 1 }, hovering, mx, my);
		}
	}

	// ---- the overview, with the map put aside ----
	void draw_population_overview(sys::state& state, float w, float h, bool hovering, float mx, float my) {
		auto const& v = population;
		auto const foreign = v.nation && v.nation != state.local_player_nation;
		auto const stepped = v.of_province || foreign;
		// The map recedes; the page takes the room right of the ribbon.
		fill(state, ribbon_w, top_h, w - ribbon_w, h - top_h, ink, 0.55f);
		auto const x0 = ribbon_w + 10.f;
		auto const y0 = top_h + 10.f;
		auto const pw = w - x0 - 12.f;
		auto const ph = h - y0 - 12.f;
		dip_frame(state, x0, y0, pw, ph, true);
		auto const inner = std::min(pw - 64.f, 1320.f);
		auto const left = std::round(x0 + (pw - inner) * 0.5f);
		auto const right = left + inner;
		constexpr float head_h = 118.f;
		auto const body_top = y0 + head_h;
		auto const body_bottom = y0 + ph - 10.f;
		overview_view_h = body_bottom - body_top;
		overview_scroll = std::clamp(overview_scroll, 0.f, std::max(0.f, overview_content_h - overview_view_h));

		// ---- the head: whose people, how many, which way ----
		{
			auto const scope = v.of_province ? localized(state, "katechon_ppl_scope_region", "Population  ·  region")
				: (foreign ? localized(state, "katechon_ppl_scope_foreign", "Population  ·  foreign country") : localized(state, "katechon_ppl_scope_own", "Population  ·  your country"));
			sans(state, upper(localized(state, "katechon_ppl_overview", "Population overview")) + "   ·   " + upper(scope), left, y0 + 26.f, 10, dip_bronze, false, 0.16f);
			if(v.nation)
				dip_flag(state, v.nation, left, y0 + 44.f, 54.f, 36.f);
			auto const tx = left + (v.nation ? 70.f : 0.f);
			auto const title_room = std::max(120.f, right - tx - 420.f);
			draw_text(state, fit(state, v.title, 32, 0.01f, title_room), tx, y0 + 60.f, 32, 0.01f, dip_name);
			auto const asof = filled(localized(state, "katechon_ppl_asof", "as of %1"), text::date_to_string(state, state.current_date));
			auto const sub = v.subtitle.empty() ? asof : v.subtitle + "  ·  " + asof;
			auto const sub_w = sans(state, sans_fit(state, sub, 14, title_room), tx, y0 + 90.f, 14, dip_muted);
			if(stepped) {
				clip_top = -1e9f;
				clip_bottom = 1e9f;
				pop_crumbs(state, v, tx + sub_w + 22.f, y0 + 90.f, hovering, mx, my);
			}
			// the close, and the way to the map
			hit_target close{ right - 12.f, y0 + 14.f, 26.f, 26.f, action::toggle_population };
			draw_icon(state, icon::close, close.x + 13.f, close.y + 13.f, 12.f, hovering && over(close, mx, my) ? dip_text : dip_bronze);
			targets.push_back(close);
			if(hovering && over(close, mx, my))
				hint(localized(state, "katechon_ppl_close", "Close"), close);
			constexpr float button_w = 170.f;
			dip_gold_button(state, right - button_w, y0 + 50.f, button_w, 34.f, localized(state, "katechon_ppl_show_map", "Show on the map"), true,
				hit_target{ 0.f, 0.f, 0.f, 0.f, action::population_overview, nullptr, 0 }, hovering, mx, my);
			// how many
			auto const fx = right - button_w - 28.f;
			if(v.units > 0.f) {
				sans(state, people(v.units), fx, y0 + 60.f, 34, dip_name, true, 0.f);
				auto const rate = v.change_month / v.units;
				auto const tone = rate < -0.00005f ? dip_no : (rate > 0.00005f ? dip_yes : dip_muted);
				auto const line = change_line(state, v) + "  (" + signed_count(v.change_month) + ")";
				auto const lw = sans(state, line, fx, y0 + 90.f, 13, tone, true);
				if(std::abs(rate) > 0.00005f)
					arrow(state, fx - lw - 10.f, y0 + 90.f, rate > 0.f, tone);
			} else {
				sans(state, localized(state, "katechon_pop_empty", "Nobody lives here."), fx, y0 + 64.f, 15, dip_faint, true);
			}
			fill(state, left, body_top - 1.f, right - left, 1.f, dip_bronze, 0.45f);
		}

		// ---- the body, which scrolls under the head ----
		clip_top = body_top;
		clip_bottom = body_bottom;
		{
			ogl::scissor_box clip(state, int32_t(x0 + 2.f), int32_t(body_top), int32_t(pw - 4.f), int32_t(body_bottom - body_top));
			auto y = body_top + 20.f - overview_scroll;
			auto const anchor = [&](size_t i, float at) {
				overview_anchor[i] = std::max(0.f, at + overview_scroll - body_top - 12.f);
			};
			if(v.units <= 0.f) {
				sans(state, localized(state, "katechon_ppl_nobody", "Nobody lives here, so there is nothing to show."), left, y + 12.f, 15, dip_faint);
				y += 40.f;
			} else {
				y = overview_cards(state, left, right, y, hovering, mx, my);
				y += 26.f;
				// As many columns as the width takes, one under the other when it takes none.
				auto const columns = inner >= 1140.f ? 3 : (inner >= 740.f ? 2 : 1);
				constexpr float gap = 36.f;
				auto const col_w = (inner - gap * float(columns - 1)) / float(columns);
				// the course of the number, and what makes it
				anchor(0, y);
				if(columns >= 2) {
					auto const split = std::round(left + (inner - gap) * 0.56f);
					auto const a = overview_trend(state, left, split, y);
					auto const b = overview_drivers(state, split + gap, right, y);
					y = std::max(a, b);
				} else {
					y = overview_trend(state, left, right, y) + 20.f;
					y = overview_drivers(state, left, right, y);
				}
				y += 30.f;
				// the five readings, as many in a row as the width takes
				using block = float (map_hud_element::*)(sys::state&, float, float, float, bool, float, float);
				static constexpr std::array<std::pair<block, size_t>, 5> blocks{ std::pair<block, size_t>{ &map_hud_element::overview_labour, 1 },
					std::pair<block, size_t>{ &map_hud_element::overview_living, 2 }, std::pair<block, size_t>{ &map_hud_element::overview_movement, 3 },
					std::pair<block, size_t>{ &map_hud_element::overview_identity, 4 }, std::pair<block, size_t>{ &map_hud_element::overview_mind, 5 } };
				for(size_t i = 0; i < blocks.size(); i += size_t(columns)) {
					auto row_end = y;
					for(size_t c = 0; c < size_t(columns) && i + c < blocks.size(); ++c) {
						auto const bx = left + float(c) * (col_w + gap);
						anchor(blocks[i + c].second, y);
						row_end = std::max(row_end, (this->*blocks[i + c].first)(state, bx, bx + col_w, y, hovering, mx, my));
					}
					y = row_end + 30.f;
				}
				y = overview_regions(state, left, right, y, hovering, mx, my);
				y += 12.f;
				sans(state, localized(state, "katechon_ppl_source", "Every figure is the running simulation's own. A dash marks what it does not keep."), left, y + 8.f, 11, dip_faint);
				y += 30.f;
			}
			overview_content_h = y + overview_scroll - body_top;
		}

		// How far down the page is.
		if(overview_content_h > overview_view_h + 1.f) {
			auto const track_x = x0 + pw - 14.f;
			auto const track_h = body_bottom - body_top - 8.f;
			fill(state, track_x, body_top + 4.f, 3.f, track_h, dip_rule, 1.f);
			auto const thumb_h = std::max(24.f, track_h * overview_view_h / overview_content_h);
			auto const thumb_y = body_top + 4.f + (track_h - thumb_h) * overview_scroll / (overview_content_h - overview_view_h);
			fill(state, track_x, thumb_y, 3.f, thumb_h, dip_bronze, 0.9f);
		}
	}

	// Four figures that answer the first questions, each a door to its part of the page.
	float overview_cards(sys::state& state, float left, float right, float y, bool hovering, float mx, float my) {
		auto const& v = population;
		struct card {
			std::string label;
			icon image;
			std::string value;
			ogl::color3f colour;
			std::string caption;
			std::string foot;
			ogl::color3f foot_colour;
			std::string about;
			int32_t anchor;
		};
		std::array<card, 4> cards;
		{
			auto const density = metric_value(state, v.nation, v.province, population_metric::density);
			auto foot = v.area > 0.f ? filled(localized(state, "katechon_ppl_land", "%1 km² of land, roughly"), text::prettify(int64_t(v.area))) : std::string();
			if(v.part_of_country >= 0.f)
				foot = filled(localized(state, "katechon_ppl_of_country", "%1 of the country"), text::format_percentage(v.part_of_country, 1));
			cards[0] = card{ localized(state, "katechon_ppl_c_demography", "Demography"), icon::person, metric_text(state, population_metric::density, density), density ? dip_name : dip_faint,
				localized(state, "katechon_ppl_c_density", "people to the square kilometre"), foot, dip_muted, metric_about(state, population_metric::density), 0 };
		}
		{
			auto const known = v.wage_share >= 0.f;
			cards[1] = card{ localized(state, "katechon_ppl_c_labour", "Labour"), icon::work, known ? share_text(v.wage_share) : localized(state, "katechon_ppl_no_data", "No data"), known ? dip_name : dip_faint,
				localized(state, "katechon_ppl_c_wage", "of workers under a wage contract"),
				v.own_share >= 0.f ? filled(localized(state, "katechon_ppl_c_own", "%1 work their own land and workshops"), share_text(v.own_share)) : localized(state, "katechon_ppl_c_own_none", "Household work is not recorded here"),
				dip_muted, metric_about(state, population_metric::work), 1 };
		}
		{
			auto const rate = metric_value(state, v.nation, v.province, population_metric::migration);
			auto const moving = rate && std::abs(*rate) >= 0.05f;
			auto const foot = v.of_province ? filled(localized(state, "katechon_ppl_c_moves", "%1 from other regions, %2 from abroad"), signed_count(v.internal_month), signed_count(v.external_month))
				: filled(localized(state, "katechon_ppl_c_abroad", "%1 across the border, this month's estimate"), signed_count(v.external_month));
			cards[2] = card{ localized(state, "katechon_ppl_c_migration", "Migration"), icon::movement, moving ? metric_text(state, population_metric::migration, rate) : localized(state, "katechon_ppl_c_still", "No movement"),
				moving ? tone_colour(metric_tone(population_metric::migration, *rate)) : dip_faint,
				moving ? localized(state, "katechon_ppl_c_net", "net arrivals to the thousand, a month") : localized(state, "katechon_ppl_c_none_yet", "none recorded this month"),
				foot, dip_muted, metric_about(state, population_metric::migration), 3 };
		}
		{
			auto const known = v.life >= 0.f;
			cards[3] = card{ localized(state, "katechon_ppl_c_living", "Living standards"), icon::society, known ? share_text(v.life) : localized(state, "katechon_ppl_no_data", "No data"),
				known ? tone_colour(metric_tone(population_metric::needs, v.life)) : dip_faint, localized(state, "katechon_ppl_c_basic", "of basic needs met"),
				filled(localized(state, "katechon_ppl_c_more", "everyday %1  ·  luxury %2"), share_text(v.everyday), share_text(v.luxury)), dip_muted, metric_about(state, population_metric::needs), 2 };
		}
		auto const columns = right - left >= 1000.f ? 4 : 2;
		constexpr float gap = 16.f;
		constexpr float card_h = 124.f;
		auto const card_w = (right - left - gap * float(columns - 1)) / float(columns);
		for(size_t i = 0; i < cards.size(); ++i) {
			auto const& c = cards[i];
			auto const cx = left + float(i % size_t(columns)) * (card_w + gap);
			auto const cy = y + float(i / size_t(columns)) * (card_h + gap);
			hit_target t{ cx, cy, card_w, card_h, action::population_scroll_to, nullptr, c.anchor };
			auto const hovered = pop_over(t, hovering, mx, my);
			sheet(state, cx, cy, card_w, card_h, pop_tile, 0.92f);
			frame(state, cx, cy, card_w, card_h, hovered ? dip_bronze : dip_rule, hovered ? 0.9f : 1.f);
			sans(state, upper(c.label), cx + 18.f, cy + 22.f, 10, dip_bronze, false, 0.16f);
			draw_icon(state, c.image, cx + card_w - 24.f, cy + 22.f, 18.f, hovered ? gold : dip_bronze);
			sans(state, sans_fit(state, c.value, 32, card_w - 36.f, 0.f), cx + 18.f, cy + 56.f, 32, c.colour, false, 0.f);
			sans(state, sans_fit(state, c.caption, 12, card_w - 36.f), cx + 18.f, cy + 84.f, 12, dip_muted);
			sans(state, sans_fit(state, c.foot, 12, card_w - 36.f), cx + 18.f, cy + 104.f, 12, c.foot_colour);
			pop_target(t);
			if(hovered)
				hint(c.about, t);
		}
		return y + float((cards.size() + size_t(columns) - 1) / size_t(columns)) * (card_h + gap) - gap;
	}

	float overview_trend(sys::state& state, float left, float right, float y) {
		auto const& v = population;
		std::string aside;
		if(v.history.size() >= 2 && v.history.front() > 0.f) {
			auto const change = v.history.back() / v.history.front() - 1.f;
			aside = filled(localized(state, "katechon_ppl_over_days", "%1 over the period"), signed_percent(change));
		}
		y = pop_heading(state, localized(state, "katechon_ppl_trend", "Population, the last 30 days"), aside, left, right, y);
		constexpr float chart_h = 168.f;
		if(v.history.size() >= 2) {
			trend_chart(state, v.history, left, y + 6.f, right - left, chart_h, true);
			sans(state, localized(state, "katechon_ppl_days_ago", "30 days ago"), left, y + chart_h + 20.f, 11, dip_faint);
			sans(state, localized(state, "katechon_ppl_today", "today"), right, y + chart_h + 20.f, 11, dip_faint, true);
		} else {
			sheet(state, left, y + 6.f, right - left, chart_h, pop_tile, 0.6f);
			auto const why = v.of_province || (v.nation && v.nation != state.local_player_nation) ? localized(state, "katechon_ppl_trend_own_only", "The game keeps this record for your own country only.")
				: localized(state, "katechon_ppl_trend_filling", "The record fills as the days pass.");
			auto const tw = sans_w(state, why, 13);
			sans(state, why, left + (right - left - tw) * 0.5f, y + 6.f + chart_h * 0.5f, 13, dip_faint);
		}
		return y + chart_h + 30.f;
	}

	// What makes the month's change, cause by cause, on one scale.
	float overview_drivers(sys::state& state, float left, float right, float y) {
		auto const& v = population;
		y = pop_heading(state, localized(state, "katechon_ppl_drivers", "Why it changes, a month"), std::string(), left, right, y);
		if(!v.drivers_known) {
			sans(state, localized(state, "katechon_ppl_no_record", "The simulation keeps no record of this here."), left, y + 12.f, 13, dip_faint);
			return y + 34.f;
		}
		struct driver {
			std::string label;
			float amount;
		};
		std::vector<driver> rows{ driver{ localized(state, "katechon_ppl_d_natural", "Births over deaths"), v.natural },
			driver{ localized(state, "katechon_ppl_d_hunger", "Hunger"), -v.starvation },
			driver{ localized(state, "katechon_ppl_d_crowding", "Crowded housing"), -v.overcrowding },
			driver{ localized(state, "katechon_ppl_d_transition", "Smaller families"), -v.transition } };
		if(!v.of_province)
			rows.push_back(driver{ localized(state, "katechon_ppl_d_army", "Losses in the army"), -v.army });
		if(v.of_province)
			rows.push_back(driver{ localized(state, "katechon_ppl_d_internal", "From other regions"), v.internal_month });
		rows.push_back(driver{ v.external_estimated ? localized(state, "katechon_ppl_d_abroad_estimate", "From abroad (estimate)") : localized(state, "katechon_ppl_d_abroad", "From abroad"), v.external_month });
		auto largest = std::abs(v.change_month);
		for(auto const& r : rows)
			largest = std::max(largest, std::abs(r.amount));
		auto const label_w = std::min(170.f, (right - left) * 0.36f);
		constexpr float value_w = 92.f;
		auto const bar_left = left + label_w + 8.f;
		auto const bar_right = right - value_w - 8.f;
		auto const zero = std::round((bar_left + bar_right) * 0.5f);
		auto const half = (bar_right - bar_left) * 0.5f;
		auto const row = [&](std::string const& label, float amount, bool total) {
			auto const tone = std::abs(amount) * people_per_unit < 0.5f ? dip_faint : (amount > 0.f ? dip_yes : dip_no);
			sans(state, sans_fit(state, label, total ? 14 : 13, label_w), left, y + 11.f, total ? 14 : 13, total ? dip_text : dip_muted);
			if(largest > 0.f && std::abs(amount) > 0.f) {
				auto const bw = std::max(2.f, std::round(half * std::abs(amount) / largest));
				fill(state, amount > 0.f ? zero : zero - bw, y + 5.f, bw, 12.f, tone, total ? 1.f : 0.80f);
			}
			sans(state, signed_count(amount), right, y + 11.f, total ? 14 : 13, tone, true);
			y += total ? 28.f : 26.f;
		};
		auto const top = y;
		for(auto const& r : rows)
			row(r.label, r.amount, false);
		fill(state, zero, top, 1.f, y - top, dip_text, 0.35f);
		fill(state, left, y + 2.f, right - left, 1.f, dip_rule, 1.f);
		y += 6.f;
		row(localized(state, "katechon_ppl_d_net", "Net change"), v.change_month, true);
		// The cause that weighs most, said plainly.
		driver const* heaviest = nullptr;
		for(auto const& r : rows) {
			if(!heaviest || std::abs(r.amount) > std::abs(heaviest->amount))
				heaviest = &r;
		}
		if(heaviest && std::abs(heaviest->amount) * people_per_unit >= 0.5f) {
			auto const sentence = filled(localized(state, "katechon_ppl_d_main", "Weighs most: %1, %2 a month."), heaviest->label, signed_count(heaviest->amount));
			y = sans_paragraph(state, sentence, left, right, y + 4.f, 13, dip_text, 2, true);
		}
		return y + 4.f;
	}

	float overview_labour(sys::state& state, float left, float right, float y, bool hovering, float mx, float my) {
		auto const& v = population;
		y = pop_heading(state, localized(state, "katechon_ppl_labour", "How people make a living"), std::string(), left, right, y);
		if(v.wage_share < 0.f && v.own_share < 0.f) {
			sans(state, localized(state, "katechon_ppl_no_record", "The simulation keeps no record of this here."), left, y + 12.f, 13, dip_faint);
			return y + 34.f;
		}
		auto const wage = std::max(0.f, v.wage_share);
		auto const own = std::max(0.f, v.own_share);
		auto const other = v.own_share >= 0.f && v.wage_share >= 0.f ? std::max(0.f, 1.f - wage - own) : -1.f;
		parts_bar(state, left, y + 2.f, right - left, 12.f, { { own, pop_own }, { wage, pop_wage }, { std::max(0.f, other), pop_other } });
		y += 24.f;
		auto const workforce = v.units / persons_per_unit;
		auto const legend = [&](ogl::color3f colour, std::string const& label, float part, std::string const& about) {
			fill(state, left, y + 6.f, 10.f, 10.f, colour, 1.f);
			hit_target t{ left - 4.f, y, right - left + 8.f, 24.f, action::none };
			auto const value = part < 0.f ? std::string("—") : share_text(part) + "   " + people(workforce * part);
			auto const vw = sans(state, value, right, y + 11.f, 13, part < 0.f ? dip_faint : dip_text, true);
			sans(state, sans_fit(state, label, 13, right - left - vw - 30.f), left + 18.f, y + 11.f, 13, dip_muted);
			if(pop_over(t, hovering, mx, my))
				hint(about, t);
			y += 24.f;
		};
		legend(pop_own, localized(state, "katechon_ppl_l_own", "Own land and workshops"), v.own_share,
			localized(state, "katechon_ppl_l_own_about", "Households that work their own farms and crafts, unpaid, and live off what they make."));
		legend(pop_wage, localized(state, "katechon_ppl_l_wage", "For wages"), v.wage_share,
			localized(state, "katechon_ppl_l_wage_about", "Workers hired by firms and the state under a contract."));
		legend(pop_other, localized(state, "katechon_ppl_l_other", "Army, bonded, between jobs"), other,
			localized(state, "katechon_ppl_l_other_about", "Everyone else of working age: in uniform, in bondage, or looking for work."));
		if(v.job_access >= 0.f) {
			y += 6.f;
			y = share_row(state, localized(state, "katechon_ppl_job_access", "Labour on offer that found an employer"), v.job_access, pop_wage, left, right, y);
		}
		if(!v.occupations.empty()) {
			y += 8.f;
			sans(state, upper(localized(state, "katechon_ppl_trades", "Occupations")), left, y + 8.f, 10, dip_bronze, false, 0.14f);
			y += 24.f;
			for(auto const& o : v.occupations)
				y = share_row(state, o.label, o.part, dip_bronze, left, right, y);
		}
		return y;
	}

	float overview_living(sys::state& state, float left, float right, float y, bool, float, float) {
		auto const& v = population;
		y = pop_heading(state, localized(state, "katechon_ppl_living", "Social structure and living standards"), std::string(), left, right, y);
		auto const strata = v.poor + v.middle + v.rich;
		if(strata <= 0.f) {
			sans(state, localized(state, "katechon_ppl_no_record", "The simulation keeps no record of this here."), left, y + 12.f, 13, dip_faint);
			return y + 34.f;
		}
		parts_bar(state, left, y + 2.f, right - left, 12.f, { { v.poor / strata, pop_poor }, { v.middle / strata, pop_middle }, { v.rich / strata, pop_rich } });
		y += 24.f;
		std::array<std::string, 3> const names{ localized(state, "katechon_pop_poor", "Poor"), localized(state, "katechon_pop_middle", "Middle"), localized(state, "katechon_pop_rich", "Rich") };
		std::array<float, 3> const sizes{ v.poor, v.middle, v.rich };
		std::array<ogl::color3f, 3> const colours{ pop_poor, pop_middle, pop_rich };
		{
			auto const third = (right - left) / 3.f;
			for(size_t i = 0; i < 3; ++i) {
				auto const cx = left + float(i) * third;
				fill(state, cx, y + 6.f, 10.f, 10.f, colours[i], 1.f);
				sans(state, names[i], cx + 16.f, y + 11.f, 13, dip_muted);
				sans(state, share_text(sizes[i] / strata), cx + 16.f, y + 30.f, 16, dip_text);
			}
			y += 50.f;
		}
		// What each class obtains of what it needs: a row each, a column for each kind of need.
		sans(state, upper(localized(state, "katechon_ppl_needs_by_class", "Needs met, by class")), left, y + 8.f, 10, dip_bronze, false, 0.14f);
		y += 22.f;
		auto const label_w = std::min(110.f, (right - left) * 0.28f);
		auto const cell_w = (right - left - label_w) / 3.f;
		std::array<std::string, 3> const kinds{ localized(state, "katechon_ppl_basic", "Basic"), localized(state, "katechon_ppl_everyday", "Everyday"), localized(state, "katechon_ppl_luxury", "Luxury") };
		for(size_t k = 0; k < 3; ++k) {
			auto const tw = sans_w(state, kinds[k], 11);
			sans(state, kinds[k], left + label_w + float(k) * cell_w + (cell_w - tw) * 0.5f, y + 8.f, 11, dip_faint);
		}
		y += 18.f;
		auto const cell = [&](float part, float cx, bool strong) {
			if(part >= 0.f)
				fill(state, cx + 2.f, y + 1.f, cell_w - 4.f, 24.f, mix(dip_no, dip_yes, std::clamp(part, 0.f, 1.f)), strong ? 0.42f : 0.28f);
			else
				fill(state, cx + 2.f, y + 1.f, cell_w - 4.f, 24.f, pop_tile, 0.8f);
			auto const value = share_text(part);
			auto const tw = sans_w(state, value, strong ? 14 : 13);
			sans(state, value, cx + (cell_w - tw) * 0.5f, y + 13.f, strong ? 14 : 13, part < 0.f ? dip_faint : dip_text);
		};
		for(size_t s = 0; s < 3; ++s) {
			sans(state, names[s], left, y + 13.f, 13, sizes[s] > 0.f ? dip_muted : dip_faint);
			for(size_t k = 0; k < 3; ++k)
				cell(sizes[s] > 0.f ? v.strata_needs[s][k] : -1.f, left + label_w + float(k) * cell_w, false);
			y += 28.f;
		}
		sans(state, localized(state, "katechon_ppl_everyone", "Everyone"), left, y + 13.f, 14, dip_text);
		cell(v.life, left + label_w, true);
		cell(v.everyday, left + label_w + cell_w, true);
		cell(v.luxury, left + label_w + 2.f * cell_w, true);
		y += 34.f;
		if(v.life >= 0.f && v.life < 0.5f)
			y = sans_paragraph(state, localized(state, "katechon_ppl_hunger_note", "Under half of basic needs met: people go hungry, and hunger takes lives."), left, right, y, 12, dip_no, 2, true);
		return y;
	}

	float overview_movement(sys::state& state, float left, float right, float y, bool hovering, float mx, float my) {
		auto const& v = population;
		y = pop_heading(state, localized(state, "katechon_ppl_movement", "Migration, a month"), std::string(), left, right, y);
		{
			auto const half = (right - left - 16.f) * 0.5f;
			auto const figure = [&](float x, std::string const& label, float amount, std::string const& note) {
				sans(state, sans_fit(state, label, 12, half), x, y + 8.f, 12, dip_muted);
				auto const tone = std::abs(amount) * people_per_unit < 0.5f ? dip_faint : (amount > 0.f ? dip_yes : dip_no);
				sans(state, signed_count(amount), x, y + 32.f, 22, tone, false, 0.f);
				sans(state, sans_fit(state, note, 11, half), x, y + 52.f, 11, dip_faint);
			};
			if(v.of_province)
				figure(left, localized(state, "katechon_ppl_between_regions", "From other regions"), v.internal_month, localized(state, "katechon_ppl_net", "net"));
			else
				figure(left, localized(state, "katechon_ppl_relocating", "Moving between regions"), v.relocations, localized(state, "katechon_ppl_relocating_note", "no change to the total"));
			figure(left + half + 16.f, localized(state, "katechon_ppl_across", "Across the border"), v.external_month,
				v.external_estimated ? localized(state, "katechon_ppl_estimate", "this month's estimate") : localized(state, "katechon_ppl_net", "net"));
			y += 68.f;
		}
		auto const region_rows = [&](std::string const& label, std::vector<population_view::region> const& rows) {
			if(rows.empty())
				return;
			sans(state, upper(label), left, y + 8.f, 10, dip_bronze, false, 0.14f);
			y += 22.f;
			for(auto const& r : rows) {
				hit_target t{ left - 4.f, y, right - left + 8.f, 24.f, action::population_region, nullptr, region_value(r.id) };
				auto const hovered = pop_over(t, hovering, mx, my);
				if(hovered)
					fill(state, t.x, t.y, t.w, t.h, dip_bronze, 0.10f);
				auto const vw = sans(state, signed_count(r.moved), right, y + 12.f, 13, r.moved > 0.f ? dip_yes : dip_no, true);
				sans(state, sans_fit(state, r.name, 13, right - left - vw - 12.f), left, y + 12.f, 13, hovered ? dip_name : dip_text);
				pop_target(t);
				y += 24.f;
			}
			y += 6.f;
		};
		if(!v.of_province) {
			region_rows(localized(state, "katechon_pop_gaining", "Gaining people"), v.gaining);
			region_rows(localized(state, "katechon_pop_losing", "Losing people"), v.losing);
		}
		if(!v.abroad.empty()) {
			sans(state, upper(localized(state, "katechon_ppl_abroad_with", "Abroad: where to and where from")), left, y + 8.f, 10, dip_bronze, false, 0.14f);
			y += 22.f;
			for(auto const& p : v.abroad) {
				dip_flag(state, p.nation, left, y + 6.f, 18.f, 12.f);
				auto const vw = sans(state, signed_count(p.amount), right, y + 12.f, 13, p.amount > 0.f ? dip_yes : dip_no, true);
				sans(state, sans_fit(state, nation_name(state, p.nation), 13, right - left - vw - 40.f), left + 26.f, y + 12.f, 13, dip_text);
				y += 24.f;
			}
		}
		if(v.gaining.empty() && v.losing.empty() && v.abroad.empty() && std::abs(v.internal_month) * people_per_unit < 0.5f && std::abs(v.external_month) * people_per_unit < 0.5f)
			y = sans_paragraph(state, localized(state, "katechon_pop_none", "No movement recorded yet."), left, right, y, 13, dip_faint, 2, true);
		return y;
	}

	float overview_identity(sys::state& state, float left, float right, float y, bool, float, float) {
		auto const& v = population;
		y = pop_heading(state, localized(state, "katechon_ppl_identity", "Who they are"), std::string(), left, right, y);
		if(v.cultures.empty() && v.religions.empty()) {
			sans(state, localized(state, "katechon_ppl_no_record", "The simulation keeps no record of this here."), left, y + 12.f, 13, dip_faint);
			return y + 34.f;
		}
		if(v.accepted >= 0.f)
			y = share_row(state, localized(state, "katechon_ppl_accepted", "Of the state's own or accepted cultures"), v.accepted, gold, left, right, y) + 6.f;
		if(!v.cultures.empty()) {
			sans(state, upper(localized(state, "katechon_pop_cultures", "Cultures")), left, y + 8.f, 10, dip_bronze, false, 0.14f);
			y += 24.f;
			for(auto const& c : v.cultures)
				y = share_row(state, c.label, c.part, pop_poor, left, right, y);
		}
		if(!v.religions.empty()) {
			y += 6.f;
			sans(state, upper(localized(state, "katechon_pop_religions", "Religions")), left, y + 8.f, 10, dip_bronze, false, 0.14f);
			y += 24.f;
			for(auto const& r : v.religions)
				y = share_row(state, r.label, r.part, pop_middle, left, right, y);
		}
		return y;
	}

	// What people know, what they have and how they feel about it.
	float overview_mind(sys::state& state, float left, float right, float y, bool hovering, float mx, float my) {
		auto const& v = population;
		y = pop_heading(state, localized(state, "katechon_ppl_mind", "Development and mood"), std::string(), left, right, y);
		auto const row = [&](std::string const& label, float part, ogl::color3f colour, std::string const& about) {
			hit_target t{ left - 4.f, y, right - left + 8.f, 30.f, action::none };
			y = share_row(state, label, part, colour, left, right, y);
			if(pop_over(t, hovering, mx, my))
				hint(about, t);
		};
		row(localized(state, "katechon_ppl_literacy", "Literacy"), v.literacy, pop_wage, metric_about(state, population_metric::literacy));
		row(localized(state, "katechon_ppl_schooling", "Access to schooling"), v.education_access, pop_wage,
			localized(state, "katechon_ppl_schooling_about", "How much of the education people seek they can obtain, paid for or free."));
		row(localized(state, "katechon_ppl_towns", "Living in towns"), v.urbanization, pop_middle,
			localized(state, "katechon_ppl_towns_about", "The share of the people who live in towns and cities."));
		row(localized(state, "katechon_ppl_housing", "Housing to be had in towns"), v.housing_access, pop_middle,
			localized(state, "katechon_ppl_housing_about", "Where towns have a housing market, how much of the housing sought is found. Short housing crowds people and costs births."));
		row(localized(state, "katechon_ppl_development", "Human development"), v.development, pop_own,
			localized(state, "katechon_ppl_development_about", "Housing, literacy and schooling taken together."));
		// militancy and consciousness run from 0 to 10
		auto const mood = [&](std::string const& label, float value, std::string const& about) {
			hit_target t{ left - 4.f, y, right - left + 8.f, 30.f, action::none };
			auto const known = value >= 0.f;
			auto const text_value = known ? text::format_float(value, 1) + " / 10" : std::string("—");
			auto const tone = known && value >= 5.f ? dip_no : dip_text;
			auto const vw = sans(state, text_value, right, y + 8.f, 13, known ? tone : dip_faint, true);
			sans(state, sans_fit(state, label, 13, right - left - vw - 12.f), left, y + 8.f, 13, dip_muted);
			fill(state, left, y + 19.f, right - left, 3.f, ink, 0.45f);
			if(known && value > 0.f)
				fill(state, left, y + 19.f, std::max(2.f, std::round((right - left) * std::min(value / 10.f, 1.f))), 3.f, value >= 5.f ? dip_no : dip_bronze, 1.f);
			if(pop_over(t, hovering, mx, my))
				hint(about, t);
			y += 30.f;
		};
		y += 4.f;
		mood(localized(state, "katechon_ppl_militancy", "Militancy"), v.militancy,
			localized(state, "katechon_ppl_militancy_about", "How ready people are to take up arms against the state. From five on, unrest is near."));
		mood(localized(state, "katechon_ppl_consciousness", "Political consciousness"), v.consciousness,
			localized(state, "katechon_ppl_consciousness_about", "How much people think about politics and want a say in it."));
		return y;
	}

	// The regions, largest first, with what sets them apart; a press studies one.
	float overview_regions(sys::state& state, float left, float right, float y, bool hovering, float mx, float my) {
		auto const& v = population;
		if(v.regions.size() < 2)
			return y;
		auto const country_name = text::get_name_as_string(state, dcon::fatten(state.world, v.nation));
		y = pop_heading(state, filled(localized(state, "katechon_ppl_regions", "The regions of %1"), country_name),
			filled(localized(state, "katechon_ppl_regions_count", "%1 regions, largest first"), std::to_string(v.regions.size())), left, right, y);
		// the columns, from the right: migration, literacy, needs, growth, share, population
		struct column {
			std::string label;
			float w;
		};
		std::array<column, 6> columns{ column{ localized(state, "katechon_ppl_t_people", "People"), 110.f }, column{ localized(state, "katechon_ppl_t_share", "Share"), 76.f },
			column{ localized(state, "katechon_ppl_t_growth", "Growth a month"), 120.f }, column{ localized(state, "katechon_ppl_t_needs", "Basic needs"), 100.f },
			column{ localized(state, "katechon_ppl_t_literacy", "Literacy"), 86.f }, column{ localized(state, "katechon_ppl_t_moved", "Migration ‰"), 104.f } };
		auto narrow = right - left < 900.f;
		std::array<float, 6> edges{};
		{
			auto at = right;
			for(size_t i = columns.size(); i-- > 0;) {
				if(narrow && (i == 1 || i == 4)) {
					edges[i] = -1.f;
					continue;
				}
				edges[i] = at;
				at -= columns[i].w;
			}
		}
		auto const name_right = [&]() {
			auto lowest = right;
			for(auto const e : edges) {
				if(e > 0.f)
					lowest = std::min(lowest, e);
			}
			for(size_t i = 0; i < columns.size(); ++i) {
				if(edges[i] > 0.f && edges[i] == lowest)
					return lowest - columns[i].w - 12.f;
			}
			return right;
		}();
		sans(state, localized(state, "katechon_ppl_t_region", "Region"), left, y + 8.f, 11, dip_faint);
		for(size_t i = 0; i < columns.size(); ++i) {
			if(edges[i] > 0.f)
				sans(state, columns[i].label, edges[i], y + 8.f, 11, dip_faint, true);
		}
		y += 22.f;
		auto const total = std::max(1.f, v.units > 0.f && !v.of_province ? v.units : state.world.nation_get_demographics(v.nation, demographics::total));
		std::vector<population_view::region const*> rows;
		for(size_t i = 0; i < v.regions.size() && rows.size() < 12; ++i)
			rows.push_back(&v.regions[i]);
		if(v.of_province && std::none_of(rows.begin(), rows.end(), [&](auto const* r) { return r->id == v.province; })) {
			for(auto const& r : v.regions) {
				if(r.id == v.province)
					rows.push_back(&r);
			}
		}
		for(auto const* r : rows) {
			hit_target t{ left - 8.f, y, right - left + 16.f, 26.f, action::population_region, nullptr, region_value(r->id) };
			auto const hovered = pop_over(t, hovering, mx, my);
			auto const studied = r->id == v.province;
			if(studied)
				fill(state, t.x, t.y, t.w, t.h, dip_bronze, 0.18f);
			else if(hovered)
				fill(state, t.x, t.y, t.w, t.h, dip_bronze, 0.09f);
			fill(state, left, y + 25.f, right - left, 1.f, dip_rule, 0.7f);
			auto const mid = y + 13.f;
			sans(state, sans_fit(state, r->name, 13, name_right - left), left, mid, 13, studied || hovered ? dip_name : dip_text);
			std::array<std::pair<std::string, ogl::color3f>, 6> cells{
				std::pair{ people(r->units), dip_text },
				std::pair{ text::format_percentage(r->units / total, 1), dip_muted },
				std::pair{ r->units > 0.f ? signed_percent(r->growth / r->units) : std::string("—"), r->growth < 0.f ? dip_no : (r->growth > 0.f ? dip_yes : dip_faint) },
				std::pair{ share_text(r->needs), r->needs < 0.f ? dip_faint : tone_colour(metric_tone(population_metric::needs, r->needs)) },
				std::pair{ share_text(r->literacy), r->literacy < 0.f ? dip_faint : dip_text },
				std::pair{ metric_text(state, population_metric::migration, r->units > 0.f ? r->moved / r->units * 1000.f : 0.f), std::abs(r->moved) * people_per_unit < 0.5f ? dip_faint : (r->moved > 0.f ? dip_yes : dip_no) } };
			for(size_t i = 0; i < cells.size(); ++i) {
				if(edges[i] > 0.f)
					sans(state, cells[i].first, edges[i], mid, 13, cells[i].second, true);
			}
			pop_target(t);
			if(hovered)
				hint(localized(state, "katechon_ppl_t_hint", "Study this region"), t);
			y += 26.f;
		}
		if(v.regions.size() > rows.size())
			sans(state, filled(localized(state, "katechon_ppl_t_more", "and %1 smaller regions"), std::to_string(v.regions.size() - rows.size())), left, y + 12.f, 11, dip_faint);
		return y + 26.f;
	}

	// ---- The land forces ----
	//
	// One panel and the map between them command the army. The map shows the
	// chain of command as far down as the scale allows: groupings from afar,
	// their armies and then their brigades as the camera comes down. The panel
	// says what is chosen: the whole of the forces, a grouping, a formation.

	using force_id = military::land_forces::stable_id;

	force_unit const* unit_of(force_id id) const {
		auto const found = std::lower_bound(forces.units.begin(), forces.units.end(), id, [](force_unit const& u, force_id target) { return u.id < target; });
		return found != forces.units.end() && found->id == id ? &*found : nullptr;
	}

	// Simulated persons, as people.
	std::string troops(uint64_t persons) const {
		return text::prettify(int64_t(double(persons) * double(people_per_unit) / 4.0));
	}

	static std::string days_text(sys::state& state, float days) {
		if(days >= 3650.f)
			return localized(state, "katechon_mil_no_need", "no need");
		return filled(localized(state, "katechon_mil_days", "$1 d"), std::to_string(int32_t(days)));
	}

	std::string echelon_word(sys::state& state, military::land_forces::echelon level, bool brief) {
		static constexpr std::array<std::string_view, 7> full{ "FORCES", "GROUPING", "ARMY", "CORPS", "DIVISION", "BRIGADE", "REGIMENT" };
		static constexpr std::array<std::string_view, 7> abbreviated{ "", "", "A", "C", "D", "B", "R" };
		auto const i = size_t(level);
		return localized(state, std::string(brief ? "katechon_mil_s" : "katechon_mil_e") + std::to_string(i), brief ? abbreviated[i] : full[i]);
	}

	// What a formation is called on the map, where there is room for a word:
	// its number and the mark of its echelon, or the first word of its name.
	std::string tag_of(sys::state& state, std::string const& name, military::land_forces::echelon level) {
		auto const first = name.substr(0, name.find(' '));
		size_t digits = 0;
		while(digits < first.size() && first[digits] >= '0' && first[digits] <= '9')
			++digits;
		auto const roman = !first.empty() && first.find_first_not_of("IVXLC") == std::string::npos;
		if(digits == 0 && !roman)
			return level == military::land_forces::echelon::unspecified ? localized(state, "katechon_mil_tag_army", "Army") : first;
		auto const mark = echelon_word(state, level, true);
		return (roman ? first : first.substr(0, digits)) + (mark.empty() ? "" : " " + mark);
	}

	void gather_forces(sys::state& state) {
		namespace lf = military::land_forces;
		forces = force_view{};
		forces_stale = false;
		if(!lf::initialized(state))
			return;
		auto const player = state.local_player_nation;
		std::vector<force_id> own;
		for(auto const& f : lf::all_formations(state)) {
			if(f.status == lf::formation_status::destroyed)
				continue;
			force_unit u;
			u.id = f.id;
			u.parent = f.parent_id;
			u.owner = f.owner;
			u.own = f.owner == player;
			auto const label = lf::find_label(state, f.id);
			u.level = label ? label->level : lf::echelon::unspecified;
			u.name = label ? localized(state, label->name_key, label->name_key) : localized(state, "katechon_mil_unnamed", "Formation");
			u.tag = tag_of(state, u.name, u.level);
			u.at = state.world.site_get_province_from_site_location(f.location);
			u.position = state.world.province_get_mid_point(u.at);
			u.towards = u.position;
			u.goal = u.position;
			if(auto const march = lf::find_march(state, f.id); march) {
				u.marching = true;
				u.days_left = march->days_remaining;
				u.blocked = march->blocked;
				u.destination = state.world.site_get_province_from_site_location(march->destination);
				u.goal = state.world.province_get_mid_point(u.destination);
				if(march->next_site && march->leg_days > 0) {
					u.towards = state.world.province_get_mid_point(state.world.site_get_province_from_site_location(march->next_site));
					u.leg = float(march->leg_progress) / float(march->leg_days);
				}
			}
			auto const under = lf::subtree(state, f.id);
			u.grouping = under.size() > 1;
			u.total = lf::summarize(state, under);
			if(u.own)
				own.push_back(f.id);
			forces.units.push_back(std::move(u));
		}
		for(auto& u : forces.units) {
			auto parent = u.parent;
			while(parent != 0 && u.depth < 8) {
				auto const above = unit_of(parent);
				if(!above)
					break;
				++u.depth;
				parent = above->parent;
			}
		}
		forces.national = lf::summarize(state, own);
		forces.can_command = governance::command::forces_answer(state, player);
		forces.can_mobilize = player && military::mobilized_regiments_pop_limit(state, player) > 0;
		forces.mobilized = player && state.world.nation_get_is_mobilized(player);

		// What is chosen, taken together: every formation under the chosen ones, once.
		std::erase_if(chosen_formations, [&](force_id id) {
			auto const u = unit_of(id);
			return !u || !u->own;
		});
		std::vector<force_id> focus;
		if(chosen_formations.empty()) {
			focus = own;
		} else {
			for(auto id : chosen_formations) {
				auto const under = lf::subtree(state, id);
				focus.insert(focus.end(), under.begin(), under.end());
			}
			std::sort(focus.begin(), focus.end());
			focus.erase(std::unique(focus.begin(), focus.end()), focus.end());
		}
		forces.focus = lf::summarize(state, focus);
		std::vector<lf::equipment_model> models(lf::all_equipment_models(state).begin(), lf::all_equipment_models(state).end());
		std::sort(models.begin(), models.end(), [](lf::equipment_model const& a, lf::equipment_model const& b) { return a.category < b.category; });
		for(auto const& model : models) {
			uint64_t held = 0;
			uint64_t authorized = 0;
			for(auto id : focus) {
				held += lf::equipment_count(state, id, model.id);
				authorized += lf::equipment_authorization(state, id, model.id);
			}
			if(authorized == 0 && held == 0)
				continue;
			auto name = localized(state, "katechon_mil_eq_" + std::to_string(model.category), "");
			if(name.empty())
				name = text::produce_simple_string(state, state.world.commodity_get_name(model.commodity));
			forces.equipment.push_back(labelled{ std::move(name), text::prettify(int64_t(held)) + " / " + text::prettify(int64_t(authorized)), held * 2 < authorized });
		}
		static constexpr std::array<std::pair<std::string_view, std::string_view>, 3> kinds{ std::pair<std::string_view, std::string_view>{ "katechon_mil_food", "Food" },
			std::pair<std::string_view, std::string_view>{ "katechon_mil_fuel", "Fuel" }, std::pair<std::string_view, std::string_view>{ "katechon_mil_ammunition", "Ammunition" } };
		for(size_t i = 0; i < kinds.size(); ++i) {
			auto const kind = lf::consumable_kind(uint8_t(i));
			auto held = 0.0;
			auto demand = 0.0;
			for(auto id : focus) {
				held += lf::consumable_quantity(state, id, kind);
				demand += lf::daily_consumable_demand(state, id, kind);
			}
			if(demand > 0.0) {
				auto const days = float(held / demand);
				forces.supply.push_back(labelled{ localized(state, kinds[i].first, kinds[i].second), days_text(state, days), days < 3.f });
			}
			auto national_demand = 0.0;
			for(auto id : own)
				national_demand += lf::daily_consumable_demand(state, id, kind);
			if(national_demand > 0.0) {
				auto const days = float(lf::depot_stock(state, player, kind) / national_demand);
				forces.depots.push_back(labelled{ localized(state, kinds[i].first, kinds[i].second), days_text(state, days), days < 30.f });
			}
		}
	}

	void open_military(sys::state& state) {
		if(country_window_open(state))
			state.ui_state.topbar_subwindow->set_visible(state, false);
		close_population(state);
		close_diplomacy(state);
		close_trade(state);
		military_open = true;
		gather_forces(state);
	}

	void close_military() {
		military_open = false;
		awaiting_destination = false;
		military_more = false;
		chosen_formations.clear();
	}

	void choose(sys::state& state, force_id id, bool add) {
		auto const found = std::find(chosen_formations.begin(), chosen_formations.end(), id);
		if(!add) {
			chosen_formations.assign(1, id);
		} else if(found != chosen_formations.end()) {
			chosen_formations.erase(found);
		} else {
			chosen_formations.push_back(id);
		}
		military_more = false;
		forces_stale = true;
	}

	// The formations an order from the panel is addressed to: the chosen ones,
	// or, when nothing is chosen, every formation of the player's that answers
	// to no other.
	std::vector<force_id> addressed() const {
		if(!chosen_formations.empty())
			return chosen_formations;
		std::vector<force_id> result;
		for(auto const& u : forces.units) {
			if(u.own && u.depth == 0)
				result.push_back(u.id);
		}
		return result;
	}

	void order(sys::state& state, command::formation_order_kind kind, dcon::province_id destination) {
		for(auto id : addressed())
			command::give_formation_order(state, state.local_player_nation, id, kind, destination, true);
		forces_stale = true;
	}

	static float smooth(float from, float to, float value) {
		auto const t = std::clamp((value - from) / (to - from), 0.f, 1.f);
		return t * t * (3.f - 2.f * t);
	}

	// How far a formation has stepped out from under its superior at this scale.
	float emerged(sys::state& state, force_unit const& u) const {
		if(u.depth == 0 || std::find(chosen_formations.begin(), chosen_formations.end(), u.id) != chosen_formations.end())
			return 1.f;
		auto const zoom = state.map_state.zoom;
		return u.depth == 1 ? smooth(2.4f, 3.4f, zoom) : smooth(4.6f, 6.2f, zoom);
	}

	static ogl::color3f readiness_colour(float readiness) {
		return readiness >= 0.75f ? good : (readiness >= 0.5f ? gold : alarm);
	}

	// The marks of the echelon, as on a staff map: one for a brigade, more for what stands above it.
	void draw_pips(sys::state& state, military::land_forces::echelon level, float centre, float y, ogl::color3f colour) {
		static constexpr std::array<int32_t, 7> count{ 0, 5, 4, 3, 2, 1, 1 };
		auto const n = count[size_t(level)];
		if(n == 0)
			return;
		auto const span = float(n) * 3.f + float(n - 1) * 2.f;
		fill(state, std::round(centre - span * 0.5f) - 2.f, y - 1.f, span + 4.f, 5.f, ink, 0.85f);
		for(int32_t i = 0; i < n; ++i)
			fill(state, std::round(centre - span * 0.5f) + float(i) * 5.f, y, 3.f, 3.f, colour, 1.f);
	}

	void draw_forces(sys::state& state, float w, float h, bool hovering, float mx, float my) {
		chips.clear();
		ogl::render_alpha_colored_rect(state, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
		// Superiors first: a subordinate steps out from where its superior is drawn.
		std::vector<size_t> sequence(forces.units.size());
		for(size_t i = 0; i < sequence.size(); ++i)
			sequence[i] = i;
		std::stable_sort(sequence.begin(), sequence.end(), [&](size_t a, size_t b) { return forces.units[a].depth < forces.units[b].depth; });
		for(auto i : sequence) {
			auto& u = forces.units[i];
			auto const t = emerged(state, u);
			auto const own_place = u.position + (u.towards - u.position) * u.leg;
			auto const above = unit_of(u.parent);
			u.shown = above ? above->shown + (own_place - above->shown) * t : own_place;
			// A formation quartered with its superior is told by the superior's mark,
			// until one of the two is chosen or the map is close enough to tell them apart.
			auto const is_chosen = [&](force_id id) { return std::find(chosen_formations.begin(), chosen_formations.end(), id) != chosen_formations.end(); };
			u.folded = above && (above->visible || above->folded) && above->at == u.at && !u.marching && state.map_state.zoom < 9.f && !is_chosen(u.id) && !is_chosen(above->id);
			u.visible = t > 0.04f && !u.folded;
		}
		// Where the marching are going, as dotted lines to the place, under every mark.
		for(auto const& u : forces.units) {
			glm::vec2 at;
			glm::vec2 goal;
			if(!u.visible || !u.marching || !u.own || !project(state, u.shown, at) || !project(state, u.goal, goal))
				continue;
			auto const length = glm::distance(at, goal);
			for(auto step = 12.f; step < length; step += 9.f) {
				auto const dot = at + (goal - at) * (step / length);
				fill(state, std::round(dot.x) - 1.f, std::round(dot.y) - 1.f, 3.f, 3.f, ink, 0.8f);
				fill(state, std::round(dot.x) - 1.f, std::round(dot.y) - 1.f, 2.f, 2.f, gold, 1.f);
			}
			for(int32_t k = -3; k <= 3; ++k)
				fill(state, std::round(goal.x) - float(3 - std::abs(k)) - 0.5f, std::round(goal.y) + float(k), float(2 * (3 - std::abs(k)) + 1), 1.f, gold, 1.f);
		}
		for(auto i : sequence) {
			auto const& u = forces.units[i];
			glm::vec2 at;
			if(!u.visible || !project(state, u.shown, at) || at.x < ribbon_w || at.x > w || at.y < top_h || at.y > h)
				continue;
			auto const chosen = std::find(chosen_formations.begin(), chosen_formations.end(), u.id) != chosen_formations.end();
			auto const strength = u.grouping ? troops(u.total.personnel) : std::string();
			auto const tag_w = shaped(state, u.tag, 11, 0.04f).width;
			auto const strength_w = strength.empty() ? 0.f : shaped(state, strength, 10, 0.02f).width + 7.f;
			auto const flag_w = u.own ? 0.f : 19.f;
			rect r{ 0.f, 0.f, std::round(flag_w + tag_w + strength_w + 14.f), 19.f };
			r.x = std::round(at.x - r.w * 0.5f);
			r.y = std::round(at.y - r.h * 0.5f);
			// Formations that share a place are laid out one under another.
			for(int32_t tries = 0; tries < 16; ++tries) {
				auto const taken = std::any_of(chips.begin(), chips.end(), [&](rect const& other) {
					return r.x < other.x + other.w + 2.f && other.x < r.x + r.w + 2.f && r.y < other.y + other.h + 5.f && other.y < r.y + r.h + 5.f;
				});
				if(!taken)
					break;
				r.y += 25.f;
			}
			chips.push_back(r);
			hit_target target{ r.x, r.y, r.w, r.h, action::pick_formation, nullptr, 0, u.id };
			auto const hovered = u.own && hovering && over(target, mx, my);

			fill(state, r.x + 1.f, r.y + 2.f, r.w + 1.f, r.h, ink, 0.45f);
			auto const body = u.own ? (hovered || chosen ? ogl::color3f{ 0.46f, 0.11f, 0.11f } : ogl::color3f{ 0.31f, 0.07f, 0.08f }) : ogl::color3f{ 0.13f, 0.15f, 0.18f };
			sheet(state, r.x, r.y, r.w, r.h, body, u.own ? 0.97f : 0.90f);
			fill(state, r.x, r.y, r.w, 1.f, u.own ? gold : slate, u.own ? 0.75f : 0.45f);
			if(chosen) {
				frame(state, r.x - 2.f, r.y - 2.f, r.w + 4.f, r.h + 4.f, ink, 0.9f);
				frame(state, r.x - 1.f, r.y - 1.f, r.w + 2.f, r.h + 2.f, gold, 1.f);
			}
			draw_pips(state, u.level, r.x + r.w * 0.5f, r.y - 4.f, u.own ? gold : slate);
			auto x = r.x + 7.f;
			if(!u.own) {
				if(auto const handle = flag_of(state, u.owner); handle)
					ogl::render_textured_rect(state, ogl::color_modification::none, x - 2.f, r.y + 4.f, 15.f, 10.f, handle, ui::rotation::upright, false, false);
				x += flag_w;
			}
			draw_text(state, u.tag, x, r.y + 8.f, 11, 0.04f, u.own ? bright : ivory);
			if(!strength.empty())
				draw_text(state, strength, x + tag_w + 7.f, r.y + 8.f, 10, 0.02f, u.own ? ogl::color3f{ 0.95f, 0.80f, 0.52f } : muted);
			// Readiness along the foot of the mark.
			fill(state, r.x, r.y + r.h - 3.f, r.w, 3.f, ink, 0.55f);
			auto const ready = std::clamp(u.total.operational_readiness, 0.f, 1.f);
			auto const tone = readiness_colour(ready);
			fill(state, r.x, r.y + r.h - 2.f, std::round(r.w * ready), 2.f, tone, 1.f);
			if(u.total.short_of_supply > 0)
				fill(state, r.x + r.w - 4.f, r.y - 2.f, 5.f, 5.f, alarm, 1.f);
			if(u.own) {
				targets.push_back(target);
				panels.push_back(r);
				if(hovered)
					hint(u.name + "  ·  " + troops(u.total.personnel), target);
			}
		}
	}

	// A figure with its measure under it.
	void gauge(sys::state& state, float x, float y, float w, std::string const& label, std::string const& value, float part, bool warning) {
		draw_text(state, label, x, y + 6.f, 9, 0.20f, label_tone);
		draw_text(state, value, x, y + 25.f, 17, 0.01f, warning ? alarm : cream);
		fill(state, x, y + 38.f, w, 3.f, ink, 0.45f);
		fill(state, x, y + 38.f, std::round(w * std::clamp(part, 0.f, 1.f)), 3.f, warning ? alarm : readiness_colour(part), 1.f);
	}

	// One formation in a list: its echelon, its name, its strength; readiness as a rule under it.
	void force_row(sys::state& state, force_unit const& u, float left, float right, float y, bool hovering, float mx, float my) {
		hit_target t{ left - 8.f, y, right - left + 16.f, 26.f, action::pick_formation, nullptr, 1, u.id };
		auto const hovered = hovering && over(t, mx, my);
		if(hovered)
			fill(state, t.x, t.y, t.w, t.h - 1.f, cream, 0.07f);
		auto const mark = echelon_word(state, u.level, true);
		auto const strength = troops(u.total.personnel);
		auto const strength_w = shaped(state, strength, 14, 0.02f).width;
		auto at = left;
		if(u.marching) {
			draw_icon(state, icon::chevron_right, at + 4.f, y + 12.f, 9.f, gold);
			at += 13.f;
		}
		draw_text(state, fit(state, u.name, 13, 0.02f, right - at - strength_w - 14.f), at, y + 11.f, 13, 0.02f, hovered ? ogl::color3f{ 1.f, 0.93f, 0.76f } : cream);
		draw_text(state, strength, right, y + 11.f, 14, 0.02f, u.total.short_of_supply > 0 ? alarm : cream, true);
		fill(state, left, y + 22.f, right - left, 2.f, ink, 0.35f);
		auto const ready = std::clamp(u.total.operational_readiness, 0.f, 1.f);
		fill(state, left, y + 22.f, std::round((right - left) * ready), 2.f, readiness_colour(ready), 0.9f);
		targets.push_back(t);
	}

	void draw_military(sys::state& state, float h, bool hovering, float mx, float my) {
		namespace lf = military::land_forces;
		auto const w = dock_w;
		auto const x0 = ribbon_w;
		auto const y0 = top_h + hang;
		auto const left = x0 + 24.f;
		auto const right = x0 + w - 26.f;
		auto const one = chosen_formations.size() == 1 ? unit_of(chosen_formations.front()) : nullptr;
		auto const several = chosen_formations.size() > 1;
		auto const& total = forces.focus;

		// What is listed under the figures: the groupings, what a grouping consists of, or what was chosen.
		std::vector<force_unit const*> listed;
		for(auto const& u : forces.units) {
			if(!u.own)
				continue;
			if(several ? std::find(chosen_formations.begin(), chosen_formations.end(), u.id) != chosen_formations.end() : (one ? u.parent == one->id : u.depth == 0))
				listed.push_back(&u);
		}
		std::stable_sort(listed.begin(), listed.end(), [](force_unit const* a, force_unit const* b) { return a->total.personnel > b->total.personnel; });
		auto const leaf = one && listed.empty();
		auto const overview = !one && !several;

		// Its troubles, one to a line; none is the usual state of affairs.
		std::vector<std::string> troubles;
		if(!forces.can_command)
			troubles.push_back(localized(state, "katechon_mil_no_commander", "The chain of command is broken: orders are not accepted"));
		if(one && one->marching && one->blocked != lf::march_block::none) {
			troubles.push_back(one->blocked == lf::march_block::awaiting_supply ? localized(state, "katechon_mil_wait_supply", "The march waits for a supply column on its way")
				: (one->blocked == lf::march_block::in_combat ? localized(state, "katechon_mil_wait_combat", "The march is held by fighting") : localized(state, "katechon_mil_wait_route", "The way is closed")));
		}
		if(total.short_of_supply > 0)
			troubles.push_back(filled(localized(state, "katechon_mil_short", "Under three days of supply: $1"), std::to_string(total.short_of_supply)));
		if(total.below_strength > 0)
			troubles.push_back(filled(localized(state, "katechon_mil_unready", "Not fit for action: $1"), std::to_string(total.below_strength)));

		constexpr float row_h = 26.f;
		auto const rows_room = [&](float used) { return std::max(0, int32_t((std::min(plate_image_h, h - y0 - plate_margin) - used) / row_h)); };
		auto const facts = leaf ? forces.equipment.size() + forces.supply.size() : size_t(0);
		auto const fixed = 42.f + 64.f + (overview ? 0.f : 24.f) + 56.f + 58.f + float(troubles.size()) * 20.f + (troubles.empty() ? 0.f : 8.f)
			+ ((one && one->marching) ? 30.f : 0.f) + float(facts) * 24.f + (facts ? 14.f : 0.f) + 86.f + (military_more ? 34.f : 0.f) + (overview ? 116.f : 0.f) + 22.f;
		auto const shown_rows = std::min(int32_t(listed.size()), rows_room(fixed + (listed.empty() ? 0.f : 30.f)));
		auto const sheet_h = std::clamp(fixed + (shown_rows > 0 ? 30.f + float(shown_rows) * row_h : 0.f), plate_foot + 60.f, std::min(plate_image_h, h - y0 - plate_margin));
		draw_plate(state, x0, y0, w, sheet_h);

		draw_text(state, overview ? localized(state, "katechon_mil_scope", "LAND FORCES") : (several ? filled(localized(state, "katechon_mil_scope_many", "CHOSEN  ·  $1"), std::to_string(chosen_formations.size())) : echelon_word(state, one->level, false)),
			left, y0 + 28.f, 9, 0.26f, gold);
		{
			hit_target close{ right - 16.f, y0 + 14.f, 28.f, 28.f, action::toggle_military };
			draw_icon(state, icon::close, close.x + 14.f, close.y + 14.f, 11.f, hovering && over(close, mx, my) ? cream : gold);
			targets.push_back(close);
		}
		auto y = y0 + 42.f;

		// Whose forces, or which formation.
		auto const title = overview ? snap.player_name : (several ? localized(state, "katechon_mil_title_many", "Group of formations") : one->name);
		draw_text(state, fit(state, title, 22, 0.01f, right - left), left, y + 18.f, 22, 0.01f, cream);
		if(overview) {
			if(snap.player)
				draw_flag(state, snap.player, left, y + 38.f, 18.f, 12.f);
			draw_text(state, filled(localized(state, "katechon_mil_count", "FORMATIONS  ·  $1"), std::to_string(total.formations)), left + (snap.player ? 27.f : 0.f), y + 44.f, 9, 0.22f, gold);
		} else if(one) {
			draw_text(state, fit(state, capitals_of(text::get_name_as_string(state, one->at)) + (one->grouping ? "  ·  " + filled(localized(state, "katechon_mil_count", "FORMATIONS  ·  $1"), std::to_string(total.formations)) : std::string()),
				9, 0.22f, right - left), left, y + 44.f, 9, 0.22f, gold);
		} else {
			draw_text(state, filled(localized(state, "katechon_mil_count", "FORMATIONS  ·  $1"), std::to_string(total.formations)), left, y + 44.f, 9, 0.22f, gold);
		}
		y += 64.f;
		// One step back up the chain of command.
		if(!overview) {
			auto const above = one ? unit_of(one->parent) : nullptr;
			auto const label = above ? above->name : localized(state, "katechon_mil_all", "All land forces");
			hit_target back{ left - 4.f, y, std::min(right - left, shaped(state, label, 12, 0.04f).width + 24.f), 20.f, action::military_back, nullptr, 0, above ? above->id : force_id(0) };
			auto const hovered = hovering && over(back, mx, my);
			draw_icon(state, icon::chevron_left, left + 4.f, y + 10.f, 10.f, hovered ? cream : label_tone);
			draw_text(state, fit(state, label, 12, 0.04f, right - left - 20.f), left + 14.f, y + 10.f, 12, 0.04f, hovered ? cream : label_tone);
			targets.push_back(back);
			y += 24.f;
		}

		// How many stand in the ranks.
		{
			auto const figure_w = draw_text(state, troops(total.personnel), left - 1.f, y + 26.f, 40, -0.01f, cream);
			draw_text(state, localized(state, "katechon_mil_men", "in the ranks"), left + figure_w + 10.f, y + 20.f, 12, 0.03f, label_tone);
			draw_text(state, filled(localized(state, "katechon_mil_of", "of $1 on establishment"), troops(total.personnel_authorization)), left + figure_w + 10.f, y + 36.f, 12, 0.03f, quiet);
		}
		y += 56.f;

		// Men, equipment, supply: the three things readiness is made of.
		{
			auto const cell = (right - left - 24.f) / 3.f;
			auto const equipped = total.equipment_tonnes_authorized > 0.0 ? float(total.equipment_tonnes / total.equipment_tonnes_authorized) : 1.f;
			gauge(state, left, y, cell, localized(state, "katechon_mil_ready", "READINESS"), text::format_percentage(total.operational_readiness, 0), total.operational_readiness, total.operational_readiness < 0.5f);
			gauge(state, left + cell + 12.f, y, cell, localized(state, "katechon_mil_equipment", "EQUIPMENT"), text::format_percentage(equipped, 0), equipped, equipped < 0.5f);
			gauge(state, left + 2.f * (cell + 12.f), y, cell, localized(state, "katechon_mil_supply", "SUPPLY"), days_text(state, total.supply_days), total.supply_days / 30.f, total.supply_days < 3.f);
		}
		y += 58.f;

		for(auto const& trouble : troubles) {
			fill(state, left, y + 7.f, 4.f, 4.f, alarm, 1.f);
			draw_text(state, fit(state, trouble, 12, 0.02f, right - left - 12.f), left + 12.f, y + 9.f, 12, 0.02f, alarm);
			y += 20.f;
		}
		if(!troubles.empty())
			y += 8.f;

		// The order it is carrying out.
		if(one && one->marching) {
			draw_icon(state, icon::chevron_right, left + 5.f, y + 11.f, 11.f, gold);
			draw_text(state, fit(state, filled(localized(state, "katechon_mil_marching", "On the march to $1"), text::get_name_as_string(state, one->destination)), 13, 0.02f, right - left - 80.f), left + 16.f, y + 11.f, 13, 0.02f, cream);
			draw_text(state, days_text(state, float(one->days_left)), right, y + 11.f, 13, 0.02f, gold, true);
			y += 30.f;
		}

		// A formation that commands no other: what it holds.
		if(leaf) {
			for(auto const& row : forces.equipment) {
				auto const label_w = draw_text(state, row.label, left, y + 11.f, 13, 0.02f, label_tone);
				draw_text(state, fit(state, row.value, 14, 0.02f, right - left - label_w - 12.f), right, y + 11.f, 14, 0.02f, row.warning ? alarm : cream, true);
				y += 24.f;
			}
			for(auto const& row : forces.supply) {
				auto const label_w = draw_text(state, row.label, left, y + 11.f, 13, 0.02f, label_tone);
				draw_text(state, fit(state, row.value, 14, 0.02f, right - left - label_w - 12.f), right, y + 11.f, 14, 0.02f, row.warning ? alarm : cream, true);
				y += 24.f;
			}
			y += 14.f;
		}

		// The chain of command, one level down.
		if(shown_rows > 0) {
			draw_text(state, overview ? localized(state, "katechon_mil_groupings", "GROUPINGS") : (several ? localized(state, "katechon_mil_chosen", "CHOSEN") : localized(state, "katechon_mil_composition", "COMPOSITION")), left, y + 10.f, 9, 0.26f, gold);
			if(int32_t(listed.size()) > shown_rows)
				draw_text(state, filled(localized(state, "katechon_mil_more_rows", "and $1 more"), std::to_string(listed.size() - size_t(shown_rows))), right, y + 10.f, 10, 0.04f, quiet, true);
			fill(state, left, y + 23.f, right - left, 1.f, cream, 0.10f);
			y += 30.f;
			for(int32_t i = 0; i < shown_rows; ++i) {
				force_row(state, *listed[size_t(i)], left, right, y, hovering, mx, my);
				y += row_h;
			}
		}

		// What can be ordered. A march, supply and reinforcement come first.
		y += 12.f;
		{
			auto const third = (right - left - 16.f) / 3.f;
			text_button(state, left, y, third, 26.f, awaiting_destination ? localized(state, "katechon_mil_cancel", "CANCEL") : localized(state, "katechon_mil_march", "MARCH"),
				hit_target{ 0.f, 0.f, 0.f, 0.f, action::military_march }, hovering, mx, my);
			text_button(state, left + third + 8.f, y, third, 26.f, localized(state, "katechon_mil_resupply", "SUPPLY"), hit_target{ 0.f, 0.f, 0.f, 0.f, action::military_order, nullptr, int32_t(command::formation_order_kind::resupply) }, hovering, mx, my);
			text_button(state, left + 2.f * (third + 8.f), y, third, 26.f, localized(state, "katechon_mil_replenish", "REINFORCE"), hit_target{ 0.f, 0.f, 0.f, 0.f, action::military_order, nullptr, int32_t(command::formation_order_kind::replenish) }, hovering, mx, my);
		}
		y += 34.f;
		{
			hit_target more{ left - 4.f, y, shaped(state, localized(state, "katechon_mil_more", "More"), 11, 0.10f).width + 26.f, 20.f, action::military_more };
			auto const hovered = hovering && over(more, mx, my);
			draw_text(state, localized(state, "katechon_mil_more", "More"), left, y + 10.f, 11, 0.10f, hovered ? cream : label_tone);
			draw_icon(state, military_more ? icon::chevron_up : icon::chevron_down, more.x + more.w - 9.f, y + 10.f, 8.f, hovered ? cream : label_tone);
			targets.push_back(more);
			draw_text(state, awaiting_destination ? localized(state, "katechon_mil_pick_place", "Point to the place on the map") : localized(state, "katechon_mil_right_button", "Right button on the map: march"), right, y + 10.f, 10, 0.04f, awaiting_destination ? gold : quiet, true);
		}
		y += 26.f;
		if(military_more) {
			text_button(state, left, y, right - left, 26.f, localized(state, "katechon_mil_halt", "HALT THE MARCH"), hit_target{ 0.f, 0.f, 0.f, 0.f, action::military_order, nullptr, int32_t(command::formation_order_kind::halt) }, hovering, mx, my);
			y += 34.f;
		}

		// Call-up and reserves, and what the depots hold: the forces as a whole.
		if(overview && y + 100.f <= y0 + sheet_h) {
			draw_text(state, localized(state, "katechon_mil_reserves", "CALL-UP AND RESERVES"), left, y + 10.f, 9, 0.26f, gold);
			fill(state, left, y + 23.f, right - left, 1.f, cream, 0.10f);
			y += 28.f;
			auto const vacancies = total.personnel_authorization > total.personnel ? total.personnel_authorization - total.personnel : uint64_t(0);
			auto const label_w = draw_text(state, localized(state, "katechon_mil_vacancies", "Vacancies"), left, y + 10.f, 13, 0.02f, label_tone);
			draw_text(state, troops(vacancies), right, y + 10.f, 14, 0.02f, vacancies * 4 > total.personnel_authorization ? alarm : cream, true);
			(void)label_w;
			y += 22.f;
			if(forces.can_mobilize) {
				hit_target call{ left, y, right - left, 22.f, action::military_mobilize };
				auto const hovered = hovering && over(call, mx, my);
				draw_text(state, forces.mobilized ? localized(state, "katechon_mil_demobilize", "Stand the reserves down") : localized(state, "katechon_mil_mobilize", "Call up the reserves"), left, y + 10.f, 13, 0.02f, hovered ? cream : gold);
				draw_icon(state, icon::chevron_right, right - 5.f, y + 10.f, 9.f, hovered ? cream : gold);
				targets.push_back(call);
				y += 22.f;
			}
			// What the depots hold, in days of the whole army's need.
			y += 4.f;
			auto const cell = (right - left) / 3.f;
			for(size_t i = 0; i < forces.depots.size() && i < 3; ++i) {
				auto const& depot = forces.depots[i];
				draw_text(state, fit(state, capitals_of(depot.label), 9, 0.16f, cell - 6.f), left + float(i) * cell, y + 6.f, 9, 0.16f, label_tone);
				draw_text(state, depot.value, left + float(i) * cell, y + 22.f, 13, 0.02f, depot.warning ? alarm : cream);
			}
			if(forces.depots.empty())
				draw_text(state, localized(state, "katechon_mil_no_depots", "No depots"), left, y + 12.f, 11, 0.04f, quiet);
		}
	}

#include "gui_map_hud_diplomacy.hpp"

#include "gui_map_hud_trade.hpp"

	void draw_navigation(sys::state& state, float w, float h, bool hovering, float mx, float my) {
		struct tool {
			icon image;
			std::string label;
			hit_target target;
			bool lit = false;
			bool starts_group = false;
		};
		auto bottom = h - edge;
		auto* log = state.ui_state.msg_log_window;
		auto const log_open = log && log->is_visible();
		if(log_open)
			bottom = std::min(bottom, float(ui::get_absolute_location(state, *log).y) - 8.f);
		auto* outliner = state.ui_state.outliner_window;

		auto const mode = uint8_t(state.map_state.active_map_mode);
		auto mode_name = localized(state, "mapmode_" + std::to_string(int32_t(mode)), "");
		if(mode_name.empty())
			mode_name = localized(state, "katechon_hud_layers", "LAYERS");
		if(diplomacy_open && diplomacy_layer)
			mode_name = localized(state, "katechon_dip_layer", "Diplomacy");
		if(trade_open && trade_layer) // the trade mode paints the map itself
			mode_name = localized(state, "katechon_trade_layer", "Trade");

		// Grouped by what they are for: finding a place, the books of the
		// state, what is going on, the depth of the map, the game itself.
		std::vector<tool> tools;
		tools.push_back(tool{ icon::capital, localized(state, "katechon_hud_capital", "CAPITAL"), hit_target{ 0.f, 0.f, 0.f, 0.f, action::capital }, false, true });
		if(search_button)
			tools.push_back(tool{ icon::search, localized(state, "katechon_hud_search", "SEARCH"), hit_target{ 0.f, 0.f, 0.f, 0.f, action::press, search_button } });
		if(ledger_button)
			tools.push_back(tool{ icon::ledger, localized(state, "katechon_hud_ledger", "LEDGER"), hit_target{ 0.f, 0.f, 0.f, 0.f, action::press, ledger_button }, false, true });
		if(economy_button)
			tools.push_back(tool{ icon::economy, localized(state, "katechon_hud_economy", "ECONOMY"), hit_target{ 0.f, 0.f, 0.f, 0.f, action::press, economy_button }, false, !ledger_button });
		if(outliner)
			tools.push_back(tool{ icon::outliner, localized(state, "katechon_hud_outliner", "OUTLINER"), hit_target{ 0.f, 0.f, 0.f, 0.f, action::toggle_outliner }, outliner->is_visible(), true });
		if(log)
			tools.push_back(tool{ icon::log, localized(state, "katechon_hud_log", "LOG"), hit_target{ 0.f, 0.f, 0.f, 0.f, action::toggle_log }, log_open, !outliner });
		tools.push_back(tool{ icon::minus, localized(state, "katechon_hud_zoom_out", "ZOOM OUT"), hit_target{ 0.f, 0.f, 0.f, 0.f, action::zoom_out }, false, true });
		tools.push_back(tool{ icon::plus, localized(state, "katechon_hud_zoom_in", "ZOOM IN"), hit_target{ 0.f, 0.f, 0.f, 0.f, action::zoom_in } });
		if(menu_button)
			tools.push_back(tool{ icon::menu, localized(state, "katechon_hud_menu", "MENU"), hit_target{ 0.f, 0.f, 0.f, 0.f, action::press, menu_button }, false, true });

		constexpr float group_gap = 7.f;
		auto const name_w = shaped(state, mode_name, 13, 0.04f).width;
		auto const chip_w = std::round(12.f + 18.f + 9.f + name_w + 8.f + 10.f + 12.f);
		auto strip_w = chip_w;
		for(auto const& t : tools)
			strip_w += tool_w + (t.starts_group ? group_gap : 0.f);

		auto const strip_x = std::round(w - edge - strip_w);
		auto const strip_y = std::round(bottom - tool_h);
		auto const mid = strip_y + tool_h * 0.5f;
		draw_panel(state, strip_x, strip_y, strip_w, tool_h);

		// The map layer in use is named on the switch that changes it.
		{
			hit_target t{ strip_x, strip_y, chip_w, tool_h, action::toggle_layers };
			auto const hovered = hovering && over(t, mx, my);
			sheet(state, t.x + 1.f, t.y + 1.f, t.w - 1.f, t.h - 2.f, layers_open ? ogl::color3f{ 0.270f, 0.212f, 0.112f } : (hovered ? ogl::color3f{ 0.190f, 0.220f, 0.260f } : ogl::color3f{ 0.150f, 0.176f, 0.210f }));
			fill(state, t.x + t.w, t.y + 1.f, 1.f, t.h - 2.f, ink, 0.50f);
			if(layers_open)
				fill(state, t.x + 1.f, t.y + t.h - 3.f, t.w - 1.f, 2.f, gold, 1.f);
			draw_icon(state, icon::layers, strip_x + 12.f + 9.f, mid, 18.f, gold);
			draw_text(state, mode_name, strip_x + 12.f + 18.f + 9.f, mid, 13, 0.04f, bright);
			draw_icon(state, layers_open ? icon::chevron_down : icon::chevron_up, strip_x + chip_w - 12.f - 5.f, mid, 10.f, layers_open || hovered ? bright : slate);
			targets.push_back(t);
			if(hovered && !layers_open)
				hint(localized(state, "katechon_hud_layers_title", "MAP LAYERS"), t);
		}
		auto x = strip_x + chip_w;
		for(auto& t : tools) {
			if(t.starts_group) {
				fill(state, x + (group_gap - 1.f) * 0.5f, strip_y + 8.f, 1.f, tool_h - 16.f, slate, 0.35f);
				x += group_gap;
			}
			t.target.x = x;
			t.target.y = strip_y;
			t.target.w = tool_w;
			t.target.h = tool_h;
			auto const hovered = hovering && over(t.target, mx, my);
			if(hovered)
				sheet(state, x, strip_y + 1.f, tool_w, tool_h - 2.f, ogl::color3f{ 0.190f, 0.220f, 0.260f });
			else if(t.lit)
				fill(state, x, strip_y + 1.f, tool_w, tool_h - 2.f, gold, 0.14f);
			if(t.lit)
				fill(state, x, strip_y + tool_h - 3.f, tool_w, 2.f, gold, 1.f);
			draw_icon(state, t.image, x + tool_w * 0.5f, mid, 18.f, t.lit ? gold : (hovered ? bright : ivory));
			targets.push_back(t.target);
			if(hovered)
				hint(t.label, t.target);
			x += tool_w;
		}

		if(!layers_open)
			return;

		// The layer list opens above the bar.
		constexpr int32_t columns = (layer_count + layer_rows - 1) / layer_rows;
		auto const list_w = float(columns) * layer_col_w + 2.f * 10.f;
		auto const list_h = 40.f + float(layer_rows) * layer_pitch + 10.f;
		auto const list_x = std::max(edge, std::min(strip_x, w - edge - list_w));
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
	~map_hud_element() override {
		if(the_hud == this) {
			the_hud = nullptr;
			sys::diplomacy_screen = nullptr;
			sys::diplomacy_context = nullptr;
		}
	}

	void on_update(sys::state& state) noexcept override {
		refresh(state);
	}

	mouse_probe impl_probe_mouse(sys::state& state, int32_t x, int32_t y, mouse_probe_type type) noexcept override {
		// While a destination is being named the map's presses are the HUD's; its wheel stays the map's.
		if(awaiting_destination && type == mouse_probe_type::click)
			return mouse_probe{ this, xy_pair{ int16_t(x), int16_t(y) } };
		for(auto const& p : panels) {
			if(float(x) >= p.x && float(x) < p.x + p.w && float(y) >= p.y && float(y) < p.y + p.h)
				return mouse_probe{ this, xy_pair{ int16_t(x), int16_t(y) } };
		}
		return mouse_probe{ nullptr, xy_pair{ int16_t(x), int16_t(y) } };
	}

	message_result on_lbutton_down(sys::state& state, int32_t x, int32_t y, sys::key_modifiers mods) noexcept override {
		shift_held = (uint8_t(mods) & uint8_t(sys::key_modifiers::modifiers_shift)) != 0;
		// Later targets are drawn on top, so they are asked first.
		for(size_t i = targets.size(); i-- > 0;) {
			auto const t = targets[i];
			if(over(t, float(x), float(y))) {
				activate(state, t);
				return message_result::consumed;
			}
		}
		if(awaiting_destination) {
			auto const on_a_panel = std::any_of(panels.begin(), panels.end(), [&](rect const& p) { return float(x) >= p.x && float(x) < p.x + p.w && float(y) >= p.y && float(y) < p.y + p.h; });
			if(auto const destination = on_a_panel ? dcon::province_id{} : state.map_state.get_province_under_mouse(state, state.mouse_x_position, state.mouse_y_position, state.x_size, state.y_size); destination) {
				order(state, command::formation_order_kind::march, destination);
				awaiting_destination = false;
				play_click(state);
			}
		}
		return message_result::consumed;
	}

	message_result on_rbutton_down(sys::state& state, int32_t x, int32_t y, sys::key_modifiers mods) noexcept override {
		return message_result::consumed;
	}

	message_result on_scroll(sys::state& state, int32_t x, int32_t y, float amount, sys::key_modifiers mods) noexcept override {
		// The population overview is longer than the window; the wheel runs it up and down.
		if(population_open && population_overview) {
			overview_scroll = std::clamp(overview_scroll - amount * 72.f, 0.f, std::max(0.f, overview_content_h - overview_view_h));
			return message_result::consumed;
		}
		return message_result::consumed;
	}

	message_result on_key_down(sys::state& state, sys::virtual_key key, sys::key_modifiers mods) noexcept override {
		if(key == sys::virtual_key::ESCAPE && layers_open) {
			layers_open = false;
			return message_result::consumed;
		}
		if(key == sys::virtual_key::T && uint8_t(mods) == 0 && state.local_player_nation) {
			if(trade_open)
				close_trade(state);
			else
				open_trade(state);
			play_click(state);
			return message_result::consumed;
		}
		if(key == sys::virtual_key::ESCAPE && trade_open) {
			if(trade_analysis)
				trade_analysis = false; // back to the map
			else if(trade_focus != 0)
				trade_focus = 0; // one step back, to the world
			else
				close_trade(state);
			return message_result::consumed;
		}
		if(key == sys::virtual_key::ESCAPE && population_open) {
			// one step back: from a region to its country, then out
			if(atlas_province) {
				atlas_province = dcon::province_id{};
				overview_scroll = 0.f;
				gather_population(state);
			} else {
				close_population(state);
			}
			return message_result::consumed;
		}
		if(key == sys::virtual_key::ESCAPE && radial_open) {
			radial_open = false;
			return message_result::consumed;
		}
		if(key == sys::virtual_key::ESCAPE && deal_open) {
			deal_open = false;
			review_deed = deed::none;
			deal_gives = 0;
			deal_asks = 0;
			return message_result::consumed;
		}
		if(key == sys::virtual_key::ESCAPE && diplomacy_open && dip_tab != 0) {
			dip_tab = 0;
			return message_result::consumed;
		}
		if(key == sys::virtual_key::ESCAPE && diplomacy_open) {
			if(diplomacy_focus)
				open_diplomacy_panel(state, state.local_player_nation);
			else
				close_diplomacy(state);
			return message_result::consumed;
		}
		if(key == sys::virtual_key::ESCAPE && military_open && (awaiting_destination || !chosen_formations.empty())) {
			if(awaiting_destination)
				awaiting_destination = false;
			else
				chosen_formations.clear();
			forces_stale = true;
			return message_result::consumed;
		}
		return message_result::unseen;
	}

	void render(sys::state& state, int32_t x, int32_t y) noexcept override {
		if(!resolved)
			resolve(state);
		// Beside the map, a region chosen on the map is the region studied.
		if(population_open && !population_overview && state.map_state.selected_province != atlas_seen_province) {
			atlas_seen_province = state.map_state.selected_province;
			if(auto const p = atlas_seen_province; p && p.index() < state.province_definitions.first_sea_province.index() && state.world.province_get_nation_from_province_ownership(p))
				atlas_province = p;
		}
		if(!snap.ready || snap.province != state.map_state.selected_province || snap.player != state.local_player_nation)
			refresh(state);
		if(military_open && forces_stale)
			gather_forces(state);
		if(!qa_diplomacy.empty() && state.local_player_nation) {
			auto target = state.local_player_nation;
			for(auto n : state.world.in_nation) {
				if(nations::int_to_tag(n.get_identity_from_identity_holder().get_identifying_int()) == qa_diplomacy)
					target = n.id;
			}
			auto const deal_was = deal_open;
			open_diplomacy_panel(state, target);
			deal_open = deal_was && target != state.local_player_nation;
			deal_gives = qa_deal_gives;
			deal_asks = qa_deal_asks;
			if(qa_dip_tab >= 0)
				dip_tab = qa_dip_tab;
			if(qa_deal_send)
				send_deal(state);
			if(qa_radial)
				open_radial(state, target);
			qa_diplomacy.clear();
		}
		// A shortcut of the old top bar may still open the old window; the sheet takes its place.
		if(auto* old = state.ui_state.diplomacy_subwindow; old && old->is_visible()) {
			old->set_visible(state, false);
			if(state.ui_state.topbar_subwindow == old)
				state.ui_state.topbar_subwindow = nullptr;
			open_diplomacy_panel(state, diplomacy_focus ? diplomacy_focus : state.local_player_nation);
		}
		if(qa_trade && state.local_player_nation) {
			qa_trade = false;
			open_trade(state);
			trade_analysis = qa_trade_analysis;
		}
		intercept_legacy_trade(state);
		// With the sheet open, a country chosen on the map is the one on the sheet.
		if(diplomacy_open && state.map_state.selected_province != diplomacy_seen_province) {
			diplomacy_seen_province = state.map_state.selected_province;
			if(auto const owner = state.world.province_get_nation_from_province_ownership(diplomacy_seen_province); owner && owner != diplomacy.nation) {
				diplomacy_focus = owner == state.local_player_nation ? dcon::nation_id{} : owner;
				gather_diplomacy(state);
				if(diplomacy_layer)
					paint_relations(state);
			}
		}
		if(military_open && !qa_formation.empty()) {
			// several keys, separated by commas, are chosen together
			chosen_formations.clear();
			for(auto const& label : military::land_forces::all_labels(state)) {
				auto const keys = "," + qa_formation + ",";
				if(keys.find("," + std::string(label.name_key) + ",") != std::string::npos)
					choose(state, label.formation_id, true);
			}
			gather_forces(state);
			if(qa_order > 0 && uint32_t(qa_order) <= state.world.province_size())
				order(state, command::formation_order_kind::march, province::from_map_id(uint16_t(qa_order)));
			qa_formation.clear();
		}

		auto const w = qa_width > 0.f ? std::min(qa_width, width(state)) : width(state);
		auto const h = height(state);
		auto const mx = qa_mouse_x >= 0.f ? qa_mouse_x : float(state.mouse_x_position) / state.user_settings.ui_scale;
		auto const my = qa_mouse_x >= 0.f ? qa_mouse_y : float(state.mouse_y_position) / state.user_settings.ui_scale;
		auto const hovering = qa_mouse_x >= 0.f || state.ui_state.under_mouse == this;
		targets.clear();
		panels.clear();
		tip.clear();
		dock_w = std::round(std::clamp(w * 0.2f, 320.f, 360.f));

		if(qa_zoom > 0.f) {
			state.map_state.zoom = qa_zoom;
			if(qa_province)
				state.map_state.center_map_on_province(state, qa_province);
		}
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
		auto const overview = population_open && population_overview;
		if(!country_window_open(state)) {
			if(!overview)
				draw_map_names(state, w, h);
			auto* details = state.ui_state.province_window;
			if(military_open) {
				draw_forces(state, w, h, hovering, mx, my);
				draw_military(state, h, hovering, mx, my);
			} else if(diplomacy_open) {
				if(diplomacy_layer && --diplomacy_repaint_in <= 0)
					paint_relations(state);
				// the sheet has its own key for what it paints
				for(auto* legend : { state.ui_state.map_gradient_legend, state.ui_state.map_dip_legend }) {
					if(diplomacy_layer && legend && legend->is_visible())
						legend->set_visible(state, false);
				}
				draw_relation_web(state, w, h);
				draw_diplomacy(state, h, hovering, mx, my);
				// the map under the mouse speaks of the country on the sheet; the old province card does not
				if(diplomacy_layer) {
					if(auto* old_tip = state.ui_state.tooltip.get(); old_tip && (state.ui_state.under_mouse == nullptr || state.ui_state.under_mouse == this))
						old_tip->set_visible(state, false);
					if(qa_mouse_x >= 0.f || state.ui_state.under_mouse == nullptr || state.ui_state.under_mouse == this)
						draw_map_hover(state, w, h, mx, my);
				}
			} else if(trade_open) {
				if(trade_layer && --trade_repaint_in <= 0)
					paint_trade(state);
				for(auto* legend : { state.ui_state.map_gradient_legend, state.ui_state.map_dip_legend }) {
					if(trade_layer && legend && legend->is_visible())
						legend->set_visible(state, false);
				}
				draw_trade(state, w, h, hovering, mx, my);
			} else if(population_open) {
				if(population_layer && --repaint_in <= 0)
					paint_layer(state); // the game repaints its own layer when the world moves on
				// The inspector has its own key for what it paints.
				if(auto* legend = state.ui_state.map_gradient_legend; population_layer && legend && legend->is_visible())
					legend->set_visible(state, false);
				draw_population(state, w, h, hovering, mx, my);
			} else if(snap.province && !(details && details->is_visible())) {
				draw_card(state, hovering, mx, my);
			}
			// the overview has put the map aside, and the map's tools with it
			if(!overview)
				draw_navigation(state, w, h, hovering, mx, my);
		}
		draw_ribbon(state, hovering, mx, my);
		draw_top_bar(state, w, hovering, mx, my);
		// What other states put to us, answered here; the old request window is never shown.
		if(auto* old = state.ui_state.request_window; old && old->is_visible())
			old->set_visible(state, false);
		if(!country_window_open(state))
			draw_requests(state, w, hovering, mx, my);
		if(radial_open)
			draw_radial(state, w, h, hovering, mx, my);
		if(!tip.empty())
			dip_tip.clear(); // a hint of one line under the mouse wins
		draw_dip_hint(state, w, h);
		draw_hint(state, w);
		qa_capture(state);
	}
};

}
