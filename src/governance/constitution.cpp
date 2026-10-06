#include "constitution.hpp"

#include "system_state.hpp"
#include "governance/law/law.hpp"
#include "governance/elections.hpp"
#include "governance/offices.hpp"
#include "persons/exact_population.hpp"
#include "persons/persons.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <map>
#include <set>
#include <unordered_set>

namespace governance::constitution {
namespace {

// ---------------------------------------------------------------- names

constexpr std::array<std::string_view, 21> institution_kind_names{
	"central_government", "ministry", "central_bank", "tax_authority", "regulator", "court", "prosecutor", "military_command",
	"regional_government", "municipality", "other", "household_sector", "finance_ministry", "education_ministry",
	"interior_ministry", "public_works_ministry", "head_of_state", "cabinet", "legislature", "legislative_chamber", "agency" };
constexpr std::array<std::string_view, 19> office_kind_names{
	"president", "prime_minister", "finance_minister", "central_bank_governor", "chief_of_general_staff", "mayor",
	"agency_director", "judge_seat", "other", "head_of_state", "head_of_government", "minister", "legislator", "speaker",
	"regional_governor", "chief_justice", "prosecutor_general", "heir", "deputy_head_of_state" };
constexpr std::array<std::string_view, 19> authority_kind_names{
	"administer", "regulate", "levy_tax", "spend_public_funds", "license", "enforce", "adjudicate", "command_forces",
	"appoint", "dismiss", "issue_currency", "expropriate", "other", "issue_public_debt", "legislate", "confirm", "assent",
	"amend_constitution", "appropriate" };
constexpr std::array<std::string_view, 12> service_kind_names{
	"none", "administration", "education", "policing", "construction", "revenue", "monetary", "defense", "legislation", "justice",
	"intelligence", "counterintelligence" };

template<size_t N>
int32_t index_of(std::array<std::string_view, N> const& names, std::string_view value) {
	for(size_t i = 0; i < N; ++i) if(names[i] == value) return int32_t(i);
	return -1;
}

// ---------------------------------------------------------------- parsing

std::string_view trim(std::string_view value) {
	while(!value.empty() && (value.front() == ' ' || value.front() == '\t' || value.front() == '\r')) value.remove_prefix(1);
	while(!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r')) value.remove_suffix(1);
	return value;
}

struct parsed_line {
	std::vector<std::string> cells;
	uint32_t line = 0;
};

bool read_rows(std::string_view text, std::string_view table, std::string_view header, std::vector<parsed_line>& rows, std::string& error) {
	uint32_t line_number = 0;
	bool header_seen = false;
	size_t columns = size_t(std::count(header.begin(), header.end(), ';')) + 1;
	while(!text.empty()) {
		auto newline = text.find('\n');
		auto line = trim(text.substr(0, newline));
		text = newline == std::string_view::npos ? std::string_view{} : text.substr(newline + 1);
		++line_number;
		if(line.empty() || line.front() == '#') continue;
		if(!header_seen) {
			header_seen = true;
			if(line != header) {
				error = std::string(table) + ":" + std::to_string(line_number) + ": header must be '" + std::string(header) + "'";
				return false;
			}
			continue;
		}
		parsed_line row{ {}, line_number };
		while(true) {
			auto delimiter = line.find(';');
			row.cells.emplace_back(trim(line.substr(0, delimiter)));
			if(delimiter == std::string_view::npos) break;
			line.remove_prefix(delimiter + 1);
		}
		if(row.cells.size() != columns) {
			error = std::string(table) + ":" + std::to_string(line_number) + ": expected " + std::to_string(columns) + " columns";
			return false;
		}
		rows.push_back(std::move(row));
	}
	if(!header_seen) {
		error = std::string(table) + ": missing header";
		return false;
	}
	return true;
}

template<typename T>
bool parse_number(std::string const& text, T& result) {
	if(text.empty()) return false;
	if constexpr(std::is_floating_point_v<T>) {
		char* end = nullptr;
		auto value = std::strtod(text.c_str(), &end);
		if(end != text.c_str() + text.size() || !std::isfinite(value)) return false;
		result = T(value);
		return true;
	} else {
		auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), result);
		return ec == std::errc{} && end == text.data() + text.size();
	}
}

std::string row_error(std::string_view table, uint32_t line, std::string_view message) {
	return std::string(table) + ":" + std::to_string(line) + ": " + std::string(message);
}

constexpr std::string_view institutions_header = "key;kind;parent;scope;independent;service;staffing_per_capita;staff_occupation;wage_multiplier";
constexpr std::string_view offices_header = "key;institution;kind;appointer;confirmer;removal;remover;term_days;succession;successor;exclusive;seats";
constexpr std::string_view authorities_header = "holder;kind;scope;delegated_from;source";
constexpr std::string_view elections_header = "body;system;districts;term_days";
constexpr std::array<std::string_view, 7> electoral_system_names{ "none", "proportional", "plurality", "direct", "legislative", "presiding", "running_mate" };
constexpr std::array<std::string_view, 3> district_names{ "national", "regional", "own" };

// ---------------------------------------------------------------- models

constexpr std::string_view common_institutions = R"(
state;central_government;;national;0;none;0;0;1
head_of_state;head_of_state;state;national;0;none;0;0;1
cabinet;cabinet;state;national;0;none;0;0;1
finance;finance_ministry;cabinet;national;0;administration;0.00035;2;1.25
tax;tax_authority;finance;national;0;revenue;0;0;1
interior;interior_ministry;cabinet;national;0;policing;0.0015;0;0.85
intelligence;agency;state;national;0;intelligence;0.00008;2;1
counterintelligence;agency;state;national;0;counterintelligence;0.00004;2;1
education;education_ministry;cabinet;national;0;education;0.001;2;1.15
works;public_works_ministry;cabinet;national;0;construction;0;0;1
defense;military_command;head_of_state;national;0;defense;0;0;1
prosecution;prosecutor;state;national;0;justice;0;0;1
legislature;legislature;state;national;0;legislation;0;0;1
region;regional_government;state;regional;0;administration;0.0001;2;1
capital;municipality;state;capital;0;administration;0.00015;2;1
)";

constexpr std::string_view common_offices = R"(
finance_minister;finance;finance_minister;chief_executive;;appointer;;0;acting;chief_executive;0;1
interior_minister;interior;minister;chief_executive;;appointer;;0;acting;chief_executive;0;1
education_minister;education;minister;chief_executive;;appointer;;0;acting;chief_executive;0;1
works_minister;works;minister;chief_executive;;appointer;;0;acting;chief_executive;0;1
tax_director;tax;agency_director;finance_minister;;appointer;;0;acting;finance_minister;0;1
intelligence_director;intelligence;agency_director;chief_executive;;appointer;;0;none;;1;1
counterintelligence_director;counterintelligence;agency_director;chief_executive;;appointer;;0;none;;1;1
prosecutor_general;prosecution;prosecutor_general;chief_executive;;appointer;;0;none;;1;1
commander;defense;chief_of_general_staff;chief_executive;;appointer;;0;none;;1;1
)";

