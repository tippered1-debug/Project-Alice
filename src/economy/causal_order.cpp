#include "causal_order.hpp"

#include "system_state.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <memory>
#include <set>

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

snapshot export_snapshot(sys::state const& state) {
	snapshot result;
	auto store = ensure(const_cast<sys::state&>(state));
	result.next_sequence = store->next_sequence;
	result.dcon_sequences.reserve(store->dcon_sequences.size());
	for(auto const& [key, sequence] : store->dcon_sequences)
		result.dcon_sequences.push_back({event_kind(key.first), key.second, sequence});
	std::sort(result.dcon_sequences.begin(), result.dcon_sequences.end(), [](auto const& left, auto const& right) {
		if(left.kind != right.kind) return uint8_t(left.kind) < uint8_t(right.kind);
		return left.stable_id < right.stable_id;
	});
	return result;
}

bool import_snapshot(sys::state& state, snapshot const& value) {
	if(value.version != 1) return false;
	auto candidate = std::make_shared<causal_order_store>();
	candidate->next_sequence = value.next_sequence;
	std::set<uint64_t> sequences;
	uint64_t greatest_sequence = 0;
	for(auto const& record : value.dcon_sequences) {
		if(uint8_t(record.kind) > uint8_t(event_kind::employment_contract)
			|| record.stable_id == 0 || record.sequence == 0
			|| !candidate->dcon_sequences.emplace(
				std::make_pair(uint8_t(record.kind), record.stable_id), record.sequence).second
			|| !sequences.insert(record.sequence).second) return false;
		greatest_sequence = std::max(greatest_sequence, record.sequence);
	}
	if(value.next_sequence != 0 && value.next_sequence <= greatest_sequence) return false;
	if(value.next_sequence == 0 && !value.dcon_sequences.empty()
		&& greatest_sequence != std::numeric_limits<uint64_t>::max()) return false;
	state.causal_order = std::move(candidate);
	return true;
}

void clear_store(sys::state& state) {
	state.causal_order.reset();
}

} // namespace economy::causal_order
