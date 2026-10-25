// The trade mode of the KATECHON map HUD. This file is included inside the
// body of ui::map_hud_element (gui_map_hud.hpp) and uses its drawing helpers;
// it is not a header of its own.
//
// Two levels. On the map, a narrow inspector answers how much the country
// trades, with whom, what earns it money and what it cannot do without, and
// the map draws the flows: exports copper, imports blue, wider for more value.
// Choosing a partner or a good, on the sheet or on the map, puts it forward:
// the map then shows only its flows. The full analysis takes the screen for
// the economy itself: performance over time, the structure of the goods, the
// partners, and the dependencies that could hurt. Every figure comes from
// economy/trade_picture.hpp; the old trade window stays one press away. It is
// drawn in the atlas of gui_map_hud_atlas.hpp.

bool trade_open = false;
bool trade_layer = false; // the mode paints the map until the player picks a layer
bool trade_analysis = false; // the full analysis is up instead of the inspector
bool legacy_trade = false; // the old window was asked for from the mode: let it stay
bool qa_trade = false; // KATECHON_HUD_TRADE=1: open the mode once there is a player
bool qa_trade_analysis = false; // KATECHON_HUD_TRADE=2: and the full analysis
int32_t trade_repaint_in = 0;
economy::trade_picture::picture trade;
dcon::province_id trade_seen_province; // the province chosen on the map when the mode last looked

dcon::commodity_id good_before_trade; // what the old trade window had chosen, given back on closing
bool had_good_before_trade = false;

int32_t trade_span = 30; // the span every figure covers, in days
// What the mode puts forward: 0 the whole trade, above 0 a good (its index + 1), below 0 a partner (-(index + 1)).
int32_t trade_focus = 0;
int32_t lanes_filter = 0; // what the lanes on the map were last gathered for
std::vector<float> good_out_by_nation; // with a good forward: what each country sold of it abroad, by value
std::vector<float> good_in_by_nation;

// The map as an old sea chart: every other country in the parchment of its own
// colour, the partners cooler by their share of our trade, our country in gold.
inline static constexpr ogl::color3f trade_self{ 0.74f, 0.57f, 0.33f };
inline static constexpr ogl::color3f trade_partner{ 0.55f, 0.76f, 0.93f };
inline static constexpr ogl::color3f trade_seller{ 0.80f, 0.42f, 0.30f }; // terracotta: apart from our own gold
inline static constexpr ogl::color3f trade_none{ 0.40f, 0.38f, 0.34f };

element_base* legacy_trade_window() {
	for(size_t i = 0; i < tabs.size(); ++i) {
		if(tabs[i].element == "topbarbutton_trade" && tab_buttons[i])
			return static_cast<topbar_tab_button*>(tab_buttons[i])->topbar_subwindow;
	}
	return nullptr;
}

// ---- the goods ----

// The groups of goods, in the order of assets/katechon_goods_icons.png (tools/katechon_ui/build_trade.py).
enum class goods_glyph : int32_t { grain, food, oil, coal, metal, timber, textile, machinery, electronics, chemicals, arms, ships, furniture, count };
GLuint goods_icons = 0;
bool trade_art_read = false;
std::vector<int8_t> glyph_of_good; // by commodity index

void read_trade_art(sys::state& state) {
	trade_art_read = true;
	auto root = simple_fs::get_root(state.common_fs);
	auto assets = simple_fs::open_directory(root, NATIVE("assets"));
	goods_icons = ogl::make_gl_texture(assets, NATIVE("katechon_goods_icons.png"));
	if(goods_icons) {
		glBindTexture(GL_TEXTURE_2D, goods_icons);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glBindTexture(GL_TEXTURE_2D, 0);
	}
}

void map_glyphs(sys::state& state) {
	struct group {
		std::string_view key;
		goods_glyph glyph;
	};
	static constexpr std::array<group, 48> groups{ group{ "grain", goods_glyph::grain }, group{ "canned_food", goods_glyph::food }, group{ "cattle", goods_glyph::food },
		group{ "fish", goods_glyph::food }, group{ "fruit", goods_glyph::food }, group{ "tea", goods_glyph::food }, group{ "coffee", goods_glyph::food },
		group{ "tobacco", goods_glyph::food }, group{ "wine", goods_glyph::food }, group{ "liquor", goods_glyph::food }, group{ "opium", goods_glyph::food },
		group{ "oil", goods_glyph::oil }, group{ "fuel", goods_glyph::oil }, group{ "coal", goods_glyph::coal }, group{ "iron", goods_glyph::metal },
		group{ "steel", goods_glyph::metal }, group{ "precious_metal", goods_glyph::metal }, group{ "sulphur", goods_glyph::chemicals }, group{ "timber", goods_glyph::timber },
		group{ "tropical_wood", goods_glyph::timber }, group{ "lumber", goods_glyph::timber }, group{ "cotton", goods_glyph::textile }, group{ "dye", goods_glyph::textile },
		group{ "wool", goods_glyph::textile }, group{ "silk", goods_glyph::textile }, group{ "fabric", goods_glyph::textile }, group{ "regular_clothes", goods_glyph::textile },
		group{ "luxury_clothes", goods_glyph::textile }, group{ "machine_parts", goods_glyph::machinery }, group{ "automobiles", goods_glyph::machinery },
		group{ "aeroplanes", goods_glyph::machinery }, group{ "barrels", goods_glyph::machinery }, group{ "electric_gear", goods_glyph::electronics },
		group{ "telephones", goods_glyph::electronics }, group{ "radio", goods_glyph::electronics }, group{ "fertilizer", goods_glyph::chemicals },
		group{ "explosives", goods_glyph::chemicals }, group{ "cement", goods_glyph::chemicals }, group{ "glass", goods_glyph::chemicals }, group{ "paper", goods_glyph::chemicals },
		group{ "rubber", goods_glyph::chemicals }, group{ "ammunition", goods_glyph::arms }, group{ "small_arms", goods_glyph::arms }, group{ "artillery", goods_glyph::arms },
		group{ "clipper_convoy", goods_glyph::ships }, group{ "steamer_convoy", goods_glyph::ships }, group{ "furniture", goods_glyph::furniture },
		group{ "luxury_furniture", goods_glyph::furniture } };
	glyph_of_good.assign(state.world.commodity_size(), int8_t(goods_glyph::food));
	for(uint32_t i = 0; i < state.world.commodity_size(); ++i) {
		auto const key = state.to_string_view(state.world.commodity_get_name(dcon::commodity_id{ dcon::commodity_id::value_base_t(i) }));
		for(auto const& g : groups) {
			if(key == g.key)
				glyph_of_good[i] = int8_t(g.glyph);
		}
	}
}

int32_t glyph_for(sys::state& state, dcon::commodity_id c) {
	if(glyph_of_good.size() != state.world.commodity_size())
		map_glyphs(state);
	return c && size_t(c.index()) < glyph_of_good.size() ? int32_t(glyph_of_good[size_t(c.index())]) : int32_t(goods_glyph::food);
}

void good_icon(sys::state& state, dcon::commodity_id c, float cx, float cy, float size, ogl::color3f colour) {
	if(!trade_art_read)
		read_trade_art(state);
	if(goods_icons)
		ogl::render_tinted_subsprite(state, glyph_for(state, c), int32_t(goods_glyph::count), std::round(cx - size * 0.5f), std::round(cy - size * 0.5f), size, size,
			colour.r, colour.g, colour.b, goods_icons, ui::rotation::upright, false, false);
}

std::string goods_name(sys::state& state, dcon::commodity_id c) {
	return text::produce_simple_string(state, state.world.commodity_get_name(c));
}

// 2: people live on it; 1: industry and the army run on it; 0: neither.
int32_t weight_of_good(sys::state& state, dcon::commodity_id c) {
	auto const glyph = goods_glyph(glyph_for(state, c));
	if(state.world.commodity_get_is_life_need(c) || glyph == goods_glyph::grain)
		return 2;
	switch(glyph) {
	case goods_glyph::oil: case goods_glyph::coal: case goods_glyph::metal: case goods_glyph::arms: case goods_glyph::machinery:
		return 1;
	default:
		return 0;
	}
}

// ---- what the figures mean ----

// A good the country could not easily do without: how much of its use comes
// from abroad, and from how few hands.
struct trade_dependency {
	dcon::commodity_id good_id;
	float import_share = 0.f; // dependence: the part of demand our own production does not meet
	float cover = -1.f; // what its own places make of it, against its use
	float imported = 0.f; // value
	dcon::nation_id source;
	float source_share = 0.f; // of the imports of it, by value
	int32_t suppliers = 0;
	int32_t kind = 0; // weight_of_good
	int32_t level = 0; // 0 to watch, 1 high, 2 critical
};
// A partner, and what would go with it.
struct trade_partner_view {
	dcon::nation_id nation;
	float exported = 0.f;
	float imported = 0.f;
	float share = 0.f; // of exports and imports together
	dcon::commodity_id sells; // what we sell them most of
	float sells_value = 0.f;
	dcon::commodity_id buys; // what we buy from them most of
	float buys_value = 0.f;
	dcon::commodity_id leans_on; // the good of ours they supply the most of, against our use of it
	float leans_share = 0.f;
	bool hard_to_replace = false; // and they are most of what we import of it
};
std::vector<trade_dependency> trade_deps;
std::vector<trade_partner_view> trade_partners;
std::vector<int32_t> line_of_good; // index into trade.commodities, -1 for none

economy::trade_picture::commodity_line const* line_for(dcon::commodity_id c) const {
	if(!c || size_t(c.index()) >= line_of_good.size())
		return nullptr;
	auto const i = line_of_good[size_t(c.index())];
	return i < 0 ? nullptr : &trade.commodities[size_t(i)];
}

trade_partner_view const* partner_view(dcon::nation_id n) const {
	for(auto const& p : trade_partners) {
		if(p.nation == n)
			return &p;
	}
	return nullptr;
}

void analyse_trade(sys::state& state) {
	line_of_good.assign(state.world.commodity_size(), -1);
	for(size_t i = 0; i < trade.commodities.size(); ++i)
		line_of_good[size_t(trade.commodities[i].commodity.index())] = int32_t(i);

	trade_deps.clear();
	for(auto const& line : trade.commodities) {
		auto const share = line.dependence();
		if(share < 0.25f || line.imported <= 0.f)
			continue;
		auto const kind = weight_of_good(state, line.commodity);
		if(kind == 0 && trade.imported > 0.f && line.imported < 0.01f * trade.imported)
			continue; // too small to matter
		trade_dependency d{ line.commodity, share, line.self_sufficiency(), line.imported, line.main_source, line.main_source_share, line.suppliers, kind, 0 };
		if(share >= 0.75f || (share >= 0.5f && (kind > 0 || line.main_source_share >= 0.7f)))
			d.level = 2;
		else if(share >= 0.5f || line.main_source_share >= 0.7f)
			d.level = 1;
		trade_deps.push_back(d);
	}
	std::stable_sort(trade_deps.begin(), trade_deps.end(), [](trade_dependency const& a, trade_dependency const& b) {
		if(a.level != b.level)
			return a.level > b.level;
		if(a.kind != b.kind)
			return a.kind > b.kind;
		return a.imported > b.imported;
	});

	trade_partners.clear();
	std::vector<int32_t> at(state.world.nation_size(), -1);
	auto const total = trade.exported + trade.imported;
	for(auto const& p : trade.partners) {
		at[size_t(p.nation.index())] = int32_t(trade_partners.size());
		trade_partners.push_back(trade_partner_view{ p.nation, p.exported, p.imported, total > 0.f ? p.total() / total : 0.f });
	}
	for(auto const& part : trade.parts) {
		auto const i = size_t(part.nation.index()) < at.size() ? at[size_t(part.nation.index())] : -1;
		if(i < 0)
			continue;
		auto& v = trade_partners[size_t(i)];
		if(part.outgoing) {
			if(part.value > v.sells_value) {
				v.sells_value = part.value;
				v.sells = part.good;
			}
			continue;
		}
		if(part.value > v.buys_value) {
			v.buys_value = part.value;
			v.buys = part.good;
		}
		if(auto const* line = line_for(part.good); line && line->bought > 0.f) {
			auto const of_use = std::min(1.f, part.quantity / line->bought);
			if(of_use > v.leans_share) {
				v.leans_share = of_use;
				v.leans_on = part.good;
				v.hard_to_replace = of_use >= 0.25f && line->imported > 0.f && part.value / line->imported >= 0.6f;
			}
		}
	}
}

// Until a year's course has three months in it, the course is drawn by the day.
economy::trade_picture::picture trade_days;

void gather_trade(sys::state& state) {
	trade = economy::trade_picture::take(state, state.local_player_nation, trade_span);
	if(trade_span > 30 && trade.exported_series.size() - std::min(trade.first_recorded_bucket, trade.exported_series.size()) < 3)
		trade_days = economy::trade_picture::take(state, state.local_player_nation, 30);
	else
		trade_days = economy::trade_picture::picture{};
	analyse_trade(state);
	index_trade(state);
	gather_insights(state);
	gather_lanes(state);
}

// Called whenever the world moves on; with the mode open it follows the world.
void trade_tick(sys::state& state) {
	if(!trade_open)
		return;
	gather_trade(state);
	if(trade_layer)
		paint_trade(state);
}

// ---- the map ----

// The lanes of the deals crossing a border, for the map: from the capital of
// the region the goods were taken in to the capital of the region they went
// to, by sea between continents and over land within one. Ours in full, of the
// rest of the world only the busiest, faintly. With a partner forward only the
// trade with it; with a good forward the whole world's trade in that good.
void gather_lanes(sys::state& state) {
	namespace kt = map::katechon_trade;
	auto const player = state.local_player_nation;
	auto const capital_of = [&](dcon::site_id site) {
		auto const p = state.world.site_get_province_from_site_location(site);
		auto const zone = p ? state.world.province_get_state_membership(p) : dcon::state_instance_id{};
		auto const capital = zone ? state.world.state_instance_get_capital(zone) : dcon::province_id{};
		return capital ? dcon::province_id(capital) : dcon::province_id(p);
	};
	auto const port_of = [&](dcon::province_id p) {
		if(!p || state.world.province_get_port_to(p))
			return p;
		auto const zone = state.world.province_get_state_membership(p);
		return zone ? province::state_get_coastal_capital(state, zone) : dcon::province_id{};
	};
	std::unordered_map<uint64_t, kt::lane> merged;
	auto const today = int32_t(state.current_date.to_raw_value());
	lanes_filter = trade_focus;
	auto const partner = lanes_filter < 0 ? dcon::nation_id{ dcon::nation_id::value_base_t(-lanes_filter - 1) } : dcon::nation_id{};
	auto const focus_good = lanes_filter > 0 ? dcon::commodity_id{ dcon::commodity_id::value_base_t(lanes_filter - 1) } : dcon::commodity_id{};
	good_out_by_nation.assign(state.world.nation_size(), 0.f);
	good_in_by_nation.assign(state.world.nation_size(), 0.f);
	economy::trade_picture::for_each_deal(state, today - trade_span + 1, [&](economy::trade_picture::deal const& d) {
		auto const from = economy::trade_picture::nation_for_site(state, d.from);
		auto const to = economy::trade_picture::nation_for_site(state, d.to);
		if(!from || !to || from == to)
			return;
		if(focus_good && d.commodity != focus_good)
			return;
		if(partner && ((from != player || to != partner) && (from != partner || to != player)))
			return;
		auto const price = std::isfinite(d.price) && d.price > 0.f ? d.price : economy::trade_picture::unit_price(state, economy::physical::concrete_market::market_for_site(state, d.from), d.commodity);
		auto const value = d.quantity * price;
		if(focus_good) {
			good_out_by_nation[size_t(from.index())] += value;
			good_in_by_nation[size_t(to.index())] += value;
		}
		auto const kind = from == player ? kt::lane_kind::outgoing : (to == player ? kt::lane_kind::incoming : kt::lane_kind::foreign);
		auto a = capital_of(d.from);
		auto b = capital_of(d.to);
		if(!a || !b || a == b)
			return;
		auto const sea = state.world.province_get_continent(a) != state.world.province_get_continent(b);
		if(sea) {
			a = port_of(a);
			b = port_of(b);
			if(!a || !b || a == b)
				return;
		}
		auto const key = (((uint64_t(a.index()) << 32) | uint64_t(b.index())) << 3) | (uint64_t(sea) << 2) | uint64_t(kind);
		auto& l = merged[key];
		l.from = a;
		l.to = b;
		l.sea = sea;
		l.kind = kind;
		l.value += value;
	});
	std::vector<kt::lane> own;
	std::vector<kt::lane> others;
	for(auto const& [k, l] : merged)
		(l.kind == kt::lane_kind::foreign ? others : own).push_back(l);
	auto const busiest = [](kt::lane const& a, kt::lane const& b) { return a.value > b.value; };
	std::sort(own.begin(), own.end(), busiest);
	std::sort(others.begin(), others.end(), busiest);
	if(own.size() > 240)
		own.resize(240);
	if(others.size() > (focus_good ? 240u : 160u))
		others.resize(focus_good ? 240u : 160u);
	kt::lanes = std::move(others);
	kt::lanes.insert(kt::lanes.end(), own.begin(), own.end());
	kt::dirty.store(true, std::memory_order::release);
}

// The countries in the parchment of their own colour; what is forward decides which stand out.
void paint_trade(sys::state& state) {
	auto const player = state.local_player_nation;
	if(!player)
		return;
	auto const province_size = uint32_t(state.world.province_size()) + 1;
	auto const texture_size = province_size + 256 - province_size % 256;
	std::vector<uint32_t> colours(size_t(texture_size) * 2, 0);
	auto const nations = state.world.nation_size();
	auto const by_good = trade_focus > 0 && good_out_by_nation.size() == nations && good_in_by_nation.size() == nations;
	auto const partner = trade_focus < 0 ? size_t(-trade_focus - 1) : size_t(-1);
	auto top = 0.f;
	for(size_t i = 0; i < nations; ++i) {
		if(by_good)
			top = std::max(top, std::abs(good_out_by_nation[i] - good_in_by_nation[i]));
		else if(i < trade.share_by_nation.size())
			top = std::max(top, trade.share_by_nation[i]);
	}
	std::vector<ogl::color3f> by_nation(nations, trade_none);
	for(size_t i = 0; i < nations; ++i) {
		auto const n = dcon::nation_id{ dcon::nation_id::value_base_t(i) };
		auto const political = state.world.nation_get_color(n);
		auto const r = sys::red_from_int(political), g = sys::green_from_int(political), b = sys::blue_from_int(political);
		auto const lum = 0.30f * r + 0.59f * g + 0.11f * b;
		auto const parchment = mix(ogl::color3f{ lum * 1.02f, lum * 0.95f, lum * 0.82f }, ogl::color3f{ r, g, b }, 0.22f);
		auto c = parchment;
		if(by_good) {
			// sellers of the good warm, buyers cool, by how much on balance
			auto const net = good_out_by_nation[i] - good_in_by_nation[i];
			if(top > 0.f && net != 0.f)
				c = mix(parchment, net > 0.f ? trade_seller : trade_partner, 0.30f + 0.45f * std::sqrt(std::min(1.f, std::abs(net) / top)));
		} else if(trade_focus < 0) {
			if(i == partner)
				c = mix(parchment, trade_partner, 0.75f);
		} else {
			auto const share = i < trade.share_by_nation.size() ? trade.share_by_nation[i] : 0.f;
			if(share > 0.f && top > 0.f)
				c = mix(parchment, trade_partner, 0.30f + 0.45f * std::sqrt(std::min(1.f, share / top)));
		}
		by_nation[i] = c;
	}
	by_nation[size_t(player.index())] = trade_self;
	province::for_each_land_province(state, [&](dcon::province_id p) {
		auto const owner = state.world.province_get_nation_from_province_ownership(p);
		auto const c = owner ? by_nation[size_t(owner.index())] : trade_none;
		auto const packed = sys::pack_color(c.r, c.g, c.b);
		auto const i = province::to_map_id(p);
		colours[i] = packed;
		colours[i + texture_size] = packed;
	});
	state.map_state.set_province_color(colours, map_mode::mode::handled_from_outside);
	// The game repaints its own mode every day; with this one it leaves the colours alone,
	// and the map still takes them as data.
	state.map_state.active_map_mode = map_mode::mode::handled_from_outside;
	trade_repaint_in = 30;
}

