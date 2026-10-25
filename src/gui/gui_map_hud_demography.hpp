// The demography tab of the KATECHON population pages. This file is included
// inside the body of ui::map_hud_element (gui_map_hud.hpp) and uses its
// drawing helpers; it is not a header of its own.
//
// The overview says what the people are now; this tab says what they will
// become. Every person in the simulation has a birth day, so the age profile
// is counted, not guessed. The years ahead hold today's monthly rates and
// follow the game's own rules: a growing group adds newborns, a shrinking one
// loses people of every age alike, and moves across the border and the
// army's losses touch every age alike too. Five pages: the profile with the
// outlook beside it, the forecast year by year, the generations, the
// movement of people, and the age groups. The map beside each page is
// painted with the figure the page is about.

struct demography_view {
	static constexpr int32_t horizon = 40; // years ahead
	static constexpr int32_t oldest = 140; // the last age kept; everyone older counts with it
	static constexpr int32_t bands = 19; // five years each: 0-4 ... 85-89, then 90 and older
	bool ready = false;
	dcon::nation_id nation;
	dcon::province_id province;
	int32_t month = -1; // the month it was counted in
	int32_t year = 0; // the year of the first column
	std::vector<std::vector<float>> ages; // units by age, one profile for each year from now
	std::vector<std::vector<float>> anchors; // the same for the workforce anchors
	std::vector<std::vector<float>> men; // the same for the men
	std::array<std::array<float, bands>, horizon + 1> grouped{}; // everyone in five-year groups
	std::array<float, horizon + 1> total{};
	std::array<float, horizon + 1> total_relieved{}; // without hunger, crowded housing and the army's losses
	std::array<float, horizon + 1> born{}; // from now to each year
	std::array<float, horizon + 1> died{};
	std::array<float, horizon + 1> moved{}; // across the border and in the army, net
	bool relief = false; // the second line differs from the first
	// the month's change the years ahead hold, in units
	float births = 0.f;
	float deaths = 0.f;
	float other = 0.f;
	float spared = 0.f; // hunger and crowded housing
	float army = 0.f;
	// people by age now: with a wage contract, and serving in a formation
	std::vector<float> contracted;
	std::vector<float> serving;
	// every region of the country, for the map
	struct place {
		dcon::province_id id;
		float units = 0.f;
		std::vector<float> ages;
		float change = 0.f; // by the forecast's middle year, as a part of today
	};
	std::vector<place> places;
	struct flow {
		dcon::nation_id nation;
		dcon::province_id region;
		float amount = 0.f; // a month; positive when they come
	};
	std::vector<flow> abroad_in; // the game's estimate of the month's moves across the border, largest first
	std::vector<flow> abroad_out;
	std::vector<flow> regions_in; // the month's moves between the country's regions
	std::vector<flow> regions_out;
};
demography_view demography;
int32_t population_tab = 0; // 0 the overview, 1 demography
int32_t demography_page = 0; // 0 the profile, 1 the forecast, 2 the generations, 3 movement, 4 age groups
int32_t demography_age = 20; // the generation followed, by its age now; below 0 not yet born
int32_t demography_compare = demo_no_compare; // a second generation beside it
int32_t demography_year = 25; // the year the forecast inspects, in years from now
bool demography_internal = false; // movement between regions in place of movement across the border
bool demography_inputs = false; // the forecast's monthly rates in place of its sums
ogl::lines demo_line{ uint32_t(demography_view::horizon + 1) };
ogl::lines demo_relief_line{ uint32_t(demography_view::horizon + 1) };
ogl::lines demo_cohort_line{ uint32_t(demography_view::horizon + 1) };
ogl::lines demo_flow_line{ 16 };

static constexpr int32_t demo_no_compare = -1000;
static constexpr int32_t demo_mid_years = 25; // the forecast's middle year, which the overview and the map read
// The working ages of the person kernel, from 14 to the 65th birthday, and
// the ages land forces recruit from, 18 to the 65th birthday.
static constexpr int32_t demo_work_from = persons::policy::minimum_working_age_days / 365;
static constexpr int32_t demo_work_to = persons::policy::maximum_working_age_days / 365;
static constexpr int32_t demo_recruit_from = 18;
inline static constexpr ogl::color3f demo_young{ 0.46f, 0.66f, 0.62f };
inline static constexpr ogl::color3f demo_working{ 0.78f, 0.63f, 0.38f };
inline static constexpr ogl::color3f demo_old{ 0.50f, 0.58f, 0.70f };
inline static constexpr ogl::color3f demo_few{ 0.085f, 0.125f, 0.180f };
inline static constexpr ogl::color3f demo_many{ 0.86f, 0.60f, 0.44f };
inline static constexpr ogl::color3f demo_even{ 0.25f, 0.27f, 0.32f }; // a group of the year's average size
inline static constexpr ogl::color3f demo_second{ 0.56f, 0.74f, 0.86f }; // the generation compared
inline static constexpr ogl::color3f demo_men{ 0.42f, 0.58f, 0.76f };
inline static constexpr ogl::color3f demo_women{ 0.82f, 0.52f, 0.50f };

// People of the ages [from, to) in a profile; to below 0 runs to the end.
static float demo_sum(std::vector<float> const& profile, int32_t from, int32_t to) {
	auto const end = to < 0 ? int32_t(profile.size()) : std::min(to, int32_t(profile.size()));
	auto sum = 0.f;
	for(auto a = std::max(from, 0); a < end; ++a)
		sum += profile[size_t(a)];
	return sum;
}
static float demo_median(std::vector<float> const& profile) {
	auto const all = demo_sum(profile, 0, -1);
	auto seen = 0.f;
	for(size_t a = 0; a < profile.size(); ++a) {
		if(seen + profile[a] >= all * 0.5f)
			return float(a) + (profile[a] > 0.f ? (all * 0.5f - seen) / profile[a] : 0.f);
		seen += profile[a];
	}
	return float(profile.size());
}
static std::string demo_band_name(int32_t band) {
	if(band >= demography_view::bands - 1)
		return std::to_string(band * 5) + "+";
	return std::to_string(band * 5) + "–" + std::to_string(band * 5 + 4);
}
static std::string demo_range(int32_t from, int32_t to) {
	return to < 0 ? std::to_string(from) + "+" : std::to_string(from) + "–" + std::to_string(to - 1);
}
// The members of a generation, by its age now, a number of years on.
float demo_members(int32_t age, int32_t years_on) const {
	auto const& d = demography;
	if(years_on < 0 || size_t(years_on) >= d.ages.size())
		return 0.f;
	auto const then = age + years_on;
	if(then < 0 || then >= demography_view::oldest)
		return 0.f;
	return d.ages[size_t(years_on)][size_t(then)];
}
int32_t demo_birth_year(int32_t age) const {
	return demography.year - age;
}
// The age a generation has a number of years on, as words.
std::string demo_age_text(int32_t age, int32_t years_on) const {
	auto const then = age + years_on;
	return then < 0 ? std::string("—") : std::to_string(then);
}
// The three stages the person kernel knows.
std::string demo_stage(sys::state& state, float age) const {
	if(age < 0.f)
		return localized(state, "katechon_demo_unborn", "Not yet born");
	if(age < float(demo_work_from))
		return localized(state, "katechon_demo_stage_child", "Childhood");
	if(age < float(demo_work_to))
		return localized(state, "katechon_demo_stage_work", "Working age");
	return localized(state, "katechon_demo_stage_old", "Past working age");
}
// The course of a working life, named by age around the kernel's bounds:
// work from 14, the army from 18, past working age at 65.
std::string demo_course(sys::state& state, int32_t age) const {
	if(age < 0)
		return localized(state, "katechon_demo_unborn", "Not yet born");
	if(age < demo_work_from)
		return localized(state, "katechon_demo_stage_child", "Childhood");
	if(age < demo_recruit_from)
		return localized(state, "katechon_demo_course_enter", "Entering work");
	if(age < 30)
		return localized(state, "katechon_demo_course_early", "Early career");
	if(age < 50)
		return localized(state, "katechon_demo_course_peak", "Peak workforce");
	if(age < 60)
		return localized(state, "katechon_demo_course_senior", "Senior workers");
	if(age < demo_work_to)
		return localized(state, "katechon_demo_course_retiring", "Retirement approaches");
	return localized(state, "katechon_demo_stage_old", "Past working age");
}
std::string demo_years(sys::state& state, float age) const {
	return filled(localized(state, "katechon_demo_years", "%1 years"), text::format_float(age, 1));
}

// ---- counting ----

