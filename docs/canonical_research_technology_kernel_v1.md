# Canonical Research and Technology Kernel v1

The kernel makes organization capability the runtime technology fact. It does
not infer capabilities from a nation's Victoria technology or invention bits.
The causal path for the first production slice is:

`exact person -> research organization -> funded program -> capability holder -> transfer -> local adoption -> factory process`

Capabilities use authored stable keys hashed with a namespaced FNV-1a function.
The kernel contains no named modern-technology enum. Domain names, effort, and
process links come from scenario data.

## Scenario activation and files

Place these UTF-8, semicolon-separated tables in
`common/canonical_runtime/`. The presence of `capabilities.csv` activates the
canonical technology runtime. When it is present, every table below is
required, including header-only tables. An absent `capabilities.csv` leaves an
older scenario in the isolated legacy compatibility path; it does not import
legacy flags into the canonical kernel.

| File | Columns | Purpose |
| --- | --- | --- |
| `capabilities.csv` | `capability_id;domain;research_effort;codified;transferable` | Defines stable capability keys, domain key, required effort, and transfer policy. The two flags are `0` or `1`. |
| `capability_prerequisites.csv` | `capability_id;prerequisite_id` | Requires the same organization to hold each prerequisite at maturity 0.5 or above. References must resolve and cycles are rejected. |
| `capability_processes.csv` | `capability_id;factory_type` | Connects a capability to an exact existing factory type identifier. The factory type key is also stored as a namespaced stable process ID. Each listed requirement for a factory type must be usable by its operating organization. |
| `research_sites.csv` | `site_id;province_id` | Adds stable research site IDs at original scenario province IDs. An existing asset site with the same `site_id` may be reused if it is in that province. |
| `research_organizations.csv` | `research_organization_id;firm_id;site_id;role;effectiveness` | Binds a stable research identity to an existing firm organization, site, role, and effectiveness in `[0,1]`. Roles: `university`, `public_laboratory`, `corporate_rd`, `military_government`, `other`. |
| `research_funding.csv` | `funding_id;source_firm_id;research_organization_id;settlement;amount;date` | Transfers opening funds from the source firm's existing account into the research organization's existing account. Settlement must match. Transfers use the normal money ledger and fail if funds are unavailable. |
| `capability_holders.csv` | `firm_id;capability_id;maturity` | Authored opening knowledge for an existing organization, with maturity in `[0,1]`. Maturity below `0.5` cannot satisfy prerequisites or be adopted. |
| `research_programs.csv` | `program_id;research_organization_id;capability_id;status;start_date;opening_effort;cost_per_effort` | Creates persistent programs. Status is `planned`, `active`, `paused`, `completed`, or `cancelled`. Required effort comes from the target capability. |
| `research_assignments.csv` | `program_id;person;allocation_fraction;productivity_input` | Assigns an exact living person using `source_population_cell:ordinal`, with allocation in `(0,1]`. Productivity `0` derives the v1 literacy proxy; otherwise it is authored in `[0,1]`. |
| `capability_adoptions.csv` | `firm_id;capability_id;date` | Applies opening adoption only when that organization already holds usable knowledge and prerequisites. |
| `capability_transfers.csv` | `transfer_id;source_firm_id;destination_firm_id;capability_id;date;authorized_relation` | Records an explicit authorized transfer. The capability must be transferable and held by the source. The destination receives the source maturity and must separately adopt it. |

All references resolve against the existing canonical firm, site, person,
factory-type, and commodity tables. Missing references, duplicate IDs,
allocation over 1.0 across programs, dead staff, invalid funding/progress, and
invalid holder or adoption relations reject scenario loading. IDs use the
canonical runtime key syntax already used by the firm and asset tables. Dates
use `YYYY-MM-DD` and opening transfers/adoptions/funding cannot be dated after
the selected scenario date.

`research_funding.csv` moves actual account balances between distinct firm
accounts. A corporate research organization can use its own firm's opening cash
directly, so it does not need a self-transfer funding row. During play, assigned
researchers must be alive and work-eligible. Their productivity is explicit or
derived as `0.1 + 0.9 * literacy`, multiplied by assignment allocation and
research-organization effectiveness. The program pays the resulting effort
cost from its organization's account to exact-person accounts. Empty staffing,
zero available funds, reserved funds, paused programs, and unmet prerequisites
stop progress. Programs advance in stable-ID order; planned programs are
selected deterministically per research organization. No research RNG is used.

## Causality and compatibility

In canonical mode, the legacy adapter boundary disables nation research-point
generation, AI nation research selection, technology/invention repopulation,
legacy technology modifier recreation and application/removal, invention
discovery, and invention-count demand scaling. There is no reverse mapping from
`active_technologies`, `active_inventions`, or `current_research` into
capabilities. Existing Victoria state remains available to old scenario
parsing, UI, and scripts as compatibility data.

For a mapped factory type, firm agency and physical production require the
factory operator organization to hold the capability at maturity 0.5 or
above, have adopted it, and satisfy its prerequisites. A national legacy flag
cannot pass this gate. Factory types without canonical requirement rows keep
their existing behavior so this v1 slice does not claim to migrate every old
process. The public `organization_can_use_capability` API provides the same
holder/adoption/prerequisite check for a future equipment-model integration;
military combat and equipment definitions are unchanged.

The save schema is versioned independently inside the exact-runtime extension.
The technology snapshot is written in both scenario data and the exact-runtime
save payload. Save loading restores the DCON world first, then validates
technology references against it. `deterministic_checksum` visits sorted
capability, prerequisite, process, organization, holder, program, assignment,
adoption, and transfer rows, and includes research account balances and
reservations.

## Example

```csv
# capabilities.csv
capability_id;domain;research_effort;codified;transferable
precision_process;industrial_process;180;1;1
```

```csv
# research_organizations.csv
research_organization_id;firm_id;site_id;role;effectiveness
public_lab;national_research_firm;capital_lab;public_laboratory;0.9
```

```csv
# research_programs.csv
program_id;research_organization_id;capability_id;status;start_date;opening_effort;cost_per_effort
precision_program;public_lab;precision_process;active;2010-01-01;0;25
```

The remaining files still need their exact headers when empty. A receiving
factory operator must have an explicit transfer and adoption row before it can
run the mapped process.
