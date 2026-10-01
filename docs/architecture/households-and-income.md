# Households and income

Two invariants hold for the canonical economy:

- every living person belongs to exactly one active economic budget;
- every income — wages, self-employment, rent, interest, dividends, inheritance, transfers — reaches a concrete actor's ledger, where it is consumed, saved, or invested.

## Budgets

| Budget | Who | Ledger | Consumption |
| --- | --- | --- | --- |
| Individual | A person with needs of their own: a worker under contract, or someone living on cash of their own | The person's exact account | Exact-person bids and own stock |
| Family | The living dependents of an individual workforce anchor (the next three ordinals) who share their population and have no budget of their own | The anchor's exact account | The anchor's needs are the per-capita profile times the family size |
| Household cohort | Every other living person of a free role in a province | The cohort's operating account | The cohort's own stock first, then ordinary bids |
| Military | People assigned to a formation | Formation supply | Army stockpiles |
| Bonded | Slaves | None (deferred) | None |

Cohort roles map every free POP type: `peasant` (farmers and labourers), `landed` (aristocrats), and `urban` (every other free type). A province with living people of a role must declare that role's cohort. Membership is never stored: a cohort's members are the living people of its role in its province minus individual budgets, their families, and soldiers, recounted daily. `households::audit` reports where every living person's budget is; outside bonded labor the uncovered count is zero.

POP types are a read model. A person's role selects their cohort; it never drives prices, wages, or consumption.

## Transitions

- **Cohort → wage work.** Local recruitment draws a cohort member for an offer whose commute-adjusted daily wage exceeds 1.1 times the cohort's reservation wage: the smoothed daily value added per worker of its own farms and workshops. Once hired, the person and their family hold an individual budget.
- **Wage work → cohort.** A displaced worker keeps their budget for a 30-day job-search window, then returns. Any other individual consumer with no contract and no cash returns immediately. Returning moves their goods and cash to the cohort and ends their individual needs (`households::return_to_household`).
- **Self-employment.** A peasant cohort works its farms with its members' labor, split by land. An urban cohort works workshops: ordinary factories for craft recipes (content production types with `type = artisan`). Members work only the crafts whose output is worth more than their inputs at local prices, split equally; they buy inputs through ordinary factory procurement, keep a day of inputs from being eaten, and sell output through the cohort's asks. Self-employed labor is unpaid and bounded by the members' own work, not by installed capacity.
- **Death.** `estates::process` moves a dead person's cash, goods, ownership stakes, and bank deposits to their heir: the cohort of their role in their home province, or else the treasury of the nation owning it.

## Capital income

Every actor holds cash in one ledger per settlement (`economy::wallets`): a person in their exact account, a government institution in its treasury, an organization or household in its operating account. A person therefore invests, receives income, and consumes from the same money. Capital owners declared as persons hold their opening cash in their exact account.

- **Dividends** (`economy::dividends`): every 30 days, staggered, a firm pays half its positive retained earnings, limited to cash above 90 days of recent operating costs and unpaid wages. A firm with no plants, deposits, projects, or active debts returns all of its cash, first as earnings and then as paid-in capital. A bank pays from reserves only while solvent, with its capital ratio kept 2 points above its minimum and liquidity at its requirement. Payouts go to equity owners by economic fraction: companies, banks, governments, households, and persons.
- **Rent**: tenants pay land owners in cash every 30 days and in kind at harvest.
- **Owner contributions and investment**: owners recapitalize firms and found companies from their own ledger.
- **Public contractors** are owned by the commissioning government.
- **Closed plants** keep their goods as the operator's property.

## Outside the canonical economy

Only bonded labor (slaves, and serfs bound to an estate) remains outside a free budget. It is deliberately deferred.