// Counts the ages of the people of the country or region on the page and
// carries them forward. Once a month, or when the page studies another place.
void gather_demography(sys::state& state) {
	auto const& v = population;
	auto& d = demography;
	auto const today = state.current_date.to_ymd(state.start_date);
	auto const month = int32_t(today.year) * 12 + int32_t(today.month);
	if(d.ready && d.nation == v.nation && d.province == v.province && d.month == month)
		return;
	d = demography_view{};
	d.ready = true;
	d.nation = v.nation;
	d.province = v.province;
	d.month = month;
	d.year = int32_t(today.year);
	if(!v.nation || v.units <= 0.f || !state.exact_population)
		return;
	constexpr int32_t oldest = demography_view::oldest;

	// Each region of the country is a group. The month's growth of each row is
	// split the way the game applies it: a growing row adds newborns, a shrinking one loses people.
	std::vector<int32_t> group_of_pop(size_t(state.world.pop_size()), -1);
	std::vector<float> births_of;
	std::vector<float> deaths_of;
	for(auto own : state.world.nation_get_province_ownership(v.nation)) {
		auto const p = own.get_province().id;
		auto const group = int32_t(d.places.size());
		d.places.push_back(demography_view::place{ p });
		auto births = 0.f;
		auto deaths = 0.f;
		for(auto location : state.world.province_get_pop_location(p)) {
			auto const pop = location.get_pop().id;
			group_of_pop[size_t(pop.index())] = group;
			auto const change = demographics::get_monthly_pop_increase(state, pop);
			if(change > 0.f)
				births += change;
			else
				deaths -= change;
		}
		births_of.push_back(births);
		deaths_of.push_back(deaths);
	}
	auto const counted = persons::exact_population::count_ages(state, state.current_date, oldest, group_of_pop, int32_t(d.places.size()));
	auto const ours = [&](size_t group) {
		return !v.province || d.places[group].id == v.province;
	};
	std::vector<double> everyone(size_t(oldest) + 1, 0.0);
	std::vector<double> anchors(size_t(oldest) + 1, 0.0);
	std::vector<double> men(size_t(oldest) + 1, 0.0);
	for(size_t g = 0; g < d.places.size(); ++g) {
		auto& place = d.places[g];
		place.units = state.world.province_get_demographics(place.id, demographics::total);
		auto counted_here = 0.0;
		for(auto const c : counted.everyone[g])
			counted_here += c;
		if(counted_here > 0.0) {
			place.ages.resize(size_t(oldest) + 1);
			for(size_t a = 0; a < place.ages.size(); ++a)
				place.ages[a] = float(counted.everyone[g][a] * double(place.units) / counted_here);
		}
		// the total holds a fixed monthly rate, so the forecast of a region is a power of it
		if(place.units > 0.f) {
			auto const moved = (state.world.province_get_daily_net_migration(place.id) + state.world.province_get_daily_net_immigration(place.id)) * 30.f;
			auto const rate = std::max(-0.99, double(births_of[g] - deaths_of[g] + moved) / double(place.units));
			place.change = float(std::pow(1.0 + rate, 12.0 * demo_mid_years) - 1.0);
		}
		if(!ours(g))
			continue;
		for(size_t a = 0; a < everyone.size(); ++a) {
			everyone[a] += counted.everyone[g][a];
			anchors[a] += counted.anchors[g][a];
			men[a] += counted.men[g][a];
		}
		d.births += births_of[g];
		d.deaths += deaths_of[g];
	}
	auto counted_all = 0.0;
	for(auto const c : everyone)
		counted_all += c;
	if(counted_all <= 0.0)
		return;
	// in the units of the rest of the page: units for each person counted
	auto const per_person = double(v.units) / counted_all;
	std::vector<float> start(everyone.size());
	std::vector<float> start_anchors(everyone.size());
	std::vector<float> start_men(everyone.size());
	for(size_t a = 0; a < everyone.size(); ++a) {
		start[a] = float(everyone[a] * per_person);
		start_anchors[a] = float(anchors[a] * per_person);
		start_men[a] = float(men[a] * per_person);
	}

	// Who works for a wage and who serves, by age, from the people themselves.
	// Very long lists are read at a stride and each reading stands for the stride.
	auto const subject = [&](dcon::pop_id pop) {
		if(!pop || size_t(pop.index()) >= group_of_pop.size())
			return false;
		auto const g = group_of_pop[size_t(pop.index())];
		return g >= 0 && ours(size_t(g));
	};
	d.contracted.assign(size_t(oldest) + 1, 0.f);
	d.serving.assign(size_t(oldest) + 1, 0.f);
	auto const workers = economy::exact_person_economy::active_contract_workers(state);
	auto const worker_step = std::max<size_t>(1, workers.size() / 200000);
	for(size_t i = 0; i < workers.size(); i += worker_step) {
		auto const key = workers[i];
		if(!subject(persons::current_population(state, key)))
			continue;
		auto const age = persons::age_years(state, key, state.current_date);
		if(age >= 0)
			d.contracted[size_t(std::min(age, oldest))] += float(double(std::min(worker_step, workers.size() - i)) * per_person);
	}
	for(auto const& range : persons::exact_population::all_military_assignments(state)) {
		auto const step = std::max<uint64_t>(1, range.count / 2048);
		for(uint64_t i = 0; i < range.count; i += step) {
			persons::person_key const key{ range.source_population_cell, range.first_ordinal + i * uint64_t(range.ordinal_stride) };
			if(!persons::alive(state, key) || !subject(persons::current_population(state, key)))
				continue;
			auto const age = persons::age_years(state, key, state.current_date);
			if(age >= 0)
				d.serving[size_t(std::min(age, oldest))] += float(double(std::min(step, range.count - i)) * per_person);
		}
	}

	// The years ahead.
	auto const units = v.units;
	auto const natural_net = v.natural - v.starvation - v.overcrowding - v.transition;
	d.other = v.change_month - natural_net; // across the border, between regions, and the army
	d.spared = v.starvation + v.overcrowding;
	d.army = v.army;
	// Holds the monthly rates for the years ahead. Each year the year's
	// newborns become the youngest, and everyone else is a year older.
	// One in four newborns is a workforce anchor, as the catalog numbers them,
	// and a little more than half are boys.
	auto const run = [&](float born_rate, float death_rate, float other_rate, bool keep) {
		std::array<float, demography_view::horizon + 1> totals{};
		auto profile = start;
		auto anchor_profile = start_anchors;
		auto men_profile = start_men;
		totals[0] = units;
		if(keep) {
			d.ages.push_back(profile);
			d.anchors.push_back(anchor_profile);
			d.men.push_back(men_profile);
		}
		double born = 0.0, died = 0.0, moved = 0.0;
		auto const remains = std::max(0.f, 1.f - death_rate + other_rate);
		for(int32_t y = 1; y <= demography_view::horizon; ++y) {
			auto fresh = 0.f;
			for(int32_t m = 0; m < 12; ++m) {
				auto all = fresh;
				for(auto const p : profile)
					all += p;
				born += double(born_rate * all);
				died += double(death_rate * all);
				moved += double(other_rate * all);
				for(auto& p : profile)
					p *= remains;
				for(auto& p : anchor_profile)
					p *= remains;
				for(auto& p : men_profile)
					p *= remains;
				fresh = fresh * remains + born_rate * all;
			}
			for(auto* p : { &profile, &anchor_profile, &men_profile }) {
				p->back() += (*p)[p->size() - 2];
				for(size_t a = p->size() - 2; a > 0; --a)
					(*p)[a] = (*p)[a - 1];
			}
			profile[0] = fresh;
			anchor_profile[0] = fresh * 0.25f;
			men_profile[0] = fresh * 0.512f;
			for(auto const p : profile)
				totals[size_t(y)] += p;
			if(keep) {
				d.ages.push_back(profile);
				d.anchors.push_back(anchor_profile);
				d.men.push_back(men_profile);
				d.born[size_t(y)] = float(born);
				d.died[size_t(y)] = float(died);
				d.moved[size_t(y)] = float(moved);
			}
		}
		return totals;
	};
	d.total = run(d.births / units, d.deaths / units, d.other / units, true);
	// Without hunger, crowded housing and the army's losses: first fewer deaths, then more births.
	auto const fewer_deaths = std::min(d.deaths, d.spared);
	d.total_relieved = run((d.births + d.spared - fewer_deaths) / units, (d.deaths - fewer_deaths) / units, (d.other + d.army) / units, false);
	d.relief = (d.spared + d.army) * people_per_unit >= 1.f;
	for(size_t t = 0; t < d.ages.size(); ++t) {
		for(int32_t k = 0; k < demography_view::bands; ++k)
			d.grouped[t][size_t(k)] = demo_sum(d.ages[t], k * 5, k == demography_view::bands - 1 ? -1 : k * 5 + 5);
	}

	// Movement: the game's estimate across the border (for a country), and the
	// month's moves between its regions.
	if(!v.province) {
		std::vector<float> flows;
		demographics::estimate_directed_immigration(state, v.nation, flows);
		for(uint32_t i = 0; i < flows.size(); ++i) {
			if(!std::isfinite(flows[i]) || std::abs(flows[i]) * people_per_unit < 0.5f)
				continue;
			auto& into = flows[i] > 0.f ? d.abroad_in : d.abroad_out;
			into.push_back(demography_view::flow{ dcon::nation_id{ dcon::nation_id::value_base_t(i) }, dcon::province_id{}, flows[i] });
		}
	}
	for(auto const& r : v.regions) {
		auto const moved = state.world.province_get_daily_net_migration(r.id) * 30.f;
		if(std::abs(moved) * people_per_unit < 0.5f)
			continue;
		(moved > 0.f ? d.regions_in : d.regions_out).push_back(demography_view::flow{ dcon::nation_id{}, r.id, moved });
	}
	for(auto* list : { &d.abroad_in, &d.abroad_out, &d.regions_in, &d.regions_out })
		std::sort(list->begin(), list->end(), [](auto const& a, auto const& b) { return std::abs(a.amount) > std::abs(b.amount); });
}

// ---- the map ----

// Each page paints the map with its own figure.
struct demo_lens {
	std::string name;
	std::string note;
	metric_scale const* scale = nullptr;
	bool percent = false; // the bounds are parts of a whole
	bool signed_bounds = false;
};
bool demo_lens_on() const {
	return population_open && population_overview && population_tab == 1 && demography.ready && !demography.places.empty();
}
demo_lens lens_of_page(sys::state& state) const {
	// ages: from a pale teal of the young through steel to a muted gold of the old
	static metric_scale const median{ { 30.f, 36.f, 42.f, 48.f },
		{ ogl::color3f{ 0.30f, 0.50f, 0.50f }, ogl::color3f{ 0.29f, 0.41f, 0.47f }, ogl::color3f{ 0.30f, 0.33f, 0.42f }, ogl::color3f{ 0.45f, 0.40f, 0.36f }, ogl::color3f{ 0.62f, 0.52f, 0.36f } }, 0, 0.f };
	static metric_scale const change{ { -0.2f, -0.05f, 0.05f, 0.2f }, { map_loss, map_loss_soft, map_neutral, map_gain_soft, map_gain }, -1, -0.05f };
	static metric_scale const generation{ { 0.8f, 0.95f, 1.05f, 1.2f },
		{ ogl::color3f{ 0.18f, 0.20f, 0.27f }, ogl::color3f{ 0.25f, 0.28f, 0.40f }, ogl::color3f{ 0.33f, 0.38f, 0.52f }, ogl::color3f{ 0.47f, 0.52f, 0.64f }, ogl::color3f{ 0.66f, 0.69f, 0.77f } }, 0, 0.f };
	static metric_scale const old{ { 0.10f, 0.15f, 0.20f, 0.25f },
		{ ogl::color3f{ 0.30f, 0.50f, 0.50f }, ogl::color3f{ 0.29f, 0.41f, 0.47f }, ogl::color3f{ 0.30f, 0.33f, 0.42f }, ogl::color3f{ 0.45f, 0.40f, 0.36f }, ogl::color3f{ 0.62f, 0.52f, 0.36f } }, 0, 0.f };
	switch(demography_page) {
	case 1:
		return demo_lens{ filled(localized(state, "katechon_demo_lens_change", "Change by %1"), std::to_string(demography.year + demo_mid_years)), localized(state, "katechon_demo_lens_change_note", "at today's rates, region by region"), &change, true, true };
	case 2:
		return demo_lens{ filled(localized(state, "katechon_demo_lens_generation", "Born in %1"), std::to_string(demo_birth_year(demography_age))), localized(state, "katechon_demo_lens_generation_note", "share of the people, against the country's"), &generation, false, false };
	case 3:
		return demo_lens{ metric_name(state, population_metric::migration), metric_about(state, population_metric::migration), &scale_of(population_metric::migration), false, true };
	case 4:
		return demo_lens{ filled(localized(state, "katechon_demo_lens_old", "Ages %1 and older"), std::to_string(demo_work_to)), localized(state, "katechon_demo_lens_old_note", "share of the people"), &old, true, false };
	default:
		if(!demo_median_varies())
			return demo_lens{ metric_name(state, population_metric::density), localized(state, "katechon_demo_lens_density_note", "the regions do not differ in age"), &scale_of(population_metric::density), false, false };
		return demo_lens{ localized(state, "katechon_demo_median", "Median age"), localized(state, "katechon_demo_lens_median_note", "years, region by region"), &median, false, false };
	}
}
static size_t demo_class(metric_scale const& scale, float value) {
	size_t c = 0;
	while(c < scale.cuts.size() && value >= scale.cuts[c])
		++c;
	return c;
}
// Counts what the page needs and paints the map with it, when the map is the page's.
void repaint_demography(sys::state& state) {
	if(!population_open || !population_overview)
		return;
	if(population_tab == 1)
		gather_demography(state);
	if(population_layer)
		paint_layer(state);
}
// Whether the regions differ in median age by a year or more: otherwise its map says nothing.
bool demo_median_varies() const {
	auto lo = 1e9f;
	auto hi = -1e9f;
	for(auto const& place : demography.places) {
		if(place.ages.empty() || place.units <= 0.f)
			continue;
		auto const m = demo_median(place.ages);
		lo = std::min(lo, m);
		hi = std::max(hi, m);
	}
	return hi - lo >= 1.f;
}
// The figure of a region on the page's map, when the region is the country's.
std::optional<float> demo_lens_value(sys::state& state, dcon::province_id p) const {
	auto const& d = demography;
	if(demography_page == 3)
		return state.world.province_get_nation_from_province_ownership(p) == d.nation ? metric_value(state, d.nation, p, population_metric::migration) : std::nullopt;
	for(auto const& place : d.places) {
		if(place.id != p)
			continue;
		if(place.ages.empty() || place.units <= 0.f)
			return std::nullopt;
		switch(demography_page) {
		case 1:
			return place.change;
		case 2: {
			auto const country = d.ages.empty() ? 0.f : demo_members(demography_age, 0) / std::max(d.total[0], 1e-6f);
			auto const then = demography_age;
			if(then < 0 || then >= demography_view::oldest || country <= 0.f)
				return std::nullopt;
			return place.ages[size_t(then)] / place.units / country;
		}
		case 4:
			return demo_sum(place.ages, demo_work_to, -1) / place.units;
		default:
			if(!demo_median_varies())
				return metric_value(state, d.nation, p, population_metric::density);
			return demo_median(place.ages);
		}
	}
	return std::nullopt;
}

