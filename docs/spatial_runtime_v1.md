# Spatial Runtime v1

Spatial Runtime v1 is the canonical physical-geography layer for the first
spatial economic slice.

`world::spatial_runtime::bootstrap()` creates or reuses one deterministic
settlement, site, and infrastructure node for each existing province. It then
projects the existing province-adjacency graph into infrastructure edges.
Sources are processed by stable province/object identifiers, and existing
saved relationships are reused, so a repeated bootstrap is idempotent and a
DCON save/load preserves the same spatial objects and routes.

Factories, markets, and resource/deposit compatibility paths resolve to these
sites where a canonical site is available. Province remains the administrative
and legacy compatibility geography; the physical path is site → node → route
→ site. Existing factory/deposit-specific sites are retained when already
present, but no second canonical endpoint is created for the same province.

Infrastructure edge capacity and traversal time are derived from persisted
edge distance and infrastructure type. `shortest_path()` minimizes physical
distance. `cheapest_path()` minimizes generalized transport cost, including
infrastructure quality and cargo congestion, and rejects a path whose edge
capacity cannot admit the requested cargo. Equal routes use stable endpoint
keys and are independent of relationship/container iteration order.

`market_access::evaluate_province()` uses the network route when the spatial
mapping and route are available. Its old distance/railroad/control formula is
only the compatibility fallback for worlds without a usable spatial endpoint
or route; it is not a second canonical network source.

Canonical cargo transit receives route distance, route-derived travel time,
capacity, cargo weight, and utilization. `average_travel_days` remains only
for explicit legacy callers. Transit therefore applies capacity blocking and
spoilage over the physical route duration.

The v1 slice intentionally does not model historical city topology, dynamic
construction of infrastructure, sea-lane geometry, multi-commodity flow
optimization, or persistent route caches. Overseas trade routes without a
spatial path use an explicit compatibility projection of their existing trade
route distance until those spatial networks are introduced.
