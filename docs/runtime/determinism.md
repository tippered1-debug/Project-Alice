# Determinism and runtime validation

Canonical IDs derive from authored keys or stable domain identifiers. Scenario entities and persistent decisions are processed in defined stable order where their contracts require it. Domain checksums cover sorted canonical records and exclude projection-only allocation details.

On load, canonical snapshots are validated against the restored world before play continues. Invalid references, incomplete memberships, inconsistent account or inventory transfers, malformed routes, and missing required domain records are rejected; the runtime does not repair canonical history from aggregate POP or market values.

The exact records and restore sequence are listed in [Serialization](serialization.md). Domain-specific ordering and checksum rules are documented in [Persons](../architecture/persons.md), [Logistics](../architecture/logistics.md), [Military](../architecture/military.md), and [Foreign policy](../architecture/foreign-policy.md).
