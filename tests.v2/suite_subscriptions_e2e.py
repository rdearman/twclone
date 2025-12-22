import random
import time
import sys
import os
import uuid
from twclient import TWClient # Import the new TWClient

# Configuration
HOST = os.getenv("HOST", "127.0.0.1")
PORT = int(os.getenv("PORT", 1234))

# Helper to give a ship to a player, needs admin client
def give_ship_to_player(admin_client: TWClient, player_id: int, sector: int, username: str):
    sql = f"""
        INSERT OR IGNORE INTO shiptypes (id, name, basecost, maxholds) VALUES (1, 'Scout', 1000, 100);
        DELETE FROM ships WHERE id = {player_id};
        INSERT INTO ships (id, type_id, name, holds, sector, ported) VALUES ({player_id}, 1, '{username}Ship', 100, {sector}, 0);
        UPDATE players SET ship = {player_id}, sector = {sector} WHERE id = {player_id};
        INSERT OR IGNORE INTO ship_ownership (ship_id, player_id, is_primary) VALUES ({player_id}, {player_id}, 1);
    """
    admin_client.send_json({"command": "sys.raw_sql_exec", "data": {"sql": sql}})
    resp = admin_client.recv_next_non_notice()
    if resp.get("status") != "ok":
        print(f"ERROR: Failed to give ship to player {player_id}: {resp}")
        sys.exit(1)

# Helper to pathfind and warp, adapted for TWClient
def pathfind_and_warp(client: TWClient, name: str, target_sector: int):
    # Refresh player info to get current sector
    client.send_json({"command": "player.my_info"})
    resp = client.recv_next_non_notice()
    if resp.get("status") != "ok":
        print(f"[{name}] Failed to get player info: {resp}")
        return False
    
    current_sector = resp["data"]["player"]["sector"] # Corrected line

    if current_sector == target_sector:
        print(f"[{name}] Already at sector {target_sector}")
        return True

    print(f"[{name}] Pathfinding {current_sector} -> {target_sector}")
    client.send_json({"command": "move.pathfind", "data": {"to": target_sector}})
    resp = client.recv_next_non_notice()
    
    if resp.get("status") != "ok":
        print(f"[{name}] Pathfind failed: {resp.get('error')}")
        return False
        
    path = resp["data"]["steps"]
    if not path:
        print(f"[{name}] No path found (or empty).")
        return False
        
    print(f"[{name}] Path found: {path} (Cost: {resp['data']['total_cost']})")
    
    # Traverse
    for next_sec in path[1:]: # path[0] is current_sector, start from next step
        print(f"[{name}] Warping {current_sector} -> {next_sec}...", end="")
        client.send_json({"command": "move.warp", "data": {"to_sector_id": next_sec}})
        
        w_resp = client.recv_next_non_notice()
        
        if w_resp.get("status") == "ok":
            print(" OK")
            current_sector = next_sec
        else:
            print(f" FAIL: {w_resp}")
            return False
            
    return current_sector == target_sector


