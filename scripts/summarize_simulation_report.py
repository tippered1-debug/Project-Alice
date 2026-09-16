#!/usr/bin/env python3
"""Print balance-relevant checkpoints from a Project Alice JSONL simulation report."""

from __future__ import annotations

import argparse
import json
import math
import pathlib
import sys
from typing import Any


def number(snapshot: dict[str, Any], *path: str) -> float:
    value: Any = snapshot
    for key in path:
        value = value[key]
    result = float(value)
    if not math.isfinite(result):
        raise ValueError(f"non-finite {'.'.join(path)}")
    return result


def load_report(path: pathlib.Path) -> list[dict[str, Any]]:
    snapshots: list[dict[str, Any]] = []
    with path.open(encoding="utf-8") as report:
        for line_number, line in enumerate(report, start=1):
            if not line.strip():
                continue
            try:
                snapshot = json.loads(line)
                if not snapshot["valid"]:
                    raise ValueError("reported invariant violation")
                snapshots.append(snapshot)
            except (json.JSONDecodeError, KeyError, TypeError, ValueError) as error:
                raise ValueError(f"{path}:{line_number}: invalid snapshot: {error}") from error
    if not snapshots:
        raise ValueError(f"{path}: no snapshots")
    return snapshots


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Summarize economy, living-standard and factory telemetry from a JSONL simulation report."
    )
    parser.add_argument("report", type=pathlib.Path)
    parser.add_argument(
        "--every", type=int, default=12,
        help="print every Nth snapshot after the initial row (default: 12)",
    )
    args = parser.parse_args()
    if args.every < 1:
        parser.error("--every must be at least one")

    try:
        snapshots = load_report(args.report)
        print("date_raw,population,gdp,unemployment,life_needs,everyday_needs,luxury_needs,"
              "factory_profit,unprofitable_factory_share,debt,treasury,cpi,daily_inflation,demand_pressure,real_wage_index,"
              "money_supply,money_net_to_gross,money_unaccounted_share,household_money_share,producer_till_balance,"
              "market_quantity_traded,unfilled_life_needs,unfilled_intermediate,"
              "trade_requested,trade_delivered,cargo_in_transit,foreign_settlement_floor,"
              "legitimacy,coalition_power,government_stability,cabinet_confidence,"
              "fragile_governments,government_turnovers")
        last_index = len(snapshots) - 1
        for index, snapshot in enumerate(snapshots):
            if index not in (0, last_index) and index % args.every:
                continue
            population = number(snapshot, "economy", "population")
            factories = number(snapshot, "counts", "factories")
            # The money supply is the denominator for the two questions this
            # report exists to answer: how much of it is unexplained, and who
            # is holding it.
            money_supply = number(snapshot, "money", "total")
            row = (
                int(number(snapshot, "date_raw")),
                population,
                number(snapshot, "economy", "market_gdp"),
                number(snapshot, "living_standards", "unemployed_population") / population if population else 0.0,
                number(snapshot, "living_standards", "life_needs_population_sum") / population if population else 0.0,
                number(snapshot, "living_standards", "everyday_needs_population_sum") / population if population else 0.0,
                number(snapshot, "living_standards", "luxury_needs_population_sum") / population if population else 0.0,
                number(snapshot, "economy", "factory_profit"),
                number(snapshot, "counts", "unprofitable_factories") / factories if factories else 0.0,
                number(snapshot, "finance", "government_debt"),
                number(snapshot, "finance", "treasury"),
                number(snapshot, "economy", "consumer_price_index"),
                number(snapshot, "economy", "inflation"),
                number(snapshot, "economy", "consumer_demand_pressure"),
                number(snapshot, "labor", "real_price_sum") / number(snapshot, "labor", "price_sum")
                if number(snapshot, "labor", "price_sum") else 0.0,
                money_supply,
                number(snapshot, "money", "net_to_gross"),
                number(snapshot, "money", "unaccounted") / money_supply if money_supply else 0.0,
                number(snapshot, "money", "pop_savings") / money_supply if money_supply else 0.0,
                number(snapshot, "money", "producer_banks"),
                number(snapshot, "market_clearing", "quantity_traded"),
                number(snapshot, "market_clearing", "unfilled_life_needs"),
                number(snapshot, "market_clearing", "unfilled_intermediate"),
                number(snapshot, "trade", "requested_cargo"),
                number(snapshot, "trade", "delivered_cargo"),
                number(snapshot, "trade", "cargo_in_transit"),
                number(snapshot, "trade", "minimum_foreign_settlement"),
                number(snapshot, "tracked_nation", "legitimacy"),
                number(snapshot, "tracked_nation", "coalition_power"),
                number(snapshot, "tracked_nation", "government_stability"),
                number(snapshot, "tracked_nation", "minimum_cabinet_confidence"),
                number(snapshot, "counts", "fragile_governments"),
                number(snapshot, "counts", "government_turnovers"),
            )
            print(",".join(
                str(row[0]) if column == 0 else f"{value:.6g}"
                for column, value in enumerate(row)
            ))
    except (OSError, KeyError, ValueError) as error:
        print(error, file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
