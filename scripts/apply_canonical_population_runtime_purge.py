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
        raise RuntimeError(f"{path}: expected one match, found {count}: {old[:100]!r}")
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


# Household relocation: exact membership is the mutation; DCON is projection.
replace_once(
    "src/economy/physical/household_mobility.cpp",
    "#include <cmath>\n",
    "#include <cmath>\n#include <cstdlib>\n"
)
replace_between(
    "src/economy/physical/household_mobility.cpp",
    "dcon::pop_id pop_for_cell_id(sys::state const& state, uint32_t source_cell) {",
    "dcon::pop_id pop_for_person_cell(sys::state& state, uint32_t source_cell,",
    """dcon::pop_id pop_for_cell_id(sys::state const& state, uint32_t source_cell) {
\tif(source_cell == 0 || !state.exact_population) return {};
\treturn persons::exact_population::population_for_source_cell(state, source_cell);
}

"""
)
replace_between(
    "src/economy/physical/household_mobility.cpp",
    "dcon::pop_id find_or_create_population_cell(sys::state& state, dcon::province_id province,",
    "bool transfer_population_unit(sys::state& state, uint32_t source_cell,",
    """dcon::pop_id find_or_create_population_cell(sys::state& state, dcon::province_id province,
\tdcon::pop_id source) {
\tif(!state.exact_population || !province || !source) return {};
\tauto culture = state.world.pop_get_culture(source);
\tauto religion = state.world.pop_get_religion(source);
\tauto type = state.world.pop_get_poptype(source);
\tdcon::pop_id result{};
\tstate.world.province_for_each_pop_location(province, [&](auto relation) {
\t\tauto pop = state.world.pop_location_get_pop(relation);
\t\tif(!result && pop && state.world.pop_get_culture(pop) == culture
\t\t\t&& state.world.pop_get_religion(pop) == religion
\t\t\t&& state.world.pop_get_poptype(pop) == type) result = pop;
\t});
\tif(result) {
\t\tif(persons::exact_population::source_cell_for_population(
\t\t\tstatic_cast<sys::state const&>(state), result) == 0) std::abort();
\t\treturn result;
\t}
\tresult = state.world.create_pop();
\tstate.world.force_create_pop_location(result, province);
\tstate.world.pop_set_culture(result, culture);
\tstate.world.pop_set_religion(result, religion);
\tstate.world.pop_set_poptype(result, type);
\tstate.world.pop_set_savings(result, 0.0f);
\tstate.world.pop_set_uemployment(result, state.world.pop_get_uemployment(source));
\tstate.world.pop_set_uliteracy(result, state.world.pop_get_uliteracy(source));
\tstate.world.pop_set_umilitancy(result, state.world.pop_get_umilitancy(source));
\tstate.world.pop_set_uconsciousness(result, state.world.pop_get_uconsciousness(source));
\tstate.world.pop_set_satisfaction(result, state.world.pop_get_satisfaction(source));
\tstate.world.pop_set_is_primary_or_accepted_culture(result,
\t\tstate.world.pop_get_is_primary_or_accepted_culture(source));
\tauto registration = persons::exact_population::register_population_cell(state, result);
\tif(registration.result != persons::exact_population::status::created
\t\t&& registration.result != persons::exact_population::status::already_registered)
\t\tstd::abort();
\treturn result;
}

"""
)
replace_between(
    "src/economy/physical/household_mobility.cpp",
    "bool transfer_population_unit(sys::state& state, uint32_t source_cell,",
    "bool same_nation(sys::state const& state, dcon::province_id left, dcon::province_id right) {",
    """bool transfer_population_unit(sys::state& state, uint32_t source_cell,
\tdcon::province_id origin, dcon::province_id destination,
\tdcon::culture_id culture, dcon::religion_id religion, dcon::pop_type_id type,
\tstd::optional<persons::exact_population::person_key> member = std::nullopt) {
\tif(!source_cell || !origin || !destination || origin == destination) return true;
\tif(!state.exact_population) std::abort();
\tauto source = pop_for_person_cell(state, source_cell, origin, culture, religion, type);
\tif(!source) return false;
\tauto target = find_or_create_population_cell(state, destination, source);
\tif(!target || target == source) return false;
\tif(persons::exact_population::source_cell_for_population(
\t\tstatic_cast<sys::state const&>(state), source) == 0
\t\t|| persons::exact_population::source_cell_for_population(
\t\t\tstatic_cast<sys::state const&>(state), target) == 0) std::abort();
\tif(member && (!persons::exact_population::exists(state, *member)
\t\t|| !persons::exact_population::alive(state, *member)
\t\t|| persons::exact_population::current_population_cell(state, *member) != source_cell)) return false;
\tconstexpr float population_units_per_literal_person =
\t\t1.0f / float(persons::population_materialization::literal_person_multiplier);
\tauto source_size = state.world.pop_get_size(source);
\tauto source_savings = state.world.pop_get_savings(source);
\tauto target_savings = state.world.pop_get_savings(target);
\tif(!std::isfinite(source_size) || source_size < population_units_per_literal_person
\t\t|| !std::isfinite(source_savings) || source_savings < 0.0f
\t\t|| !std::isfinite(target_savings) || target_savings < 0.0f) return false;
\tauto moved_savings = source_size > epsilon
\t\t? source_savings * population_units_per_literal_person / source_size : 0.0f;
\tif(!std::isfinite(moved_savings) || moved_savings > source_savings
\t\t|| moved_savings > std::numeric_limits<float>::max() - target_savings) return false;
\tbool transferred = false;
\tif(member) {
\t\ttransferred = persons::exact_population::transfer_population_person_membership(state, *member,
\t\t\ttarget, persons::exact_population::population_transition_cause::household_relocation);
\t} else {
\t\tauto transfer = persons::exact_population::transfer_population_membership(state, source, target,
\t\t\tpopulation_units_per_literal_person,
\t\t\tpersons::exact_population::population_transition_cause::household_relocation);
\t\tif(!transfer.complete) std::abort();
\t\ttransferred = transfer.people_moved == 1;
\t}
\tif(!transferred) return false;
\tstate.world.pop_set_savings(source, source_savings - moved_savings);
\tstate.world.pop_set_savings(target, target_savings + moved_savings);
\treturn true;
}

"""
)

