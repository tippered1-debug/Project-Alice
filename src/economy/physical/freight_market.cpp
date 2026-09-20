#include "freight_market.hpp"
#include "exact_person_freight.hpp"

#include "accounts/accounts.hpp"
#include "economy/causal_order.hpp"
#include "commodity_logistics.hpp"
#include "inventory.hpp"
#include "shipments.hpp"
#include "system_state.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace economy::physical::freight_market {
namespace {

constexpr float epsilon = 1.0e-5f;

bool positive_finite(float value) noexcept { return std::isfinite(value) && value > 0.0f; }
bool nonnegative_finite(float value) noexcept { return std::isfinite(value) && value >= 0.0f; }

float active_bid_reservations(sys::state const& state, dcon::monetary_account_id account) {
	float result = 0.0f;
	state.world.for_each_concrete_market_bid([&](dcon::concrete_market_bid_id bid) {
		if(state.world.concrete_market_bid_get_status(bid) != 0
			|| !state.world.concrete_market_bid_get_concrete_bid_account(bid)
			|| state.world.concrete_market_bid_get_monetary_account_from_concrete_bid_account(bid) != account)
			return;
		result += std::max(0.0f, state.world.concrete_market_bid_get_reserved_amount(bid));
	});
	return std::isfinite(result) ? result : 0.0f;
}

float free_payer_cash(sys::state const& state, dcon::monetary_account_id account) {
	return std::max(0.0f, accounts::balance(state, account) - active_bid_reservations(state, account));
}

dcon::market_id market_for_site(sys::state const& state, dcon::site_id site) {
	if(!site || !state.world.site_is_valid(site)) return {};
	auto province = state.world.site_get_province_from_site_location(site);
	if(!province || !state.world.province_is_valid(province)) return {};
	auto zone = state.world.province_get_state_membership(province);
	return zone ? state.world.state_instance_get_market_from_local_market(zone) : dcon::market_id{};
}

bool carrier_has_presence_for(sys::state const& state, dcon::carrier_id carrier,
	dcon::market_id origin, dcon::market_id /*destination*/) {
	auto presence = state.world.carrier_get_carrier_market_presence(carrier);
	if(!presence) return true; // An empty presence set is the v1 open service area.
	auto market = state.world.carrier_market_presence_get_market(presence);
	// Presence identifies the carrier's home/origin availability. An offer's
	// explicit destination scope controls where that service may end.
	return !origin || market == origin;
}

bool offer_matches(sys::state const& state, dcon::freight_offer_id offer,
	dcon::market_id origin, dcon::market_id destination, uint8_t required_modes,
	float cargo_units, float route_distance, offer_quote& result) {
	if(!offer || !state.world.freight_offer_is_valid(offer)
		|| state.world.freight_offer_get_status(offer) != uint8_t(freight_offer_status::active)) return false;
	result = {};
	result.offer = offer;
	result.carrier = state.world.freight_offer_get_carrier_from_freight_offer_carrier(offer);
	auto carrier = result.carrier;
	if(!carrier || !state.world.carrier_is_valid(carrier)
		|| state.world.carrier_get_status(carrier) != uint8_t(carrier_status::active)) return false;
	if((state.world.carrier_get_mode_mask(carrier) & required_modes) != required_modes
		|| (state.world.freight_offer_get_mode_mask(offer) & required_modes) != required_modes) return false;
	auto offer_origin = state.world.freight_offer_get_origin_market(offer);
	auto offer_destination = state.world.freight_offer_get_destination_market(offer);
	if((offer_origin && offer_origin != origin) || (offer_destination && offer_destination != destination)
		|| !carrier_has_presence_for(state, carrier, origin, destination)) return false;
	auto committed = state.world.freight_offer_get_committed_capacity(offer);
	auto capacity = state.world.freight_offer_get_service_capacity(offer);
	auto carrier_committed = state.world.carrier_get_committed_capacity(carrier);
	auto carrier_capacity = state.world.carrier_get_service_capacity(carrier);
	if(!nonnegative_finite(committed) || !nonnegative_finite(capacity)
		|| !nonnegative_finite(carrier_committed) || !nonnegative_finite(carrier_capacity)
		|| cargo_units > capacity - committed + epsilon
		|| cargo_units > carrier_capacity - carrier_committed + epsilon) return false;
	result.carrier_account = state.world.carrier_get_monetary_account_from_carrier_account(carrier);
	if(!result.carrier_account || !accounts::owner_of(state, result.carrier_account)
		|| accounts::settlement_of(state, result.carrier_account) == dcon::commodity_id{}) return false;
	result.price = state.world.freight_offer_get_base_handling_charge(offer)
		+ cargo_units * route_distance * state.world.freight_offer_get_cargo_distance_rate(offer);
	return positive_finite(result.price);
}

} // namespace

