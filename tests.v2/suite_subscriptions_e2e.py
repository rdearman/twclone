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

    client.send_json({"command": "move.pathfind", "data": {
        "from_sector_id": current_sector,
        "to_sector_id": target_sector
    }})
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
    notice_sector = None
    try:
        # Set up a real sector-notice source and put both test users there.
        obs_client.connect()
        if not obs_client.register("observer_user", "password"): return False
        if not obs_client.login("observer_user", "password"): return False
        act_client.connect()
        if not act_client.register("actor_user", "password"): return False
        if not act_client.login("actor_user", "password"): return False
        admin = TWClient(host=HOST, port=PORT)
        try:
            admin.connect()
            if not admin.login("System", "BOT"):
                return False
            admin.send_json({
                "command": "sys.raw_sql_exec",
                "data": {"sql": "SELECT sector_id FROM sectors WHERE sector_id > 10 AND (beacon IS NULL OR beacon = '') ORDER BY sector_id LIMIT 1"}
            })
            sector_resp = admin.recv_next_non_notice()
            rows = sector_resp.get("data", {}).get("rows", [])
            if sector_resp.get("status") != "ok" or not rows:
                return False
            notice_sector = int(rows[0][0])
            admin.send_json({
                "command": "sys.raw_sql_exec",
                "data": {"sql": f"UPDATE sectors SET beacon = NULL WHERE sector_id = {notice_sector}"}
            })
            if admin.recv_next_non_notice().get("status") != "ok":
                return False
        finally:
            admin.close()

        if not pathfind_and_warp(obs_client, "Observer", notice_sector): return False
        if not pathfind_and_warp(act_client, "Actor", notice_sector): return False
        # These overlapping filters must produce one copy of a scoped event.
        for data in (
                {"topic": f"sector.{notice_sector}"},
                {"topic": "sector.*"},
                {"event_type": "sector.notice"}):
            obs_client.send_json({"command": "subscribe.add", "data": data})
            if obs_client.recv_next_non_notice().get("status") != "ok": return False

        beacon_text = f"notice-e2e-{int(time.time())}"
        act_client.send_json({"command": "sector.set_beacon", "data": {"sector_id": notice_sector, "text": beacon_text}})
        beacon_resp = act_client.recv_next_non_notice()
        if beacon_resp.get("status") != "ok": return False

        notice_event = None
        start = time.time()
        while time.time() - start < 5:
            obs_client.sock.settimeout(1.0)
            try:
                evt = obs_client.recv_json()
                if evt and evt.get("type") == "sector.notice":
                    notice_event = evt
                    break
            except socket.timeout:
                continue
        if not notice_event:
            return False
        notice_data = notice_event.get("data", {})
        if (notice_data.get("sector_id") != notice_sector
                or notice_data.get("subtype") != "beacon_set"
                or notice_data.get("details", {}).get("beacon") != beacon_text
                or notice_data.get("notice_id", 0) <= 0):
            return False

        # Exact event, scoped sector and namespace wildcard overlap without
        # duplicating the durable event.
        obs_client.sock.settimeout(0.2)
        try:
            duplicate = obs_client.recv_json()
            if duplicate and duplicate.get("type") == "sector.notice":
                return False
        except socket.timeout:
            pass

        obs_client.send_json({"command": "notice.list", "data": {"limit": 100}})
        list_resp = obs_client.recv_next_non_notice()
        if list_resp.get("status") != "ok":
            return False
        sector_rows = [row for row in list_resp.get("data", {}).get("items", [])
                       if row.get("id") == notice_data["notice_id"]]
        if not sector_rows:
            return False
        row = sector_rows[0]
        if (row.get("scope") != "sector" or row.get("sector_id") != notice_sector
                or row.get("meta", {}).get("beacon") != beacon_text):
            return False

        return True

    except Exception as e:
        print(f"Error: {e}")
        return False
    finally:
        if notice_sector is not None:
            admin = TWClient(host=HOST, port=PORT)
            try:
                admin.connect()
                if admin.login("System", "BOT"):
                    admin.send_json({
                        "command": "sys.raw_sql_exec",
                        "data": {"sql": f"UPDATE sectors SET beacon = NULL WHERE sector_id = {notice_sector}"}
                    })
                    admin.recv_next_non_notice()
            except Exception:
                pass
            finally:
                admin.close()
        obs_client.close()
        act_client.close()

if __name__ == "__main__":
    if not test_subscriptions():
        sys.exit(1)
    print("E2E Subscriptions Test Passed.")