// The key of the page's map, and its name above the map.
void demo_map_key(sys::state& state, float x, float y, float w, float map_left, float map_right) {
	auto const lens = lens_of_page(state);
	constexpr float kh = 94.f;
	sheet(state, x, y, w, kh, panel_head, 0.92f);
	frame(state, x, y, w, kh, dip_rule, 1.f);
	panels.push_back(rect{ x, y, w, kh });
	sans(state, sans_fit(state, lens.name, 14, w - 28.f), x + 14.f, y + 18.f, 14, dip_name);
	if(!lens.scale)
		return;
	auto const cut = [&](float value) {
		if(lens.percent)
			return (lens.signed_bounds && value > 0.f ? "+" : "") + text::format_percentage(value, 0);
		if(lens.signed_bounds)
			return (value > 0.f ? "+" : "") + text::format_float(value, std::abs(value) < 1.f ? 1 : 0);
		return text::format_float(value, value < 2.f ? 2 : 0);
	};
	auto const sw = (w - 28.f) / 5.f;
	for(size_t c = 0; c < 5; ++c) {
		auto const cx = x + 14.f + sw * float(c);
		fill(state, cx, y + 34.f, sw - 2.f, 8.f, lens.scale->colours[c], 1.f);
		std::string label;
		if(c == 0)
			label = "< " + cut(lens.scale->cuts[0]);
		else if(c == 4)
			label = cut(lens.scale->cuts[3]) + "+";
		else
			label = cut(lens.scale->cuts[c - 1]) + " – " + cut(lens.scale->cuts[c]);
		sans_centered(state, sans_fit(state, label, 10, sw - 2.f), cx, sw - 2.f, y + 54.f, 10, dip_muted);
	}
	sans(state, sans_fit(state, lens.note, 11, w - 28.f), x + 14.f, y + 76.f, 11, dip_faint);
}

// ---- the overview's parts ----

// Where today's rates lead, in four lines; the tab has the rest.
void overview_outlook(sys::state& state, float left, float right, float top, float bottom, bool hovering, float mx, float my) {
	gather_demography(state);
	auto const& d = demography;
	auto y = top;
	pop_label(state, localized(state, "katechon_demo_outlook", "Outlook  ·  at today's rates"), left, y + 6.f);
	pop_link(state, localized(state, "katechon_demo_to_tab", "Demography  ›"), right, y + 6.f, action::population_tab, hovering, mx, my, true, 1);
	y += 26.f;
	if(d.ages.empty()) {
		sans(state, localized(state, "katechon_demo_no_ages", "The simulation keeps no ages for these people."), left, y + 12.f, 14, dip_faint);
		return;
	}
	constexpr int32_t soon = 10;
	constexpr int32_t later = demo_mid_years;
	auto const row = [&](std::string const& label, std::string const& value, ogl::color3f colour, std::string const& about) {
		if(y + 40.f > bottom)
			return;
		hit_target t{ left, y, right - left, 40.f, action::population_tab, nullptr, 1 };
		auto const lit = pop_over(t, hovering, mx, my);
		sans(state, label, left, y + 18.f, 14, lit ? dip_name : dip_text);
		sans(state, value, right, y + 18.f, 20, colour, true, 0.f);
		fill(state, left, y + 38.f, right - left, 1.f, dip_rule, 0.7f);
		if(lit)
			hint(about, t);
		pop_target(t);
		y += 44.f;
	};
	auto const total_tone = [&](int32_t years) {
		auto const change = d.total[size_t(years)] - d.total[0];
		return std::abs(change) * people_per_unit < 0.5f ? dip_text : (change < 0.f ? pop_bad : pop_good);
	};
	auto const ahead = localized(state, "katechon_demo_outlook_about", "Today's monthly rates held for the years ahead. The Demography tab shows them year by year.");
	row(filled(localized(state, "katechon_demo_population_in", "Population  ·  %1"), std::to_string(d.year + soon)), people(d.total[soon]) + "  " + demo_percent((d.total[soon] - d.total[0]) / std::max(d.total[0], 1e-6f)), total_tone(soon), ahead);
	row(filled(localized(state, "katechon_demo_population_in", "Population  ·  %1"), std::to_string(d.year + later)), people(d.total[later]) + "  " + demo_percent((d.total[later] - d.total[0]) / std::max(d.total[0], 1e-6f)), total_tone(later), ahead);
	auto const working = [&](int32_t years) {
		return demo_sum(d.ages[size_t(years)], demo_work_from, demo_work_to) / std::max(d.total[size_t(years)], 1e-6f);
	};
	row(filled(localized(state, "katechon_demo_working_now_then", "Working age, now and in %1"), std::to_string(d.year + later)), share_text(working(0)) + "  →  " + share_text(working(later)),
		working(later) < working(0) - 0.005f ? pop_bad : dip_text, ahead);
	row(filled(localized(state, "katechon_demo_median_now_then", "Median age, now and in %1"), std::to_string(d.year + later)), text::format_float(demo_median(d.ages[0]), 1) + "  →  " + text::format_float(demo_median(d.ages[later]), 1),
		dip_text, ahead);
}

// Where the people live: town and country, and the largest regions.
void overview_location(sys::state& state, float left, float right, float top, bool hovering, float mx, float my) {
	auto const& v = population;
	constexpr float gap = 40.f;
	auto const split = std::round(left + (right - left - gap) * 0.36f);
	pop_label(state, localized(state, "katechon_ppl_by_location", "Population by location"), left, top + 6.f);
	auto y = top + 30.f;
	if(v.urbanization >= 0.f) {
		auto const towns = std::clamp(v.urbanization, 0.f, 1.f);
		parts_bar(state, left, y, split - left, 4.f, { { towns, pop_wage }, { 1.f - towns, pop_own } });
		y += 24.f;
		auto x = left;
		x += demo_key(state, pop_wage, localized(state, "katechon_ppl_urban", "Urban") + "  " + share_text(towns) + "  (" + people(v.units * towns) + ")", x, y);
		y += 22.f;
		demo_key(state, pop_own, localized(state, "katechon_ppl_rural", "Rural") + "  " + share_text(1.f - towns) + "  (" + people(v.units * (1.f - towns)) + ")", left, y);
	} else {
		sans(state, localized(state, "katechon_ppl_no_record", "The simulation keeps no record of this here."), left, y + 4.f, 13, dip_faint);
	}
	// the largest regions, in two columns
	auto const rx = split + gap;
	auto const col = (right - rx - gap * 0.5f) * 0.5f;
	auto const total = std::max(v.units, 1e-6f);
	for(size_t i = 0; i < v.regions.size() && i < 6; ++i) {
		auto const& r = v.regions[i];
		auto const cx = rx + float(i / 3) * (col + gap * 0.5f);
		auto const cy = top + 6.f + float(i % 3) * 26.f;
		hit_target t{ cx - 4.f, cy - 11.f, col + 8.f, 24.f, action::population_region, nullptr, region_value(r.id) };
		auto const lit = pop_over(t, hovering, mx, my);
		auto const sw = sans(state, share_text(r.units / total), cx + col, cy, 13, dip_muted, true);
		auto const pw = sans(state, people(r.units), cx + col - sw - 16.f, cy, 13, dip_text, true);
		sans(state, sans_fit(state, r.name, 13, col - sw - pw - 30.f), cx, cy, 13, lit ? dip_name : dip_text);
		fill(state, cx, cy + 12.f, col, 1.f, dip_rule, 0.6f);
		if(lit)
			hint(localized(state, "katechon_ppl_region_hint", "Open it in the map inspector"), t);
		pop_target(t);
	}
}

// ---- drawing ----

// Four figures in a row on a quiet tile.
float demo_figures(sys::state& state, std::array<std::array<std::string, 3>, 4> const& cells, std::array<ogl::color3f, 4> const& colours, float left, float right, float y) {
	constexpr float bh = 88.f;
	fill(state, left, y, right - left, bh, pop_tile, 0.9f);
	auto const col = (right - left - 24.f) / 4.f;
	for(size_t i = 0; i < 4; ++i) {
		auto const cx = left + 24.f + float(i) * col;
		if(i > 0)
			fill(state, cx - 16.f, y + 18.f, 1.f, bh - 36.f, dip_rule, 1.f);
		sans(state, upper(cells[i][0]), cx, y + 22.f, 11, dip_muted, false, 0.14f);
		sans(state, sans_fit(state, cells[i][1], 30, col - 22.f, 0.f), cx - 1.f, y + 51.f, 30, colours[i], false, 0.f);
		sans(state, sans_fit(state, cells[i][2], 13, col - 22.f), cx, y + 74.f, 13, dip_faint);
	}
	return y + bh;
}

// A block's head: what kind of figure in small capitals, its name in the serif, and where it leads.
float demo_head(sys::state& state, std::string const& kind, std::string const& name, std::string const& link, int32_t page, float left, float right, float y, bool hovering, float mx, float my) {
	pop_label(state, kind, left, y + 6.f);
	draw_text(state, name, left, y + 30.f, 22, 0.004f, dip_name);
	if(!link.empty())
		pop_link(state, link, right, y + 30.f, action::demography_page, hovering, mx, my, true, page);
	return y + 52.f;
}

// A dot and its words; returns the width taken.
float demo_key(sys::state& state, ogl::color3f colour, std::string const& text, float x, float y) {
	fill(state, x, y - 3.f, 6.f, 6.f, colour, 1.f);
	return 14.f + sans(state, text, x + 12.f, y, 12, dip_muted) + 22.f;
}

// The foot of a page: one quiet line on what the figures rest on, and a way on.
// What the figures rest on is said in a hint, not written across the page.
void demo_foot(sys::state& state, std::string const& text, float left, float right, float bottom, bool hovering, float mx, float my, std::string const& link = std::string(), int32_t page = 0) {
	fill(state, left, bottom - 26.f, right - left, 1.f, dip_rule, 0.8f);
	auto const label = localized(state, "katechon_demo_about", "How these figures are made");
	hit_target t{ left - 4.f, bottom - 22.f, sans_w(state, label, 12) + 8.f, 22.f, action::none };
	auto const lit = pop_over(t, hovering, mx, my);
	auto const lw = sans(state, label, left, bottom - 10.f, 12, lit ? dip_text : dip_faint);
	fill(state, left, bottom - 2.f, lw, 1.f, lit ? dip_text : dip_faint, lit ? 0.8f : 0.35f);
	if(lit)
		hint(text, t);
	if(!link.empty())
		pop_link(state, link, right, bottom - 10.f, action::demography_page, hovering, mx, my, true, page);
}
// A change as a part of today, in whole percents unless it is small.
static std::string demo_percent(float part) {
	auto const digits = std::abs(part) < 0.01f ? 1 : 0;
	return (part > 0.f ? "+" : "") + text::format_float(part * 100.f, size_t(digits)) + "%";
}

// The page's back link.
void demo_back(sys::state& state, float left, float top, bool hovering, float mx, float my) {
	pop_link(state, localized(state, "katechon_demo_back", "‹  Demography"), left, top + 2.f, action::demography_page, hovering, mx, my, false, 0);
}

