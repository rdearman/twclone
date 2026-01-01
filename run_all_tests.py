import os
import time
import subprocess
import glob
import signal

TEST_DIR = "tests.v2"
EXCLUDE = ["not_covered.json", "tests_tdb.json", "macros.json", "debug_auth.json", "debug_sysop.json", "json_runner.py", "run_suites.py", "run_suites_all.py"]
SERVER_BIN = "./bin/server"
SERVER_LOG = "server.log"

def run_tests():
    test_files = glob.glob(os.path.join(TEST_DIR, "*.json"))
    test_files = [f for f in test_files if os.path.basename(f) not in EXCLUDE]
    test_files.sort()

    for test_file in test_files:
        print(f"----------------------------------------------------------------")
        print(f"Running test file: {test_file}")
        print(f"----------------------------------------------------------------")

        try:
            # Run Test
            result = subprocess.run(
                ["python3", "tests.v2/json_runner.py", test_file],
                capture_output=True,
                text=True
            )
            print(result.stdout)
            if result.stderr:
                print("Errors:", result.stderr)
            
            if result.returncode != 0:
                print(f"Test {test_file} FAILED with return code {result.returncode}")
            else:
                print(f"Test {test_file} PASSED")

        except Exception as e:
            print(f"Exception running test {test_file}: {e}")
        
        finally:
            pass # No server management by the test script

if __name__ == "__main__":
    run_tests()
