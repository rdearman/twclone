#!/usr/bin/env python3
import socket
import json
import uuid
import os
import argparse
from typing import Any, Dict, List, Optional
import traceback
# --- add near top ---
from typing import Tuple
import re


# ----------------------------
# Defaults (override via CLI/env)
# ----------------------------
HOST = "localhost"
PORT = 1234
TIMEOUT = 25
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
    print(f"DEBUG: Loading JSON file: {path}") # ADD THIS LINE
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
        if "suites" in doc and isinstance(doc.get("suites"), dict):
            suites = doc["suites"]
            if suite:
                if suite not in suites:
                    available = ", ".join(sorted(suites.keys()))
                    raise ValueError(f"Suite '{suite}' not found. Available: {available}")
                if not isinstance(suites[suite], list):
                    raise ValueError(f"Suite '{suite}' is not a list.")
                return suites[suite]
            else:
                # If no suite is specified, run all suites
                all_tests: List[Dict[str, Any]] = []
                for suite_name, tests in suites.items():
                    if isinstance(tests, list):
                        all_tests.extend(tests)
                return all_tests

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
            # Handle dot notation for nested keys in expect
            actual_value = _get_by_path(actual, k) if "." in k else actual.get(k)
            if actual_value is None and not (k in actual and actual.get(k) is None): # Check for key existence explicitly if value is None
                return False # Key not found in actual, or actual.get(k) is genuinely None while expect has non-None value
            if not soft_match(actual_value, v, strict=strict):
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
    # Check if the test dictionary itself IS the command envelope
    # (i.e., 'command' is a top-level string, and 'data'/'auth' are also top-level)
    if "command" in test and isinstance(test["command"], str):
        cmd_envelope = {"command": test["command"]}
        if "data" in test:
            cmd_envelope["data"] = test["data"]
        if "auth" in test:
            cmd_envelope["auth"] = test["auth"]
        return cmd_envelope

    # Legacy: Check for "type" or "cmd" as top-level keys
    if "type" in test and isinstance(test["type"], str):
        cmd_envelope = {"command": test["type"]}
        if "data" in test:
            cmd_envelope["data"] = test["data"]
        if "auth" in test:
            cmd_envelope["auth"] = test["auth"]
        return cmd_envelope
        
    if "cmd" in test and isinstance(test["cmd"], str):
        cmd_envelope = {"command": test["cmd"]}
        if "data" in test:
            cmd_envelope["data"] = test["data"]
        if "auth" in test:
            cmd_envelope["auth"] = test["auth"]
        return cmd_envelope
        
    # If none of the above, it's a malformed test without a recognized command key.
    # Log a debug message and return an empty dict, which will cause a controlled
    # failure in the calling run_test function for this specific malformed test.
    print(f"DEBUG: Malformed test '{name}': Missing 'command', 'type', or 'cmd' key. Test will be marked as failed.")
    return {}



# ----------------------------
# Auth bootstrap (optional)
# ----------------------------

def try_register(sock: socket.socket, user: str, passwd: str, register_if_missing: bool = False) -> None:
    send_json_line(sock, {"command": "auth.register", "data": {"username": user, "passwd": passwd}})
    resp = json.loads(recv_line(sock) or "{}")
    # ok → proceed; error 1210 (exists) → proceed; any other error → print but continue to login
    if resp.get("status") == "ok":
        print(f"[auth.register] PASS  (user={user})")
        return
    err = (resp.get("error") or {}).get("code")
    if err == 1210 and register_if_missing:
        print(f"[auth.register] SKIP (already exists)")
        return
    print(f"[auth.register] NOTE: {resp}")

def do_login(sock: socket.socket, user: str, passwd: str) -> bool:
    send_json_line(sock, {"command": "auth.login", "data": {"username": user, "passwd": passwd}})
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


