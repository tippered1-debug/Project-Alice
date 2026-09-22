# Alice: Age of Transformation

`Age of Transformation` is Project Alice's opt-in political-economy ruleset. Its
core loop is deliberately small and explainable:

```text
production and wages
  -> income, savings and property proxies
  -> interest-group political power
  -> governing coalition and legitimacy
  -> local policy execution
  -> new economic and political outcomes
```

The ruleset is selected with the `Alice: Age of Transformation` game rule. It is
disabled by default. When disabled, interest-group updates and their gameplay
effects are no-ops, and existing Victoria 2 parties, elections, issues, reforms,
events and decisions retain their legacy behavior.

## Compatibility contract

- Existing parties remain the public government identity and the ruling party is
  the coalition anchor.
- The incumbent coalition, its establishment date and cabinet confidence are
  stored in a versioned handwritten save extension. Older saves without that
  extension rebuild a new cabinet deterministically on the first refresh.
- Interest groups are aggregate national views, not hard membership stored on
  every POP. Migration, splitting and merging therefore cannot leave dangling
  political membership.
- Derived diagnostics are never serialized. Physical state that must survive a
  checkpoint may be serialized only as an explicit schema change. The cargo
  pipeline is such a change: its two directional commodity arrays require a
  regenerated scenario and saves written by the new build.
- The master game rule reuses the reserved hardcoded-game-rule slot, preserving
  the size and ordering of the handwritten scenario section.
- Existing tax and construction paths already apply administrative control and
  must not be multiplied by it a second time.

## Stabilization gate

The vertical slice may be expanded only after all of these hold:

1. Migration opportunity uses the same multi-tier wages, employment and target
   culture acceptance as actual POP income.
2. Labor ratios never evaluate a masked divide by zero, and all migration weights
   are finite and bounded.
3. Army logistics performs at most one route search per army and one per depot per
   daily cache build; UI reads do not rebuild simulation state.
4. Old saves without logistics fields receive full initial reserves and normal
   priority through an explicit load migration.
5. A bounded headless runner can emit JSONL snapshots and fail on invariant
   violations.
6. Fixed-input runs reproduce exactly across separate processes. Checkpoint
   continuation must preserve every material stock and flow; the strict full
   checksum comparison remains a release gate while reconstructed legacy caches
   still differ immediately after load.

## Interest groups and political power

The first release uses stable archetypes derived from existing POP types. Each
group reports population support, political power, wealth, income security,
property, literacy and consciousness. Wealth and property inputs are capped and transformed
through bounded signals before normalization so a single rich POP cannot overflow
the model.

Political power is based on:

```text
population weight
  * group affinity
  * bounded per-capita power from wealth, income security, property,
    literacy and consciousness
```

Land ownership is persistent economic state in the flagship ruleset. Existing
province landowner and capitalist shares initialize old scenarios, then change
only through a capped monthly land market. Its accounting rules are:

1. The price capitalizes a 270-day (nine-month) exponential average of net RGO
   rent, with the existing dividend bank as a conservative initialization floor.
2. Buyers submit cash-backed bids and owners submit separate voluntary or forced
   asks; only their matched minimum becomes turnover.
3. POP bids may use only savings above six months of current life-needs costs.
4. Unmet life needs, unemployment and a lack-of-cash debt proxy create forced
   listings. Tenant protection comes only from an explicit tenant-law option;
   pensions and unemployment benefits no longer rewrite property contracts.
5. Rural land use is reported separately as owner-smallholders, tenants and
   landless laborers; these categories are not treated as interchangeable POPs.
6. Sale proceeds follow the seller class's persisted wealth/ownership proxy, not
   local population, so a numerous class cannot receive another owner's sale.
7. State and foreign holdings participate alongside smallholders, landed elites
   and capitalists. Their RGO dividends go to the domestic treasury or the
   investing nations in proportion to recorded foreign investment.
