#!/usr/bin/env python3
import argparse
import glob
import json
import os
import sys
from json_runner import JsonSuiteRunner

import subprocess

# Defaults
HOST = "localhost"
PORT = 1234

def discover_suites(base_dir):
    """Use the canonical JSON directory, not a hand-maintained subset."""
    suites = []
    for path in sorted(glob.glob(os.path.join(base_dir, "suite_*.json"))):
        if os.path.basename(path) == "suite_regression_full.json":
            continue
        with open(path, encoding="utf-8") as source:
            data = json.load(source)
        if isinstance(data.get("tests"), list):
            suites.append(path)
    return suites

def run_all():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default=os.environ.get("TWCLONE_TEST_HOST", HOST))
    parser.add_argument("--port", type=int,
                        default=int(os.environ.get("TWCLONE_TEST_PORT", PORT)))
    parser.add_argument("--suite", help="Run specific suite file")
    parser.add_argument("--capture-responses", help="Append redacted RPC responses as JSONL")
    args = parser.parse_args()

    runner = JsonSuiteRunner(args.host, args.port,
                             capture_responses=args.capture_responses)
    
    suites_to_run = discover_suites(os.path.dirname(os.path.abspath(__file__)))
    if args.suite:
        suites_to_run = [args.suite]

    failed = []
    for suite in suites_to_run:
        if not os.path.exists(suite):
            print(f"Skipping missing suite: {suite}")
            continue
            
        print(f"\n>>> EXECUTING SUITE: {suite}")
        
        success = False
        if suite.endswith(".json"):
            success = runner.run_suite(suite)
        elif suite.endswith(".py"):
            try:
                # Pass host/port via env vars or args to the script
                env = os.environ.copy()
                env["HOST"] = str(args.host)
                env["PORT"] = str(args.port)
                ret = subprocess.run([sys.executable, suite], env=env)
                success = (ret.returncode == 0)
            except Exception as e:
                print(f"Subprocess error: {e}")
                success = False
        
        if not success:
            failed.append(suite)
            print(f">>> SUITE FAILED: {suite}")
        else:
            print(f">>> SUITE PASSED: {suite}")

    print("\n" + "="*40)
    if failed:
        print(f"FAILURES: {len(failed)} suites failed.")
        for f in failed:
            print(f" - {f}")
        sys.exit(1)
    else:
        print("ALL SUITES PASSED.")
        sys.exit(0)

if __name__ == "__main__":
    run_all()
