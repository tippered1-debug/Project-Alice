# Canonical runtime transition contract

The canonical runtime is mandatory for every domain required by the scenario and save contract. Once a domain is active, missing required runtime state, invalid records, and unfunded canonical operations are errors; simulation does not silently fall back when a required canonical record is missing. Explicitly unmapped content may keep the behavior stated in its domain contract. A domain with an explicit scenario activation boundary remains on its documented compatibility path before activation. Canonical research and technology is activated by `capabilities.csv`; after activation, legacy technology flags are never read back into canonical capability state.

## Authority and projections

| State | Canonical authority | Projection or authored input |
| --- | --- | --- |
| Person identity and location | Exact population store | Authored scenario POP rows seed identity once; DCON POP size is a projection. |
| Household cash and goods | Exact-person accounts and household inventory | DCON savings and needs are read views. |
| Work and wages | Exact worker applications, contracts, and payroll | DCON employment and wage prices are projections. |
| Factory production | Firm agency, productive capacity, inputs, and physical output | DCON factory output and employment are projections. |
| Market activity | Concrete bids, asks, fills, inventories, freight, and shipments | DCON markets are price and activity views. |
| Resource production | Site deposits, extraction capacity, inventories, and shipments | Scenario RGO values seed deposits; DCON RGO output is not a producer. |
| Firm finance | Economic actor accounts, equity, loans, and asset ownership | Factory finance fields are diagnostics and display data. |
| Public finance | Institutional tax obligations, treasury accounts, payroll, procurement, and services | National and provincial budget fields are views or policy inputs. |
| Political authority | Transformation politics, governance institutions, and staged bills | Parties and reform IDs remain content and interface identities. |
| Foreign policy | Strategic Statecraft | Existing war and diplomacy entities carry outcomes and scenario-authored relationships. |

DCON writes are allowed only when they import authored scenario data, execute a game command against its canonical model, or project canonical state for an existing consumer. A legacy aggregate must not be read back to infer a canonical transaction.

## Save transition boundary

Only the documented current save format is accepted. Unsupported aggregate POP saves are rejected; individual history is not reconstructed from aggregate totals. Required sections and validation are listed in [Serialization](../runtime/serialization.md).

## Household and consumer authority boundary

The POP consumer hard cut is complete; this section records its current ownership and compatibility boundary.


`persons::person_key` is the consumer identity. The sparse exact-person goods
store owns need profiles, physical stock by site, active bids, fills, period
consumption, and unmet quantities. Person accounts and all purchases use the
exact-person account and transfer ledger. A daily sparse phase advances needs,
accounts for home stock and incoming freight, places real bids in the concrete
market, then consumes only physically delivered stock. Remote fills create
freight requests; they do not credit home stock before arrival.

`individual_consumption` is a compatibility facade for existing DCON person
callers. The DCON person-need relation is not an authoritative store and is
rejected by household validation. Static scenario POP-type needs may be copied
once into exact-person profiles; after import, POP savings, needs satisfaction,
market needs weights, and synthetic POP wages do not fund bids or determine
consumption. POP cash is projected from live exact-person accounts for existing
screens. POP-targeted cash mutations fail because an aggregate POP has no
canonical recipient.

`validate_canonical_household_economy` runs after scenario initialization and
runtime restore. It checks canonical owners, balances, need/stock invariants,
bid reservations, fill transfers and inventory deltas, freight ownership, and
absence of legacy per-person needs. The household checksum includes sorted
accounts, labor references, transactions, needs, stock, bids, fills, and
freight state.

New scenarios import authored POP rows, factory definitions, resource signals, and static geography before daily simulation begins. Each factory and resource deposit must also carry a canonical operator, asset, and complete ownership stakes. Initialization no longer invents replacement firms or ownership when those records are absent; such a scenario is rejected at startup. Canonical identities, sites, deposits, accounts, institutions, and Statecraft profiles are initialized before the game clock advances.

## Change checklist

When adding a state field, identify one canonical owner and any one-way DCON projection. When adding an action, require the actor, account, authority, and physical resource needed to execute it. Do not add optional runtime flags or silent fallbacks for missing required records. Keep any scenario activation boundary explicit and domain-scoped. Raise an assertion or reject the load when required canonical state is absent.