8. Property institutions use typed laws. Estate limits, tenant rights, worker
   buyouts and profit tax each have their own `alice_*` issue-option keys;
   universal voting has no property-law side effects. Existing construction and
   foreign-investment rules remain fallbacks only where they literally grant a
   right to build or invest.

The five shares remain bounded and sum to one, buyer payments equal seller
receipts, land-tax receipts go to the treasury, and classic games retain the
legacy immediate recalculation. New state, foreign, smoothing and turnover
values are derived runtime fields rather than a silent save-format change.

The typed-law content contract uses these issue-option keys: estates
`alice_estates_unrestricted` / `alice_estates_concentration_limit`; tenants
`alice_tenants_free_contract` / `alice_tenants_regulated_rent` /
`alice_tenants_secure_tenure` / `alice_tenants_right_to_buy`; industry
`alice_industry_open_market` / `alice_industry_nationalizing` /
`alice_industry_privatizing`; worker ownership `alice_worker_ownership_none` /
`alice_worker_ownership_buyout`; foreign capital
`alice_foreign_capital_prohibited` / `alice_foreign_capital_permitted`; and
separate `alice_land_tax_*` and `alice_profit_tax_*` bands (`none`, `low`,
`standard`, `high`). Collective bargaining accepts the corresponding
`alice_collective_bargaining_*` keys and maps Victoria's existing
`state_controlled`, `non_socialist` and `all_trade_unions` options explicitly.
If a scenario does not define a dedicated property-law issue, the conservative
default grants no expropriation, buyout or special tax power.

Industrial ownership is the counterpart of the land system. Project Alice's
factory object records a type, a size, employment and costs and nothing about
who it belongs to, so a capitalist's income was a dividend from an abstract
investment pool rather than a return on anything owned. Five shares per province
fix that: capitalists, landed elites, the state, foreign owners and the
workforce.

1. Capitalists are the residual of the four stored shares, so a save written
   before the fields existed loads as fully private industry. That is the right
   default and needs no migration pass.
2. Industry is priced by capitalizing a nine-month average of factory profit,
   the same window the land price uses. A loss-making industry is worth nothing
   rather than a negative amount.
3. The market clears monthly. Buyers commit only cash above six months of
   essential needs; holders list a small voluntary fraction plus a distress
   share driven by unemployment and unmet needs.
4. Factory dividends are split by these shares. The state's and foreign owners'
   portions are paid to the treasury and to the investing nations directly, and
   only the private remainder enters the POP dividend pool.
5. The legal layer is explicit: industry regime, foreign-capital access, worker
   buyout rights and profit-tax bands are separate typed institutions. The rich
   income-tax slider is no longer treated as a tax on asset value, and universal
   voting no longer creates industrial democracy by implication.
6. Expropriation is limited by what the treasury can actually pay, and every
   transfer is bounded by administrative capacity.

Foreign holdings also grow, not only shrink. While the law permits foreign
investment, the foreign share converges on what recorded investment in the
country would justify owning, and the capital buys in: local owners are paid and
the flow stops once the holding matches what was actually invested. Forbidding
foreign investment reverses it into a wind-down.

Opening positions are an explicit historical choice rather than a number derived
from whoever lives in the province: state-led (Russia, Japan, Turkey), foreign
concession (Persia, Egypt, China), landed industrialists (Prussia, Austria),
liberal private (Britain, USA, Belgium). Country tags are packed by
`nations::tag_to_int`; a lookup that disagrees with it silently misses and every
country falls back to unassigned, so the packing is covered by a test.

Ownership feeds politics rather than sitting beside it. A class's political
property standing is no longer a constant per POP role: it follows what that
class actually owns, blending the land and industry shares by their market
values, so a province with no industry behaves exactly as it did before. Worker
ownership therefore converts into political weight for organized labor, which is
the point of industrial democracy, and a government presiding over an economy
owned from abroad carries a bounded legitimacy penalty.

