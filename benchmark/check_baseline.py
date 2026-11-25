#!/usr/bin/env python3
"""Check benchmark medians against a baseline for the SAME CPU.

Absolute latency and the relative cost of in-process vs socket transports both
move with the CPU, so one global baseline cannot serve multiple runner types:
an AMD EPYC baseline compared against an Intel Xeon run produced 53-56% ratio
drift and failed the gate without any code regression. Baselines are therefore
kept per CPU under benchmark/baselines/ and selected from the CPU row the
workflow writes into the results file.

An unknown CPU fails closed with a message naming where to add a baseline:
guessing a nearby CPU's numbers is worse than a loud, actionable failure.
"""

import argparse
import glob
import json
import os
import re
import sys

TRANSPORTS = ("sameloop", "inproc", "ipc", "udp", "tcp")
ROW = re.compile(
    r"^\|\s*(sameloop|inproc|ipc|udp|tcp)\s*\|\s*([0-9.]+)\s*us\s*\|"
    r"\s*([0-9.]+)\s*req/s\s*\|\s*([0-9.]+)\s*us\s*\|"
    r"\s*([0-9.]+)\s*us\s*\|\s*([0-9.]+)x\s*\|$"
)
CPU_ROW = re.compile(r"^\|\s*CPU\s*\|\s*(.+?)\s*\|$")


class GateError(Exception):
    """An actionable reason the gate cannot run."""


def parse_results(path):
    medians = {}
    spreads = {}
    cpu = None
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            stripped = line.strip()
            match = ROW.match(stripped)
            if match:
                name, median, _, _, _, spread = match.groups()
                medians[name] = float(median)
                spreads[name] = float(spread)
                continue
            match = CPU_ROW.match(stripped)
            if match:
                cpu = match.group(1)
    return medians, spreads, cpu


def select_baseline(directory, cpu):
    if not cpu:
        raise GateError(
            "results have no CPU row; the workflow must record the runner CPU"
        )
    needle = cpu.lower()
    matches = []
    for path in sorted(glob.glob(os.path.join(directory, "*.json"))):
        with open(path, encoding="utf-8") as handle:
            baseline = json.load(handle)
        patterns = baseline.get("cpu_patterns", [])
        if any(pattern.lower() in needle for pattern in patterns):
            matches.append((path, baseline))
    if not matches:
        raise GateError(
            f"no baseline for CPU {cpu!r}; add a JSON file to {directory} "
            "from a clean run of this code on that CPU"
        )
    if len(matches) > 1:
        names = ", ".join(os.path.basename(p) for p, _ in matches)
        raise GateError(f"multiple baselines match CPU {cpu!r}: {names}")
    return matches[0]


def check(medians, spreads, baseline, name):
    errors = []
    base = baseline["median_us"]
    ref = baseline["reference_transport"]

    missing = [n for n in TRANSPORTS if n not in medians]
    if missing:
        errors.append("missing benchmark results: " + ", ".join(missing))

    max_spread = float(baseline["max_spread"])
    for n in TRANSPORTS:
        if n in spreads and spreads[n] > max_spread:
            errors.append(f"{n}: sample spread {spreads[n]:.1f}x exceeds {max_spread:.1f}x")

    if missing:
        return errors

    base_ref = base[ref]
    cur_ref = medians[ref]
    abs_limit = base_ref * (1.0 + float(baseline["absolute_tolerance"]))
    if cur_ref > abs_limit:
        errors.append(f"{ref}: {cur_ref:.2f} us exceeds absolute ceiling {abs_limit:.2f} us")

    rel_tol = float(baseline["relative_tolerance"])
    for n in TRANSPORTS:
        if n == ref:
            continue
        base_ratio = base[n] / base_ref
        cur_ratio = medians[n] / cur_ref
        delta = abs(cur_ratio / base_ratio - 1.0)
        print(
            f"{n}: ratio {cur_ratio:.3f} vs baseline {base_ratio:.3f} "
            f"({delta * 100:.1f}% delta)"
        )
        if delta > rel_tol:
            errors.append(
                f"{n}: ratio delta {delta * 100:.1f}% exceeds {rel_tol * 100:.0f}%"
            )

    print(f"baseline: {name} (CPU: {baseline.get('source_cpu', 'unknown')})")
    return errors


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--results", required=True)
    parser.add_argument("--baselines", required=True, help="directory of per-CPU baselines")
    args = parser.parse_args()

    try:
        medians, spreads, cpu = parse_results(args.results)
        path, baseline = select_baseline(args.baselines, cpu)
        errors = check(medians, spreads, baseline, os.path.basename(path))
    except (GateError, OSError, ValueError, KeyError) as exc:
        print("FAIL: " + str(exc))
        return 1

    for error in errors:
        print("FAIL: " + error)
    if errors:
        return 1
    print("benchmark gates: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
