#!/usr/bin/env python3
"""
Regression Test Orchestrator
────────────────────────────────────────────────────────────────────────────────
Runs all critical feature test suites in sequence, providing a single entry point
for full regression validation.

Usage:
    python3 run_regression_full.py [--host localhost] [--port 1234]

Suites executed (in order):
  1. suite_fedspace_aggression_captainz.json
     - Captain Z hard-punish enforcement in sectors 1-10
  2. suite_fedspace_msl_asset_enforcement.json
     - FedSpace MSL asset deployment denial
  3. suite_cluster_generation_v2_invariants.json
     - Database schema invariants and cluster generation constraints
  4. suite_police_phase_a.json
     - Police RPC handlers and jurisdiction logic
  5. suite_npc_ferengi_init.json
     - Ferengi NPC auto-creation and idempotency
"""

import argparse
import os
import sys
import glob
import subprocess

# Defaults
HOST = "localhost"
PORT = 1234

# Regression suites in execution order
REGRESSION_SUITES = [
    "suite_fedspace_aggression_captainz.json",
    "suite_fedspace_msl_asset_enforcement.json",
    "suite_cluster_generation_v2_invariants.json",
    "suite_police_phase_a.json",
    "suite_npc_ferengi_init.json"
]

def run_regression():
    parser = argparse.ArgumentParser(
        description="Run full regression test suite for police/FedSpace/cluster features"
    )
    parser.add_argument("--host", default=HOST, help="Server hostname")
    parser.add_argument("--port", type=int, default=PORT, help="Server port")
    parser.add_argument("--no-rig", action="store_true", help="Skip database rigging (assume pre-rigged)")
    args = parser.parse_args()

    base_dir = os.path.dirname(os.path.abspath(__file__))
    
    # --- RIG DATABASE (unless --no-rig) ---
    if not args.no_rig:
        print("\n" + "="*80)
        print(">>> RIGGING DATABASE...")
        print("="*80)
        rig_script = os.path.join(base_dir, "rig_db.py")
        rig_config = os.path.join(base_dir, "test_rig.json")
        try:
            ret = subprocess.run(
                [sys.executable, rig_script, "--config", rig_config, "--reset"],
                check=True
            )
            print(">>> DATABASE RIGGED SUCCESSFULLY.")
        except subprocess.CalledProcessError:
            print(">>> FATAL: DATABASE RIGGING FAILED. Aborting tests.")
            sys.exit(1)
        except Exception as e:
            print(f">>> FATAL: Error running rig script: {e}")
            sys.exit(1)
        
        # PAUSE FOR SERVER RESTART
        input("\n>>> IMPORTANT: Database rigged. Please restart the game server now. Press Enter to continue tests...")
    else:
        print("\n>>> Skipping database rigging (--no-rig)")
    
    # --- RUN REGRESSION SUITES ---
    print("\n" + "="*80)
    print(">>> STARTING REGRESSION TEST SUITE")
    print("="*80)
    
    results = {}
    failed_suites = []
    
    for suite_name in REGRESSION_SUITES:
        suite_path = os.path.join(base_dir, suite_name)
        
        if not os.path.exists(suite_path):
            print(f"\n⚠ WARNING: Suite not found: {suite_path}")
            results[suite_name] = "MISSING"
            failed_suites.append(suite_name)
            continue
        
        print(f"\n" + "-"*80)
        print(f"Running: {suite_name}")
        print("-"*80)
        
        try:
            result = subprocess.run(
                [sys.executable, os.path.join(base_dir, "json_runner.py"), suite_path,
                 "--host", args.host, "--port", str(args.port)],
                capture_output=True,
                text=True,
                timeout=300
            )
            
            print(result.stdout)
            if result.stderr:
                print("STDERR:", result.stderr)
            
            if result.returncode != 0:
                print(f"❌ FAILED: {suite_name} (return code {result.returncode})")
                results[suite_name] = "FAILED"
                failed_suites.append(suite_name)
            else:
                print(f"✅ PASSED: {suite_name}")
                results[suite_name] = "PASSED"
        
        except subprocess.TimeoutExpired:
            print(f"❌ TIMEOUT: {suite_name} (exceeded 300s)")
            results[suite_name] = "TIMEOUT"
            failed_suites.append(suite_name)
        except Exception as e:
            print(f"❌ ERROR: {suite_name} - {e}")
            results[suite_name] = "ERROR"
            failed_suites.append(suite_name)
    
    # --- SUMMARY ---
    print("\n" + "="*80)
    print(">>> REGRESSION TEST RESULTS")
    print("="*80)
    
    for suite_name, status in results.items():
        symbol = "✅" if status == "PASSED" else "❌"
        print(f"{symbol} {suite_name}: {status}")
    
    print("\n" + "="*80)
    if failed_suites:
        print(f"FAILED: {len(failed_suites)} suite(s)")
        print("Failed suites:")
        for suite in failed_suites:
            print(f"  - {suite}")
        print("="*80)
        sys.exit(1)
    else:
        print(f"SUCCESS: All {len(REGRESSION_SUITES)} suites passed!")
        print("="*80)
        sys.exit(0)

if __name__ == "__main__":
    run_regression()