Shares stay bounded and sum to one, buyer payments equal seller receipts, and
classic games leave every value untouched.

**Adding these fields invalidates existing scenario files.** The province object
gained serialized properties, so a `.bin` produced before them cannot be read
and will fault during scenario loading. Regenerate with `-test`, which rebuilds
`development_test_file.bin` from the Victoria 2 data when it is absent, or
rebuild your scenario from the launcher.

Coalition selection is deterministic. Equal candidates are ordered by stable group
id. The smallest coalition reaching the configured power threshold governs. The
pure selector can model an incumbent, but the live ruleset derives its coalition
only from serialized society and economy state; unsaved hysteresis cannot make a
reloaded campaign diverge from a continuous run.

Legitimacy is an explainable balance of political-power mandate, popular support,
coalition cohesion, social breadth and majority status, with explicit minority,
representation-gap and fragmentation penalties. Every component and the final
value are bounded to `[0, 100]`.

## State capacity and execution

Policy execution is a read-only coverage calculation built on the existing
administration system:

- national administrative efficiency;
- normalized provincial `control_ratio`;
- policy funding;
- availability of bureaucratic labor;
- political compliance from legitimacy/coalition support.

Each policy declares funding and casework requirements. Cash coverage, staffed
office coverage and territorial coverage are complementary: a surplus in one
cannot replace missing benefit money, caseworkers or access to a province.
Political opposition increases casework instead of acting as a generic penalty.

Connected gameplay consumers now include:

- crime suppression and the share of a movement it can actually dismantle;
- public education delivery and literacy growth (private education is unaffected);
- pensions and unemployment-benefit delivery, with undelivered funds retained by
  the treasury instead of disappearing;
- the implementation gap that drives reform movements;
- provincial mobilization speed.

Assimilation is not part of this balance pass.

## Cities and human development

The first demographic expansion reuses the existing local housing, city,
education and literacy systems and introduces no serialized fields. In the
flagship ruleset:

- unmet urban housing demand creates an overcrowding pressure proportional to
  urbanization;
- overcrowding adds a bounded monthly growth penalty and militancy adjustment;
- literacy, education access and urbanization create an aggregate demographic
  transition that reduces natural growth without requiring age/sex cohorts;
- internal and colonial migration combine real life-needs opportunity with a
  bounded housing-availability multiplier;
- classic games return before reading the new service inputs and remain an exact
  numerical no-op.

The POP growth tooltip exposes housing access, urbanization, overcrowding,
demographic transition and the local human-development index. Headless JSONL
snapshots add a demographic account with baseline natural growth, starvation,
housing loss, transition reduction, net natural change, population-weighted
development inputs and gross migration flows. These values make the first balance
pass observable without changing save compatibility.

## Reforms, movements and political conflict

Each issue option now has an explainable position for every interest group. POP
issue support is weighted twice: once by population and once by the wealth,
property, literacy and organization model that creates political power. A reform
therefore exposes popular support, electoral support, political-power support
and governing-coalition support as distinct values.

Under the flagship ruleset, political and social reform eligibility requires a
50% composite mandate: 40% political-power support, 30% coalition support, 15%
electoral support and 15% popular support. AI governments rank eligible reforms with the same signals,
movement pressure and expected implementation capacity. Legacy upper-house rules
remain unchanged when the flagship rule is disabled.

Political and social reforms are proposed as one visible bill at a time. A bill
passes through negotiation, a parliamentary vote and implementation; the
negotiated coalition combines POP-backed coalition support, the ruling party's
position and explicit concessions. A bill may be withdrawn during negotiation or
voting through the same command queue used by the rest of the game, with a small
cabinet-confidence cost. Once implementation starts, withdrawal is no longer
available. The active bill, sponsor, stage, elapsed days, mandate, compromise,
party support and execution readiness are shown in the politics UI and saved in
a framed, backward-compatible extension.

