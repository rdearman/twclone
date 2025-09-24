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



def enter_ship_menu(conn: Conn, current_sector: int, desc: Optional[Dict[str, Any]]) -> int:
    """
    Enter Ship Menu (custom): appears when a boardable ship is present.
    Boardable rule (client-side): owner is missing/empty or equals 'derelict' (case-insensitive).
    """
    data = get_data(desc or {})
    ships = data.get("ships") or []

    # Filter potential targets
    boardable = []
    for s in ships:
        owner = (s.get("owner") or "").strip().lower()
        if not owner or owner == "derelict":
            boardable.append(s)

    if not boardable:
        print("No boardable ships here.")
        return current_sector

    # Let player pick a target (if more than one)
    print("\nBoardable ships in this sector:")
    for idx, s in enumerate(boardable, 1):
        nm = s.get("name") or s.get("ship_name") or "Unnamed"
        tp = s.get("ship_type") or s.get("type") or "?"
        ow = s.get("owner") or "derelict"
        print(f" {idx}) {nm}  [{tp}]  owner={ow}")

    target = boardable[0]
    if len(boardable) > 1:
        sel = input("Choose ship (number): ").strip()
        try:
            i = int(sel)
            if 1 <= i <= len(boardable):
                target = boardable[i-1]
        except ValueError:
            pass

    ship_id = target.get("id") or target.get("ship_id")

    while True:
        print("\n--- Enter Ship Menu ---")
        print(" (I) Inspect Ship")
        print(" (B) Board / Claim")
        print(" (R) Rename / Re-register")
        print(" (P) Set Primary")
        print(" (Q) Quit to Main Menu")
        cmd = input("Enter-Ship Command: ").strip().lower()

        if cmd == "q":
            return current_sector

        elif cmd == "i":
            try:
                r = conn.rpc("ship.inspect", {"ship_id": ship_id})
            except Exception:
                r = {"status": "ok", "type": "ship.inspect", "data": target}
            print(json.dumps(r, ensure_ascii=False, indent=2))

        elif cmd == "b":
            try:
                r = conn.rpc("ship.board", {"ship_id": ship_id})
                print(json.dumps(r, ensure_ascii=False, indent=2))
                _ = describe_sector(conn, current_sector)
                return current_sector
            except Exception as e:
                print(f"Board failed: {e}")

        elif cmd == "r":
            new_name = input("New ship name (registration): ").strip()
            if not new_name:
                print("Name cannot be empty.")
                continue
            try:
                r = conn.rpc("ship.rename", {"ship_id": ship_id, "name": new_name})
                print(json.dumps(r, ensure_ascii=False, indent=2))
                _ = describe_sector(conn, current_sector)
            except Exception as e:
                print(f"Rename failed: {e}")

        elif cmd == "p":
            try:
                r = conn.rpc("ship.set_primary", {"ship_id": ship_id})
                print(json.dumps(r, ensure_ascii=False, indent=2))
                _ = describe_sector(conn, current_sector)
            except Exception as e:
                print(f"Set Primary failed: {e}")

        else:
            print("Invalid command. Please try again.")


def render_sector_view(desc: Dict[str, Any]) -> None:
    """Print the canonical sector header block used at login, re-display, and after moves."""
    sid, name, warps = extract_sector_overview(desc)
    data = get_data(desc) or {}
    adj = data.get("adjacent") or warps or []
    ports = data.get("ports") or []
    ships = data.get("ships") or []
    planets = data.get("planets") or []
    beacon = data.get("beacon")

    print(f"\nYou are in sector {sid} — {name}.")
    print(f"Adjacent sectors: {', '.join(map(str, adj)) if adj else 'none'}")

    if ports:
        port_lines = []
        for p in ports:
            pname = p.get("name") or "Unknown"
            ptype = p.get("type") or "?"
            # show ONLY name and type to stay consistent
            port_lines.append(f"{pname} ({ptype})")
        print("Ports here: " + ", ".join(port_lines))
    else:
        print("Ports here: none")


    if ships:
        names = []
        for s in ships:
            nm = s.get("name") or s.get("ship_name") or s.get("player_name") or "Unnamed"
            names.append(nm)
        print("Ships here: " + ", ".join(names))
    else:
        print("Ships here: none")

    if planets:
        print("Planets: " + ", ".join(pl.get("name") or "Unnamed" for pl in planets))
    else:
        print("Planets: none")

    print("Beacon: " + (beacon if beacon else "none"))



