import random
import time
import sys
import os
import uuid
from twclient import TWClient

# Configuration
HOST = os.getenv("HOST", "127.0.0.1")
PORT = int(os.getenv("PORT", 1234))
TEST_ITERATIONS = int(os.getenv("TEST_ITERATIONS", 20)) # Reduced for regression speed
MAX_SECTOR = 500

def run_test():
    client = TWClient(host=HOST, port=PORT)
    username = "mover_user"
    password = "password"

    try:
        client.connect()
        print(f"Authenticating as {username}...")
        if not client.login(username, password):
            print("Authentication failed.")
            sys.exit(1)
        
        client.send_json({"command": "player.my_info"})
        resp = client.recv_next_non_notice()
        if resp.get("status") != "ok":
            print(f"Failed to get player info: {resp}")
            sys.exit(1)
        
        current_sector = resp["data"]["player"]["sector"]
        print(f"Starting at sector {current_sector}")

        success_count = 0
        fail_count = 0

        for i in range(TEST_ITERATIONS):
            dest = random.randint(1, MAX_SECTOR)
            if dest == current_sector: continue

            print(f"[{i+1}/{TEST_ITERATIONS}] Pathfinding {current_sector} -> {dest}")
            client.send_json({"command": "move.pathfind", "data": {"to": dest}})
            resp = client.recv_next_non_notice()
            
            if resp.get("status") != "ok":
                print(f"  Pathfind failed: {resp.get('error')}")
                fail_count += 1
                continue
                
            path = resp["data"]["steps"]
            if not path:
                fail_count += 1
                continue
                
            for next_sector in path[1:]:
                client.send_json({"command": "move.warp", "data": {"to_sector_id": next_sector}})
                w_resp = client.recv_next_non_notice()
                if w_resp.get("status") == "ok":
                    current_sector = next_sector
                else:
                    fail_count += 1
                    break
            
            if current_sector == path[-1]:
                success_count += 1

    except Exception as e:
        print(f"An unexpected error occurred: {e}")
        sys.exit(1)
    finally:
        client.close()

    print(f"Test Complete. Success paths: {success_count}, Failures: {fail_count}")
    if fail_count > 0:
        sys.exit(1)

if __name__ == "__main__":
    run_test()
