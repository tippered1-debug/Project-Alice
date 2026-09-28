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


replace_once(
    "src/economy/demographics.hpp",
    "float transfer_pop_amount(sys::state& state, dcon::pop_id source, dcon::pop_id target,\n\tfloat requested_amount,",
    "dcon::pop_id find_or_make_canonical_pop(sys::state& state, dcon::province_id province,\n\tdcon::culture_id culture, dcon::religion_id religion, dcon::pop_type_id type, float literacy);\n\nfloat transfer_pop_amount(sys::state& state, dcon::pop_id source, dcon::pop_id target,\n\tfloat requested_amount,"
)

replace_once(
    "src/economy/demographics.cpp",
    "}\n} // namespace impl\n\nvoid apply_type_changes(sys::state& state, uint32_t offset, uint32_t divisions, promotion_buffer& promotion_buf, promotion_buffer& demotion_buf) {",
    "}\n} // namespace impl\n\ndcon::pop_id find_or_make_canonical_pop(sys::state& state, dcon::province_id province,\n\tdcon::culture_id culture, dcon::religion_id religion, dcon::pop_type_id type, float literacy) {\n\tif(!state.exact_population) std::abort();\n\tauto pop = impl::find_or_make_pop(state, province, culture, religion, type, literacy);\n\tif(!pop || persons::exact_population::source_cell_for_population(\n\t\tstatic_cast<sys::state const&>(state), pop) == 0) std::abort();\n\treturn pop;\n}\n\nvoid apply_type_changes(sys::state& state, uint32_t offset, uint32_t divisions, promotion_buffer& promotion_buf, promotion_buffer& demotion_buf) {"
)

replace_once(
    "src/scripting/effects.cpp",
    "uint32_t ef_move_pop(EFFECT_PARAMTERS) {\n\tws.world.pop_set_province_from_pop_location(trigger::to_pop(primary_slot), trigger::payload(tval[1]).prov_id);\n\treturn 0;\n}\nuint32_t ef_pop_type(EFFECT_PARAMTERS) {\n\tws.world.pop_set_poptype(trigger::to_pop(primary_slot), trigger::payload(tval[1]).popt_id);\n\treturn 0;\n}",
    "uint32_t ef_move_pop(EFFECT_PARAMTERS) {\n\tauto source = trigger::to_pop(primary_slot);\n\tauto destination = trigger::payload(tval[1]).prov_id;\n\tif(!source || !ws.world.pop_is_valid(source) || !destination || !ws.world.province_is_valid(destination))\n\t\treturn 0;\n\tauto source_size = ws.world.pop_get_size(source);\n\tif(source_size <= 0.0f) return 0;\n\tauto target = demographics::find_or_make_canonical_pop(ws, destination,\n\t\tws.world.pop_get_culture(source), ws.world.pop_get_religion(source),\n\t\tws.world.pop_get_poptype(source), pop_demographics::get_literacy(ws, source));\n\tif(target == source) return 0;\n\tauto moved = demographics::transfer_pop_amount(ws, source, target, source_size,\n\t\tpersons::exact_population::population_transition_cause::internal_migration);\n\tif(std::abs(moved - source_size) > 1.0e-5f) std::abort();\n\treturn 0;\n}\nuint32_t ef_pop_type(EFFECT_PARAMTERS) {\n\tauto source = trigger::to_pop(primary_slot);\n\tauto target_type = trigger::payload(tval[1]).popt_id;\n\tif(!source || !ws.world.pop_is_valid(source) || !target_type || !ws.world.pop_type_is_valid(target_type))\n\t\treturn 0;\n\tauto source_size = ws.world.pop_get_size(source);\n\tif(source_size <= 0.0f || ws.world.pop_get_poptype(source) == target_type) return 0;\n\tauto province = ws.world.pop_get_province_from_pop_location(source);\n\tauto target = demographics::find_or_make_canonical_pop(ws, province,\n\t\tws.world.pop_get_culture(source), ws.world.pop_get_religion(source),\n\t\ttarget_type, pop_demographics::get_literacy(ws, source));\n\tauto moved = demographics::transfer_pop_amount(ws, source, target, source_size,\n\t\tpersons::exact_population::population_transition_cause::population_merge);\n\tif(std::abs(moved - source_size) > 1.0e-5f) std::abort();\n\treturn 0;\n}"
)

# The scripted population API may not directly mutate location or type anymore.
effects = read("src/scripting/effects.cpp")
for forbidden in ("pop_set_province_from_pop_location", "pop_set_poptype"):
    if forbidden in effects:
        raise RuntimeError(f"effects.cpp still contains forbidden direct population mutation: {forbidden}")

print("scripted population cutover applied")
