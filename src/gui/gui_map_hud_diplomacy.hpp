// The diplomacy of the KATECHON map HUD. This file is included inside the body
// of ui::map_hud_element (gui_map_hud.hpp) and uses its drawing helpers; it is
// not a header of its own.
//
// The sheet shows how two states stand and why, and what can be done about it;
// it is not a list of commands. For another country it shows how we stand
// (relations, the trust it places in our word, our leverage over it), what we
// share and what divides us, and the actions that matter now. How it would
// answer a proposal is not known in advance: the sheet shows our reading of it
// (nations::strategic_statecraft::assess_*), as good as our contacts, our
// intelligence and our envoys make it, with the reasons as we read them. A
// proposal is reviewed before it is sent: what it binds us to, what it costs,
// how long it holds, how it ends and whom else it concerns. Actions come in
// four kinds (relations, agreements, influence, coercion); an action that
// cannot be taken says what would allow it. The map draws the country's ties
// as arcs between capitals and colours every country by its attitude to it;
// under the mouse a country says how it stands with it.

// ---- what the player can do ----
enum class deed : uint8_t {
	none, improve, worsen, ally, break_alliance, ask_access, end_access, give_access, revoke_access, free_trade, embargo, lift_embargo,
	subsidise, end_subsidies, declare, justify, cancel_justification, white_peace, press_goals, concede, back_attacker, back_defender, take_interest,
	open_dossier, open_deal, lift_their_embargo
};
enum class dip_kind : uint8_t { relations, agreements, influence, coercion, count };
using likelihood = nations::strategic_statecraft::likelihood;
// The cells of assets/katechon_dip_icons.png (tools/katechon_ui/build_diplomacy.py).
enum class dip_icon : int32_t { shield, passage, scales, coin, heart, peace, swords, eye, scroll, quill, people, star, lock, envelope, check, cross, count };
struct deed_entry {
	deed what = deed::none;
	dip_kind kind = dip_kind::relations;
	std::string label;
	std::string effect; // what it costs and what it does
	std::string requirement; // when it cannot be taken now: what would allow it
	bool possible = false;
	bool proposal = false; // the other side answers it
	likelihood verdict = likelihood::unknown;
	std::string reason; // the reasons for that answer, as we read them
	bool warlike = false;
	dcon::cb_type_id cb;
	dcon::state_definition_id where;
	float cost = 0.f; // in diplomats
	float relevance = 0.f; // how much it matters now
	uint16_t ask_bit = 0; // as an item of a deal
	// When they would likely refuse, what we could give in return for them to agree.
	uint16_t in_return = 0;
	std::string in_return_text;
};
struct counterpart {
	dcon::nation_id id;
	std::string name;
	std::string note;
	bool warning = false;
};
// A point in common or of friction, said in a few words; why, under the mouse.
struct interest_chip {
	std::string text;
	int8_t tone = 0; // 1 common ground, -1 friction, 0 neither
	std::string why;
};
struct history_line {
	int32_t day = 0;
	std::string text;
	int8_t tone = 0;
};
// One of the three figures under the name: relations, trust, influence.
struct standing_figure {
	std::string value;
	ogl::color3f colour{ 0.94f, 0.92f, 0.87f };
	int32_t trend = 0; // -1, 0, 1
	bool trend_known = false;
	std::string trend_word;
	std::vector<std::string> detail;
};
// A tie drawn on the map from the country on the sheet.
enum class tie_kind : uint8_t { war, alliance, subject, hostile, claim, rival, cordial };
struct tie {
	dcon::nation_id other;
	tie_kind kind = tie_kind::cordial;
	float weight = 0.f;
};
struct diplomacy_view {
	dcon::nation_id nation;
	bool own = true;
	std::string title;
	std::string subtitle;
	std::string dispatch; // a sentence or two on how things stand
	// another country
	float relation = 0.f;
	int32_t temper = 2; // 0 hostile .. 4 close
	std::string temper_word;
	standing_figure relations;
	standing_figure trust;
	standing_figure influence;
	nations::strategic_statecraft::reading knowledge;
	std::string knowledge_word;
	std::vector<std::string> knowledge_detail;
	std::vector<counterpart> friends;
	std::vector<counterpart> foes;
	std::vector<interest_chip> common;
	std::vector<interest_chip> frictions;
	std::vector<interest_chip> priorities;
	std::vector<deed_entry> deeds;
	std::vector<size_t> top; // the deeds put forward, the first most of all
	dcon::war_id war;
	std::vector<history_line> history;
	// one's own country
	float diplomats = 0.f;
	float infamy = 0.f;
	float infamy_limit = 0.f;
	std::vector<counterpart> allies;
	std::vector<counterpart> prospects;
	std::vector<counterpart> wars;
	bool crisis = false;
	std::string crisis_title;
	std::vector<deed_entry> crisis_deeds;
	std::vector<tie> ties;
};
// Something another state has put to us and waits for an answer.
struct request_card {
	diplomatic_message::message message;
	std::string title;
	std::string body;
};
// What became of something we put to another state.
struct journal_entry {
	dcon::nation_id target;
	std::string what;
	uint16_t gives = 0;
	uint16_t asks = 0;
	deed kind = deed::none;
	int8_t outcome = 0; // 0 waiting, 1 taken, -1 refused
	sys::date sent;
	std::chrono::steady_clock::time_point sent_at;
};
// A figure recorded once a day, for the way it went over the last month.
struct day_series {
	std::array<float, 32> value{};
	std::array<int32_t, 32> day{};
	void put(int32_t today, float v) {
		value[size_t(today) % 32] = v;
		day[size_t(today) % 32] = today;
	}
	// The change over the last thirty days, or over as many as were recorded when they are a week or more.
	bool change(int32_t today, float& out) const {
		if(day[size_t(today) % 32] != today)
			return false;
		for(int32_t back = 30; back >= 7; --back) {
			auto const d = today - back;
			if(d > 0 && day[size_t(d) % 32] == d) {
				out = value[size_t(today) % 32] - value[size_t(d) % 32];
				return true;
			}
		}
		return false;
	}
};

bool diplomacy_open = false;
bool diplomacy_layer = false; // the sheet paints the map by relations until the player picks a layer
diplomacy_view diplomacy;
dcon::nation_id diplomacy_focus; // the country on the sheet; the player's own when unset
dcon::province_id diplomacy_seen_province; // a new choice on the map changes the country on the sheet
int32_t diplomacy_repaint_in = 0;
int32_t dip_tab = 0; // 0 what matters now, 1..4 the four kinds of action, 5 the history
std::string qa_diplomacy;
// The review of a proposal: the deed, the ground of a war, what we ask and what we give with it.
bool deal_open = false;
deed review_deed = deed::none;
uint64_t review_ground = 0;
uint16_t deal_gives = 0;
uint16_t deal_asks = 0;
bool deal_why = false;
std::vector<nations::strategic_statecraft::reaction> review_reactions;
dcon::nation_id reactions_for;
deed reactions_deed = deed::none;
int32_t reactions_day = -1;
bool radial_open = false;
bool qa_radial = false;
uint16_t qa_deal_gives = 0;
uint16_t qa_deal_asks = 0;
int32_t qa_dip_tab = -1;
bool qa_deal_send = false; // KATECHON_HUD_DEAL_SEND=1: put the QA proposal to them once the sheet is open
dcon::nation_id radial_nation;
float radial_x = 0.f;
float radial_y = 0.f;
std::vector<request_card> requests;
std::vector<uint8_t> diplomacy_related; // by nation: the country on the panel, the player, and whoever is tied to the first
std::vector<journal_entry> journal; // the session's proposals, newest last
std::vector<journal_entry> journal_kept; // all of them, for the history
struct toast {
	dcon::nation_id from;
	std::string text;
	bool good = false;
	std::chrono::steady_clock::time_point since;
};
std::vector<toast> toasts;
// how relations and trust stood, by day, as seen from the player; influence for the countries put on the sheet
std::vector<day_series> relation_series;
std::vector<day_series> trust_series;
std::vector<day_series> influence_series;
// the share of a country's foreign trade that is with the player, by the day it was taken
dcon::nation_id trade_share_for;
int32_t trade_share_day = -1;
float trade_share_value = 0.f;
// a hint of several lines, for what needs more than one
std::vector<std::string> dip_tip;
rect dip_tip_at{};
inline static map_hud_element* the_hud = nullptr;
inline static constexpr std::array<ramp_stop, 3> relation_ramp{ ramp_stop{ -1.f, { 0.56f, 0.28f, 0.24f } }, ramp_stop{ 0.f, { 0.25f, 0.27f, 0.30f } },
	ramp_stop{ 1.f, { 0.30f, 0.52f, 0.46f } } };
inline static constexpr ogl::color3f temper_colours[5]{ { 0.90f, 0.40f, 0.34f }, { 0.86f, 0.60f, 0.42f }, { 0.70f, 0.70f, 0.66f }, { 0.56f, 0.80f, 0.62f }, { 0.48f, 0.80f, 0.90f } };

static uint64_t pack_ground(dcon::cb_type_id cb, dcon::state_definition_id where) {
	return (uint64_t(cb ? cb.index() + 1 : 0) << 32) | uint64_t(where ? where.index() + 1 : 0);
}
static dcon::cb_type_id ground_cb(uint64_t packed) {
	auto const v = uint32_t(packed >> 32);
	return v ? dcon::cb_type_id{ dcon::cb_type_id::value_base_t(v - 1) } : dcon::cb_type_id{};
}
static dcon::state_definition_id ground_state(uint64_t packed) {
	auto const v = uint32_t(packed & 0xFFFFFFFFu);
	return v ? dcon::state_definition_id{ dcon::state_definition_id::value_base_t(v - 1) } : dcon::state_definition_id{};
}
static int32_t nation_value(dcon::nation_id n) {
	return n ? int32_t(n.index()) + 1 : 0;
}
static dcon::nation_id value_nation(int32_t v) {
	return v > 0 ? dcon::nation_id{ dcon::nation_id::value_base_t(v - 1) } : dcon::nation_id{};
}
static sys::date date_of_day(int32_t day) {
	return day > 0 ? sys::date{ sys::date::value_base_t(day - 1) } : sys::date{};
}
std::string nation_name(sys::state& state, dcon::nation_id n) {
	return n ? text::get_name_as_string(state, dcon::fatten(state.world, n)) : std::string();
}
static float relation_between(sys::state& state, dcon::nation_id a, dcon::nation_id b) {
	return state.world.diplomatic_relation_get_value(state.world.get_diplomatic_relation_by_diplomatic_pair(a, b));
}
static int32_t temper_of(float relation) {
	if(relation <= -100.f)
		return 0;
	if(relation <= -25.f)
		return 1;
	if(relation < 25.f)
		return 2;
	if(relation < 100.f)
		return 3;
	return 4;
}
std::string temper_word(sys::state& state, int32_t temper) {
	static constexpr std::array<std::pair<std::string_view, std::string_view>, 5> words{ std::pair<std::string_view, std::string_view>{ "katechon_dip_t0", "Hostile" },
		std::pair<std::string_view, std::string_view>{ "katechon_dip_t1", "Unfriendly" }, std::pair<std::string_view, std::string_view>{ "katechon_dip_t2", "Neutral" },
		std::pair<std::string_view, std::string_view>{ "katechon_dip_t3", "Friendly" }, std::pair<std::string_view, std::string_view>{ "katechon_dip_t4", "Close" } };
	auto const& w = words[size_t(std::clamp(temper, 0, 4))];
	return localized(state, w.first, w.second);
}
static bool holds_claim(sys::state& state, dcon::nation_id holder, dcon::nation_id claimant) {
	auto const identity = state.world.nation_get_identity_from_identity_holder(claimant);
	if(!identity)
		return false;
	for(auto ownership : state.world.nation_get_province_ownership(holder)) {
		for(auto core : ownership.get_province().get_core_as_province()) {
			if(core.get_identity() == identity)
				return true;
		}
	}
	return false;
}
static dcon::nation_id overlord_of(sys::state& state, dcon::nation_id n) {
	return state.world.overlord_get_ruler(state.world.nation_get_overlord_as_subject(n));
}
static bool embargoes(sys::state& state, dcon::nation_id from, dcon::nation_id on) {
	auto const rel = state.world.get_unilateral_relationship_by_unilateral_pair(on, from);
	return rel && state.world.unilateral_relationship_get_embargo(rel);
}
static bool gives_access(sys::state& state, dcon::nation_id giver, dcon::nation_id to) {
	auto const rel = state.world.get_unilateral_relationship_by_unilateral_pair(giver, to);
	return rel && state.world.unilateral_relationship_get_military_access(rel);
}
static bool subsidises(sys::state& state, dcon::nation_id payer, dcon::nation_id to) {
	auto const rel = state.world.get_unilateral_relationship_by_unilateral_pair(to, payer);
	return rel && state.world.unilateral_relationship_get_war_subsidies(rel);
}
// A sentence from a pattern with %1 and %2.
static std::string sentence(std::string pattern, std::string const& a, std::string const& b = std::string()) {
	if(auto at = pattern.find("%1"); at != std::string::npos)
		pattern.replace(at, 2, a);
	if(auto at = pattern.find("%2"); at != std::string::npos)
		pattern.replace(at, 2, b);
	return pattern;
}
std::string join_names(sys::state& state, std::vector<counterpart> const& list, size_t most) {
	std::string out;
	for(size_t i = 0; i < list.size() && i < most; ++i)
		out += (i ? ", " : "") + list[i].name;
	if(list.size() > most)
		out += " " + filled(localized(state, "katechon_dip_and_more", "and %1 more"), std::to_string(list.size() - most));
	return out;
}
std::string diplomats_word(sys::state& state, float n) {
	return filled(localized(state, "katechon_dip_n_diplomats", "diplomats: %1"), text::format_float(n, n == std::floor(n) ? 0 : 1));
}
static std::string whole(float n) {
	return text::format_float(n, n == std::floor(n) ? 0 : 1);
}

// ---- the words of the model ----
// A reason, as it appears among the reasons for an answer.
std::string factor_word(sys::state& state, nations::strategic_statecraft::forecast_factor f) {
	using ff = nations::strategic_statecraft::forecast_factor;
	switch(f) {
	case ff::common_threat: return localized(state, "katechon_dip_f_threat", "a rival we share");
	case ff::strategic_value: return localized(state, "katechon_dip_f_value", "what we are worth to them");
	case ff::reliability: return localized(state, "katechon_dip_f_reliability", "our record of keeping our word");
	case ff::proximity: return localized(state, "katechon_dip_f_proximity", "how near we are");
	case ff::entanglement: return localized(state, "katechon_dip_f_entanglement", "the wars and pacts we would bring");
	case ff::claim_conflict: return localized(state, "katechon_dip_f_claims", "land one of us claims from the other");
	case ff::infamy: return localized(state, "katechon_dip_f_infamy", "their ill repute");
	case ff::shared_enemy: return localized(state, "katechon_dip_f_enemy", "an enemy we fight together");
	case ff::alliance: return localized(state, "katechon_dip_f_alliance", "our alliance");
	case ff::market_gain: return localized(state, "katechon_dip_f_market", "what our market offers them");
	case ff::security_cost: return localized(state, "katechon_dip_f_security", "the risk to their security");
	case ff::relations: return localized(state, "katechon_dip_f_relations", "relations between us");
	case ff::third_parties: return localized(state, "katechon_dip_f_third", "our ties to their rivals and friends");
	case ff::none: break;
	}
	return std::string();
}
std::string verdict_word(sys::state& state, likelihood l) {
	switch(l) {
	case likelihood::very_likely: return localized(state, "katechon_dip_l_very_likely", "Very likely");
	case likelihood::likely: return localized(state, "katechon_dip_l_likely", "Likely");
	case likelihood::uncertain: return localized(state, "katechon_dip_l_uncertain", "Uncertain");
	case likelihood::unlikely: return localized(state, "katechon_dip_l_unlikely", "Unlikely");
	case likelihood::very_unlikely: return localized(state, "katechon_dip_l_very_unlikely", "Very unlikely");
	case likelihood::out_of_question: return localized(state, "katechon_dip_l_never", "Out of the question");
	case likelihood::unknown: break;
	}
	return std::string();
}
ogl::color3f verdict_colour(likelihood l) {
	switch(l) {
	case likelihood::very_likely: return ogl::color3f{ 0.50f, 0.84f, 0.58f };
	case likelihood::likely: return ogl::color3f{ 0.66f, 0.82f, 0.52f };
	case likelihood::uncertain: return ogl::color3f{ 0.92f, 0.76f, 0.42f };
	case likelihood::unlikely: return ogl::color3f{ 0.94f, 0.58f, 0.40f };
	case likelihood::very_unlikely: return ogl::color3f{ 0.93f, 0.46f, 0.38f };
	case likelihood::out_of_question: return ogl::color3f{ 0.80f, 0.40f, 0.36f };
	case likelihood::unknown: break;
	}
	return ogl::color3f{ 0.62f, 0.64f, 0.66f };
}
static bool favourable(likelihood l) {
	return l == likelihood::likely || l == likelihood::very_likely;
}
// The reasons in a line: what pulls for the answer and what against, as we read them.
std::string reasons_line(sys::state& state, nations::strategic_statecraft::assessment const& a) {
	namespace sc = nations::strategic_statecraft;
	if(!a.modelled)
		return std::string();
	if(a.veto != sc::forecast_factor::none)
		return sentence(localized(state, "katechon_dip_r_veto", "Ruled out by %1"), factor_word(state, a.veto));
	auto best_for = sc::forecast_factor::none;
	auto best_against = sc::forecast_factor::none;
	auto pull_for = 0.02f;
	auto pull_against = 0.02f;
	for(auto const& p : a.parts) {
		if(p.factor == sc::forecast_factor::none)
			continue;
		if(p.weight > pull_for) {
			pull_for = p.weight;
			best_for = p.factor;
		}
		if(-p.weight > pull_against) {
			pull_against = -p.weight;
			best_against = p.factor;
		}
	}
	auto const pro = factor_word(state, best_for);
	auto const con = factor_word(state, best_against);
	if(favourable(a.verdict) || con.empty())
		return pro.empty() ? std::string() : sentence(localized(state, "katechon_dip_r_for", "For: %1"), pro);
	if(a.verdict == likelihood::uncertain && !pro.empty())
		return sentence(localized(state, "katechon_dip_r_both", "For: %1  ·  Against: %2"), pro, con);
	return sentence(localized(state, "katechon_dip_r_against", "Against: %1"), con);
}
std::string objective_word(sys::state& state, nations::strategic_statecraft::objective_kind k) {
	using ok = nations::strategic_statecraft::objective_kind;
	switch(k) {
	case ok::security: return localized(state, "katechon_dip_aim_security", "security");
	case ok::territorial_claim: return localized(state, "katechon_dip_aim_claim", "a territorial claim");
	case ok::market_access: return localized(state, "katechon_dip_aim_market", "access to markets");
	case ok::resource_access: return localized(state, "katechon_dip_aim_resources", "access to resources");
	case ok::route_access: return localized(state, "katechon_dip_aim_routes", "trade routes");
	case ok::protect_partner: return localized(state, "katechon_dip_aim_protect", "protecting a partner");
	case ok::prestige: return localized(state, "katechon_dip_aim_prestige", "prestige");
	case ok::balance_threat: return localized(state, "katechon_dip_aim_balance", "a counterweight to a threat");
	case ok::alliance: return localized(state, "katechon_dip_aim_alliance", "an alliance");
	case ok::none: break;
	}
	return std::string();
}

// ---- the record of a month, for the trends ----
void record_relations(sys::state& state) {
	auto const player = state.local_player_nation;
	if(!player)
		return;
	auto const today = int32_t(state.current_date.value);
	auto const count = state.world.nation_size();
	if(relation_series.size() != count) {
		relation_series.assign(count, day_series{});
		trust_series.assign(count, day_series{});
		influence_series.assign(count, day_series{});
	}
	for(auto other : state.world.in_nation) {
		auto const i = size_t(other.id.index());
		if(relation_series[i].day[size_t(today) % 32] == today)
			continue;
		relation_series[i].put(today, relation_between(state, player, other.id));
		trust_series[i].put(today, nations::strategic_statecraft::trust_of(state, other.id, player).reliability);
	}
}
// A figure's way over the month, in a word.
void set_trend(sys::state& state, standing_figure& f, day_series const* series, float step, std::string_view up_key, std::string_view up, std::string_view down_key, std::string_view down) {
	float change = 0.f;
	if(!series || !series->change(int32_t(state.current_date.value), change))
		return;
	f.trend_known = true;
	f.trend = change >= step ? 1 : (change <= -step ? -1 : 0);
	f.trend_word = f.trend > 0 ? localized(state, up_key, up) : (f.trend < 0 ? localized(state, down_key, down) : localized(state, "katechon_dip_steady", "Stable"));
}

