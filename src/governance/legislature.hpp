#pragma once

#include "dcon_generated.hpp"
#include "date_interface.hpp"
#include "governance/governance.hpp"

#include <vector>

namespace sys { class state; }

namespace governance::legislature {

// A legislature is an institution whose chambers are institutions of kind
// legislative_chamber. A chamber's seats are offices of kind legislator, held
// through ordinary tenures; who fills them (appointment, founding, or a later
// electoral layer) is the seat's office rule. A chamber decides by the votes of
// the persons holding its seats on the day the question is decided.
// carried_no_confidence: a motion of no confidence that removed its office's
// holder; it binds no later tenure.
enum class motion_kind : uint8_t { enact = 0, repeal = 1, confirm = 2, no_confidence = 3, carried_no_confidence = 4 };
inline constexpr float simple_majority = 0.5f;
// Amending the constitution needs more than two thirds of the filled seats.
inline constexpr float constitutional_majority = 2.0f / 3.0f;

bool is_chamber(sys::state const&, dcon::institution_id);
std::vector<dcon::institution_id> chambers_of(sys::state const&, dcon::institution_id legislature);
std::vector<dcon::office_id> seats_of(sys::state const&, dcon::institution_id chamber);
// The seat the person holds in the chamber on `date`.
dcon::office_id seat_of(sys::state const&, dcon::person_id, dcon::institution_id chamber, sys::date);
uint32_t filled_seats(sys::state const&, dcon::institution_id chamber, sys::date);

dcon::motion_id find_instrument_motion(sys::state const&, dcon::institution_id chamber, dcon::legal_instrument_id, motion_kind);
dcon::motion_id find_confirmation_motion(sys::state const&, dcon::institution_id chamber, dcon::office_id, dcon::person_id candidate);
// A seat holder votes; a later vote by the same person replaces the earlier.
dcon::vote_id vote_on_instrument(sys::state&, dcon::person_id, dcon::institution_id chamber, dcon::legal_instrument_id,
	motion_kind, bool in_favor, sys::date);
dcon::vote_id vote_on_confirmation(sys::state&, dcon::person_id, dcon::institution_id chamber, dcon::office_id,
	dcon::person_id candidate, bool in_favor, sys::date);
// A seat holder of the office's confirming chamber moves or backs a motion of
// no confidence in the office's holder. Each tenure faces its own motion.
dcon::motion_id open_no_confidence(sys::state&, dcon::office_id, sys::date);
dcon::vote_id vote_on_no_confidence(sys::state&, dcon::person_id, dcon::office_id, bool in_favor, sys::date);
bool no_confidence_passed(sys::state const&, dcon::office_id, sys::date);
// Marks the office's current motion of no confidence as carried.
void carry_no_confidence(sys::state&, dcon::office_id);
// Votes in favor by today's seat holders exceed `threshold` of the filled seats.
bool passed(sys::state const&, dcon::motion_id, sys::date, float threshold);

// Chambers holding the authority over the jurisdiction on the date.
std::vector<dcon::institution_id> deciding_chambers(sys::state const&, authority_kind, jurisdiction, sys::date);
// Every deciding chamber passed the instrument. False when no chamber decides.
bool instrument_passed(sys::state const&, dcon::legal_instrument_id, motion_kind, authority_kind, jurisdiction,
	sys::date, float threshold);
// The office's confirming chamber passed the candidate.
bool confirmation_passed(sys::state const&, dcon::office_id, dcon::person_id candidate, sys::date);

} // namespace governance::legislature
