# Exact Person Freight v1

Exact freight is sparse state keyed by `PersonKey`: requests, contracts, and
`dcon::shipment_id -> PersonKey` ownership mappings. Registering logical
population creates no freight records. Exact request matching uses the same
DCON Carrier, FreightOffer, route quote, mode mask, service capacity, carrier
capacity, and price calculation as ordinary freight.

Physical ownership is explicit and conserved:

`Seller DCON stock at A`
`-> purchase`
`-> Exact stock at A`
`-> freight dispatch`
`-> Shipment in transit`
`-> arrival`
`-> Exact stock at B`

The exact consumer never receives a DCON Person, EconomicActor,
MonetaryAccount, or PhysicalStock. The in-transit DCON Shipment has no fake
exact-person owner relation; its sparse external mapping identifies the exact
owner and contract.

Remote market purchase and freight are separate causal events. Purchase money
is paid to the real seller immediately and the exact fill records goods at the
seller source site. A pending request reserves those exact source units. A
second request cannot reserve the same units. If no carrier is available, the
purchase remains valid and the goods remain at source.

Freight payer selection considers eligible Carrier offers in deterministic
`price -> FreightOffer ID -> exact account ID` order. The payer settlement
must match the carrier's real DCON account. Free exact cash is reduced by
active exact GoodsBid reservations. Freight payment is a mixed exact-to-DCON
transaction of kind `freight`; it is not added to the commodity fill price.

On dispatch, reserved exact stock is removed and becomes Shipment cargo. The
existing route planner, route legs, capacity admission, travel time, and
commodity spoilage are reused. Arrival credits only the surviving quantity to
exact destination stock, completes the exact request/contract, releases offer
and carrier capacity exactly once, and removes the external mapping.

Incoming exact quantity toward a destination is counted by purchase decisions
so an in-transit purchase cannot be duplicated. Incoming quantity is not
consumed or treated as home stock until arrival.

The isolated snapshot boundary persists exact requests, contracts, and
shipment ownership mappings with IDs and validates references. Normal save
integration remains unwired. Restore ordering is:

`Exact Population -> Exact Person Economy -> Exact Person Goods -> DCON world shipments -> Exact Person Freight`.

The remaining future work is outside this milestone: exact-person selling,
households, credit, taxes, population-wide shopping, and migration.
