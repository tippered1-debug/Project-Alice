# OUR TIME runtime overview

The canonical runtime is the normal simulation. There is no Age of Transformation gamerule, runtime override, or parallel legacy economy selected by a ruleset switch.

```text
people and institutions
  -> exact jobs, wages, taxes, household accounts, and needs
  -> concrete goods orders, inventories, freight, and delivery
  -> firm and public decisions
  -> production, services, population change, and political outcomes
```

## Runtime ownership

| Domain | Runtime authority | DCON role |
| --- | --- | --- |
| Population | `exact_population` owns stable person identities, living membership, births, deaths, and cell transfers. | POP size is projected from living membership. POP savings is projected from exact household cash accounts. |
| Labor and households | Exact workers hold concrete job contracts; wages settle into person accounts; household purchases create person-backed orders. | Employment, labor prices, needs, and savings fields are views for existing screens and APIs. |
| Goods and production | Firms place input orders; factories consume physical inventory and produce goods; deposits and shipments represent extraction and delivery. | Market supply, demand, prices, inventories, and trade statistics are projected views. |
| Firms and credit | Firm actors own operating accounts and assets. Bank underwriting originates firm-linked credit and loan servicing uses canonical accounts. | Factory and national fields expose information to legacy UI and scripts; they do not create canonical cash. |
| Government | Public institutions assess taxes, maintain treasury accounts, employ staff, procure goods, and deliver services. | Budget fields expose the account-backed public position and policy settings. |
| Domestic policy | Interest groups, coalitions, and staged reform bills inform political authority and execution. | Party and issue identifiers remain content-facing identities and UI vocabulary. |
| Foreign policy | Strategic Statecraft owns alliance and crisis choices and is required for every loaded country. | Existing diplomacy structures supply game content and display state. |

The simulation writes DCON market and POP values through explicit projectors after canonical state changes. Scripts that attempt to alter canonical household cash or create factories without a canonical account or funded capital project fail loudly.

## Initialization and saves

A new scenario imports authored POP rows, factories, geography, and resource signals into canonical runtime records. Factories and deposits require authored firm/operator, asset, and ownership records; the importer does not create placeholder firms. A scenario missing that canonical ownership graph fails during initialization. Aggregate history is never reconstructed from an old campaign save.

The accepted save format, required sections, and restore validation are specified in [Serialization](../runtime/serialization.md).

## Running bounded simulation reports

The headless runner needs no ruleset override:

```sh
AliceIncremental 1.bin --days 365 --snapshot-every 5 --seed 424242 \
  --report-jsonl continuous.jsonl

AliceIncremental 1.bin --days 180 --snapshot-every 5 --seed 424242 \
  --save-at-end midpoint --report-jsonl before.jsonl

AliceIncremental 1.bin --load-save midpoint.bin --days 185 \
  --snapshot-every 5 --seed 424242 --report-jsonl after.jsonl

python3 scripts/compare_simulation_reports.py continuous.jsonl after.jsonl
```

For a baseline and report schema, see [Economy experiments](../runtime/simulation-reports.md).
