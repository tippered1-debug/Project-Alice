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

**Market outlook.** Entry is ended by competition, not by a rule. For each market and good the outlook holds:

- **demand**: the daily quantity buyers bid for over the last 30 days;
- **supply**: the daily output of standing plants (capacity; extraction ceilings; actual output of farms and workshops) and of projects still being built.

An entrant adding output *q* expects to sell the share *D / (S + q)* of it, at most 95%, at a price that scales with that ratio to the power 1/1.5, between half and 1.25 times today's reference price. Every plant built or begun, including one begun earlier the same day, raises supply for the next sponsor. Several firms can run the same recipe in one province until the excess return is gone, and entry resumes when demand grows.

**Evaluating opportunities.** One evaluator (`economy::investment::evaluate`) appraises every project from:

- the construction bill at reference prices;
- the entrant's expected sell-through and price;
- materials;
- the wage needed to recruit locally: the larger of what local plants already pay and 1.1 times the local cohorts' reservation wage, never below the bootstrap offer.

The sponsor's required return is the financing cost. A sponsor picks the best of:

- **Greenfield plants** in its country.
- **Mines** on known deposits without a plant that its organization may work (as the deposit's operator or under an active extraction right). A mine must outlast twice its payback, and is built only when fully funded.
- **Plants offered for sale**, by expected profit over the asking price.

**Financing a project:**

1. The sponsor's own investable money. A person founds a company and must put in at least 20% of the capital.
2. New shares sold to investors.
3. Bank credit against the project itself, for the rest of the budget (see below).

A plant goes ahead once half of its budget plus working capital is raised and is scaled down to what was raised.

**Project credit.** A bank lends to a sponsor that may own no plant yet. It needs all of the following:

- the project's expected cash flow to repay half the loan over the five-year term;
- the sponsor's equity to be at least 30% of equity plus debt;
- the loan to be within half the value of the plant it finances;
- a clean repayment history;
- an expected return above the bank's rate.

How short of this a bank will go depends on its risk appetite. The loan is due at the end of the term. Once the plant is built, the loan follows the plant, so the plant's distress and insolvency handling covers it.

**Project accounts.** A project's budget sits in a dedicated account. When the project completes or is cancelled, the unspent remainder returns to the sponsor's ledger.

**Workshops.** An urban cohort reviews its crafts every 30 days. It takes up a craft its members do not practise yet when two conditions hold:

- A worker would earn more at the craft than the cohort's own production now gives. The earnings are computed at the price and sales the market outlook allows, with the members' labor shared with the cohort's other paying crafts.
- The cohort holds a month of the craft's inputs in savings and cash.

**Depreciation.** Built plants lose 5% of their capacity a year, and their operator books the worn share of replacement value as a cost. Farm land and household crafts do not wear. A firm replaces worn capacity through ordinary expansion once its plants run full.

## Plant sales

**When a plant is for sale.** Only when its operator puts it up:

- **Bankruptcy auction.** A bankrupt plant is offered at 60% of its replacement value, falling linearly to 20% over the 120-day sale window. Replacement value is the construction bill of its capacity at today's prices. A plant still unsold at the end of the window is closed.
- **Divestment.** An operator in distress for 90 days, losing money but not in default, offers the plant at 60% of replacement value.

**Who receives the price.** The plant's creditors are paid first. The rest goes to the plant's owners by economic stake; the operator's own share stays with it. The buyer takes the title, the operator role, and the stock at the plant.

A person buyer founds a company for the plant. Plants bound to a deposit or land, and household plants, are never sold.

## Share trading

Once every 30 days each private holder of a company's shares reviews its stake.

- **Sale to a higher valuation.** The holder sells to the investor of its country who values the shares most, when that investor requires a lower return. The price is halfway between their valuations: expected earnings over required return.
- **Sale for cash.** A holder with under 10 of liquid money sells at a tenth below the buyer's valuation.

A buyer takes no more than its concentration limit allows, so it may buy only part of the stake.

## Not yet modelled

- Investment abroad, capital controls, and the legal forms of firms. These wait for law and politics.
- Banks do not foreclose on unfinished projects.