// The people by age in five-year groups, the oldest at the top: men to the
// left of the ages, women to the right. A press follows a generation.
void demo_profile(sys::state& state, float left, float right, float top, float bottom, bool hovering, float mx, float my) {
	auto const& d = demography;
	auto const& now = d.ages[0];
	auto const& men_now = d.men[0];
	auto const all = std::max(d.total[0], 1e-6f);
	constexpr float key_h = 52.f;
	constexpr float head_h = 22.f;
	auto const row_h = std::floor((bottom - top - key_h - head_h) / float(demography_view::bands));
	auto const rows_top = top + head_h;
	std::array<float, demography_view::bands> men{};
	std::array<float, demography_view::bands> women{};
	auto largest = 0.f;
	for(int32_t k = 0; k < demography_view::bands; ++k) {
		auto const to = k == demography_view::bands - 1 ? -1 : k * 5 + 5;
		men[size_t(k)] = demo_sum(men_now, k * 5, to);
		women[size_t(k)] = std::max(0.f, demo_sum(now, k * 5, to) - men[size_t(k)]);
		largest = std::max({ largest, men[size_t(k)], women[size_t(k)] });
	}
	constexpr float label_w = 50.f;
	auto const mid = std::round((left + right) * 0.5f);
	auto const half = mid - label_w * 0.5f - 6.f - left;
	auto const men_all = demo_sum(men_now, 0, -1);
	// rounded so that the two make a hundred
	auto const men_percent = std::clamp(int32_t(std::lround(men_all / all * 100.f)), 0, 100);
	sans(state, upper(localized(state, "katechon_demo_men", "Men")) + "  " + std::to_string(men_percent) + "%", mid - label_w * 0.5f - 6.f, top + 8.f, 11, demo_men, true, 0.12f);
	sans(state, upper(localized(state, "katechon_demo_women", "Women")) + "  " + std::to_string(100 - men_percent) + "%", mid + label_w * 0.5f + 6.f, top + 8.f, 11, demo_women, false, 0.12f);
	auto const chosen_band = demography_age >= 0 ? std::min(demography_age / 5, demography_view::bands - 1) : -1;
	for(int32_t k = 0; k < demography_view::bands; ++k) {
		auto const ry = rows_top + float(demography_view::bands - 1 - k) * row_h;
		auto const cy = std::round(ry + row_h * 0.5f);
		auto const chosen = k == chosen_band;
		auto const open = k < demography_view::bands - 1;
		hit_target t{ left - 6.f, ry, right - left + 12.f, row_h, open ? action::demography_generation : action::none, nullptr, k * 5 + 2 };
		auto const hovered = pop_over(t, hovering, mx, my);
		if(chosen || (hovered && open))
			fill(state, t.x, ry, t.w, row_h, dip_bronze, chosen ? 0.12f : 0.06f);
		sans_centered(state, demo_band_name(k), mid - label_w * 0.5f, label_w, cy, 11, chosen ? dip_name : dip_faint);
		auto const bar = std::max(3.f, std::min(6.f, row_h - 8.f));
		if(largest > 0.f) {
			auto const mw = std::round(half * men[size_t(k)] / largest);
			auto const ww = std::round(half * women[size_t(k)] / largest);
			if(mw > 0.f)
				fill(state, mid - label_w * 0.5f - 6.f - mw, cy - bar * 0.5f, mw, bar, demo_men, chosen || hovered ? 1.f : 0.8f);
			if(ww > 0.f)
				fill(state, mid + label_w * 0.5f + 6.f, cy - bar * 0.5f, ww, bar, demo_women, chosen || hovered ? 1.f : 0.8f);
		}
		if(open)
			pop_target(t);
		if(hovered)
			hint(filled(localized(state, "katechon_demo_band_hint", "Ages %1: %2 of the people."), demo_band_name(k), share_text((men[size_t(k)] + women[size_t(k)]) / all))
				+ "  " + filled(localized(state, "katechon_demo_band_sexes", "Men %1, women %2."), people(men[size_t(k)]), people(women[size_t(k)]))
				+ (open ? " " + filled(localized(state, "katechon_demo_band_follow", "Born %1. Press to follow this generation."), std::to_string(demo_birth_year(k * 5 + 4)) + "–" + std::to_string(demo_birth_year(k * 5))) : std::string()), t);
	}
	// the axis of people under each side
	auto const axis_y = rows_top + row_h * float(demography_view::bands) + 4.f;
	fill(state, left, axis_y, mid - label_w * 0.5f - 6.f - left, 1.f, dip_rule, 1.f);
	fill(state, mid + label_w * 0.5f + 6.f, axis_y, right - mid - label_w * 0.5f - 6.f, 1.f, dip_rule, 1.f);
	sans(state, people(largest), left, axis_y + 12.f, 10, dip_faint);
	sans(state, people(largest), right, axis_y + 12.f, 10, dip_faint, true);
	// the three stages of life, with their shares
	auto const y = bottom - 10.f;
	sans(state, filled(localized(state, "katechon_demo_children_line", "Children under %1: %2"), std::to_string(demo_work_from), share_text(demo_sum(now, 0, demo_work_from) / all)), left, y, 12, dip_muted);
}

// The total over the years ahead: today's rates, and the same without the losses.
// With a year chosen, a hairline marks it and each year can be pressed.
void demo_chart(sys::state& state, float left, float top, float right, float bottom, bool inspect, bool hovering, float mx, float my) {
	auto const& d = demography;
	constexpr int32_t n = demography_view::horizon + 1;
	// the scale is today's rates'; the second line may leave it, and says where it ends
	auto lo = d.total[0];
	auto hi = d.total[0];
	for(int32_t i = 0; i < n; ++i) {
		lo = std::min(lo, d.total[size_t(i)]);
		hi = std::max(hi, d.total[size_t(i)]);
	}
	// round bounds and three steps between them, in people
	auto const span_people = std::max(double(hi - lo) * double(people_per_unit), double(hi) * double(people_per_unit) * 0.04);
	auto const raw_tick = span_people / 3.0;
	auto const magnitude = std::pow(10.0, std::floor(std::log10(std::max(raw_tick, 1.0))));
	auto tick = magnitude;
	for(auto const m : { 1.0, 2.0, 2.5, 5.0, 10.0 }) {
		if(magnitude * m >= raw_tick) {
			tick = magnitude * m;
			break;
		}
	}
	auto const lo_people = std::floor(double(lo) * double(people_per_unit) / tick) * tick;
	auto ticks = int32_t(std::ceil((double(hi) * double(people_per_unit) - lo_people) / tick));
	ticks = std::max(ticks, 1);
	lo = float(lo_people / double(people_per_unit));
	hi = float((lo_people + tick * double(ticks)) / double(people_per_unit));
	auto const px = left + 58.f;
	auto const pw = right - px;
	auto const ph = bottom - top - 24.f;
	for(int32_t i = 0; i <= ticks; ++i) {
		auto const gy = std::round(top + ph * (1.f - float(i) / float(ticks)));
		fill(state, px, gy, pw, 1.f, dip_rule, i == 0 ? 1.f : 0.5f);
		sans(state, people(lo + (hi - lo) * float(i) / float(ticks)), px - 10.f, gy, 11, dip_faint, true);
	}
	for(int32_t i = 0; i < n; i += 10)
		sans_centered(state, std::to_string(d.year + i), px + pw * float(i) / float(n - 1) - 30.f, 60.f, bottom - 8.f, 11, dip_faint);
	auto const plot = [&](std::array<float, demography_view::horizon + 1> const& series, ogl::lines& line, ogl::color3f colour, float alpha) {
		std::array<float, n> points{};
		for(int32_t i = 0; i < n; ++i)
			points[size_t(i)] = std::clamp((series[size_t(i)] - lo) / (hi - lo), 0.f, 1.f);
		line.set_y(points.data());
		ogl::render_linegraph(state, ogl::color_modification::none, px, top, pw, ph, colour.r, colour.g, colour.b, alpha, line);
	};
	if(d.relief && demo_relief_fits(lo, hi))
		plot(d.total_relieved, demo_relief_line, demo_young, 0.7f);
	plot(d.total, demo_line, dip_bronze, 1.f);
	auto const at = [&](int32_t i, float value) {
		return glm::vec2(px + pw * float(i) / float(n - 1), top + ph * (1.f - (value - lo) / (hi - lo)));
	};
	auto const first = at(0, d.total[0]);
	fill(state, first.x - 3.f, first.y - 3.f, 6.f, 6.f, dip_name, 1.f);
	if(!inspect)
		return;
	auto const chosen = at(demography_year, d.total[size_t(demography_year)]);
	fill(state, chosen.x, top, 1.f, ph, dip_name, 0.35f);
	fill(state, chosen.x - 3.f, chosen.y - 3.f, 6.f, 6.f, dip_name, 1.f);
	auto const step = pw / float(n - 1);
	for(int32_t i = 1; i < n; ++i) {
		hit_target t{ px + step * (float(i) - 0.5f), top, step, ph, action::demography_year, nullptr, i };
		if(pop_over(t, hovering, mx, my)) {
			fill(state, px + step * float(i), top, 1.f, ph, dip_bronze, 0.30f);
			hint(std::to_string(d.year + i) + ":  " + people(d.total[size_t(i)]), t);
		}
		pop_target(t);
	}
}

// Whether the second line stays within a scale; when it does not, only its key is shown, with where it ends.
bool demo_relief_fits(float lo, float hi) const {
	for(auto const v : demography.total_relieved)
		if(v < lo || v > hi)
			return false;
	return true;
}
std::string demo_relief_key(sys::state& state) const {
	auto const& d = demography;
	return filled(localized(state, "katechon_demo_relief_end", "Without hunger, crowding and war: %1 in %2"), people(d.total_relieved[size_t(demography_view::horizon)]), std::to_string(d.year + demography_view::horizon));
}

// Generations through time: a column for each year, a row for each five-year
// age group, lighter where more people are. A generation runs up a diagonal.
void demo_heatmap(sys::state& state, float left, float top, float right, float bottom, bool inspect, bool hovering, float mx, float my) {
	auto const& d = demography;
	constexpr int32_t n = demography_view::horizon + 1;
	constexpr int32_t rows = demography_view::bands - 1; // 0-4 ... 85-89: the open group of the oldest gathers everyone past it
	auto const px = left + 34.f;
	auto const pw = right - px;
	auto const ph = bottom - top - 24.f;
	auto const cw = pw / float(n);
	auto const rh = ph / float(rows);
	// each group against the average group of its year: smaller ones cold, larger ones warm
	std::array<float, demography_view::horizon + 1> average{};
	for(size_t t = 0; t < d.grouped.size(); ++t) {
		for(int32_t k = 0; k < rows; ++k)
			average[t] += d.grouped[t][size_t(k)];
		average[t] /= float(rows);
	}
	for(int32_t t = 0; t < n; ++t) {
		auto const x = std::floor(px + cw * float(t));
		auto const x_end = std::floor(px + cw * float(t + 1));
		for(int32_t k = 0; k < rows; ++k) {
			auto const y = std::floor(top + rh * float(rows - 1 - k));
			auto const y_end = std::floor(top + rh * float(rows - k));
			auto const ratio = average[size_t(t)] > 0.f ? d.grouped[size_t(t)][size_t(k)] / average[size_t(t)] : 0.f;
			auto const part = std::clamp((ratio - 0.4f) / 1.2f, 0.f, 1.f);
			auto const mixed = [&](ogl::color3f a, ogl::color3f b, float x) { return ogl::color3f{ a.r + (b.r - a.r) * x, a.g + (b.g - a.g) * x, a.b + (b.b - a.b) * x }; };
			auto const c = part < 0.5f ? mixed(demo_few, demo_even, part * 2.f) : mixed(demo_even, demo_many, (part - 0.5f) * 2.f);
			fill(state, x, y, x_end - x, y_end - y, c, 1.f);
			if(!inspect)
				continue;
			// the generation in the middle of this group this year
			auto const age = std::clamp(k * 5 + 2 - t, -demography_view::horizon, demography_view::oldest - 1);
			hit_target cell{ x, y, x_end - x, y_end - y, action::demography_generation, nullptr, age };
			if(pop_over(cell, hovering, mx, my)) {
				frame(state, x, y, x_end - x, y_end - y, dip_name, 0.8f);
				hint(std::to_string(d.year + t) + "  ·  " + filled(localized(state, "katechon_demo_band_hint", "Ages %1: %2 of the people."), demo_band_name(k), people(d.grouped[size_t(t)][size_t(k)]))
					+ " " + filled(localized(state, "katechon_demo_band_follow", "Born %1. Press to follow this generation."), std::to_string(demo_birth_year(age))), cell);
			}
			pop_target(cell);
		}
	}
	for(int32_t age = 0; age <= 80; age += 20)
		sans(state, std::to_string(age), px - 8.f, std::round(top + ph - rh * (float(age) / 5.f + 0.5f)), 11, dip_faint, true);
	for(int32_t i = 0; i < n; i += 10)
		sans_centered(state, std::to_string(d.year + i), px + cw * (float(i) + 0.5f) - 30.f, 60.f, bottom - 8.f, 11, dip_faint);
	// the generations followed, along their diagonals
	auto const top_age = float(rows * 5);
	auto const diagonal = [&](int32_t age, ogl::color3f colour) {
		std::array<float, n> points{};
		for(int32_t t = 0; t < n; ++t)
			points[size_t(t)] = std::clamp((float(age + t) + 0.5f) / top_age, 0.f, 1.f);
		demo_cohort_line.set_y(points.data());
		ogl::render_linegraph(state, ogl::color_modification::none, px + cw * 0.5f, top, pw - cw, ph, colour.r, colour.g, colour.b, 0.9f, demo_cohort_line);
		fill(state, px + cw * 0.5f - 3.f, top + ph * (1.f - points[0]) - 3.f, 6.f, 6.f, colour, 1.f);
	};
	if(demography_compare != demo_no_compare)
		diagonal(demography_compare, demo_second);
	diagonal(demography_age, dip_name);
}