Economic and military reforms use the same bill state. Their research-point
cost is checked when the bill is opened and again before implementation, so a
long negotiation cannot silently spend research points that no longer exist.
AI nations use the same path, which prevents the player and AI from following
different political rules.

Civil conflict also creates an emergency-concession route. The population and
radicalism of a movement demanding the selected issue, or an uprising's rebel
membership and territorial occupation, produce a separate bounded pressure
score. At 50% pressure a progressive issue bill can obtain an emergency mandate
floor and negotiate faster even when normal elite support is insufficient. A
successful settlement lowers militancy most strongly among its supporters and
removes those supporters from future rebel recruitment, but it never despawns
armies already in the field. General rebellion pressure cannot be used to roll
political rights backwards.

The politics window also shows the electoral reach of the population, so a
large gap between mass pressure and formal representation is visible instead
of being hidden inside a single vote percentage. Enacted reforms feed a small
bounded confidence adjustment back into the incumbent cabinet: supportive
groups become more secure, while a narrow power-based reform costs confidence.
If cabinet confidence falls below the crisis threshold and a credible challenger
exists, coalition hysteresis is overridden and the government turns over.

Election results now feed the same cabinet state. The winning party's actual
vote share is stored as its electoral mandate, including when an incumbent is
reelected. Version 1 saves migrate with an unknown mandate and use the old
ideology proxy only until a real election is observed. A low-mandate party is
visible as a contested or fragile cabinet.

Industrial organization is also state-backed. Factory-worker membership in an
existing movement, literacy, consciousness and the explicit collective-
bargaining law determine organization. A strike requires organization, unmet
needs or unemployment, and militancy simultaneously; its result is unavailable
labor in the labor market, capped at 25%, rather than an abstract throughput
modifier. Classic rules remain an exact no-op.

Movement radicalism adds a bounded, inspectable adjustment from political-power
backing, opposition to the coalition, low legitimacy, member hardship and the
reform-implementation gap. It also measures the provinces where the movement
is concentrated: weak local control and weak regional execution add pressure
even when the national average looks healthy. Failed suppression creates
additional backlash and militancy; effective state capacity removes a larger
deterministic share of the movement. Radicalism still enters the existing movement-to-rebel pipeline at the
normal threshold, so this extends rather than replaces Project Alice's rebellion
system.

## Markets, inventories and logistics

Age of Transformation replaces the single proportional purchase probability
with an aggregate daily call auction. It does not create an order object for
every POP or factory: demand is grouped by market, commodity and economic
purpose, which keeps clearing bounded and deterministic. The current classes are
life, everyday and luxury consumption, industrial inputs, government,
construction, inventory replenishment and trade.

Each class submits a quantity and an economically meaningful reservation price.
Offers and bids cross at one marginal quote; equal-price orders retain a stable
order. Scarcity therefore has distributional consequences. Food and required
inputs can clear while luxury or inventory demand remains unfilled instead of
every buyer receiving the same fraction. Class fills feed back into POP need
satisfaction, production, construction and trade. The quote is an allocation
diagnostic, not a second price controller: the existing smoothed aggregate
supply-and-demand curve remains the sole daily price-discovery path. This avoids
a thin classified auction systematically pulling all prices downward.

Physical goods no longer share one implicit storage and transport profile. A
commodity profile defines cargo weight, spoilage, storage/handling cost, target
inventory days and the maximum safe daily inventory release. Bulk RGOs consume
more freight capacity and perishable goods carry a larger holding loss. Merchants
release stock against expected consumption and a target buffer rather than a
fixed fraction of whatever happens to be stored. A shortage can be covered
immediately up to the commodity's safe daily release cap, while surplus stock is
unwound over a 90-day horizon. This keeps inventories useful without turning a
large initial warehouse into artificial daily production.

The logistics chain is split into three distinct constraints:

1. Provincial market access combines distance, rail, control, transport labour
   and local congestion. Weak access reduces the realized farm-gate or
   factory-gate value and raises the logistics risk used by investment ranking.
