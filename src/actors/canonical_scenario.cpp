#include "canonical_scenario.hpp"

#include "actors/organizations/organizations.hpp"
#include "actors/ownership.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/physical/deposits.hpp"
#include "economy/relations/relations.hpp"
#include "governance/governance.hpp"
#include "nations/nations.hpp"
#include "parsing/parsers.hpp"
#include "parsers_declarations.hpp"
#include "persons/persons.hpp"
#include "system_state.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace actors::canonical_scenario {
namespace {

struct row {
	std::vector<std::string> cells;
	uint32_t line = 0;
};

struct table {
	std::vector<row> rows;
};

std::string_view trim(std::string_view value) {
	while(!value.empty() && (value.front() == ' ' || value.front() == '\t' || value.front() == '\r')) value.remove_prefix(1);
	while(!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r')) value.remove_suffix(1);
	return value;
}

std::vector<std::string> split_row(std::string_view line) {
	std::vector<std::string> result;
	while(true) {
		auto delimiter = line.find(';');
		auto part = trim(line.substr(0, delimiter));
		result.emplace_back(part);
		if(delimiter == std::string_view::npos) break;
		line.remove_prefix(delimiter + 1);
	}
	return result;
}

std::optional<table> read_table(simple_fs::directory const& directory, char const* filename,
	std::span<std::string_view const> expected_header, parsers::error_handler& err) {
	auto file = simple_fs::open_file(directory, simple_fs::utf8_to_native(filename));
	if(!file) {
		err.file_name = std::string("common/canonical_runtime/") + filename;
		err.accumulated_errors += err.file_name + " is required; canonical scenario loading has no legacy ownership fallback\n";
		return std::nullopt;
	}
	err.file_name = std::string("common/canonical_runtime/") + filename;
	auto content = simple_fs::view_contents(*file);
	std::string_view input(content.data, content.file_size);
	if(input.size() >= 3 && uint8_t(input[0]) == 0xEF && uint8_t(input[1]) == 0xBB && uint8_t(input[2]) == 0xBF)
		input.remove_prefix(3);

	table result;
	uint32_t line_number = 0;
	bool header_seen = false;
	while(!input.empty()) {
		auto newline = input.find('\n');
		auto line = trim(input.substr(0, newline));
		if(newline == std::string_view::npos) input = {};
		else input.remove_prefix(newline + 1);
		++line_number;
		if(line.empty() || line.front() == '#') continue;
		auto cells = split_row(line);
		if(!header_seen) {
			header_seen = true;
			if(cells.size() != expected_header.size()) {
				err.accumulated_errors += err.file_name + ":1: header has " + std::to_string(cells.size())
					+ " columns; expected " + std::to_string(expected_header.size()) + "\n";
				return result;
			}
			for(size_t i = 0; i < cells.size(); ++i) {
				if(cells[i] != expected_header[i]) {
					err.accumulated_errors += err.file_name + ":1: column " + std::to_string(i + 1)
						+ " must be '" + std::string(expected_header[i]) + "'\n";
					return result;
				}
			}
			continue;
		}
		if(cells.size() != expected_header.size()) {
			err.accumulated_errors += err.file_name + ":" + std::to_string(line_number)
				+ ": row has " + std::to_string(cells.size()) + " columns; expected "
				+ std::to_string(expected_header.size()) + "\n";
			continue;
		}
		result.rows.push_back(row{ std::move(cells), line_number });
	}
	if(!header_seen) err.accumulated_errors += err.file_name + ": missing required header\n";
	return result;
}

bool parse_float(std::string_view text, float& result) {
	text = trim(text);
	if(text.empty()) return false;
	std::string value(text);
	char* end = nullptr;
	errno = 0;
	result = std::strtof(value.c_str(), &end);
	return errno != ERANGE && end == value.c_str() + value.size() && std::isfinite(result);
}

template<typename T>
bool parse_integer(std::string_view text, T& result) {
	text = trim(text);
	if(text.empty()) return false;
	auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
	return error == std::errc{} && end == text.data() + text.size();
}

bool valid_key(std::string_view value) {
	if(value.empty()) return false;
	return std::all_of(value.begin(), value.end(), [](unsigned char c) {
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
			|| c == '_' || c == '-' || c == '.';
	});
}

uint64_t stable_id(std::string_view value) {
	uint64_t hash = 14695981039346656037ULL;
	for(unsigned char c : value) {
		hash ^= uint64_t(c);
		hash *= 1099511628211ULL;
	}
	return hash == 0 ? 1 : hash;
}

bool claim_id(std::unordered_map<uint64_t, std::string>& registry, uint64_t id,
	std::string const& key, std::string& error) {
	auto [it, inserted] = registry.emplace(id, key);
	if(!inserted && it->second != key) {
		error = "stable ID hash collision between '" + it->second + "' and '" + key + "'";
		return false;
	}
	return true;
}

template<typename Setter>
bool assign_id(uint64_t current, Setter&& setter, std::string const& key,
	std::unordered_map<uint64_t, std::string>& registry, std::string& error) {
	error.clear();
	auto id = stable_id(key);
	if(current != 0 && current != id) {
		error = "canonical object already carries a different stable ID for '" + key + "'";
		return false;
	}
	if(!claim_id(registry, id, key, error)) return false;
	setter(id);
	return true;
}

struct firm_record {
	std::string id;
	uint32_t line = 0;
	std::string settlement_key;
	ownership::actor_kind kind = ownership::actor_kind::invalid;
	dcon::commodity_id settlement{};
	float opening_cash = 0.0f;
	float retained_earnings = 0.0f;
	float paid_in_equity = 0.0f;
	dcon::organization_id organization{};
	dcon::economic_actor_id actor{};
	dcon::asset_id equity_asset{};
};

struct owner_record {
	std::string id;
	uint32_t line = 0;
	std::string kind;
	std::string reference;
	std::string settlement_key;
	dcon::commodity_id settlement{};
	float opening_cash = 0.0f;
	dcon::economic_actor_id actor{};
	dcon::nation_id nation{};
	dcon::institution_id institution{};
	dcon::person_id person{};
};

struct asset_record {
	std::string id;
	uint32_t line = 0;
	std::string kind;
	std::string site_key;
	std::string operator_id;
	std::string building_key;
	std::string commodity_key;
	uint32_t province_original_id = 0;
	uint32_t ordinal = 0;
	float appraised_value = 0.0f;
	dcon::province_id province{};
	dcon::site_id site{};
	dcon::factory_id factory{};
	dcon::resource_deposit_id deposit{};
	dcon::asset_id asset{};
};

struct stake_record {
	std::string asset_id;
	std::string owner_type;
	std::string owner_id;
	float ownership = 0.0f;
	float voting = 0.0f;
	float economic = 0.0f;
	uint32_t line = 0;
};

struct loan_record {
	std::string id;
	std::string asset_id;
	std::string creditor_type;
	std::string creditor_id;
	float principal = 0.0f;
	float annual_rate = 0.0f;
	float accrued_interest = 0.0f;
	std::string creation_date_text;
	std::string due_date_text;
	uint32_t line = 0;
};

struct owner_binding {
	dcon::economic_actor_id actor{};
	bool government = false;
	bool person = false;
};

void add_row_error(parsers::error_handler& err, std::string const& file, uint32_t line,
	std::string const& message) {
	err.accumulated_errors += "common/canonical_runtime/" + file;
	if(line != 0) err.accumulated_errors += ":" + std::to_string(line);
	err.accumulated_errors += ": " + message + "\n";
}

dcon::commodity_id find_commodity(parsers::scenario_building_context const& context,
	std::string const& key) {
	auto it = context.map_of_commodity_names.find(key);
	return it == context.map_of_commodity_names.end() ? dcon::commodity_id{} : it->second;
}

dcon::province_id province_from_original_id(sys::state const& state,
	parsers::scenario_building_context const& context, uint32_t original_id) {
	if(original_id == 0 || original_id >= context.original_id_to_prov_id_map.size()) return {};
	auto province = context.original_id_to_prov_id_map[original_id];
	return province && state.world.province_is_valid(province) ? province : dcon::province_id{};
}

dcon::nation_id find_nation_by_tag(sys::state const& state, std::string const& tag) {
	if(tag.size() != 3) return {};
	auto code = nations::tag_to_int(tag[0], tag[1], tag[2]);
	dcon::nation_id result{};
	state.world.for_each_national_identity([&](dcon::national_identity_id identity) {
		if(state.world.national_identity_get_identifying_int(identity) == code)
			result = state.world.national_identity_get_nation_from_identity_holder(identity);
	});
	return result && state.world.nation_is_valid(result) ? result : dcon::nation_id{};
}

std::optional<sys::year_month_day> parse_iso_date(std::string_view value) {
	if(value.size() != 10 || value[4] != '-' || value[7] != '-') return std::nullopt;
	int32_t year = 0;
	uint32_t month = 0;
	uint32_t day = 0;
	if(!parse_integer(value.substr(0, 4), year) || !parse_integer(value.substr(5, 2), month)
		|| !parse_integer(value.substr(8, 2), day)) return std::nullopt;
	if(year < 1 || month < 1 || month > 12) return std::nullopt;
	static constexpr std::array<uint8_t, 12> days_per_month = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	auto days = days_per_month[month - 1];
	if(month == 2 && sys::is_leap_year(year)) ++days;
	if(day < 1 || day > days) return std::nullopt;
	return sys::year_month_day{ year, uint16_t(month), uint16_t(day) };
}

bool resolve_person_reference(sys::state const& state, std::string const& value,
	dcon::person_id& result) {
	auto separator = value.find(':');
	if(separator == std::string::npos || value.find(':', separator + 1) != std::string::npos) return false;
	uint32_t cell = 0;
	uint32_t ordinal = 0;
	if(!parse_integer(std::string_view(value).substr(0, separator), cell)
		|| !parse_integer(std::string_view(value).substr(separator + 1), ordinal) || cell == 0) return false;
	state.world.for_each_person([&](dcon::person_id person) {
		if(state.world.person_get_source_population_cell(person) == cell
			&& state.world.person_get_source_population_ordinal(person) == ordinal)
			result = person;
	});
	return result && state.world.person_is_valid(result)
		&& state.world.person_get_alive(result)
		&& bool(persons::actor_for_person(state, result));
}

bool read_and_parse_tables(simple_fs::directory const& common,
	parsers::error_handler& err, table& firms, table& owners, table& assets,
	table& ownerships, table& loans) {
	static constexpr std::array<std::string_view, 6> firms_header = {
		"firm_id", "kind", "settlement", "opening_cash", "retained_earnings", "paid_in_equity"
	};
	static constexpr std::array<std::string_view, 5> owners_header = {
		"owner_id", "kind", "reference", "settlement", "opening_cash"
	};
	static constexpr std::array<std::string_view, 9> assets_header = {
		"asset_id", "kind", "site_id", "operator_id", "province_id", "building", "ordinal", "commodity_id", "opening_value"
	};
	static constexpr std::array<std::string_view, 6> ownership_header = {
		"asset_id", "owner_type", "owner_id", "ownership", "voting", "economic"
	};
	static constexpr std::array<std::string_view, 9> loans_header = {
		"loan_id", "asset_id", "creditor_type", "creditor_id", "principal", "annual_rate", "creation_date", "due_date", "accrued_interest"
	};
	auto canonical = simple_fs::open_directory(common, NATIVE("canonical_runtime"));
	auto before = err.accumulated_errors.size();
	auto f = read_table(canonical, "firms.csv", firms_header, err);
	auto o = read_table(canonical, "capital_owners.csv", owners_header, err);
	auto a = read_table(canonical, "assets.csv", assets_header, err);
	auto s = read_table(canonical, "ownership.csv", ownership_header, err);
	auto l = read_table(canonical, "loans.csv", loans_header, err);
	if(!f || !o || !a || !s || !l || err.accumulated_errors.size() != before) return false;
	firms = std::move(*f);
	owners = std::move(*o);
	assets = std::move(*a);
	ownerships = std::move(*s);
	loans = std::move(*l);
	return true;
}

bool parse_tables(sys::state& state, parsers::scenario_building_context& context,
	parsers::error_handler& err, table const& firm_table, table const& owner_table,
	table const& asset_table, table const& ownership_table, table const& loan_table,
	std::vector<firm_record>& firms, std::vector<owner_record>& owners,
	std::vector<asset_record>& assets, std::vector<stake_record>& stakes,
	std::vector<loan_record>& loans) {
	auto before = err.accumulated_errors.size();
	std::unordered_set<std::string> firm_ids;
	for(auto const& source : firm_table.rows) {
		firm_record value;
		value.id = source.cells[0];
		value.line = source.line;
		value.settlement_key = source.cells[2];
		if(!valid_key(value.id)) add_row_error(err, "firms.csv", source.line, "firm_id must use ASCII letters, digits, '.', '_' or '-'");
		if(!firm_ids.insert(value.id).second) add_row_error(err, "firms.csv", source.line, "duplicate firm_id '" + value.id + "'");
		if(source.cells[1] == "company") value.kind = ownership::actor_kind::company;
		else if(source.cells[1] == "bank") value.kind = ownership::actor_kind::bank;
		else if(source.cells[1] == "fund") value.kind = ownership::actor_kind::fund;
		else if(source.cells[1] == "cooperative") value.kind = ownership::actor_kind::cooperative;
		else if(source.cells[1] == "state_entity") value.kind = ownership::actor_kind::state_entity;
		else if(source.cells[1] == "other") value.kind = ownership::actor_kind::other;
		else add_row_error(err, "firms.csv", source.line, "unknown firm kind '" + source.cells[1] + "'");
		value.settlement = find_commodity(context, value.settlement_key);
		if(!value.settlement) add_row_error(err, "firms.csv", source.line, "unknown settlement commodity '" + value.settlement_key + "'");
		if(!parse_float(source.cells[3], value.opening_cash) || value.opening_cash < 0.0f)
			add_row_error(err, "firms.csv", source.line, "opening_cash must be finite and nonnegative");
		if(!parse_float(source.cells[4], value.retained_earnings))
			add_row_error(err, "firms.csv", source.line, "retained_earnings must be finite");
		if(!parse_float(source.cells[5], value.paid_in_equity) || value.paid_in_equity < 0.0f)
			add_row_error(err, "firms.csv", source.line, "paid_in_equity must be finite and nonnegative");
		firms.push_back(std::move(value));
	}

	std::unordered_set<std::string> owner_ids;
	std::unordered_set<std::string> owner_references;
	for(auto const& source : owner_table.rows) {
		owner_record value;
		value.id = source.cells[0];
		value.line = source.line;
		value.kind = source.cells[1];
		value.reference = source.cells[2];
		value.settlement_key = source.cells[3];
		if(!valid_key(value.id)) add_row_error(err, "capital_owners.csv", source.line, "owner_id must use ASCII letters, digits, '.', '_' or '-'");
		if(!owner_ids.insert(value.id).second) add_row_error(err, "capital_owners.csv", source.line, "duplicate owner_id '" + value.id + "'");
		if(value.kind != "government" && value.kind != "person")
			add_row_error(err, "capital_owners.csv", source.line, "kind must be 'government' or 'person'");
		if(value.kind == "government") {
			if(value.reference.size() != 3) add_row_error(err, "capital_owners.csv", source.line, "government reference must be a three-letter country tag");
		} else {
			dcon::person_id person{};
			if(!resolve_person_reference(state, value.reference, person))
				add_row_error(err, "capital_owners.csv", source.line,
					"person reference '" + value.reference + "' must identify a live exact person as source_population_cell:ordinal");
			else value.person = person;
		}
		auto unique_reference = value.kind + ":" + value.reference;
		if(!owner_references.insert(unique_reference).second)
			add_row_error(err, "capital_owners.csv", source.line, "the same economic owner reference is declared more than once");
		value.settlement = find_commodity(context, value.settlement_key);
		if(!value.settlement) add_row_error(err, "capital_owners.csv", source.line, "unknown settlement commodity '" + value.settlement_key + "'");
		if(!parse_float(source.cells[4], value.opening_cash) || value.opening_cash < 0.0f)
			add_row_error(err, "capital_owners.csv", source.line, "opening_cash must be finite and nonnegative");
		owners.push_back(std::move(value));
	}

	std::unordered_set<std::string> asset_ids;
	for(auto const& source : asset_table.rows) {
		asset_record value;
		value.id = source.cells[0];
		value.line = source.line;
		value.kind = source.cells[1];
		value.site_key = source.cells[2];
		value.operator_id = source.cells[3];
		value.building_key = source.cells[5];
		value.commodity_key = source.cells[7];
		if(!valid_key(value.id) || !valid_key(value.site_key))
			add_row_error(err, "assets.csv", source.line, "asset_id and site_id must use ASCII letters, digits, '.', '_' or '-'");
		if(value.id.starts_with("equity.")) add_row_error(err, "assets.csv", source.line, "asset_id cannot use the reserved 'equity.' prefix");
		if(!asset_ids.insert(value.id).second) add_row_error(err, "assets.csv", source.line, "duplicate asset_id '" + value.id + "'");
		if(!valid_key(value.operator_id) || !firm_ids.contains(value.operator_id))
			add_row_error(err, "assets.csv", source.line, "operator_id must reference a firm row");
		if(!parse_integer(source.cells[4], value.province_original_id) || value.province_original_id == 0)
			add_row_error(err, "assets.csv", source.line, "province_id must be a nonzero original scenario province ID");
		else {
			value.province = province_from_original_id(state, context, value.province_original_id);
			if(!value.province) add_row_error(err, "assets.csv", source.line, "province_id does not resolve in this scenario");
		}
		if(value.kind == "factory") {
			if(context.map_of_factory_names.find(value.building_key) == context.map_of_factory_names.end())
				add_row_error(err, "assets.csv", source.line, "unknown factory building identifier '" + value.building_key + "'");
			if(!parse_integer(source.cells[6], value.ordinal) || value.ordinal == 0)
				add_row_error(err, "assets.csv", source.line, "factory ordinal must be a one-based positive integer");
			if(!value.commodity_key.empty()) add_row_error(err, "assets.csv", source.line, "commodity_id must be empty for a factory asset");
		} else if(value.kind == "deposit") {
			if(!value.building_key.empty() || !source.cells[6].empty())
				add_row_error(err, "assets.csv", source.line, "building and ordinal must be empty for a deposit asset");
			if(!find_commodity(context, value.commodity_key))
				add_row_error(err, "assets.csv", source.line, "unknown deposit commodity '" + value.commodity_key + "'");
		} else add_row_error(err, "assets.csv", source.line, "kind must be 'factory' or 'deposit'");
		if(!parse_float(source.cells[8], value.appraised_value) || value.appraised_value < 0.0f)
			add_row_error(err, "assets.csv", source.line, "opening_value must be finite and nonnegative");
		assets.push_back(std::move(value));
	}

	for(auto const& source : ownership_table.rows) {
		stake_record value;
		value.asset_id = source.cells[0];
		value.owner_type = source.cells[1];
		value.owner_id = source.cells[2];
		value.line = source.line;
		if(!parse_float(source.cells[3], value.ownership) || !ownership::valid_fraction(value.ownership))
			add_row_error(err, "ownership.csv", source.line, "ownership fraction must be finite and between 0 and 1");
		if(!parse_float(source.cells[4], value.voting) || !ownership::valid_fraction(value.voting))
			add_row_error(err, "ownership.csv", source.line, "voting fraction must be finite and between 0 and 1");
		if(!parse_float(source.cells[5], value.economic) || !ownership::valid_fraction(value.economic))
			add_row_error(err, "ownership.csv", source.line, "economic fraction must be finite and between 0 and 1");
		if(value.owner_type != "firm" && value.owner_type != "capital_owner")
			add_row_error(err, "ownership.csv", source.line, "owner_type must be 'firm' or 'capital_owner'");
		if(!valid_key(value.owner_id)) add_row_error(err, "ownership.csv", source.line, "owner_id must use ASCII letters, digits, '.', '_' or '-'");
		stakes.push_back(std::move(value));
	}

	for(auto const& source : loan_table.rows) {
		loan_record value;
		value.id = source.cells[0];
		value.asset_id = source.cells[1];
		value.creditor_type = source.cells[2];
		value.creditor_id = source.cells[3];
		value.creation_date_text = source.cells[6];
		value.due_date_text = source.cells[7];
		value.line = source.line;
		if(!valid_key(value.id)) add_row_error(err, "loans.csv", source.line, "loan_id must use ASCII letters, digits, '.', '_' or '-'");
		if(value.creditor_type != "firm" && value.creditor_type != "capital_owner")
			add_row_error(err, "loans.csv", source.line, "creditor_type must be 'firm' or 'capital_owner'");
		if(!valid_key(value.creditor_id)) add_row_error(err, "loans.csv", source.line, "creditor_id must use ASCII letters, digits, '.', '_' or '-'");
		if(!parse_float(source.cells[4], value.principal) || value.principal <= 0.0f)
			add_row_error(err, "loans.csv", source.line, "principal must be finite and positive");
		if(!parse_float(source.cells[5], value.annual_rate) || value.annual_rate < 0.0f)
			add_row_error(err, "loans.csv", source.line, "annual_rate must be finite and nonnegative");
		if(!parse_float(source.cells[8], value.accrued_interest) || value.accrued_interest < 0.0f)
			add_row_error(err, "loans.csv", source.line, "accrued_interest must be finite and nonnegative");
		loans.push_back(std::move(value));
	}
	return err.accumulated_errors.size() == before;
}

bool load_firms(sys::state& state, parsers::scenario_building_context& context,
	parsers::error_handler& err, std::vector<firm_record>& firms,
	std::unordered_map<std::string, size_t>& firm_by_id,
	std::unordered_map<uint64_t, std::string>& actor_ids,
	std::unordered_map<uint64_t, std::string>& organization_ids,
	std::unordered_map<uint64_t, std::string>& asset_ids,
	std::unordered_map<uint64_t, std::string>& account_ids,
	std::unordered_map<std::string, dcon::asset_id>& assets_by_id) {
	auto initial_errors = err.accumulated_errors.size();
	std::sort(firms.begin(), firms.end(), [](auto const& left, auto const& right) { return left.id < right.id; });
	for(size_t i = 0; i < firms.size(); ++i) {
		auto& firm = firms[i];
		firm.organization = organizations::create_organization(state, firm.kind);
		if(!firm.organization) {
			add_row_error(err, "firms.csv", firm.line, "could not create explicitly declared firm '" + firm.id + "'");
			continue;
		}
		firm.actor = organizations::actor_for_organization(state, firm.organization);
		firm.equity_asset = organizations::equity_asset_for_organization(state, firm.organization);
		std::string error;
		auto org_key = "organization:firm:" + firm.id;
		auto actor_key = "actor:firm:" + firm.id;
		auto equity_key = "asset:equity:" + firm.id;
		assign_id(state.world.organization_get_canonical_id(firm.organization),
			[&](uint64_t id) { state.world.organization_set_canonical_id(firm.organization, id); }, org_key, organization_ids, error);
		if(!error.empty()) add_row_error(err, "firms.csv", firm.line, error);
		assign_id(state.world.economic_actor_get_canonical_id(firm.actor),
			[&](uint64_t id) { state.world.economic_actor_set_canonical_id(firm.actor, id); }, actor_key, actor_ids, error);
		if(!error.empty()) add_row_error(err, "firms.csv", firm.line, error);
		assign_id(state.world.asset_get_canonical_id(firm.equity_asset),
			[&](uint64_t id) { state.world.asset_set_canonical_id(firm.equity_asset, id); }, equity_key, asset_ids, error);
		if(!error.empty()) add_row_error(err, "firms.csv", firm.line, error);
		state.world.organization_set_retained_earnings(firm.organization, firm.retained_earnings);
		state.world.organization_set_paid_in_equity(firm.organization, firm.paid_in_equity);
		state.world.asset_set_appraised_value(firm.equity_asset,
			std::max(0.0f, firm.paid_in_equity + firm.retained_earnings));

		if(economy::accounts::find_account(state, firm.actor, firm.settlement)) {
			add_row_error(err, "firms.csv", firm.line, "firm '" + firm.id + "' already has an account for settlement '" + firm.settlement_key + "'");
		} else {
			auto account = economy::accounts::open_account(state, firm.actor, firm.settlement);
			if(!account || !economy::accounts::bootstrap_set_balance(state, account, firm.opening_cash)) {
				add_row_error(err, "firms.csv", firm.line, "could not create opening cash account for firm '" + firm.id + "'");
			} else {
				auto account_key = "account:firm:" + firm.id + ":" + firm.settlement_key;
				assign_id(state.world.monetary_account_get_canonical_id(account),
					[&](uint64_t id) { state.world.monetary_account_set_canonical_id(account, id); },
					account_key, account_ids, error);
				if(!error.empty()) add_row_error(err, "firms.csv", firm.line, error);
			}
		}
		firm_by_id.emplace(firm.id, i);
		assets_by_id.emplace("equity:" + firm.id, firm.equity_asset);
	}
	(void)context;
	return err.accumulated_errors.size() == initial_errors;
}

bool load_capital_owners(sys::state& state, parsers::scenario_building_context& context,
	parsers::error_handler& err, std::vector<owner_record>& owners,
	std::unordered_map<std::string, owner_binding>& owners_by_id,
	std::unordered_map<uint64_t, std::string>& actor_ids,
	std::unordered_map<uint64_t, std::string>& institution_ids,
	std::unordered_map<uint64_t, std::string>& account_ids) {
	auto initial_errors = err.accumulated_errors.size();
	std::sort(owners.begin(), owners.end(), [](auto const& left, auto const& right) { return left.id < right.id; });
	for(auto& owner : owners) {
		std::string canonical_actor_key;
		if(owner.kind == "government") {
			owner.nation = find_nation_by_tag(state, owner.reference);
			if(!owner.nation) {
				add_row_error(err, "capital_owners.csv", owner.line, "government owner '" + owner.reference + "' is not an active country in this scenario");
				continue;
			}
			auto institutions = governance::institutions_of(state, owner.nation);
			for(auto institution : institutions) {
				if(state.world.institution_get_kind(institution) == uint8_t(governance::institution_kind::central_government)) {
					if(owner.institution) {
						add_row_error(err, "capital_owners.csv", owner.line, "country '" + owner.reference + "' has multiple central government institutions");
						owner.institution = {};
						break;
					}
					owner.institution = institution;
				}
			}
			if(!owner.institution) {
				add_row_error(err, "capital_owners.csv", owner.line, "country '" + owner.reference + "' has no loaded central government institution");
				continue;
			}
			owner.actor = governance::actor_for_institution(state, owner.institution);
			canonical_actor_key = "actor:government:" + owner.reference;
			std::string error;
			assign_id(state.world.institution_get_canonical_id(owner.institution),
				[&](uint64_t id) { state.world.institution_set_canonical_id(owner.institution, id); },
				"institution:central-government:" + owner.reference, institution_ids, error);
			if(!error.empty()) add_row_error(err, "capital_owners.csv", owner.line, error);
		} else {
			owner.actor = persons::actor_for_person(state, owner.person);
			canonical_actor_key = "actor:person:" + owner.reference;
		}
		if(!owner.actor || !state.world.economic_actor_is_valid(owner.actor)) {
			add_row_error(err, "capital_owners.csv", owner.line, "owner '" + owner.id + "' resolves to no canonical economic actor");
			continue;
		}
		std::string error;
		assign_id(state.world.economic_actor_get_canonical_id(owner.actor),
			[&](uint64_t id) { state.world.economic_actor_set_canonical_id(owner.actor, id); },
			canonical_actor_key, actor_ids, error);
		if(!error.empty()) add_row_error(err, "capital_owners.csv", owner.line, error);
		if(economy::accounts::find_account(state, owner.actor, owner.settlement)) {
			add_row_error(err, "capital_owners.csv", owner.line, "owner '" + owner.id + "' already has an account for settlement '" + owner.settlement_key + "'");
			continue;
		}
		auto account = economy::accounts::open_account(state, owner.actor, owner.settlement);
		if(!account || !economy::accounts::bootstrap_set_balance(state, account, owner.opening_cash)) {
			add_row_error(err, "capital_owners.csv", owner.line, "could not create opening cash account for owner '" + owner.id + "'");
			continue;
		}
		auto account_key = "account:owner:" + owner.id + ":" + owner.settlement_key;
		assign_id(state.world.monetary_account_get_canonical_id(account),
			[&](uint64_t id) { state.world.monetary_account_set_canonical_id(account, id); },
			account_key, account_ids, error);
		if(!error.empty()) add_row_error(err, "capital_owners.csv", owner.line, error);
		owners_by_id.emplace(owner.id, owner_binding{ owner.actor, owner.kind == "government", owner.kind == "person" });
	}
	(void)context;
	return err.accumulated_errors.size() == initial_errors;
}

bool resolve_factory(sys::state& state, parsers::scenario_building_context& context,
	asset_record& asset, parsers::error_handler& err, uint32_t line) {
	auto province = province_from_original_id(state, context, asset.province_original_id);
	if(!province) return false;
	auto type_it = context.map_of_factory_names.find(asset.building_key);
	if(type_it == context.map_of_factory_names.end()) return false;
	std::vector<dcon::factory_id> candidates;
	for(auto location : state.world.province_get_factory_location(province)) {
		auto factory = location.get_factory();
		if(factory.get_building_type().id == type_it->second)
			candidates.push_back(factory.id);
	}
	std::sort(candidates.begin(), candidates.end(), [](auto left, auto right) { return left.index() < right.index(); });
	if(asset.ordinal == 0 || asset.ordinal > candidates.size()) {
		add_row_error(err, "assets.csv", line, "factory selector " + std::to_string(asset.province_original_id) + "/"
			+ asset.building_key + "#" + std::to_string(asset.ordinal) + " does not resolve");
		return false;
	}
	asset.factory = candidates[asset.ordinal - 1];
	asset.site = state.world.factory_get_site_from_factory_site(asset.factory);
	if(!asset.site || !state.world.site_is_valid(asset.site)) {
		add_row_error(err, "assets.csv", line, "factory dcon:" + std::to_string(asset.factory.index()) + " has no valid canonical site");
		return false;
	}
	return true;
}

bool resolve_deposit(sys::state& state, parsers::scenario_building_context& context,
	asset_record& asset, parsers::error_handler& err, uint32_t line) {
	asset.province = province_from_original_id(state, context, asset.province_original_id);
	if(!asset.province) return false;
	auto commodity = find_commodity(context, asset.commodity_key);
	if(!commodity) return false;
	std::vector<dcon::resource_deposit_id> matches;
	state.world.for_each_resource_deposit([&](dcon::resource_deposit_id deposit) {
		auto site = state.world.resource_deposit_get_site_from_resource_deposit_site(deposit);
		auto province = site ? state.world.site_get_province_from_site_location(site) : dcon::province_id{};
		if(province == asset.province && state.world.resource_deposit_get_commodity(deposit) == commodity)
			matches.push_back(deposit);
	});
	if(matches.size() != 1) {
		add_row_error(err, "assets.csv", line, "deposit selector " + std::to_string(asset.province_original_id) + "/"
			+ asset.commodity_key + " resolves to " + std::to_string(matches.size()) + " deposits; expected one");
		return false;
	}
	asset.deposit = matches.front();
	asset.site = state.world.resource_deposit_get_site_from_resource_deposit_site(asset.deposit);
	return bool(asset.site);
}

bool load_assets(sys::state& state, parsers::scenario_building_context& context,
	parsers::error_handler& err, std::vector<asset_record>& assets,
	std::vector<firm_record> const& firms,
	std::unordered_map<std::string, size_t> const& firm_by_id,
	std::unordered_map<uint64_t, std::string>& asset_ids,
	std::unordered_map<uint64_t, std::string>& site_ids,
	std::unordered_map<std::string, dcon::asset_id>& assets_by_id,
	std::unordered_map<std::string, size_t>& asset_row_by_id) {
	auto initial_errors = err.accumulated_errors.size();
	std::sort(assets.begin(), assets.end(), [](auto const& left, auto const& right) { return left.id < right.id; });
	std::unordered_map<uint32_t, std::string> bound_factory_ids;
	std::unordered_map<uint32_t, std::string> bound_deposit_ids;
	std::unordered_map<uint64_t, std::pair<dcon::site_id, std::string>> site_key_bindings;
	std::unordered_map<uint32_t, std::string> dcon_site_bindings;
	for(size_t i = 0; i < assets.size(); ++i) {
		auto& record = assets[i];
		bool resolved = record.kind == "factory"
			? resolve_factory(state, context, record, err, record.line)
			: resolve_deposit(state, context, record, err, record.line);
		if(!resolved) continue;
		auto dcon_site = record.site.index();
		auto site_key = "site:" + record.site_key;
		auto site_hash = stable_id(site_key);
		if(auto it = site_key_bindings.find(site_hash); it != site_key_bindings.end()
			&& (it->second.first != record.site || it->second.second != record.site_key)) {
			add_row_error(err, "assets.csv", record.line, "site_id '" + record.site_key + "' resolves to more than one physical site");
		} else site_key_bindings.emplace(site_hash, std::pair{ record.site, record.site_key });
		if(auto it = dcon_site_bindings.find(dcon_site); it != dcon_site_bindings.end() && it->second != record.site_key)
			add_row_error(err, "assets.csv", record.line, "site dcon:" + std::to_string(dcon_site) + " is assigned multiple stable site_id values");
		else dcon_site_bindings.emplace(dcon_site, record.site_key);
		std::string id_error;
		assign_id(state.world.site_get_canonical_id(record.site),
			[&](uint64_t id) { state.world.site_set_canonical_id(record.site, id); }, site_key, site_ids, id_error);
		if(!id_error.empty()) add_row_error(err, "assets.csv", record.line, id_error);
		if(record.kind == "factory") {
			if(auto it = bound_factory_ids.find(record.factory.index()); it != bound_factory_ids.end())
				add_row_error(err, "assets.csv", record.line, "factory dcon:" + std::to_string(record.factory.index())
					+ " is assigned by both asset_id '" + it->second + "' and '" + record.id + "'");
			else bound_factory_ids.emplace(record.factory.index(), record.id);
		} else {
			if(auto it = bound_deposit_ids.find(record.deposit.index()); it != bound_deposit_ids.end())
				add_row_error(err, "assets.csv", record.line, "deposit dcon:" + std::to_string(record.deposit.index())
					+ " is assigned by both asset_id '" + it->second + "' and '" + record.id + "'");
			else bound_deposit_ids.emplace(record.deposit.index(), record.id);
		}
		auto operator_firm = firm_by_id.find(record.operator_id);
		if(operator_firm == firm_by_id.end()) continue;
		std::string error;
		if(record.kind == "factory") {
			assign_id(state.world.factory_get_canonical_id(record.factory),
				[&](uint64_t id) { state.world.factory_set_canonical_id(record.factory, id); },
				"factory:productive:" + record.id, asset_ids, error);
		} else {
			assign_id(state.world.resource_deposit_get_canonical_id(record.deposit),
				[&](uint64_t id) { state.world.resource_deposit_set_canonical_id(record.deposit, id); },
				"deposit:productive:" + record.id, asset_ids, error);
		}
		if(!error.empty()) add_row_error(err, "assets.csv", record.line, error);
		if(record.kind == "factory" && state.world.factory_get_asset_from_factory_asset(record.factory))
			add_row_error(err, "assets.csv", record.line, "factory dcon:" + std::to_string(record.factory.index()) + " already has an asset relation before canonical import");
		if(record.kind == "deposit" && state.world.resource_deposit_get_asset_from_resource_deposit_asset(record.deposit))
			add_row_error(err, "assets.csv", record.line, "deposit dcon:" + std::to_string(record.deposit.index()) + " already has an asset relation before canonical import");
		record.asset = state.world.create_asset();
		auto asset_key = "asset:productive:" + record.id;
		assign_id(state.world.asset_get_canonical_id(record.asset),
			[&](uint64_t id) { state.world.asset_set_canonical_id(record.asset, id); }, asset_key, asset_ids, error);
		if(!error.empty()) add_row_error(err, "assets.csv", record.line, error);
		state.world.asset_set_appraised_value(record.asset, record.appraised_value);
		if(record.kind == "factory") {
			state.world.force_create_factory_asset(record.factory, record.asset);
			if(!organizations::bind_factory_operator(state, firms[operator_firm->second].organization, record.factory))
				add_row_error(err, "assets.csv", record.line, "could not bind explicitly declared operator to factory dcon:" + std::to_string(record.factory.index()));
		} else {
			state.world.force_create_resource_deposit_asset(record.deposit, record.asset);
			if(!organizations::bind_deposit_operator(state, firms[operator_firm->second].organization, record.deposit))
				add_row_error(err, "assets.csv", record.line, "could not bind explicitly declared operator to deposit dcon:" + std::to_string(record.deposit.index()));
		}
		assets_by_id.emplace(record.id, record.asset);
		asset_row_by_id.emplace(record.id, i);
	}

	state.world.for_each_factory([&](dcon::factory_id factory) {
		if(bound_factory_ids.contains(factory.index())) return;
		auto site = state.world.factory_get_site_from_factory_site(factory);
		add_row_error(err, "assets.csv", 0, "factory dcon:" + std::to_string(factory.index())
			+ " at site dcon:" + std::to_string(site.index())
			+ " has no explicit canonical asset, operator, cash-bearing firm, and ownership row");
	});
	state.world.for_each_resource_deposit([&](dcon::resource_deposit_id deposit) {
		if(bound_deposit_ids.contains(deposit.index())) return;
		auto site = state.world.resource_deposit_get_site_from_resource_deposit_site(deposit);
		add_row_error(err, "assets.csv", 0, "resource deposit dcon:" + std::to_string(deposit.index())
			+ " at site dcon:" + std::to_string(site.index()) + " has no explicit canonical asset and operator row");
	});
	return err.accumulated_errors.size() == initial_errors;
}

bool load_ownership(sys::state& state, parsers::error_handler& err,
	std::vector<firm_record> const& firms, std::vector<owner_record> const& owners,
	std::vector<asset_record> const& assets, std::vector<stake_record>& stakes,
	std::unordered_map<std::string, owner_binding> const& owners_by_id,
	std::unordered_map<std::string, dcon::asset_id>& assets_by_id,
	std::unordered_map<uint64_t, std::string>& stake_ids) {
	auto initial_errors = err.accumulated_errors.size();
	std::sort(stakes.begin(), stakes.end(), [](auto const& left, auto const& right) {
		if(left.asset_id != right.asset_id) return left.asset_id < right.asset_id;
		if(left.owner_type != right.owner_type) return left.owner_type < right.owner_type;
		return left.owner_id < right.owner_id;
	});
	std::unordered_map<std::string, std::unordered_set<std::string>> owners_per_asset;
	std::unordered_map<std::string, bool> equity_has_government_owner;
	for(auto const& firm : firms) equity_has_government_owner.emplace("equity:" + firm.id, false);
	std::unordered_map<std::string, dcon::economic_actor_id> actor_by_firm;
	for(auto const& firm : firms) actor_by_firm.emplace(firm.id, firm.actor);
	std::unordered_map<std::string, dcon::asset_id> target_assets = assets_by_id;
	for(auto const& firm : firms) target_assets.emplace("equity:" + firm.id, firm.equity_asset);
	for(auto const& stake : stakes) {
		auto target = target_assets.find(stake.asset_id);
		if(target == target_assets.end()) {
			add_row_error(err, "ownership.csv", stake.line, "unknown asset_id '" + stake.asset_id + "'");
			continue;
		}
		dcon::economic_actor_id owner{};
		bool government_owner = false;
		if(stake.owner_type == "firm") {
			auto firm = actor_by_firm.find(stake.owner_id);
			if(firm != actor_by_firm.end()) owner = firm->second;
		} else {
			auto capital_owner = owners_by_id.find(stake.owner_id);
			if(capital_owner != owners_by_id.end()) {
				owner = capital_owner->second.actor;
				government_owner = capital_owner->second.government;
			}
		}
		if(!owner) {
			add_row_error(err, "ownership.csv", stake.line, "owner '" + stake.owner_type + ":" + stake.owner_id + "' is not explicitly declared");
			continue;
		}
		auto owner_key = stake.owner_type + ":" + stake.owner_id;
		if(!owners_per_asset[stake.asset_id].insert(owner_key).second) {
			add_row_error(err, "ownership.csv", stake.line, "duplicate owner row for asset '" + stake.asset_id + "'");
			continue;
		}
		if(stake.asset_id.starts_with("equity:") && government_owner)
			equity_has_government_owner[stake.asset_id] = true;
		if(stake.asset_id.starts_with("equity:") && stake.owner_type == "firm"
			&& stake.owner_id == stake.asset_id.substr(7)) {
			add_row_error(err, "ownership.csv", stake.line, "firm cannot own its own equity asset '" + stake.asset_id + "'");
			continue;
		}
		auto created = ownership::create_stake(state, owner, target->second,
			stake.ownership, stake.voting, stake.economic);
		if(!created) {
			add_row_error(err, "ownership.csv", stake.line, "could not create ownership stake for asset '" + stake.asset_id + "'");
			continue;
		}
		std::string error;
		auto canonical_key = "stake:" + stake.asset_id + ":" + owner_key;
		assign_id(state.world.ownership_stake_get_canonical_id(created),
			[&](uint64_t id) { state.world.ownership_stake_set_canonical_id(created, id); },
			canonical_key, stake_ids, error);
		if(!error.empty()) add_row_error(err, "ownership.csv", stake.line, error);
	}
	for(auto const& [asset_id, asset] : target_assets) {
		if(owners_per_asset[asset_id].empty()) {
			std::string detail = "asset '" + asset_id + "' has no explicitly declared owner relation";
			if(auto source = std::find_if(assets.begin(), assets.end(), [&](auto const& row) { return row.id == asset_id; }); source != assets.end()) {
				if(source->kind == "factory") {
					auto site = source->factory ? state.world.factory_get_site_from_factory_site(source->factory) : dcon::site_id{};
					detail += " for factory dcon:" + std::to_string(source->factory.index())
						+ " at site dcon:" + std::to_string(site.index());
				} else {
					auto site = source->deposit ? state.world.resource_deposit_get_site_from_resource_deposit_site(source->deposit) : dcon::site_id{};
					detail += " for resource deposit dcon:" + std::to_string(source->deposit.index())
						+ " at site dcon:" + std::to_string(site.index());
				}
			}
			add_row_error(err, "ownership.csv", 0, detail);
		}
	}
	for(auto const& firm : firms) {
		if(firm.kind == ownership::actor_kind::state_entity
			&& !equity_has_government_owner["equity:" + firm.id])
			add_row_error(err, "ownership.csv", 0, "state_entity firm '" + firm.id + "' must have an explicit government owner on equity:" + firm.id);
	}
	(void)owners;
	(void)assets;
	return err.accumulated_errors.size() == initial_errors;
}

bool load_loans(sys::state& state, parsers::error_handler& err,
	std::vector<firm_record> const& firms, std::vector<asset_record> const& assets,
	std::vector<loan_record>& loans,
	std::unordered_map<std::string, owner_binding> const& owners_by_id,
	std::unordered_map<uint64_t, std::string>& obligation_ids) {
	auto initial_errors = err.accumulated_errors.size();
	std::sort(loans.begin(), loans.end(), [](auto const& left, auto const& right) { return left.id < right.id; });
	std::unordered_map<std::string, firm_record const*> firm_by_id;
	for(auto const& firm : firms) firm_by_id.emplace(firm.id, &firm);
	std::unordered_map<std::string, asset_record const*> asset_by_id;
	for(auto const& asset : assets) asset_by_id.emplace(asset.id, &asset);
	std::unordered_set<std::string> loan_ids;
	for(auto const& loan : loans) {
		if(!loan_ids.insert(loan.id).second) {
			add_row_error(err, "loans.csv", loan.line, "duplicate loan_id '" + loan.id + "'");
			continue;
		}
		auto asset = asset_by_id.find(loan.asset_id);
		if(asset == asset_by_id.end() || asset->second->kind != "factory" || !asset->second->factory) {
			add_row_error(err, "loans.csv", loan.line, "asset_id '" + loan.asset_id + "' must identify a loaded factory asset");
			continue;
		}
		auto debtor = firm_by_id.find(asset->second->operator_id);
		if(debtor == firm_by_id.end()) continue;
		dcon::economic_actor_id creditor{};
		if(loan.creditor_type == "firm") {
			auto firm = firm_by_id.find(loan.creditor_id);
			if(firm != firm_by_id.end() && firm->second->kind == ownership::actor_kind::bank
				&& firm->second->settlement == debtor->second->settlement)
				creditor = firm->second->actor;
			else add_row_error(err, "loans.csv", loan.line,
				"firm creditor must be an explicitly declared bank with the debtor's settlement commodity");
		} else {
			auto owner = owners_by_id.find(loan.creditor_id);
			if(owner != owners_by_id.end() && owner->second.government) creditor = owner->second.actor;
			else add_row_error(err, "loans.csv", loan.line, "capital_owner creditor must be an explicitly declared government institution");
		}
		if(!creditor) continue;
		if(creditor == debtor->second->actor) {
			add_row_error(err, "loans.csv", loan.line, "debtor and creditor resolve to the same economic actor");
			continue;
		}
		auto issue_ymd = parse_iso_date(loan.creation_date_text);
		auto due_ymd = parse_iso_date(loan.due_date_text);
		if(!issue_ymd || !due_ymd) {
			add_row_error(err, "loans.csv", loan.line, "creation_date and due_date must use valid YYYY-MM-DD dates");
			continue;
		}
		sys::absolute_time_point issue_absolute(*issue_ymd);
		sys::absolute_time_point due_absolute(*due_ymd);
		sys::absolute_time_point scenario_absolute(state.current_date.to_ymd(state.start_date));
		if(issue_absolute < state.start_date || issue_absolute > scenario_absolute || due_absolute < issue_absolute) {
			add_row_error(err, "loans.csv", loan.line,
				"loan dates must satisfy campaign_start <= creation_date <= scenario_date and due_date >= creation_date");
			continue;
		}
		auto issue_date = sys::date(*issue_ymd, state.start_date);
		auto due_date = sys::date(*due_ymd, state.start_date);
		if(!issue_date || !due_date) {
			add_row_error(err, "loans.csv", loan.line, "loan date exceeds the saveable campaign date range");
			continue;
		}
		auto obligation = economy::relations::create_obligation(state,
			debtor->second->actor, creditor, loan.principal, debtor->second->settlement,
			issue_date, due_date, loan.annual_rate, economy::relations::obligation_kind::loan);
		if(!obligation) {
			add_row_error(err, "loans.csv", loan.line, "could not create firm-specific loan '" + loan.id + "'");
			continue;
		}
		state.world.obligation_set_accrued_interest(obligation, loan.accrued_interest);
		state.world.obligation_set_last_interest_accrual_date(obligation, state.current_date);
		state.world.force_create_obligation_factory(obligation, asset->second->factory);
		std::string error;
		assign_id(state.world.obligation_get_canonical_id(obligation),
			[&](uint64_t id) { state.world.obligation_set_canonical_id(obligation, id); },
			"obligation:loan:" + loan.id, obligation_ids, error);
		if(!error.empty()) add_row_error(err, "loans.csv", loan.line, error);
	}
	return err.accumulated_errors.size() == initial_errors;
}

void clear_legacy_producer_support(sys::state& state) {
	state.world.for_each_factory([&](dcon::factory_id factory) {
		state.world.factory_set_subsidized(factory, false);
	});
	state.world.for_each_nation([&](dcon::nation_id nation) {
		state.world.nation_set_subsidy_token_total(nation, 0.0f);
		state.world.nation_set_subsidy_token_price(nation, 0.0f);
	});
}

} // namespace

bool load(sys::state& state, simple_fs::directory const& common,
	parsers::scenario_building_context& context, parsers::error_handler& err) {
	auto initial_errors = err.accumulated_errors.size();
	table firm_table;
	table owner_table;
	table asset_table;
	table ownership_table;
	table loan_table;
	if(!read_and_parse_tables(common, err, firm_table, owner_table, asset_table, ownership_table, loan_table)) {
		err.fatal = true;
		return false;
	}
	std::vector<firm_record> firms;
	std::vector<owner_record> owners;
	std::vector<asset_record> assets;
	std::vector<stake_record> stakes;
	std::vector<loan_record> loans;
	if(!parse_tables(state, context, err, firm_table, owner_table, asset_table,
		ownership_table, loan_table, firms, owners, assets, stakes, loans)) {
		err.fatal = true;
		return false;
	}

	std::unordered_map<uint64_t, std::string> actor_ids;
	std::unordered_map<uint64_t, std::string> organization_ids;
	std::unordered_map<uint64_t, std::string> institution_ids;
	std::unordered_map<uint64_t, std::string> asset_ids;
	std::unordered_map<uint64_t, std::string> site_ids;
	std::unordered_map<uint64_t, std::string> account_ids;
	std::unordered_map<uint64_t, std::string> stake_ids;
	std::unordered_map<uint64_t, std::string> obligation_ids;
	std::unordered_map<std::string, size_t> firm_by_id;
	std::unordered_map<std::string, owner_binding> owners_by_id;
	std::unordered_map<std::string, dcon::asset_id> assets_by_id;
	std::unordered_map<std::string, size_t> asset_row_by_id;
	if(!load_firms(state, context, err, firms, firm_by_id, actor_ids, organization_ids,
		asset_ids, account_ids, assets_by_id)
		|| !load_capital_owners(state, context, err, owners, owners_by_id,
			actor_ids, institution_ids, account_ids)
		|| !load_assets(state, context, err, assets, firms, firm_by_id, asset_ids, site_ids,
			assets_by_id, asset_row_by_id)
		|| !load_ownership(state, err, firms, owners, assets, stakes, owners_by_id,
			assets_by_id, stake_ids)
		|| !load_loans(state, err, firms, assets, loans, owners_by_id, obligation_ids)) {
		err.fatal = true;
		return false;
	}

	clear_legacy_producer_support(state);
	std::vector<std::string> invariant_errors;
	ownership::collect_canonical_ownership_errors(state, invariant_errors);
	if(!invariant_errors.empty()) {
		for(auto const& message : invariant_errors)
			err.accumulated_errors += "canonical ownership invariant: " + message + "\n";
		err.fatal = true;
		return false;
	}
	if(err.accumulated_errors.size() != initial_errors) {
		err.fatal = true;
		return false;
	}
	return true;
}

} // namespace actors::canonical_scenario
