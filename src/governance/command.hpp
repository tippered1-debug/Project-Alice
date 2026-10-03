#pragma once

#include "dcon_generated.hpp"
#include "date_interface.hpp"
#include "military/land_forces.hpp"
#include "persons/persons.hpp"

#include <span>

namespace sys { class state; }

namespace governance::command {

// A formation answers to its nation's military command. Orders are given by a
// person exercising an office that holds `command_forces` over the nation:
// the commander in chief under the constitution, or the chief of the general
// staff under the command's delegated power. The combat system itself is
// unchanged; these are the only lawful entry points for orders.
dcon::institution_id commanding_institution(sys::state const&, dcon::nation_id);
bool may_command(sys::state const&, dcon::person_id, dcon::nation_id, sys::date);
bool authorized_move(sys::state&, dcon::person_id, military::land_forces::stable_id formation, dcon::site_id destination);
uint64_t authorized_recruit(sys::state&, dcon::person_id, military::land_forces::stable_id formation,
	std::span<persons::person_key const> candidates, uint64_t requested, uint16_t training_days);
uint64_t authorized_demobilize(sys::state&, dcon::person_id, military::land_forces::stable_id formation);

} // namespace governance::command
