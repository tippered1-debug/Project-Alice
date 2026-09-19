# Exact-person ownership and consumption v1

This milestone adds a sparse causal goods loop for a logical person identified
by `persons::exact_population::person_key`. It does not create a DCON Person,
EconomicActor, MonetaryAccount, or PhysicalStock for that person.

## Representation and causal path

Exact physical ownership is stored as sparse records keyed by
`PersonKey + site + commodity`. Exact needs, GoodsBids, and fill records use
the same sparse store. A GoodsBid records its exact account, destination,
market, commodity, quantity, limit, reservation, and status. Exact bids are
matched in the existing concrete market order stream; equal-date cross-kind
ties place DCON bids before exact bids, then use the stable ID.

An exact fill is local-only in v1: the DCON ask source site must equal the
exact bid destination/home site. A successful fill atomically transfers exact
money to the seller's real DCON account and real seller inventory to sparse
exact stock. It records the mixed transaction ID and exact fill ID. Failed
validation changes neither side.

Free exact cash is the account balance less active exact-bid reservations.
Canceled or expired bids release their reservation without moving balances.
Purchase decisions only use settlements offered by active concrete asks and
select the highest-free-cash compatible exact account, with lowest account ID
as the tie-break. No account is opened merely because a need exists.

Concrete observed prices include exact fills. Exact needs use persistent
`consumed_this_period`; consumption is capped by
`desired - consumed_this_period` and exact home-site stock. A later explicit
`begin_period` resets period fulfillment.

## Persistence and scale

Goods/consumption has an isolated snapshot API for stocks, needs, bids, fills,
and deterministic IDs. Normal Project Alice save integration remains deferred.
Snapshot dependencies are:

`Exact Population snapshot -> Exact Person Economy snapshot -> Exact Goods/Consumption snapshot`.

Registering a million logical persons remains O(1) with respect to economic
records; only activated accounts, needs, bids, stock, and fills are allocated.

The remaining blocker before exact freight is remote physical delivery:
exact-person freight requests and shipments are intentionally not implemented,
so remote asks cannot fill or teleport goods in this version.

The existing `accounts::cash_inflow`, `cash_outflow`, and
`operating_cash_flow` helpers remain DCON-Transaction observations. Mixed
exact-person sales update the real seller DCON account and the exact mixed
transaction ledger, but do not synthesize a DCON buyer or DCON Transaction.
Those helpers therefore do not yet report mixed exact sales; a unified
accounting/statistics observation path is deferred to that milestone.
