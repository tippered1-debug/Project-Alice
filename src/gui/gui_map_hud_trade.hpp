// The trade mode of the KATECHON map HUD. This file is included inside the
// body of ui::map_hud_element (gui_map_hud.hpp) and uses its drawing helpers;
// it is not a header of its own.
//
// Two levels. On the map, a narrow inspector answers how much the country
// trades, with whom, what earns it money and what it cannot do without, and
// the map draws the flows: exports gold, imports blue, wider for more value.
// Choosing a partner or a good, on the sheet or on the map, puts it forward:
// the map then shows only its flows. The full analysis takes the screen for
// the economy itself: performance over time, the structure of the goods, the
// partners, and the dependencies that could hurt. Every figure comes from
// economy/trade_picture.hpp; the old trade window stays one press away.

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
inline static constexpr ogl::color3f trade_seller{ 0.86f, 0.62f, 0.34f };
inline static constexpr ogl::color3f trade_none{ 0.40f, 0.38f, 0.34f };
// Exports and imports in the colours of their lanes (trade_route_f.glsl).
inline static constexpr ogl::color3f out_tone{ 0.93f, 0.75f, 0.44f };
inline static constexpr ogl::color3f in_tone{ 0.52f, 0.74f, 0.90f };
inline static constexpr ogl::color3f high_tone{ 0.90f, 0.66f, 0.38f };

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
	float import_share = 0.f; // of all the country bought of it
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
		auto const share = line.import_share();
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

