#pragma once

#include "dcon_generated.hpp"
#include "date_interface.hpp"
#include "persons/exact_population.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace sys { class state; }

namespace economy::physical::exact_person_freight {

using person_key = persons::exact_population::person_key;

enum class request_status : uint8_t { pending = 0, contracted = 1, fulfilled = 2, canceled = 3 };
enum class contract_status : uint8_t { accepted = 0, fulfilled = 1, canceled = 2 };

struct request_record {
	uint64_t id = 0;
	person_key requester{};
	dcon::site_id source{};
	dcon::site_id destination{};
	dcon::commodity_id commodity{};
	float quantity = 0.0f;
	float cargo_units = 0.0f;
	sys::date created_on{};
	float route_distance = 0.0f;
	uint8_t required_mode_mask = 0;
	dcon::trade_route_id primary_trade_route{};
	uint8_t route_leg_count = 0;
	request_status status = request_status::pending;
	uint64_t originating_fill_id = 0;
};

struct contract_record {
	uint64_t id = 0;
	uint64_t request_id = 0;
	person_key requester{};
	dcon::freight_offer_id offer{};
	dcon::carrier_id carrier{};
	dcon::monetary_account_id carrier_account{};
	uint64_t exact_payer_account_id = 0;
	dcon::commodity_id commodity{};
	dcon::site_id source{};
	dcon::site_id destination{};
	float quantity = 0.0f;
	float cargo_units = 0.0f;
	float agreed_freight_price = 0.0f;
	uint64_t exact_payment_transaction_id = 0;
	dcon::shipment_id shipment{};
	sys::date created_on{};
	contract_status status = contract_status::accepted;
};

struct shipment_owner_record {
	dcon::shipment_id shipment{};
	person_key owner{};
	uint64_t contract_id = 0;
};

struct freight_snapshot {
	uint32_t version = 1;
	std::vector<request_record> requests;
	std::vector<contract_record> contracts;
	std::vector<shipment_owner_record> shipment_owners;
};

float reserved_source_quantity(sys::state const&, person_key, dcon::site_id, dcon::commodity_id);
float incoming_quantity(sys::state const&, person_key, dcon::site_id, dcon::commodity_id);

uint64_t create_request(sys::state&, person_key, dcon::site_id source, dcon::site_id destination,
	dcon::commodity_id, float quantity, uint64_t originating_fill_id = 0);
std::optional<request_record> request(sys::state const&, uint64_t request_id);
std::optional<contract_record> contract(sys::state const&, uint64_t contract_id);
uint64_t request_count(sys::state const&);
uint64_t contract_count(sys::state const&);
uint64_t shipment_owner_count(sys::state const&);

uint64_t match_request(sys::state&, uint64_t request_id);
void process_pending_requests(sys::state&);
void cancel_request(sys::state&, uint64_t request_id);

bool register_shipment_owner(sys::state&, dcon::shipment_id, person_key, uint64_t contract_id);
bool is_external_shipment(sys::state const&, dcon::shipment_id);
bool complete_external_shipment(sys::state&, dcon::shipment_id, float surviving_quantity);

freight_snapshot export_snapshot(sys::state const&);
bool import_snapshot(sys::state&, freight_snapshot const&);
void clear_store(sys::state&);

} // namespace economy::physical::exact_person_freight
