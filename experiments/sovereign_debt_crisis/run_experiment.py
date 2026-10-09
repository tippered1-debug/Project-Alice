#!/usr/bin/env python3
"""Run and compare the deterministic sovereign-debt crisis scenarios."""

from __future__ import annotations

import argparse
import csv
import json
import subprocess
from pathlib import Path


SCENARIOS = ("baseline", "revenue-shock", "austerity")


def run_once(binary: Path, scenario: str, seed: int, days: int, shock_day: int, output: Path) -> dict:
	command = [
		str(binary),
		"--single-thread",
		"--sovereign-debt-crisis-lab",
		"--debt-lab-scenario", scenario,
		"--debt-lab-seed", str(seed),
		"--debt-lab-days", str(days),
		"--debt-lab-shock-day", str(shock_day),
		"--debt-lab-output", str(output),
	]
	completed = subprocess.run(command, check=True, capture_output=True, text=True)
	json_lines = [line for line in completed.stdout.splitlines() if line.startswith("{")]
	if not json_lines:
		raise RuntimeError(f"No result JSON from {' '.join(command)}\n{completed.stderr}")
	return json.loads(json_lines[-1])


def read_summary(path: Path) -> dict:
	with (path / "summary.csv").open(newline="", encoding="utf-8") as source:
		return next(csv.DictReader(source))


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--binary", type=Path, required=True, help="Path to the built Alice executable")
	parser.add_argument("--output", type=Path, default=Path("sovereign-debt-crisis-results"))
	parser.add_argument("--seed", type=int, default=424242)
	parser.add_argument("--days", type=int, default=540)
	parser.add_argument("--shock-day", type=int, default=90)
	args = parser.parse_args()
	if not args.binary.is_file():
		parser.error(f"executable does not exist: {args.binary}")
	if args.days < 2 or args.days > 50000 or args.shock_day < 1 or args.shock_day >= args.days:
		parser.error("days must be 2..50000 and shock-day must fall inside the horizon")

	args.output.mkdir(parents=True, exist_ok=True)
	results = []
	for scenario in SCENARIOS:
		first_dir = args.output / scenario
		replay_dir = args.output / "determinism-replay" / scenario
		first = run_once(args.binary, scenario, args.seed, args.days, args.shock_day, first_dir)
		replay = run_once(args.binary, scenario, args.seed, args.days, args.shock_day, replay_dir)
		if first["checksum"] != replay["checksum"]:
			raise RuntimeError(f"non-deterministic output for {scenario}: {first['checksum']} != {replay['checksum']}")
		metrics = read_summary(first_dir)
		defaulted = metrics["debt_defaulted"] == "true"
		if ((scenario == "revenue-shock") != defaulted):
			raise RuntimeError(f"unexpected default outcome for {scenario}: {defaulted}")
		if scenario == "revenue-shock" and float(metrics["service_shortfall_cash"]) <= 0.0:
			raise RuntimeError("revenue-shock case did not expose a public-service funding shortfall")
		if scenario == "austerity" and float(metrics["service_shortfall_cash"]) > 1.0e-4:
			raise RuntimeError("austerity case unexpectedly failed to fund its planned public services")
		results.append({
			"scenario": scenario,
			"seed": args.seed,
			"days": args.days,
			"checksum": first["checksum"],
			"deterministic_replay": True,
			"debt_defaulted": defaulted,
			"final_defaulted_claim_cash": float(metrics["final_defaulted_claim_cash"]),
			"service_paid_cash": float(metrics["service_paid_cash"]),
			"service_shortfall_cash": float(metrics["service_shortfall_cash"]),
			"bank_base_model_net_worth_cash": float(metrics["bank_base_model_net_worth_cash"]),
			"bank_marked_net_worth_recovery_0_cash": float(metrics["bank_marked_net_worth_recovery_0_cash"]),
			"bank_marked_net_worth_recovery_50_cash": float(metrics["bank_marked_net_worth_recovery_50_cash"]),
			"bank_marked_net_worth_recovery_100_cash": float(metrics["bank_marked_net_worth_recovery_100_cash"]),
		})

	manifest = {
		"experiment": "sovereign_debt_crisis",
		"seed": args.seed,
		"days": args.days,
		"shock_day": args.shock_day,
		"scenarios": results,
		"interpretation": "Recovery-adjusted bank net worth is an experiment sensitivity; the game balance sheet keeps defaulted sovereign claims at face value.",
	}
	(args.output / "comparison.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
	print(json.dumps(manifest, indent=2))
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
