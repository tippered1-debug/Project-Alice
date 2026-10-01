# OUR TIME architecture

This is the entry point for the architecture that describes the current runtime. Each domain has one canonical owner; other representations are authored inputs or one-way read projections.

**Authority rule:** `canonical runtime -> projection -> legacy/UI`. After a domain activates its canonical runtime, legacy or UI state is never read back to reconstruct canonical state or transactions. A documented scenario activation boundary may keep a domain on its compatibility path before activation; it does not permit reverse reads after activation.

| Domain | Responsibility | Read next |
| --- | --- | --- |
| World | Geographic places, sites, and physical routes used by simulation. | [World](world.md) |
| Persons | Stable human identity, lifecycle, and current membership. | [Persons](persons.md) |
| Actors | Organizations and economic agents that hold accounts, assets, and obligations. | [Actors](actors.md) |
| Economy | Labor, household exchange, production, and market activity. | [Economy](economy.md) |
| Households and income | Budgets of every living person, transitions, dividends, inheritance. | [Households and income](households-and-income.md) |
| Primary production | Deposits, land, farms, extraction, and leases. | [Primary production](primary-production.md) |
| Banking | Operating money, reserves, deposits, loans, and settlement. | [Banking](banking.md) |
| Ownership | Explicit owners and stakes in productive assets and firms. | [Ownership](ownership.md) |
| Government | Public institutions, treasury, and account-backed public activity. | [Government](government.md) |
| Military | Canonical formations, assigned people, equipment, and supply. | [Military](military.md) |
| Technology | Research organizations, capabilities, and adoption. | [Technology](technology.md) |
| Logistics | Physical shipment routes, capacity, and delivery. | [Logistics](logistics.md) |
| Causality | Which values may cause runtime changes and which are projections. | [Causality](causality.md) |
| Foreign policy | Strategic Statecraft decisions and saved country state. | [Foreign policy](foreign-policy.md) |

For authored inputs and save contracts, see [Runtime](../runtime/README.md). For source-to-canonical transition boundaries, see [Migrations](../migrations/README.md).
