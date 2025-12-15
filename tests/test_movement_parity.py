import socket
import json
import random
import time
import sys
import os

HOST = "127.0.0.1"
PORT = 1234 # Default, should match server config
if os.getenv("TW_PORT"):
    PORT = int(os.getenv("TW_PORT"))

MAX_SECTOR = 2000 # Approximate, will adapt if errors occur
TEST_ITERATIONS = 100

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

def login_or_register(sock, username, password):
    # Try login
    send_json(sock, {"command": "auth.login", "data": {"username": username, "passwd": password}})
    resp = None
    for _ in range(10): # Max reads
        r = recv_next(sock)
        if not r: break # Socket closed
        if r.get("type") == "auth.session":
            resp = r
            break
        if r.get("type") == "system.notice": continue # Ignore
        # If error, we store it but break to try registration (unless it's a fatal connection error)
        if r.get("status") == "error": 
            resp = r
            break

    if resp and resp.get("status") == "ok":
        return True
    
    # Try register (if login failed)
    send_json(sock, {"command": "auth.register", "data": {"username": username, "passwd": password}})
    resp = None
    for _ in range(10):
        r = recv_next(sock)
        if not r: return False
        if r.get("type") == "auth.session":
            resp = r
            break
        if r.get("type") == "system.notice": continue # Ignore
        if r.get("status") == "error": 
            resp = r
            break

    if resp and resp.get("status") == "ok":
        return True
    elif resp and resp.get("error", {}).get("code") == 1210: # Already exists (race condition?)
        # Login again after registration fails with "already exists"
        send_json(sock, {"command": "auth.login", "data": {"username": username, "passwd": password}})
        resp = None
        for _ in range(10):
            r = recv_next(sock)
            if not r: return False
            if r.get("type") == "auth.session":
                resp = r
                break
            if r.get("type") == "system.notice": continue # Ignore
        return resp and resp.get("status") == "ok"
    
    return False

def run_test():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        s.connect((HOST, PORT))
    except ConnectionRefusedError:
        print(f"Could not connect to {HOST}:{PORT}. Is server running?")
        sys.exit(1)

    import uuid
    username = f"mover_{str(uuid.uuid4())[:8]}"
    password = "password"
    
    print(f"Authenticating as {username}...")
    if not login_or_register(s, username, password):
        print("Authentication failed.")
        sys.exit(1)
    
    # Get current state
    send_json(s, {"command": "player.my_info"})
    resp = recv_next(s)
    current_sector = resp["data"]["player"]["sector"]
    print(f"Starting at sector {current_sector}")

    success_count = 0
    fail_count = 0

    for i in range(TEST_ITERATIONS):
        dest = random.randint(1, MAX_SECTOR)
        if dest == current_sector: continue

        print(f"[{i+1}/{TEST_ITERATIONS}] Pathfinding {current_sector} -> {dest}")
        
        send_json(s, {"command": "move.pathfind", "data": {"to": dest}})
        resp = recv_next(s)
        
        if resp.get("status") != "ok":
            print(f"  Pathfind failed: {resp.get('error')}")
            continue
            
        path = resp["data"]["steps"]
        if not path:
            print("  No path found (or empty).")
            continue
            
        print(f"  Path found: {path} (Cost: {resp['data']['total_cost']})")
        
        # Traverse
        # path includes origin? usually steps is [from, step1, step2... to] or [step1, step2... to]
        # The implementation in server_universe.c: 
        # json_array_append_new (steps, json_integer (from)); ... reverse ... 
        # So it returns [from, ..., to]
        
        # We start at path[0] (current_sector). We warp to path[1], then path[2]...
        
        expected_start = path[0]
        if expected_start != current_sector:
             print(f"  WARNING: Path starts at {expected_start} but I am at {current_sector}")
             # Attempt to resync
             current_sector = expected_start 

        for next_sector in path[1:]:
            # Drain async notices (like sector.entered messages from previous warps)
            # Use a short timeout or non-blocking check? 
            # Simpler: just ignore anything that isn't the response to our command.
            
            print(f"    Warping {current_sector} -> {next_sector}...", end="")
            send_json(s, {"command": "move.warp", "data": {"to_sector_id": next_sector}})
            
            while True:
                w_resp = recv_next(s)
                if not w_resp: 
                    print(" Connection closed.")
                    sys.exit(1)
                if w_resp.get("type") == "system.notice": continue # Ignore notices
                # Also ignore broadcast events if we receive them
                if w_resp.get("type", "").startswith("sector."): continue 
                
                break
            
            if w_resp.get("status") == "ok":
                print(" OK")
                current_sector = next_sector
            else:
                print(f" FAIL! {w_resp}")
                fail_count += 1
                # Abort this path, try next iteration
                break
        
        if current_sector == path[-1]:
            success_count += 1

    print(f"Test Complete. Success paths: {success_count}, Failures: {fail_count}")
    s.close()
    if fail_count > 0:
        sys.exit(1)

if __name__ == "__main__":
    run_test()
