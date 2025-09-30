#!/usr/bin/env python3
import socket
import json
import uuid
import os
import time
import select
from collections import defaultdict, deque

# -----------------------------
# Defaults; overridden by CLI
# -----------------------------
HOST = "localhost"
PORT = 1234
TIMEOUT = 5.0
TEST_DATA_FILE = "test_data.json"

# -----------------------------
# Socket / I/O helpers
# -----------------------------
def recv_line(sock, limit=65536):
    """Read a single newline-terminated line from the socket."""
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
    """Send a JSON command (adds id/ts) and receive one JSON line back."""
    # Attach a unique id; keep ts a simple ISO-ish string
    message_dict = dict(message_dict)  # shallow copy
    message_dict.setdefault("id", str(uuid.uuid4()))
    message_dict.setdefault("ts", time.strftime("%Y-%m-%dT%H:%M:%S") + "Z")

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

def recv_json_with_timeout(sock, timeout=5.0):
    """Wait up to timeout seconds for a single JSON line; return dict or None."""
    r, _, _ = select.select([sock], [], [], timeout)
    if not r:
        return None
    line = recv_line(sock).strip()
    if not line:
        return None
    try:
        return json.loads(line)
    except Exception:
        return None

def push_stream_drain_until(sock, predicate, timeout=5.0, max_msgs=100):
    """
    Read messages until predicate(msg) returns True or timeout elapses.
    Returns (matched_msg, seen_messages_list).
    """
    end = time.time() + timeout
    seen = []
    while time.time() < end and len(seen) < max_msgs:
        msg = recv_json_with_timeout(sock, timeout=max(0.0, end - time.time()))
        if msg is None:
            continue
        seen.append(msg)
        if predicate(msg):
            return msg, seen
    return None, seen

def pretty(obj):
    try:
        return json.dumps(obj, ensure_ascii=False)
    except Exception:
        return repr(obj)

def dump_seen(label, seen):
    print(f"--- {label}: {len(seen)} message(s) ---")
    for i, msg in enumerate(seen, 1):
        print(f"[{i}] {pretty(msg)}")
    print("--- end ---")

# -----------------------------
# Auth / small command helpers
# -----------------------------
def register_user(sock, username, password="secret"):
    return send_and_receive(sock, {
        "command": "auth.register",
        "data": {"user_name": username, "password": password}
    })

def login_user(sock, username, password="secret"):
    return send_and_receive(sock, {
        "command": "auth.login",
        "data": {"user_name": username, "password": password}
    })

def ensure_session(sock, username, password="secret"):
    """
    Try register; if username exists, try login. Returns server reply.
    Use when you need the socket authenticated now.
    """
    r = register_user(sock, username, password)
    if r.get("status") == "ok" and r.get("type") == "auth.session":
        return r

    err = (r.get("error") or {}).get("message", "")
    if "exists" in err.lower() or "taken" in err.lower() or r.get("error", {}).get("code") == 1210:
        l = login_user(sock, username, password)
        return l

    return r

def subscribe(sock, topic):
    return send_and_receive(sock, {"command": "subscribe.add", "data": {"topic": topic}})

def try_warp(sock, to_sector_id):
    # Server expects key "to_sector_id"
    return send_and_receive(sock, {"command": "move.warp", "data": {"to_sector_id": to_sector_id}})

def sector_info(sock):
    return send_and_receive(sock, {"command": "sector.info", "data": {"sector_id": None}})

# -----------------------------
# JSON test runner (table-driven)
# -----------------------------
def run_single_test(name, sock, message, expect):
    print(f"TRIED: {name}")
    resp = send_and_receive(sock, message)
    print(f"RETURNED: {resp}")

    ok = True
    # Simple matcher: top-level keys in 'expect'
    for k, v in expect.items():
        if k == "code":  # some tests expect nested error.code
            if resp.get("error", {}).get(k) != v:
                ok = False
                break
        else:
            if resp.get(k) != v:
                ok = False
                break

    # Transport errors are failures unless explicitly expected
    if resp.get("status") in ["TIMEOUT", "ERROR"] and not (expect and expect.get("status") in ["TIMEOUT", "ERROR"]):
        ok = False

    print(f"TEST: {'PASS' if ok else 'FAIL'}")
    print("-" * 20)
    return ok

