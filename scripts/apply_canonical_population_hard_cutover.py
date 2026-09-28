from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def write(path, text):
    (ROOT / path).write_text(text, encoding="utf-8")


def replace_once(path, old, new):
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected exactly one match, found {count}: {old[:80]!r}")
    write(path, text.replace(old, new, 1))


def replace_between(path, start, end, replacement):
    text = read(path)
    i = text.find(start)
    if i < 0:
        raise RuntimeError(f"{path}: start marker not found: {start!r}")
    j = text.find(end, i + len(start))
    if j < 0:
        raise RuntimeError(f"{path}: end marker not found: {end!r}")
    write(path, text[:i] + replacement + text[j:])


def replace_balanced_block(path, marker, replacement):
    text = read(path)
    i = text.find(marker)
    if i < 0:
        raise RuntimeError(f"{path}: block marker not found: {marker!r}")
    brace = text.find("{", i)
    if brace < 0:
        raise RuntimeError(f"{path}: opening brace not found after marker")
    depth = 0
    end = None
    for pos in range(brace, len(text)):
        ch = text[pos]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                end = pos + 1
                break
    if end is None:
        raise RuntimeError(f"{path}: unterminated block after marker")
    write(path, text[:i] + replacement + text[end:])


# ---------------------------------------------------------------------------
# exact_population.hpp: expose only canonical mutation/projection operations.
# ---------------------------------------------------------------------------
replace_once(
    "src/persons/exact_population.hpp",
    "struct population_transfer_result {\n\tuint64_t people_moved = 0;\n\tbool complete = true;\n};",
    "struct population_transfer_result {\n\tuint64_t people_moved = 0;\n\tfloat population_amount_moved = 0.0f;\n\tbool complete = true;\n};\n\nstruct population_lifecycle_result {\n\tuint64_t people_born = 0;\n\tuint64_t people_died = 0;\n\tfloat population_amount_applied = 0.0f;\n\tbool complete = true;\n};"
)
replace_once(
    "src/persons/exact_population.hpp",
    "population_reconciliation_result reconcile_population_lifecycle(sys::state&);\npopulation_transfer_result transfer_population_membership",
    "population_reconciliation_result reconcile_population_lifecycle(sys::state&);\npopulation_lifecycle_result apply_population_lifecycle_delta(sys::state&, dcon::pop_id, float population_delta);\npopulation_transfer_result transfer_population_membership"
)
replace_once(
    "src/persons/exact_population.hpp",
    "uint64_t living_people_in_population_cell(sys::state const&, uint32_t population_cell);\nbool project_population_membership(sys::state&);",
    "uint64_t living_people_in_population_cell(sys::state const&, uint32_t population_cell);\nbool project_population(sys::state&, dcon::pop_id);\nbool project_population_membership(sys::state&);"
)

# ---------------------------------------------------------------------------
# exact_population.cpp: DCON size becomes projection only.
# ---------------------------------------------------------------------------
replace_once(
    "src/persons/exact_population.cpp",
    "uint64_t mix(uint64_t value) {\n\tvalue ^= value >> 30;\n\tvalue *= 0xbf58476d1ce4e5b9ULL;\n\tvalue ^= value >> 27;\n\tvalue *= 0x94d049bb133111ebULL;\n\treturn value ^ (value >> 31);\n}\n",
    "uint64_t mix(uint64_t value) {\n\tvalue ^= value >> 30;\n\tvalue *= 0xbf58476d1ce4e5b9ULL;\n\tvalue ^= value >> 27;\n\tvalue *= 0x94d049bb133111ebULL;\n\treturn value ^ (value >> 31);\n}\n\nuint64_t quantize_population_amount(double amount, uint32_t source_cell, int32_t day,\n\tuint64_t nonce, bool& valid) {\n\tvalid = false;\n\tif(!std::isfinite(amount) || amount < 0.0) return 0;\n\tauto scaled = amount * 4.0;\n\tif(!std::isfinite(scaled) || scaled >= double(std::numeric_limits<uint64_t>::max())) return 0;\n\tauto whole = uint64_t(std::floor(scaled));\n\tauto fraction = scaled - double(whole);\n\tif(fraction > 0.0) {\n\t\tauto entropy = mix((uint64_t(source_cell) << 32) ^ uint32_t(day)\n\t\t\t^ nonce ^ 0x6a09e667f3bcc909ULL);\n\t\tauto draw = double(entropy >> 11) * (1.0 / 9007199254740992.0);\n\t\tif(draw < fraction) {\n\t\t\tif(whole == std::numeric_limits<uint64_t>::max()) return 0;\n\t\t\t++whole;\n\t\t}\n\t}\n\tvalid = true;\n\treturn whole;\n}\n"
)