# Military losses are canonical deaths, never direct POP shrinkage.
replace_once("src/military/military.cpp", "#include <chrono>\n", "#include <chrono>\n#include <cstdlib>\n")
replace_once(
    "src/military/military.cpp",
    "\t\t\t\tif(backing_pop) {\n\t\t\t\t\tauto& psize = state.world.pop_get_size(backing_pop);\n\t\t\t\t\tfloat damage_modifier = std::max(state.defines.soldier_to_pop_damage - state.world.nation_get_modifier_values(tech_nation, sys::national_mod_offsets::soldier_to_pop_loss), 0.0f);\n\t\t\t\t\tstate.world.pop_set_size(backing_pop, psize - state.defines.pop_size_per_regiment * pending_combat_damage * damage_modifier);\n\t\t\t\t}",
    "\t\t\t\tif(backing_pop) {\n\t\t\t\t\tfloat damage_modifier = std::max(state.defines.soldier_to_pop_damage - state.world.nation_get_modifier_values(tech_nation, sys::national_mod_offsets::soldier_to_pop_loss), 0.0f);\n\t\t\t\t\tauto loss = state.defines.pop_size_per_regiment * pending_combat_damage * damage_modifier;\n\t\t\t\t\tauto result = persons::exact_population::apply_population_lifecycle_delta(state, backing_pop, -loss);\n\t\t\t\t\tif(!result.complete) std::abort();\n\t\t\t\t}"
)
replace_once(
    "src/military/military.cpp",
    "\t\t\t\tif(backing_pop) {\n\t\t\t\t\tauto& psize = state.world.pop_get_size(backing_pop);\n\t\t\t\t\tfloat damage_modifier = std::max(state.defines.soldier_to_pop_damage - state.world.nation_get_modifier_values(tech_nation, sys::national_mod_offsets::soldier_to_pop_loss), 0.0f);\n\t\t\t\t\tstate.world.pop_set_size(backing_pop, psize - state.defines.pop_size_per_regiment * pending_attrition_damage * damage_modifier);\n\t\t\t\t}",
    "\t\t\t\tif(backing_pop) {\n\t\t\t\t\tfloat damage_modifier = std::max(state.defines.soldier_to_pop_damage - state.world.nation_get_modifier_values(tech_nation, sys::national_mod_offsets::soldier_to_pop_loss), 0.0f);\n\t\t\t\t\tauto loss = state.defines.pop_size_per_regiment * pending_attrition_damage * damage_modifier;\n\t\t\t\t\tauto result = persons::exact_population::apply_population_lifecycle_delta(state, backing_pop, -loss);\n\t\t\t\t\tif(!result.complete) std::abort();\n\t\t\t\t}"
)

