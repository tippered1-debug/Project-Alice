# Capital allocation

Savings become productive capital only through decisions of the actors who own them. No rule takes an owner's cash, sells a plant its operator did not offer, or builds a plant nobody chose to fund.

```text
wages, rent, dividends -> wallet -> bank deposit -> equity or credit -> capital project -> plant -> earnings -> dividends and new investment
```

## Savers and investors

Each day `economy::capital_market` builds a pool of investors in a stable order. Each investor has:

- a nation;
- a settlement;
- money it would commit (*investable*);
- a required return.

| Investor | Investable money |
| --- | --- |
| Person or household cohort | Half of its free bank deposit. Its wallet stays working cash. |
| Company, fund, or cooperative | Half of its liquid money (wallet and deposit) above its operating reserve: unpaid wages plus 90 days of recent plant costs. |
| Bank or government | Nothing. |

**Who becomes a saver.** A person without a profile whose wallet holds more than 50 becomes a saver: they gain a profile, and the liquidity policy banks their savings (see [Banking](banking.md)).

**Required return.** An investor requires the lending base rate of its bank plus a 5% equity premium, or 10% with no bank.

**Where investors invest.** An investor invests only in its own country: its home or operating province's owner, else its bank's jurisdiction.

## Equity offerings

A firm raises equity by offering new shares. The offering states:

- how much money it seeks;
- the pre-money value of the existing shares: book value, i.e. paid-in capital plus retained earnings;
- the expected annual earnings of the firm once the money is invested.

**Who subscribes.** Every investor of the issuer's country and settlement whose required return the earnings yield meets offers up to a quarter of its investable money. The yield is earnings divided by pre-money plus the amount sought. When willing demand exceeds the amount sought, each subscriber gets a pro-rata share.

**Settlement.** Subscribers pay from their deposit first, then their wallet, and receive new shares at the offering price. Existing holders are diluted; nobody is called for cash.

**When firms offer shares:**

- **Expansion** of a plant running near capacity. The firm pays from its own cash above a working reserve, then borrows, then offers shares.
- **Greenfield entry.** See [Sponsors and projects](#sponsors-and-projects).
- **Recapitalization** of a plant in default. The firm offers shares for the defaulted amount. Investors buy only if the firm's expected earnings justify the price, and the proceeds service the defaulted loans.

**Working capital** is bank credit only.

## Sponsors and projects

**Review schedule.** Companies, funds, cooperatives, and persons each review their investments every 30 days. First reviews are spread over the period.

**Evaluating opportunities.** A sponsor compares two kinds of opportunity in its country by their return above its required return:

- **Greenfield plants.** One evaluator (`economy::investment::evaluate`) uses:
  - the construction bill at reference prices;
  - expected sell-through;
  - materials;
  - the wage needed to recruit locally: 1.1 times the local cohorts' reservation wage, never below the bootstrap offer.

  The sponsor's required return is the financing cost. Extraction plants, farms, and workshops are not greenfield opportunities.
- **Plants offered for sale.** The return is expected profit over the asking price.

**No duplicates.** A province and recipe that already has a plant, or a project being built, is not an opportunity. Neither is one another sponsor took the same day.

**Financing a greenfield project:**

1. The sponsor's own investable money. A person founds a company and must put in at least 20% of the capital.
2. Bank credit against a profitable plant the sponsor already runs.
3. New shares sold to investors.

The project goes ahead once half of its budget plus working capital is raised. It is scaled down to what was raised.

**Project accounts.** A project's budget sits in a dedicated account. When the project completes or is cancelled, the unspent remainder returns to the sponsor's ledger.

## Plant sales

**When a plant is for sale.** Only when its operator puts it up:

- **Bankruptcy auction.** A bankrupt plant is offered at 60% of its replacement value, falling linearly to 20% over the 120-day sale window. Replacement value is the construction bill of its capacity at today's prices. A plant still unsold at the end of the window is closed.
- **Divestment.** An operator in distress for 90 days, losing money but not in default, offers the plant at 60% of replacement value.

**Who receives the price.** The plant's creditors are paid first. The rest goes to the plant's owners by economic stake; the operator's own share stays with it. The buyer takes the title, the operator role, and the stock at the plant.

A person buyer founds a company for the plant. Plants bound to a deposit or land, and household plants, are never sold.

## Not yet modelled

- Interest rates do not respond to the supply of savings.
- Plants do not depreciate.
- Banks do not lend against the project itself.
- Shares cannot be resold, and investors do not invest abroad.
- Extraction plants and workshops are not opened at runtime.
