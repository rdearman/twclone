#!/usr/bin/env python3
"""
Interactive TW JSON client

Envelope:
  {"id":"<cli-seq>", "command":"<action>", "data":{...}}
Common commands:
  - session.ping / session.hello
  - auth.login {"user_name": "...", "password": "..."} (fallback: {"player_name": "...", "password": "..."})
  - move.describe_sector {"sector_id": <int>}
  - move.warp {"to_sector_id": <int>}
"""

import argparse
import getpass
import json
import socket
import sys
from typing import Any, Dict, List, Optional, Tuple

HOST, PORT = "127.0.0.1", 1234

# ---------------- transport ----------------

class Conn:
    def __init__(self, sock: socket.socket, debug: bool = False):
        self.sock = sock
        self._seq = 0
        self._r = sock.makefile("r", encoding="utf-8", newline="\n")
        self.debug = debug

    def _next_id(self) -> str:
        self._seq += 1
        return f"cli-{self._seq:04d}"

    def send(self, obj: Dict[str, Any]) -> None:
        line = json.dumps(obj, separators=(",", ":")) + "\n"
        if self.debug:
            print(f"[DBG] << {line!r}")
        self.sock.sendall(line.encode("utf-8"))

    def recv(self) -> Dict[str, Any]:
        self.sock.settimeout(15.0)
        line = self._r.readline()
        if self.debug:
            print(f"[DBG] >> {line!r}")
        if not line:
            raise ConnectionError("Server closed connection.")
        return json.loads(line)

    def rpc(self, command: str, data: Dict[str, Any]) -> Dict[str, Any]:
        req_id = self._next_id()
        self.send({"id": req_id, "command": command, "data": data})
        while True:
            try:
                resp = self.recv()
                if resp.get("reply_to") == req_id:
                    return resp
                print(f"[WARN] Ignoring response with unexpected id: {resp.get('id')} with reply_to: {resp.get('reply_to')}")
            except (ConnectionError, socket.timeout) as e:
                print(f"[ERROR] RPC call timed out or connection closed: {e}")
                raise e

# ---------------- client ----------------

last_desc: Optional[Dict[str, Any]] = None

def get_data(resp: Dict[str, Any]) -> Dict[str, Any]:
    return resp.get("data") or {}

def extract_current_sector(resp: Dict[str, Any]) -> Any:
    try:
        data = resp.get("data", {})
        if "current_sector" in data:
            return data.get("current_sector")
        if "session" in data:
            return data.get("session", {}).get("current_sector")
        if "sector_id" in data:
            return data.get("sector_id")
    except Exception:
        pass
    return None

def extract_sector_overview(desc: Optional[Dict[str, Any]]):
    if not desc:
        return None, None, []
    data = get_data(desc)
    sid = data.get("sector_id")
    name = data.get("name")
    warps = data.get("adjacent") or []
    return sid, name, warps

def describe_sector(conn: Conn, sector_id: int):
    global last_desc
    desc = conn.rpc("move.describe_sector", {"sector_id": sector_id})
    last_desc = desc
    return desc

def show_extras(desc: Optional[Dict[str, Any]]):
    if not desc: return
    data = get_data(desc)
    if not data: return
    
    planets = data.get("planets") or []
    if planets:
        print("\nPlanets:")
        for p in planets:
            print(f"  - {p.get('name')} (id: {p.get('id')}, class: {p.get('class')}, ore: {p.get('ore_amount')})")
    
    ports = data.get("ports") or []
    if ports:
        print("\nStarports:")
        for p in ports:
            print(f"  - {p.get('name')} (id: {p.get('id')}, class: {p.get('class')})")
    
    ships = data.get("ships") or []
    if ships:
        print("\nShips:")
        for s in ships:
            print(f"  - {s.get('name') or s.get('player_name')} ({s.get('ship_type')})")

# ---------------- Sub-menus ----------------