replace_between(
    "src/persons/exact_population.cpp",
    "uint64_t synchronize_current_pop_bindings(sys::state& state) {",
    "population_reconciliation_result reconcile_population_lifecycle(sys::state& state) {",
    """uint64_t synchronize_current_pop_bindings(sys::state& state) {
\tif(!state.exact_population) return 1;
\tauto store = state.exact_population;
\tfor(auto it = store->source_by_pop_slot.begin(); it != store->source_by_pop_slot.end();) {
\t\tauto pop = dcon::pop_id{dcon::pop_id::value_base_t(it->first - 1u)};
\t\tif(!state.world.pop_is_valid(pop)) {
\t\t\tstore->retired_source_cells.insert(it->second);
\t\t\tstore->pop_slot_by_source.erase(it->second);
\t\t\tit = store->source_by_pop_slot.erase(it);
\t\t} else {
\t\t\t++it;
\t\t}
\t}
\tuint64_t unbound = 0;
\tstate.world.for_each_pop([&](auto pop) {
\t\tif(source_cell_for_population(static_cast<sys::state const&>(state), pop) == 0)
\t\t\t++unbound;
\t});
\treturn unbound;
}

"""
)

replace_between(
    "src/persons/exact_population.cpp",
    "population_reconciliation_result reconcile_population_lifecycle(sys::state& state) {",
    "population_transfer_result transfer_population_membership(sys::state& state,",
    """population_reconciliation_result reconcile_population_lifecycle(sys::state& state) {
\tpopulation_reconciliation_result result;
\tif(!state.exact_population) {
\t\tresult.complete = false;
\t\tresult.unbound_populations = 1;
\t\treturn result;
\t}
\tresult.unbound_populations = synchronize_current_pop_bindings(state);
\tif(result.unbound_populations != 0) {
\t\tresult.complete = false;
\t\treturn result;
\t}
\tif(!project_population_membership(state)) {
\t\tresult.complete = false;
\t\treturn result;
\t}
\t// Reconciliation is validation/projection only. Births and deaths are
\t// canonical mutations and are never inferred from DCON POP deltas.
\tcapture_population_baseline(state, *state.exact_population);
\treturn result;
}

population_lifecycle_result apply_population_lifecycle_delta(sys::state& state,
\tdcon::pop_id pop, float population_delta) {
\tpopulation_lifecycle_result result;
\tif(!state.exact_population || !pop || !state.world.pop_is_valid(pop)
\t\t|| !std::isfinite(population_delta)) {
\t\tresult.complete = false;
\t\treturn result;
\t}
\tauto source_cell = source_cell_for_population(static_cast<sys::state const&>(state), pop);
\tauto store = state.exact_population;
\tauto index = find_cell_index(*store, source_cell);
\tif(source_cell == 0 || index == std::numeric_limits<std::size_t>::max()) {
\t\tresult.complete = false;
\t\treturn result;
\t}
\tauto living = living_count_in_population_cell(*store, source_cell);
\tbool valid_amount = false;
\tauto const day = state.current_date ? state.current_date.to_raw_value() - 1 : 0;
\tauto requested = quantize_population_amount(std::abs(double(population_delta)), source_cell,
\t\tday, living ^ store->cells[index].literal_count, valid_amount);
\tif(!valid_amount) {
\t\tresult.complete = false;
\t\treturn result;
\t}
\tif(population_delta > 0.0f && requested != 0) {
\t\tauto& cell = store->cells[index];
\t\tif(requested > std::numeric_limits<uint64_t>::max() - cell.literal_count) {
\t\t\tresult.complete = false;
\t\t\treturn result;
\t\t}
\t\tauto first_ordinal = cell.literal_count;
\t\tcell.literal_count += requested;
\t\tappend_default_membership(*store, cell, first_ordinal, requested);
\t\tbirth_cohort cohort;
\t\tcohort.source_population_cell = source_cell;
\t\tcohort.first_ordinal = first_ordinal;
\t\tcohort.count = requested;
\t\tcohort.birth_day = day;
\t\tcohort.home_site = cell.home_site;
\t\tcohort.culture = state.world.pop_get_culture(pop);
\t\tcohort.religion = state.world.pop_get_religion(pop);
\t\tcohort.pop_type = state.world.pop_get_poptype(pop);
\t\tstore->birth_cohorts.push_back(cohort);
\t\tstd::sort(store->birth_cohorts.begin(), store->birth_cohorts.end(), [](auto const& left, auto const& right) {
\t\t\treturn left.source_population_cell == right.source_population_cell
\t\t\t\t? left.first_ordinal < right.first_ordinal
\t\t\t\t: left.source_population_cell < right.source_population_cell;
\t\t});
\t\tresult.people_born = requested;
\t\tresult.population_amount_applied = float(requested) / 4.0f;
\t} else if(population_delta < 0.0f && requested != 0) {
\t\trequested = std::min(requested, living);
\t\tauto retired = retire_oldest_members_in_cell(*store, source_cell, requested);
\t\tif(retired != requested) {
\t\t\tresult.complete = false;
\t\t\treturn result;
\t\t}
\t\tresult.people_died = retired;
\t\tresult.population_amount_applied = -float(retired) / 4.0f;
\t}
\tif(!project_population(state, pop)) result.complete = false;
\treturn result;
}

"""
)