// The generation followed, in one strip: its year of birth and its ages now and later.
void demo_generation_strip(sys::state& state, float left, float right, float y) {
	auto const& d = demography;
	pop_label(state, localized(state, "katechon_demo_selected", "Selected generation"), left, y + 6.f);
	draw_text(state, filled(localized(state, "katechon_demo_born_in", "Born in %1"), std::to_string(demo_birth_year(demography_age))), left, y + 34.f, 22, 0.004f, dip_name);
	auto const col = (right - left - 190.f) / 3.f;
	std::array<int32_t, 3> const at{ 0, 20, demography_view::horizon };
	for(size_t i = 0; i < 3; ++i) {
		auto const cx = left + 190.f + float(i) * col;
		pop_label(state, i == 0 ? localized(state, "katechon_demo_age_now", "Age now") : filled(localized(state, "katechon_demo_at_year", "At %1"), std::to_string(d.year + at[i])), cx, y + 6.f);
		auto const age = demography_age + at[i];
		draw_text(state, age < 0 ? std::string("—") : filled(localized(state, "katechon_demo_years_short", "%1 years"), std::to_string(age)), cx, y + 34.f, 20, 0.004f, dip_text);
		sans(state, people(demo_members(demography_age, at[i])), cx, y + 56.f, 12, dip_faint);
	}
}

// Who comes and who leaves, in one line: the largest on each side.
void demo_migration_strip(sys::state& state, float left, float right, float y, bool hovering, float mx, float my) {
	auto const& v = population;
	auto const& d = demography;
	pop_label(state, localized(state, "katechon_demo_movement", "Movement of people"), left, y + 6.f);
	draw_text(state, localized(state, "katechon_demo_migration", "Migration flows"), left, y + 34.f, 22, 0.004f, dip_name);
	pop_link(state, localized(state, "katechon_demo_explore", "Explore  ›"), right, y + 34.f, action::demography_page, hovering, mx, my, true, 3);
	auto const name_of = [&](demography_view::flow const& f) {
		return f.region ? text::get_name_as_string(state, f.region) : text::get_name_as_string(state, dcon::fatten(state.world, f.nation));
	};
	auto const& in = v.province ? d.regions_in : d.abroad_in;
	auto const& out = v.province ? d.regions_out : d.abroad_out;
	auto const cy = y + 66.f;
	auto x = left;
	x += sans(state, v.province ? localized(state, "katechon_demo_arrivals", "Arrivals") : localized(state, "katechon_demo_expected_in", "Expected arrivals"), x, cy, 13, demo_young) + 8.f;
	if(in.empty())
		x += sans(state, "—", x, cy, 12, dip_faint) + 10.f;
	if(!in.empty())
		x += sans(state, sans_fit(state, name_of(in[0]) + " " + signed_count(in[0].amount), 12, 150.f), x, cy, 12, dip_muted) + 10.f;
	x += sans(state, "›", x, cy, 13, dip_bronze) + 10.f;
	x += sans(state, upper(v.title), x, cy, 12, dip_name, false, 0.12f) + 10.f;
	x += sans(state, "›", x, cy, 13, dip_bronze) + 10.f;
	x += sans(state, v.province ? localized(state, "katechon_demo_departures", "Departures") : localized(state, "katechon_demo_expected_out", "Expected departures"), x, cy, 13, pop_bad) + 8.f;
	if(out.empty())
		sans(state, "—", x, cy, 12, dip_faint);
	else
		sans(state, sans_fit(state, name_of(out[0]) + " " + signed_count(out[0].amount), 12, std::max(40.f, right - x)), x, cy, 12, dip_muted);
}

void draw_demography(sys::state& state, float left, float right, float top, float bottom, bool hovering, float mx, float my) {
	gather_demography(state);
	auto const& d = demography;
	if(d.ages.empty()) {
		fill(state, left, top, right - left, 1.f, dip_rule, 1.f);
		sans(state, localized(state, "katechon_demo_no_ages", "The simulation keeps no ages for these people."), left, top + 34.f, 16, dip_faint);
		return;
	}
	demography_year = std::clamp(demography_year, 1, demography_view::horizon);
	demography_age = std::clamp(demography_age, -demography_view::horizon, demography_view::oldest - 1);
	switch(demography_page) {
	case 1: demo_forecast_page(state, left, right, top, bottom, hovering, mx, my); break;
	case 2: demo_generations_page(state, left, right, top, bottom, hovering, mx, my); break;
	case 3: demo_movement_page(state, left, right, top, bottom, hovering, mx, my); break;
	case 4: demo_groups_page(state, left, right, top, bottom, hovering, mx, my); break;
	default: demo_main_page(state, left, right, top, bottom, hovering, mx, my); break;
	}
}

// ---- the profile, with the outlook beside it ----
void demo_main_page(sys::state& state, float left, float right, float top, float bottom, bool hovering, float mx, float my) {
	auto const& d = demography;
	auto const& now = d.ages[0];
	auto const all = std::max(d.total[0], 1e-6f);
	auto y = demo_figures(state,
		{ std::array<std::string, 3>{ localized(state, "katechon_ppl_population", "Population"), people(all), filled(localized(state, "katechon_demo_in_year", "In %1"), std::to_string(d.year)) },
			std::array<std::string, 3>{ localized(state, "katechon_demo_median", "Median age"), text::format_float(demo_median(now), 1), localized(state, "katechon_demo_median_note", "half are older") },
			std::array<std::string, 3>{ filled(localized(state, "katechon_demo_working_ages", "Ages %1"), demo_range(demo_work_from, demo_work_to)), share_text(demo_sum(now, demo_work_from, demo_work_to) / all), localized(state, "katechon_demo_working", "Working age") },
			std::array<std::string, 3>{ filled(localized(state, "katechon_demo_old_ages", "Ages %1 and older"), std::to_string(demo_work_to)), share_text(demo_sum(now, demo_work_to, -1) / all), localized(state, "katechon_demo_old", "Past working age") } },
		{ dip_name, dip_text, dip_text, dip_text }, left, right, top);
	y += 24.f;
	constexpr float gap = 44.f;
	constexpr float strip_h = 96.f;
	auto const split = std::round(left + (right - left - gap) * 0.44f);
	auto const lower = bottom - 26.f - strip_h;
	// the profile
	{
		auto const body = demo_head(state, localized(state, "katechon_demo_profile_kind", "Age and life stage  ·  five-year groups"), localized(state, "katechon_demo_profile", "Age profile"),
			std::string(), 0, left, split, y, hovering, mx, my);
		demo_profile(state, left, split, body, lower - 18.f, hovering, mx, my);
	}
	// the forecast and the generations
	{
		auto const rx = split + gap;
		auto const half = std::round((lower - 18.f - y) * 0.5f);
		auto body = demo_head(state, filled(localized(state, "katechon_demo_forecast_kind", "%1  ·  at today's rates"), std::to_string(d.year) + "–" + std::to_string(d.year + demography_view::horizon)),
			localized(state, "katechon_demo_forecast", "Population forecast"), localized(state, "katechon_demo_open_forecast", "Open forecast  ›"), 1, rx, right, y, hovering, mx, my);
		auto const chart_bottom = y + half - 30.f;
		demo_chart(state, rx, body, right, chart_bottom, false, hovering, mx, my);
		{
			hit_target t{ rx, body, right - rx, chart_bottom - body, action::demography_page, nullptr, 1 };
			if(pop_over(t, hovering, mx, my))
				hint(localized(state, "katechon_demo_open_forecast_hint", "Open the forecast, year by year"), t);
			pop_target(t);
		}
		auto kx = rx + 58.f;
		kx += demo_key(state, dip_bronze, localized(state, "katechon_demo_line_today", "At today's rates"), kx, chart_bottom + 12.f);
		if(d.relief)
			demo_key(state, demo_young, demo_relief_key(state), kx, chart_bottom + 12.f);
		auto const gy = y + half + 8.f;
		fill(state, rx, gy - 10.f, right - rx, 1.f, dip_rule, 0.8f);
		body = demo_head(state, localized(state, "katechon_demo_generations_kind", "Age groups through the years"), localized(state, "katechon_demo_generations", "Generational history"),
			localized(state, "katechon_demo_open_generations", "Open generations  ›"), 2, rx, right, gy, hovering, mx, my);
		demo_heatmap(state, rx, body, right, lower - 18.f, false, hovering, mx, my);
		hit_target t{ rx, body, right - rx, lower - 18.f - body, action::demography_page, nullptr, 2 };
		if(pop_over(t, hovering, mx, my))
			hint(localized(state, "katechon_demo_open_generations_hint", "Follow a generation through the years"), t);
		pop_target(t);
	}
	// the generation followed, and movement
	fill(state, left, lower - 4.f, right - left, 1.f, dip_rule, 1.f);
	demo_generation_strip(state, left, split, lower + 12.f);
	fill(state, split + gap * 0.5f, lower + 14.f, 1.f, strip_h - 24.f, dip_rule, 1.f);
	demo_migration_strip(state, split + gap, right, lower + 12.f, hovering, mx, my);
	demo_foot(state, localized(state, "katechon_demo_foot_main", "Ages are counted person by person. The years ahead hold today's monthly rates."), left, right, bottom, hovering, mx, my,
		localized(state, "katechon_demo_open_groups", "Indicators by age group  ›"), 4);
}

