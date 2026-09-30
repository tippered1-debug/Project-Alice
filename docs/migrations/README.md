# Migration status

This folder describes transitions from Project Alice/Victoria-shaped data to the canonical runtime. For the destination architecture, start at [Architecture](../architecture/README.md).

## Current transition boundaries

| Surface | Status in the current docs |
| --- | --- |
| Scenario authoring | Canonical firm, owner, asset, banking, and land-force tables are required by the scenario contract. Authored POP rows, factory definitions, geography, and resource signals remain bootstrap inputs. See [Scenario format](../runtime/scenario-format.md). |
| Save compatibility | The runtime accepts the documented current save format and rejects unsupported aggregate saves; it does not reconstruct individual history from POP totals. See [Serialization](../runtime/serialization.md). |
| Household consumers | The POP consumer-engine purge is complete in the recorded audit. Exact accounts, needs, goods, and fills are canonical; POP fields are projections. The audit is preserved in [Archive](../archive/README.md). |
| Causal legacy reads | Current boundaries are in [Causality](../architecture/causality.md). The older purge note is a historical implementation record. |
| Technology | Canonical capability tables have an explicit scenario activation boundary. When active, legacy technology flags are not read into the capability kernel. See [Technology](../architecture/technology.md). |

## Hard-cut status

This checkout contains no `common/canonical_runtime/` scenario tables, so the migration state of external scenario packs cannot be verified here. The existing documentation does not identify a separate, still-open subsystem hard-cut plan with current scope and status. Older milestone snapshots contain deferred work and audit sections written against earlier code; those are not current work queues. In particular, the old Person Kernel audit's pending POP-manpower items are superseded by the current [Military](../architecture/military.md) contract, and its aggregate household items are superseded by the completed consumer purge.

Treat a hard cut as active only when a current migration document names its remaining legacy causal path and completion boundary. That status is not established by the archived v1 snapshots in this checkout.
