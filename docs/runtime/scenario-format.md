# Canonical Scenario Firms and Ownership

Every scenario must provide twelve firm, household, ownership, banking, resource, and land tables under `common/canonical_runtime/`: `firms.csv`, `capital_owners.csv`, `households.csv`, `deposits.csv`, `assets.csv`, `land_titles.csv`, `farms.csv`, `leases.csv`, `ownership.csv`, `loans.csv`, `banks.csv`, and `bank_deposits.csv`. The canonical land-force tables are also required; see [Canonical Land Forces](../architecture/military.md) for their schemas and validation rules. The files are required even when a table has no rows; an empty table contains its header only. Rows use UTF-8, semicolon separators, one header row, and `#` comment lines. Quoting is not supported. IDs use ASCII letters, digits, `_`, `-`, and `.`. A scenario with a missing file, missing actor, missing owner, or invalid ownership graph is rejected before simulation starts.

## Firms

`firms.csv` has columns `firm_id;kind;settlement;opening_cash;retained_earnings;paid_in_equity`. `kind` is `company`, `bank`, `fund`, `cooperative`, `state_entity`, or `other`. `settlement` is the exact commodity key from scenario content. Opening cash becomes the firm's account in that settlement. A firm operating several plants shares its company account across the portfolio. Retained earnings and paid-in equity are saved on the organization and updated by canonical firm profits and owner contributions. Bank rows must set `opening_cash` to zero; reserves and customer deposits come from the bank tables below.

Example row: `steel_co;company;money;12000;4500;8000`.

## Capital owners

`capital_owners.csv` has columns `owner_id;kind;reference;settlement;opening_cash`. Supported kinds are `government` and `person`. Government references are active three-letter country tags and resolve to the already bootstrapped central-government institution. Person references use `source_population_cell:ordinal` and must resolve to a live exact person already materialized by the scenario. The loader does not create a substitute owner. One row per government or person reference is allowed.

Example rows: `usa_treasury;government;USA;money;40000` and `founder_1;person;17:0;money;2500`.

## Resource deposits

`deposits.csv` has columns `province_id;commodity_id;original_reserves;remaining_reserves;grade;daily_capacity`. Each row creates one subsoil deposit at the province's site. `commodity_id` must be a content commodity whose production type has `mine = yes`; precious metal is rejected while it is the settlement unit. A province can declare only one deposit per commodity. Reserves must satisfy `0 <= remaining_reserves <= original_reserves` with `original_reserves > 0`, and `grade` and `daily_capacity` must be positive. `grade` scales output per worker. `daily_capacity` is the most the deposit yields per day. Province RGO values in history files create no deposits.

Example row: `253;coal;120000;120000;1;12`.

## Productive assets

`assets.csv` has columns `asset_id;kind;site_id;operator_id;province_id;building;ordinal;commodity_id;opening_value`. It gives every scenario factory, resource deposit, and extraction plant a stable ID, site, operator, and opening appraised value. `province_id` is the original numeric scenario province ID. A factory selector is `province_id/building/ordinal`: `building` is the exact building identifier from scenario content and `ordinal` is one-based within that province and building type, ordered by its authored scenario instances. Deposit selectors are `province_id/commodity_id`. Factory rows leave `commodity_id` empty; deposit rows leave `building` and `ordinal` empty. `site_id` is an authored stable key; co-located factories and deposits use the same key. Every factory and deposit instance created by the scenario must occur exactly once.

An `extraction` row creates an extraction plant on the deposit selected by `province_id/commodity_id`. `building` is the content production type that extracts that commodity (for example `coal_mine`), and `ordinal` is empty. The plant is an ordinary factory at the deposit's site, sized so full capacity extracts the deposit's `daily_capacity`. A deposit can host one plant. The plant extracts only while its operator is the deposit's operator or holds an active extraction right, and it takes part in labor, loans, and ownership like any factory.

Example rows: `us_pa_coal;deposit;pittsburgh;steel_co;253;;;coal;9000` and `us_pa_coal_mine;extraction;pittsburgh;steel_co;253;coal_mine;;coal;4000`.

Example factory row: `us_pittsburgh_steel;factory;pittsburgh;steel_co;253;steel_factory;1;;18000`.

## Households

`households.csv` has columns `household_id;province_id;role;settlement;opening_cash`. A household is the shared budget of the rural people of one province: role `peasant` covers farmers and labourers, role `landed` covers aristocrats. Members are not listed. Every living person of the role in the province belongs to the household unless that person holds individual economic records (a hired worker, for example) or serves in a military formation. A province can declare one household per role, and every province with living people of a role must declare its household. A household has no shareholders; it can own land, operate farms, and hold leases.

Example rows: `pa_peasants;253;peasant;money;0` and `pa_gentry;253;landed;money;2500`.

## Land, farms, and leases

`land_titles.csv` has columns `title_id;site_id;province_id;area_hectares;suitability;opening_value`. Each row creates one land title, a natural asset at the province's site. `suitability` lists `commodity=factor` pairs separated by commas (for example `grain=1,cattle=0.6`); a title must suit at least one commodity. `title_id` is an asset ID: `ownership.csv` gives the title its owners, and the holder of a majority voting stake is its owner.