replace_between(
    "src/persons/exact_population.cpp",
    "population_transfer_result transfer_population_membership(sys::state& state,",
    "bool transfer_population_person_membership(sys::state& state, person_key key,",
    """population_transfer_result transfer_population_membership(sys::state& state,
\tdcon::pop_id source, dcon::pop_id destination, float population_amount,
\tpopulation_transition_cause cause) {
\tpopulation_transfer_result result;
\tif(!state.exact_population || !source || !destination || source == destination
\t\t|| !state.world.pop_is_valid(source) || !state.world.pop_is_valid(destination)
\t\t|| !std::isfinite(population_amount) || population_amount <= 0.0f) {
\t\tresult.complete = false;
\t\treturn result;
\t}
\tauto source_cell = source_cell_for_population(static_cast<sys::state const&>(state), source);
\tauto destination_cell = source_cell_for_population(static_cast<sys::state const&>(state), destination);
\tauto store = state.exact_population;
\tif(source_cell == 0 || destination_cell == 0 || source_cell == destination_cell
\t\t|| !find_cell(state, source_cell) || !find_cell(state, destination_cell)) {
\t\tresult.complete = false;
\t\treturn result;
\t}
\tauto remainder = std::find_if(store->transfer_remainders.begin(), store->transfer_remainders.end(),
\t\t[&](auto const& record) {
\t\t\treturn record.from_population_cell == source_cell
\t\t\t\t&& record.to_population_cell == destination_cell;
\t\t});
\tauto pending = remainder == store->transfer_remainders.end() ? 0.0 : remainder->pending_people;
\tauto total_people = double(population_amount) * 4.0 + pending;
\tif(!std::isfinite(total_people) || total_people < 0.0
\t\t|| total_people >= double(std::numeric_limits<uint64_t>::max())) {
\t\tresult.complete = false;
\t\treturn result;
\t}
\tauto requested = uint64_t(std::floor(total_people));
\tauto next_pending = total_people - double(requested);
\tauto available = living_count_in_population_cell(*store, source_cell);
\tauto destination_people = living_count_in_population_cell(*store, destination_cell);
\tif(requested > available || requested > std::numeric_limits<uint64_t>::max() - destination_people) {
\t\tresult.complete = false;
\t\treturn result;
\t}
\tstd::vector<population_membership_range> updated;
\tupdated.reserve(store->membership_ranges.size() + 8);
\tauto remaining = requested;
\tstd::vector<population_transition_record> new_transitions;
\tfor(auto const& range : store->membership_ranges) {
\t\tif(range.current_population_cell != source_cell || remaining == 0) {
\t\t\tupdated.push_back(range);
\t\t\tcontinue;
\t\t}
\t\tauto cursor = range.first_ordinal;
\t\tauto end = range.first_ordinal + range.count;
\t\tfor(auto const& living_range : living_ranges_in_interval(*store, range.identity_population_cell,
\t\t\trange.first_ordinal, range.count)) {
\t\t\tif(living_range.first_ordinal > cursor)
\t\t\t\tupdated.push_back({range.identity_population_cell, source_cell, cursor,
\t\t\t\t\tliving_range.first_ordinal - cursor});
\t\t\tauto moved = std::min(remaining, living_range.count);
\t\t\tif(moved != 0) {
\t\t\t\tupdated.push_back({range.identity_population_cell, destination_cell,
\t\t\t\t\tliving_range.first_ordinal, moved});
\t\t\t\tpopulation_transition_record event;
\t\t\t\tevent.identity_population_cell = range.identity_population_cell;
\t\t\t\tevent.from_population_cell = source_cell;
\t\t\t\tevent.to_population_cell = destination_cell;
\t\t\t\tevent.cause = uint8_t(cause);
\t\t\t\tevent.first_ordinal = living_range.first_ordinal;
\t\t\t\tevent.count = moved;
\t\t\t\tevent.transition_day = state.current_date ? state.current_date.to_raw_value() - 1 : 0;
\t\t\t\tnew_transitions.push_back(event);
\t\t\t\tremaining -= moved;
\t\t\t}
\t\t\tif(moved < living_range.count)
\t\t\t\tupdated.push_back({range.identity_population_cell, source_cell,
\t\t\t\t\tliving_range.first_ordinal + moved, living_range.count - moved});
\t\t\tcursor = living_range.first_ordinal + living_range.count;
\t\t}
\t\tif(cursor < end)
\t\t\tupdated.push_back({range.identity_population_cell, source_cell, cursor, end - cursor});
\t}
\tif(remaining != 0) {
\t\tresult.complete = false;
\t\treturn result;
\t}
\tstore->membership_ranges = std::move(updated);
\tnormalize_membership_ranges(*store);
\tfor(auto const& event : new_transitions) {
\t\tif(!store->transitions.empty()) {
\t\t\tauto& previous = store->transitions.back();
\t\t\tif(previous.identity_population_cell == event.identity_population_cell
\t\t\t\t&& previous.from_population_cell == event.from_population_cell
\t\t\t\t&& previous.to_population_cell == event.to_population_cell
\t\t\t\t&& previous.cause == event.cause && previous.transition_day == event.transition_day
\t\t\t\t&& previous.first_ordinal + previous.count == event.first_ordinal) {
\t\t\t\tprevious.count += event.count;
\t\t\t\tcontinue;
\t\t\t}
\t\t}
\t\tstore->transitions.push_back(event);
\t}
\tif(remainder == store->transfer_remainders.end()) {
\t\tstore->transfer_remainders.push_back({source_cell, destination_cell, next_pending});
\t} else {
\t\tremainder->pending_people = next_pending;
\t}
\tresult.people_moved = requested;
\tresult.population_amount_moved = float(requested) / 4.0f;
\tif(!project_population(state, source) || !project_population(state, destination)) {
\t\tresult.complete = false;
\t\treturn result;
\t}
\treturn result;
}

"""
)

