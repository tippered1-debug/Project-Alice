# Canonical Population Hard Cutover v1

`OUR TIME` does not run a hybrid population simulation. Once a scenario or save
has crossed the import boundary, `exact_population` is the only causal owner of
population identity, membership and lifecycle.

## Non-negotiable ownership rule

DCON POP rows are projections and indexing shells. They may expose derived
population size to existing readers while those readers are being migrated, but
runtime code must never use a DCON size mutation as the cause of a canonical
birth, death, migration, class change, assimilation or household relocation.

There is no runtime fallback from canonical population state to legacy
population simulation.

If canonical population bootstrap/import, binding or validation fails, loading
or the attempted canonical operation fails explicitly. The engine must not
continue by silently executing the legacy path.

## Allowed legacy boundary

Legacy aggregate state is allowed exactly once as an import source:

1. Load a scenario or supported older save.
2. Build deterministic canonical population cells, identities and opening
   memberships from the information that actually exists in the input.
3. Validate the canonical store.
4. Project canonical totals into DCON observation fields.
5. Enter runtime with canonical ownership.

After step 5 the imported aggregate values are no longer causal inputs.
Information absent from an old save is not reconstructed as fake individual
history.

## Runtime invariants

- Every live DCON POP row used by OUR TIME has exactly one canonical population
  binding.
- Every canonical population mutation happens before any DCON projection.
- `pop.size` is derived from canonical live membership and is never a writable
  demographic source during runtime.
- Migration, promotion/demotion, assimilation, row merge and household movement
  preserve exact-person identity and mutate current membership directly.
- Birth and death mutate the exact store directly. They are never inferred by
  comparing a new DCON POP size with an earlier DCON POP size.
- A failed canonical mutation leaves the canonical store unchanged. There is no
  rollback into a legacy simulation path.
- Save/load preserves canonical state required to continue deterministically.
- Missing canonical state is an error, not a reason to run legacy behavior.

## Purge order

### 1. Population transfers

Replace the current dual-write sequence in `demographics.cpp`:

`write DCON size -> transfer exact membership -> rollback DCON on failure`

with:

`mutate exact membership -> validate -> project touched DCON rows`.

Transfer results report the actually represented canonical population amount;
sub-person movement is accumulated or deterministically resolved inside the
canonical store rather than represented first in legacy state.

### 2. Population lifecycle

Remove runtime inference in `reconcile_population_lifecycle()`. The current
implementation observes DCON row/world deltas and converts those deltas into
birth and death ranges. That is reverse causality and is forbidden after the
cutover.

Population growth/starvation/event deaths must call canonical birth/death APIs
directly. Lifecycle reconciliation becomes validation/projection only, or is
removed once all callers use canonical mutation APIs.

### 3. Row creation and deletion

Creating a POP row and registering its canonical cell is one transaction.
Deleting/merging a row transfers or retires canonical membership first and then
updates the DCON shell. Lazy registration from arbitrary runtime call sites is
removed.

### 4. Load boundary

Bootstrap/import is explicit and mandatory for OUR TIME. A failed bootstrap,
missing source binding, invalid membership range or incompatible canonical save
extension stops the load instead of logging a warning and continuing with
legacy behavior.

### 5. Delete compatibility branches

Once all population mutators are canonical, delete runtime branches conditioned
on the presence of `state.exact_population`. Canonical population state is a
runtime invariant of OUR TIME, not an optional acceleration layer.

## Exit gate

The population legacy path is considered gone only when all of the following
hold:

- runtime grep finds no direct demographic `pop_set_size` writes outside
  canonical projection/import code;
- no population mutation is conditional on `state.exact_population` being
  present;
- lifecycle code cannot infer exact births/deaths from DCON size deltas;
- missing canonical state fails explicitly;
- migration/class/assimilation/merge/household movement use one canonical write
  path;
- save/reload and repeated-transfer invariants pass without consulting mutable
  legacy population totals.