`farms.csv` has columns `farm_id;title_id;production_type;operator_type;operator_id;opening_value`. Each row creates a farm: an ordinary factory bound to one title, using a content production type with `farm = yes`. `operator_type` is `firm` or `household`. A peasant household works its own farms with its members' unpaid labor, keeps its harvest, eats from it, keeps 60 days of its needs, and sells the rest; a firm or landed household hires workers. A title hosts one farm. `farm_id` is an asset ID owned through `ownership.csv`. Output follows `A * suitability * L^0.6 * T^0.4`, where `T` is the area in reference holdings of 5 hectares per worker. Farm recipes are calibrated at load so that a worker on a reference holding yields 1.5 times the value of the farmer life needs of the four people one worker represents.

`leases.csv` has columns `lease_id;title_id;tenant_type;tenant_id;settlement;cash_rent_per_hectare_year;output_share;valid_from;valid_until`. `tenant_type` is `firm`, `capital_owner`, or `household`. A lease lets its tenant farm the title instead of the owner. Cash rent is paid every 30 days from the tenant's account, and what the tenant cannot pay accrues as unpaid rent. `output_share` (from 0 to 1) is the owner's share of each harvest, delivered in kind at the farm site. Leases on one title cannot overlap, and the tenant cannot be the owner.

Example rows: `pa_wheat_land;pittsburgh;253;400;grain=1,cattle=0.5;6000`, `pa_wheat;pa_wheat_land;grain_farm;firm;penn_estate;1500`, and `pa_wheat_lease;pa_wheat_land;firm;penn_tenants;money;2;0.25;1836-01-01;1846-01-01`.

## Ownership graph

`ownership.csv` has columns `asset_id;owner_type;owner_id;ownership;voting;economic`. `asset_id` refers to an `assets.csv` productive asset or a firm's synthetic equity asset `equity:<firm_id>`. `owner_type` is `firm`, `capital_owner`, or `household`. Each asset's ownership, voting, and economic fractions must each sum to 1 within floating-point tolerance. Owners cannot be duplicated within one asset. A `state_entity` firm's equity asset must include an explicit government owner. No ownership row is inferred from province control or legacy producer state.

Example rows: `us_pittsburgh_steel;firm;steel_co;1;1;1`, `equity:steel_co;capital_owner;founder_1;0.7;0.7;0.7`, and `equity:steel_co;capital_owner;usa_treasury;0.3;0.3;0.3`.

## Opening loans

`loans.csv` has columns `loan_id;asset_id;creditor_type;creditor_id;principal;annual_rate;creation_date;due_date;accrued_interest;collateral_value`. It loads only declared bank-to-firm obligations and does not credit cash a second time. `creditor_type` must be `firm`; `creditor_id` must name a declared bank using the debtor's settlement commodity. Dates use `YYYY-MM-DD`; creation cannot predate the campaign start or postdate the selected scenario date. Imported accrued interest is the opening value, so interest accrual starts from the selected scenario date. The loan is linked to the operator actor and exact factory asset; listed principal is not added to any account balance. `collateral_value` is the authored value pledged to that loan, not an inferred recovery or cash balance.

Example row: `steel_mortgage_01;us_pittsburgh_steel;firm;clearing_bank;5000;0.06;1836-01-01;1846-01-01;120;9000`.

## Banks and opening deposits

`banks.csv` has columns `bank_id;jurisdiction;settlement;opening_reserves;opening_equity;opening_deposit_liabilities;lending_base_rate;minimum_capital_ratio;liquidity_target;risk_appetite;lending_spread;max_single_borrower_exposure;reserve_requirement;capital_breach_grace_days`. Each `bank_id` must reference exactly one `firms.csv` row with `kind=bank`. `jurisdiction` is an active three-letter country tag; `settlement` must equal the bank firm's settlement commodity. Every policy value is required and saved on the bank. Ratios and spread are authored fractions from 0 to 1; no fallback rate or hidden lending limits are applied.

`bank_deposits.csv` has columns `deposit_id;bank_id;owner_type;owner_id;opening_balance`. `owner_type` is `firm` or `capital_owner`, and each account owner is explicitly declared in the corresponding table. A bank can have one customer deposit account per owner. The sum of each bank's account balances must equal `opening_deposit_liabilities` in `banks.csv`.

Opening equity is derived from the complete balance sheet after reserves, deposits, and declared loans load. It must equal `opening_equity` in `banks.csv` and `paid_in_equity + retained_earnings` in `firms.csv`. A mismatch rejects the scenario with the bank ID and the authored and derived values. The loader never creates a balancing asset or liability.

## Stable identity and validation

Stable IDs are deterministic 64-bit hashes of the authored keys and are saved on firms, actors, accounts, sites, factories, deposits, assets, ownership stakes, institutions, and obligations. Entity creation is sorted by authored ID. Entities created later by a capital project receive stable runtime IDs from their entity type, creation date, and DCON index. The runtime checks that every factory and deposit has one operator, one distinct asset, a complete owner graph, and stable identity. A failure names the DCON factory/deposit and site IDs and stops scenario generation. Legacy province producer debt and ownership shares are excluded from simulation, and factory subsidy state is cleared and ignored; none of these fields fill missing canonical rows.

Canonical research and factory-process capability tables are documented in [Technology and research](../architecture/technology.md). The presence of `capabilities.csv` activates that data-driven runtime and requires all technology tables.