replace_between(
    "src/persons/exact_population.cpp",
    "bool transfer_population_person_membership(sys::state& state, person_key key,",
    "uint32_t current_population_cell(sys::state const& state, person_key key) {",
    """bool transfer_population_person_membership(sys::state& state, person_key key,
\tdcon::pop_id destination, population_transition_cause cause) {
\tif(!state.exact_population || !exists(state, key) || !alive(state, key) || !destination
\t\t|| !state.world.pop_is_valid(destination)) return false;
\tauto membership = membership_range_for(*state.exact_population, key);
\tauto destination_cell = source_cell_for_population(static_cast<sys::state const&>(state), destination);
\tif(!membership || membership->current_population_cell == 0 || destination_cell == 0
\t\t|| membership->current_population_cell == destination_cell
\t\t|| !find_cell(state, destination_cell)) return false;
\tauto const old_range = *membership;
\tauto const source_cell = old_range.current_population_cell;
\tauto source = population_for_source_cell(state, source_cell);
\tif(!source || !state.world.pop_is_valid(source)) return false;
\tauto store = state.exact_population;
\tstd::vector<population_membership_range> updated;
\tupdated.reserve(store->membership_ranges.size() + 2);
\tbool replaced = false;
\tfor(auto const& range : store->membership_ranges) {
\t\tif(!replaced && range.identity_population_cell == key.source_population_cell
\t\t\t&& range.current_population_cell == source_cell
\t\t\t&& key.ordinal >= range.first_ordinal
\t\t\t&& key.ordinal - range.first_ordinal < range.count) {
\t\t\tif(key.ordinal > range.first_ordinal)
\t\t\t\tupdated.push_back({range.identity_population_cell, source_cell,
\t\t\t\t\trange.first_ordinal, key.ordinal - range.first_ordinal});
\t\t\tupdated.push_back({range.identity_population_cell, destination_cell, key.ordinal, 1});
\t\t\tauto const after = range.first_ordinal + range.count - key.ordinal - 1;
\t\t\tif(after != 0)
\t\t\t\tupdated.push_back({range.identity_population_cell, source_cell, key.ordinal + 1, after});
\t\t\treplaced = true;
\t\t} else {
\t\t\tupdated.push_back(range);
\t\t}
\t}
\tif(!replaced) return false;
\tstore->membership_ranges = std::move(updated);
\tnormalize_membership_ranges(*store);
\tpopulation_transition_record event;
\tevent.identity_population_cell = key.source_population_cell;
\tevent.from_population_cell = source_cell;
\tevent.to_population_cell = destination_cell;
\tevent.cause = uint8_t(cause);
\tevent.first_ordinal = key.ordinal;
\tevent.count = 1;
\tevent.transition_day = state.current_date ? state.current_date.to_raw_value() - 1 : 0;
\tif(!store->transitions.empty()) {
\t\tauto& previous = store->transitions.back();
\t\tif(previous.identity_population_cell == event.identity_population_cell
\t\t\t&& previous.from_population_cell == event.from_population_cell
\t\t\t&& previous.to_population_cell == event.to_population_cell
\t\t\t&& previous.cause == event.cause && previous.transition_day == event.transition_day
\t\t\t&& previous.first_ordinal + previous.count == event.first_ordinal) {
\t\t\t++previous.count;
\t\t} else {
\t\t\tstore->transitions.push_back(event);
\t\t}
\t} else {
\t\tstore->transitions.push_back(event);
\t}
\treturn project_population(state, source) && project_population(state, destination);
}

"""
)