def _get_value_from_obj_by_part(obj: Any, part: str) -> Any:
    if isinstance(obj, dict):
        return obj.get(part)
    elif isinstance(obj, list):
        # Handle array filtering: *[key=value]
        match_filter = re.match(r"\*\[(\w+)=([^\]]+)\]", part)
        if match_filter:
            filter_key = match_filter.group(1)
            filter_value = match_filter.group(2)
            for item in obj:
                if isinstance(item, dict) and item.get(filter_key) == filter_value:
                    return item
            return None # No matching item found
        elif part.isdigit():
            idx = int(part)
            if 0 <= idx < len(obj):
                return obj[idx]
            else:
                return None
        else:
            return None
    return None

def _get_by_path(obj: Any, path: str) -> Any:
    if path == "$turns":
        return _find_turns_value(obj)
    # support fallback: "a.b|x.y|z"
    for candidate in path.split("|"):
        current_obj = obj
        found = True
        for part in candidate.strip().split("."):
            current_obj = _get_value_from_obj_by_part(current_obj, part)
            if current_obj is None: # Explicitly check for None, allowing 0 or "" to pass
                found = False
                break
        if found:
            return current_obj
    return None


def _expand_vars_in_obj(o: Any, vars: Dict[str, Any]) -> Any:
    if isinstance(o, str):
        val = o
        if "*uuid*" in val:
            import uuid
            val = val.replace("*uuid*", str(uuid.uuid4()))

        def replace_var(match):
            var_name = match.group(1)
            resolved_val = vars.get(var_name)
            print(f"DEBUG: Resolving embedded @{var_name} to {resolved_val!r} (type: {type(resolved_val)})")
            return str(resolved_val) if resolved_val is not None else match.group(0)

        val = re.sub(r"@(\w+)", replace_var, val)

        return val
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
        # New: Handle *array operator
        if op == "*array":
            if not isinstance(actual, list):
                ok_all = False
                errs.append(f"assert *array failed at {path}: actual={actual!r} is not a list")
            continue

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

        if op == "defined":
            if actual is None:
                ok_all = False
                errs.append(f"assert defined failed at {path}: actual is None")
            continue

        if op == "undefined":
            if actual is not None:
                ok_all = False
                errs.append(f"assert undefined failed at {path}: actual={actual!r}")
            continue

        expected = _resolve_value(a.get("value"), vars)
        try:
            if op == "==":  cond = (actual == expected)
            elif op == "!=": cond = (actual != expected)
            elif op == ">":  cond = (actual >  expected)
            elif op == "<":  cond = (actual <  expected)
            elif op == ">=": cond = (actual >= expected)
            elif op == "<=": cond = (actual <= expected)
            elif op == "glob":
                if not isinstance(actual, str):
                    cond = False
                else:
                    # Simple glob: 'prefix * suffix'
                    parts = expected.split('*')
                    if len(parts) == 2:
                        prefix, suffix = parts
                        cond = actual.startswith(prefix) and actual.endswith(suffix)
                    elif len(parts) == 1:
                        cond = actual == expected # No wildcard, exact match
                    else:
                        cond = False # More complex globs not supported
                if not cond:
                    errs.append(f"assert glob failed at {path}: actual={actual!r} expected={expected!r}")
            else:
                cond = False
                errs.append(f"unknown op '{op}'")
        except Exception:
            cond = False
        if not cond:
            ok_all = False
            errs.append(f"assert {op} failed at {path}: actual={actual!r} expected={expected!r}")
    return ok_all, errs


def _generate_idempotency_key() -> str:
    return str(uuid.uuid4())

