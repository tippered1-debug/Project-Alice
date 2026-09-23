# Actor Market v1

The canonical transformed market uses the saved concrete market orders and
fills. A concrete ask belongs to one economic actor and one source site; its
reserved quantity is backed by that actor's physical stock. A concrete bid
belongs to one buyer, destination site, settlement account, commodity and
limit price. Both order types have stable DCON identities and support partial
execution.

`concrete_market::match_all()` clears the actor order book across origin and
destination markets. Bids are processed by causal date/sequence and asks are
ranked for each buyer by landed unit cost, then stable ask identity. Landed
cost includes the Spatial Runtime route cost, cargo weight and expected route
spoilage. The mutable legacy `market.price` is never used as the canonical
price fallback; concrete trade history and immutable commodity cost are used
instead.

Goods payment and ownership transfer are recorded by a concrete transaction.
For a cross-site fill, the buyer initially owns the stock at the source while
freight is pending. A freight request then reserves a carrier service and
dispatches a routed shipment. Only arrival materializes buyer inventory at the
destination. Seller revenue is therefore exactly the goods price of filled
transactions; unsold asks create no revenue.

Factory input procurement posts bids for real factory demand and asks for all
reachable actor-owned PhysicalStock, rather than reading aggregate province
supply. `match_all()` creates the same routed shipment path for remote input
sellers. Production consumes only destination inventory, so undelivered input
constrains production naturally.

Aggregate market demand, fill and price fields remain compatibility/UI
projections. They are not physical storage, seller identity, ownership, or a
second source of canonical goods. `market_clearing::clear_call_auction()` now
also exposes concrete buyer-order/seller-order match records for callers that
use the pure auction primitive.

Realized ask/fill history is exposed as sell-through feedback to transformed AI
investment ranking when a current actor-market observation exists; the old
probability estimate remains the explicit no-observation fallback. Network
market access continues to come from Spatial Runtime reachability.

Current v1 limits are unchanged: freight availability can leave an accepted
goods fill in a pending request, legacy-only commodities still use explicit
compatibility paths, and the autonomous firm pricing strategy remains outside
this milestone.
