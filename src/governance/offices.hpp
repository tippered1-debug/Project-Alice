#pragma once

#include "dcon_generated.hpp"
#include "date_interface.hpp"
#include "governance/governance.hpp"

#include <vector>

namespace sys { class state; }

namespace governance::offices {

// An office's constitutional rules. The appointer is the office whose holder
// may fill it, using that office's `appoint` authority over the office's
// jurisdiction; an office without an appointer is filled only by founding (and,
// in a later layer, by election). A confirming chamber must pass the candidate
// first. The term ends a tenure automatically. On a vacancy the successor
// office's holder acts in it, or takes it over in full.
enum class succession_mode : uint8_t { none = 0, acting = 1, full = 2 };
enum class removal_rule : uint8_t { by_appointer = 0, by_remover = 1, irremovable = 2 };

struct office_rules {
	dcon::office_id appointer{};
	dcon::institution_id confirmer{};
	removal_rule removal = removal_rule::by_appointer;
	dcon::office_id remover{};
	uint16_t term_days = 0;
	succession_mode succession = succession_mode::none;
	dcon::office_id successor{};
	// A holder of an exclusive office holds no other office, and nobody
	// holding an exclusive office takes another.
	bool exclusive = false;
};

// Validates and stores the rules: the related offices and the confirming
// chamber must belong to the same nation, and an office cannot appoint,
// remove, or succeed itself.
bool set_rules(sys::state&, dcon::office_id, office_rules const&);
office_rules rules_of(sys::state const&, dcon::office_id);
jurisdiction jurisdiction_of(sys::state const&, dcon::office_id);

// The tenure through which the office is exercised today: its holder, or an
// acting holder during a vacancy.
dcon::office_tenure_id current_tenure(sys::state const&, dcon::office_id);
dcon::person_id holder(sys::state const&, dcon::office_id);
bool acting(sys::state const&, dcon::office_tenure_id);
bool vacant(sys::state const&, dcon::office_id);
// The person's tenure in the office, if they exercise it on `date`.
dcon::office_tenure_id tenure_of(sys::state const&, dcon::person_id, dcon::office_id, sys::date);
// An office the person exercises on `date` that holds the authority over the
// jurisdiction, and the tenure through which they exercise it.
struct exercise {
	dcon::office_id office{};
	dcon::office_tenure_id tenure{};
	explicit operator bool() const noexcept { return bool(office); }
};
exercise exercising(sys::state const&, dcon::person_id, authority_kind, jurisdiction, sys::date);

// Founding installs a holder without an appointer: the constitution's first
// holders and scenario-authored holders. Exclusivity still applies.
dcon::office_tenure_id install(sys::state&, dcon::person_id, dcon::office_id, sys::date);
// Rule-checked appointment by the holder of the appointing office.
dcon::institutional_action_id appoint(sys::state&, dcon::person_id initiator, dcon::person_id candidate,
	dcon::office_id, sys::date);
// Rule-checked dismissal by the holder of the removing office.
dcon::institutional_action_id dismiss(sys::state&, dcon::person_id initiator, dcon::office_id, sys::date);
bool resign(sys::state&, dcon::person_id, dcon::office_id, sys::date);
// Ends the office's current tenure and applies its succession rule.
void vacate(sys::state&, dcon::office_id, sys::date);
// Ends every tenure whose term is over by `date`.
void expire_terms(sys::state&, sys::date);

} // namespace governance::offices
