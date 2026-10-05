#pragma once

namespace sys { class state; }

namespace economy::social_entitlements {

// Materializes rights from effective legislation and pays the accrued claim
// from the named public institution's existing treasury balance.
void process(sys::state&);

} // namespace economy::social_entitlements
