# Canonical Routed Logistics v1

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
Each day, a concrete leg resource is given its physical capacity from the
existing market/infrastructure capacity primitive. Aggregate route volume and
market fill are not read. Shipments are allocated by persistent Shipment ID,
so older queued work is not bypassed nondeterministically. A shipment larger
than one day's capacity retains `remaining_transport_work` on its leg and is
admitted over multiple days.

Capacity is throughput, not travel time. Once all transport work for a leg is
admitted, the leg traverses for its stored distance-derived number of days.
The next leg is not admitted until the current leg completes, so an
intermediate bottleneck creates a real queue and cannot be skipped.

## Spoilage and conservation

Every active Shipment spoils once per logistics day using the existing
commodity logistics profile, including queued time. The Shipment retains its
physical owner throughout transit. At arrival, only its remaining quantity is
added to the destination inventory and the Shipment identity is removed.
Consequently, physical site inventory plus quantities in active Shipments are
conserved except for explicit profile spoilage or another explicit loss.

## Legacy compatibility boundary

The existing aggregate `world_trade_capacity::clear_trade_shipments()` path,
route volumes, congestion reporting, merchant expansion signals, and classic
trade behavior remain operational for their existing callers. They are not a
carrier or Shipment allocator. Canonical movement has its own exact-Shipment
queue and capacity path.

## Deferred work

The following are deliberately outside v1:

- carrier ownership, carrier assets, rolling stock, fleets, and freight bidding;
- freight contracts, freight payment, and delivered-price integration;
- profit-aware route choice and firm logistics strategy;
- a full physical road/rail/port/sea-lane graph;
- concrete transport labor, service, or carrier actors;
- deriving all legacy route statistics from concrete Shipment events.
