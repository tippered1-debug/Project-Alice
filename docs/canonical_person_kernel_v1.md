# Canonical Person Kernel v1

## Identity and profiles

`persons::person_key` is the identity of every logical human. The type is
declared in `persons`, while `persons::exact_population` retains the compact
storage implementation. A key remains `{source_population_cell, ordinal}`;
large cohorts use cell descriptors and ranges, not one C++ object per person.

`persons::exists`, `alive`, `birth_day_index`, `age_days`, `current_population`,
`current_population_cell`, `home_site`, and `pop_type` are the domain boundary
for identity, lifecycle, and current demographic membership. Current population,
type, culture, and religion are empty for a dead person or a person without a
bound POP projection. The catalog keeps the last
membership range for persistence and historical projection, while live-count
queries exclude retired identities. Missing membership is invalid; callers do
not infer a person's location from the source cell.

`dcon::person_id` is a sparse materialized profile. The exact store maintains
both key-to-profile and profile-to-key indexes and rejects duplicate mappings
while loading. The public API is `materialized_profile`,
`materialize_profile`, and `canonical_key`. Source-cell traits on a profile
remain origin metadata; current class, culture, religion, and home are read from
the person's current membership and canonical home override. The reverse bridge
is authoritative even for ordinals wider than DCON's old 32-bit display field;
that mirror field is zeroed when the ordinal does not fit. An orphan profile
that still carries a nonzero canonical source cell is rejected by canonical
reads instead of restoring stale mirror values.

`persons::create_person` remains for named/public people which require a DCON
profile and actor. It creates a singleton canonical cell and materializes its
profile. Mass POP bootstrap never calls it. The explicitly bounded
`population_materialization` utility is used by compatibility/measurement
tests only; no game runtime source calls it.

## Lifecycle and domain ownership

Canonical birth cohorts and death ranges remain in the exact population store.
`persons::adjust_population_size` creates or retires identities for aggregate
demographic changes; `persons::transfer_population` and
`transfer_population_membership` preserve keys through migration,
reclassification, promotion, and demotion. Public person membership queries
resolve the current DCON POP projection through the store and never fall back
to an old source location.

`persons::kill_person` performs one deterministic lifecycle transaction. It
records death day and cause, releases a military assignment through the
military relation hook, closes exact labor applications and active contracts,
updates the profile mirror, and ends active office tenures using their start/end
date rules. The account owner key, balances, and transaction records remain
resolvable after death. Repeating a death request does not create another
death.

Domain data stays with its owner: exact economy owns accounts and employment
relations; `military::land_forces` owns formations and assignment queries;
governance owns offices and tenures; household mobility owns household stock
and relocation policy. These systems use `persons::person_key` where they refer
to a human. The canonical military assignment path accepts a span of
`persons::person_key`; it has no soldier-only identity.

## Save, validation, and determinism

The exact runtime save extension is version 10. It persists compact death
ranges alongside the existing cells, memberships, birth cohorts, sparse
overrides, profile mappings, and military assignments. Snapshot import rebuilds
the reverse profile index and rejects invalid or duplicate mappings and
incomplete membership coverage.

`persons::validate_person_kernel` checks namespace and membership coverage,
dead/live state, profile mirrors and one-to-one mappings, death coverage,
and exact account/application/contract ownership. It also rejects active
contracts for dead workers and duplicate active primary contracts.
`persons::person_kernel_checksum` hashes normalized catalog order and
lifecycle-relevant data. DCON profile allocation IDs and POP slot bindings are
projection details and do not affect the checksum.

## Legacy human-state audit

### A. Authored input

- Scenario POP rows supply bootstrap size, culture, religion, type, and initial
  site through `exact_population::bootstrap_from_current_pops` and
  `register_population_cell`. Registration seals these into canonical cell
  descriptors and creates range-backed keys.
- DCON POP fields remain authored inputs for legacy aggregate demographic
  rules and scenario triggers. A population row is not a person identity.

### B. One-way projection, statistics, or aggregate policy/social behavior

- `exact_population::project_population_membership` writes live canonical
  counts back to DCON POP sizes. Demographics, aggregate statistics, scripts,
  and UI still read these rows as weighted population totals.
- `exact_person_economy::project_population_cash_balances` writes exact account
  totals to POP savings for aggregate budget/statistics consumers. Individual
  account ownership and transaction history remain in exact-person economy.
- UI demographic and population screens read POP size, savings, and employment
  as aggregates.
- `economy::labor_relations::evaluate_province` intentionally weights POP
  size, literacy, consciousness, militancy, employment, movement membership,
  and needs to estimate union and strike behavior. It remains an
  aggregate-derived policy/social layer; it does not identify workers or
  determine exact contract employment.
- The DCON profile's alive, birth, source-trait, and home fields are mirrors.
  Domain queries go through `persons`; direct reads of the mirrors are confined
  to `persons` validation/materialization and old-save compatibility fallback.

### C. Remaining causal legacy for the next hard cut

- `military::regiments_possible_from_pop`,
  `regiments_max_possible_from_province`,
  `main_culture_regiments_max_possible_from_province`,
  `mobilized_regiments_possible_from_province`,
  `find_available_soldier`, `find_available_soldier_anywhere`, and
  `can_pop_form_regiment` still use POP size/type/culture as regiment
  authorization. `update_recruitable_regiments` and
  `update_all_recruitable_regiments` publish those counts. Replace the old
  recruitment and reinforcement command path with `land_forces` assignment
  queries over canonical keys and template authorization.
- `military::regiment_calculate_reinforcement` still limits regiment strength
  using the size of its backing POP. `military::advance_mobilizations` retains
  a POP-to-regiment path when the canonical `land_forces` store is not active.
  Both are in the next canonical military hard cut.
- `military::apply_regiment_damage` now records aggregate deaths through the
  Person Kernel, but the selected victims are still inferred by reducing the
  regiment's backing POP. `disband_regiment_w_pop_death` also reduces backing
  POP size. Replace both with named canonical assignment casualty events; a
  POP reduction must not choose combat victims.
- `culture::rebels` derives possible rebel regiment counts and rebel faction
  capacity from POP size. This remains aggregate manpower causality and should
  move with rebel/army integration to the canonical assignment system.
- `economy::economy_pops` still uses POP savings and POP employment in
  aggregate household budgets, unemployment benefits, and daily cash flow.
  Exact accounts are canonical for person-owned balances, but these aggregate
  processes still make POP-level cash/employment assumptions. A later household
  economy cut must allocate individual causal changes through exact accounts
  and labor contracts, while preserving aggregate output as projection.

These remaining paths operate on aggregate or legacy regiment state; they do
not create a competing `person_key`. The military items are the required next
hard cut before legacy POP manpower can be considered non-causal. The aggregate
household and policy items need replacement behavior before those aggregate
systems can stop consuming POP rows.