void open_trade(sys::state& state) {
	close_population(state);
	close_military();
	close_diplomacy(state);
	if(country_window_open(state))
		state.ui_state.topbar_subwindow->set_visible(state, false);
	trade_open = true;
	trade_layer = true;
	trade_analysis = false;
	trade_focus = 0;
	trade_seen_province = state.map_state.selected_province;
	// KATECHON_HUD_TRADE_FOCUS=<good key or country tag> opens the mode with that put forward.
	if(auto const* wanted = std::getenv("KATECHON_HUD_TRADE_FOCUS"); wanted && wanted[0]) {
		for(uint32_t i = 0; i < state.world.commodity_size(); ++i) {
			if(state.to_string_view(state.world.commodity_get_name(dcon::commodity_id{ dcon::commodity_id::value_base_t(i) })) == wanted)
				trade_focus = int32_t(i) + 1;
		}
		for(auto n : state.world.in_nation) {
			if(nations::int_to_tag(n.get_identity_from_identity_holder().get_identifying_int()) == wanted)
				trade_focus = -(int32_t(n.id.index()) + 1);
		}
	}
	// KATECHON_HUD_TRADE_SPAN=365 and KATECHON_HUD_TRADE_CHART=<0..3> open on that span and that course.
	if(auto const* span = std::getenv("KATECHON_HUD_TRADE_SPAN"); span && std::atoi(span) == 365)
		trade_span = 365;
	if(auto const* chart = std::getenv("KATECHON_HUD_TRADE_CHART"); chart && chart[0] >= '0' && chart[0] <= '3')
		trade_chart_mode = chart[0] - '0';
	// what was put forward is also shown in the register; KATECHON_HUD_TRADE_SEARCH=<words> searches it,
	// and KATECHON_HUD_TRADE_COUNTRIES=1 opens the register on the countries
	if(trade_focus != 0)
		trade_choose(trade_focus);
	if(auto const* words = std::getenv("KATECHON_HUD_TRADE_SEARCH"); words && words[0])
		trade_search = words;
	if(std::getenv("KATECHON_HUD_TRADE_COUNTRIES"))
		register_tab = 1;
	// KATECHON_HUD_TRADE_TRENDS=1 opens the analysis on its trends.
	if(auto const* trends = std::getenv("KATECHON_HUD_TRADE_TRENDS"); trends && trends[0] == '1')
		trade_trends = true;
	// paint_trade takes the map over; the bar names the layer Trade.
	for(auto* legend : { state.ui_state.map_gradient_legend, state.ui_state.map_civ_level_legend })
		if(legend)
			legend->set_visible(state, false);
	// The map draws its trade lines only while some good is chosen; the lanes take their place.
	had_good_before_trade = true;
	good_before_trade = state.selected_trade_good;
	if(!state.selected_trade_good && state.world.commodity_size() > 0)
		state.selected_trade_good = dcon::commodity_id{ dcon::commodity_id::value_base_t(0) };
	map::katechon_trade::active = true;
	gather_trade(state);
	paint_trade(state);
}

void close_trade(sys::state& state) {
	if(!trade_open)
		return;
	trade_open = false;
	trade_analysis = false;
	trade_focus = 0;
	map::katechon_trade::active = false;
	map::katechon_trade::lanes.clear();
	if(had_good_before_trade) {
		state.selected_trade_good = good_before_trade;
		had_good_before_trade = false;
	}
	state.map_state.map_data.trade_flow_vertices.clear();
	state.map_state.map_data.trade_flow_arrow_starts.clear();
	state.map_state.map_data.trade_flow_arrow_counts.clear();
	state.update_trade_flow.store(true, std::memory_order::release);
	if(trade_layer) {
		trade_layer = false;
		map_mode::set_map_mode(state, map_mode::mode::political);
	}
}

// Every table of the old dashboard, for whoever wants them.
void open_legacy_trade(sys::state& state) {
	close_trade(state);
	for(size_t i = 0; i < tabs.size(); ++i) {
		if(tabs[i].element == "topbarbutton_trade" && tab_buttons[i]) {
			legacy_trade = true;
			press(state, tab_buttons[i]);
		}
	}
}

// The old window may still be opened by a shortcut of the old top bar; the mode takes its place.
void intercept_legacy_trade(sys::state& state) {
	auto* old = legacy_trade_window();
	if(!old || !old->is_visible()) {
		legacy_trade = false;
		return;
	}
	if(legacy_trade)
		return;
	old->set_visible(state, false);
	open_trade(state);
}

// A foreign country chosen on the map is put forward. One's own land leaves the
// sheet as it is: the game itself chooses the capital at times, and the way back
// to the whole trade is the link and Esc.
void follow_map_choice(sys::state& state) {
	auto const p = state.map_state.selected_province;
	if(p == trade_seen_province)
		return;
	trade_seen_province = p;
	if(!p)
		return;
	auto const owner = state.world.province_get_nation_from_province_ownership(p);
	if(!owner || owner == state.local_player_nation)
		return;
	trade_analysis = false;
	trade_focus = -(int32_t(owner.index()) + 1);
}

// ---- words and figures ----
//
// The trade mode is drawn in the atlas (gui_map_hud_atlas.hpp): the country,
// the span and the controls on the dark corpus, the analysis on paper.

static constexpr float trade_row_h = 30.f;
// The lanes on the map, in the colours trade_route_f.glsl gives them.
inline static constexpr ogl::color3f lane_out{ 0.94f, 0.57f, 0.37f }; // copper, as atlas_export on the sheet
inline static constexpr ogl::color3f lane_in{ 0.62f, 0.84f, 0.98f };

static std::string signed_money(float v) {
	return (v > 0.f ? "+" : (v < 0.f ? "-" : "")) + text::format_money(std::abs(v));
}
// Money without its sign of currency, for columns whose heading carries it.
static std::string bare_money(float v) {
	auto s = text::format_money(v);
	if(auto const at = s.rfind(' '); at != std::string::npos && s.find_first_of("0123456789", at) == std::string::npos)
		s.erase(at);
	return s;
}
static std::string signed_bare_money(float v) {
	return (v > 0.f ? "+" : (v < 0.f ? "-" : "")) + bare_money(std::abs(v));
}
static int32_t partner_value(dcon::nation_id n) {
	return -(int32_t(n.index()) + 1);
}
static int32_t good_value(dcon::commodity_id c) {
	return int32_t(c.index()) + 1;
}

// The span the figures cover: "Last 30 days", or the days since the start.
std::string period_words(sys::state& state) const {
	if(trade.recorded_days <= 1)
		return localized(state, "katechon_trade_period_first_day", "First day of play");
	if(trade.recorded_days < trade.span_days)
		return filled(localized(state, "katechon_trade_period_since", "First %1 days of play"), std::to_string(std::max(1, trade.recorded_days)));
	return trade.span_days <= 30 ? localized(state, "katechon_trade_period_month", "Last 30 days") : localized(state, "katechon_trade_period_year", "Last 12 months");
}
// The days the figures cover, "January 1 – January 7, 2010", the year once when both share it.
std::string span_range_words(sys::state& state) const {
	auto const days = std::max(1, trade.recorded_days);
	auto const last = text::date_to_string(state, trade.as_of);
	if(days == 1)
		return last;
	auto first = text::date_to_string(state, trade.as_of - (days - 1));
	if(auto const comma = last.rfind(", "); comma != std::string::npos && first.size() > last.size() - comma && first.compare(first.size() - (last.size() - comma), last.size() - comma, last, comma, last.size() - comma) == 0)
		first.erase(first.size() - (last.size() - comma));
	return first + " – " + last;
}
std::string as_of_words(sys::state& state) const {
	return filled(localized(state, "katechon_trade_asof", "as of %1"), text::date_to_string(state, trade.as_of));
}

ogl::color3f level_tone(int32_t level) const {
	return level >= 2 ? atlas_risk : (level == 1 ? atlas_caution : atlas_type_soft);
}
std::string level_word(sys::state& state, int32_t level) {
	return level >= 2 ? localized(state, "katechon_trade_level_critical", "Critical") : (level == 1 ? localized(state, "katechon_trade_level_high", "High") : localized(state, "katechon_trade_level_watch", "Watch"));
}
std::string group_word(sys::state& state, int32_t kind) {
	return kind >= 2 ? localized(state, "katechon_trade_group_vital", "Vital needs") : (kind == 1 ? localized(state, "katechon_trade_group_strategic", "Industry and defence") : localized(state, "katechon_trade_group_other", "Other goods"));
}
// Coverage is only coloured where it means trouble.
ogl::color3f cover_tone(float cover) const {
	return cover < 0.f ? atlas_type_faint : (cover < 0.5f ? atlas_risk : (cover < 1.f ? atlas_caution : atlas_type));
}
ogl::color3f balance_tone(float v, bool on_paper = true) const {
	if(on_paper)
		return v > 0.f ? atlas_gain : (v < 0.f ? atlas_risk : atlas_type_soft);
	return v > 0.f ? atlas_gain : (v < 0.f ? atlas_risk : atlas_type_soft);
}

// Coverage as a percentage; far past the country's own use it only says how far.
static std::string cover_words(float cover) {
	if(cover < 0.f)
		return "—";
	return cover > 5.f ? ">500%" : text::format_percentage(cover, 0);
}

bool good_with_partner(dcon::commodity_id g, dcon::nation_id n) const {
	for(auto const& part : trade.parts) {
		if(part.good == g && part.nation == n)
			return true;
	}
	return false;
}

std::vector<economy::trade_picture::commodity_line const*> top_exports(size_t most) const {
	std::vector<economy::trade_picture::commodity_line const*> out;
	for(auto const& line : trade.commodities) {
		if(line.exported > 0.f)
			out.push_back(&line);
	}
	std::stable_sort(out.begin(), out.end(), [](auto const* a, auto const* b) { return a->exported > b->exported; });
	if(out.size() > most)
		out.resize(most);
	return out;
}
std::vector<economy::trade_picture::flow_part const*> parts_with(dcon::nation_id n, bool outgoing, size_t most) const {
	std::vector<economy::trade_picture::flow_part const*> out;
	for(auto const& part : trade.parts) {
		if(part.nation == n && part.outgoing == outgoing && out.size() < most)
			out.push_back(&part);
	}
	return out;
}
std::vector<economy::trade_picture::flow_part const*> parts_of(dcon::commodity_id c, bool outgoing, size_t most) const {
	std::vector<economy::trade_picture::flow_part const*> out;
	for(auto const& part : trade.parts) {
		if(part.good == c && part.outgoing == outgoing && out.size() < most)
			out.push_back(&part);
	}
	return out;
}

std::string reliance_words(sys::state& state, trade_partner_view const* p) {
	if(p && p->leans_on && p->leans_share >= 0.1f) {
		auto words = sentence(localized(state, "katechon_trade_reliance", "They supply %1 of all the %2 we use."), text::format_percentage(p->leans_share, 0), goods_name(state, p->leans_on));
		if(p->hard_to_replace)
			words += " " + localized(state, "katechon_trade_hard_replace", "Hardly anyone else sells it to us.");
		return words;
	}
	return localized(state, "katechon_trade_no_reliance", "Nothing we use depends on them for a tenth of its supply.");
}
std::string leverage_words(sys::state& state, float out, float in) {
	if(out <= 0.f && in <= 0.f)
		return filled(localized(state, "katechon_trade_no_partner_trade", "No trade with them: %1."), period_words(state));
	if(in > 0.f && out <= 0.f)
		return localized(state, "katechon_trade_lever_theirs_only", "We need what they sell and they need nothing of ours: the leverage is theirs.");
	if(out > in * 1.5f)
		return localized(state, "katechon_trade_lever_ours", "They take far more from us than they sell: the leverage is ours.");
	if(in > out * 1.5f)
		return localized(state, "katechon_trade_lever_theirs", "We buy far more from them than we sell: the leverage is theirs.");
	return localized(state, "katechon_trade_lever_even", "The trade runs about even both ways.");
}

// ---- what the figures say ----

// A conclusion drawn from the figures; a press puts its subject forward. Each is weighed by what is at
// stake (value), how much the good matters (what people live on and what industry and the army run on
// weigh more) and how concentrated it is, and the heaviest is the one the analysis leads with.
struct trade_insight {
	std::string head;
	std::string detail;
	int32_t focus = 0;
	int32_t level = 0; // 0 a remark, 1 high, 2 critical
	float weight = 0.f;
	std::string brief; // the same, short enough for the inspector on the map
	std::string brief_note;
};
std::vector<trade_insight> trade_insights;

void gather_insights(sys::state& state) {
	trade_insights.clear();
	if(trade.exported + trade.imported <= 0.f)
		return;
	// what our demand rests on abroad: dependence is always the imported part of demand
	for(auto const& d : trade_deps) {
		auto const name = goods_name(state, d.good_id);
		auto head = d.source && d.source_share >= 0.6f ? sentence(localized(state, "katechon_trade_ins_dep_on", "%1 supply heavily dependent on %2"), name, nation_name(state, d.source))
			: (d.import_share >= 0.5f ? filled(localized(state, "katechon_trade_ins_dep_imports", "%1 supply heavily dependent on imports"), name)
			: filled(localized(state, "katechon_trade_ins_dep_partly", "%1 supply partly imported"), name));
		auto const from = d.source ? sentence(localized(state, "katechon_trade_ins_from", "%1 of the imports from %2"), text::format_percentage(d.source_share, 0), nation_name(state, d.source))
			: localized(state, "katechon_trade_ins_many", "Bought from several countries");
		auto detail = filled(localized(state, "katechon_trade_ins_dep_detail", "%1 of demand depends on imports"), text::format_percentage(d.import_share, 0)) + " · " + from;
		trade_insights.push_back(trade_insight{ head, detail, good_value(d.good_id), d.level, d.imported * float(1 + d.kind) * d.import_share * (0.5f + d.source_share),
			sentence(localized(state, "katechon_trade_ins_brief", "%1 — %2 import-dependent"), name, text::format_percentage(d.import_share, 0)),
			d.level >= 2 ? localized(state, "katechon_trade_dep_critical", "Critical dependency") : (d.level == 1 ? localized(state, "katechon_trade_dep_high", "High dependency") : localized(state, "katechon_trade_dep_watch", "Dependency to watch")) });
	}
	// a surplus on the whole that hides a deficit with someone who matters
	if(trade.exported >= trade.imported) {
		for(auto const& p : trade_partners) {
			auto const balance = p.exported - p.imported;
			if(balance < 0.f && p.share >= 0.15f)
				trade_insights.push_back(trade_insight{ filled(localized(state, "katechon_trade_ins_hidden", "Hidden deficit with %1"), nation_name(state, p.nation)),
					sentence(localized(state, "katechon_trade_ins_hidden_detail", "Overall %1 · with them %2"), signed_money(trade.exported - trade.imported), signed_money(balance)), partner_value(p.nation), 1,
					-balance * 0.6f });
		}
	}
	// one buyer, or one good, carrying the exports
	if(trade.exported > 0.f) {
		for(auto const& p : trade_partners) {
			auto const share = p.exported / trade.exported;
			if(share >= 0.35f)
				trade_insights.push_back(trade_insight{ sentence(localized(state, "katechon_trade_ins_buyer", "%1 buys %2 of our exports"), nation_name(state, p.nation), text::format_percentage(share, 0)),
					sentence(localized(state, "katechon_trade_ins_buyer_detail", "Exports %1 against imports %2"), text::format_money(trade.exported), text::format_money(trade.imported)), partner_value(p.nation),
					share >= 0.5f ? 1 : 0, trade.exported * share * 0.4f });
		}
		for(auto const& line : trade.commodities) {
			auto const share = line.exported / trade.exported;
			if(share >= 0.5f)
				trade_insights.push_back(trade_insight{ sentence(localized(state, "katechon_trade_ins_good", "%1 earns %2 of export income"), goods_name(state, line.commodity), text::format_percentage(share, 0)),
					localized(state, "katechon_trade_ins_good_detail", "One good carries the export side"), good_value(line.commodity), 0, trade.exported * share * 0.3f });
		}
	}
	std::stable_sort(trade_insights.begin(), trade_insights.end(), [](trade_insight const& a, trade_insight const& b) { return a.weight > b.weight; });
	if(trade_insights.size() > 6)
		trade_insights.resize(6);
}

// ---- the state of the analysis ----

int32_t trade_chart_mode = 0; // 0 the two flows, 3 the balance
bool trade_trends = false; // the analysis shows the trends of the span instead of its structure
static constexpr int32_t trends_toggle = 100; // the value of action::trade_chart that switches between structure and trends
int32_t goods_sort_col = -1; // -1: by trade both ways
bool goods_sort_up = false;
int32_t partners_sort_col = -1;
bool partners_sort_up = false;
int32_t goods_first = 0; // the first row shown, as the wheel moves the register
int32_t partners_first = 0;
int32_t goods_total = 0;
int32_t goods_view = 0;
int32_t partners_total = 0;
int32_t partners_view = 0;
rect goods_area;
rect partners_area;

void trade_act(action a, int32_t value) {
	if(a == action::trade_select) {
		trade_focus = trade_focus == value ? 0 : value;
		reveal_pending = 0; // chosen in sight
	} else if(a == action::trade_findings) {
		trade_choose(value); // chosen from the context or the risk line, and shown in the register
	} else if(a == action::trade_chart) {
		if(value == findings_next)
			++finding_at;
		else if(value == trends_toggle)
			trade_trends = !trade_trends;
		else
			trade_chart_mode = value;
	} else if(a == action::trade_sort && value >= register_tab_base) {
		trade_register_act(value);
	} else if(a == action::trade_sort) {
		auto& column = value / 100 == 0 ? goods_sort_col : partners_sort_col;
		auto& up = value / 100 == 0 ? goods_sort_up : partners_sort_up;
		if(column == value % 100) {
			up = !up;
		} else {
			column = value % 100;
			up = column == 0; // names from A, figures from the largest
		}
	}
}

bool trade_wheel(float x, float y, float amount) {
	if(!trade_open || !trade_analysis)
		return false;
	auto const inside = [&](rect const& r) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; };
	auto const step = amount > 0.f ? -2 : 2;
	reveal_pending = 0; // the player moves the register now
	if(inside(goods_area)) {
		goods_first = std::clamp(goods_first + step, 0, std::max(0, goods_total - goods_view));
		return true;
	}
	if(inside(partners_area)) {
		partners_first = std::clamp(partners_first + step, 0, std::max(0, partners_total - partners_view));
		return true;
	}
	return false;
}

// ---- shared parts ----

// A figure: what it is, with the swatch of its series; the figure; how it stands against the span before.
void trade_figure(sys::state& state, std::string const& label, ogl::color3f const* swatch, std::string const& value, ogl::color3f colour, float now, float before, bool rising_is_good, bool as_money,
	bool compare, float x, float y, float w, int32_t size, bool on_paper) {
	auto lx = x;
	if(swatch) {
		fill(state, x, y + 4.f, 9.f, 9.f, *swatch, 1.f);
		lx += 15.f;
	}
	sans(state, sans_fit(state, label, 13, w - (lx - x) - 6.f), lx, y + 8.f, 13, on_paper ? atlas_type_soft : atlas_type_soft);
	sans(state, sans_fit(state, value, size, w - 8.f), x, y + 22.f + float(size) * 0.5f, size, colour);
	if(!compare || !trade.has_previous || (!as_money && before == 0.f))
		return;
	auto const delta = now - before;
	auto const change = as_money ? delta : delta / std::abs(before);
	auto const up = change >= 0.f;
	auto const flat = as_money ? std::abs(delta) < 0.005f * std::max(1.f, std::abs(before)) : std::abs(change) < 0.005f;
	auto const tone = flat ? (on_paper ? atlas_type_soft : atlas_type_soft) : (up == rising_is_good ? (on_paper ? atlas_gain : atlas_gain) : (on_paper ? atlas_risk : atlas_risk));
	auto const cy = y + 32.f + float(size);
	arrow(state, x + 1.f, cy, up, tone);
	auto const figure = as_money ? signed_money(delta) : text::format_percentage(std::abs(change), 0);
	sans(state, sans_fit(state, figure + " " + localized(state, "katechon_trade_vs_before", "vs previous"), 12, w - 18.f), x + 10.f, cy, 12, tone);
}

