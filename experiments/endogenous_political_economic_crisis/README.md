# Endogenous Political-Economic Crisis v1

This experiment joins the canonical supply-chain, exact-worker, firm, household, treasury, tax, bank, sovereign-bond, election, coalition and statute systems in one three-country state. It calls `economy::daily_update` once per date; the experiment records canonical state and events but does not set layoffs, election results, coalition membership, bank losses or defaults.

## Run

Build Alice, then run the committed experiment set with:

```sh
python3 experiments/endogenous_political_economic_crisis/run_experiment.py \
  --binary build/macos-arm64-release/Alice \
  --output experiments/endogenous_political_economic_crisis/results/seed_424242
```

The runner writes daily CSV and JSONL, event CSV and JSONL, per-run metadata, a repeatability audit, a comparison CSV/JSON and a concise report. It repeats the countercyclical policy run with the same seed and records whether the checksum matches.

The default horizon covers the embargo (days 90–179), the 120-day constitutional election, subsequent government action, and the initial bond's maturity and statutory default grace period. The recorded outputs, not the scenario names, determine which effects are reported.

## Scenario definitions

- `baseline`: no trade restriction.
- `trade_shock`: a canonical A–B embargo for 90 days.
- `countercyclical_policy`: the same embargo plus a rule that compares actual B wage-tax receipts over a rolling 30-day window with the same run's 30-day pre-shock receipts. If the rolling receipts fall below 80%, the current finance minister may offer a sovereign bond for the measured quarterly revenue gap. Bank consent, treasury authority, the debt law, and reserve settlement can accept or reject the action.

The policy case is the alternative government decision compared with keeping the original fiscal response. Elections, coalition formation and enacted budget statutes continue in every case through the constitutional runtime.

All three countries have extraction or manufacturing firms, exact residents, money accounts, workers and party competition. A and C extract ore; B manufactures the finished good. B's bank buys its initial bond through an accepted consent proposal and authorized debt issuance. The bank report distinguishes the bond's legal face claim from its carrying asset after default. Available factory credit is quoted from the local bank's current capital, reserve and borrower-exposure limits.

## Integrity checks

The run records the final canonical checksum, exact household invariants, banking validation, total money conservation, election result events, coalition changes, enacted instruments, tax receipts, public debt, legal default status, bank capital and available new credit. The integration tests also exercise save/load continuation and deterministic seed replay.
