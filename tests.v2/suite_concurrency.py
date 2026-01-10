#!/usr/bin/env python3
import threading
import time
import os
import random
import traceback
from typing import List, Dict, Any
from twclient import TWClient

# Config
NUM_CLIENTS = int(os.getenv("NUM_CLIENTS", 5))
ITERATIONS = int(os.getenv("ITERATIONS", 10))
DURATION = int(os.getenv("DURATION", 30))
HOST = os.getenv("HOST", "localhost")
PORT = int(os.getenv("PORT", 1234))

results = []
lock = threading.Lock()

def client_worker(client_id: int):
    client = TWClient(HOST, PORT)
    user = f"conc_user_{client_id}"
    pwd = "password"
    
    log = []
    
    try:
        client.connect()
        # Register/Login
        client.register(user, pwd, fail_if_exists=False)
        if not client.login(user, pwd):
            log.append({"status": "FAIL", "msg": "Login failed"})
            return

        # Setup (ensure ship)
        # We can't use raw_sql here easily unless we have a sysop session.
        # For concurrency tests, we might want to assume the user is just "playing".
        # Or we can use a shared sysop session to set them up before threads start.
        
        start_time = time.time()
        i = 0
        while time.time() - start_time < DURATION and i < ITERATIONS:
            # Simple Trade Scenario
            # 1. My Info
            client.send_json({"command": "player.my_info"})
            resp = client.recv_next_non_notice()

            if not isinstance(resp, dict) or resp.get("status") != "ok":
                log.append({"status": "FAIL", "msg": f"player.my_info bad response: {resp!r}"})
                return            
            # 2. Sector Info
            # sector = resp.get("data", {}).get("ship", {}).get("location", {}).get("sector_id", 1)
            data = resp.get("data") or {}
            sector = (((data.get("ship") or {}).get("location") or {}).get("sector_id")) or 1

            client.send_json({"command": "move.describe_sector", "data": {"sector_id": sector}})
            client.recv_next_non_notice()
            
            # 3. Random sleep
            time.sleep(random.uniform(0.1, 0.5))
            
            i += 1
            
        log.append({"status": "PASS", "iterations": i})

    except Exception as e:
        log.append({"status": "ERROR", "msg": str(e)})
        traceback.print_exc()
    finally:
        client.close()
        with lock:
            results.append({"id": client_id, "log": log})

def run_concurrency_suite():
    print(f"--- Running Concurrency Suite ({NUM_CLIENTS} clients) ---")
    threads = []
    for i in range(NUM_CLIENTS):
        t = threading.Thread(target=client_worker, args=(i,))
        threads.append(t)
        t.start()
        
    for t in threads:
        t.join()
        
    # Check results
    failures = 0
    for r in results:
        for entry in r["log"]:
            if entry["status"] != "PASS":
                failures += 1
                print(f"Client {r['id']} Failed: {entry['msg']}")
                
    if failures == 0:
        print("CONCURRENCY SUITE PASSED")
        sys.exit(0)
    else:
        print(f"CONCURRENCY SUITE FAILED ({failures} errors)")
        sys.exit(1)

import sys
if __name__ == "__main__":
    run_concurrency_suite()