// ---- the forecast, year by year ----
void demo_forecast_page(sys::state& state, float left, float right, float top, float bottom, bool hovering, float mx, float my) {
	auto const& d = demography;
	auto const& v = population;
	constexpr int32_t mid_years = demo_mid_years;
	auto const mid = d.total[mid_years];
	auto const end = d.total[demography_view::horizon];
	auto const change = mid - d.total[0];
	demo_back(state, left, top, hovering, mx, my);
	auto const end_year = std::to_string(d.year + demography_view::horizon);
	auto const& end_ages = d.ages[size_t(demography_view::horizon)];
	auto const end_working = demo_sum(end_ages, demo_work_from, demo_work_to);
	auto const end_dependants = demo_sum(end_ages, 0, demo_work_from) + demo_sum(end_ages, demo_work_to, -1);
	auto const end_recruits = std::max(0.f, demo_sum(d.anchors[size_t(demography_view::horizon)], demo_recruit_from, demo_work_to) - demo_sum(d.serving, 0, -1));
	auto const end_change = end - d.total[0];
	auto y = demo_figures(state,
		{ std::array<std::string, 3>{ filled(localized(state, "katechon_demo_projected", "Projected  ·  %1"), end_year), people(end), demo_percent(end_change / std::max(d.total[0], 1e-6f)) + " " + filled(localized(state, "katechon_demo_since", "since %1"), std::to_string(d.year)) },
			std::array<std::string, 3>{ filled(localized(state, "katechon_demo_median_in", "Median age  ·  %1"), end_year), text::format_float(demo_median(end_ages), 1), filled(localized(state, "katechon_demo_now_is", "now %1"), text::format_float(demo_median(d.ages[0]), 1)) },
			std::array<std::string, 3>{ filled(localized(state, "katechon_demo_dependency_in", "Dependency  ·  %1"), end_year), end_working > 0.f ? std::to_string(int32_t(std::lround(end_dependants / end_working * 100.f))) + " / 100" : std::string("—"), localized(state, "katechon_demo_dependency_short", "dependants per hundred") },
			std::array<std::string, 3>{ filled(localized(state, "katechon_demo_recruits_in", "Potential recruits  ·  %1"), end_year), people(end_recruits), localized(state, "katechon_demo_line_today", "At today's rates") } },
		{ end_change < 0.f ? pop_bad : (end_change > 0.f ? pop_good : dip_text), dip_text, dip_text, dip_text }, left, right, top + 22.f);
	y += 24.f;
	constexpr float gap = 44.f;
	constexpr float why_h = 120.f;
	auto const split = std::round(left + (right - left - gap) * 0.62f);
	auto const lower = bottom - 26.f - why_h;
	// the chart
	{
		auto const body = demo_head(state, std::to_string(d.year) + "–" + std::to_string(d.year + demography_view::horizon), filled(localized(state, "katechon_demo_future_of", "The future of %1"), v.title), std::string(), 0, left, split, y, hovering, mx, my);
		demo_chart(state, left, body, split, lower - 40.f, true, hovering, mx, my);
		auto kx = left + 58.f;
		kx += demo_key(state, dip_bronze, localized(state, "katechon_demo_line_today", "At today's rates"), kx, lower - 22.f);
		if(d.relief)
			demo_key(state, demo_young, demo_relief_key(state), kx, lower - 22.f);
	}
	// the year inspected
	auto const rx = split + gap;
	auto const t = demography_year;
	auto const& p = d.ages[size_t(t)];
	auto const total = std::max(d.total[size_t(t)], 1e-6f);
	{
		pop_label(state, localized(state, "katechon_demo_inspector", "Forecast inspector"), rx, y + 6.f);
		draw_text(state, filled(localized(state, "katechon_demo_year", "Year %1"), std::to_string(d.year + t)), rx, y + 30.f, 22, 0.004f, dip_name);
		for(int32_t dir : { -1, 1 }) {
			hit_target arrow{ dir < 0 ? right - 52.f : right - 24.f, y + 17.f, 26.f, 26.f, action::demography_year, nullptr, t + dir };
			auto const lit = pop_over(arrow, hovering, mx, my);
			draw_icon(state, dir < 0 ? icon::chevron_left : icon::chevron_right, arrow.x + 13.f, arrow.y + 13.f, 10.f, lit ? dip_name : dip_bronze);
			pop_target(arrow);
		}
		auto ry = y + 62.f;
		sans(state, people(total), rx, ry + 22.f, 40, dip_name, false, 0.f);
		auto const since = total - d.total[0];
		sans(state, filled(localized(state, since < 0.f ? "katechon_demo_fewer" : "katechon_demo_more", since < 0.f ? "%1 fewer than in %2" : "%1 more than in %2"), people(std::abs(since)), std::to_string(d.year)), rx, ry + 56.f, 13,
			since < 0.f ? pop_bad : pop_good);
		ry += 72.f;
		auto const young = demo_sum(p, 0, demo_work_from);
		auto const working = demo_sum(p, demo_work_from, demo_work_to);
		auto const old = demo_sum(p, demo_work_to, -1);
		auto const recruits = std::max(0.f, demo_sum(d.anchors[size_t(t)], demo_recruit_from, demo_work_to) - demo_sum(d.serving, 0, -1));
		auto const row = [&](std::string const& label, std::string const& value, std::string const& about) {
			hit_target r{ rx, ry, right - rx, 32.f, action::none };
			sans(state, label, rx, ry + 15.f, 13, dip_text);
			sans(state, value, right, ry + 15.f, 15, dip_name, true);
			fill(state, rx, ry + 31.f, right - rx, 1.f, dip_rule, 0.7f);
			if(!about.empty() && pop_over(r, hovering, mx, my))
				hint(about, r);
			ry += 33.f;
		};
		row(filled(localized(state, "katechon_demo_children_ages", "Children  ·  0–%1"), std::to_string(demo_work_from - 1)), people(young) + "   " + share_text(young / total), std::string());
		row(filled(localized(state, "katechon_demo_working_row", "Working age  ·  %1"), demo_range(demo_work_from, demo_work_to)), people(working) + "   " + share_text(working / total), std::string());
		row(filled(localized(state, "katechon_demo_old_row", "%1 and older"), std::to_string(demo_work_to)), people(old) + "   " + share_text(old / total), std::string());
		row(localized(state, "katechon_demo_dependency", "Dependency ratio"), working > 0.f ? std::to_string(int32_t(std::lround((young + old) / working * 100.f))) + " / 100" : std::string("—"),
			localized(state, "katechon_demo_dependency_about", "Children and people past working age for every hundred of working age."));
		row(localized(state, "katechon_demo_median", "Median age"), demo_years(state, demo_median(p)), std::string());
		row(localized(state, "katechon_demo_recruits", "Potential recruits"), people(recruits),
			localized(state, "katechon_demo_recruits_about", "The workforce of 18 to 64 that land forces recruit from, less those serving today."));
	}
	// why it changes, from now to the year inspected; or what the forecast holds
	{
		fill(state, left, lower - 4.f, right - left, 1.f, dip_rule, 1.f);
		auto wy = lower + 10.f;
		auto const kind = demography_inputs ? localized(state, "katechon_demo_inputs_kind", "A month, held for every year ahead")
			: filled(localized(state, "katechon_demo_why_kind", "%1  ·  the sum of the months"), std::to_string(d.year) + "–" + std::to_string(d.year + t));
		pop_label(state, kind, left, wy + 6.f);
		draw_text(state, demography_inputs ? localized(state, "katechon_demo_inputs", "Scenario inputs") : localized(state, "katechon_demo_why", "Why the future changes"), left, wy + 30.f, 22, 0.004f, dip_name);
		pop_link(state, demography_inputs ? localized(state, "katechon_demo_show_why", "Why the future changes  ›") : localized(state, "katechon_demo_show_inputs", "Scenario inputs  ›"), right, wy + 30.f, action::demography_inputs, hovering, mx, my, true, demography_inputs ? 0 : 1);
		wy += 52.f;
		struct part {
			std::string label;
			float amount;
			std::string note;
		};
		std::vector<part> parts;
		if(demography_inputs) {
			auto const per_thousand = [&](float amount) {
				return text::format_float(amount / std::max(d.total[0], 1e-6f) * 1000.f, 2) + " ‰";
			};
			parts = { part{ localized(state, "katechon_demo_in_births", "Births in growing groups"), d.births, per_thousand(d.births) },
				part{ localized(state, "katechon_demo_in_deaths", "Losses in shrinking groups"), -d.deaths, per_thousand(d.deaths) },
				part{ localized(state, "katechon_demo_in_moves", "Migration and the army"), d.other, per_thousand(std::abs(d.other)) },
				part{ localized(state, "katechon_demo_in_relief", "Hunger and crowding, lifted in the second line"), -(d.spared + d.army), per_thousand(d.spared + d.army) } };
		} else {
			parts = { part{ localized(state, "katechon_demo_births", "Births"), d.born[size_t(t)], std::string() }, part{ localized(state, "katechon_demo_deaths", "Deaths"), -d.died[size_t(t)], std::string() },
				part{ localized(state, "katechon_demo_moves", "Migration and war"), d.moved[size_t(t)], std::string() } };
		}
		auto largest = 1e-6f;
		for(auto const& q : parts)
			largest = std::max(largest, std::abs(q.amount));
		auto const col = (right - left - gap) * 0.5f;
		for(size_t i = 0; i < parts.size(); ++i) {
			auto const cx = left + float(i % 2) * (col + gap);
			auto const cy = wy + float(i / 2) * 30.f + 10.f;
			auto const tone = std::abs(parts[i].amount) * people_per_unit < 0.5f ? dip_faint : (parts[i].amount > 0.f ? pop_good : pop_bad);
			auto const label_w = demography_inputs ? 230.f : 150.f;
			constexpr float value_w = 150.f;
			sans(state, sans_fit(state, parts[i].label, 13, label_w - 8.f), cx, cy, 13, i == 3 && !demography_inputs ? dip_name : dip_text);
			auto const bw = col - label_w - value_w - 16.f;
			fill(state, cx + label_w, cy - 1.f, bw, 1.f, dip_rule, 1.f);
			fill(state, cx + label_w, cy - 1.5f, std::max(1.f, std::round(bw * std::abs(parts[i].amount) / largest)), 3.f, tone, 0.9f);
			auto const value = signed_count(parts[i].amount) + (parts[i].note.empty() ? std::string() : "  ·  " + parts[i].note);
			sans(state, value, cx + col, cy, 13, tone, true);
		}
	}
	demo_foot(state, localized(state, "katechon_demo_foot_forecast", "Today's monthly rates, held for every year ahead. In this world deaths do not depend on age."), left, right, bottom, hovering, mx, my);
}

