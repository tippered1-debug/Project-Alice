# Legacy POP Consumer Economy Purge / Hard Cut

Audited base: `main`, `baa8648f4` (Make exact persons canonical consumers).
Scope: remove the parallel POP consumer engine while retaining the existing exact-person monetary and physical chain.

## Reachability and disposition

Categories: A = runtime causality, B = statistics/projection, C = UI/tooltips, D = scenario/scripts, E = dead/unreachable. Classification describes the audited base before this patch; the last column describes the result.

| Function or callsite | Class | Source/caller | Disposition |
| --- | --- | --- | --- |
| `rebalance_needs_weights`, `need_weight_change` | A | `economy::daily_update`, per-market phase | Removed, including the daily phase. Frozen scenario weight initialization remains explicitly named `initialize_compatibility_needs_weights`. |
| Daily life/everyday/luxury costs, invention counting used only by that costs pass | A compatibility machinery | `economy::daily_update` | Removed. Canonical needs and purchase prices are exact-person/concrete-market inputs. |
| `prepare_pop_budget`, `prepare_pop_budget_templated` | A/B/C | `demographics::get_estimated_literacy_change`; monetary statistics; `alice_ui::describe_lit/describe_money`; budget details | Deleted. Literacy uses existing education-ministry labor contracts; statistics and tooltips use exact ledger and fills. |
| `investment_rate`, `bank_saving_rate`, `adjusted_subsistence_score`, safe budget ratio helpers, vectorized POP budget types | Transitive A/B/C; E after deleting budget callers | Internal POP budget engine | Deleted with `economy_pops_constants.hpp`. No canonical firm, capital-project, or banking caller used these rates. |
| `estimate_next_day_raw_income`, `estimate_next_day_budget_before_taxes`, `estimate_total_wage` | B/C internal dependencies; unused forecast helper E | Old taxation/budget forecasts | Deleted; tax spending is recorded tax payments, not projected income times tax rate. |
| `estimate_slave_income` overloads, `estimate_artisan_income` overloads, `estimate_local_trade_income`, `estimate_trade_income` | B/C | Monetary breakdown; slave/artisan/trade budget rows | Deleted. Unbacked legacy income rows show zero. No such transfers are invented. |
| `estimate_income_from_nation`, `money_from_nation` | C | Pension, unemployment and national-income rows | Deleted; aggregate public transfers come from typed exact transactions. Unsupported pension/unemployment breakdowns show zero. |
| `estimate_trade_spending`, `estimate_tax_spending` | B/C | Market investments estimate; budget detail tax/trade spending | Deleted. Capital contributions and taxes use actual exact transactions. Market statistics no longer add synthetic POP investment or dividends. |
| `estimate_pop_demand_internal_life/everyday/luxury` and `estimate_pop_spending_life/everyday/luxury` | B/C | Monetary statistics; commodity spending rows | Deleted; `projected_spending` aggregates current-day exact fills for each category. |
| `estimate_wage(pop)` | B/C | Monetary breakdown; budget wage list | Deleted. `projected_payroll` reports actual current-day payroll. UI labels the total as payroll received. |
| `estimate_wage(province,type,accepted,size)` | A advisory read | Demographic migration opportunity report | Retained as `compatibility_wage_opportunities`: projected labor-price opportunities, never a payment, account balance, budget or purchase allocation. |
| Expected life-needs coverage | A demographic advisory | `demographics::expected_life_needs_coverage` | In an exact runtime, reprices existing person need quantities at concrete reference prices; legacy needs costs/subsistence are only a pre-bootstrap compatibility fallback. Does not consult savings, satisfaction or market needs weights. |
| `estimate_pops_consumption` | B | Physical consumption/statistical callers | Retained as actual current-date consumed quantity, with stable person/commodity ordering. `nation_pop_consumption` now aggregates this source. |
| `market_cut` declaration, POP savings debug support, old artisan-wage template, unused POP minimum wage helpers, empty profiler and commented savings/budget diagnostics | E | No live consumer caller; no-op instrumentation | Deleted; the existing external `sanity_check` compatibility hook remains empty, with canonical domain checks in their original owners. |
| Scalar and vector `pop_demographics::get_*_needs` | A/B/C/D read interface | Demographics, UI, interpreted triggers | Exact runtime reads three independent physical consumption ratios. Raw encoded satisfaction is a pre-bootstrap fallback only. |
| FIF `tf_life_needs/tf_everyday_needs/tf_luxury_needs` | D script access | Compiled/interpreted script support | Uses the same native `consumption-ratio` query. No raw satisfaction decode after activation. |
| Budget chart satisfaction, financial and literacy tooltips | C | `budgetwindow`, `alice_ui`, `pop_budget_details` | Exact consumption, actual payroll/purchases/transfers/taxes/equity, and existing institutional teaching capacity. Removed synthetic subsistence, deposit, education-spending and investment forecasts. |
| Raw `pop_get_savings` in population tables/sorting, ecodump and `simulation_runner` | B/C | Display and validation only | Retained output readers. `project_population_cash_balances` is the sole production writer; exact person accounts remain authoritative. |
| Raw satisfaction in `simulation_runner`, scenario initialization and synthetic fixtures | B/D | Output validation and imported fixture metadata | Retained; never a consumer input. Canonical daily consumption and save restore overwrite the projection. New mobility POPs start with an empty projection, rather than copying another POP's bar. |
| Authored POP needs | D bootstrap; B/C category metadata | `household_mobility` profile import; UI category mapping | Existing import marker retained and tested. Imported needs cannot be overwritten by automatic reimport. Category labels may use authored metadata, without multiplying bids by category weights. |
| Market need weights | D/C | Scenario initialization; compatibility POP needs display | Frozen fields, no daily rebalancer. Canonical consumer loop does not read them. |
| `expected_probability_to_buy` | B/advisory production compatibility | Concrete-market projection and old producer/UI availability estimates | Retained as a market availability read model; POP utility/budget optimization was deleted. Existing producer code is outside this consumer purge. |
| AI economy | A | Firm decisions and existing industrial/capital-project paths | No POP savings/budget/rate caller found. Existing owner/account-based paths retained. |
| CPI / price level | B | `price_level::measure_basket` | Already reads exact person need records and aggregate concrete activity; no consumer-engine rewrite needed. |
| Payroll, exact accounts, exact goods purchase/matching, freight, banking, money ontology | A | Existing canonical domains | Operational chain retained. Added const record accessors and a derived consumption cache only. |
| Normal save/load | B persistence | Exact-runtime extension restore | Rebuilds POP cash and physical consumption outputs after validated restore, without importing authored needs or changing canonical data. |