// ---- our leverage over another country ----
struct leverage {
	float total = 0.f;
	float trade_share = 0.f; // of their foreign trade, the part with us
	float power = 0.5f; // our military share against theirs, as we read their view of it
	bool subsidies = false;
	bool free_trade = false;
	bool overlord = false;
	bool sphere = false;
};
float trade_share_with(sys::state& state, dcon::nation_id target, dcon::nation_id player) {
	auto const today = int32_t(state.current_date.value);
	if(trade_share_for != target || trade_share_day != today) {
		trade_share_for = target;
		trade_share_day = today;
		auto const picture = economy::trade_picture::take(state, target, 30);
		trade_share_value = size_t(player.index()) < picture.share_by_nation.size() ? picture.share_by_nation[size_t(player.index())] : 0.f;
	}
	return trade_share_value;
}
leverage leverage_over(sys::state& state, dcon::nation_id player, dcon::nation_id target, float quality) {
	namespace sc = nations::strategic_statecraft;
	leverage l{};
	l.trade_share = std::clamp(trade_share_with(state, target, player), 0.f, 1.f);
	// what they believe of our strength is theirs; we read it from our own view of them
	auto const* theirs = sc::perception_of(state, target, player);
	auto const* ours = sc::perception_of(state, player, target);
	auto const truth = theirs ? theirs->military_power : 0.5f;
	auto const prior = ours ? 1.f - ours->military_power : 0.5f;
	l.power = prior + (truth - prior) * quality;
	l.subsidies = subsidises(state, player, target);
	l.free_trade = bool(economy::nation_gives_free_trade_rights(state, target, player)) || bool(economy::nation_gives_free_trade_rights(state, player, target));
	l.overlord = overlord_of(state, target) == player;
	l.sphere = state.world.nation_get_in_sphere_of(target) == player;
	l.total = std::clamp(0.35f * std::min(1.f, l.trade_share / 0.4f) + 0.25f * std::clamp((l.power - 0.5f) * 2.f, 0.f, 1.f) + (l.subsidies ? 0.15f : 0.f)
		+ (l.free_trade ? 0.10f : 0.f) + (l.overlord ? 0.5f : 0.f) + (l.sphere ? 0.35f : 0.f), 0.f, 1.f);
	return l;
}

// ---- gathering ----
void gather_ties(sys::state& state, diplomacy_view& v) {
	auto const focus = v.nation;
	auto const player = state.local_player_nation;
	for(auto other : state.world.in_nation) {
		if(other.id == focus || other.get_owned_province_count() == 0)
			continue;
		auto const relation = relation_between(state, focus, other.id);
		if(military::are_at_war(state, focus, other.id))
			v.ties.push_back(tie{ other.id, tie_kind::war, 500.f });
		else if(nations::are_allied(state, focus, other.id))
			v.ties.push_back(tie{ other.id, tie_kind::alliance, 400.f + relation });
		else if(overlord_of(state, other.id) == focus || overlord_of(state, focus) == other.id || state.world.nation_get_in_sphere_of(other.id) == focus || state.world.nation_get_in_sphere_of(focus) == other.id)
			v.ties.push_back(tie{ other.id, tie_kind::subject, 350.f });
		else if(holds_claim(state, focus, other.id) || holds_claim(state, other.id, focus))
			v.ties.push_back(tie{ other.id, tie_kind::claim, 300.f - relation * 0.5f });
		else if(relation <= -100.f)
			v.ties.push_back(tie{ other.id, tie_kind::hostile, -relation });
		else if((state.world.nation_get_ai_rival(other.id) == focus || state.world.nation_get_ai_rival(focus) == other.id)
			&& (focus == player || other.id == player || nations::strategic_statecraft::read_intentions(state, player, other.id == player ? focus : other.id).quality >= 0.3f))
			v.ties.push_back(tie{ other.id, tie_kind::rival, 200.f });
		else if(relation >= 100.f)
			v.ties.push_back(tie{ other.id, tie_kind::cordial, relation - 60.f });
	}
	std::sort(v.ties.begin(), v.ties.end(), [](tie const& a, tie const& b) { return a.weight > b.weight; });
	if(v.ties.size() > 28)
		v.ties.resize(28);
	diplomacy_related.assign(state.world.nation_size(), 0);
	diplomacy_related[size_t(focus.index())] = 1;
	if(player)
		diplomacy_related[size_t(player.index())] = 1;
	for(auto const& t : v.ties)
		diplomacy_related[size_t(t.other.index())] = 1;
}

void gather_requests(sys::state& state) {
	requests.clear();
	auto const player = state.local_player_nation;
	if(!player)
		return;
	using mt = diplomatic_message::type_t;
	for(auto const& m : state.pending_messages) {
		if(m.type == mt::none || m.to != player || !m.from)
			continue;
		request_card c{ m };
		auto const from = nation_name(state, m.from);
		switch(m.type) {
		case mt::alliance_request:
			c.title = localized(state, "katechon_req_alliance", "Alliance proposed");
			c.body = sentence(localized(state, "katechon_req_alliance_body", "%1 asks us to become its ally."), from);
			break;
		case mt::access_request:
			c.title = localized(state, "katechon_req_access", "Passage asked");
			c.body = sentence(localized(state, "katechon_req_access_body", "%1 asks for its forces to cross our land."), from);
			break;
		case mt::call_ally_request:
			c.title = localized(state, "katechon_req_call", "Called to war");
			c.body = sentence(localized(state, "katechon_req_call_body", "%1 calls on us as its ally: %2."), from, m.data.war ? military::get_war_name(state, m.data.war) : std::string());
			break;
		case mt::peace_offer:
			c.title = localized(state, "katechon_req_peace", "Peace offered");
			c.body = sentence(localized(state, "katechon_req_peace_body", "%1 offers terms of peace."), from);
			break;
		case mt::free_trade_agreement:
			c.title = localized(state, "katechon_req_trade", "Free trade proposed");
			c.body = sentence(localized(state, "katechon_req_trade_body", "%1 proposes free trade between us."), from);
			break;
		case mt::state_transfer:
			c.title = localized(state, "katechon_req_transfer", "Territory offered");
			c.body = sentence(localized(state, "katechon_req_transfer_body", "%1 offers us %2."), from, m.data.state ? text::get_name_as_string(state, m.data.state) : std::string());
			break;
		case mt::be_crisis_primary_attacker:
		case mt::be_crisis_primary_defender:
		case mt::take_crisis_side_offer:
		case mt::crisis_peace_offer:
			c.title = localized(state, "katechon_req_crisis", "In the crisis");
			c.body = sentence(localized(state, "katechon_req_crisis_body", "%1 asks us to take a part in the crisis."), from);
			break;
		case mt::none:
			break;
		}
		requests.push_back(std::move(c));
		if(requests.size() == 4)
			break;
	}
}

// Whether what was put to another state has come about.
void settle_journal(sys::state& state) {
	namespace sc = nations::strategic_statecraft;
	auto const player = state.local_player_nation;
	auto const now = std::chrono::steady_clock::now();
	for(auto& e : journal) {
		if(e.outcome != 0 || !player)
			continue;
		auto done = false;
		switch(e.kind) {
		case deed::ally: done = nations::are_allied(state, player, e.target); break;
		case deed::ask_access: done = gives_access(state, e.target, player); break;
		case deed::free_trade: done = bool(economy::nation_gives_free_trade_rights(state, e.target, player)); break;
		case deed::open_deal: {
			done = true;
			if(e.asks & sc::deal_alliance)
				done = done && nations::are_allied(state, player, e.target);
			if(e.asks & sc::deal_access_for_us)
				done = done && gives_access(state, e.target, player);
			if(e.asks & sc::deal_free_trade)
				done = done && bool(economy::nation_gives_free_trade_rights(state, e.target, player));
			if(e.asks & sc::deal_lift_their_embargo)
				done = done && !embargoes(state, e.target, player);
			if(e.gives & sc::deal_access_for_them)
				done = done && gives_access(state, player, e.target);
			if(e.gives & sc::deal_subsidies)
				done = done && subsidises(state, player, e.target);
			break;
		}
		default: done = true; break;
		}
		if(done)
			e.outcome = 1;
		else if(state.current_date > e.sent || now - e.sent_at > std::chrono::milliseconds(1500))
			e.outcome = -1;
		if(e.outcome != 0) {
			auto const who = nation_name(state, e.target);
			toasts.push_back(toast{ e.target, sentence(localized(state, e.outcome > 0 ? "katechon_toast_yes" : "katechon_toast_no", e.outcome > 0 ? "%1 agreed: %2" : "%1 refused: %2"), who, e.what), e.outcome > 0, now });
			for(auto& k : journal_kept) {
				if(k.target == e.target && k.sent == e.sent && k.kind == e.kind && k.outcome == 0)
					k.outcome = e.outcome;
			}
		}
		if(e.outcome != 0 && qa_deal_send)
			std::fprintf(stderr, "KATECHON-QA diplomacy: %s to %s %s (allied %d, relation %.0f)\n", e.what.c_str(), nation_name(state, e.target).c_str(),
				e.outcome > 0 ? "taken" : "refused", int(nations::are_allied(state, player, e.target)), relation_between(state, player, e.target));
	}
	while(journal.size() > 6)
		journal.erase(journal.begin());
	while(journal_kept.size() > 200)
		journal_kept.erase(journal_kept.begin());
}

void note_sent(sys::state& state, dcon::nation_id target, deed kind, std::string what, uint16_t gives = 0, uint16_t asks = 0) {
	journal_entry e{ target, std::move(what), gives, asks, kind, 0, state.current_date, std::chrono::steady_clock::now() };
	journal.push_back(e);
	journal_kept.push_back(e);
}

// What would allow an action that cannot be taken now. Empty when it can.
std::string requirement_for(sys::state& state, deed what, dcon::nation_id player, dcon::nation_id target, float cost, dcon::cb_type_id cb = dcon::cb_type_id{}) {
	auto const points = nations::diplomatic_points(state, player);
	auto const at_war = military::are_at_war(state, player, target);
	auto const their_overlord = overlord_of(state, target);
	auto const our_overlord = overlord_of(state, player);
	auto const need_diplomats = [&]() {
		return sentence(localized(state, "katechon_dip_q_diplomats", "Diplomats needed: %1 (we have %2)"), whole(cost), whole(points));
	};
	switch(what) {
	case deed::improve:
		if(at_war)
			return localized(state, "katechon_dip_q_war", "Not while we are at war with them");
		if(relation_between(state, player, target) >= 200.f)
			return localized(state, "katechon_dip_q_max", "Relations are at their height");
		break;
	case deed::worsen:
		if(at_war)
			return localized(state, "katechon_dip_q_war", "Not while we are at war with them");
		if(relation_between(state, player, target) <= -200.f)
			return localized(state, "katechon_dip_q_min", "Relations cannot fall further");
		break;
	case deed::ally:
		if(at_war)
			return localized(state, "katechon_dip_q_war", "Not while we are at war with them");
		if(state.world.nation_get_is_great_power(player) && state.world.nation_get_is_great_power(target) && state.current_crisis_state != sys::crisis_state::inactive)
			return localized(state, "katechon_dip_q_crisis", "Great powers cannot ally while a crisis lasts");
		if(state.defines.alice_allow_subjects_declare_wars == 0 && our_overlord)
			return sentence(localized(state, "katechon_dip_q_our_overlord", "We answer to %1"), nation_name(state, our_overlord));
		if(state.defines.alice_allow_subjects_declare_wars == 0 && their_overlord)
			return sentence(localized(state, "katechon_dip_q_their_overlord", "They answer to %1"), nation_name(state, their_overlord));
		break;
	case deed::ask_access:
	case deed::give_access:
		if(at_war)
			return localized(state, "katechon_dip_q_war", "Not while we are at war with them");
		break;
	case deed::free_trade:
		if(at_war)
			return localized(state, "katechon_dip_q_war", "Not while we are at war with them");
		if(our_overlord)
			return sentence(localized(state, "katechon_dip_q_our_overlord", "We answer to %1"), nation_name(state, our_overlord));
		if(their_overlord && their_overlord != player)
			return sentence(localized(state, "katechon_dip_q_their_overlord", "They answer to %1"), nation_name(state, their_overlord));
		if(auto const sphere = state.world.nation_get_in_sphere_of(player); sphere)
			return sentence(localized(state, "katechon_dip_q_our_sphere", "We move in the sphere of %1"), nation_name(state, sphere));
		if(auto const sphere = state.world.nation_get_in_sphere_of(target); sphere && sphere != player)
			return sentence(localized(state, "katechon_dip_q_their_sphere", "They move in the sphere of %1"), nation_name(state, sphere));
		if(embargoes(state, player, target) || embargoes(state, target, player))
			return localized(state, "katechon_dip_q_embargo", "An embargo stands between us: lift it first");
		break;
	case deed::embargo: {
		if(at_war)
			return localized(state, "katechon_dip_q_war", "Not while we are at war with them");
		if(our_overlord || their_overlord)
			return localized(state, "katechon_dip_q_subjects", "Subjects can neither impose nor suffer an embargo");
		if(state.world.nation_get_in_sphere_of(target) == player)
			return localized(state, "katechon_dip_q_own_sphere", "They move in our own sphere");
		if(auto const sphere = state.world.nation_get_in_sphere_of(player); sphere)
			return sentence(localized(state, "katechon_dip_q_our_sphere", "We move in the sphere of %1"), nation_name(state, sphere));
		sys::date until{};
		for(auto const& pair : { std::pair{ target, player }, std::pair{ player, target } }) {
			auto const rel = state.world.get_unilateral_relationship_by_unilateral_pair(pair.first, pair.second);
			if(rel && state.current_date < state.world.unilateral_relationship_get_no_tariffs_until(rel))
				until = std::max(until, state.world.unilateral_relationship_get_no_tariffs_until(rel));
		}
		if(until)
			return sentence(localized(state, "katechon_dip_q_free_trade", "Free trade binds us until %1"), text::date_to_string(state, until));
		break;
	}
	case deed::subsidise:
		if(at_war)
			return localized(state, "katechon_dip_q_war", "Not while we are at war with them");
		break;
	case deed::justify: {
		if(at_war)
			return localized(state, "katechon_dip_q_war", "Not while we are at war with them");
		if(auto const busy = state.world.nation_get_constructing_cb_target(player); busy)
			return sentence(localized(state, "katechon_dip_q_justifying", "We are already preparing grounds against %1"), nation_name(state, busy));
		if(military::has_truce_with(state, player, target))
			return sentence(localized(state, "katechon_dip_q_truce", "A truce holds until %1"), text::date_to_string(state, military::truce_end_date(state, player, target)));
		if(state.world.nation_get_in_sphere_of(target) == player)
			return localized(state, "katechon_dip_q_own_sphere", "They move in our own sphere");
		if(!cb)
			return localized(state, "katechon_dip_q_no_grounds", "Nothing in our laws or claims gives grounds against them");
		break;
	}
	case deed::declare:
		if(military::has_truce_with(state, player, target))
			return sentence(localized(state, "katechon_dip_q_truce", "A truce holds until %1"), text::date_to_string(state, military::truce_end_date(state, player, target)));
		if(!cb)
			return localized(state, "katechon_dip_q_prepare", "We have no grounds: prepare them first (Coercion)");
		break;
	default:
		break;
	}
	if(cost > 0.f && points < cost)
		return need_diplomats();
	return localized(state, "katechon_dip_q_now", "Not possible now");
}

// The grounds we hold, or could prepare, for a war on them.
void gather_grounds(sys::state& state, std::vector<deed_entry>& out, dcon::nation_id player, dcon::nation_id target, bool preparable) {
	auto const add_ground = [&](dcon::cb_type_id cb, deed what) {
		if(out.size() >= 3 || std::any_of(out.begin(), out.end(), [&](deed_entry const& d) { return d.cb == cb; }))
			return;
		if(military::cb_requires_selection_of_a_liberatable_tag(state, cb) || military::cb_requires_selection_of_a_valid_nation(state, cb))
			return;
		auto const name = text::produce_simple_string(state, state.world.cb_type_get_name(cb));
		auto const usable = [&](dcon::state_definition_id where) {
			return what == deed::declare ? command::can_declare_war<false>(state, player, target, cb, where, dcon::national_identity_id{}, dcon::nation_id{})
				: command::can_fabricate_cb(state, player, target, cb, where);
		};
		auto const label = [&](dcon::state_definition_id where) {
			if(what == deed::declare)
				return where ? sentence(localized(state, "katechon_dip_declare_for", "Declare war for %1"), text::get_name_as_string(state, where)) : sentence(localized(state, "katechon_dip_declare_on", "Declare war: %1"), name);
			return where ? sentence(localized(state, "katechon_dip_justify_for", "Prepare grounds for %1"), text::get_name_as_string(state, where)) : sentence(localized(state, "katechon_dip_justify_on", "Prepare grounds: %1"), name);
		};
		if(!military::cb_requires_selection_of_a_state(state, cb)) {
			if(usable(dcon::state_definition_id{}))
				out.push_back(deed_entry{ what, dip_kind::coercion, label(dcon::state_definition_id{}), name, std::string(), true, false, likelihood::unknown, std::string(), true, cb });
			return;
		}
		for(auto ownership : state.world.nation_get_state_ownership(target)) {
			auto const definition = ownership.get_state().get_definition().id;
			if(!usable(definition))
				continue;
			out.push_back(deed_entry{ what, dip_kind::coercion, label(definition), name, std::string(), true, false, likelihood::unknown, std::string(), true, cb, definition });
			return;
		}
	};
	if(preparable) {
		for(auto cb : state.world.in_cb_type) {
			auto const bits = cb.get_type_bits();
			if((bits & (military::cb_flag::always | military::cb_flag::is_not_constructing_cb)) == 0)
				add_ground(cb, deed::justify);
		}
		return;
	}
	for(auto const& held : state.world.nation_get_available_cbs(player)) {
		if(held.target == target)
			add_ground(held.cb_type, deed::declare);
	}
	for(auto cb : state.world.in_cb_type) {
		if((cb.get_type_bits() & military::cb_flag::always) != 0 && military::cb_conditions_satisfied(state, player, target, cb))
			add_ground(cb, deed::declare);
	}
}

