# Carrier & Freight Market v1

Carrier and freight market v1 is the concrete logistics settlement layer. A
carrier is a persistent economic actor with one exact settlement account,
supported transport-mode mask, service capacity, committed capacity, status,
and optional home/origin market presence. Offers publish route scope, supported
modes, capacity, and a deterministic handling-plus-distance price basis.

A freight request is created from a concrete movement requirement: requester,
exact payer account, commodity, quantity, owned source stock, destination, and
the current routed-leg quote. It does not read market demand, market fill,
trade-route volume, congestion, or any other aggregate proxy. A successful
concrete goods purchase transfers goods ownership at the source immediately;
the request can remain pending with the buyer-owned stock if no carrier can
match it. The regular logistics tick retries pending requests in deterministic
request-id order; later matching consumes that stock into the normal routed
shipment.

Matching considers active offers in stable price-then-offer-id order. The
selected offer and carrier reserve cargo capacity separately from physical
infrastructure capacity. The exact payer account transfers the agreed freight
price to the exact carrier account before the shipment is created. The
contract stores both accounts, the payment transaction, request, offer,
commodity, source, destination, original quantity, cargo units, and shipment.

Freight capacity is released when the linked shipment arrives. Physical route
capacity remains an independent gate: carrier capacity does not create roads,
ports, routes, or throughput. Shipment spoilage changes the delivered
quantity, but the contract retains the original contracted quantity and does
not refund freight.

The goods purchase price and freight price are separate transactions. The
buyer owns purchased stock before delivery, and delivered acquisition cost is
the goods payment plus any freight payment. v1 deliberately does not add
carrier assets, fleets, labor, operating costs, bankruptcy, insurance, refunds,
firm supplier integration, market-clearing aggregates, or congestion inputs.
