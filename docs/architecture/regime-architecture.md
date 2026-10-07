# Regime architecture and power topology

A country's regime is represented by several different graphs. The graphs
answer different questions and do not substitute for one another:

1. **Institutional hierarchy** says which public institution sits under which
   institution.
2. **Legal authority** says which institution or office may take a public
   action, for what jurisdiction, and under which legal instrument.
3. **Economic ownership** says who owns an asset and who holds its voting and
   economic claims.
4. **Effective power topology** records which organizations and officeholders
   supervise, appoint, command, embed within, or otherwise influence one
   another.
5. **Political resources and information** record transfers, campaign work,
   reports, and beliefs that can explain how influence is acquired and used.

```text
constitutional state ──legal authority──> public action
organizations ──effective ties───────────> institutions, offices, firms
owners ──ownership stakes────────────────> assets
money and information ──causal flows─────> decisions and support
```

The runtime types live in `governance::power_topology`. Every edge connects
two graph nodes. A node is either an economic actor (including a person,
organization, or institution) or a constitutional office. This lets a party
relate to a ministry, a party committee relate to a firm, an individual belong
to that committee, and an organization relate to a specific office without
flattening those cases into a country score.

## Effective relationships

The saved graph supports these relationship kinds:

| Kind | Meaning |
| --- | --- |
| `appoints`, `confirms`, `dismisses` | Effective role in selecting or removing officeholders. |
| `commands`, `supervises`, `vetoes` | Direction, oversight, or a practical blocking position. |
| `controls_voting`, `controls_management` | Effective control that may differ from a legal shareholding or board rule. |
| `funds`, `funding_dependence` | A funding tie or dependence; the edge itself transfers no money. |
| `embeds_organization` | One organization maintains an organization inside another. |
| `member_of` | A person actor belongs to an organization, including an embedded body. |
| `nominates_cadres` | An organization supplies or selects personnel for another body. |
| `sets_strategic_direction` | Strategic direction outside an ordinary corporate or administrative chain. |
| `political_influence` | A named influence relationship between two actors. |

Each edge records an intensity in `[0, 1]` and a half-open date interval
`[valid_from, valid_until)`. The intensity belongs to that specific tie; it is
not a universal measure of national power. Empty dates mean an open boundary.
Revocation sets the exclusive end date and preserves the historical record.
Queries return only active edges in stable relationship-ID order.

The C++ API exposes `create`, `revoke`, `valid_at`, `from`, and `to` for dated
graph updates and lookups. Scenario-authored edges use the same validation and
storage path.

Legal authority remains constitutional: an effective tie cannot authorize a
public action or appoint someone contrary to the constitution. Active ties now
change who can exercise authority inside those legal boundaries.

## Runtime effects

**Appointments and dismissal.** `offices::appoint` first checks the named
appointer, that office's legal authority, and any required chamber confirmation.
Then it checks active topology. A `vetoes` edge to the office, its institution,
or the candidate blocks the appointment. `appoints`, `controls_management`, or
`nominates_cadres` edges to the office or its institution require the candidate
to be a member of a source organization or holder of a source office. Explicit
`member_of` ties supply organization membership; existing party membership also
counts when the source is a party. `confirms` ties restrict who may cast votes
in the legally required confirmation chamber. Active `dismisses` ties similarly
require the lawful initiator to belong to a dismissing organization or hold its
source office. A topology veto can block dismissal. Elections apply the same
candidate gate; `controls_voting` can restrict who may fill elected seats.

**Military command.** Exact-person command actions still require an office
holder with constitutional `command_forces` authority. If a scenario authors an
active `commands` edge into its military-command institution or the exercising
office, the person must match that command source or belong to its organization.
Without an authored command edge, the constitutional chain remains sufficient.

**Company decisions.** A person with majority voting ownership of a company's
equity can approve its investment proposals. Active `controls_voting` ties
provide the same investor authority to members of the controlling organization.
Active `controls_management`, `appoints`, or `nominates_cadres` ties let their
members make the company's existing person-level borrowing, lending, and
investment decisions. The company remains the contracting actor and supplies
the cash. This connects power to real consent and settlement; the project still
does not have a persistent board-seat roster or board election process.

These hooks keep ownership, effective control, legal authority, and money
transfers as separate records. A topology edge grants decision access only
where the corresponding runtime action already exists; it never transfers
ownership or cash by itself.

## Scenario authoring

`common/canonical_runtime/power_organizations.csv` can declare non-firm
political and social organizations before the relationship rows are loaded.
It has the header `organization_key;country;kind`. Supported kinds are
`political_organization`, `party_committee`, `media`, `foundation`,
`civil_society`, `security_service`, `labor_union`, and
`employer_association`. The key is stable and is used in relationship
endpoints; these organizations have members, not shareholder equity.

`common/canonical_runtime/power_relationships.csv` is optional. It has this
header:

```text
country;source;relationship;target;intensity;valid_from;valid_until
```

`country` is an active three-letter country tag. Endpoints are:

- `institution:<key>` for an institution in that country's scenario
  constitution;
- `office:<key>:<seat>` for a one-based seat in that constitution;
- `person:<cell>:<ordinal>` for a living exact person;
- `actor:<canonical_id>` for a loaded economic actor;
- `organization:<key>` for a key in `power_organizations.csv` or a firm ID;
- `organization:<canonical_id>` for a loaded organization with that canonical
  ID.

The relationship column uses one of the kinds listed above. Dates use
`YYYY-MM-DD`; blank dates are open. The loader rejects unknown endpoints,
unknown relationship kinds, invalid intensities, and duplicate or empty
intervals.

For example, a political organization can set strategic direction for the
cabinet, embed a committee in a state-owned company, and nominate a candidate
for the chief executive office. A person can be recorded as a committee member:

```text
organization_key;country;kind
communist_party;CHN;political_organization
steel_committee;CHN;party_committee
```

```text
country;source;relationship;target;intensity;valid_from;valid_until
CHN;organization:communist_party;sets_strategic_direction;institution:cabinet;1;;
CHN;organization:steel_committee;embeds_organization;organization:state_steel;1;;
CHN;person:17:0;member_of;organization:steel_committee;1;;
CHN;organization:communist_party;nominates_cadres;office:chief_executive:1;0.8;;
```

The firm ID `state_steel` must exist in `firms.csv`. Institution and office
keys must exist in a constitution selected in `constitutions.csv` or authored
in the country's constitution tables. These rows describe the political
topology; they do not imply that the referenced organizations are legally
part of the state.

## Regime change

A reform, coup, or revolution can end edges and add new ones on its effective
date. Institutions and firms can continue to exist while their appointment,
supervision, command, or embedded-organization ties are rebuilt. The graph
therefore represents changes in control without encoding regime identity as
an ideology enum or a national modifier.

The topology API and scenario loader establish the durable relationships.
Simulation systems can query those relationships when implementing cadre
selection, state-enterprise oversight, command, or institutional capture.
Formal authority remains the source of legal permission; effective topology
records who can shape decisions in practice.

See [Government](government.md), [Politics](politics.md), [Ownership](ownership.md),
and [Political power and information](political_power_and_information.md).
