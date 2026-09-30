#include "canonical_scenario.hpp"

#include "actors/organizations/organizations.hpp"
#include "actors/ownership.hpp"
#include "economy/accounts/accounts.hpp"
#include "economy/banking/banking.hpp"
#include "economy/physical/exact_person_goods.hpp"
#include "economy/physical/deposits.hpp"
#include "economy/relations/relations.hpp"
#include "governance/governance.hpp"
#include "military/land_forces.hpp"
#include "nations/nations.hpp"
#include "parsing/parsers.hpp"
#include "parsers_declarations.hpp"
#include "persons/persons.hpp"
#include "persons/exact_population.hpp"
#include "system_state.hpp"
#include "technology/technology_kernel.hpp"
#include "world/spatial_runtime.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <map>
#include <optional>
#include <set>
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
	float collateral_value = 0.0f;
	std::string creation_date_text;
	std::string due_date_text;
	uint32_t line = 0;
};

struct bank_record {
	std::string id;
	std::string jurisdiction_key;
	std::string settlement_key;
	uint32_t line = 0;
	dcon::nation_id jurisdiction{};
	dcon::commodity_id settlement{};
	economy::banking::bank_policy policy{};
	float opening_reserves = 0.0f;
	float opening_equity = 0.0f;
	float opening_deposit_liabilities = 0.0f;
	dcon::organization_id organization{};
};

struct bank_deposit_record {
	std::string id;
	std::string bank_id;
	std::string owner_type;
	std::string owner_id;
	uint32_t line = 0;
	float opening_balance = 0.0f;
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

bool resolve_person_reference(sys::state& state, std::string const& value,
	dcon::person_id& result) {
	auto separator = value.find(':');
	if(separator == std::string::npos || value.find(':', separator + 1) != std::string::npos) return false;
	uint32_t cell = 0;
	uint64_t ordinal = 0;
	if(!parse_integer(std::string_view(value).substr(0, separator), cell)
		|| !parse_integer(std::string_view(value).substr(separator + 1), ordinal) || cell == 0) return false;
	persons::person_key key{cell, ordinal};
	if(!persons::exists(state, key) || !persons::alive(state, key)) return false;
	result = persons::materialize_profile(state, key);
	return result && state.world.person_is_valid(result)
		&& bool(persons::actor_for_person(state, result));
}

bool read_and_parse_tables(simple_fs::directory const& common,
	parsers::error_handler& err, table& firms, table& owners, table& assets,
	table& ownerships, table& loans, table& banks, table& bank_deposits) {
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
	static constexpr std::array<std::string_view, 10> loans_header = {
		"loan_id", "asset_id", "creditor_type", "creditor_id", "principal", "annual_rate", "creation_date", "due_date", "accrued_interest", "collateral_value"
	};
	static constexpr std::array<std::string_view, 14> banks_header = {
		"bank_id", "jurisdiction", "settlement", "opening_reserves", "opening_equity",
		"opening_deposit_liabilities", "lending_base_rate", "minimum_capital_ratio",
		"liquidity_target", "risk_appetite", "lending_spread", "max_single_borrower_exposure",
		"reserve_requirement", "capital_breach_grace_days"
	};
	static constexpr std::array<std::string_view, 5> bank_deposits_header = {
		"deposit_id", "bank_id", "owner_type", "owner_id", "opening_balance"
	};
	auto canonical = simple_fs::open_directory(common, NATIVE("canonical_runtime"));
	auto before = err.accumulated_errors.size();
	auto f = read_table(canonical, "firms.csv", firms_header, err);
	auto o = read_table(canonical, "capital_owners.csv", owners_header, err);
	auto a = read_table(canonical, "assets.csv", assets_header, err);
	auto s = read_table(canonical, "ownership.csv", ownership_header, err);
	auto l = read_table(canonical, "loans.csv", loans_header, err);
	auto b = read_table(canonical, "banks.csv", banks_header, err);
	auto d = read_table(canonical, "bank_deposits.csv", bank_deposits_header, err);
	if(!f || !o || !a || !s || !l || !b || !d || err.accumulated_errors.size() != before) return false;
	firms = std::move(*f);
	owners = std::move(*o);
	assets = std::move(*a);
	ownerships = std::move(*s);
	loans = std::move(*l);
	banks = std::move(*b);
	bank_deposits = std::move(*d);
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
		if(value.creditor_type != "firm")
			add_row_error(err, "loans.csv", source.line, "creditor_type must be 'firm'; canonical firm loans require an explicitly declared bank creditor");
		if(!valid_key(value.creditor_id)) add_row_error(err, "loans.csv", source.line, "creditor_id must use ASCII letters, digits, '.', '_' or '-'");
		if(!parse_float(source.cells[4], value.principal) || value.principal <= 0.0f)
			add_row_error(err, "loans.csv", source.line, "principal must be finite and positive");
		if(!parse_float(source.cells[5], value.annual_rate) || value.annual_rate < 0.0f)
			add_row_error(err, "loans.csv", source.line, "annual_rate must be finite and nonnegative");
		if(!parse_float(source.cells[8], value.accrued_interest) || value.accrued_interest < 0.0f)
			add_row_error(err, "loans.csv", source.line, "accrued_interest must be finite and nonnegative");
		if(!parse_float(source.cells[9], value.collateral_value) || value.collateral_value < 0.0f)
			add_row_error(err, "loans.csv", source.line, "collateral_value must be finite and nonnegative");
		loans.push_back(std::move(value));
	}
	return err.accumulated_errors.size() == before;
}