constexpr std::string_view common_authorities = R"(
institution:cabinet;administer;national;;constitution
institution:finance;administer;national;;constitution
institution:finance;spend_public_funds;national;;constitution
institution:finance;appropriate;national;;constitution
institution:finance;levy_tax;national;;constitution
institution:tax;levy_tax;national;institution:finance;
institution:tax;spend_public_funds;national;institution:finance;
institution:interior;administer;national;institution:cabinet;
institution:interior;spend_public_funds;national;;constitution
institution:interior;enforce;national;;constitution
institution:intelligence;administer;national;;constitution
institution:counterintelligence;administer;national;;constitution
office:intelligence_director;administer;national;institution:intelligence;
office:counterintelligence_director;administer;national;institution:counterintelligence;
institution:education;administer;national;institution:cabinet;
institution:education;spend_public_funds;national;;constitution
institution:works;administer;national;institution:cabinet;
institution:works;spend_public_funds;national;;constitution
institution:prosecution;enforce;national;;constitution
office:finance_minister;regulate;national;;constitution
office:finance_minister;issue_public_debt;national;;constitution
office:finance_minister;spend_public_funds;national;;constitution
office:finance_minister;appoint;national;;constitution
office:finance_minister;dismiss;national;;constitution
office:interior_minister;administer;national;institution:interior;
office:education_minister;administer;national;institution:education;
office:works_minister;administer;national;institution:works;
office:works_minister;license;national;;constitution
office:tax_director;levy_tax;national;institution:tax;
office:prosecutor_general;enforce;national;institution:prosecution;
)";

struct executive_model {
	std::string_view name, institutions, offices, authorities, elections;
};

// Rows every legislature with two elected chambers shares.
#define ALICE_BICAMERAL_INSTITUTIONS \
	"lower_chamber;legislative_chamber;legislature;national;0;legislation;0;0;1\n" \
	"upper_chamber;legislative_chamber;legislature;national;0;legislation;0;0;1\n"
#define ALICE_BICAMERAL_OFFICES \
	"speaker;lower_chamber;speaker;;;irremovable;;0;none;;0;1\n" \
	"lower_seat;lower_chamber;legislator;;;irremovable;;0;none;;0;7\n" \
	"upper_seat;upper_chamber;legislator;;;irremovable;;0;none;;0;5\n"
#define ALICE_BICAMERAL_AUTHORITIES \
	"institution:lower_chamber;legislate;national;;constitution\n" \
	"institution:upper_chamber;legislate;national;;constitution\n" \
	"institution:lower_chamber;amend_constitution;national;;constitution\n" \
	"institution:upper_chamber;amend_constitution;national;;constitution\n" \
	"institution:lower_chamber;confirm;national;;constitution\n" \
	"institution:upper_chamber;confirm;national;;constitution\n"
#define ALICE_INDEPENDENT_BODIES \
	"central_bank;central_bank;state;national;1;monetary;0;0;1\n" \
	"judiciary;court;state;national;1;justice;0;0;1\n"
#define ALICE_SUBORDINATE_BODIES \
	"central_bank;central_bank;cabinet;national;0;monetary;0;0;1\n" \
	"judiciary;court;state;national;0;justice;0;0;1\n"
#define ALICE_SHARED_BODY_AUTHORITIES \
	"institution:central_bank;issue_currency;national;;constitution\n" \
	"institution:central_bank;administer;national;;constitution\n" \
	"office:bank_governor;issue_currency;national;institution:central_bank;\n" \
	"institution:judiciary;adjudicate;national;;constitution\n" \
	"office:chief_justice;adjudicate;national;institution:judiciary;\n"
// The forces answer to the military command, which holds command under the
// commander in chief's constitutional power.
#define ALICE_COMMAND_UNDER(office) \
	"institution:defense;command_forces;national;office:" office ";\n" \
	"office:commander;command_forces;national;institution:defense;\n"

