# Canonical construction hard cut

`economy::capital_projects::process_projects` is the daily construction authority for factories, expansions, extraction plants, infrastructure, military equipment production, and naval orders.

## Physical and financial contract

Each new project opens a separate sponsor-owned account and a separate construction yard in the requested province. The yard is an ordinary site in the shared physical inventory; retrieve it through `capital_project_site`. Inventory at the caller's original site is not automatically appropriated. Materials must arrive through shipments or be explicitly contributed to the yard.

Requirements are authored while planned, before funding or consumption. Duplicate commodity rows are merged. Procurement uses canonical reference prices, unreserved project cash, concrete orders, seller payments, and routed freight. Scarce funding is allocated proportionally and bids are emitted in commodity order, independent of requirement insertion order. Pending paid freight and in-transit shipments count as committed cargo; awaiting a carrier does not trigger a second purchase. Awaiting cargo and active-yard materials cannot be resold through the market.

Only actual inventory removal increments the consumed ledger. Completion requires every positive, finite requirement to have `consumed_quantity == required_quantity`. Cached progress, elapsed build time, aggregate demand, and a rounded total percentage cannot grant completion. Empty recipes cannot complete. Transport loss requires real replacement purchases, including a minimum physical lot for sub-epsilon residuals; unused material remains real inventory.

Suspension/cancellation releases open bids and their cash reservations. Paid cargo and account cash remain sponsor-owned; cancellation does not mint a refund. Resume is explicit.

## Results and request adapters

- Factory completion creates the factory, operator binding, asset, and sponsor stake. Expansion changes target factory capacity exactly once.
- `create_extraction_plant` builds an extraction plant on an existing authored deposit; construction never creates reserves or deposits. The operating company must be the deposit's operator or hold an active extraction right when the project is created and again at completion. Materials come from the recipe's construction bill scaled by plant capacity, as for a greenfield factory; a recipe without one cannot be built. Completion creates the plant, its binding to the deposit, the sponsor's asset and stake. The generic factory path rejects extraction recipes.
- Typed infrastructure completion changes the provincial building level and associates the infrastructure node. Railroad completion updates existing edge types and effective route capacity.
- `create_military_production` consumes an explicit physical recipe and emits the requested equipment commodity into shared inventory. It creates no people or regiments.
- Naval requests capture their unit type and nation and create a ship once, after physical consumption.

Existing province-building and naval command queues are request/UI facades backed by saved project bindings. Public project funding passes through real treasury accounts and `authorized_spend_by_institution`; fiscal authority is required. Older factory upgrades with an existing operator can use that operator's real account. Existing canonical firm systems continue to author greenfield factories and expansion projects.

The aggregate `construction.cpp` engine, demand accumulation, purchased-goods advancement, POP/private budgets, hypothetical construction demand, and synthetic foreign-investment credit are removed. `purchased_goods`, `construction_demand`, and `private_construction_demand` are one-way UI projections. Progress queries and factory enumeration read projects. Instant-build cannot fill synthetic stock.

## Save and compatibility boundary

AOEX v15 saves request-to-project bindings and typed results. Request rows have no padding bytes. DCON persists project accounts, recipes, consumed quantities, sites, inventory and results. Older supported AOEX versions isolate unfinished projects onto new yards without claiming unassigned stock or importing synthetic purchased goods. Actual previously consumed requirements remain consumed.

The causal registry accepts zero-based DCON bid/freight-request index 0, including after restore. Sequence 0 remains the missing-sequence sentinel.

Legacy POP recruitment is closed; exact land-force personnel and formations retain authority. There is no automatic mapping from an old unit type to an authored canonical formation template. Legacy manual factory/refit commands remain closed; this change adds no refit or factory-creation UI. Unbacked private POP requests do not receive money from POP savings.

## Validation

`tests/canonical_construction_tests.cpp` is included in the independent `consumer_purge_tests` target. It exercises per-requirement completion, isolated stock/accounts, budget reservations, suspend/cancel, seller payment, arrival and loss, each result kind, corruption of legacy fields, insertion order, fiscal authority, carrier waiting, zero-index causal replay, and normal save/load continuation. Existing capital extraction tests now consume physical materials instead of writing progress=1.

Focused results are recorded with the implementation report. The historical omnibus suite still contains removed consumer APIs; this document does not claim that suite passes.