2. Inter-market freight consumes either land or sea capacity. Rail cannot stand
   in for a missing port, and naval capacity cannot clear an inland bottleneck.
   Routes sharing an endpoint are allocated with deterministic max-min fairness.
3. Delivered trade enters a directional cargo pipeline. Travel time follows
   route distance and mode; only the daily arriving share becomes a market
   import, while the remainder stays serialized in transit and may spoil.

Foreign trade also has a settlement constraint. Export receipts finance imports
first, followed by a bounded draw on the importing nation's liquid reserves.
Merchant bills of exchange can bridge up to 20% of a daily bill through the
existing signed market-cash account; they prevent a reserve-poor importer from
dropping instantly to zero without removing the external constraint. When these
sources are insufficient, only the foreign part of requested cargo is scaled
down and an exchange-rate premium records the shortage. Domestic shipments do
not consume foreign settlement capacity.

Private and state construction now use separate project rankings. Both consider
expected revenue, input and wage costs, sell-through and logistics reliability.
Private capital applies stronger interest and demand-risk penalties; public
capital assigns explicit value to employment, strategic shortages and reducing
import dependence. Candidate order is stable, so the distinction is policy and
risk tolerance rather than a random factory roll.

All of these behavioral paths are gated by the master rule. Classic clearing,
immediate delivery, inventory release and investment selection retain their
legacy paths. Because cargo in transit is real checkpoint state, this development
schema change invalidates previously generated scenario binaries; rebuild the
scenario before starting a campaign with this version.

## Money supply and credit

The money supply is an accounted quantity. Nine stocks hold every unit of cash
in the world: POP savings, market cash, national treasuries, `national_bank`,
`private_investment`, the three producer tills (`rgo_bank`, `factory_bank`,
`artisan_bank`) and advanced province building private savings. The list is
every `tag{save}` money float in the data container; anything left out reads as
money vanishing whenever it is paid into. Each day the books are closed:

```text
expected supply = yesterday's supply + gold actually minted today
residual        = observed supply - expected supply
```

Gold emission is recorded by the money-RGO payout itself, so the figure is what
happened rather than an estimate. Market cash, the producer tills and the
residual are signed: merchants and firms may run a net overdraft, and money can
go missing as easily as it can appear. A **negative net money supply is normal
vanilla behaviour**, not corruption, and is never treated as an invariant
failure.

The accounting changes no balance, in any mode. An earlier version renormalized
the supply to absorb the residual; measurement on a real scenario showed that
was the wrong instrument and it has been removed. What it was compensating for
is described below.

### Consumer prices, real wages and inflation

The flagship ruleset no longer calls the legacy `0.999` daily destruction of POP
savings and market cash "inflation". Classic games retain that factor exactly;
Age of Transformation uses a neutral balance multiplier of one and observes
inflation where it actually happens: in goods prices.

Each local market receives a consumer price index. Its basket is the
population-weighted life and everyday needs of the POP types living in that
market, using the adaptive needs weights already chosen by the economy. Luxury
needs are excluded so a rich consumption boom cannot redefine the subsistence
cost of living. Current basket expenditure is divided by the same quantities at
commodity base costs, making `1.0` the scenario's real-price reference.

The basket is frozen after needs weights rebalance and immediately before the
daily commodity price update. Closing CPI uses the same quantities at the new
prices:

```text
market CPI       = current basket cost / base-cost basket
daily inflation  = closing CPI / opening CPI - 1
real wage        = nominal local labor price / market CPI
```

Because commodity prices already react to effective demand and available
supply, the measured rate is causal: income, credit, gold-financed public
spending, shortages and transport constraints affect purchases; purchases and
supply affect local prices; local prices change CPI and real wages. A
basket-weighted excess-demand diagnostic in `[-1, 1]` distinguishes demand-pull
pressure from slack. Market results aggregate to national and world CPI by
population and are emitted by the headless runner. The opening and closing
samples belong to one tick, so the derived account adds no save field.