std::vector<std::pair<std::string, int32_t>> span_items(sys::state& state) {
	return { { localized(state, "katechon_trade_span_month", "30 days"), 30 }, { localized(state, "katechon_trade_span_year", "12 months"), 365 } };
}

// A heading on the sheet: the serif, and what the figures under it are.
float trade_heading(sys::state& state, std::string const& title, std::string const& aside, float left, float right, float y) {
	atlas_serif(state, title, left, y + 11.f, 20, atlas_type);
	if(!aside.empty())
		sans(state, aside, right, y + 12.f, 12, atlas_type_faint, true);
	return y + 32.f;
}

// What the lines on the map mean, in their own colours on a scrap of the night map.
float lane_key(sys::state& state, float left, float right, float y) {
	auto x = left;
	for(auto const& [colour, label] : { std::pair{ lane_out, localized(state, "katechon_trade_exports", "Exports") }, std::pair{ lane_in, localized(state, "katechon_trade_imports", "Imports") } }) {
		fill(state, x, y + 3.f, 22.f, 10.f, atlas_deep, 1.f);
		fill(state, x + 3.f, y + 7.f, 16.f, 2.f, colour, 1.f);
		x += 28.f + sans(state, label, x + 28.f, y + 8.f, 12, atlas_type_soft) + 16.f;
	}
	sans(state, localized(state, "katechon_trade_key_width", "wider: more value"), right, y + 8.f, 12, atlas_type_faint, true);
	y += 22.f;
	if(trade_focus > 0) {
		x = left;
		for(auto const& [colour, label] : { std::pair{ trade_seller, localized(state, "katechon_trade_key_sellers", "sell it abroad") }, std::pair{ trade_partner, localized(state, "katechon_trade_key_buyers", "buy it abroad") } }) {
			fill(state, x, y + 3.f, 11.f, 11.f, colour, 1.f);
			frame(state, x, y + 3.f, 11.f, 11.f, ink, 0.25f);
			x += 17.f + sans(state, label, x + 17.f, y + 8.f, 12, atlas_type_soft) + 16.f;
		}
		sans(state, localized(state, "katechon_trade_key_world", "the whole world"), right, y + 8.f, 12, atlas_type_faint, true);
		y += 22.f;
	}
	return y;
}

// ---- the inspector on the map ----

float partner_row(sys::state& state, trade_partner_view const& p, float top_total, float left, float right, float y, bool hovering, float mx, float my) {
	hit_target t{ left - 10.f, y, right - left + 20.f, trade_row_h, action::trade_focus, nullptr, partner_value(p.nation) };
	auto const hovered = hovering && over(t, mx, my);
	atlas_row(state, t, hovered, false, false);
	auto const cy = y + trade_row_h * 0.5f;
	atlas_flag(state, p.nation, left, cy - 6.f, 18.f, 12.f);
	sans(state, text::format_percentage(p.share, 0), right, cy, 14, atlas_type, true);
	constexpr float bar_w = 64.f;
	auto const bx = right - 46.f - bar_w;
	auto const total = p.exported + p.imported;
	auto const length = std::max(3.f, std::round(bar_w * (top_total > 0.f ? total / top_total : 0.f)));
	auto const out_w = std::round(length * (total > 0.f ? p.exported / total : 0.f));
	fill(state, bx, cy - 2.f, bar_w, 4.f, atlas_track, 1.f);
	fill(state, bx, cy - 2.f, out_w, 4.f, atlas_export, 1.f);
	fill(state, bx + out_w, cy - 2.f, length - out_w, 4.f, atlas_import, 1.f);
	sans(state, sans_fit(state, nation_name(state, p.nation), 15, bx - left - 40.f), left + 28.f, cy, 15, atlas_type);
	targets.push_back(t);
	return y + trade_row_h;
}

float goods_line(sys::state& state, economy::trade_picture::flow_part const& part, float whole, float left, float right, float y, bool hovering, float mx, float my) {
	hit_target t{ left - 10.f, y, right - left + 20.f, trade_row_h, action::trade_focus, nullptr, good_value(part.good) };
	auto const hovered = hovering && over(t, mx, my);
	atlas_row(state, t, hovered, false, false);
	auto const cy = y + trade_row_h * 0.5f;
	good_icon(state, part.good, left + 8.f, cy, 16.f, atlas_type_soft);
	auto const pw = sans(state, text::format_percentage(whole > 0.f ? part.value / whole : 0.f, 0), right, cy, 13, atlas_type_faint, true);
	auto const vw = sans(state, text::format_money(part.value), right - std::max(pw, 34.f) - 12.f, cy, 14, atlas_type, true);
	sans(state, sans_fit(state, goods_name(state, part.good), 14, right - left - 24.f - std::max(pw, 34.f) - vw - 30.f), left + 24.f, cy, 14, atlas_type);
	targets.push_back(t);
	return y + trade_row_h;
}

float nation_line(sys::state& state, dcon::nation_id n, float value, float whole, float left, float right, float y, bool hovering, float mx, float my) {
	hit_target t{ left - 10.f, y, right - left + 20.f, trade_row_h, action::trade_focus, nullptr, partner_value(n) };
	auto const hovered = hovering && over(t, mx, my);
	atlas_row(state, t, hovered, false, false);
	auto const cy = y + trade_row_h * 0.5f;
	atlas_flag(state, n, left, cy - 6.f, 18.f, 12.f);
	auto const pw = sans(state, text::format_percentage(whole > 0.f ? value / whole : 0.f, 0), right, cy, 13, atlas_type_faint, true);
	auto const vw = sans(state, text::format_money(value), right - std::max(pw, 34.f) - 12.f, cy, 14, atlas_type, true);
	sans(state, sans_fit(state, nation_name(state, n), 14, right - left - 28.f - std::max(pw, 34.f) - vw - 30.f), left + 28.f, cy, 14, atlas_type);
	targets.push_back(t);
	return y + trade_row_h;
}

std::vector<std::pair<std::string, ogl::color3f>> home_words(sys::state& state, dcon::commodity_id c) {
	auto const* line = line_for(c);
	std::vector<std::pair<std::string, ogl::color3f>> lines;
	if(line && line->bought > 0.f) {
		auto const share = line->dependence();
		if(share > 0.f) {
			auto words = filled(localized(state, "katechon_trade_imported_use", "%1 of demand depends on imports."), text::format_percentage(share, 0));
			if(line->main_source)
				words += " " + sentence(localized(state, "katechon_trade_main_source", "%1 of the imports from %2."), text::format_percentage(line->main_source_share, 0), nation_name(state, line->main_source));
			lines.emplace_back(words, share >= 0.5f ? atlas_risk : (share >= 0.25f ? atlas_caution : atlas_type));
		} else {
			lines.emplace_back(localized(state, "katechon_trade_all_home", "All we use of it is made at home."), atlas_type);
		}
	} else if(line && line->exported > 0.f) {
		lines.emplace_back(localized(state, "katechon_trade_only_abroad", "We do not buy it at home: it is made for sale abroad."), atlas_type);
	}
	auto const capital = state.world.nation_get_capital(trade.nation);
	auto const home_market = capital ? state.world.state_instance_get_market_from_local_market(state.world.province_get_state_membership(capital)) : dcon::market_id{};
	auto const ours = economy::trade_picture::unit_price(state, home_market, c);
	auto const world = state.world.commodity_get_median_price(c);
	if(ours > 0.f && world > 0.f) {
		auto words = sentence(localized(state, "katechon_trade_prices", "Price at home %1, in the world %2."), text::format_money(ours), text::format_money(world));
		if(std::abs(ours / world - 1.f) >= 0.03f)
			words += " " + (ours < world ? filled(localized(state, "katechon_trade_price_cheaper", "Selling abroad pays %1 more."), text::format_percentage(world / ours - 1.f, 0))
				: filled(localized(state, "katechon_trade_price_dearer", "Buying abroad is %1 cheaper."), text::format_percentage(1.f - world / ours, 0)));
		lines.emplace_back(words, atlas_type_soft);
	}
	return lines;
}

float focus_partner_height(sys::state& state, dcon::nation_id partner) {
	auto const sells = parts_with(partner, true, 4).size();
	auto const buys = parts_with(partner, false, 4).size();
	return 2.f * (32.f + 12.f) + float(std::max<size_t>(1, sells) + std::max<size_t>(1, buys)) * trade_row_h + 2.f * 19.f * 2.f + 16.f + 44.f;
}
float focus_good_height(sys::state& state, dcon::commodity_id c) {
	auto const* line = line_for(c);
	auto const outgoing = line && line->exported >= line->imported && line->exported > 0.f;
	auto const main_rows = std::max<size_t>(1, parts_of(c, outgoing, 5).size());
	auto const other_rows = parts_of(c, !outgoing, 3).size();
	return 32.f + float(main_rows) * trade_row_h + 12.f + (other_rows ? 32.f + float(other_rows) * trade_row_h + 12.f : 0.f) + 32.f + 3.f * 19.f * 2.f;
}

void draw_trade(sys::state& state, float w, float h, bool hovering, float mx, float my) {
	follow_map_choice(state);
	if(trade.span_days != trade_span) {
		gather_trade(state);
		if(trade_layer)
			paint_trade(state);
	}
	if(trade_focus != lanes_filter) {
		gather_lanes(state);
		if(trade_layer)
			paint_trade(state);
	}
	if(trade_analysis) {
		draw_trade_analysis(state, w, h, hovering, mx, my);
		return;
	}

	// What is happening, at a glance: the three figures, the one risk, the partners, what we
	// sell most and buy most. The rest is in the full analysis.
	auto const& v = trade;
	constexpr float sheet_w = 392.f;
	auto const x0 = ribbon_w + 8.f;
	auto const y0 = top_h + 12.f;
	auto const left = x0 + 28.f;
	auto const right = x0 + sheet_w - 28.f;
	auto const room = h - y0 - 16.f;
	constexpr float head_h = 86.f;
	constexpr float foot_h = 64.f;
	constexpr float figures_h = 90.f;
	auto const partner = focus_partner();
	auto const focus_good_id = focus_good();
	auto const has_trade = v.exported + v.imported > 0.f;
	auto const show_risk = trade_focus == 0 && !trade_insights.empty();
	auto const partner_rows = std::min<size_t>(4, trade_partners.size());
	float body = 0.f;
	if(trade_focus == 0)
		body = (show_risk ? 66.f : 0.f) + 34.f + float(std::max<size_t>(1, partner_rows)) * trade_row_h + (has_trade ? 14.f + 60.f : 0.f) + 10.f;
	else
		body = (trade_focus > 0 ? focus_good_height(state, focus_good_id) + 58.f : focus_partner_height(state, partner)) + 10.f;
	auto const sheet_h = std::min(room, head_h + 16.f + figures_h + body + foot_h);
	atlas_window(state, x0, y0, sheet_w, sheet_h);

	// the head: whose trade, or what is forward, and over what span
	atlas_close(state, right - 10.f, y0 + 12.f, action::toggle_trade, hovering, mx, my);
	if(trade_focus != 0)
		atlas_link(state, "‹ " + localized(state, "katechon_trade_all", "All trade"), right - 22.f, y0 + 72.f, hit_target{ 0.f, 0.f, 0.f, 0.f, action::trade_focus, nullptr, 0 }, hovering, mx, my, true);
	std::string subtitle = localized(state, "katechon_trade_overview", "Trade overview") + " · " + span_range_words(state);
	if(partner) {
		atlas_flag(state, partner, left, y0 + 20.f, 36.f, 24.f);
		atlas_serif(state, fit(state, nation_name(state, partner), 26, 0.004f, right - left - 90.f), left + 48.f, y0 + 32.f, 26, atlas_type);
		auto const* p = partner_view(partner);
		subtitle = filled(localized(state, "katechon_trade_of_our_trade", "%1 of our trade"), text::format_percentage(p ? p->share : 0.f, 0)) + " · " + span_range_words(state);
	} else if(focus_good_id) {
		fill(state, left, y0 + 17.f, 32.f, 30.f, atlas_deep, 1.f);
		frame(state, left, y0 + 17.f, 32.f, 30.f, atlas_accent, 0.6f);
		good_icon(state, focus_good_id, left + 16.f, y0 + 32.f, 20.f, atlas_type);
		atlas_serif(state, fit(state, goods_name(state, focus_good_id), 26, 0.004f, right - left - 90.f), left + 48.f, y0 + 32.f, 26, atlas_type);
		auto const* line = line_for(focus_good_id);
		auto const out = line ? line->exported : 0.f;
		auto const in = line ? line->imported : 0.f;
		subtitle = out > in ? localized(state, "katechon_trade_role_out", "Net export") : (in > 0.f ? localized(state, "katechon_trade_role_in", "Net import") : localized(state, "katechon_trade_role_home", "Traded at home only"));
		if(auto const k = weight_of_good(state, focus_good_id); k > 0)
			subtitle += " · " + group_word(state, k);
	} else {
		if(v.nation)
			atlas_flag(state, v.nation, left, y0 + 20.f, 36.f, 24.f);
		atlas_serif(state, fit(state, nation_name(state, v.nation), 26, 0.004f, right - left - 90.f), left + 48.f, y0 + 32.f, 26, atlas_type);
	}
	sans(state, sans_fit(state, subtitle, 14, trade_focus != 0 ? right - left - 110.f : right - left), left, y0 + 72.f, 14, atlas_type_soft);

	// the sheet
	auto const sx = x0 + 10.f;
	auto const sy = y0 + head_h;
	auto const sh = sheet_h - head_h - foot_h;
	atlas_panel(state, sx, sy, sheet_w - 20.f, sh);
	auto y = sy + 16.f;
	{
		auto const col = std::floor((right - left) / 3.f);
		float out = v.exported, in = v.imported;
		auto comparable = true;
		if(partner) {
			auto const* p = partner_view(partner);
			out = p ? p->exported : 0.f;
			in = p ? p->imported : 0.f;
			comparable = false; // the span before is summed for the whole trade only
		} else if(focus_good_id) {
			auto const* line = line_for(focus_good_id);
			out = line ? line->exported : 0.f;
			in = line ? line->imported : 0.f;
			comparable = false;
		}
		trade_figure(state, localized(state, "katechon_trade_kpi_out", "Exports"), &atlas_export, text::format_money(out), atlas_type, out, v.previous_exported, true, false, comparable, left, y, col - 8.f, 25, false);
		trade_figure(state, localized(state, "katechon_trade_kpi_in", "Imports"), &atlas_import, text::format_money(in), atlas_type, in, v.previous_imported, false, false, comparable, left + col, y, col - 8.f, 25, false);
		if(focus_good_id) {
			auto const* line = line_for(focus_good_id);
			auto const dependence = line ? line->dependence() : -1.f;
			trade_figure(state, localized(state, "katechon_trade_kpi_dependence", "Dependence"), nullptr, dependence < 0.f ? std::string("—") : text::format_percentage(dependence, 0),
				dependence >= 0.5f ? atlas_risk : (dependence >= 0.25f ? atlas_caution : atlas_type), 0.f, 0.f, true, false, false, left + 2.f * col, y, col, 25, false);
		} else {
			trade_figure(state, localized(state, "katechon_trade_kpi_net", "Balance"), nullptr, signed_money(out - in), balance_tone(out - in, false), out - in, v.previous_exported - v.previous_imported, true, true, comparable, left + 2.f * col, y, col, 25, false);
		}
		y += figures_h - 8.f;
		atlas_rule(state, left, right, y);
		y += 8.f;
	}
	auto const floor_y = sy + sh - 10.f;
	if(trade_focus == 0) {
		// the one risk: a press opens the good
		if(show_risk) {
			auto const& ins = trade_insights.front();
			auto const tone = ins.level >= 2 ? atlas_risk : (ins.level == 1 ? atlas_caution : atlas_type_soft);
			hit_target t{ left - 10.f, y, right - left + 20.f, 56.f, ins.focus ? action::trade_focus : action::none, nullptr, ins.focus };
			auto const hovered = ins.focus != 0 && hovering && over(t, mx, my);
			fill(state, t.x, t.y, t.w, t.h, hovered ? atlas_hover : atlas_chosen, 1.f);
			fill(state, t.x, t.y, 4.f, t.h, tone, 1.f);
			sans(state, sans_fit(state, ins.brief.empty() ? ins.head : ins.brief, 16, right - left - 24.f), left + 2.f, y + 19.f, 16, atlas_type);
			sans(state, sans_fit(state, ins.brief_note.empty() ? ins.detail : ins.brief_note, 14, right - left - 24.f), left + 2.f, y + 39.f, 14, tone);
			if(ins.focus != 0) {
				draw_icon(state, icon::chevron_right, right - 2.f, y + 28.f, 10.f, hovered ? atlas_type : atlas_type_faint);
				targets.push_back(t);
			}
			y += 66.f;
		}
		// the partners
		atlas_serif(state, localized(state, "katechon_trade_partners", "Trading partners"), left, y + 13.f, 20, atlas_type);
		sans(state, localized(state, "katechon_trade_share_aside", "trade share"), right, y + 14.f, 13, atlas_type_soft, true);
		y += 34.f;
		if(trade_partners.empty()) {
			sans(state, filled(localized(state, "katechon_trade_quiet_border", "Nothing crossed the border: %1"), period_words(state)), left, y + 15.f, 14, atlas_type_soft);
			y += trade_row_h;
		} else {
			auto const top_total = trade_partners.front().exported + trade_partners.front().imported;
			for(size_t i = 0; i < partner_rows && y + trade_row_h <= floor_y; ++i)
				y = partner_row(state, trade_partners[i], top_total, left, right, y, hovering, mx, my);
		}
		// what we sell most, what we buy most
		if(has_trade && y + 74.f <= floor_y) {
			y += 14.f;
			atlas_rule(state, left, right, y - 6.f);
			auto const half = std::floor((right - left - 16.f) * 0.5f);
			auto const cell = [&](std::string const& label, economy::trade_picture::commodity_line const* line, bool outgoing, float x, float cw) {
				sans(state, label, x, y + 10.f, 13, atlas_type_soft);
				if(!line) {
					sans(state, "—", x, y + 34.f, 15, atlas_type_faint);
					return;
				}
				hit_target t{ x - 6.f, y, cw + 12.f, 50.f, action::trade_focus, nullptr, good_value(line->commodity) };
				auto const hovered = hovering && over(t, mx, my);
				if(hovered)
					fill(state, t.x, t.y, t.w, t.h, atlas_hover, 1.f);
				good_icon(state, line->commodity, x + 8.f, y + 34.f, 16.f, outgoing ? atlas_export : atlas_import);
				sans(state, sans_fit(state, goods_name(state, line->commodity) + " · " + text::format_money(outgoing ? line->exported : line->imported), 15, cw - 26.f), x + 24.f, y + 34.f, 15, atlas_type);
				targets.push_back(t);
			};
			economy::trade_picture::commodity_line const* top_out = nullptr;
			economy::trade_picture::commodity_line const* top_in = nullptr;
			for(auto const& line : v.commodities) {
				if(line.exported > 0.f && (!top_out || line.exported > top_out->exported))
					top_out = &line;
				if(line.imported > 0.f && (!top_in || line.imported > top_in->imported))
					top_in = &line;
			}
			cell(localized(state, "katechon_trade_top_export", "Top export"), top_out, true, left, half);
			cell(localized(state, "katechon_trade_top_import", "Top import"), top_in, false, left + half + 16.f, half);
		}
	} else if(trade_focus > 0) {
		draw_focus_good(state, focus_good_id, left, right, y, floor_y - 58.f, hovering, mx, my);
		atlas_rule(state, left, right, floor_y - 54.f);
		lane_key(state, left, right, floor_y - 46.f);
	} else {
		draw_focus_partner(state, partner, left, right, y, floor_y, hovering, mx, my);
	}

	// the way to the whole analysis, on the corpus
	atlas_button(state, left, y0 + sheet_h - 16.f - 36.f, right - left, 36.f, localized(state, "katechon_trade_open_analysis", "Full trade analysis"),
		hit_target{ 0.f, 0.f, 0.f, 0.f, action::trade_analysis }, hovering, mx, my, atlas_kind::primary, icon::chevron_right);
}

