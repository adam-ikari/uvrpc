#!/usr/bin/env python3
"""Check benchmark medians against a checked-in baseline.

Absolute latency moves with the runner, so gate on ratios to the reference
transport (SAMELOOP) and keep a loose absolute ceiling for a global collapse.
"""

import argparse
import json
import re
import sys

TRANSPORTS = ("sameloop", "inproc", "ipc", "udp", "tcp")
ROW = re.compile(
    r"^\|\s*(sameloop|inproc|ipc|udp|tcp)\s*\|\s*([0-9.]+)\s*us\s*\|"
    r"\s*([0-9.]+)\s*req/s\s*\|\s*([0-9.]+)\s*us\s*\|"
    r"\s*([0-9.]+)\s*us\s*\|\s*([0-9.]+)x\s*\|$"
)


def parse_results(path):
    medians = {}
    spreads = {}
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            match = ROW.match(line.strip())
            if not match:
                continue
            name, median, _, _, _, spread = match.groups()
            medians[name] = float(median)
            spreads[name] = float(spread)
    return medians, spreads


def check(results_path, baseline_path):
    with open(baseline_path, encoding="utf-8") as handle:
        baseline = json.load(handle)

    medians, spreads = parse_results(results_path)
    base = baseline["median_us"]
    ref = baseline["reference_transport"]
    errors = []

    missing = [name for name in TRANSPORTS if name not in medians]
    if missing:
        errors.append("missing benchmark results: " + ", ".join(missing))

    max_spread = float(baseline["max_spread"])
    for name in TRANSPORTS:
        if name in spreads and spreads[name] > max_spread:
            errors.append(
                f"{name}: sample spread {spreads[name]:.1f}x exceeds {max_spread:.1f}x"
            )

    if missing:
        return errors

    base_ref = base[ref]
    cur_ref = medians[ref]
    abs_limit = base_ref * (1.0 + float(baseline["absolute_tolerance"]))
    if cur_ref > abs_limit:
        errors.append(
            f"{ref}: {cur_ref:.2f} us exceeds absolute ceiling {abs_limit:.2f} us"
        )

    rel_tol = float(baseline["relative_tolerance"])
    for name in TRANSPORTS:
        if name == ref:
            continue
        base_ratio = base[name] / base_ref
        cur_ratio = medians[name] / cur_ref
        delta = abs(cur_ratio / base_ratio - 1.0)
        print(
            f"{name}: ratio {cur_ratio:.3f} vs baseline {base_ratio:.3f} "
            f"({delta * 100:.1f}% delta)"
        )
        if delta > rel_tol:
            errors.append(
                f"{name}: ratio delta {delta * 100:.1f}% exceeds "
                f"{rel_tol * 100:.0f}%"
            )

    return errors


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--results", required=True)
    parser.add_argument("--baseline", required=True)
    args = parser.parse_args()

    errors = check(args.results, args.baseline)
    for error in errors:
        print("FAIL: " + error)
    if errors:
        return 1
    print("benchmark gates: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
