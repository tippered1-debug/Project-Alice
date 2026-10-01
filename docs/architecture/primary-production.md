# Primary production

Primary production is how raw goods enter the physical economy: mining, farming, herding, fishing, and forestry. OUR TIME has no RGO. A Victoria II RGO fused five different things: a natural endowment, a productive facility, an employer, a land tenure, and a province label used by scripts and POP types. Each of those has its own owner here.

## Target architecture

### One production engine

Every primary producer is an ordinary productive establishment (DCON `factory`) run by `industrial_production::produce_factory`. It has the same operator firm, exact labor contracts, physical inventory, shipments, market asks, payroll, and capital projects as any factory. There is no separate extraction engine.

A primary recipe (`factory_type`) carries a **natural binding**: the establishment is attached to one natural asset, and that asset limits its output.

| Kind | Binding | Output |
| --- | --- | --- |
| Mines, wells, quarries | finite `resource_deposit` | labor units × output per unit × grade, limited by daily capacity and remaining reserves, which fall by the amount extracted |
| Fishing, forestry | renewable `resource_deposit` | effort × stock; the stock regenerates logistically toward its carrying capacity |
| Crops, livestock, plantations | `land_title` (area × suitability) | `A · L^0.6 · T^0.4`: labor has diminishing returns on fixed land |

Primary recipes come from content production types (`type = rgo`). Efficiency inputs such as tools and fertilizer raise productivity. They are never required for output.

### Natural assets

Natural assets are nature, not capital. Construction never creates reserves or land. A capital project builds an establishment on an asset that already exists.

- `resource_deposit`: commodity, reserves, grade, daily capacity, and regeneration for renewable stocks.
- `land_title`: area and suitability per commodity at a site.

Both are assets with ownership stakes.

### Access and rent

To operate a natural asset, an actor must either own it or hold a `use_right` from its owner. `use_right` generalizes `resource_extraction_right`: it records the asset, the holder, the granting owner, the term, and the rent terms. Rent can take three forms:

- `cash`: money per unit of area or output;
- `share`: part of the output, transferred physically (sharecropping, royalty in kind);
- `labor`: part of the members' labor time, applied to the owner's establishment (corvée).

Tenancy, sharecropping, quitrent, corvée, and mining concessions are all `use_right`s.

Subsoil ownership is a law policy rule, `subsoil_regime`:
- `crown`: the treasury owns deposits. A concession requires an office holding `license` authority.
- `accession`: the owner of the land title above a deposit owns it.

### Rural households

A **household cohort** is an organization of kind `household`. It has an account, physical stock, and assets like any actor. Its members are exact persons, stored as range-compressed membership records in the exact population store. Members share one budget in equal per-capita shares.

A compressed record is valid only while every member's state is identical. Any divergence — a job, migration, military service, individual property — splits that person out into an individual sparse record.

**Invariant:** every living person belongs to exactly one budget, either a cohort or an individual account.

A cohort operates its own establishments. Its members' labor is self-employment, with no payroll. Each day the cohort:
1. produces;
2. keeps 60 days of its members' needs in its own produce;
3. offers the surplus through the common ask path;
4. buys everything else with its cash;
5. consumes, drawing first on its own stock without a market transaction.

The cohort's income per worker is its members' reservation wage. A member takes a job when the commute-adjusted wage exceeds 1.1 × the reservation wage. A displaced worker who finds no job for 30 days rejoins their home cohort.

A landed-elite cohort owns land titles and runs its demesne directly, so its income arrives without dividends. A bonded cohort has no account: its labor goes to the owner's establishment, and the owner pays for its needs.

### Other rules

- **POP type** is a projection of a person's role, not of a province's resource.
- **Money:** commodity 0 is an abstract unit of account. Precious metal is an ordinary physical commodity.

## First hard cut (implemented)

The first slice covers only finite commercial extraction:

- `common/canonical_runtime/deposits.csv` creates deposits from authored natural data. Province RGO values no longer seed anything.
- Content production types with `type = rgo` and `mine = yes` become `factory_type`s marked `extracts_deposit`. Precious metal is excluded until money and gold are split.
- An `assets.csv` row of kind `extraction` creates an establishment at a deposit's site and binds it through `factory_resource_deposit`.
- Its operator may extract if it is the deposit's operator or holds an active extraction right.
- Output passes through the normal factory path: labor, deposit limit, reserve depletion, site stock, shipment to the hub, ask, payroll.
- Open job offers recruit unemployed, work-eligible exact persons who live in the workplace province. This is the population's entry into the labor market for every employer.
- `process_rgo_output`, `update_rgo_production`, standalone `extract_resource`, and the RGO-seeded deposit bootstrap are deleted.

## Rural hard cut (implemented)

- Land titles are natural assets with area and per-commodity suitability, owned through ownership stakes (`land_titles.csv`).
- Farms are factories bound to one title (`farms.csv`) with output `A * s * L^0.6 * T^0.4`. Recipes come from content production types with `farm = yes`, calibrated from farmer life needs.
- Leases (`leases.csv`) let a tenant farm a title, with cash rent every 30 days and the owner's output share delivered in kind. A leased title is farmed only by its tenant; otherwise only by its owner.
- Firms neither build greenfield farms nor acquire them, and generic factory projects reject farm recipes.

Capital projects follow the same boundary: `create_extraction_plant` builds a plant on an existing deposit whose operator or right holder is the operating company. Construction never creates a deposit, reserves, or grade.

Deferred, in this order:
1. land titles and agriculture;
2. renewable stocks;
3. household cohorts with in-kind consumption and the labor return path;
4. `use_right` rent and subsoil law;
5. splitting money from gold;
6. POP roles as projections;
7. removal of legacy RGO fields and GUI;
8. artisans as household establishments.

A tool to author canonical CSV data from content is a separate prerequisite for full-world runs.
