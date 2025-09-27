
#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
test_clientv3.py — Data-driven menu client for TWClone (server-connected, v2 parity)

- Reads menus from menus.json (or uses built-in fallback)
- Generic menu engine (submenu/back/rpc/pycall/flow/post/help)
- Real socket Conn (v2-compatible), login, sector fetch & normalization
- Handlers ported/aligned with v2: redisplay header, beacon flow, enter-ship,
  testing (full), adjacency-aware move, tow placeholder, help, shipyard, warp,
  autopilot route (add/clear/start), intercept, land, computer tools.
"""
from __future__ import annotations
import argparse
import getpass
import json
import os
import re
import socket
import sys
from dataclasses import dataclass, field
from typing import Any, Dict, Callable, List, Optional

# ---------------------------
# CLI defaults (match v2)
# ---------------------------
HOST = "127.0.0.1"
PORT = 1234

def parse_args():
    p = argparse.ArgumentParser(description="Trade Wars 2002 interactive client (v3).")
    p.add_argument("--host", default=HOST, help=f"Server host (default: {HOST})")
    p.add_argument("--port", type=int, default=PORT, help=f"Server port (default: {PORT})")
    p.add_argument("--user", help="Player name for login.")
    p.add_argument("--passwd", help="Password for login.")
    p.add_argument("--debug", action="store_true", help="Enable debug output")
    p.add_argument("--menus", default="./menus.json",
                   help="Path to menus.json (default: ./menus.json)")
    return p.parse_args()

# ---------------------------
# Handler registry
# ---------------------------
HANDLERS: Dict[str, Callable] = {}

def register(name: str):
    def deco(fn: Callable):
        HANDLERS[name] = fn
        return fn
    return deco

def call_handler(name: str, ctx: "Context", *args, **kwargs):
    fn = HANDLERS.get(name)
    if not fn:
        print(f"[NYI] Action '{name}' isn’t implemented in the client yet.")
        return None
    return fn(ctx, *args, **kwargs)

# ---------------------------
# Conn (ported from v2)
# ---------------------------
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
            resp = self.recv()
            if resp.get("reply_to") == req_id or resp.get("status") in ("error", "refused"):
                return resp
            print(f"[WARN] Ignoring frame id={resp.get('id')} reply_to={resp.get('reply_to')}")

# ---------------------------
# Context
# ---------------------------
@dataclass
class Context:
    conn: Conn
    menus: Dict[str, Any]
    menu_stack: List[str] = field(default_factory=lambda: ["MAIN"])
    last_sector_desc: Dict[str, Any] = field(default_factory=dict)
    player_info: Dict[str, Any] = field(default_factory=dict)
    state: Dict[str, Any] = field(default_factory=dict)

    @property
    def current_menu(self) -> str:
        return self.menu_stack[-1] if self.menu_stack else "MAIN"

    @property
    def current_sector_id(self) -> Optional[int]:
        return self.last_sector_desc.get("id")

    def push(self, menu_id: str):
        self.menu_stack.append(menu_id)

    def pop(self):
        if len(self.menu_stack) > 1:
            self.menu_stack.pop()

# ---------------------------
# Flags & helpers
# ---------------------------
def get_data(resp: Dict[str, Any]) -> Dict[str, Any]:
    return (resp or {}).get("data") or {}

def extract_current_sector(resp: Dict[str, Any]) -> Optional[int]:
    d = get_data(resp)
    for k in ("current_sector", "sector_id"):
        v = d.get(k)
        if isinstance(v, int):
            return v
    sess = d.get("session") or {}
    if isinstance(sess.get("current_sector"), int):
        return sess["current_sector"]
    # player.my_info nested style
    pl = d.get("player") or {}
    ship = d.get("ship") or {}
    if isinstance(ship.get("location"), dict) and isinstance(ship["location"].get("sector_id"), int):
        return ship["location"]["sector_id"]
    return None

def normalize_sector(d: Dict[str, Any]) -> Dict[str, Any]:
    """Convert server sector data to the simplified shape used by v3 flags/menus."""
    sid = d.get("sector_id") or (d.get("sector") or {}).get("id") or d.get("id")
    name = d.get("name") or (d.get("sector") or {}).get("name") or (f"Sector {sid}" if sid else "Unknown")
    adj = d.get("adjacent") or d.get("adjacent_sectors") or d.get("neighbors") or []
    # Port/class synthesis
    port_obj = {}
    ports = d.get("ports") or []
    if ports and isinstance(ports, list):
        p0 = ports[0]
        pcls = p0.get("class") or p0.get("type")
        if isinstance(pcls, int) or (isinstance(pcls, str) and pcls.isdigit()):
            port_obj = {"class": int(pcls)}
        if p0.get("name"):
            port_obj["name"] = p0["name"]
    elif isinstance(d.get("port"), dict):
        port_obj = dict(d.get("port"))
        if "class" not in port_obj and isinstance(port_obj.get("type"), int):
            port_obj["class"] = port_obj["type"]
    # ships/planets/beacon
    planets = d.get("planets") or []
    ships = d.get("ships") or d.get("entities", {}).get("ships") or []
    beacon = d.get("beacon") or d.get("beacon_text") or ""
    return {
        "id": sid, "name": name,
        "adjacent": adj,
        "port": port_obj,
        "planets": planets,
        "ships": ships,
        "beacon": beacon
    }

def get_my_player_name(conn: Conn) -> Optional[str]:
    try:
        d = get_data(conn.rpc("player.my_info", {}))
    except Exception:
        return None
    for key in ("name","player_name","display_name","username"):
        v = d.get(key)
        if isinstance(v, str) and v.strip():
            return v.strip()
    for objk in ("player","me"):
        obj = d.get(objk)
        if isinstance(obj, dict):
            for key in ("name","player_name","display_name","username"):
                v = obj.get(key)
                if isinstance(v, str) and v.strip():
                    return v.strip()
    return None

def get_my_ship_id(conn: Conn) -> Optional[int]:
    try:
        d = get_data(conn.rpc("player.my_info", {}))
    except Exception:
        return None
    for k in ("ship_id","current_ship_id"):
        if isinstance(d.get(k), int):
            return d[k]
    for objk in ("ship","current_ship","vessel","active_ship"):
        obj = d.get(objk)
        if isinstance(obj, dict):
            sid = obj.get("id") or obj.get("ship_id")
            if isinstance(sid, int):
                return sid
    ship = d.get("ship") or {}
    loc = ship.get("location") or {}
    if isinstance(loc.get("sector_id"), int):
        return ship.get("id")
    return None

def _extract_beacon_count(d: Dict[str, Any]) -> Optional[int]:
    for k in ("beacons","beacon_count","marker_beacons","marker_beacon_count"):
        v = d.get(k)
        if isinstance(v, int): return v
    for k in ("inventory","cargo","hold","items","equipment","hardware"):
        inv = d.get(k)
        if isinstance(inv, dict):
            for kk in ("beacon","beacons","marker_beacon","marker_beacons"):
                v = inv.get(kk)
                if isinstance(v, int): return v
    for outer in ("player","me"):
        obj = d.get(outer)
        if isinstance(obj, dict):
            c = _extract_beacon_count(obj)
            if isinstance(c, int): return c
    return None

def get_my_beacon_count(conn: Conn) -> Optional[int]:
    try:
        d = get_data(conn.rpc("player.my_info", {}))
        return _extract_beacon_count(d)
    except Exception:
        return None

def has_boardable_ship(ships, my_ship_id: Optional[int] = None, my_name: Optional[str] = None) -> bool:
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

def has_tow_target(ships, my_ship_id=None) -> bool:
    for s in ships or []:
        sid = s.get("id") or s.get("ship_id")
        if sid is None or sid != my_ship_id:
            return True
    return False

def compute_flags(ctx: Context) -> Dict[str, bool]:
    d = ctx.last_sector_desc or {}
    ships = d.get("ships") or []
    my_ship_id = get_my_ship_id(ctx.conn)
    my_name = get_my_player_name(ctx.conn)
    # keep beacon count handy for label interpolation
    ctx.state["beacon_count"] = get_my_beacon_count(ctx.conn)

    flags = {
        "has_port": bool(d.get("port")),
        "is_stardock": bool(isinstance(d.get("port"), dict) and d.get("port", {}).get("class") == 9),
        "has_planet": bool(d.get("planets")),
        "can_set_beacon": (d.get("beacon") in (None, "")),
        "has_boardable": has_boardable_ship(ships, my_ship_id=my_ship_id, my_name=my_name),
        "has_tow_target": has_tow_target(ships, my_ship_id=my_ship_id),
        "on_planet": bool(ctx.state.get("on_planet")),
        "planet_has_products": bool(ctx.state.get("planet_products_available")),
        "is_corp_member": bool(ctx.player_info.get("corp_id")),
        "can_autopilot": bool(ctx.state.get("ap_route_plotted")),
    }
    return flags


@register("print_last_rpc")
def print_last_rpc(ctx):
    resp = ctx.state.get("last_rpc")
    if resp is None:
        print("[no response captured]")
        return
    print(json.dumps(resp, ensure_ascii=False, indent=2))
@register("disconnect_and_quit")

def disconnect_and_quit(ctx):
    try:
        resp = ctx.conn.rpc("system.disconnect", {})
        ctx.state["last_rpc"] = resp
        print(json.dumps(resp, ensure_ascii=False, indent=2))
    except Exception as e:
        print(f"disconnect error: {e}")
    sys.exit(0)



# ---------------------------
# Placeholder & prompt resolution
# ---------------------------
PROMPT_RE = re.compile(r"^<prompt:(int|float|str)(?::(.+))?>$")
CTX_RE     = re.compile(r"^<ctx:([a-zA-Z0-9_\.]+)>$")

def resolve_value(val: Any, ctx: Context) -> Any:
    if isinstance(val, dict):
        return {k: resolve_value(v, ctx) for k, v in val.items()}
    if isinstance(val, list):
        return [resolve_value(x, ctx) for x in val]
    if isinstance(val, str):
        m = PROMPT_RE.match(val)
        if m:
            typ, msg = m.group(1), (m.group(2) or "Enter value")
            raw = input(f"{msg}: ").strip()
            try:
                return int(raw) if typ == "int" else (float(raw) if typ == "float" else raw)
            except Exception:
                print(f"Invalid {typ}.")
                return None
        c = CTX_RE.match(val)
        if c:
            path = c.group(1).split(".")
            ref: Any = ctx
            for part in path:
                if isinstance(ref, Context) and hasattr(ref, part):
                    ref = getattr(ref, part)
                elif isinstance(ref, dict):
                    ref = ref.get(part)
                else:
                    ref = None
                if ref is None:
                    break
            return ref
        return val
    return val

# ---------------------------
# Menu engine
# ---------------------------
def _interp_label(label: str, ctx: Context) -> str:
    # Add beacon count to Release Beacon label, if known
    if label.strip().lower().startswith("(r) release beacon"):
        bc = ctx.state.get("beacon_count")
        if isinstance(bc, int):
            return f"{label} [{bc}]"
    return label

def render_menu(ctx: Context):
    menu = ctx.menus.get(ctx.current_menu)
    if not menu:
        print(f"[Error] Menu '{ctx.current_menu}' not found.")
        return
    flags = compute_flags(ctx)
    title = menu.get("title", ctx.current_menu)
    # Simple template replacement
    title = title.replace("{{sector_id}}", str(ctx.current_sector_id or "?"))
    port_name = (ctx.last_sector_desc.get("port") or {}).get("name") or "Port"
    title = title.replace("{{port_name}}", str(port_name))
    print("\n" + title)
    if "sections" in menu:
        for sec in menu["sections"]:
            if sec.get("name"):
                print(f"\n[{sec['name']}]")
            for opt in sec.get("options", []):
                if option_visible(opt, flags):
                    print(" ", _interp_label(opt["label"], ctx))
    else:
        for opt in menu.get("options", []):
            if option_visible(opt, flags):
                print(" ", _interp_label(opt["label"], ctx))

def option_visible(opt: Dict[str, Any], flags: Dict[str, bool]) -> bool:
    show_if = opt.get("show_if", [])
    hide_if = opt.get("hide_if", [])
    if show_if and not all(flags.get(k, False) for k in show_if):
        return False
    if hide_if and any(flags.get(k, False) for k in hide_if):
        return False
    return True

def read_choice() -> str:
    try:
        return input("\nCommand: ").strip().lower()
    except (EOFError, KeyboardInterrupt):
        return "q"

def handle_choice(ctx: Context, choice: str):
    menu = ctx.menus.get(ctx.current_menu, {})
    opts: List[Dict[str, Any]] = []
    if "sections" in menu:
        for sec in menu["sections"]:
            opts.extend(sec.get("options", []))
    else:
        opts = menu.get("options", [])
    match = None
    for opt in opts:
        if opt.get("key","").lower() == choice:
            match = opt
            break
    if not match:
        print("Unknown command.")
        return
    action = match.get("action", {})
    dispatch_action(ctx, action)

def dispatch_action(ctx: Context, action: Dict[str, Any]):
    if "submenu" in action:
        ctx.push(action["submenu"]); return
    if action.get("back"):
        ctx.pop(); return
    if "pycall" in action:
        call_handler(action["pycall"], ctx)
        if post := action.get("post"):
            call_handler(post, ctx)
        return
    if "flow" in action:
        call_handler(action["flow"], ctx)
        if post := action.get("post"):
            call_handler(post, ctx)
        return
    if "rpc" in action:
        payload = dict(action["rpc"])
        cmd = payload.get("command")
        data = resolve_value(payload.get("data", {}), ctx)
        if data is None:
            print("Cancelled."); return
        resp = ctx.conn.rpc(cmd, data)
        ctx.state["last_rpc"] = resp
        if post := action.get("post"):
            call_handler(post, ctx)
        return
    print("[Warn] No action defined.")

# ---------------------------
# Render sector header (canon-ish)
# ---------------------------
@register("redisplay_sector")
def redisplay_sector(ctx: Context):
    d = ctx.last_sector_desc or {}
    sid, name = d.get("id"), d.get("name")
    adj = d.get("adjacent") or []
    port = d.get("port")
    ships = d.get("ships") or []
    planets = d.get("planets") or []
    beacon = d.get("beacon")

    print(f"\nYou are in sector {sid}{' — ' + name if name else ''}.")
    print(f"Adjacent sectors: {', '.join(map(str, adj)) if adj else 'none'}")

    if port:
        cls = port.get("class", "?")
        pnm = port.get("name") or f"class {cls}"
        print(f"Ports here: {pnm}")
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

def _render_scan_card(scan: dict) -> str:
    """
    Pretty-print the server's move.scan response.
    Robust against partial/misaligned payloads (missing sector_id, flat counts, etc).
    """
    try:
        # Basic shape checks
        if not isinstance(scan, dict):
            return str(scan)

        # Error envelope -> show code/message
        if scan.get("status") == "error":
            err = scan.get("error") or {}
            code = err.get("code")
            msg = err.get("message") or "Unknown error"
            return f"\n── Fast Scan: ERROR {code} — {msg}"

        d = scan.get("data") or {}
        if not isinstance(d, dict):
            return "\n── Fast Scan: (no data)"

        # Core fields (tolerant fallbacks)
        sid = d.get("sector_id") or d.get("id")
        name = d.get("name") or "Unknown"

        sec = d.get("security") or {}
        fed = bool(sec.get("fedspace"))
        safe = bool(sec.get("safe_zone"))
        locked = bool(sec.get("combat_locked"))

        adj = d.get("adjacent") or []
        if not isinstance(adj, list):
            adj = []

        port = d.get("port") or {}
        port_present = bool(port.get("present"))

        # Counts: prefer nested object; else reconstruct from flat fields
        counts = d.get("counts")
        if isinstance(counts, dict):
            ships = int(counts.get("ships") or 0)
            planets = int(counts.get("planets") or 0)
            mines = int(counts.get("mines") or 0)
            fighters = int(counts.get("fighters") or 0)
        else:
            ships = int((d.get("ships") or d.get("ship_count") or d.get("ships_count") or 0) or 0)
            planets = int((d.get("planets") or d.get("planet_count") or d.get("planets_count") or 0) or 0)
            mines = int(d.get("mines") or 0)
            fighters = int(d.get("fighters") or 0)

        beacon = d.get("beacon")
        # normalise beacon: allow string or explicit null
        beacon_str = beacon if isinstance(beacon, str) and beacon.strip() else None

        # Render
        lines = []
        lines.append(f"\n── Fast Scan: Sector {sid if sid is not None else '∅'} — {name}")
        lines.append(f"   Adjacent: {', '.join(map(str, adj)) if adj else 'none'}")
        lines.append(f"   Security: fedspace={fed}, safe_zone={safe}, combat_locked={locked}")
        lines.append(f"   Port: {'present' if port_present else 'none'}")
        lines.append(f"   Counts: ships={ships}, planets={planets}, mines={mines}, fighters={fighters}")
        lines.append(f"   Beacon: {beacon_str if beacon_str else 'none'}")
        return "\n".join(lines)

    except Exception as e:
        # Last-resort: dump raw for debugging
        try:
            import json
            return "\n── Fast Scan (raw) ──\n" + json.dumps(scan, ensure_ascii=False, indent=2)
        except Exception:
            return f"\n── Fast Scan: <unrenderable> ({e})"


    
@register("scan_sector")
def scan_sector(ctx: "Context"):
    # Call the server
    resp = ctx.conn.rpc("move.scan", {})
    ctx.state["last_rpc"] = resp

    # Pretty print the scan
    try:
        print(_render_scan_card(resp))
    except Exception:
        print(json.dumps(resp, ensure_ascii=False, indent=2))

    # Optional: lightly merge a few fields into our cached view WITHOUT
    # wiping richer objects from describe_sector (ships/planets lists, etc.)
    try:
        d = (resp or {}).get("data") or {}
        if not isinstance(d, dict):
            return
        cur = ctx.last_sector_desc or {}
        merged = dict(cur)

        if isinstance(d.get("sector_id"), int):
            merged["id"] = d["sector_id"]
        if isinstance(d.get("name"), str):
            merged["name"] = d["name"]
        if isinstance(d.get("adjacent"), list):
            merged["adjacent"] = d["adjacent"]

        # Represent scan’s port presence as a minimal port object
        port = d.get("port") or {}
        if isinstance(port, dict):
            merged["port"] = {"present": bool(port.get("present"))}
            # keep old 'class' if we had one previously
            if isinstance(cur.get("port"), dict) and "class" in cur["port"]:
                merged["port"]["class"] = cur["port"]["class"]

        # Beacon string if present
        if d.get("beacon"):
            merged["beacon"] = d["beacon"]

        ctx.last_sector_desc = merged
    except Exception:
        pass


    
# ---------------------------
# Help
# ---------------------------
@register("help_main")
def help_main(ctx: Context):
    d = ctx.last_sector_desc or {}
    ships = d.get("ships") or []
    towable_exists = has_tow_target(ships, my_ship_id=get_my_ship_id(ctx.conn))
    boardable_exists = has_boardable_ship(ships, my_ship_id=get_my_ship_id(ctx.conn), my_name=get_my_player_name(ctx.conn))

    print("--- Help ---")
    print("M: Move to a sector")
    print("D: Re-display current sector info")
    print("P: Port & Trade (port/stardock flows inside)")
    print("L: Land on a Planet (opens Planet Menu)")
    print("C: Ship's Computer (canon submenu)")
    print("V: View Game Status")
    print("R: Release Beacon")
    if towable_exists:
        print("W: Tow SpaceCraft — shown only when another ship is present")
    if boardable_exists:
        print("E: Enter Ship — shown only when a boardable derelict is present (not yours)")
    print("Y: Testing/Developer menu")
    print("Q: Quit and disconnect")

# ---------------------------
# Beacon flow
# ---------------------------
@register("set_beacon_flow")
def set_beacon_flow(ctx: Context):
    d = ctx.last_sector_desc or {}
    sid = d.get("id")
    name = d.get("name") or ""
    beacon_text = d.get("beacon") or None

    if not isinstance(sid, int):
        print("Can't determine sector id."); return

    if 1 <= sid <= 10:
        print("FedSpace (1–10): You cannot set a beacon here."); return

    my_count = get_my_beacon_count(ctx.conn)
    if isinstance(my_count, int):
        print(f"Marker beacons aboard: {my_count}")
        if my_count <= 0:
            print("You have no marker beacons. Buy one at StarDock (Hardware)."); return

    if beacon_text:
        print(f"A beacon already exists here: {beacon_text!r}")
        yn = input("Launching another will destroy BOTH beacons, leaving NONE. Proceed? (y/N): ").strip().lower()
        if yn != "y":
            print("Cancelled."); return

    text = input(f"Set beacon for sector {sid} — {name}\nBeacon text (max 80 chars, blank cancels): ").strip()
    if not text:
        print("Cancelled."); return
    if len(text) > 80:
        print("Too long (max 80)."); return

    resp = ctx.conn.rpc("sector.set_beacon", {"sector_id": sid, "text": text})
    if isinstance(resp, dict) and resp.get("status") == "ok":
        new = ctx.conn.rpc("move.describe_sector", {"sector_id": sid})
        ctx.last_sector_desc = normalize_sector(get_data(new))
        print("Beacon deployed.")
    else:
        print(json.dumps(resp, ensure_ascii=False, indent=2))

# ---------------------------
# Enter ship menu
# ---------------------------

def _fmt_int(n):
    try:
        return f"{int(n):,}"
    except Exception:
        return str(n)

def _bool_flags(d: dict) -> str:
    if not isinstance(d, dict):
        return ""
    on = []
    for k, v in d.items():
        if k == "raw":  # internal bitset, noisy
            continue
        if bool(v):
            on.append(k)
    return ", ".join(on) if on else "none"

def _render_cargo(cargo) -> str:
    # Server sometimes returns "" or {} or dict of commodities
    if cargo in ("", None):
        return "none"
    if isinstance(cargo, dict) and cargo:
        parts = []
        for k, v in cargo.items():
            parts.append(f"{k}: {_fmt_int(v)}")
        return ", ".join(parts)
    return str(cargo)

def render_ship_card(ship: dict) -> str:
    """Return a pretty printable card for a ship dict from ship.inspect."""
    sid   = ship.get("id") or ship.get("ship_id") or "?"
    name  = ship.get("name") or ship.get("ship_name") or "Unnamed"
    stype = (ship.get("type") or {}).get("name") or ship.get("ship_type") or "?"
    sector_id = ship.get("sector_id") or ship.get("sector") or "?"
    owner = (ship.get("owner") or {}).get("name") if isinstance(ship.get("owner"), dict) else ship.get("owner")
    owner = owner or "unknown"
    flags = _bool_flags(ship.get("flags") or {})
    shields = _fmt_int((ship.get("defence") or {}).get("shields") or 0)
    fighters = _fmt_int((ship.get("defence") or {}).get("fighters") or 0)
    holds_total = (ship.get("holds") or {}).get("total") or 0
    holds_free  = (ship.get("holds") or {}).get("free") or 0
    holds_used  = max(0, int(holds_total) - int(holds_free)) if isinstance(holds_total, int) and isinstance(holds_free, int) else "?"
    holds_line  = f"{_fmt_int(holds_used)} used / {_fmt_int(holds_total)} total"
    cargo = _render_cargo(ship.get("cargo"))

    lines = []
    lines.append(f"\n┌─ Ship: {name}  (ID {sid})")
    lines.append(f"│   Type: {stype}")
    lines.append(f"│   Sector: {sector_id}    Owner: {owner}")
    lines.append(f"│   Flags: {flags}")
    lines.append(f"│   Defence: Shields {_fmt_int(shields)}, Fighters {_fmt_int(fighters)}")
    lines.append(f"│   Holds:   {holds_line}")
    lines.append(f"│   Cargo:   {cargo}")
    lines.append("└────────────────────────────────────────────")
    return "\n".join(lines)

def print_inspect_response_pretty(resp: dict):
    """Pretty-print ship.inspect response; fall back to JSON if shape unexpected."""
    try:
        data = (resp or {}).get("data") or {}
        ships = data.get("ships") or []
        if not isinstance(ships, list) or not ships:
            # Some servers return a single 'ship' object
            ship = data.get("ship")
            if isinstance(ship, dict):
                print(render_ship_card(ship))
                return
            raise ValueError("No ships found in response.")
        for ship in ships:
            print(render_ship_card(ship))
        return
    except Exception:
        # Fall back to raw JSON dump if structure differs
        print(json.dumps(resp, ensure_ascii=False, indent=2))



@register("enter_ship_menu")
def enter_ship_menu(ctx: Context):
    d = ctx.last_sector_desc or {}
    ships = d.get("ships") or []
    my_ship_id = get_my_ship_id(ctx.conn)
    my_name = get_my_player_name(ctx.conn)

    # Build list of boardable ships
    boardable = []
    for s in ships:
        owner = (s.get("owner") or "").strip().lower()
        sid = s.get("id") or s.get("ship_id")
        is_derelict = (not owner) or owner == "derelict"
        if is_derelict and (sid != my_ship_id) and (owner != (my_name or "").lower()):
            boardable.append(s)

    if not boardable:
        print("No boardable ships here.")
        return

    def show_boardables():
        print("\nBoardable ships in this sector:")
        for idx, s in enumerate(boardable, 1):
            nm = s.get("name") or s.get("ship_name") or "Unnamed"
            tp = s.get("ship_type") or s.get("type") or "?"
            ow = s.get("owner") or "derelict"
            print(f" {idx}) {nm}  [{tp}]  owner={ow}")

    def current_target(idx: int):
        s = boardable[idx]
        nm = s.get("name") or s.get("ship_name") or "Unnamed"
        tp = s.get("ship_type") or s.get("type") or "?"
        sid = s.get("id") or s.get("ship_id")
        return s, nm, tp, sid

    # Initial selection (ask user even if there's only one)
    show_boardables()
    target_idx = 0
    sel = input("Choose ship number (default 1): ").strip()
    if sel:
        try:
            i = int(sel)
            if 1 <= i <= len(boardable):
                target_idx = i - 1
        except ValueError:
            pass

    while True:
        target, nm, tp, ship_id = current_target(target_idx)
        print("\n--- Enter Ship Menu ---")
        print(f" Target: #{target_idx+1} \"{nm}\" [{tp}] (ship_id={ship_id})")
        print(" (I) Inspect Ship")
        print(" (C) Claim")
        #print(" (R) Rename / Re-register")
        print(" (P) Set Primary")
        #print(" (S) Select a different ship")
        print(" (Q) Quit to Previous Menu")
        cmd = input("Enter-Ship Command: ").strip().lower()

        if cmd == "q":
            return

        if cmd == "s":
            # re-prompt selection
            show_boardables()
            sel = input("Choose ship number: ").strip()
            try:
                i = int(sel)
                if 1 <= i <= len(boardable):
                    target_idx = i - 1
            except ValueError:
                print("Invalid selection.")
            continue

        # Confirm current target before any action
        confirm = input(f'Operate on #{target_idx+1} "{nm}" [{tp}] (ship_id={ship_id})? (y/N): ').strip().lower()
        if confirm != "y":
            print("Cancelled.")
            continue

        elif cmd == "i":
            resp = ctx.conn.rpc("ship.inspect", {"ship_id": ship_id})
            print_inspect_response_pretty(resp)

        elif cmd == "c":
            # Claim an unpiloted ship in this sector
            payload = {"ship_id": ship_id}
            resp = ctx.conn.rpc("ship.claim", payload)
            print(json.dumps(resp, ensure_ascii=False, indent=2))

            if resp.get("status") == "ok":
                # Optional: update client’s idea of current ship (if you track it)
                try:
                    claimed = resp.get("data", {}).get("ship", {})
                    ctx.current_ship_id = claimed.get("id", ctx.current_ship_id)
                except Exception:
                    pass

                # Refresh sector after claiming (your old ship remains derelict here)
                new = ctx.conn.rpc("move.describe_sector", {"sector_id": d.get("id")})
                ctx.last_sector_desc = normalize_sector(get_data(new))
                return
            else:
                # Nicer errors for common cases
                err = (resp.get("error") or {}).get("message") or "Claim failed"
                print(f"⚠ {err}")


                
        elif cmd == "p":
            resp = ctx.conn.rpc("ship.set_primary", {"ship_id": ship_id})
            print(json.dumps(resp, ensure_ascii=False, indent=2))

        else:
            print("Invalid command. Please try again.")



# ---------------------------
# Tow (placeholder like v2)
# ---------------------------
@register("tow_flow")
def tow_flow(ctx: Context):
    d = ctx.last_sector_desc or {}
    ships = d.get("ships") or []
    if not has_tow_target(ships, my_ship_id=get_my_ship_id(ctx.conn)):
        print("No towable targets in this sector.")
    else:
        print("Not Implemented: Tow SpaceCraft")

# ---------------------------
# MOVE flows: adjacency-aware move, warp, intercept
# ---------------------------
@register("move_to_adjacent")
def move_to_adjacent(ctx: Context):
    d = ctx.last_sector_desc or {}
    adj = d.get("adjacent") or []
    raw = input("Move to sector: ").strip()
    try:
        target = int(raw)
    except ValueError:
        print("Invalid sector ID."); return
    if target not in adj:
        print(f"{target} is not adjacent. Valid: {', '.join(map(str, adj)) if adj else '(none)'}")
        return
    _ = ctx.conn.rpc("move.warp", {"to_sector_id": target})
    new = ctx.conn.rpc("move.describe_sector", {"sector_id": target})
    ctx.last_sector_desc = normalize_sector(get_data(new))
    call_handler("redisplay_sector", ctx)

@register("warp_flow")
def warp_flow(ctx: Context):
    raw = input("Warp to sector id: ").strip()
    try:
        target = int(raw)
    except ValueError:
        print("Invalid sector ID."); return
    _ = ctx.conn.rpc("move.warp", {"to_sector_id": target})
    new = ctx.conn.rpc("move.describe_sector", {"sector_id": target})
    ctx.last_sector_desc = normalize_sector(get_data(new))
    call_handler("redisplay_sector", ctx)

@register("intercept_flow")
def intercept_flow(ctx: Context):
    try:
        target_ship = int(input("Target ship id: ").strip())
    except ValueError:
        print("Invalid ship id."); return
    r = ctx.conn.rpc("move.intercept", {"ship_id": target_ship})
    print(json.dumps(r, ensure_ascii=False, indent=2))

# ---------------------------
# Autopilot route management
# ---------------------------
@register("autopilot_flow")
def autopilot_flow(ctx: Context):
    print("\n--- Autopilot ---")
    print(" (A) Add to route")
    print(" (C) Clear route")
    print(" (S) Start autopilot")
    print(" (Q) Back")
    cmd = input("Autopilot command: ").strip().lower()
    if cmd == "a":
        call_handler("add_route_flow", ctx)
    elif cmd == "c":
        call_handler("clear_route", ctx)
    elif cmd == "s":
        call_handler("start_autopilot", ctx)
    elif cmd == "q":
        return
    else:
        print("Unknown autopilot command.")

@register("add_route_flow")
def add_route_flow(ctx: Context):
    raw = input("Add sector to route: ").strip()
    try:
        sector = int(raw)
    except ValueError:
        print("Invalid sector."); return
    route = ctx.state.setdefault("ap_route", [])
    route.append(sector)
    ctx.state["ap_route_plotted"] = bool(route)
    print("Route:", " -> ".join(map(str, route)))

@register("clear_route")
def clear_route(ctx: Context):
    ctx.state["ap_route"] = []
    ctx.state["ap_route_plotted"] = False
    print("Autopilot route cleared.")

@register("start_autopilot")
def start_autopilot(ctx: Context):
    route = ctx.state.get("ap_route") or []
    if not route:
        print("No route plotted."); return
    print("Running autopilot:", " -> ".join(map(str, route)))
    for target in route:
        _ = ctx.conn.rpc("move.warp", {"to_sector_id": target})
    new = ctx.conn.rpc("move.describe_sector", {"sector_id": route[-1]})
    ctx.last_sector_desc = normalize_sector(get_data(new))
    ctx.state["ap_route"] = []
    ctx.state["ap_route_plotted"] = False
    call_handler("redisplay_sector", ctx)

# ---------------------------
# Planet & Computer flows
# ---------------------------
@register("land_flow")
def land_flow(ctx: Context):
    try:
        pid = int(input("Planet ID: ").strip())
    except ValueError:
        print("Invalid planet id."); return
    r = ctx.conn.rpc("planet.land", {"planet_id": pid})
    print(json.dumps(r, ensure_ascii=False, indent=2))

@register("map_flow")
def map_flow(ctx: Context):
    # Fallback generic call; adjust to your server's map endpoint
    r = ctx.conn.rpc("map.render", {"center_sector_id": ctx.current_sector_id})
    print(json.dumps(r, ensure_ascii=False, indent=2))

@register("target_info_flow")
def target_info_flow(ctx: Context):
    cmd = input("Enter target command (e.g., 'player.info' or 'ship.inspect'): ").strip()
    data = input("Enter JSON for data (or blank): ").strip()
    try:
        payload = json.loads(data) if data else {}
    except json.JSONDecodeError:
        print("Invalid JSON."); return
    r = ctx.conn.rpc(cmd, payload)
    print(json.dumps(r, ensure_ascii=False, indent=2))

# ---------------------------
# Shipyard flows
# ---------------------------
@register("buy_ship_flow")
def buy_ship_flow(ctx: Context):
    st = input("Ship type: ").strip()
    r = ctx.conn.rpc("dock.ship.buy", {"type": st})
    print(json.dumps(r, ensure_ascii=False, indent=2))

@register("upgrade_ship_flow")
def upgrade_ship_flow(ctx: Context):
    try:
        holds = int(input("Holds to buy: ").strip())
        fighters = int(input("Fighters to buy: ").strip())
        shields = int(input("Shields to buy: ").strip())
    except ValueError:
        print("Invalid numbers."); return
    r = ctx.conn.rpc("dock.ship.upgrades", {"holds": holds, "fighters": fighters, "shields": shields})
    print(json.dumps(r, ensure_ascii=False, indent=2))

@register("downgrade_ship_flow")
def downgrade_ship_flow(ctx: Context):
    try:
        holds = int(input("Sell back holds: ").strip())
        fighters = int(input("Sell back fighters: ").strip())
        shields = int(input("Sell back shields: ").strip())
    except ValueError:
        print("Invalid numbers."); return
    r = ctx.conn.rpc("dock.ship.downgrades", {"holds": holds, "fighters": fighters, "shields": shields})
    print(json.dumps(r, ensure_ascii=False, indent=2))

@register("sell_ship_flow")
def sell_ship_flow(ctx: Context):
    try:
        ship_id = int(input("Ship ID to sell: ").strip())
    except ValueError:
        print("Invalid ship id."); return
    r = ctx.conn.rpc("dock.ship.sell", {"ship_id": ship_id})
    print(json.dumps(r, ensure_ascii=False, indent=2))




@register("rename_ship_flow")
def rename_ship_flow(ctx: Context):
    """
    Rename/re-register *your current ship* without asking for a ship id.
    - Resolves current ship_id from player.my_info
    - Shows current name, prompts for new name (default = keep)
    - Confirms, then calls ship.rename
    """
    # Resolve current ship id
    ship_id = get_my_ship_id(ctx.conn)
    if not isinstance(ship_id, int):
        print("Could not determine your current ship id.")
        return

    # Try to fetch current ship name for a nice prompt
    current_name = None
    try:
        info = ctx.conn.rpc("ship.info", {})
        data = (info or {}).get("data") or {}
        # common shapes: { data: { name: ... } } or { data: { ship: { name: ... } } }
        current_name = data.get("name") \
                       or (data.get("ship") or {}).get("name")
    except Exception:
        pass

    # Prompt for new name (default keeps current)
    if not current_name:
        current_name = "(unnamed)"
    new_name = input(f'New ship name (current: "{current_name}") [Enter to cancel]: ').strip()
    if not new_name:
        print("Rename cancelled.")
        return

    # Confirm
    confirm = input(f'Rename current ship (id={ship_id}) to "{new_name}"? (y/N): ').strip().lower()
    if confirm != "y":
        print("Cancelled.")
        return

    # Do it
    resp = ctx.conn.rpc("ship.rename", {"ship_id": ship_id, "new_name": new_name})
    print(json.dumps(resp, ensure_ascii=False, indent=2))

    # Optional: refresh sector view if the server shows ship names there
    try:
        sid = ctx.current_sector_id
        if isinstance(sid, int):
            desc = ctx.conn.rpc("move.describe_sector", {"sector_id": sid})
            ctx.last_sector_desc = normalize_sector((desc or {}).get("data") or {})
    except Exception:
        pass

    


# ---------------------------
# Testing menu helpers (cap spam, buy, raw JSON) to support menus.json
# ---------------------------
@register("cap_spam_handler")
def cap_spam_handler(ctx: Context) -> None:
    n_str = input("How many times? (default 3): ")
    n = int(n_str) if n_str.isdigit() else 3
    for i in range(n):
        r = ctx.conn.rpc("system.hello", {})
        print(f"[{i+1}] {r.get('status')} {r.get('type')}")

@register("simple_buy_handler")
def simple_buy_handler(ctx: Context) -> None:
    try:
        port_id = int(input("Port ID: ").strip())
    except ValueError:
        print("Invalid port id."); return
    commodity = input("Commodity (ore/organics/equipment): ").strip()
    try:
        qty = int(input("Quantity: ").strip())
    except ValueError:
        print("Quantity must be an integer."); return
    r = ctx.conn.rpc("trade.buy", {"port_id": port_id, "commodity": commodity, "quantity": qty})
    print(json.dumps(r, ensure_ascii=False, indent=2))

@register("interactive_buy_handler")
def interactive_buy_handler(ctx: Context) -> None:
    idem = input("Idempotency key (optional): ").strip() or None
    try:
        port_id = int(input("Port ID: ").strip())
        qty = int(input("Quantity: ").strip())
    except ValueError:
        print("Port ID and quantity must be integers."); return
    commodity = input("Commodity: ").strip()
    env = {"id": ctx.conn._next_id(), "command": "trade.buy",
            "data": {"port_id": port_id, "commodity": commodity, "quantity": qty}}
    if idem:
        env["meta"] = {"idempotency_key": idem}
    ctx.conn.send(env)
    print(json.dumps(ctx.conn.recv(), ensure_ascii=False, indent=2))

@register("raw_json_handler")
def raw_json_handler(ctx: Context) -> None:
    print("--- Raw JSON Command ---")
    command = input("Enter command (e.g., 'move.warp'): ").strip()
    data_str = input("Enter JSON data (e.g., '{\"to_sector_id\": 1}'): ").strip()
    try:
        data = json.loads(data_str) if data_str else {}
        resp = ctx.conn.rpc(command, data)
        print(json.dumps(resp, ensure_ascii=False, indent=2))
    except json.JSONDecodeError:
        print("Invalid JSON data.")
    except Exception as e:
        print(f"An error occurred: {e}")

# ---------------------------
# Quit
# ---------------------------
@register("quit_client")
def quit_client(ctx: Context):
    print("Goodbye.")
    try:
        _ = ctx.conn.rpc("system.disconnect", {})
    except Exception:
        pass
    sys.exit(0)

# ---------------------------
# Menu loading
# ---------------------------
def load_menus(path: Optional[str]) -> Dict[str, Any]:
    if path and os.path.exists(path):
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
        return data.get("menus", data)
    # Fallback minimal
    return {
        "MAIN": {
            "title": "--- Main Menu (Sector {{sector_id}}) ---",
            "options": [
                {"key":"h","label":"(H) Help","action":{"pycall":"help_main"}},
                {"key":"q","label":"(Q) Quit","action":{"pycall":"quit_client"}}
            ]
        }
    }

# ---------------------------
# Main
# ---------------------------
def main():
    args = parse_args()

    menus = load_menus(args.menus)

    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.connect((args.host, args.port))
            conn = Conn(s, debug=args.debug)

            user = args.user if args.user else input("Player Name: ")
            passwd = args.passwd if args.passwd else getpass.getpass("Password: ")

            # optional hello
            try:
                _ = conn.rpc("system.hello", {"client": "twclone-cli", "version": "3.x"})
            except Exception:
                pass

            login = conn.rpc("auth.login", {"user_name": user, "password": passwd})
            if login.get("status") in ("error","refused"):
                fb = conn.rpc("auth.login", {"player_name": user, "password": passwd})
                login = fb

            if login.get("status") in ("error","refused"):
                print("Login failed."); return 2

            current = extract_current_sector(login)
            if not isinstance(current, int):
                info = conn.rpc("player.my_info", {})
                current = extract_current_sector(info) or 1

            # describe sector and normalize
            desc = conn.rpc("move.describe_sector", {"sector_id": int(current)})
            norm = normalize_sector(get_data(desc))

            ctx = Context(conn=conn, menus=menus, last_sector_desc=norm)
            ctx.state["cli"] = {"host": args.host, "port": args.port, "user": user, "debug": bool(args.debug)}

            # First render sector header once
            call_handler("redisplay_sector", ctx)

            while True:
                render_menu(ctx)
                choice = read_choice()
                handle_choice(ctx, choice)

    except ConnectionRefusedError:
        print(f"Couldn’t connect to {args.host}:{args.port}. Is the server running?")
        return 1

if __name__ == "__main__":
    main()
