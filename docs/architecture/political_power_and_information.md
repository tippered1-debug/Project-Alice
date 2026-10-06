# Political power and information

Political power should grow out of organizations and information that already
have real actors, money, people, and dates. It extends the current
`economy -> voters -> parties -> offices -> law` loop without replacing its
constitutional rules.

## Political resources: first implementation

The first step connects personal money, party treasuries, campaign organizers,
and election results:

```text
supporter wallet -> party contribution -> party treasury -> organizer payroll
  -> campaign reach -> votes -> seats and offices -> enacted fiscal policy
```

When an election is due, each household cohort or individually budgeted voter
contributes 1% of spendable cash to the party whose platform is closest to
their interests. The payment is an ordinary money transfer. A party may then
spend up to 25% of its available treasury on up to twenty living party members
who work as campaign organizers. Those payments are recorded as
`campaign_expenditure` transactions.

Only recorded organizer payroll from the preceding 90 days affects vote choice.
Its effect rises logarithmically with spending relative to one day's median
payroll for roughly one organizer per fifty adults, with a capped utility
bonus. Cash therefore affects the result only after it moves from a supporter
to a party and from the party to a person doing campaign work. The same ledgers
expose balances and the source, recipient, amount, and date of each transfer.

The contribution and payroll API also accepts economic actors, so firm or
association funding can use the same money rails. Automatic firm funding,
campaign law, donor dependence, and lobbying are still future work. The current
fundraising behavior represents individual and household supporters.

## Information architecture

The world has objective facts, but decision-makers should act on dated reports
and beliefs rather than read every fact directly:

```text
world fact -> source -> report -> delay / confidence -> recipient belief -> decision
```

An information report records its reported value, fact date, publication date,
receipt date, confidence, source, recipient, and subject. Adopting a report
creates a dated belief for its recipient. Conflicting reports can revise that
belief over time.

The existing report-and-belief primitive now carries military and economic
power assessments as well as sovereign debt. Each month, a staffed and funded
intelligence agency reports on foreign countries within its access. The report
is sourced to the agency, delivered to the central government, and arrives
after 30 days. Its estimate approaches the measured relative power according
to agency readiness and access; confidence records that quality. Strategic
statecraft updates its military and economic views from received government
beliefs, then gradually returns stale views toward uncertainty. Intelligence
reports and their linked beliefs expire after a year.

Intelligence and counterintelligence agencies are constitutional institutions
with appointed director offices. Their staffing and treasury readiness come
from the ordinary institutional payroll and public-funding systems. The
policing appropriation is shared across policing, intelligence, and
counterintelligence services. Counterintelligence detection and strategic
decisions that retain explicit links to the reports they used are still future
work.

## Institutions and sequence

Political and social organizations use the existing actor and account model.
Parties, labor unions, and employer associations already have organization
records and financial actors. Intelligence and counterintelligence now use
constitutional institutions with real budgets, staff, directors, and service
mandates. Media and civil-society organizations, along with explicit access
and jurisdiction rules for information work, remain future additions.

The work is staged so later actions have a causal source and recipient:

1. Political resources: real contributions and organizer payroll connect
   supporters to campaigns and election results.
2. Information and belief: military and economic assessments flow through
   reports into the strategic statecraft views used by AI decisions.
3. Intelligence institutions: intelligence reports now depend on staffed,
   funded agencies, constitutional mandates, and known access relationships;
   counterintelligence operations remain to be connected.
4. Covert action: recruitment, leaks, deception, sabotage, and intervention
   follow once information and counterintelligence are in place.

That information layer can later connect statecraft to economic expectations:
an audit or intelligence report revises beliefs about public debt, bond sales
change refinancing costs, and the resulting fiscal pressure feeds back into
political support. Cross-border trade, customs, foreign exchange, sanctions,
and settlement can then meet political action through the same actors and
money flows.