replace_once(
    "src/persons/exact_population.cpp",
    "uint32_t current_population_cell(sys::state const& state, person_key key) {\n\tif(!exists(state, key)) return 0;\n\tif(state.exact_population)\n\t\tif(auto range = membership_range_for(*state.exact_population, key))\n\t\t\treturn range->current_population_cell;\n\treturn key.source_population_cell;\n}",
    "uint32_t current_population_cell(sys::state const& state, person_key key) {\n\tif(!state.exact_population || !exists(state, key)) return 0;\n\tif(auto range = membership_range_for(*state.exact_population, key))\n\t\treturn range->current_population_cell;\n\treturn 0;\n}"
)

replace_between(
    "src/persons/exact_population.cpp",
    "bool project_population_membership(sys::state& state) {",
    "bool source_cell_registered(sys::state const& state, uint32_t source_population_cell) {",
    """bool project_population(sys::state& state, dcon::pop_id pop) {
\tif(!state.exact_population || !pop || !state.world.pop_is_valid(pop)) return false;
\tauto source = source_cell_for_population(static_cast<sys::state const&>(state), pop);
\tif(source == 0 || !find_cell(state, source)) return false;
\tauto people = living_count_in_population_cell(*state.exact_population, source);
\tauto size = double(people) / 4.0;
\tif(!std::isfinite(size) || size > double(std::numeric_limits<float>::max())) return false;
\tstate.world.pop_set_size(pop, float(size));
\treturn true;
}

bool project_population_membership(sys::state& state) {
\tif(!state.exact_population) return false;
\tbool complete = true;
\tstate.world.for_each_pop([&](auto pop) {
\t\tif(!project_population(state, pop)) complete = false;
\t});
\treturn complete;
}

"""
)

