# Banking

Canonical money is denominated in a settlement commodity and uses three balance instruments:

| Instrument | Meaning |
| --- | --- |
| Operating account | Spendable cash held by a firm, government, person, or other actor. |
| Base-money reserve | A bank's settlement reserve balance. |
| Bank deposit | A bank liability to a named account owner. |

Sparse exact-person accounts use the operating-account instrument; their storage in the exact-person ledger does not create a fourth instrument. A DCON monetary account is identified as a reserve account by its reserve-bank relation. Customer deposits live in a separate deposit ledger.

Transfers are posted through the ledger that owns the account. Cross-ledger settlement validates owner, settlement currency, balance, and transaction before changing either side. A settlement commodity named `money` is a unit of account; it does not by itself specify a central-bank issuance model.

Banks, reserve policy, opening deposits, and declared loans are scenario-authored and balance-checked; see [Scenario format](../runtime/scenario-format.md). The original monetary ontology milestone is retained in [Archive](../archive/README.md).
