#pragma once

namespace sys { class state; }

namespace economy::industrial_dynamics {

// Runs firm restructuring, insolvency resolution, acquisitions and private
// greenfield entry for the autonomous economy.
void process(sys::state&);

} // namespace economy::industrial_dynamics