constexpr std::array<executive_model, 7> executive_models{ {
	{ "parliamentary_republic",
		ALICE_INDEPENDENT_BODIES ALICE_BICAMERAL_INSTITUTIONS,
		"head_of_state;head_of_state;head_of_state;;;irremovable;;0;acting;speaker;1;1\n"
		"chief_executive;cabinet;head_of_government;head_of_state;lower_chamber;no_confidence;;0;none;;0;1\n"
		"bank_governor;central_bank;central_bank_governor;head_of_state;lower_chamber;irremovable;;0;none;;1;1\n"
		"chief_justice;judiciary;chief_justice;head_of_state;upper_chamber;irremovable;;0;none;;1;1\n"
		ALICE_BICAMERAL_OFFICES,
		ALICE_BICAMERAL_AUTHORITIES
		"office:head_of_state;assent;national;;constitution\n"
		"office:head_of_state;appoint;national;;constitution\n"
		"office:head_of_state;dismiss;national;;constitution\n"
		"office:head_of_state;command_forces;national;;constitution\n"
		"office:chief_executive;appoint;national;;constitution\n"
		"office:chief_executive;dismiss;national;;constitution\n"
		"office:chief_executive;administer;national;;constitution\n"
		"office:chief_executive;regulate;national;;constitution\n"
		ALICE_COMMAND_UNDER("head_of_state"),
		"institution:lower_chamber;proportional;national;1460\n" "institution:upper_chamber;plurality;regional;2190\n" "office:head_of_state;legislative;national;1825\n" "office:speaker;presiding;national;0\n" },
	{ "parliamentary_monarchy",
		ALICE_INDEPENDENT_BODIES ALICE_BICAMERAL_INSTITUTIONS,
		"head_of_state;head_of_state;head_of_state;;;irremovable;;0;full;heir;1;1\n"
		"heir;head_of_state;heir;;;irremovable;;0;none;;1;1\n"
		"chief_executive;cabinet;head_of_government;head_of_state;lower_chamber;no_confidence;;0;none;;0;1\n"
		"bank_governor;central_bank;central_bank_governor;head_of_state;lower_chamber;irremovable;;0;none;;1;1\n"
		"chief_justice;judiciary;chief_justice;head_of_state;upper_chamber;irremovable;;0;none;;1;1\n"
		ALICE_BICAMERAL_OFFICES,
		ALICE_BICAMERAL_AUTHORITIES
		"office:head_of_state;assent;national;;constitution\n"
		"office:head_of_state;appoint;national;;constitution\n"
		"office:head_of_state;dismiss;national;;constitution\n"
		"office:head_of_state;command_forces;national;;constitution\n"
		"office:chief_executive;appoint;national;;constitution\n"
		"office:chief_executive;dismiss;national;;constitution\n"
		"office:chief_executive;administer;national;;constitution\n"
		"office:chief_executive;regulate;national;;constitution\n"
		ALICE_COMMAND_UNDER("head_of_state"),
		"institution:lower_chamber;proportional;national;1460\n" "institution:upper_chamber;plurality;regional;2190\n" "office:speaker;presiding;national;0\n" },
	{ "presidential",
		ALICE_INDEPENDENT_BODIES ALICE_BICAMERAL_INSTITUTIONS,
		"chief_executive;head_of_state;head_of_state;;;irremovable;;0;full;vice_president;1;1\n"
		"vice_president;head_of_state;deputy_head_of_state;;;irremovable;;0;none;;1;1\n"
		"bank_governor;central_bank;central_bank_governor;chief_executive;upper_chamber;irremovable;;0;none;;1;1\n"
		"chief_justice;judiciary;chief_justice;chief_executive;upper_chamber;irremovable;;0;none;;1;1\n"
		ALICE_BICAMERAL_OFFICES,
		ALICE_BICAMERAL_AUTHORITIES
		"office:chief_executive;assent;national;;constitution\n"
		"office:chief_executive;appoint;national;;constitution\n"
		"office:chief_executive;dismiss;national;;constitution\n"
		"office:chief_executive;administer;national;;constitution\n"
		"office:chief_executive;regulate;national;;constitution\n"
		"office:chief_executive;command_forces;national;;constitution\n"
		ALICE_COMMAND_UNDER("chief_executive"),
		"office:chief_executive;direct;national;1460\n" "office:vice_president;running_mate;national;0\n" "institution:lower_chamber;plurality;regional;730\n" "institution:upper_chamber;plurality;regional;2190\n" "office:speaker;presiding;national;0\n" },
	{ "semi_presidential",
		ALICE_INDEPENDENT_BODIES ALICE_BICAMERAL_INSTITUTIONS,
		"head_of_state;head_of_state;head_of_state;;;irremovable;;0;acting;speaker;1;1\n"
		"chief_executive;cabinet;head_of_government;head_of_state;lower_chamber;no_confidence;;0;none;;0;1\n"
		"bank_governor;central_bank;central_bank_governor;head_of_state;upper_chamber;irremovable;;0;none;;1;1\n"
		"chief_justice;judiciary;chief_justice;head_of_state;upper_chamber;irremovable;;0;none;;1;1\n"
		ALICE_BICAMERAL_OFFICES,
		ALICE_BICAMERAL_AUTHORITIES
		"office:head_of_state;assent;national;;constitution\n"
		"office:head_of_state;appoint;national;;constitution\n"
		"office:head_of_state;dismiss;national;;constitution\n"
		"office:head_of_state;command_forces;national;;constitution\n"
		"office:chief_executive;appoint;national;;constitution\n"
		"office:chief_executive;dismiss;national;;constitution\n"
		"office:chief_executive;administer;national;;constitution\n"
		"office:chief_executive;regulate;national;;constitution\n"
		ALICE_COMMAND_UNDER("head_of_state"),
		"office:head_of_state;direct;national;1825\n" "institution:lower_chamber;proportional;national;1825\n" "institution:upper_chamber;plurality;regional;2190\n" "office:speaker;presiding;national;0\n" },
	{ "dual_monarchy",
		ALICE_SUBORDINATE_BODIES ALICE_BICAMERAL_INSTITUTIONS,
		"head_of_state;head_of_state;head_of_state;;;irremovable;;0;full;heir;1;1\n"
		"heir;head_of_state;heir;;;irremovable;;0;none;;1;1\n"
		"chief_executive;cabinet;head_of_government;head_of_state;;remover;head_of_state;0;none;;0;1\n"
		"bank_governor;central_bank;central_bank_governor;head_of_state;;appointer;;0;none;;1;1\n"
		"chief_justice;judiciary;chief_justice;head_of_state;;appointer;;0;none;;1;1\n"
		ALICE_BICAMERAL_OFFICES,
		ALICE_BICAMERAL_AUTHORITIES
		"office:head_of_state;assent;national;;constitution\n"
		"office:head_of_state;appoint;national;;constitution\n"
		"office:head_of_state;dismiss;national;;constitution\n"
		"office:head_of_state;regulate;national;;constitution\n"
		"office:head_of_state;command_forces;national;;constitution\n"
		"office:chief_executive;appoint;national;;constitution\n"
		"office:chief_executive;dismiss;national;;constitution\n"
		"office:chief_executive;administer;national;;constitution\n"
		"office:chief_executive;regulate;national;;constitution\n"
		ALICE_COMMAND_UNDER("head_of_state"),
		"institution:lower_chamber;plurality;regional;1460\n" "institution:upper_chamber;plurality;regional;2190\n" "office:speaker;presiding;national;0\n" },
	{ "absolute_monarchy",
		ALICE_SUBORDINATE_BODIES
		"lower_chamber;legislative_chamber;legislature;national;0;legislation;0;0;1\n",
		"chief_executive;head_of_state;head_of_state;;;irremovable;;0;full;heir;1;1\n"
		"heir;head_of_state;heir;;;irremovable;;0;none;;1;1\n"
		"bank_governor;central_bank;central_bank_governor;chief_executive;;appointer;;0;none;;1;1\n"
		"chief_justice;judiciary;chief_justice;chief_executive;;appointer;;0;none;;1;1\n"
		"lower_seat;lower_chamber;legislator;chief_executive;;appointer;;0;none;;0;7\n",
		"office:chief_executive;legislate;national;;constitution\n"
		"office:chief_executive;amend_constitution;national;;constitution\n"
		"office:chief_executive;regulate;national;;constitution\n"
		"office:chief_executive;appoint;national;;constitution\n"
		"office:chief_executive;dismiss;national;;constitution\n"
		"office:chief_executive;administer;national;;constitution\n"
		"office:chief_executive;command_forces;national;;constitution\n"
		ALICE_COMMAND_UNDER("chief_executive"),
		"" },
	{ "authoritarian",
		ALICE_SUBORDINATE_BODIES
		"lower_chamber;legislative_chamber;legislature;national;0;legislation;0;0;1\n",
		"chief_executive;head_of_state;head_of_state;;;irremovable;;0;acting;interior_minister;1;1\n"
		"bank_governor;central_bank;central_bank_governor;chief_executive;;appointer;;0;none;;1;1\n"
		"chief_justice;judiciary;chief_justice;chief_executive;;appointer;;0;none;;1;1\n"
		"lower_seat;lower_chamber;legislator;chief_executive;;appointer;;0;none;;0;7\n",
		"institution:lower_chamber;legislate;national;;constitution\n"
		"institution:lower_chamber;amend_constitution;national;;constitution\n"
		"office:chief_executive;assent;national;;constitution\n"
		"office:chief_executive;regulate;national;;constitution\n"
		"office:chief_executive;appoint;national;;constitution\n"
		"office:chief_executive;dismiss;national;;constitution\n"
		"office:chief_executive;administer;national;;constitution\n"
		"office:chief_executive;command_forces;national;;constitution\n"
		ALICE_COMMAND_UNDER("chief_executive"),
		"" },
} };