// ---- the generations ----
void demo_generations_page(sys::state& state, float left, float right, float top, float bottom, bool hovering, float mx, float my) {
	auto const& d = demography;
	auto const g = demography_age;
	constexpr int32_t h = demography_view::horizon;
	demo_back(state, left, top, hovering, mx, my);
	// its size against the average single year of age under ninety, its place among the people, its work
	auto const members_now = demo_members(g, 0);
	auto const average_year = demo_sum(d.ages[0], 0, 90) / 90.f;
	auto const younger = demo_sum(d.ages[0], 0, std::max(g, 0)) / std::max(d.total[0], 1e-6f);
	auto const anchors_now = g >= 0 && g < demography_view::oldest ? d.anchors[0][size_t(g)] : 0.f;
	auto y = demo_figures(state,
		{ std::array<std::string, 3>{ localized(state, "katechon_demo_selected", "Selected generation"), std::to_string(demo_birth_year(g)), localized(state, "katechon_demo_birth_year", "Year of birth") },
			std::array<std::string, 3>{ localized(state, "katechon_demo_size", "Size"), average_year > 0.f && members_now > 0.f ? text::format_float(members_now / average_year, 2) + "×" : std::string("—"), localized(state, "katechon_demo_size_note", "of an average year of age") },
			std::array<std::string, 3>{ localized(state, "katechon_demo_place", "Place in the pyramid"), g < 0 ? std::string("—") : share_text(younger), localized(state, "katechon_demo_place_note", "of the people are younger") },
			std::array<std::string, 3>{ localized(state, "katechon_demo_in_labor", "In the labor force"), members_now > 0.f && g >= demo_work_from && g < demo_work_to ? share_text(anchors_now / members_now) : std::string("—"), filled(localized(state, "katechon_demo_in_year", "In %1"), std::to_string(d.year)) } },
		{ dip_name, dip_text, dip_text, dip_text }, left, right, top + 22.f);
	y += 24.f;
	constexpr float gap = 44.f;
	constexpr float course_h = 136.f;
	auto const split = std::round(left + (right - left - gap) * 0.60f);
	auto const lower = bottom - 26.f - course_h;
	auto const comparing = demography_compare != demo_no_compare;
	{
		auto const body = demo_head(state, localized(state, "katechon_demo_generations_kind", "Age groups through the years"), localized(state, "katechon_demo_through_time", "Generations through time"), std::string(), 0, left, split, y, hovering, mx, my);
		demo_heatmap(state, left, body, split, lower - 40.f, true, hovering, mx, my);
		auto kx = left + 34.f;
		kx += demo_key(state, demo_few, localized(state, "katechon_demo_fewer_people", "Smaller than the year's average group"), kx, lower - 22.f);
		kx += demo_key(state, demo_many, localized(state, "katechon_demo_more_people", "Larger"), kx, lower - 22.f);
		if(comparing)
			demo_key(state, demo_second, filled(localized(state, "katechon_demo_compared", "Compared: %1"), std::to_string(demo_birth_year(demography_compare))), kx, lower - 22.f);
		sans(state, localized(state, "katechon_demo_press_cell", "Press a cell to follow its generation"), split, lower - 22.f, 12, dip_faint, true);
	}
	// the generation's place in the country, now and later
	{
		auto const rx = split + gap;
		pop_label(state, localized(state, "katechon_demo_profile_of", "Selected generation  ·  profile"), rx, y + 6.f);
		draw_text(state, localized(state, "katechon_demo_now_and_later", "Now and later"), rx, y + 30.f, 22, 0.004f, dip_name);
		pop_link(state, comparing ? localized(state, "katechon_demo_compare_clear", "Stop comparing  ›") : localized(state, "katechon_demo_compare", "Compare cohorts  ›"), right, y + 30.f, action::demography_compare, hovering, mx, my, true,
			comparing ? demo_no_compare : g);
		auto ry = y + 58.f;
		std::array<int32_t, 3> const at{ 0, 20, h };
		auto const label_w = std::max(110.f, (right - rx) * 0.30f);
		auto const col = (right - rx - label_w) / 3.f;
		for(size_t i = 0; i < 3; ++i)
			sans(state, std::to_string(d.year + at[i]), rx + label_w + col * float(i + 1), ry + 8.f, 12, dip_bronze, true);
		ry += 20.f;
		auto const row = [&](std::string const& label, std::array<std::string, 3> const& values, ogl::color3f colour, bool second) {
			sans(state, sans_fit(state, label, 13, label_w - 6.f), rx, ry + 13.f, 13, second ? colour : dip_text);
			for(size_t i = 0; i < 3; ++i)
				sans(state, sans_fit(state, values[i], 13, col - 6.f), rx + label_w + col * float(i + 1), ry + 13.f, 13, colour, true);
			fill(state, rx, ry + 27.f, right - rx, 1.f, dip_rule, 0.7f);
			ry += 29.f;
		};
		auto const lines_of = [&](int32_t age, std::array<std::string, 3>& ages, std::array<std::string, 3>& members, std::array<std::string, 3>& shares, std::array<std::string, 3>& stages) {
			for(size_t i = 0; i < 3; ++i) {
				auto const m = demo_members(age, at[i]);
				ages[i] = demo_age_text(age, at[i]);
				members[i] = m > 0.f ? people(m) : std::string("—");
				shares[i] = m > 0.f ? share_text(m / std::max(d.total[size_t(at[i])], 1e-6f)) : std::string("—");
				stages[i] = demo_stage(state, float(age + at[i]));
			}
		};
		std::array<std::string, 3> ages, members, shares, stages;
		lines_of(g, ages, members, shares, stages);
		row(localized(state, "katechon_demo_members", "Members"), members, dip_name, false);
		row(localized(state, "katechon_demo_share", "Share of the people"), shares, dip_name, false);
		if(comparing) {
			lines_of(demography_compare, ages, members, shares, stages);
			auto const born = std::to_string(demo_birth_year(demography_compare));
			row(filled(localized(state, "katechon_demo_born_short", "Born %1"), born), members, demo_second, true);
			row(born + "  ·  " + localized(state, "katechon_demo_share_short", "share"), shares, demo_second, true);
		}
		ry += 8.f;
		// the generation in the kernel's own terms, now and in twenty years
		pop_label(state, filled(localized(state, "katechon_demo_outlook_in", "Outlook in %1"), std::to_string(d.year + 20)), rx, ry + 6.f);
		ry += 22.f;
		auto const bar = [&](std::string const& label, float part, ogl::color3f colour, std::string const& about) {
			hit_target r{ rx, ry, right - rx, 22.f, action::none };
			sans(state, label, rx, ry + 10.f, 12, dip_muted);
			auto const bx = rx + std::max(label_w, sans_w(state, label, 12) + 16.f);
			auto const bw = right - bx - 56.f;
			fill(state, bx, ry + 10.f, bw, 1.f, dip_rule, 1.f);
			if(part > 0.f)
				fill(state, bx, ry + 9.f, std::max(1.f, std::round(bw * std::min(part, 1.f))), 3.f, colour, 0.9f);
			sans(state, part < 0.f ? std::string("—") : share_text(part), right, ry + 10.f, 13, dip_text, true);
			if(pop_over(r, hovering, mx, my))
				hint(about, r);
			ry += 24.f;
		};
		auto const then = g + 20;
		auto const members_now = demo_members(g, 0);
		auto const members_then = demo_members(g, 20);
		auto const anchors_then = then >= 0 && then < demography_view::oldest ? d.anchors[20][size_t(then)] : 0.f;
		bar(localized(state, "katechon_demo_in_labor", "In the labor force"), members_then > 0.f && then >= demo_work_from && then < demo_work_to ? anchors_then / members_then : 0.f, demo_young,
			localized(state, "katechon_demo_in_labor_about", "The workforce anchors of working age: one person in four, as the catalog numbers them."));
		if(members_now > 0.f)
			bar(localized(state, "katechon_demo_still_here", "Still in the country"), members_then / members_now, demo_working,
				localized(state, "katechon_demo_still_here_about", "Of its members today, the part still counted here in twenty years at today's rates."));
	}
	// the course of its life, a milestone each ten years
	{
		fill(state, left, lower - 4.f, right - left, 1.f, dip_rule, 1.f);
		auto cy = lower + 10.f;
		pop_label(state, std::to_string(d.year) + "–" + std::to_string(d.year + h) + "  ·  " + localized(state, "katechon_demo_life_course", "Life course"), left, cy + 6.f);
		draw_text(state, localized(state, "katechon_demo_what_happens", "What happens to this generation"), left, cy + 30.f, 22, 0.004f, dip_name);
		cy += 56.f;
		constexpr int32_t stops = 5;
		auto const x0 = left + 50.f;
		auto const x1 = right - 50.f;
		fill(state, x0, cy, x1 - x0, 1.f, dip_rule, 1.f);
		for(int32_t i = 0; i < stops; ++i) {
			auto const years_on = i * h / (stops - 1);
			auto const x = std::round(x0 + (x1 - x0) * float(i) / float(stops - 1));
			frame(state, x - 4.f, cy - 4.f, 9.f, 9.f, dip_bronze, 1.f);
			fill(state, x - 2.f, cy - 2.f, 5.f, 5.f, pop_tile, 1.f);
			sans_centered(state, std::to_string(d.year + years_on), x - 60.f, 120.f, cy + 19.f, 15, dip_name);
			sans_centered(state, demo_course(state, g + years_on), x - 80.f, 160.f, cy + 36.f, 12, dip_muted);
			sans_centered(state, g + years_on < 0 ? std::string("—") : filled(localized(state, "katechon_demo_age_short", "Age %1"), std::to_string(g + years_on)), x - 60.f, 120.f, cy + 51.f, 11, dip_bronze);
		}
	}
	demo_foot(state, localized(state, "katechon_demo_foot_generations", "A diagonal follows one generation through the years. Deaths here do not depend on age."), left, right, bottom, hovering, mx, my);
}

// ---- movement of people ----
void demo_movement_page(sys::state& state, float left, float right, float top, float bottom, bool hovering, float mx, float my) {
	auto const& d = demography;
	auto const& v = population;
	demo_back(state, left, top, hovering, mx, my);
	auto sum = [](std::vector<demography_view::flow> const& list) {
		auto s = 0.f;
		for(auto const& f : list)
			s += f.amount;
		return s;
	};
	auto const internal = demography_internal || v.province;
	auto const& in = internal ? d.regions_in : d.abroad_in;
	auto const& out = internal ? d.regions_out : d.abroad_out;
	auto const coming = sum(in);
	auto const going = sum(out);
	// what was made last month, and what the game expects of this one; nothing yet is a dash, not a zero
	auto const amount = [&](float units, bool with_sign) {
		return std::abs(units) * people_per_unit < 0.5f ? std::string("—") : (with_sign ? signed_count(units) : people(std::abs(units)));
	};
	auto const nothing = localized(state, "katechon_demo_nothing_yet", "none recorded yet");
	auto const expected = internal ? localized(state, "katechon_demo_between_regions", "between regions, a month") : localized(state, "katechon_demo_estimate", "the game's estimate, a month");
	auto y = demo_figures(state,
		{ std::array<std::string, 3>{ localized(state, "katechon_demo_net_abroad", "Moved across the border"), amount(v.external_month, true), std::abs(v.external_month) * people_per_unit < 0.5f ? nothing : localized(state, "katechon_demo_month_made", "a month, the moves made") },
			std::array<std::string, 3>{ internal ? localized(state, "katechon_demo_arrivals", "Arrivals") : localized(state, "katechon_demo_expected_in", "Expected arrivals"), amount(coming, false), std::abs(coming) * people_per_unit < 0.5f ? nothing : expected },
			std::array<std::string, 3>{ internal ? localized(state, "katechon_demo_departures", "Departures") : localized(state, "katechon_demo_expected_out", "Expected departures"), amount(going, false), std::abs(going) * people_per_unit < 0.5f ? nothing : expected },
			std::array<std::string, 3>{ localized(state, "katechon_demo_relocations", "Moves within the country"), amount(v.relocations, false), v.relocations * people_per_unit < 0.5f ? nothing : localized(state, "katechon_ppl_a_month", "a month") } },
		{ std::abs(v.external_month) * people_per_unit < 0.5f ? dip_faint : (v.external_month < 0.f ? pop_bad : pop_good), std::abs(coming) * people_per_unit < 0.5f ? dip_faint : pop_good,
			std::abs(going) * people_per_unit < 0.5f ? dip_faint : pop_bad, v.relocations * people_per_unit < 0.5f ? dip_faint : dip_text }, left, right, top + 22.f);
	y += 24.f;
	// the two kinds of movement
	{
		std::array<std::string, 2> const kinds{ localized(state, "katechon_demo_international", "International"), localized(state, "katechon_demo_internal", "Internal") };
		auto x = right;
		for(int32_t i = 1; i >= 0; --i) {
			if(v.province && i == 0)
				continue;
			auto const tw = sans_w(state, kinds[size_t(i)], 13);
			hit_target t{ x - tw - 24.f, y, tw + 24.f, 28.f, action::demography_flows, nullptr, i };
			auto const on = (i == 1) == internal;
			auto const lit = pop_over(t, hovering, mx, my);
			frame(state, t.x, t.y, t.w, t.h, on ? dip_bronze : dip_rule, 1.f);
			if(on)
				fill(state, t.x + 1.f, t.y + 1.f, t.w - 2.f, t.h - 2.f, dip_bronze, 0.10f);
			sans_centered(state, kinds[size_t(i)], t.x, t.w, t.y + 14.f, 13, on ? dip_name : (lit ? dip_text : dip_muted));
			if(!on)
				pop_target(t);
			x -= t.w + 8.f;
		}
		pop_label(state, localized(state, "katechon_demo_movement", "Movement of people"), left, y + 6.f);
		draw_text(state, localized(state, "katechon_demo_migration", "Migration flows"), left, y + 30.f, 22, 0.004f, dip_name);
	}
	y += 62.f;
	// the flows: where they come from, the country, where they go
	auto const area_bottom = bottom - 40.f;
	constexpr size_t shown = 7;
	constexpr float col_w = 280.f;
	auto const box_w = 200.f;
	auto const box_x = std::round((left + right - box_w) * 0.5f);
	auto const rows_used = float(std::max<size_t>({ std::min(in.size(), shown), std::min(out.size(), shown), size_t(1) }));
	auto const row_h = std::min(52.f, (area_bottom - y) / float(shown));
	y = std::round(y + std::max(0.f, (area_bottom - y - row_h * rows_used) * 0.15f));
	auto const box_h = 76.f;
	auto const box_y = std::round(y + (row_h * rows_used - box_h) * 0.5f);
	auto const name_of = [&](demography_view::flow const& f) {
		return f.region ? text::get_name_as_string(state, f.region) : text::get_name_as_string(state, dcon::fatten(state.world, f.nation));
	};
	auto const curve = [&](float x0, float y0, float x1, float y1, ogl::color3f colour, float alpha) {
		auto const top_y = std::min(y0, y1);
		auto const h = std::max(1.f, std::abs(y1 - y0));
		std::array<float, 16> points{};
		for(size_t i = 0; i < points.size(); ++i) {
			auto const s = float(i) / float(points.size() - 1);
			auto const e = s * s * (3.f - 2.f * s);
			auto const yy = y0 + (y1 - y0) * e;
			points[i] = std::abs(y1 - y0) < 1.f ? 0.5f : 1.f - (yy - top_y) / h;
		}
		demo_flow_line.set_y(points.data());
		ogl::render_linegraph(state, ogl::color_modification::none, x0, top_y, x1 - x0, h, colour.r, colour.g, colour.b, alpha, demo_flow_line);
	};
	auto largest = 1e-6f;
	for(auto const* list : { &in, &out })
		for(size_t i = 0; i < list->size() && i < shown; ++i)
			largest = std::max(largest, std::abs((*list)[i].amount));
	auto const side = [&](std::vector<demography_view::flow> const& list, bool coming_in) {
		auto const x = coming_in ? left : right - col_w;
		pop_label(state, coming_in ? localized(state, "katechon_demo_from", "From") : localized(state, "katechon_demo_to", "To"), x, y - 10.f);
		if(list.empty())
			sans(state, localized(state, "katechon_demo_nobody_moves", "Nobody this month"), x, y + 18.f, 13, dip_faint);
		for(size_t i = 0; i < list.size() && i < shown; ++i) {
			auto const& f = list[i];
			auto const cy = std::round(y + row_h * (float(i) + 0.5f));
			auto tx = x;
			if(f.nation) {
				dip_flag(state, f.nation, x, cy - 7.f, 21.f, 14.f);
				tx += 30.f;
			}
			auto const amount = signed_count(f.amount);
			auto const aw = sans(state, amount, x + col_w, cy, 13, coming_in ? demo_young : pop_bad, true);
			sans(state, sans_fit(state, name_of(f), 13, col_w - (tx - x) - aw - 12.f), tx, cy, 13, dip_text);
			auto const part = std::abs(f.amount) / largest;
			fill(state, x, cy + 12.f, std::max(1.f, std::round(col_w * part)), 1.f, coming_in ? demo_young : pop_bad, 0.8f);
			// to the country, or from it
			auto const at_box = box_y + box_h * (float(i) + 1.f) / float(std::min(list.size(), shown) + 1);
			if(coming_in)
				curve(x + col_w + 10.f, cy, box_x - 2.f, at_box, demo_young, 0.25f + 0.6f * part);
			else
				curve(box_x + box_w + 2.f, at_box, x - 10.f, cy, pop_bad, 0.25f + 0.6f * part);
		}
	};
	side(in, true);
	side(out, false);
	// the country itself
	frame(state, box_x, box_y, box_w, box_h, dip_bronze, 1.f);
	fill(state, box_x + 1.f, box_y + 1.f, box_w - 2.f, box_h - 2.f, pop_tile, 1.f);
	sans_centered(state, upper(v.title), box_x, box_w, box_y + box_h * 0.5f, 14, dip_name, 0.12f);
	demo_foot(state, internal ? localized(state, "katechon_demo_foot_internal", "The month's moves between the country's regions, as the game made them.")
		: localized(state, "katechon_demo_foot_abroad", "Across the border, the game's estimate of the month's moves by country. The figure above counts the moves made."), left, right, bottom, hovering, mx, my);
}