// What can be done with them, in the four kinds, each with what it costs, what
// it does, our reading of their answer, and how much it matters now.
void gather_deeds(sys::state& state, diplomacy_view& v, dcon::nation_id player, dcon::nation_id target) {
	namespace sc = nations::strategic_statecraft;
	auto const& d = state.defines;
	auto const points = nations::diplomatic_points(state, player);
	auto const at_war = military::are_at_war(state, player, target);
	auto const allied = nations::are_allied(state, player, target);
	auto const relation = v.relation;
	auto& deeds = v.deeds;
	auto const add = [&](deed what, dip_kind kind, std::string_view key, std::string_view fallback, bool possible, float cost, float relevance) -> deed_entry& {
		deed_entry e{ what, kind, localized(state, key, fallback) };
		e.possible = possible && (cost <= 0.f || points >= cost);
		e.cost = cost;
		e.relevance = relevance;
		if(!e.possible)
			e.requirement = requirement_for(state, what, player, target, cost);
		deeds.push_back(std::move(e));
		return deeds.back();
	};
	auto const answer = [&](deed_entry& e, sc::assessment const& a, uint16_t ask_bit) {
		e.proposal = true;
		e.ask_bit = ask_bit;
		e.verdict = a.verdict;
		e.reason = reasons_line(state, a);
		switch(a.verdict) {
		case likelihood::very_likely: e.relevance += 40.f; break;
		case likelihood::likely: e.relevance += 28.f; break;
		case likelihood::uncertain: e.relevance += 8.f; break;
		case likelihood::unlikely: e.relevance -= 10.f; break;
		case likelihood::very_unlikely: e.relevance -= 24.f; break;
		case likelihood::out_of_question: e.relevance -= 40.f; break;
		case likelihood::unknown: break;
		}
	};

	// relations
	if(at_war) {
		if(v.war) {
			auto& peace = add(deed::white_peace, dip_kind::relations, "katechon_dip_white_peace", "Offer peace as things stand", command::can_start_peace_offer(state, player, target, v.war, false), 0.f, 100.f);
			peace.effect = localized(state, "katechon_dip_e_white_peace", "Both sides keep what they hold; a truce follows");
		}
	} else {
		auto& mission = add(deed::improve, dip_kind::relations, "katechon_dip_mission", "Diplomatic mission", command::can_increase_relations(state, player, target) || (points < d.increaserelation_diplomatic_cost && relation < 200.f),
			d.increaserelation_diplomatic_cost, 22.f);
		mission.effect = sentence(localized(state, "katechon_dip_e_mission", "Diplomats: %1  ·  +%2 relations  ·  our envoys report on their government"), whole(d.increaserelation_diplomatic_cost),
			whole(d.increaserelation_relation_on_accept));
		// worth most where we stand badly with them, or know them least
		if(relation < 50.f)
			mission.relevance += 22.f;
		if(v.knowledge.quality < 0.35f)
			mission.relevance += 18.f;
		auto& denounce = add(deed::worsen, dip_kind::relations, "katechon_dip_denounce", "Denounce their government", command::can_decrease_relations(state, player, target) || (points < d.decreaserelation_diplomatic_cost && relation > -200.f),
			d.decreaserelation_diplomatic_cost, 1.f);
		denounce.effect = sentence(localized(state, "katechon_dip_e_denounce", "Diplomats: %1  ·  %2 relations"), whole(d.decreaserelation_diplomatic_cost), "-" + whole(std::abs(d.decreaserelation_relation_on_accept)));
	}

	// agreements
	if(!at_war) {
		if(allied) {
			auto& end = add(deed::break_alliance, dip_kind::agreements, "katechon_dip_break_alliance", "End the alliance", command::can_cancel_alliance(state, player, target, true), d.cancelalliance_diplomatic_cost, 2.f);
			end.effect = localized(state, "katechon_dip_e_break", "Remembered on both sides as a broken promise");
		} else {
			auto& ally = add(deed::ally, dip_kind::agreements, "katechon_dip_ally", "Propose an alliance", command::can_ask_for_alliance(state, player, target, true), d.alliance_diplomatic_cost, 46.f);
			ally.effect = localized(state, "katechon_dip_e_ally", "Each answers the other's call when attacked");
			answer(ally, sc::assess_alliance(state, player, target), sc::deal_alliance);
		}
		if(gives_access(state, target, player)) {
			auto& end = add(deed::end_access, dip_kind::agreements, "katechon_dip_end_access", "Give up our passage", command::can_cancel_military_access(state, player, target, true), 0.f, 1.f);
			end.effect = localized(state, "katechon_dip_e_end_access", "Our forces leave their land");
		} else {
			auto& access = add(deed::ask_access, dip_kind::agreements, "katechon_dip_ask_access", "Ask passage for our forces", command::can_ask_for_access(state, player, target, true), d.askmilaccess_diplomatic_cost,
				state.world.nation_get_is_at_war(player) ? 34.f : 12.f);
			access.effect = localized(state, "katechon_dip_e_access", "Our forces may cross their land");
			answer(access, sc::assess_military_access(state, player, target), sc::deal_access_for_us);
		}
		if(!economy::nation_gives_free_trade_rights(state, target, player)) {
			auto& trade = add(deed::free_trade, dip_kind::agreements, "katechon_dip_propose_trade", "Propose free trade", command::can_ask_for_free_trade_agreement(state, player, target, true), d.askmilaccess_diplomatic_cost, 30.f);
			trade.effect = sentence(localized(state, "katechon_dip_e_trade", "No duties either way for %1 years"), whole(d.alice_free_trade_agreement_years));
			answer(trade, sc::assess_free_trade(state, player, target), sc::deal_free_trade);
		}
		if(embargoes(state, target, player)) {
			auto& lift = add(deed::lift_their_embargo, dip_kind::agreements, "katechon_dip_ask_lift", "Ask them to lift their embargo", command::can_propose_deal(state, player, target, 0, sc::deal_lift_their_embargo),
				command::deal_cost(state, 0, sc::deal_lift_their_embargo), 50.f);
			lift.effect = localized(state, "katechon_dip_e_ask_lift", "Their market opens to our goods again");
			answer(lift, sc::assess_deal(state, player, target, 0, sc::deal_lift_their_embargo), sc::deal_lift_their_embargo);
		}
	}

	// influence
	if(!at_war) {
		if(subsidises(state, player, target)) {
			auto& end = add(deed::end_subsidies, dip_kind::influence, "katechon_dip_end_subsidies", "Stop the war subsidies", command::can_cancel_war_subsidies(state, player, target), 0.f, 2.f);
			end.effect = localized(state, "katechon_dip_e_end_subsidies", "Our payments to their war end");
		} else {
			auto& pay = add(deed::subsidise, dip_kind::influence, "katechon_dip_subsidise", "Subsidise their war", command::can_give_war_subsidies(state, player, target) || points < d.warsubsidy_diplomatic_cost,
				d.warsubsidy_diplomatic_cost, state.world.nation_get_is_at_war(target) && relation >= 25.f ? 26.f : 4.f);
			pay.effect = localized(state, "katechon_dip_e_subsidise", "We pay a share of their war; our leverage grows");
		}
		if(gives_access(state, player, target)) {
			auto& revoke = add(deed::revoke_access, dip_kind::influence, "katechon_dip_revoke_access", "Withdraw their passage", command::can_cancel_given_military_access(state, player, target, true), 0.f, 1.f);
			revoke.effect = localized(state, "katechon_dip_e_revoke", "Their forces must leave our land");
		} else {
			auto& grant = add(deed::give_access, dip_kind::influence, "katechon_dip_give_access", "Grant passage to their forces", command::can_give_military_access(state, player, target, true), d.givemilaccess_diplomatic_cost,
				state.world.nation_get_is_at_war(target) ? 14.f : 3.f);
			grant.effect = localized(state, "katechon_dip_e_give_access", "A favour they will weigh in what they owe us");
		}
		if(embargoes(state, player, target)) {
			auto& lift = add(deed::lift_embargo, dip_kind::influence, "katechon_dip_lift_embargo", "Lift our embargo", command::can_switch_embargo_status(state, player, target, true), d.askmilaccess_diplomatic_cost, 12.f);
			lift.effect = localized(state, "katechon_dip_e_lift", "Our market opens to them again");
		}
	}

	// coercion
	if(at_war) {
		if(v.war) {
			bool ours = false;
			bool theirs = false;
			for(auto wg : state.world.war_get_wargoals_attached(v.war)) {
				auto const goal = wg.get_wargoal();
				ours = ours || (goal.get_added_by() == player && goal.get_target_nation() == target);
				theirs = theirs || (goal.get_added_by() == target && goal.get_target_nation() == player);
			}
			if(ours) {
				auto& press = add(deed::press_goals, dip_kind::coercion, "katechon_dip_press_goals", "Demand what we went to war for", command::can_start_peace_offer(state, player, target, v.war, false), 0.f, 95.f);
				press.effect = localized(state, "katechon_dip_e_press", "Peace on our war goals");
				press.warlike = true;
			}
			if(theirs) {
				auto& concede = add(deed::concede, dip_kind::relations, "katechon_dip_concede", "Give them what they want", command::can_start_peace_offer(state, player, target, v.war, true), 0.f, 60.f);
				concede.effect = localized(state, "katechon_dip_e_concede", "Peace on their war goals");
			}
		}
	} else {
		if(!embargoes(state, player, target)) {
			auto& embargo = add(deed::embargo, dip_kind::coercion, "katechon_dip_embargo", "Impose an embargo", command::can_switch_embargo_status(state, player, target, true), d.askmilaccess_diplomatic_cost,
				relation <= -100.f ? 20.f : 3.f);
			embargo.effect = localized(state, "katechon_dip_e_embargo", "Trade between us stops");
			embargo.warlike = true;
		}
		if(state.world.nation_get_constructing_cb_target(player) == target) {
			auto& stop = add(deed::cancel_justification, dip_kind::coercion, "katechon_dip_cancel_justify", "Stop preparing grounds", command::can_cancel_cb_fabrication(state, player), 0.f, 4.f);
			stop.effect = localized(state, "katechon_dip_e_cancel_justify", "What was prepared is lost");
			stop.warlike = true;
		} else {
			std::vector<deed_entry> preparable;
			gather_grounds(state, preparable, player, target, true);
			if(!preparable.empty()) {
				auto e = preparable.front();
				e.cost = d.make_cb_diplomatic_cost;
				e.possible = points >= e.cost;
				e.requirement = e.possible ? std::string() : requirement_for(state, deed::justify, player, target, e.cost, e.cb);
				e.effect = sentence(localized(state, "katechon_dip_e_justify", "Diplomats: %1  ·  infamy +%2 when ready  ·  may be discovered"), whole(e.cost),
					text::format_float(military::cb_infamy(state, e.cb, target, e.where), 1));
				e.relevance = relation <= -100.f ? 12.f : 2.f;
				deeds.push_back(std::move(e));
			} else {
				auto& justify = add(deed::justify, dip_kind::coercion, "katechon_dip_justify", "Prepare grounds for war", false, d.make_cb_diplomatic_cost, 1.f);
				justify.requirement = requirement_for(state, deed::justify, player, target, 0.f);
				justify.warlike = true;
			}
		}
		std::vector<deed_entry> grounds;
		gather_grounds(state, grounds, player, target, false);
		if(!grounds.empty()) {
			for(auto& g : grounds) {
				g.effect = sentence(localized(state, "katechon_dip_e_declare", "On the grounds of %1"), g.effect);
				g.relevance = relation <= -100.f ? 30.f : 5.f;
				deeds.push_back(std::move(g));
			}
		} else {
			auto& declare = add(deed::declare, dip_kind::coercion, "katechon_dip_declare", "Declare war", false, 0.f, 1.f);
			declare.requirement = requirement_for(state, deed::declare, player, target, 0.f);
			declare.warlike = true;
		}
	}

	// For what they would likely refuse, the least we could give in return to have it, as we read them.
	{
		struct gift {
			uint16_t bit;
			std::string_view key;
			std::string_view fallback;
		};
		static constexpr std::array<gift, 3> gifts{ gift{ sc::deal_access_for_them, "katechon_swe_access", "passage for their forces" },
			gift{ sc::deal_subsidies, "katechon_swe_subsidies", "war subsidies" }, gift{ sc::deal_lift_our_embargo, "katechon_swe_embargo", "an end to our embargo" } };
		uint16_t allowed = 0;
		for(auto const& g : gifts)
			if(command::can_deal_item(state, player, target, g.bit))
				allowed |= g.bit;
		for(auto& e : deeds) {
			if(!e.ask_bit || !e.proposal || favourable(e.verdict) || e.verdict == likelihood::out_of_question || !allowed)
				continue;
			uint16_t best = 0;
			for(int32_t size = 1; size <= 3 && !best; ++size) {
				for(uint16_t mask = 1; mask < 128 && !best; ++mask) {
					if((mask & ~allowed) || std::popcount(mask) != size)
						continue;
					if(favourable(sc::assess_deal(state, player, target, mask, e.ask_bit).verdict) && command::can_propose_deal(state, player, target, mask, e.ask_bit))
						best = mask;
				}
			}
			if(!best)
				continue;
			e.in_return = best;
			for(auto const& g : gifts)
				if(best & g.bit)
					e.in_return_text += (e.in_return_text.empty() ? "" : ", ") + localized(state, g.key, g.fallback);
		}
	}

	// What is put forward: the four that matter most. An action that cannot be taken now
	// is put forward only when it would matter a great deal.
	std::vector<size_t> order(deeds.size());
	for(size_t i = 0; i < order.size(); ++i)
		order[i] = i;
	auto const weight = [&](size_t i) { return deeds[i].possible ? deeds[i].relevance : deeds[i].relevance - 30.f; };
	std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return weight(a) > weight(b); });
	for(auto i : order) {
		if(v.top.size() == 4)
			break;
		if(weight(i) > 0.f || v.top.size() < 2)
			v.top.push_back(i);
	}
}

void gather_interests(sys::state& state, diplomacy_view& v, dcon::nation_id player, dcon::nation_id target, leverage const& lev) {
	namespace sc = nations::strategic_statecraft;
	auto const them = v.title;
	auto const q = v.knowledge.quality;
	auto const both_ways = [&](dcon::nation_id a, dcon::nation_id b) { return nations::are_allied(state, a, b); };
	// common ground
	if(nations::are_allied(state, player, target))
		v.common.push_back(interest_chip{ localized(state, "katechon_dip_c_allies", "Allies"), 1, localized(state, "katechon_dip_c_allies_why", "A war on one is a war on both") });
	for(auto participant : state.world.nation_get_war_participant(player)) {
		auto const war = participant.get_war();
		auto const role = military::get_role(state, war, target);
		if(role == military::war_role::none)
			continue;
		if((role == military::war_role::attacker) == participant.get_is_attacker()) {
			auto const enemy = participant.get_is_attacker() ? war.get_primary_defender() : war.get_primary_attacker();
			v.common.push_back(interest_chip{ sentence(localized(state, "katechon_dip_c_enemy", "Common enemy: %1"), nation_name(state, enemy)), 1, military::get_war_name(state, war) });
			break;
		}
	}
	for(auto rel : state.world.nation_get_diplomatic_relation(player)) {
		if(!rel.get_are_allied())
			continue;
		auto const other = rel.get_related_nations(0) == player ? rel.get_related_nations(1) : rel.get_related_nations(0);
		if(other && other != target && both_ways(target, other)) {
			v.common.push_back(interest_chip{ sentence(localized(state, "katechon_dip_c_ally", "Common ally: %1"), nation_name(state, other)), 1,
				localized(state, "katechon_dip_c_ally_why", "Friends of friends weigh it in our favour") });
			break;
		}
	}
	if(auto const rival = state.world.nation_get_ai_rival(player); rival && rival != target && state.world.nation_get_ai_rival(target) == rival && q >= 0.35f)
		v.common.push_back(interest_chip{ sentence(localized(state, "katechon_dip_c_rival", "Shared rival: %1"), nation_name(state, rival)), 1, localized(state, "katechon_dip_c_rival_why", "As our envoys and agents read them") });
	if(lev.trade_share >= 0.05f)
		v.common.push_back(interest_chip{ sentence(localized(state, "katechon_dip_c_trade", "Trade: %1 of theirs"), text::format_percentage(lev.trade_share, 0)), 1,
			localized(state, "katechon_dip_c_trade_why", "Of their foreign trade over the last month, the part with us") });
	if(lev.free_trade)
		v.common.push_back(interest_chip{ localized(state, "katechon_dip_c_free_trade", "Free trade"), 1, localized(state, "katechon_dip_c_free_trade_why", "No duties between us") });
	// frictions
	if(holds_claim(state, player, target))
		v.frictions.push_back(interest_chip{ localized(state, "katechon_dip_x_their_claim", "They claim our land"), -1, localized(state, "katechon_dip_x_claim_why", "Land one side holds is the other's core") });
	if(holds_claim(state, target, player))
		v.frictions.push_back(interest_chip{ localized(state, "katechon_dip_x_our_claim", "We claim their land"), -1, localized(state, "katechon_dip_x_claim_why", "Land one side holds is the other's core") });
	for(auto rel : state.world.nation_get_diplomatic_relation(player)) {
		if(!rel.get_are_allied())
			continue;
		auto const other = rel.get_related_nations(0) == player ? rel.get_related_nations(1) : rel.get_related_nations(0);
		if(other && other != target && military::are_at_war(state, target, other)) {
			v.frictions.push_back(interest_chip{ sentence(localized(state, "katechon_dip_x_war_ally", "At war with our ally %1"), nation_name(state, other)), -1, std::string() });
			break;
		}
	}
	for(auto participant : state.world.nation_get_war_participant(player)) {
		auto const war = participant.get_war();
		bool found = false;
		for(auto other : state.world.war_get_war_participant(war)) {
			if(other.get_is_attacker() != participant.get_is_attacker() && nations::are_allied(state, target, other.get_nation())) {
				v.frictions.push_back(interest_chip{ sentence(localized(state, "katechon_dip_x_ally_enemy", "Allied to our enemy %1"), nation_name(state, other.get_nation())), -1, military::get_war_name(state, war) });
				found = true;
				break;
			}
		}
		if(found)
			break;
	}
	if(embargoes(state, target, player))
		v.frictions.push_back(interest_chip{ localized(state, "katechon_dip_x_their_embargo", "Their embargo on us"), -1, std::string() });
	if(embargoes(state, player, target))
		v.frictions.push_back(interest_chip{ localized(state, "katechon_dip_x_our_embargo", "Our embargo on them"), -1, std::string() });
	if(state.world.nation_get_constructing_cb_target(player) == target)
		v.frictions.push_back(interest_chip{ localized(state, "katechon_dip_x_justifying", "We prepare grounds against them"), -1, localized(state, "katechon_dip_x_justifying_why", "Should they learn of it, relations suffer") });
	if(auto const rival = state.world.nation_get_ai_rival(target); rival == player && q >= 0.3f)
		v.frictions.push_back(interest_chip{ localized(state, "katechon_dip_x_rival", "They count us their rival"), -1, localized(state, "katechon_dip_c_rival_why", "As our envoys and agents read them") });
	// their aims, as far as we can read them
	if(size_t(target.index()) < state.strategic_interests.size() && state.strategic_interests[size_t(target.index())].enabled) {
		if(q < 0.3f) {
			v.priorities.push_back(interest_chip{ localized(state, "katechon_dip_p_unclear", "Their aims are unclear"), 0,
				localized(state, "katechon_dip_p_unclear_why", "A diplomatic mission or intelligence would tell us more") });
		} else {
			auto const& wants = state.strategic_interests[size_t(target.index())];
			struct weight {
				float value;
				std::string_view key;
				std::string_view fallback;
			};
			std::array<weight, 7> weights{ weight{ wants.security, "katechon_dip_care_security", "Security" }, weight{ wants.territorial_claims, "katechon_dip_care_claims", "Territorial claims" },
				weight{ wants.market_access, "katechon_dip_care_markets", "Markets" }, weight{ wants.resource_access, "katechon_dip_care_resources", "Resources" },
				weight{ wants.route_access, "katechon_dip_care_routes", "Trade routes" }, weight{ wants.ally_subject_protection, "katechon_dip_care_partners", "Protecting partners" },
				weight{ wants.prestige_influence, "katechon_dip_care_prestige", "Regional influence" } };
			std::stable_sort(weights.begin(), weights.end(), [](weight const& a, weight const& b) { return a.value > b.value; });
			auto const shown = q >= 0.55f ? size_t(3) : size_t(2);
			for(size_t i = 0; i < shown; ++i)
				v.priorities.push_back(interest_chip{ localized(state, weights[i].key, weights[i].fallback), 0, localized(state, "katechon_dip_p_why", "What they care for most, as we read them") });
			if(wants.objective != uint8_t(sc::objective_kind::none) && q >= 0.55f) {
				auto aim = objective_word(state, sc::objective_kind(wants.objective));
				if(!aim.empty() && wants.objective_target != sc::interests::no_nation) {
					auto const toward = dcon::nation_id{ dcon::nation_id::value_base_t(wants.objective_target) };
					if(state.world.nation_is_valid(toward))
						aim += " " + sentence(localized(state, "katechon_dip_with", "with %1"), toward == player ? localized(state, "katechon_dip_us", "us") : nation_name(state, toward));
				}
				if(!aim.empty())
					v.priorities.back().why = sentence(localized(state, "katechon_dip_p_now", "Now seeking %1"), aim);
			}
		}
	}
	(void)them;
}

// What has passed between us: the promises made and ended, the wars, the truce, our own proposals.
void gather_history(sys::state& state, diplomacy_view& v, dcon::nation_id player, dcon::nation_id target) {
	namespace sc = nations::strategic_statecraft;
	auto const pi = uint32_t(player.index());
	auto const ti = uint32_t(target.index());
	for(auto const& c : state.strategic_commitments) {
		auto const ours = c.promisor == pi && c.beneficiary == ti;
		auto const theirs = c.promisor == ti && c.beneficiary == pi;
		if(!ours && !theirs)
			continue;
		switch(sc::commitment_kind(c.kind)) {
		case sc::commitment_kind::alliance:
			if(!ours)
				break; // an alliance makes a promise each way; one line for both
			v.history.push_back(history_line{ c.signed_day, localized(state, "katechon_dip_h_alliance", "Alliance concluded"), 1 });
			if(!c.active && c.resolved_day > 0)
				v.history.push_back(history_line{ c.resolved_day, localized(state, "katechon_dip_h_alliance_end", "Alliance ended: a broken promise"), -1 });
			break;
		case sc::commitment_kind::guarantee:
			v.history.push_back(history_line{ c.signed_day, ours ? localized(state, "katechon_dip_h_we_guarantee", "We took them under our protection") : localized(state, "katechon_dip_h_they_guarantee", "They took us under their protection"), 1 });
			if(!c.active && c.resolved_day > 0)
				v.history.push_back(history_line{ c.resolved_day, localized(state, "katechon_dip_h_guarantee_end", "The protection ended"), -1 });
			break;
		case sc::commitment_kind::crisis_support:
			v.history.push_back(history_line{ c.signed_day, ours ? localized(state, "katechon_dip_h_we_backed", "We backed them in a crisis") : localized(state, "katechon_dip_h_they_backed", "They backed us in a crisis"), 1 });
			break;
		}
	}
	auto const record = sc::trust_of(state, target, player);
	if(record.last_war_day > 0)
		v.history.push_back(history_line{ record.last_war_day, localized(state, "katechon_dip_h_war", "War between us"), -1 });
	if(record.last_commitment_day > 0 && (record.kept || record.broken))
		v.history.push_back(history_line{ record.last_commitment_day, sentence(localized(state, "katechon_dip_h_record", "Our word, as they keep count: kept %1, broken %2"), std::to_string(record.kept), std::to_string(record.broken)),
			record.broken > record.kept ? int8_t(-1) : int8_t(1) });
	if(!military::are_at_war(state, player, target) && military::has_truce_with(state, player, target))
		v.history.push_back(history_line{ int32_t(state.current_date.value), sentence(localized(state, "katechon_dip_s_truce", "A truce binds us until %1."), text::date_to_string(state, military::truce_end_date(state, player, target))), 0 });
	for(auto const& e : journal_kept) {
		if(e.target != target)
			continue;
		auto const word = e.outcome > 0 ? localized(state, "katechon_dip_j_yes", "agreed") : (e.outcome < 0 ? localized(state, "katechon_dip_j_no", "refused") : localized(state, "katechon_dip_j_wait", "awaiting"));
		v.history.push_back(history_line{ int32_t(e.sent.value), sentence(localized(state, "katechon_dip_h_proposal", "We proposed: %1 — %2"), e.what, word), e.outcome });
	}
	std::stable_sort(v.history.begin(), v.history.end(), [](history_line const& a, history_line const& b) { return a.day > b.day; });
}

