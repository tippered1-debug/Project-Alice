# Government

Public power belongs to concrete institutions and offices. Every public action follows one chain:

```text
person -> office (tenure) -> institution -> authority grant -> jurisdiction -> legal or fiscal action -> world state
```

An institution may also act in its own name under its own grants: a ministry paying staff, the tax authority assessing taxes, a central bank lending reserves. No action happens because a nation exists, or because of a legacy flag. The modules are in `src/governance`.

## Institutions

A state is a tree of institutions under a root `central_government` institution. That root is the state as a legal person: the owner of public property and the heir of last resort. It holds no power of its own.

| Institution kind | Role |
| --- | --- |
| `head_of_state` | Presidency or crown. |
| `cabinet` | The executive government. |
| Ministries | `finance_ministry`, `interior_ministry`, `education_ministry`, `public_works_ministry`, and generic `ministry` or `agency` kinds. |
| `tax_authority` | Revenue collection. |
| `military_command` | Command of the armed forces. |
| `central_bank` | Currency issue and monetary policy. |
| `court`, `prosecutor` | Judiciary and prosecution. |
| `legislature`, `legislative_chamber` | Lawmaking. |
| `regional_government`, `municipality` | Territorial government. |

**Mandate.** Each institution carries a mandate:

- a service (administration, education, policing, construction, revenue, monetary, defense, legislation, or justice);
- staff per inhabitant of its jurisdiction, the occupation of that staff, and their wage multiplier;
- an independence flag.

**Territory.** Territorial institutions govern a territorial unit:

- one regional unit per state definition the nation owns land in;
- the capital's municipal unit inside its region.

A province belongs to its innermost unit.

## Authority

An authority grant has:

- a holder, which is an institution or an office;
- a power: `legislate`, `regulate`, `administer`, `levy_tax`, `spend_public_funds`, `appropriate`, `issue_public_debt`, `appoint`, `dismiss`, `confirm`, `assent`, `command_forces`, `adjudicate`, `enforce`, `license`, `issue_currency`, `expropriate`, or `amend_constitution`;
- a jurisdiction: a nation or a territorial unit;
- a validity interval;
- the legal instrument it rests on (the constitution, for constitutional powers).

**Jurisdiction is exact.** A national grant does not cover a territorial action, and a territorial grant does not cover a national one. A grant over a region covers the municipal unit nested inside it.

**No inheritance.** An institution does not inherit its parent's powers.

**Delegation.** A grant can be delegated to another holder within its jurisdiction. A national power may be narrowed to one of the nation's territorial units. A delegated grant lasts only while its delegator is valid.

**Revocation.** Revoking a grant ends it from that date, together with every grant delegated from it. The history is kept.

## Continuity and discretion

An institution keeps executing what law and budget already decided while its offices are empty. In its own name it may:

- administer;
- levy the taxes the law sets;
- spend its treasury;
- disburse appropriations;
- operate its mandate (a central bank issuing currency by its rule);
- enforce.

Every other power is discretionary:

- legislating and regulating;
- appointing, dismissing, confirming, and assenting;
- borrowing;
- licensing and expropriating;
- adjudicating;
- commanding forces;
- amending the constitution.

A discretionary power is exercised only by a person through an office, or by a chamber through its members' votes. An institution may hold a discretionary grant only as a link in a delegation chain, and it never acts on one in its own name (`governance::acts_administratively`). An empty ministry therefore keeps paying its staff, but it cannot change tax policy, issue a regulation, or appoint anyone.

## Offices

An office is a slot in an institution with constitutional rules:

- **Appointer.** The office whose holder may fill it, using that office's `appoint` power over the office's jurisdiction. An office without an appointer is filled only by founding (and, in the next layer, by election).
- **Confirmer.** An optional legislative chamber that must first pass the candidate by majority.
- **Removal.** Removal is by the appointer, by a named remover office, or not at all. Removal uses the `dismiss` power.
- **Term.** A term in days, or indefinite. Expired terms end daily.
- **Succession.** On a vacancy, from death, resignation, dismissal, or term end, nobody may act; or the successor office's holder acts; or the successor takes the office in full and leaves their own.
- **Exclusivity.** The holder of an exclusive office holds no other office.

A tenure records the person, start, end, term end, and whether it is acting. When a person dies, all their tenures end in the death procedure itself and each office's succession rule applies. No power is exercised through a former or dead holder.

## Constitutions as data

A constitution is three row sets. The format is in `governance/constitution.hpp`.

- **Institutions:** kind, parent, scope (national, regional, or capital), independence, and mandate.
- **Offices:** institution, kind, appointer, confirmer, removal, term, succession, exclusivity, and seats.
- **Authorities:** holder, power, scope (national or territory), and either the constitution as source or the holder it is delegated from.

One founder builds every model from rows. It also validates them and fails before founding on:

- unknown kinds;
- cycles in the institution tree;
- missing references;
- non-chamber confirmers;
- national powers given to territorial holders, or the reverse;
- delegations from a holder without an earlier grant of that power.

The built-in executive models are:

| Model | Head of state | Chief executive | Legislature | Central bank |
| --- | --- | --- | --- | --- |
| `parliamentary_republic` | Speaker acts on a vacancy | Appointed by the head of state, confirmed by the lower chamber | Two chambers | Independent |
| `parliamentary_monarchy` | Hereditary (full succession by the heir) | Appointed by the head of state, confirmed by the lower chamber | Two chambers | Independent |
| `presidential` | The president is also chief executive (full succession by the vice president) | — | Two chambers | Independent |
| `semi_presidential` | President appoints the chief executive and commands the forces | Appointed by the president, confirmed by the lower chamber | Two chambers | Independent |
| `dual_monarchy` | Monarch appoints and dismisses the chief executive freely | Appointed and dismissed by the monarch | Two chambers | Under the cabinet |
| `absolute_monarchy` | The monarch legislates | — | Advisory chamber | Under the cabinet |
| `authoritarian` | The leader appoints a one-chamber legislature | — | Appointed chamber | Under the cabinet |

The territorial models are:

- `unitary`: regions and the capital act under powers delegated from the cabinet and the finance ministry, and governors are appointed;
- `federal`: regions hold their own constitutional powers to administer, spend and tax, and governors regulate in their territory.

**New game.** A nation that has no constitution is founded from the model its legacy government type suggests, unitary. The government type is read only at this point, as bootstrap input. Founding seats living adults of the nation in every office.

**Regions gained later.** A region acquired after founding gets a regional government cloned from an existing one, with its offices vacant.

## Legislature

A legislature institution has one or more chambers. A chamber's seats are offices of kind `legislator`, held through ordinary tenures. A seat holder votes; a later vote by the same person replaces the earlier one.

**Passing a motion.** A motion passes when votes in favor, cast by the persons holding seats on the decision date, exceed a threshold of filled seats:

- a simple majority;
- more than two thirds for constitutional amendments.

Confirmation motions decide on a named candidate for a named office.

## Law

Every state rule is a legal instrument: a constitution, a statute, a regulation, or an administrative act. It records:

- jurisdiction;
- issuing institution;
- authorizing office;
- enacting person;
- enactment, effective, and repeal dates;
- status;
- policy rules.

**Required power.** Each kind requires a power: `amend_constitution`, `legislate`, `regulate`, or `administer`.

**Enactment through chambers.** Where chambers hold that power over the jurisdiction, every such chamber must have passed the instrument. The enacting person must hold an office in one of those chambers. Where an office holds `assent`, its holder must have assented. Such an instrument is issued by the legislature.

**Enactment through an office.** Otherwise, the enacting person must exercise an office holding the power: a presidential or royal decree, or a ministerial regulation.

**Policy rules.** Policy rules are typed. A new policy area adds a rule kind; the enactment machinery does not change. Current kinds:

- public debt ceiling and prohibition;
- income tax rate per stratum;
- appropriation share per institution;
- monthly disbursement rate;
- central bank inflation target.

**Which rule applies.** For each rule, the newest effective instrument of the jurisdiction decides.

## Public finance

Money moves only between real accounts; allocating a budget never creates money.

1. The tax authority assesses wage taxes under its delegated `levy_tax` power, at the rates of the fiscal law in force. Without a law, nothing is due.
2. On the first of the month, the tax authority hands collected revenue to the finance ministry's national treasury.
3. The finance ministry disburses the share the law sets. It transfers to each institution the law appropriates, using its `appropriate` and `spend_public_funds` powers.
4. Institutions spend their own treasuries on staff, procurement, and projects under their own spending powers:
   - ministries recruit staff where they govern, paid from their own treasury;
   - municipal administrations buy office supplies;
   - the public works ministry commissions and pays for public construction.

## Legacy inputs

The tax and spending sliders are policy inputs, not authority. When they differ from the fiscal law in force, the holder of the office empowered to regulate public finance issues a regulation and repeals the one it replaces. That office is the finance minister where one exists. With no such holder, the sliders change nothing.

Budget figures on nations and provinces are interface projections.

Ruling party, reforms, and national modifiers still feed population politics. No state action covered here reads them.

## Central bank and armed forces

**Central bank.** The central bank organization (see [Banking](banking.md)) is the economic arm of the constitutional `central_bank` institution. It reviews rates and lends reserves only while that institution holds `issue_currency` over the nation. Its inflation target is set by law.

Who appoints and may remove its governor, and whether it is independent of the cabinet, are constitutional rows.

**Armed forces.** A formation answers to its nation's military command. Orders (`governance::command`) need a person exercising an office with `command_forces` over the formation's nation: the commander in chief under the constitution, or the chief of the general staff under the command's delegated power.

## Not yet modelled

The next layer decides who gets these offices and why. That covers:

- elections and parties;
- coalition bargaining;
- the political behavior that fills vacancies and votes in chambers.

Until then:

- seats and elected offices have no term;
- vacancies stay open unless a succession rule fills them;
- legislators vote only when told to.

See [Actors](actors.md), [Banking](banking.md), [Ownership](ownership.md), and [Scenario format](../runtime/scenario-format.md).