`--money-audit` reports, for each named phase of the economy day, how much money
appeared or vanished inside it. It measures every stock at every phase boundary,
so it is only usable on short bounded runs, but it attributes creation to a
phase instead of leaving it as a daily aggregate.

### Unbounded producer credit

Producer tills have no floor in the base game. A firm pays wages and buys inputs
from a balance that simply goes more negative, forever, at no cost. Household
savings are its mirror image, one for one: on a real scenario the positive
stocks and the producer overdraft each reach millions while the net supply stays
near a hundred thousand and then crosses zero. That is the largest single
distortion in the model, and it creates credit rather than net money.

The flagship ruleset routes a negative till through the bank instead:

- The loan rate is a price. It rises with the share of bank assets held as
  outstanding claims and with financial stress, and is derived from serialized state, so
  a continuous run and a resumed save price credit identically.
- The asset statement separates cash reserves, government bonds, investment
  loans and producer loans. Existing claims raise utilization and rates, but are
  not subtracted from cash a second time.
- The bank lends free reserves to the investment pool when private construction
  cannot fund itself, and to producer tills that have overdrawn, keeping a fixed
  reserve share unlent. A bank in poor health lends less and charges more.
- What it lends becomes an outstanding claim it collects on, not a one-day flow.
- Deployed capital services the bank daily, so lending returns as reserves.

Every credit movement is a transfer between stocks that already exist, so it
creates and destroys no money. With the flagship rule disabled the rate is the
legacy base rate and no lending occurs.

On a real scenario the bank is far too small to fund the deficit: it reaches a
few hundred per day against an unfunded deficit in the hundreds of thousands.
The unfunded remainder is an explicit, reported number
(`credit.producer_unfunded`) instead of an invisible free overdraft.

The loop is closed on the employment side. The share of the deficit that could
not be financed contracts factory hiring the next day, capped at a few per cent
per day so a credit squeeze is a squeeze rather than an instant collapse. That
is what turns the till from an accounting line into a budget constraint: a firm
that cannot finance its losses has to shrink.

Borrowing also has a memory. An advance joins a persisted producer loan book
rather than vanishing the day it was made, and every day that book is serviced
before any new lending happens:

- interest accrues on the outstanding stock at the policy rate;
- a firm pays interest first, then amortizes principal, and only ever out of a
  till that is in surplus;
- interest a firm cannot pay capitalizes into the principal, which is what makes
  a debt spiral possible without anyone deciding to lend more;
- interest and repayment move from the till to the bank, so the loan book is a
  claim and never a source of money.
- writing off a bad claim removes that claim; it does not destroy reserves a
  second time after the original advance already left the bank.

That stock is what a credit cycle needs: cheap credit funds expansion, the book
grows, servicing costs rise with it, and a firm that stops covering interest
compounds its way into contraction.

Fiscal cash now circulates under a stock-flow rule. Budget sliders are still
bounded by recurring daily revenue, but a treasury retains only a 90-day revenue
buffer as its operating reserve. Cash above that buffer is added to the daily
budget gradually over 365 days. This neither creates money nor spends the entire
treasury at once; it prevents permanent surpluses from becoming a one-way demand
sink. A 180-day release was tested and rejected: it bought little additional
employment while reducing cargo delivery and accumulating larger financial
balances.

A deterministic ten-year run on `development_test_file.bin` shows the intended
trade-off. The residual 10% transport connectivity prevents a temporary zero in
port staffing from permanently isolating a market. The flagship produces a much
denser industrial network, but constrained working capital and real delivery
delays leave employment and subsistence consumption below classic at this
balance point:

| after 10 years | factories | unemployment | life needs | nominal GDP |
| --- | ---: | ---: | ---: | ---: |
| classic | 231 | 32.8% | 88.7% | 73,095 |
| flagship | 916 | 45.8% | 77.2% | 116,380 |

