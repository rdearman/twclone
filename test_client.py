#!/usr/bin/env python3
"""
Interactive TW JSON client

Protocol used:
  - Request envelope: {"id":"<cli-seq>", "command":"<action>", "data":{...}}
  - session.ping / session.hello
  - auth.login expects {"user_name": "...", "password": "..."} (fallback: {"player_name": "...", "password": "..."})
  - move.describe_sector expects/accepts {"sector_id": <int>}
  - move.warp expects {"to_sector_id": <int>}

Features:
  - Shows sector name, adjacent sectors, ports, players, and beacons (if provided by server)
  - Client enforces adjacency before warping
  - Debug mode prints raw send/recv lines
  - Extra commands: 'raw', 'raw desc <id>' to dump server replies

Commands:
  help                 Show help
  where                Show current sector id
  desc                 Re-describe current sector (and list warps/ports/players/beacons)
  desc <id>            Peek at another sector's info & warps without moving
  warps                List adjacent sector ids for current sector
  move <id>            Move to adjacent sector <id>
  raw                  Dump last describe JSON
  raw desc <id>        Dump raw JSON for that sector
  q | quit | exit      Quit
"""

import argparse
import getpass
import json
import socket
import sys
from typing import Any, Dict, List, Optional, Tuple

HOST, PORT = "127.0.0.1", 1234

