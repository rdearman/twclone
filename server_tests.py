#!/usr/bin/env python3
import socket
import json

HOST = "localhost"
PORT = 1234
TIMEOUT = 5

VALID_USER = {
    "player_num": 1,
    "player_name": "thor",
    "password": "hammer",
    "ship_num": 1,
    "sector": 1,
    "ship_name": "Valhalla",
    "ship_type": 1,
}

INVALID_USER = {
    "player_num": 999,
    "player_name": "loki",
    "password": "lie",
    "ship_num": 999,
    "sector": 999,
    "ship_name": "Jotunheim",
    "ship_type": 1,
}

# ---------- I/O helpers ----------

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

def run_test(name, sock, message, expect_exact=None, expect=None):
    print(f"TRIED: {name}")
    resp = send_and_receive(sock, message)
    print(f"RETURNED: {resp}")

    ok = True
    if expect_exact is not None:
        ok = (resp == expect_exact)
    if expect is not None:
        for k, v in expect.items():
            if resp.get(k) != v:
                ok = False
                break
    # Treat transport/parser failures as FAIL unless explicitly expected
    if resp.get("status") in ["TIMEOUT", "ERROR"] and not (expect and expect.get("status") == "ERROR"):
        ok = False

    print(f"TEST: {'PASS' if ok else 'FAIL'}")
    print("-" * 20)
    return resp

# ---------- Schema-guard tests ----------

def test_schema_guard_missing_command(sock):
    run_test("Schema guard (missing 'command'/'event')",
             sock,
             {"player_name": "thor", "password": "hammer"},
             expect={"status": "error", "code": 1300})

def test_schema_guard_not_json(sock):
    print("TRIED: Schema guard (not JSON)")
    try:
        sock.sendall(b"this is not json\n")
        resp = json.loads(recv_line(sock))
    except Exception as e:
        resp = {"status": "ERROR", "message": str(e)}
    print(f"RETURNED: {resp}")
    ok = (resp.get("status") == "error" and resp.get("code") == 1300)
    print(f"TEST: {'PASS' if ok else 'FAIL'}")
    print("-" * 20)

# ---------- Suites matching current stubbed server behaviour ----------

def suite_valid_user(sock):
    # Login OK
    run_test("login(valid)", sock,
             {"command": "login", "player_name": VALID_USER["player_name"], "password": VALID_USER["password"]},
             expect={"status": "OK"})

    # Schema guards
    test_schema_guard_missing_command(sock)
    test_schema_guard_not_json(sock)

    # Commands that currently return OK (no auth gating yet)
    for cmd in ["DESCRIPTION", "MYINFO", "ONLINE", "QUIT"]:
        run_test(cmd, sock, {"command": cmd}, expect={"status": "OK"})

    # Known commands with required args → OK
    run_test("PLAYERINFO OK", sock,
             {"command": "PLAYERINFO", "player_num": VALID_USER["player_num"]},
             expect={"status": "OK"})
    run_test("SHIPINFO OK", sock,
             {"command": "SHIPINFO", "ship_num": VALID_USER["ship_num"]},
             expect={"status": "OK"})

    # Missing required fields → 1201
    run_test("PLAYERINFO missing field", sock,
             {"command": "PLAYERINFO"},
             expect={"status": "error", "code": 1201})
    run_test("SHIPINFO missing field", sock,
             {"command": "SHIPINFO"},
             expect={"status": "error", "code": 1201})

    # Unknown commands → 1400 (includes NEW until implemented)
    run_test("INVALID_COMMAND", sock,
             {"command": "INVALID_COMMAND"},
             expect={"status": "error", "code": 1400})
    run_test("NEW (unknown for now)", sock,
             {"command": "NEW", "player_name": "loki", "password": "lie", "ship_name": "Jotunheim"},
             expect={"status": "error", "code": 1400})

def suite_invalid_user(sock):
    # Invalid login still returns OK in the current stub (no auth check yet)
    run_test("login(invalid creds - stub OK)", sock,
             {"command": "login", "player_name": INVALID_USER["player_name"], "password": INVALID_USER["password"]},
             expect={"status": "OK"})

    # Pre-login commands behave the same for now (OK stubs)
    for cmd in ["DESCRIPTION", "MYINFO", "ONLINE"]:
        run_test(f"{cmd} (pre-login stub)", sock, {"command": cmd}, expect={"status": "OK"})

    # Player/Ship info with numbers (stub OK)
    run_test("PLAYERINFO pre-login stub", sock,
             {"command": "PLAYERINFO", "player_num": INVALID_USER["player_num"]},
             expect={"status": "OK"})
    run_test("SHIPINFO pre-login stub", sock,
             {"command": "SHIPINFO", "ship_num": INVALID_USER["ship_num"]},
             expect={"status": "OK"})

    # Quit
    run_test("QUIT", sock, {"command": "QUIT"}, expect={"status": "OK"})

# ---------- Main ----------

if __name__ == "__main__":
    print("--- Running TWClone Protocol Tests ---")
    print("--- Testing with VALID User Data ---")
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.settimeout(TIMEOUT)
            s.connect((HOST, PORT))
            suite_valid_user(s)
    except ConnectionRefusedError:
        print(f"\nERROR: Could not connect to {HOST}:{PORT}. Ensure the server is running.")
    except Exception as e:
        print(f"\nUnexpected error: {e}")

    print("\n" + "=" * 40)
    print("--- Running TWClone Protocol Tests ---")
    print("--- Testing with INVALID User Data ---")
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.settimeout(TIMEOUT)
            s.connect((HOST, PORT))
            suite_invalid_user(s)
    except ConnectionRefusedError:
        print(f"\nERROR: Could not connect to {HOST}:{PORT}. Ensure the server is running.")
    except Exception as e:
        print(f"\nUnexpected error: {e}")