def test_subscriptions():
    # Admin client for setup
    admin_client = TWClient(host=HOST, port=PORT)
    try:
        admin_client.connect()
        admin_client.register("admin", "password") # Ensure admin user exists
        admin_client.login("admin", "password") # Login admin
    except Exception as e:
        print(f"Admin client setup failed: {e}")
        return False

    # 1. Observer Setup
    obs_client = TWClient(host=HOST, port=PORT)
    obs_user = f"obs_{str(uuid.uuid4())[:8]}"
    try:
        obs_client.connect()
        obs_client.register(obs_user, "pass")
        if not obs_client.login(obs_user, "pass"): 
            print("Observer auth failed")
            return False
        
        # Give ship to Observer and force refresh
        give_ship_to_player(admin_client, obs_client.player_id, 1, obs_user)
        obs_client.login(obs_user, "pass") # Re-login to refresh ship state

        # Ensure sectors 1 and 2 exist
        admin_client.send_json({"command": "sys.raw_sql_exec", "data": {"sql": "INSERT OR IGNORE INTO sectors (id, name) VALUES (1, 'Sector 1'); INSERT OR IGNORE INTO sectors (id, name) VALUES (2, 'Sector 2');"}})
        admin_client.recv_next_non_notice()
        
        # Warp observer to Sector 1
        if not pathfind_and_warp(obs_client, "Observer", 1): return False
        
        # Subscribe to Sector 1
        print("[Observer] Subscribing to sector.1")
        obs_client.send_json({"command": "subscribe.add", "data": {"topic": "sector.1"}})
        r = obs_client.recv_next_non_notice()
        if r.get("status") != "ok": 
            print(f"[Observer] Subscribe failed: {r}")
            return False

        # 2. Actor Setup
        act_client = TWClient(host=HOST, port=PORT)
        act_user = f"act_{str(uuid.uuid4())[:8]}"
        act_client.connect()
        act_client.register(act_user, "pass")
        if not act_client.login(act_user, "pass"):
            print("Actor auth failed")
            return False
        
        # Give ship to Actor and force refresh
        give_ship_to_player(admin_client, act_client.player_id, 2, act_user)
        act_client.login(act_user, "pass") # Re-login to refresh ship state

        # Warp Actor to Sector 2
        if not pathfind_and_warp(act_client, "Actor", 2): return False

        # 3. Action: Actor enters Sector 1
        print("[Actor] Warping to Sector 1...")
        if not pathfind_and_warp(act_client, "Actor", 1): return False
        
        # 4. Verify: Observer sees event
        print("[Observer] Waiting for enter event...")
        found_enter = False
        start = time.time()
        while time.time() - start < 5:
            # Temporarily set timeout for polling
            obs_client.sock.settimeout(1.0) 
            try:
                evt = obs_client.recv_next_non_notice()
                if not evt: break
                
                if evt.get("type") == "sector.player_entered":
                    data = evt.get("data", {})
                    if data.get("sector_id") == 1:
                        print("SUCCESS: Observer saw Actor enter Sector 1")
                        found_enter = True
                        break
            except socket.timeout:
                continue
            except Exception as e:
                print(f"[Observer] Error receiving event: {e}")
                break
        
        obs_client.sock.settimeout(obs_client.timeout) # Reset timeout
        if not found_enter:
            print("FAILURE: Observer did not receive sector.player_entered event")
            return False

        # 5. Action: Actor leaves Sector 1 (to Sector 2)
        print("[Actor] Warping back to Sector 2...")
        if not pathfind_and_warp(act_client, "Actor", 2): return False

        # 6. Verify: Observer sees exit
        print("[Observer] Waiting for leave event...")
        found_leave = False
        start = time.time()
        while time.time() - start < 5:
            obs_client.sock.settimeout(1.0)
            try:
                evt = obs_client.recv_next_non_notice()
                if not evt: break
                
                if evt.get("type") == "sector.player_left":
                    data = evt.get("data", {})
                    if data.get("sector_id") == 1:
                        print("SUCCESS: Observer saw Actor leave Sector 1")
                        found_leave = True
                        break
            except socket.timeout:
                continue
            except Exception as e:
                print(f"[Observer] Error receiving event: {e}")
                break

        obs_client.sock.settimeout(obs_client.timeout) # Reset timeout
        if not found_leave:
            print("FAILURE: Observer did not receive sector.player_left event")
            return False

        print("E2E Subscriptions Test Passed.")
        return True

    except Exception as e:
        print(f"An unexpected error occurred during subscriptions test: {e}")
        import traceback
        traceback.print_exc()
        return False
    finally:
        obs_client.close()
        # act_client.close() # This might be unbound if an error occurs early
        if 'act_client' in locals() and act_client is not None:
             act_client.close()
        admin_client.close() # Close admin client too

if __name__ == "__main__":
    if not test_subscriptions():
        sys.exit(1)