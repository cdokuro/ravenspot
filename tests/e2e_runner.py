#!/usr/bin/env python3
"""
RavenSpot Native C++ E2E Test Runner Orchestrator.
Dispatches and reports across Tiers 1-4 with ANSI colors and JSON export.
"""

import argparse
import io
import json
import os
import sys
import time
import unittest

# Color definitions
RESET = "\033[0m"
BOLD = "\033[1m"
RED = "\033[31m"
GREEN = "\033[32m"
YELLOW = "\033[33m"
BLUE = "\033[34m"
MAGENTA = "\033[35m"
CYAN = "\033[36m"
WHITE = "\033[37m"

PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
if PROJECT_ROOT not in sys.path:
    sys.path.insert(0, PROJECT_ROOT)

class ColorTestResult(unittest.TestResult):
    def __init__(self, stream=None, descriptions=None, verbosity=1):
        super().__init__(stream, descriptions, verbosity)
        self.verbosity = verbosity
        self.test_records = []

    def startTest(self, test):
        super().startTest(test)
        self._test_start_time = time.perf_counter()

    def addSuccess(self, test):
        super().addSuccess(test)
        elapsed = time.perf_counter() - self._test_start_time
        record = {
            "name": test.id(),
            "status": "PASS",
            "duration": elapsed,
            "error": None
        }
        self.test_records.append(record)
        if self.verbosity >= 2:
            print(f"  {GREEN}PASS{RESET} {test.id()} ({elapsed*1000:.1f}ms)")
        elif self.verbosity == 1:
            sys.stdout.write(f"{GREEN}.{RESET}")
            sys.stdout.flush()

    def addFailure(self, test, err):
        super().addFailure(test, err)
        elapsed = time.perf_counter() - self._test_start_time
        record = {
            "name": test.id(),
            "status": "FAIL",
            "duration": elapsed,
            "error": self._exc_info_to_string(err, test)
        }
        self.test_records.append(record)
        if self.verbosity >= 2:
            print(f"  {RED}FAIL{RESET} {test.id()} ({elapsed*1000:.1f}ms)")
        elif self.verbosity == 1:
            sys.stdout.write(f"{RED}F{RESET}")
            sys.stdout.flush()

    def addError(self, test, err):
        super().addError(test, err)
        elapsed = time.perf_counter() - self._test_start_time
        record = {
            "name": test.id(),
            "status": "ERROR",
            "duration": elapsed,
            "error": self._exc_info_to_string(err, test)
        }
        self.test_records.append(record)
        if self.verbosity >= 2:
            print(f"  {RED}ERROR{RESET} {test.id()} ({elapsed*1000:.1f}ms)")
        elif self.verbosity == 1:
            sys.stdout.write(f"{RED}E{RESET}")
            sys.stdout.flush()

def load_tests_for_tier(tier: int, feature: int = None):
    loader = unittest.TestLoader()
    suite = unittest.TestSuite()
    tests_dir = os.path.dirname(__file__)

    if tier == 1:
        start_dir = os.path.join(tests_dir, "tier1_features")
        if feature is not None:
            pattern = f"test_f{feature:02d}_*.py"
        else:
            pattern = "test_f*.py"
        suite.addTests(loader.discover(start_dir, pattern=pattern))
    elif tier == 2:
        start_dir = os.path.join(tests_dir, "tier2_boundaries")
        if feature is not None:
            pattern = f"test_b{feature:02d}_*.py"
        else:
            pattern = "test_b*.py"
        suite.addTests(loader.discover(start_dir, pattern=pattern))
    elif tier == 3:
        start_dir = os.path.join(tests_dir, "tier3_interactions")
        suite.addTests(loader.discover(start_dir, pattern="test_*.py"))
    elif tier == 4:
        start_dir = os.path.join(tests_dir, "tier4_scenarios")
        suite.addTests(loader.discover(start_dir, pattern="test_*.py"))

    return suite

def print_banner():
    banner = f"""
{CYAN}{BOLD}======================================================================
  RavenSpot Native C++ Rewrite — Opaque-Box E2E Test Suite
======================================================================{RESET}"""
    print(banner)