def run_test_suite_from_json():
    if not os.path.exists(TEST_DATA_FILE):
        print(f"ERROR: Test data file '{TEST_DATA_FILE}' not found.")
        return

    try:
        with open(TEST_DATA_FILE, 'r', encoding='utf-8') as f:
            tests = json.load(f)
    except json.JSONDecodeError:
        print(f"ERROR: Invalid JSON in file '{TEST_DATA_FILE}'.")
        return

    print("--- Running TWClone Protocol Tests (JSON) ---")
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.settimeout(TIMEOUT)
            s.connect((HOST, PORT))
            for test in tests:
                run_single_test(test["name"], s, test["command"], test["expect"])
    except ConnectionRefusedError:
        print(f"\nERROR: Could not connect to {HOST}:{PORT}. Ensure the server is running.")
    except Exception as e:
        print(f"\nUnexpected error: {e}")

# -----------------------------
# Warp broadcast test (two sockets)
# -----------------------------
EDGES = [
    (1,2),(1,3),(1,4),(1,5),(1,6),(1,7),
    (2,3),(2,7),(2,8),(2,9),(2,10),
    (3,1),(3,4),
    (4,3),(4,5),
    (5,1),(5,4),(5,6),
    (6,1),(6,5),(6,7),
    (7,1),(7,6),(7,8),
    (8,7),
]

def build_adj(edges):
    adj = defaultdict(list)
    for a, b in edges:
        adj[a].append(b)
    return adj

def bfs_path(adj, start, goal):
    if start == goal:
        return [start]
    q = deque([start])
    prev = {start: None}
    while q:
        u = q.popleft()
        for v in adj.get(u, []):
            if v not in prev:
                prev[v] = u
                if v == goal:
                    path = [v]
                    while u is not None:
                        path.append(u)
                        u = prev[u]
                    path.reverse()
                    return path
                q.append(v)
    return None  # unreachable

