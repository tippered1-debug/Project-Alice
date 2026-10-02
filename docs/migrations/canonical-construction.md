# Canonical construction

Construction is an industry. A capital project is built only by the crew of a contractor's construction works. Progress is work that crew actually performs, and the materials that work installs.

```text
project sponsor (firm, investor, or treasury)
  -> capital project: account, yard, material recipe, required work
  -> construction contract (fixed price per work unit)
  -> contractor organization and its construction works
  -> exact workers (job offers, contracts, payroll) and equipment stock
  -> daily work = min(crew capacity, site limit, materials at the yard, project cash / price)
  -> materials installed in proportion to the work; contractor paid for exactly that work
  -> completed asset when every material and every unit of work is in
```

`economy::capital_projects::process_projects` runs procurement, then `economy::construction_industry::process`: contract upkeep and default, tendering, equipment procurement, each works' day of work, and its payroll.

## Projects

Each project opens its own sponsor-owned account and yard (a site in shared inventory). Requirements are set while it is planned. Procurement buys what the yard still lacks: requirement minus installed, on-site, in-transit, freight-requested, and bid quantities. Scarce cash is split between materials and the crew's remaining work in proportion to what each still costs. Stock at the yard is never sold back and is never installed on arrival.

`required_work` is sealed when the first contract is signed: `build_days * reference_crew`, in reference worker-days. `build_days` is the content build time scaled by the existing `alice_*_build_time_mult` defines; 30 days applies when content has none. `reference_crew = max(1, 0.05 * total material quantity)`. This is a staffing plan, not a clock. Nothing advances with elapsed time.

`apply_work` is the only path that installs materials. Work may advance only as far as every material, installed or at the yard, carries it. Each material is installed in proportion to the work done. The final step installs exactly what remains, and only when all of it is physically at the yard. Completion requires `work_done == required_work` and every requirement fully consumed. `physical_progress` is the lower of the work fraction and the material fraction, and is a projection only.

## Contractors

A construction works is a DCON `factory` with `construction_works = 1` and no product recipe. It has its own site, an operator organization, an asset owned by that operator, and a payroll settlement. Its capacity is its crew and equipment:

- Crew: exact workers hired through ordinary job offers. `firm_agency::decide_factory` delegates to `construction_industry::decide_works`, so vacancies, applications, layoffs and wage offers follow the contractor's planned crews. Wages are settled daily by `payroll::settle_factory`, whether or not the sites were workable.
- Equipment: stock of commodities flagged `construction_equipment` (content `machine_parts`), held at the works and owned by the contractor. A worker does 1 work unit a day by hand and 2 when equipped (0.5 equipment units per worker). Equipment in use wears out at 0.2% a day of full use. The contractor buys equipment for its planned crews through the concrete market, from cash beyond ten days of wages.

Production, recipe input planning, factory modifiers, acquisition and legacy consumption estimates skip works. If a works is liquidated, its equipment stays the operator's property.

## Contracts

`construction_contract` records the project, the works, the unit price, the planned crew, the work performed and amount paid, and idle and stalled day counters. Its status is `active`, `completed`, `cancelled`, or `defaulted`.

- **Tender.** A funded project without an active contract picks the cheapest operating works in its province with the same settlement. A works qualifies only if its committed crews plus the new crew fit within `max(25, 2 * current crew)`, and it has never defaulted on this project. If no local works has room, the project's responsible organization opens its own works and builds with its own crew under the same contract and payroll.
- **Price and payment.** A quote is wage per worker divided by work per worker, plus a 15% margin, fixed at signing. After each day's work, the project account pays work times price to the contractor's operating account. Nothing is advanced and nothing is owed.
- **Competition.** A works serves its contracts in signing order. A site absorbs at most its planned crew working fully equipped. Several projects compete for one crew, and contractors compete for workers in the ordinary labor market.
- **Stand-down.** A contract stalled for 7 days by its sponsor (no materials or no cash) leaves the works' staffing plan until the site is workable again.
- **Default.** The sponsor terminates a contract when the works is bankrupt or liquidated, has lost its operator, or performed no work for 30 days on a site that was workable and paid for. Work already done stays on the project, and installed materials stay installed. The contractor keeps payments for work it did. The project is re-tendered the same day.
- **Cancellation** releases bids and ends the contract. Yard stock and project cash remain the sponsor's.

## Kinds of project

Factories, factory expansions, extraction plants on existing deposits, typed infrastructure and naval orders all use this mechanism, and their completion results are unchanged. Extraction plants still never create deposits or reserves.

Military equipment production is no longer a capital project. Project kind 4 is `retired_military_production`, and `create` rejects it. Equipment is a manufactured commodity made by ordinary factories and bought in the concrete market. Unfinished orders of this kind in older saves are cancelled; their stock and cash stay with the sponsor.

## Public construction

Legacy province-building and naval queues remain request/UI facades backed by saved project bindings. Their projects are sponsored by the central government's actor. The responsible organization is the nation's public works enterprise, a `state_entity` wholly owned by the government and created on first use (`nation_public_works`). Treasury funding passes through `authorized_spend_by_institution` and covers both materials and remaining labor at the reference unit price. Tendering, contracts, payroll and payment are the same as for private projects.

Firm expansion and greenfield budgets include a labor estimate from `capital_projects::labor_cost_estimate`.

## Saves

DCON persists projects, requirements, contracts, works and equipment stock; the exact runtime extension persists the crews' labor contracts. AOEX v16 marks the industry cut. When a v15 or older save is loaded, each unfinished project's required work is sealed. Its `work_done` is set to the share that its already-consumed materials carry, because the old engine installed materials on arrival. Retired military orders are cancelled.

## Outside the model

- Workers live in the works' province; construction crews do not travel between provinces, and commuting to the yard is not modeled.
- Crews are general labor; there is no skilled-trade mix or learning curve.
- Contracts are pay-as-you-go. There are no retainers, performance bonds, penalty clauses, or contractor credit lines; a contractor short of cash falls into wage arrears, loses its crew, and defaults.
- Equipment is a single flagged commodity; there are no machine types or equipment rental.
- The legacy manual factory and refit commands remain closed, and land-unit equipment procurement is not a construction path.

## Validation

`tests/canonical_construction_tests.cpp` is built into the independent `consumer_purge_tests` target. It covers materials without workers, workers without materials, the limited daily rate (by hand, mechanized, and short-handed), two projects competing for one crew, money conservation across payments and wages, cancellation, default by non-performance and by bankruptcy with re-tender, save/load of unfinished work and contracts, every result kind built only by contracted work, public works through the state enterprise, and the earlier procurement, carrier, rounding and isolation guarantees.
