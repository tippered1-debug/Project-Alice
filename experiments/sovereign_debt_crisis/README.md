# Sovereign Debt Crisis Lab

This experiment runs the same small canonical finance fixture under three
540-day policies: a balanced baseline, a persistent tax-revenue shock, and the
same shock with lower public-service spending. Each run creates a real
government institution, finance office, tax assessment, treasury, consented
bank bond purchase, public-spending transfers, and sovereign-debt obligation.
Daily servicing, overdue status, the 30-day default grace period, and bank
claims are read from the game systems.

The deterministic fixture starts with 600 units of 5% annual public debt due
after 365 days, 12 units/day of tax revenue, and 10 units/day of planned public
services. On day 90, the two crisis cases reduce tax revenue to 3 units/day.
The austerity case also reduces planned services to 2 units/day. When the
treasury cannot cover the planned service payment, the fixture funds the
remaining cash and records the shortfall. Spending is processed before debt
service; this ordering is an explicit experiment policy, not a general
government priority rule in the full game. The creditor bank starts with 2,000
reserve units and 1,750 units of deposit liabilities, so sovereign impairment
can be compared with its opening capital cushion.

## Run

Build the normal Alice executable, then run:

```sh
python3 experiments/sovereign_debt_crisis/run_experiment.py \
  --binary build/Alice --output sovereign-debt-crisis-results
```

The runner executes each scenario twice with the same seed, checks matching
checksums, and checks the expected default/service-funding outcomes. It writes
`timeseries.csv`, `events.csv`, and `summary.csv` under each scenario folder,
the replay files under `determinism-replay/`, and the comparison manifest to
`comparison.json`.

One scenario can be run directly:

```sh
build/Alice --single-thread --sovereign-debt-crisis-lab \
  --debt-lab-scenario revenue-shock --debt-lab-days 540 \
  --debt-lab-seed 424242 --debt-lab-shock-day 90 \
  --debt-lab-output sovereign-debt-crisis-results/revenue-shock
```

The output includes daily tax assessed/collected, planned and funded public
services, service shortfall, debt principal and interest, treasury cash, bank
reserves and sovereign claims, bank status, and money-conservation error.
Events link the issue, daily tax transfers, public spending, debt service, and
default transition. The summary includes bank net-worth sensitivity at 0%,
25%, 50%, 75%, and 100% recovery of defaulted claims.

## Reading the results

The base banking system keeps a defaulted sovereign claim at face value. The
recovery columns are a transparent stress overlay over the actual bank balance
sheet; they do not mutate the bank, write down the obligation, or model a
government recapitalization. The fixture also does not run production,
household consumption, voter preferences, elections, or government
replacement. It demonstrates fiscal distress and creditor exposure through
the real debt and account systems, and keeps those broader political channels
explicitly outside this experiment's claim.

The numeric bank status is `0` solvent, `1` constrained, or `2` insolvent.

No results are committed until the runner is executed against a built binary.
The repository's current workspace instruction says not to start a build, so
this change adds the reproducible experiment and its outcome checks without
claiming measured run results.
