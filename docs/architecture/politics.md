# Politics

Politics decides who holds the offices of the [constitutional state](government.md) and what they do with them. It reads only canonical state: people's economic positions, the constitution, seats, and offices. No legacy party, ideology, issue, government type, slider, or population type enters.

```text
economy -> voters' interests -> parties -> elections -> seats and offices -> government -> fiscal law -> economy
```

The political runtime also records organization-level campaign resources and
information; see [Political power and information](political_power_and_information.md).
Effective control ties among parties, institutions, offices, and firms are
described in [Regime architecture and power topology](regime-architecture.md).

## Voters and interests

`governance::electorate` builds a nation's voters. There are two kinds:

- **Household cohort.** Votes as the bloc of its adult members (60% of its members). Its income per adult is the value its own production creates per worker. Its wealth is the cohort's liquid money.
- **Person with an individual budget.** Votes with the adults of their family. Their income is their wage. Their wealth is their cash and deposits. They count as a public employee if they work for an institution.

**Ideal positions.** A voter's ideal position follows from their position in the economy, against the national median income:

- **Higher income:** a lower tax level and less progressive taxes.
- **Public employment:** more of everything public.
- **Dependents:** more for schooling.
- **Wealth:** more for policing.
- **Rural livelihood:** more for public works and local government.

**Turnout.** Turnout rises with income, wealth, and public employment.

## Policy space

Parties compete over six dimensions. Each is a lever the fiscal law already has:

- **Tax level:** the base rate on wage income, from 0 to 40%.
- **Progressivity:** from 0 (one rate for all) to 1 (the poor pay 40% of the base rate and the rich pay twice it).
- **Budget weights:** education, policing, public works, and local government.

`governance::policy` turns a position into tax rates per stratum and appropriation shares. A position becomes law as a regulation enacted by the office holder empowered to regulate public finance, normally the finance minister.

## Parties

A party is an organization of one nation with a platform (a position) and members: persons ranked on its list. The first member is its leader.

**Founding.** At founding the electorate is clustered into up to four parties by weighted k-means on ideal positions, seeded deterministically. Each party then recruits members:

- first among its own supporters;
- people with individual budgets join themselves;
- cohorts send adults of their role from their province.

**Between elections, on the eve of each chamber election:**

- every party's platform moves 30% of the way toward the centre of its own voters;
- a party below 2% of the vote in two elections in a row dissolves;
- when at least 15% of the electorate is far from every platform, it founds a new party at its centre;
- lists are topped up to the number of seats plus six.

## Elections

The constitution's election rows give each elected body a system, a district rule, and a term.

| System | Used for | How it decides |
| --- | --- | --- |
| `proportional` | Chamber seats | Highest averages (D'Hondt) over party votes. |
| `plurality` | Chamber seats | Every seat of a district goes to the party with the most votes there. |
| `direct` | An office | The leader of the winning party takes it, after a runoff between the top two when nobody wins a majority. |
| `legislative` | An office | The leader of the party with the most seats takes it. |
| `presiding` | A chamber's presiding office | The leader of the chamber's largest party takes it. |
| `running_mate` | An office | The winner's party's next member takes it, together with the directly elected office. |

**Districts.** Districts are the nation, its regions (seats apportioned by adults with largest remainders), or the office's own territory.

**Counting votes.** Each voter splits their turnout over the parties by a logit on policy distance. Governing parties gain or lose twice the change in median income since the government formed, capped at 20%. Recorded campaign organizer payroll from the preceding 90 days adds a capped, logarithmic utility bonus. Votes are counted deterministically, without randomness.

**Taking office.** The winners take the seats in list order through ordinary office tenures. An office holder who wins an exclusive office leaves their other offices.

**Records.** Each election records its date, electorate, turnout, and each party's votes and seats. The first elections are held at founding; later ones fall due at the end of each term.

## Government

**The chief executive.** The chief executive is the office that appoints the finance minister.

**Parliamentary formation.** Where a chamber confirms the chief executive:

1. The leader of that chamber's largest party becomes formateur.
2. The formateur adds the parties nearest their platform until the coalition holds a majority.
3. The chamber confirms the formateur by party-line vote.
4. The constitutional appointer (the head of state) appoints them.

The outgoing chief executive resigns for a new government.

**Elected or hereditary chief executive.** Where the chief executive is elected or hereditary, their party governs.

**Ministries.** The chief executive appoints members of the governing parties to the ministries, shared among the parties by seats with highest averages.

**Programme.** The governing programme is the governing parties' platforms weighted by seats. A government without a party base serves the ideal of the wealthiest tenth of the electorate. Whenever the fiscal law differs from the programme, the finance minister enacts the programme.

**Confidence.** A chief executive whose office is removable by no confidence must keep a majority in the confirming chamber. When the governing parties no longer hold one, the opposition moves a motion of no confidence. If it passes, the chief executive is removed and a new government forms.

**Voting.** Members vote by party line: governing parties for the government's candidates and against no-confidence motions, the opposition the other way.

**Vacancies.** The holder of each appointing office fills vacancies:

- party offices with members of their own party, then of the coalition;
- the central bank, the judiciary, and the general staff with adults outside party politics;
- a seat whose holder died passes to the next member of their party's list.

An office whose appointer is vacant stays vacant. An institution never fills it or decides anything in its own name (see continuity and discretion in [Government](government.md)).

## Constitutional models

| Model | Elections |
| --- | --- |
| Parliamentary republic | Lower chamber by proportional representation; upper chamber by regional plurality; head of state chosen by the legislature. |
| Parliamentary monarchy | As above, but the monarch is hereditary. |
| Presidential | President and running mate elected directly; both chambers elected by regional plurality. |
| Semi-presidential | President elected directly; lower chamber by proportional representation. |
| Dual monarchy | Both chambers elected by regional plurality; the monarch appoints the chief executive. |
| Absolute monarchy, authoritarian | No elections; the ruler appoints. |

**Speakers and mayors.** Speakers are presiding offices. Capital mayors (and federal governors) are elected directly in their territory wherever the model holds elections.

## Not yet modelled

- Firm-initiated political contributions, donor dependence, and lobbying.
- Campaign staffing beyond party members and influence from unions or other organizations.
- Protests and coups.
- Policy beyond the fiscal law.
- Ideological identity that persists apart from economic interest.