void gather_foreign(sys::state& state, diplomacy_view& v, dcon::nation_id player, dcon::nation_id target) {
	namespace sc = nations::strategic_statecraft;
	auto const them = v.title;
	auto const today = int32_t(state.current_date.value);
	v.relation = relation_between(state, player, target);
	v.temper = temper_of(v.relation);
	v.temper_word = temper_word(state, v.temper);
	v.knowledge = sc::read_intentions(state, player, target);
	v.war = military::find_war_between(state, player, target);

	// how well we read them, and from what
	{
		auto const q = v.knowledge.quality;
		v.knowledge_word = q >= 0.6f ? localized(state, "katechon_dip_k_good", "Good") : (q >= 0.35f ? localized(state, "katechon_dip_k_fair", "Fair") : (q >= 0.15f ? localized(state, "katechon_dip_k_poor", "Poor") : localized(state, "katechon_dip_k_none", "Next to nothing")));
		auto& k = v.knowledge_detail;
		k.push_back(localized(state, "katechon_dip_k_title", "How sure our readings of their intentions are"));
		if(nations::are_allied(state, player, target))
			k.push_back(localized(state, "katechon_dip_k_allied", "Allies: we know their councils"));
		else if(state.world.get_nation_adjacency_by_nation_adjacency_pair(player, target))
			k.push_back(localized(state, "katechon_dip_k_border", "A common border"));
		else if(v.knowledge.contact > 0.f)
			k.push_back(localized(state, "katechon_dip_k_contact", "Standing contact: war, rivalry or a great power's attention"));
		else
			k.push_back(localized(state, "katechon_dip_k_no_contact", "No standing contact"));
		k.push_back(v.knowledge.intelligence_age >= 0 ? sentence(localized(state, "katechon_dip_k_intel", "Intelligence report: %1 days old"), std::to_string(v.knowledge.intelligence_age))
			: localized(state, "katechon_dip_k_no_intel", "No intelligence report on them"));
		k.push_back(v.knowledge.embassy_age >= 0 ? sentence(localized(state, "katechon_dip_k_envoys", "Our envoys reported %1 days ago"), std::to_string(v.knowledge.embassy_age))
			: localized(state, "katechon_dip_k_no_envoys", "No report from our envoys: a diplomatic mission would bring one"));
		if(v.knowledge.openness >= 0.05f)
			k.push_back(localized(state, "katechon_dip_k_open", "Good relations: their government talks to us freely"));
	}

	// relations, trust, leverage
	{
		auto& r = v.relations;
		r.value = (v.relation > 0.f ? "+" : "") + std::to_string(int32_t(v.relation));
		r.colour = temper_colours[size_t(v.temper)];
		set_trend(state, r, size_t(target.index()) < relation_series.size() ? &relation_series[size_t(target.index())] : nullptr, 5.f, "katechon_dip_rising", "Improving", "katechon_dip_falling", "Worsening");
		r.detail.push_back(filled(localized(state, "katechon_dip_exact", "Relations %1 on a scale from -200 to +200"), r.value));
		r.detail.push_back(localized(state, "katechon_dip_relations_why", "Missions raise them; insults, claims and wars lower them"));
	}
	{
		auto const record = sc::trust_of(state, target, player);
		auto& t = v.trust;
		auto const r = record.reliability;
		t.value = r >= 0.62f ? localized(state, "katechon_dip_tr_high", "High") : (r >= 0.45f ? localized(state, "katechon_dip_tr_moderate", "Moderate") : (r >= 0.3f ? localized(state, "katechon_dip_tr_low", "Low") : localized(state, "katechon_dip_tr_broken", "Broken")));
		t.colour = r >= 0.62f ? temper_colours[4] : (r >= 0.45f ? temper_colours[2] : temper_colours[r >= 0.3f ? 1 : 0]);
		set_trend(state, t, size_t(target.index()) < trust_series.size() ? &trust_series[size_t(target.index())] : nullptr, 0.02f, "katechon_dip_tr_rising", "Rising", "katechon_dip_tr_falling", "Falling");
		t.detail.push_back(localized(state, "katechon_dip_tr_title", "How far they trust us to keep our word"));
		t.detail.push_back(sentence(localized(state, "katechon_dip_tr_record", "Promises to them kept: %1, broken: %2"), std::to_string(record.kept), std::to_string(record.broken)));
		t.detail.push_back(localized(state, "katechon_dip_tr_how", "Kept alliances and backing raise it; abandoned allies lower it"));
	}
	auto const lev = leverage_over(state, player, target, v.knowledge.quality);
	{
		auto& f = v.influence;
		auto const l = lev.total;
		f.value = l < 0.08f ? localized(state, "katechon_dip_in_none", "Negligible") : (l < 0.22f ? localized(state, "katechon_dip_in_low", "Low") : (l < 0.45f ? localized(state, "katechon_dip_in_moderate", "Moderate") : (l < 0.7f ? localized(state, "katechon_dip_in_high", "High") : localized(state, "katechon_dip_in_dominant", "Dominant"))));
		f.colour = l >= 0.45f ? temper_colours[4] : (l >= 0.22f ? temper_colours[3] : temper_colours[2]);
		if(size_t(target.index()) < influence_series.size()) {
			influence_series[size_t(target.index())].put(today, l);
			set_trend(state, f, &influence_series[size_t(target.index())], 0.03f, "katechon_dip_in_rising", "Growing", "katechon_dip_in_falling", "Waning");
		}
		f.detail.push_back(localized(state, "katechon_dip_in_title", "Our leverage over their decisions"));
		f.detail.push_back(sentence(localized(state, "katechon_dip_in_trade", "Trade: %1 of their foreign trade is with us"), text::format_percentage(lev.trade_share, 0)));
		f.detail.push_back(lev.power >= 0.55f ? localized(state, "katechon_dip_in_stronger", "Power: they see us as the stronger (our reading)")
			: (lev.power <= 0.45f ? localized(state, "katechon_dip_in_weaker", "Power: they see us as the weaker (our reading)") : localized(state, "katechon_dip_in_even", "Power: they see us as their equal (our reading)")));
		if(lev.subsidies)
			f.detail.push_back(localized(state, "katechon_dip_in_subsidies", "We pay subsidies to their war"));
		if(lev.free_trade)
			f.detail.push_back(localized(state, "katechon_dip_in_free_trade", "Free trade binds our markets"));
		if(lev.overlord)
			f.detail.push_back(localized(state, "katechon_dip_in_subject", "They answer to us"));
		else if(lev.sphere)
			f.detail.push_back(localized(state, "katechon_dip_in_sphere", "They move in our sphere"));
	}

	// Whom it stands with, and against.
	for(auto other : state.world.in_nation) {
		if(other.id == target || other.get_owned_province_count() == 0)
			continue;
		auto const r = relation_between(state, target, other.id);
		if(military::are_at_war(state, target, other.id))
			v.foes.push_back(counterpart{ other.id, nation_name(state, other.id), localized(state, "katechon_dip_at_war", "at war"), true });
		else if(nations::are_allied(state, target, other.id))
			v.friends.push_back(counterpart{ other.id, nation_name(state, other.id), std::string() });
		else if(r <= -100.f)
			v.foes.push_back(counterpart{ other.id, nation_name(state, other.id), std::string() });
		else if(r >= 100.f)
			v.friends.push_back(counterpart{ other.id, nation_name(state, other.id), std::string() });
	}
	auto const by_power = [&](counterpart const& a, counterpart const& b) { return state.world.nation_get_rank(a.id) < state.world.nation_get_rank(b.id); };
	std::sort(v.friends.begin(), v.friends.end(), by_power);
	std::sort(v.foes.begin(), v.foes.end(), by_power);

	// A sentence on how things stand: what is settled between us, and where they lean.
	auto const allied = nations::are_allied(state, player, target);
	auto const at_war = military::are_at_war(state, player, target);
	std::string text;
	if(at_war)
		text = sentence(localized(state, "katechon_dip_s_war", "We are at war with %1."), them);
	else if(allied)
		text = sentence(localized(state, "katechon_dip_s_ally", "%1 is our ally."), them);
	else if(military::has_truce_with(state, player, target))
		text = sentence(localized(state, "katechon_dip_s_truce", "A truce binds us until %1."), text::date_to_string(state, military::truce_end_date(state, player, target)));
	if(auto const overlord = overlord_of(state, target); overlord)
		text += (text.empty() ? "" : " ") + sentence(localized(state, "katechon_dip_s_subject", "It answers to %1."), overlord == player ? localized(state, "katechon_dip_us", "us") : nation_name(state, overlord));
	else if(auto const leader = state.world.nation_get_in_sphere_of(target); leader)
		text += (text.empty() ? "" : " ") + sentence(localized(state, "katechon_dip_s_sphere", "It moves in the orbit of %1."), nation_name(state, leader));
	else if(!v.friends.empty() && !allied)
		text += (text.empty() ? "" : " ") + sentence(localized(state, "katechon_dip_s_leans", "It leans on %1."), join_names(state, v.friends, 2));
	else if(v.friends.empty() && !allied)
		text += (text.empty() ? "" : " ") + localized(state, "katechon_dip_s_alone", "It stands in no alliance.");
	if(!v.foes.empty() && !at_war)
		text += " " + sentence(localized(state, "katechon_dip_s_foes", "It is at odds with %1."), join_names(state, v.foes, 2));
	v.dispatch = std::move(text);

	gather_interests(state, v, player, target, lev);
	gather_deeds(state, v, player, target);
	gather_history(state, v, player, target);
}

void gather_own(sys::state& state, diplomacy_view& v, dcon::nation_id player) {
	namespace sc = nations::strategic_statecraft;
	v.infamy = state.world.nation_get_infamy(player);
	v.infamy_limit = state.defines.badboy_limit;
	struct ranked {
		counterpart who;
		float weight = 0.f;
	};
	std::vector<ranked> prospects;
	std::vector<counterpart> hostile;
	for(auto other : state.world.in_nation) {
		if(other.id == player || other.get_owned_province_count() == 0)
			continue;
		auto const r = relation_between(state, player, other.id);
		if(nations::are_allied(state, player, other.id)) {
			v.allies.push_back(counterpart{ other.id, nation_name(state, other.id), temper_word(state, temper_of(r)) });
		} else if(!military::are_at_war(state, player, other.id)) {
			if(r <= -100.f)
				hostile.push_back(counterpart{ other.id, nation_name(state, other.id), std::string(), true });
			if(!other.get_is_player_controlled() && r >= 0.f) {
				// as we read them: a likely answer, not a known one
				auto const a = sc::assess_alliance(state, player, other.id);
				if(a.modelled && favourable(a.verdict))
					prospects.push_back(ranked{ counterpart{ other.id, nation_name(state, other.id), verdict_word(state, a.verdict) }, a.margin - a.spread });
			}
		}
	}
	auto const by_rank = [&](counterpart const& a, counterpart const& b) { return state.world.nation_get_rank(a.id) < state.world.nation_get_rank(b.id); };
	std::sort(v.allies.begin(), v.allies.end(), by_rank);
	std::sort(hostile.begin(), hostile.end(), by_rank);
	std::sort(prospects.begin(), prospects.end(), [](ranked const& a, ranked const& b) { return a.weight > b.weight; });
	for(size_t i = 0; i < prospects.size() && i < 6; ++i)
		v.prospects.push_back(std::move(prospects[i].who));
	for(auto participant : state.world.nation_get_war_participant(player)) {
		auto const war = participant.get_war();
		auto const attacker = participant.get_is_attacker();
		auto const enemy = attacker ? war.get_primary_defender() : war.get_primary_attacker();
		v.wars.push_back(counterpart{ enemy, military::get_war_name(state, war), std::string(), true });
	}
	v.foes = hostile;
	v.friends = v.allies;

	std::string text;
	if(!v.wars.empty())
		text = sentence(localized(state, "katechon_dip_o_wars", "We are at war: %1."), join_names(state, v.wars, 2));
	else
		text = localized(state, "katechon_dip_o_peace", "We are at peace with everyone.");
	if(v.infamy >= v.infamy_limit * 0.5f)
		text += " " + localized(state, "katechon_dip_o_infamy", "Our repute is wearing thin.");
	v.dispatch = std::move(text);

	if(state.current_crisis_state != sys::crisis_state::inactive) {
		v.crisis = true;
		v.crisis_title = sentence(localized(state, "katechon_dip_crisis_between", "%1 against %2"), state.primary_crisis_attacker ? nation_name(state, state.primary_crisis_attacker) : std::string("?"),
			state.primary_crisis_defender ? nation_name(state, state.primary_crisis_defender) : std::string("?"));
		if(command::can_take_sides_in_crisis(state, player, true))
			v.crisis_deeds.push_back(deed_entry{ deed::back_attacker, dip_kind::relations, localized(state, "katechon_dip_back_attacker", "Back the claimant"), std::string(), std::string(), true });
		if(command::can_take_sides_in_crisis(state, player, false))
			v.crisis_deeds.push_back(deed_entry{ deed::back_defender, dip_kind::relations, localized(state, "katechon_dip_back_defender", "Back the defender"), std::string(), std::string(), true });
		if(command::can_become_interested_in_crisis(state, player))
			v.crisis_deeds.push_back(deed_entry{ deed::take_interest, dip_kind::relations, localized(state, "katechon_dip_take_interest", "Take an interest"), std::string(), std::string(), true });
	}
}

// The ties of the country on the sheet as arcs on the map, from capital to capital.
void lay_tie_lanes(sys::state& state) {
	namespace kt = map::katechon_trade;
	kt::lanes.clear();
	auto const from = state.world.nation_get_capital(diplomacy.nation);
	if(from) {
		for(auto const& t : diplomacy.ties) {
			auto const to = state.world.nation_get_capital(t.other);
			if(!to || to == from)
				continue;
			kt::lane_kind kind = kt::lane_kind::tie_cordial;
			switch(t.kind) {
			case tie_kind::war: kind = kt::lane_kind::tie_war; break;
			case tie_kind::alliance: kind = kt::lane_kind::tie_alliance; break;
			case tie_kind::subject: kind = kt::lane_kind::tie_subject; break;
			case tie_kind::hostile:
			case tie_kind::claim:
			case tie_kind::rival: kind = kt::lane_kind::tie_hostile; break;
			case tie_kind::cordial: kind = kt::lane_kind::tie_cordial; break;
			}
			kt::lanes.push_back(kt::lane{ from, to, false, kind, 1.f, true });
		}
	}
	kt::dirty.store(true, std::memory_order::release);
}

void gather_diplomacy(sys::state& state) {
	auto const player = state.local_player_nation;
	record_relations(state);
	gather_requests(state);
	settle_journal(state);
	diplomacy = diplomacy_view{};
	if(!player)
		return;
	if(diplomacy_focus && (!state.world.nation_is_valid(diplomacy_focus) || state.world.nation_get_owned_province_count(diplomacy_focus) == 0))
		diplomacy_focus = dcon::nation_id{};
	auto const n = diplomacy_focus ? diplomacy_focus : player;
	auto& v = diplomacy;
	v.nation = n;
	v.own = n == player;
	v.title = nation_name(state, n);
	v.subtitle = std::string();
	if(auto const government = state.world.nation_get_government_type(n); government)
		v.subtitle = text::produce_simple_string(state, state.world.government_type_get_name(government));
	if(auto const status = get_status_text(state, n); !status.empty())
		v.subtitle += (v.subtitle.empty() ? "" : "  ·  ") + status;
	v.diplomats = nations::diplomatic_points(state, player);
	if(v.own)
		gather_own(state, v, player);
	else
		gather_foreign(state, v, player, n);
	gather_ties(state, v);
	if(diplomacy_open && diplomacy_layer)
		lay_tie_lanes(state);
	if(deal_open && v.own)
		deal_open = false;
}

// The map by attitude to the country on the sheet: itself in gold, its allies
// in blue, its enemies in red, and every other country from cold to warm.
void paint_relations(sys::state& state) {
	auto const focus = diplomacy.nation;
	if(!focus)
		return;
	auto const province_size = uint32_t(state.world.province_size()) + 1;
	auto const texture_size = province_size + 256 - province_size % 256;
	std::vector<uint32_t> colours(size_t(texture_size) * 2, 0);
	std::vector<ogl::color3f> by_nation(state.world.nation_size(), ogl::color3f{ 0.f, 0.f, 0.f });
	for(auto other : state.world.in_nation) {
		auto& c = by_nation[size_t(other.id.index())];
		if(other.id == focus)
			c = ogl::color3f{ 0.90f, 0.72f, 0.40f };
		else if(other.id == state.local_player_nation && !military::are_at_war(state, focus, other.id) && !nations::are_allied(state, focus, other.id))
			c = ogl::color3f{ 0.56f, 0.48f, 0.66f };
		else if(military::are_at_war(state, focus, other.id))
			c = ogl::color3f{ 0.70f, 0.26f, 0.22f };
		else if(nations::are_allied(state, focus, other.id))
			c = ogl::color3f{ 0.30f, 0.50f, 0.70f };
		else if(state.world.nation_get_in_sphere_of(other.id) == focus || overlord_of(state, other.id) == focus)
			c = ogl::color3f{ 0.46f, 0.60f, 0.70f };
		else {
			// everyone shows its attitude; who is not tied to it says so more quietly
			auto const related = size_t(other.id.index()) < diplomacy_related.size() && diplomacy_related[size_t(other.id.index())];
			auto const tone = along(relation_ramp, std::clamp(relation_between(state, focus, other.id) / 200.f, -1.f, 1.f));
			auto const quiet = ogl::color3f{ 0.20f, 0.22f, 0.25f };
			auto const k = related ? 1.f : 0.55f;
			c = ogl::color3f{ quiet.r + (tone.r - quiet.r) * k, quiet.g + (tone.g - quiet.g) * k, quiet.b + (tone.b - quiet.b) * k };
		}
	}
	province::for_each_land_province(state, [&](dcon::province_id p) {
		auto const owner = state.world.province_get_nation_from_province_ownership(p);
		auto const c = owner ? by_nation[size_t(owner.index())] : ogl::color3f{ 0.20f, 0.21f, 0.23f };
		auto const packed = sys::pack_color(c.r, c.g, c.b);
		auto const i = province::to_map_id(p);
		colours[i] = packed;
		colours[i + texture_size] = packed;
	});
	state.map_state.set_province_color(colours, map_mode::mode::handled_from_outside);
	// the daily refresh of map modes leaves an outside mode alone; it still counts as data for the shader
	state.map_state.active_map_mode = map_mode::mode::handled_from_outside;
	diplomacy_repaint_in = 30;
}

// ---- opening and acting ----
bool had_good_before_diplomacy = false;
dcon::commodity_id good_before_diplomacy;

void open_diplomacy_panel(sys::state& state, dcon::nation_id target) {
	close_population(state);
	close_military();
	close_trade(state);
	if(country_window_open(state))
		state.ui_state.topbar_subwindow->set_visible(state, false);
	auto const next = target == state.local_player_nation ? dcon::nation_id{} : target;
	if(next != diplomacy_focus) {
		deal_open = false;
		review_deed = deed::none;
		deal_gives = 0;
		deal_asks = 0;
		dip_tab = 0;
	}
	diplomacy_focus = next;
	diplomacy_seen_province = state.map_state.selected_province;
	diplomacy_open = true;
	diplomacy_layer = true;
	radial_open = false;
	map_mode::set_map_mode(state, map_mode::mode::diplomatic);
	// The map draws its lines only while some good is chosen; the ties take their place.
	if(!map::katechon_trade::active) {
		had_good_before_diplomacy = true;
		good_before_diplomacy = state.selected_trade_good;
		if(!state.selected_trade_good && state.world.commodity_size() > 0)
			state.selected_trade_good = dcon::commodity_id{ dcon::commodity_id::value_base_t(0) };
		map::katechon_trade::active = true;
	}
	gather_diplomacy(state);
	paint_relations(state);
}

void close_diplomacy(sys::state& state) {
	deal_open = false;
	review_deed = deed::none;
	radial_open = false;
	if(!diplomacy_open)
		return;
	diplomacy_open = false;
	map::katechon_trade::active = false;
	map::katechon_trade::lanes.clear();
	if(had_good_before_diplomacy) {
		state.selected_trade_good = good_before_diplomacy;
		had_good_before_diplomacy = false;
	}
	state.map_state.map_data.trade_flow_vertices.clear();
	state.map_state.map_data.trade_flow_arrow_starts.clear();
	state.map_state.map_data.trade_flow_arrow_counts.clear();
	state.update_trade_flow.store(true, std::memory_order::release);
	if(diplomacy_layer) {
		diplomacy_layer = false;
		map_mode::set_map_mode(state, map_mode::mode::political);
	}
}

void open_radial(sys::state& state, dcon::nation_id target) {
	if(!target || !state.local_player_nation)
		return;
	radial_open = true;
	radial_nation = target;
	radial_x = qa_mouse_x >= 0.f ? qa_mouse_x : float(state.mouse_x_position) / state.user_settings.ui_scale;
	radial_y = qa_mouse_x >= 0.f ? qa_mouse_y : float(state.mouse_y_position) / state.user_settings.ui_scale;
}

// What a deed asks of them as an item of a deal.
static uint16_t ask_of(deed d) {
	namespace sc = nations::strategic_statecraft;
	switch(d) {
	case deed::ally: return sc::deal_alliance;
	case deed::ask_access: return sc::deal_access_for_us;
	case deed::free_trade: return sc::deal_free_trade;
	case deed::lift_their_embargo: return sc::deal_lift_their_embargo;
	default: return 0;
	}
}
static deed deed_of_ask(uint16_t ask) {
	namespace sc = nations::strategic_statecraft;
	if(ask & sc::deal_alliance)
		return deed::ally;
	if(ask & sc::deal_access_for_us)
		return deed::ask_access;
	if(ask & sc::deal_free_trade)
		return deed::free_trade;
	if(ask & sc::deal_lift_their_embargo)
		return deed::lift_their_embargo;
	return deed::none;
}
// Whether a deed is reviewed before it is done: what binds us, ends something, or turns to force.
static bool reviewed(deed d) {
	switch(d) {
	case deed::ally: case deed::ask_access: case deed::free_trade: case deed::lift_their_embargo: case deed::break_alliance: case deed::embargo: case deed::declare:
		return true;
	default:
		return false;
	}
}

void open_review(sys::state& state, deed what, uint64_t ground, uint16_t gives = 0) {
	review_deed = what;
	review_ground = ground;
	deal_asks = ask_of(what);
	deal_gives = gives;
	deal_open = true;
	reactions_day = -1;
}

