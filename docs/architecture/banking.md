# Banking

Canonical money is denominated in a settlement commodity and uses three balance instruments:

| Instrument | Meaning |
| --- | --- |
| Operating account | Spendable cash held by a firm, government, person, or other actor. |
| Base-money reserve | A bank's settlement reserve balance. |
| Bank deposit | A bank liability to a named account owner. |

Sparse exact-person accounts use the operating-account instrument; their storage in the exact-person ledger does not create a fourth instrument. A DCON monetary account is identified as a reserve account by its reserve-bank relation. Customer deposits live in a separate deposit ledger.

Transfers are posted through the ledger that owns the account. Cross-ledger settlement validates owner, settlement currency, balance, and transaction before changing either side. A settlement commodity named `money` is a unit of account; it does not by itself specify a central-bank issuance model.

## Deposits in circulation

Operating cash is base money. Moving it into or out of a bank changes the bank's balance sheet, not just two balances:

| Operation | Customer | Bank |
| --- | --- | --- |
| `deposit_cash` | wallet −X, deposit +X | reserves +X, deposit liabilities +X |
| `withdraw_cash` | deposit −X, wallet +X | reserves −X, deposit liabilities −X |

A bank pays out no more than its reserves hold; a withdrawal larger than its reserves is paid in part.

Illiquidity is not insolvency. A bank is `solvent` while it meets its capital and liquidity requirements, `constrained` while it is short of reserves or capital, and `insolvent` only when its net worth is negative or a capital shortfall outlasts its grace period. A reserve shortage alone keeps a bank constrained for as long as it lasts, and the bank recovers when reserves return; `insolvent` is final. The wallet must belong to the deposit's owner: the same actor, or the exact person whose profile actor owns the deposit.

`pay_from_deposit` pays with immediate settlement:

- to a deposit at the same bank: liabilities move between customers and reserves stay;
- to a deposit at another bank: liabilities move and the payer's bank pays the payee's bank in reserves at once (gross settlement);
- to a wallet: the payer's bank pays base money out of its reserves.

It fails without change when the payer's deposit or, for an external payment, the payer bank's reserves cannot cover the amount. Queued interbank instructions (`transfer_deposit` between banks) still clear daily with reserve netting.

**Market payments.** A buyer whose wallet cannot cover an order funds the bid from their bank deposit, if their bank is not insolvent; the bid reserves against that deposit instead of the wallet. On a fill the seller is paid into their deposit at the buyer's bank, else into a deposit at another bank, else into their operating wallet. Freight is still paid from the buyer's wallet.

**Liquidity policy** (`economy::liquidity`). Each day every actor holding a deposit keeps its free wallet cash near 30% of its free liquid money, withdrawing below 20% and depositing above 40%. A depositor of a bank that is not solvent withdraws everything the bank can pay, so runs and contagion follow from balance sheets rather than scripts. An actor with more than 10 of idle cash and no deposit opens one at the solvent bank of its country with the most liquidity. A person without a profile whose wallet holds more than 50 gains one first, so savers bank. Public treasuries hold base money. Deposits are the savings that investors commit to equity; see [Capital allocation](capital-allocation.md).

## Monetary policy

`economy::monetary_policy` gives each nation a central bank per settlement. The central bank is a state-owned organization, opened when the nation first has a configured bank. Its income goes to the government. Who controls it politically is not modelled yet.

- **Price index.** The cost of what buyers bid for in the nation's markets over the last 30 days, at today's reference prices relative to base costs.
- **Policy rate.** Every 30 days the central bank updates smoothed inflation, half from the latest month. The rule's rate is the 2% neutral real rate, plus inflation, plus half the gap between inflation and the 2% target, between 0 and 30%. The policy rate moves a quarter of the way toward the rule's rate. The first policy rate is the average rate the nation's banks were authored with.
- **Bank lending rates.** A bank lends at the policy rate plus a liquidity premium. The premium is 3% when the bank's reserves only meet its liquidity requirement and falls to zero at twice the requirement. Savings deposited as reserves therefore make credit cheaper.
- **Deposit interest.** Each deposit is credited monthly with 80% of the yield of its bank's performing loans, never more than the bank's lending rate. The interest is a bank liability, not new base money.
- **Discount window.** Every day a bank that is short of reserves can borrow the shortfall from the central bank, unless it is insolvent. It borrows at the policy rate plus 2%, for 30 days, against at most half of its performing loans. The lent reserves are new base money, recorded as issued by the central bank. The bank repays out of reserves above its requirement; repaid principal retires the money, and interest is central bank income. A bank that cannot repay at maturity rolls the loan over.

The sum of all operating, reserve and exact accounts, less money issued by central banks, is conserved by every operation.

## Authoring

Banks, reserve policy, opening deposits, and declared loans are scenario-authored and balance-checked; see [Scenario format](../runtime/scenario-format.md). The original monetary ontology milestone is retained in [Archive](../archive/README.md).
