# Exact Individual Population Store v1

This milestone adds a scalable exact identity catalog beside the existing
DCON `person` and `economic_actor` objects. A DCON Person remains useful for
named figures, compatibility, and explicit bridges, but it is not the
canonical identity of mass population. Its fixed storage capacity is therefore
not a limit on the logical population represented by this catalog.

## Canonical identity

An exact individual is addressed by:

```text
PersonKey = { source_population_cell, ordinal }
```

The source cell is the stable source-POP identity (`pop.index() + 1` for a
legacy POP registration), and the ordinal is a `uint64_t` in the half-open
range `[0, literal_count)`. Each valid key denotes one human. No weight,
represented-population field, representative person, or aggregate labor agent
exists in this store.

For a source POP with `pop.size = N`, v3 bootstrap semantics register
`floor(max(0, N) * 4)` literal identities. The catalog stores one sealed cell
descriptor and its range, not one heap/DCON object per ordinal. Identity-range
compression is allowed; representative-agent causality is forbidden.

## Sealed source descriptors

Registration copies the source cell, literal count, bootstrap version, home
site, culture, religion, POP type, bootstrap day, and demographic seed/policy
into a cell descriptor. Later mutation of legacy POP size or attributes cannot
rewrite the descriptor or add/remove exact identities.

The four-slot bootstrap interpretation preserves inherited workforce semantics:
`source_unit = ordinal / 4` and `bootstrap_slot = ordinal % 4`. Slot zero is
the `source_workforce_anchor`; the other slots are separate non-anchor humans.
This is provenance only, not a household relation, permanent occupation, or
permanent prohibition on future work. The catalog does not submit dependents to
the labor market.

Anchor ages are deterministically generated in the 18--64 working-age range.
Non-anchor ages use a deterministic 0--90-year synthetic initial-condition
policy. These ages are generated because the source POP has no historical
individual ages; they are not observations. Signed birth-day indices remain the
canonical age representation, and advancing the query date increases age.

The exact query API addresses `exists`, count, alive state, birth day, age,
home site, source culture/religion/POP type, and workforce-anchor status for a
single key. Repeated queries are deterministic and have no DCON allocation side
effects.

## Sparse mutable state and compatibility

Default alive/home state is derived from the sealed descriptor. Death and home
changes are stored in a sparse map keyed by the exact PersonKey, so changing
one individual cannot change a neighbor or allocate state for the rest of the
cell.

`materialize_legacy_person_bridge` is an explicit, idempotent compatibility
projection. It creates exactly one DCON Person and its existing mandatory
EconomicActor for the requested key, records the mapping, and copies the
canonical signed birth/source state. Registering a catalog cell, probing a key,
or changing an overlay creates zero DCON Persons and zero EconomicActors.

## Scale and persistence boundary

Catalog storage is approximately:

```text
O(source POP cells + mutable exact-person overrides + explicit bridges)
```

Synthetic range tests cover 1 million, 100 million, and 1 billion logical
humans without iterating through the range or increasing DCON Person,
EconomicActor, account, inventory, or employment-contract counts.

The catalog exposes an isolated `catalog_snapshot` export/import interface for
save integration. It contains bootstrap version, sealed descriptors, sparse
overrides, and bridge mappings, and restores without per-human allocation. The
normal scenario/save serialization pipeline is not wired to this interface in
this milestone; callers must explicitly persist and restore the snapshot.

## Deliberate non-goals

This is an identity/storage foundation only. Job offers and applications,
employment contracts, goods bids, consumption, payroll, ownership, money,
logistics, taxation, banking, households, births, migration, and full-world
catalog registration remain future milestones. No full-world DCON materializing
call is introduced, and exact registration does not make the logical person an
economic agent.
