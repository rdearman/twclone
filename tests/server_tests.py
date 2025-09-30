#!/usr/bin/env python3
import socket
import json
import uuid
import os

# Configuration from the test data file
HOST = "localhost"
PORT = 1234
TIMEOUT = 5
TEST_DATA_FILE = "test_data.json"

# ---------- I/O helpers (unchanged) ----------

def recv_line(sock, limit=65536):
    buf = bytearray()
    while True:
        chunk = sock.recv(4096)
        if not chunk:
            break
        buf.extend(chunk)
        if b"\n" in chunk or len(buf) >= limit:
            break
    s = bytes(buf)
    if b"\n" in s:
        s = s[:s.index(b"\n")]
    return s.decode("utf-8")

def send_and_receive(sock, message_dict):
    # Add a unique ID and timestamp to each message for correlation
    message_dict["id"] = str(uuid.uuid4())
    message_dict["ts"] = "2025-09-17T19:45:12.345Z" # Placeholder timestamp

    try:
        msg = json.dumps(message_dict) + "\n"
        sock.sendall(msg.encode("utf-8"))
        line = recv_line(sock).strip()
        return json.loads(line) if line else {"status": "ERROR", "message": "empty reply"}
    except socket.timeout:
        return {"status": "TIMEOUT"}
    except json.JSONDecodeError:
        return {"status": "ERROR", "message": "Invalid JSON response from server"}
    except Exception as e:
        return {"status": "ERROR", "message": str(e)}

# ---------- Test runner function ----------

def run_test(name, sock, message, expect):
    print(f"TRIED: {name}")
    resp = send_and_receive(sock, message)
    print(f"RETURNED: {resp}")

    ok = True
    for k, v in expect.items():
        if k == "code":  # Special handling for nested error code
            if resp.get("error", {}).get(k) != v:
                ok = False
                break
        else:
            if resp.get(k) != v:
                ok = False
                break
    
    # Check for unexpected transport/parser failures
    if resp.get("status") in ["TIMEOUT", "ERROR"] and not (expect and expect.get("status") in ["TIMEOUT", "ERROR"]):
        ok = False

    print(f"TEST: {'PASS' if ok else 'FAIL'}")
    print("-" * 20)
    return resp

def run_test_suite():
    if not os.path.exists(TEST_DATA_FILE):
        print(f"ERROR: Test data file '{TEST_DATA_FILE}' not found.")
        return

    try:
        with open(TEST_DATA_FILE, 'r') as f:
            tests = json.load(f)
    except json.JSONDecodeError:
        print(f"ERROR: Invalid JSON in file '{TEST_DATA_FILE}'.")
        return

    print("--- Running TWClone Protocol Tests ---")
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.settimeout(TIMEOUT)
            s.connect((HOST, PORT))
            
            for test in tests:
                run_test(test["name"], s, test["command"], test["expect"])
            
    except ConnectionRefusedError:
        print(f"\nERROR: Could not connect to {HOST}:{PORT}. Ensure the server is running.")
    except Exception as e:
        print(f"\nUnexpected error: {e}")

# ---------- Main execution block ----------

if __name__ == "__main__":
    run_test_suite()
