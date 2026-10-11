#!/usr/bin/env python3
"""Run the canonical three-country political-economy crisis experiment."""

from __future__ import annotations

import argparse
import csv
import json
import statistics
import subprocess
import tempfile
from pathlib import Path
from typing import Any


COUNTRIES = ("A", "B", "C")
SHOCK_DAY = 90
SHOCK_END_DAY = 180
CASES = {
    "baseline": ("baseline", False),
    "trade_shock": ("embargo", False),
    "countercyclical_policy": ("embargo", True),
}


def read_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))


def write_jsonl(csv_path: Path, jsonl_path: Path) -> None:
    with csv_path.open(newline="", encoding="utf-8") as source, jsonl_path.open(
        "w", encoding="utf-8"
    ) as target:
        for row in csv.DictReader(source):
            target.write(json.dumps(row, sort_keys=True, ensure_ascii=False) + "\n")


def number(row: dict[str, str], field: str) -> float:
    value = row.get(field, "")
    return float(value) if value else 0.0


def run_case(
    binary: Path,
    output: Path,
    name: str,
    shock: str,
    stabilizer: bool,
    days: int,
    seed: int,
    dependency: float,
    shock_day: int,
    shock_end_day: int,
) -> dict[str, Any]:
    target = output / "runs" / name
    target.mkdir(parents=True, exist_ok=True)
    command = [
        str(binary),
        "--single-thread",
        "--endogenous-political-economic-crisis-lab",
        "--lab-shock", shock,
        "--lab-days", str(days),
        "--lab-shock-day", str(shock_day),
        "--lab-shock-end-day", str(shock_end_day),
        "--lab-seed", str(seed),
        "--lab-dependency", f"{dependency:.6f}",
        "--lab-output", str(target),
    ]
    if stabilizer:
        command.append("--lab-countercyclical-borrowing")
    completed = subprocess.run(command, text=True, capture_output=True, check=False)
    if completed.returncode:
        raise RuntimeError(
            f"{name} exited {completed.returncode}\nstdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
        )
    result_line = next((line for line in reversed(completed.stdout.splitlines()) if line.startswith("{")), "{}")
    binary_result = json.loads(result_line)
    required_checks = (
        "baseline_trade_valid",
        "banking_invariants_valid",
        "monetary_invariants_valid",
        "household_invariants_valid",
    )
    failed_checks = [key for key in required_checks if not binary_result.get(key, False)]
    if binary_result.get("ticks_completed") != days or failed_checks:
        raise RuntimeError(
            f"{name} failed run integrity: ticks={binary_result.get('ticks_completed')}, "
            f"failed checks={failed_checks}"
        )
    time_path = target / "timeseries.csv"
    event_path = target / "events.csv"
    if not time_path.exists() or not event_path.exists():
        raise RuntimeError(f"{name} did not write timeseries.csv and events.csv")
    write_jsonl(time_path, target / "timeseries.jsonl")
    write_jsonl(event_path, target / "events.jsonl")
    info = {
        "name": name,
        "seed": seed,
        "days": days,
        "shock": shock,
        "shock_day": shock_day,
        "shock_end_day": shock_end_day,
        "a_dependency_share": dependency,
        "countercyclical_borrowing": stabilizer,
        "command": command,
        "binary_result": binary_result,
        "timeseries_csv": str(time_path.relative_to(output)),
        "timeseries_jsonl": str((target / "timeseries.jsonl").relative_to(output)),
        "events_csv": str(event_path.relative_to(output)),
        "events_jsonl": str((target / "events.jsonl").relative_to(output)),
    }
    (target / "run.json").write_text(json.dumps(info, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return info


def summarize_case(info: dict[str, Any], root: Path, shock_day: int) -> dict[str, Any]:
    rows = read_rows(root / info["timeseries_csv"])
    events = read_rows(root / info["events_csv"])
    by_country = {
        country: [row for row in rows if row["country"] == country]
        for country in COUNTRIES
    }

    def mean(country: str, field: str, low: int, high: int) -> float:
        values = [number(row, field) for row in by_country[country]
                  if low <= int(row["day"]) < high]
        return statistics.fmean(values) if values else 0.0

    b = by_country["B"]
    default_days = [int(row["day"]) for row in b if int(row["defaulted_bonds"]) > 0]
    election_events = [row for row in events if row["event"] == "party_election_result"]
    coalition_events = [row for row in events if row["event"] == "government_coalition_formed"]
    law_events = [row for row in events if row["event"] == "new_law_enacted"]
    b_law_events = [row for row in law_events if row["country_from"] == "B"]
    credit_events = [row for row in events if row["event"].startswith("firm_credit_")]
    employment_events = [row for row in events if row["event"] == "employment_contracts_ended"]
    return {
        "name": info["name"],
        "checksum": info["binary_result"].get("checksum"),
        "b_mean": {
            "pre_shock": {
                "imports": mean("B", "imports_units", 1, shock_day),
                "production": mean("B", "production_units_per_day", 1, shock_day),
                "employment": mean("B", "employed_contracts", 1, shock_day),
                "unemployment": mean("B", "unemployed_people", 1, shock_day),
                "wage_income": mean("B", "wage_income_cash", 1, shock_day),
                "consumption": mean("B", "household_consumption_units", 1, shock_day),
                "tax_collected": mean("B", "tax_collected_cash", 1, shock_day),
            },
            "shock_window": {
                "imports": mean("B", "imports_units", shock_day, info["shock_end_day"]),
                "production": mean("B", "production_units_per_day", shock_day, info["shock_end_day"]),
                "employment": mean("B", "employed_contracts", shock_day, info["shock_end_day"]),
                "unemployment": mean("B", "unemployed_people", shock_day, info["shock_end_day"]),
                "wage_income": mean("B", "wage_income_cash", shock_day, info["shock_end_day"]),
                "consumption": mean("B", "household_consumption_units", shock_day, info["shock_end_day"]),
                "tax_collected": mean("B", "tax_collected_cash", shock_day, info["shock_end_day"]),
            },
            "post_government": {
                "production": mean("B", "production_units_per_day", 121, info["days"] + 1),
                "employment": mean("B", "employed_contracts", 121, info["days"] + 1),
                "tax_collected": mean("B", "tax_collected_cash", 121, info["days"] + 1),
            },
            "final": {field: number(b[-1], field) for field in (
                "treasury_cash", "public_debt_outstanding_cash", "public_debt_defaulted_cash",
                "bank_reserves_cash", "bank_debt_claims_face_cash", "bank_debt_carrying_assets_cash",
                "bank_default_impairment_cash", "bank_net_worth_cash", "bank_capital_ratio",
                "bank_status", "available_new_credit_cash", "enacted_statutes",
                "income_tax_rate",
            )},
            "final_politics": {field: b[-1][field] for field in (
                "latest_party_vote_shares", "lower_chamber_party_seats", "coalition_party_ids",
            )},
        },
        "default_day": min(default_days) if default_days else None,
        "election_results": len(election_events),
        "coalition_changes": len(coalition_events),
        "new_laws": len(law_events),
        "b_new_laws": len(b_law_events),
        "firm_credit_events": len(credit_events),
        "employment_loss_events": len(employment_events),
        "stabilizer_events": [row for row in events if row["event"].startswith("countercyclical_public_debt_")],
        "observed_effect": "pending_comparison",
    }


def write_report(root: Path, summaries: list[dict[str, Any]], repeatability: dict[str, Any]) -> None:
    metadata = json.loads(root.joinpath("metadata.json").read_text())
    by_name = {item["name"]: item for item in summaries}
    baseline = by_name["baseline"]["b_mean"]
    shock = by_name["trade_shock"]["b_mean"]
    policy = by_name["countercyclical_policy"]["b_mean"]
    rows = [
        "# Endogenous Political-Economic Crisis v1",
        "",
        "This report summarizes a fixed-seed, three-country run of the canonical daily economy. A supplies ore to B; C is the alternate supplier. B's firms employ exact workers and buy delivered inputs through the physical market and freight contracts. Each country has a parliament, parties, taxes, treasury and commercial bank. B's bank holds a consented public bond.",
        "",
        f"Seed `{metadata['seed']}`; simulation horizon `{metadata['days']}` days; the embargo starts day {metadata['shock_day']} and is removed day {metadata['shock_end_day']}. Initial elections occur at founding; constitutional terms are 120 days, so the first post-shock election and government process are included.",
        "",
        "The policy case applies one countercyclical rule: if the actual 30-day wage-tax receipts after the embargo fall below 80% of the same run's 30-day pre-shock average, the sitting finance minister may seek a bank-consented bond for the realized quarterly revenue gap. The finance authority, debt law, investor consent, reserve payment and obligation APIs decide whether it is issued. All resulting production, employment, tax, debt, bank and election outcomes come from the canonical daily runtime.",
        "",
        "## B during the embargo",
        "",
        "| Case | Ore imports / day | Output / day | Employed contracts | Unemployed people | Wage income | Household use | Collected tax |",
        "|---|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for name in ("baseline", "trade_shock", "countercyclical_policy"):
        label = name.replace("_", " ")
        values = by_name[name]["b_mean"]["shock_window"]
        rows.append(
            f"| {label} | {values['imports']:.4f} | {values['production']:.4f} | "
            f"{values['employment']:.3f} | {values['unemployment']:.3f} | {values['wage_income']:.4f} | "
            f"{values['consumption']:.4f} | {values['tax_collected']:.4f} |"
        )
    rows += [
        "",
        "## Fiscal, banking and political results",
        "",
        "| Case | B default day | Final Treasury | Debt due | Bank impairment | Bank capital | New credit | Elections | Coalition changes | New laws |",
        "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for name in ("baseline", "trade_shock", "countercyclical_policy"):
        item = by_name[name]
        final = item["b_mean"]["final"]
        rows.append(
            f"| {name.replace('_', ' ')} | {item['default_day'] or 'none'} | "
            f"{float(final['treasury_cash']):.3f} | {float(final['public_debt_outstanding_cash']):.3f} | "
            f"{float(final['bank_default_impairment_cash']):.3f} | "
            f"{float(final['bank_capital_ratio']):.4f} | {float(final['available_new_credit_cash']):.3f} | "
            f"{item['election_results']} | {item['coalition_changes']} | {item['new_laws']} |"
        )
    rows += [
        "",
        "## B electorate and enacted policy",
        "",
        "| Case | Last party vote shares | Lower chamber seats | Coalition | Final income tax rate | B laws enacted |",
        "|---|---|---|---|---:|---:|",
    ]
    for name in ("baseline", "trade_shock", "countercyclical_policy"):
        item = by_name[name]
        politics = item["b_mean"]["final_politics"]
        final = item["b_mean"]["final"]
        rows.append(
            f"| {name.replace('_', ' ')} | {politics['latest_party_vote_shares']} | "
            f"{politics['lower_chamber_party_seats']} | {politics['coalition_party_ids']} | "
            f"{float(final['income_tax_rate']):.4f} | {item['b_new_laws']} |"
        )
    rows += [
        "",
        "## Integrity and repeatability",
        "",
        f"Repeated policy run checksum: `{repeatability['first_checksum']}` and `{repeatability['repeat_checksum']}`; identical: `{str(repeatability['identical']).lower()}`.",
        "",
        "The binary reports money conservation, banking validation, household validation, election count, law count, legal default claim, and post-default credit capacity for each run in `audit.json`. CSV and JSONL files preserve the daily series and event records. The report below does not infer a mechanism from a scenario label: each claim is backed by the run's measured rows and events.",
        "",
        "## Interpretation",
        "",
    ]
    shock_item = by_name["trade_shock"]
    policy_item = by_name["countercyclical_policy"]
    if shock_item["observed_effect"] == "no_effect":
        rows.append("Compared with baseline on the same dates, the trade shock produced no measured change in the recorded B indicators; no downstream effect is claimed.")
    else:
        pre = shock_item["b_mean"]["pre_shock"]
        stressed = shock_item["b_mean"]["shock_window"]
        rows.append(
            f"The recorded embargo-window means in B were {stressed['imports']:.4f} ore imports/day, "
            f"{stressed['production']:.4f} output/day and {stressed['employment']:.3f} contracts, "
            f"against same-day baseline means {baseline['shock_window']['imports']:.4f}, "
            f"{baseline['shock_window']['production']:.4f} and {baseline['shock_window']['employment']:.3f}, "
            f"and pre-shock means {pre['imports']:.4f}, {pre['production']:.4f} and {pre['employment']:.3f}. "
            f"The event series contains {shock_item['employment_loss_events']} actual contract-loss records, "
            f"{shock_item['election_results']} party result rows, {shock_item['coalition_changes']} coalition changes "
            f"and {shock_item['new_laws']} enacted instruments."
        )
    if policy_item["stabilizer_events"]:
        rows.append(
            f"The countercyclical case recorded {len(policy_item['stabilizer_events'])} bond decision event(s); "
            f"its realized Treasury, debt, bank impairment and credit results are shown in the table."
        )
    else:
        rows.append("The countercyclical bond trigger did not issue debt in the actual run; the report records that absence and makes no claim of fiscal support.")
    rows.append("")
    root.joinpath("REPORT.md").write_text("\n".join(rows) + "\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("endogenous-crisis-results"))
    parser.add_argument("--days", type=int, default=420)
    parser.add_argument("--seed", type=int, default=424242)
    parser.add_argument("--dependency", type=float, default=0.80)
    parser.add_argument("--shock-day", type=int, default=SHOCK_DAY)
    parser.add_argument("--shock-end-day", type=int, default=SHOCK_END_DAY)
    args = parser.parse_args()
    binary = args.binary.resolve()
    output = args.output.resolve()
    if not binary.is_file():
        raise SystemExit(f"binary not found: {binary}")
    if args.days <= 0 or args.days > 50000 or args.shock_day <= 0 \
            or args.shock_end_day <= args.shock_day:
        raise SystemExit("days must be 1..50000 and shock-end-day must follow shock-day")
    output.mkdir(parents=True, exist_ok=True)
    runs = [
        run_case(binary, output, name, shock, stabilizer, args.days, args.seed,
                 args.dependency, args.shock_day, args.shock_end_day)
        for name, (shock, stabilizer) in CASES.items()
    ]
    summaries = [summarize_case(info, output, args.shock_day) for info in runs]
    summaries_by_name = {item["name"]: item for item in summaries}
    shock_metrics = ("imports", "production", "employment", "unemployment", "wage_income",
                     "consumption", "tax_collected")
    baseline_window = summaries_by_name["baseline"]["b_mean"]["shock_window"]
    trade_window = summaries_by_name["trade_shock"]["b_mean"]["shock_window"]
    for item in summaries:
        measured_deltas = {
            metric: item["b_mean"]["shock_window"][metric] - baseline_window[metric]
            for metric in shock_metrics
        }
        item["shock_window_delta_vs_baseline"] = measured_deltas
        item["observed_effect"] = "measured" if any(abs(value) > 1.0e-5
            for value in measured_deltas.values()) else "no_effect"
        if item["name"] == "countercyclical_policy":
            item["policy_window_delta_vs_trade_shock"] = {
                metric: item["b_mean"]["shock_window"][metric] - trade_window[metric]
                for metric in shock_metrics
            }
    policy_info = next(item for item in runs if item["name"] == "countercyclical_policy")
    with tempfile.TemporaryDirectory(prefix="repeat-policy-", dir=output) as temporary:
        repeated_dir = Path(temporary)
        # The executable writes beside its CSV output; compare its final checksum
        # without keeping a redundant full copy in the committed results.
        command = [
            str(binary), "--single-thread", "--endogenous-political-economic-crisis-lab",
            "--lab-shock", "embargo", "--lab-days", str(args.days),
            "--lab-shock-day", str(args.shock_day), "--lab-shock-end-day", str(args.shock_end_day),
            "--lab-seed", str(args.seed),
            "--lab-dependency", f"{args.dependency:.6f}", "--lab-output", str(repeated_dir / "run"),
            "--lab-countercyclical-borrowing",
        ]
        repeated = subprocess.run(command, text=True, capture_output=True, check=False)
        if repeated.returncode:
            raise RuntimeError(f"repeatability run failed:\n{repeated.stdout}\n{repeated.stderr}")
        repeated_line = next((line for line in reversed(repeated.stdout.splitlines()) if line.startswith("{")), "{}")
        repeated_result = json.loads(repeated_line)
        repeatability = {
            "seed": args.seed,
            "first_checksum": policy_info["binary_result"].get("checksum"),
            "repeat_checksum": repeated_result.get("checksum"),
            "identical": policy_info["binary_result"].get("checksum") == repeated_result.get("checksum"),
        }
        if not repeatability["identical"]:
            raise RuntimeError(
                "the repeated fixed-seed policy run produced a different checksum: "
                f"{repeatability['first_checksum']} != {repeatability['repeat_checksum']}"
            )
    audit = {
        "seed": args.seed,
        "days": args.days,
        "shock_day": args.shock_day,
        "shock_end_day": args.shock_end_day,
        "runs": [
            {"name": item["name"], **item["binary_result"]}
            for item in runs
        ],
        "repeatability": repeatability,
    }
    (output / "audit.json").write_text(json.dumps(audit, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    (output / "comparison.json").write_text(json.dumps(summaries, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    (output / "metadata.json").write_text(json.dumps({"seed": args.seed, "days": args.days,
        "shock_day": args.shock_day, "shock_end_day": args.shock_end_day,
        "dependency": args.dependency}, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    with (output / "comparison.csv").open("w", newline="", encoding="utf-8") as stream:
        fields = ["case", "checksum", "B_imports_shock_mean", "B_output_shock_mean",
                  "B_employment_shock_mean", "B_unemployment_shock_mean", "B_tax_shock_mean", "B_treasury_final",
                  "B_debt_default_final", "B_bank_impairment_final", "B_credit_final",
                  "B_final_income_tax_rate", "B_last_party_vote_shares",
                  "B_lower_chamber_party_seats", "B_coalition_party_ids", "elections",
                  "coalition_changes", "new_laws", "B_new_laws", "observed_effect"]
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        for item in summaries:
            writer.writerow({
                "case": item["name"], "checksum": item["checksum"],
                "B_imports_shock_mean": item["b_mean"]["shock_window"]["imports"],
                "B_output_shock_mean": item["b_mean"]["shock_window"]["production"],
                "B_employment_shock_mean": item["b_mean"]["shock_window"]["employment"],
                "B_unemployment_shock_mean": item["b_mean"]["shock_window"]["unemployment"],
                "B_tax_shock_mean": item["b_mean"]["shock_window"]["tax_collected"],
                "B_treasury_final": item["b_mean"]["final"]["treasury_cash"],
                "B_debt_default_final": item["b_mean"]["final"]["public_debt_defaulted_cash"],
                "B_bank_impairment_final": item["b_mean"]["final"]["bank_default_impairment_cash"],
                "B_credit_final": item["b_mean"]["final"]["available_new_credit_cash"],
                "B_final_income_tax_rate": item["b_mean"]["final"]["income_tax_rate"],
                "B_last_party_vote_shares": item["b_mean"]["final_politics"]["latest_party_vote_shares"],
                "B_lower_chamber_party_seats": item["b_mean"]["final_politics"]["lower_chamber_party_seats"],
                "B_coalition_party_ids": item["b_mean"]["final_politics"]["coalition_party_ids"],
                "elections": item["election_results"], "coalition_changes": item["coalition_changes"],
                "new_laws": item["new_laws"], "B_new_laws": item["b_new_laws"],
                "observed_effect": item["observed_effect"],
            })
    write_report(output, summaries, repeatability)
    print(json.dumps(audit, sort_keys=True))


if __name__ == "__main__":
    main()