def planet_menu(conn: Conn, current_sector: int) -> int:
    print("\n--- Planet Menu ---")
    print(" (L) Land on Planet")
    print(" (U) Unload Cargo")
    print(" (S) Sell Cargo")
    print(" (R) Release Cargo")
    print(" (D) Drop Probe")
    print(" (B) Back to Main Menu")

    cmd = input("Planet Command: ").strip().lower()

    if cmd == "l":
        print("Not Implemented: Land on Planet")
    elif cmd == "u":
        print("Not Implemented: Unload Cargo")
    elif cmd == "s":
        print("Not Implemented: Sell Cargo")
    elif cmd == "r":
        print("Not Implemented: Release Cargo")
    elif cmd == "d":
        print("Not Implemented: Drop Probe")
    elif cmd == "b":
        return current_sector
    else:
        print("Invalid command. Please try again.")

    return planet_menu(conn, current_sector)

def stardock_menu(conn: Conn, current_sector: int) -> int:
    print("\n--- Stardock Menu ---")
    print(" (B) Buy Ship")
    print(" (S) Sell Ship")
    print(" (R) Repair Ship")
    print(" (U) Upgrade Ship")
    print(" (L) Back to Main Menu")

    cmd = input("Stardock Command: ").strip().lower()

    if cmd == "b":
        print("Not Implemented: Buy Ship")
    elif cmd == "s":
        print("Not Implemented: Sell Ship")
    elif cmd == "r":
        print("Not Implemented: Repair Ship")
    elif cmd == "u":
        print("Not Implemented: Upgrade Ship")
    elif cmd == "l":
        return current_sector
    else:
        print("Invalid command. Please try again.")

    return stardock_menu(conn, current_sector)

def trade_menu(conn: Conn, current_sector: int) -> int:
    print("\n--- Trade Menu ---")
    print(" (B) Buy Commodity")
    print(" (S) Sell Commodity")
    print(" (L) Back to Main Menu")

    cmd = input("Trade Command: ").strip().lower()

    if cmd == "b":
        print("Not Implemented: Buy Commodity")
    elif cmd == "s":
        print("Not Implemented: Sell Commodity")
    elif cmd == "l":
        return current_sector
    else:
        print("Invalid command. Please try again.")

    return trade_menu(conn, current_sector)

def klingon_menu(conn: Conn, current_sector: int) -> int:
    print("\n--- Klingon Commands ---")
    print(" (F) Fire Torpedo")
    print(" (I) Infiltrate Starbase")
    print(" (L) Back to Main Menu")

    cmd = input("Klingon Command: ").strip().lower()

    if cmd == "f":
        print("Not Implemented: Fire Torpedo")
    elif cmd == "i":
        print("Not Implemented: Infiltrate Starbase")
    elif cmd == "l":
        return current_sector
    else:
        print("Invalid command. Please try again.")

    return klingon_menu(conn, current_sector)

def zarkonian_menu(conn: Conn, current_sector: int) -> int:
    print("\n--- Zarkonian Commands ---")
    print(" (A) Activate Stealth")
    print(" (P) Plant Mine")
    print(" (L) Back to Main Menu")

    cmd = input("Zarkonian Command: ").strip().lower()

    if cmd == "a":
        print("Not Implemented: Activate Stealth")
    elif cmd == "p":
        print("Not Implemented: Plant Mine")
    elif cmd == "l":
        return current_sector
    else:
        print("Invalid command. Please try again.")

    return zarkonian_menu(conn, current_sector)