bool quote_offer(sys::state const& state, dcon::freight_offer_id offer,
	dcon::market_id origin_market, dcon::market_id destination_market,
	uint8_t required_modes, float cargo_units, float route_distance, offer_quote& result) {
	return offer_matches(state, offer, origin_market, destination_market, required_modes,
		cargo_units, route_distance, result);
}

bool reserve_capacity(sys::state& state, dcon::freight_offer_id offer, dcon::carrier_id carrier,
	float cargo_units) {
	if(!positive_finite(cargo_units) || !offer || !carrier || !state.world.freight_offer_is_valid(offer)
		|| !state.world.carrier_is_valid(carrier)) return false;
	auto offer_committed = state.world.freight_offer_get_committed_capacity(offer);
	auto offer_capacity = state.world.freight_offer_get_service_capacity(offer);
	auto carrier_committed = state.world.carrier_get_committed_capacity(carrier);
	auto carrier_capacity = state.world.carrier_get_service_capacity(carrier);
	if(!nonnegative_finite(offer_committed) || !nonnegative_finite(offer_capacity)
		|| !nonnegative_finite(carrier_committed) || !nonnegative_finite(carrier_capacity)
		|| cargo_units > offer_capacity - offer_committed + epsilon
		|| cargo_units > carrier_capacity - carrier_committed + epsilon) return false;
	state.world.freight_offer_set_committed_capacity(offer, offer_committed + cargo_units);
	state.world.carrier_set_committed_capacity(carrier, carrier_committed + cargo_units);
	return true;
}

void release_capacity(sys::state& state, dcon::freight_offer_id offer, dcon::carrier_id carrier,
	float cargo_units) {
	if(!positive_finite(cargo_units)) return;
	if(offer && state.world.freight_offer_is_valid(offer))
		state.world.freight_offer_set_committed_capacity(offer,
			std::max(0.0f, state.world.freight_offer_get_committed_capacity(offer) - cargo_units));
	if(carrier && state.world.carrier_is_valid(carrier))
		state.world.carrier_set_committed_capacity(carrier,
			std::max(0.0f, state.world.carrier_get_committed_capacity(carrier) - cargo_units));
}

dcon::carrier_id create_carrier(sys::state& state, dcon::economic_actor_id actor,
	dcon::monetary_account_id account, float service_capacity, uint8_t mode_mask,
	dcon::market_id market_presence) {
	if(!actor || !account || accounts::owner_of(state, account) != actor
		|| !positive_finite(service_capacity) || mode_mask == 0) return {};
	auto carrier = state.world.create_carrier();
	state.world.carrier_set_mode_mask(carrier, mode_mask);
	state.world.carrier_set_service_capacity(carrier, service_capacity);
	state.world.carrier_set_committed_capacity(carrier, 0.0f);
	state.world.carrier_set_status(carrier, uint8_t(carrier_status::active));
	state.world.force_create_carrier_actor(carrier, actor);
	state.world.force_create_carrier_account(carrier, account);
	if(market_presence) state.world.force_create_carrier_market_presence(carrier, market_presence);
	return carrier;
}

