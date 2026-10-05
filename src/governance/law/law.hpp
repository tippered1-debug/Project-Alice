#pragma once

#include "dcon_generated.hpp"
#include "date_interface.hpp"
#include "governance/governance.hpp"
#include "governance/policy.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace sys { class state; }

namespace governance::law {

// Every state rule is a legal instrument: a constitution, a statute, a
// regulation, or an administrative act. It has a jurisdiction, an issuing
// institution, an authorizing office (and enacting person), enactment,
// effective and repeal dates, a status, and policy rules. New policy areas add
// rule kinds; the enactment machinery does not change.
// Values are saved: new kinds are appended.
enum class legal_instrument_kind : uint8_t { statute = 0, regulation = 1, constitution = 2, administrative_action = 3 };
enum class legal_status : uint8_t { draft = 0, enacted = 1, repealed = 2 };
enum class policy_rule_kind : uint8_t {
	public_debt_ceiling = 0, public_debt_issuance_prohibited = 1,
	// parameter: 0 poor, 1 middle, 2 rich; amount: the rate on wage income.
	income_tax_rate = 2,
	// target: the institution; amount: its share of each disbursement.
	appropriation_share = 3,
	// amount: the central bank's annual inflation target.
	inflation_target = 4,
	// amount: share of the national treasury disbursed each month.
	disbursement_rate = 5,
	// A typed rule whose stable topic id and tagged value live on the policy rule.
	substantive_policy = 6
};
enum class legal_action_kind : uint8_t { enactment = 0, repeal = 1, assent = 2, founding = 3 };

struct public_debt_policy {
	bool issuance_allowed = true;
	std::optional<float> ceiling;
};

struct rule {
	policy_rule_kind kind = policy_rule_kind::public_debt_ceiling;
	dcon::commodity_id settlement{};
	float amount = 0.0f;
	uint8_t parameter = 0;
	dcon::institution_id target{};
	policy::topic_id topic = policy::topic_id::income_tax;
	policy::policy_value topic_value = 0.0f;
};

// The power an instrument of a kind requires: amend_constitution, legislate,
// regulate, or administer.
authority_kind required_authority(legal_instrument_kind);
jurisdiction jurisdiction_of(sys::state const&, dcon::legal_instrument_id);

dcon::legal_instrument_id create_draft_instrument(sys::state&, legal_instrument_kind,
	dcon::nation_id nation = {}, dcon::territorial_unit_id territorial_unit = {});
bool add_rule(sys::state&, dcon::legal_instrument_id, rule const&);
bool add_public_debt_ceiling_rule(sys::state&, dcon::legal_instrument_id, dcon::commodity_id, float);
bool add_public_debt_prohibition_rule(sys::state&, dcon::legal_instrument_id, dcon::commodity_id);
bool add_topic_rule(sys::state&, dcon::legal_instrument_id, policy::topic_id,
	policy::policy_value const&, dcon::institution_id target = {});

// Enactment. Where chambers hold the required power over the jurisdiction,
// every such chamber must have passed the instrument (a constitution by a two
// thirds majority) and the enacting person must hold an office in one of them;
// where an office holds `assent` over the jurisdiction, its holder must have
// assented. Otherwise the enacting person must exercise an office holding the
// required power. Anything else fails without change.
dcon::legal_action_id authorized_enact(sys::state&, dcon::person_id, dcon::legal_instrument_id,
	sys::date enacted_on, sys::date effective_from);
dcon::legal_action_id authorized_repeal(sys::state&, dcon::person_id, dcon::legal_instrument_id, sys::date);
dcon::legal_action_id authorized_assent(sys::state&, dcon::person_id, dcon::legal_instrument_id, sys::date);
bool instrument_is_effective(sys::state const&, dcon::legal_instrument_id, sys::date);

// The newest effective instrument (by effective date, then enactment) of the
// jurisdiction decides a rule's value.
std::optional<float> effective_amount(sys::state const&, jurisdiction, policy_rule_kind, sys::date,
	uint8_t parameter = 0, dcon::institution_id target = {}, dcon::commodity_id settlement = {});
std::optional<policy::policy_value> effective_topic(sys::state const&, jurisdiction, policy::topic_id, sys::date);
dcon::institution_id effective_topic_target(sys::state const&, jurisdiction, policy::topic_id, sys::date);
struct appropriation {
	dcon::institution_id institution{};
	float share = 0.0f;
};
std::vector<appropriation> appropriations(sys::state const&, jurisdiction, sys::date);
public_debt_policy public_debt_policy_for(sys::state const&, dcon::nation_id, dcon::commodity_id, sys::date);
std::vector<dcon::legal_instrument_id> effective_instruments(sys::state const&, jurisdiction, sys::date);

// The founding constitution: enacted at founding by no one, issued by the
// institution that embodies the constituent power. Constitutional grants cite it.
dcon::legal_instrument_id found_constitution(sys::state&, dcon::nation_id, dcon::institution_id issuer, sys::date);
dcon::legal_instrument_id constitution_of(sys::state const&, dcon::nation_id);

} // namespace governance::law