void draw_focus_partner(sys::state& state, dcon::nation_id partner, float left, float right, float y, float floor_y, bool hovering, float mx, float my) {
	auto const* p = partner_view(partner);
	auto const out = p ? p->exported : 0.f;
	auto const in = p ? p->imported : 0.f;
	for(auto const outgoing : { true, false }) {
		y = trade_heading(state, outgoing ? localized(state, "katechon_trade_they_buy", "We sell them") : localized(state, "katechon_trade_they_sell", "We buy from them"),
			localized(state, "katechon_trade_of_flow", "share of it"), left, right, y);
		auto const list = parts_with(partner, outgoing, 4);
		if(list.empty()) {
			sans(state, localized(state, "katechon_trade_nothing_goods", "Nothing"), left, y + 14.f, 13, atlas_type_faint);
			y += trade_row_h;
		}
		for(auto const* part : list) {
			if(y + trade_row_h > floor_y)
				break;
			y = goods_line(state, *part, outgoing ? out : in, left, right, y, hovering, mx, my);
		}
		y += 12.f;
	}
	y = sans_paragraph(state, reliance_words(state, p), left, right, y, 14, p && p->hard_to_replace ? atlas_risk : atlas_type, 2, true) + 4.f;
	y = sans_paragraph(state, leverage_words(state, out, in), left, right, y, 14, atlas_type_soft, 2, true) + 10.f;
	atlas_button(state, left, std::min(y, floor_y - 40.f), right - left, 32.f, localized(state, "katechon_trade_talks", "Open talks"),
		hit_target{ 0.f, 0.f, 0.f, 0.f, action::diplomacy_target, nullptr, nation_value(partner) }, hovering, mx, my, atlas_kind::quiet);
}

void draw_focus_good(sys::state& state, dcon::commodity_id c, float left, float right, float y, float floor_y, bool hovering, float mx, float my) {
	auto const* line = line_for(c);
	auto const out = line ? line->exported : 0.f;
	auto const in = line ? line->imported : 0.f;
	auto const outgoing = out >= in && out > 0.f;
	for(auto const direction : { outgoing, !outgoing }) {
		auto const list = parts_of(c, direction, direction == outgoing ? 5 : 3);
		if(direction != outgoing && list.empty())
			break;
		y = trade_heading(state, direction ? localized(state, "katechon_trade_buyers", "Who buys it from us") : localized(state, "katechon_trade_sellers", "Who sells it to us"),
			localized(state, "katechon_trade_of_flow", "share of it"), left, right, y);
		if(list.empty()) {
			sans(state, filled(localized(state, "katechon_trade_not_across", "Not traded across the border: %1"), period_words(state)), left, y + 14.f, 13, atlas_type_faint);
			y += trade_row_h;
		}
		for(auto const* part : list) {
			if(y + trade_row_h > floor_y)
				break;
			y = nation_line(state, part->nation, part->value, direction ? out : in, left, right, y, hovering, mx, my);
		}
		y += 12.f;
	}
	y = trade_heading(state, localized(state, "katechon_trade_home", "At home"), std::string(), left, right, y);
	for(auto const& [words, colour] : home_words(state, c)) {
		if(y + 20.f > floor_y)
			break;
		y = sans_paragraph(state, words, left, right, y, 14, colour, 2, true) + 4.f;
	}
}

// ---- the trade workbench ----
//
// The full analysis is one workbench in the atlas language. A fixed head
// carries the state, the span and its three figures; under it, one line names
// the gravest finding when there is one. The body is a register on the left
// and a context on the right that never changes the size of the window: the
// register lists every good of the simulation, or every partner, filtered,
// searched, sorted and scrolled on its own; a press on a row opens what belongs
// to it in the context, and a press there on a supplier, a buyer or a good goes
// to it in one step. Trends are a separate instrument a press away. What is
// chosen, the register, its filter, its search and its scroll are kept while
// the player goes to the map and back.
//
// Three measures are kept apart throughout, all from the deals themselves:
// production against demand (the register's domestic supply and its shortage
// or surplus); domestic coverage, the part of what was bought that came from
// home (the context's leading figure); and the import share, the part of what
// was bought that came from abroad (under the imports in the register).

static constexpr float list_row_h = 40.f;
static constexpr float columns_h = 34.f;

dcon::nation_id focus_partner() const {
	return trade_focus < 0 ? dcon::nation_id{ dcon::nation_id::value_base_t(-trade_focus - 1) } : dcon::nation_id{};
}
dcon::commodity_id focus_good() const {
	return trade_focus > 0 ? dcon::commodity_id{ dcon::commodity_id::value_base_t(trade_focus - 1) } : dcon::commodity_id{};
}

// ---- the indexed snapshot ----
//
// Taken when the picture is taken, never while drawing: one link for every good traded with every
// partner, both ways, indexed by good and by partner, and what each good weighs in the economy.
struct trade_link {
	dcon::nation_id nation;
	dcon::commodity_id good;
	float in_value = 0.f;
	float out_value = 0.f;
	float in_quantity = 0.f;
	float out_quantity = 0.f;
};
std::vector<trade_link> trade_links;
std::vector<std::vector<uint32_t>> links_by_good; // by commodity index, largest first
std::vector<std::vector<uint32_t>> links_by_nation; // by nation index, largest first
std::vector<float> good_weight; // by commodity index: its demand at home prices and its trade both ways
uint32_t trade_generation = 0; // counts the pictures, so that what is derived from one knows when it is stale

void index_trade(sys::state& state) {
	++trade_generation;
	trade_links.clear();
	links_by_good.assign(state.world.commodity_size(), {});
	links_by_nation.assign(state.world.nation_size(), {});
	std::unordered_map<uint64_t, uint32_t> at;
	for(auto const& part : trade.parts) {
		auto const key = (uint64_t(part.good.index()) << 32) | uint64_t(part.nation.index());
		auto const [it, fresh] = at.try_emplace(key, uint32_t(trade_links.size()));
		if(fresh)
			trade_links.push_back(trade_link{ part.nation, part.good });
		auto& link = trade_links[it->second];
		if(part.outgoing) {
			link.out_value += part.value;
			link.out_quantity += part.quantity;
		} else {
			link.in_value += part.value;
			link.in_quantity += part.quantity;
		}
	}
	for(uint32_t i = 0; i < trade_links.size(); ++i) {
		links_by_good[size_t(trade_links[i].good.index())].push_back(i);
		links_by_nation[size_t(trade_links[i].nation.index())].push_back(i);
	}
	auto const larger = [&](uint32_t a, uint32_t b) { return trade_links[a].in_value + trade_links[a].out_value > trade_links[b].in_value + trade_links[b].out_value; };
	for(auto& list : links_by_good)
		std::stable_sort(list.begin(), list.end(), larger);
	for(auto& list : links_by_nation)
		std::stable_sort(list.begin(), list.end(), larger);
	auto const capital = state.world.nation_get_capital(trade.nation);
	auto const home_market = capital ? state.world.state_instance_get_market_from_local_market(state.world.province_get_state_membership(capital)) : dcon::market_id{};
	good_weight.assign(state.world.commodity_size(), 0.f);
	for(auto const& line : trade.commodities)
		good_weight[size_t(line.commodity.index())] = line.bought * economy::trade_picture::unit_price(state, home_market, line.commodity) + line.exported + line.imported;
}

trade_link const* link_of(dcon::commodity_id g, dcon::nation_id n) const {
	if(!g || size_t(g.index()) >= links_by_good.size())
		return nullptr;
	for(auto const i : links_by_good[size_t(g.index())]) {
		if(trade_links[i].nation == n)
			return &trade_links[i];
	}
	return nullptr;
}

// The imported part of demand, the domestic part, and the part production could not meet.
static float domestic_coverage(economy::trade_picture::commodity_line const& l) {
	return l.bought > 0.f ? std::clamp(1.f - l.imported_quantity / l.bought, 0.f, 1.f) : -1.f;
}
static bool idle_good(economy::trade_picture::commodity_line const& l) {
	return l.bought <= 0.f && l.sold <= 0.f && l.exported <= 0.f && l.imported <= 0.f;
}
size_t idle_goods_count() const {
	size_t n = 0;
	for(auto const& l : trade.commodities)
		n += idle_good(l) ? 1 : 0;
	return n;
}
static std::string percent_or_dash(float part) {
	return part < 0.f ? std::string("—") : text::format_percentage(part, 0);
}
// A share as a whole percentage, rounded (the game's own format cuts the fraction off, so that 83% at
// home and 17% from abroad would read 82% and 17%); a share too small to round to one is under it.
static std::string share_words(float part) {
	if(part < 0.f)
		return std::string("—");
	if(part > 0.f && part < 0.005f)
		return std::string("<1%");
	return std::to_string(std::lround(part * 100.f)) + "%";
}

// ---- the register's state ----

int32_t register_tab = 0; // 0 goods, 1 countries
int32_t register_filter = 0; // 0 all, 1 import dependencies, 2 exports
std::string trade_search;
bool trade_search_focus = false;
bool search_focus_request = false;
int32_t reveal_pending = 0; // a choice made outside the register, kept in view until the player scrolls or chooses in the register
std::vector<uint32_t> register_rows; // indices into trade.commodities or trade_partners, in the order shown
std::tuple<int32_t, int32_t, int32_t, bool, std::string, uint32_t, bool> register_key{ -1, -1, -1, false, std::string(), 0, false };
bool show_idle_goods = false; // the goods with no production, demand or trade over the span, opened from their summary row
static constexpr uint32_t idle_summary_row = 0xFFFFFFFFu; // the row that stands for them while they are folded

// The register's controls ride on action::trade_sort above the sorting values.
static constexpr int32_t register_tab_base = 1000;
static constexpr int32_t register_filter_base = 1100;
static constexpr int32_t register_search_focus = 1200;
static constexpr int32_t register_search_clear = 1300;
static constexpr int32_t register_idle_toggle = 1400;

void trade_register_act(int32_t value) {
	if(value >= register_idle_toggle) {
		show_idle_goods = !show_idle_goods;
	} else if(value >= register_search_clear) {
		trade_search.clear();
		goods_first = partners_first = 0;
	} else if(value >= register_search_focus) {
		search_focus_request = true;
	} else if(value >= register_filter_base) {
		register_filter = value - register_filter_base;
		goods_first = partners_first = 0;
	} else if(value >= register_tab_base) {
		register_tab = value - register_tab_base;
	}
}

// A choice made in the context or on the risk line: chosen, never unchosen, and shown in the register.
void trade_choose(int32_t value) {
	trade_focus = value;
	reveal_pending = value;
	if(value != 0) {
		register_tab = value > 0 ? 0 : 1;
		register_filter = 0;
		trade_search.clear();
	}
}

static std::string folded(std::string s) {
	for(auto& c : s) {
		if(uint8_t(c) < 128)
			c = char(std::tolower(uint8_t(c)));
	}
	return s;
}

// The rows of the register, in order; worked out again only when what decides them changes.
std::vector<uint32_t> const& register_order(sys::state& state) {
	auto const key = std::tuple{ register_tab, register_filter, register_tab == 0 ? goods_sort_col : partners_sort_col, register_tab == 0 ? goods_sort_up : partners_sort_up, trade_search, trade_generation, show_idle_goods };
	if(key == register_key)
		return register_rows;
	register_key = key;
	register_rows.clear();
	auto const needle = folded(trade_search);
	if(register_tab == 0) {
		for(uint32_t i = 0; i < trade.commodities.size(); ++i) {
			auto const& l = trade.commodities[i];
			if(register_filter == 1 && !(l.dependence() >= 0.1f))
				continue;
			if(register_filter == 2 && !(l.exported > 0.f))
				continue;
			if(!needle.empty() && folded(goods_name(state, l.commodity)).find(needle) == std::string::npos)
				continue;
			register_rows.push_back(i);
		}
		auto const weight = [&](uint32_t i) { return size_t(trade.commodities[i].commodity.index()) < good_weight.size() ? good_weight[size_t(trade.commodities[i].commodity.index())] : 0.f; };
		auto const key_of = [&](uint32_t i) {
			auto const& l = trade.commodities[i];
			switch(goods_sort_col) {
			case 1: return domestic_coverage(l);
			case 3: return l.imported;
			case 4: return l.exported;
			case 5: return l.exported - l.imported;
			default: return weight(i);
			}
		};
		std::stable_sort(register_rows.begin(), register_rows.end(), [&](uint32_t a, uint32_t b) {
			if(goods_sort_col == 0) {
				auto const an = goods_name(state, trade.commodities[a].commodity), bn = goods_name(state, trade.commodities[b].commodity);
				return goods_sort_up ? an < bn : an > bn;
			}
			return goods_sort_up ? key_of(a) < key_of(b) : key_of(a) > key_of(b);
		});
		// the goods that did nothing over the span go last, folded into one row unless opened
		std::vector<uint32_t> idle;
		std::erase_if(register_rows, [&](uint32_t i) {
			if(!idle_good(trade.commodities[i]))
				return false;
			idle.push_back(i);
			return true;
		});
		if(!idle.empty()) {
			register_rows.push_back(idle_summary_row);
			if(show_idle_goods)
				register_rows.insert(register_rows.end(), idle.begin(), idle.end());
		}
	} else {
		for(uint32_t i = 0; i < trade_partners.size(); ++i) {
			auto const& p = trade_partners[i];
			if(register_filter == 1 && !(p.leans_share >= 0.1f))
				continue;
			if(register_filter == 2 && !(p.exported > 0.f))
				continue;
			if(!needle.empty() && folded(nation_name(state, p.nation)).find(needle) == std::string::npos)
				continue;
			register_rows.push_back(i);
		}
		auto const key_of = [&](uint32_t i) {
			auto const& p = trade_partners[i];
			switch(partners_sort_col) {
			case 1: return p.exported;
			case 2: return p.imported;
			case 3: return p.exported - p.imported;
			case 5: return p.leans_share;
			case 6: return p.exported + p.imported;
			default: return p.share;
			}
		};
		std::stable_sort(register_rows.begin(), register_rows.end(), [&](uint32_t a, uint32_t b) {
			if(partners_sort_col == 0) {
				auto const an = nation_name(state, trade_partners[a].nation), bn = nation_name(state, trade_partners[b].nation);
				return partners_sort_up ? an < bn : an > bn;
			}
			return partners_sort_up ? key_of(a) < key_of(b) : key_of(a) > key_of(b);
		});
	}
	return register_rows;
}

// ---- the search field ----

ui::focus_result on_get_focus(sys::state& state) noexcept override {
	return trade_search_focus ? ui::focus_result::accepted : ui::focus_result::ignored;
}
void on_lose_focus(sys::state& state) noexcept override {
	trade_search_focus = false;
}
void on_text(sys::state& state, char32_t ch) noexcept override {
	if(!trade_search_focus || ch < 32 || ch == 127 || trade_search.size() > 40)
		return;
	// the character as UTF-8
	if(ch < 0x80) {
		trade_search.push_back(char(ch));
	} else if(ch < 0x800) {
		trade_search.push_back(char(0xC0 | (ch >> 6)));
		trade_search.push_back(char(0x80 | (ch & 0x3F)));
	} else if(ch < 0x10000) {
		trade_search.push_back(char(0xE0 | (ch >> 12)));
		trade_search.push_back(char(0x80 | ((ch >> 6) & 0x3F)));
		trade_search.push_back(char(0x80 | (ch & 0x3F)));
	}
	goods_first = partners_first = 0;
}
void release_search(sys::state& state) {
	if(trade_search_focus && state.ui_state.edit_target_internal == this)
		state.ui_state.set_focus_target(state, nullptr);
	trade_search_focus = false;
}
// Keys while the search has the keyboard: Esc and Enter let it go, Backspace takes a character back,
// and every other key is the field's, so that no shortcut fires while the player types.
bool trade_search_key(sys::state& state, sys::virtual_key key) {
	if(!trade_search_focus)
		return false;
	if(key == sys::virtual_key::ESCAPE || key == sys::virtual_key::RETURN) {
		release_search(state);
	} else if(key == sys::virtual_key::BACK && !trade_search.empty()) {
		auto at = trade_search.size() - 1;
		while(at > 0 && (uint8_t(trade_search[at]) & 0xC0) == 0x80)
			--at;
		trade_search.erase(at);
		goods_first = partners_first = 0;
	}
	return true;
}

// ---- the window ----
//
// One composition, read from the top: the country and its balance; the gravest finding beside the
// instruments of the span; then the flows of trade across the left, where the eye goes first, and the
// register of goods or countries under them; on the right, through the whole height, what is chosen,
// or the trade position when nothing is. The flows and the register are one column: the register is as
// tall as its rows, up to a limit past which it scrolls, and the flows take what it leaves. No area
// wears a fill of its own; the right column is set off by a hairline and its room.

static constexpr float wb_pad = 20.f;
static constexpr float wb_gap = 14.f;
static constexpr float wb_head_h = 88.f;
static constexpr float wb_strip_h = 46.f;
static constexpr float wb_control_h = 34.f;
static constexpr float wb_register_head_h = 52.f;
static constexpr float wb_context_need = 600.f;
int32_t finding_at = 0; // which finding the strip shows
static constexpr int32_t findings_next = 101; // the value of action::trade_chart that shows the next finding

std::vector<trade_insight const*> trade_findings_shown() const {
	std::vector<trade_insight const*> list;
	for(auto const& ins : trade_insights) {
		if(ins.level >= 1 && ins.focus != 0)
			list.push_back(&ins);
	}
	return list;
}

void draw_trade_analysis(sys::state& state, float w, float h, bool hovering, float mx, float my) {
	if(search_focus_request) {
		search_focus_request = false;
		trade_search_focus = true;
		state.ui_state.set_focus_target(state, this);
	}
	atlas_bands_used = 0;
	auto const x0 = ribbon_w + 8.f;
	auto const y0 = top_h + 12.f;
	auto const x1 = w - 12.f;
	auto const max_h = h - 12.f - tool_h - 10.f - y0;
	auto const left = x0 + wb_pad;
	auto const right = x1 - wb_pad;

	// the register is laid out for the rows of its tab (not of its filter, so that narrowing it moves
	// nothing)
	auto const fixed = wb_head_h + wb_strip_h + wb_gap + wb_pad;
	auto const body_max = std::max(500.f, max_h - fixed);
	auto const rows = float(std::max<size_t>(1, register_rows_full()));
	auto const register_need = wb_register_head_h + columns_h + 4.f + rows * list_row_h + 8.f;
	// the flows are as tall as their rows; the register takes the rest, scrolling past it
	auto const register_least = wb_register_head_h + columns_h + 4.f + 3.f * list_row_h + 8.f;
	auto const flows_h = std::min(flows_need(), body_max - wb_gap - register_least);
	auto const body_h = std::min(body_max, std::max(wb_context_need, flows_h + wb_gap + register_need));
	auto const win_h = fixed + body_h;
	atlas_window(state, x0, y0, x1 - x0, win_h);
	draw_analysis_head(state, x0, y0, x1, hovering, mx, my);
	auto y = y0 + wb_head_h;
	atlas_rule(state, left, right, y);
	draw_strip(state, left, y, right, hovering, mx, my);
	y += wb_strip_h + wb_gap;
	auto const bottom = y + body_h;
	goods_area = rect{};
	partners_area = rect{};
	if(trade_trends) {
		draw_trends(state, left, y, right, bottom, hovering, mx, my);
		return;
	}
	auto const split = std::round(left + (right - left) * 0.665f);
	draw_flows(state, left, y, split - 24.f, y + flows_h, hovering, mx, my);
	draw_register(state, left, y + flows_h + wb_gap, split - 24.f, bottom, hovering, mx, my);
	atlas_hair(state, split, y, 1.f, body_h, 0.07f);
	draw_context(state, split + 1.f, y, right, bottom, hovering, mx, my);
}

