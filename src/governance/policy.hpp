#pragma once

#include "dcon_generated.hpp"
#include "date_interface.hpp"

#include <cstdint>
#include <utility>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

namespace sys { class state; }

namespace governance::policy {

// Topic ids are save data. Append new ids; never renumber an existing topic.
// The registry describes the domain and the law hook, while platform values
// are sparse so a party may leave an issue unaddressed.
enum class topic_id : uint16_t {
	income_tax = 0,
	progressivity = 1,
	education_appropriation = 2,
	policing_appropriation = 3,
	public_works_appropriation = 4,
	local_government_appropriation = 5,
	minimum_wage = 6,
	labor_protection = 7,
	collective_bargaining = 8,
	unemployment_replacement = 9,
	unemployment_duration = 10,
	dividend_tax = 11,
	resource_royalty = 12,
	bank_reserve_requirement = 13
};

enum class value_kind : uint8_t { continuous = 0, ordinal = 1, categorical = 2, binary = 3, structured = 4 };
enum class implementation : uint8_t { statute = 0, delegated_regulation = 1, appropriation = 2 };
enum class jurisdiction_kind : uint8_t { national = 0, territorial = 1 };

struct category_value { uint16_t value = 0; friend bool operator==(category_value, category_value) = default; };
struct structured_value {
	float first = 0.0f;
	float second = 0.0f;
	friend bool operator==(structured_value const&, structured_value const&) = default;
};
using policy_value = std::variant<float, int32_t, category_value, bool, structured_value>;

struct topic_definition {
	topic_id id{};
	std::string_view name;
	value_kind kind = value_kind::continuous;
	implementation enacted_by = implementation::statute;
	jurisdiction_kind jurisdiction = jurisdiction_kind::national;
	float minimum = 0.0f;
	float maximum = 1.0f;
	uint16_t category_count = 0;
	policy_value default_value = 0.0f;
};

struct entry { topic_id topic{}; policy_value value{}; };
struct issue_salience { topic_id topic{}; float weight = 0.0f; };
using salience = std::vector<issue_salience>;
struct position {
	std::vector<entry> entries;
	bool empty() const noexcept { return entries.empty(); }
	size_t size() const noexcept { return entries.size(); }
};

std::vector<topic_definition> const& topics();
topic_definition const* definition(topic_id);
bool valid(topic_id, policy_value const&);
std::optional<policy_value> get(position const&, topic_id);
policy_value value_or(position const&, topic_id);
bool set(position&, topic_id, policy_value const&);
position clamp(position);
float distance(position const&, position const&);
float distance(position const&, position const&, salience const&);
position weighted_mean(std::vector<std::pair<position const*, float>> const&);
position move_toward(position const&, position const&, float fraction);

struct fiscal_rules {
	float tax_rates[3] = {};
	std::vector<std::pair<dcon::institution_id, float>> shares;
};

inline constexpr float disbursement_rate = 0.35f;
fiscal_rules rules_for(sys::state const&, dcon::nation_id, position const&);
bool current(sys::state const&, dcon::nation_id, sys::date, position&);
bool law_matches(sys::state const&, dcon::nation_id, position const&, sys::date);

} // namespace governance::policy