def two_client_warp_broadcast_test(FROM=1, TO=2):
    print(f"\n--- Running Warp Broadcast Test (2 clients) FROM={FROM} TO={TO} ---")

    adj = build_adj(EDGES)

    def get_current_sector_id(sock):
        resp = sector_info(sock)
        if resp.get("status") == "ok":
            d = resp.get("data") or {}
            # Try common keys
            return d.get("sector_id") or d.get("current_sector") or d.get("id")
        return None

    # Open two sockets
    s_actor = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s_observer = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s_actor.settimeout(TIMEOUT)
    s_observer.settimeout(TIMEOUT)

    try:
        s_actor.connect((HOST, PORT))
        s_observer.connect((HOST, PORT))

        # Auth both sockets (new unique users so we don't collide)
        uname_actor = f"Actor_{uuid.uuid4().hex[:8]}"
        uname_obs   = f"Observer_{uuid.uuid4().hex[:8]}"

        r1 = ensure_session(s_actor, uname_actor)
        r2 = ensure_session(s_observer, uname_obs)
        print("Actor session:", pretty(r1))
        print("Observer session:", pretty(r2))

        # Observer subscribes to sector.* (to see both left and entered)
        sub_resp = subscribe(s_observer, "sector.*")
        print("Observer subscribe sector.*:", pretty(sub_resp))

        # Try to discover the actor's current sector (optional)
        cur = get_current_sector_id(s_actor)
        if cur is None:
            print("NOTE: sector.info not available or no sector returned; "
                  "will proceed anyway and rely on move.result for from/to.")

            # If we don't know, assume we're already at FROM for the pathing step
            cur = FROM

        # If not at FROM, walk a BFS path to FROM
        if cur != FROM:
            path = bfs_path(adj, cur, FROM)
            if path is None:
                print(f"Warp test: cannot reach sector {FROM} from {cur}; FAIL.")
                return
            print(f"Driving actor via path {path} -> reach FROM={FROM}")
            for i in range(1, len(path)):
                step_to = path[i]
                resp = try_warp(s_actor, step_to)
                print(f"  hop {path[i-1]}->{step_to}: {pretty(resp)}")
                if not (resp.get("status") == "ok" and resp.get("type") == "move.result"):
                    print("Path warp failed; aborting.")
                    return

        # Final hop FROM -> TO (this should emit left(FROM) then entered(TO))
        resp = try_warp(s_actor, TO)
        print("Final warp move.warp:", pretty(resp))
        if not (resp.get("status") == "ok" and resp.get("type") == "move.result"):
            print("Warp broadcast TEST: FAIL (actor warp failed)")
            return

        data = resp.get("data", {}) or {}
        pid = data.get("player_id")
        from_id = data.get("from_sector_id")
        to_id = data.get("to_sector_id")

        if (from_id, to_id) != (FROM, TO):
            print(f"WARNING: server reported from/to {from_id}->{to_id}, expected {FROM}->{TO}")

        # Predicates for observer’s unsolicited pushes
        def is_left(msg):
            return (
                msg.get("status") == "ok" and
                msg.get("type") == "player.left_sector_v1" and
                msg.get("meta", {}).get("topic") == f"sector.{from_id}" and
                msg.get("data", {}).get("player_id") == pid and
                msg.get("data", {}).get("from_sector_id") == from_id and
                msg.get("data", {}).get("to_sector_id") == to_id
            )

        def is_entered(msg):
            return (
                msg.get("status") == "ok" and
                msg.get("type") == "player.entered_sector_v1" and
                msg.get("meta", {}).get("topic") == f"sector.{to_id}" and
                msg.get("data", {}).get("player_id") == pid and
                msg.get("data", {}).get("from_sector_id") == from_id and
                msg.get("data", {}).get("to_sector_id") == to_id
            )

        # Wait for LEFT
        left_msg, seen1 = push_stream_drain_until(s_observer, is_left, timeout=TIMEOUT)
        if not left_msg:
            print("Warp broadcast TEST: FAIL (did not see LEFT event)")
            dump_seen("Observer stream (LEFT wait)", seen1)
            more, seen_more = push_stream_drain_until(s_observer, lambda m: False, timeout=1.5)
            dump_seen("Observer extra stream after LEFT timeout", seen_more)
            return
        else:
            print("Saw LEFT:", pretty(left_msg))

        # Wait for ENTERED
        entered_msg, seen2 = push_stream_drain_until(s_observer, is_entered, timeout=TIMEOUT)
        if not entered_msg:
            print("Warp broadcast TEST: FAIL (did not see ENTERED event)")
            dump_seen("Observer stream (ENTERED wait)", seen2)
            more, seen_more = push_stream_drain_until(s_observer, lambda m: False, timeout=1.5)
            dump_seen("Observer extra stream after ENTERED timeout", seen_more)
            return
        else:
            print("Saw ENTERED:", pretty(entered_msg))

        print("Warp broadcast TEST: PASS (left(from) then entered(to) with correct topics and payload)")

    except ConnectionRefusedError:
        print(f"\nERROR: Could not connect to {HOST}:{PORT}. Ensure the server is running on {HOST}:{PORT}.")
    except socket.timeout:
        print("\nUnexpected error in warp test: socket timed out")
    except Exception as e:
        print(f"\nUnexpected error in warp test: {e}")
    finally:
        try: s_actor.close()
        except: pass
        try: s_observer.close()
        except: pass

# -----------------------------
# CLI + main
# -----------------------------
if __name__ == "__main__":
    import argparse

    ap = argparse.ArgumentParser(description="TWClone test runner")
    ap.add_argument("--host", default=os.environ.get("TW_HOST", HOST),
                    help="Server host (default: localhost or $TW_HOST)")
    ap.add_argument("--port", type=int, default=int(os.environ.get("TW_PORT", PORT)),
                    help="Server port (default: 1234 or $TW_PORT)")
    ap.add_argument("--timeout", type=float, default=float(os.environ.get("TW_TIMEOUT", TIMEOUT)),
                    help="Socket read timeout seconds (default: 5)")
    ap.add_argument("--from-to", default=os.environ.get("TW_FROM_TO", "1-2"),
                    help="Test hop as 'FROM-TO' (default: 1-2)")
    ap.add_argument("--skip-json-suite", action="store_true",
                    help="Skip the table-driven tests (test_data.json)")

    args = ap.parse_args()

    # Apply CLI to globals
    HOST = args.host
    PORT = args.port
    TIMEOUT = args.timeout

    # 1) Run the existing JSON-driven tests unless skipped
    if not args.skip_json_suite:
        run_test_suite_from_json()

    # 2) Run the two-socket broadcast test
    ft = args.from_to.split("-", 1)
    try:
        ft_from, ft_to = int(ft[0]), int(ft[1])
    except Exception:
        print(f"Invalid --from-to '{args.from_to}', expected like 1-2; using 1-2")
        ft_from, ft_to = 1, 2

    two_client_warp_broadcast_test(FROM=ft_from, TO=ft_to)

