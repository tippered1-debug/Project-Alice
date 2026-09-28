# Simulation Continuity v1

The normal save section carries a framed `AOEX` extension for state kept outside
the DCON world. The extension is versioned and length-delimited. Save versions
48 through 51 are readable; version 48 has no exact-person extension, version
49 uses `AOEX` v1, version 50 writes `AOEX` v4, and version 51 writes `AOEX`
v5. The latest extension persists row-lifetime bindings, retired identity
ranges, birth cohorts, population-size checkpoints, current membership ranges,
transition records and fractional transfer remainders without changing the
DCON schema. Older executables reject version 51 instead of misreading
extension bytes as DCON data.

The snapshot preserves the exact population catalog and its overrides, sparse
person accounts and transactions, job applications and contracts, household
stocks and needs, goods fills, freight requests and shipment ownership,
employment separations, and causal ordering for DCON and exact events. Loading
restores the catalog before records that refer to exact people, then restores
their economy, goods, freight, labor history, and causal ordering.

Each subsystem validates its records against the loaded DCON world before
accepting the snapshot. If a snapshot is malformed or references missing
entities, the exact runtime stores are cleared together instead of leaving a
partially restored economy. The DCON world remains the source of truth for
treasuries, institutions, sites, jobs, and shipments.

The extension is included in ordinary saves, scenario-plus-save files, and the
handwritten multiplayer state transfer. It does not serialize derived market
caches; those are rebuilt by the existing load path.

With Age of Transformation enabled, loading a new scenario bootstraps virtual
exact-person identity ranges from its POP rows. A v48 save, a v49 save with an
empty first-generation catalog, and a v49 save with an existing catalog receive
the compatible bootstrap or binding migration after the DCON world is loaded.
AOEX v2 preserves row-lifetime bindings; v3 records retired row tokens; v4
persists virtual-person birth and death ranges and the aggregate population
checkpoint; v5 adds current membership ranges and compact movement history.
Daily reconciliation appends new identities for net population growth and
projects living membership counts back to DCON POP sizes. Net shrinkage retires
a deterministic low-ordinal range, ends active exact labor contracts, and
cancels open purchase bids. Migration, class changes, assimilation, population
merges and exact household relocation update membership without changing
identity keys. Accounts, stock and historical transactions remain attached to
retired keys, so no value is silently deleted. This creates no DCON people or
economic actors.

Aggregate saves do not contain individual employment, balance, or consumption
histories, so migration does not invent those records. Older AOEX v1-v4 saves
initialize current membership in each person's original row, then use the
hooked transfer paths going forward. Direct POP size edits that bypass those
paths still require explicit migration rules. Death does not yet create
inheritance for retired accounts. Opening balances, stocks and employment also
remain to be migrated without double counting.
The wider ownership and retirement gates are tracked in
[Canonical Runtime Migration](canonical-runtime-migration.md).
