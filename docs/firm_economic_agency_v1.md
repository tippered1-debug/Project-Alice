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

The saved profit and cashflow observations use completed factory-linked
sales, input fills, freight contracts, and payroll events. Profit includes
gross payroll due; cashflow includes payroll actually paid. Both use a short
exponential history so underwriting sees recent realized performance rather
than reference-price cost estimates.

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
request funding for a capital project. The project waterfall commits cash above
a two-day operating reserve, then solicits pro-rata owner equity from factory
asset stakeholders, then requests bank credit. Owner contributions transfer
real balances and are limited to 20% of each owner's unreserved cash per call.
The operating company is never treated as an external equity source for itself.
The project reserves 150% of estimated construction material cost. Partial
funding scales capacity and material requirements by the funded share; no project
starts below 10% funding. Capacity increases only after materials arrive and are
consumed at the site.

Working-capital needs are calculated separately for input procurement and
payroll from the planned production level. The firm first spends available
operating cash after bid reservations and its safety reserve, then requests
owner equity and bank credit for the shortfall. Funding requests are saved with
factory identity, requested and funded amounts, underwriting terms, collateral
value, and any resulting obligation. A bank can approve the full request, lend
within its risk limit, or reject it. Underwriting uses realized firm cashflow and
profit, repayment/default history, current debt, distress, factory collateral,
project return, and a risk-adjusted interest rate. Credit is originated once
through a bank deposit and transferred from bank reserves into the firm's actual
operating account. Origination is limited by the bank's capital and settlement
liquidity; an empty bank cannot lend. Input, wage, and freight payments continue
through the existing account ledger.

Existing bank loans are serviced by the borrower's economic actor once per
simulation day. Interest accrues by elapsed calendar days. At maturity, the firm
repays from its bank deposit first and its operating account second; an unpaid
balance remains overdue and defaults after 30 days. A defaulted factory-linked
loan places that factory into persistent distress; an unlinked legacy actor loan
still affects that actor's canonical factories. Distress blocks expansion.
The last interest-accrual date is saved with the obligation so repeated updates
on the same simulation date do not charge interest twice.

Aggregate legacy price and employment calculations remain available for
compatibility factories and projections. Canonical production, output asks,
procurement, and hiring use firm decisions and concrete market outcomes.
Loans accrue interest from their saved origination date, mature after one year,
and use the existing service path. Default distress blocks new credit and
expansion. This tranche does not restructure debt, transfer assets after
default, close firms, or support factory-specific occupation mixes. The scorer
remains an evaluation primitive; the firm makes the project decision.
