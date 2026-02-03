import sys
import os
import time
import subprocess
import json

# Add current dir to path to find twclient
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
from twclient import TWClient

def execute_sql(sql):
    env = os.environ.copy()
    env["PGPASSWORD"] = "B1lb0 Bagg1ns!"
    cmd = ["psql", "-h", "localhost", "-U", "postgres", "-d", "twclone", "-t", "-A", "-c", sql]
    res = subprocess.run(cmd, env=env, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"SQL Error: {res.stderr}")
        raise Exception("SQL failed")
    return res.stdout.strip()

def test_ferengi_init():
    print("--- Ferengi Auto-Creation Test ---")
    
    # 1. Setup Admin
    c_admin = TWClient()
    c_admin.connect()
    if not c_admin.login("System", "BOT"):
        raise Exception("Admin login failed")

    # Resolve System ID
    system_id = int(execute_sql("SELECT player_id FROM players WHERE name='System';"))
    print(f"System Player ID: {system_id}")

    # 2. Check if Ferengi Corp exists, if not trigger creation
    # Note: It might already exist if the server restarted during the conversation
    res = execute_sql("SELECT name, owner_id FROM corporations WHERE tag='FENG';")
    
    if not res:
        print("Ferengi Corporation missing. Triggering Init...")
        c_admin.send_json({"command": "sys.npc.ferengi_tick_once", "data": {}})
        resp = c_admin.recv_next_non_notice()
        if resp.get("status") != "ok":
            raise Exception(f"Tick failed: {resp}")
        
        res = execute_sql("SELECT name, owner_id FROM corporations WHERE tag='FENG';")
        if not res:
            raise Exception("Ferengi corporation was not created after tick")
    else:
        print("Ferengi Corporation already exists (from startup). Verifying...")

    print(f"Current DB Result: {res}")
    name, owner_id = res.split('|')
    if name != "Ferengi Alliance":
        raise Exception(f"Incorrect name: {name}")
    if int(owner_id) != system_id:
        raise Exception(f"Incorrect owner_id: {owner_id} (expected {system_id})")

    # 3. Idempotency Check (Creation path in C handles conflict)
    print("Checking Idempotency (Running tick again)...")
    c_admin.send_json({"command": "sys.npc.ferengi_tick_once", "data": {}})
    resp = c_admin.recv_next_non_notice()
    if resp.get("status") != "ok":
        raise Exception(f"Second tick failed: {resp}")

    count = int(execute_sql("SELECT count(*) FROM corporations WHERE tag='FENG';"))
    if count != 1:
        raise Exception(f"Duplicate corporations found! Count: {count}")

    print("SUCCESS: Ferengi Alliance verified and idempotent.")

if __name__ == "__main__":
    try:
        test_ferengi_init()
    except Exception as e:
        print(f"TEST FAILED: {e}")
        sys.exit(1)
