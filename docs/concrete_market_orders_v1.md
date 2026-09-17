# Concrete market orders v1

Canonical physical trading now uses persisted `concrete_market_bid`,
`concrete_market_ask`, and `concrete_trade_fill` objects. Orders identify their
actors, account or source stock site, market, commodity, purpose, limits,
remaining quantity, and lifecycle. Active asks reserve physical stock by
ledgered order quantity; active bids reserve account capacity by limit value.

Crossing orders are matched deterministically by price and object identity.
Each fill calls the existing atomic `exchange::purchase` and then dispatches a
real shipment. The fill records both resulting objects, and observed price is
the executed-fill VWAP for the market, commodity, and date. The old reference
price remains a fallback observation and is not used to replace a concrete
execution.

The first producer integration is canonical factory input demand: planning
posts a real bid, and fulfillment posts asks only for real eligible inventory
at the market hub. Legacy aggregate demand and non-canonical consumers remain
on the compatibility path. Firm agency, POP consumers/labor, transport
allocation, banking, and other aggregate callers are intentionally deferred.
