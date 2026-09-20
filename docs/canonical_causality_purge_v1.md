# Canonical Causality Purge v1

The canonical transformation economy is isolated from the mutable legacy
aggregate economy at three boundaries.

## Prices and firm decisions

Canonical firm agency and concrete procurement use the concrete-market price
hierarchy: prior concrete `TradeFill` history, then an explicit finite
fallback, then the immutable commodity bootstrap cost. They never read the
mutable legacy `market.price`. The legacy price remains available only through
an explicitly named compatibility helper.

## Factory inputs and output

Canonical factories decide physical output from concrete owned inventory (and
consume that inventory). A recipe with local or money-RGO inputs is not a
canonical physical recipe and cannot scale canonical production. The legacy
`market_clearing` fill remains available only through the explicit legacy
compatibility input evaluator; it is not consulted by canonical production.

## Resource extraction

Legacy RGO fields are bootstrap calibration signals only. When a missing
resource deposit is created, `rgo_size` is converted to a deterministic reserve
scale and `rgo_amount` to daily capacity; these values are not interpreted as
runtime tonnes. Canonical extraction thereafter reads the mutable DCON deposit,
extraction-right, inventory, and shipment state. `province_get_rgo_output()` is
retained for legacy observation and compatibility callers and is not a source
for canonical `PhysicalStock` or shipments.

These compatibility boundaries are intentionally retained so old callers can
continue to operate, but their aggregates are observation/compatibility state,
not canonical causal inputs. The isolation regressions cover mutable legacy
prices, intermediate clearing, and post-bootstrap RGO mutations.
