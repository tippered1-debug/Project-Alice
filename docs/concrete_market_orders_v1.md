# Concrete market orders v1

Canonical physical trading now uses persisted `concrete_market_bid`,
`concrete_market_ask`, and `concrete_trade_fill` objects. Orders identify their
actors, account or source stock site, market, commodity, purpose, limits,
remaining quantity, and lifecycle. Active asks reserve physical stock by
ledgered order quantity; active bids reserve account capacity by limit value.

Crossing orders are matched deterministically by price and object identity.
Each fill calls the existing atomic account-explicit exchange primitive and
then dispatches a real shipment. The fill records both resulting objects, and observed price is
the executed-fill VWAP for the market, commodity, and date. The concrete path's
`canonical_reference_price` uses the latest prior concrete VWAP first; the old
aggregate market price is only an explicit bootstrap/reference anchor when no
concrete history exists. It is never written back as concrete truth.

The first producer integration is canonical factory input demand: planning
posts a real bid, and fulfillment posts asks only for real eligible inventory
at the market hub. Legacy aggregate demand and non-canonical consumers remain
on the compatibility path. Endogenous bid/ask valuation, including distinct
willingness-to-pay and seller cost formation, is explicitly deferred to `Firm
Economic Agency v1`. This milestone does not claim full endogenous price
discovery. POP consumers, labor, transport allocation, banking, and other
aggregate callers are also intentionally deferred.