// The head: the country and its span on the left; on the right the balance, large, with the exports and
// imports it comes from beside it; then the way back to the map and the close mark.
void draw_analysis_head(sys::state& state, float x0, float y0, float x1, bool hovering, float mx, float my) {
	auto const& v = trade;
	auto const hl = x0 + wb_pad;
	auto const hr = x1 - wb_pad;
	auto const cy = y0 + wb_head_h * 0.5f;
	atlas_close(state, hr - 28.f, cy - 14.f, action::toggle_trade, hovering, mx, my);
	constexpr float back_w = 150.f;
	auto const bx = hr - 28.f - 16.f - back_w;
	atlas_button(state, bx, cy - wb_control_h * 0.5f, back_w, wb_control_h, localized(state, "katechon_trade_back_map", "Back to map"), hit_target{ 0.f, 0.f, 0.f, 0.f, action::trade_analysis }, hovering,
		mx, my, atlas_kind::primary, icon::chevron_left);

	auto const balance_x = bx - 44.f - 190.f;
	atlas_label(state, localized(state, "katechon_trade_kpi_net", "Balance"), balance_x, y0 + 27.f, atlas_type_soft);
	atlas_figure(state, signed_money(v.exported - v.imported), balance_x, y0 + 58.f, 32, atlas_type, false);
	auto const div_x = balance_x - 28.f;
	atlas_hair(state, div_x, y0 + 20.f, 1.f, 50.f, 0.12f);
	auto const flow_figure = [&](float fx, std::string const& label, ogl::color3f swatch, float value) {
		fill(state, fx, y0 + 23.f, 8.f, 8.f, swatch, 1.f);
		atlas_label(state, label, fx + 14.f, y0 + 27.f, atlas_type_soft);
		atlas_figure(state, text::format_money(value), fx, y0 + 62.f, 22, atlas_type_soft, false);
	};
	auto const imports_x = div_x - 28.f - 140.f;
	flow_figure(imports_x, localized(state, "katechon_trade_kpi_in", "Imports"), atlas_import, v.imported);
	auto const exports_x = imports_x - 150.f;
	flow_figure(exports_x, localized(state, "katechon_trade_kpi_out", "Exports"), atlas_export, v.exported);

	if(v.nation)
		atlas_flag(state, v.nation, hl, cy - 17.f, 51.f, 34.f);
	atlas_serif(state, fit(state, nation_name(state, v.nation) + " · " + localized(state, "katechon_trade_word", "Trade"), 30, 0.004f, exports_x - hl - 110.f), hl + 66.f, y0 + 36.f, 30, atlas_type);
	sans(state, sans_fit(state, span_range_words(state), 14, exports_x - hl - 110.f), hl + 66.f, y0 + 64.f, 14, atlas_type_soft);
}

// What a finding says in one line: for a dependency, the good, its deficit and its main supplier.
std::string finding_words(sys::state& state, trade_insight const& ins) {
	for(auto const& d : trade_deps) {
		if(good_value(d.good_id) != ins.focus)
			continue;
		auto words = goods_name(state, d.good_id) + " · " + filled(localized(state, "katechon_trade_deficit_line", "%1 structural supply deficit"), text::format_percentage(d.import_share, 0));
		if(d.source)
			words += " · " + sentence(localized(state, "katechon_trade_supplies_line", "%1 supplies %2 of imports"), nation_name(state, d.source), text::format_percentage(d.source_share, 0));
		return words;
	}
	return ins.head;
}

// The strip under the head: the gravest finding on the left, a press away from its subject, the others
// one at a time behind a small counter; the instruments of the span on the right.
void draw_strip(sys::state& state, float x, float y, float right, bool hovering, float mx, float my) {
	auto const cy = y + wb_strip_h * 0.5f;
	auto rx = right;
	atlas_icon_button(state, rx - 32.f, cy - 16.f, 32.f, icon::ledger, hit_target{ 0.f, 0.f, 0.f, 0.f, action::trade_details }, hovering, mx, my, localized(state, "katechon_trade_all_tables", "Classic trade tables"));
	rx -= 32.f + 8.f;
	auto const trends_label = trade_trends ? localized(state, "katechon_trade_structure_view", "Structure") : localized(state, "katechon_trade_trends", "Trends");
	auto const tw = std::round(sans_w(state, trends_label, 14) + 42.f);
	atlas_button(state, rx - tw, cy - 16.f, tw, 32.f, trends_label, hit_target{ 0.f, 0.f, 0.f, 0.f, action::trade_chart, nullptr, trends_toggle }, hovering, mx, my, atlas_kind::quiet,
		trade_trends ? icon::ledger : icon::growth);
	rx -= tw + 22.f;
	auto const items = span_items(state);
	auto const sw = atlas_toggles_w(state, items, 14, false, 22.f);
	atlas_toggles(state, rx - sw, cy, items, trade_span, action::trade_span, hovering, mx, my, 14, false, 22.f);
	rx -= sw + 26.f;
	atlas_hair(state, rx, cy - 12.f, 1.f, 24.f, 0.12f);
	rx -= 18.f;

	auto const findings = trade_findings_shown();
	if(findings.empty()) {
		sans(state, localized(state, "katechon_trade_no_finding", "Nothing in the trade calls for attention"), x, cy, 15, atlas_type_faint);
		return;
	}
	auto const& ins = *findings[size_t(finding_at) % findings.size()];
	auto const tone = ins.level >= 2 ? atlas_risk : atlas_caution;
	auto more_w = 0.f;
	if(findings.size() > 1) {
		auto const more = std::to_string(size_t(finding_at) % findings.size() + 1) + " / " + std::to_string(findings.size()) + "  ›";
		more_w = sans_w(state, more, 14) + 18.f;
		atlas_link(state, more, rx, cy, hit_target{ 0.f, 0.f, 0.f, 0.f, action::trade_chart, nullptr, findings_next }, hovering, mx, my, true);
	}
	hit_target body{ x - 8.f, y + 5.f, rx - more_w - x, wb_strip_h - 10.f, action::trade_findings, nullptr, ins.focus };
	auto const hovered = hovering && over(body, mx, my);
	if(hovered)
		fill(state, body.x, body.y, body.w, body.h, atlas_hover, 1.f);
	atlas_lozenge(state, x + 5.f, cy, 5, tone);
	auto const lw = atlas_label(state, ins.brief_note.empty() ? level_word(state, ins.level) : ins.brief_note, x + 20.f, cy, tone);
	auto const tx = x + 20.f + lw + 14.f;
	auto const tw2 = sans(state, sans_fit(state, finding_words(state, ins), 16, body.x + body.w - tx - 30.f), tx, cy, 16, atlas_type);
	draw_icon(state, icon::chevron_right, tx + tw2 + 14.f, cy, 9.f, hovered ? atlas_accent_lit : atlas_type_faint);
	targets.push_back(body);
}

// ---- the flows ----
//
// The flows of trade as a ledger with routes between its two columns: where the imports come from on
// the left, where the exports go on the right, the country in the middle as the hub they pass through.
// Both columns keep one grid: a row a partner, the flag, the name and the value in fixed places, the
// totals of each side at the head of its column. The routes are thin, of one scale for both sides and
// never wider than a few pixels; each runs flat out of its row, crosses on one straight slope and runs
// flat into the hub. They follow what is chosen: all trade by country, one good by country, or one
// country by good, so that the diagram answers the same question as the register and the context
// beside it. A press on a row chooses it.

struct flow_node {
	dcon::nation_id nation; // a country, when the sides are countries
	dcon::commodity_id good; // a good, when the sides are the goods traded with one country
	float value = 0.f;
	int32_t rest = 0; // how many the rest together holds; 0 for a row of its own
};
static constexpr size_t flows_named = 6;
static constexpr float flows_pitch = 30.f;
static constexpr float flows_rows_top = 84.f; // from the top of the diagram to its first row
static constexpr float flows_foot = 18.f; // under the last row: the hub is centred on the rows, its name inside them but for three
static constexpr float flows_widest = 9.f; // no route is wider

// The two sides for a choice: countries for all goods (nothing chosen), countries for one good, or the
// goods traded with one country; by value, six of their own and the rest together.
std::array<std::vector<flow_node>, 2> flow_sides(dcon::commodity_id g, dcon::nation_id n) const {
	std::array<std::vector<flow_node>, 2> sides;
	for(auto const outgoing : { false, true }) {
		std::vector<flow_node> list;
		auto const take = [&](trade_link const& link, bool by_good) {
			if(auto const v = outgoing ? link.out_value : link.in_value; v >= 0.01f)
				list.push_back(by_good ? flow_node{ dcon::nation_id{}, link.good, v } : flow_node{ link.nation, dcon::commodity_id{}, v });
		};
		if(g) {
			if(size_t(g.index()) < links_by_good.size()) {
				for(auto const i : links_by_good[size_t(g.index())])
					take(trade_links[i], false);
			}
		} else if(n) {
			if(size_t(n.index()) < links_by_nation.size()) {
				for(auto const i : links_by_nation[size_t(n.index())])
					take(trade_links[i], true);
			}
		} else {
			for(auto const& p : trade_partners) {
				if(auto const v = outgoing ? p.exported : p.imported; v >= 0.01f)
					list.push_back(flow_node{ p.nation, dcon::commodity_id{}, v });
			}
		}
		std::stable_sort(list.begin(), list.end(), [](flow_node const& a, flow_node const& b) { return a.value > b.value; });
		auto& nodes = sides[outgoing ? 1 : 0];
		for(size_t k = 0; k < list.size(); ++k) {
			if(k < flows_named || list.size() == flows_named + 1) {
				nodes.push_back(list[k]);
			} else {
				if(nodes.size() == flows_named)
					nodes.push_back(flow_node{});
				nodes.back().value += list[k].value;
				nodes.back().rest += 1;
			}
		}
	}
	return sides;
}
// The rows the diagram is laid out for: those of all trade, or of the choice when it has more, so that
// choosing a good or a country does not move the register under the mouse.
size_t flows_rows() const {
	auto const all = flow_sides(dcon::commodity_id{}, dcon::nation_id{});
	auto const now = flow_sides(focus_good(), focus_partner());
	return std::max({ all[0].size(), all[1].size(), now[0].size(), now[1].size(), size_t(3) });
}
float flows_need() const {
	return flows_rows_top + float(flows_rows()) * flows_pitch + flows_foot;
}

void draw_flows(sys::state& state, float x, float y, float right, float bottom, bool hovering, float mx, float my) {
	auto const lx = x;
	auto const rx = right;
	auto const chosen_good = focus_good();
	auto const chosen_partner = focus_partner();
	auto const by_good = bool(chosen_partner) && !chosen_good;

	// the title, what the flows are narrowed to, and the mark that widens them again
	auto const title_w = atlas_serif(state, localized(state, "katechon_trade_flows_title", "Flows of trade"), lx, y + 16.f, 22, atlas_type);
	if(chosen_good || chosen_partner) {
		auto const subject = chosen_good ? goods_name(state, chosen_good) : nation_name(state, chosen_partner);
		auto const sx = lx + title_w + 16.f;
		fill(state, sx, y + 9.f, 1.f, 16.f, atlas_type, 0.16f);
		auto const sw = sans(state, subject, sx + 16.f, y + 17.f, 16, atlas_type_soft);
		atlas_icon_button(state, sx + 16.f + sw + 6.f, y + 4.f, 24.f, icon::close, hit_target{ 0.f, 0.f, 0.f, 0.f, action::trade_select, nullptr, trade_focus }, hovering, mx, my,
			localized(state, "katechon_trade_flows_all", "Show all trade"));
	}

	auto const sides = flow_sides(chosen_good, chosen_partner);
	auto const& ins = sides[0];
	auto const& outs = sides[1];
	auto total_in = 0.f;
	auto total_out = 0.f;
	auto largest = 0.f;
	for(auto const& node : ins) {
		total_in += node.value;
		largest = std::max(largest, node.value);
	}
	for(auto const& node : outs) {
		total_out += node.value;
		largest = std::max(largest, node.value);
	}

	// the grid: a column of labels each side, the values set on its inner edge, the routes between
	auto const label_w = std::clamp((rx - lx) * 0.27f, 200.f, 252.f);
	auto const in_value_r = lx + label_w;
	auto const in_x = in_value_r + 16.f;
	auto const out_label_x = rx - label_w;
	auto const out_x = out_label_x - 16.f;
	auto const head_y = y + 54.f;
	auto const in_tone = mix(atlas_import, atlas_type_soft, 0.30f);
	auto const out_tone = mix(atlas_export, atlas_type_soft, 0.30f);
	atlas_label(state, localized(state, "katechon_trade_flows_in", "Imports from"), lx, head_y, in_tone);
	atlas_figure(state, total_in > 0.f ? bare_money(total_in) : std::string("—"), in_value_r, head_y, 13, atlas_type_soft);
	atlas_rule(state, lx, in_value_r, head_y + 13.f, 0.10f);
	atlas_label(state, localized(state, "katechon_trade_flows_out", "Exports to"), out_label_x, head_y, out_tone);
	atlas_figure(state, total_out > 0.f ? bare_money(total_out) : std::string("—"), rx, head_y, 13, atlas_type_soft);
	atlas_rule(state, out_label_x, rx, head_y + 13.f, 0.10f);

	// the room is laid out for the rows of all trade (flows_rows), so that a choice moves nothing; a
	// choice with fewer rows is set in the middle of it rather than left hanging from the top
	auto const block_rows = std::max({ ins.size(), outs.size(), size_t(3) });
	auto const block_h = float(block_rows) * flows_pitch;
	auto const room_rows = std::max(block_rows, flows_rows());
	auto const row_top = y + flows_rows_top + std::floor(float(room_rows - block_rows) * flows_pitch * 0.5f);
	auto const row_cy = [&](size_t i) { return std::round(row_top + flows_pitch * (float(i) + 0.5f)); };
	if(total_in + total_out <= 0.f) {
		sans(state, filled(localized(state, "katechon_trade_quiet_border", "Nothing crossed the border: %1"), period_words(state)), lx, row_cy(0), 14, atlas_type_faint);
		return;
	}
	auto const mid_y = std::round(row_top + block_h * 0.5f);
	auto const k = std::min(flows_widest / largest, 0.36f * block_h / std::max(total_in, total_out));
	auto const thick = [&](flow_node const& node) { return std::max(1.f, std::round(node.value * k * 2.f) * 0.5f); };
	constexpr float stack_gap = 1.f; // the routes meet the hub a pixel apart, each still its own
	auto const stack_h = [&](std::vector<flow_node> const& nodes) {
		auto h = 0.f;
		for(size_t i = 0; i < nodes.size(); ++i)
			h += thick(nodes[i]) + (i > 0 ? stack_gap : 0.f);
		return h;
	};
	auto const mid = std::round((in_x + out_x) * 0.5f);
	constexpr float core_w = 3.f;
	auto const core_h = std::max(stack_h(ins), stack_h(outs)) + 8.f;
	auto const core_top = std::round(mid_y - core_h * 0.5f);

	// the row the mouse is over
	auto const row_target = [&](flow_node const& node, size_t i, bool outgoing) {
		auto const value = node.rest ? 0 : (node.good ? good_value(node.good) : partner_value(node.nation));
		auto const cy = row_cy(i);
		return outgoing ? hit_target{ out_x - 6.f, cy - flows_pitch * 0.5f, rx - out_x + 14.f, flows_pitch, action::trade_findings, nullptr, value }
			: hit_target{ lx - 8.f, cy - flows_pitch * 0.5f, in_x - lx + 14.f, flows_pitch, action::trade_findings, nullptr, value };
	};
	auto hover_side = -1;
	size_t hover_row = 0;
	for(auto const outgoing : { false, true }) {
		auto const& nodes = outgoing ? outs : ins;
		for(size_t i = 0; i < nodes.size(); ++i) {
			if(!nodes[i].rest && hovering && over(row_target(nodes[i], i, outgoing), mx, my)) {
				hover_side = outgoing ? 1 : 0;
				hover_row = i;
			}
		}
	}

	// the routes, stacked at the hub in the order of their rows
	for(auto const outgoing : { false, true }) {
		auto const& nodes = outgoing ? outs : ins;
		auto at = std::round(mid_y - stack_h(nodes) * 0.5f);
		auto const colour = outgoing ? atlas_export : atlas_import;
		for(size_t i = 0; i < nodes.size(); ++i) {
			auto const t = thick(nodes[i]);
			auto const lit = hover_side == (outgoing ? 1 : 0) && hover_row == i;
			auto const alpha = hover_side < 0 ? (nodes[i].rest ? 0.42f : 0.74f) : (lit ? 1.f : 0.16f);
			auto const row_y = row_cy(i) - t * 0.5f;
			if(outgoing)
				atlas_route(state, mid + core_w * 0.5f + 1.f, at, out_x, row_y, t, lit ? mix(colour, atlas_type, 0.2f) : colour, alpha);
			else
				atlas_route(state, in_x, row_y, mid - core_w * 0.5f - 1.f, at, t, lit ? mix(colour, atlas_type, 0.2f) : colour, alpha);
			at += t + stack_gap;
		}
	}

	// the hub: a slender core where the routes meet, on a hairline axis capped at both ends; the flag
	// over the axis, the name under it
	auto const axis_top = core_top - 14.f;
	auto const axis_bottom = core_top + core_h + 14.f;
	atlas_hair(state, mid, axis_top, 1.f, axis_bottom - axis_top, 0.30f);
	atlas_hair(state, mid - 5.f, axis_top, 11.f, 1.f, 0.42f);
	atlas_hair(state, mid - 5.f, axis_bottom - 1.f, 11.f, 1.f, 0.42f);
	fill(state, mid - core_w * 0.5f + 0.5f, core_top, core_w, core_h, atlas_home, 0.95f);
	if(trade.nation) {
		atlas_flag(state, trade.nation, mid + 0.5f - 12.f, axis_top - 22.f, 24.f, 16.f);
		auto const name = upper(nation_name(state, trade.nation));
		auto const nw = sans_w(state, name, 12, 0.08f);
		sans(state, name, std::round(mid + 0.5f - nw * 0.5f), axis_bottom + 14.f, 12, atlas_type_soft, false, 0.08f);
	}

	// the two columns: a tick where the route leaves, the mark, the name and the value on the grid
	for(auto const outgoing : { false, true }) {
		auto const& nodes = outgoing ? outs : ins;
		auto const colour = outgoing ? atlas_export : atlas_import;
		for(size_t i = 0; i < nodes.size(); ++i) {
			auto const& node = nodes[i];
			auto const cy = row_cy(i);
			auto const t = row_target(node, i, outgoing);
			auto const lit = hover_side == (outgoing ? 1 : 0) && hover_row == i;
			auto const dim = hover_side >= 0 && !lit;
			if(lit)
				fill(state, t.x, t.y + 1.f, t.w, t.h - 2.f, atlas_hover, 1.f);
			auto const th = thick(node);
			fill(state, outgoing ? out_x - 2.f : in_x, std::round(cy - th * 0.5f), 2.f, th, colour, dim ? 0.4f : 1.f);
			auto const name_x = outgoing ? out_label_x : lx;
			auto const value_r = outgoing ? rx : in_value_r;
			auto tx = name_x;
			if(node.nation) {
				atlas_flag(state, node.nation, name_x, cy - 6.f, 18.f, 12.f);
				tx += 28.f;
			} else if(node.good) {
				good_icon(state, node.good, name_x + 9.f, cy, 16.f, dim ? atlas_type_faint : atlas_type_soft);
				tx += 28.f;
			} else {
				tx += 28.f; // the rest keeps the column of the names
			}
			auto const vw = atlas_figure(state, bare_money(node.value), value_r, cy, 14, dim || node.rest ? atlas_type_faint : atlas_type_soft);
			auto const name = node.rest ? filled(by_good ? localized(state, "katechon_trade_other_goods_n", "Other goods (%1)") : localized(state, "katechon_trade_other_parts", "Other (%1)"), std::to_string(node.rest))
				: (node.good ? goods_name(state, node.good) : nation_name(state, node.nation));
			sans(state, sans_fit(state, name, 14, value_r - vw - 16.f - tx), tx, cy, 14, node.rest || dim ? atlas_type_faint : (lit ? atlas_accent_lit : atlas_type));
			if(!node.rest)
				targets.push_back(t);
		}
	}
	if(ins.empty())
		sans(state, localized(state, "katechon_trade_nothing_in", "Nothing came in"), lx + 28.f, row_cy(0), 14, atlas_type_faint);
	if(outs.empty())
		sans(state, localized(state, "katechon_trade_nothing_out", "Nothing went out"), out_label_x + 28.f, row_cy(0), 14, atlas_type_faint);
}