The flagship's closing CPI is `1.089`, avoiding the previous deep-deflation
endpoint. Its cargo delivery rate reaches 93.5%, producer debt ends at 21,469 and
daily write-offs reach zero by the final snapshot. In the one-year check it
records GDP 271,144, 43 factories and 77.7% unemployment, versus classic's
228,631, 32 and 75.7%. These are checked
balance baselines, not targets or a promise that every scenario will produce the
same ratios.

The persisted producer debt is serviced from positive tills: interest is paid
first, principal amortizes next, and unpaid interest capitalizes. Only cash
actually transferred from the bank creates a bank claim; the unfinanced part of
a negative producer till remains a separately reported operating deficit. This
keeps the bank asset statement in double-entry balance instead of recognizing
the whole requested shortfall as a loan before any money was advanced.

## Reproducibility

Repeated headless runs of the same scenario, seed and arguments are reproducible.
`scripts/soak_determinism.py` measures this across separate processes rather
than by eyeballing two runs:

```sh
python3 scripts/soak_determinism.py \
  build/macos-arm64-release/Alice development_test_file.bin \
  --cwd /path/to/Victoria2 --repeats 8 --days 120 --snapshot-every 10
```

The ARM64 divergence had two independent causes. Temporary port-reduction
buffers were read before their first write, and the scalar vector backend
converted zero-is-null entity IDs to raw values twice. Tagged gather/store then
addressed the preceding entity, so allocator contents leaked into every economy
subsystem. The buffers are now explicitly seeded and scalar tagged indices use
the same object-index convention as the SIMD backends. Null nation, market and
pop-type lanes are also guarded before array access. A six-process, 120-day
flagship soak now produces one checksum sequence in all six runs, and the
loaded-scenario unit gate compares every one of its first ten ticks exactly.

## Simulation observability

The bounded runner advances the same `single_game_tick()` used by interactive play.
Snapshots include date/tick, a 256-bit checksum of the serialized save state,
population, savings, employment, nominal and real labor prices, local CPI and
daily inflation, treasuries/debt/banks,
administrative control, logistics reserves, political legitimacy, banking
stress, auction turnover and unfilled demand, requested/delivered/in-transit
cargo, land and sea capacity demand, foreign-settlement pressure, trade
congestion, crisis pressure, demographic accounts and invariant
failures. The `money` account reports the nine stocks, the expected and observed
supply, gross positions and their ratio to the net, recorded gold emission, the
residual, the applied correction and whether it was clamped or suppressed; the
`credit` account reports policy rate, utilization, the government share of it,
lending capacity and credit actually extended. Political telemetry also exposes coalition power, cabinet stability,
member confidence, stable/contested/fragile government counts and coalition
changes. JSONL fields use a stable order to make output diffable.

Recommended automation:

- pull requests: unit tests and a one-year smoke run;
- nightly: fixed-seed 10–30-year run with invariant failure enabled;
- weekly: 1836-to-end run, save/load at midpoint, material-account continuity
  checks and a paired-state checksum diagnostic.

When no scenario `.bin` or Victoria 2 data is available, use the built-in
synthetic model lab:

```sh
Alice --synthetic-lab --days 365 --snapshot-every 30 \
  --report-jsonl alice-lab-year.jsonl
```

The lab creates a deterministic micro-world in memory and exercises economy
telemetry and Transformation politics under a controlled demand cycle. It does
not run map, military, event or world-AI pipelines; those require definitions
from a real parsed scenario. This makes the lab suitable for formula,
observability and invariant regression, while a real `.bin` remains the
full-world integration gate.

The summary script includes the tracked country's legitimacy, coalition power,
government stability and weakest cabinet-member confidence, so long runs can be
inspected without opening the client. It also prints the money supply, the
residual as a share of it, the applied correction, the household share of all
cash, and the average loan rate, credit utilization and credit extended, which
together make a monetary regression visible in a single column scan.