def run_test(name: str, sock: socket.socket, test: Dict[str, Any], strict: bool = False, vars: Optional[Dict[str,Any]] = None, session_tokens: Optional[Dict[str,str]] = None) -> None:
    if vars is None: vars = {}
    if session_tokens is None: session_tokens = {} # Initialize if None
    print(f"DEBUG: Vars for '{name}' before expand: {vars}")

    command_envelope = coerce_command_from_test(name, test)
    expect = test.get("expect", {"status": "ok"})

    # --- Auth handling: inject session token if user specified ---
    test_user = test.get("user")
    if test_user and test_user in session_tokens:
        if "auth" not in command_envelope:
            command_envelope["auth"] = {}
        command_envelope["auth"]["session"] = session_tokens[test_user]
    # -----------------------------------------------------------

    # Handle dynamic idempotency_key generation
    if test.get("idempotency_key") == "*generate*":
        command_envelope["data"]["idempotency_key"] = _generate_idempotency_key()
    elif isinstance(command_envelope.get("data"), dict) and "idempotency_key" in test:
        command_envelope["data"].setdefault("idempotency_key", test["idempotency_key"])

    if vars is None: vars = {} # Ensure vars is initialized if None
    print(f"DEBUG: Vars for '{name}' before expand: {vars}")
    print(f"DEBUG: Command envelope for '{name}' before expansion: {command_envelope}")
    command_envelope = _expand_vars_in_obj(command_envelope, vars)
    print(f"DEBUG: Command envelope for '{name}' after expansion: {command_envelope}")
    print(f"DEBUG: Vars before save for '{name}': {vars}")
        
    send_json_line(sock, command_envelope)
    
    max_reads = 10
    resp = {}
    
    for _ in range(max_reads):
        line = recv_line(sock)
        if not line.strip():
            print(f"[{name}] FAIL: empty response")
            return

        try:
            current_resp = json.loads(line)
            rtype = current_resp.get("type")
            
            # Skip system notices which are asynchronous broadcasts
            if rtype == "system.notice":
                # print(f"DEBUG: Ignoring system.notice: {current_resp}")
                continue
                
            resp = current_resp
            break
            
        except Exception as e:
            print(f"[{name}] FAIL: could not parse JSON response: {e}\n  raw: {line!r}")
            return
    else:
        print(f"[{name}] FAIL: timed out waiting for non-notice response")
        return

    # soft expect first
    ok = soft_match(resp, expect, strict=strict)

    # Special handling for auth.register with register_if_missing
    if command_envelope.get("command") == "auth.register" and test.get("register_if_missing"):
        if resp.get("status") == "error":
            err = (resp.get("error") or {}).get("code")
            if err == 1210: # Username already exists
                ok = True # Treat as a success (skip)
                print(f"[{name}] SKIP (already exists)")
                return # Exit early, as we've handled the outcome

    status = resp.get("status")
    rtype = resp.get("type")
    if rtype == "auth.session" and test_user: # Only if it's an auth.session response and a user is specified
        session_token = resp.get("data", {}).get("session")
        player_id = resp.get("data", {}).get("player_id")
        if session_token:
            session_tokens[test_user] = session_token
        if player_id:
            vars[test_user + "_id"] = player_id
    # handle save (and capture)
    print(f"DEBUG: id(vars) before save: {id(vars)}")
    save_spec = test.get("save") or test.get("capture")
    if isinstance(save_spec, dict):
        # Support both {"var": "name", "path": "..."}
        var = save_spec.get("var"); path = save_spec.get("path")
        if var and path:
            vars[var] = _get_by_path(resp, path)
        # -----------------------------------
        # Support {"name": "path", ...}
        else:
            for var_name, var_path in save_spec.items():
                if isinstance(var_path, str):
                    vars[var_name] = _get_by_path(resp, var_path)
    print(f"DEBUG: id(vars) after save: {id(vars)}")
    print(f"DEBUG: Vars after save for '{name}': {vars}")

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

            session_tokens: Dict[str, str] = {}
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
                run_test(name, s, test, strict=args.strict, vars=vars_store, session_tokens=session_tokens)
                # run_test(name, s, test, strict=args.strict)

    except ConnectionRefusedError:
        print(f"\nERROR: Could not connect to {HOST}:{PORT}. Ensure the server is running.")
    except Exception as e:
        print(f"\nUnexpected error: {e}")
        print(traceback.format_exc()) # ADD THIS LINE

# ----------------------------
# Main
# ----------------------------

if __name__ == "__main__":
    run_test_suite()
