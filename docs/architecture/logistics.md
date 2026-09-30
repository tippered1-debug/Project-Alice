# Logistics

Canonical physical shipments are authoritative for goods that are moving
between sites. `trade_route.volume`, market clearing fill, and legacy cargo
allocation remain compatibility observations; they do not advance a canonical
shipment and cannot create one.

## Lifecycle

Dispatch first resolves a deterministic route, then removes the requested
quantity from the source inventory and creates one Shipment plus its persistent
ordered `shipment_route_leg` records. A Shipment is `queued` for its current
leg, `travelling` after that leg has been admitted, and is deleted only after
arrival. A malformed or incomplete persisted route is marked `blocked`; it is
never silently teleported.

The route plan is saved in DCON state. Each leg stores its mode, endpoints,
coarse trade-route backbone (when applicable), sequence, distance, remaining
transport work, and traversal days. The Shipment stores the current leg,
route-leg count, lifecycle, and current traversal countdown.

## Route shape and planning

Local legs connect a site to its market hub, or the hub to a destination site.
Same-market shipments therefore still consume local capacity. Inter-market
legs use the existing `trade_route` topology as a coarse backbone and select a
shortest feasible path by physical distance, with stable route-ID tie breaks.
Land and sea are represented by the existing transport modes. This is not yet
the final road, rail, port, or sea-lane graph.

Dispatch fails before inventory removal when a required inter-market path is
disconnected or a required endpoint is unavailable. Small editor states with
no market mapping use one explicit local leg as a transitional test/editor
resource; this is still queued and capacity-limited, not a direct-distance
Shipment countdown.

## Capacity and queues

Each active Shipment requests `logistics::cargo_units(profile_for(commodity),
quantity)`, so commodity cargo weight changes physical capacity consumption.
Each day, a concrete leg resource is given its physical capacity from explicit
railroad, naval-base, and civilian-port infrastructure proxies. Legacy market
throughput, population, aggregate route volume, and market fill are not read.
Shipments are allocated by persistent Shipment ID,
so older queued work is not bypassed nondeterministically. A shipment larger
than one day's capacity retains `remaining_transport_work` on its leg and is
admitted over multiple days.

Capacity is throughput, not travel time. Once all transport work for a leg is
admitted, the leg traverses for its stored distance-derived number of days.
The next leg is not admitted until the current leg completes, so an
intermediate bottleneck creates a real queue and cannot be skipped.

## Spoilage and conservation

Every active Shipment spoils once per logistics day using the existing
commodity logistics profile, including queued time. While a leg is queued,
only its unconsumed `remaining_transport_work` is scaled by that spoilage;
capacity already consumed is never refunded. A future leg starts with zero
work and is initialized from the quantity remaining when the preceding leg
arrives. The Shipment retains its physical owner throughout transit. At
arrival, only its remaining quantity is added to the destination inventory and
the Shipment identity is removed.
Consequently, physical site inventory plus quantities in active Shipments are
conserved except for explicit profile spoilage or another explicit loss.

## Actor markets and freight

Concrete bids and asks identify their actors, settlement accounts or source stock, market, commodity, price limits, and remaining quantity. Matching follows causal order and stable identities; asks are ranked by landed cost, including route cost, cargo weight, and expected spoilage. A fill transfers money to the actual seller and transfers real stock ownership to the buyer at the source site. Aggregate market fields remain read projections and do not create stock or seller identity.

Remote movement uses the same routed shipment processor. A freight request reserves buyer-owned stock and seeks a carrier offer in deterministic price and offer-ID order. Carrier service capacity and physical route capacity are separate limits. Freight payment is a distinct transaction from the goods fill; delivery releases the reserved capacities and credits only the surviving cargo quantity at destination. A purchase can remain valid with owned stock at origin while no carrier is available.

Exact-person consumers use sparse freight requests and shipment-owner mappings keyed by `PersonKey`. They do not require a DCON Person, EconomicActor, or MonetaryAccount. Person stock is credited at the source after a purchase and at the destination only after shipment arrival. Exact freight uses the same route, capacity, travel-time, and spoilage path as other canonical shipments.

## Legacy compatibility boundary

Aggregate market demand, prices, fills, route volume, congestion reporting, merchant expansion signals, and classic trade behavior remain available to their existing callers. They do not allocate canonical shipments or replace actor, carrier, account, inventory, or route records. Explicit legacy-only commodities and scenarios without a usable canonical route may use the compatibility paths documented by their domain contracts.

The current spatial model does not claim a complete physical road/rail/port/sea-lane graph, carrier fleets and operating costs, or derived legacy route statistics from every concrete shipment event. See [World](world.md) for spatial scope and [Causality](causality.md) for the no-reverse-read rule.