void carry_out(sys::state& state, deed what, uint64_t ground, dcon::nation_id on = dcon::nation_id{}, bool confirmed = false) {
	auto const player = state.local_player_nation;
	auto const target = on ? on : diplomacy.nation;
	if(!player || !target)
		return;
	if(reviewed(what) && !confirmed) {
		if(diplomacy.nation != target || !diplomacy_open)
			open_diplomacy_panel(state, target);
		open_review(state, what, ground);
		return;
	}
	auto const war = military::find_war_between(state, player, target);
	switch(what) {
	case deed::improve: command::increase_relations(state, player, target); break;
	case deed::worsen: command::decrease_relations(state, player, target); break;
	case deed::ally:
		command::ask_for_alliance(state, player, target);
		note_sent(state, target, deed::ally, localized(state, "katechon_dip_j_ally", "Alliance"));
		break;
	case deed::break_alliance: command::cancel_alliance(state, player, target); break;
	case deed::ask_access:
		command::ask_for_military_access(state, player, target);
		note_sent(state, target, deed::ask_access, localized(state, "katechon_dip_j_access", "Passage"));
		break;
	case deed::end_access: command::cancel_military_access(state, player, target); break;
	case deed::give_access: command::give_military_access(state, player, target); break;
	case deed::revoke_access: command::cancel_given_military_access(state, player, target); break;
	case deed::free_trade:
		command::ask_for_free_trade_agreement(state, player, target);
		note_sent(state, target, deed::free_trade, localized(state, "katechon_dip_j_trade", "Free trade"));
		break;
	case deed::lift_their_embargo:
		if(command::can_propose_deal(state, player, target, 0, nations::strategic_statecraft::deal_lift_their_embargo)) {
			command::propose_deal(state, player, target, 0, nations::strategic_statecraft::deal_lift_their_embargo);
			note_sent(state, target, deed::open_deal, localized(state, "katechon_dip_j_lift", "Lift their embargo"), 0, nations::strategic_statecraft::deal_lift_their_embargo);
		}
		break;
	case deed::embargo:
	case deed::lift_embargo: command::switch_embargo_status(state, player, target); break;
	case deed::subsidise: command::give_war_subsidies(state, player, target); break;
	case deed::end_subsidies: command::cancel_war_subsidies(state, player, target); break;
	case deed::declare:
		command::declare_war(state, player, target, ground_cb(ground), ground_state(ground), dcon::national_identity_id{}, dcon::nation_id{}, true, false);
		break;
	case deed::justify: command::fabricate_cb(state, player, target, ground_cb(ground), ground_state(ground)); break;
	case deed::cancel_justification: command::cancel_cb_fabrication(state, player); break;
	case deed::white_peace:
		if(war) {
			command::start_peace_offer(state, player, target, war, false);
			command::send_peace_offer(state, player);
		}
		break;
	case deed::press_goals:
	case deed::concede:
		if(war) {
			// the commands run in order, so the offer exists when the goals are added to it
			auto const conceding = what == deed::concede;
			command::start_peace_offer(state, player, target, war, conceding);
			for(auto wg : state.world.war_get_wargoals_attached(war)) {
				auto const goal = wg.get_wargoal();
				if(conceding ? (goal.get_added_by() == target && goal.get_target_nation() == player) : (goal.get_added_by() == player && goal.get_target_nation() == target))
					command::add_to_peace_offer(state, player, goal.id);
			}
			command::send_peace_offer(state, player);
		}
		break;
	case deed::back_attacker: command::take_sides_in_crisis(state, player, true); break;
	case deed::back_defender: command::take_sides_in_crisis(state, player, false); break;
	case deed::take_interest: command::become_interested_in_crisis(state, player); break;
	case deed::open_dossier: open_diplomacy_panel(state, target); break;
	case deed::open_deal:
		open_diplomacy_panel(state, target);
		break;
	case deed::none: break;
	}
}

// The exchange offered under a refused proposal: the review opens with it in place.
void trade_for(sys::state& state, uint16_t ask, uint16_t gives) {
	if(auto const d = deed_of_ask(ask); d != deed::none)
		open_review(state, d, 0, gives);
}

// Sends what is under review: the plain proposal, or a deal when something is given with it.
void send_deal(sys::state& state) {
	auto const player = state.local_player_nation;
	auto const target = diplomacy.nation;
	auto what = review_deed != deed::none ? review_deed : deed_of_ask(deal_asks);
	if(!player || !target || what == deed::none)
		return;
	auto const ask = ask_of(what);
	if(deal_gives != 0 && ask != 0) {
		if(!command::can_propose_deal(state, player, target, deal_gives, ask))
			return;
		command::propose_deal(state, player, target, deal_gives, ask);
		auto const label = what == deed::ally ? localized(state, "katechon_dip_j_ally", "Alliance")
			: (what == deed::ask_access ? localized(state, "katechon_dip_j_access", "Passage") : (what == deed::free_trade ? localized(state, "katechon_dip_j_trade", "Free trade") : localized(state, "katechon_dip_j_lift", "Lift their embargo")));
		note_sent(state, target, deed::open_deal, label, deal_gives, ask);
	} else {
		carry_out(state, what, review_ground, target, true);
	}
	deal_open = false;
	review_deed = deed::none;
	deal_gives = 0;
	deal_asks = 0;
}

// ---- drawing ----
//
// The art is assets/katechon_dip_*.png (tools/katechon_ui/build_diplomacy.py):
// a lacquered panel inside one bronze line, medallions, the gold button and
// line icons. Names and titles are set in the serif of the shell; everything
// read in passing (labels, figures, the lines under a name) in a narrow sans,
// the locale's header font.

// Capitals, Cyrillic as well as Latin, for the few labels set in tracked capitals.
static std::string upper(std::string value) {
	for(size_t i = 0; i < value.size(); ++i) {
		auto const c = uint8_t(value[i]);
		if(c < 128) {
			value[i] = char(std::toupper(c));
		} else if(c == 0xD0 && i + 1 < value.size()) {
			auto const n = uint8_t(value[i + 1]);
			if(n >= 0xB0 && n <= 0xBF) // а..п
				value[i + 1] = char(n - 0x20);
			++i;
		} else if(c == 0xD1 && i + 1 < value.size()) {
			auto const n = uint8_t(value[i + 1]);
			if(n >= 0x80 && n <= 0x8F) { // р..я
				value[i] = char(0xD0);
				value[i + 1] = char(n + 0x20);
			} else if(n == 0x91) { // ё
				value[i] = char(0xD0);
				value[i + 1] = char(0x81);
			}
			++i;
		}
	}
	return value;
}

// ---- the sans ----
ankerl::unordered_dense::map<std::string, std::unique_ptr<text_run>> sans_cache;
float sans_cache_scale = 0.f;
int32_t sans_cache_height = 0;
int32_t sans_cache_locale = -1;

