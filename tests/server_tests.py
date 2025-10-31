#!/usr/bin/env python3
import socket
import json
import uuid
import os
import argparse
from typing import Any, Dict, List, Optional

# ----------------------------
# Defaults (override via CLI/env)
# ----------------------------
HOST = "localhost"
PORT = 1234
TIMEOUT = 5

# ----------------------------
# CLI + file selection
# ----------------------------

def parse_args():
    p = argparse.ArgumentParser(description="TWClone Protocol Test Runner")
    p.add_argument("file", nargs="?", default=None,
                   help="Path to test JSON (default: test_data.json)")
    p.add_argument("--suite", default=None,
                   help="Suite name when using a suites file (e.g., 02_nav)")
    p.add_argument("--host", default=None, help=f"Host (default: {HOST})")
    p.add_argument("--port", type=int, default=None, help=f"Port (default: {PORT})")
    p.add_argument("--timeout", type=int, default=None, help=f"Socket timeout seconds (default: {TIMEOUT})")
    p.add_argument("--list-suites", action="store_true", help="List suites in the file and exit")
    p.add_argument("--strict", action="store_true", help="Strict expect matching (no wildcards)")
    # Auth bootstrap
    p.add_argument("--user", default=os.getenv("TW_USER"), help="Username to auth/login")
    p.add_argument("--passwd", default=os.getenv("TW_PASSWD"), help="Password to auth/login")
    p.add_argument("--register-if-missing", action="store_true",
                   help="Try auth.register first; ignore 'already exists' and proceed to login")
    return p.parse_args()

def resolve_test_file(args) -> str:
    # Priority: CLI arg > env var > default
    if args.file:
        return args.file
    if os.getenv("TW_TESTS"):
        return os.getenv("TW_TESTS")
    return "test_data.json"

def resolve_suite(args) -> Optional[str]:
    return args.suite or os.getenv("TW_SUITE")

def load_json(path: str) -> Any:
    with open(path, "r", encoding="utf-8") as fh:
        return json.load(fh)

def list_suites(doc: Any) -> List[str]:
    if isinstance(doc, dict) and isinstance(doc.get("suites"), dict):
        return list(doc["suites"].keys())
    return []

def normalise_tests(doc: Any, suite: Optional[str]) -> List[Dict[str, Any]]:
    """
    Accepted shapes:
      - list of tests
      - { "tests": [...] }
      - { "suites": { "01_smoke": [...], ... } }
      - legacy dict-of-lists (flatten)
    """
    if isinstance(doc, list):
        return doc

    if isinstance(doc, dict):
        if suite and "suites" in doc and isinstance(doc["suites"], dict):
            if suite not in doc["suites"]:
                available = ", ".join(sorted(doc["suites"].keys()))
                raise ValueError(f"Suite '{suite}' not found. Available: {available}")
            if not isinstance(doc["suites"][suite], list):
                raise ValueError(f"Suite '{suite}' is not a list.")
            return doc["suites"][suite]

        if "tests" in doc and isinstance(doc["tests"], list):
            return doc["tests"]

        # legacy: flatten arrays of tests at top-level
        flattened: List[Dict[str, Any]] = []
        for v in doc.values():
            if isinstance(v, list) and v and isinstance(v[0], dict) and "command" in v[0]:
                flattened.extend(v)
        if flattened:
            return flattened

    raise ValueError("Unrecognised test file shape. Expected list, or {tests:[...]}, or {suites:{...}}.")

# ----------------------------
# Socket I/O
# ----------------------------

def recv_line(sock: socket.socket, limit: int = 65536) -> str:
    buf = bytearray()
    while True:
        chunk = sock.recv(1)
        if not chunk:
            break
        if chunk == b"\n":
            break
        buf.extend(chunk)
        if len(buf) >= limit:
            break
    return buf.decode("utf-8", errors="replace")

def send_json_line(sock: socket.socket, obj: Dict[str, Any]) -> None:
    data = json.dumps(obj, ensure_ascii=False)
    sock.sendall(data.encode("utf-8") + b"\n")

# ----------------------------
# Expectation matching
# ----------------------------

def _is_wc(v: Any) -> bool:
    return isinstance(v, str) and v == "*"

def _is_arr_wc(v: Any) -> bool:
    return isinstance(v, str) and v == "*array"

def soft_match(actual: Any, expect: Any, strict: bool = False) -> bool:
    if not strict:
        if _is_wc(expect):
            return True
        if _is_arr_wc(expect):
            return isinstance(actual, list)

    if isinstance(expect, dict):
        if not isinstance(actual, dict):
            return False
        for k, v in expect.items():
            if k not in actual:
                return False
            if not soft_match(actual[k], v, strict=strict):
                return False
        return True

    if isinstance(expect, list):
        if not isinstance(actual, list):
            return False
        if len(expect) > len(actual):
            return False
        for i, ev in enumerate(expect):
            if not soft_match(actual[i], ev, strict=strict):
                return False
        return True

    return actual == expect

