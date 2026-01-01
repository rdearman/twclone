#!/usr/bin/env python3
"""
Database setup helper for regression tests.
Loads test configuration from JSON and sets up database state.
No external dependencies - uses psql subprocess.
"""

import subprocess
import json
import os
import re
from pathlib import Path


class DBSetupHelper:
    """DB setup helper using psql subprocess."""
    
    def __init__(self, config_file=None):
        """Initialize with database connection info from bigbang.json."""
        if config_file is None:
            if Path("bin/bigbang.json").exists():
                config_file = "bin/bigbang.json"
            elif Path("bigbang.json").exists():
                config_file = "bigbang.json"
            else:
                raise FileNotFoundError("Cannot find bigbang.json")
        
        with open(config_file) as f:
            cfg = json.load(f)
        
        conn_str = cfg.get("app", "")
        db_name = cfg.get("db", "twclone")
        conn_str = conn_str.replace("%DB%", db_name)
        
        # Parse connection string (handles quoted values with spaces)
        self.conn_params = {"dbname": db_name}
        parts = []
        current = ""
        in_quotes = False
        for char in conn_str:
            if char == "'":
                in_quotes = not in_quotes
                current += char
            elif char == " " and not in_quotes:
                if current:
                    parts.append(current)
                current = ""
            else:
                current += char
        if current:
            parts.append(current)
        
        for part in parts:
            if '=' in part:
                k, v = part.split('=', 1)
                v = v.strip("'\"")
                self.conn_params[k] = v
    
    def execute(self, sql):
        """Execute SQL using psql with env vars."""
        env = os.environ.copy()
        
        # Set PGPASSWORD env var
        if "password" in self.conn_params:
            env["PGPASSWORD"] = self.conn_params["password"]
        
        # Build psql command
        cmd = ["psql"]
        if "host" in self.conn_params:
            cmd.extend(["-h", self.conn_params["host"]])
        if "port" in self.conn_params:
            cmd.extend(["-p", self.conn_params["port"]])
        if "user" in self.conn_params:
            cmd.extend(["-U", self.conn_params["user"]])
        if "dbname" in self.conn_params:
            cmd.extend(["-d", self.conn_params["dbname"]])
        
        cmd.extend(["-c", sql])
        
        result = subprocess.run(cmd, env=env, capture_output=True, text=True)
        
        if result.returncode != 0:
            print(f"ERROR executing SQL:\n{sql}\nError: {result.stderr}")
            raise Exception(f"SQL execution failed")
        
        return result.stdout
    
    def setup_from_config(self, config_file="tests.v2/test_setup.json"):
        """Load and apply test configuration from JSON file."""
        with open(config_file) as f:
            config = json.load(f)
        
        print(f"\n📋 Loading test configuration from {config_file}...")
        
        # Create sectors
        for sector in config.get("test_sectors", []):
            self._create_sector(sector)
        
        # Create players with ships and assets
        for player in config.get("test_players", []):
            self._create_player_full(player)
            print(f"  ✓ {player['username']}")
        
        # Create planets with citadels
        for planet in config.get("test_planets", []):
            self._create_planet(planet)
            print(f"  ✓ {planet['name']}")
        
        print("\n✅ Test setup complete!\n")
    
    def _create_sector(self, sector):
        """Create a sector if it doesn't exist."""
        sql = f"""
        INSERT INTO sectors (sector_id, name)
        VALUES ({sector['sector_id']}, '{sector.get("name", f"Sector {sector['sector_id']}")}')
        ON CONFLICT DO NOTHING;
        """
        self.execute(sql)
    
    def _get_player_id(self, username):
        """Get player ID by username."""
        sql = f"SELECT player_id FROM players WHERE name = '{username}' LIMIT 1;"
        result = self.execute(sql)
        # Parse output: skip header, separator, fetch value
        lines = result.strip().split('\n')
        for line in lines:
            line = line.strip()
            if line and not line.startswith('-') and not line.startswith('player_id'):
                try:
                    return int(line)
                except:
                    pass
        return None
    
    def _create_player_full(self, player_config):
        """Create a player with ships and deployed assets."""
        username = player_config["username"]
        password = player_config["password"]
        credits = player_config.get("credits", 5000)
        
        sql = f"""
        INSERT INTO players (name, passwd, credits, type, sector_id, commission_id, loggedin)
        VALUES ('{username}', '{password}', {credits}, 2, 1, 1, now())
        ON CONFLICT (name) DO NOTHING;
        """
        self.execute(sql)
        
        player_id = self._get_player_id(username)
        if not player_id:
            print(f"    WARNING: Could not create/find player {username}")
            return
        
        # Create ships
        for ship_config in player_config.get("ships", []):
            self._create_ship(player_id, ship_config)
        
        # Deploy assets
        for asset in player_config.get("deployed_assets", []):
            sql = f"""
            INSERT INTO sector_assets
            (sector_id, owner_id, asset_type, quantity, offensive_setting, deployed_at)
            VALUES ({asset['sector_id']}, {player_id}, {asset['asset_type']},
                    {asset['quantity']}, {asset.get('offensive_setting', 1)},
                    now());
            """
            self.execute(sql)
    
    def _create_ship(self, player_id, ship_config):
        """Create a ship for a player."""
        ship_name = ship_config.get("name", "Test Ship")
        type_id = ship_config.get("type_id", 1)
        sector_id = ship_config.get("sector_id", 1)
        
        # Ensure shiptype exists
        sql = f"""
        INSERT INTO shiptypes (shiptypes_id, name, basecost, maxholds)
        VALUES ({type_id}, 'Scout', 1000, 100)
        ON CONFLICT DO NOTHING;
        """
        self.execute(sql)
        
        # Create ship
        sql = f"""
        INSERT INTO ships (type_id, name, sector_id, holds, fighters, shields, hull)
        VALUES ({type_id}, '{ship_name}', {sector_id}, 100,
                {ship_config.get("fighters", 10)},
                {ship_config.get("shields", 50)},
                {ship_config.get("hull", 100)})
        RETURNING ship_id;
        """
        result = self.execute(sql)
        
        # Parse ship_id from output
        lines = result.strip().split('\n')
        ship_id = None
        for line in lines:
            line = line.strip()
            if line and not line.startswith('-') and not line.startswith('ship_id'):
                try:
                    ship_id = int(line)
                    break
                except:
                    pass
        
        if not ship_id:
            return
        
        # Link ship to player
        sql = f"""
        INSERT INTO ship_ownership (ship_id, player_id, role_id, is_primary)
        VALUES ({ship_id}, {player_id}, 1, TRUE)
        ON CONFLICT DO NOTHING;
        """
        self.execute(sql)
        
        # Update player's ship
        sql = f"""
        UPDATE players SET ship_id = {ship_id}, sector_id = {sector_id}
        WHERE player_id = {player_id};
        """
        self.execute(sql)
    
    def _create_planet(self, planet_config):
        """Create a planet with optional citadel."""
        planet_id = planet_config["planet_id"]
        num = planet_config["num"]
        sector_id = planet_config["sector_id"]
        name = planet_config["name"]
        owner_id = planet_config.get("owner_id", 0)
        owner_type = planet_config.get("owner_type", "player")
        class_code = planet_config.get("class", "M")
        created_by = planet_config.get("created_by", 0)
        
        sql = f"""
        INSERT INTO planets
        (planet_id, num, sector_id, name, owner_id, owner_type, class, created_by)
        VALUES ({planet_id}, {num}, {sector_id}, '{name}', {owner_id}, '{owner_type}', '{class_code}', {created_by})
        ON CONFLICT DO NOTHING;
        """
        self.execute(sql)
        
        # Create citadel if specified
        if "citadel" in planet_config:
            citadel = planet_config["citadel"]
            sql = f"""
            INSERT INTO citadels
            (planet_id, level, qCannonSector, qCannonAtmosphere, militaryReactionLevel)
            VALUES ({planet_id}, {citadel.get("level", 1)}, {citadel.get("qCannonSector", 0)},
                    {citadel.get("qCannonAtmosphere", 0)}, {citadel.get("militaryReactionLevel", 0)})
            ON CONFLICT DO NOTHING;
            """
            self.execute(sql)


if __name__ == "__main__":
    helper = DBSetupHelper()
    helper.setup_from_config("tests.v2/test_setup.json")
