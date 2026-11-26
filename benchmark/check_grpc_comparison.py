#!/usr/bin/env python3
"""Local diagnostic for the UVRPC-vs-gRPC comparison data.

Not a CI gate. The Benchmark workflow runs compare_grpc.sh as an informational
comparison and does not call this script: gRPC is an external library whose
numbers move with runner hardware, version and load, so losing to it must not
fail this project's CI. Run this by hand when you want to check whether a
captured comparison is usable (all sides present, spread <= 2.0x) and which
side came out ahead.
"""

import argparse
import re
import sys

REQUIRED = ("UVRPC_tcp", "UVRPC_sync", "gRPC_tcp")
UVRPC_SIDES = ("UVRPC_tcp", "UVRPC_sync")


def number(cell):
    match = re.search(r"\d+(?:\.\d+)?", cell)
    return float(match.group()) if match else None


def parse(path):
    rows = {}
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            if not line.startswith("|"):
                continue
            cells = [c.strip() for c in line.strip().strip("|").split("|")]
            if len(cells) >= 7 and cells[0] in REQUIRED:
                rows[cells[0]] = {
                    "latency": number(cells[1]),
                    "best": number(cells[3]),
                    "worst": number(cells[4]),
                    "spread": number(cells[5]),
                    "rss": number(cells[6]) if len(cells) > 6 else None,
                }
    return rows


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--results", required=True)
    args = parser.parse_args()

    rows = parse(args.results)
    failures = []

    for side in REQUIRED:
        if side not in rows:
            failures.append(f"{side}: no result row")
            continue
        for field in ("latency", "best", "worst", "spread", "rss"):
            if rows[side][field] is None:
                failures.append(f"{side}: missing {field}")
        spread = rows[side]["spread"]
        if spread is not None and spread > 2.0:
            failures.append(
                f"{side}: sample spread {spread:.1f}x > 2.0x "
                "(host contention, rerun before quoting)"
            )

    if all(side in rows for side in REQUIRED):
        grpc = rows["gRPC_tcp"]
        for side in UVRPC_SIDES:
            uv = rows[side]
            if None not in (uv["latency"], grpc["latency"]):
                if uv["latency"] > grpc["latency"]:
                    failures.append(
                        f"latency: {side} {uv['latency']:.2f} us > gRPC "
                        f"{grpc['latency']:.2f} us"
                    )
                else:
                    print(
                        f"OK latency: {side} {uv['latency']:.2f} us <= gRPC "
                        f"{grpc['latency']:.2f} us "
                        f"({grpc['latency'] / uv['latency']:.2f}x faster)"
                    )
            if None not in (uv["rss"], grpc["rss"]):
                if uv["rss"] > grpc["rss"]:
                    failures.append(
                        f"memory: {side} {uv['rss']:.0f} KiB > gRPC "
                        f"{grpc['rss']:.0f} KiB"
                    )
                else:
                    print(
                        f"OK memory: {side} {uv['rss']:.0f} KiB <= gRPC "
                        f"{grpc['rss']:.0f} KiB "
                        f"({grpc['rss'] / uv['rss']:.2f}x lower)"
                    )

    if failures:
        for failure in failures:
            print(f"FAIL {failure}")
        return 1
    print("comparison usable (informational; not a CI gate)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