constexpr std::string_view common_elections = R"(
office:mayor;direct;own;1460
)";
constexpr std::string_view federal_elections = R"(
office:governor;direct;own;1460
)";

constexpr std::string_view unitary_offices = R"(
governor;region;regional_governor;chief_executive;;appointer;;0;none;;0;1
mayor;capital;mayor;;;irremovable;;0;none;;0;1
)";
constexpr std::string_view unitary_authorities = R"(
institution:region;administer;territory;institution:cabinet;
institution:region;spend_public_funds;territory;institution:finance;
office:governor;administer;territory;institution:region;
institution:capital;administer;territory;institution:cabinet;
institution:capital;spend_public_funds;territory;institution:finance;
office:mayor;administer;territory;institution:capital;
)";
constexpr std::string_view federal_offices = R"(
governor;region;regional_governor;;;irremovable;;0;none;;0;1
mayor;capital;mayor;;;irremovable;;0;none;;0;1
)";
constexpr std::string_view federal_authorities = R"(
institution:region;administer;territory;;constitution
institution:region;spend_public_funds;territory;;constitution
institution:region;levy_tax;territory;;constitution
office:governor;administer;territory;institution:region;
office:governor;regulate;territory;;constitution
institution:capital;administer;territory;;constitution
institution:capital;spend_public_funds;territory;;constitution
office:mayor;administer;territory;institution:capital;
)";

// ---------------------------------------------------------------- founding helpers

bool is_scope(std::string_view value) { return value == "national" || value == "regional" || value == "capital"; }

struct holder_ref {
	bool is_office = false;
	std::string key;
};

bool parse_holder(std::string const& text, holder_ref& result) {
	if(text.starts_with("institution:")) { result = { false, text.substr(12) }; return !result.key.empty(); }
	if(text.starts_with("office:")) { result = { true, text.substr(7) }; return !result.key.empty(); }
	return false;
}

dcon::nation_id owner_of(sys::state const& state, dcon::territorial_unit_id unit) {
	return state.world.territorial_unit_get_nation_from_territorial_unit_owner(unit);
}

std::vector<dcon::territorial_unit_id> region_units(sys::state const& state, dcon::nation_id nation) {
	std::vector<dcon::territorial_unit_id> result;
	state.world.for_each_territorial_unit([&](dcon::territorial_unit_id unit) {
		if(owner_of(state, unit) == nation && state.world.territorial_unit_get_state_definition_from_territorial_unit_region(unit))
			result.push_back(unit);
	});
	std::sort(result.begin(), result.end(), [](auto a, auto b) { return a.index() < b.index(); });
	return result;
}

dcon::territorial_unit_id create_unit(sys::state& state, dcon::nation_id nation, uint8_t level) {
	auto unit = state.world.create_territorial_unit();
	state.world.territorial_unit_set_level(unit, level);
	state.world.force_create_territorial_unit_owner(unit, nation);
	return unit;
}

void set_unit_parent(sys::state& state, dcon::territorial_unit_id child, dcon::territorial_unit_id parent) {
	if(!child || parent_of(state, child) == parent) return;
	if(auto relation = state.world.territorial_unit_get_territorial_unit_parent_as_child(child)) state.world.delete_territorial_unit_parent(relation);
	if(parent) state.world.force_create_territorial_unit_parent(child, parent);
}

void assign(sys::state& state, dcon::province_id province, dcon::territorial_unit_id unit) {
	auto membership = state.world.province_get_territorial_unit_membership(province);
	if(membership) {
		if(state.world.territorial_unit_membership_get_territorial_unit(membership) != unit)
			state.world.territorial_unit_membership_set_territorial_unit(membership, unit);
	} else {
		state.world.force_create_territorial_unit_membership(province, unit);
	}
}

// A territorial institution's government: its seat and jurisdiction.
void bind_territory(sys::state& state, dcon::institution_id institution, dcon::territorial_unit_id unit) {
	auto local_government = state.world.institution_get_local_government_from_local_government_institution(institution);
	if(!local_government) {
		local_government = state.world.create_local_government();
		(void)bind_local_government(state, local_government, institution);
	}
	if(auto relation = state.world.local_government_get_local_government_jurisdiction(local_government)) {
		if(state.world.local_government_jurisdiction_get_territorial_unit(relation) != unit)
			state.world.local_government_jurisdiction_set_territorial_unit(relation, unit);
	} else {
		state.world.force_create_local_government_jurisdiction(local_government, unit);
	}
}

bool land_province(sys::state const& state, dcon::province_id province) {
	auto first_sea = state.province_definitions.first_sea_province;
	return !first_sea || province.index() < first_sea.index();
}

dcon::territorial_unit_id region_containing(sys::state const& state, dcon::territorial_unit_id unit) {
	for(auto current = unit; current; current = parent_of(state, current))
		if(state.world.territorial_unit_get_state_definition_from_territorial_unit_region(current)) return current;
	return {};
}

// Adults the founding can seat: alive, at least 25 years old on the date, and
// holding no office yet. Capital residents first.
struct founder_pool {
	sys::state& state;
	dcon::nation_id nation;
	sys::date date;
	std::vector<dcon::pop_id> pops;
	size_t pop_index = 0;
	uint64_t ordinal = 0;
	std::unordered_set<uint64_t> taken;

	founder_pool(sys::state& s, dcon::nation_id n, sys::date d) : state(s), nation(n), date(d) {
		auto capital = state.world.nation_get_capital(nation);
		auto add_province = [&](dcon::province_id province) {
			for(auto location : state.world.province_get_pop_location(province)) pops.push_back(location.get_pop().id);
		};
		if(capital) add_province(capital);
		for(auto ownership : state.world.nation_get_province_ownership(nation))
			if(ownership.get_province().id != capital) add_province(ownership.get_province().id);
	}

	dcon::person_id next() {
		while(pop_index < pops.size()) {
			auto cell = persons::source_population_cell_for_population(state, pops[pop_index]);
			auto count = cell ? persons::exact_population::literal_count_for_cell(state, cell) : 0;
			while(cell && ordinal < count) {
				persons::person_key key{ cell, ordinal++ };
				if(!persons::exact_population::alive(state, key)) continue;
				auto age = (int32_t(date.to_raw_value()) - 1) - persons::exact_population::birth_day_index(state, key);
				if(age < 25 * 365 || age > 70 * 365) continue;
				auto existing = persons::exact_population::profile_for_person(state, key);
				if(existing && !persons::active_offices_of(state, existing).empty()) continue;
				if(!taken.insert((uint64_t(cell) << 40) ^ key.ordinal).second) continue;
				if(auto person = persons::materialize_profile(state, key)) return person;
			}
			++pop_index;
			ordinal = 0;
		}
		return {};
	}
};

} // namespace

dcon::institution_id founded::institution(std::string const& key) const {
	auto it = institutions.find(key);
	return it == institutions.end() || it->second.empty() ? dcon::institution_id{} : it->second.front();
}