// The search field: a sunk well, brass-edged while it has the keyboard.
void draw_search(sys::state& state, float x, float y, float w, float h, bool hovering, float mx, float my) {
	hit_target t{ x, y, w, h, action::trade_sort, nullptr, register_search_focus };
	auto const hovered = hovering && over(t, mx, my);
	fill(state, x, y, w, h, atlas_deep, 1.f);
	if(trade_search_focus)
		frame(state, x, y, w, h, atlas_accent, 0.8f);
	else
		atlas_outline(state, x, y, w, h, hovered ? 0.18f : 0.08f);
	draw_icon(state, icon::search, x + 17.f, y + h * 0.5f, 13.f, trade_search_focus ? atlas_accent_lit : atlas_type_soft);
	auto const tx = x + 34.f;
	auto const room = w - 34.f - (trade_search.empty() ? 10.f : 34.f);
	if(trade_search.empty() && !trade_search_focus) {
		sans(state, register_tab == 0 ? localized(state, "katechon_trade_search_goods", "Search goods") : localized(state, "katechon_trade_search_countries", "Search countries"), tx, y + h * 0.5f, 14, atlas_type_faint);
	} else {
		auto shown = trade_search;
		while(!shown.empty() && sans_w(state, shown, 14) > room) {
			shown.erase(0, 1);
			while(!shown.empty() && (uint8_t(shown[0]) & 0xC0) == 0x80)
				shown.erase(0, 1);
		}
		auto const sw = sans(state, shown, tx, y + h * 0.5f, 14, atlas_type);
		if(trade_search_focus && (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() / 530) % 2 == 0)
			fill(state, tx + sw + 2.f, y + 8.f, 1.f, h - 16.f, atlas_type, 1.f);
	}
	targets.push_back(t);
	if(!trade_search.empty())
		atlas_icon_button(state, x + w - 30.f, y + (h - 26.f) * 0.5f, 26.f, icon::close, hit_target{ 0.f, 0.f, 0.f, 0.f, action::trade_sort, nullptr, register_search_clear }, hovering, mx, my,
			localized(state, "katechon_trade_clear_search", "Clear the search"));
}

// ---- the register ----

struct trade_column {
	std::string heading;
	float right = 0.f;
	bool numeric = true;
	int32_t sort = 0;
	std::string note; // what the figures are, for the mouse
};

// The heads of the columns on a sunk strip; a press sorts by the column, a second press turns the order.
void list_head(sys::state& state, std::vector<trade_column> const& columns, int32_t table, int32_t sorted, bool up, float left, float right, float y, bool hovering, float mx, float my) {
	auto at = left;
	for(auto const& c : columns) {
		hit_target t{ at - 6.f, y + 3.f, c.right - at + 12.f, columns_h - 6.f, action::trade_sort, nullptr, table * 100 + c.sort };
		auto const hovered = hovering && over(t, mx, my);
		auto const on = c.sort == sorted;
		if(hovered)
			fill(state, t.x, t.y, t.w, t.h, atlas_hover, 1.f);
		auto const colour = on || hovered ? atlas_type : atlas_type_soft;
		auto const cy = y + columns_h * 0.5f;
		auto const lw = c.numeric ? sans(state, c.heading, c.right, cy, 13, colour, true) : sans(state, c.heading, at, cy, 13, colour);
		if(on)
			arrow(state, c.numeric ? c.right - lw - 10.f : at + lw + 6.f, cy, up, atlas_accent);
		if(hovered && !c.note.empty())
			hint(c.note, t);
		targets.push_back(t);
		at = c.right + 14.f;
	}
}

void scroll_mark(sys::state& state, float x, float y, float h, int32_t first, int32_t view, int32_t total) {
	if(total <= view || total <= 0)
		return;
	fill(state, x, y, 2.f, h, atlas_track, 1.f);
	auto const bh = std::max(24.f, h * float(view) / float(total));
	auto const by = y + (h - bh) * float(first) / float(std::max(1, total - view));
	fill(state, x, by, 2.f, bh, atlas_type_faint, 1.f);
}

// How short a good is, in words and in the colour of how much it matters.
static ogl::color3f shortage_tone(float shortage) {
	return shortage >= 0.5f ? atlas_risk : (shortage >= 0.25f ? atlas_caution : atlas_type_soft);
}

// The rows the register is laid out for: every row it can show on this tab, whatever is filtered or
// searched, so that the register and what is under it keep their places while the player narrows it.
size_t register_rows_full() const {
	if(register_tab == 1)
		return trade_partners.size();
	auto const idle = idle_goods_count();
	return trade.commodities.size() - idle + (idle > 0 ? 1 : 0) + (show_idle_goods ? idle : 0);
}

void draw_register(sys::state& state, float x, float y, float right, float bottom, bool hovering, float mx, float my) {
	if(auto const g = focus_good(); g && reveal_pending == good_value(g) && !show_idle_goods) {
		if(auto const* l = line_for(g); l && idle_good(*l))
			show_idle_goods = true;
	}
	auto const& rows = register_order(state);
	auto const panel_bottom = bottom;
	auto const px = x + 12.f;
	auto const pr = right - 12.f;
	// the head of the register: what it lists in the serif, which of it, and the search
	{
		auto const cy = y + wb_register_head_h * 0.5f;
		std::vector<std::pair<std::string, int32_t>> tabs_{ { localized(state, "katechon_trade_goods", "Goods"), register_tab_base }, { localized(state, "katechon_trade_countries", "Countries"), register_tab_base + 1 } };
		auto const tw = atlas_toggles(state, x, cy - 2.f, tabs_, register_tab_base + register_tab, action::trade_sort, hovering, mx, my, 22, true, 28.f);
		size_t counts[3] = { 0, 0, 0 };
		if(register_tab == 0) {
			counts[0] = trade.commodities.size();
			for(auto const& l : trade.commodities) {
				counts[1] += l.dependence() >= 0.1f ? 1 : 0;
				counts[2] += l.exported > 0.f ? 1 : 0;
			}
		} else {
			counts[0] = trade_partners.size();
			for(auto const& p : trade_partners) {
				counts[1] += p.leans_share >= 0.1f ? 1 : 0;
				counts[2] += p.exported > 0.f ? 1 : 0;
			}
		}
		std::vector<std::pair<std::string, int32_t>> filters{ { localized(state, "katechon_trade_filter_all", "All") + " " + std::to_string(counts[0]), register_filter_base },
			{ localized(state, "katechon_trade_filter_deps", "Import dependencies") + " " + std::to_string(counts[1]), register_filter_base + 1 },
			{ localized(state, "katechon_trade_filter_exports", "Exports") + " " + std::to_string(counts[2]), register_filter_base + 2 } };
		auto const fx = x + tw + 44.f;
		atlas_hair(state, fx - 22.f, cy - 11.f, 1.f, 22.f, 0.12f);
		auto const fw = atlas_toggles(state, fx, cy, filters, register_filter_base + register_filter, action::trade_sort, hovering, mx, my, 14, false, 20.f);
		auto const search_w = std::clamp(pr - (fx + fw + 24.f), 150.f, 250.f);
		draw_search(state, right - search_w, cy - 16.f, search_w, 32.f, hovering, mx, my);
	}
	auto const top = y + wb_register_head_h;
	atlas_rule(state, x, right, top + columns_h - 1.f, 0.10f);
	auto const ry0 = top + columns_h + 4.f;
	auto const room = panel_bottom - ry0 - 6.f;
	auto& first = register_tab == 0 ? goods_first : partners_first;
	auto& total = register_tab == 0 ? goods_total : partners_total;
	auto& view = register_tab == 0 ? goods_view : partners_view;
	total = int32_t(rows.size());
	view = std::max(1, int32_t(room / list_row_h));
	// a choice made elsewhere is brought into view
	if(reveal_pending != 0 && (reveal_pending > 0) == (register_tab == 0)) {
		for(int32_t r = 0; r < total; ++r) {
			if(rows[size_t(r)] == idle_summary_row)
				continue;
			auto const value = register_tab == 0 ? good_value(trade.commodities[rows[size_t(r)]].commodity) : partner_value(trade_partners[rows[size_t(r)]].nation);
			if(value == reveal_pending) {
				if(r < first || r >= first + view)
					first = std::max(0, r - view / 2);
				break;
			}
		}
	}
	first = std::clamp(first, 0, std::max(0, total - view));
	(register_tab == 0 ? goods_area : partners_area) = rect{ x, ry0, right - x, room };
	scroll_mark(state, right - 6.f, ry0, room, first, view, total);
	auto const row_x = x + 4.f;
	auto const row_w = right - x - 14.f;

	if(register_tab == 0) {
		// Four short figures a good, none of which can be read against another: the part of demand met
		// from home, as a bar and a share (red where production itself falls short, not only the trade);
		// imports and exports in money; their balance in money. How output and demand split between home
		// and abroad is the context's to explain.
		auto const net_r = pr;
		auto const exports_r = net_r - 128.f;
		auto const imports_r = exports_r - 112.f;
		auto const cover_r = imports_r - 136.f;
		constexpr float bar_w = 120.f;
		auto const bar_x = cover_r - 52.f - bar_w;
		auto const name_r = bar_x - 20.f;
		std::vector<trade_column> columns{ { localized(state, "katechon_trade_col_good", "Commodity"), name_r, false, 0 },
			{ localized(state, "katechon_trade_col_home", "Met from home"), cover_r, true, 1,
				localized(state, "katechon_trade_home_note", "Of all the good the country bought, the part its own producers supplied; the rest came from abroad") },
			{ localized(state, "katechon_trade_col_imports", "Imports, £"), imports_r, true, 3, localized(state, "katechon_trade_imports_note", "What came in over the span") },
			{ localized(state, "katechon_trade_col_exports", "Exports, £"), exports_r, true, 4, localized(state, "katechon_trade_exports_note", "What went out over the span") },
			{ localized(state, "katechon_trade_col_net", "Net, £"), net_r, true, 5, localized(state, "katechon_trade_net_note", "Exports less imports") } };
		list_head(state, columns, 0, goods_sort_col, goods_sort_up, px, pr, top, hovering, mx, my);
		if(rows.empty())
			sans(state, localized(state, "katechon_trade_nothing_found", "Nothing matches"), px, ry0 + 20.f, 15, atlas_type_soft);
		auto const partner = focus_partner();
		auto const chosen = focus_good();
		auto ry = ry0;
		for(int32_t r = first; r < total && r < first + view; ++r) {
			if(rows[size_t(r)] == idle_summary_row) {
				hit_target t{ row_x, ry, row_w, list_row_h, action::trade_sort, nullptr, register_idle_toggle };
				auto const hovered = hovering && over(t, mx, my);
				atlas_row(state, t, hovered, false, false);
				auto const cy = ry + list_row_h * 0.5f;
				draw_icon(state, show_idle_goods ? icon::chevron_down : icon::chevron_right, px + 9.f, cy, 10.f, hovered ? atlas_accent_lit : atlas_type_faint);
				sans(state, filled(localized(state, "katechon_trade_idle_goods", "%1 goods with no production, demand or trade over the span"), std::to_string(idle_goods_count())), px + 28.f, cy, 14,
					hovered ? atlas_type_soft : atlas_type_faint);
				targets.push_back(t);
				ry += list_row_h;
				continue;
			}
			auto const& l = trade.commodities[rows[size_t(r)]];
			hit_target t{ row_x, ry, row_w, list_row_h, action::trade_select, nullptr, good_value(l.commodity) };
			auto const hovered = hovering && over(t, mx, my);
			auto const is_chosen = l.commodity == chosen;
			atlas_row(state, t, hovered, is_chosen, partner && link_of(l.commodity, partner));
			auto const quiet = idle_good(l);
			auto const main = quiet ? atlas_type_faint : atlas_type;
			auto const cy = ry + list_row_h * 0.5f;
			good_icon(state, l.commodity, px + 10.f, cy, 18.f, quiet ? atlas_type_faint : (is_chosen ? atlas_accent_lit : atlas_type_soft));
			sans(state, sans_fit(state, goods_name(state, l.commodity), 16, name_r - px - 32.f), px + 32.f, cy, 16, is_chosen ? atlas_accent_lit : main);
			auto const shortage = l.dependence();
			if(auto const cover = domestic_coverage(l); cover >= 0.f) {
				auto const lit = std::round(bar_w * cover);
				if(lit >= 1.f)
					fill(state, bar_x, cy - 2.f, lit, 4.f, atlas_home, 0.85f);
				if(lit < bar_w)
					fill(state, bar_x + lit, cy - 2.f, bar_w - lit, 4.f, shortage >= 0.25f ? atlas_risk : atlas_import, shortage >= 0.25f ? 0.9f : 0.55f);
				atlas_figure(state, share_words(cover), cover_r, cy, 15, shortage >= 0.25f ? shortage_tone(shortage) : main);
			} else if(l.sold >= 0.01f) {
				sans(state, localized(state, "katechon_trade_no_home_demand", "made, not used at home"), cover_r, cy, 13, atlas_type_faint, true);
			} else {
				sans(state, "—", cover_r, cy, 15, atlas_type_faint, true);
			}
			atlas_figure(state, l.imported > 0.f ? bare_money(l.imported) : std::string("—"), imports_r, cy, 15, l.imported > 0.f ? main : atlas_type_faint);
			atlas_figure(state, l.exported > 0.f ? bare_money(l.exported) : std::string("—"), exports_r, cy, 15, l.exported > 0.f ? main : atlas_type_faint);
			if(l.exported > 0.f || l.imported > 0.f)
				atlas_figure(state, signed_bare_money(l.exported - l.imported), net_r, cy, 15, atlas_type_soft);
			else
				sans(state, "—", net_r, cy, 15, atlas_type_faint, true);
			targets.push_back(t);
			ry += list_row_h;
		}
	} else {
		// Turnover on one scale for every country the register holds: the largest fills the column, the
		// others in proportion, each split into what we sell them and what we buy from them.
		auto const share_r = pr;
		auto const balance_r = share_r - 80.f;
		auto const imports_r = balance_r - 118.f;
		auto const exports_r = imports_r - 106.f;
		auto const bar_r = exports_r - 100.f;
		auto const bar_w = std::clamp(bar_r - px - 240.f, 120.f, 260.f);
		auto const bar_x = bar_r - bar_w;
		auto const name_r = bar_x - 20.f;
		std::vector<trade_column> columns{ { localized(state, "katechon_trade_col_country", "Country"), name_r, false, 0 },
			{ localized(state, "katechon_trade_col_turnover", "Turnover"), bar_r, false, 6, localized(state, "katechon_trade_turnover_note", "Exports and imports together, on one scale for every country listed") },
			{ localized(state, "katechon_trade_col_exports", "Exports, £"), exports_r, true, 1 }, { localized(state, "katechon_trade_col_imports", "Imports, £"), imports_r, true, 2 },
			{ localized(state, "katechon_trade_col_balance", "Balance, £"), balance_r, true, 3 },
			{ localized(state, "katechon_trade_col_share", "Share"), share_r, true, 4, localized(state, "katechon_trade_partners_aside", "share of exports + imports") } };
		list_head(state, columns, 1, partners_sort_col, partners_sort_up, px, pr, top, hovering, mx, my);
		if(rows.empty())
			sans(state, trade_partners.empty() ? filled(localized(state, "katechon_trade_quiet_border", "Nothing crossed the border: %1"), period_words(state)) : localized(state, "katechon_trade_nothing_found", "Nothing matches"), px,
				ry0 + 22.f, 15, atlas_type_soft);
		auto largest = 0.f;
		for(auto const i : rows)
			largest = std::max(largest, trade_partners[i].exported + trade_partners[i].imported);
		auto const good = focus_good();
		auto const chosen = focus_partner();
		auto ry = ry0;
		for(int32_t r = first; r < total && r < first + view; ++r) {
			auto const& p = trade_partners[rows[size_t(r)]];
			hit_target t{ row_x, ry, row_w, list_row_h, action::trade_select, nullptr, partner_value(p.nation) };
			auto const hovered = hovering && over(t, mx, my);
			atlas_row(state, t, hovered, p.nation == chosen, good && link_of(good, p.nation));
			auto const cy = ry + list_row_h * 0.5f;
			atlas_flag(state, p.nation, px, cy - 7.f, 21.f, 14.f);
			sans(state, sans_fit(state, nation_name(state, p.nation), 16, name_r - px - 34.f), px + 34.f, cy, 16, p.nation == chosen ? atlas_accent_lit : atlas_type);
			auto const both = p.exported + p.imported;
			if(both > 0.f && largest > 0.f) {
				auto const length = std::max(2.f, std::round(bar_w * both / largest));
				auto const out_w = std::round(length * p.exported / both);
				if(out_w >= 1.f)
					fill(state, bar_x, cy - 2.f, out_w, 4.f, atlas_export, 1.f);
				if(length - out_w >= 1.f)
					fill(state, bar_x + out_w, cy - 2.f, length - out_w, 4.f, atlas_import, 1.f);
				hit_target bar{ bar_x - 4.f, cy - 12.f, bar_w + 8.f, 24.f };
				if(hovering && over(bar, mx, my))
					hint(sentence(localized(state, "katechon_trade_turnover_hint", "Exports %1 · Imports %2"), text::format_money(p.exported), text::format_money(p.imported)) + " · "
						+ filled(localized(state, "katechon_trade_turnover_is", "Turnover %1"), text::format_money(both)), bar);
			}
			atlas_figure(state, p.exported > 0.f ? bare_money(p.exported) : std::string("—"), exports_r, cy, 15, p.exported > 0.f ? atlas_type : atlas_type_faint);
			atlas_figure(state, p.imported > 0.f ? bare_money(p.imported) : std::string("—"), imports_r, cy, 15, p.imported > 0.f ? atlas_type : atlas_type_faint);
			atlas_figure(state, signed_bare_money(p.exported - p.imported), balance_r, cy, 15, atlas_type_soft);
			atlas_figure(state, p.share >= 0.01f ? text::format_percentage(p.share, 0) : std::string("<1%"), share_r, cy, 15, p.share >= 0.01f ? atlas_type : atlas_type_soft);
			targets.push_back(t);
			ry += list_row_h;
		}
	}
}

