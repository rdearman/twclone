#!/usr/bin/env python3
import argparse
import os
import sys
from json_runner import JsonSuiteRunner

import subprocess

# Defaults
HOST = "localhost"
PORT = 1234

SUITES = [
    "tests.v2/suite_smoke.json",
    "tests.v2/suite_planets.json",
    "tests.v2/suite_trade_market.json",
    "tests.v2/suite_shipyard_and_ships.json",
    "tests.v2/suite_corporations.json",
    "tests.v2/suite_combat_and_crime.json",
    "tests.v2/suite_movement_parity.py",
    "tests.v2/suite_subscriptions_e2e.py",
    # "tests.v2/suite_concurrency.py",  # Uncomment when ready to run
    # "tests.v2/suite_auth_and_settings.json",
    # "tests.v2/suite_economy_and_bank.json",
    # "tests.v2/suite_trade_market.json",
    # "tests.v2/suite_shipyard_and_ships.json",
    # "tests.v2/suite_corporations.json",
    # "tests.v2/suite_combat_and_crime.json",
    # "tests.v2/suite_navigation.json",
    # "tests.v2/suite_admin_sysop.json",
]

def run_all():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default=HOST)
    parser.add_argument("--port", type=int, default=PORT)
    parser.add_argument("--suite", help="Run specific suite file")
    args = parser.parse_args()

    runner = JsonSuiteRunner(args.host, args.port)
    
    suites_to_run = SUITES
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
