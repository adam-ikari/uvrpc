#!/usr/bin/env python3
"""Gate benchmark results on CPU-independent, performance-reflecting ratios.

Absolute latency and the ratio between in-process and socket transports both
move with the CPU. Three CI runs on EPYC 7763, Xeon 6975P-C and EPYC 9V45 showed
stable *within-family* ratios and a wildly varying cross-family ratio:

    inproc/sameloop   0.989  1.000  1.020
    ipc/tcp           0.569  0.598  0.564
    udp/tcp           1.023  1.104  1.040
    sameloop/tcp      0.046  0.104  0.033   <- hardware, not the library

The gate therefore compares only within-family ratios. A new CPU never blocks
CI on its hardware signature; instead its medians are recorded under --records
so the difference is kept, and the decision uses the ratios that reflect the
library. Guards remain for missing transports, sample spread, and a global
absolute collapse.
"""

import argparse
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
HOST_ROW = re.compile(r"^\|\s*Host\s*\|\s*(.+?)\s*\|$")


def parse_results(path):
    medians = {}
    spreads = {}
    cpu = None
    host = None
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
                continue
            match = HOST_ROW.match(stripped)
            if match:
                host = match.group(1)
    return medians, spreads, cpu, host


def record_cpu(directory, cpu, host, medians):
    """Keep a new CPU's numbers. This is a record, never a gate input.

    Deduplicate on the CPU identity, not on a filename slug: two filenames can
    describe the same CPU (an earlier run wrote amd-epyc-7763.json with
    source_cpu, a later one would generate amd-epyc-7763-64-core-processor.json
    with cpu). Both field names are checked so old records still dedupe.
    """
    if not cpu:
        return None
    os.makedirs(directory, exist_ok=True)
    for name in os.listdir(directory):
        if not name.endswith(".json"):
            continue
        with open(os.path.join(directory, name), encoding="utf-8") as handle:
            existing = json.load(handle)
        if cpu in (existing.get("cpu"), existing.get("source_cpu")):
            return None
    slug = re.sub(r"[^a-z0-9]+", "-", cpu.lower()).strip("-")
    path = os.path.join(directory, slug + ".json")
    entry = {
        "source_cpu": cpu,
        "host": host,
        "median_us": {name: medians[name] for name in TRANSPORTS if name in medians},
    }
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(entry, handle, indent=2)
        handle.write("\n")
    return path


def check(medians, spreads, baseline):
    errors = []
    missing = [name for name in TRANSPORTS if name not in medians]
    if missing:
        errors.append("missing benchmark results: " + ", ".join(missing))
        return errors

    max_spread = float(baseline["max_spread"])
    for name in TRANSPORTS:
        if name in spreads and spreads[name] > max_spread:
            errors.append(f"{name}: sample spread {spreads[name]:.1f}x exceeds {max_spread:.1f}x")

    ceiling = float(baseline["absolute_ceiling_us"])
    if medians["sameloop"] > ceiling:
        errors.append(f"sameloop: {medians['sameloop']:.2f} us exceeds global ceiling {ceiling:.1f} us")

    rel_tol = float(baseline["relative_tolerance"])
    for label, (a, b) in (
        ("inproc/sameloop", ("inproc", "sameloop")),
        ("ipc/tcp", ("ipc", "tcp")),
        ("udp/tcp", ("udp", "tcp")),
    ):
        reference = float(baseline["reference_ratios"][label])
        current = medians[a] / medians[b]
        delta = abs(current / reference - 1.0)
        print(f"{label}: {current:.3f} vs reference {reference:.3f} ({delta * 100:.1f}% delta)")
        if delta > rel_tol:
            errors.append(f"{label}: delta {delta * 100:.1f}% exceeds {rel_tol * 100:.0f}%")

    cross = medians["sameloop"] / medians["tcp"]
    print(f"(informational, not gated) sameloop/tcp: {cross:.3f} — hardware-dependent")
    return errors


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--results", required=True)
    parser.add_argument("--baseline", required=True, help="single baseline of within-family ratios")
    parser.add_argument("--records", help="directory to record previously unseen CPUs")
    args = parser.parse_args()

    try:
        with open(args.baseline, encoding="utf-8") as handle:
            baseline = json.load(handle)
        medians, spreads, cpu, host = parse_results(args.results)
        if args.records:
            path = record_cpu(args.records, cpu, host, medians)
            if path:
                print(f"recorded new CPU {cpu!r} in {path} (not used for gating)")
        errors = check(medians, spreads, baseline)
    except (OSError, ValueError, KeyError) as exc:
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