def testing_menu(conn: Conn, current_sector: int) -> int:
    global last_desc
    print("\n--- Testing Menu ---")
    print(" (C) Capabilities (cap)")
    print(" (X) Cap Spam (capspam)")
    print(" (S) Schema")
    print(" (M) My Info (me)")
    print(" (H) Ship Info (ship)")
    print(" (O) Online Players (online)")
    print(" (B) Buy (buy)")
    print(" (I) Interactive Buy (ibuy)")
    print(" (F) Force Move (force_move)")
    print(" (R) Register Player (register)")
    print(" (E) Refresh Player (refresh)")
    print(" (L) Logout")
    print(" (D) Disconnect")
    print(" (J) Raw JSON command")
    print(" (Q) Back to Main Menu")

    cmd = input("Testing Command: ").strip().lower()

    if cmd == "c":
        r = conn.rpc("system.hello", {})
        print(json.dumps(r, ensure_ascii=False, indent=2))
    elif cmd == "x":
        n_str = input("How many times? (default 3): ")
        n = int(n_str) if n_str.isdigit() else 3
        for i in range(n):
            r = conn.rpc("system.hello", {})
            print(f"[{i+1}] {r.get('status')} {r.get('type')}")
    elif cmd == "s":
        key = input("Schema key (optional): ")
        if key:
            r = conn.rpc("system.describe_schema", {"key": key})
        else:
            r = conn.rpc("system.describe_schema", {})
        print(json.dumps(r, ensure_ascii=False, indent=2))
    elif cmd == "m":
        r = conn.rpc("player.my_info", {})
        print(json.dumps(r, ensure_ascii=False, indent=2))
    elif cmd == "h":
        r = conn.rpc("ship.info", {})
        print(json.dumps(r, ensure_ascii=False, indent=2))
    elif cmd == "o":
        r = conn.rpc("player.list_online", {})
        print(json.dumps(r, ensure_ascii=False, indent=2))
        data = get_data(r)
        names = [p.get("name") for p in (data.get("players") or []) if isinstance(p, dict)]
        if names:
            print("Online:", ", ".join(n for n in names if isinstance(n, str)))
    elif cmd == "b":
        if not last_desc:
            print("No sector data. Describe a sector first.")
            return current_sector
        port_id = None
        ports = get_data(last_desc).get("ports", [])
        if ports:
            port_id = ports[0].get("id")
        if port_id is None:
            print("No port found in the current sector. Cannot buy.")
            return current_sector
        
        commodity = input("Commodity: ")
        qty_str = input("Quantity: ")
        try:
            qty = int(qty_str)
            r = conn.rpc("trade.buy", {"port_id": port_id, "commodity": commodity, "quantity": qty})
            print(json.dumps(r, ensure_ascii=False, indent=2))
        except ValueError:
            print("Quantity must be an integer.")
    elif cmd == "i":
        idem = input("Idempotency key: ")
        port_id_str = input("Port ID: ")
        commodity = input("Commodity: ")
        qty_str = input("Quantity: ")
        try:
            port_id = int(port_id_str)
            qty = int(qty_str)
            env = {"id": conn._next_id(), "command": "trade.buy",
                   "data": {"port_id": port_id, "commodity": commodity, "quantity": qty},
                   "meta": {"idempotency_key": idem}}
            conn.send(env)
            print(json.dumps(conn.recv(), ensure_ascii=False, indent=2))
        except ValueError:
            print("Port ID and quantity must be integers.")
    elif cmd == "f":
        target_str = input("Target sector ID: ")
        try:
            target = int(target_str)
            r = conn.rpc("move.warp", {"to_sector_id": target})
            print(json.dumps(r, ensure_ascii=False, indent=2))
        except ValueError:
            print("Sector ID must be an integer.")
    elif cmd == "r":
        user2 = input("Username: ")
        pass2 = input("Password: ")
        r = conn.rpc("auth.register", {"user_name": user2, "password": pass2})
        print(json.dumps(r, ensure_ascii=False, indent=2))
    elif cmd == "e":
        r = conn.rpc("auth.refresh", {})
        print(json.dumps(r, ensure_ascii=False, indent=2))
    elif cmd == "l":
        r = conn.rpc("auth.logout", {})
        print(json.dumps(r, ensure_ascii=False, indent=2))
    elif cmd == "d":
        r = conn.rpc("system.disconnect", {})
        print(json.dumps(r, ensure_ascii=False, indent=2))
        return -1
    elif cmd == "j":
        print("--- Raw JSON Command ---")
        command = input("Enter command (e.g., 'move.warp'): ")
        data_str = input("Enter JSON data (e.g., '{\"to_sector_id\": 1}'): ")
        try:
            data = json.loads(data_str)
            resp = conn.rpc(command, data)
            print(json.dumps(resp, ensure_ascii=False, indent=2))
        except json.JSONDecodeError:
            print("Invalid JSON data.")
        except Exception as e:
            print(f"An error occurred: {e}")
    elif cmd == "q":
        pass
    else:
        print("Invalid testing command. Please try again.")

    return current_sector

