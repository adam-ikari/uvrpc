#!/usr/bin/env python3
"""
UVRPC Performance Results Comparison Tool

This tool compares multiple performance test reports and generates
comprehensive comparison charts and tables.
"""

import os
import re
import sys
from pathlib import Path
from datetime import datetime
from typing import Dict, List, Tuple
import argparse

class PerformanceResult:
    """Represents a single performance test result."""
    
    def __init__(self, name: str, transport: str, threads: int, clients: int,
                 concurrency: int, interval: int, sent: int, received: int,
                 failures: int, success_rate: str, throughput: str, memory: str):
        self.name = name
        self.transport = transport
        self.threads = threads
        self.clients = clients
        self.concurrency = concurrency
        self.interval = interval
        self.sent = int(sent)
        self.received = int(received)
        self.failures = int(failures)
        self.success_rate = float(success_rate.rstrip('%'))
        self.throughput = float(throughput)
        self.memory = float(memory)
    
    @property
    def total_clients(self) -> int:
        return self.threads * self.clients
    
    @property
    def actual_success_rate(self) -> float:
        """Calculate actual success rate from sent/received."""
        if self.sent == 0:
            return 0.0
        return (self.received / self.sent) * 100

def parse_markdown_report(file_path: str) -> List[PerformanceResult]:
    """Parse a markdown performance report and extract results."""
    results = []
    
    with open(file_path, 'r') as f:
        content = f.read()
    
    # Extract table rows
    # Pattern: | Test | Interval | Sent | Received | Failures | Success Rate | Throughput | Memory |
    table_pattern = r'\| ([^|]+) \| ([^|]+)ms \| ([\d,]+) \| ([\d,]+) \| ([\d,]+) \| ([\d.]+)% \| ([\d,]+) \| ([\d.]+) \|'
    
    for match in re.finditer(table_pattern, content):
        name = match.group(1)
        interval = int(match.group(2))
        sent = int(match.group(3).replace(',', ''))
        received = int(match.group(4).replace(',', ''))
        failures = int(match.group(5).replace(',', ''))
        success_rate = match.group(6)
        throughput = float(match.group(7).replace(',', ''))
        memory = float(match.group(8))
        
        # Parse transport and configuration from name
        if name.startswith('TCP_'):
            transport = 'TCP'
        elif name.startswith('UDP_'):
            transport = 'UDP'
        elif name.startswith('IPC_'):
            transport = 'IPC'
        else:
            continue
        
        # Parse threads and clients from name
        # Pattern: TCP_2t2c100_i2
        config_match = re.search(r'(\d+)t(\d+)c(\d+)_i(\d+)', name)
        if config_match:
            threads = int(config_match.group(1))
            clients = int(config_match.group(2))
            concurrency = int(config_match.group(3))
            interval_config = int(config_match.group(4))
        else:
            threads = 1
            clients = 1
            concurrency = 100
            interval_config = interval
        
        result = PerformanceResult(
            name, transport, threads, clients, concurrency, interval,
            sent, received, failures, success_rate, throughput, memory
        )
        results.append(result)
    
    return results

