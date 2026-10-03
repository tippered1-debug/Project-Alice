#pragma once

#include "dcon_generated.hpp"
#include "date_interface.hpp"
#include "governance/governance.hpp"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sys { class state; }

namespace governance::constitution {

// A constitution is data: institution rows, office rows (with their rules),
// and authority rows. One founder builds every constitutional model from rows;
// a presidential, parliamentary, or monarchic state differs only in which
// offices exist, who appoints and removes them, and which institution or
// office holds which power. Scenarios author the same rows.
//
// institutions: key;kind;parent;scope;independent;service;staffing_per_capita;staff_occupation;wage_multiplier
//   scope: national, regional (one per region of the nation), or capital (the
//   capital's municipality).
// offices: key;institution;kind;appointer;confirmer;removal;remover;term_days;succession;successor;exclusive;seats
//   removal: appointer, remover, or irremovable; succession: none, acting, full.
//   An office without an appointer is filled by founding (and later by election).
// authorities: holder;kind;scope;delegated_from;source
//   holder and delegated_from: institution:<key> or office:<key>; scope:
//   national or territory; source: constitution, or empty.
// elections: body;system;districts;term_days
//   body: institution:<chamber> or office:<key>; system: proportional,
//   plurality, direct, legislative, presiding, or running_mate; districts:
//   national, regional, or own (the office's territory).
//   removal (offices): no_confidence lets the confirming chamber remove the holder.

struct institution_row {
	std::string key, kind, parent, scope, service;
	bool independent = false;
	float staffing_per_capita = 0.0f;
	uint8_t staff_occupation = 0;
	float wage_multiplier = 1.0f;
	uint32_t line = 0;
};
struct office_row {
	std::string key, institution, kind, appointer, confirmer, removal, remover, succession, successor;
	uint16_t term_days = 0;
	bool exclusive = false;
	uint32_t seats = 1;
	uint32_t line = 0;
};
struct authority_row {
	std::string holder, kind, scope, delegated_from, source;
	uint32_t line = 0;
};
struct election_row {
	std::string body, system, districts;
	uint16_t term_days = 0;
	uint32_t line = 0;
};
struct document {
	std::vector<institution_row> institutions;
	std::vector<office_row> offices;
	std::vector<authority_row> authorities;
	std::vector<election_row> elections;
};

// Parses rows; errors name the table and line.
bool parse_institutions(std::string_view text, document&, std::string& error);
bool parse_offices(std::string_view text, document&, std::string& error);
bool parse_authorities(std::string_view text, document&, std::string& error);
bool parse_elections(std::string_view text, document&, std::string& error);
// Checks the rows are a coherent constitution: unique keys, known kinds, an
// acyclic institution tree, existing references, chambers as confirmers,
// delegations from earlier rows of the same power, and territorial powers
// only for territorial institutions.
bool validate(document const&, std::string& error);

// Built-in constitutional models, assembled from shared rows.
// Executive models: parliamentary_republic, parliamentary_monarchy,
// presidential, semi_presidential, dual_monarchy, absolute_monarchy,
// authoritarian. Territorial models: unitary, federal.
bool model(std::string_view executive, std::string_view territorial, document&, std::string& error);
// The model of a nation the scenario does not constitute.
inline constexpr std::string_view default_executive_model = "parliamentary_republic";

struct founded {
	dcon::legal_instrument_id constitution{};
	std::unordered_map<std::string, std::vector<dcon::institution_id>> institutions;
	std::unordered_map<std::string, std::vector<dcon::office_id>> offices;
	dcon::institution_id institution(std::string const& key) const;
	dcon::office_id office(std::string const& key) const;
};

// Divides the nation's land into regional territorial units (one per state
// definition it owns land in) and, when a capital municipality exists, the
// capital's unit inside its region.
void synchronize_territories(sys::state&, dcon::nation_id);
// Builds the state from the document: institutions, offices and their rules,
// the founding constitution, and its grants. Fails without a partial state
// being valid: the caller treats failure as fatal.
bool found(sys::state&, dcon::nation_id, document const&, sys::date, founded&, std::string& error);
// Installs a living adult of the nation in every office no political process
// fills: offices with neither an appointer nor an electoral rule (a hereditary
// monarch, an heir). Elections, government formation and appointers fill the rest.
bool appoint_founders(sys::state&, dcon::nation_id, founded const&, sys::date, std::string& error);
// Founds every nation that has no constitution with the default model, unitary.
void bootstrap(sys::state&);
// Regions gained after founding get a regional government cloned from an
// existing one of the same nation: same offices, rules, and powers over the new
// territory. Their offices start vacant.
void synchronize(sys::state&);

} // namespace governance::constitution