dcon::freight_offer_id create_offer(sys::state& state, dcon::carrier_id carrier,
	dcon::market_id origin_market, dcon::market_id destination_market, uint8_t mode_mask,
	float service_capacity, float base_handling_charge, float cargo_distance_rate) {
	if(!carrier || !state.world.carrier_is_valid(carrier)
		|| state.world.carrier_get_status(carrier) != uint8_t(carrier_status::active)
		|| mode_mask == 0 || (mode_mask & state.world.carrier_get_mode_mask(carrier)) != mode_mask
		|| !positive_finite(service_capacity) || !nonnegative_finite(base_handling_charge)
		|| !nonnegative_finite(cargo_distance_rate)
		|| (!positive_finite(base_handling_charge) && !positive_finite(cargo_distance_rate))) return {};
	if(origin_market && !state.world.market_is_valid(origin_market)) return {};
	if(destination_market && !state.world.market_is_valid(destination_market)) return {};
	auto offer = state.world.create_freight_offer();
	state.world.freight_offer_set_origin_market(offer, origin_market);
	state.world.freight_offer_set_destination_market(offer, destination_market);
	state.world.freight_offer_set_mode_mask(offer, mode_mask);
	state.world.freight_offer_set_service_capacity(offer, service_capacity);
	state.world.freight_offer_set_committed_capacity(offer, 0.0f);
	state.world.freight_offer_set_base_handling_charge(offer, base_handling_charge);
	state.world.freight_offer_set_cargo_distance_rate(offer, cargo_distance_rate);
	state.world.freight_offer_set_created_on(offer, state.current_date);
	state.world.freight_offer_set_status(offer, uint8_t(freight_offer_status::active));
	state.world.force_create_freight_offer_carrier(offer, carrier);
	return offer;
}

dcon::freight_request_id create_request(sys::state& state, dcon::economic_actor_id requester,
	dcon::monetary_account_id payer, dcon::site_id source, dcon::site_id destination,
	dcon::commodity_id commodity, float quantity) {
	if(!requester || !payer || accounts::owner_of(state, payer) != requester
		|| !source || !destination || source == destination || !state.world.site_is_valid(source)
		|| !state.world.site_is_valid(destination) || !commodity || !state.world.commodity_is_valid(commodity)
		|| !positive_finite(quantity) || inventory::quantity(state, source, commodity, requester) + epsilon < quantity)
		return {};
	auto profile = logistics::profile_for(state, commodity);
	auto request = state.world.create_freight_request();
	state.world.freight_request_set_quantity(request, quantity);
	state.world.freight_request_set_cargo_units(request, logistics::cargo_units(profile, quantity));
	state.world.freight_request_set_created_on(request, state.current_date);
	state.world.freight_request_set_status(request, uint8_t(freight_request_status::pending));
	shipments::route_quote quote;
	if(shipments::quote_route(state, source, destination, quote)) {
		state.world.freight_request_set_route_distance(request, quote.distance);
		state.world.freight_request_set_primary_trade_route(request, quote.primary_trade_route);
		state.world.freight_request_set_required_mode_mask(request, quote.required_mode_mask);
		state.world.freight_request_set_route_leg_count(request, quote.route_leg_count);
	} else {
		state.world.freight_request_set_route_distance(request, 0.0f);
		state.world.freight_request_set_primary_trade_route(request, {});
		state.world.freight_request_set_required_mode_mask(request, 0);
		state.world.freight_request_set_route_leg_count(request, 0);
	}
	state.world.force_create_freight_request_requester(request, requester);
	state.world.force_create_freight_request_payer_account(request, payer);
	state.world.force_create_freight_request_commodity(request, commodity);
	state.world.force_create_freight_request_source(request, source);
	state.world.force_create_freight_request_destination(request, destination);
	if(economy::causal_order::sequence_for_dcon(state, economy::causal_order::event_kind::freight_request,
		uint64_t(request.index())) == 0) return {};
	return request;
}