# ----------------------------
# Test shape coercion
# ----------------------------

def coerce_command_from_test(name: str, test: Dict[str, Any]) -> Dict[str, Any]:
    """
    Accept either:
      - test["command"] is a dict -> use as-is
      - test["command"] is a string -> wrap to {"command": "<string>"}
      - legacy: test has "type" or "cmd" -> wrap accordingly
    """
    if "command" in test:
        cmd = test["command"]
        if isinstance(cmd, dict):
            return cmd
        if isinstance(cmd, str):
            return {"command": cmd}
        raise TypeError(f"Test '{name}': 'command' must be a string or object.")
    # legacy fallback:
    if "type" in test and isinstance(test["type"], str):
        return {"command": test["type"]}
    if "cmd" in test and isinstance(test["cmd"], str):
        return {"command": test["cmd"]}
    raise TypeError(f"Test '{name}': 'command' must be an object with 'command' field, a string, or legacy 'type'/'cmd'.")

# ----------------------------
# Auth bootstrap (optional)
# ----------------------------

def try_register(sock: socket.socket, user: str, passwd: str) -> None:
    send_json_line(sock, {"command": "auth.register", "data": {"user_name": user, "password": passwd}})
    resp = json.loads(recv_line(sock) or "{}")
    # ok → proceed; error 1210 (exists) → proceed; any other error → print but continue to login
    if resp.get("status") == "ok":
        print(f"[auth.register] PASS  (user={user})")
        return
    err = (resp.get("error") or {}).get("code")
    if err == 1210:
        print(f"[auth.register] SKIP (already exists)")
        return
    print(f"[auth.register] NOTE: {resp}")

def do_login(sock: socket.socket, user: str, passwd: str) -> bool:
    send_json_line(sock, {"command": "auth.login", "data": {"user_name": user, "password": passwd}})
    resp = json.loads(recv_line(sock) or "{}")
    ok = (resp.get("status") == "ok")
    print(f"[auth.login] {'PASS' if ok else 'FAIL'} (user={user})")
    if not ok:
        print("  response:", json.dumps(resp, ensure_ascii=False))
    return ok

# ----------------------------
# Single test execution
# ----------------------------

def run_test(name: str, sock: socket.socket, test: Dict[str, Any], strict: bool = False) -> None:
    command = coerce_command_from_test(name, test)
    expect = test.get("expect", {"status": "ok"})

    # Pass through idempotency_key if present
    if isinstance(command.get("data"), dict) and "idempotency_key" in test:
        command["data"].setdefault("idempotency_key", test["idempotency_key"])

    # Send
    send_json_line(sock, command)

    # Receive
    line = recv_line(sock)
    if not line.strip():
        print(f"[{name}] FAIL: empty response")
        return

    try:
        resp = json.loads(line)
    except Exception as e:
        print(f"[{name}] FAIL: could not parse JSON response: {e}\n  raw: {line!r}")
        return

    ok = soft_match(resp, expect, strict=strict)
    status = resp.get("status")
    rtype = resp.get("type")

    if ok:
        print(f"[{name}] PASS  (status={status}, type={rtype})")
    else:
        print(f"[{name}] FAIL  (status={status}, type={rtype})")
        print("  expect:", json.dumps(expect, ensure_ascii=False))
        print("  actual:", json.dumps(resp, ensure_ascii=False))

# ----------------------------
# Suite execution
# ----------------------------

def run_test_suite():
    args = parse_args()

    test_file = resolve_test_file(args)
    suite = resolve_suite(args)

    # Optional host/port override
    global HOST, PORT, TIMEOUT
    if args.host:
        HOST = args.host
    if args.port:
        PORT = args.port
    if args.timeout:
        TIMEOUT = args.timeout

    print("--- Running TWClone Protocol Tests ---")
    print(f"Using file: {test_file}" + (f" (suite: {suite})" if suite else ""))

    try:
        doc = load_json(test_file)

        if args.list_suites:
            suites = list_suites(doc)
            if suites:
                print("Suites available:")
                for s in sorted(suites):
                    print("  -", s)
            else:
                print("No suites found in this file.")
            return

        tests = normalise_tests(doc, suite=suite)

        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.settimeout(TIMEOUT)
            s.connect((HOST, PORT))

            # Optional auth bootstrap
            if args.user and args.passwd:
                if args.register_if_missing:
                    try_register(s, args.user, args.passwd)
                if not do_login(s, args.user, args.passwd):
                    print("Auth failed; continuing without auth…")

            for test in tests:
                if not isinstance(test, dict):
                    raise TypeError(f"Test must be an object, got: {type(test)} -> {test!r}")

                name = test.get("name", "<unnamed>")
                run_test(name, s, test, strict=args.strict)

    except ConnectionRefusedError:
        print(f"\nERROR: Could not connect to {HOST}:{PORT}. Ensure the server is running.")
    except Exception as e:
        print(f"\nUnexpected error: {e}")

# ----------------------------
# Main
# ----------------------------

if __name__ == "__main__":
    run_test_suite()