# ---------------------------------------------------------------------------
# demographics.cpp: canonical mutation first, never legacy size first.
# ---------------------------------------------------------------------------
replace_once("src/economy/demographics.cpp", "#include <limits>\n", "#include <limits>\n#include <cstdlib>\n")
replace_once(
    "src/economy/demographics.cpp",
    "\t\tauto old_size = state.world.pop_get_size(ids);\n\t\tauto new_size = old_size * total_factor + old_size;\n\n\t\tstate.world.pop_set_size(ids,\n\t\t\t\tve::select((owner != dcon::nation_id{}), new_size, old_size));",
    "\t\tauto old_size = state.world.pop_get_size(ids);\n\t\tauto population_delta = old_size * total_factor;\n\n\t\tve::apply([&](dcon::pop_id pop, dcon::nation_id pop_owner, float delta) {\n\t\t\tif(!pop_owner) return;\n\t\t\tauto result = persons::exact_population::apply_population_lifecycle_delta(state, pop, delta);\n\t\t\tif(!result.complete) std::abort();\n\t\t}, ids, owner, population_delta);"
)

replace_between(
    "src/economy/demographics.cpp",
    "float transfer_pop_amount(sys::state& state, dcon::pop_id source, dcon::pop_id target,",
    "namespace impl {",
    """float transfer_pop_amount(sys::state& state, dcon::pop_id source, dcon::pop_id target,
\tfloat requested_amount, persons::exact_population::population_transition_cause cause) {
\tif(!source || !target || source == target
\t\t|| !state.world.pop_is_valid(source) || !state.world.pop_is_valid(target)
\t\t|| !std::isfinite(requested_amount) || requested_amount <= 0.f) {
\t\treturn 0.f;
\t}
\tif(!state.exact_population
\t\t|| persons::exact_population::source_cell_for_population(static_cast<sys::state const&>(state), source) == 0
\t\t|| persons::exact_population::source_cell_for_population(static_cast<sys::state const&>(state), target) == 0) {
\t\tstd::abort();
\t}
\n\tauto const source_size = state.world.pop_get_size(source);
\tauto const source_savings = state.world.pop_get_savings(source);
\tauto const target_savings = state.world.pop_get_savings(target);
\tif(!std::isfinite(source_size) || source_size <= 0.f
\t\t|| !std::isfinite(source_savings) || source_savings < 0.f
\t\t|| !std::isfinite(target_savings) || target_savings < 0.f
\t\t|| source_savings > std::numeric_limits<float>::max() - target_savings) {
\t\treturn 0.f;
\t}
\tauto requested = std::min(requested_amount, source_size);
\tauto transfer = persons::exact_population::transfer_population_membership(state, source, target, requested, cause);
\tif(!transfer.complete) std::abort();
\tauto const moved = transfer.population_amount_moved;
\tif(moved <= 0.f) return 0.f;
\tif(moved > source_size + 1.0e-5f) std::abort();
\tauto const remaining_size = std::max(0.f, source_size - moved);
\tauto const remaining_savings = moved >= source_size
\t\t? 0.f
\t\t: source_savings * (remaining_size / source_size);
\tauto const moved_savings = source_savings - remaining_savings;
\tif(!std::isfinite(remaining_savings) || remaining_savings < 0.f
\t\t|| !std::isfinite(moved_savings) || moved_savings < 0.f
\t\t|| moved_savings > std::numeric_limits<float>::max() - target_savings) {
\t\tstd::abort();
\t}
\tstate.world.pop_set_savings(source, remaining_savings);
\tstate.world.pop_set_savings(target, target_savings + moved_savings);
\treturn moved;
}

"""
)