def get_my_player_name(conn) -> Optional[str]:
    """Best-effort extraction of the player's display name."""
    try:
        r = conn.rpc("player.my_info", {})
        d = get_data(r) or {}
    except Exception:
        return None

    for key in ("name", "player_name", "display_name", "username"):
        val = d.get(key)
        if isinstance(val, str) and val.strip():
            return val.strip()
    for key in ("player", "me"):
        obj = d.get(key)
        if isinstance(obj, dict):
            for k in ("name", "player_name", "display_name", "username"):
                val = obj.get(k)
                if isinstance(val, str) and val.strip():
                    return val.strip()
    return None



def get_my_ship_id(conn) -> Optional[int]:
    """Best-effort extraction of the player's current ship id from player.my_info."""
    try:
        r = conn.rpc("player.my_info", {})
        d = get_data(r) or {}
    except Exception:
        return None

    # Common shapes to try:
    for key in ("ship_id", "current_ship_id"):
        if isinstance(d.get(key), int):
            return d[key]

    # Nested structures we might see:
    for key in ("ship", "current_ship", "vessel", "active_ship"):
        obj = d.get(key)
        if isinstance(obj, dict):
            sid = obj.get("id") or obj.get("ship_id")
            if isinstance(sid, int):
                return sid

    # Last resort: scan shallow dict values for an object that looks like a ship
    for v in d.values():
        if isinstance(v, dict) and ("type" in v or "ship_type" in v):
            sid = v.get("id") or v.get("ship_id")
            if isinstance(sid, int):
                return sid
    return None


def has_boardable_ship(ships, my_ship_id: Optional[int] = None, my_name: Optional[str] = None) -> bool:
    """
    True if there is a boardable ship (owner empty/None/'derelict'), excluding:
      - your current ship (id == my_ship_id)
      - ships owned by you (owner == my_name)
    """
    my_name_lc = (my_name or "").strip().lower()
    for s in ships or []:
        sid = s.get("id") or s.get("ship_id")
        owner_lc = (s.get("owner") or "").strip().lower()
        is_derelict = (not owner_lc) or owner_lc == "derelict"
        if not is_derelict:
            continue
        if my_ship_id is not None and isinstance(sid, int) and sid == my_ship_id:
            continue
        if my_name_lc and owner_lc == my_name_lc:
            continue
        return True
    return False

def can_set_beacon_here(desc: Dict[str, Any]) -> bool:
    """Quick client-side check: not FedSpace (1–10) and no existing beacon."""
    data = desc.get("data", {}) or {}
    sid = data.get("sector_id")
    has_beacon = bool(data.get("has_beacon")) or bool(data.get("beacon"))
    if isinstance(sid, int) and 1 <= sid <= 10:
        return False
    return not has_beacon

def _extract_beacon_count(d: Dict[str, Any]) -> Optional[int]:
    """Try common shapes that might hold the player's marker beacon count."""
    # Flat keys (most likely)
    for k in ("beacons", "beacon_count", "marker_beacons", "marker_beacon_count"):
        v = d.get(k)
        if isinstance(v, int):
            return v
    # Nested inventory dicts
    for k in ("inventory", "cargo", "hold", "items", "equipment", "hardware"):
        inv = d.get(k)
        if isinstance(inv, dict):
            for key in ("beacon", "beacons", "marker_beacon", "marker_beacons"):
                v = inv.get(key)
                if isinstance(v, int):
                    return v
    # Sometimes the server nests under 'player' or 'me'
    for outer in ("player", "me"):
        obj = d.get(outer)
        if isinstance(obj, dict):
            c = _extract_beacon_count(obj)
            if isinstance(c, int):
                return c
    return None


