#!/usr/bin/env python3
"""Measure how reproducible a Project Alice headless run actually is.

Two matching runs prove nothing when divergence is intermittent. This runs the
same command many times and reports how many distinct outcomes appear, at which
tick they first separate, and how large the separation is. It can also sweep
environment variables so a hypothesis (scheduler, uninitialised memory) can be
tested rather than guessed at.
"""

from __future__ import annotations

import argparse
import collections
import json
import os
import pathlib
import subprocess
import sys
import tempfile
from typing import Any


def load(path: pathlib.Path) -> list[dict[str, Any]]:
    snapshots = []
    with path.open(encoding="utf-8") as report:
        for line in report:
            if line.strip():
                snapshots.append(json.loads(line))
    return snapshots


def first_divergence(runs: list[list[dict[str, Any]]]) -> tuple[int | None, float]:
    """Earliest tick where any two runs disagree, and the widest spread there."""
    reference = runs[0]
    for index, snapshot in enumerate(reference):
        checksums = {run[index]["save_checksum"] for run in runs if index < len(run)}
        if len(checksums) > 1:
            savings = [
                run[index]["economy"]["pop_savings"] for run in runs if index < len(run)
            ]
            spread = max(savings) - min(savings)
            relative = spread / abs(max(savings)) if max(savings) else 0.0
            return snapshot["tick"], relative
    return None, 0.0


def run_once(binary: str, scenario: str, cwd: str, extra: list[str],
        env: dict[str, str], report: pathlib.Path) -> bool:
    command = [binary, scenario, "--report-jsonl", str(report)] + extra
    completed = subprocess.run(
        command, cwd=cwd, env=env,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
    return completed.returncode == 0 and report.exists()


def soak(binary: str, scenario: str, cwd: str, extra: list[str],
        env_overrides: dict[str, str], repeats: int, label: str) -> int:
    env = dict(os.environ)
    env.update(env_overrides)
    runs: list[list[dict[str, Any]]] = []
    with tempfile.TemporaryDirectory() as scratch:
        for index in range(repeats):
            report = pathlib.Path(scratch) / f"run{index}.jsonl"
            if not run_once(binary, scenario, cwd, extra, env, report):
                print(f"{label}: run {index} failed", file=sys.stderr)
                return 2
            runs.append(load(report))

        finals = collections.Counter(run[-1]["save_checksum"] for run in runs)
        tick, spread = first_divergence(runs)
        distinct = len(finals)
        verdict = "reproducible" if distinct == 1 else f"{distinct} distinct outcomes"
        detail = "" if tick is None else f", first split at tick {tick} (spread {spread:.2e})"
        print(f"{label}: {repeats} runs -> {verdict}{detail}")
        largest = finals.most_common(1)[0][1]
        print(f"    most common outcome appeared {largest}/{repeats} times")
    return 0 if distinct == 1 else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary")
    parser.add_argument("scenario")
    parser.add_argument("--cwd", required=True,
        help="Victoria II root; the binary resolves game data relative to it")
    parser.add_argument("--repeats", type=int, default=8)
    parser.add_argument("--days", type=int, default=120)
    parser.add_argument("--snapshot-every", type=int, default=10)
    parser.add_argument("--seed", type=int, default=424242)
    parser.add_argument("--arg", action="append", default=[],
        help="extra argument passed to every run; repeatable")
    parser.add_argument("--sweep", action="append", default=[],
        help="LABEL:VAR=VALUE[,VAR=VALUE] configuration to test; repeatable")
    args = parser.parse_args()
    if args.repeats < 2:
        parser.error("--repeats must be at least two; one run can never disagree")

    extra = [
        "--days", str(args.days),
        "--snapshot-every", str(args.snapshot_every),
        "--seed", str(args.seed),
    ] + args.arg

    configurations: list[tuple[str, dict[str, str]]] = [("baseline", {})]
    for sweep in args.sweep:
        label, _, assignments = sweep.partition(":")
        overrides = {}
        for assignment in assignments.split(",") if assignments else []:
            name, _, value = assignment.partition("=")
            overrides[name] = value
        configurations.append((label, overrides))

    status = 0
    for label, overrides in configurations:
        status |= soak(args.binary, args.scenario, args.cwd, extra,
            overrides, args.repeats, label)
    return status


if __name__ == "__main__":
    raise SystemExit(main())
