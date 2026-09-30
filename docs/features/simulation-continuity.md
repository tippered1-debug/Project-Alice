# Simulation Continuity

The current normal save is version 57. Its framed `AOEX` v13 extension stores runtime state that lives outside the DCON world:

- exact-person identity catalog, active memberships, birth/death ranges, and transfers;
- exact household accounts, transactions, job applications, contracts, and separations;
- household goods, needs, purchases, freight requests, and shipment ownership;
- labor history and deterministic causal ordering.

Strategic Statecraft is serialized in its required versioned section. Loading succeeds only after the exact-person and Statecraft data validate against the loaded DCON world. Unsupported save headers, missing runtime records, and malformed snapshots are rejected; older aggregate POP saves are not reconstructed.

The extension is written for ordinary saves and multiplayer state transfer. Derived market and labor read views are rebuilt from canonical state after loading. DCON POP size and savings are projected from the exact population and person-account stores.

New scenarios are initialized from their authored POP rows, factory definitions, sites, and resource signals. Factories and resource deposits must include canonical operator firms, assets, and complete ownership stakes; missing ownership data rejects scenario initialization instead of creating placeholder owners. The importer does not infer employment, cash, or consumption history from a previous campaign.

For ownership by domain and implementation rules, see [Canonical Runtime Contract](canonical-runtime-migration.md).
