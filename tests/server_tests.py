#!/usr/bin/env python3
import socket
import json
import uuid
import os
import argparse
from typing import Any, Dict, List, Optional
# --- add near top ---
from typing import Tuple


# ----------------------------
# Defaults (override via CLI/env)
# ----------------------------
HOST = "localhost"
PORT = 1234
TIMEOUT = 5
DUMP_ON_FAIL = False

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
    p.add_argument("--dump-on-fail", action="store_true",
                   help="Print full JSON response on expect/assert failures")
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
# --- add with other helpers ---

def _walk_json(obj, path_prefix=""):
    """Yield tuples (path, value) for all dict key paths to leaf values."""
    if isinstance(obj, dict):
        for k, v in obj.items():
            newp = f"{path_prefix}.{k}" if path_prefix else k
            yield from _walk_json(v, newp)
    elif isinstance(obj, list):
        for i, v in enumerate(obj):
            newp = f"{path_prefix}.{i}" if path_prefix else str(i)
            yield from _walk_json(v, newp)
    else:
        yield (path_prefix, obj)

def _looks_like_turns(key: str, val: Any) -> bool:
    if not isinstance(val, (int, float)):
        return False
    k = key.lower()
    # prefer exact-ish names
    strong = ("turns_remaining", "remaining_turns", "turns.left", "turns.remaining")
    if any(s in k for s in strong):
        return True
    # fallback keywords check
    return ("turn" in k and ("remain" in k or "left" in k or k.endswith(".turns")))

def _find_turns_value(obj: Any) -> Optional[Any]:
    """Search typical places first, then walk everything."""
    # Fast-path guesses
    fast_paths = [
        "data.player.turns_remaining",
        "data.turns_remaining",
        "data.turns.remaining",
        "data.turns",
        "player.turns_remaining",
        "player.turns",
        "turns_remaining",
        "turns",
    ]
    for p in fast_paths:
        v = _get_by_path(obj, p)
        if isinstance(v, (int, float)):
            return v
    # Heuristic search
    best = None
    best_path_len = 999
    for path, val in _walk_json(obj):
        if _looks_like_turns(path, val):
            # choose the shortest path as "best"
            pl = path.count(".")
            if pl < best_path_len:
                best = val
                best_path_len = pl
    return best


def _get_by_path_one(obj: Any, path: str) -> Any:
    if not path:
        return obj
    cur = obj
    for part in path.split("."):
        if isinstance(cur, dict):
            cur = cur.get(part)
        elif isinstance(cur, list) and part.isdigit():
            idx = int(part)
            if 0 <= idx < len(cur):
                cur = cur[idx]
            else:
                return None
        else:
            return None
    return cur

def _get_by_path(obj: Any, path: str) -> Any:
    if path == "$turns":
        return _find_turns_value(obj)
    # support fallback: "a.b|x.y|z"
    for candidate in path.split("|"):
        val = _get_by_path_one(obj, candidate.strip())
        if val is not None:
            return val
    return None


def _expand_vars_in_obj(o: Any, vars: Dict[str, Any]) -> Any:
    if isinstance(o, str) and o.startswith("@"):
        return vars.get(o[1:], o)  # leave as-is if missing
    if isinstance(o, list):
        return [_expand_vars_in_obj(x, vars) for x in o]
    if isinstance(o, dict):
        return {k: _expand_vars_in_obj(v, vars) for k, v in o.items()}
    return o


def _resolve_value(val: Any, vars: Dict[str, Any]) -> Any:
    if isinstance(val, str) and val.startswith("@"):
        return vars.get(val[1:])
    return val

