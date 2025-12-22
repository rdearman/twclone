#!/usr/bin/env python3
import argparse
import os
import sys
import glob
from json_runner import JsonSuiteRunner
import subprocess

# Defaults
HOST = "localhost"
PORT = 1234

def run_all():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default=HOST)
    parser.add_argument("--port", type=int, default=PORT)
    args = parser.parse_args()

    runner = JsonSuiteRunner(args.host, args.port)
    
    # Find all .json suites in tests.v2
    suites_to_run = sorted(glob.glob("tests.v2/suite_*.json"))
    
    # Add python suites if needed (excluding this script and run_suites.py)
    py_suites = sorted(glob.glob("tests.v2/suite_*.py"))
    suites_to_run.extend(py_suites)

    failed = []
    for suite in suites_to_run:
        print(f"\n>>> EXECUTING SUITE: {suite}")
        
        success = False
        if suite.endswith(".json"):
            success = runner.run_suite(suite)
        elif suite.endswith(".py"):
            try:
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
