# Canonical person ownership and consumption

> Historical project record. This file preserves a past milestone or audit and is not a current runtime contract. See the [current architecture map](../../architecture/README.md) and [migration status](../../migrations/README.md).

`persons::person_key` is the sole canonical household consumer. `exact_person_goods`
owns needs, physical stock, bids, fills, consumption, and unmet quantities;
`exact_person_economy` owns the consumer's account and transaction ledger.
`individual_consumption` is a temporary `dcon::person_id` facade over these APIs.
It creates no independent needs, stock, account, bid, or fill state.

## Representation and causal path

Physical ownership is stored as sparse records keyed by
`PersonKey + site + commodity`. A GoodsBid records its exact account,
destination, market, commodity, quantity, limit, reservation, and status. The
daily consumer phase processes stable `person_key + commodity` order from the
sparse need store, matches affected commodities through the existing concrete
market, and consumes only stock present at the home site. It does not scan all
persons or all commodities. Incoming exact freight is included when calculating
how much more a person may bid.

An exact fill transfers money through `exact_person_economy::transfer` to the
seller's real account and moves real seller inventory into sparse exact stock
at the ask source site. Local fills complete ownership immediately. Remote
fills create an exact freight request for the owned source stock; goods remain
at the source until the existing shipment system delivers them.

Free exact cash is the account balance less active exact-bid reservations.
Canceled or expired bids release their reservation without moving balances.
Purchase decisions use settlements offered by active concrete asks and select
the compatible account with the most spendable canonical cash. The person's
balance sets the maximum order size. No synthetic income or account is created
because a need exists. Exact labor payroll credits the same account that the
person uses to bid.

Concrete observed prices include exact fills. Exact needs persist
`consumed_this_period`; consumption is capped by
`desired - consumed_this_period` and exact home-site stock. The daily phase
advances the period and updates unmet consumption from physical stock.

## Persistence, validation, and scale

Goods/consumption exposes a snapshot API for stocks, needs, bids, fills,
profile-import markers, and deterministic IDs. The normal `AOEX` save
extension persists this state with person, economy, freight, labor, and causal
ordering snapshots. Load rejects invalid household state; there is no
reconstruction from POP savings or satisfaction.

`validate_canonical_household_economy` checks account owners and balances,
contracts, needs, stock, bid reservations, fill transfers and seller inventory
deltas, freight ownership, and absence of the legacy person-need relation.
`canonical_household_checksum` hashes stable, sorted canonical state.

Snapshot dependencies are:

`Exact Population snapshot -> Exact Person Economy snapshot -> Exact Goods/Consumption snapshot`.

Registering a million logical persons remains O(1) with respect to economic
records; only activated accounts, needs, bids, stock, and fills are allocated.
Remote physical delivery uses sparse exact freight requests/contracts and the
existing routed Shipment infrastructure.

The existing `accounts::cash_inflow`, `cash_outflow`, and
`operating_cash_flow` helpers remain DCON-Transaction observations. Mixed
exact-person sales update the real seller DCON account and exact mixed
transaction ledger, but do not synthesize a DCON buyer or DCON Transaction.
Those helpers therefore do not report mixed exact sales. Concrete-market fill
observations and the exact transaction ledger are the canonical sources for
consumer purchase reporting.