bool parse_bank_tables(sys::state& state, parsers::error_handler& err,
	table const& bank_table, table const& deposit_table,
	std::vector<firm_record> const& firms, std::vector<owner_record> const& owners,
	std::vector<bank_record>& banks, std::vector<bank_deposit_record>& deposits) {
	auto initial_errors = err.accumulated_errors.size();
	std::unordered_map<std::string, firm_record const*> firms_by_id;
	for(auto const& firm : firms) firms_by_id.emplace(firm.id, &firm);
	std::unordered_set<std::string> bank_ids;
	for(auto const& source : bank_table.rows) {
		bank_record value;
		value.id = source.cells[0];
		value.jurisdiction_key = source.cells[1];
		value.settlement_key = source.cells[2];
		value.line = source.line;
		if(!valid_key(value.id)) add_row_error(err, "banks.csv", source.line, "bank_id must use ASCII letters, digits, '.', '_' or '-'");
		if(!bank_ids.insert(value.id).second) add_row_error(err, "banks.csv", source.line, "duplicate bank_id '" + value.id + "'");
		auto firm = firms_by_id.find(value.id);
		if(firm == firms_by_id.end() || firm->second->kind != ownership::actor_kind::bank)
			add_row_error(err, "banks.csv", source.line, "bank_id must reference a firm row with kind 'bank'");
		value.jurisdiction = find_nation_by_tag(state, value.jurisdiction_key);
		if(!value.jurisdiction) add_row_error(err, "banks.csv", source.line, "jurisdiction must name an active three-letter country tag");
		if(firm != firms_by_id.end() && firm->second->settlement_key != value.settlement_key)
			add_row_error(err, "banks.csv", source.line, "settlement must match the bank firm's settlement currency");
		if(!parse_float(source.cells[3], value.opening_reserves) || value.opening_reserves < 0.0f)
			add_row_error(err, "banks.csv", source.line, "opening_reserves must be finite and nonnegative");
		if(!parse_float(source.cells[4], value.opening_equity))
			add_row_error(err, "banks.csv", source.line, "opening_equity must be finite");
		if(!parse_float(source.cells[5], value.opening_deposit_liabilities) || value.opening_deposit_liabilities < 0.0f)
			add_row_error(err, "banks.csv", source.line, "opening_deposit_liabilities must be finite and nonnegative");
		if(!parse_float(source.cells[6], value.policy.lending_base_rate))
			add_row_error(err, "banks.csv", source.line, "lending_base_rate must be finite");
		if(!parse_float(source.cells[7], value.policy.minimum_capital_ratio))
			add_row_error(err, "banks.csv", source.line, "minimum_capital_ratio must be finite");
		if(!parse_float(source.cells[8], value.policy.liquidity_target))
			add_row_error(err, "banks.csv", source.line, "liquidity_target must be finite");
		if(!parse_float(source.cells[9], value.policy.risk_appetite))
			add_row_error(err, "banks.csv", source.line, "risk_appetite must be finite");
		if(!parse_float(source.cells[10], value.policy.lending_spread))
			add_row_error(err, "banks.csv", source.line, "lending_spread must be finite");
		if(!parse_float(source.cells[11], value.policy.max_single_borrower_exposure))
			add_row_error(err, "banks.csv", source.line, "max_single_borrower_exposure must be finite");
		if(!parse_float(source.cells[12], value.policy.reserve_requirement))
			add_row_error(err, "banks.csv", source.line, "reserve_requirement must be finite");
		if(!parse_integer(source.cells[13], value.policy.capital_breach_grace_days))
			add_row_error(err, "banks.csv", source.line, "capital_breach_grace_days must be an unsigned 16-bit integer");
		auto fraction = [](float parameter) {
			return std::isfinite(parameter) && parameter >= 0.0f && parameter <= 1.0f;
		};
		if(!fraction(value.policy.minimum_capital_ratio))
			add_row_error(err, "banks.csv", source.line, "minimum_capital_ratio must be between 0 and 1");
		if(!fraction(value.policy.liquidity_target))
			add_row_error(err, "banks.csv", source.line, "liquidity_target must be between 0 and 1");
		if(!fraction(value.policy.risk_appetite))
			add_row_error(err, "banks.csv", source.line, "risk_appetite must be between 0 and 1");
		if(!fraction(value.policy.lending_spread))
			add_row_error(err, "banks.csv", source.line, "lending_spread must be between 0 and 1");
		if(!fraction(value.policy.max_single_borrower_exposure))
			add_row_error(err, "banks.csv", source.line, "max_single_borrower_exposure must be between 0 and 1");
		if(!fraction(value.policy.reserve_requirement))
			add_row_error(err, "banks.csv", source.line, "reserve_requirement must be between 0 and 1");
		if(std::isfinite(value.policy.lending_base_rate)
			&& (value.policy.lending_base_rate < 0.0f || value.policy.lending_base_rate > 1.0f))
			add_row_error(err, "banks.csv", source.line, "lending_base_rate must be between 0 and 1");
		if(std::isfinite(value.policy.lending_base_rate) && std::isfinite(value.policy.lending_spread)
			&& value.policy.lending_base_rate + value.policy.lending_spread > 1.0f)
			add_row_error(err, "banks.csv", source.line, "lending_base_rate plus lending_spread cannot exceed 1");
		value.settlement = firm == firms_by_id.end() ? dcon::commodity_id{} : firm->second->settlement;
		value.policy.jurisdiction = value.jurisdiction;
		value.policy.settlement = value.settlement;
		if(firm != firms_by_id.end()) {
			value.organization = firm->second->organization;
			if(firm->second->opening_cash != 0.0f)
				add_row_error(err, "firms.csv", firm->second->line,
					"bank opening cash must be zero; authored bank reserves belong in banks.csv");
			if(std::abs(firm->second->paid_in_equity + firm->second->retained_earnings - value.opening_equity)
				> 1.0e-4f * std::max(1.0f, std::abs(value.opening_equity)))
				add_row_error(err, "banks.csv", source.line,
					"opening_equity must equal the bank firm's paid_in_equity plus retained_earnings");
		}
		banks.push_back(std::move(value));
	}
	for(auto const& firm : firms) {
		if(firm.kind == ownership::actor_kind::bank && !bank_ids.contains(firm.id))
			add_row_error(err, "banks.csv", 0, "bank firm '" + firm.id + "' has no authored bank policy and opening balance sheet");
	}

	std::unordered_map<std::string, bank_record const*> banks_by_id;
	for(auto const& bank : banks) banks_by_id.emplace(bank.id, &bank);
	std::unordered_map<std::string, owner_record const*> owners_by_id;
	for(auto const& owner : owners) owners_by_id.emplace(owner.id, &owner);
	std::unordered_set<std::string> deposit_ids;
	std::set<std::pair<std::string, std::string>> owner_accounts;
	std::unordered_map<std::string, double> opening_deposit_totals;
	for(auto const& source : deposit_table.rows) {
		bank_deposit_record value;
		value.id = source.cells[0];
		value.bank_id = source.cells[1];
		value.owner_type = source.cells[2];
		value.owner_id = source.cells[3];
		value.line = source.line;
		if(!valid_key(value.id) || !deposit_ids.insert(value.id).second)
			add_row_error(err, "bank_deposits.csv", source.line, "deposit_id must be valid and unique");
		if(!banks_by_id.contains(value.bank_id))
			add_row_error(err, "bank_deposits.csv", source.line, "bank_id must reference an explicitly declared bank");
		if(value.owner_type == "firm") {
			if(!firms_by_id.contains(value.owner_id)) add_row_error(err, "bank_deposits.csv", source.line, "firm owner_id is not declared in firms.csv");
		} else if(value.owner_type == "capital_owner") {
			if(!owners_by_id.contains(value.owner_id)) add_row_error(err, "bank_deposits.csv", source.line, "capital_owner owner_id is not declared in capital_owners.csv");
		} else add_row_error(err, "bank_deposits.csv", source.line, "owner_type must be 'firm' or 'capital_owner'");
		if(!parse_float(source.cells[4], value.opening_balance) || value.opening_balance < 0.0f)
			add_row_error(err, "bank_deposits.csv", source.line, "opening_balance must be finite and nonnegative");
		if(!owner_accounts.emplace(value.bank_id, value.owner_type + ":" + value.owner_id).second)
			add_row_error(err, "bank_deposits.csv", source.line, "a bank may have only one deposit account per declared owner");
		opening_deposit_totals[value.bank_id] += value.opening_balance;
		deposits.push_back(std::move(value));
	}
	for(auto const& bank : banks) {
		auto actual = opening_deposit_totals[bank.id];
		if(std::abs(actual - bank.opening_deposit_liabilities)
			> 1.0e-4 * std::max(1.0, std::abs(double(bank.opening_deposit_liabilities))))
			add_row_error(err, "banks.csv", bank.line,
				"opening_deposit_liabilities does not equal the sum of explicitly declared bank_deposits.csv accounts for '" + bank.id + "'");
	}
	return err.accumulated_errors.size() == initial_errors;
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

		if(firm.kind == ownership::actor_kind::bank) {
			// Bank cash and deposits are authored as reserve and customer liability
			// accounts by banks.csv and bank_deposits.csv below.
		} else if(economy::accounts::find_account(state, firm.actor, firm.settlement)) {
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

bool load_banks(sys::state& state, parsers::error_handler& err,
	std::vector<bank_record>& banks, std::vector<bank_deposit_record>& deposits,
	std::vector<firm_record> const& firms,
	std::unordered_map<std::string, owner_binding> const& owners_by_id,
	std::unordered_map<uint64_t, std::string>& account_ids) {
	auto initial_errors = err.accumulated_errors.size();
	std::sort(banks.begin(), banks.end(), [](auto const& left, auto const& right) { return left.id < right.id; });
	std::unordered_map<std::string, firm_record const*> firms_by_id;
	std::unordered_map<std::string, bank_record*> banks_by_id;
	for(auto const& firm : firms) firms_by_id.emplace(firm.id, &firm);
	for(auto& bank : banks) banks_by_id.emplace(bank.id, &bank);
	for(auto& bank : banks) {
		auto firm = firms_by_id.find(bank.id);
		if(firm == firms_by_id.end() || firm->second->kind != ownership::actor_kind::bank) continue;
		bank.organization = firm->second->organization;
		if(!economy::banking::configure_bank_policy(state, bank.organization, bank.policy)) {
			add_row_error(err, "banks.csv", bank.line, "could not configure authored bank policy for '" + bank.id + "'");
			continue;
		}
		auto reserve_key = "account:bank-reserve:" + bank.id + ":" + bank.settlement_key;
		auto reserve = economy::banking::open_reserve_account(state, bank.organization, bank.settlement, stable_id(reserve_key));
		if(!reserve || !economy::banking::bootstrap_set_reserve_balance(state, reserve, bank.opening_reserves)) {
			add_row_error(err, "banks.csv", bank.line, "could not create opening reserve account for bank '" + bank.id + "'");
			continue;
		}
		std::string error;
		assign_id(state.world.monetary_account_get_canonical_id(reserve),
			[&](uint64_t id) { state.world.monetary_account_set_canonical_id(reserve, id); },
			reserve_key, account_ids, error);
		if(!error.empty()) add_row_error(err, "banks.csv", bank.line, error);
	}
	std::sort(deposits.begin(), deposits.end(), [](auto const& left, auto const& right) {
		if(left.bank_id != right.bank_id) return left.bank_id < right.bank_id;
		return left.id < right.id;
	});
	std::unordered_map<std::string, double> total_by_bank;
	for(auto const& record : deposits) {
		auto bank = banks_by_id.find(record.bank_id);
		if(bank == banks_by_id.end()) continue;
		dcon::economic_actor_id owner{};
		if(record.owner_type == "firm") {
			auto firm = firms_by_id.find(record.owner_id);
			if(firm != firms_by_id.end()) owner = firm->second->actor;
		} else if(record.owner_type == "capital_owner") {
			auto capital_owner = owners_by_id.find(record.owner_id);
			if(capital_owner != owners_by_id.end()) owner = capital_owner->second.actor;
		}
		if(!owner) {
			add_row_error(err, "bank_deposits.csv", record.line, "deposit owner '" + record.owner_type + ":" + record.owner_id + "' resolves to no canonical actor");
			continue;
		}
		auto id_key = "deposit:" + record.id;
		auto account = economy::banking::open_deposit_account(state, bank->second->organization,
			owner, bank->second->settlement, stable_id(id_key));
		if(!account || !economy::banking::bootstrap_set_deposit_balance(state, account, record.opening_balance)) {
			add_row_error(err, "bank_deposits.csv", record.line, "could not create authored deposit account '" + record.id + "'");
			continue;
		}
		std::string error;
		assign_id(state.world.deposit_account_get_canonical_id(account),
			[&](uint64_t id) { state.world.deposit_account_set_canonical_id(account, id); },
			id_key, account_ids, error);
		if(!error.empty()) add_row_error(err, "bank_deposits.csv", record.line, error);
		total_by_bank[record.bank_id] += record.opening_balance;
	}
	for(auto const& bank : banks) {
		if(std::abs(total_by_bank[bank.id] - double(bank.opening_deposit_liabilities))
			> 1.0e-4 * std::max(1.0, std::abs(double(bank.opening_deposit_liabilities))))
			add_row_error(err, "banks.csv", bank.line,
				"loaded deposit accounts do not equal opening_deposit_liabilities for bank '" + bank.id + "'");
	}
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
		auto firm = firm_by_id.find(loan.creditor_id);
		if(firm != firm_by_id.end() && firm->second->kind == ownership::actor_kind::bank
			&& firm->second->settlement == debtor->second->settlement)
			creditor = firm->second->actor;
		else add_row_error(err, "loans.csv", loan.line,
			"creditor must be an explicitly declared bank with the debtor's settlement commodity");
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
		state.world.obligation_set_collateral_value(obligation, loan.collateral_value);
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

bool load_land_forces(sys::state& state, parsers::scenario_building_context const& context,
	simple_fs::directory const& common, parsers::error_handler& err) {
	static constexpr std::array<std::string_view, 8> equipment_header = {
		"equipment_id", "category", "commodity", "mass", "reliability", "attack", "defense", "range_km"
	};
	static constexpr std::array<std::string_view, 2> templates_header = {
		"template_id", "personnel_authorization"
	};
	static constexpr std::array<std::string_view, 3> template_equipment_header = {
		"template_id", "equipment_id", "quantity"
	};
	static constexpr std::array<std::string_view, 5> template_consumables_header = {
		"template_id", "consumable", "commodity", "per_person", "per_equipment_tonne"
	};
	static constexpr std::array<std::string_view, 8> formations_header = {
		"formation_id", "parent_formation_id", "template_id", "owner_tag", "province_id",
		"status", "operational_tempo", "legacy_regiment_index"
	};
	static constexpr std::array<std::string_view, 6> personnel_header = {
		"formation_id", "source_population_cell", "first_ordinal", "count", "ordinal_stride", "training_days"
	};
	static constexpr std::array<std::string_view, 3> formation_equipment_header = {
		"formation_id", "equipment_id", "quantity"
	};
	static constexpr std::array<std::string_view, 3> formation_consumables_header = {
		"formation_id", "consumable", "quantity"
	};
	static constexpr std::array<std::string_view, 8> stockpiles_header = {
		"stockpile_id", "owner_tag", "province_id", "stockpile_kind", "cargo_kind",
		"equipment_id", "consumable", "quantity"
	};
	auto canonical = simple_fs::open_directory(common, NATIVE("canonical_runtime"));
	auto before = err.accumulated_errors.size();
	auto equipment_rows = read_table(canonical, "military_equipment.csv", equipment_header, err);
	auto template_rows = read_table(canonical, "formation_templates.csv", templates_header, err);
	auto template_equipment_rows = read_table(canonical, "formation_template_equipment.csv", template_equipment_header, err);
	auto template_consumable_rows = read_table(canonical, "formation_template_consumables.csv", template_consumables_header, err);
	auto formation_rows = read_table(canonical, "military_formations.csv", formations_header, err);
	auto personnel_rows = read_table(canonical, "formation_personnel.csv", personnel_header, err);
	auto holding_rows = read_table(canonical, "formation_equipment.csv", formation_equipment_header, err);
	auto inventory_rows = read_table(canonical, "formation_consumables.csv", formation_consumables_header, err);
	auto stockpile_rows = read_table(canonical, "military_stockpiles.csv", stockpiles_header, err);
	if(!equipment_rows || !template_rows || !template_equipment_rows || !template_consumable_rows
		|| !formation_rows || !personnel_rows || !holding_rows || !inventory_rows || !stockpile_rows
		|| err.accumulated_errors.size() != before) return false;

	auto qualified = [](std::string_view domain, std::string_view key) {
		return std::string("land-force:") + std::string(domain) + ":" + std::string(key);
	};
	std::unordered_map<uint64_t, std::string> ids;
	std::unordered_map<std::string, uint64_t> equipment_ids;
	std::unordered_map<std::string, uint64_t> template_ids;
	std::unordered_map<std::string, uint64_t> formation_ids;
	std::unordered_map<std::string, uint64_t> stockpile_ids;
	auto declare_id = [&](std::string_view domain, std::string const& key, std::string const& file,
		uint32_t line) -> uint64_t {
		if(!valid_key(key)) {
			add_row_error(err, file, line, "stable ID key must use ASCII letters, digits, '.', '_' or '-'");
			return 0;
		}
		auto q = qualified(domain, key);
		auto id = stable_id(q);
		std::string collision;
		if(!claim_id(ids, id, q, collision)) {
			add_row_error(err, file, line, collision);
			return 0;
		}
		return id;
	};
	auto reference_id = [&](std::string_view domain, std::string const& key) {
		return key.empty() ? uint64_t(0) : stable_id(qualified(domain, key));
	};
	for(auto const& source : equipment_rows->rows) {
		military::land_forces::equipment_model model;
		model.id = declare_id("equipment", source.cells[0], "military_equipment.csv", source.line);
		if(!parse_integer(source.cells[1], model.category))
			add_row_error(err, "military_equipment.csv", source.line, "category must be an unsigned integer");
		model.commodity = find_commodity(context, source.cells[2]);
		if(!model.commodity) add_row_error(err, "military_equipment.csv", source.line, "commodity must reference a product");
		if(!parse_float(source.cells[3], model.mass) || model.mass <= 0.0f)
			add_row_error(err, "military_equipment.csv", source.line, "mass must be finite and positive");
		if(!parse_float(source.cells[4], model.reliability) || model.reliability < 0.0f || model.reliability > 1.0f)
			add_row_error(err, "military_equipment.csv", source.line, "reliability must be in [0,1]");
		if(!parse_float(source.cells[5], model.attack) || model.attack < 0.0f)
			add_row_error(err, "military_equipment.csv", source.line, "attack must be finite and nonnegative");
		if(!parse_float(source.cells[6], model.defense) || model.defense < 0.0f)
			add_row_error(err, "military_equipment.csv", source.line, "defense must be finite and nonnegative");
		if(!parse_float(source.cells[7], model.range_km) || model.range_km < 0.0f)
			add_row_error(err, "military_equipment.csv", source.line, "range_km must be finite and nonnegative");
		if(model.id != 0) equipment_ids.emplace(source.cells[0], model.id);
		if(model.id != 0 && !military::land_forces::add_equipment_model(state, model))
			add_row_error(err, "military_equipment.csv", source.line, "duplicate equipment ID or invalid equipment model");
	}
	for(auto const& source : template_rows->rows) {
		military::land_forces::formation_template item;
		item.id = declare_id("template", source.cells[0], "formation_templates.csv", source.line);
		if(!parse_integer(source.cells[1], item.personnel_authorization) || item.personnel_authorization == 0)
			add_row_error(err, "formation_templates.csv", source.line, "personnel_authorization must be a positive integer");
		if(item.id != 0) template_ids.emplace(source.cells[0], item.id);
		if(item.id != 0 && !military::land_forces::add_template(state, item))
			add_row_error(err, "formation_templates.csv", source.line, "duplicate template ID or invalid template");
	}
	for(auto const& source : template_equipment_rows->rows) {
		military::land_forces::template_equipment_authorization item;
		item.template_id = reference_id("template", source.cells[0]);
		item.equipment_model_id = reference_id("equipment", source.cells[1]);
		if(item.template_id == 0 || !template_ids.contains(source.cells[0]))
			add_row_error(err, "formation_template_equipment.csv", source.line, "template_id references a missing template");
		if(item.equipment_model_id == 0 || !equipment_ids.contains(source.cells[1]))
			add_row_error(err, "formation_template_equipment.csv", source.line, "equipment_id references a missing equipment model");
		if(!parse_integer(source.cells[2], item.quantity) || item.quantity == 0)
			add_row_error(err, "formation_template_equipment.csv", source.line, "quantity must be a positive integer");
		if(item.template_id && item.equipment_model_id && !military::land_forces::authorize_template_equipment(state, item))
			add_row_error(err, "formation_template_equipment.csv", source.line, "duplicate or invalid template equipment authorization");
	}
	for(auto const& source : template_consumable_rows->rows) {
		military::land_forces::template_consumable_requirement item;
		item.template_id = reference_id("template", source.cells[0]);
		if(source.cells[1] == "food") item.kind = military::land_forces::consumable_kind::food;
		else if(source.cells[1] == "fuel") item.kind = military::land_forces::consumable_kind::fuel;
		else if(source.cells[1] == "ammunition") item.kind = military::land_forces::consumable_kind::ammunition;
		else add_row_error(err, "formation_template_consumables.csv", source.line, "consumable must be food, fuel, or ammunition");
		item.commodity = find_commodity(context, source.cells[2]);
		if(!item.commodity) add_row_error(err, "formation_template_consumables.csv", source.line,
			"commodity must reference a product");
		float per_person = 0.0f, per_tonne = 0.0f;
		if(!parse_float(source.cells[3], per_person) || per_person < 0.0f)
			add_row_error(err, "formation_template_consumables.csv", source.line, "per_person must be finite and nonnegative");
		if(!parse_float(source.cells[4], per_tonne) || per_tonne < 0.0f)
			add_row_error(err, "formation_template_consumables.csv", source.line, "per_equipment_tonne must be finite and nonnegative");
		item.per_person = per_person;
		item.per_equipment_tonne = per_tonne;
		if(item.template_id && !military::land_forces::set_template_consumable_requirement(state, item))
			add_row_error(err, "formation_template_consumables.csv", source.line, "template_id references a missing template or requirement is invalid");
	}
	struct authored_formation {
		row const* source = nullptr;
		military::land_forces::formation value{};
		std::string key;
		std::string parent_key;
		std::optional<uint32_t> legacy_regiment_index;
		bool valid = true;
	};
	std::vector<authored_formation> formations;
	for(auto const& source : formation_rows->rows) {
		authored_formation item;
		item.source = &source;
		item.key = source.cells[0];
		item.parent_key = source.cells[1];
		item.value.id = declare_id("formation", item.key, "military_formations.csv", source.line);
		if(!item.parent_key.empty()) item.value.parent_id = reference_id("formation", item.parent_key);
		item.value.template_id = reference_id("template", source.cells[2]);
		if(!template_ids.contains(source.cells[2])) add_row_error(err, "military_formations.csv", source.line, "template_id references a missing template"), item.valid = false;
		item.value.owner = find_nation_by_tag(state, source.cells[3]);
		if(!item.value.owner) add_row_error(err, "military_formations.csv", source.line, "owner_tag references a missing nation"), item.valid = false;
		uint32_t original_province = 0;
		auto province_ok = parse_integer(source.cells[4], original_province);
		auto province = province_ok ? province_from_original_id(state, context, original_province) : dcon::province_id{};
		item.value.location = province ? world::spatial_runtime::site_for_province(state, province) : dcon::site_id{};
		if(!item.value.location) add_row_error(err, "military_formations.csv", source.line, "province_id has no canonical site"), item.valid = false;
		if(source.cells[5] == "active") item.value.status = military::land_forces::formation_status::active;
		else if(source.cells[5] == "reserve") item.value.status = military::land_forces::formation_status::reserve;
		else if(source.cells[5] == "destroyed") item.value.status = military::land_forces::formation_status::destroyed;
		else add_row_error(err, "military_formations.csv", source.line, "status must be active, reserve, or destroyed"), item.valid = false;
		if(!parse_float(source.cells[6], item.value.operational_tempo) || item.value.operational_tempo < 0.0f || item.value.operational_tempo > 1.0f)
			add_row_error(err, "military_formations.csv", source.line, "operational_tempo must be in [0,1]"), item.valid = false;
		if(!source.cells[7].empty()) {
			uint32_t index = 0;
			if(!parse_integer(source.cells[7], index))
				add_row_error(err, "military_formations.csv", source.line, "legacy_regiment_index must be an unsigned runtime index or empty"), item.valid = false;
			else item.legacy_regiment_index = index;
		}
		if(item.value.id != 0) formation_ids.emplace(item.key, item.value.id);
		formations.push_back(std::move(item));
	}
	std::unordered_map<std::string, authored_formation*> formations_by_key;
	for(auto& item : formations) formations_by_key.emplace(item.key, &item);
	std::vector<authored_formation*> pending;
	for(auto& item : formations) pending.push_back(&item);
	while(!pending.empty()) {
		bool made_progress = false;
		for(auto it = pending.begin(); it != pending.end();) {
			auto item = *it;
			if(!item->valid || item->value.id == 0) { it = pending.erase(it); made_progress = true; continue; }
			if(!item->parent_key.empty()) {
				auto parent = formations_by_key.find(item->parent_key);
				if(parent == formations_by_key.end()) {
					add_row_error(err, "military_formations.csv", item->source->line, "parent_formation_id references a missing formation");
					item->valid = false;
					it = pending.erase(it);
					made_progress = true;
					continue;
				}
				if(!military::land_forces::find_formation(state, parent->second->value.id)) { ++it; continue; }
			}
			if(!military::land_forces::create_formation(state, item->value)) {
				add_row_error(err, "military_formations.csv", item->source->line, "formation has invalid parent, owner, location, template, or duplicate ID");
				item->valid = false;
				it = pending.erase(it);
				made_progress = true;
				continue;
			}
			if(item->legacy_regiment_index) {
				auto regiment = dcon::regiment_id{dcon::regiment_id::value_base_t(*item->legacy_regiment_index)};
				auto army = state.world.regiment_is_valid(regiment)
					? state.world.regiment_get_army_from_army_membership(regiment) : dcon::army_id{};
				if(!army || state.world.army_get_controller_from_army_control(army) != item->value.owner
					|| !military::land_forces::map_legacy_regiment(state, item->value.id, regiment)) {
					add_row_error(err, "military_formations.csv", item->source->line,
						"legacy_regiment_index must resolve to one existing regiment owned by owner_tag");
					item->valid = false;
				}
			}
			it = pending.erase(it);
			made_progress = true;
		}
		if(!made_progress) {
			for(auto item : pending) add_row_error(err, "military_formations.csv", item->source->line,
				"parent formation cycle prevents deterministic creation");
			break;
		}
	}
	for(auto const& source : personnel_rows->rows) {
		auto formation_id = reference_id("formation", source.cells[0]);
		uint32_t source_cell = 0, stride = 1;
		uint64_t first = 0, count = 0;
		uint16_t training = 0;
		if(!formation_ids.contains(source.cells[0]) || !military::land_forces::find_formation(state, formation_id))
			add_row_error(err, "formation_personnel.csv", source.line, "formation_id references a missing formation");
		if(!parse_integer(source.cells[1], source_cell) || source_cell == 0)
			add_row_error(err, "formation_personnel.csv", source.line, "source_population_cell must be positive");
		if(!parse_integer(source.cells[2], first) || !parse_integer(source.cells[3], count) || count == 0)
			add_row_error(err, "formation_personnel.csv", source.line, "first_ordinal and positive count are required");
		if(!parse_integer(source.cells[4], stride) || (stride != 1 && stride != 4))
			add_row_error(err, "formation_personnel.csv", source.line, "ordinal_stride must be 1 or 4");
		if(!parse_integer(source.cells[5], training))
			add_row_error(err, "formation_personnel.csv", source.line, "training_days must be an unsigned 16-bit value");
		auto source_pop = persons::population_for_source_cell(state, source_cell);
		auto source_province = source_pop ? state.world.pop_get_province_from_pop_location(source_pop) : dcon::province_id{};
		auto source_site = source_province
			? world::spatial_runtime::site_for_province(state, source_province) : dcon::site_id{};
		auto formation = military::land_forces::find_formation(state, formation_id);
		if(!source_pop || !source_province || !formation
			|| state.world.province_get_nation_from_province_ownership(source_province) != formation->owner)
			add_row_error(err, "formation_personnel.csv", source.line, "personnel range must reference a living source POP owned by the formation nation");
		else if(source_site && count != 0
			&& !military::land_forces::personnel_route_is_valid(state, formation->owner,
				source_site, formation->location, count))
			add_row_error(err, "formation_personnel.csv", source.line,
				"personnel range cannot reach the formation over a friendly supply route");
		if(source_cell && count && stride && formation
			&& !persons::exact_population::assign_military_range(state,
				{source_cell, stride, first, count, formation_id, training, 0}))
			add_row_error(err, "formation_personnel.csv", source.line, "personnel range is dead, duplicated, outside its source cell, or exceeds identity storage");
	}
	for(auto const& source : holding_rows->rows) {
		auto formation_id = reference_id("formation", source.cells[0]);
		auto equipment_id = reference_id("equipment", source.cells[1]);
		uint64_t quantity = 0;
		if(!formation_ids.contains(source.cells[0]) || !military::land_forces::find_formation(state, formation_id))
			add_row_error(err, "formation_equipment.csv", source.line, "formation_id references a missing formation");
		if(!equipment_ids.contains(source.cells[1]) || !military::land_forces::find_equipment_model(state, equipment_id))
			add_row_error(err, "formation_equipment.csv", source.line, "equipment_id references a missing model");
		if(!parse_integer(source.cells[2], quantity))
			add_row_error(err, "formation_equipment.csv", source.line, "quantity must be an unsigned integer");
		if(formation_id && equipment_id && !military::land_forces::set_initial_equipment_holding(state, formation_id, equipment_id, quantity))
			add_row_error(err, "formation_equipment.csv", source.line, "equipment holding is not authorized or exceeds template quantity");
	}
	for(auto const& source : inventory_rows->rows) {
		auto formation_id = reference_id("formation", source.cells[0]);
		military::land_forces::consumable_kind kind = military::land_forces::consumable_kind::food;
		if(source.cells[1] == "fuel") kind = military::land_forces::consumable_kind::fuel;
		else if(source.cells[1] == "ammunition") kind = military::land_forces::consumable_kind::ammunition;
		else if(source.cells[1] != "food") add_row_error(err, "formation_consumables.csv", source.line, "consumable must be food, fuel, or ammunition");
		float quantity = 0.0f;
		if(!parse_float(source.cells[2], quantity) || quantity < 0.0f)
			add_row_error(err, "formation_consumables.csv", source.line, "quantity must be finite and nonnegative");
		if(!formation_ids.contains(source.cells[0]) || !military::land_forces::find_formation(state, formation_id))
			add_row_error(err, "formation_consumables.csv", source.line, "formation_id references a missing formation");
		if(formation_id && !military::land_forces::set_initial_consumable_inventory(state, formation_id, kind, quantity))
			add_row_error(err, "formation_consumables.csv", source.line, "duplicate or invalid formation inventory");
	}
	for(auto const& source : stockpile_rows->rows) {
		military::land_forces::stockpile item;
		item.id = declare_id("stockpile", source.cells[0], "military_stockpiles.csv", source.line);
		item.owner = find_nation_by_tag(state, source.cells[1]);
		if(!item.owner) add_row_error(err, "military_stockpiles.csv", source.line, "owner_tag references a missing nation");
		uint32_t original_province = 0;
		auto parsed_province = parse_integer(source.cells[2], original_province);
		auto province = parsed_province ? province_from_original_id(state, context, original_province) : dcon::province_id{};
		item.location = province ? world::spatial_runtime::site_for_province(state, province) : dcon::site_id{};
		if(!item.location) add_row_error(err, "military_stockpiles.csv", source.line, "province_id has no canonical site");
		if(source.cells[3] == "warehouse") item.kind = military::land_forces::stockpile_kind::warehouse;
		else if(source.cells[3] == "depot") item.kind = military::land_forces::stockpile_kind::depot;
		else add_row_error(err, "military_stockpiles.csv", source.line, "stockpile_kind must be warehouse or depot");
		if(source.cells[4] == "equipment") item.cargo = military::land_forces::cargo_kind::equipment;
		else if(source.cells[4] == "consumable") item.cargo = military::land_forces::cargo_kind::consumable;
		else add_row_error(err, "military_stockpiles.csv", source.line, "cargo_kind must be equipment or consumable");
		if(item.cargo == military::land_forces::cargo_kind::equipment) {
			item.equipment_model_id = reference_id("equipment", source.cells[5]);
			if(!equipment_ids.contains(source.cells[5])) add_row_error(err, "military_stockpiles.csv", source.line, "equipment_id references a missing model");
		} else {
			if(!source.cells[5].empty()) add_row_error(err, "military_stockpiles.csv", source.line, "equipment_id must be empty for consumable cargo");
			if(source.cells[6] == "fuel") item.consumable = military::land_forces::consumable_kind::fuel;
			else if(source.cells[6] == "ammunition") item.consumable = military::land_forces::consumable_kind::ammunition;
			else if(source.cells[6] != "food") add_row_error(err, "military_stockpiles.csv", source.line, "consumable must be food, fuel, or ammunition");
		}
		float quantity = 0.0f;
		if(!parse_float(source.cells[7], quantity) || quantity < 0.0f)
			add_row_error(err, "military_stockpiles.csv", source.line, "quantity must be finite and nonnegative");
		item.quantity = quantity;
		if(item.id != 0) stockpile_ids.emplace(source.cells[0], item.id);
		if(item.id != 0 && !military::land_forces::create_stockpile(state, item))
			add_row_error(err, "military_stockpiles.csv", source.line, "duplicate or invalid stockpile record");
	}
	state.world.for_each_regiment([&](dcon::regiment_id regiment) {
		if(!military::land_forces::formation_for_legacy_regiment(state, regiment))
			add_row_error(err, "military_formations.csv", 0,
				"legacy regiment index " + std::to_string(regiment.index()) + " has no canonical formation mapping");
	});
	auto validation = military::land_forces::validate_canonical_land_forces(state);
	for(auto const& message : validation.errors)
		add_row_error(err, "military_formations.csv", 0, message);
	return err.accumulated_errors.size() == before;
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

bool load_canonical_technology(sys::state& state, simple_fs::directory const& common,
	parsers::scenario_building_context& context, parsers::error_handler& err,
	std::vector<firm_record> const& firms, std::vector<asset_record> const& assets) {
	auto canonical = simple_fs::open_directory(common, NATIVE("canonical_runtime"));
	auto capability_file = simple_fs::open_file(canonical, NATIVE("capabilities.csv"));
	if(!capability_file) return true; // Legacy scenarios remain in the isolated compatibility mode.

	static constexpr std::array<std::string_view, 5> capabilities_header = {
		"capability_id", "domain", "research_effort", "codified", "transferable"
	};
	static constexpr std::array<std::string_view, 2> prerequisites_header = { "capability_id", "prerequisite_id" };
	static constexpr std::array<std::string_view, 2> processes_header = { "capability_id", "factory_type" };
	static constexpr std::array<std::string_view, 2> research_sites_header = { "site_id", "province_id" };
	static constexpr std::array<std::string_view, 5> research_organizations_header = {
		"research_organization_id", "firm_id", "site_id", "role", "effectiveness"
	};
	static constexpr std::array<std::string_view, 6> research_funding_header = {
		"funding_id", "source_firm_id", "research_organization_id", "settlement", "amount", "date"
	};
	static constexpr std::array<std::string_view, 3> holders_header = { "firm_id", "capability_id", "maturity" };
	static constexpr std::array<std::string_view, 7> programs_header = {
		"program_id", "research_organization_id", "capability_id", "status", "start_date", "opening_effort", "cost_per_effort"
	};
	static constexpr std::array<std::string_view, 4> assignments_header = {
		"program_id", "person", "allocation_fraction", "productivity_input"
	};
	static constexpr std::array<std::string_view, 3> adoptions_header = { "firm_id", "capability_id", "date" };
	static constexpr std::array<std::string_view, 6> transfers_header = {
		"transfer_id", "source_firm_id", "destination_firm_id", "capability_id", "date", "authorized_relation"
	};

	auto before = err.accumulated_errors.size();
	table capability_table;
	{
		auto content = simple_fs::view_contents(*capability_file);
		std::string_view input(content.data, content.file_size);
		if(input.size() >= 3 && uint8_t(input[0]) == 0xEF && uint8_t(input[1]) == 0xBB && uint8_t(input[2]) == 0xBF)
			input.remove_prefix(3);
		err.file_name = "common/canonical_runtime/capabilities.csv";
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
				if(cells.size() != capabilities_header.size()) {
					err.accumulated_errors += err.file_name + ":1: header has wrong column count\n";
					break;
				}
				for(size_t i = 0; i < cells.size(); ++i) if(cells[i] != capabilities_header[i]) {
					err.accumulated_errors += err.file_name + ":1: column " + std::to_string(i + 1)
						+ " must be '" + std::string(capabilities_header[i]) + "'\n";
					break;
				}
				continue;
			}
			if(cells.size() != capabilities_header.size()) {
				err.accumulated_errors += err.file_name + ":" + std::to_string(line_number) + ": wrong column count\n";
				continue;
			}
			capability_table.rows.push_back({std::move(cells), line_number});
		}
		if(!header_seen) err.accumulated_errors += err.file_name + ": missing required header\n";
	}
	auto read = [&](char const* name, auto const& header) -> std::optional<table> {
		return read_table(canonical, name, header, err);
	};
	auto prerequisites = read("capability_prerequisites.csv", prerequisites_header);
	auto processes = read("capability_processes.csv", processes_header);
	auto research_sites = read("research_sites.csv", research_sites_header);
	auto organizations = read("research_organizations.csv", research_organizations_header);
	auto research_funding = read("research_funding.csv", research_funding_header);
	auto holders = read("capability_holders.csv", holders_header);
	auto programs = read("research_programs.csv", programs_header);
	auto assignments = read("research_assignments.csv", assignments_header);
	auto adoptions = read("capability_adoptions.csv", adoptions_header);
	auto transfers = read("capability_transfers.csv", transfers_header);
	if(!prerequisites || !processes || !research_sites || !organizations || !research_funding || !holders || !programs
		|| !assignments || !adoptions || !transfers || err.accumulated_errors.size() != before) return false;

	using namespace technology::kernel;
	snapshot value;
	value.canonical_runtime_active = 1;
	std::unordered_map<std::string, technology::kernel::stable_id> capability_ids;
	std::unordered_map<technology::kernel::stable_id, std::string> capability_hashes;
	for(auto const& source : capability_table.rows) {
		auto const& id = source.cells[0];
		auto const& domain = source.cells[1];
		float effort = 0.0f;
		uint32_t codified = 0, transferable = 0;
		if(!valid_key(id) || !valid_key(domain)) add_row_error(err, "capabilities.csv", source.line, "capability_id and domain must be nonempty stable keys");
		if(!parse_float(source.cells[2], effort) || effort <= 0.0f) add_row_error(err, "capabilities.csv", source.line, "research_effort must be finite and positive");
		if(!parse_integer(source.cells[3], codified) || codified > 1) add_row_error(err, "capabilities.csv", source.line, "codified must be 0 or 1");
		if(!parse_integer(source.cells[4], transferable) || transferable > 1) add_row_error(err, "capabilities.csv", source.line, "transferable must be 0 or 1");
		auto stable = stable_id_for("capability", id);
		if(capability_ids.contains(id)) add_row_error(err, "capabilities.csv", source.line, "duplicate capability_id '" + id + "'");
		if(auto [it, inserted] = capability_hashes.emplace(stable, id); !inserted && it->second != id)
			add_row_error(err, "capabilities.csv", source.line, "capability stable ID hash collision");
		capability_ids.emplace(id, stable);
		value.capabilities.push_back({stable, stable_id_for("technology-domain", domain), effort,
			uint8_t(codified), uint8_t(transferable)});
	}
	auto lookup_capability = [&](std::string const& id, char const* file, uint32_t line) -> technology::kernel::stable_id {
		auto it = capability_ids.find(id);
		if(it == capability_ids.end()) {
			add_row_error(err, file, line, "capability_id '" + id + "' is missing from capabilities.csv");
			return 0;
		}
		return it->second;
	};
	for(auto const& source : prerequisites->rows) {
		auto capability = lookup_capability(source.cells[0], "capability_prerequisites.csv", source.line);
		auto prerequisite = lookup_capability(source.cells[1], "capability_prerequisites.csv", source.line);
		if(capability && prerequisite) value.prerequisites.push_back({capability, prerequisite});
	}
	std::unordered_map<technology::kernel::stable_id, std::string> process_ids;
	for(auto const& source : processes->rows) {
		auto capability = lookup_capability(source.cells[0], "capability_processes.csv", source.line);
		auto process_id = stable_id_for("factory-process", source.cells[1]);
		auto [process_it, process_inserted] = process_ids.emplace(process_id, source.cells[1]);
		if(!process_inserted && process_it->second != source.cells[1])
			add_row_error(err, "capability_processes.csv", source.line, "factory process stable ID hash collision");
		auto factory = context.map_of_factory_names.find(source.cells[1]);
		if(factory == context.map_of_factory_names.end())
			add_row_error(err, "capability_processes.csv", source.line, "factory_type '" + source.cells[1] + "' is missing");
		else if(capability) value.factory_processes.push_back({capability,
			process_id, factory->second});
	}

	std::unordered_map<std::string, firm_record const*> firm_by_id;
	for(auto const& firm : firms) firm_by_id.emplace(firm.id, &firm);
	std::unordered_map<std::string, dcon::site_id> site_by_id;
	for(auto const& asset : assets) {
		auto [it, inserted] = site_by_id.emplace(asset.site_key, asset.site);
		if(!inserted && it->second != asset.site)
			add_row_error(err, "research_organizations.csv", 0, "site_id resolves to multiple physical sites");
	}
	std::unordered_set<std::string> research_site_ids;
	for(auto const& source : research_sites->rows) {
		if(!valid_key(source.cells[0]) || !research_site_ids.insert(source.cells[0]).second) {
			add_row_error(err, "research_sites.csv", source.line, "site_id must be a valid unique stable key");
			continue;
		}
		uint32_t original_province = 0;
		if(!parse_integer(source.cells[1], original_province) || original_province == 0) {
			add_row_error(err, "research_sites.csv", source.line, "province_id must be a nonzero original scenario province ID");
			continue;
		}
		auto province = province_from_original_id(state, context, original_province);
		if(!province) {
			add_row_error(err, "research_sites.csv", source.line, "province_id does not resolve in this scenario");
			continue;
		}
		if(auto existing = site_by_id.find(source.cells[0]); existing != site_by_id.end()) {
			if(state.world.site_get_province_from_site_location(existing->second) != province)
				add_row_error(err, "research_sites.csv", source.line, "site_id conflicts with an asset site in another province");
			continue;
		}
		auto site = state.world.create_site();
		state.world.force_create_site_location(site, province);
		state.world.site_set_canonical_id(site, stable_id_for("site", source.cells[0]));
		site_by_id.emplace(source.cells[0], site);
	}
	std::unordered_map<std::string, technology::kernel::stable_id> research_ids;
	std::unordered_map<std::string, technology::kernel::stable_id> firm_stable_ids;
	for(auto const& [firm_id, firm] : firm_by_id)
		firm_stable_ids.emplace(firm_id, state.world.organization_get_canonical_id(firm->organization));
	for(auto const& source : organizations->rows) {
		auto const& id = source.cells[0];
		auto firm = firm_by_id.find(source.cells[1]);
		auto site = site_by_id.find(source.cells[2]);
		if(!valid_key(id) || research_ids.contains(id)) add_row_error(err, "research_organizations.csv", source.line, "research_organization_id must be valid and unique");
		if(firm == firm_by_id.end()) add_row_error(err, "research_organizations.csv", source.line, "firm_id '" + source.cells[1] + "' is missing");
		if(site == site_by_id.end()) add_row_error(err, "research_organizations.csv", source.line, "site_id '" + source.cells[2] + "' is missing");
		research_role role = research_role::other;
		if(source.cells[3] == "university") role = research_role::university;
		else if(source.cells[3] == "public_laboratory") role = research_role::public_laboratory;
		else if(source.cells[3] == "corporate_rd") role = research_role::corporate_rd;
		else if(source.cells[3] == "military_government") role = research_role::military_government;
		else if(source.cells[3] != "other") add_row_error(err, "research_organizations.csv", source.line, "unknown research role '" + source.cells[3] + "'");
		float effectiveness = 0.0f;
		if(!parse_float(source.cells[4], effectiveness) || effectiveness < 0.0f || effectiveness > 1.0f)
			add_row_error(err, "research_organizations.csv", source.line, "effectiveness must be between 0 and 1");
		auto research_id = stable_id_for("research-organization", id);
		research_ids.emplace(id, research_id);
		if(firm != firm_by_id.end()) {
			auto account = economy::accounts::find_account(state, firm->second->actor, firm->second->settlement);
			if(!account) add_row_error(err, "research_organizations.csv", source.line, "firm has no funding account");
			if(site != site_by_id.end() && account)
				value.organizations.push_back({research_id, firm->second->organization, site->second, account, role, effectiveness});
		}
	}
	for(auto const& source : holders->rows) {
		auto firm = firm_stable_ids.find(source.cells[0]);
		if(firm == firm_stable_ids.end()) add_row_error(err, "capability_holders.csv", source.line, "firm_id '" + source.cells[0] + "' is missing");
		auto capability = lookup_capability(source.cells[1], "capability_holders.csv", source.line);
		float maturity = 0.0f;
		if(!parse_float(source.cells[2], maturity) || maturity < 0.0f || maturity > 1.0f)
			add_row_error(err, "capability_holders.csv", source.line, "maturity must be between 0 and 1");
		if(firm != firm_stable_ids.end() && capability) value.holders.push_back({firm->second, capability, maturity});
	}
	std::unordered_map<std::string, technology::kernel::stable_id> program_ids;
	for(auto const& source : programs->rows) {
		auto const& id = source.cells[0];
		auto research_org = research_ids.find(source.cells[1]);
		auto capability = lookup_capability(source.cells[2], "research_programs.csv", source.line);
		if(!valid_key(id) || program_ids.contains(id)) add_row_error(err, "research_programs.csv", source.line, "program_id must be valid and unique");
		if(research_org == research_ids.end()) add_row_error(err, "research_programs.csv", source.line, "research_organization_id '" + source.cells[1] + "' is missing");
		program_status status = program_status::planned;
		if(source.cells[3] == "active") status = program_status::active;
		else if(source.cells[3] == "paused") status = program_status::paused;
		else if(source.cells[3] == "completed") status = program_status::completed;
		else if(source.cells[3] == "cancelled") status = program_status::cancelled;
		else if(source.cells[3] != "planned") add_row_error(err, "research_programs.csv", source.line, "unknown program status '" + source.cells[3] + "'");
		auto start = parse_iso_date(source.cells[4]);
		if(!start) add_row_error(err, "research_programs.csv", source.line, "start_date must use YYYY-MM-DD");
		float opening_effort = 0.0f, cost_per_effort = 0.0f;
		if(!parse_float(source.cells[5], opening_effort) || opening_effort < 0.0f)
			add_row_error(err, "research_programs.csv", source.line, "opening_effort must be finite and nonnegative");
		if(!parse_float(source.cells[6], cost_per_effort) || cost_per_effort <= 0.0f)
			add_row_error(err, "research_programs.csv", source.line, "cost_per_effort must be finite and positive");
		float required_effort = 0.0f;
		if(capability) for(auto const& definition : value.capabilities)
			if(definition.id == capability) required_effort = definition.research_effort;
		if(opening_effort > required_effort) add_row_error(err, "research_programs.csv", source.line, "opening_effort cannot exceed capability research_effort");
		auto program_id = stable_id_for("research-program", id);
		program_ids.emplace(id, program_id);
		if(research_org != research_ids.end() && capability && start)
			value.programs.push_back({program_id, research_org->second, capability, status,
				sys::date(*start, state.start_date), opening_effort, required_effort, cost_per_effort});
	}
	for(auto const& source : assignments->rows) {
		auto program = program_ids.find(source.cells[0]);
		if(program == program_ids.end()) add_row_error(err, "research_assignments.csv", source.line, "program_id '" + source.cells[0] + "' is missing");
		auto separator = source.cells[1].find(':');
		persons::person_key person{};
		bool valid_person = separator != std::string::npos
			&& source.cells[1].find(':', separator + 1) == std::string::npos
			&& parse_integer(std::string_view(source.cells[1]).substr(0, separator), person.source_population_cell)
			&& parse_integer(std::string_view(source.cells[1]).substr(separator + 1), person.ordinal)
			&& persons::exists(state, person) && persons::alive(state, person);
		if(!valid_person) add_row_error(err, "research_assignments.csv", source.line, "person must reference an existing living exact person as source_population_cell:ordinal");
		float allocation = 0.0f, productivity = 0.0f;
		if(!parse_float(source.cells[2], allocation) || allocation <= 0.0f || allocation > 1.0f)
			add_row_error(err, "research_assignments.csv", source.line, "allocation_fraction must be greater than 0 and at most 1");
		if(!parse_float(source.cells[3], productivity) || productivity < 0.0f || productivity > 1.0f)
			add_row_error(err, "research_assignments.csv", source.line, "productivity_input must be 0 (derive) or between 0 and 1");
		if(program != program_ids.end() && valid_person)
			value.assignments.push_back({program->second, person, allocation, productivity});
	}

	if(err.accumulated_errors.size() != before) return false;
	if(!install_scenario_state(state, value)) {
		add_row_error(err, "capabilities.csv", 0, "canonical technology state failed invariants; check organization, site, staff, holder, prerequisite, and program references");
		return false;
	}
	std::unordered_set<std::string> funding_ids;
	for(auto const& source : research_funding->rows) {
		auto firm = firm_by_id.find(source.cells[1]);
		auto research_org = research_ids.find(source.cells[2]);
		if(!valid_key(source.cells[0]) || !funding_ids.insert(source.cells[0]).second)
			add_row_error(err, "research_funding.csv", source.line, "funding_id must be a valid unique stable key");
		if(firm == firm_by_id.end()) add_row_error(err, "research_funding.csv", source.line, "source_firm_id is missing");
		if(research_org == research_ids.end()) add_row_error(err, "research_funding.csv", source.line, "research_organization_id is missing");
		auto settlement = context.map_of_commodity_names.find(source.cells[3]);
		if(settlement == context.map_of_commodity_names.end()) add_row_error(err, "research_funding.csv", source.line, "settlement commodity is missing");
		float amount = 0.0f;
		if(!parse_float(source.cells[4], amount) || amount <= 0.0f) add_row_error(err, "research_funding.csv", source.line, "amount must be finite and positive");
		auto date = parse_iso_date(source.cells[5]);
		if(!date) add_row_error(err, "research_funding.csv", source.line, "date must use YYYY-MM-DD");
		if(firm == firm_by_id.end() || research_org == research_ids.end()
			|| settlement == context.map_of_commodity_names.end() || !date || amount <= 0.0f) continue;
		auto target = std::find_if(value.organizations.begin(), value.organizations.end(), [&](auto const& record) {
			return record.id == research_org->second;
		});
		if(target == value.organizations.end()
			|| economy::accounts::settlement_of(state, target->funding_account) != settlement->second) {
			add_row_error(err, "research_funding.csv", source.line, "funding settlement must match the research organization's account");
			continue;
		}
		auto funding_date = sys::date(*date, state.start_date);
		if(state.current_date && funding_date > state.current_date) {
			add_row_error(err, "research_funding.csv", source.line, "opening funding date cannot be after scenario start");
			continue;
		}
		auto source_account = economy::accounts::find_account(state, firm->second->actor, settlement->second);
		auto transfer_kind = firm->second->kind == ownership::actor_kind::state_entity
			? economy::relations::transaction_kind::public_spending : economy::relations::transaction_kind::transfer;
		if(!source_account || !economy::accounts::transfer(state, source_account, target->funding_account,
			amount, transfer_kind, funding_date))
			add_row_error(err, "research_funding.csv", source.line, "funding transfer failed because the source account lacks available funds");
	}
	for(auto const& source : transfers->rows) {
		auto source_firm = firm_stable_ids.find(source.cells[1]);
		auto destination_firm = firm_stable_ids.find(source.cells[2]);
		auto capability = lookup_capability(source.cells[3], "capability_transfers.csv", source.line);
		auto date = parse_iso_date(source.cells[4]);
		if(source_firm == firm_stable_ids.end()) add_row_error(err, "capability_transfers.csv", source.line, "source firm is missing");
		if(destination_firm == firm_stable_ids.end()) add_row_error(err, "capability_transfers.csv", source.line, "destination firm is missing");
		if(!valid_key(source.cells[0]) || !valid_key(source.cells[5])) add_row_error(err, "capability_transfers.csv", source.line, "transfer_id and authorized_relation must be stable keys");
		if(!date) add_row_error(err, "capability_transfers.csv", source.line, "date must use YYYY-MM-DD");
		if(date && state.current_date && sys::date(*date, state.start_date) > state.current_date)
			add_row_error(err, "capability_transfers.csv", source.line, "transfer date cannot be after scenario start");
		if(source_firm != firm_stable_ids.end() && destination_firm != firm_stable_ids.end() && capability && date) {
			auto ok = transfer_capability(state, source_firm->second, destination_firm->second, capability,
				sys::date(*date, state.start_date), stable_id_for("technology-authorization", source.cells[5]),
				stable_id_for("capability-transfer", source.cells[0]));
			if(!ok) add_row_error(err, "capability_transfers.csv", source.line, "transfer is unauthorized, duplicated, nontransferable, or lacks a source holder");
		}
	}
	for(auto const& source : adoptions->rows) {
		auto firm = firm_stable_ids.find(source.cells[0]);
		auto capability = lookup_capability(source.cells[1], "capability_adoptions.csv", source.line);
		auto date = parse_iso_date(source.cells[2]);
		if(firm == firm_stable_ids.end()) add_row_error(err, "capability_adoptions.csv", source.line, "firm_id '" + source.cells[0] + "' is missing");
		if(!date) add_row_error(err, "capability_adoptions.csv", source.line, "date must use YYYY-MM-DD");
		if(date && state.current_date && sys::date(*date, state.start_date) > state.current_date)
			add_row_error(err, "capability_adoptions.csv", source.line, "adoption date cannot be after scenario start");
		if(firm != firm_stable_ids.end() && capability && date
			&& !adopt_capability(state, firm->second, capability, sys::date(*date, state.start_date)))
			add_row_error(err, "capability_adoptions.csv", source.line, "adoption requires local holder knowledge and resolved prerequisites");
	}
	std::vector<std::string> validation_errors;
	if(!validate_canonical_technology_state(state, validation_errors))
		for(auto const& message : validation_errors) add_row_error(err, "capabilities.csv", 0, message);
	return err.accumulated_errors.size() == before;
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
	table bank_table;
	table bank_deposit_table;
	if(!read_and_parse_tables(common, err, firm_table, owner_table, asset_table,
		ownership_table, loan_table, bank_table, bank_deposit_table)) {
		err.fatal = true;
		return false;
	}
	std::vector<firm_record> firms;
	std::vector<owner_record> owners;
	std::vector<asset_record> assets;
	std::vector<stake_record> stakes;
	std::vector<loan_record> loans;
	std::vector<bank_record> banks;
	std::vector<bank_deposit_record> bank_deposits;
	if(!parse_tables(state, context, err, firm_table, owner_table, asset_table,
		ownership_table, loan_table, firms, owners, assets, stakes, loans)
		|| !parse_bank_tables(state, err, bank_table, bank_deposit_table,
			firms, owners, banks, bank_deposits)) {
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
		|| !load_banks(state, err, banks, bank_deposits, firms, owners_by_id, account_ids)
		|| !load_assets(state, context, err, assets, firms, firm_by_id, asset_ids, site_ids,
			assets_by_id, asset_row_by_id)
		|| !load_ownership(state, err, firms, owners, assets, stakes, owners_by_id,
			assets_by_id, stake_ids)
			|| !load_loans(state, err, firms, assets, loans, obligation_ids)
		|| !load_land_forces(state, context, common, err)) {
		err.fatal = true;
		return false;
	}
	if(!load_canonical_technology(state, common, context, err, firms, assets)) {
		err.fatal = true;
		return false;
	}
	economy::banking::update_bank_statuses(state, state.current_date);
	for(auto const& bank : banks) {
		auto firm = std::find_if(firms.begin(), firms.end(), [&](auto const& row) { return row.id == bank.id; });
		if(firm == firms.end() || !firm->organization) continue;
		auto balance_sheet = economy::banking::bank_balance_sheet(state, firm->organization, bank.settlement);
		if(std::abs(balance_sheet.net_worth - bank.opening_equity)
			> 1.0e-4f * std::max(1.0f, std::abs(bank.opening_equity)))
			add_row_error(err, "banks.csv", bank.line,
				"opening balance sheet does not balance to opening_equity for bank '" + bank.id
				+ "' (authored=" + std::to_string(bank.opening_equity)
				+ ", derived=" + std::to_string(balance_sheet.net_worth) + ")");
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
	std::vector<std::string> banking_errors;
	if(!economy::banking::validate_canonical_banking_state(state, banking_errors)) {
		for(auto const& message : banking_errors)
			err.accumulated_errors += "canonical banking invariant: " + message + "\n";
		err.fatal = true;
		return false;
	}
	if(err.accumulated_errors.size() != initial_errors) {
		err.fatal = true;
		return false;
	}
	if(!economy::physical::exact_person_goods::validate_canonical_household_economy(state)) {
		err.accumulated_errors += "canonical household economy invariant failed after scenario load\n";
		err.fatal = true;
		return false;
	}
	return true;
}

} // namespace actors::canonical_scenario