## Canonical chain after the cut

Existing jobs/contracts and payroll → existing household mobility → exact-person bids → concrete market fills and seller receipts → exact freight → home stock → physical consumption → read models.

The new category cache is derived, excluded from serialization and checksums, stable in person/commodity order, and invalidated by consumption, period changes, deaths and membership changes. It never posts bids or transfers money. Scalar, SIMD and script queries read this same source. The compatibility `pop.satisfaction` field is an aggregate output; the three categories are queried independently.

Money projections only aggregate the canonical default settlement; different currencies are not silently added. Const need/fill/transaction record views avoid copying the entire runtime for each UI query.

## Investment and capital remainder

No residual adapter of the form `capitalist/aristocrat/farmer POP savings × rate → funds` remains in the consumer engine. `actors::ownership::contribute_equity_to_factory` calls concrete owners subject to their cash accounts and transfers funds. `capital_projects` funds/acquires materials through sponsor/project accounts and physical stock. `firm_agency`/industrial dynamics retain their current behavior.

Retained, separate ownership/construction work: `industry_ownership` group-share analysis (`clear_market`, `split_dividend`, historical distributions), factory/RGO priority-token reports, construction and expansion policies. These do not read POP savings or resurrect the removed POP consumer investment path. This patch neither implements a new investment system nor performs the separate construction/housing/capital hard cut.

## Save/load issues found by the required regression

1. `exact_runtime_payload_size` omitted the participation-overrides count and its records, underallocating the save buffer by at least four bytes. It now accounts for exactly what the existing writer emits; the test includes a nonempty override.
2. `read_exact_runtime_save` reset flags already read from preceding politics/legislation sections. The restore gate consequently rejected otherwise-valid exact snapshots. Preserve those flags across the runtime snapshot reset.

Neither correction changes the emitted record format or adds purchasing behavior.

## Regression coverage

Standalone `consumer_purge_tests` uses current APIs and the same engine, GUI, platform and linked support sources as the existing tests target. It covers:

- Four independent corruption sections (savings; satisfaction; weights/availability; combined), identical canonical worlds, 12 payroll/mobility/consumer days and equal household checksums each day.
- Exact balance caps purchases; an actual contract wage restores purchasing power; seller revenue and physical inventory/stock/consumption conservation.
- Exact cash projection after purchases; actual fill spending and independent physical category bars; native and FIF interpreter parity; projection queries and output writes preserve the canonical checksum.
- One-time authored profile import despite subsequent legacy changes.
- Normal save/write/load and 8 further replay days, with poisoned legacy fields and nonempty labor participation overrides.
- Account/need insertion order invariance.
- Actual institutional teaching contracts determine education access and literacy independently of legacy wealth/satisfaction.

Validation results: all 7 cases passed, 1,355 assertions. The independent executable was compiled and linked with the existing macOS ARM64 build dependencies; modified engine and GUI translation units were rebuilt, with no dependency download. `git diff --check` passes.

The older omnibus `tests_project` contains pre-existing obsolete API references (`fully_canonical`, deleted `concrete_labor`, old planned-quantity signatures, removed factory canonical flags). The independent target avoids those fixtures; this report does not claim that the entire legacy test suite builds or passes. No full scenario campaign or rendered UI smoke test is claimed.
