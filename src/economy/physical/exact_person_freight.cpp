#include "exact_person_freight.hpp"

#include "accounts/accounts.hpp"
#include "commodity_logistics.hpp"
#include "economy/exact_person_economy.hpp"
#include "economy/relations/relations.hpp"
#include "exact_person_goods.hpp"
#include "freight_market.hpp"
#include "shipments.hpp"
#include "system_state.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace economy::physical {

struct exact_person_freight_store {
	std::vector<exact_person_freight::request_record> requests;
	std::vector<exact_person_freight::contract_record> contracts;
	std::vector<exact_person_freight::shipment_owner_record> shipment_owners;
	uint64_t next_request_id = 1;
	uint64_t next_contract_id = 1;
};

} // namespace economy::physical

namespace economy::physical::exact_person_freight {
namespace {

constexpr uint32_t snapshot_version = 1;
constexpr float epsilon = 1.0e-5f;

std::shared_ptr<exact_person_freight_store> ensure_store(sys::state& state) {
	if(!state.exact_person_freight)
		state.exact_person_freight = std::make_shared<exact_person_freight_store>();
	return state.exact_person_freight;
}

std::shared_ptr<exact_person_freight_store> ensure_store(sys::state const& state) {
	return ensure_store(const_cast<sys::state&>(state));
}

bool positive_finite(float value) { return std::isfinite(value) && value > 0.0f; }
bool nonnegative_finite(float value) { return std::isfinite(value) && value >= 0.0f; }
bool approximately_equal(float left, float right) {
	return std::isfinite(left) && std::isfinite(right)
		&& std::fabs(left - right) <= epsilon * std::max(1.0f, std::max(std::fabs(left), std::fabs(right)));
}

uint64_t next_id(uint64_t& value) {
	if(value == 0 || value == std::numeric_limits<uint64_t>::max()) return 0;
	return value++;
}

request_record* request_for(sys::state& state, uint64_t id) {
	for(auto& record : ensure_store(state)->requests) if(record.id == id) return &record;
	return nullptr;
}

contract_record* contract_for(sys::state& state, uint64_t id) {
	for(auto& record : ensure_store(state)->contracts) if(record.id == id) return &record;
	return nullptr;
}

shipment_owner_record* shipment_owner_for(sys::state& state, dcon::shipment_id shipment) {
	for(auto& record : ensure_store(state)->shipment_owners)
		if(record.shipment == shipment) return &record;
	return nullptr;
}

float exact_free_cash(sys::state const& state, exact_person_economy::account_ref account) {
	return std::max(0.0f, exact_person_economy::balance(state, account)
		- exact_person_goods::reserved_bid_amount(state, account.exact_account_id));
}

dcon::market_id market_for_site(sys::state const& state, dcon::site_id site) {
	if(!site || !state.world.site_is_valid(site)) return {};
	auto province = state.world.site_get_province_from_site_location(site);
	if(!province || !state.world.province_is_valid(province)) return {};
	auto zone = state.world.province_get_state_membership(province);
	return zone ? state.world.state_instance_get_market_from_local_market(zone) : dcon::market_id{};
}

struct candidate {
	freight_market::offer_quote quote;
	exact_person_economy::account_ref payer{};
	float free_cash = 0.0f;
};

} // namespace

float reserved_source_quantity(sys::state const& state, person_key owner,
	dcon::site_id source, dcon::commodity_id commodity) {
	float result = 0.0f;
	for(auto const& record : ensure_store(state)->requests)
		if(record.requester == owner && record.source == source && record.commodity == commodity
			&& record.status == request_status::pending) result += record.quantity;
	return std::max(0.0f, result);
}

float incoming_quantity(sys::state const& state, person_key owner, dcon::site_id destination,
	dcon::commodity_id commodity) {
	float result = 0.0f;
	for(auto const& request : ensure_store(state)->requests) {
		if(request.requester != owner || request.destination != destination || request.commodity != commodity) continue;
		if(request.status == request_status::pending) result += request.quantity;
	}
	for(auto const& contract : ensure_store(state)->contracts) {
		if(contract.requester != owner || contract.destination != destination || contract.commodity != commodity
			|| contract.status != contract_status::accepted) continue;
		if(contract.shipment && state.world.shipment_is_valid(contract.shipment))
			result += std::max(0.0f, state.world.shipment_get_remaining_quantity(contract.shipment));
	}
	return std::isfinite(result) ? result : 0.0f;
}

uint64_t create_request(sys::state& state, person_key requester, dcon::site_id source,
	dcon::site_id destination, dcon::commodity_id commodity, float quantity, uint64_t originating_fill_id) {
	if(!persons::exact_population::exists(state, requester) || !persons::exact_population::alive(state, requester)
		|| !source || !destination || source == destination || !state.world.site_is_valid(source)
		|| !state.world.site_is_valid(destination) || !commodity || !state.world.commodity_is_valid(commodity)
		|| !positive_finite(quantity)
		|| exact_person_goods::stock_quantity(state, requester, source, commodity)
			- float(reserved_source_quantity(state, requester, source, commodity)) + epsilon < quantity) return 0;
	auto profile = logistics::profile_for(state, commodity);
	auto id = next_id(ensure_store(state)->next_request_id);
	if(!id) return 0;
	request_record record;
	record.id = id; record.requester = requester; record.source = source; record.destination = destination;
	record.commodity = commodity; record.quantity = quantity;
	record.cargo_units = logistics::cargo_units(profile, quantity);
	record.created_on = state.current_date; record.originating_fill_id = originating_fill_id;
	shipments::route_quote quote;
	if(shipments::quote_route(state, source, destination, quote)) {
		record.route_distance = quote.distance; record.primary_trade_route = quote.primary_trade_route;
		record.required_mode_mask = quote.required_mode_mask; record.route_leg_count = quote.route_leg_count;
	}
	ensure_store(state)->requests.push_back(record);
	return id;
}

std::optional<request_record> request(sys::state const& state, uint64_t request_id) {
	if(auto record = request_for(const_cast<sys::state&>(state), request_id)) return *record;
	return std::nullopt;
}

std::optional<contract_record> contract(sys::state const& state, uint64_t contract_id) {
	if(auto record = contract_for(const_cast<sys::state&>(state), contract_id)) return *record;
	return std::nullopt;
}

uint64_t request_count(sys::state const& state) { return ensure_store(state)->requests.size(); }
uint64_t contract_count(sys::state const& state) { return ensure_store(state)->contracts.size(); }
uint64_t shipment_owner_count(sys::state const& state) { return ensure_store(state)->shipment_owners.size(); }

uint64_t match_request(sys::state& state, uint64_t request_id) {
	auto request = request_for(state, request_id);
	if(!request || request->status != request_status::pending
		|| !persons::exact_population::exists(state, request->requester)) return 0;
	shipments::route_quote route;
	if(!shipments::quote_route(state, request->source, request->destination, route)) return 0;
	request->route_distance = route.distance; request->primary_trade_route = route.primary_trade_route;
	request->required_mode_mask = route.required_mode_mask; request->route_leg_count = route.route_leg_count;
	if(exact_person_goods::stock_quantity(state, request->requester, request->source, request->commodity)
		+ epsilon < request->quantity) return 0;
	if(!shipments::can_dispatch(state, request->source, request->destination, request->commodity, request->quantity)) return 0;
	auto origin_market = market_for_site(state, request->source);
	auto destination_market = market_for_site(state, request->destination);
	std::vector<candidate> candidates;
	state.world.for_each_freight_offer([&](auto offer) {
		freight_market::offer_quote quote;
		if(!freight_market::quote_offer(state, offer, origin_market, destination_market,
			request->required_mode_mask, request->cargo_units, request->route_distance, quote)) return;
		for(auto account : exact_person_economy::accounts_for_person(state, request->requester)) {
			if(exact_person_economy::settlement_of(state, account) != accounts::settlement_of(state, quote.carrier_account)) continue;
			auto free_cash = exact_free_cash(state, account);
			if(free_cash + epsilon >= quote.price) candidates.push_back({quote, account, free_cash});
		}
	});
	std::sort(candidates.begin(), candidates.end(), [](auto const& left, auto const& right) {
		if(left.quote.price != right.quote.price) return left.quote.price < right.quote.price;
		if(left.quote.offer.index() != right.quote.offer.index()) return left.quote.offer.index() < right.quote.offer.index();
		return left.payer.exact_account_id < right.payer.exact_account_id;
	});
	if(candidates.empty()) return 0;
	auto selected = candidates.front();
	if(!freight_market::reserve_capacity(state, selected.quote.offer, selected.quote.carrier, request->cargo_units)) return 0;
	auto payment = exact_person_economy::transfer_with_result(state, selected.payer,
		exact_person_economy::account_ref::from_dcon(selected.quote.carrier_account), selected.quote.price,
		relations::transaction_kind::freight, state.current_date);
	if(!payment.success) {
		freight_market::release_capacity(state, selected.quote.offer, selected.quote.carrier, request->cargo_units);
		return 0;
	}
	auto contract_id = next_id(ensure_store(state)->next_contract_id);
	if(!contract_id) {
		(void)exact_person_economy::transfer_with_result(state,
			exact_person_economy::account_ref::from_dcon(selected.quote.carrier_account), selected.payer,
			selected.quote.price, relations::transaction_kind::freight, state.current_date);
		freight_market::release_capacity(state, selected.quote.offer, selected.quote.carrier, request->cargo_units);
		return 0;
	}
	contract_record record;
	record.id = contract_id; record.request_id = request->id; record.requester = request->requester;
	record.offer = selected.quote.offer; record.carrier = selected.quote.carrier;
	record.carrier_account = selected.quote.carrier_account; record.exact_payer_account_id = selected.payer.exact_account_id;
	record.commodity = request->commodity; record.source = request->source; record.destination = request->destination;
	record.quantity = request->quantity; record.cargo_units = request->cargo_units;
	record.agreed_freight_price = selected.quote.price; record.exact_payment_transaction_id = payment.exact_transaction_id;
	record.created_on = state.current_date;
	ensure_store(state)->contracts.push_back(record);
	auto shipment = shipments::dispatch_exact(state, request->requester, request->source, request->destination,
		request->commodity, request->quantity, contract_id);
	if(!shipment) {
		ensure_store(state)->contracts.pop_back();
		(void)exact_person_economy::transfer_with_result(state,
			exact_person_economy::account_ref::from_dcon(selected.quote.carrier_account), selected.payer,
			selected.quote.price, relations::transaction_kind::freight, state.current_date);
		freight_market::release_capacity(state, selected.quote.offer, selected.quote.carrier, request->cargo_units);
		return 0;
	}
	ensure_store(state)->contracts.back().shipment = shipment;
	request->status = request_status::contracted;
	return contract_id;
}

void process_pending_requests(sys::state& state) {
	if(!state.exact_person_freight) return;
	std::vector<uint64_t> pending;
	for(auto const& request : ensure_store(state)->requests)
		if(request.status == request_status::pending) pending.push_back(request.id);
	std::sort(pending.begin(), pending.end());
	for(auto id : pending) (void)match_request(state, id);
}

void cancel_request(sys::state& state, uint64_t request_id) {
	if(auto record = request_for(state, request_id); record && record->status == request_status::pending)
		record->status = request_status::canceled;
}

bool register_shipment_owner(sys::state& state, dcon::shipment_id shipment, person_key owner, uint64_t contract_id) {
	if(!shipment || !state.world.shipment_is_valid(shipment) || !persons::exact_population::exists(state, owner)
		|| !contract_for(state, contract_id)) return false;
	if(shipment_owner_for(state, shipment)) return false;
	ensure_store(state)->shipment_owners.push_back({shipment, owner, contract_id});
	return true;
}

bool is_external_shipment(sys::state const& state, dcon::shipment_id shipment) {
	return shipment_owner_for(const_cast<sys::state&>(state), shipment) != nullptr;
}

bool complete_external_shipment(sys::state& state, dcon::shipment_id shipment, float surviving_quantity) {
	if(!shipment || !state.world.shipment_is_valid(shipment) || !nonnegative_finite(surviving_quantity)) return false;
	auto owner_record = shipment_owner_for(state, shipment);
	if(!owner_record) return false;
	auto contract = contract_for(state, owner_record->contract_id);
	if(!contract || contract->status != contract_status::accepted || contract->shipment != shipment
		|| contract->requester != owner_record->owner) return false;
	auto request = request_for(state, contract->request_id);
	if(!request || request->status != request_status::contracted
		|| request->requester != contract->requester || request->source != contract->source
		|| request->destination != contract->destination || request->commodity != contract->commodity) return false;
	if(!persons::exact_population::exists(state, owner_record->owner)
		|| !contract->source || !contract->destination || contract->source == contract->destination
		|| !state.world.site_is_valid(contract->source) || !state.world.site_is_valid(contract->destination)
		|| !contract->commodity || !state.world.commodity_is_valid(contract->commodity)
		|| !contract->offer || !state.world.freight_offer_is_valid(contract->offer)
		|| !contract->carrier || !state.world.carrier_is_valid(contract->carrier)) return false;
	auto shipment_origin_relation = state.world.shipment_get_shipment_origin(shipment);
	auto shipment_destination_relation = state.world.shipment_get_shipment_destination(shipment);
	if(!shipment_origin_relation || !shipment_destination_relation
		|| state.world.shipment_origin_get_site(shipment_origin_relation) != contract->source
		|| state.world.shipment_destination_get_site(shipment_destination_relation) != contract->destination
		|| state.world.shipment_get_commodity(shipment) != contract->commodity) return false;
	auto remaining = state.world.shipment_get_remaining_quantity(shipment);
	if(!nonnegative_finite(remaining) || surviving_quantity > remaining + epsilon) return false;
	auto mappings = std::count_if(ensure_store(state)->shipment_owners.begin(),
		ensure_store(state)->shipment_owners.end(), [&](auto const& record) { return record.shipment == shipment; });
	if(mappings != 1) return false;
	auto existing_stock = exact_person_goods::stock_quantity(state, owner_record->owner,
		contract->destination, contract->commodity);
	if(!nonnegative_finite(existing_stock)
		|| surviving_quantity > std::numeric_limits<float>::max() - existing_stock) return false;
	if(surviving_quantity > epsilon
		&& exact_person_goods::add_stock(state, owner_record->owner, contract->destination,
			contract->commodity, surviving_quantity) != surviving_quantity) return false;

	freight_market::release_capacity(state, contract->offer, contract->carrier, contract->cargo_units);
	contract->status = contract_status::fulfilled;
	contract->shipment = {};
	request->status = request_status::fulfilled;
	ensure_store(state)->shipment_owners.erase(std::remove_if(ensure_store(state)->shipment_owners.begin(),
		ensure_store(state)->shipment_owners.end(), [&](auto const& record) { return record.shipment == shipment; }),
		ensure_store(state)->shipment_owners.end());
	return true;
}

freight_snapshot export_snapshot(sys::state const& state) {
	freight_snapshot result; result.version = snapshot_version;
	result.requests = ensure_store(state)->requests; result.contracts = ensure_store(state)->contracts;
	result.shipment_owners = ensure_store(state)->shipment_owners;
	return result;
}

bool import_snapshot(sys::state& state, freight_snapshot const& snapshot) {
	if(snapshot.version != snapshot_version) return false;
	auto candidate = std::make_shared<exact_person_freight_store>();
	for(auto const& record : snapshot.requests) {
		if(!record.id || !persons::exact_population::exists(state, record.requester) || !record.source || !record.destination
			|| !state.world.site_is_valid(record.source) || !state.world.site_is_valid(record.destination)
			|| record.source == record.destination || !record.commodity || !state.world.commodity_is_valid(record.commodity)
			|| !positive_finite(record.quantity) || !nonnegative_finite(record.cargo_units)
			|| uint8_t(record.status) > uint8_t(request_status::canceled)
			|| std::any_of(candidate->requests.begin(), candidate->requests.end(),
				[&](auto const& existing) { return existing.id == record.id; })) return false;
		candidate->requests.push_back(record); candidate->next_request_id = std::max(candidate->next_request_id, record.id + 1);
	}
	for(auto const& record : snapshot.contracts) {
		auto request = std::find_if(candidate->requests.begin(), candidate->requests.end(),
			[&](auto const& candidate_request) { return candidate_request.id == record.request_id; });
		auto payer = exact_person_economy::account_ref::from_exact(record.exact_payer_account_id);
		auto carrier_account = exact_person_economy::account_ref::from_dcon(record.carrier_account);
		auto payment = exact_person_economy::transaction(state, record.exact_payment_transaction_id);
		if(!record.id || !record.request_id || !record.offer || !state.world.freight_offer_is_valid(record.offer)
			|| !record.carrier || !state.world.carrier_is_valid(record.carrier) || !record.carrier_account
			|| !state.world.monetary_account_is_valid(record.carrier_account)
			|| !record.exact_payer_account_id
			|| !exact_person_economy::account_exists(state, payer)
			|| !persons::exact_population::exists(state, record.requester)
			|| exact_person_economy::owner_of(state, payer) != record.requester
			|| !record.commodity || !state.world.commodity_is_valid(record.commodity) || !record.source || !record.destination
			|| !state.world.site_is_valid(record.source) || !state.world.site_is_valid(record.destination)
			|| !positive_finite(record.quantity) || !nonnegative_finite(record.cargo_units)
			|| !positive_finite(record.agreed_freight_price) || !record.exact_payment_transaction_id
			|| !payment || payment->kind != relations::transaction_kind::freight
			|| payment->source != payer || payment->destination != carrier_account
			|| payment->settlement != exact_person_economy::settlement_of(state, payer)
			|| payment->settlement != accounts::settlement_of(state, record.carrier_account)
			|| !approximately_equal(payment->amount, record.agreed_freight_price)
			|| state.world.freight_offer_get_carrier_from_freight_offer_carrier(record.offer) != record.carrier
			|| state.world.carrier_get_monetary_account_from_carrier_account(record.carrier) != record.carrier_account
			|| uint8_t(record.status) > uint8_t(contract_status::canceled)
			|| std::any_of(candidate->contracts.begin(), candidate->contracts.end(),
				[&](auto const& existing) { return existing.id == record.id || existing.request_id == record.request_id; })
			|| request == candidate->requests.end()
			|| request->requester != record.requester || request->source != record.source
			|| request->destination != record.destination || request->commodity != record.commodity
			|| (record.status == contract_status::accepted
				&& (request->status != request_status::contracted
					|| !record.shipment || !state.world.shipment_is_valid(record.shipment)))
			|| (record.status == contract_status::fulfilled && request->status != request_status::fulfilled)
			|| (record.status != contract_status::accepted && record.shipment)) return false;
		candidate->contracts.push_back(record); candidate->next_contract_id = std::max(candidate->next_contract_id, record.id + 1);
	}
	for(auto const& record : snapshot.shipment_owners) {
		auto contract = std::find_if(candidate->contracts.begin(), candidate->contracts.end(),
			[&](auto const& candidate_contract) { return candidate_contract.id == record.contract_id; });
		if(!record.shipment || !state.world.shipment_is_valid(record.shipment) || !persons::exact_population::exists(state, record.owner)
			|| contract == candidate->contracts.end() || contract->status != contract_status::accepted
			|| contract->requester != record.owner
			|| contract->shipment != record.shipment
			|| std::any_of(candidate->shipment_owners.begin(), candidate->shipment_owners.end(),
				[&](auto const& existing) { return existing.shipment == record.shipment; })) return false;
		candidate->shipment_owners.push_back(record);
	}
	for(auto const& request : candidate->requests) {
		auto accepted_contracts = std::count_if(candidate->contracts.begin(), candidate->contracts.end(),
			[&](auto const& contract) { return contract.request_id == request.id && contract.status == contract_status::accepted; });
		if(request.status == request_status::pending && accepted_contracts != 0) return false;
		if(request.status == request_status::contracted && accepted_contracts != 1) return false;
	}
	for(auto const& contract : candidate->contracts) {
		auto mappings = std::count_if(candidate->shipment_owners.begin(), candidate->shipment_owners.end(),
			[&](auto const& mapping) { return mapping.contract_id == contract.id; });
		if(contract.status == contract_status::accepted && mappings != 1) return false;
		if(contract.status != contract_status::accepted && mappings != 0) return false;
	}
	state.exact_person_freight = std::move(candidate);
	return true;
}

void clear_store(sys::state& state) { state.exact_person_freight.reset(); }

} // namespace economy::physical::exact_person_freight