// ---- the context ----
//
// The right column says only what the register cannot: for a good, where its demand came from and where
// its output went, on one scale so that the part made and used at home is seen to be the same in both,
// and in words why a good can be imported and exported at once; then its suppliers, its buyers and what
// would happen if the supply from abroad stopped. For a country, the goods each way and what losing its
// trade would cost. With nothing chosen, the trade position. Shares, not sums: the money is in the
// register.

// A heading in the context: small capitals, and what the figures under it are on the right.
float context_title(sys::state& state, std::string const& title, std::string const& aside, float x, float right, float y) {
	atlas_label(state, title, x, y + 10.f, atlas_type_soft);
	if(!aside.empty())
		sans(state, aside, right, y + 10.f, 13, atlas_type_faint, true);
	return y + 26.f;
}

// A bar of parts on one scale: each part in its colour, a pixel apart, the rest of the scale left empty.
void parts_bar(sys::state& state, float x, float y, float w, std::vector<std::pair<float, ogl::color3f>> const& parts, float whole, float h = 8.f) {
	auto at = x;
	if(whole > 0.f) {
		for(auto const& [value, colour] : parts) {
			auto const pw = std::round(w * value / whole);
			if(pw >= 1.f && at < x + w)
				fill(state, at, y, std::max(1.f, std::min(pw, x + w - at) - 1.f), h, colour, 1.f);
			at += pw;
		}
	}
}

// A row of a key: its colour, a flag or a good's mark, the name and its share. With a value it goes to
// what it names.
float key_row(sys::state& state, int32_t value, ogl::color3f const* swatch, dcon::nation_id n, dcon::commodity_id g, std::string const& name, std::string const& share, ogl::color3f share_tone, float x,
	float right, float y, bool hovering, float mx, float my) {
	constexpr float h = 28.f;
	hit_target t{ x - 8.f, y, right - x + 16.f, h, action::trade_findings, nullptr, value };
	auto const hovered = value != 0 && hovering && over(t, mx, my);
	if(hovered)
		fill(state, t.x, t.y, t.w, t.h, atlas_hover, 1.f);
	auto const cy = y + h * 0.5f;
	auto tx = x;
	if(swatch) {
		fill(state, x, cy - 4.f, 8.f, 8.f, *swatch, 1.f);
		tx += 18.f;
	}
	if(n) {
		atlas_flag(state, n, tx, cy - 6.f, 18.f, 12.f);
		tx += 27.f;
	} else if(g) {
		good_icon(state, g, tx + 8.f, cy, 16.f, atlas_type_soft);
		tx += 25.f;
	}
	auto const sr = right - (value != 0 ? 18.f : 0.f);
	auto const sw = atlas_figure(state, share, sr, cy, 15, share_tone);
	sans(state, sans_fit(state, name, 15, sr - sw - 14.f - tx), tx, cy, 15, hovered ? atlas_accent_lit : atlas_type);
	if(value != 0) {
		draw_icon(state, icon::chevron_right, right - 3.f, cy, 8.f, hovered ? atlas_accent_lit : atlas_type_faint);
		targets.push_back(t);
	}
	return y + h;
}

void draw_context(sys::state& state, float x, float y, float right, float bottom, bool hovering, float mx, float my) {
	auto const cx = x + 24.f;
	auto const cr = right;
	auto const actions_y = bottom - wb_control_h;
	auto yy = y + 4.f;

	if(auto const g = focus_good(); g && line_for(g)) {
		draw_good_context(state, g, *line_for(g), cx, cr, yy, actions_y - 12.f, hovering, mx, my);
		atlas_button(state, cx, actions_y, 168.f, wb_control_h, localized(state, "katechon_trade_show_map", "Show on map"), hit_target{ 0.f, 0.f, 0.f, 0.f, action::trade_focus, nullptr, good_value(g) }, hovering, mx, my,
			atlas_kind::primary, icon::chevron_right);
		return;
	}
	if(auto const n = focus_partner(); n && partner_view(n)) {
		draw_partner_context(state, n, *partner_view(n), cx, cr, yy, actions_y - 12.f, hovering, mx, my);
		atlas_button(state, cx, actions_y, 168.f, wb_control_h, localized(state, "katechon_trade_show_map", "Show on map"), hit_target{ 0.f, 0.f, 0.f, 0.f, action::trade_focus, nullptr, partner_value(n) }, hovering, mx,
			my, atlas_kind::primary, icon::chevron_right);
		atlas_button(state, cx + 180.f, actions_y, 150.f, wb_control_h, localized(state, "katechon_trade_talks", "Open talks"), hit_target{ 0.f, 0.f, 0.f, 0.f, action::diplomacy_target, nullptr, nation_value(n) }, hovering,
			mx, my, atlas_kind::quiet);
		return;
	}
	draw_position_context(state, cx, cr, yy, bottom, hovering, mx, my);
}

// The head of a chosen thing: its mark, its name in the serif, one quiet line under it, the close mark.
float context_head(sys::state& state, float cx, float cr, float yy, std::string const& name, std::string const& under, dcon::nation_id n, dcon::commodity_id g, bool hovering, float mx, float my) {
	atlas_icon_button(state, cr - 28.f, yy, 28.f, icon::close, hit_target{ 0.f, 0.f, 0.f, 0.f, action::trade_select, nullptr, trade_focus }, hovering, mx, my,
		localized(state, "katechon_trade_close_details", "Close details"));
	auto tx = cx;
	if(n) {
		atlas_flag(state, n, cx, yy + 4.f, 30.f, 20.f);
		tx += 42.f;
	} else if(g) {
		good_icon(state, g, cx + 11.f, yy + 14.f, 22.f, atlas_type);
		tx += 32.f;
	}
	atlas_serif(state, fit(state, name, 24, 0.004f, cr - tx - 40.f), tx, yy + 14.f, 24, atlas_type);
	if(!under.empty())
		sans(state, sans_fit(state, under, 14, cr - cx - 40.f), cx, yy + 40.f, 14, atlas_type_faint);
	return yy + 62.f;
}

// The parts of a side of a good, a country each and the rest together: what a key and its bar show.
struct side_part {
	dcon::nation_id nation; // none for the rest
	float quantity = 0.f;
	int32_t rest = 0;
};
static std::vector<side_part> side_parts(std::vector<trade_link const*> const& links, bool outgoing, size_t most) {
	std::vector<side_part> out;
	for(size_t k = 0; k < links.size(); ++k) {
		auto const q = outgoing ? links[k]->out_quantity : links[k]->in_quantity;
		if(k < most || links.size() == most + 1) {
			out.push_back(side_part{ links[k]->nation, q });
		} else {
			if(out.size() == most)
				out.push_back(side_part{});
			out.back().quantity += q;
			out.back().rest += 1;
		}
	}
	return out;
}
// The colour of the k-th part of a side: the direction's colour, a step darker each.
static ogl::color3f side_tone(bool outgoing, size_t k, bool rest) {
	auto const base = outgoing ? atlas_export : atlas_import;
	return rest ? mix(base, atlas_body, 0.62f) : mix(base, atlas_body, std::min(0.5f, 0.17f * float(k)));
}

// A good: demand and output on one scale, why both ways, suppliers, buyers, and the cost of a stop.
void draw_good_context(sys::state& state, dcon::commodity_id g, economy::trade_picture::commodity_line const& l, float cx, float cr, float yy, float floor_y, bool hovering, float mx, float my) {
	auto const k_of = weight_of_good(state, g);
	yy = context_head(state, cx, cr, yy, goods_name(state, g), k_of > 0 ? group_word(state, k_of) : std::string(), dcon::nation_id{}, g, hovering, mx, my);
	if(idle_good(l)) {
		sans_paragraph(state, filled(localized(state, "katechon_trade_idle_good", "No production, demand or trade: %1"), period_words(state)), cx, cr, yy, 15, atlas_type_soft, 3, true);
		return;
	}
	auto const room_for = [&](float h) { return yy + h <= floor_y; };
	auto const units = [&](float q) { return text::prettify_float(q); };

	auto const& links = size_t(g.index()) < links_by_good.size() ? links_by_good[size_t(g.index())] : std::vector<uint32_t>{};
	std::vector<trade_link const*> suppliers;
	std::vector<trade_link const*> buyers;
	for(auto const i : links) {
		if(trade_links[i].in_quantity > 0.f)
			suppliers.push_back(&trade_links[i]);
		if(trade_links[i].out_quantity > 0.f)
			buyers.push_back(&trade_links[i]);
	}
	std::stable_sort(suppliers.begin(), suppliers.end(), [](auto const* a, auto const* b) { return a->in_quantity > b->in_quantity; });
	std::stable_sort(buyers.begin(), buyers.end(), [](auto const* a, auto const* b) { return a->out_quantity > b->out_quantity; });
	auto const in_parts = side_parts(suppliers, false, 3);
	auto const out_parts = side_parts(buyers, true, 3);

	// Demand and output on one scale. What was made and used at home stands at the start of both bars,
	// the same length in each: demand runs on with the imports, output with the exports.
	auto const home = std::max(0.f, l.bought - l.imported_quantity);
	auto const scale = std::max({ l.bought, l.sold, 0.01f });
	auto const bar_w = cr - cx;
	auto const bar_row = [&](std::string const& label, float total, bool outgoing, std::vector<side_part> const& parts) {
		atlas_label(state, label, cx, yy + 8.f, atlas_type_soft);
		atlas_figure(state, total > 0.f ? units(total) + " " + localized(state, "katechon_trade_units_short", "units") : std::string("—"), cr, yy + 8.f, 14, atlas_type_soft);
		std::vector<std::pair<float, ogl::color3f>> segments{ { home, atlas_home } };
		for(size_t k = 0; k < parts.size(); ++k)
			segments.emplace_back(parts[k].quantity, side_tone(outgoing, k, !parts[k].nation));
		fill(state, cx, yy + 22.f, bar_w, 8.f, atlas_track, 0.6f);
		parts_bar(state, cx, yy + 22.f, bar_w, segments, scale);
		yy += 44.f;
	};
	auto const bars_top = yy + 18.f;
	bar_row(localized(state, "katechon_trade_demand_bar", "Demand"), l.bought, false, in_parts);
	bar_row(localized(state, "katechon_trade_output_bar", "Output"), l.sold, true, out_parts);
	if(home > 0.f) {
		auto const hx = std::round(cx + bar_w * home / scale);
		atlas_hair(state, hx - 1.f, bars_top, 1.f, yy - bars_top - 6.f, 0.30f);
	}
	yy += 4.f;

	// why a good can come in and go out at once, in words
	{
		std::string words;
		if(l.bought > 0.f && home < 0.01f)
			words = localized(state, "katechon_trade_why_none_home", "None of what was used here was made here.");
		else if(l.bought > 0.f)
			words = filled(localized(state, "katechon_trade_why_home", "%1 units were made and used at home."), units(home));
		else
			words = localized(state, "katechon_trade_why_no_use", "Nothing of it was used at home.");
		if(l.imported_quantity > 0.f && l.exported_quantity > 0.f) {
			words += " " + filled(localized(state, "katechon_trade_why_both", "Besides, %1 came in and %2 went out: regions near a border buy abroad while others sell there."), units(l.imported_quantity),
				units(l.exported_quantity));
			auto const net = l.exported_quantity - l.imported_quantity;
			words += " " + filled(net >= 0.f ? localized(state, "katechon_trade_why_net_out", "Net, %1 more went out than came in.") : localized(state, "katechon_trade_why_net_in", "Net, %1 more came in than went out."),
				units(std::abs(net)));
		} else if(l.imported_quantity > 0.f) {
			words += " " + filled(localized(state, "katechon_trade_why_in", "The other %1 came in from abroad."), units(l.imported_quantity));
		} else if(l.exported_quantity > 0.f) {
			words += " " + filled(localized(state, "katechon_trade_why_out", "Another %1 went abroad."), units(l.exported_quantity));
		}
		if(room_for(sans_paragraph(state, words, cx, cr, yy, 14, atlas_type_soft, 4, false) - yy))
			yy = sans_paragraph(state, words, cx, cr, yy, 14, atlas_type_soft, 4, true) + 14.f;
	}

	// who supplies it, as parts of demand; who buys it, as parts of output; the room for what a stop
	// would cost is kept first
	auto const stop_h = l.imported_quantity > 0.f && l.bought > 0.f ? 26.f + 32.f + 64.f : (l.exported_quantity > 0.f && l.sold > 0.f ? 26.f + 32.f : 0.f);
	auto const key = [&](std::vector<side_part> const& parts, bool outgoing, float whole, std::string const& title, std::string const& aside) {
		if(parts.empty() || !room_for(26.f + 28.f * 2.f + stop_h))
			return;
		yy = context_title(state, title, aside, cx, cr, yy);
		for(size_t k = 0; k < parts.size() && room_for(28.f + 12.f + stop_h); ++k) {
			auto const& part = parts[k];
			auto const tone = side_tone(outgoing, k, !part.nation);
			auto const share = whole > 0.f ? std::min(1.f, part.quantity / whole) : 0.f;
			auto const name = part.nation ? nation_name(state, part.nation)
				: filled(outgoing ? localized(state, "katechon_trade_other_buyers", "Other buyers (%1)") : localized(state, "katechon_trade_other_suppliers", "Other suppliers (%1)"), std::to_string(part.rest));
			yy = key_row(state, part.nation ? partner_value(part.nation) : 0, &tone, part.nation, dcon::commodity_id{}, name, share_words(share),
				!outgoing && share >= 0.25f ? atlas_risk : atlas_type, cx, cr, yy, hovering, mx, my);
		}
		yy += 12.f;
	};
	key(in_parts, false, l.bought, localized(state, "katechon_trade_suppliers", "Suppliers"), localized(state, "katechon_trade_of_demand", "of demand"));
	key(out_parts, true, l.sold, localized(state, "katechon_trade_main_buyers", "Buyers"), localized(state, "katechon_trade_of_output", "of output"));

	// what a stop would cost
	if(l.imported_quantity > 0.f && l.bought > 0.f && room_for(26.f + 44.f)) {
		yy = context_title(state, localized(state, "katechon_trade_if_imports_stop", "If imports stopped"), std::string(), cx, cr, yy);
		auto const gap = std::min(1.f, l.imported_quantity / l.bought);
		auto const shortage = l.dependence();
		auto const tone = shortage >= 0.25f ? shortage_tone(shortage) : atlas_type;
		auto const fw = atlas_figure(state, share_words(gap), cx, yy + 12.f, 24, tone, false);
		sans(state, localized(state, "katechon_trade_unmet", "of demand would go unmet at first"), cx + fw + 12.f, yy + 12.f, 14, atlas_type_soft);
		yy += 32.f;
		std::string then;
		ogl::color3f then_tone = atlas_type_soft;
		if(l.exported_quantity >= l.imported_quantity) {
			then = filled(localized(state, "katechon_trade_cover_by_exports", "Our exports, %1 units, are larger: kept at home they would close the gap."), units(l.exported_quantity));
			then_tone = atlas_gain;
		} else if(l.exported_quantity > 0.f) {
			then = filled(localized(state, "katechon_trade_partly_by_exports", "Kept at home, our exports would close %1 of it."), text::format_percentage(l.exported_quantity / l.imported_quantity, 0));
		} else {
			then = localized(state, "katechon_trade_nothing_to_divert", "We export none of it: there is nothing to keep at home instead.");
		}
		if(shortage >= 0.05f)
			then += " " + filled(localized(state, "katechon_trade_structural", "Our production meets only %1 of demand."), text::format_percentage(std::max(0.f, 1.f - shortage), 0));
		if(room_for(sans_paragraph(state, then, cx, cr, yy, 14, then_tone, 3, false) - yy))
			yy = sans_paragraph(state, then, cx, cr, yy, 14, then_tone, 3, true);
	} else if(l.exported_quantity > 0.f && l.sold > 0.f && room_for(26.f + 44.f)) {
		yy = context_title(state, localized(state, "katechon_trade_if_buyers_stop", "If buyers abroad stopped"), std::string(), cx, cr, yy);
		auto const fw = atlas_figure(state, share_words(std::min(1.f, l.exported_quantity / l.sold)), cx, yy + 12.f, 24, atlas_type, false);
		sans(state, localized(state, "katechon_trade_lose_market", "of our output would lose its market"), cx + fw + 12.f, yy + 12.f, 14, atlas_type_soft);
		yy += 32.f;
	}
	if(suppliers.empty() && buyers.empty() && room_for(30.f))
		sans(state, filled(localized(state, "katechon_trade_not_across", "Not traded across the border: %1"), period_words(state)), cx, yy + 14.f, 14, atlas_type_soft);
}

// A country: the goods each way as parts of our own figures, then what losing its trade would cost.
void draw_partner_context(sys::state& state, dcon::nation_id n, trade_partner_view const& p, float cx, float cr, float yy, float floor_y, bool hovering, float mx, float my) {
	auto const rel = relation_between(state, trade.nation, n);
	yy = context_head(state, cx, cr, yy, nation_name(state, n),
		sentence(localized(state, "katechon_trade_partner_sub", "%1 of our trade · relations %2"), text::format_percentage(p.share, 0), (rel > 0.f ? "+" : "") + std::to_string(int32_t(std::round(rel)))), n,
		dcon::commodity_id{}, hovering, mx, my);
	auto const room_for = [&](float h) { return yy + h <= floor_y; };

	auto const& links = size_t(n.index()) < links_by_nation.size() ? links_by_nation[size_t(n.index())] : std::vector<uint32_t>{};
	std::vector<trade_link const*> sold;
	std::vector<trade_link const*> bought;
	for(auto const i : links) {
		if(trade_links[i].out_value > 0.f)
			sold.push_back(&trade_links[i]);
		if(trade_links[i].in_value > 0.f)
			bought.push_back(&trade_links[i]);
	}
	std::stable_sort(sold.begin(), sold.end(), [](auto const* a, auto const* b) { return a->out_value > b->out_value; });
	std::stable_sort(bought.begin(), bought.end(), [](auto const* a, auto const* b) { return a->in_value > b->in_value; });
	constexpr float stop_h = 26.f + 3.f * 21.f + 12.f;
	// the room is shared between the two lists, the goods we buy first when they hold a dependency
	auto const rows_room = std::max(0, int32_t((floor_y - yy - stop_h - (sold.empty() ? 0.f : 38.f) - (bought.empty() ? 0.f : 38.f)) / 28.f));
	auto bought_rows = std::min<int32_t>(int32_t(bought.size()), std::max(rows_room / 2, rows_room - int32_t(sold.size())));
	auto sold_rows = std::min<int32_t>(int32_t(sold.size()), rows_room - bought_rows);
	bought_rows = std::min<int32_t>(int32_t(bought.size()), rows_room - sold_rows);
	if(!bought.empty() && bought_rows > 0) {
		yy = context_title(state, localized(state, "katechon_trade_they_sell", "We buy from them"), localized(state, "katechon_trade_of_our_demand", "of our demand for it"), cx, cr, yy);
		for(int32_t k = 0; k < bought_rows; ++k) {
			auto const& link = *bought[size_t(k)];
			auto const* line = line_for(link.good);
			auto const part = line && line->bought > 0.f ? std::min(1.f, link.in_quantity / line->bought) : -1.f;
			yy = key_row(state, good_value(link.good), nullptr, dcon::nation_id{}, link.good, goods_name(state, link.good), share_words(part), part >= 0.25f ? atlas_risk : atlas_type, cx, cr, yy, hovering, mx, my);
		}
		yy += 12.f;
	}
	if(!sold.empty() && sold_rows > 0) {
		yy = context_title(state, localized(state, "katechon_trade_they_buy", "We sell them"), localized(state, "katechon_trade_of_our_exports_of_it", "of our exports of it"), cx, cr, yy);
		for(int32_t k = 0; k < sold_rows; ++k) {
			auto const& link = *sold[size_t(k)];
			auto const* line = line_for(link.good);
			auto const part = line && line->exported > 0.f ? std::min(1.f, link.out_value / line->exported) : -1.f;
			yy = key_row(state, good_value(link.good), nullptr, dcon::nation_id{}, link.good, goods_name(state, link.good), share_words(part), atlas_type, cx, cr, yy, hovering, mx, my);
		}
		yy += 12.f;
	}
	if(sold.empty() && bought.empty()) {
		if(room_for(30.f))
			sans(state, filled(localized(state, "katechon_trade_quiet_border", "Nothing crossed the border: %1"), period_words(state)), cx, yy + 14.f, 14, atlas_type_soft);
		return;
	}

	// what a break would cost: the parts of our exports and imports, then who holds the lever
	if(!room_for(stop_h))
		return;
	yy = context_title(state, localized(state, "katechon_trade_if_trade_stops", "If trade with them stopped"), std::string(), cx, cr, yy);
	std::string words;
	if(trade.exported > 0.f && p.exported > 0.f)
		words += filled(localized(state, "katechon_trade_lose_exports", "%1 of our exports would lose their buyer."), text::format_percentage(p.exported / trade.exported, 0)) + " ";
	if(trade.imported > 0.f && p.imported > 0.f)
		words += filled(localized(state, "katechon_trade_replace_imports", "%1 of our imports would have to be found elsewhere."), text::format_percentage(p.imported / trade.imported, 0)) + " ";
	if(p.hard_to_replace && p.leans_on)
		words += filled(localized(state, "katechon_trade_hard_replace_good", "Hardly anyone else sells us %1."), goods_name(state, p.leans_on)) + " ";
	words += leverage_words(state, p.exported, p.imported);
	sans_paragraph(state, words, cx, cr, yy, 14, atlas_type_soft, int32_t(std::max(1.f, (floor_y - yy) / 20.f)), true);
}