In the interactive client, the treasury tooltip exposes the same national
control surface when the flagship rule is active: nominal and CPI-deflated GDP,
CPI and daily inflation, the annual credit rate, bank utilization and lending
capacity, producer debt, the day's unfunded loss and the resulting employment
scale. Classic tooltips remain unchanged.

For bounded regression runs on scenarios created before the new gamerule existed,
the Unix executable accepts `--age-of-transformation`. This is an unsaved runtime
override intended for automation; a normal flagship campaign should be created by
selecting the bundled mod and building its scenario.

Example bounded run:

```sh
AliceIncremental 1.bin --days 365 --snapshot-every 30 --seed 424242 \
  --age-of-transformation --report-jsonl alice-aot-year.jsonl
```

For the strict save/load determinism diagnostic, compare a continuous run with a
checkpointed continuation. Save names are resolved in Project Alice's normal
save directory:

```sh
AliceIncremental 1.bin --days 365 --snapshot-every 5 --seed 424242 \
  --age-of-transformation --report-jsonl continuous.jsonl

AliceIncremental 1.bin --days 180 --snapshot-every 5 --seed 424242 \
  --age-of-transformation --save-at-end aot-midpoint \
  --report-jsonl before-checkpoint.jsonl

AliceIncremental 1.bin --load-save aot-midpoint.bin --days 185 \
  --snapshot-every 5 --age-of-transformation \
  --report-jsonl after-checkpoint.jsonl

python3 scripts/compare_simulation_reports.py continuous.jsonl after-checkpoint.jsonl
```

The comparison is keyed by simulation date, so the resumed run's local tick
counter may start from zero. Any checksum mismatch fails with the first divergent
date. Schema v48 serializes industry and land collateral as well as cargo and
producer debt; this removed the large false bankruptcy/write-off jump previously
seen after a checkpoint. A current v48 continuation matches GDP, producer debt,
treasuries, population and cargo exactly at the load boundary. Five simulated
days later its world aggregates differ from the continuous run by less than
`0.4` GDP and `0.8` producer debt. The whole-state checksum can still differ at
the boundary because older derived engine caches are reconstructed at load, so
the strict comparator is intentionally retained as a visible unfinished release
gate rather than presented as passing.

To turn a report into balance-review checkpoints, use the companion summarizer.
It exits non-zero when a snapshot is invalid and prints population-normalized
need fulfilment and unemployment alongside GDP, factory profitability, debt and
inflation. With monthly snapshots, its default is one row per year:

```sh
python3 scripts/summarize_simulation_report.py alice-aot-year.jsonl
```

For a reproducible baseline-versus-candidate hypothesis test, see
[Economy experiments](../economy-experiments.md).

## First expansion slice

The first deliberately narrow post-vertical-slice systems are now connected:

1. Banking exposes bounded credit health, reserve/debt-service coverage and
   financial stress. Its risk premium is consumed once by the credit market.
   The asset statement explicitly separates liquid reserves from its three loan
   books, preventing double subtraction and double losses on write-off.
2. World trade records requested, dispatched and in-transit cargo for each
   route/commodity pair. Land and sea capacity clear independently, commodity
   weight consumes the corresponding endpoint resource, and shared endpoints use
   deterministic max-min fairness. Travel time, spoilage, congestion and foreign
   settlement now affect delivery instead of appearing only as diagnostics.
3. Diplomatic crises expose deterministic stages, escalation pressure, settlement
   pressure and war risk from the existing crisis state. This first pass is
   diagnostic and intentionally does not rewrite crisis AI or temperature updates.

All three systems are exact no-ops when the flagship rule is disabled. Separate
bank liabilities/equity, regional credit institutions, wartime convoy
interception, long-term trade contracts, commodity substitution, global trade
institutions and new crisis templates remain later balance/content work rather
than hidden changes to legacy campaigns.
