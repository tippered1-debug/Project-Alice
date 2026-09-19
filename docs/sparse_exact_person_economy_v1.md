# Sparse Exact-Person Economy v1

This milestone connects one exact logical human to the existing concrete labor
economy without creating a DCON `person` or `economic_actor`. The canonical
identity remains `persons::exact_population::person_key`, a sealed source-cell
and ordinal range entry. The new economic store is sparse: its baseline cost is
proportional to active exact economic persons, accounts, applications,
contracts, and mixed transactions, not to total logical population.

## Participation and labor

Working age is an individual fact derived from the exact population store. It
is separate from labor-force participation:

* `is_work_eligible` requires an existing, alive exact person aged 14 through
  64 under the canonical signed birth-day policy;
* `is_labor_force_participant` is individual state, not a POP or provincial
  aggregate;
* a source-workforce anchor defaults to participation when eligible;
* a non-anchor exact person defaults to non-participation even when adult;
* `set_labor_force_participation` stores only deviations from that default in a
  sparse PersonKey map.

No POP employment, unemployment, satisfaction, labor supply, or national rate
is consulted. An anchor is bootstrap provenance, not an automatic daily job
seeker or permanent occupation. Exact job search is explicitly invoked for a
chosen PersonKey; there is no full-world scan.

## Exact accounts and mixed transfers

`ExactPersonAccount` is represented by a stable local ID, owner PersonKey,
settlement commodity, and exact balance. Opening an account is idempotent for
the same owner and settlement, starts at zero, and creates no DCON Monetary
Account, Person, or EconomicActor. POP savings are not copied.

`account_ref` is a tagged endpoint for either a DCON MonetaryAccount or an exact
account. Transfers involving an exact endpoint validate both accounts,
settlement equality, positive finite amount, source balance, and distinct
endpoints before mutating either balance. Such transfers are recorded in the
sparse exact transaction ledger; ordinary DCON-to-DCON transfers continue to
use the existing account path.

## Applications and contracts

Exact applications store their own ID, worker PersonKey, existing DCON
JobOffer, date, and pending/accepted/rejected/withdrawn status. Submission
requires existence, life, working-age eligibility, individual participation,
an open offer, and no active exact contract or duplicate pending application.

When the existing job-market pending phase is processed, exact applications
can accept an existing JobOffer and decrement its openings exactly once. The
result is a sparse exact EmploymentContract containing worker, employer
actor, factory, workplace, occupation, labor capacity, wage terms, payer DCON
account, exact worker-account ID, dates, status, and individual unpaid wage
arrears. No DCON JobApplication or EmploymentContract is created for the
exact worker, and no worker bridge is created.

Concrete factory labor and wage-cost queries sum legacy active contracts and
active exact contracts. Therefore the causal path is:

```text
PersonKey -> exact application -> exact contract
          -> factory labor capacity -> production and wage cost
```

Ending an exact contract immediately removes its labor from that sum.

## Payroll and arrears

Canonical factory payroll settles exact contracts through the mixed transfer
primitive:

```text
factory DCON payer account -> ExactPersonAccount
```

The worker balance increases only by the transferred amount and the payer is
debited by exactly that amount. Payer free cash excludes active concrete bid
reservations. If cash is insufficient, no money is minted and the contract's
own `unpaid_wages` field preserves the arrears; no fake creditor actor or POP
aggregate obligation is created. The exact transaction ledger is authoritative
for the mixed payment.

## Persistence boundary

`economy_snapshot` exports/imports participation overrides, exact accounts and
balances, applications, active/ended contracts, individual arrears, and mixed
transactions. Import validates PersonKeys, JobOffers, factories, sites,
employer actors, payer accounts, worker accounts, and settlement commodities
before replacing the store. This is an isolated reconstruction interface; the
normal Project Alice save pipeline is not wired to it in this milestone.

The exact population snapshot override path also seals the real nonzero
PersonKey in every sparse override, so alive/home changes round-trip without
collapsing to `{0,0}`.

## Scale and non-goals

A one-million-person logical catalog can activate exactly one exact account,
application, and contract while DCON Person/EconomicActor counts remain
unchanged. Querying other ordinals remains identity-only and creates no
economic records.

Exact goods inventory, GoodsBid, consumption, generic market-buyer migration,
freight ownership, banking, taxes, benefits, pensions, households, births,
migration, full-world search, historical employment distribution, and
layoffs/quits policy remain future milestones. The next economy step is exact
person goods ownership and consumption; this milestone deliberately stops at
labor and money primitives.
