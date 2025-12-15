import socket
import json
import time
import sys
import os
import uuid

HOST = "127.0.0.1"
PORT = 1234
if os.getenv("TW_PORT"):
    PORT = int(os.getenv("TW_PORT"))

def create_client(name):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        s.connect((HOST, PORT))
        return s
    except Exception as e:
        print(f"[{name}] Connect failed: {e}")
        return None

def send_json(sock, cmd):
    line = json.dumps(cmd) + "\n"
    sock.sendall(line.encode("utf-8"))

def recv_next(sock):
    buf = b""
    while True:
        chunk = sock.recv(1)
        if not chunk:
            if not buf: return None
            break
        if chunk == b"\n":
            break
        buf += chunk
    return json.loads(buf.decode("utf-8"))

def get_response(sock, expected_type=None):
    """Reads next message, ignoring system.notice, until expected_type or any non-notice if None."""
    while True:
        r = recv_next(sock)
        if not r: return None
        if r.get("type") == "system.notice": continue
        if expected_type and r.get("type") != expected_type:
            # If we get an error instead of expected type, return it to caller
            if r.get("status") == "error": return r
            continue 
        return r

def login(sock, user, pw):
    send_json(sock, {"command": "auth.login", "data": {"username": user, "passwd": pw}})
    r = get_response(sock, "auth.session")
    if r and r.get("status") == "ok": return True
    return False

def register(sock, user, pw):
    send_json(sock, {"command": "auth.register", "data": {"username": user, "passwd": pw}})
    r = get_response(sock, "auth.session")
    if r and r.get("status") == "ok": return True
    if r and r.get("status") == "error" and r.get("error", {}).get("code") == 1210:
        return login(sock, user, pw)
    return False

def get_current_sector(sock):
    send_json(sock, {"command": "player.my_info"})
    # my_info might return type "player.info_v1" or generic success
    # Wait for response
    while True:
        r = recv_next(sock)
        if not r: return None
        if r.get("type") == "system.notice": continue
        if r.get("status") == "ok" and "player" in r.get("data", {}):
            return r["data"]["player"]["sector"]
        if r.get("status") == "error":
            print(f"Error getting info: {r}")
            return None

def pathfind_and_warp(sock, name, target_sector):
    current = get_current_sector(sock)
    if current == target_sector:
        print(f"[{name}] Already at {target_sector}")
        return True

    print(f"[{name}] Pathfinding {current} -> {target_sector}")
    send_json(sock, {"command": "move.pathfind", "data": {"to": target_sector}})
    r = recv_next(sock)
    while r and r.get("type") == "system.notice": r = recv_next(sock)
    
    if not r or r.get("status") != "ok":
        print(f"[{name}] Pathfind failed: {r}")
        return False

    steps = r["data"]["steps"]
    # steps is [current, next, next...]
    if not steps:
        print(f"[{name}] No path found")
        return False

    # Traverse
    for next_sec in steps[1:]:
        print(f"[{name}] Warping {current} -> {next_sec}...", end="")
        send_json(sock, {"command": "move.warp", "data": {"to_sector_id": next_sec}})
        
        # Wait for move result
        while True:
            wr = recv_next(sock)
            if not wr: 
                print(" Disconnected")
                return False
            if wr.get("type") == "system.notice": continue
            if wr.get("type", "").startswith("sector."): continue # Ignore events while warping
            
            if wr.get("status") == "ok":
                print(" OK")
                current = next_sec
                break
            else:
                print(f" FAIL: {wr}")
                return False
                
    return current == target_sector


def test_subscriptions():
    # 1. Observer Setup
    obs = create_client("Observer")
    if not obs: return False
    obs_user = f"obs_{str(uuid.uuid4())[:8]}"
    if not register(obs, obs_user, "pass"): 
        print("Observer auth failed")
        return False
    
    # Warp observer to Sector 1
    if not pathfind_and_warp(obs, "Observer", 1): return False
    
    # Subscribe to Sector 1
    print("[Observer] Subscribing to sector.1")
    send_json(obs, {"command": "subscribe.add", "data": {"topic": "sector.1"}})
    # Wait for response
    while True:
        r = recv_next(obs)
        if r.get("type") == "system.notice": continue
        if r.get("status") == "ok": break
        print(f"[Observer] Subscribe failed: {r}")
        return False

    # 2. Actor Setup
    act = create_client("Actor")
    if not act: return False
    act_user = f"act_{str(uuid.uuid4())[:8]}"
    if not register(act, act_user, "pass"):
        print("Actor auth failed")
        return False

    # Warp Actor to Sector 2
    if not pathfind_and_warp(act, "Actor", 2): return False

    # 3. Action: Actor enters Sector 1
    # Need direct warp 2->1. If they are adjacent, pathfind will return [2, 1].
    print("[Actor] Warping to Sector 1...")
    # We use pathfind_and_warp just to be safe, it handles single step too
    if not pathfind_and_warp(act, "Actor", 1): return False
    
    # 4. Verify: Observer sees event
    print("[Observer] Waiting for enter event...")
    found_enter = False
    start = time.time()
    while time.time() - start < 5:
        obs.settimeout(1.0)
        try:
            evt = recv_next(obs)
            if not evt: break
            # Ignore system notices
            if evt.get("type") == "system.notice": continue
            
            if evt.get("type") == "sector.player_entered":
                data = evt.get("data", {})
                # We don't strictly enforce 'from_sector_id' checking because pathfind might have taken weird route
                # but we expect it to be 1
                if data.get("sector_id") == 1:
                    print("SUCCESS: Observer saw Actor enter Sector 1")
                    found_enter = True
                    break
        except socket.timeout:
            continue
    
    obs.settimeout(None)
    if not found_enter:
        print("FAILURE: Observer did not receive sector.player_entered event")
        return False

    # 5. Action: Actor leaves Sector 1 (to Sector 2)
    print("[Actor] Warping back to Sector 2...")
    if not pathfind_and_warp(act, "Actor", 2): return False

    # 6. Verify: Observer sees exit
    print("[Observer] Waiting for leave event...")
    found_leave = False
    start = time.time()
    while time.time() - start < 5:
        obs.settimeout(1.0)
        try:
            evt = recv_next(obs)
            if not evt: break
            if evt.get("type") == "system.notice": continue
            
            if evt.get("type") == "sector.player_left":
                data = evt.get("data", {})
                if data.get("sector_id") == 1:
                    print("SUCCESS: Observer saw Actor leave Sector 1")
                    found_leave = True
                    break
        except socket.timeout:
            continue

    if not found_leave:
        print("FAILURE: Observer did not receive sector.player_left event")
        return False

    print("E2E Subscriptions Test Passed.")
    obs.close()
    act.close()
    return True

if __name__ == "__main__":
    if not test_subscriptions():
        sys.exit(1)
