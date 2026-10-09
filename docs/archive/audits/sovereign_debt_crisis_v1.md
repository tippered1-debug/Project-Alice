# Sovereign debt crisis audit

Status: lifecycle audit plus a deterministic 540-day experiment implementation.
The experiment runner has not been run against a newly built executable, so
this document does not claim measured results.

## Experiment v1

`experiments/sovereign_debt_crisis/` contains the runner and scenario notes.
The native fixture recreates the same government, treasury, taxpayer, creditor
bank, opening deposit liability, and consented sovereign bond for each case.
It compares a balanced baseline, a day-90 persistent revenue shock, and that
shock paired with lower planned public-service spending. It uses the real tax
assessment and collection APIs, account transfers, debt servicing and default
lifecycle, and bank balance sheet.

The daily CSV reports tax assessed and collected, planned and funded service,
shortfall, debt principal and interest, treasury cash, creditor reserves and
claims, bank status, and account-cash conservation. The event CSV records bond
issuance, tax collection, public spending, debt service, and default. The
comparison runner repeats each case with the same seed and verifies checksum
reproducibility and the expected default/funding outcomes.

Recovery-adjusted bank net worth is reported as a stress sensitivity only. The
base banking system still carries a defaulted sovereign claim at face value;
the lab does not mutate that balance sheet or simulate a recapitalization.

## Existing causal links

| Link | Existing mechanism |
| --- | --- |
| Tax base → assessment → collection | `economy_government` assesses wages against enacted tax rules. Finance records tax obligations and transfers through real accounts. |
| Tax authority → finance ministry | `public_administration::allocate_budget` sweeps collected cash on the first of each month. |
| Investor → state | Public issuance checks an office's `issue_public_debt` authority, an accepted investment proposal, investor account balance, currency, and the law's debt ceiling. The proceeds move through the account ledger. |
| Debt → creditor | An obligation stores its debtor, creditor, currency, original and outstanding principal, rate, accrued interest, creation and due dates, and accrual date. |
| Treasury → institutions | Monthly appropriations transfer actual cash from the finance ministry to the named institution accounts. |
| Institutions → workers and services | Public payroll is settled from institution accounts; public-service delivery and procurement use those same accounts. Wage arrears and unpaid social claims are represented in their existing systems. |
| Household income → elections | The electorate reads worker income, employment and arrears. Election results include median-income performance and recorded campaign spending. |
| Government confidence | A no-confidence test checks the governing coalition's chamber seats. Treasury cash and public-debt status are not direct confidence conditions. |
| Bank creditor → balance sheet | Public-debt claims are assets of their named holder. Default status is now exposed as a separate face-value asset measure; there is no impairment or recovery-valuation rule for sovereign bonds. |

## Lifecycle rules added

- Interest is simple interest on outstanding principal, on an ACT/365 basis.
- Daily processing advances from the obligation's saved last-accrual date, so
  calling the lifecycle twice on the same date cannot accrue twice.
- At maturity, the debtor institution pays from its own treasury. Cash reserved
  for market bids and foreign-exchange orders is unavailable for debt service.
- Available funds can make a partial payment. Accrued interest is paid before
  principal. Every payment is a real account transfer and is linked to its
  obligation in the saved transaction ledger.
- An unpaid balance remains active and overdue on the maturity date. At
  maturity plus 30 days it becomes defaulted and receives a fiscal-action
  record. Default does not erase the obligation. A later payment can reduce a
  defaulted claim; full payment closes it as paid.
- On the first of the month, collected taxes are swept to the finance ministry,
  debt service runs, and the remaining treasury balance is appropriated by law.
  On other days, servicing uses only each debtor institution's extant treasury.

## Causal links still absent or unverified

- There is no automatic refinancing market or yield formation. New issues still
  need an explicit authorized issue and investor consent; an experiment must
  report experimental rate offers as exogenous terms.
- There is no sovereign-bond impairment rule. Defaulted bank-held claims remain
  at face value, so a default alone does not generate a booked capital loss.
- The government-confidence rule has no fiscal input. The political result may
  remain unchanged even when the treasury cannot meet obligations.
- Fiscal instruments and household/economic outcomes are implemented in
  separate existing systems, but a full-game 540-day crisis path through
  public-service quality, household consumption, party choice, election, and
  government replacement has not yet been demonstrated.
- The fixture is a small finance-only world, not a full-game scenario. It does
  not run production, household consumption, voter preferences, elections, or
  government replacement. It demonstrates fiscal distress, public-service
  funding shortfalls, and creditor exposure, but does not establish political
  contagion.
- No measured run output has been committed yet. Until the runner executes
  against a built binary, claims about the observed timing and magnitude of
  default, service shortfalls, or recovery are not supported.