dcon::freight_contract_id match_request(sys::state& state, dcon::freight_request_id request) {
	if(!request || !state.world.freight_request_is_valid(request)) return {};
	if(state.world.freight_request_get_status(request) != uint8_t(freight_request_status::pending)) return {};
	auto requester = state.world.freight_request_get_economic_actor_from_freight_request_requester(request);
	auto payer = state.world.freight_request_get_monetary_account_from_freight_request_payer_account(request);
	auto commodity = state.world.freight_request_get_commodity_from_freight_request_commodity(request);
	auto source = state.world.freight_request_get_site_from_freight_request_source(request);
	auto destination = state.world.freight_request_get_site_from_freight_request_destination(request);
	if(!requester || !payer || !commodity || !source || !destination) return {};
	shipments::route_quote quote;
	if(!shipments::quote_route(state, source, destination, quote)) return {};
	state.world.freight_request_set_route_distance(request, quote.distance);
	state.world.freight_request_set_primary_trade_route(request, quote.primary_trade_route);
	state.world.freight_request_set_required_mode_mask(request, quote.required_mode_mask);
	state.world.freight_request_set_route_leg_count(request, quote.route_leg_count);
	auto origin_market = market_for_site(state, source);
	auto destination_market = market_for_site(state, destination);
	std::vector<offer_quote> candidates;
	state.world.for_each_freight_offer([&](dcon::freight_offer_id offer) {
		offer_quote candidate;
		if(offer_matches(state, offer, origin_market, destination_market,
			quote.required_mode_mask, state.world.freight_request_get_cargo_units(request),
			quote.distance, candidate)
			&& accounts::settlement_of(state, payer) == accounts::settlement_of(state, candidate.carrier_account)
			&& free_payer_cash(state, payer) + epsilon >= candidate.price)
			candidates.push_back(candidate);
	});
	std::sort(candidates.begin(), candidates.end(), [](auto const& a, auto const& b) {
		return a.price == b.price ? a.offer.index() < b.offer.index() : a.price < b.price;
	});
	if(candidates.empty() || inventory::quantity(state, source, commodity, requester) + epsilon
		< state.world.freight_request_get_quantity(request)) return {};
	auto selected = candidates.front();
	auto cargo_units = state.world.freight_request_get_cargo_units(request);
	if(!shipments::can_dispatch(state, source, destination, commodity,
		state.world.freight_request_get_quantity(request))) return {};
	if(!reserve_capacity(state, selected.offer, selected.carrier, cargo_units)) return {};
	auto payment = accounts::transfer(state, payer, selected.carrier_account, selected.price,
		relations::transaction_kind::freight, state.current_date);
	if(!payment) {
		release_capacity(state, selected.offer, selected.carrier, cargo_units);
		return {};
	}
	// The route and inventory preconditions above make this dispatch commit
	// deterministic: payment and service capacity are committed first, then the
	// already buyer-owned stock enters the existing routed shipment system.
	auto shipment = shipments::dispatch_transfer(state, source, destination, commodity,
		state.world.freight_request_get_quantity(request), requester, requester);
	if(!shipment) {
		// This is a defensive rollback for a state mutation between the preflight
		// and dispatch. Normal matching cannot reach this path, and spoilage never
		// invokes it: accepted contracts have no automatic freight refund.
		accounts::transfer(state, selected.carrier_account, payer, selected.price,
			relations::transaction_kind::freight, state.current_date);
		release_capacity(state, selected.offer, selected.carrier, cargo_units);
		return {};
	}
	auto contract = state.world.create_freight_contract();
	state.world.freight_contract_set_quantity(contract, state.world.freight_request_get_quantity(request));
	state.world.freight_contract_set_cargo_units(contract, cargo_units);
	state.world.freight_contract_set_agreed_freight_price(contract, selected.price);
	state.world.freight_contract_set_created_on(contract, state.current_date);
	state.world.freight_contract_set_status(contract, uint8_t(freight_contract_status::accepted));
	state.world.force_create_freight_contract_request(contract, request);
	state.world.force_create_freight_contract_offer(contract, selected.offer);
	state.world.force_create_freight_contract_requester(contract, requester);
	state.world.force_create_freight_contract_carrier(contract, selected.carrier);
	state.world.force_create_freight_contract_payer_account(contract, payer);
	state.world.force_create_freight_contract_carrier_account(contract, selected.carrier_account);
	state.world.force_create_freight_contract_commodity(contract, commodity);
	state.world.force_create_freight_contract_source(contract, source);
	state.world.force_create_freight_contract_destination(contract, destination);
	state.world.force_create_freight_contract_payment(contract, payment);
	state.world.force_create_freight_contract_shipment(contract, shipment);
	state.world.force_create_shipment_carrier(shipment, selected.carrier);
	state.world.freight_request_set_status(request, uint8_t(freight_request_status::contracted));
	return contract;
}

