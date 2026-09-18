# Population Materialization Foundation v1

This milestone introduces literal individual population bootstrap. One
materialized row is one person. There is no weight, represented population,
household weight, agent size, or representative-agent field.

## Bootstrap identity and idempotency

`persons::population_materialization::materialize_population_cell(state, pop)`
reads a legacy POP cell once. For a finite non-negative POP size `N`, v1 uses
`floor(N)` and creates exactly `N` persons. Each generated person stores the
stable key:

`PersonKey = { source_population_cell, ordinal }`

where `source_population_cell` is the stable DCON POP index plus one and
`ordinal` is `0..N-1`. The order is sorted by ordinal and does not use random
state. Source pop type, culture, and religion are copied as immutable bootstrap
demographic identity fields.

A persistent `population_materialization` marker records that the source cell
has been completed. A second call returns the existing persons and does not
read the POP size, create actors, create sites, or create any economic state.
Consequently, later mutations to POP size, savings, employment, or satisfaction
do not create, delete, hire, fire, fund, or consume for persons. The
`materialize_initial_population` API applies the same rule to all POP cells in
stable POP-ID order.

## Home sites and sparse state

Each materialized person receives a concrete `home_site`. If the caller does
not supply one, the lowest existing site ID in the source province is shared by
all persons from that cell. If the province has no site, one shared site is
created at the province midpoint. No per-person site/building is created.

Materialization uses the existing `persons::create_person` path, so each person
gets exactly one existing-style economic actor. It creates no monetary
accounts, inventories, employment contracts, job applications, needs, goods,
or transactions. Those remain sparse and are created only by later causal
events. Existing job search, concrete labor/payroll, individual money, and
individual consumption APIs operate on the generated person IDs without POP
state.

## Measurement and scaling

`measure_initial_population_materialization` reports elapsed microseconds,
persons created/total, economic actor total, and materialization marker count.
It is an explicit measurement hook, not a fabricated benchmark result. No
100k/1m runtime benchmark result is recorded for this checkout because a fresh
runtime executable was not available; only syntax validation was available.

The current DCON person representation is suitable for the small bootstrap
and sparse-state tests in this milestone, but it is not yet evidence of
billion-person scalability. The generated person storage is currently sized
for 100,000 entries, so a 1m-person run requires an explicit storage-capacity
decision and measurement before being attempted.

If that representation becomes unsuitable, the next exact-person option is a
packed Individual Population Store / PersonCore SoA. Each packed row would
still represent exactly one person and retain the PersonKey, demographics,
alive flag, and home site; heavy economic objects would remain sparse. This
milestone does not implement that rewrite.

## Deferred

Runtime births/deaths, migration, households, family relations, historical
employment assignment, wealth distribution, banking, taxes, pensions, housing,
social networks, political behavior, and billion-person production rollout are
outside this foundation milestone.
