#pragma once

#include "date_interface.hpp"

#include <cstdint>
#include <vector>

namespace sys { class state; }

namespace economy::causal_order {

enum class event_kind : uint8_t {
	goods_bid,
	job_application,
	freight_request,
	employment_contract
};

struct causal_order_key {
	sys::date effective_date{};
	uint64_t sequence = 0;
};

struct dcon_sequence_record {
	event_kind kind = event_kind::goods_bid;
	uint64_t stable_id = 0;
	uint64_t sequence = 0;
};

struct snapshot {
	uint32_t version = 1;
	uint64_t next_sequence = 1;
	std::vector<dcon_sequence_record> dcon_sequences;
};

// Sequences are allocated only for active economic events. DCON objects use a
// small auxiliary registry; sparse exact records persist their sequence in
// their own snapshots.
uint64_t allocate(sys::state&, event_kind);
uint64_t sequence_for_dcon(sys::state&, event_kind, uint64_t stable_id);
void observe(sys::state&, uint64_t sequence);
bool before(causal_order_key const&, causal_order_key const&) noexcept;
snapshot export_snapshot(sys::state const&);
bool import_snapshot(sys::state&, snapshot const&);
void clear_store(sys::state&);

} // namespace economy::causal_order
