#include "causal_order.hpp"

#include "system_state.hpp"

#include <limits>
#include <map>
#include <memory>

namespace economy {

struct causal_order_store {
	uint64_t next_sequence = 1;
	std::map<std::pair<uint8_t, uint64_t>, uint64_t> dcon_sequences;
};

} // namespace economy

namespace economy::causal_order {
namespace {
std::shared_ptr<causal_order_store> ensure(sys::state& state) {
	if(!state.causal_order) state.causal_order = std::make_shared<causal_order_store>();
	return state.causal_order;
}
}

uint64_t allocate(sys::state& state, event_kind) {
	auto store = ensure(state);
	if(store->next_sequence == 0 || store->next_sequence == std::numeric_limits<uint64_t>::max()) return 0;
	return store->next_sequence++;
}

uint64_t sequence_for_dcon(sys::state& state, event_kind kind, uint64_t stable_id) {
	if(stable_id == 0) return 0;
	auto store = ensure(state);
	auto key = std::make_pair(uint8_t(kind), stable_id);
	if(auto it = store->dcon_sequences.find(key); it != store->dcon_sequences.end()) return it->second;
	auto sequence = allocate(state, kind);
	if(sequence) store->dcon_sequences.emplace(key, sequence);
	return sequence;
}

void observe(sys::state& state, uint64_t sequence) {
	if(sequence == 0) return;
	auto store = ensure(state);
	if(sequence >= store->next_sequence) {
		if(sequence == std::numeric_limits<uint64_t>::max()) store->next_sequence = 0;
		else store->next_sequence = sequence + 1;
	}
}

bool before(causal_order_key const& left, causal_order_key const& right) noexcept {
	if(left.effective_date != right.effective_date) return left.effective_date < right.effective_date;
	if(left.sequence != right.sequence) return left.sequence < right.sequence;
	return false;
}

} // namespace economy::causal_order
