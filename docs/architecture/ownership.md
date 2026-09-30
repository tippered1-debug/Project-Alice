# Ownership

Productive assets have explicit operators and ownership stakes. Scenario data names each factory and resource deposit as an asset, binds it to a site and operator, and declares its owners. Ownership, voting, and economic fractions are separate values and each must form a complete share of the asset.

Firm equity is represented by an explicit equity asset with its own owner graph. Government or person capital owners are declared by reference to an existing institution or live exact person. Ownership is never inferred from province control, legacy producer shares, or factory finance fields. Missing or incomplete records reject scenario initialization instead of creating placeholder owners.

Canonical ownership is used for account-backed contributions, asset control, dividends, and secured credit. See [Actors](actors.md), [Banking](banking.md), and the full [scenario format](../runtime/scenario-format.md).
