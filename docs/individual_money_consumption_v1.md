# Individual Money Consumption v1

Individual Money Consumption v1 makes a persistent `person` and its existing
`economic_actor` the canonical consumer. It does not create households,
representative consumers, weighted persons, or a second wallet.

## Person needs and location

Each active `person_commodity_need` belongs to exactly one person and one
commodity. It stores desired quantity per period, current unmet quantity, and
the last consumed quantity/date. Owned quantity is read from concrete physical
inventory rather than copied into an aggregate satisfaction field.

Each canonical person may have one persistent `home_site`. Autonomous
purchasing is limited to the market associated with that site. v1 does not
invent migration or household location rules.

## Money and bids

The spending account is selected deterministically from accounts owned by the
person's economic actor: highest free cash wins, with lowest account ID as the
tie-break. Free cash is the exact account balance minus active concrete bid
reservations. If no account exists, no account or money is synthesized.

For each person with a concrete need, processing is stable by person ID and
need/commodity ID. It computes unmet quantity from home-site inventory, uses a
prior concrete trade-fill price or the commodity's concrete cost as a fallback,
and posts a `concrete_market_bid` only for the affordable quantity. The bid
stores the exact person actor, exact payer account, home-site destination,
market, commodity, quantity, and limit price. Existing Concrete Market
matching performs reservations, fills, and exact money transfer; legacy POP
purchase clearing is not called.

Active reservations therefore prevent double-spending. A bid may remain active
when no seller is available. No demand aggregate is mutated by this pass.

## Ownership and consumption

A successful local fill transfers seller inventory to the buyer's exact
economic actor through the existing Concrete Market and inventory APIs. The
consumption step removes only goods owned by that actor at the person's home
site. It records the consumed quantity/date and recomputes unmet need. If the
owned quantity is insufficient, only the owned amount is consumed and the
remainder stays unmet. Goods owned by another actor cannot satisfy the need.

For a remote source, existing Concrete Market/Freight behavior remains in
charge: the buyer-owned goods stay at source until the existing freight/shipment
path delivers them. v1 does not teleport goods or create a parallel consumer
logistics system. Autonomous consumers are intended for goods already
reachable at their local home site/market.

## POP cutover and derived statistics

Under the transformed canonical ruleset, the legacy POP consumption update is
not used to create consumer demand; its post-clearing bookkeeping buffers are
zeroed for compatibility. POP savings, aggregate needs satisfaction, expected
purchase probability, market need weights, aggregate demand buffers, and POP
purchase clearing are legacy/observation paths and are not inputs to an
individual bid or consumption decision.

Canonical savings are balances in concrete individual monetary accounts.
Canonical demand is concrete bids/orders. Canonical consumption is quantity
actually removed from concrete person-owned inventory. Aggregate totals,
averages, poverty/unmet-needs statistics, CPI inputs, and demand observations
may be derived afterward but are not causal inputs.

## Deferred

Households, income pooling, dependents, migration, housing/rent, taxes,
benefits, credit, banks, pensions, nutrition utility, preference learning,
advertising, luxury-status behavior, social networks, black markets, stochastic
shopping, and a broader personal freight policy remain deferred.
