# Population semantics and scale gate v1

## Audited meaning of `pop.size`

The repository evidence separates the engine accounting unit from the source
demographic meaning. Project Alice inherits the Victoria II POP history format
(`history/pops`); the parser reads `province-id;size;culture;religion;type;
rebel-faction`, and no Victoria II history files are bundled in this checkout.
The source-data meaning is nevertheless established by the compatibility UI
and its explicit conversion:

- `assets/localisation/en-US/alice.csv` labels the direct population value
  “Adult male population” and separately labels “total population”.
- `src/gui/gui_topbar.hpp` supplies the direct demographic total for the adult
  male line and computes the total line as `demographics::total * 4`.
- The parser and POP mechanics operate on the inherited `history/pops` `size`
  field, so that source unit is the adult-male/workforce demographic unit used
  by the imported data.

The engine accounting evidence is:

- `src/gamestate/simulation_runner.hpp` adds `pop_get_size(pop)` directly to
  `aggregate_snapshot::tracked_nation_population` and uses the same value in
  population-weighted statistics.
- `src/economy/demographics.cpp` computes employment as the stored employment
  fraction multiplied by `pop_get_size(pop)`, and its demographic totals sum
  `pop_get_size(pop)` directly.
- Most engine aggregates intentionally sum `pop_get_size(pop)` directly. That
  is the internal adult-male/workforce accounting unit, not a claim that it is
  the literal human count.
- `src/military/military.cpp` uses `defines.pop_size_per_regiment` only when
  converting population into regiment manpower. That is a military conversion,
  not a population semantic multiplier.
- Population growth, splitting, migration, and parser creation write that
  source unit itself. They do not change the source demographic interpretation.

Therefore the final source conclusion is: **source `pop.size` is demonstrated
to be an adult/working demographic unit with a documented literal expansion
rule of 4**. This is not an external convention guessed from the absence of a
multiplier; the repository's population UI names the unit and contains the
explicit total-population conversion.

The v3 bootstrap policy is:

`population_materialization::bootstrap_semantics_version == 3`

For bootstrap, `literal_persons = floor(max(0, pop.size) * 4)`. The estimator
reports source units and the intended literal-person/actor totals without
allocating persons. A negative or non-finite source size is reported as an
estimator/materialization error; arithmetic overflow and the current DCON
Person/EconomicActor capacities are reported separately. A cell that cannot
fit is rejected before marker, site, person, or actor allocation, so it cannot
partially materialize.

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
demographic ages are deterministic from `{source_population_cell, ordinal}`
and range from 5 through 84 years (365-day years). The persisted signed
`person.birth_day_index` is authoritative and represents days relative to the
simulation calendar base, including negative pre-base births. The old
`person.birth_date : sys::date` remains only as a compatibility mirror; it is
null when the old uint16 representation cannot encode the signed birth day and
must not be used for age behavior. `persons::age_days`, `age_years`, and
`born_on_or_before` centralize age calculations. `is_work_eligible` uses only
valid/alive individual state and the 14-inclusive through 65-exclusive age
policy; POP employment and labor aggregates are irrelevant.

## Persistent identity and indexes

The persisted marker stores `bootstrap_version`. A matching marker returns its
indexed persons; a version mismatch returns `version_mismatch` and never
appends or duplicates. Marker lookup uses the indexed POP-to-marker relation.
Marker-person lookup uses the indexed DCON relation and preserves ordinal
ordering. Neither canonical lookup scans every marker or every person.

## Measurement and scale gate

`materialization_measurement` reports persons created, economic actors created,
totals, marker count, elapsed microseconds, and persons/second. The explicit
synthetic benchmark hook accepts 10,000, 100,000, or 1,000,000 intended literal
persons as caller-owned inputs; no result is claimed here because this
checkout has no fresh runtime build. Current generated DCON capacity is
100,000 Persons and 50,000 EconomicActors, so the full-world rollout remains
blocked by the actor capacity (and by the need for fresh capacity/RSS
measurements). No bytes/person claim is made.

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