void process_pending_requests(sys::state& state) {
	struct candidate {
		bool exact = false;
		dcon::freight_request_id legacy{};
		uint64_t exact_id = 0;
		sys::date created_on{};
		uint64_t causal_sequence = 0;
		uint64_t stable_id = 0;
	};
	std::vector<candidate> pending;
	state.world.for_each_freight_request([&](dcon::freight_request_id request) {
		if(state.world.freight_request_get_status(request) == uint8_t(freight_request_status::pending))
			pending.push_back({false, request, 0, state.world.freight_request_get_created_on(request),
				economy::causal_order::sequence_for_dcon(state, economy::causal_order::event_kind::freight_request,
					uint64_t(request.index())), uint64_t(request.index())});
	});
	for(auto id : exact_person_freight::pending_request_ids(state)) {
		auto request = exact_person_freight::request(state, id);
		if(request) pending.push_back({true, {}, id, request->created_on, request->causal_sequence, id});
	}
	std::sort(pending.begin(), pending.end(), [](auto const& left, auto const& right) {
		if(economy::causal_order::before({left.created_on, left.causal_sequence},
			{right.created_on, right.causal_sequence})) return true;
		if(economy::causal_order::before({right.created_on, right.causal_sequence},
			{left.created_on, left.causal_sequence})) return false;
		return left.stable_id < right.stable_id;
	});
	for(auto const& item : pending) {
		if(item.exact) (void)exact_person_freight::match_request(state, item.exact_id);
		else if(state.world.freight_request_is_valid(item.legacy)) (void)match_request(state, item.legacy);
	}
}

void complete_contract_for_shipment(sys::state& state, dcon::shipment_id shipment) {
	if(!shipment || !state.world.shipment_is_valid(shipment)) return;
	auto contract = state.world.shipment_get_freight_contract_from_freight_contract_shipment(shipment);
	if(!contract || !state.world.freight_contract_is_valid(contract)
		|| state.world.freight_contract_get_status(contract) != uint8_t(freight_contract_status::accepted)) return;
	auto offer = state.world.freight_contract_get_freight_offer_from_freight_contract_offer(contract);
	auto carrier = state.world.freight_contract_get_carrier_from_freight_contract_carrier(contract);
	auto cargo_units = state.world.freight_contract_get_cargo_units(contract);
	release_capacity(state, offer, carrier, cargo_units);
	state.world.freight_contract_set_status(contract, uint8_t(freight_contract_status::fulfilled));
	auto request = state.world.freight_contract_get_freight_request_from_freight_contract_request(contract);
	if(request && state.world.freight_request_is_valid(request))
		state.world.freight_request_set_status(request, uint8_t(freight_request_status::fulfilled));
}

} // namespace economy::physical::freight_market