def main():
    parser = argparse.ArgumentParser(description="RavenSpot E2E Test Suite Runner")
    parser.add_argument("--tier", type=int, choices=[1, 2, 3, 4], help="Run only specific tier (1, 2, 3, or 4)")
    parser.add_argument("--feature", type=int, choices=range(1, 21), help="Run specific feature (1-20, applicable to tier 1 or 2)")
    parser.add_argument("--all", action="store_true", default=True, help="Run all tiers (default)")
    parser.add_argument("--verbose", "-v", action="store_true", help="Verbose per-test output")
    parser.add_argument("--json", type=str, help="Export test results to specified JSON file")
    args = parser.parse_args()

    print_banner()

    tiers_to_run = [args.tier] if args.tier else [1, 2, 3, 4]
    verbosity = 2 if args.verbose else 1

    overall_start = time.perf_counter()
    tier_results = {}
    all_records = []
    total_passed = 0
    total_failed = 0
    total_errors = 0

    tier_descriptions = {
        1: "Tier 1: Feature Coverage (20 Features, >=5 tests each)",
        2: "Tier 2: Boundary & Corner Cases (20 Features, >=5 tests each)",
        3: "Tier 3: Pairwise Combinatorial Interactions (Cross-Feature)",
        4: "Tier 4: Real-World Application Workload Scenarios"
    }

    for t in tiers_to_run:
        desc = tier_descriptions[t]
        print(f"\n{BOLD}{YELLOW}▶ Executing {desc}{RESET}")
        suite = load_tests_for_tier(t, args.feature)
        count = suite.countTestCases()
        if count == 0:
            print(f"  {YELLOW}No tests found matching filter.{RESET}")
            continue

        result = ColorTestResult(verbosity=verbosity)
        suite.run(result)
        if verbosity == 1:
            print()  # newline after dots

        passed = len(result.test_records) - len(result.failures) - len(result.errors)
        failed = len(result.failures)
        errors = len(result.errors)

        total_passed += passed
        total_failed += failed
        total_errors += errors

        tier_results[t] = {
            "total": count,
            "passed": passed,
            "failed": failed,
            "errors": errors,
            "failures": result.failures,
            "error_list": result.errors
        }
        all_records.extend(result.test_records)

    overall_duration = time.perf_counter() - overall_start

    # Summary Table
    print(f"\n{CYAN}{BOLD}======================================================================{RESET}")
    print(f"{CYAN}{BOLD}                      E2E TEST EXECUTION SUMMARY                      {RESET}")
    print(f"{CYAN}{BOLD}======================================================================{RESET}")
    print(f" {'Tier':<8} | {'Scope / Description':<40} | {'Total':<6} | {'Passed':<6} | {'Failed':<6}")
    print(f" {('-'*8)} | {('-'*40)} | {('-'*6)} | {('-'*6)} | {('-'*6)}")

    for t, res in tier_results.items():
        status_color = GREEN if (res['failed'] == 0 and res['errors'] == 0) else RED
        print(f" Tier {t:<3} | {tier_descriptions[t][:40]:<40} | {res['total']:<6} | {status_color}{res['passed']:<6}{RESET} | {res['failed'] + res['errors']:<6}")

    print(f" {('-'*8)} | {('-'*40)} | {('-'*6)} | {('-'*6)} | {('-'*6)}")
    total_tests = total_passed + total_failed + total_errors
    summary_color = GREEN if (total_failed == 0 and total_errors == 0) else RED
    print(f" {'TOTAL':<8} | {'All Executed Tiers':<40} | {total_tests:<6} | {summary_color}{total_passed:<6}{RESET} | {total_failed + total_errors:<6}")
    print(f"{CYAN}{BOLD}======================================================================{RESET}")
    print(f" Execution time : {overall_duration:.2f} seconds")
    print(f" Final Status   : {summary_color}{BOLD}{'ALL TESTS PASSED' if (total_failed == 0 and total_errors == 0) else 'TEST FAILURES DETECTED'}{RESET}")
    print(f"{CYAN}{BOLD}======================================================================{RESET}\n")

    # JSON export if requested
    if args.json:
        report_data = {
            "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "total_duration_sec": overall_duration,
            "summary": {
                "total": total_tests,
                "passed": total_passed,
                "failed": total_failed,
                "errors": total_errors,
                "success": (total_failed == 0 and total_errors == 0)
            },
            "tiers": {
                f"tier_{t}": {
                    "total": r["total"],
                    "passed": r["passed"],
                    "failed": r["failed"] + r["errors"]
                } for t, r in tier_results.items()
            },
            "tests": all_records
        }
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump(report_data, f, indent=2)
        print(f"{GREEN}✓ Test results exported to JSON:{RESET} {args.json}\n")

    # Failures detail print
    if total_failed > 0 or total_errors > 0:
        print(f"{RED}{BOLD}Failures and Errors Detail:{RESET}")
        for t, res in tier_results.items():
            for test, traceback_str in res["failures"]:
                print(f"{RED}[FAIL] Tier {t} - {test.id()}:{RESET}\n{traceback_str}")
            for test, traceback_str in res["error_list"]:
                print(f"{RED}[ERROR] Tier {t} - {test.id()}:{RESET}\n{traceback_str}")
        sys.exit(1)

    sys.exit(0)

if __name__ == "__main__":
    main()
