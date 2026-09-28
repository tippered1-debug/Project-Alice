# Canonical Runtime Migration

This document records which simulation layer owns each kind of state and what
must be true before its compatibility path can be removed. `Age of
Transformation` is an opt-in hybrid ruleset today; turning it on does not
automatically migrate every running subsystem or historical save into one
fully individual simulation.

## Current ownership by domain

| Domain | Source of truth in the flagship ruleset | Compatibility still in use | Cutover requirement |
| --- | --- | --- | --- |
| Population identity | DCON POP rows own aggregate totals; `exact_population` owns stable virtual person keys. | AOEX v2/v3/v4 persist row bindings, retired row tokens, birth/death ranges and aggregate checkpoints. Net births append dated cohorts; net deaths retire keys, end active exact jobs and cancel open exact purchase bids. Accounts, stock and transaction history remain attached to retired keys. Zero-sum row changes currently preserve total identities without reassigning them among rows. | Assign exact people to destination rows during migration, promotion, split and merge. Add inheritance/estate rules for balances and goods, and distinguish transfers from births/deaths when aggregate totals change in the same tick. |
| Jobs and households | A factory's canonical production state gates firm-directed offers; concrete labor, exact-person contracts and household settlement can all participate. | Legacy DCON workers/contracts and exact virtual workers coexist. Existing aggregate POP employment and income calculations remain in the simulation. | Assign existing jobs and initial household resources once, prevent duplicate wages/consumption, and account for every worker when moving between aggregate and exact representations. |
| Goods and production | Canonical factory production, physical inventory, orders and shipments drive participating factories and routes. | Unflagged factories, local or money-like goods, and legacy RGO output retain compatibility behavior. | Cover every production/input/output type, prove stock and money conservation, then retire each matching legacy path. |
| Firms and ownership | Factory agency makes decisions for canonical factory sites; economic actors and ownership relations hold runtime firm ownership. | Legacy factories and aggregate producer debt remain supported for older economy paths; bootstrap placeholder owners represent incomplete historical ownership. | Migrate factory cash, retained earnings, debt and ownership once. Remove province-level producer debt only after no autonomous firm relies on it. |
| Treasury and administration | Institutional treasury accounts, public payroll, procurement and delivered services execute through the public-administration runtime. | DCON national/provincial fiscal fields and existing budget/tax systems remain inputs or compatibility flows. | Reconcile opening balances and tax obligations; demonstrate that collection, expenditure, payroll and service delivery are neither omitted nor booked twice. |
| Domestic politics | Age of Transformation computes aggregate interest groups, coalition legitimacy, law effects and staged reform bills. | Parties remain the public government identity; legacy elections and parts of reform/event behavior remain active. Governance grants are not yet the universal authority source. | Move authority and decision execution by institution and policy area, while retaining the existing election/party interface until an explicit replacement is ready. |
| Foreign policy | Migrated countries use saved strategic interests, beliefs, commitments and crisis state. | Countries without initialized strategic state continue through the legacy diplomacy AI. | Initialize and validate every country on scenario and save load, preserve older-save behavior, and compare all outcomes before removing fallback decisions. |
| Save continuity | DCON stores scenario entities; the versioned `AOEX` extension stores exact-person and other handwritten runtime records. | Older saves lack some or all newer records; only information present in those saves can be migrated. | Keep explicit version migrations, validate checkpoint continuation, and regenerate scenario binaries after serialized DCON schema changes. Never invent individual histories from aggregate totals. |

## Migration stages

1. **Seed identities on load.** With Age of Transformation enabled, scenarios
   and compatible saves register deterministic virtual identity ranges for
   current POP rows. This is now done by the exact-population bootstrap. It
   creates no wallets, jobs or DCON person entities.
2. **Track population lifecycle.** Stable row-lifetime tokens, deletion hooks
   for core simulation paths, net growth/death reconciliation and end-of-day
   checkpoints are now in place. Transfers that leave world totals unchanged
   do not currently move person keys or update their aggregate row membership.
   The next lifecycle work is to map those transfers, splits, merges and class
   changes while preserving each person's balances, goods and contracts.
3. **Migrate opening economic positions.** Specify conservation rules for
   wages, savings, household stocks, factory cash, debt, treasury balances and
   inventories. Each legacy quantity must have exactly one destination.
4. **Cut over one domain at a time.** Make the new representation authoritative
   for one complete domain, retain a measurable compatibility adapter during
   transition, then remove only that domain's fallback after its coverage and
   conservation gates pass.
5. **Retire legacy paths last.** Test new scenarios, supported older saves,
   multiplayer snapshots and save/reload continuation against stable,
   deterministic invariants before deleting compatibility code or changing
   default rules.

## Current migration boundary

The exact-population bootstrap seeds virtual keys once. AOEX v4 checkpoints the
world total and each bound row's size; later net growth appends a birth cohort,
and net decline retires compact ordinal ranges. Zero-sum changes between rows
leave identities in their original cell namespaces. Death selection currently
uses stable low ordinals so deaths do not preferentially retire newly appended
birth cohorts, because aggregate POPs do not identify which people
died. Active exact employment and purchase orders are closed for retired
people, while balances, goods and historical records remain attached to their
keys. The system does not yet infer employment or payment histories before the
bootstrap or reassign people across row splits, merges and transfers.

Schema and runtime migration are separate concerns. A save can be readable and
still lack the individual records needed for equivalent future behavior. A
legacy path should be removed only after both concerns have explicit migration
rules and continuation evidence for that domain.