# -------------- transport --------------

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
            print(f"[DBG->] {line.strip()}")
        self.sock.sendall(line.encode("utf-8"))

    def recv(self, timeout: float = 15.0) -> Dict[str, Any]:
        self.sock.settimeout(timeout)
        line = self._r.readline()
        if not line:
            raise ConnectionError("Server closed connection.")
        if self.debug:
            print(f"[DBG<-] {line.strip()}")
        return json.loads(line)

    def rpc(self, command: str, data: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        env = {"id": self._next_id(), "command": command, "data": data if isinstance(data, dict) else {}}
        self.send(env)
        return self.recv()

# -------------- helpers --------------

def get_data(obj: Dict[str, Any]) -> Dict[str, Any]:
    d = obj.get("data")
    return d if isinstance(d, dict) else {}

def extract_current_sector(obj: Dict[str, Any]) -> Optional[int]:
    node = get_data(obj)
    cur = node.get("current_sector")
    if isinstance(cur, int):
        return cur
    for k in ("ship", "player", "sector"):
        sub = node.get(k)
        if isinstance(sub, dict):
            sid = sub.get("sector_id") or sub.get("id")
            if isinstance(sid, int):
                return sid
    return None

def _normalise_warp_list(warps: Any) -> List[int]:
    ids: List[int] = []
    if isinstance(warps, list):
        for w in warps:
            if isinstance(w, int):
                ids.append(w)
            elif isinstance(w, dict):
                # accept several common keys
                for key in ("to_sector_id", "to", "dest", "destination", "sector_id", "id"):
                    v = w.get(key)
                    if isinstance(v, int):
                        ids.append(v)
                        break
    return ids

def extract_sector_overview(desc: Dict[str, Any]) -> Tuple[Optional[int], Optional[str], List[int]]:
    """
    Extract (sector_id, name, warps[int]) with generous schema tolerance.
    Prefers flat fields; also checks nested {sector:{...}}.
    Warps keys tried: adjacent, warps, links, neighbors/neighbours, warps_out.
    """
    node = get_data(desc)
    sid = node["sector_id"] if isinstance(node.get("sector_id"), int) else None
    name = node["name"] if isinstance(node.get("name"), str) else None

    warp_candidates = []
    for k in ("adjacent", "warps", "links", "neighbors", "neighbours", "warps_out"):
        if k in node:
            warp_candidates.append(node[k])

    sec = node.get("sector")
    if isinstance(sec, dict):
        if sid is None and isinstance(sec.get("id"), int):
            sid = sec["id"]
        if name is None and isinstance(sec.get("name"), str):
            name = sec["name"]
        for k in ("adjacent", "warps", "links", "neighbors", "neighbours", "warps_out"):
            if k in sec:
                warp_candidates.append(sec[k])

    warp_ids: List[int] = []
    for cand in warp_candidates:
        warp_ids.extend(_normalise_warp_list(cand))

    # dedupe, preserve order
    seen = set()
    warp_ids = [x for x in warp_ids if (x not in seen and not seen.add(x))]
    return sid, name, warp_ids

def print_banner(host: str, port: int) -> None:
    print("=" * 64)
    print("TW Interactive Test Client")
    print(f"Server: {host}:{port}")
    print("=" * 64)

def print_help() -> None:
    print("Commands:")
    print("  help                 Show this help")
    print("  where                Show current sector id")
    print("  desc                 Re-describe current sector (and list warps/ports/players/beacons)")
    print("  desc <id>            Peek at another sector's info & warps without moving")
    print("  warps                List adjacent sector ids")
    print("  move <id>            Move to adjacent sector <id>")
    print("  raw                  Dump last describe JSON")
    print("  raw desc <id>        Dump raw JSON for that sector")
    print("  q | quit | exit      Quit")

def show_extras(desc: Dict[str, Any]) -> None:
    node = get_data(desc)

    # Ports
    ports = node.get("ports") or []
    if isinstance(ports, list) and ports:
        names = [p.get("name") for p in ports if isinstance(p, dict) and isinstance(p.get("name"), str)]
        if names:
            print(f"Ports here: {', '.join(names)}")

    # Players
    players = node.get("players") or []
    if isinstance(players, list) and players:
        pnames = [p.get("name") for p in players if isinstance(p, dict) and isinstance(p.get("name"), str)]
        if pnames:
            print(f"Players here: {', '.join(pnames)}")

    # Planets  ← NEW
    planets = node.get("planets") or []
    if isinstance(planets, list) and planets:
        pnames = [p.get("name") for p in planets if isinstance(p, dict) and isinstance(p.get("name"), str)]
        if pnames:
            print(f"Planets here: {', '.join(pnames)}")

    # Beacons (always show something)
    beacons = node.get("beacons")
    if isinstance(beacons, list):
        if beacons:
            print("Beacons:")
            for b in beacons:
                if isinstance(b, dict):
                    msg = b.get("message")
                    if isinstance(msg, str) and msg.strip():
                        print(f"  - {msg.strip()}")

    # -------------- interactive --------------

def describe_sector(conn: Conn, sector_id: int) -> Dict[str, Any]:
    # Always send sector explicitly so server returns adjacency & extras
    return conn.rpc("move.describe_sector", {"sector_id": sector_id})

def interactive_loop(conn: Conn, current_sector: int) -> None:
    last_desc: Optional[Dict[str, Any]] = None

    # Initial describe
    desc = describe_sector(conn, current_sector)
    sid, name, warps = extract_sector_overview(desc)
    if isinstance(sid, int):
        current_sector = sid
    last_desc = desc

    print(f"\nYou are in sector {current_sector}{' — ' + name if name else ''}.")
    print(f"Adjacent sectors: {', '.join(map(str, warps)) if warps else '(none)'}")
    show_extras(desc)
    print("\nType 'help' for commands.")

    while True:
        try:
            line = input("> ").strip()
        except (EOFError, KeyboardInterrupt):
            print("\nExiting.")
            return

        if not line:
            line = "desc"

        parts = line.split()
        cmd = parts[0].lower()

        if cmd in ("q", "quit", "exit"):
            print("Goodbye.")
            return

        elif cmd == "help":
            print_help()

        elif cmd == "where":
            print(f"Current sector: {current_sector}")

        elif cmd == "raw":
            if len(parts) == 1:
                if last_desc is None:
                    print("(no last describe yet)")
                else:
                    print(json.dumps(last_desc, ensure_ascii=False, indent=2))
            elif len(parts) == 3 and parts[1].lower() == "desc":
                try:
                    look = int(parts[2])
                except ValueError:
                    print("Usage: raw desc <sector_id>")
                    continue
                raw = describe_sector(conn, look)
                print(json.dumps(raw, ensure_ascii=False, indent=2))
            else:
                print("Usage: raw | raw desc <sector_id>")

        elif cmd == "desc":
            # Optional: desc <id> to peek elsewhere
            if len(parts) == 2:
                try:
                    look = int(parts[1])
                except ValueError:
                    print("Usage: desc [sector_id]")
                    continue
                peek = describe_sector(conn, look)
                _, n2, w2 = extract_sector_overview(peek)
                print(f"Sector {look}{' — ' + n2 if n2 else ''}")
                print(f"Adjacent: {', '.join(map(str, w2)) if w2 else '(none)'}")
                show_extras(peek)
                continue

            desc = describe_sector(conn, current_sector)
            sid, name, warps = extract_sector_overview(desc)
            if isinstance(sid, int):
                current_sector = sid
            last_desc = desc
            print(f"\nSector {current_sector}{' — ' + name if name else ''}")
            print(f"Adjacent sectors: {', '.join(map(str, warps)) if warps else '(none)'}")
            show_extras(desc)

        elif cmd == "warps":
            if last_desc is None:
                last_desc = describe_sector(conn, current_sector)
            _, _, warps = extract_sector_overview(last_desc)
            print(f"Adjacent sectors: {', '.join(map(str, warps)) if warps else '(none)'}")

        elif cmd == "move":
            if len(parts) < 2:
                print("Usage: move <sector_id>")
                continue
            try:
                target = int(parts[1])
            except ValueError:
                print("Sector id must be an integer.")
                continue

            # Refresh describe and enforce adjacency
            last_desc = describe_sector(conn, current_sector)
            _, _, warps = extract_sector_overview(last_desc)
            if not warps:
                print("No adjacency data available; try 'desc' or 'raw' to inspect.")
                continue
            if target not in warps:
                print(f"{target} is not adjacent. Valid: {', '.join(map(str, warps))}")
                continue

            moved = conn.rpc("move.warp", {"to_sector_id": target})
            new_cur = extract_current_sector(moved)
            current_sector = new_cur if isinstance(new_cur, int) else target

            # Re-describe
            last_desc = describe_sector(conn, current_sector)
            sid, name, warps = extract_sector_overview(last_desc)
            if isinstance(sid, int):
                current_sector = sid
            print(f"\nArrived sector {current_sector}{' — ' + name if name else ''}.")
            print(f"Adjacent sectors: {', '.join(map(str, warps)) if warps else '(none)'}")
            show_extras(last_desc)

        else:
            print("Unknown command. Type 'help'.")

# -------------- main --------------

def main() -> int:
    ap = argparse.ArgumentParser(description="Interactive TW JSON client")
    ap.add_argument("--host", default=HOST)
    ap.add_argument("--port", type=int, default=PORT)
    ap.add_argument("--debug", action="store_true", help="print raw send/recv lines")
    args = ap.parse_args()

    user = input("Username: ").strip()
    passwd = getpass.getpass("Password: ")

    print_banner(args.host, args.port)

    try:
        with socket.create_connection((args.host, args.port), timeout=10.0) as sock:
            conn = Conn(sock, debug=args.debug)

            print("[session] ping:", json.dumps(conn.rpc("session.ping", {}), ensure_ascii=False))
            print("[session] hello:", json.dumps(conn.rpc("session.hello", {}), ensure_ascii=False))

            login = conn.rpc("auth.login", {"user_name": user, "password": passwd})
            print("[auth] login:", json.dumps(login, ensure_ascii=False))
            if login.get("status") in ("error", "refused"):
                # Optional fallback accepted by some builds
                fallback = conn.rpc("auth.login", {"player_name": user, "password": passwd})
                print("[auth] login (fallback):", json.dumps(fallback, ensure_ascii=False))
                login = fallback

            if login.get("status") in ("error", "refused"):
                print("Login failed.")
                return 2

            current = extract_current_sector(login)
            if not isinstance(current, int):
                info = conn.rpc("player.my_info", {})
                print("[auth] player.info:", json.dumps(info, ensure_ascii=False))
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
        sys.exit(130)
