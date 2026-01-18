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

    # Get the directory where this script is located
    base_dir = os.path.dirname(os.path.abspath(__file__))
    
    # --- RIG DATABASE ---
    print("\n>>> RIGGING DATABASE...")
    rig_script = os.path.join(base_dir, "rig_db.py")
    rig_config = os.path.join(base_dir, "test_rig.json")
    try:
        # Run rig_db.py with --reset
        ret = subprocess.run([sys.executable, rig_script, "--config", rig_config, "--reset"], check=True)
        print(">>> DATABASE RIGGED SUCCESSFULLY.")
    except subprocess.CalledProcessError:
        print(">>> FATAL: DATABASE RIGGING FAILED. Aborting tests.")
        sys.exit(1)
    except Exception as e:
        print(f">>> FATAL: Error running rig script: {e}")
        sys.exit(1)
    # --------------------

    # PAUSE FOR SERVER RESTART
    input("\n>>> IMPORTANT: Database rigged. Please restart the game server now. Press Enter to continue tests...")
    # --------------------

    runner = JsonSuiteRunner(args.host, args.port, macros_path=os.path.join(base_dir, "macros.json"))
    
    # Find all .json suites relative to this script
    suites_to_run = sorted(glob.glob(os.path.join(base_dir, "suite_*.json")))
    
    # Add python suites if needed
    py_suites = sorted(glob.glob(os.path.join(base_dir, "suite_*.py")))
    for p in py_suites:
        if os.path.basename(p) not in ["run_suites_all.py", "run_suites.py"]:
            suites_to_run.append(p)

    if not suites_to_run:
        print("ERROR: No test suites found!")
        sys.exit(1)

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