def get_my_beacon_count(conn) -> Optional[int]:
    """RPC to fetch player.my_info and extract marker beacon count, if available."""
    try:
        r = conn.rpc("player.my_info", {})
        d = r.get("data") or {}
        return _extract_beacon_count(d)
    except Exception:
        return None

def set_beacon_flow(conn: Conn, current_sector: int, last_desc: Dict[str, Any]) -> int:
    data = last_desc.get("data", {}) or {}
    sid = data.get("sector_id")
    name = data.get("name") or ""
    has_beacon = bool(data.get("has_beacon")) or bool(data.get("beacon"))
    beacon_text = data.get("beacon") or None

    if not isinstance(sid, int):
        print("Can't determine sector id.")
        return current_sector

    # FedSpace prohibition (canon)
    if 1 <= sid <= 10:
        print("FedSpace (1–10): You cannot set a beacon here.")
        return current_sector

    # NEW: query and show my beacon count; block if zero
    my_count = get_my_beacon_count(conn)
    if isinstance(my_count, int):
        print(f"Marker beacons aboard: {my_count}")
        if my_count <= 0:
            print("You have no marker beacons. Buy one at StarDock (Hardware) or acquire one by other means.")
            return current_sector

    # Canon warning if one already exists (explode both)
    if has_beacon:
        print(f"A beacon already exists here: {beacon_text!r}")
        yn = input("Launching another will destroy BOTH beacons, leaving NO beacon. Proceed? (y/N): ").strip().lower()
        if yn != "y":
            print("Cancelled.")
            return current_sector

    print(f"\nSet beacon for sector {sid} — {name}")
    text = input("Beacon text (max 80 chars, blank to cancel): ").strip()
    if not text:
        print("Cancelled.")
        return current_sector
    if len(text) > 80:
        print("Too long (max 80).")
        return current_sector

    try:
        resp = conn.rpc("sector.set_beacon", {"sector_id": sid, "text": text})
        if isinstance(resp, dict) and resp.get("status") == "ok" and resp.get("type") == "sector.info" :
            globals()["last_desc"] = resp  # refresh view on next loop
            # If the server’s meta.message is present, show it; otherwise show generic success
            meta = resp.get("meta") or {}
            msg = meta.get("message")
            print(msg or "Beacon deployed.")
        else:
            print(json.dumps(resp, ensure_ascii=False, indent=2))
    except Exception as e:
        print(f"Set beacon failed: {e}")

    return current_sector



