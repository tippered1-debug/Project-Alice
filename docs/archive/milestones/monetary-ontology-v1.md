# Monetary Ontology

> Historical project record. This file preserves a past milestone or audit and is not a current runtime contract. See the [current architecture map](../../architecture/README.md) and [migration status](../../migrations/README.md).

Canonical money balances use three instruments, all denominated in a
settlement commodity:

| Instrument | Ledger | Meaning |
| --- | --- | --- |
| Operating account | `monetary_account` without a reserve-bank relation | Spendable cash held by a firm, government, organization, or other actor. |
| Base-money reserve | `monetary_account` with a reserve-bank relation | A bank's settlement reserve balance. This is the modeled base-money balance. |
| Bank deposit | `deposit_account` | A bank liability to its account owner. |

Sparse exact-person accounts are operating accounts backed by the exact-person
ledger. They are a storage choice, not a fourth balance instrument. A DCON
`monetary_account` is classified by its reserve-bank relation; the separate
`deposit_account` ledger is classified as a bank deposit.

`economy::monetary::ontology` is the shared read boundary. It reports an account's
instrument, backing ledger, owner, settlement commodity, and balance. Existing
banking and operating-account transfer paths remain responsible for posting
changes to their own ledgers, including reserve clearing and deposit settlement.

The legacy `economy::money(0)` value names a settlement unit of account. It does
not mean that every account is a deposit, identify who issued the unit, or
provide a central-bank issuance model. The current base-money quantity is the
bank reserve balances represented in the reserve-account ledger.