dcon::office_id founded::office(std::string const& key) const {
	auto it = offices.find(key);
	return it == offices.end() || it->second.empty() ? dcon::office_id{} : it->second.front();
}

bool parse_institutions(std::string_view text, document& result, std::string& error) {
	std::vector<parsed_line> rows;
	if(!read_rows(text, "institutions", institutions_header, rows, error)) return false;
	for(auto& row : rows) {
		institution_row value{};
		value.key = row.cells[0];
		value.kind = row.cells[1];
		value.parent = row.cells[2];
		value.scope = row.cells[3];
		value.service = row.cells[5];
		value.line = row.line;
		int32_t occupation = 0;
		if((row.cells[4] != "0" && row.cells[4] != "1") || !parse_number(row.cells[6], value.staffing_per_capita)
			|| !parse_number(row.cells[7], occupation) || occupation < 0 || occupation > 2
			|| !parse_number(row.cells[8], value.wage_multiplier)) {
			error = row_error("institutions", row.line, "independent must be 0 or 1, and staffing numbers must be valid");
			return false;
		}
		value.independent = row.cells[4] == "1";
		value.staff_occupation = uint8_t(occupation);
		result.institutions.push_back(std::move(value));
	}
	return true;
}

bool parse_offices(std::string_view text, document& result, std::string& error) {
	std::vector<parsed_line> rows;
	if(!read_rows(text, "offices", offices_header, rows, error)) return false;
	for(auto& row : rows) {
		office_row value{};
		value.key = row.cells[0];
		value.institution = row.cells[1];
		value.kind = row.cells[2];
		value.appointer = row.cells[3];
		value.confirmer = row.cells[4];
		value.removal = row.cells[5];
		value.remover = row.cells[6];
		value.succession = row.cells[8];
		value.successor = row.cells[9];
		value.line = row.line;
		uint32_t term = 0;
		if(!parse_number(row.cells[7], term) || term > 65535 || (row.cells[10] != "0" && row.cells[10] != "1")
			|| !parse_number(row.cells[11], value.seats) || value.seats == 0) {
			error = row_error("offices", row.line, "term_days, exclusive and seats must be valid");
			return false;
		}
		value.term_days = uint16_t(term);
		value.exclusive = row.cells[10] == "1";
		result.offices.push_back(std::move(value));
	}
	return true;
}

bool parse_elections(std::string_view text, document& result, std::string& error) {
	std::vector<parsed_line> rows;
	if(!read_rows(text, "elections", elections_header, rows, error)) return false;
	for(auto& row : rows) {
		uint32_t term = 0;
		if(!parse_number(row.cells[3], term) || term > 65535) {
			error = row_error("elections", row.line, "term_days must be a number of days");
			return false;
		}
		result.elections.push_back({ row.cells[0], row.cells[1], row.cells[2], uint16_t(term), row.line });
	}
	return true;
}

bool parse_authorities(std::string_view text, document& result, std::string& error) {
	std::vector<parsed_line> rows;
	if(!read_rows(text, "authorities", authorities_header, rows, error)) return false;
	for(auto& row : rows)
		result.authorities.push_back({ row.cells[0], row.cells[1], row.cells[2], row.cells[3], row.cells[4], row.line });
	return true;
}