def main_menu(conn: Conn, current_sector: int) -> int:
    global last_desc

    # Ensure we have fresh sector info and render the canonical header
    if not last_desc:
        last_desc = describe_sector(conn, current_sector)
    render_sector_view(last_desc)

    data = last_desc.get("data", {}) or {}
    ships = data.get("ships") or []

    my_ship_id = get_my_ship_id(conn)
    my_name    = get_my_player_name(conn)

    boardable_exists = has_boardable_ship(ships, my_ship_id=my_ship_id, my_name=my_name)
    towable_exists   = has_tow_target(ships, my_ship_id=my_ship_id)
    can_beacon    = can_set_beacon_here(last_desc)  # FedSpace check only
    beacon_count  = get_my_beacon_count(conn)       # may be None if server doesn't expose it
    beacon_label  = "(R) Release Beacon"
    
    if isinstance(beacon_count, int):
        beacon_label = f"(R) Release Beacon [{beacon_count}]"

    def _menu_row(left: str, right: str, width: int = 28):
        print(f" {left:<{width}}| {right}")

    rows = [
        ("(M) Move to a Sector",       "(D) Re-display Sector"),
        ("(P) Port & Trade",           "(L) Land on a Planet"),
        ("(C) Ship's Computer",        "(V) View Game Status"),
    ]
    right4 = []
    if towable_exists:
        right4.append("(W) Tow SpaceCraft")
    if boardable_exists:
        right4.append("(E) Enter Ship*")
    rows.append((beacon_label, "   ".join(right4)))  # ← use the label with count
    rows.append(("(Y) Testing Menu", "(H) Help"))
    rows.append(("(Q) Quit", ""))


    print(f"\n--- Main Menu (Sector {current_sector}) ---")
    for left, right in rows:
        _menu_row(left, right)

    cmd = input("Command: ").strip().lower()

    if cmd == "q":
        return -1

    elif cmd == "m":
        to_sector_id_str = input("Move to sector: ").strip()
        try:
            to_sector_id = int(to_sector_id_str)
            # Validate adjacency from current last_desc
            cur_data = last_desc.get("data", {}) or {}
            warps = cur_data.get("adjacent") or []
            if to_sector_id not in warps:
                print(f"{to_sector_id} is not adjacent. Valid: {', '.join(map(str, warps)) if warps else '(none)'}")
            else:
                moved = conn.rpc("move.warp", {"to_sector_id": to_sector_id})
                # Now refresh last_desc but don't print here; next loop prints header+menu
                last_desc = describe_sector(conn, to_sector_id)
                # Update sector id for the next loop
                md = last_desc.get("data", {}) or {}
                sid = md.get("sector_id")
                if isinstance(sid, int):
                    current_sector = sid
                else:
                    current_sector = to_sector_id
        except ValueError:
            print("Invalid sector ID.")
        except Exception as e:
            print(f"An error occurred: {e}")

    elif cmd == "d":
        last_desc = describe_sector(conn, current_sector)

    elif cmd == "p":
        current_sector = trade_menu(conn, current_sector)

    elif cmd == "l":
        current_sector = planet_menu(conn, current_sector)

    elif cmd == "c":
        print("Not Implemented: Ship's Computer")

    elif cmd == "v":
        print("Not Implemented: View Game Status")

    elif cmd == "r":
        current_sector = set_beacon_flow(conn, current_sector, last_desc)

    elif cmd == "w":
        if not towable_exists:
            print("No towable targets in this sector.")
        else:
            print("Not Implemented: Tow SpaceCraft")

    elif cmd == "e":
        if not boardable_exists:
            print("No boardable ships in this sector.")
        else:
            current_sector = enter_ship_menu(conn, current_sector, last_desc)
            # enter_ship_menu may change ownership/ships; header will refresh next loop

    elif cmd == "y":
        current_sector = testing_menu(conn, current_sector)

    elif cmd == "h":
        print("--- Help ---")
        print("M: Move to an adjacent sector")
        print("D: Re-display current sector info")
        print("P: Port & Trade (port/stardock flows inside)")
        print("L: Land on a Planet (opens Planet Menu)")
        print("C: Ship's Computer (canon submenu, TBD)")
        print("V: View Game Status (canon)")
        print("R: Release Beacon (canon)")
        if towable_exists:
            print("W: Tow SpaceCraft (canon) — shown only when another ship is present")
        if boardable_exists:
            print("E: Enter Ship (custom) — shown only when a boardable derelict is present (not yours)")
        print("Y: Testing/Developer menu (custom)")
        print("Q: Quit and disconnect")

    else:
        print("Invalid command. Please try again.")

    return current_sector


def has_tow_target(ships, my_ship_id=None) -> bool:
    # Simplest rule: show Tow if there is ANY ship here that isn't your own.
    # If you can fetch the player’s current ship id from session, pass it in.
    for s in ships or []:
        sid = s.get("id") or s.get("ship_id")
        if sid is None or sid != my_ship_id:
            return True
    return False


def _menu_row(left: str, right: str, width: int = 28):
    print(f" {left:<{width}}| {right}")



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
        names = []
        for s in ships:
            nm = s.get("name") or s.get("ship_name") or s.get("player_name") or "Unnamed"
            names.append(nm)
        print("Ships here:", ", ".join(names))
    else:
        print("Ships here: none")

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