// Nothing chosen: the trade position. How far exports pay for imports, as one figure; then the goods
// that lean on supply from abroad, each with the country it leans on, which the register does not say.
void draw_position_context(sys::state& state, float cx, float cr, float yy, float floor_y, bool hovering, float mx, float my) {
	auto const room_for = [&](float h) { return yy + h <= floor_y; };
	atlas_serif(state, localized(state, "katechon_trade_position", "Trade position"), cx, yy + 14.f, 24, atlas_type);
	yy += 44.f;
	auto const& v = trade;
	auto const cover = v.imported > 0.f ? v.exported / v.imported : -1.f;
	atlas_label(state, localized(state, "katechon_trade_cover_label", "Exports cover imports"), cx, yy + 8.f, atlas_type_soft);
	auto const fw = atlas_figure(state, cover < 0.f ? std::string("—") : (cover > 9.99f ? std::string(">999%") : text::format_percentage(cover, 0)), cx, yy + 40.f, 34, atlas_type, false);
	auto const balance = v.exported - v.imported;
	auto const words = balance >= 0.f ? localized(state, "katechon_trade_cover_surplus_short", "Exports pay for every import") : localized(state, "katechon_trade_cover_deficit_short", "Imports run past exports");
	sans(state, sans_fit(state, words, 15, cr - cx - fw - 16.f), cx + fw + 16.f, yy + 38.f, 15, atlas_type_soft);
	yy += 76.f;

	// the goods that lean on imports: a part of demand from abroad, and from whom most of it
	struct lean {
		economy::trade_picture::commodity_line const* line;
		trade_link const* source;
	};
	std::vector<lean> leans;
	for(auto const& l : v.commodities) {
		if(!(l.bought >= 0.01f) || !(l.import_share() >= 0.05f))
			continue;
		trade_link const* source = nullptr;
		if(size_t(l.commodity.index()) < links_by_good.size()) {
			for(auto const i : links_by_good[size_t(l.commodity.index())]) {
				if(!source || trade_links[i].in_quantity > source->in_quantity)
					source = &trade_links[i];
			}
		}
		leans.push_back(lean{ &l, source && source->in_quantity > 0.f ? source : nullptr });
	}
	std::stable_sort(leans.begin(), leans.end(), [](lean const& a, lean const& b) { return a.line->import_share() > b.line->import_share(); });
	yy = context_title(state, localized(state, "katechon_trade_leans", "Where we lean on others"), localized(state, "katechon_trade_leans_aside", "of demand from abroad"), cx, cr, yy);
	if(leans.empty()) {
		sans(state, localized(state, "katechon_trade_none_leans", "Our own producers supply every good we use"), cx, yy + 12.f, 14, atlas_type_soft);
		return;
	}
	for(size_t k = 0; k < leans.size() && k < 6 && room_for(46.f); ++k) {
		auto const& l = *leans[k].line;
		hit_target t{ cx - 8.f, yy, cr - cx + 16.f, 46.f, action::trade_findings, nullptr, good_value(l.commodity) };
		auto const hovered = hovering && over(t, mx, my);
		if(hovered)
			fill(state, t.x, t.y, t.w, t.h, atlas_hover, 1.f);
		good_icon(state, l.commodity, cx + 8.f, yy + 15.f, 16.f, atlas_type_soft);
		auto const share = l.import_share();
		auto const shortage = l.dependence();
		auto const sw = atlas_figure(state, share_words(share), cr - 18.f, yy + 15.f, 15, shortage >= 0.25f ? shortage_tone(shortage) : atlas_type);
		sans(state, sans_fit(state, goods_name(state, l.commodity), 15, cr - 18.f - sw - 14.f - cx - 25.f), cx + 25.f, yy + 15.f, 15, hovered ? atlas_accent_lit : atlas_type);
		if(auto const* src = leans[k].source) {
			auto const of = std::min(1.f, src->in_quantity / l.bought);
			sans(state, sans_fit(state, sentence(localized(state, "katechon_trade_lean_source", "mostly from %1, %2 of demand"), nation_name(state, src->nation), share_words(of)), 13, cr - cx - 43.f),
				cx + 25.f, yy + 34.f, 13, atlas_type_faint);
		}
		draw_icon(state, icon::chevron_right, cr - 3.f, yy + 15.f, 8.f, hovered ? atlas_accent_lit : atlas_type_faint);
		targets.push_back(t);
		yy += 46.f;
	}
}

// ---- the trends ----

// The picture the trends are drawn from: by the day until a year's has three months in it.
economy::trade_picture::picture const& course_picture() const {
	return trade_span > 30 && !trade_days.exported_series.empty() ? trade_days : trade;
}
// The buckets that are drawn: from the start of the game, and by the day without today, which is not over.
std::pair<size_t, size_t> course_range() const {
	auto const& v = course_picture();
	auto const first = std::min(v.first_recorded_bucket, v.exported_series.size());
	auto const end = v.span_days <= 30 && v.exported_series.size() > first ? v.exported_series.size() - 1 : v.exported_series.size();
	return { first, std::max(first, end) };
}
// The flows of what is forward, a bucket at a time: the whole trade, one partner, or one good.
float course_out(economy::trade_picture::picture const& v, size_t i) const {
	if(auto const n = focus_partner(); n && v.nations_count > uint32_t(n.index()))
		return v.exported_by_nation_series[i * v.nations_count + size_t(n.index())];
	if(auto const g = focus_good(); g && v.goods_count > uint32_t(g.index()))
		return v.exported_by_good_series[i * v.goods_count + size_t(g.index())];
	return v.exported_series[i];
}
float course_in(economy::trade_picture::picture const& v, size_t i) const {
	if(auto const n = focus_partner(); n && v.nations_count > uint32_t(n.index()))
		return v.imported_by_nation_series[i * v.nations_count + size_t(n.index())];
	if(auto const g = focus_good(); g && v.goods_count > uint32_t(g.index()))
		return v.imported_by_good_series[i * v.goods_count + size_t(g.index())];
	return v.imported_series[i];
}
// A rounded figure for the top of a scale, close above the data.
static float nice_ceiling(float v) {
	if(!(v > 0.f))
		return 1.f;
	auto const magnitude = std::pow(10.f, std::floor(std::log10(v)));
	for(auto const step : { 1.f, 1.5f, 2.f, 2.5f, 3.f, 4.f, 5.f, 6.f, 8.f, 10.f }) {
		if(step * magnitude >= v)
			return step * magnitude;
	}
	return 10.f * magnitude;
}
std::vector<economy::trade_picture::commodity_line const*> traded_goods() const {
	std::vector<economy::trade_picture::commodity_line const*> rows;
	for(auto const& line : trade.commodities) {
		if(line.exported > 0.f || line.imported > 0.f)
			rows.push_back(&line);
	}
	return rows;
}

// The trends of the span: the two flows (the week's average once there is a week), the days that stood
// out and what made them, and how the goods shifted between the first half of the span and the second.
void draw_trends(sys::state& state, float x, float y, float right, float bottom, bool hovering, float mx, float my) {
	auto const& v = course_picture();
	auto const [first, end] = course_range();
	auto const n = end - first;
	if(n < 3) {
		atlas_panel(state, x, y, right - x, bottom - y);
		sans(state, sentence(localized(state, "katechon_trade_chart_wait", "The trends appear once three %1 are recorded, %2 so far."), v.span_days <= 30 ? localized(state, "katechon_trade_day", "days") : localized(state, "katechon_trade_month", "months"),
			std::to_string(n)), x + 20.f, (y + bottom) * 0.5f, 16, atlas_type_soft);
		return;
	}
	auto const smooth = v.span_days <= 30 && n >= 7;
	auto const average = [&](size_t i, bool outgoing) {
		auto sum = 0.f;
		auto count = 0;
		for(size_t k = i + 1; k-- > first && count < 7;) {
			sum += outgoing ? course_out(v, k) : course_in(v, k);
			++count;
		}
		return count > 0 ? sum / float(count) : 0.f;
	};
	auto const value = [&](size_t i, bool outgoing) { return smooth ? average(i, outgoing) : (outgoing ? course_out(v, i) : course_in(v, i)); };
	auto const date_of = [&](size_t i) {
		auto const back = int32_t(v.exported_series.size() - 1 - i);
		return v.span_days <= 30 ? v.as_of - back : v.as_of - back * 30;
	};
	auto const chart_h = std::min(260.f, (bottom - y) * 0.55f);
	atlas_panel(state, x, y, right - x, chart_h);
	auto const partner = focus_partner();
	auto const good = focus_good();
	auto const subject = partner ? nation_name(state, partner) : (good ? goods_name(state, good) : std::string());
	auto kx = x + 18.f + atlas_serif(state, localized(state, "katechon_trade_flows", "Trade flows") + (subject.empty() ? std::string() : " · " + subject), x + 18.f, y + 22.f, 20, atlas_type) + 24.f;
	for(auto const& [colour, label] : { std::pair{ atlas_export, localized(state, "katechon_trade_exports", "Exports") }, std::pair{ atlas_import, localized(state, "katechon_trade_imports", "Imports") } }) {
		fill(state, kx, y + 21.f, 18.f, 3.f, colour, 1.f);
		kx += 26.f + sans(state, label, kx + 26.f, y + 22.f, 14, atlas_type_soft) + 20.f;
	}
	sans(state, smooth ? localized(state, "katechon_trade_week_note", "7-day average") : (v.span_days <= 30 ? localized(state, "katechon_trade_day_note", "each day") : localized(state, "katechon_trade_month_note", "each month")), kx, y + 22.f, 14, atlas_type_faint);
	auto const plot_top = y + 52.f;
	auto const plot_bottom = y + chart_h - 34.f;
	constexpr float axis_w = 72.f;
	auto const cx = x + 18.f + axis_w;
	auto const cw = right - 24.f - cx;
	auto most = 0.f;
	for(size_t i = first; i < end; ++i)
		most = std::max({ most, value(i, true), value(i, false) });
	auto const top = 3.f * nice_ceiling(most * 1.04f / 3.f);
	for(int32_t k = 0; k <= 3; ++k) {
		auto const gy = std::round(plot_bottom - (plot_bottom - plot_top) * float(k) / 3.f);
		fill(state, cx, gy, cw, 1.f, k == 0 ? atlas_type_faint : atlas_track, 1.f);
		sans(state, k == 0 ? std::string("0") : text::format_money(top * float(k) / 3.f), cx - 12.f, gy, 13, atlas_type_soft, true);
	}
	auto const step = cw / float(n);
	auto const px = [&](size_t i) { return cx + (float(i - first) + 0.5f) * step; };
	auto const py = [&](float val) { return plot_bottom - (plot_bottom - plot_top) * std::clamp(val / top, 0.f, 1.f); };
	std::vector<std::pair<float, float>> out_line;
	std::vector<std::pair<float, float>> in_line;
	for(size_t i = first; i < end; ++i) {
		out_line.emplace_back(px(i), py(value(i, true)));
		in_line.emplace_back(px(i), py(value(i, false)));
	}
	atlas_polyline(state, in_line, atlas_import, 2.5f);
	atlas_polyline(state, out_line, atlas_export, 2.5f);
	sans(state, text::date_to_string(state, date_of(first)), cx, plot_bottom + 18.f, 13, atlas_type_soft);
	if(n >= 5)
		sans_centered(state, text::date_to_string(state, date_of(first + n / 2)), px(first + n / 2) - 80.f, 160.f, plot_bottom + 18.f, 13, atlas_type_soft);
	sans(state, text::date_to_string(state, date_of(end - 1)), cx + cw, plot_bottom + 18.f, 13, atlas_type_soft, true);
	std::vector<size_t> days;
	for(size_t i = first; i < end; ++i)
		days.push_back(i);
	std::stable_sort(days.begin(), days.end(), [&](size_t a, size_t b) { return course_out(v, a) + course_in(v, a) > course_out(v, b) + course_in(v, b); });
	if(days.size() > 5)
		days.resize(5);
	for(size_t k = 0; k < std::min<size_t>(3, days.size()); ++k)
		atlas_lozenge(state, px(days[k]), plot_top - 6.f, 4, atlas_accent);
	if(hovering && mx >= cx && mx < cx + cw && my >= plot_top - 8.f && my <= plot_bottom + 6.f) {
		auto const i = first + std::min(n - 1, size_t(std::max(0.f, (mx - cx) / step)));
		auto const gx = std::round(px(i));
		fill(state, gx, plot_top, 1.f, plot_bottom - plot_top, atlas_type_faint, 1.f);
		for(auto const& [val, colour] : { std::pair{ value(i, true), atlas_export }, std::pair{ value(i, false), atlas_import } }) {
			auto const at = std::round(py(val));
			fill(state, gx - 4.f, at - 4.f, 9.f, 9.f, colour, 1.f);
		}
		auto const label = text::date_to_string(state, date_of(i)) + "   " + localized(state, "katechon_trade_kpi_out", "Exports") + " " + text::format_money(course_out(v, i)) + "   " + localized(state, "katechon_trade_kpi_in", "Imports") + " " +
			text::format_money(course_in(v, i));
		auto const lw = sans_w(state, label, 14) + 24.f;
		auto const lx = std::clamp(gx - lw * 0.5f, cx, cx + cw - lw);
		fill(state, lx, plot_top - 2.f, lw, 26.f, atlas_chosen, 1.f);
		frame(state, lx, plot_top - 2.f, lw, 26.f, atlas_accent, 0.6f);
		sans(state, label, lx + 12.f, plot_top + 11.f, 14, atlas_type);
	}
	auto const by = y + chart_h + 12.f;
	auto const half = std::round((right - x - 12.f) * 0.5f);
	atlas_panel(state, x, by, half, bottom - by);
	atlas_panel(state, x + half + 12.f, by, right - x - half - 12.f, bottom - by);
	{
		auto const lx = x + 18.f;
		auto const lr = x + half - 18.f;
		atlas_serif(state, localized(state, "katechon_trade_largest_days", "Largest days"), lx, by + 20.f, 20, atlas_type);
		auto ry = by + 40.f;
		for(auto const i : days) {
			if(ry + 34.f > bottom)
				break;
			std::string makers;
			if(!good && !partner) {
				std::vector<std::pair<float, uint32_t>> ranked;
				for(uint32_t g = 0; g < v.goods_count; ++g) {
					auto const val = v.exported_by_good_series[i * v.goods_count + g] + v.imported_by_good_series[i * v.goods_count + g];
					if(val > 0.f)
						ranked.emplace_back(val, g);
				}
				std::sort(ranked.begin(), ranked.end(), [](auto const& a, auto const& b) { return a.first > b.first; });
				for(size_t k = 0; k < ranked.size() && k < 2; ++k)
					makers += (k ? ", " : "") + goods_name(state, dcon::commodity_id{ dcon::commodity_id::value_base_t(ranked[k].second) }) + " " + text::format_money(ranked[k].first);
			}
			sans(state, text::date_to_string(state, date_of(i)), lx, ry + 17.f, 15, atlas_type);
			sans(state, text::format_money(course_out(v, i) + course_in(v, i)), lx + 170.f, ry + 17.f, 15, atlas_type);
			sans(state, sans_fit(state, makers, 14, lr - lx - 270.f), lx + 270.f, ry + 17.f, 14, atlas_type_soft);
			ry += 34.f;
		}
	}
	{
		auto const lx = x + half + 30.f;
		auto const lr = right - 18.f;
		atlas_serif(state, localized(state, "katechon_trade_shifts", "Shifts in structure"), lx, by + 20.f, 20, atlas_type);
		auto const mid = first + n / 2;
		auto const imports_r = lr;
		auto const exports_r = lr - 200.f;
		sans(state, localized(state, "katechon_trade_col_exports", "Exports, £"), exports_r, by + 46.f, 14, atlas_type_soft, true);
		sans(state, localized(state, "katechon_trade_col_imports", "Imports, £"), imports_r, by + 46.f, 14, atlas_type_soft, true);
		auto ry = by + 62.f;
		std::vector<std::tuple<dcon::commodity_id, float, float, float, float>> shifts;
		for(auto const* line : traded_goods()) {
			auto const gi = size_t(line->commodity.index());
			if(gi >= v.goods_count)
				continue;
			float out_a = 0.f, out_b = 0.f, in_a = 0.f, in_b = 0.f;
			for(size_t i = first; i < end; ++i) {
				(i < mid ? out_a : out_b) += v.exported_by_good_series[i * v.goods_count + gi];
				(i < mid ? in_a : in_b) += v.imported_by_good_series[i * v.goods_count + gi];
			}
			shifts.emplace_back(line->commodity, out_a, out_b, in_a, in_b);
		}
		std::stable_sort(shifts.begin(), shifts.end(), [](auto const& a, auto const& b) {
			return std::abs(std::get<2>(a) - std::get<1>(a)) + std::abs(std::get<4>(a) - std::get<3>(a)) > std::abs(std::get<2>(b) - std::get<1>(b)) + std::abs(std::get<4>(b) - std::get<3>(b));
		});
		auto const change = [&](float before, float after) {
			return before <= 0.f && after <= 0.f ? std::string("—") : bare_money(before) + " → " + bare_money(after);
		};
		for(auto const& [g, out_a, out_b, in_a, in_b] : shifts) {
			if(ry + 30.f > bottom - 20.f)
				break;
			good_icon(state, g, lx + 9.f, ry + 15.f, 16.f, atlas_type_soft);
			sans(state, sans_fit(state, goods_name(state, g), 15, exports_r - 200.f - lx - 28.f), lx + 28.f, ry + 15.f, 15, atlas_type);
			sans(state, change(out_a, out_b), exports_r, ry + 15.f, 15, out_b > out_a * 1.1f ? atlas_gain : (out_b < out_a * 0.9f ? atlas_caution : atlas_type), true);
			sans(state, change(in_a, in_b), imports_r, ry + 15.f, 15, in_b > in_a * 1.1f ? atlas_caution : (in_b < in_a * 0.9f ? atlas_gain : atlas_type), true);
			ry += 30.f;
		}
		sans(state, localized(state, "katechon_trade_halves_note", "first half of the span → second half"), lx, bottom - 14.f, 13, atlas_type_faint);
	}
}
