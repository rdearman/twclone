import random
import time
import sys
import os
import socket
from twclient import TWClient

# Configuration
HOST = os.getenv("HOST", "127.0.0.1")
PORT = int(os.getenv("PORT", 1234))

def pathfind_and_warp(client: TWClient, name: str, target_sector: int):
    client.send_json({"command": "player.my_info"})
    resp = client.recv_next_non_notice()
    current_sector = resp["data"]["player"]["sector"]

    if current_sector == target_sector:
        return True

    client.send_json({"command": "move.pathfind", "data": {"to": target_sector}})
    resp = client.recv_next_non_notice()
    if resp.get("status") != "ok": return False
        
    path = resp["data"]["steps"]
    for next_sec in path[1:]:
        client.send_json({"command": "move.warp", "data": {"to_sector_id": next_sec}})
        w_resp = client.recv_next_non_notice()
        if w_resp.get("status") == "ok":
            current_sector = next_sec
        else:
            return False
    return current_sector == target_sector

def test_subscriptions():
    obs_client = TWClient(host=HOST, port=PORT)
    act_client = TWClient(host=HOST, port=PORT)
    try:
        # Observer
        obs_client.connect()
        if not obs_client.login("observer_user", "password"): return False
        if not pathfind_and_warp(obs_client, "Observer", 1): return False
        obs_client.send_json({"command": "subscribe.add", "data": {"topic": "sector.1"}})
        obs_client.recv_next_non_notice()

        # Actor
        act_client.connect()
        if not act_client.login("actor_user", "password"): return False
        if not pathfind_and_warp(act_client, "Actor", 2): return False

        # Action: Enter Sector 1
        if not pathfind_and_warp(act_client, "Actor", 1): return False
        
        # Verify: Observer sees enter
        found_enter = False
        start = time.time()
        while time.time() - start < 5:
            obs_client.sock.settimeout(1.0) 
            try:
                evt = obs_client.recv_next_non_notice()
                if evt and evt.get("type") == "sector.player_entered":
                    if evt.get("data", {}).get("sector_id") == 1:
                        found_enter = True
                        break
            except socket.timeout: continue
        if not found_enter: return False

        # Action: Leave Sector 1
        if not pathfind_and_warp(act_client, "Actor", 2): return False

        # Verify: Observer sees exit
        found_leave = False
        start = time.time()
        while time.time() - start < 5:
            obs_client.sock.settimeout(1.0)
            try:
                evt = obs_client.recv_next_non_notice()
                if evt and evt.get("type") == "sector.player_left":
                    if evt.get("data", {}).get("sector_id") == 1:
                        found_leave = True
                        break
            except socket.timeout: continue
        return found_leave

    except Exception as e:
        print(f"Error: {e}")
        return False
    finally:
        obs_client.close()
        act_client.close()

if __name__ == "__main__":
    if not test_subscriptions():
        sys.exit(1)
    print("E2E Subscriptions Test Passed.")