def _run_asserts(resp: Dict[str, Any], assertions: List[Dict[str, Any]], vars: Dict[str, Any]) -> Tuple[bool, List[str]]:
    errs = []
    ok_all = True
    for a in assertions:
        path = a.get("path")
        op = a.get("op", "==")
        actual = _get_by_path(resp, path)
        if op == "delta":
            baseline = _resolve_value(a.get("baseline"), vars)
            want_delta = _resolve_value(a.get("value"), vars)
            try:
                if actual - baseline != want_delta:
                    ok_all = False
                    errs.append(f"assert delta failed: ({actual}) - ({baseline}) != {want_delta} at {path}")
            except Exception:
                ok_all = False
                errs.append(f"assert delta failed (non-numeric?) at {path} actual={actual!r} baseline={baseline!r}")
            continue

        expected = _resolve_value(a.get("value"), vars)
        try:
            if op == "==":  cond = (actual == expected)
            elif op == "!=": cond = (actual != expected)
            elif op == ">":  cond = (actual >  expected)
            elif op == "<":  cond = (actual <  expected)
            elif op == ">=": cond = (actual >= expected)
            elif op == "<=": cond = (actual <= expected)
            else:
                cond = False
                errs.append(f"unknown op '{op}'")
        except Exception:
            cond = False
        if not cond:
            ok_all = False
            errs.append(f"assert {op} failed at {path}: actual={actual!r} expected={expected!r}")
    return ok_all, errs

# in run_test(...):
def run_test(name: str, sock: socket.socket, test: Dict[str, Any], strict: bool = False, vars: Optional[Dict[str,Any]] = None) -> None:
    if vars is None: vars = {}

    command = coerce_command_from_test(name, test)
    expect = test.get("expect", {"status": "ok"})

    if isinstance(command.get("data"), dict) and "idempotency_key" in test:
        command["data"].setdefault("idempotency_key", test["idempotency_key"])

    command = _expand_vars_in_obj(command, vars)        
        
    send_json_line(sock, command)
    line = recv_line(sock)
    if not line.strip():
        print(f"[{name}] FAIL: empty response")
        return

    try:
        resp = json.loads(line)
    except Exception as e:
        print(f"[{name}] FAIL: could not parse JSON response: {e}\n  raw: {line!r}")
        return

    # soft expect first
    ok = soft_match(resp, expect, strict=strict)
    status = resp.get("status"); rtype = resp.get("type")

    # handle save
    save_spec = test.get("save")
    if isinstance(save_spec, dict):
        var = save_spec.get("var"); path = save_spec.get("path")
        if var and path:
            vars[var] = _get_by_path(resp, path)

    # handle asserts
    asserts = test.get("asserts")
    if isinstance(asserts, list) and asserts:
        ok2, errs = _run_asserts(resp, asserts, vars)
        if not ok2:
            ok = False
            print(f"[{name}] ASSERT FAIL  (status={status}, type={rtype})")
            for e in errs:
                print("  -", e)
    if ok:
        print(f"[{name}] PASS  (status={status}, type={rtype})")
    else:
        printed = False
        if not isinstance(test.get("asserts"), list) or not test["asserts"]:
            print(f"[{name}] FAIL  (status={status}, type={rtype})")
            print("  expect:", json.dumps(expect, ensure_ascii=False))
            print("  actual:", json.dumps(resp, ensure_ascii=False))
            printed = True
        if DUMP_ON_FAIL and not printed:
            print(f"  full response: {json.dumps(resp, ensure_ascii=False)}")

# ----------------------------
# Suite execution
# ----------------------------

def run_test_suite():
    args = parse_args()
    global DUMP_ON_FAIL
    DUMP_ON_FAIL = getattr(args, "dump_on_fail", False)
    
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

            vars_store = {}
            for test in tests:
                if not isinstance(test, dict):
                    raise TypeError(f"Test must be an object, got: {type(test)} -> {test!r}")

                name = test.get("name", "<unnamed>")
                run_test(name, s, test, strict=args.strict, vars=vars_store)
                # run_test(name, s, test, strict=args.strict)

    except ConnectionRefusedError:
        print(f"\nERROR: Could not connect to {HOST}:{PORT}. Ensure the server is running.")
    except Exception as e:
        print(f"\nUnexpected error: {e}")

# ----------------------------
# Main
# ----------------------------

if __name__ == "__main__":
    run_test_suite()