bool validate(document const& doc, std::string& error) {
	std::map<std::string, institution_row const*> institutions;
	for(auto const& row : doc.institutions) {
		if(row.key.empty() || !institutions.emplace(row.key, &row).second) { error = row_error("institutions", row.line, "key must be unique and nonempty"); return false; }
		if(index_of(institution_kind_names, row.kind) < 0) { error = row_error("institutions", row.line, "unknown kind '" + row.kind + "'"); return false; }
		if(!is_scope(row.scope)) { error = row_error("institutions", row.line, "scope must be national, regional or capital"); return false; }
		if(index_of(service_kind_names, row.service) < 0) { error = row_error("institutions", row.line, "unknown service '" + row.service + "'"); return false; }
		if(row.staffing_per_capita < 0.0f || row.wage_multiplier <= 0.0f) { error = row_error("institutions", row.line, "staffing must be nonnegative and wages positive"); return false; }
	}
	size_t roots = 0;
	for(auto const& row : doc.institutions) {
		if(row.parent.empty()) {
			++roots;
			if(row.kind != "central_government" || row.scope != "national") { error = row_error("institutions", row.line, "only the state may lack a parent"); return false; }
			continue;
		}
		auto parent = institutions.find(row.parent);
		if(parent == institutions.end()) { error = row_error("institutions", row.line, "parent '" + row.parent + "' is not an institution"); return false; }
		if(row.scope == "national" && parent->second->scope != "national") { error = row_error("institutions", row.line, "a national institution cannot sit under a territorial one"); return false; }
		std::set<std::string> seen{ row.key };
		for(auto current = parent->second; current && !current->parent.empty(); current = institutions.at(current->parent)) {
			if(!seen.insert(current->key).second) { error = row_error("institutions", row.line, "institution tree has a cycle"); return false; }
			if(!institutions.contains(current->parent)) break;
		}
	}
	if(roots != 1) { error = "institutions: exactly one root (the state) is required"; return false; }
	if(std::count_if(doc.institutions.begin(), doc.institutions.end(), [](auto const& r) { return r.scope == "capital"; }) > 1) {
		error = "institutions: at most one capital municipality"; return false;
	}

	std::map<std::string, office_row const*> offices;
	for(auto const& row : doc.offices) {
		if(row.key.empty() || !offices.emplace(row.key, &row).second) { error = row_error("offices", row.line, "key must be unique and nonempty"); return false; }
		if(!institutions.contains(row.institution)) { error = row_error("offices", row.line, "institution '" + row.institution + "' is not defined"); return false; }
		if(index_of(office_kind_names, row.kind) < 0) { error = row_error("offices", row.line, "unknown kind '" + row.kind + "'"); return false; }
		if(row.removal != "appointer" && row.removal != "remover" && row.removal != "irremovable" && row.removal != "no_confidence") { error = row_error("offices", row.line, "removal must be appointer, remover, irremovable or no_confidence"); return false; }
		if(row.removal == "no_confidence" && row.confirmer.empty()) { error = row_error("offices", row.line, "removal by no confidence needs a confirming chamber"); return false; }
		if(row.succession != "none" && row.succession != "acting" && row.succession != "full") { error = row_error("offices", row.line, "succession must be none, acting or full"); return false; }
	}
	auto office_scope = [&](office_row const& row) { return institutions.at(row.institution)->scope; };
	for(auto const& row : doc.offices) {
		for(auto const& [name, reference] : std::array<std::pair<char const*, std::string const*>, 3>{ { { "appointer", &row.appointer }, { "remover", &row.remover }, { "successor", &row.successor } } }) {
			if(reference->empty()) continue;
			auto other = offices.find(*reference);
			if(other == offices.end() || *reference == row.key) { error = row_error("offices", row.line, std::string(name) + " '" + *reference + "' is not another office"); return false; }
			if(other->second->seats != 1) { error = row_error("offices", row.line, std::string(name) + " cannot be a multi-seat office"); return false; }
			auto scope = office_scope(*other->second);
			if(scope != "national" && scope != office_scope(row)) { error = row_error("offices", row.line, std::string(name) + " must be national or in the same territory"); return false; }
		}
		if(row.removal == "remover" && row.remover.empty()) { error = row_error("offices", row.line, "removal by remover needs a remover"); return false; }
		if(row.succession != "none" && row.successor.empty()) { error = row_error("offices", row.line, "succession needs a successor"); return false; }
		if(!row.confirmer.empty()) {
			auto chamber = institutions.find(row.confirmer);
			if(chamber == institutions.end() || chamber->second->kind != "legislative_chamber") { error = row_error("offices", row.line, "confirmer must be a legislative chamber"); return false; }
		}
	}
	// Two exclusive offices one succession rule would combine are contradictory
	// only for full succession, which moves the holder; acting is allowed.

	std::map<std::pair<std::string, std::string>, bool> granted;
	for(auto const& row : doc.authorities) {
		holder_ref holder;
		if(!parse_holder(row.holder, holder)) { error = row_error("authorities", row.line, "holder must be institution:<key> or office:<key>"); return false; }
		std::string scope_of_holder;
		if(holder.is_office) {
			auto office = offices.find(holder.key);
			if(office == offices.end()) { error = row_error("authorities", row.line, "office '" + holder.key + "' is not defined"); return false; }
			scope_of_holder = office_scope(*office->second);
		} else {
			auto institution = institutions.find(holder.key);
			if(institution == institutions.end()) { error = row_error("authorities", row.line, "institution '" + holder.key + "' is not defined"); return false; }
			scope_of_holder = institution->second->scope;
		}
		if(index_of(authority_kind_names, row.kind) < 0) { error = row_error("authorities", row.line, "unknown authority '" + row.kind + "'"); return false; }
		if(row.scope != "national" && row.scope != "territory") { error = row_error("authorities", row.line, "scope must be national or territory"); return false; }
		if((row.scope == "territory") != (scope_of_holder != "national")) { error = row_error("authorities", row.line, "territorial powers belong to territorial holders, national powers to national ones"); return false; }
		if(row.source != "constitution" && !row.source.empty()) { error = row_error("authorities", row.line, "source must be constitution or empty"); return false; }
		if(row.source.empty() == row.delegated_from.empty()) { error = row_error("authorities", row.line, "a power is either constitutional or delegated"); return false; }
		if(!row.delegated_from.empty()) {
			holder_ref delegator;
			if(!parse_holder(row.delegated_from, delegator) || !granted.contains({ row.delegated_from, row.kind })) {
				error = row_error("authorities", row.line, "delegated_from '" + row.delegated_from + "' holds no earlier grant of " + row.kind); return false;
			}
		}
		granted[{ row.holder, row.kind }] = true;
	}
	std::set<std::string> elected;
	for(auto const& row : doc.elections) {
		holder_ref body;
		if(!parse_holder(row.body, body)) { error = row_error("elections", row.line, "body must be institution:<chamber> or office:<key>"); return false; }
		auto system = index_of(electoral_system_names, row.system);
		if(system <= 0 || index_of(district_names, row.districts) < 0) { error = row_error("elections", row.line, "unknown electoral system or district rule"); return false; }
		if(!elected.insert(row.body).second) { error = row_error("elections", row.line, row.body + " has two electoral rules"); return false; }
		bool seat_system = row.system == "proportional" || row.system == "plurality";
		if(body.is_office) {
			auto office = offices.find(body.key);
			if(office == offices.end()) { error = row_error("elections", row.line, "office '" + body.key + "' is not defined"); return false; }
			if(seat_system) { error = row_error("elections", row.line, "an office is not elected by a seat system"); return false; }
			if(!office->second->appointer.empty()) { error = row_error("elections", row.line, "an office with an appointer is not elected"); return false; }
			if(row.districts == "own" && office_scope(*office->second) == "national") { error = row_error("elections", row.line, "only a territorial office votes in its own territory"); return false; }
		} else {
			auto chamber = institutions.find(body.key);
			if(chamber == institutions.end() || chamber->second->kind != "legislative_chamber") { error = row_error("elections", row.line, "an institution must be a legislative chamber to be elected"); return false; }
			if(!seat_system) { error = row_error("elections", row.line, "a chamber is elected by a seat system"); return false; }
		}
	}
	return true;
}

bool model(std::string_view executive, std::string_view territorial, document& result, std::string& error) {
	auto found_model = std::find_if(executive_models.begin(), executive_models.end(), [&](auto const& m) { return m.name == executive; });
	if(found_model == executive_models.end()) { error = "unknown executive model '" + std::string(executive) + "'"; return false; }
	if(territorial != "unitary" && territorial != "federal") { error = "unknown territorial model '" + std::string(territorial) + "'"; return false; }
	auto federal = territorial == "federal";
	auto join = [](std::string_view header, std::initializer_list<std::string_view> parts) {
		std::string text(header);
		text += '\n';
		for(auto part : parts) { text += part; text += '\n'; }
		return text;
	};
	result = {};
	return parse_institutions(join(institutions_header, { common_institutions, found_model->institutions }), result, error)
		&& parse_offices(join(offices_header, { common_offices, found_model->offices, federal ? federal_offices : unitary_offices }), result, error)
		&& parse_authorities(join(authorities_header, { common_authorities, found_model->authorities, ALICE_SHARED_BODY_AUTHORITIES,
			federal ? federal_authorities : unitary_authorities }), result, error)
		// Autocracies hold no elections at all.
		&& parse_elections(join(elections_header, { found_model->elections,
			found_model->elections.empty() ? std::string_view{} : common_elections,
			federal && !found_model->elections.empty() ? federal_elections : std::string_view{} }), result, error)
		&& validate(result, error);
}

void synchronize_territories(sys::state& state, dcon::nation_id nation) {
	if(!nation) return;
	std::map<uint32_t, dcon::territorial_unit_id> regions;
	for(auto unit : region_units(state, nation))
		regions.emplace(state.world.territorial_unit_get_state_definition_from_territorial_unit_region(unit).index(), unit);
	auto capital_institution = find_institution(state, nation, institution_kind::municipality);
	auto capital_unit = capital_institution ? territory_of(state, capital_institution) : dcon::territorial_unit_id{};
	if(capital_institution && !capital_unit) {
		capital_unit = create_unit(state, nation, 2);
		bind_territory(state, capital_institution, capital_unit);
	}
	auto capital = state.world.nation_get_capital(nation);
	for(auto ownership : state.world.nation_get_province_ownership(nation)) {
		auto province = ownership.get_province().id;
		if(!land_province(state, province)) continue;
		dcon::state_definition_id definition = state.world.province_get_state_from_abstract_state_membership(province);
		if(!definition) continue;
		auto& region = regions[definition.index()];
		if(!region) {
			region = create_unit(state, nation, 1);
			state.world.force_create_territorial_unit_region(region, definition);
		}
		if(province == capital && capital_unit) {
			set_unit_parent(state, capital_unit, region);
			assign(state, province, capital_unit);
		} else {
			assign(state, province, region);
		}
	}
}

