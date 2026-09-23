# Autonomous Firm Agency v1

Canonical factories save their own operating expectations and decision memory.
The state is attached to each factory and serialized with the world: planned
production and employment, sell-through and price expectations, input cost,
input reliability and delivery time, inventory targets, pricing markup, recent
sales/cost/profit observations, unmet input demand, cashflow, and persistent
distress.

The daily firm cycle observes that factory's prior offers, concrete fills,
revenue, inventory, shipments, and procurement account. Sell-through, realized
price, input reliability, and inbound lead time adapt with a 20% update rate.
Operating targets are reconsidered every eight days. Production moves at most
10% of capacity per decision, adjusts to expected sell-through and output
stock, and is capped by the firm's available procurement and payroll cash.
Observed input reliability both limits the production target and sets a modest
procurement safety buffer. Input bids use a firm-specific limit markup; the
Actor Market continues to select suppliers and execute trades.

After production, factories post asks for output held at their market hub. The
ask reflects the firm's own price expectation, expected unit cost, and adaptive
markup. Concrete market fills transfer revenue through the existing actor
account and shipment path. The next observation updates the firm's own history,
so otherwise identical firms can diverge after different sales or delivery
outcomes.

The existing concrete labor market uses the firm's cash-limited production
decision for vacancies, while labor availability and contracts continue to
determine actual staffing. Persistent weak sales or negative operating results
accumulate distress; after 90 distressed days effective productive capacity
contracts gradually. No factory is closed after a short-lived shock.

Expansion is scored using each firm's sales, margin, input reliability, and
delivery history. Strong utilization and persistent sales can cause the firm to
start a capital project, reserve 150% of estimated construction material cost
from its operating cash, and bid for construction goods in the concrete market.
Capacity increases only after materials arrive and are consumed at the site.

Existing bank loans are serviced by the borrower's economic actor once per
simulation day. Interest accrues by elapsed calendar days. At maturity, the firm
repays from its bank deposit first and its operating account second; an unpaid
balance remains overdue and defaults after 30 days. A defaulted loan places its
operated canonical factories into persistent distress and blocks expansion.
The last interest-accrual date is saved with the obligation so repeated updates
on the same simulation date do not charge interest twice.

Aggregate legacy price and employment calculations remain available for
compatibility factories and projections. Canonical production, output asks,
procurement, and hiring use firm decisions and concrete market outcomes.
Expansion uses retained operating cash only. This tranche services already
approved loans; it does not originate credit, inject owner equity, transfer
assets after default, close firms, or support factory-specific occupation mixes.
The scorer remains an evaluation primitive; the firm makes the project decision.