def compare_results(results_list: List[Tuple[str, List[PerformanceResult]]]) -> str:
    """Compare multiple test runs and generate comparison report."""
    
    report = f"""# UVRPC Performance Comparison Report

**Generated:** {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}  
**Number of Reports:** {len(results_list)}

---

## Summary

"""
    
    # Find best results by category
    best_throughput = None
    best_success_rate = None
    best_memory = None
    
    for report_name, results in results_list:
        for result in results:
            if best_throughput is None or result.throughput > best_throughput.throughput:
                best_throughput = result
            if best_success_rate is None or result.success_rate > best_success_rate.success_rate:
                best_success_rate = result
            if best_memory is None or result.memory < best_memory.memory:
                best_memory = result
    
    report += f"""
### Best Performance

| Metric | Test | Value | Configuration |
|--------|------|-------|---------------|
| **Max Throughput** | {best_throughput.name} | {best_throughput.throughput:,.0f} ops/s | {best_throughput.threads}t, {best_throughput.clients}c, {best_throughput.interval}ms |
| **Best Success Rate** | {best_success_rate.name} | {best_success_rate.success_rate:.1f}% | {best_success_rate.threads}t, {best_success_rate.clients}c, {best_success_rate.interval}ms |
| **Best Memory** | {best_memory.name} | {best_memory.memory:.0f} MB | {best_memory.threads}t, {best_memory.clients}c, {best_memory.interval}ms |

---

## Transport Comparison

### TCP Transport

| Test Name | Threads | Clients | Interval | Sent | Received | Success Rate | Throughput | Memory |
|-----------|---------|---------|----------|------|----------|--------------|------------|--------|
"""
    
    # TCP results
    for report_name, results in results_list:
        for result in results:
            if result.transport == 'TCP':
                report += f"| {result.name} | {result.threads} | {result.clients} | {result.interval}ms | {result.sent:,} | {result.received:,} | {result.success_rate:.1f}% | {result.throughput:,.0f} | {result.memory:.0f} |\n"
    
    report += f"""
### UDP Transport

| Test Name | Threads | Clients | Interval | Sent | Received | Success Rate | Throughput | Memory |
|-----------|---------|---------|----------|------|----------|--------------|------------|--------|
"""
    
    # UDP results
    for report_name, results in results_list:
        for result in results:
            if result.transport == 'UDP':
                report += f"| {result.name} | {result.threads} | {result.clients} | {result.interval}ms | {result.sent:,} | {result.received:,} | {result.success_rate:.1f}% | {result.throughput:,.0f} | {result.memory:.0f} |\n"
    
    report += f"""
### IPC Transport

| Test Name | Threads | Clients | Interval | Sent | Received | Success Rate | Throughput | Memory |
|-----------|---------|---------|----------|------|----------|--------------|------------|--------|
"""
    
    # IPC results
    for report_name, results in results_list:
        for result in results:
            if result.transport == 'IPC':
                report += f"| {result.name} | {result.threads} | {result.clients} | {result.interval}ms | {result.sent:,} | {result.received:,} | {result.success_rate:.1f}% | {result.throughput:,.0f} | {result.memory:.0f} |\n"
    
    report += f"""

---

## Timer Interval Analysis

### TCP Transport by Interval

| Interval | Avg Throughput | Avg Success Rate | Avg Memory | Notes |
|----------|----------------|-------------------|------------|-------|
"""
    
    # Group TCP results by interval
    tcp_by_interval = {}
    for report_name, results in results_list:
        for result in results:
            if result.transport == 'TCP':
                if result.interval not in tcp_by_interval:
                    tcp_by_interval[result.interval] = []
                tcp_by_interval[result.interval].append(result)
    
    for interval in sorted(tcp_by_interval.keys()):
        results = tcp_by_interval[interval]
        avg_throughput = sum(r.throughput for r in results) / len(results)
        avg_success = sum(r.success_rate for r in results) / len(results)
        avg_memory = sum(r.memory for r in results) / len(results)
        
        if interval == 1:
            notes = "Highest throughput, lower success rate"
        elif interval == 2:
            notes = "**Recommended** - Best balance"
        else:
            notes = "Highest success rate, lower throughput"
        
        report += f"| {interval}ms | {avg_throughput:,.0f} | {avg_success:.1f}% | {avg_memory:.0f} | {notes} |\n"
    
    report += f"""
### UDP Transport by Interval

| Interval | Avg Throughput | Avg Success Rate | Avg Memory |
|----------|----------------|-------------------|------------|
"""
    
    # Group UDP results by interval
    udp_by_interval = {}
    for report_name, results in results_list:
        for result in results:
            if result.transport == 'UDP':
                if result.interval not in udp_by_interval:
                    udp_by_interval[result.interval] = []
                udp_by_interval[result.interval].append(result)
    
    for interval in sorted(udp_by_interval.keys()):
        results = udp_by_interval[interval]
        avg_throughput = sum(r.throughput for r in results) / len(results)
        avg_success = sum(r.success_rate for r in results) / len(results)
        avg_memory = sum(r.memory for r in results) / len(results)
        report += f"| {interval}ms | {avg_throughput:,.0f} | {avg_success:.1f}% | {avg_memory:.0f} |\n"
    
    report += f"""

---

## Scalability Analysis

### Throughput vs Thread Count

| Threads | Avg Throughput (TCP) | Avg Throughput (UDP) | Notes |
|---------|---------------------|---------------------|-------|
"""
    
    # Group by thread count
    tcp_by_threads = {}
    udp_by_threads = {}
    
    for report_name, results in results_list:
        for result in results:
            if result.transport == 'TCP':
                if result.threads not in tcp_by_threads:
                    tcp_by_threads[result.threads] = []
                tcp_by_threads[result.threads].append(result)
            elif result.transport == 'UDP':
                if result.threads not in udp_by_threads:
                    udp_by_threads[result.threads] = []
                udp_by_threads[result.threads].append(result)
    
    all_threads = sorted(set(tcp_by_threads.keys()) | set(udp_by_threads.keys()))
    
    for threads in all_threads:
        tcp_avg = sum(r.throughput for r in tcp_by_threads.get(threads, [])) / len(tcp_by_threads.get(threads, [1])) if threads in tcp_by_threads else 0
        udp_avg = sum(r.throughput for r in udp_by_threads.get(threads, [])) / len(udp_by_threads.get(threads, [1])) if threads in udp_by_threads else 0
        report += f"| {threads} | {tcp_avg:,.0f} | {udp_avg:,.0f} |\n"
    
    report += f"""

---

## Recommendations

Based on the analysis:

1. **Timer Interval:**
   - Use **2ms** for best balance of throughput and reliability
   - Use 1ms for maximum throughput (accept lower success rate)
   - Use 5ms for maximum stability (accept lower throughput)

2. **Transport Selection:**
   - **TCP** - Recommended for most use cases (reliable, high-throughput)
   - **UDP** - Good for broadcast scenarios (connectionless)
   - **IPC** - Not recommended until frame protocol is implemented

3. **Concurrency:**
   - 2 threads, 2 clients: Good balance
   - 4 threads, 2 clients: Good scalability
   - 2 threads, 5 clients: Good for high client count

---

**Generated by:** UVRPC Performance Comparison Tool  
**Version:** 1.0
"""
    
    return report

def main():
    parser = argparse.ArgumentParser(description='Compare UVRPC performance test results')
    parser.add_argument('reports', nargs='+', help='Path to markdown report files')
    parser.add_argument('-o', '--output', help='Output file (default: stdout)')
    
    args = parser.parse_args()
    
    # Parse all reports
    results_list = []
    for report_path in args.reports:
        report_name = Path(report_path).stem
        results = parse_markdown_report(report_path)
        if results:
            results_list.append((report_name, results))
            print(f"Parsed {len(results)} results from {report_name}")
        else:
            print(f"Warning: No results found in {report_name}")
    
    if not results_list:
        print("Error: No results found in any report")
        sys.exit(1)
    
    # Generate comparison report
    report = compare_results(results_list)
    
    # Output report
    if args.output:
        with open(args.output, 'w') as f:
            f.write(report)
        print(f"Report saved to: {args.output}")
    else:
        print(report)

if __name__ == '__main__':
    main()