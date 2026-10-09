#!/usr/bin/env python3
"""Run and compare the canonical OUR TIME supply-chain contagion fixture."""

from __future__ import annotations

import argparse
import csv
import json
import statistics
import subprocess
from pathlib import Path
from typing import Any


COUNTRY_ORDER = ("A", "B", "C")
VARIANTS = ("baseline", "embargo", "blockade", "substitution")
DEPENDENCY_SHARES = (0.50, 0.65, 0.80, 0.90)
SHOCK_DAY = 90
SHOCK_END_DAY = 180


def read_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))


def write_jsonl(csv_path: Path, jsonl_path: Path) -> None:
    with csv_path.open(newline="", encoding="utf-8") as source, jsonl_path.open(
        "w", encoding="utf-8"
    ) as target:
        for row in csv.DictReader(source):
            target.write(json.dumps(row, ensure_ascii=False, sort_keys=True) + "\n")


def numeric(row: dict[str, str], column: str) -> float:
    value = row.get(column, "")
    return float(value) if value else 0.0


def run_case(
    binary: Path, out_root: Path, variant: str, dependency: float,
    days: int, seed: int, save_restore_day: int = 0,
) -> dict[str, Any]:
    key = f"{variant}_dep_{dependency:.2f}"
    target = out_root / "runs" / key
    target.mkdir(parents=True, exist_ok=True)
    args = [
        str(binary), "--single-thread", "--supply-chain-contagion-lab",
        "--lab-shock", variant, "--lab-days", str(days), "--lab-seed", str(seed),
        "--lab-dependency", f"{dependency:.6f}", "--lab-output", str(target),
    ]
    if save_restore_day:
        args.extend(("--lab-save-restore-day", str(save_restore_day)))
    completed = subprocess.run(args, text=True, capture_output=True, check=False)
    if completed.returncode:
        raise RuntimeError(
            f"lab run failed ({key}, exit {completed.returncode}):\n"
            f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
        )
    metadata_line = next(
        (line for line in reversed(completed.stdout.splitlines()) if line.startswith("{")),
        "{}",
    )
    metadata = json.loads(metadata_line)
    timeseries = target / "timeseries.csv"
    events = target / "events.csv"
    if not timeseries.exists() or not events.exists():
        raise RuntimeError(f"lab run {key} did not produce both CSV outputs")
    write_jsonl(events, target / "events.jsonl")
    run_info = {
        "variant": variant,
        "dependency_share_a": dependency,
        "seed": seed,
        "days": days,
        "shock_day": SHOCK_DAY,
        "shock_end_day": SHOCK_END_DAY,
        "command": args,
        "binary_result": metadata,
        "timeseries_csv": str(timeseries.relative_to(out_root)),
        "events_csv": str(events.relative_to(out_root)),
        "events_jsonl": str((target / "events.jsonl").relative_to(out_root)),
    }
    (target / "run.json").write_text(
        json.dumps(run_info, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return run_info


def country_rows(info: dict[str, Any], root: Path, country: str) -> dict[int, dict[str, str]]:
    path = root / info["timeseries_csv"]
    return {
        int(row["day"]): row
        for row in read_rows(path)
        if row["country"] == country
    }


def event_rows(info: dict[str, Any], root: Path) -> list[dict[str, str]]:
    return read_rows(root / info["events_csv"])


def compare(control: dict[str, Any], case: dict[str, Any], root: Path) -> dict[str, Any]:
    control_b = country_rows(control, root, "B")
    case_b = country_rows(case, root, "B")
    control_a = country_rows(control, root, "A")
    case_a = country_rows(case, root, "A")
    control_c = country_rows(control, root, "C")
    case_c = country_rows(case, root, "C")
    shared_days = sorted(set(control_b) & set(case_b))
    pre_days = [day for day in shared_days if day < SHOCK_DAY]
    shock_days = [day for day in shared_days if SHOCK_DAY <= day < SHOCK_END_DAY]
    post_days = [day for day in shared_days if day >= SHOCK_END_DAY]

    def mean(rows: dict[int, dict[str, str]], days: list[int], column: str) -> float:
        values = [numeric(rows[day], column) for day in days]
        return statistics.fmean(values) if values else 0.0

    baseline_level = mean(control_b, pre_days, "production_units_per_day")
    first_trade_change = next((
        day for day in shock_days
        if abs(numeric(case_a[day], "exports_units") - numeric(control_a[day], "exports_units")) > 1e-5
    ), None)
    first_stock_change = next((
        day for day in shock_days
        if abs(numeric(case_b[day], "b_input_stock_units")
               - numeric(control_b[day], "b_input_stock_units")) > 1e-5
    ), None)
    first_output_change = next((
        day for day in shock_days
        if abs(numeric(case_b[day], "production_units_per_day")
               - numeric(control_b[day], "production_units_per_day")) > 1e-5
    ), None)
    declines = [
        max(0.0, numeric(control_b[day], "production_units_per_day")
            - numeric(case_b[day], "production_units_per_day"))
        for day in shock_days
    ]
    max_drop = max(declines, default=0.0)
    lost_output = sum(declines)
    baseline_a = sum(numeric(control_a[day], "exports_units") for day in shock_days)
    case_a_quantity = sum(numeric(case_a[day], "exports_units") for day in shock_days)
    c_quantity = sum(numeric(case_c[day], "exports_units") for day in shock_days)
    avoided_a = max(0.0, baseline_a - case_a_quantity)
    replacement_share = min(1.0, c_quantity / avoided_a) if avoided_a else None
    recovery = None
    streak = 0
    if baseline_level > 0.0:
        for day in post_days:
            if numeric(case_b[day], "production_units_per_day") >= 0.95 * baseline_level:
                streak += 1
                if streak >= 5:
                    recovery = day - 4
                    break
            else:
                streak = 0
    no_measured_effect = first_trade_change is None and first_stock_change is None and first_output_change is None
    return {
        "variant": case["variant"],
        "dependency_share_a": case["dependency_share_a"],
        "first_real_trade_change_day": first_trade_change,
        "first_input_stock_change_day": first_stock_change,
        "first_factory_output_change_day": first_output_change,
        "maximum_output_decline_units_per_day": max_drop,
        "cumulative_lost_output_units": lost_output,
        "mean_employment_change_contracts": mean(case_b, shock_days, "employed_contracts")
            - mean(control_b, shock_days, "employed_contracts"),
        "mean_household_consumption_change_units_per_day": mean(case_b, shock_days, "household_consumption_units")
            - mean(control_b, shock_days, "household_consumption_units"),
        "c_share_of_displaced_a_imports": replacement_share,
        "recovery_day_after_intervention_end": recovery,
        "mechanism_status": "active",
        "observed_effect": "no_effect" if no_measured_effect else "measured",
        "government_revenue_status": "not_observed_fixture_boundary",
        "baseline_mean_factory_output_units_per_day": baseline_level,
        "shock_days": len(shock_days),
    }


def svg_chart(path: Path, title: str, series: list[tuple[str, list[tuple[int, float]], str]],
              y_max: float, y_label: str) -> None:
    width, height = 960, 480
    left, right, top, bottom = 78, 25, 42, 62
    plot_w, plot_h = width - left - right, height - top - bottom
    y_max = max(y_max, 1e-6)
    colors = ("#355c9a", "#c64949", "#b27618", "#4e8c67", "#785aa8")
    all_x = [x for _, points, _ in series for x, _ in points]
    x_max = max(all_x, default=365)
    x_max = max(1, x_max)
    chunks = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="#ffffff"/>',
        f'<text x="{left}" y="26" font-family="sans-serif" font-size="18" font-weight="600">{title}</text>',
    ]
    for tick in range(5):
        value = y_max * tick / 4
        y = top + plot_h * (1 - tick / 4)
        chunks.append(f'<line x1="{left}" y1="{y:.1f}" x2="{width-right}" y2="{y:.1f}" stroke="#e3e6eb"/>')
        chunks.append(f'<text x="{left-10}" y="{y+4:.1f}" text-anchor="end" font-family="sans-serif" font-size="11" fill="#545b66">{value:.2f}</text>')
    for day in (0, 90, 180, x_max):
        if day > x_max:
            continue
        x = left + plot_w * day / x_max
        chunks.append(f'<line x1="{x:.1f}" y1="{top}" x2="{x:.1f}" y2="{height-bottom}" stroke="#edf0f4"/>')
        chunks.append(f'<text x="{x:.1f}" y="{height-bottom+20}" text-anchor="middle" font-family="sans-serif" font-size="11" fill="#545b66">{day}</text>')
    for index, (label, points, explicit_color) in enumerate(series):
        color = explicit_color or colors[index % len(colors)]
        coords = []
        for day, value in points:
            x = left + plot_w * day / x_max
            y = top + plot_h * (1 - max(0.0, value) / y_max)
            coords.append(f"{x:.1f},{y:.1f}")
        if coords:
            chunks.append(f'<polyline fill="none" stroke="{color}" stroke-width="2" points="{" ".join(coords)}"/>')
        legend_y = top + 15 + index * 19
        chunks.append(f'<line x1="{width-240}" y1="{legend_y}" x2="{width-218}" y2="{legend_y}" stroke="{color}" stroke-width="3"/>')
        chunks.append(f'<text x="{width-211}" y="{legend_y+4}" font-family="sans-serif" font-size="11" fill="#343942">{label}</text>')
    shock_start = left + plot_w * min(SHOCK_DAY, x_max) / x_max
    shock_end = left + plot_w * min(SHOCK_END_DAY, x_max) / x_max
    chunks.extend([
        f'<line x1="{shock_start:.1f}" y1="{top}" x2="{shock_start:.1f}" y2="{height-bottom}" stroke="#cf3434" stroke-dasharray="5 4"/>',
        f'<line x1="{shock_end:.1f}" y1="{top}" x2="{shock_end:.1f}" y2="{height-bottom}" stroke="#cf3434" stroke-dasharray="5 4"/>',
        f'<text x="{left}" y="{height-14}" font-family="sans-serif" font-size="12" fill="#343942">Day</text>',
        f'<text x="18" y="{top+plot_h/2:.1f}" transform="rotate(-90 18 {top+plot_h/2:.1f})" font-family="sans-serif" font-size="12" fill="#343942">{y_label}</text>',
        "</svg>",
    ])
    path.write_text("\n".join(chunks) + "\n", encoding="utf-8")


def write_report(root: Path, runs: list[dict[str, Any]], comparisons: list[dict[str, Any]],
                 days: int, seed: int) -> None:
    baseline_by_share = {
        float(run["dependency_share_a"]): run
        for run in runs if run["variant"] == "baseline"
    }
    compare_by_variant_share = {
        (item["variant"], float(item["dependency_share_a"])): item
        for item in comparisons
    }
    lines = [
        "# OUR TIME — Experiment 002: Supply Chain Contagion",
        "",
        f"Seed `{seed}`; horizon `{days}` days; shock begins day `{SHOCK_DAY}` and ends day `{SHOCK_END_DAY}`.",
        "All executable runs use `--single-thread`. Each intervention is compared with a baseline initialized from the same fixture parameters and seed.",
        "",
        "## Observed effects at 80% A dependency",
        "",
        "| Variant | First trade change | First stock change | First output change | Max output loss / day | Lost output | C replaces displaced A | Recovery day | Effect status |",
        "|---|---:|---:|---:|---:|---:|---:|---:|---|",
    ]
    for variant in VARIANTS[1:]:
        item = compare_by_variant_share.get((variant, 0.80))
        if not item:
            continue
        lines.append(
            f"| {variant} | {item['first_real_trade_change_day'] or '—'} | "
            f"{item['first_input_stock_change_day'] or '—'} | {item['first_factory_output_change_day'] or '—'} | "
            f"{item['maximum_output_decline_units_per_day']:.4f} | {item['cumulative_lost_output_units']:.4f} | "
            f"{item['c_share_of_displaced_a_imports'] if item['c_share_of_displaced_a_imports'] is not None else '—'} | "
            f"{item['recovery_day_after_intervention_end'] or '—'} | {item['observed_effect']} |"
        )
    lines.extend([
        "",
        "## Dependency series: A share vs B output loss",
        "",
        "| A supply capacity share | Max daily output loss | C share of displaced A imports |",
        "|---:|---:|---:|",
    ])
    for share in DEPENDENCY_SHARES:
        item = compare_by_variant_share.get(("blockade", share))
        if item:
            repl = item["c_share_of_displaced_a_imports"]
            lines.append(f"| {share:.0%} | {item['maximum_output_decline_units_per_day']:.4f} | {repl:.3f} |" if repl is not None else f"| {share:.0%} | {item['maximum_output_decline_units_per_day']:.4f} | — |")
    lines.extend([
        "",
        "## Charts",
        "",
        "![B actual factory output](b_factory_output.svg)",
        "",
        "![Ore imports and shipments](ore_imports.svg)",
        "",
        "## Causal trace and data",
        "",
        "Each run directory contains `timeseries.csv`, source `events.csv`, JSON Lines `events.jsonl`, and `run.json`. Event rows link goods-fill IDs to freight request, contract and shipment IDs, arrival, physical input consumption, factory output, payroll, and exact-person purchases.",
        "",
        "The baseline is marked valid only when paid A→B fills, fulfilled freight contracts, B input-stock consumption, and actual B output are all observed before the shock.",
        "`no_effect` means the active mechanism was applied but no measured variable changed relative to its paired control. `not_observed_fixture_boundary` marks public budget income because this fixture omits taxation and national budget collection.",
        "",
        "## Fixture boundary and architectural findings",
        "",
        "This small world runs the connected canonical subset in production order: `factory_inputs::plan/fulfill` → `concrete_market::match_all` → separate goods settlement and freight contract → `shipments::process_arrivals` → `industrial_production::produce_factory` and physical input removal → `payroll::settle_factory` → `exact_person_goods::process_daily`. It uses extraction enterprises, firm and carrier accounts, scoped land freight, explicit spatial infrastructure, exact-person labor contracts and a real worker need. It does not run the full `single_game_tick` macro economy, tax, government, military, migration, event or investment pipelines. No legacy Victoria II aggregate supplies physical production or cargo.",
        "",
        "A trade fill pays for goods and transfers their ownership at the origin; a fill alone does not mean delivery. Freight is a second payment and a shipment is a separate physical entity. Removing the A–B graph edge stops new route quotes. Existing `travelling` shipments continue, while a route-unavailable queued shipment remains delayed; the engine's `blocked` lifecycle is reserved for invalid route legs. The scenario reports the observed states separately.",
        "",
        "Supplier substitution is not a forced supplier command. Canonical market matching ranks accessible real asks by landed cost and actual available stock, and the experiment reads whether C fills orders. The `substitution` run is therefore an A–B blockade with C enabled; if it matches `blockade`, that is expected because both use the same physical intervention.",
        "",
        "The intervention lasts day 90 through day 179; day 180 restores trade access so recovery can be observed. This duration is an experiment convention. Recovery means B output is at least 95% of paired baseline for five consecutive days after the intervention ends.",
        "",
        "## Reproduction",
        "",
        "```sh",
        "cmake --build build/macos-arm64-release --target Alice -j 4",
        "python3 experiments/supply_chain_contagion/run_experiment.py --binary build/macos-arm64-release/Alice --output experiments/supply_chain_contagion/results",
        "```",
        "",
        "The standalone C++ interface accepts `--supply-chain-contagion-lab --single-thread --lab-shock baseline|embargo|blockade|substitution --lab-days 365 --lab-seed 424242 --lab-dependency 0.80 --lab-output DIR`.",
        "",
    ])
    (root / "report.md").write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("experiments/supply_chain_contagion/results"))
    parser.add_argument("--days", type=int, default=365)
    parser.add_argument("--seed", type=int, default=424242)
    parser.add_argument("--skip-dependency-series", action="store_true")
    args = parser.parse_args()
    binary = args.binary.resolve()
    root = args.output.resolve()
    if not binary.is_file():
        parser.error(f"binary does not exist: {binary}")
    if args.days < SHOCK_END_DAY + 5:
        parser.error(f"--days must be at least {SHOCK_END_DAY + 5} to observe recovery")
    root.mkdir(parents=True, exist_ok=True)
    shares = (0.80,) if args.skip_dependency_series else DEPENDENCY_SHARES
    runs: list[dict[str, Any]] = []
    for share in shares:
        runs.append(run_case(binary, root, "baseline", share, args.days, args.seed))
    for variant in VARIANTS[1:]:
        runs.append(run_case(binary, root, variant, 0.80, args.days, args.seed))
    if not args.skip_dependency_series:
        for share in DEPENDENCY_SHARES:
            if abs(share - 0.80) < 1e-6:
                continue
            runs.append(run_case(binary, root, "blockade", share, args.days, args.seed))

    baselines = {float(item["dependency_share_a"]): item for item in runs if item["variant"] == "baseline"}
    comparisons = []
    for case in runs:
        if case["variant"] == "baseline":
            continue
        control = baselines[float(case["dependency_share_a"])]
        comparisons.append(compare(control, case, root))
    summary = {
        "seed": args.seed,
        "days": args.days,
        "shock_day": SHOCK_DAY,
        "shock_end_day": SHOCK_END_DAY,
        "runs": runs,
        "comparisons": comparisons,
        "dependency_series": not args.skip_dependency_series,
    }
    (root / "summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    baseline = baselines[0.80]
    for case in runs:
        if case["variant"] == "baseline" or float(case["dependency_share_a"]) != 0.80:
            continue
        comparison = compare(baseline, case, root)
        for filename, column, ylabel in (
            ("b_factory_output.svg", "production_units_per_day", "Physical product units per day"),
        ):
            series = []
            for info, label in ((baseline, "paired baseline"), (case, case["variant"])):
                rows = country_rows(info, root, "B")
                points = [(day, numeric(row, column)) for day, row in sorted(rows.items())]
                series.append((label, points, ""))
            maximum = max((value for _, points, _ in series for _, value in points), default=1.0)
            svg_chart(root / filename, f"B actual output — {case['variant']}", series, maximum * 1.05, ylabel)
        a_rows = country_rows(baseline, root, "A")
        c_rows = country_rows(case, root, "C")
        series = [
            ("A exports in paired baseline", [(d, numeric(r, "exports_units")) for d, r in sorted(a_rows.items())], "#355c9a"),
            ("C exports during intervention", [(d, numeric(r, "exports_units")) for d, r in sorted(c_rows.items())], "#b27618"),
        ]
        maximum = max((value for _, points, _ in series for _, value in points), default=1.0)
        svg_chart(root / "ore_imports.svg", f"Supplier flows — {case['variant']}", series, maximum * 1.05, "Critical ore physical units per day")
        break
    write_report(root, runs, comparisons, args.days, args.seed)
    print(root / "report.md")
    print(root / "summary.json")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