text_run const& sans_run(sys::state& state, std::string_view content, int32_t size, float tracking) {
	auto const locale = state.font_collection.get_current_locale().index();
	if(sans_cache_scale != state.user_settings.ui_scale || sans_cache_height != state.y_size || sans_cache_locale != locale || sans_cache.size() > 768) {
		sans_cache.clear();
		sans_cache_scale = state.user_settings.ui_scale;
		sans_cache_height = state.y_size;
		sans_cache_locale = locale;
	}
	auto key = std::string(content) + '\x1f' + std::to_string(size) + '\x1f' + std::to_string(int32_t(tracking * 1000.f));
	if(auto it = sans_cache.find(key); it != sans_cache.end())
		return *it->second;
	auto utf16 = simple_fs::utf8_to_utf16(content);
	auto span = std::span<uint16_t>(reinterpret_cast<uint16_t*>(utf16.data()), utf16.size());
	auto run = std::make_unique<text_run>(text::stored_glyphs(state, size, text::font_selection::header_font, span, text::stored_glyphs::no_bidi{}));
	auto const extra = int32_t(tracking * float(size) * state.user_settings.ui_scale * text::fixed_to_fp);
	float total = 0.f;
	for(auto& g : run->glyphs.glyph_info) {
		g.x_advance += extra;
		total += float(g.x_advance) / text::fixed_to_fp;
	}
	if(!run->glyphs.glyph_info.empty())
		total -= float(extra) / text::fixed_to_fp;
	run->width = total / state.user_settings.ui_scale;
	return *sans_cache.emplace(std::move(key), std::move(run)).first->second;
}
float sans_w(sys::state& state, std::string_view content, int32_t size, float tracking = 0.02f) {
	return content.empty() ? 0.f : sans_run(state, content, size, tracking).width;
}
float sans(sys::state& state, std::string_view content, float x, float center_y, int32_t size, ogl::color3f colour, bool align_right = false, float tracking = 0.02f) {
	if(content.empty())
		return 0.f;
	auto const& run = sans_run(state, content, size, tracking);
	auto const left = align_right ? x - run.width : x;
	auto& font = state.font_collection.get_font(state, text::font_selection::header_font);
	ogl::render_new_text(state, run.glyphs, ogl::color_modification::none, left, center_y - float(size) * 0.66f, float(size), colour, font);
	return run.width;
}
float sans_centered(sys::state& state, std::string_view content, float x, float w, float center_y, int32_t size, ogl::color3f colour, float tracking = 0.02f) {
	auto const tw = sans_w(state, content, size, tracking);
	return sans(state, content, x + (w - tw) * 0.5f, center_y, size, colour, false, tracking);
}
std::string sans_fit(sys::state& state, std::string content, int32_t size, float max_w, float tracking = 0.02f) {
	if(content.empty() || sans_w(state, content, size, tracking) <= max_w)
		return content;
	while(!content.empty()) {
		auto const removed = uint8_t(content.back());
		content.pop_back();
		if((removed & 0xC0) == 0x80)
			continue;
		while(!content.empty() && content.back() == ' ')
			content.pop_back();
		if(sans_w(state, content + "...", size, tracking) <= max_w)
			return content + "...";
	}
	return content;
}
// Running text in the sans, broken at the right edge; with draw off it only measures. Returns the bottom.
float sans_paragraph(sys::state& state, std::string const& content, float x, float right, float y, int32_t size, ogl::color3f colour, int32_t max_lines, bool draw) {
	auto const pitch = std::round(float(size) * 1.45f);
	std::string line;
	size_t at = 0;
	auto lines = 0;
	auto const put = [&]() {
		if(draw)
			sans(state, line, x, y + pitch * 0.5f, size, colour);
		y += pitch;
		++lines;
	};
	while(at <= content.size() && lines < max_lines) {
		auto const next = content.find(' ', at);
		auto const word = content.substr(at, next == std::string::npos ? std::string::npos : next - at);
		auto const candidate = line.empty() ? word : line + " " + word;
		if(!line.empty() && sans_w(state, candidate, size) > right - x) {
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

// ---- the art ----
GLuint dip_panel = 0;
GLuint dip_diamond = 0;
GLuint dip_header = 0;
GLuint dip_item = 0;
GLuint dip_item_on = 0;
GLuint dip_button = 0;
GLuint dip_button_hover = 0;
GLuint dip_chip = 0;
GLuint dip_medal = 0;
GLuint dip_icons = 0;
bool dip_art_read = false;
inline static constexpr ogl::color3f dip_muted{ 0.66f, 0.70f, 0.74f }; // what a thing is
inline static constexpr ogl::color3f dip_faint{ 0.50f, 0.55f, 0.60f }; // what is not available
inline static constexpr ogl::color3f dip_text{ 0.94f, 0.92f, 0.87f };
inline static constexpr ogl::color3f dip_name{ 0.98f, 0.94f, 0.86f };
inline static constexpr ogl::color3f dip_bronze{ 0.78f, 0.63f, 0.38f };
inline static constexpr ogl::color3f dip_yes{ 0.50f, 0.80f, 0.56f };
inline static constexpr ogl::color3f dip_no{ 0.92f, 0.48f, 0.40f };
inline static constexpr ogl::color3f dip_rule{ 0.19f, 0.24f, 0.30f };
inline static constexpr ogl::color3f dip_war_text{ 0.98f, 0.70f, 0.62f };

void load_dip_art(sys::state& state) {
	dip_art_read = true;
	auto root = simple_fs::get_root(state.common_fs);
	auto assets = simple_fs::open_directory(root, NATIVE("assets"));
	dip_panel = ogl::make_gl_texture(assets, NATIVE("katechon_dip_panel.png"));
	dip_diamond = ogl::make_gl_texture(assets, NATIVE("katechon_dip_diamond.png"));
	dip_header = ogl::make_gl_texture(assets, NATIVE("katechon_dip_header.png"));
	dip_item = ogl::make_gl_texture(assets, NATIVE("katechon_dip_item.png"));
	dip_item_on = ogl::make_gl_texture(assets, NATIVE("katechon_dip_item_on.png"));
	dip_button = ogl::make_gl_texture(assets, NATIVE("katechon_dip_button.png"));
	dip_button_hover = ogl::make_gl_texture(assets, NATIVE("katechon_dip_button_hover.png"));
	dip_chip = ogl::make_gl_texture(assets, NATIVE("katechon_dip_chip.png"));
	dip_medal = ogl::make_gl_texture(assets, NATIVE("katechon_dip_medal.png"));
	dip_icons = ogl::make_gl_texture(assets, NATIVE("katechon_dip_icons.png"));
	for(auto const texture : { dip_panel, dip_diamond, dip_header, dip_item, dip_item_on, dip_button, dip_button_hover, dip_chip, dip_medal, dip_icons }) {
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

// A plate of the art drawn in nine pieces, so that its corners keep their shape.
void nine(sys::state& state, GLuint texture, float x, float y, float w, float h, float image_w, float image_h, float corner_px, float corner) {
	if(!texture)
		return;
	ogl::render_alpha_colored_rect(state, x, y, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
	corner = std::min({ corner, w * 0.5f, h * 0.5f });
	auto const u = corner_px / image_w;
	auto const v = corner_px / image_h;
	std::array<float, 3> const xs{ x, x + corner, x + w - corner };
	std::array<float, 3> const ws{ corner, w - 2.f * corner, corner };
	std::array<float, 3> const ys{ y, y + corner, y + h - corner };
	std::array<float, 3> const hs{ corner, h - 2.f * corner, corner };
	std::array<float, 3> const us{ 0.f, u, 1.f - u };
	std::array<float, 3> const uw{ u, 1.f - 2.f * u, u };
	std::array<float, 3> const vs{ 0.f, v, 1.f - v };
	std::array<float, 3> const vh{ v, 1.f - 2.f * v, v };
	for(size_t row = 0; row < 3; ++row) {
		for(size_t col = 0; col < 3; ++col) {
			if(ws[col] <= 0.f || hs[row] <= 0.f)
				continue;
			ogl::render_subrect(state, xs[col], ys[row], ws[col], hs[row], us[col], vs[row], uw[col], vh[row], texture);
		}
	}
}

// A lacquered panel; the lozenges at its corners only on the window that matters most.
void dip_frame(sys::state& state, float x, float y, float w, float h, bool with_corners = false) {
	fill(state, x + 6.f, y + 10.f, w, h, ink, 0.32f);
	nine(state, dip_panel, x, y, w, h, 128.f, 128.f, 20.f, 10.f);
	if(with_corners && dip_diamond) {
		art(state, dip_diamond, x - 7.f, y - 7.f, 16.f, 16.f);
		art(state, dip_diamond, x + w - 9.f, y - 7.f, 16.f, 16.f);
		art(state, dip_diamond, x - 7.f, y + h - 9.f, 16.f, 16.f);
		art(state, dip_diamond, x + w - 9.f, y + h - 9.f, 16.f, 16.f);
	}
	panels.push_back(rect{ x, y, w, h });
}

void dip_glyph(sys::state& state, dip_icon which, float cx, float cy, float size, ogl::color3f c) {
	if(!dip_icons)
		return;
	ogl::render_tinted_subsprite(state, int(which), int(dip_icon::count), std::round(cx - size * 0.5f), std::round(cy - size * 0.5f), size, size, c.r, c.g, c.b, dip_icons, ui::rotation::upright, false, false);
}

void dip_medallion(sys::state& state, dip_icon which, float cx, float cy, float size, bool lit, ogl::color3f glyph = ogl::color3f{ 0.89f, 0.82f, 0.66f }) {
	if(dip_medal)
		ogl::render_tinted_subsprite(state, lit ? 1 : 0, 2, cx - size * 0.5f, cy - size * 0.5f, size, size, 1.f, 1.f, 1.f, dip_medal, ui::rotation::upright, false, false);
	dip_glyph(state, which, cx, cy, size * 0.52f, lit ? ogl::color3f{ 1.f, 0.90f, 0.64f } : glyph);
}

// A flag on a thin dark edge: frames are for the window, not for every flag on it.
void dip_flag(sys::state& state, dcon::nation_id n, float x, float y, float w, float h) {
	fill(state, x - 1.f, y - 1.f, w + 2.f, h + 2.f, ink, 0.9f);
	if(auto const handle = flag_of(state, n); handle)
		ogl::render_textured_rect(state, ogl::color_modification::none, x, y, w, h, handle, ui::rotation::upright, false, false);
}

// A word in a pill, with a dot of its colour.
float dip_chip_label(sys::state& state, std::string const& text, float x, float y, ogl::color3f dot, int32_t size = 12) {
	auto const cw = sans_w(state, text, size) + 28.f;
	nine(state, dip_chip, x, y, cw, 22.f, 64.f, 32.f, 16.f, 11.f);
	fill(state, x + 10.f, y + 8.f, 6.f, 6.f, dot, 1.f);
	sans(state, text, x + 20.f, y + 11.f, size, ogl::color3f{ 0.86f, 0.88f, 0.90f });
	return cw;
}

// A heading: the one tracked capital line a section gets.
float dip_heading(sys::state& state, std::string const& text, float left, float right, float y) {
	sans(state, upper(text), left, y + 7.f, 10, dip_bronze, false, 0.16f);
	auto const tw = sans_w(state, upper(text), 10, 0.16f);
	fill(state, left + tw + 10.f, y + 7.f, std::max(0.f, right - left - tw - 10.f), 1.f, dip_bronze, 0.30f);
	return y + 22.f;
}

void dip_rule_line(sys::state& state, float left, float right, float y) {
	fill(state, left, y, right - left, 1.f, dip_rule, 1.f);
}

bool dip_gold_button(sys::state& state, float x, float y, float w, float h, std::string const& label, bool enabled, hit_target t, bool hovering, float mx, float my, bool pressed = false) {
	t.x = x;
	t.y = y;
	t.w = w;
	t.h = h;
	auto const hovered = enabled && hovering && over(t, mx, my);
	if(enabled) {
		nine(state, hovered || pressed ? dip_button_hover : dip_button, x, y, w, h, 128.f, 64.f, 16.f, 8.f);
		sans_centered(state, upper(label), x, w, y + h * 0.5f - 1.f, 13, ogl::color3f{ 0.14f, 0.09f, 0.03f }, 0.12f);
		targets.push_back(t);
	} else {
		nine(state, dip_item, x, y, w, h, 128.f, 64.f, 16.f, 8.f);
		sans_centered(state, upper(label), x, w, y + h * 0.5f, 13, dip_faint, 0.12f);
	}
	return hovered;
}

void dip_ghost_button(sys::state& state, float x, float y, float w, float h, std::string const& label, hit_target t, bool hovering, float mx, float my) {
	t.x = x;
	t.y = y;
	t.w = w;
	t.h = h;
	auto const hovered = hovering && over(t, mx, my);
	nine(state, dip_item, x, y, w, h, 128.f, 64.f, 16.f, 8.f);
	if(hovered)
		frame(state, x + 2.f, y + 2.f, w - 4.f, h - 4.f, dip_bronze, 0.6f);
	sans_centered(state, upper(label), x, w, y + h * 0.5f, 12, hovered ? dip_text : ogl::color3f{ 0.82f, 0.86f, 0.88f }, 0.12f);
	targets.push_back(t);
}

// A hint of several lines, the first in bronze.
void dip_hint(std::vector<std::string> lines, hit_target const& t) {
	dip_tip = std::move(lines);
	dip_tip_at = rect{ t.x, t.y, t.w, t.h };
}
void draw_dip_hint(sys::state& state, float screen_w, float screen_h) {
	if(dip_tip.empty())
		return;
	float wmax = 0.f;
	for(size_t i = 0; i < dip_tip.size(); ++i)
		wmax = std::max(wmax, sans_w(state, dip_tip[i], i == 0 ? 13 : 12));
	auto const tw = std::min(wmax + 28.f, 460.f);
	auto const th = 14.f + 20.f * float(dip_tip.size());
	auto x = dip_tip_at.x + dip_tip_at.w + 10.f;
	if(x + tw > screen_w - edge)
		x = std::max(edge, dip_tip_at.x - tw - 10.f);
	auto const y = std::clamp(dip_tip_at.y, top_h + 6.f, screen_h - th - 12.f);
	fill(state, x + 4.f, y + 6.f, tw, th, ink, 0.35f);
	nine(state, dip_panel, x, y, tw, th, 128.f, 128.f, 20.f, 8.f);
	for(size_t i = 0; i < dip_tip.size(); ++i)
		sans(state, sans_fit(state, dip_tip[i], i == 0 ? 13 : 12, tw - 28.f), x + 14.f, y + 17.f + 20.f * float(i), i == 0 ? 13 : 12, i == 0 ? dip_bronze : dip_text);
	dip_tip.clear();
}

static dip_icon deed_icon(deed d) {
	switch(d) {
	case deed::ally: case deed::break_alliance: return dip_icon::shield;
	case deed::ask_access: case deed::end_access: case deed::give_access: case deed::revoke_access: return dip_icon::passage;
	case deed::free_trade: return dip_icon::scales;
	case deed::embargo: case deed::lift_embargo: case deed::lift_their_embargo: return dip_icon::lock;
	case deed::subsidise: case deed::end_subsidies: return dip_icon::coin;
	case deed::improve: return dip_icon::envelope;
	case deed::worsen: return dip_icon::heart;
	case deed::declare: case deed::justify: case deed::cancel_justification: return dip_icon::swords;
	case deed::white_peace: case deed::press_goals: case deed::concede: return dip_icon::peace;
	case deed::open_dossier: return dip_icon::scroll;
	case deed::open_deal: return dip_icon::quill;
	default: return dip_icon::star;
	}
}

// Our reading of their answer, as a pill.
float verdict_pill(sys::state& state, likelihood l, float right, float cy, int32_t size = 12) {
	auto const word = verdict_word(state, l);
	if(word.empty())
		return 0.f;
	auto const cw = sans_w(state, word, size) + 28.f;
	nine(state, dip_chip, right - cw, cy - 11.f, cw, 22.f, 64.f, 32.f, 16.f, 11.f);
	auto const c = verdict_colour(l);
	fill(state, right - cw + 10.f, cy - 3.f, 6.f, 6.f, c, 1.f);
	sans(state, word, right - cw + 20.f, cy, size, c);
	return cw;
}

// An action as a row: its medallion, its name, the answer as we read it, and
// on the second line what it does, why they would answer so, or what would
// allow it. The exchange that would win a refused proposal follows under it.
float action_row(sys::state& state, deed_entry const& d, float left, float right, float y, bool hovering, float mx, float my) {
	constexpr float row = 48.f;
	auto const usable = d.possible && d.verdict != likelihood::out_of_question;
	auto const act = !usable ? action::none : (reviewed(d.what) ? action::dip_review : action::diplomacy_deed);
	hit_target t{ left - 8.f, y, right - left + 16.f, row, act, nullptr, int32_t(d.what), pack_ground(d.cb, d.where) };
	auto const hovered = hovering && over(t, mx, my);
	if(hovered && usable)
		fill(state, t.x, t.y + 2.f, t.w, t.h - 4.f, dip_bronze, 0.10f);
	dip_medallion(state, deed_icon(d.what), left + 16.f, y + row * 0.5f, 32.f, hovered && usable, !usable ? ogl::color3f{ 0.45f, 0.48f, 0.52f } : (d.warlike ? ogl::color3f{ 0.95f, 0.60f, 0.52f } : ogl::color3f{ 0.89f, 0.82f, 0.66f }));
	float right_w = 0.f;
	if(!d.possible) {
		dip_glyph(state, dip_icon::lock, right - 8.f, y + 16.f, 13.f, dip_faint);
		right_w = 20.f;
	} else if(d.proposal) {
		right_w = verdict_pill(state, d.verdict, right, y + 16.f) + 8.f;
	}
	sans(state, sans_fit(state, d.label, 15, right - left - 46.f - right_w), left + 42.f, y + 16.f, 15, !usable ? dip_faint : (d.warlike ? dip_war_text : dip_text));
	std::string under;
	ogl::color3f under_colour = dip_muted;
	if(!d.possible) {
		under = d.requirement;
		under_colour = ogl::color3f{ 0.84f, 0.70f, 0.50f };
	} else if(d.proposal && !d.reason.empty()) {
		under = d.reason;
	} else {
		under = d.effect;
	}
	sans(state, sans_fit(state, under, 12, right - left - 46.f), left + 42.f, y + 34.f, 12, under_colour);
	if(hovered) {
		std::vector<std::string> lines{ d.label };
		if(!d.effect.empty())
			lines.push_back(d.effect);
		if(d.cost > 0.f)
			lines.push_back(diplomats_word(state, d.cost));
		if(d.proposal && !d.reason.empty())
			lines.push_back(verdict_word(state, d.verdict) + ":  " + d.reason);
		if(!d.possible && !d.requirement.empty())
			lines.push_back(d.requirement);
		if(usable && reviewed(d.what))
			lines.push_back(localized(state, "katechon_dip_review_hint", "Press to review the terms before anything is sent"));
		dip_hint(std::move(lines), t);
	}
	if(usable)
		targets.push_back(t);
	y += row;
	if(d.in_return && usable) {
		auto const text = sentence(localized(state, "katechon_swe_line", "Offer in return: %1"), d.in_return_text);
		hit_target link{ left + 34.f, y - 8.f, right - left - 34.f, 22.f, action::dip_trade, nullptr, int32_t(d.ask_bit), uint64_t(d.in_return) };
		auto const on = hovering && over(link, mx, my);
		if(on)
			fill(state, link.x, link.y, link.w, link.h, dip_bronze, 0.12f);
		draw_icon(state, icon::chevron_right, left + 46.f, y + 3.f, 9.f, on ? dip_name : dip_bronze);
		auto const tw = sans(state, sans_fit(state, text, 12, right - left - 62.f), left + 56.f, y + 3.f, 12, on ? dip_name : dip_bronze);
		if(on) {
			fill(state, left + 56.f, y + 10.f, tw, 1.f, dip_name, 0.8f);
			hint(localized(state, "katechon_swe_hint", "As we read them, this would win their agreement"), link);
		}
		targets.push_back(link);
		y += 22.f;
	}
	return y;
}

// The action that matters most, set apart: a framed card with its reasons and a button.
float primary_card(sys::state& state, deed_entry const& d, float left, float right, float y, bool hovering, float mx, float my) {
	constexpr float h = 112.f;
	auto const usable = d.possible && d.verdict != likelihood::out_of_question;
	auto const x = left - 10.f;
	auto const w = right - left + 20.f;
	nine(state, usable ? dip_item_on : dip_item, x, y, w, h, 128.f, 64.f, 16.f, 8.f);
	frame(state, x, y, w, h, dip_bronze, usable ? 0.55f : 0.25f);
	dip_medallion(state, deed_icon(d.what), x + 30.f, y + 34.f, 40.f, usable, !usable ? ogl::color3f{ 0.45f, 0.48f, 0.52f } : (d.warlike ? ogl::color3f{ 0.95f, 0.60f, 0.52f } : ogl::color3f{ 0.92f, 0.85f, 0.68f }));
	float pill = 0.f;
	if(d.proposal && d.possible)
		pill = verdict_pill(state, d.verdict, right, y + 24.f, 13);
	else if(!d.possible)
		dip_glyph(state, dip_icon::lock, right - 8.f, y + 24.f, 14.f, dip_faint);
	sans(state, sans_fit(state, d.label, 17, right - left - 52.f - pill), x + 58.f, y + 24.f, 17, usable ? (d.warlike ? dip_war_text : dip_name) : dip_faint);
	auto const second = !d.possible ? d.requirement : (d.proposal ? d.reason : std::string());
	if(!second.empty())
		sans(state, sans_fit(state, second, 13, right - x - 58.f), x + 58.f, y + 46.f, 13, !d.possible ? ogl::color3f{ 0.84f, 0.70f, 0.50f } : dip_text);
	sans_paragraph(state, d.effect, x + 58.f, right, y + (second.empty() ? 36.f : 56.f), 12, dip_muted, second.empty() ? 2 : 1, true);
	auto const label = reviewed(d.what) ? localized(state, "katechon_dip_review", "Review proposal") : localized(state, "katechon_dip_do", "Carry it out");
	auto const bw = std::max(150.f, sans_w(state, upper(label), 13, 0.12f) + 36.f);
	dip_gold_button(state, right - bw, y + h - 38.f, bw, 28.f, label, usable,
		hit_target{ 0.f, 0.f, 0.f, 0.f, reviewed(d.what) ? action::dip_review : action::diplomacy_deed, nullptr, int32_t(d.what), pack_ground(d.cb, d.where) }, hovering, mx, my);
	if(d.cost > 0.f && d.effect.find(whole(d.cost)) == std::string::npos)
		sans(state, diplomats_word(state, d.cost), x + 58.f, y + h - 24.f, 12, dip_faint);
	return y + h + 10.f;
}

// A row of flags; a press puts that country on the panel.
float flag_row(sys::state& state, std::vector<counterpart> const& list, float left, float right, float y, bool hovering, float mx, float my) {
	auto x = left;
	size_t shown = 0;
	for(auto const& c : list) {
		if(x + 34.f > right)
			break;
		hit_target t{ x - 2.f, y - 2.f, 32.f, 24.f, action::diplomacy_target, nullptr, nation_value(c.id) };
		auto const hovered = hovering && over(t, mx, my);
		dip_flag(state, c.id, x, y, 28.f, 19.f);
		if(hovered) {
			frame(state, x - 3.f, y - 3.f, 34.f, 25.f, dip_bronze, 1.f);
			hint(c.note.empty() ? c.name : c.name + "  ·  " + c.note, t);
		}
		targets.push_back(t);
		x += 34.f;
		++shown;
	}
	if(shown < list.size())
		sans(state, "+" + std::to_string(list.size() - shown), x + 2.f, y + 10.f, 12, dip_muted);
	return y + 28.f;
}

// One of the three figures: a caption, the figure, and the way it went.
void figure_column(sys::state& state, std::string const& caption, standing_figure const& f, float x, float w, float y, bool hovering, float mx, float my) {
	sans(state, upper(caption), x, y + 6.f, 10, dip_bronze, false, 0.16f);
	sans(state, sans_fit(state, f.value, 20, w - 6.f), x, y + 28.f, 20, f.colour);
	if(f.trend_known) {
		auto const c = f.trend > 0 ? dip_yes : (f.trend < 0 ? dip_no : dip_muted);
		if(f.trend != 0)
			arrow(state, x + 4.f, y + 50.f, f.trend > 0, c);
		sans(state, f.trend_word, x + (f.trend != 0 ? 14.f : 0.f), y + 50.f, 12, c);
	} else {
		sans(state, localized(state, "katechon_dip_no_record", "no record yet"), x, y + 50.f, 12, dip_faint);
	}
	hit_target t{ x - 4.f, y - 4.f, w, 64.f, action::none };
	if(hovering && over(t, mx, my) && !f.detail.empty())
		dip_hint(f.detail, t);
}

// A line of chips under a label; returns the bottom.
float chip_line(sys::state& state, std::string const& label, std::vector<interest_chip> const& chips, float left, float right, float y, bool hovering, float mx, float my) {
	if(chips.empty())
		return y;
	sans(state, label, left, y + 11.f, 12, dip_muted);
	auto x = left + 104.f;
	for(size_t i = 0; i < chips.size(); ++i) {
		auto const& c = chips[i];
		auto const dot = c.tone > 0 ? dip_yes : (c.tone < 0 ? dip_no : dip_bronze);
		auto const cw = sans_w(state, c.text, 12) + 28.f;
		if(x + cw > right) {
			if(i < chips.size())
				sans(state, "+" + std::to_string(chips.size() - i), x + 2.f, y + 11.f, 12, dip_muted);
			break;
		}
		dip_chip_label(state, c.text, x, y, dot);
		hit_target t{ x, y, cw, 22.f, action::none };
		if(hovering && over(t, mx, my) && !c.why.empty())
			dip_hint({ c.text, c.why }, t);
		x += cw + 6.f;
	}
	return y + 28.f;
}

// The tabs of the actions: what matters now, the four kinds, and the history.
float action_tabs(sys::state& state, float left, float right, float y, bool hovering, float mx, float my) {
	static constexpr std::array<std::pair<std::string_view, std::string_view>, 5> names{ std::pair<std::string_view, std::string_view>{ "katechon_dip_tab_now", "Priority" },
		std::pair<std::string_view, std::string_view>{ "katechon_dip_tab_relations", "Relations" }, std::pair<std::string_view, std::string_view>{ "katechon_dip_tab_agreements", "Agreements" },
		std::pair<std::string_view, std::string_view>{ "katechon_dip_tab_influence", "Influence" }, std::pair<std::string_view, std::string_view>{ "katechon_dip_tab_coercion", "Coercion" } };
	float total = 0.f;
	for(auto const& n : names)
		total += sans_w(state, localized(state, n.first, n.second), 13);
	auto const gap = std::max(10.f, (right - left - total) / float(names.size() - 1));
	auto x = left;
	for(size_t i = 0; i < names.size(); ++i) {
		auto const label = localized(state, names[i].first, names[i].second);
		auto const tw = sans_w(state, label, 13);
		hit_target t{ x - 4.f, y, tw + 8.f, 24.f, action::dip_tab, nullptr, int32_t(i) };
		auto const on = dip_tab == int32_t(i);
		auto const hovered = hovering && over(t, mx, my);
		sans(state, label, x, y + 11.f, 13, on ? dip_name : (hovered ? dip_text : dip_muted));
		if(on)
			fill(state, x, y + 21.f, tw, 2.f, dip_bronze, 1.f);
		targets.push_back(t);
		x += tw + gap;
	}
	dip_rule_line(state, left, right, y + 24.f);
	return y + 30.f;
}

void draw_own_panel(sys::state& state, float x0, float y0, float w, float h, bool hovering, float mx, float my) {
	auto const& v = diplomacy;
	auto const left = x0 + 24.f;
	auto const right = x0 + w - 24.f;
	auto content = 96.f + sans_paragraph(state, v.dispatch, left, right, 0.f, 14, dip_text, 4, false) + 18.f;
	for(auto const* rows : { &v.wars, &v.allies, &v.prospects, &v.foes })
		content += rows->empty() ? 0.f : 36.f;
	if(v.crisis)
		content += 26.f + 48.f * float(v.crisis_deeds.size());
	content += journal.empty() ? 0.f : 30.f + 24.f * float(std::min<size_t>(journal.size(), 3));
	content += 30.f;
	auto const ph = std::min(h - y0 - 16.f, content);
	dip_frame(state, x0, y0, w, ph);
	auto const floor_y = y0 + ph - 14.f;
	if(v.nation)
		dip_flag(state, v.nation, left, y0 + 22.f, 66.f, 44.f);
	auto const tx = left + 82.f;
	draw_text(state, fit(state, v.title, 25, 0.01f, right - tx - 24.f), tx, y0 + 34.f, 25, 0.01f, dip_name);
	sans(state, diplomats_word(state, v.diplomats) + (v.infamy > 0.f ? "  ·  " + filled(localized(state, "katechon_dip_infamy", "infamy %1"), text::format_float(v.infamy, 1)) : std::string()),
		tx, y0 + 58.f, 14, dip_muted);
	{
		hit_target close{ right - 12.f, y0 + 10.f, 26.f, 26.f, action::toggle_diplomacy };
		draw_icon(state, icon::close, close.x + 13.f, close.y + 13.f, 11.f, hovering && over(close, mx, my) ? dip_text : dip_bronze);
		targets.push_back(close);
	}
	auto y = y0 + 86.f;
	y = sans_paragraph(state, v.dispatch, left, right, y, 14, dip_text, 4, true) + 12.f;
	auto const row_of = [&](std::string const& label, std::vector<counterpart> const& rows) {
		if(rows.empty() || y + 32.f > floor_y)
			return;
		sans(state, label, left, y + 12.f, 13, dip_muted);
		flag_row(state, rows, left + 112.f, right, y + 3.f, hovering, mx, my);
		y += 36.f;
	};
	if(v.crisis) {
		sans(state, localized(state, "katechon_dip_crisis", "Crisis") + ": " + v.crisis_title, left, y + 10.f, 14, dip_no);
		y += 26.f;
		for(auto const& d : v.crisis_deeds)
			y = action_row(state, d, left, right, y, hovering, mx, my);
	}
	row_of(localized(state, "katechon_dip_wars", "At war with"), v.wars);
	row_of(localized(state, "katechon_dip_allies", "Allies"), v.allies);
	row_of(localized(state, "katechon_dip_prospects", "Likely partners"), v.prospects);
	row_of(localized(state, "katechon_dip_hostile_to_us", "Hostile to us"), v.foes);
	if(!journal.empty() && y + 50.f < floor_y) {
		dip_rule_line(state, left, right, y + 4.f);
		y += 14.f;
		for(size_t i = journal.size(), shown = 0; i-- > 0 && shown < 3; ++shown) {
			auto const& e = journal[i];
			dip_flag(state, e.target, left, y + 4.f, 18.f, 12.f);
			sans(state, sans_fit(state, e.what, 13, right - left - 130.f), left + 26.f, y + 10.f, 13, dip_text);
			auto const word = e.outcome > 0 ? localized(state, "katechon_dip_j_yes", "agreed") : (e.outcome < 0 ? localized(state, "katechon_dip_j_no", "refused") : localized(state, "katechon_dip_j_wait", "awaiting"));
			sans(state, word, right, y + 10.f, 13, e.outcome > 0 ? dip_yes : (e.outcome < 0 ? dip_no : dip_muted), true);
			y += 24.f;
		}
	}
	sans(state, localized(state, "katechon_dip_pick_hint", "Choose a country on the map; a right press shows what can be done with it"), left, floor_y - 6.f, 12, dip_faint);
}

// ---- the review of a proposal ----
struct review_line {
	std::string text;
	int8_t tone = 0;
	bool inferred = false;
	std::string strength;
};

std::string strength_word(sys::state& state, float w) {
	auto const a = std::abs(w);
	return a >= 0.15f ? localized(state, "katechon_dip_w_strong", "strong") : (a >= 0.06f ? localized(state, "katechon_dip_w_moderate", "moderate") : localized(state, "katechon_dip_w_slight", "slight"));
}

void draw_review(sys::state& state, float x, float y0, float screen_h, bool hovering, float mx, float my) {
	namespace sc = nations::strategic_statecraft;
	auto const player = state.local_player_nation;
	auto const target = diplomacy.nation;
	if(!player || !target || diplomacy.own) {
		deal_open = false;
		return;
	}
	if(review_deed == deed::none)
		review_deed = deed_of_ask(deal_asks);
	auto const what = review_deed;
	if(what == deed::none) {
		deal_open = false;
		return;
	}
	auto const& d = state.defines;
	auto const them = diplomacy.title;
	auto const points = nations::diplomatic_points(state, player);
	auto const ask = ask_of(what);
	auto const proposal = ask != 0;
	// the gifts that can go with it
	struct gift {
		uint16_t bit;
		std::string_view key;
		std::string_view fallback;
		std::string_view line_key;
		std::string_view line_fallback;
	};
	static constexpr std::array<gift, 3> gifts{ gift{ sc::deal_access_for_them, "katechon_deal_g_access", "Passage for their forces", "katechon_deal_g_access_line", "Their forces may cross our land" },
		gift{ sc::deal_subsidies, "katechon_deal_g_subsidies", "War subsidies", "katechon_deal_g_subsidies_line", "We pay a share of their war" },
		gift{ sc::deal_lift_our_embargo, "katechon_deal_g_embargo", "Lift our embargo", "katechon_deal_g_embargo_line", "Our market opens to them again" } };
	uint16_t allowed = 0;
	if(proposal) {
		for(auto const& g : gifts)
			if(command::can_deal_item(state, player, target, g.bit))
				allowed |= g.bit;
	}
	deal_gives &= allowed;
	auto const dealing = proposal && (deal_gives != 0 || what == deed::lift_their_embargo);

	// our reading of their answer
	sc::assessment a{};
	if(proposal) {
		if(dealing)
			a = sc::assess_deal(state, player, target, deal_gives, ask);
		else if(what == deed::ally)
			a = sc::assess_alliance(state, player, target);
		else if(what == deed::ask_access)
			a = sc::assess_military_access(state, player, target);
		else if(what == deed::free_trade)
			a = sc::assess_free_trade(state, player, target);
	}
	std::vector<review_line> pros;
	std::vector<review_line> cons;
	if(a.modelled) {
		if(a.veto != sc::forecast_factor::none)
			cons.push_back(review_line{ factor_word(state, a.veto), -1, false, localized(state, "katechon_dip_w_veto", "rules it out") });
		for(auto const& p : a.parts) {
			if(p.factor == sc::forecast_factor::none || p.weight <= -1.f || std::abs(p.weight) < 0.01f)
				continue;
			(p.weight > 0.f ? pros : cons).push_back(review_line{ factor_word(state, p.factor), int8_t(p.weight > 0.f ? 1 : -1), p.inferred, strength_word(state, p.weight) });
		}
		for(auto const& g : gifts) {
			if(deal_gives & g.bit) {
				auto const value = a.item_value[size_t(std::countr_zero(g.bit))];
				pros.push_back(review_line{ localized(state, g.key, g.fallback), 1, true, strength_word(state, value) });
			}
		}
		if((ask & sc::deal_lift_their_embargo) != 0)
			cons.push_back(review_line{ localized(state, "katechon_dip_w_lever", "a lever they give up"), -1, false, strength_word(state, a.item_value[size_t(std::countr_zero(uint16_t(sc::deal_lift_their_embargo)))]) });
	}

	// what it binds us to, what it costs, how long and how it ends
	std::vector<std::string> binds;
	std::string cost_line;
	std::string term_line;
	std::string end_line;
	std::string refusal_line;
	float cost = 0.f;
	std::string title;
	std::string send_label;
	switch(what) {
	case deed::ally: {
		title = localized(state, "katechon_dip_ally", "Propose an alliance");
		send_label = localized(state, "katechon_dip_send", "Send proposal");
		binds.push_back(localized(state, "katechon_dip_b_ally", "Each answers the other's call when attacked"));
		for(auto participant : state.world.nation_get_war_participant(target)) {
			binds.push_back(sentence(localized(state, "katechon_dip_b_their_war", "%1 is at war (%2): it may call us in"), them, military::get_war_name(state, participant.get_war())));
			break;
		}
		for(auto participant : state.world.nation_get_war_participant(player)) {
			binds.push_back(sentence(localized(state, "katechon_dip_b_our_war", "We are at war (%1): we may call %2 in"), military::get_war_name(state, participant.get_war()), them));
			break;
		}
		if(!diplomacy.friends.empty())
			binds.push_back(sentence(localized(state, "katechon_dip_b_their_allies", "Their other ties do not bind us: %1"), join_names(state, diplomacy.friends, 3)));
		term_line = localized(state, "katechon_dip_t_ally", "Until one side ends it");
		end_line = sentence(localized(state, "katechon_dip_x_ally", "Diplomats: %1; remembered as a broken promise"), whole(d.cancelalliance_diplomatic_cost));
		break;
	}
	case deed::ask_access:
		title = localized(state, "katechon_dip_ask_access", "Ask passage for our forces");
		send_label = localized(state, "katechon_dip_send", "Send proposal");
		binds.push_back(localized(state, "katechon_dip_b_access", "Our forces may cross their land and fight from it"));
		term_line = localized(state, "katechon_dip_t_access", "Until either side withdraws it");
		end_line = localized(state, "katechon_dip_x_access", "They may withdraw it at any time");
		break;
	case deed::free_trade: {
		title = localized(state, "katechon_dip_propose_trade", "Propose free trade");
		send_label = localized(state, "katechon_dip_send", "Send proposal");
		binds.push_back(localized(state, "katechon_dip_b_trade", "No duties on trade either way"));
		binds.push_back(localized(state, "katechon_dip_b_trade_embargo", "Neither side may impose an embargo while it lasts"));
		auto const until = state.current_date + int32_t(365 * d.alice_free_trade_agreement_years);
		term_line = sentence(localized(state, "katechon_dip_t_trade", "%1 years, until %2"), whole(d.alice_free_trade_agreement_years), text::date_to_string(state, until));
		end_line = localized(state, "katechon_dip_x_trade", "It cannot be cut short; it lapses at its term");
		break;
	}
	case deed::lift_their_embargo:
		title = localized(state, "katechon_dip_ask_lift", "Ask them to lift their embargo");
		send_label = localized(state, "katechon_dip_send", "Send proposal");
		binds.push_back(localized(state, "katechon_dip_e_ask_lift", "Their market opens to our goods again"));
		term_line = localized(state, "katechon_dip_t_lift", "Until they impose it again");
		break;
	case deed::break_alliance:
		title = localized(state, "katechon_dip_break_alliance", "End the alliance");
		send_label = localized(state, "katechon_dip_break_alliance", "End the alliance");
		binds.push_back(localized(state, "katechon_dip_b_break", "Each side records a broken promise: their trust in us falls"));
		if(state.world.nation_get_is_at_war(target))
			binds.push_back(sentence(localized(state, "katechon_dip_b_break_war", "%1 is at war: we leave it to fight alone"), them));
		binds.push_back(sentence(localized(state, "katechon_dip_b_break_trust", "Their trust in us now: %1"), diplomacy.trust.value));
		break;
	case deed::embargo:
		title = localized(state, "katechon_dip_embargo", "Impose an embargo");
		send_label = localized(state, "katechon_dip_embargo", "Impose an embargo");
		binds.push_back(localized(state, "katechon_dip_b_embargo", "Our market closes to their goods"));
		binds.push_back(localized(state, "katechon_dip_b_embargo_trade", "No free trade is possible while it stands"));
		end_line = sentence(localized(state, "katechon_dip_x_embargo", "Lifting it later costs diplomats: %1"), whole(d.askmilaccess_diplomatic_cost));
		break;
	case deed::declare: {
		title = localized(state, "katechon_dip_declare", "Declare war");
		send_label = localized(state, "katechon_dip_declare", "Declare war");
		auto const cb = ground_cb(review_ground);
		if(cb)
			binds.push_back(sentence(localized(state, "katechon_dip_b_grounds", "On the grounds of %1"), text::produce_simple_string(state, state.world.cb_type_get_name(cb))));
		std::vector<counterpart> their_allies;
		for(auto const& f : diplomacy.friends)
			if(nations::are_allied(state, target, f.id))
				their_allies.push_back(f);
		if(!their_allies.empty())
			binds.push_back(sentence(localized(state, "katechon_dip_b_their_side", "Their allies may join them: %1"), join_names(state, their_allies, 3)));
		std::vector<counterpart> ours;
		for(auto rel : state.world.nation_get_diplomatic_relation(player)) {
			if(!rel.get_are_allied())
				continue;
			auto const other = rel.get_related_nations(0) == player ? rel.get_related_nations(1) : rel.get_related_nations(0);
			if(other && other != target)
				ours.push_back(counterpart{ other, nation_name(state, other) });
		}
		if(!ours.empty())
			binds.push_back(sentence(localized(state, "katechon_dip_b_our_side", "We may call our allies: %1"), join_names(state, ours, 3)));
		break;
	}
	default:
		break;
	}
	for(auto const& g : gifts) {
		if(deal_gives & g.bit)
			binds.push_back(localized(state, g.line_key, g.line_fallback));
	}
	switch(what) {
	case deed::ally: cost = dealing ? command::deal_cost(state, deal_gives, ask) : d.alliance_diplomatic_cost; break;
	case deed::ask_access: case deed::free_trade: case deed::lift_their_embargo: cost = dealing ? command::deal_cost(state, deal_gives, ask) : d.askmilaccess_diplomatic_cost; break;
	case deed::break_alliance: cost = d.cancelalliance_diplomatic_cost; break;
	case deed::embargo: cost = d.askmilaccess_diplomatic_cost; break;
	default: break;
	}
	// what a refusal costs: the diplomats are spent either way
	if(proposal) {
		auto const lost = dealing ? sc::deal_refusal_relation : (what == deed::ally ? d.alliance_relation_on_decline : 0.f);
		refusal_line = lost < 0.f ? sentence(localized(state, "katechon_dip_refused_rel", "Relations %1; the diplomats are spent"), "-" + whole(std::abs(lost)))
			: localized(state, "katechon_dip_refused_none", "Relations unchanged; the diplomats are spent");
	}
	if(cost > 0.f)
		cost_line = sentence(localized(state, "katechon_dip_cost", "Diplomats: %1 (we have %2)"), whole(cost), whole(points));

	// who else it concerns, for an alliance: worked out once a day, it asks every government
	if(what == deed::ally) {
		auto const today = int32_t(state.current_date.value);
		if(reactions_for != target || reactions_deed != what || reactions_day != today) {
			review_reactions = sc::alliance_reactions(state, player, target, 4);
			reactions_for = target;
			reactions_deed = what;
			reactions_day = today;
		}
	} else {
		review_reactions.clear();
	}

	auto possible = false;
	switch(what) {
	case deed::ally: possible = dealing ? command::can_propose_deal(state, player, target, deal_gives, ask) : command::can_ask_for_alliance(state, player, target); break;
	case deed::ask_access: possible = dealing ? command::can_propose_deal(state, player, target, deal_gives, ask) : command::can_ask_for_access(state, player, target); break;
	case deed::free_trade: possible = dealing ? command::can_propose_deal(state, player, target, deal_gives, ask) : command::can_ask_for_free_trade_agreement(state, player, target); break;
	case deed::lift_their_embargo: possible = command::can_propose_deal(state, player, target, deal_gives, ask); break;
	case deed::break_alliance: possible = command::can_cancel_alliance(state, player, target); break;
	case deed::embargo: possible = command::can_switch_embargo_status(state, player, target); break;
	case deed::declare: possible = command::can_declare_war<false>(state, player, target, ground_cb(review_ground), ground_state(review_ground), dcon::national_identity_id{}, dcon::nation_id{}); break;
	default: break;
	}
	possible = possible && a.verdict != likelihood::out_of_question;

	// measure
	constexpr float w = 392.f;
	auto const left = x + 22.f;
	auto const right = x + w - 22.f;
	float content = 78.f;
	if(a.modelled)
		content += 34.f + 22.f + 20.f * float(std::min<size_t>(pros.size(), 5) + std::min<size_t>(cons.size(), 5)) + (pros.empty() ? 0.f : 6.f) + (cons.empty() ? 0.f : 6.f) + 8.f;
	content += 22.f + 20.f * float(binds.size()) + 8.f;
	content += (cost_line.empty() ? 0.f : 20.f) + (term_line.empty() ? 0.f : 20.f) + (end_line.empty() ? 0.f : 20.f) + (refusal_line.empty() ? 0.f : 20.f) + 30.f;
	if(!review_reactions.empty())
		content += 22.f + 26.f * float(review_reactions.size()) + 8.f;
	if(allowed)
		content += 22.f + 30.f * float(std::popcount(allowed)) + 8.f;
	content += 56.f + 24.f;
	auto const h = std::min(screen_h - y0 - 16.f, content);
	dip_frame(state, x, y0, w, h, true);
	auto const floor_y = y0 + h - 16.f;

	// title
	dip_medallion(state, deed_icon(what), left + 18.f, y0 + 36.f, 36.f, true, what == deed::declare || what == deed::embargo ? ogl::color3f{ 0.95f, 0.60f, 0.52f } : ogl::color3f{ 0.92f, 0.85f, 0.68f });
	sans(state, sans_fit(state, title, 17, right - left - 80.f), left + 46.f, y0 + 28.f, 17, dip_name);
	dip_flag(state, target, left + 46.f, y0 + 42.f, 18.f, 12.f);
	sans(state, sans_fit(state, them, 13, right - left - 90.f), left + 70.f, y0 + 48.f, 13, dip_muted);
	{
		hit_target close{ right - 12.f, y0 + 10.f, 26.f, 26.f, action::dip_deal_toggle };
		draw_icon(state, icon::close, close.x + 13.f, close.y + 13.f, 11.f, hovering && over(close, mx, my) ? dip_text : dip_bronze);
		targets.push_back(close);
	}
	auto y = y0 + 70.f;

	// the answer as we read it, and why
	if(a.modelled) {
		dip_rule_line(state, left, right, y);
		y += 8.f;
		sans(state, localized(state, "katechon_dip_reading", "Our reading of their answer"), left, y + 12.f, 13, dip_muted);
		verdict_pill(state, a.verdict, right, y + 12.f, 13);
		y += 28.f;
		{
			auto const conf = sentence(localized(state, "katechon_dip_confidence", "Confidence: %1"), diplomacy.knowledge_word);
			auto const cw = sans(state, conf, left, y + 8.f, 12, dip_faint);
			hit_target t{ left, y - 2.f, cw, 20.f, action::none };
			if(hovering && over(t, mx, my))
				dip_hint(diplomacy.knowledge_detail, t);
			y += 22.f;
		}
		auto const list = [&](std::string const& heading, std::vector<review_line> const& lines, ogl::color3f dot) {
			if(lines.empty())
				return;
			sans(state, heading, left, y + 8.f, 12, dip_bronze);
			y += 20.f;
			for(size_t i = 0; i < lines.size() && i < 5; ++i) {
				auto const& l = lines[i];
				fill(state, left + 4.f, y + 6.f, 5.f, 5.f, dot, 1.f);
				auto const tail = l.strength + (l.inferred ? "  ≈" : "");
				auto const tw = sans(state, tail, right, y + 9.f, 12, l.inferred ? dip_faint : dip_muted, true);
				sans(state, sans_fit(state, l.text, 13, right - left - 22.f - tw), left + 16.f, y + 9.f, 13, dip_text);
				y += 20.f;
			}
			y += 6.f;
		};
		list(localized(state, "katechon_dip_for_it", "What speaks for it"), pros, dip_yes);
		list(localized(state, "katechon_dip_against_it", "What weighs against it"), cons, dip_no);
		if(std::any_of(pros.begin(), pros.end(), [](review_line const& l) { return l.inferred; }) || std::any_of(cons.begin(), cons.end(), [](review_line const& l) { return l.inferred; })) {
			hit_target t{ left, y - 6.f, right - left, 18.f, action::none };
			sans(state, localized(state, "katechon_dip_inferred", "≈ our reconstruction of what they do not show"), left, y + 2.f, 11, dip_faint);
			if(hovering && over(t, mx, my))
				dip_hint(diplomacy.knowledge_detail, t);
			y += 16.f;
		}
	}

	// what it binds us to
	if(y + 40.f < floor_y) {
		y = dip_heading(state, localized(state, "katechon_dip_binds", "Commitments and consequences"), left, right, y + 4.f);
		for(auto const& b : binds) {
			if(y + 20.f > floor_y)
				break;
			fill(state, left + 4.f, y + 6.f, 5.f, 5.f, dip_bronze, 1.f);
			sans(state, sans_fit(state, b, 13, right - left - 16.f), left + 16.f, y + 9.f, 13, dip_text);
			y += 20.f;
		}
		y += 6.f;
		auto const term = [&](std::string const& label, std::string const& value) {
			if(value.empty() || y + 20.f > floor_y)
				return;
			auto const lw = sans(state, label, left, y + 9.f, 12, dip_muted);
			auto const vx = left + std::max(lw + 10.f, 92.f);
			sans(state, sans_fit(state, value, 13, right - vx), vx, y + 9.f, 13, dip_text);
			y += 20.f;
		};
		term(localized(state, "katechon_dip_term_cost", "Cost"), cost_line);
		term(localized(state, "katechon_dip_term_length", "Duration"), term_line);
		term(localized(state, "katechon_dip_term_end", "Ending it"), end_line);
		term(localized(state, "katechon_dip_term_refused", "If refused"), refusal_line);
		y += 4.f;
	}

	// who else it concerns
	if(!review_reactions.empty() && y + 48.f < floor_y) {
		y = dip_heading(state, localized(state, "katechon_dip_others", "Others concerned"), left, right, y + 4.f);
		for(auto const& r : review_reactions) {
			if(y + 26.f > floor_y)
				break;
			auto const name = nation_name(state, r.nation);
			std::string text;
			if(r.before >= 0.f && r.after < 0.f)
				text = sentence(localized(state, "katechon_dip_re_lose", "%1 might turn away from an alliance with us"), name);
			else if(r.before < 0.f && r.after >= 0.f)
				text = sentence(localized(state, "katechon_dip_re_gain", "%1 might come to ally with us"), name);
			else if(r.after < r.before)
				text = sentence(localized(state, "katechon_dip_re_cool", "%1 would think less of us as an ally"), name);
			else
				text = sentence(localized(state, "katechon_dip_re_warm", "%1 would think better of us as an ally"), name);
			std::string why;
			if(military::are_at_war(state, r.nation, target))
				why = sentence(localized(state, "katechon_dip_re_war", "at war with %1"), them);
			else if(nations::are_allied(state, r.nation, target))
				why = sentence(localized(state, "katechon_dip_re_ally", "an ally of %1"), them);
			else if(state.world.nation_get_ai_rival(r.nation) == target)
				why = sentence(localized(state, "katechon_dip_re_rival", "counts %1 its rival"), them);
			dip_flag(state, r.nation, left, y + 4.f, 21.f, 14.f);
			auto const good = r.after > r.before;
			sans(state, sans_fit(state, text, 13, right - left - 30.f), left + 30.f, y + 7.f, 13, good ? dip_yes : dip_no);
			if(!why.empty())
				sans(state, sans_fit(state, why, 11, right - left - 30.f), left + 30.f, y + 20.f, 11, dip_faint);
			y += 26.f;
		}
		y += 6.f;
	}

	// what we could give with it
	if(allowed && y + 52.f < floor_y - 46.f) {
		y = dip_heading(state, localized(state, "katechon_dip_sweeten", "Offer something in return"), left, right, y + 4.f);
		for(auto const& g : gifts) {
			if(!(allowed & g.bit) || y + 30.f > floor_y - 46.f)
				continue;
			auto const on = (deal_gives & g.bit) != 0;
			hit_target t{ left - 4.f, y, right - left + 8.f, 28.f, action::dip_item, nullptr, int32_t(g.bit) | 0x10000 };
			auto const hovered = hovering && over(t, mx, my);
			if(hovered)
				fill(state, t.x, t.y + 1.f, t.w, t.h - 2.f, dip_bronze, 0.10f);
			fill(state, left, y + 7.f, 14.f, 14.f, ink, 0.8f);
			frame(state, left, y + 7.f, 14.f, 14.f, on ? dip_bronze : ogl::color3f{ 0.40f, 0.46f, 0.52f }, 1.f);
			if(on)
				dip_glyph(state, dip_icon::check, left + 7.f, y + 14.f, 12.f, ogl::color3f{ 1.f, 0.88f, 0.56f });
			sans(state, localized(state, g.key, g.fallback), left + 24.f, y + 14.f, 13, on ? dip_name : dip_text);
			targets.push_back(t);
			if(hovered)
				hint(localized(state, g.line_key, g.line_fallback), t);
			y += 30.f;
		}
		y += 4.f;
	}

	// send, or think again
	auto const by = std::min(floor_y - 34.f, y + 8.f);
	auto const bw = (right - left - 10.f) * 0.58f;
	dip_gold_button(state, left, by, bw, 32.f, send_label, possible, hit_target{ 0.f, 0.f, 0.f, 0.f, action::dip_send }, hovering, mx, my);
	dip_ghost_button(state, left + bw + 10.f, by, right - left - bw - 10.f, 32.f, localized(state, "katechon_dip_cancel", "Not now"), hit_target{ 0.f, 0.f, 0.f, 0.f, action::dip_deal_toggle }, hovering, mx, my);
	if(!possible) {
		hit_target t{ left, by, bw, 32.f, action::none };
		auto const why = a.verdict == likelihood::out_of_question ? sentence(localized(state, "katechon_dip_r_veto", "Ruled out by %1"), factor_word(state, a.veto)) : requirement_for(state, what, player, target, cost, ground_cb(review_ground));
		if(hovering && over(t, mx, my))
			hint(why, t);
	}
}

// ---- the panel of another country ----
void draw_foreign_panel(sys::state& state, float x0, float y0, float w, float h, bool hovering, float mx, float my) {
	auto const& v = diplomacy;
	auto const left = x0 + 24.f;
	auto const right = x0 + w - 24.f;
	// as tall as what it has to say
	float content = 92.f + 74.f;
	content += sans_paragraph(state, v.dispatch, left, right, 0.f, 13, dip_text, 3, false) + 10.f;
	auto const chip_rows = float(!v.common.empty()) + float(!v.frictions.empty()) + float(!v.priorities.empty());
	content += chip_rows > 0.f ? 24.f + 28.f * chip_rows + 4.f : 0.f;
	content += 36.f;
	if(dip_tab == 0) {
		for(size_t i = 0; i < v.top.size(); ++i)
			content += i == 0 ? 122.f : 48.f + (v.deeds[v.top[i]].in_return ? 22.f : 0.f);
	} else if(dip_tab >= 1 && dip_tab <= 4) {
		for(auto const& d : v.deeds)
			if(int32_t(d.kind) == dip_tab - 1)
				content += 48.f + (d.in_return && d.possible ? 22.f : 0.f);
	} else {
		content += 22.f * float(std::max<size_t>(1, std::min<size_t>(v.history.size(), 12)));
	}
	content += 58.f;
	auto const ph = std::min(h - y0 - 16.f, content);
	dip_frame(state, x0, y0, w, ph);
	auto const floor_y = y0 + ph - 14.f;

	// who they are, and how they stand with us
	dip_flag(state, v.nation, left, y0 + 22.f, 69.f, 46.f);
	auto const tx = left + 84.f;
	auto const stance_w = sans_w(state, v.temper_word, 14) + 28.f;
	draw_text(state, fit(state, v.title, 25, 0.01f, right - tx - 34.f), tx, y0 + 34.f, 25, 0.01f, dip_name);
	sans(state, sans_fit(state, v.subtitle, 13, right - tx - stance_w - 8.f), tx, y0 + 60.f, 13, dip_muted);
	{
		auto const sx = right - stance_w;
		nine(state, dip_chip, sx, y0 + 49.f, stance_w, 22.f, 64.f, 32.f, 16.f, 11.f);
		fill(state, sx + 10.f, y0 + 57.f, 6.f, 6.f, temper_colours[size_t(v.temper)], 1.f);
		sans(state, v.temper_word, sx + 20.f, y0 + 60.f, 14, temper_colours[size_t(v.temper)]);
	}
	{
		hit_target close{ right - 12.f, y0 + 10.f, 26.f, 26.f, action::toggle_diplomacy };
		draw_icon(state, icon::close, close.x + 13.f, close.y + 13.f, 11.f, hovering && over(close, mx, my) ? dip_text : dip_bronze);
		targets.push_back(close);
		hit_target home{ right - 40.f, y0 + 10.f, 26.f, 26.f, action::diplomacy_home };
		draw_icon(state, icon::home, home.x + 13.f, home.y + 13.f, 12.f, hovering && over(home, mx, my) ? dip_text : dip_bronze);
		if(hovering && over(home, mx, my))
			hint(localized(state, "katechon_dip_home", "Our own diplomacy"), home);
		targets.push_back(home);
	}
	auto y = y0 + 88.f;

	// the three figures
	dip_rule_line(state, left, right, y);
	y += 10.f;
	{
		auto const cw = (right - left) / 3.f;
		figure_column(state, localized(state, "katechon_dip_fig_relations", "Relations"), v.relations, left, cw, y, hovering, mx, my);
		figure_column(state, localized(state, "katechon_dip_fig_trust", "Trust"), v.trust, left + cw, cw, y, hovering, mx, my);
		figure_column(state, localized(state, "katechon_dip_fig_influence", "Influence"), v.influence, left + 2.f * cw, cw, y, hovering, mx, my);
	}
	y += 62.f;
	dip_rule_line(state, left, right, y);
	y += 8.f;

	// how things stand
	y = sans_paragraph(state, v.dispatch, left, right, y, 13, dip_text, 3, true) + 6.f;

	// what we share and what divides us
	if(chip_rows > 0.f) {
		y = dip_heading(state, localized(state, "katechon_dip_interests", "Strategic interests"), left, right, y + 2.f);
		y = chip_line(state, localized(state, "katechon_dip_common", "Common ground"), v.common, left, right, y, hovering, mx, my);
		y = chip_line(state, localized(state, "katechon_dip_frictions", "Frictions"), v.frictions, left, right, y, hovering, mx, my);
		y = chip_line(state, localized(state, "katechon_dip_their_aims", "Their priorities"), v.priorities, left, right, y, hovering, mx, my);
		y += 4.f;
	}

	// what can be done
	y = action_tabs(state, left, right, y + 4.f, hovering, mx, my);
	auto const room = floor_y - 42.f;
	if(dip_tab == 0) {
		for(size_t i = 0; i < v.top.size(); ++i) {
			auto const& d = v.deeds[v.top[i]];
			if(i == 0) {
				if(y + 112.f > room)
					break;
				y = primary_card(state, d, left, right, y + 2.f, hovering, mx, my);
			} else {
				if(y + 48.f > room)
					break;
				y = action_row(state, d, left, right, y, hovering, mx, my);
			}
		}
	} else if(dip_tab >= 1 && dip_tab <= 4) {
		auto any = false;
		for(auto const& d : v.deeds) {
			if(int32_t(d.kind) != dip_tab - 1)
				continue;
			any = true;
			if(y + 48.f > room)
				break;
			y = action_row(state, d, left, right, y, hovering, mx, my);
		}
		if(!any)
			sans(state, localized(state, "katechon_dip_none_here", "Nothing of this kind can be done with them now"), left, y + 14.f, 13, dip_faint);
	} else {
		if(v.history.empty())
			sans(state, localized(state, "katechon_dip_h_none", "Nothing has passed between us yet"), left, y + 14.f, 13, dip_faint);
		for(auto const& e : v.history) {
			if(y + 22.f > room)
				break;
			auto const date = e.day > 0 ? text::date_to_string(state, date_of_day(e.day)) : std::string();
			auto const dw = sans(state, date, left, y + 11.f, 12, dip_faint);
			auto const dot = e.tone > 0 ? dip_yes : (e.tone < 0 ? dip_no : dip_bronze);
			fill(state, left + std::max(dw, 86.f) + 6.f, y + 8.f, 5.f, 5.f, dot, 1.f);
			sans(state, sans_fit(state, e.text, 13, right - left - std::max(dw, 86.f) - 18.f), left + std::max(dw, 86.f) + 18.f, y + 11.f, 13, dip_text);
			y += 22.f;
		}
	}

	// how well we read them, and the way to what has passed
	{
		auto const fy = floor_y - 40.f;
		dip_rule_line(state, left, right, fy);
		auto const label = localized(state, "katechon_dip_our_reading", "Our reading of them:");
		auto const lw = sans(state, label, left, fy + 16.f, 12, dip_muted);
		auto const q = v.knowledge.quality;
		auto const qc = q >= 0.6f ? dip_yes : (q >= 0.35f ? ogl::color3f{ 0.92f, 0.76f, 0.42f } : dip_no);
		auto const kw = sans(state, v.knowledge_word, left + lw + 6.f, fy + 16.f, 12, qc);
		auto const bar_x = left + lw + kw + 16.f;
		auto const bar_w = 60.f;
		fill(state, bar_x, fy + 14.f, bar_w, 4.f, dip_rule, 1.f);
		fill(state, bar_x, fy + 14.f, bar_w * std::clamp(q / 0.9f, 0.f, 1.f), 4.f, qc, 1.f);
		hit_target t{ left, fy + 4.f, bar_x + bar_w - left, 22.f, action::none };
		if(hovering && over(t, mx, my))
			dip_hint(v.knowledge_detail, t);
		auto const history_label = dip_tab == 5 ? localized(state, "katechon_dip_back", "Back to actions") : localized(state, "katechon_dip_history", "Diplomatic history");
		auto const hw = sans_w(state, history_label, 13);
		hit_target link{ right - hw - 18.f, fy + 6.f, hw + 18.f, 22.f, action::dip_tab, nullptr, dip_tab == 5 ? 0 : 5 };
		auto const on = hovering && over(link, mx, my);
		sans(state, history_label, right - 14.f, fy + 16.f, 13, on ? dip_name : dip_bronze, true);
		draw_icon(state, icon::chevron_right, right - 5.f, fy + 16.f, 9.f, on ? dip_name : dip_bronze);
		targets.push_back(link);
		sans(state, sans_fit(state, diplomats_word(state, v.diplomats), 12, right - left), left, fy + 34.f, 12, dip_faint);
	}
}

void draw_diplomacy(sys::state& state, float h, bool hovering, float mx, float my) {
	if(!dip_art_read)
		load_dip_art(state);
	constexpr float w = 436.f;
	auto const x0 = ribbon_w + 8.f;
	auto const y0 = top_h + 12.f;
	if(diplomacy.own)
		draw_own_panel(state, x0, y0, w, h, hovering, mx, my);
	else
		draw_foreign_panel(state, x0, y0, w, h, hovering, mx, my);
	if(deal_open)
		draw_review(state, x0 + w + 14.f, y0, h, hovering, mx, my);
}

// Under the mouse, while the sheet paints the map: how a country stands with the one on the sheet.
void draw_map_hover(sys::state& state, float w, float h, float mx, float my) {
	if(!diplomacy_open || !diplomacy_layer || radial_open)
		return;
	for(auto const& p : panels) {
		if(mx >= p.x && mx < p.x + p.w && my >= p.y && my < p.y + p.h)
			return;
	}
	if(my < top_h || mx < ribbon_w)
		return;
	auto const scale = state.user_settings.ui_scale;
	auto const province = state.map_state.get_province_under_mouse(state, int32_t(mx * scale), int32_t(my * scale), state.x_size, state.y_size);
	if(!province)
		return;
	auto const owner = state.world.province_get_nation_from_province_ownership(province);
	auto const focus = diplomacy.nation;
	auto const player = state.local_player_nation;
	if(!owner || !focus || owner == focus)
		return;
	std::vector<std::pair<std::string, ogl::color3f>> lines;
	auto const r = relation_between(state, owner, focus);
	auto const t = temper_of(r);
	auto const toward = focus == player ? localized(state, "katechon_dip_us", "us") : diplomacy.title;
	lines.emplace_back(sentence(localized(state, "katechon_dip_hv_toward", "Toward %1: %2"), toward, temper_word(state, t) + " (" + (r > 0.f ? "+" : "") + std::to_string(int32_t(r)) + ")"), temper_colours[size_t(t)]);
	if(military::are_at_war(state, owner, focus))
		lines.emplace_back(sentence(localized(state, "katechon_dip_hv_war", "At war with %1"), toward), tie_colour(tie_kind::war));
	else if(nations::are_allied(state, owner, focus))
		lines.emplace_back(sentence(localized(state, "katechon_dip_hv_ally", "Allied with %1"), toward), tie_colour(tie_kind::alliance));
	if(overlord_of(state, owner) == focus || state.world.nation_get_in_sphere_of(owner) == focus)
		lines.emplace_back(sentence(localized(state, "katechon_dip_hv_under", "Under the protection of %1"), toward), tie_colour(tie_kind::subject));
	else if(overlord_of(state, focus) == owner || state.world.nation_get_in_sphere_of(focus) == owner)
		lines.emplace_back(sentence(localized(state, "katechon_dip_hv_over", "Protects %1"), toward), tie_colour(tie_kind::subject));
	if(holds_claim(state, owner, focus))
		lines.emplace_back(sentence(localized(state, "katechon_dip_hv_holds", "Holds land %1 claims"), toward), tie_colour(tie_kind::claim));
	if(holds_claim(state, focus, owner))
		lines.emplace_back(sentence(localized(state, "katechon_dip_hv_claims", "Claims land of %1"), toward), tie_colour(tie_kind::claim));
	if(state.world.nation_get_ai_rival(owner) == focus && (owner == player || nations::strategic_statecraft::read_intentions(state, player, owner).quality >= 0.3f))
		lines.emplace_back(sentence(localized(state, "katechon_dip_hv_rival", "Counts %1 its rival"), toward), tie_colour(tie_kind::rival));
	if(focus != player && owner != player) {
		auto const ours = relation_between(state, owner, player);
		lines.emplace_back(sentence(localized(state, "katechon_dip_hv_us", "Toward us: %1"), temper_word(state, temper_of(ours))), dip_muted);
	}
	auto const name = nation_name(state, owner);
	float tw = sans_w(state, name, 15) + 34.f;
	for(auto const& l : lines)
		tw = std::max(tw, sans_w(state, l.first, 12));
	tw += 28.f;
	auto const th = 40.f + 18.f * float(lines.size());
	auto x = mx + 18.f;
	auto y = my + 18.f;
	if(x + tw > w - edge)
		x = mx - tw - 12.f;
	if(y + th > h - edge)
		y = my - th - 12.f;
	fill(state, x + 4.f, y + 6.f, tw, th, ink, 0.35f);
	nine(state, dip_panel, x, y, tw, th, 128.f, 128.f, 20.f, 8.f);
	dip_flag(state, owner, x + 14.f, y + 12.f, 24.f, 16.f);
	sans(state, name, x + 46.f, y + 20.f, 15, dip_name);
	auto ly = y + 40.f;
	for(auto const& l : lines) {
		sans(state, l.first, x + 14.f, ly + 2.f, 12, l.second);
		ly += 18.f;
	}
}

static ogl::color3f tie_colour(tie_kind k) {
	switch(k) {
	case tie_kind::war: return ogl::color3f{ 0.95f, 0.40f, 0.34f };
	case tie_kind::alliance: return ogl::color3f{ 0.50f, 0.74f, 0.96f };
	case tie_kind::subject: return ogl::color3f{ 0.72f, 0.84f, 0.92f };
	case tie_kind::hostile: case tie_kind::claim: case tie_kind::rival: return ogl::color3f{ 0.92f, 0.54f, 0.40f };
	case tie_kind::cordial: return ogl::color3f{ 0.56f, 0.86f, 0.74f };
	}
	return dip_muted;
}

// The ends of the ties: a small mark at the capital of the country on the sheet.
// The arcs themselves are drawn on the map (lay_tie_lanes).
void draw_relation_web(sys::state& state, float w, float h) {
	auto const focus = diplomacy.nation;
	if(!focus)
		return;
	auto const capital = state.world.nation_get_capital(focus);
	glm::vec2 at{};
	if(!capital || !project(state, state.world.province_get_mid_point(capital), at))
		return;
	if(at.x < 0.f || at.x > w || at.y < 0.f || at.y > h)
		return;
	fill(state, at.x - 5.f, at.y - 5.f, 10.f, 10.f, ink, 0.8f);
	fill(state, at.x - 3.5f, at.y - 3.5f, 7.f, 7.f, gold, 1.f);
}

// ---- the ring of actions around a pressed country ----
void draw_radial(sys::state& state, float w, float h, bool hovering, float mx, float my) {
	if(!dip_art_read)
		load_dip_art(state);
	auto const player = state.local_player_nation;
	auto const target = radial_nation;
	if(!player || !target || !state.world.nation_is_valid(target)) {
		radial_open = false;
		return;
	}
	namespace sc = nations::strategic_statecraft;
	struct choice {
		deed what;
		std::string label;
		bool possible;
		likelihood reply;
		std::string why;
		uint64_t ground = 0;
	};
	std::vector<choice> choices;
	auto const points = nations::diplomatic_points(state, player);
	auto const& d = state.defines;
	if(target == player) {
		choices.push_back(choice{ deed::open_dossier, localized(state, "katechon_rad_overview", "Our diplomacy"), true, likelihood::unknown });
	} else {
		auto const at_war = military::are_at_war(state, player, target);
		choices.push_back(choice{ deed::open_dossier, localized(state, "katechon_rad_dossier", "Country"), true, likelihood::unknown });
		if(!at_war) {
			if(nations::are_allied(state, player, target)) {
				choices.push_back(choice{ deed::break_alliance, localized(state, "katechon_rad_break", "End alliance"), command::can_cancel_alliance(state, player, target), likelihood::unknown });
			} else {
				auto const a = sc::assess_alliance(state, player, target);
				auto const can = command::can_ask_for_alliance(state, player, target);
				choices.push_back(choice{ deed::ally, localized(state, "katechon_rad_ally", "Alliance"), can && a.verdict != likelihood::out_of_question, a.verdict,
					can ? std::string() : requirement_for(state, deed::ally, player, target, d.alliance_diplomatic_cost) });
			}
			choices.push_back(choice{ deed::improve, localized(state, "katechon_rad_improve", "Mission"), command::can_increase_relations(state, player, target), likelihood::unknown,
				points < d.increaserelation_diplomatic_cost ? requirement_for(state, deed::improve, player, target, d.increaserelation_diplomatic_cost) : std::string() });
			{
				auto const a = sc::assess_military_access(state, player, target);
				auto const can = command::can_ask_for_access(state, player, target);
				choices.push_back(choice{ deed::ask_access, localized(state, "katechon_rad_access", "Passage"), can && a.verdict != likelihood::out_of_question, a.verdict,
					can ? std::string() : requirement_for(state, deed::ask_access, player, target, d.askmilaccess_diplomatic_cost) });
			}
			std::vector<deed_entry> grounds;
			gather_grounds(state, grounds, player, target, false);
			if(!grounds.empty())
				choices.push_back(choice{ deed::declare, localized(state, "katechon_rad_war", "War"), true, likelihood::unknown, std::string(), pack_ground(grounds.front().cb, grounds.front().where) });
		} else {
			auto const war = military::find_war_between(state, player, target);
			choices.push_back(choice{ deed::white_peace, localized(state, "katechon_rad_peace", "Peace"), war && command::can_start_peace_offer(state, player, target, war, false), likelihood::unknown });
		}
	}
	constexpr float radius = 84.f;
	constexpr float button = 50.f;
	auto const cx = std::clamp(radial_x, radius + 60.f, w - radius - 60.f);
	auto const cy = std::clamp(radial_y, top_h + radius + 50.f, h - radius - 70.f);
	targets.push_back(hit_target{ 0.f, 0.f, w, h, action::dip_radial_close });
	panels.push_back(rect{ 0.f, 0.f, w, h });
	dip_flag(state, target, cx - 27.f, cy - 26.f, 54.f, 36.f);
	{
		auto const name = nation_name(state, target);
		auto const nw = sans_w(state, name, 12) + 18.f;
		nine(state, dip_chip, cx - nw * 0.5f, cy + 14.f, nw, 22.f, 64.f, 32.f, 16.f, 11.f);
		sans_centered(state, name, cx - nw * 0.5f, nw, cy + 25.f, 12, dip_text);
	}
	auto const n = choices.size();
	for(size_t i = 0; i < n; ++i) {
		auto const angle = -1.5707963f + 6.2831853f * float(i) / float(n);
		auto const bx = cx + radius * std::cos(angle);
		auto const by = cy + radius * std::sin(angle);
		auto const& c = choices[i];
		hit_target t{ bx - button * 0.5f, by - button * 0.5f, button, button, c.possible ? action::dip_radial : action::none, nullptr, int32_t(c.what), c.ground };
		auto const hovered = hovering && over(t, mx, my);
		dip_medallion(state, deed_icon(c.what), bx, by, button, hovered && c.possible,
			c.possible ? (c.what == deed::declare ? ogl::color3f{ 0.98f, 0.62f, 0.52f } : ogl::color3f{ 0.92f, 0.85f, 0.68f }) : ogl::color3f{ 0.45f, 0.48f, 0.52f });
		if(c.reply != likelihood::unknown) {
			// our reading, as a dot: not a promise of the answer
			auto const col = verdict_colour(c.reply);
			fill(state, bx + 13.f, by - 22.f, 10.f, 10.f, ink, 0.85f);
			fill(state, bx + 14.f, by - 21.f, 8.f, 8.f, col, 1.f);
		}
		auto const lw = sans_w(state, c.label, 12) + 18.f;
		nine(state, dip_chip, bx - lw * 0.5f, by + 27.f, lw, 22.f, 64.f, 32.f, 16.f, 11.f);
		sans_centered(state, c.label, bx - lw * 0.5f, lw, by + 38.f, 12, c.possible ? dip_text : dip_faint);
		targets.push_back(t);
		if(hovered) {
			std::string why;
			if(c.reply != likelihood::unknown)
				why = sentence(localized(state, "katechon_rad_reading", "Our reading: %1"), verdict_word(state, c.reply));
			if(!c.why.empty())
				why += (why.empty() ? "" : "  ·  ") + c.why;
			else if(!c.possible)
				why += (why.empty() ? "" : "  ·  ") + localized(state, "katechon_dip_r_cannot", "not possible now");
			if(!why.empty())
				hint(why, t);
		}
	}
}

// ---- what other states put to us: cards under the top bar ----
void draw_requests(sys::state& state, float w, bool hovering, float mx, float my) {
	auto const now = std::chrono::steady_clock::now();
	toasts.erase(std::remove_if(toasts.begin(), toasts.end(), [&](toast const& t) { return now - t.since > std::chrono::seconds(6); }), toasts.end());
	if(requests.empty() && toasts.empty())
		return;
	if(!dip_art_read)
		load_dip_art(state);
	constexpr float card_w = 340.f;
	constexpr float card_h = 116.f;
	auto const x = w - card_w - edge - 6.f;
	auto y = top_h + 14.f;
	for(auto const& t : toasts) {
		dip_frame(state, x, y, card_w, 52.f);
		dip_flag(state, t.from, x + 16.f, y + 14.f, 33.f, 22.f);
		dip_glyph(state, t.good ? dip_icon::check : dip_icon::cross, x + 66.f, y + 26.f, 14.f, t.good ? dip_yes : dip_no);
		sans(state, sans_fit(state, t.text, 14, card_w - 100.f), x + 82.f, y + 26.f, 14, dip_text);
		y += 62.f;
	}
	for(size_t i = 0; i < requests.size(); ++i) {
		auto const& r = requests[i];
		dip_frame(state, x, y, card_w, card_h);
		dip_flag(state, r.message.from, x + 18.f, y + 18.f, 36.f, 24.f);
		sans(state, upper(r.title), x + 66.f, y + 22.f, 10, dip_bronze, false, 0.14f);
		sans_paragraph(state, r.body, x + 66.f, x + card_w - 18.f, y + 30.f, 13, dip_text, 2, true);
		auto const bw = (card_w - 36.f - 10.f) * 0.5f;
		dip_gold_button(state, x + 18.f, y + card_h - 40.f, bw, 28.f, localized(state, "katechon_req_accept", "Accept"), true, hit_target{ 0.f, 0.f, 0.f, 0.f, action::dip_answer, nullptr, int32_t(i), 1 }, hovering, mx, my);
		dip_ghost_button(state, x + 28.f + bw, y + card_h - 40.f, bw, 28.f, localized(state, "katechon_req_decline", "Decline"), hit_target{ 0.f, 0.f, 0.f, 0.f, action::dip_answer, nullptr, int32_t(i), 0 }, hovering, mx, my);
		hit_target who{ x + 14.f, y + 14.f, 44.f, 32.f, action::diplomacy_target, nullptr, nation_value(r.message.from) };
		targets.push_back(who);
		y += card_h + 12.f;
	}
}

void answer_request(sys::state& state, int32_t index, bool yes) {
	if(index < 0 || size_t(index) >= requests.size() || !state.local_player_nation)
		return;
	auto const& m = requests[size_t(index)].message;
	command::respond_to_diplomatic_message(state, state.local_player_nation, m.from, m.type, yes);
	requests.erase(requests.begin() + index);
}

void draw_deal(sys::state&, float, float, bool, float, float) {
	// the review of a proposal is drawn beside the sheet (draw_review)
}

// KATECHON_HUD_SHOT=<file.png>[,<frames>]: once the HUD has drawn that many
// frames (300 by default), it reads the frame back and saves it, then quits
// the game. A QA hook: it works while the window is hidden behind others.
void qa_capture(sys::state& state) {
	static int32_t frames_left = -2;
	static std::string path;
	if(frames_left == -2) {
		frames_left = -1;
		if(auto const* shot = std::getenv("KATECHON_HUD_SHOT"); shot && shot[0]) {
			path = shot;
			frames_left = 300;
			if(auto const comma = path.find(','); comma != std::string::npos) {
				frames_left = std::max(1, std::atoi(path.c_str() + comma + 1));
				path.resize(comma);
			}
		}
	}
	if(frames_left < 0)
		return;
	// KATECHON_HUD_QA_DIPLOMATS=<n>: the player is given that many diplomats, to see what they unlock
	if(auto const* diplomats = std::getenv("KATECHON_HUD_QA_DIPLOMATS"); diplomats && diplomats[0] && state.local_player_nation)
		state.world.nation_set_diplomatic_points(state.local_player_nation, float(std::atof(diplomats)));
	// KATECHON_HUD_QA_MISSION=1: a diplomatic mission goes to the country on the sheet, once, early on
	static bool mission_sent = false;
	if(!mission_sent && diplomacy_open && !diplomacy.own) {
		if(auto const* mission = std::getenv("KATECHON_HUD_QA_MISSION"); mission && mission[0] == '1') {
			mission_sent = true;
			carry_out(state, deed::improve, 0);
			std::fprintf(stderr, "KATECHON-QA mission to %s on %s; reading %.2f\n", diplomacy.title.c_str(), text::date_to_string(state, state.current_date).c_str(), diplomacy.knowledge.quality);
		}
	}
	if(--frames_left > 0) {
		if(frames_left % 250 == 0)
			std::fprintf(stderr, "KATECHON-QA %d frames to go, %s\n", int(frames_left), text::date_to_string(state, state.current_date).c_str());
		return;
	}
	if(diplomacy_open && !diplomacy.own)
		std::fprintf(stderr, "KATECHON-QA sheet %s on %s: relations %.0f, reading %.2f (envoys %d days, intel %d days)\n", diplomacy.title.c_str(),
			text::date_to_string(state, state.current_date).c_str(), diplomacy.relation, diplomacy.knowledge.quality, int(diplomacy.knowledge.embassy_age), int(diplomacy.knowledge.intelligence_age));
	frames_left = -1;
	auto const w = state.x_size;
	auto const h = state.y_size;
	std::vector<uint8_t> pixels(size_t(w) * size_t(h) * 3);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
	glReadBuffer(GL_BACK);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
	std::vector<uint8_t> flipped(pixels.size());
	for(int32_t row = 0; row < h; ++row)
		std::memcpy(flipped.data() + size_t(row) * size_t(w) * 3, pixels.data() + size_t(h - 1 - row) * size_t(w) * 3, size_t(w) * 3);
	stbi_write_png(path.c_str(), w, h, 3, flipped.data(), w * 3);
	std::fprintf(stderr, "KATECHON-QA shot: %s (%dx%d)\n", path.c_str(), int(w), int(h));
	std::fflush(stderr);
	std::_Exit(0); // a QA run ends here, without tearing down the running game
}