# Scripted population effects mutate canonical lifecycle state.
replace_once("src/scripting/effects.cpp", "#include \"events.hpp\"\n", "#include \"events.hpp\"\n#include \"persons/exact_population.hpp\"\n#include <cstdlib>\n")
replace_between(
    "src/scripting/effects.cpp",
    "uint32_t ef_reduce_pop(EFFECT_PARAMTERS) {",
    "uint32_t ef_move_pop(EFFECT_PARAMTERS) {",
    """namespace {
void apply_canonical_population_factor(sys::state& state, dcon::pop_id pop, float factor) {
\tif(!pop || !state.world.pop_is_valid(pop) || !std::isfinite(factor)) return;
\tauto current = state.world.pop_get_size(pop);
\tauto result = persons::exact_population::apply_population_lifecycle_delta(
\t\tstate, pop, current * factor - current);
\tif(!result.complete) std::abort();
}
}

uint32_t ef_reduce_pop(EFFECT_PARAMTERS) {
\tauto amount = trigger::read_float_from_payload(tval + 1);
\tassert(std::isfinite(amount));
\tapply_canonical_population_factor(ws, trigger::to_pop(primary_slot), amount);
\treturn 0;
}
uint32_t ef_reduce_pop_abs(EFFECT_PARAMTERS) {
\tauto amount = trigger::read_int32_t_from_payload(tval + 1);
\tdemographics::reduce_pop_size_safe(ws, trigger::to_pop(primary_slot), amount);
\treturn 0;
}
uint32_t ef_reduce_pop_province(EFFECT_PARAMTERS) {
\tauto amount = trigger::read_float_from_payload(tval + 1);
\tassert(std::isfinite(amount));
\tfor(auto p : ws.world.province_get_pop_location(trigger::to_prov(primary_slot)))
\t\tapply_canonical_population_factor(ws, p.get_pop(), amount);
\treturn 0;
}
uint32_t ef_reduce_pop_nation(EFFECT_PARAMTERS) {
\tauto amount = trigger::read_float_from_payload(tval + 1);
\tassert(std::isfinite(amount));
\tfor(auto pr : ws.world.nation_get_province_ownership(trigger::to_nation(primary_slot)))
\t\tfor(auto p : pr.get_province().get_pop_location())
\t\t\tapply_canonical_population_factor(ws, p.get_pop(), amount);
\treturn 0;
}
uint32_t ef_reduce_pop_state(EFFECT_PARAMTERS) {
\tauto amount = trigger::read_float_from_payload(tval + 1);
\tassert(std::isfinite(amount));
\tprovince::for_each_province_in_state_instance(ws, trigger::to_state(primary_slot), [&ws, amount](dcon::province_id pr) {
\t\tfor(auto p : ws.world.province_get_pop_location(pr))
\t\t\tapply_canonical_population_factor(ws, p.get_pop(), amount);
\t});
\treturn 0;
}
"""
)

# Runtime source files may only project size from exact_population. Synthetic
# runners/materialization measurements and test fixtures are initialization code.
for path in [
    "src/economy/demographics.cpp",
    "src/economy/physical/household_mobility.cpp",
    "src/military/military.cpp",
    "src/scripting/effects.cpp",
]:
    text = read(path)
    if "pop_set_size" in text or ".get_pop().set_size(" in text:
        raise RuntimeError(f"{path}: direct runtime POP-size mutation remains")

print("remaining population runtime paths purged")