def main_menu(conn: Conn, current_sector: int) -> int:
    global last_desc
    print(f"\n--- Main Menu (Sector {current_sector}) ---")
    print(" (W) Warp to a sector         | (L) Leave Sector")
    print(" (D) Describe current sector  | (M) Move")
    print(" (P) Planet Menu              | (O) Orbital Planet Scan")
    print(" (S) Stardock Menu            | (R) Repair")
    print(" (T) Trade Menu               | (V) View Map")
    print(" (B) Set Beacon               | (X) Exit")
    print(" (C) Compute                  | (Z) Zarkonian Commands")
    print(" (E) Enter Ship               | (H) Help")
    print(" (F) Fix                      | (Q) Quit")
    print(" (I) Interdict                | (K) Klingon Commands")
    print(" (J) Jettison                 | (Y) Testing Menu")

    cmd = input("Command: ").strip().lower()

    if cmd == "q" or cmd == "x":
        return -1
    elif cmd == "w":
        to_sector_id_str = input("Warp to sector: ")
        try:
            to_sector_id = int(to_sector_id_str)
            if not last_desc:
                last_desc = describe_sector(conn, current_sector)
            _, _, warps = extract_sector_overview(last_desc)
            if to_sector_id not in warps:
                print(f"{to_sector_id} is not adjacent. Valid: {', '.join(map(str, warps))}")
            else:
                moved = conn.rpc("move.warp", {"to_sector_id": to_sector_id})
                new_cur = extract_current_sector(moved)
                current_sector = new_cur if isinstance(new_cur, int) else to_sector_id
                last_desc = describe_sector(conn, current_sector)
                sid, name, warps = extract_sector_overview(last_desc)
                if isinstance(sid, int):
                    current_sector = sid
                print(f"\nArrived sector {current_sector}{' — ' + name if name else ''}.")
                print(f"Adjacent sectors: {', '.join(map(str, warps)) if warps else '(none)'}")
                show_extras(last_desc)
        except ValueError:
            print("Invalid sector ID.")
        except Exception as e:
            print(f"An error occurred: {e}")
    elif cmd == "d":
        print(f"[move] describe_sector: {current_sector}")
        desc = describe_sector(conn, current_sector)
        sid, name, warps = extract_sector_overview(desc)
        if isinstance(sid, int):
            current_sector = sid
        print(f"\nSector {current_sector}{' — ' + name if name else ''}")
        print(f"Adjacent sectors: {', '.join(map(str, warps)) if warps else '(none)'}")
        show_extras(desc)
    elif cmd == "p":
        current_sector = planet_menu(conn, current_sector)
    elif cmd == "s":
        current_sector = stardock_menu(conn, current_sector)
    elif cmd == "t":
        current_sector = trade_menu(conn, current_sector)
    elif cmd == "k":
        current_sector = klingon_menu(conn, current_sector)
    elif cmd == "z":
        current_sector = zarkonian_menu(conn, current_sector)
    elif cmd == "b":
        print("Not Implemented: Set Beacon")
    elif cmd == "c":
        print("Not Implemented: Compute")
    elif cmd == "e":
        print("Not Implemented: Enter Ship")
    elif cmd == "f":
        print("Not Implemented: Fix")
    elif cmd == "i":
        print("Not Implemented: Interdict")
    elif cmd == "j":
        print("Not Implemented: Jettison")
    elif cmd == "l":
        print("Not Implemented: Leave Sector")
    elif cmd == "m":
        print("Not Implemented: Move")
    elif cmd == "o":
        print("Not Implemented: Orbital Planet Scan")
    elif cmd == "r":
        print("Not Implemented: Repair")
    elif cmd == "v":
        print("Not Implemented: View Map")
    elif cmd == "y":
        current_sector = testing_menu(conn, current_sector)
    elif cmd == "h":
        print("--- Help ---")
        print("Warp: Move to an adjacent sector.")
        print("Describe: Get detailed info on your current sector.")
        print("Planet Menu: Interact with planets.")
        print("Stardock Menu: Interact with star docks.")
        print("Trade Menu: Trade with ports.")
        print("Set Beacon: Leave a public message in the sector.")
        print("Compute: Perform calculations.")
        print("Enter Ship: Board a ship.")
        print("Fix: Fix your ship.")
        print("Interdict: Interdict a target.")
        print("Jettison: Jettison cargo.")
        print("Klingon Commands: Access Klingon-specific actions.")
        print("Leave Sector: Leave the current sector.")
        print("Move: Move your ship.")
        print("Orbital Planet Scan: Scan a planet from orbit.")
        print("Repair: Repair your ship.")
        print("View Map: View the sector map.")
        print("Zarkonian Commands: Access Zarkonian-specific actions.")
        print("Testing Menu: Access developer-level commands.")
        print("Quit: Disconnect and exit.")
    else:
        print("Invalid command. Please try again.")

    return current_sector

