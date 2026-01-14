import subprocess
import os
import sys

def run_cmd(cmd):
    print(f"\n>>> Executing: {' '.join(cmd)}")
    try:
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.stdout:
            print(result.stdout)
        if result.stderr:
            print(result.stderr, file=sys.stderr)
        return result.returncode == 0
    except Exception as e:
        print(f"Error: {e}")
        return False

print("Starting Phase 3A.1 Verification...")

# 1. Build
if not run_cmd(["make", "clean"]):
    print("FAILED: make clean")
elif not run_cmd(["make"]):
    print("FAILED: make")
# 2. DB Gates
elif not run_cmd(["make", "db-gates"]):
    print("FAILED: make db-gates")
# 3. Server Check
elif not run_cmd(["./bin/server", "--help"]):
    print("FAILED: server help check")
else:
    print("\n✅ VERIFICATION SUCCESSFUL")