bool found(sys::state& state, dcon::nation_id nation, document const& doc, sys::date date, founded& result, std::string& error) {
	result = {};
	if(!nation || !state.world.nation_is_valid(nation)) { error = "founding needs a valid nation"; return false; }
	if(law::constitution_of(state, nation)) { error = "the nation already has a constitution"; return false; }
	if(!validate(doc, error)) return false;
	std::map<std::string, institution_row const*> rows;
	for(auto const& row : doc.institutions) rows.emplace(row.key, &row);

	// National and capital institutions first; the capital needs its territory.
	for(auto const& row : doc.institutions) {
		if(row.scope == "regional") continue;
		auto kind = institution_kind(index_of(institution_kind_names, row.kind));
		dcon::institution_id institution{};
		if(kind == institution_kind::central_government) institution = central_government_for(state, nation);
		else institution = create_institution(state, nation, kind);
		if(!institution) { error = row_error("institutions", row.line, "could not create institution"); return false; }
		result.institutions[row.key].push_back(institution);
	}
	synchronize_territories(state, nation);
	auto regions = region_units(state, nation);
	for(auto const& row : doc.institutions) {
		if(row.scope != "regional") continue;
		auto kind = institution_kind(index_of(institution_kind_names, row.kind));
		for(auto unit : regions) {
			auto institution = create_institution(state, nation, kind);
			bind_territory(state, institution, unit);
			result.institutions[row.key].push_back(institution);
		}
	}
	// Properties and hierarchy.
	for(auto const& row : doc.institutions) {
		auto& instances = result.institutions[row.key];
		for(size_t i = 0; i < instances.size(); ++i) {
			auto institution = instances[i];
			state.world.institution_set_independent(institution, uint8_t(row.independent ? 1 : 0));
			state.world.institution_set_service(institution, uint8_t(index_of(service_kind_names, row.service)));
			state.world.institution_set_staffing_per_capita(institution, row.staffing_per_capita);
			state.world.institution_set_staff_occupation(institution, row.staff_occupation);
			state.world.institution_set_staff_wage_multiplier(institution, row.wage_multiplier);
			state.world.institution_set_territorial_template(institution, uint8_t(row.scope == "regional" ? 1 : row.scope == "capital" ? 2 : 0));
			if(row.parent.empty()) continue;
			auto const& parents = result.institutions[row.parent];
			dcon::institution_id parent{};
			if(rows.at(row.parent)->scope == "regional") {
				if(row.scope == "regional") parent = parents[i];
				else if(auto region = region_containing(state, territory_of(state, institution))) {
					for(auto candidate : parents) if(territory_of(state, candidate) == region) parent = candidate;
				}
			} else {
				parent = parents.front();
			}
			if(!parent || !set_parent(state, institution, parent)) { error = row_error("institutions", row.line, "could not place the institution under its parent"); return false; }
		}
	}

	auto issuer = result.institution("legislature");
	if(!issuer) issuer = find_institution(state, nation, institution_kind::head_of_state);
	if(!issuer) issuer = central_government_for(state, nation);
	result.constitution = law::found_constitution(state, nation, issuer, date);
	if(!result.constitution) { error = "could not found the constitution"; return false; }

	// Offices: one per seat, one per regional instance.
	std::map<std::string, office_row const*> office_rows;
	for(auto const& row : doc.offices) {
		office_rows.emplace(row.key, &row);
		auto kind = office_kind(index_of(office_kind_names, row.kind));
		for(auto institution : result.institutions[row.institution])
			for(uint32_t seat = 0; seat < row.seats; ++seat)
				result.offices[row.key].push_back(create_office(state, institution, kind));
	}
	// Regional offices referring to regional offices refer to their own region's.
	auto resolve_office = [&](std::string const& key, dcon::office_id from) -> dcon::office_id {
		if(key.empty()) return {};
		auto const& candidates = result.offices[key];
		if(candidates.size() == 1) return candidates.front();
		auto territory = territory_of(state, institution_for_office(state, from));
		for(auto candidate : candidates)
			if(territory_of(state, institution_for_office(state, candidate)) == territory) return candidate;
		return {};
	};
	for(auto const& row : doc.offices) {
		for(auto office : result.offices[row.key]) {
			offices::office_rules rules{};
			rules.appointer = resolve_office(row.appointer, office);
			rules.remover = resolve_office(row.remover, office);
			rules.successor = resolve_office(row.successor, office);
			rules.confirmer = row.confirmer.empty() ? dcon::institution_id{} : result.institution(row.confirmer);
			rules.removal = row.removal == "remover" ? offices::removal_rule::by_remover
				: row.removal == "irremovable" ? offices::removal_rule::irremovable
				: row.removal == "no_confidence" ? offices::removal_rule::by_no_confidence : offices::removal_rule::by_appointer;
			rules.succession = row.succession == "full" ? offices::succession_mode::full
				: row.succession == "acting" ? offices::succession_mode::acting : offices::succession_mode::none;
			rules.term_days = row.term_days;
			rules.exclusive = row.exclusive;
			if(!offices::set_rules(state, office, rules)) { error = row_error("offices", row.line, "office rules are inconsistent"); return false; }
		}
	}

	// Powers, in order: delegations follow the grants they draw on.
	for(auto const& row : doc.authorities) {
		holder_ref holder;
		(void)parse_holder(row.holder, holder);
		auto kind = authority_kind(index_of(authority_kind_names, row.kind));
		grant_terms terms{};
		terms.valid_from = date;
		if(row.source == "constitution") terms.source = result.constitution;
		std::vector<std::pair<dcon::institution_id, dcon::office_id>> holders;
		if(holder.is_office) for(auto office : result.offices[holder.key]) holders.push_back({ {}, office });
		else for(auto institution : result.institutions[holder.key]) holders.push_back({ institution, {} });
		for(auto const& [institution, office] : holders) {
			auto acting_institution = institution ? institution : institution_for_office(state, office);
			auto scope = row.scope == "national" ? national(nation) : local(territory_of(state, acting_institution));
			dcon::authority_grant_id grant{};
			if(row.delegated_from.empty()) {
				grant = office ? grant_authority_to_office(state, office, kind, scope, terms)
					: grant_authority_to_institution(state, institution, kind, scope, terms);
			} else {
				holder_ref delegator;
				(void)parse_holder(row.delegated_from, delegator);
				dcon::authority_grant_id source{};
				auto find_in = [&](auto delegating) {
					auto candidate_scope = row.scope == "national" ? scope : national(nation);
					if(auto own = authority_grant_for(state, delegating, kind, scope, date)) return own;
					return authority_grant_for(state, delegating, kind, candidate_scope, date);
				};
				if(delegator.is_office) {
					for(auto candidate : result.offices[delegator.key])
						if(!source && (result.offices[delegator.key].size() == 1 || territory_of(state, institution_for_office(state, candidate)) == territory_of(state, acting_institution)))
							source = find_in(candidate);
				} else {
					for(auto candidate : result.institutions[delegator.key])
						if(!source && (result.institutions[delegator.key].size() == 1 || territory_of(state, candidate) == territory_of(state, acting_institution)))
							source = find_in(candidate);
				}
				if(!source) { error = row_error("authorities", row.line, row.delegated_from + " holds no valid " + row.kind + " grant to delegate"); return false; }
				grant = office ? delegate_authority(state, source, office, scope, date) : delegate_authority(state, source, institution, scope, date);
			}
			if(!grant) { error = row_error("authorities", row.line, "could not grant " + row.kind + " to " + row.holder); return false; }
		}
	}
	// Electoral rules: the first elections are held at founding.
	for(auto const& row : doc.elections) {
		holder_ref body;
		(void)parse_holder(row.body, body);
		auto system = elections::electoral_system(index_of(electoral_system_names, row.system));
		auto districts = elections::district_rule(index_of(district_names, row.districts));
		if(body.is_office) for(auto office : result.offices[body.key]) elections::set_rule(state, office, system, districts, row.term_days, date);
		else for(auto chamber : result.institutions[body.key]) elections::set_rule(state, chamber, system, districts, row.term_days, date);
	}
	return true;
}

