# Canonical Runtime Contract

The canonical runtime is mandatory. The simulation must not switch to legacy execution when a canonical record is absent. Missing runtime state, invalid records, and unfunded canonical operations are errors.

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

## Load contract

The current save header is version 55 and its exact-runtime extension is `AOEX` v8. Strategic Statecraft state is mandatory. A save load succeeds only when the population, labor, household, goods, freight, causality, and Statecraft records validate against the loaded world. Older aggregate save versions are unsupported; there is no reconstruction of individual histories from their POP totals.

New scenarios import authored POP rows, factory definitions, resource signals, and static geography before daily simulation begins. Each factory and resource deposit must also carry a canonical operator, asset, and complete ownership stakes. Initialization no longer invents replacement firms or ownership when those records are absent; such a scenario is rejected at startup. Canonical identities, sites, deposits, accounts, institutions, and Statecraft profiles are initialized before the game clock advances.

## Change checklist

When adding a state field, identify one canonical owner and any one-way DCON projection. When adding an action, require the actor, account, authority, and physical resource needed to execute it. Do not add optional runtime flags or old-model fallbacks. Raise an assertion or reject the load when required canonical state is absent.