// ---- the age groups ----
void demo_groups_page(sys::state& state, float left, float right, float top, float bottom, bool hovering, float mx, float my) {
	auto const& d = demography;
	auto const& now = d.ages[0];
	auto const& anchors = d.anchors[0];
	auto const all = std::max(d.total[0], 1e-6f);
	demo_back(state, left, top, hovering, mx, my);
	auto const young = demo_sum(now, 0, demo_work_from);
	auto const working = demo_sum(now, demo_work_from, demo_work_to);
	auto const old = demo_sum(now, demo_work_to, -1);
	auto y = demo_figures(state,
		{ std::array<std::string, 3>{ filled(localized(state, "katechon_demo_children_ages", "Children  ·  0–%1"), std::to_string(demo_work_from - 1)), share_text(young / all), people(young) },
			std::array<std::string, 3>{ filled(localized(state, "katechon_demo_working_row", "Working age  ·  %1"), demo_range(demo_work_from, demo_work_to)), share_text(working / all), people(working) },
			std::array<std::string, 3>{ filled(localized(state, "katechon_demo_old_row", "%1 and older"), std::to_string(demo_work_to)), share_text(old / all), people(old) },
			std::array<std::string, 3>{ localized(state, "katechon_demo_dependency", "Dependency ratio"), working > 0.f ? std::to_string(int32_t(std::lround((young + old) / working * 100.f))) + " / 100" : std::string("—"), localized(state, "katechon_demo_dependency_short", "dependants per hundred") } },
		{ dip_text, dip_text, dip_text, dip_name }, left, right, top + 22.f);
	y += 30.f;
	pop_label(state, localized(state, "katechon_demo_groups_kind", "Counted person by person"), left, y + 6.f);
	draw_text(state, localized(state, "katechon_demo_groups", "Key indicators by age group"), left, y + 30.f, 22, 0.004f, dip_name);
	y += 62.f;
	struct group {
		int32_t from;
		int32_t to;
	};
	std::array<group, 5> const groups{ group{ 0, demo_work_from }, group{ demo_work_from, 25 }, group{ 25, 45 }, group{ 45, demo_work_to }, group{ demo_work_to, -1 } };
	auto const label_w = std::max(220.f, (right - left) * 0.28f);
	auto const col = (right - left - label_w) / float(groups.size());
	for(size_t i = 0; i < groups.size(); ++i)
		sans(state, demo_range(groups[i].from, groups[i].to), left + label_w + col * float(i + 1), y + 8.f, 13, dip_bronze, true);
	y += 24.f;
	fill(state, left, y, right - left, 1.f, dip_rule, 1.f);
	auto const row = [&](std::string const& label, std::string const& about, auto&& value_of) {
		// a row of nothing but zeros is not shown
		auto any = false;
		for(auto const& g : groups) {
			auto const [words, part] = value_of(g);
			any = any || part != 0.f;
		}
		if(!any)
			return;
		hit_target r{ left, y, right - left, 40.f, action::none };
		auto const lit = pop_over(r, hovering, mx, my);
		if(lit)
			fill(state, left, y, right - left, 40.f, dip_bronze, 0.05f);
		sans(state, label, left, y + 20.f, 14, lit ? dip_name : dip_text);
		for(size_t i = 0; i < groups.size(); ++i) {
			auto const [words, part] = value_of(groups[i]);
			auto const cx = left + label_w + col * float(i + 1);
			sans(state, words, cx, y + 20.f, 14, dip_name, true);
		}
		fill(state, left, y + 39.f, right - left, 1.f, dip_rule, 0.7f);
		if(lit && !about.empty())
			hint(about, r);
		y += 40.f;
	};
	using cell = std::pair<std::string, float>;
	auto const of = [&](std::vector<float> const& profile, group g) {
		return demo_sum(profile, g.from, g.to);
	};
	row(localized(state, "katechon_demo_members", "Members"), std::string(), [&](group g) { return cell{ people(of(now, g)), 1.f }; });
	row(localized(state, "katechon_demo_share", "Share of the people"), std::string(), [&](group g) { return cell{ share_text(of(now, g) / all), of(now, g) / all }; });
	auto const part_of = [&](float part_people, group g) {
		auto const people_here = of(now, g);
		return people_here > 0.f ? part_people / people_here : 0.f;
	};
	row(localized(state, "katechon_demo_sex_ratio", "Men per 100 women"), localized(state, "katechon_demo_sex_ratio_about", "Each person's sex is drawn from their identity: a few more boys are born; among the old, women outnumber men."), [&](group g) {
		auto const m = of(d.men[0], g);
		auto const w = of(now, g) - m;
		return cell{ w > 0.f ? std::to_string(int32_t(std::lround(m / w * 100.f))) : std::string("—"), 1.f };
	});
	row(localized(state, "katechon_demo_in_labor", "In the labor force"), localized(state, "katechon_demo_in_labor_about", "The workforce anchors of working age: one person in four, as the catalog numbers them."), [&](group g) {
		auto const from = std::max(g.from, demo_work_from);
		auto const to = g.to < 0 ? demo_work_to : std::min(g.to, demo_work_to);
		auto const part = to > from ? part_of(demo_sum(anchors, from, to), g) : 0.f;
		return cell{ share_text(part), part };
	});
	row(localized(state, "katechon_demo_wage", "With a wage contract"), localized(state, "katechon_demo_wage_about", "People of the group who hold an active wage contract today."), [&](group g) {
		auto const part = part_of(of(d.contracted, g), g);
		return cell{ share_text(part), part };
	});
	row(localized(state, "katechon_demo_serving", "In the army"), localized(state, "katechon_demo_serving_about", "People of the group assigned to a land formation today."), [&](group g) {
		auto const part = part_of(of(d.serving, g), g);
		return cell{ share_text(part), part };
	});
	{
		// the eligible are the labor force of 18 and over, less those serving; shown only where that differs
		auto const labor_of = [&](group g) {
			auto const from = std::max(g.from, demo_work_from);
			auto const to = g.to < 0 ? demo_work_to : std::min(g.to, demo_work_to);
			return to > from ? part_of(demo_sum(anchors, from, to), g) : 0.f;
		};
		auto const eligible_of = [&](group g) {
			auto const from = std::max(g.from, demo_recruit_from);
			auto const to = g.to < 0 ? demo_work_to : std::min(g.to, demo_work_to);
			return to > from ? part_of(std::max(0.f, demo_sum(anchors, from, to) - demo_sum(d.serving, from, to)), g) : 0.f;
		};
		auto differs = false;
		for(auto const& g : groups)
			differs = differs || std::abs(labor_of(g) - eligible_of(g)) >= 0.005f;
		if(differs)
			row(localized(state, "katechon_demo_recruitable", "Military eligible"), localized(state, "katechon_demo_recruitable_about", "Workforce anchors of 18 to 64, the people land forces recruit from."), [&](group g) {
				return cell{ share_text(eligible_of(g)), eligible_of(g) };
			});
	}
	// the stages of life over the years ahead
	y += 30.f;
	pop_label(state, filled(localized(state, "katechon_demo_structure_kind", "%1  ·  at today's rates"), std::to_string(d.year + 10) + "–" + std::to_string(d.year + demography_view::horizon)), left, y + 6.f);
	draw_text(state, localized(state, "katechon_demo_structure", "Stages of life over the years"), left, y + 30.f, 22, 0.004f, dip_name);
	{
		auto kx = right - 420.f;
		kx += demo_key(state, demo_young, localized(state, "katechon_demo_young", "Children"), kx, y + 30.f);
		kx += demo_key(state, demo_working, localized(state, "katechon_demo_working", "Working age"), kx, y + 30.f);
		demo_key(state, demo_old, localized(state, "katechon_demo_old", "Past working age"), kx, y + 30.f);
	}
	y += 52.f;
	for(int32_t years_on = 10; years_on <= demography_view::horizon && y + 30.f <= bottom - 40.f; years_on += 10) {
		auto const& p = d.ages[size_t(years_on)];
		auto const total = std::max(d.total[size_t(years_on)], 1e-6f);
		auto const young_part = demo_sum(p, 0, demo_work_from) / total;
		auto const working_part = demo_sum(p, demo_work_from, demo_work_to) / total;
		auto const old_part = std::max(0.f, 1.f - young_part - working_part);
		sans(state, std::to_string(d.year + years_on), left, y + 10.f, 13, dip_muted);
		auto const bx = left + 70.f;
		auto const bw = std::round((right - left - 70.f) * 0.55f);
		parts_bar(state, bx, y + 8.f, bw, 4.f, { { young_part, demo_young }, { working_part, demo_working }, { old_part, demo_old } });
		auto x = bx + bw + 24.f;
		x += demo_key(state, demo_young, share_text(young_part), x, y + 10.f);
		x += demo_key(state, demo_working, share_text(working_part), x, y + 10.f);
		demo_key(state, demo_old, share_text(old_part), x, y + 10.f);
		y += 28.f;
	}
	demo_foot(state, localized(state, "katechon_demo_foot_groups", "Counted from the people themselves: ages, wage contracts and assignments. Schooling is not kept by age."), left, right, bottom, hovering, mx, my);
}
