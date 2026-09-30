#pragma once

#include "technology/technology_kernel.hpp"

namespace sys { class state; }

namespace compat::technology {

// Legacy technology and invention state is a compatibility input only while
// canonical research data has not activated. This boundary is deliberately
// one-way: canonical capabilities never get reconstructed from nation flags.
inline bool legacy_national_technology_causality_enabled(sys::state const& state) {
	return !::technology::kernel::canonical_runtime_active(state);
}

} // namespace compat::technology
