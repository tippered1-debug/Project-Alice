# World and spatial runtime

Province remains the administrative and legacy-compatibility geography. Canonical physical movement uses stable sites, infrastructure nodes, and routes. Existing provinces provide the initial geography; factories, markets, and deposits resolve to their canonical sites where available.

The spatial network projects province adjacency into infrastructure edges. Route selection is deterministic: shortest paths minimize physical distance, while cheapest paths account for transport cost, infrastructure quality, cargo congestion, and capacity. Stable endpoint and route identifiers resolve ties. Market access and cargo transit use the route when a canonical endpoint and path exist; explicitly documented compatibility paths remain for worlds or overseas connections without a spatial route.

The current spatial model does not claim a historical city graph, dynamically constructed infrastructure, sea-lane geometry, or multi-commodity flow optimization. World geography and authored site/asset links are loaded through the [scenario format](../runtime/scenario-format.md). Shipment execution and delivery are described in [Logistics](logistics.md).