def interactive_loop(conn: Conn, current_sector: int) -> None:
    global last_desc
    desc = describe_sector(conn, current_sector)
    
    data = desc.get("data", {})
    
    sid = data.get("sector_id")
    name = data.get("name")
    warps = data.get("adjacent", [])
    
    if isinstance(sid, int):
        current_sector = sid
    last_desc = desc

    print(f"\nYou are in sector {current_sector}{' — ' + name if name else ''}.")
    print(f"Adjacent sectors: {', '.join(map(str, warps)) if warps else '(none)'}")
    
    ports = data.get("ports", [])
    if ports:
        print("Ports here:", ", ".join(p.get("name", "Unnamed Port") for p in ports))
    else:
        print("Ports here: none")

    ships = data.get("ships", [])
    if ships:
        print("Players here:", ", ".join(s.get("player_name", "Unknown Player") for s in ships))
    else:
        print("Players here: none")

    planets = data.get("planets", [])
    if planets:
        print("Planets:", ", ".join(p.get("name", "Unnamed Planet") for p in planets))
    else:
        print("Planets: none")
    
    beacon_text = data.get("beacon")
    if beacon_text:
        print("Beacon:", beacon_text)
    else:
        print("Beacon: none")
    
    print("\nType 'help' for commands.")
    
    while True:
        try:
            new_sector = main_menu(conn, current_sector)
            if new_sector == -1:
                break
            else:
                current_sector = new_sector
        except EOFError:
            print("\nEOF received, exiting.")
            break
        except KeyboardInterrupt:
            print("\nInterrupted.")
            break

def main() -> int:
    parser = argparse.ArgumentParser(description="Trade Wars 2002 interactive client.")
    parser.add_argument("--host", default=HOST, help=f"Server host (default: {HOST})")
    parser.add_argument("--port", type=int, default=PORT, help=f"Server port (default: {PORT})")
    parser.add_argument("--user", help="Player name for login.")
    parser.add_argument("--passwd", help="Password for login.")
    parser.add_argument("--debug", action="store_true", help="Enable debug output")
    args = parser.parse_args()

    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            #print(f"Connecting to {args.host}:{args.port}...")
            s.connect((args.host, args.port))
            conn = Conn(s, debug=args.debug)

            #print("[session] ping:", json.dumps(conn.rpc("session.ping", {}), ensure_ascii=False))
            #print("[session] hello:", json.dumps(conn.rpc("session.hello", {}), ensure_ascii=False))

            user = args.user if args.user else input("Player Name: ")
            passwd = args.passwd if args.passwd else getpass.getpass("Password: ")

            login = conn.rpc("auth.login", {"user_name": user, "password": passwd})
            #print("[auth] login:", json.dumps(login, ensure_ascii=False))
            if login.get("status") in ("error", "refused"):
                fallback = conn.rpc("auth.login", {"player_name": user, "password": passwd})
                print("[auth] login (fallback):", json.dumps(fallback, ensure_ascii=False))
                login = fallback

            if login.get("status") in ("error", "refused"):
                print("Login failed.")
                return 2

            current = extract_current_sector(login)
            if not isinstance(current, int):
                info = conn.rpc("player.my_info", {})
                # print("[auth] player.info:", json.dumps(info, ensure_ascii=False))
                current = extract_current_sector(info)
            if not isinstance(current, int):
                print("Could not determine current sector; aborting.")
                return 2

            interactive_loop(conn, current)
    except Exception as e:
        print(f"[error] {e}")
        return 1

    return 0

if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\nInterrupted.")
        sys.exit(0)
