import random
import time
import sys
import os
import uuid
from twclient import TWClient # Import the new TWClient

# Configuration (can be overridden by environment variables)
HOST = os.getenv("HOST", "127.0.0.1")
PORT = int(os.getenv("PORT", 1234))
TEST_ITERATIONS = int(os.getenv("TEST_ITERATIONS", 100))
MAX_SECTOR = 500 # Hardcoded to a reasonable value

def run_test():
    client = TWClient(host=HOST, port=PORT)
    username = f"mover_{str(uuid.uuid4())[:8]}"
    password = "password"

    try:
        client.connect()
        
        print(f"Authenticating as {username}...")
        client.register(username, password)
        if not client.login(username, password):
            print("Authentication failed.")
            sys.exit(1)
        
        # Create ship using macro_user_in_sector equivalent SQL
        # This mirrors the macro functionality to ensure a ship is assigned and player sector is set
        sql_create_ship = f"""
            INSERT OR IGNORE INTO shiptypes (id, name, basecost, maxholds) VALUES (1, 'Scout', 1000, 100);
            INSERT OR IGNORE INTO ships (id, type_id, name, holds, sector, ported) VALUES ({client.player_id}, 1, 'TestShip', 100, 1, 0);
            UPDATE players SET ship = {client.player_id}, sector = 1 WHERE id = {client.player_id};
            INSERT OR IGNORE INTO ship_ownership (ship_id, player_id, is_primary) VALUES ({client.player_id}, {client.player_id}, 1);
        """
        client.send_json({"command": "sys.raw_sql_exec", "data": {"sql": sql_create_ship}})
        resp = client.recv_next_non_notice()
        if resp.get("status") != "ok":
            print(f"ERROR: Failed to give ship to {username}: {resp}")
            sys.exit(1)
        
        # Re-login to refresh ship state in session
        print(f"Re-logging in {username} to refresh player state...")
        if not client.login(username, password):
            print(f"ERROR: Re-login for {username} failed unexpectedly.")
            sys.exit(1)
        # client.login(username, password) # This line is redundant

        # Get current state
        client.send_json({"command": "player.my_info"})
        resp = client.recv_next_non_notice()
        if resp.get("status") != "ok":
            print(f"Failed to get player info after ship creation: {resp}")
            sys.exit(1)
        
        # Corrected: Access sector directly from player object
        current_sector = resp["data"]["player"]["sector"]
        print(f"Starting at sector {current_sector}")

        success_count = 0
        fail_count = 0

        for i in range(TEST_ITERATIONS):
            if MAX_SECTOR == 0: # Avoid division by zero if no sectors exist
                print("No sectors available, skipping movement tests.")
                break

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
                print("  No path found (or empty).")
                fail_count += 1
                continue
                
            print(f"  Path found: {path} (Cost: {resp['data']['total_cost']})")
            
            expected_start = path[0]
            if expected_start != current_sector:
                 print(f"  WARNING: Path starts at {expected_start} but I am at {current_sector}. Attempting to resync.")
                 current_sector = expected_start 

            for next_sector in path[1:]:
                print(f"    Warping {current_sector} -> {next_sector}...", end="")
                client.send_json({"command": "move.warp", "data": {"to_sector_id": next_sector}})
                
                w_resp = client.recv_next_non_notice()
                
                if w_resp.get("status") == "ok":
                    print(" OK")
                    current_sector = next_sector
                else:
                    print(f" FAIL! {w_resp}")
                    fail_count += 1
                    # Abort this path, try next iteration
                    break
            
            if current_sector == path[-1] and w_resp.get("status") == "ok": # Ensure last warp was successful
                success_count += 1

    except Exception as e:
        print(f"An unexpected error occurred: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1) # Indicate failure due to unexpected error
    finally:
        client.close()

    print(f"Test Complete. Success paths: {success_count}, Failures: {fail_count}")
    if fail_count > 0:
        sys.exit(1)

if __name__ == "__main__":
    run_test()