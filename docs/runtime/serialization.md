# Serialization and continuity

The current normal save is version 61. Its framed `AOEX` v19 extension stores runtime state that lives outside the DCON world:

- exact-person identity catalog, active memberships, birth/death ranges, and transfers;
- exact household accounts, transactions, job applications, contracts, and separations;
- household goods, needs, purchases, freight requests, and shipment ownership;
- labor history and deterministic causal ordering;
- construction request-to-project bindings and typed project results (construction contracts, work and works live in DCON; v19, older extensions adopt their projects into the contract model);
- the books of each state (v18): its closed fiscal days and months, see [Government](../architecture/government.md#the-books-of-the-state). A v17 extension loads with empty books.

Land-force snapshot v4 also stores native formation-to-battle-view bindings and
retreat continuation state. The corresponding armies, regiments, active battles,
and occupation progress restore from DCON. Older land-force v2/v3 snapshots have
no native battle views; they are created from surviving staffed formations at
the next military phase, without creating people or equipment.

Strategic Statecraft is serialized in its required versioned section. Loading succeeds only after the exact-person and Statecraft data validate against the loaded DCON world. Unsupported save headers, missing runtime records, and malformed snapshots are rejected; older aggregate POP saves are not reconstructed.

The extension is written for ordinary saves and multiplayer state transfer. Derived market and labor read views are rebuilt from canonical state after loading. DCON POP size and savings are projected from the exact population and person-account stores. Household needs, stock, consumption state, bids, fills, profile-import markers, freight ownership, and causal ordering restore from their canonical sparse snapshots; they are never reconstructed from POP aggregates.

New scenarios are initialized from their authored POP rows, factory definitions, sites, and resource signals. Factories and resource deposits must include canonical operator firms, assets, and complete ownership stakes; missing ownership data rejects scenario initialization instead of creating placeholder owners. The importer does not infer employment, cash, or consumption history from a previous campaign.

For ownership by domain and implementation rules, see [Canonical Runtime Contract](../migrations/canonical-runtime.md).
