import json
import os
import glob
import csv
import sys
from collections import defaultdict

# Add tests.v2 to sys.path so we can find twclient
sys.path.append(os.path.join(os.getcwd(), "tests.v2"))
from twclient import TWClient

HOST = "127.0.0.1"
PORT = 1234
TEST_DIR = "tests.v2"
OUTPUT_CSV = "tests.v2/coverage_report.csv"
OUTPUT_MD = "tests.v2/COVERAGE.md"

def get_server_commands():
    client = TWClient(HOST, PORT)
    try:
        client.connect()
        
        # Login required for system.cmd_list
        if not client.login("admin", "password"):
            print("Failed to login admin.")
            return []

        client.send_json({"command": "system.cmd_list"})
        resp = client.recv_next_non_notice()
        if resp.get("status") != "ok":
            print(f"Error fetching commands: {resp}")
            return []
        
        cmds = resp.get("data", {}).get("commands", [])
        return [c["cmd"] for c in cmds]
    except Exception as e:
        print(f"Exception fetching commands: {e}")
        return []
    finally:
        client.close()

def scan_tests():
    coverage = defaultdict(lambda: {"pos": False, "neg": False})
    
    test_files = glob.glob(os.path.join(TEST_DIR, "*.json"))
    for tf in test_files:
        if "macros.json" in tf or "test_rig.json" in tf:
            continue
            
        with open(tf, 'r') as f:
            try:
                data = json.load(f)
                tests = data.get("tests", [])
                for t in tests:
                    cmd = t.get("command")
                    if not cmd: continue
                    
                    # Heuristic for Positive vs Negative
                    expect = t.get("expect", {"status": "ok"})
                    status = expect.get("status", "ok")
                    
                    if status == "ok":
                        coverage[cmd]["pos"] = True
                    else:
                        coverage[cmd]["neg"] = True
                        
            except json.JSONDecodeError:
                print(f"Error decoding {tf}")
                
    return coverage

def main():
    server_cmds = get_server_commands()
    if not server_cmds:
        print("No commands found from server. Is it running?")
        sys.exit(1)
        
    test_coverage = scan_tests()
    
    # Merge
    final_report = []
    # Also track commands that only appear in tests but not in system.cmd_list?
    # No, only server advertised commands.
    
    for cmd in sorted(server_cmds):
        pos = "Y" if test_coverage[cmd]["pos"] else "N"
        neg = "Y" if test_coverage[cmd]["neg"] else "N"
        final_report.append({"command": cmd, "positive": pos, "negative": neg})
        
    # Write CSV
    with open(OUTPUT_CSV, 'w', newline='') as csvfile:
        fieldnames = ['command', 'positive', 'negative']
        writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
        writer.writeheader()
        for row in final_report:
            writer.writerow(row)
            
    # Write MD
    with open(OUTPUT_MD, 'w') as md:
        md.write("# Test Coverage Report\n\n")
        md.write("| Command | Positive | Negative |\n")
        md.write("|---|---|---|")
        for row in final_report:
            md.write(f"| {row['command']} | {row['positive']} | {row['negative']} |\n")
            
    print(f"Coverage report generated: {OUTPUT_CSV} and {OUTPUT_MD}")

if __name__ == "__main__":
    main()