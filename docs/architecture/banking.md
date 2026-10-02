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

**Liquidity policy** (`economy::liquidity`). Each day every actor holding a deposit keeps its free wallet cash near 30% of its free liquid money, withdrawing below 20% and depositing above 40%. A depositor of a bank that is not solvent withdraws everything the bank can pay, so runs and contagion follow from balance sheets rather than scripts. An organization or household with more than 10 of idle cash and no deposit opens one at the solvent bank of its country with the most liquidity. Public treasuries hold base money, and persons bank only through a profile they already hold.

## Authoring

Banks, reserve policy, opening deposits, and declared loans are scenario-authored and balance-checked; see [Scenario format](../runtime/scenario-format.md). The original monetary ontology milestone is retained in [Archive](../archive/README.md).
