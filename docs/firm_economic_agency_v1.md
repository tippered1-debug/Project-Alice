# Firm Economic Agency v1

Canonical factories now derive production intent from their own economic state.
The deterministic decision observes operator cash, active concrete-bid
reservations, recipe input prices, output reference price, owned physical
input/output inventory, capacity, employment wages, and active payroll arrears.

For each factory, the policy computes expected unit revenue, recipe variable
cost, full-capacity payroll cost, and gross margin. Healthy positive-margin
firms begin from physical capacity. Negative-margin firms retain a 5% probing
level. Output inventory above three capacity-days proportionally suppresses
new desired units. Free cash is reduced by active bid reservations and payroll
arrears, then a 10% safety reserve is held; the remaining budget caps desired
units. These bounded rules provide smoothing through inventory and cash rather
than an unconditional utilization command.

Desired units are distinct from realized production. Procurement bids use the
chosen desired units minus owned inventory and in-transit quantities through
the existing factory-input calculation. Physical input availability, labor,
capacity, payroll settlement, and output materialization still determine
realized production and real money/physical state.

Price expectations use `canonical_reference_price`: prior concrete executed
trade history/VWAP has priority, while the legacy aggregate price is only a
bootstrap/reference anchor when no concrete history exists. Aggregate
production targets and compatibility utilization fields remain for legacy/UI
compatibility but are not authoritative for canonical factory decisions.

No persistent decision fields are added because this v1 decision is derived
from saved economic truth. Endogenous valuation, managers, borrowing,
bankruptcy, investment, opening/closing, transport optimization, household
demand, concrete labor matching, and sophisticated forecasting remain
deferred.
