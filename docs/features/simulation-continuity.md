# Simulation Continuity v1

The normal save section now carries a framed `AOEX` extension for state kept
outside the DCON world. The extension is versioned and length-delimited, so
version 48 saves without exact-person state continue to load and newer unknown
extension versions can be skipped. New files use version 49; older executables
reject that version instead of misreading extension bytes as DCON data.

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
