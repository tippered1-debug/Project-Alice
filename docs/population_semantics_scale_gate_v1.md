# Population semantics and scale gate v1

## Audited meaning of `pop.size`

The repository evidence supports the conclusion that `pop.size` is the
aggregate population unit represented by literal persons. There is no
repository-wide adult/working-unit multiplier and no demonstrated `*4`
conversion:

- `src/gamestate/simulation_runner.hpp` adds `pop_get_size(pop)` directly to
  `aggregate_snapshot::tracked_nation_population` and uses the same value in
  population-weighted statistics.
- `src/economy/demographics.cpp` computes employment as the stored employment
  fraction multiplied by `pop_get_size(pop)`, and its demographic totals sum
  `pop_get_size(pop)` directly.
- The web API and GUI population views consume those direct demographic totals
  or `pop_get_size(pop)`; they do not apply a universal multiplier.
- `src/military/military.cpp` uses `defines.pop_size_per_regiment` only when
  converting population into regiment manpower. That is a military conversion,
  not a population semantic multiplier.
- Population growth, splitting, migration, and parser creation write the POP
  size itself; no global literal-person multiplier is encoded there.

Therefore this milestone selects the explicit policy/API:

`population_materialization::bootstrap_semantics_version == 2`

For bootstrap, `literal_persons = floor(max(0, pop.size))`. The materializer
creates one `person` and one `EconomicActor` per literal person. The estimator
reports this policy without allocating persons. A negative or non-finite source
size is reported as an estimator/materialization error; an integer count above
the materializer's count range is reported as overflow. There is no `*4`
multiplier.

`source_pop_type`, `source_culture`, and `source_religion` are immutable
provenance copied from the source POP. They are not occupations. A concrete
`EmploymentContract` is authoritative for individual employment and job
search; no aggregate POP labor field is consulted for individual eligibility.

## Explicit materialization and synthetic initial age

`materialize_initial_population` remains an explicit caller-owned operation. It
is not wired into loading, the regular tick, economy initialization, or job
initialization. `estimate_initial_population` is allocation-free and reports
source cells, source units, intended literal persons, intended actors, largest
cell, semantics version, and overflow.

The old `create_person(current_date)` newborn bug is removed. Initial
demographic birth dates are deterministic from `{source_population_cell,
ordinal}`. The documented synthetic-age policy is an initial age uniformly
hashed into 5 through 84 years (365-day years), clamped to a null date when the
chosen age cannot fit before the supplied current date. This is bootstrap-only
demographic data, not runtime birth simulation. `persons::is_work_eligible`
requires a valid, alive person, a non-future birth date, and age from 14 years
inclusive through 65 years exclusive; it is the gate used by individual job
search.

## Persistent identity and indexes

The persisted marker stores `bootstrap_version`. A matching marker returns its
indexed persons; a version mismatch returns `version_mismatch` and never
appends or duplicates. Marker lookup uses the indexed POP-to-marker relation.
Marker-person lookup uses the indexed DCON relation and preserves ordinal
ordering. Neither canonical lookup scans every marker or every person.

## Measurement and scale gate

`materialization_measurement` reports persons created, economic actors created,
totals, marker count, elapsed microseconds, and persons/second. The explicit
synthetic benchmark hook accepts 10,000, 100,000, or 1,000,000 as caller-owned
inputs; no result is claimed here because this checkout has no fresh runtime
build. The repository's generated person storage is currently bounded around
100,000 entries, so the full-world rollout remains blocked until fresh-build
capacity/RSS measurements and a storage decision exist. No bytes/person claim
is made.

The one-actor-per-person choice is intentionally retained and called out as a
scale concern. A future packed SoA / Individual Population Store may reduce
per-person overhead, but it must still represent one literal person per row;
representative agents, weights, weighted households, and aggregate labor pools
are not substitutes.

## Deferred work

Runtime births/deaths, households, fertility, migration, banking, taxes,
politics, pensions, housing, social networks, larger economic rollout, and
full-world auto-materialization are deferred. This scale gate audits semantics,
keeps bootstrap explicit, and establishes correctness/indexing evidence; it
does not start those systems.
