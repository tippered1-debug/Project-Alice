# Causality and projections

Canonical runtime records cause changes. Legacy aggregates and UI fields may supply explicitly authored scenario input, receive a one-way projection, or remain available to isolated compatibility callers. They are not read back to invent canonical transactions or physical state after a domain activates.

Current causal boundaries include:

- Canonical firms and concrete procurement use concrete fills and explicit bootstrap price inputs, not mutable legacy market prices.
- Canonical production consumes owned physical inventory. Aggregate market clearing does not create canonical input stock or output.
- Deposits come only from authored `deposits.csv` rows; province RGO fields seed nothing. Extraction runs through plants bound to those deposits, and runtime reserves, inventory, and shipment state belong to the canonical deposit and logistics records. See [Primary production](primary-production.md).
- Population counts, savings, employment, market totals, and military strength shown to existing consumers are projections where their canonical domain is active.

This is the authority rule: `canonical runtime -> projection -> legacy/UI`. There is no reverse read from a legacy projection to canonical state after activation. The relevant import exceptions and activation conditions are defined by each runtime domain's scenario contract; see [Scenario format](../runtime/scenario-format.md) and [Technology](technology.md).

The [canonical runtime contract](../migrations/canonical-runtime.md) defines load and action failures. The completed causality purge record is retained in [Archive](../archive/README.md).