replace_once(
    "src/economy/demographics.cpp",
    "\t\t\tif(state.exact_population\n\t\t\t\t&& persons::exact_population::source_cell_for_population(state, result.id) == 0)\n\t\t\t\t(void)persons::exact_population::register_population_cell(state, result.id);\n\t\t\treturn result;",
    "\t\t\tif(!state.exact_population\n\t\t\t\t|| persons::exact_population::source_cell_for_population(static_cast<sys::state const&>(state), result.id) == 0)\n\t\t\t\tstd::abort();\n\t\t\treturn result;"
)
replace_once(
    "src/economy/demographics.cpp",
    "\tif(state.exact_population)\n\t\t(void)persons::exact_population::register_population_cell(state, np.id);\n\treturn np;",
    "\tif(!state.exact_population) std::abort();\n\tauto registration = persons::exact_population::register_population_cell(state, np.id);\n\tif(registration.result != persons::exact_population::status::created\n\t\t&& registration.result != persons::exact_population::status::already_registered)\n\t\tstd::abort();\n\treturn np;"
)
replace_once(
    "src/economy/demographics.cpp",
    "void reduce_pop_size_safe(sys::state& state, dcon::pop_id pop_id, int32_t amount) {\n\tif(state.world.pop_get_size(pop_id) >= amount) {\n\t\tstate.world.pop_set_size(pop_id, state.world.pop_get_size(pop_id) - amount);\n\t} else {\n\t\tstate.world.pop_set_size(pop_id, 0);\n\t}\n}",
    "void reduce_pop_size_safe(sys::state& state, dcon::pop_id pop_id, int32_t amount) {\n\tif(amount <= 0) return;\n\tauto result = persons::exact_population::apply_population_lifecycle_delta(state, pop_id, -float(amount));\n\tif(!result.complete) std::abort();\n}"
)

# ---------------------------------------------------------------------------
# system_state.cpp: canonical store is mandatory, reconciliation cannot heal it.
# ---------------------------------------------------------------------------
replace_once("src/gamestate/system_state.cpp", "#include <algorithm>\n", "#include <algorithm>\n#include <cstdlib>\n")
replace_balanced_block(
    "src/gamestate/system_state.cpp",
    "if(gamerule::age_of_transformation_enabled(*this)\n\t\t&& persons::exact_population::cell_count(*this) == 0)",
    """{
\t\tauto exact_population = persons::exact_population::bootstrap_from_current_pops(*this);
\t\tif(!exact_population.complete || exact_population.unbound_populations != 0
\t\t\t|| persons::exact_population::synchronize_current_pop_bindings(*this) != 0
\t\t\t|| !persons::exact_population::project_population_membership(*this)) {
\t\t\tconsole_command_error += "?R Canonical population bootstrap/validation failed; refusing legacy fallback?W\\n";
\t\t\tstd::abort();
\t\t}
\t}"""
)
replace_balanced_block(
    "src/gamestate/system_state.cpp",
    "if(gamerule::age_of_transformation_enabled(*this)) {\n\t\tauto reconciliation = persons::exact_population::reconcile_population_lifecycle(*this);",
    """{
\t\tauto reconciliation = persons::exact_population::reconcile_population_lifecycle(*this);
\t\tif(!reconciliation.complete || reconciliation.unbound_populations != 0) {
\t\t\tconsole_command_error += "?R Canonical population invariant failed; refusing legacy fallback?W\\n";
\t\t\tstd::abort();
\t\t}
\t}"""
)