bool appoint_founders(sys::state& state, dcon::nation_id nation, founded const& result, sys::date date, std::string& error) {
	founder_pool pool(state, nation, date);
	std::vector<dcon::office_id> all;
	for(auto const& [key, list] : result.offices) all.insert(all.end(), list.begin(), list.end());
	std::sort(all.begin(), all.end(), [](auto a, auto b) { return a.index() < b.index(); });
	for(auto office : all) {
		if(!offices::vacant(state, office) || offices::rules_of(state, office).appointer
			|| state.world.office_get_electoral_system(office) != 0) continue;
		// Seats of an elected chamber are filled by its elections.
		auto chamber = institution_for_office(state, office);
		if(state.world.office_get_kind(office) == uint8_t(office_kind::legislator) && state.world.institution_get_electoral_system(chamber) != 0) continue;
		auto person = pool.next();
		if(!person) { error = "not enough living adults to seat the founding offices"; return false; }
		if(!offices::install(state, person, office, date)) { error = "a founder could not take office"; return false; }
	}
	return true;
}

void bootstrap(sys::state& state) {
	state.world.for_each_nation([&](dcon::nation_id nation) {
		if(law::constitution_of(state, nation) || state.world.nation_get_province_ownership(nation).begin() == state.world.nation_get_province_ownership(nation).end()) return;
		document doc;
		founded result;
		std::string error;
		if(!model(default_executive_model, "unitary", doc, error)
			|| !found(state, nation, doc, state.current_date, result, error)
			|| !appoint_founders(state, nation, result, state.current_date, error)) {
			assert(false && "constitutional founding failed");
			std::abort();
		}
	});
}

void synchronize(sys::state& state) {
	state.world.for_each_nation([&](dcon::nation_id nation) {
		if(!law::constitution_of(state, nation)) return;
		synchronize_territories(state, nation);
		// The prototype is any regional government of the nation.
		dcon::institution_id prototype{};
		std::unordered_set<uint32_t> governed;
		for(auto institution : institutions_of(state, nation)) {
			if(state.world.institution_get_territorial_template(institution) != 1) continue;
			if(auto unit = territory_of(state, institution)) {
				governed.insert(unit.index());
				if(!prototype) prototype = institution;
			}
		}
		if(!prototype) return;
		auto date = state.current_date;
		for(auto unit : region_units(state, nation)) {
			if(governed.contains(unit.index())) continue;
			auto institution = create_institution(state, nation, kind_of(state, prototype));
			bind_territory(state, institution, unit);
			state.world.institution_set_independent(institution, state.world.institution_get_independent(prototype));
			state.world.institution_set_service(institution, state.world.institution_get_service(prototype));
			state.world.institution_set_staffing_per_capita(institution, state.world.institution_get_staffing_per_capita(prototype));
			state.world.institution_set_staff_occupation(institution, state.world.institution_get_staff_occupation(prototype));
			state.world.institution_set_staff_wage_multiplier(institution, state.world.institution_get_staff_wage_multiplier(prototype));
			state.world.institution_set_territorial_template(institution, 1);
			if(auto parent = parent_of(state, prototype)) (void)set_parent(state, institution, parent);
			std::map<uint32_t, dcon::authority_grant_id> cloned;
			auto clone_grant = [&](dcon::authority_grant_id grant, auto holder) {
				if(!grant_valid(state, grant, date)) return;
				auto kind = authority_kind(state.world.authority_grant_get_kind(grant));
				auto scope = jurisdiction_of(state, grant).territory ? local(unit) : jurisdiction_of(state, grant);
				dcon::authority_grant_id copy{};
				if(auto delegator = delegated_from(state, grant)) {
					auto mapped = cloned.find(delegator.index());
					copy = delegate_authority(state, mapped != cloned.end() ? mapped->second : delegator, holder, scope, date);
				} else {
					grant_terms terms{};
					terms.valid_from = date;
					terms.valid_until = state.world.authority_grant_get_valid_until(grant);
					terms.source = state.world.authority_grant_get_legal_instrument_from_authority_grant_source(grant);
					if constexpr(std::is_same_v<decltype(holder), dcon::office_id>) copy = grant_authority_to_office(state, holder, kind, scope, terms);
					else copy = grant_authority_to_institution(state, holder, kind, scope, terms);
				}
				if(copy) cloned[grant.index()] = copy;
			};
			state.world.institution_for_each_authority_grant_institution_holder_as_institution(prototype, [&](auto relation) {
				clone_grant(state.world.authority_grant_institution_holder_get_authority_grant(relation), institution);
			});
			for(auto office : offices_of(state, prototype)) {
				auto copy = create_office(state, institution, office_kind(state.world.office_get_kind(office)));
				auto rules = offices::rules_of(state, office);
				// Rules naming the prototype's own offices are not carried over.
				for(auto* related : { &rules.appointer, &rules.remover, &rules.successor })
					if(*related && institution_for_office(state, *related) == prototype) *related = {};
				if(rules.removal == offices::removal_rule::by_remover && !rules.remover) rules.removal = offices::removal_rule::by_appointer;
				if(!rules.successor) rules.succession = offices::succession_mode::none;
				(void)offices::set_rules(state, copy, rules);
				state.world.office_for_each_authority_grant_office_holder_as_office(office, [&](auto relation) {
					clone_grant(state.world.authority_grant_office_holder_get_authority_grant(relation), copy);
				});
			}
		}
	});
}

} // namespace governance::constitution