void gather_trade(sys::state& state) {
	trade = economy::trade_picture::take(state, state.local_player_nation, trade_span);
	analyse_trade(state);
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

// A country chosen on the map is put forward; one's own brings the whole trade back.
void follow_map_choice(sys::state& state) {
	auto const p = state.map_state.selected_province;
	if(p == trade_seen_province)
		return;
	trade_seen_province = p;
	if(!p)
		return;
	auto const owner = state.world.province_get_nation_from_province_ownership(p);
	if(!owner)
		return;
	trade_analysis = false;
	trade_focus = owner == state.local_player_nation ? 0 : -(int32_t(owner.index()) + 1);
}

// ---- the parts of the look ----

static std::string signed_money(float v) {
	return (v > 0.f ? "+" : (v < 0.f ? "-" : "")) + text::format_money(std::abs(v));
}

static int32_t partner_value(dcon::nation_id n) {
	return -(int32_t(n.index()) + 1);
}
static int32_t good_value(dcon::commodity_id c) {
	return int32_t(c.index()) + 1;
}

// The span the figures cover, in words: "the last 30 days", or the days since the start.
std::string period_words(sys::state& state) const {
	if(trade.recorded_days < trade.span_days)
		return filled(localized(state, "katechon_trade_period_since", "first %1 days of play"), std::to_string(std::max(1, trade.recorded_days)));
	return trade.span_days <= 30 ? localized(state, "katechon_trade_period_month", "the last 30 days") : localized(state, "katechon_trade_period_year", "the last 12 months");
}

ogl::color3f level_tone(int32_t level) const {
	return level >= 2 ? dip_no : (level == 1 ? high_tone : dip_muted);
}
std::string level_word(sys::state& state, int32_t level) {
	return level >= 2 ? localized(state, "katechon_trade_level_critical", "Critical") : (level == 1 ? localized(state, "katechon_trade_level_high", "High") : localized(state, "katechon_trade_level_watch", "Watch"));
}
std::string kind_word(sys::state& state, int32_t kind) {
	return kind >= 2 ? localized(state, "katechon_trade_kind_vital", "Vital") : (kind == 1 ? localized(state, "katechon_trade_kind_strategic", "Strategic") : std::string());
}
ogl::color3f cover_tone(float cover) const {
	return cover < 0.f ? dip_faint : (cover < 0.5f ? dip_no : (cover < 1.f ? high_tone : dip_yes));
}

// The tracked capital line a section opens with; its aside says what the figures are.
float trade_section(sys::state& state, std::string const& title, std::string const& aside, float left, float right, float y) {
	auto const tw = sans(state, upper(title), left, y + 8.f, 10, dip_bronze, false, 0.14f);
	if(!aside.empty())
		sans(state, sans_fit(state, aside, 11, right - left - tw - 16.f), right, y + 8.f, 11, dip_faint, true);
	return y + 24.f;
}

// A figure: what it is, the figure itself, and how it stands against the span before.
void trade_kpi(sys::state& state, std::string const& label, std::string const& value, ogl::color3f colour, float now, float before, bool rising_is_good, bool as_money, bool compare, float x, float y, float w, int32_t size) {
	sans(state, sans_fit(state, label, 11, w - 6.f), x, y + 8.f, 11, dip_muted);
	sans(state, sans_fit(state, value, size, w - 8.f), x, y + 20.f + float(size) * 0.5f, size, colour);
	if(!compare || !trade.has_previous || (!as_money && before == 0.f))
		return;
	auto const delta = now - before;
	auto const change = as_money ? delta : delta / std::abs(before);
	auto const up = change >= 0.f;
	auto const flat = as_money ? std::abs(delta) < 0.005f * std::max(1.f, std::abs(before)) : std::abs(change) < 0.005f;
	auto const tone = flat ? dip_muted : (up == rising_is_good ? dip_yes : dip_no);
	auto const cy = y + 30.f + float(size);
	arrow(state, x + 1.f, cy, up, tone);
	auto const figure = as_money ? signed_money(delta) : text::format_percentage(std::abs(change), 0);
	sans(state, sans_fit(state, figure + " " + localized(state, "katechon_trade_vs_before", "vs previous"), 11, w - 18.f), x + 10.f, cy, 11, tone);
}
static constexpr float trade_kpi_h = 74.f;

// The span: a month a day at a time, or a year a month at a time.
float span_switch(sys::state& state, float x, float y, bool hovering, float mx, float my) {
	auto at = x;
	for(auto const span : { 30, 365 }) {
		auto const label = span == 30 ? localized(state, "katechon_trade_span_month", "30 days") : localized(state, "katechon_trade_span_year", "12 months");
		auto const lw = std::round(sans_w(state, label, 12) + 24.f);
		hit_target t{ at, y, lw, 24.f, action::trade_span, nullptr, span };
		auto const on = trade_span == span;
		auto const hovered = hovering && over(t, mx, my);
		fill(state, t.x, t.y, t.w, t.h, on ? dip_bronze : ink, on ? 0.92f : (hovered ? 0.50f : 0.32f));
		sans_centered(state, label, t.x, t.w, t.y + 12.f, 12, on ? dip_ink : (hovered ? dip_text : dip_muted));
		targets.push_back(t);
		at += lw;
	}
	return at - x;
}

void share_bar(sys::state& state, float x, float cy, float w, float part, ogl::color3f c) {
	fill(state, x, cy - 2.f, w, 4.f, ink, 0.45f);
	auto const lit = std::round(w * std::clamp(part, 0.f, 1.f));
	if(lit > 0.f)
		fill(state, x, cy - 2.f, std::max(2.f, lit), 4.f, c, 1.f);
}

// What the lines on the map mean, for what is forward.
float lane_key(sys::state& state, float left, float right, float y) {
	auto x = left;
	for(auto const& [colour, label] : { std::pair{ out_tone, localized(state, "katechon_trade_exports", "Exports") }, std::pair{ in_tone, localized(state, "katechon_trade_imports", "Imports") } }) {
		fill(state, x, y + 7.f, 16.f, 3.f, colour, 1.f);
		x += 22.f + sans(state, label, x + 22.f, y + 8.f, 12, dip_muted) + 16.f;
	}
	sans(state, localized(state, "katechon_trade_key_width", "wider: more value"), right, y + 8.f, 12, dip_faint, true);
	y += 20.f;
	if(trade_focus > 0) {
		x = left;
		for(auto const& [colour, label] : { std::pair{ trade_seller, localized(state, "katechon_trade_key_sellers", "sell it abroad") }, std::pair{ trade_partner, localized(state, "katechon_trade_key_buyers", "buy it abroad") } }) {
			fill(state, x, y + 3.f, 10.f, 10.f, colour, 0.85f);
			x += 16.f + sans(state, label, x + 16.f, y + 8.f, 12, dip_muted) + 16.f;
		}
		sans(state, localized(state, "katechon_trade_key_world", "the whole world"), right, y + 8.f, 12, dip_faint, true);
		y += 20.f;
	}
	return y;
}

// ---- the inspector ----

static constexpr float trade_row_h = 26.f;

float partner_row(sys::state& state, trade_partner_view const& p, float top_total, float left, float right, float y, bool hovering, float mx, float my) {
	hit_target t{ left - 6.f, y, right - left + 12.f, trade_row_h, action::trade_focus, nullptr, partner_value(p.nation) };
	auto const hovered = hovering && over(t, mx, my);
	if(hovered)
		fill(state, t.x, t.y, t.w, t.h, dip_text, 0.05f);
	dip_flag(state, p.nation, left, y + 7.f, 18.f, 12.f);
	sans(state, text::format_percentage(p.share, 0), right, y + 13.f, 13, dip_text, true);
	constexpr float bar_w = 64.f;
	auto const bx = right - 44.f - bar_w;
	auto const total = p.exported + p.imported;
	auto const length = std::max(3.f, std::round(bar_w * (top_total > 0.f ? total / top_total : 0.f)));
	auto const out_w = std::round(length * (total > 0.f ? p.exported / total : 0.f));
	fill(state, bx, y + 11.f, bar_w, 4.f, ink, 0.45f);
	fill(state, bx, y + 11.f, out_w, 4.f, out_tone, 1.f);
	fill(state, bx + out_w, y + 11.f, length - out_w, 4.f, in_tone, 1.f);
	sans(state, sans_fit(state, nation_name(state, p.nation), 13, bx - left - 38.f), left + 28.f, y + 13.f, 13, hovered ? dip_name : dip_text);
	if(hovered)
		hint(sentence(localized(state, "katechon_trade_partner_tip", "We sell them %1, we buy %2"), text::format_money(p.exported), text::format_money(p.imported)), t);
	targets.push_back(t);
	return y + trade_row_h;
}

float dependency_row(sys::state& state, trade_dependency const& d, float left, float right, float y, bool hovering, float mx, float my) {
	hit_target t{ left - 6.f, y, right - left + 12.f, trade_row_h, action::trade_focus, nullptr, good_value(d.good_id) };
	auto const hovered = hovering && over(t, mx, my);
	if(hovered)
		fill(state, t.x, t.y, t.w, t.h, dip_text, 0.05f);
	auto const tone = level_tone(d.level);
	good_icon(state, d.good_id, left + 8.f, y + 13.f, 16.f, tone);
	sans(state, text::format_percentage(d.import_share, 0), right, y + 13.f, 13, tone, true);
	constexpr float bar_w = 64.f;
	auto const bx = right - 44.f - bar_w;
	share_bar(state, bx, y + 13.f, bar_w, d.import_share, tone);
	if(d.source)
		dip_flag(state, d.source, bx - 26.f, y + 8.f, 15.f, 10.f);
	sans(state, sans_fit(state, goods_name(state, d.good_id), 13, bx - left - 60.f), left + 24.f, y + 13.f, 13, hovered ? dip_name : dip_text);
	if(hovered) {
		auto note = sentence(localized(state, "katechon_trade_dep_tip", "%1 of what we use comes from abroad, %2"), text::format_percentage(d.import_share, 0),
			d.source ? sentence(localized(state, "katechon_trade_dep_tip_source", "%1 of that from %2"), text::format_percentage(d.source_share, 0), nation_name(state, d.source)) : std::string());
		hint(note, t);
	}
	targets.push_back(t);
	return y + trade_row_h;
}

float export_row(sys::state& state, economy::trade_picture::commodity_line const& line, float left, float right, float y, bool hovering, float mx, float my) {
	hit_target t{ left - 6.f, y, right - left + 12.f, trade_row_h, action::trade_focus, nullptr, good_value(line.commodity) };
	auto const hovered = hovering && over(t, mx, my);
	if(hovered)
		fill(state, t.x, t.y, t.w, t.h, dip_text, 0.05f);
	good_icon(state, line.commodity, left + 8.f, y + 13.f, 16.f, out_tone);
	auto const vw = sans(state, text::format_money(line.exported), right, y + 13.f, 13, dip_text, true);
	constexpr float bar_w = 64.f;
	auto const bx = right - std::max(vw, 44.f) - 12.f - bar_w;
	share_bar(state, bx, y + 13.f, bar_w, trade.exported > 0.f ? line.exported / trade.exported : 0.f, out_tone);
	sans(state, sans_fit(state, goods_name(state, line.commodity), 13, bx - left - 34.f), left + 24.f, y + 13.f, 13, hovered ? dip_name : dip_text);
	if(hovered)
		hint(filled(localized(state, "katechon_trade_export_tip", "%1 of our exports"), text::format_percentage(trade.exported > 0.f ? line.exported / trade.exported : 0.f, 0)), t);
	targets.push_back(t);
	return y + trade_row_h;
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

void draw_trade(sys::state& state, float w, float h, bool hovering, float mx, float my) {
	if(!dip_art_read)
		load_dip_art(state);
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

	auto const& v = trade;
	constexpr float sheet_w = 372.f;
	auto const x0 = ribbon_w + 8.f;
	auto const y0 = top_h + 12.f;
	auto const left = x0 + 22.f;
	auto const right = x0 + sheet_w - 22.f;
	auto const room = h - y0 - 16.f;
	constexpr float head_h = 146.f;
	constexpr float kpi_block = trade_kpi_h + 14.f;
	constexpr float key_h = 20.f + 20.f;
	constexpr float foot_h = 14.f + 36.f + 18.f;
	auto const partner = trade_focus < 0 ? dcon::nation_id{ dcon::nation_id::value_base_t(-trade_focus - 1) } : dcon::nation_id{};
	auto const focus_good = trade_focus > 0 ? dcon::commodity_id{ dcon::commodity_id::value_base_t(trade_focus - 1) } : dcon::commodity_id{};

	// What the body holds, and as many rows of it as there is room for.
	size_t partner_rows = std::min<size_t>(5, trade_partners.size());
	size_t dep_rows = std::min<size_t>(3, trade_deps.size());
	auto exports = top_exports(3);
	auto const world_body = [&]() {
		return 24.f + float(std::max<size_t>(1, partner_rows)) * trade_row_h + 10.f + 24.f + float(std::max<size_t>(1, dep_rows)) * trade_row_h + 10.f
			+ (exports.empty() ? 0.f : 24.f + float(exports.size()) * trade_row_h + 10.f);
	};
	float body = 0.f;
	if(trade_focus == 0) {
		while(head_h + kpi_block + world_body() + key_h + foot_h > room) {
			if(!exports.empty())
				exports.pop_back();
			else if(partner_rows > 3)
				--partner_rows;
			else if(dep_rows > 1)
				--dep_rows;
			else if(partner_rows > 1)
				--partner_rows;
			else
				break;
		}
		body = world_body();
	} else {
		body = trade_focus > 0 ? focus_good_height(state, focus_good, left, right) : focus_partner_height(state, partner, left, right);
	}
	auto const sheet_h = std::min(room, head_h + kpi_block + body + key_h + foot_h);
	dip_frame(state, x0, y0, sheet_w, sheet_h);

	// The head: where we are, whose, over what span.
	sans(state, upper(localized(state, "katechon_trade_scope", "Economy · Trade")), left, y0 + 22.f, 10, dip_bronze, false, 0.14f);
	{
		hit_target close{ right - 12.f, y0 + 9.f, 26.f, 26.f, action::toggle_trade };
		draw_icon(state, icon::close, close.x + 13.f, close.y + 13.f, 11.f, hovering && over(close, mx, my) ? dip_text : dip_bronze);
		targets.push_back(close);
		if(trade_focus != 0)
			dip_link(state, "‹ " + localized(state, "katechon_trade_all", "All trade"), right - 26.f, y0 + 22.f, action::trade_focus, hovering, mx, my, true);
	}
	std::string subtitle;
	if(trade_focus == 0) {
		if(v.nation)
			dip_flag(state, v.nation, left, y0 + 42.f, 30.f, 20.f);
		draw_text(state, fit(state, nation_name(state, v.nation), 24, 0.01f, right - left - 44.f), left + 42.f, y0 + 52.f, 24, 0.01f, dip_name);
		subtitle = sentence(localized(state, "katechon_trade_world_sub", "%1 trading partners · %2 deals across the border"), std::to_string(v.partners.size()), std::to_string(v.deals));
	} else if(partner) {
		dip_flag(state, partner, left, y0 + 42.f, 30.f, 20.f);
		draw_text(state, fit(state, nation_name(state, partner), 24, 0.01f, right - left - 44.f), left + 42.f, y0 + 52.f, 24, 0.01f, dip_name);
		auto const* p = partner_view(partner);
		auto const rel = relation_between(state, v.nation, partner);
		subtitle = sentence(localized(state, "katechon_trade_partner_sub", "%1 of our trade · relations %2"), text::format_percentage(p ? p->share : 0.f, 0),
			(rel > 0.f ? "+" : "") + std::to_string(int32_t(std::round(rel))) + " " + temper_word(state, temper_of(rel)));
	} else {
		auto const* line = line_for(focus_good);
		auto const cover = line ? line->self_sufficiency() : -1.f;
		fill(state, left, y0 + 37.f, 30.f, 30.f, ink, 0.40f);
		good_icon(state, focus_good, left + 15.f, y0 + 52.f, 22.f, cover_tone(cover));
		draw_text(state, fit(state, goods_name(state, focus_good), 24, 0.01f, right - left - 44.f), left + 42.f, y0 + 52.f, 24, 0.01f, dip_name);
		auto const out = line ? line->exported : 0.f;
		auto const in = line ? line->imported : 0.f;
		subtitle = out > in ? localized(state, "katechon_trade_role_out", "Net export") : (in > 0.f ? localized(state, "katechon_trade_role_in", "Net import") : localized(state, "katechon_trade_role_home", "Traded at home only"));
		if(auto const k = kind_word(state, weight_of_good(state, focus_good)); !k.empty())
			subtitle += " · " + k;
	}
	sans(state, sans_fit(state, subtitle, 13, right - left), left, y0 + 78.f, 13, dip_muted);
	span_switch(state, left, y0 + 94.f, hovering, mx, my);
	sans(state, filled(localized(state, "katechon_trade_asof", "as of %1"), text::date_to_string(state, state.current_date)), right, y0 + 106.f, 12, dip_faint, true);
	sans(state, sans_fit(state, filled(localized(state, "katechon_trade_period_line", "Value across the border, %1"), period_words(state)), 12, right - left), left, y0 + 134.f, 12, dip_faint);
	auto y = y0 + head_h;

	// Three figures.
	{
		auto const col = std::floor((right - left) / 3.f);
		float out = v.exported, in = v.imported, out_before = v.previous_exported, in_before = v.previous_imported;
		bool comparable = true;
		if(partner) {
			auto const* p = partner_view(partner);
			out = p ? p->exported : 0.f;
			in = p ? p->imported : 0.f;
			comparable = false;
		} else if(focus_good) {
			auto const* line = line_for(focus_good);
			out = line ? line->exported : 0.f;
			in = line ? line->imported : 0.f;
			comparable = false;
		}
		// the span before is summed for the whole trade only
		trade_kpi(state, localized(state, "katechon_trade_kpi_out", "Exports"), text::format_money(out), out_tone, out, out_before, true, false, comparable, left, y, col - 8.f, 24);
		trade_kpi(state, localized(state, "katechon_trade_kpi_in", "Imports"), text::format_money(in), in_tone, in, in_before, false, false, comparable, left + col, y, col - 8.f, 24);
		if(focus_good) {
			auto const* line = line_for(focus_good);
			auto const cover = line ? line->self_sufficiency() : -1.f;
			trade_kpi(state, localized(state, "katechon_trade_kpi_cover", "Home cover"), cover < 0.f ? std::string("—") : text::format_percentage(cover, 0), cover_tone(cover), 0.f, 0.f, true, false, false, left + 2.f * col, y, col, 24);
		} else {
			trade_kpi(state, localized(state, "katechon_trade_kpi_net", "Balance"), signed_money(out - in), out >= in ? dip_yes : dip_no, out - in, out_before - in_before, true, true, comparable, left + 2.f * col, y, col, 24);
		}
		y += trade_kpi_h;
		dip_rule_line(state, left, right, y + 4.f);
		y += 14.f;
	}

	auto const floor_y = y0 + sheet_h - foot_h - key_h;
	if(trade_focus == 0) {
		// With whom.
		y = trade_section(state, localized(state, "katechon_trade_partners", "Trading partners"), localized(state, "katechon_trade_partners_aside", "% of exports + imports"), left, right, y);
		if(trade_partners.empty()) {
			sans(state, filled(localized(state, "katechon_trade_quiet_border", "Nothing crossed the border over %1"), period_words(state)), left, y + 13.f, 13, dip_faint);
			y += trade_row_h;
		} else {
			auto const top_total = trade_partners.front().exported + trade_partners.front().imported;
			for(size_t i = 0; i < partner_rows && y + trade_row_h <= floor_y; ++i)
				y = partner_row(state, trade_partners[i], top_total, left, right, y, hovering, mx, my);
		}
		y += 10.f;
		// What we could not do without.
		y = trade_section(state, localized(state, "katechon_trade_deps", "Commodity dependencies"), localized(state, "katechon_trade_deps_aside", "imported share of use"), left, right, y);
		if(trade_deps.empty()) {
			sans(state, sans_fit(state, localized(state, "katechon_trade_no_deps", "No good relies on imports for a quarter of its use"), 13, right - left), left, y + 13.f, 13, dip_faint);
			y += trade_row_h;
		} else {
			for(size_t i = 0; i < dep_rows && y + trade_row_h <= floor_y; ++i)
				y = dependency_row(state, trade_deps[i], left, right, y, hovering, mx, my);
		}
		y += 10.f;
		// What earns the money.
		if(!exports.empty() && y + 24.f + trade_row_h <= floor_y) {
			y = trade_section(state, localized(state, "katechon_trade_earners", "Main exports"), localized(state, "katechon_trade_earners_aside", "bar: share of exports"), left, right, y);
			for(auto const* line : exports) {
				if(y + trade_row_h > floor_y)
					break;
				y = export_row(state, *line, left, right, y, hovering, mx, my);
			}
		}
	} else if(trade_focus > 0) {
		draw_focus_good(state, focus_good, left, right, y, floor_y, hovering, mx, my);
	} else {
		draw_focus_partner(state, partner, left, right, y, floor_y, hovering, mx, my);
	}

	// The key to the map, and the way to the whole analysis.
	{
		auto const fy = y0 + sheet_h - foot_h - key_h;
		dip_rule_line(state, left, right, fy);
		lane_key(state, left, right, fy + 4.f);
		dip_gold_button(state, left, y0 + sheet_h - 18.f - 36.f, right - left, 36.f, localized(state, "katechon_trade_open_analysis", "Full trade analysis"), true,
			hit_target{ 0.f, 0.f, 0.f, 0.f, action::trade_analysis }, hovering, mx, my);
	}
}

// ---- a partner forward ----

std::vector<economy::trade_picture::flow_part const*> parts_with(dcon::nation_id n, bool outgoing, size_t most) const {
	std::vector<economy::trade_picture::flow_part const*> out;
	for(auto const& part : trade.parts) {
		if(part.nation == n && part.outgoing == outgoing && out.size() < most)
			out.push_back(&part);
	}
	return out;
}

float focus_partner_height(sys::state& state, dcon::nation_id partner, float left, float right) {
	auto const sells = parts_with(partner, true, 4).size();
	auto const buys = parts_with(partner, false, 4).size();
	return 2.f * (24.f + 10.f) + float(std::max<size_t>(1, sells) + std::max<size_t>(1, buys)) * trade_row_h + 2.f * 19.f * 2.f + 16.f + 46.f;
}

float goods_line(sys::state& state, economy::trade_picture::flow_part const& part, float whole, ogl::color3f tone, float left, float right, float y, bool hovering, float mx, float my) {
	hit_target t{ left - 6.f, y, right - left + 12.f, trade_row_h, action::trade_focus, nullptr, good_value(part.good) };
	auto const hovered = hovering && over(t, mx, my);
	if(hovered)
		fill(state, t.x, t.y, t.w, t.h, dip_text, 0.05f);
	good_icon(state, part.good, left + 8.f, y + 13.f, 16.f, tone);
	auto const pw = sans(state, text::format_percentage(whole > 0.f ? part.value / whole : 0.f, 0), right, y + 13.f, 12, dip_faint, true);
	auto const vw = sans(state, text::format_money(part.value), right - std::max(pw, 34.f) - 12.f, y + 13.f, 13, dip_text, true);
	sans(state, sans_fit(state, goods_name(state, part.good), 13, right - left - 24.f - std::max(pw, 34.f) - vw - 30.f), left + 24.f, y + 13.f, 13, hovered ? dip_name : dip_text);
	targets.push_back(t);
	return y + trade_row_h;
}

void draw_focus_partner(sys::state& state, dcon::nation_id partner, float left, float right, float y, float floor_y, bool hovering, float mx, float my) {
	auto const* p = partner_view(partner);
	auto const out = p ? p->exported : 0.f;
	auto const in = p ? p->imported : 0.f;
	for(auto const outgoing : { true, false }) {
		y = trade_section(state, outgoing ? localized(state, "katechon_trade_they_buy", "We sell them") : localized(state, "katechon_trade_they_sell", "We buy from them"),
			localized(state, "katechon_trade_of_flow", "% of it"), left, right, y);
		auto const list = parts_with(partner, outgoing, 4);
		if(list.empty()) {
			sans(state, localized(state, "katechon_trade_nothing_goods", "Nothing"), left, y + 13.f, 13, dip_faint);
			y += trade_row_h;
		}
		for(auto const* part : list) {
			if(y + trade_row_h > floor_y)
				break;
			y = goods_line(state, *part, outgoing ? out : in, outgoing ? out_tone : in_tone, left, right, y, hovering, mx, my);
		}
		y += 10.f;
	}
	// How much we lean on them, and who holds the leverage.
	std::string reliance;
	if(p && p->leans_on && p->leans_share >= 0.1f) {
		reliance = sentence(localized(state, "katechon_trade_reliance", "They supply %1 of all the %2 we use."), text::format_percentage(p->leans_share, 0), goods_name(state, p->leans_on));
		if(p->hard_to_replace)
			reliance += " " + localized(state, "katechon_trade_hard_replace", "Hardly anyone else sells it to us.");
	} else {
		reliance = localized(state, "katechon_trade_no_reliance", "Nothing we use depends on them for a tenth of its supply.");
	}
	auto const lever = out <= 0.f && in <= 0.f ? filled(localized(state, "katechon_trade_no_partner_trade", "No trade with them over %1."), period_words(state))
		: (in > 0.f && out <= 0.f ? localized(state, "katechon_trade_lever_theirs_only", "We need what they sell and they need nothing of ours: the leverage is theirs.")
		: (out > in * 1.5f ? localized(state, "katechon_trade_lever_ours", "They take far more from us than they sell: the leverage is ours.")
		: (in > out * 1.5f ? localized(state, "katechon_trade_lever_theirs", "We buy far more from them than we sell: the leverage is theirs.")
		: localized(state, "katechon_trade_lever_even", "The trade runs about even both ways."))));
	y = sans_paragraph(state, reliance, left, right, y, 13, p && p->hard_to_replace ? dip_no : dip_text, 2, true) + 4.f;
	y = sans_paragraph(state, lever, left, right, y, 13, dip_muted, 2, true) + 12.f;
	dip_ghost_button(state, left, std::min(y, floor_y - 40.f), right - left, 32.f, localized(state, "katechon_trade_talks", "Open talks"),
		hit_target{ 0.f, 0.f, 0.f, 0.f, action::diplomacy_target, nullptr, nation_value(partner) }, hovering, mx, my);
}

// ---- a good forward ----

std::vector<economy::trade_picture::flow_part const*> parts_of(dcon::commodity_id c, bool outgoing, size_t most) const {
	std::vector<economy::trade_picture::flow_part const*> out;
	for(auto const& part : trade.parts) {
		if(part.good == c && part.outgoing == outgoing && out.size() < most)
			out.push_back(&part);
	}
	return out;
}

float focus_good_height(sys::state& state, dcon::commodity_id c, float left, float right) {
	auto const* line = line_for(c);
	auto const outgoing = line && line->exported >= line->imported && line->exported > 0.f;
	auto const main_rows = std::max<size_t>(1, parts_of(c, outgoing, 5).size());
	auto const other_rows = parts_of(c, !outgoing, 3).size();
	return 24.f + float(main_rows) * trade_row_h + 10.f + (other_rows ? 24.f + float(other_rows) * trade_row_h + 10.f : 0.f) + 24.f + 3.f * 19.f * 2.f;
}

float nation_line(sys::state& state, dcon::nation_id n, float value, float whole, bool outgoing, float left, float right, float y, bool hovering, float mx, float my) {
	hit_target t{ left - 6.f, y, right - left + 12.f, trade_row_h, action::trade_focus, nullptr, partner_value(n) };
	auto const hovered = hovering && over(t, mx, my);
	if(hovered)
		fill(state, t.x, t.y, t.w, t.h, dip_text, 0.05f);
	dip_flag(state, n, left, y + 7.f, 18.f, 12.f);
	auto const pw = sans(state, text::format_percentage(whole > 0.f ? value / whole : 0.f, 0), right, y + 13.f, 12, dip_faint, true);
	auto const vw = sans(state, text::format_money(value), right - std::max(pw, 34.f) - 12.f, y + 13.f, 13, outgoing ? out_tone : in_tone, true);
	sans(state, sans_fit(state, nation_name(state, n), 13, right - left - 28.f - std::max(pw, 34.f) - vw - 30.f), left + 28.f, y + 13.f, 13, hovered ? dip_name : dip_text);
	targets.push_back(t);
	return y + trade_row_h;
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
		y = trade_section(state, direction ? localized(state, "katechon_trade_buyers", "Who buys it from us") : localized(state, "katechon_trade_sellers", "Who sells it to us"),
			localized(state, "katechon_trade_of_flow", "% of it"), left, right, y);
		if(list.empty()) {
			sans(state, filled(localized(state, "katechon_trade_not_across", "Not traded across the border over %1"), period_words(state)), left, y + 13.f, 13, dip_faint);
			y += trade_row_h;
		}
		for(auto const* part : list) {
			if(y + trade_row_h > floor_y)
				break;
			y = nation_line(state, part->nation, part->value, direction ? out : in, direction, left, right, y, hovering, mx, my);
		}
		y += 10.f;
	}
	// At home: how much of our use is covered, from where the rest comes, what it costs.
	y = trade_section(state, localized(state, "katechon_trade_home", "At home"), std::string(), left, right, y);
	std::vector<std::pair<std::string, ogl::color3f>> lines;
	if(line && line->bought > 0.f) {
		auto const share = line->import_share();
		if(share > 0.f) {
			auto words = filled(localized(state, "katechon_trade_imported_use", "%1 of what we use comes from abroad."), text::format_percentage(share, 0));
			if(line->main_source)
				words += " " + sentence(localized(state, "katechon_trade_main_source", "%1 of the imports from %2."), text::format_percentage(line->main_source_share, 0), nation_name(state, line->main_source));
			lines.emplace_back(words, share >= 0.5f ? dip_no : (share >= 0.25f ? high_tone : dip_text));
		} else {
			lines.emplace_back(localized(state, "katechon_trade_all_home", "All we use of it is made at home."), dip_text);
		}
	} else if(out > 0.f) {
		lines.emplace_back(localized(state, "katechon_trade_only_abroad", "We do not buy it at home; it is made for sale abroad."), dip_text);
	}
	{
		auto const capital = state.world.nation_get_capital(trade.nation);
		auto const home_market = capital ? state.world.state_instance_get_market_from_local_market(state.world.province_get_state_membership(capital)) : dcon::market_id{};
		auto const ours = economy::trade_picture::unit_price(state, home_market, c);
		auto const world = state.world.commodity_get_median_price(c);
		if(ours > 0.f && world > 0.f) {
			auto words = sentence(localized(state, "katechon_trade_prices", "Price at home %1, in the world %2."), text::format_money(ours), text::format_money(world));
			if(std::abs(ours / world - 1.f) >= 0.03f)
				words += " " + (ours < world ? filled(localized(state, "katechon_trade_price_cheaper", "Selling abroad pays %1 more."), text::format_percentage(world / ours - 1.f, 0))
					: filled(localized(state, "katechon_trade_price_dearer", "Buying abroad is %1 cheaper."), text::format_percentage(1.f - world / ours, 0)));
			lines.emplace_back(words, dip_muted);
		}
	}
	for(auto const& [words, colour] : lines) {
		if(y + 19.f > floor_y)
			break;
		y = sans_paragraph(state, words, left, right, y, 13, colour, 2, true) + 4.f;
	}
}

// ---- the full analysis ----

void analysis_card(sys::state& state, float x, float y, float w, float h) {
	fill(state, x, y, w, h, ink, 0.26f);
	fill(state, x, y, w, 1.f, dip_text, 0.05f);
}

// A column of a table: where it ends on the right, and its heading.
struct trade_column {
	std::string heading;
	float right = 0.f;
	bool numeric = true;
};

void table_head(sys::state& state, std::vector<trade_column> const& columns, float left, float y) {
	auto at = left;
	for(auto const& c : columns) {
		auto const label = upper(c.heading);
		if(c.numeric)
			sans(state, label, c.right, y + 8.f, 9, dip_faint, true, 0.10f);
		else
			sans(state, label, at, y + 8.f, 9, dip_faint, false, 0.10f);
		at = c.right + 14.f;
	}
}

void draw_trade_analysis(sys::state& state, float w, float h, bool hovering, float mx, float my) {
	auto const x0 = ribbon_w + 8.f;
	auto const y0 = top_h + 12.f;
	auto const x1 = w - 12.f;
	auto const y1 = h - 12.f - tool_h - 10.f;
	dip_frame(state, x0, y0, x1 - x0, y1 - y0);
	auto const left = x0 + 26.f;
	auto const right = x1 - 26.f;
	auto const& v = trade;

	// The head.
	sans(state, upper(localized(state, "katechon_trade_analysis_scope", "Economy · Trade analysis")), left, y0 + 24.f, 10, dip_bronze, false, 0.14f);
	{
		hit_target close{ right - 12.f, y0 + 11.f, 26.f, 26.f, action::toggle_trade };
		draw_icon(state, icon::close, close.x + 13.f, close.y + 13.f, 11.f, hovering && over(close, mx, my) ? dip_text : dip_bronze);
		targets.push_back(close);
	}
	if(v.nation)
		dip_flag(state, v.nation, left, y0 + 44.f, 36.f, 24.f);
	auto const name_w = draw_text(state, nation_name(state, v.nation), left + 50.f, y0 + 56.f, 26, 0.01f, dip_name);
	sans(state, sans_fit(state, filled(localized(state, "katechon_trade_period_line", "Value across the border, %1"), period_words(state)), 13, std::max(0.f, right - 520.f - (left + 50.f + name_w + 20.f))),
		left + 50.f + name_w + 20.f, y0 + 57.f, 13, dip_muted);
	{
		constexpr float back_w = 150.f;
		dip_ghost_button(state, right - 34.f - back_w, y0 + 42.f, back_w, 30.f, localized(state, "katechon_trade_back_map", "Back to the map"), hit_target{ 0.f, 0.f, 0.f, 0.f, action::trade_analysis }, hovering, mx, my);
		auto const sw = sans_w(state, localized(state, "katechon_trade_span_month", "30 days"), 12) + sans_w(state, localized(state, "katechon_trade_span_year", "12 months"), 12) + 48.f;
		span_switch(state, std::round(right - 34.f - back_w - 18.f - sw), y0 + 45.f, hovering, mx, my);
		dip_link(state, localized(state, "katechon_trade_all_tables", "Classic trade tables"), right - 34.f, y0 + 86.f, action::trade_details, hovering, mx, my, true);
		sans(state, filled(localized(state, "katechon_trade_asof", "as of %1"), text::date_to_string(state, state.current_date)), right - 34.f - back_w - 18.f - sw - 18.f, y0 + 57.f, 12, dip_faint, true);
	}

	// Four parts: how the trade goes, what it could not do without, what it is made of, with whom.
	auto const top = y0 + 100.f;
	constexpr float gap = 16.f;
	auto const lw = std::round((right - left - gap) * 0.54f);
	auto const rx = left + lw + gap;
	auto const rw = right - rx;
	auto const th = std::round((y1 - 18.f - top - gap) * 0.45f);
	auto const by = top + th + gap;
	auto const bh = y1 - 18.f - by;
	analysis_card(state, left, top, lw, th);
	analysis_card(state, rx, top, rw, th);
	analysis_card(state, left, by, lw, bh);
	analysis_card(state, rx, by, rw, bh);
	draw_performance(state, left + 16.f, top + 12.f, lw - 32.f, th - 24.f, hovering, mx, my);
	draw_dependencies(state, rx + 16.f, top + 12.f, rw - 32.f, th - 24.f, hovering, mx, my);
	draw_structure(state, left + 16.f, by + 12.f, lw - 32.f, bh - 24.f, hovering, mx, my);
	draw_partner_table(state, rx + 16.f, by + 12.f, rw - 32.f, bh - 24.f, hovering, mx, my);
}

// Exports, imports and the balance over the span, with how they moved.
void draw_performance(sys::state& state, float x, float y, float w, float h, bool hovering, float mx, float my) {
	auto const& v = trade;
	auto const right = x + w;
	auto const chart_bottom = y + h;
	y = trade_section(state, localized(state, "katechon_trade_performance", "Trade performance"), v.has_previous ? localized(state, "katechon_trade_against_before", "against the span before") : std::string(), x, right, y);
	auto const col = std::floor(w / 4.f);
	trade_kpi(state, localized(state, "katechon_trade_kpi_out", "Exports"), text::format_money(v.exported), out_tone, v.exported, v.previous_exported, true, false, true, x, y, col - 10.f, 22);
	trade_kpi(state, localized(state, "katechon_trade_kpi_in", "Imports"), text::format_money(v.imported), in_tone, v.imported, v.previous_imported, false, false, true, x + col, y, col - 10.f, 22);
	trade_kpi(state, localized(state, "katechon_trade_kpi_net", "Balance"), signed_money(v.exported - v.imported), v.exported >= v.imported ? dip_yes : dip_no, v.exported - v.imported,
		v.previous_exported - v.previous_imported, true, true, true, x + 2.f * col, y, col - 10.f, 22);
	trade_kpi(state, localized(state, "katechon_trade_kpi_turnover", "Turnover"), text::format_money(v.exported + v.imported), dip_text, v.exported + v.imported,
		v.previous_exported + v.previous_imported, true, false, true, x + 3.f * col, y, col - 10.f, 22);
	y += trade_kpi_h + 4.f;

	// The course over the span, a bar a day or a month; nothing is drawn for days before the start.
	auto const first = std::min(v.first_recorded_bucket, v.exported_series.size());
	auto const shown = v.exported_series.size() - first;
	if(shown < 3 || chart_bottom - y < 70.f) {
		if(chart_bottom - y >= 20.f)
			sans(state, sans_fit(state, sentence(localized(state, "katechon_trade_chart_wait", "The chart appears once three %1 are recorded; %2 so far."),
				v.span_days <= 30 ? localized(state, "katechon_trade_day", "days") : localized(state, "katechon_trade_month", "months"), std::to_string(shown)), 12, w), x, y + 14.f, 12, dip_faint);
		return;
	}
	// key
	{
		auto kx = x;
		for(auto const& [colour, label] : { std::pair{ out_tone, localized(state, "katechon_trade_exports", "Exports") }, std::pair{ in_tone, localized(state, "katechon_trade_imports", "Imports") },
				 std::pair{ dip_yes, localized(state, "katechon_trade_kpi_net", "Balance") } }) {
			fill(state, kx, y + 4.f, 8.f, 8.f, colour, 1.f);
			kx += 14.f + sans(state, label, kx + 14.f, y + 8.f, 11, dip_muted) + 14.f;
		}
	}
	y += 20.f;
	auto const axis_w = 52.f;
	auto const cx = x + axis_w;
	auto const cw = w - axis_w;
	auto const balance_h = 26.f;
	auto const label_h = 16.f;
	auto const bars_bottom = chart_bottom - balance_h - label_h - 6.f;
	auto const bars_h = bars_bottom - y;
	auto top = 0.f;
	auto net_top = 0.f;
	for(size_t i = first; i < v.exported_series.size(); ++i) {
		top = std::max({ top, v.exported_series[i], v.imported_series[i] });
		net_top = std::max(net_top, std::abs(v.exported_series[i] - v.imported_series[i]));
	}
	fill(state, cx, bars_bottom, cw, 1.f, dip_text, 0.18f);
	fill(state, cx, y + bars_h * 0.5f, cw, 1.f, dip_text, 0.05f);
	sans(state, text::format_money(top), cx - 8.f, y + 4.f, 10, dip_faint, true);
	sans(state, text::format_money(top * 0.5f), cx - 8.f, y + bars_h * 0.5f, 10, dip_faint, true);
	sans(state, localized(state, "katechon_trade_kpi_net", "Balance"), cx - 8.f, bars_bottom + 6.f + balance_h * 0.5f, 10, dip_faint, true);
	auto const mid = bars_bottom + 6.f + balance_h * 0.5f;
	fill(state, cx, mid, cw, 1.f, dip_text, 0.10f);
	auto const step = cw / float(shown);
	auto const bar = std::max(1.f, std::floor((step - 3.f) * 0.5f));
	for(size_t i = first; i < v.exported_series.size(); ++i) {
		auto const bx = std::round(cx + float(i - first) * step + (step - 2.f * bar - 1.f) * 0.5f);
		auto const out = v.exported_series[i];
		auto const in = v.imported_series[i];
		auto const out_h = top > 0.f ? std::round(bars_h * out / top) : 0.f;
		auto const in_h = top > 0.f ? std::round(bars_h * in / top) : 0.f;
		hit_target t{ cx + float(i - first) * step, y, step, chart_bottom - label_h - y, action::none };
		auto const hovered = hovering && over(t, mx, my);
		if(hovered)
			fill(state, t.x, t.y, t.w, t.h, dip_text, 0.05f);
		fill(state, bx, bars_bottom - out_h, bar, out_h, out_tone, hovered ? 1.f : 0.85f);
		fill(state, bx + bar + 1.f, bars_bottom - in_h, bar, in_h, in_tone, hovered ? 1.f : 0.85f);
		auto const net = out - in;
		auto const nh = net_top > 0.f ? std::max(1.f, std::round(balance_h * 0.5f * std::abs(net) / net_top)) : 0.f;
		if(nh > 0.f)
			fill(state, bx, net >= 0.f ? mid - nh : mid + 1.f, 2.f * bar + 1.f, nh, net >= 0.f ? dip_yes : dip_no, 0.9f);
		if(hovered) {
			auto const back = int32_t(v.exported_series.size() - 1 - i);
			auto const when = v.span_days <= 30 ? text::date_to_string(state, state.current_date - back)
				: sentence(localized(state, "katechon_trade_month_of", "%1 to %2"), text::date_to_string(state, state.current_date - (back * 30 + 29)), text::date_to_string(state, state.current_date - back * 30));
			hint(when + "  ·  " + localized(state, "katechon_trade_kpi_out", "Exports") + " " + text::format_money(out) + "  ·  " + localized(state, "katechon_trade_kpi_in", "Imports") + " "
				+ text::format_money(in) + "  ·  " + localized(state, "katechon_trade_kpi_net", "Balance") + " " + signed_money(net), t);
		}
	}
	// the ends of the span
	{
		auto const back = int32_t(shown - 1);
		auto const start = v.span_days <= 30 ? state.current_date - back : state.current_date - back * 30;
		sans(state, text::date_to_string(state, start), cx, chart_bottom - label_h * 0.5f, 10, dip_faint);
		sans(state, text::date_to_string(state, state.current_date), cx + cw, chart_bottom - label_h * 0.5f, 10, dip_faint, true);
	}
}

// The goods we could not do without, the worst first.
void draw_dependencies(sys::state& state, float x, float y, float w, float h, bool hovering, float mx, float my) {
	auto const right = x + w;
	auto const bottom = y + h;
	y = trade_section(state, localized(state, "katechon_trade_strategic", "Strategic dependencies"), localized(state, "katechon_trade_deps_aside", "imported share of use"), x, right, y);
	auto critical = 0;
	for(auto const& d : trade_deps)
		critical += d.level >= 2 ? 1 : 0;
	std::string summary;
	if(trade_deps.empty())
		summary = localized(state, "katechon_trade_no_deps_long", "No good relies on imports for a quarter of its use: supply is safe from foreign shocks.");
	else
		summary = sentence(localized(state, "katechon_trade_deps_summary", "%1 goods rely on imports for a quarter or more of their use; %2 of them critically."), std::to_string(trade_deps.size()), std::to_string(critical));
	trade_partner_view const* hardest = nullptr;
	for(auto const& p : trade_partners) {
		if(p.hard_to_replace && (!hardest || p.leans_share > hardest->leans_share))
			hardest = &p;
	}
	if(hardest)
		summary += " " + sentence(localized(state, "katechon_trade_hardest", "Hardest to replace: %1, for %2."), nation_name(state, hardest->nation), goods_name(state, hardest->leans_on));
	y = sans_paragraph(state, summary, x, right, y, 13, dip_text, 2, true) + 8.f;
	if(trade_deps.empty())
		return;
	auto const risk_r = right;
	auto const source_r = right - 76.f;
	auto const share_r = source_r - 140.f;
	std::vector<trade_column> columns{ trade_column{ localized(state, "katechon_trade_col_good", "Good"), share_r - 104.f, false },
		trade_column{ localized(state, "katechon_trade_col_imported", "Imported"), share_r }, trade_column{ localized(state, "katechon_trade_col_source", "Main supplier"), source_r, false },
		trade_column{ localized(state, "katechon_trade_col_risk", "Risk"), risk_r } };
	table_head(state, columns, x, y);
	y += 20.f;
	constexpr float pitch = 28.f;
	size_t shown = 0;
	for(auto const& d : trade_deps) {
		if(y + pitch > bottom - (shown + 1 < trade_deps.size() ? 18.f : 0.f))
			break;
		hit_target t{ x - 6.f, y, w + 12.f, pitch, action::trade_focus, nullptr, good_value(d.good_id) };
		auto const hovered = hovering && over(t, mx, my);
		if(hovered)
			fill(state, t.x, t.y, t.w, t.h, dip_text, 0.05f);
		auto const tone = level_tone(d.level);
		auto const cy = y + pitch * 0.5f;
		good_icon(state, d.good_id, x + 8.f, cy, 16.f, tone);
		auto const kind = kind_word(state, d.kind);
		auto const kw = kind.empty() ? 0.f : sans_w(state, kind, 11) + 10.f;
		auto const nw = sans(state, sans_fit(state, goods_name(state, d.good_id), 13, share_r - 104.f - x - 24.f - kw), x + 24.f, cy, 13, hovered ? dip_name : dip_text);
		if(!kind.empty())
			sans(state, kind, x + 32.f + nw, cy, 11, dip_bronze);
		share_bar(state, share_r - 96.f, cy, 56.f, d.import_share, tone);
		sans(state, text::format_percentage(d.import_share, 0), share_r, cy, 13, tone, true);
		if(d.source) {
			dip_flag(state, d.source, share_r + 16.f, cy - 5.f, 15.f, 10.f);
			sans(state, sans_fit(state, nation_name(state, d.source), 12, source_r - share_r - 80.f), share_r + 38.f, cy, 12, dip_text);
			sans(state, text::format_percentage(d.source_share, 0), source_r, cy, 12, d.source_share >= 0.7f ? high_tone : dip_muted, true);
		}
		sans(state, level_word(state, d.level), risk_r, cy, 12, tone, true);
		if(hovered) {
			auto note = sentence(localized(state, "katechon_trade_dep_long_tip", "%1 suppliers; we make %2 of our use at home"), std::to_string(d.suppliers), d.cover < 0.f ? std::string("—") : text::format_percentage(std::min(d.cover, 1.f), 0));
			hint(note, t);
		}
		targets.push_back(t);
		y += pitch;
		++shown;
	}
	if(shown < trade_deps.size())
		sans(state, filled(localized(state, "katechon_trade_more", "and %1 more"), std::to_string(trade_deps.size() - shown)), x, y + 9.f, 11, dip_faint);
}

// The goods crossing the border: their value both ways, what we make and use, how far we cover ourselves.
void draw_structure(sys::state& state, float x, float y, float w, float h, bool hovering, float mx, float my) {
	auto const right = x + w;
	auto const bottom = y + h;
	y = trade_section(state, localized(state, "katechon_trade_structure", "Commodity structure"), localized(state, "katechon_trade_structure_aside", "values in money, made and used in units"), x, right, y);
	std::vector<economy::trade_picture::commodity_line const*> rows;
	size_t home_only = 0;
	for(auto const& line : trade.commodities) {
		if(line.exported > 0.f || line.imported > 0.f)
			rows.push_back(&line);
		else
			++home_only;
	}
	std::stable_sort(rows.begin(), rows.end(), [](auto const* a, auto const* b) { return a->exported + a->imported > b->exported + b->imported; });
	if(rows.empty()) {
		sans(state, filled(localized(state, "katechon_trade_quiet_border", "Nothing crossed the border over %1"), period_words(state)), x, y + 13.f, 13, dip_faint);
		return;
	}
	auto const wide = w >= 560.f;
	constexpr float num = 70.f;
	auto const partner_r = right;
	auto const cover_r = partner_r - 96.f;
	auto const used_r = cover_r - num;
	auto const made_r = wide ? used_r - num : used_r;
	auto const imports_r = wide ? made_r - num : cover_r - num;
	auto const exports_r = imports_r - num;
	std::vector<trade_column> columns{ trade_column{ localized(state, "katechon_trade_col_good", "Good"), exports_r - num, false },
		trade_column{ localized(state, "katechon_trade_kpi_out", "Exports"), exports_r }, trade_column{ localized(state, "katechon_trade_kpi_in", "Imports"), imports_r } };
	if(wide) {
		columns.push_back(trade_column{ localized(state, "katechon_trade_col_made", "Made"), made_r });
		columns.push_back(trade_column{ localized(state, "katechon_trade_col_used", "Used"), used_r });
	}
	columns.push_back(trade_column{ localized(state, "katechon_trade_col_cover", "Home cover"), cover_r });
	columns.push_back(trade_column{ localized(state, "katechon_trade_col_partner", "Main partner"), partner_r, false });
	table_head(state, columns, x, y);
	y += 20.f;
	auto shown = size_t(0);
	for(auto const* line : rows) {
		if(y + trade_row_h > bottom - (shown + 1 < rows.size() || home_only ? 18.f : 0.f))
			break;
		hit_target t{ x - 6.f, y, w + 12.f, trade_row_h, action::trade_focus, nullptr, good_value(line->commodity) };
		auto const hovered = hovering && over(t, mx, my);
		if(hovered)
			fill(state, t.x, t.y, t.w, t.h, dip_text, 0.05f);
		else if(shown % 2 == 1)
			fill(state, t.x, t.y, t.w, t.h, dip_text, 0.02f);
		auto const cy = y + trade_row_h * 0.5f;
		auto const cover = line->self_sufficiency();
		good_icon(state, line->commodity, x + 8.f, cy, 16.f, line->exported >= line->imported ? out_tone : in_tone);
		sans(state, sans_fit(state, goods_name(state, line->commodity), 13, exports_r - x - 24.f - 64.f), x + 24.f, cy, 13, hovered ? dip_name : dip_text);
		sans(state, line->exported > 0.f ? text::format_money(line->exported) : std::string("—"), exports_r, cy, 13, line->exported > 0.f ? out_tone : dip_faint, true);
		sans(state, line->imported > 0.f ? text::format_money(line->imported) : std::string("—"), imports_r, cy, 13, line->imported > 0.f ? in_tone : dip_faint, true);
		if(wide) {
			sans(state, text::prettify_float(line->sold), made_r, cy, 13, dip_muted, true);
			sans(state, text::prettify_float(line->bought), used_r, cy, 13, dip_muted, true);
		}
		sans(state, cover < 0.f ? std::string("—") : text::format_percentage(cover, 0), cover_r, cy, 13, cover_tone(cover), true);
		auto const by_out = line->exported >= line->imported;
		auto const n = by_out ? line->main_buyer : line->main_source;
		if(n) {
			dip_flag(state, n, cover_r + 16.f, cy - 5.f, 15.f, 10.f);
			sans(state, text::format_percentage(by_out ? line->main_buyer_share : line->main_source_share, 0), partner_r, cy, 12, dip_muted, true);
			if(hovered)
				hint(sentence(by_out ? localized(state, "katechon_trade_buyer_tip", "%1 buys %2 of our exports of it") : localized(state, "katechon_trade_source_tip", "%1 sells us %2 of our imports of it"),
					nation_name(state, n), text::format_percentage(by_out ? line->main_buyer_share : line->main_source_share, 0)), t);
		}
		targets.push_back(t);
		y += trade_row_h;
		++shown;
	}
	std::string more;
	if(shown < rows.size())
		more = filled(localized(state, "katechon_trade_more", "and %1 more"), std::to_string(rows.size() - shown));
	if(home_only > 0)
		more += (more.empty() ? "" : "  ·  ") + filled(localized(state, "katechon_trade_home_only", "%1 goods traded only at home"), std::to_string(home_only));
	if(!more.empty())
		sans(state, more, x, y + 9.f, 11, dip_faint);
}

// With whom we trade, how it balances, and what would go with each.
void draw_partner_table(sys::state& state, float x, float y, float w, float h, bool hovering, float mx, float my) {
	auto const right = x + w;
	auto const bottom = y + h;
	y = trade_section(state, localized(state, "katechon_trade_partners", "Trading partners"), localized(state, "katechon_trade_partners_aside", "% of exports + imports"), x, right, y);
	if(trade_partners.empty()) {
		sans(state, filled(localized(state, "katechon_trade_quiet_border", "Nothing crossed the border over %1"), period_words(state)), x, y + 13.f, 13, dip_faint);
		return;
	}
	auto const reliance_r = right;
	auto const share_r = right - 84.f;
	auto const balance_r = share_r - 46.f;
	auto const imports_r = balance_r - 72.f;
	auto const exports_r = imports_r - 66.f;
	std::vector<trade_column> columns{ trade_column{ localized(state, "katechon_trade_col_country", "Country"), exports_r - 66.f, false },
		trade_column{ localized(state, "katechon_trade_kpi_out", "Exports"), exports_r }, trade_column{ localized(state, "katechon_trade_kpi_in", "Imports"), imports_r },
		trade_column{ localized(state, "katechon_trade_kpi_net", "Balance"), balance_r }, trade_column{ localized(state, "katechon_trade_col_share", "Share"), share_r },
		trade_column{ localized(state, "katechon_trade_col_reliance", "We rely on"), reliance_r } };
	table_head(state, columns, x, y);
	y += 20.f;
	size_t shown = 0;
	for(auto const& p : trade_partners) {
		if(y + trade_row_h > bottom - (shown + 1 < trade_partners.size() ? 18.f : 0.f))
			break;
		hit_target t{ x - 6.f, y, w + 12.f, trade_row_h, action::trade_focus, nullptr, partner_value(p.nation) };
		auto const hovered = hovering && over(t, mx, my);
		if(hovered)
			fill(state, t.x, t.y, t.w, t.h, dip_text, 0.05f);
		else if(shown % 2 == 1)
			fill(state, t.x, t.y, t.w, t.h, dip_text, 0.02f);
		auto const cy = y + trade_row_h * 0.5f;
		dip_flag(state, p.nation, x, cy - 6.f, 18.f, 12.f);
		sans(state, sans_fit(state, nation_name(state, p.nation), 13, exports_r - x - 28.f - 62.f), x + 28.f, cy, 13, hovered ? dip_name : dip_text);
		sans(state, p.exported > 0.f ? text::format_money(p.exported) : std::string("—"), exports_r, cy, 13, p.exported > 0.f ? out_tone : dip_faint, true);
		sans(state, p.imported > 0.f ? text::format_money(p.imported) : std::string("—"), imports_r, cy, 13, p.imported > 0.f ? in_tone : dip_faint, true);
		sans(state, signed_money(p.exported - p.imported), balance_r, cy, 13, p.exported >= p.imported ? dip_yes : dip_no, true);
		sans(state, text::format_percentage(p.share, 0), share_r, cy, 13, dip_text, true);
		if(p.leans_on && p.leans_share >= 0.05f) {
			auto const tone = p.hard_to_replace ? dip_no : dip_muted;
			auto const pw = sans(state, text::format_percentage(p.leans_share, 0), reliance_r, cy, 12, tone, true);
			good_icon(state, p.leans_on, reliance_r - pw - 14.f, cy, 15.f, tone);
		} else {
			sans(state, "—", reliance_r, cy, 12, dip_faint, true);
		}
		if(hovered) {
			auto note = p.sells ? sentence(localized(state, "katechon_trade_sells_tip", "We sell them mostly %1, buy mostly %2"), goods_name(state, p.sells), p.buys ? goods_name(state, p.buys) : std::string("—"))
				: sentence(localized(state, "katechon_trade_sells_tip", "We sell them mostly %1, buy mostly %2"), std::string("—"), p.buys ? goods_name(state, p.buys) : std::string("—"));
			if(p.leans_on && p.leans_share >= 0.05f)
				note += ".  " + sentence(localized(state, "katechon_trade_cut_tip", "Without them we lose %1 of our %2"), text::format_percentage(p.leans_share, 0), goods_name(state, p.leans_on));
			hint(note, t);
		}
		targets.push_back(t);
		y += trade_row_h;
		++shown;
	}
	if(shown < trade_partners.size())
		sans(state, filled(localized(state, "katechon_trade_more", "and %1 more"), std::to_string(trade_partners.size() - shown)), x, y + 9.f, 11, dip_faint);
}