# ---------------------------------------------------------------------------
# serialization.cpp: absent AOEX may import once; malformed AOEX is fatal.
# ---------------------------------------------------------------------------
replace_once("src/gamestate/serialization.cpp", "#include <algorithm>\n", "#include <algorithm>\n#include <cstdlib>\n")
replace_between(
    "src/gamestate/serialization.cpp",
    "void restore_exact_runtime_state(sys::state& state, exact_runtime_snapshot const& snapshot) {",
    "bool needs_exact_population_bootstrap(exact_runtime_snapshot const& snapshot) {",
    """bool restore_exact_runtime_state(sys::state& state, exact_runtime_snapshot const& snapshot) {
\tclear_exact_runtime_state(state);
\tif(snapshot.extension_found && !snapshot.present) return false;
\tif(!snapshot.present) return true;
\tif(!persons::exact_population::import_snapshot(state, snapshot.population)
\t\t|| !economy::causal_order::import_snapshot(state, snapshot.causal_order)
\t\t|| !economy::exact_person_economy::import_snapshot(state, snapshot.economy)
\t\t|| !economy::physical::exact_person_goods::import_snapshot(state, snapshot.goods)
\t\t|| !economy::physical::exact_person_freight::import_snapshot(state, snapshot.freight)
\t\t|| !economy::physical::labor_dynamics::import_snapshot(state, snapshot.labor)) {
\t\tclear_exact_runtime_state(state);
\t\treturn false;
\t}
\treturn true;
}

"""
)
replace_once(
    "src/gamestate/serialization.cpp",
    "\trestore_exact_runtime_state(state, exact_runtime);",
    "\tif(!restore_exact_runtime_state(state, exact_runtime)) {\n\t\tstate.console_command_error += \"?R Canonical runtime save extension is invalid; refusing legacy fallback?W\\\\n\";\n\t\tstd::abort();\n\t}"
)
# There are two load paths; patch the second occurrence after the first replacement.
text = read("src/gamestate/serialization.cpp")
old_restore = "\trestore_exact_runtime_state(state, exact_runtime);"
if text.count(old_restore) != 1:
    raise RuntimeError(f"serialization.cpp: expected one remaining restore call, found {text.count(old_restore)}")
write("src/gamestate/serialization.cpp", text.replace(
    old_restore,
    "\tif(!restore_exact_runtime_state(state, exact_runtime)) {\n\t\tstate.console_command_error += \"?R Canonical runtime save extension is invalid; refusing legacy fallback?W\\\\n\";\n\t\tstd::abort();\n\t}",
    1
))
text = read("src/gamestate/serialization.cpp")
text = text.replace(
    "if(needs_exact_population_bootstrap(exact_runtime)\n\t\t&& gamerule::age_of_transformation_enabled(state)) {",
    "if(needs_exact_population_bootstrap(exact_runtime)) {"
)
if text.count("if(needs_exact_population_bootstrap(exact_runtime)) {") != 2:
    raise RuntimeError("serialization.cpp: expected two canonical bootstrap sites")
text = text.replace(
    "\t\tif(!bootstrap.complete)\n\t\t\tstate.console_command_error += std::string(\"?R Exact population migration failed for POP \")\n\t\t\t\t+ std::to_string(bootstrap.failed_population.index()) + \"?W\\\\n\";",
    "\t\tif(!bootstrap.complete || bootstrap.unbound_populations != 0) {\n\t\t\tstate.console_command_error += std::string(\"?R Canonical population import failed for POP \")\n\t\t\t\t+ std::to_string(bootstrap.failed_population.index()) + \"?W\\\\n\";\n\t\t\tstd::abort();\n\t\t}"
)
write("src/gamestate/serialization.cpp", text)

# ---------------------------------------------------------------------------
# Fix the unrelated current main linker failure so CI tests the cutover itself.
# ---------------------------------------------------------------------------
replace_once(
    "src/main.cpp",
    "#include \"diplomatic_crisis_dynamics.cpp\"\n#include \"policy_execution.cpp\"",
    "#include \"diplomatic_crisis_dynamics.cpp\"\n#include \"strategic_statecraft.cpp\"\n#include \"policy_execution.cpp\""
)

# Hard guard: after this migration, demographics may not write POP size directly.
demo = read("src/economy/demographics.cpp")
if "pop_set_size" in demo:
    raise RuntimeError("demographics.cpp still contains a direct pop_set_size write")

print("canonical population hard cutover applied")
