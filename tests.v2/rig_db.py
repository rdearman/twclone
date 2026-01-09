#!/usr/bin/env python3
import json
import argparse
import subprocess
import os
import sys

def execute_sql(sql, dbname="twclone", user="postgres", password=None, host="localhost"):
    env = os.environ.copy()
    if password:
        env["PGPASSWORD"] = password
    
    cmd = ["psql", "-h", host, "-U", user, "-d", dbname, "-c", sql]
    
    result = subprocess.run(cmd, env=env, capture_output=True, text=True)
    
    if result.returncode != 0:
        print(f"ERROR executing SQL:\n{sql}\nError: {result.stderr}")
        return False
    return True

def get_player_id(username, dbname="twclone", user="postgres", password=None, host="localhost"):
    env = os.environ.copy()
    if password:
        env["PGPASSWORD"] = password
        
    sql = f"SELECT player_id FROM players WHERE name = '{username}'"
    cmd = ["psql", "-h", host, "-U", user, "-d", dbname, "-t", "-A", "-c", sql]
    
    result = subprocess.run(cmd, env=env, capture_output=True, text=True)
    
    if result.returncode == 0 and result.stdout.strip():
        return int(result.stdout.strip())
    return None

def main():
    parser = argparse.ArgumentParser(description="Rig the database for regression tests")
    parser.add_argument("--config", required=True, help="Path to test_rig.json")
    parser.add_argument("--reset", action="store_true", help="Reset test data before rigging")
    args = parser.parse_args()

    db_config = {"dbname": "twclone", "user": "postgres", "host": "localhost", "password": "B1lb0 Bagg1ns!"}

    with open(args.config) as f:
        rig = json.load(f)

    if args.reset:
        print("Resetting test data...")
        # Specific cleanup if needed, but upserts handle most of it
        pass

    # 1. Config
    if "config" in rig:
        print("Applying Config...")
        for k, v in rig["config"].items():
            # config table has key, value, type
            sql = f"INSERT INTO config (key, value, type) VALUES ('{k}', '{v}', 'int') ON CONFLICT (key) DO UPDATE SET value = '{v}';"
            execute_sql(sql, **db_config)

    # 2. Sectors
    if "sectors" in rig:
        print("Rigging Sectors...")
        for s in rig["sectors"]:
            sql = f"INSERT INTO sectors (sector_id, name) VALUES ({s['sector_id']}, '{s['name']}') ON CONFLICT (sector_id) DO NOTHING;"
            execute_sql(sql, **db_config)

    # 3. Ports
    if "ports" in rig:
        print("Rigging Ports...")
        for p in rig["ports"]:
            sql = f"""
            INSERT INTO ports (port_id, sector_id, type, name) 
            VALUES ({p['port_id']}, {p['sector_id']}, {p['type']}, '{p['name']}') 
            ON CONFLICT (port_id) DO UPDATE SET 
                sector_id = {p['sector_id']}, 
                type = {p['type']}, 
                name = '{p['name']}';
            """
            execute_sql(sql, **db_config)

    # 4. Users
    if "users" in rig:
        print("Rigging Users...")
        for u in rig["users"]:
            credits = u.get("credits", 5000)
            sector = u.get("sector_id", 1)
            ptype = u.get("type", 2)
            align = u.get("alignment", 0)
            
            sql = f"""
            INSERT INTO players (name, passwd, credits, sector_id, type, alignment, commission_id, loggedin)
            VALUES ('{u['username']}', '{u['password']}', {credits}, {sector}, {ptype}, {align}, 1, now())
            ON CONFLICT (name) DO UPDATE SET 
                credits = {credits}, 
                sector_id = {sector}, 
                type = {ptype}, 
                alignment = {align};
            """
            execute_sql(sql, **db_config)

            # Seed turns
            pid = get_player_id(u['username'], **db_config)
            if pid:
                sql = f"INSERT INTO turns (player_id, turns_remaining, last_update) VALUES ({pid}, 1000, now()) ON CONFLICT (player_id) DO UPDATE SET turns_remaining = 1000;"
                execute_sql(sql, **db_config)

    # 5. Corporations
    if "corporations" in rig:
        print("Rigging Corporations...")
        for c in rig["corporations"]:
            owner_id = get_player_id(c["owner_username"], **db_config)
            if owner_id:
                sql = f"INSERT INTO corporations (corporation_id, name, owner_id) VALUES ({c['corporation_id']}, '{c['name']}', {owner_id}) ON CONFLICT (corporation_id) DO NOTHING;"
                execute_sql(sql, **db_config)

    # 6. Shiptypes
    if "shiptypes" in rig:
        print("Rigging Shiptypes...")
        for st in rig["shiptypes"]:
            sql = f"""
            INSERT INTO shiptypes (shiptypes_id, name, basecost, maxholds, maxfighters, maxshields) 
            VALUES ({st['shiptypes_id']}, '{st['name']}', {st['basecost']}, {st['maxholds']}, {st['maxfighters']}, {st['maxshields']}) 
            ON CONFLICT (shiptypes_id) DO NOTHING;
            """
            execute_sql(sql, **db_config)

    # 7. Ships
    if "ships" in rig:
        print("Rigging Ships...")
        for s in rig["ships"]:
            owner_id = get_player_id(s["owner_username"], **db_config)
            if owner_id:
                cols = ["ship_id", "type_id", "name", "holds", "sector_id", "fighters", "shields", "hull"]
                vals = [str(s["ship_id"]), str(s["type_id"]), f"'{s['name']}'", "100", str(s["sector_id"]), "1", "1", "100"]
                
                if "holds" in s: vals[3] = str(s["holds"])
                if "fighters" in s: vals[5] = str(s["fighters"])
                if "shields" in s: vals[6] = str(s["shields"])
                if "hull" in s: vals[7] = str(s["hull"])
                
                if "ported" in s:
                    cols.append("ported")
                    vals.append(str(s["ported"]))
                if "genesis" in s:
                    cols.append("genesis")
                    vals.append(str(s["genesis"]))
                if "ore" in s:
                    cols.append("ore")
                    vals.append(str(s["ore"]))
                if "mines" in s:
                    cols.append("mines")
                    vals.append(str(s["mines"]))
                if "has_transwarp" in s:
                    cols.append("has_transwarp")
                    vals.append(str(s["has_transwarp"]))

                sql = f"INSERT INTO ships ({','.join(cols)}) VALUES ({','.join(vals)}) ON CONFLICT (ship_id) DO UPDATE SET sector_id={s['sector_id']};"
                execute_sql(sql, **db_config)

                sql = f"UPDATE players SET ship_id = {s['ship_id']} WHERE player_id = {owner_id};"
                execute_sql(sql, **db_config)
                
                # Use subquery to avoid conflict issues if no unique index exists on (ship_id, player_id)
                sql = f"""
                INSERT INTO ship_ownership (ship_id, player_id, role_id, is_primary) 
                SELECT {s['ship_id']}, {owner_id}, 1, TRUE
                WHERE NOT EXISTS (SELECT 1 FROM ship_ownership WHERE ship_id = {s['ship_id']} AND player_id = {owner_id});
                """
                execute_sql(sql, **db_config)

    # 8. Planets
    if "planets" in rig:
        print("Rigging Planets...")
        for p in rig["planets"]:
            owner_id = get_player_id(p["owner_username"], **db_config)
            if owner_id:
                sql = f"""
                INSERT INTO planets (planet_id, num, sector_id, name, owner_id, owner_type, class, created_by)
                VALUES ({p['planet_id']}, {p['num']}, {p['sector_id']}, '{p['name']}', {owner_id}, 'player', '{p['class']}', {owner_id})
                ON CONFLICT (planet_id) DO NOTHING;
                """
                execute_sql(sql, **db_config)
                
                if "citadel" in p:
                    c = p["citadel"]
                    sql = f"""
                    INSERT INTO citadels (planet_id, level, qcannonsector, qcannonatmosphere, militaryreactionlevel)
                    VALUES ({p['planet_id']}, {c['level']}, {c['qCannonSector']}, {c['qCannonAtmosphere']}, {c['militaryReactionLevel']})
                    ON CONFLICT (planet_id) DO NOTHING;
                    """
                    execute_sql(sql, **db_config)

    # 9. Deployed Assets
    if "deployed_assets" in rig:
        print("Rigging Deployed Assets...")
        for a in rig["deployed_assets"]:
            owner_id = get_player_id(a["owner_username"], **db_config)
            if owner_id:
                sql = f"""
                INSERT INTO sector_assets (sector_id, owner_id, asset_type, quantity, offensive_setting, deployed_at)
                VALUES ({a['sector_id']}, {owner_id}, {a['asset_type']}, {a['quantity']}, {a['offensive_setting']}, now());
                """
                execute_sql(sql, **db_config)

    # 10. Shipyard Inventory
    if "shipyard_inventory" in rig:
        print("Rigging Shipyard Inventory...")
        for i in rig["shipyard_inventory"]:
            sql = f"INSERT INTO shipyard_inventory (port_id, ship_type_id, enabled) VALUES ({i['port_id']}, {i['ship_type_id']}, {i['enabled']}) ON CONFLICT (port_id, ship_type_id) DO NOTHING;"
            execute_sql(sql, **db_config)

    # 11. Player Prefs
    if "player_prefs" in rig:
        print("Rigging Player Prefs...")
        for p in rig["player_prefs"]:
            owner_id = get_player_id(p["username"], **db_config)
            if owner_id:
                sql = f"INSERT INTO player_prefs (player_prefs_id, key, type, value) VALUES ({owner_id}, '{p['key']}', '{p['type']}', '{p['value']}') ON CONFLICT (player_prefs_id, key) DO UPDATE SET value = '{p['value']}';"
                execute_sql(sql, **db_config)

    print("Rigging Complete.")

if __name__ == "__main__":
    main()