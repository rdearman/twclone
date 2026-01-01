from __future__ import annotations # <--- MUST be here


# === HOTFIX: ctx-based menu visibility + on-enter hook + Stardock context (placed at top) ===
def _option_visible_with_ctx(ctx, opt: dict) -> bool:
    if not isinstance(opt, dict):
        return True
    cond = opt.get("show_if_ctx")
    if not cond:
        return True
    if isinstance(cond, str):
        cond = [cond]
    st = getattr(ctx, "state", {}) or {}
    for flag in cond:
        if not st.get(flag):
            return False
    return True

def menu_on_enter(ctx, menu_def: dict):
    if not isinstance(menu_def, dict):
        return
    on_enter = menu_def.get("on_enter")
    if isinstance(on_enter, dict) and on_enter.get("pycall"):
        fn = globals().get(on_enter["pycall"])
        if callable(fn):
            try:
                fn(ctx)
            except Exception:
                pass

def _infer_is_stardock(data: dict) -> bool:
    if not isinstance(data, dict):
        return False
    port = data.get('port') if isinstance(data.get('port'), dict) else data
    name = str((port or {}).get('name') or data.get('port_name') or "").lower()
    pclass = str((port or {}).get('class') or (port or {}).get('type') or "").lower()
    if "stardock" in name:
        return True
    if pclass in ("stardock", "star dock"):
        return True
    sid = (port or {}).get('sector_id') or data.get('sector_id')
    if sid in (0, 1) and ("dock" in name or "dock" in pclass):
        return True
    return False

def ctx_refresh_port_context(ctx):
    ctx.state.setdefault('is_stardock', False)
    conn = getattr(ctx, 'conn', None)
    if conn is None:
        return
    def _try(cmd, payload=None):
        try:
            r = conn.rpc(cmd, payload or {})
            if isinstance(r, dict) and r.get('status') in ('ok','srv-ok','success', None):
                d = r.get('data') or {}
                if isinstance(d, dict) and d:
                    ctx.state['is_stardock'] = bool(_infer_is_stardock(d))
                    return True
        except Exception:
            pass
        return False
    for cmd in ("port.status","dock.status","port.info","sector.port.status","sector.current","whereami"):
        if _try(cmd):
            break
# === /HOTFIX ===




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
### from __future__ import annotations
import argparse
import uuid
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
    # Resolve menus.json path relative to script location
    script_dir = os.path.dirname(os.path.abspath(__file__))
    default_menus_path = os.path.join(script_dir, "menus.json")
    p.add_argument("--menus", default=default_menus_path,
                   help=f"Path to menus.json (default: {default_menus_path})")
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

# --- BEGIN AUTO-ADDED CAPS_HELPERS ---
def _ctx_capabilities_get(ctx, path, default=None):
    """Resolve dotted path from ctx.capabilities (dict)."""
    cur = getattr(ctx, 'capabilities', None) or {}
    for part in path.split("."):
        if not isinstance(cur, dict):
            return default
        cur = cur.get(part)
    return default if cur is None else cur
# --- END AUTO-ADDED CAPS_HELPERS ---


# --- BEGIN AUTO-ADDED RPC_RATE_LIMIT_WRAP ---
# Attempt to wrap Conn.rpc to capture meta.rate_limit into ctx.state.
try:
    _OldConn = Conn
    if not hasattr(_OldConn, "_twc_rl_wrapped"):
        _old_rpc = _OldConn.rpc
        def _rpc_wrapped(self, cmd, data=None, *args, **kwargs):
            resp = _old_rpc(self, cmd, data, *args, **kwargs)
            try:
                rl = ((resp or {}).get("meta") or {}).get("rate_limit")
                if rl and hasattr(self, "ctx") and isinstance(getattr(self, "ctx"), object):
                    st = getattr(self.ctx, "state", None)
                    if isinstance(st, dict):
                        st["rate_limit"] = rl
            except Exception:
                pass
            return resp
        _OldConn.rpc = _rpc_wrapped
        _OldConn._twc_rl_wrapped = True
except NameError:
    # Conn not defined in this script; ignore.
    pass
# --- END AUTO-ADDED RPC_RATE_LIMIT_WRAP ---


# --- BEGIN AUTO-ADDED RATE_LIMIT_HUD ---
def _print_rate_limit_hud(ctx):
    rl = (getattr(ctx, "state", {}) or {}).get("rate_limit")
    if not isinstance(rl, dict):
        return
    lim = rl.get("limit"); rem = rl.get("remaining"); rst = rl.get("reset")
    tail = ""
    try:
        import time
        if isinstance(rst, (int, float)):
            eta = int(rst - time.time())
            if eta >= 0:
                tail = f"  T-{eta}s"
    except Exception:
        pass
    print(f"[rate] {rem}/{lim}{tail}")
# --- END AUTO-ADDED RATE_LIMIT_HUD ---


# --- BEGIN AUTO-ADDED EVENT_HANDLERS ---
def _handle_known_broadcast(envelope: dict):
    t = envelope.get("type")
    if t in ("sector.player_entered", "sector.player_left"):
        d = envelope.get("data") or {}
        who = d.get("player_name") or d.get("player_id") or "Someone"
        sid = d.get("sector_id")
        verb = "entered" if t.endswith("entered") else "left"
        print(f"\n» {who} {verb} sector {sid}\n")
        return True
    if t == "broadcast.v1":
        d = envelope.get("data") or {}
        msg = d.get("message") or ""
        scope = d.get("scope") or "global"
        print(f"\n[notice:{scope}] {msg}\n")
        return True
    return False
# --- END AUTO-ADDED EVENT_HANDLERS ---


# --- BEGIN AUTO-ADDED AUTOPILOT ---
@register("cli_autopilot_status")
def cli_autopilot_status(ctx):
    """Query and display autopilot status."""
    r = ctx.conn.rpc("move.autopilot.status", {})
    data = (r or {}).get("data") or {}
    state = data.get("state")
    nxt = data.get("next")
    print(f"Autopilot: {state or 'unknown'}; next: {nxt if nxt is not None else '-'}")
    ctx.state["last_rpc"] = r

@register("cli_autopilot_control")
def cli_autopilot_control(ctx):
    """Send Y/N/E-style controls to autopilot."""
    print("Controls: [Y] stop at next  |  [N] continue  |  [E] express  |  [Q] back")
    while True:
        k = input("> ").strip().lower()
        if k == "y":
            _ = ctx.conn.rpc("move.autopilot.control", {"action":"stop_at_next"})
        elif k == "n":
            _ = ctx.conn.rpc("move.autopilot.control", {"action":"continue"})
        elif k == "e":
            _ = ctx.conn.rpc("move.autopilot.control", {"action":"express"})
        elif k == "q" or k == "":
            return
        else:
            print("Y/N/E or Q")
# --- END AUTO-ADDED AUTOPILOT ---


# --- BEGIN AUTO-ADDED BULK_EXEC ---
@register("cli_bulk_execute_flow")
def cli_bulk_execute_flow(ctx):
    max_bulk = _ctx_capabilities_get(ctx, "limits.max_bulk", 0)
    if not max_bulk:
        print("Bulk not available on this server.")
        return
    print(f"Enter up to {max_bulk} commands as JSON lines like: {{\"command\":\"...\",\"data\":{{}}}}")
    print("Blank line to end.")
    import json as _json
    items = []
    while len(items) < int(max_bulk):
        line = input()
        if not line.strip():
            break
        try:
            obj = _json.loads(line)
            if not isinstance(obj, dict) or "command" not in obj:
                print("Expecting {'command':'...', 'data':{}}"); continue
            items.append(obj)
        except Exception:
            print("Invalid JSON.")
    if not items:
        print("No commands.")
        return
    r = ctx.conn.rpc("bulk.execute", {"items": items})
    try:
        print(_json.dumps(r, ensure_ascii=False, indent=2))
    except Exception:
        print(r)
# --- END AUTO-ADDED BULK_EXEC ---


# --- BEGIN AUTO-ADDED SUBSCRIPTIONS ---
@register("cli_subscriptions_list")
def cli_subscriptions_list(ctx):
    r = ctx.conn.rpc("subscribe.list", {})
    data = (r or {}).get("data") or {}
    items = data.get("items") or data.get("topics") or []
    if not items:
        print("(no subscriptions)")
    for it in items:
        t = it.get("topic") or it.get("event_type")
        locked = it.get("locked")
        print(f"- {t}{' [locked]' if locked else ''}")
    ctx.state["last_rpc"] = r

@register("cli_subscriptions_remove")
def cli_subscriptions_remove(ctx):
    topic = input("Topic to remove: ").strip()
    r = ctx.conn.rpc("subscribe.remove", {"topic": topic})
    if r.get("status") in ("refused","error"):
        err = (r.get("error") or {})
        if err.get("code") == 1407:
            print("Cannot remove: topic is locked.")
    import json as _json
    try:
        print(_json.dumps(r, ensure_ascii=False, indent=2))
    except Exception:
        print(r)

@register("subscriptions_list_flow")
def subscriptions_list_flow(ctx: Context):
    r = ctx.conn.rpc("subscribe.list", {})
    ctx.state["last_rpc"] = r
    if r.get("status") == "ok":
        data = r.get("data") or {}
        items = data.get("items") or []
        if items:
            print("Current subscriptions:")
            for item in items:
                locked_str = " (locked)" if item.get("locked") else ""
                print(f"  - {item.get('event_type')}{locked_str}")
        else:
            print("No active subscriptions.")
    else:
        _pp(r)

@register("subscriptions_add_flow")
def subscriptions_add_flow(ctx: Context):
    # Get available topics from the server
    catalog_resp = ctx.conn.rpc("subscribe.catalog", {})
    if catalog_resp.get("status") != "ok":
        print("Could not retrieve subscription catalog from server.")
        _pp(catalog_resp)
        return

    catalog_data = catalog_resp.get("data") or {}
    available_topics = catalog_data.get("items") or []

    if not available_topics:
        print("No available subscription topics found on the server.")
        return

    print("Available subscription topics:")
    for i, topic in enumerate(available_topics):
        print(f"  {i+1}: {topic.get('event_type')} - {topic.get('description')}")

    try:
        choice = int(input("Choose a topic to subscribe to: "))
        if 1 <= choice <= len(available_topics):
            selected_topic = available_topics[choice - 1].get("event_type")
        else:
            print("Invalid choice.")
            return
    except ValueError:
        print("Invalid input.")
        return

    if selected_topic:
        add_resp = ctx.conn.rpc("subscribe.add", {"event_type": selected_topic})
        if add_resp.get("status") == "ok":
            print(f"Successfully subscribed to {selected_topic}.")
        else:
            print(f"Failed to subscribe to {selected_topic}.")
            _pp(add_resp)

@register("subscriptions_remove_flow")
def subscriptions_remove_flow(ctx: Context):
    topic = input("Topic to remove: ").strip()
    if not topic:
        print("Cancelled.")
        return
    r = ctx.conn.rpc("subscribe.remove", {"event_type": topic})
    if r.get("status") == "ok":
        print(f"Successfully unsubscribed from {topic}.")
    else:
        print(f"Failed to unsubscribe from {topic}.")
        _pp(r)
# --- END AUTO-ADDED SUBSCRIPTIONS ---


# --- BEGIN AUTO-ADDED CHAT_MAIL ---
@register("cli_chat_history")
def cli_chat_history(ctx):
    r = ctx.conn.rpc("chat.history", {"limit": 20})
    import json as _json
    try:
        print(_json.dumps(r, ensure_ascii=False, indent=2))
    except Exception:
        print(r)

@register("cli_chat_send")
def cli_chat_send(ctx):
    msg = input("Message: ").strip()
    if not msg:
        print("Empty message.")
        return
    scope = (input("Scope (global|corp|player) [global]: ").strip() or "global").lower()
    to = None
    if scope == "player":
        to = input("Player name/id: ").strip()
    r = ctx.conn.rpc("chat.send", {"scope": scope, "to": to, "message": msg})
    import json as _json
    try:
        print(_json.dumps(r, ensure_ascii=False, indent=2))
    except Exception:
        print(r)

@register("cli_mail_inbox")
def cli_mail_inbox(ctx):
    r = ctx.conn.rpc("mail.inbox", {"limit": 20})
    import json as _json
    try:
        print(_json.dumps(r, ensure_ascii=False, indent=2))
    except Exception:
        print(r)

@register("cli_mail_read")
def cli_mail_read(ctx):
    mid = input("Mail id: ").strip()
    try:
        mid = int(mid)
    except Exception:
        print("Bad id.")
        return
    r = ctx.conn.rpc("mail.read", {"id": mid})
    import json as _json
    try:
        print(_json.dumps(r, ensure_ascii=False, indent=2))
    except Exception:
        print(r)

@register("cli_mail_send")
def cli_mail_send(ctx):
    to = input("To (player): ").strip()
    subj = input("Subject: ").strip()
    body = input("Body: ").strip()
    r = ctx.conn.rpc("mail.send", {"to": to, "subject": subj, "body": body})
    import json as _json
    try:
        print(_json.dumps(r, ensure_ascii=False, indent=2))
    except Exception:
        print(r)

@register("cli_mail_delete")
def cli_mail_delete(ctx):
    mid = input("Mail id: ").strip()
    try:
        mid = int(mid)
    except Exception:
        print("Bad id.")
        return
    r = ctx.conn.rpc("mail.delete", {"id": mid})
    import json as _json
    try:
        print(_json.dumps(r, ensure_ascii=False, indent=2))
    except Exception:
        print(r)
    try:
        mid = int(mid)
    except Exception:
        print("Bad id.")
        return
    r = ctx.conn.rpc("mail.delete", {"id": mid})
    import json as _json
    try:
        print(_json.dumps(r, ensure_ascii=False, indent=2))
    except Exception:
        print(r)

@register("mail_inbox_flow")
def mail_inbox_flow(ctx: Context):
    r = ctx.conn.rpc("mail.inbox", {"limit": 20})
    ctx.state["last_rpc"] = r
    if r.get("status") == "ok":
        data = r.get("data") or {}
        items = data.get("items") or []
        if items:
            print("\n--- Inbox ---")
            for item in items:
                mail_id = item.get("id")
                sender = item.get("sender_name") or f"ID: {item.get('sender_id')}"
                subject = item.get("subject") or "(no subject)"
                sent_at = item.get("sent_at") or "unknown time"
                print(f"  ID: {mail_id:<4} From: {sender:<15} Subject: {subject:<30} Sent: {sent_at}")
        else:
            print("Inbox is empty.")
    else:
        _pp(r)

@register("mail_read_flow")
def mail_read_flow(ctx: Context):
    try:
        mid = int(input("Mail ID to read: ").strip())
    except ValueError:
        print("Invalid Mail ID."); return

    r = ctx.conn.rpc("mail.read", {"id": mid})
    ctx.state["last_rpc"] = r
    if r.get("status") == "ok":
        data = r.get("data") or {}
        if data:
            sender = data.get("sender_name") or f"ID: {data.get('sender_id')}"
            subject = data.get("subject") or "(no subject)"
            sent_at = data.get("sent_at") or "unknown time"
            body = data.get("body") or "(empty message)"
            print(f"\n--- Mail ID: {mid} ---")
            print(f"From: {sender}")
            print(f"Subject: {subject}")
            print(f"Sent: {sent_at}")
            print("-" * 30)
            print(body)
            print("-" * 30)
        else:
            print(f"Mail ID {mid} not found or no content.")
    else:
        _pp(r)

@register("mail_send_flow")
def mail_send_flow(ctx: Context):
    to = input("To (player name or ID): ").strip()
    if not to:
        print("Recipient cannot be empty. Cancelled."); return
    subj = input("Subject: ").strip()
    body = input("Body (enter on a new line, then Ctrl+D to finish): ")
    
    # Read multi-line input for body
    lines = []
    while True:
        try:
            line = input()
            lines.append(line)
        except EOFError:
            break
    body = "\n".join(lines)

    r = ctx.conn.rpc("mail.send", {"to": to, "subject": subj, "body": body})
    ctx.state["last_rpc"] = r
    if r.get("status") == "ok":
        print(f"Mail sent to {to}.")
    else:
        print(f"Failed to send mail to {to}.")
        _pp(r)

@register("mail_delete_flow")
def mail_delete_flow(ctx: Context):
    try:
        mid = int(input("Mail ID to delete: ").strip())
    except ValueError:
        print("Invalid Mail ID."); return

    r = ctx.conn.rpc("mail.delete", {"ids": [mid]})
    ctx.state["last_rpc"] = r
    if r.get("status") == "ok":
        print(f"Mail ID {mid} deleted.")
    else:
        print(f"Failed to delete Mail ID {mid}.")
        _pp(r)
# --- END AUTO-ADDED CHAT_MAIL ---


# --- BEGIN AUTO-ADDED HUD_HOOK ---
def _hud_after_action(ctx):
    try:
        _print_rate_limit_hud(ctx)
    except Exception:
        pass
# --- END AUTO-ADDED HUD_HOOK ---


# --- BEGIN AUTO-ADDED DOCK_CONTEXT_REFRESH ---
def _infer_is_stardock(data: dict) -> bool:
    """Try to infer if the current dock/port is 'Stardock' from various payload shapes."""
    if not isinstance(data, dict):
        return False
    # data may look like: { 'port': { 'name': 'Stardock', 'class': 'stardock', ... } }
    port = data.get('port') if isinstance(data.get('port'), dict) else data
    name = str((port or {}).get('name') or data.get('port_name') or "").lower()
    pclass = str((port or {}).get('class') or (port or {}).get('type') or "").lower()
    # Heuristics
    if "stardock" in name:
        return True
    if pclass in ("stardock", "star dock"):
        return True
    sid = (port or {}).get('sector_id') or data.get('sector_id')
    if sid in (0, 1):
        if "dock" in name or "dock" in pclass:
            return True
    return False

def ctx_refresh_port_context(ctx):
    """Best-effort fetch to set ctx.state['is_stardock'] and other access flags before showing the Dock menu."""
    ctx.state.setdefault('is_stardock', False)
    ctx.state.setdefault('is_shipyard_port', False)
    ctx.state.setdefault('has_exchange_access', False)
    ctx.state.setdefault('has_insurance_access', False)
    ctx.state.setdefault('has_tavern_access', False)
    ctx.state.setdefault('has_hardware_access', False)

    conn = getattr(ctx, 'conn', None)
    if conn is None:
        return

    def _try(cmd, payload=None):
        try:
            r = conn.rpc(cmd, payload or {})
            if isinstance(r, dict) and r.get('status') in ('ok','srv-ok','success', None):
                d = r.get('data') or {}
                if isinstance(d, dict) and d:
                    ctx.state['is_stardock'] = bool(_infer_is_stardock(d))
                    
                    port_data = d.get('port') if isinstance(d.get('port'), dict) else d
                    port_class = port_data.get('class') or port_data.get('type')
                    if port_class == 9: # Stardock
                        ctx.state['is_shipyard_port'] = True
                        ctx.state['has_hardware_access'] = True
                        ctx.state['has_exchange_access'] = True # Example: Exchange at Stardock
                        ctx.state['has_insurance_access'] = True # Example: Insurance at Stardock
                    elif port_class == 7: # Tavern (arbitrary, adjust as per game rules)
                        ctx.state['has_tavern_access'] = True
                    # Add other port types and their associated access flags as needed

                    ctx.last_sector_desc = normalize_sector(d)
                    if 'port' in d and isinstance(d['port'], dict) and 'id' in d['port']:
                        if 'port' not in ctx.last_sector_desc:
                            ctx.last_sector_desc['port'] = {}
                        ctx.last_sector_desc['port']['id'] = d['port']['id']
                    
                    # Refresh corporation context as well
                    _update_corp_context(ctx)
                    return True
        except Exception:
            pass
        return False
    for cmd in ("port.status","dock.status","port.info","sector.port.status","sector.current","whereami"):
        if _try(cmd):
            break
# --- END AUTO-ADDED DOCK_CONTEXT_REFRESH ---


# --- BEGIN AUTO-ADDED SHOW_IF_CTX_SUPPORT ---
def _option_visible_with_ctx(ctx, opt: dict) -> bool:
    """Return True if 'show_if_ctx' conditions (if any) are satisfied based on ctx.state flags."""
    if not isinstance(opt, dict):
        return True
    cond = opt.get("show_if_ctx")
    if not cond:
        return True
    if isinstance(cond, str):
        cond = [cond]
    st = getattr(ctx, "state", {}) or {}
    for flag in cond:
        if not st.get(flag):
            return False
    return True
# --- END AUTO-ADDED SHOW_IF_CTX_SUPPORT ---


# --- BEGIN AUTO-ADDED MENU_SHIMS ---
def menu_on_enter(ctx, menu_def: dict):
    """Call optional prefetch hooks.
    
    Supports menu JSON like: {"on_enter": {"pycall": "ctx_refresh_port_context"}}.
    """
    if not isinstance(menu_def, dict):
        return
    on_enter = menu_def.get("on_enter")
    if isinstance(on_enter, dict) and on_enter.get("pycall"):
        fn = globals().get(on_enter["pycall"])
        if callable(fn):
            try:
                fn(ctx)
            except Exception:
                pass

def filter_menu_options_with_ctx(ctx, options: list) -> list:
    """Filter an options list using ctx flags via _option_visible_with_ctx."""
    out = []
    for opt in options or []:
        try:
            if _option_visible_with_ctx(ctx, opt):
                out.append(opt)
        except Exception:
            out.append(opt)
    return out
# --- END AUTO-ADDED MENU_SHIMS ---

# ---------------------------
# Conn (ported from v2)
# ---------------------------
class Conn:
    def __init__(self, sock: socket.socket, debug: bool = False):
        self.sock = sock
        self._seq = 0
        self._r = sock.makefile("r", encoding="utf-8", newline="\n")
        self.debug = debug
        self._seen_notices = set()   # for de-duping system.notice

        # optional: the Context can set this after it’s created (for prompt redraws)
        self.ctx = None

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

    def rpc(self, command: str, data: dict) -> dict:
        """
        Send an RPC and wait for its reply. While waiting, handle unsolicited
        event/broadcast frames (no reply_to) so they don't break menus.
        Also de-duplicate system.notice by notice id.
        """
        req_id = self._next_id()
        self.send({"id": req_id, "command": command, "data": data})

        while True:
            resp = self.recv()

            # 1) Our RPC reply?
            if resp.get("reply_to") == req_id:
                return resp

            # 2) Typed async event/broadcast (no reply_to): e.g., system.notice
            t = resp.get("type")
            if t and not resp.get("reply_to"):
                d = resp.get("data") or {}

                if t == "system.notice":
                    # de-dup on notice id
                    nid = d.get("id")
                    if nid is not None:
                        if nid in self._seen_notices:
                            continue  # drop duplicate
                        self._seen_notices.add(nid)
                        # optional cap to avoid unbounded growth
                        if len(self._seen_notices) > 256:
                            self._seen_notices.clear()

                    title = d.get("title", "System Notice")
                    body  = d.get("body", "")
                    print(f"\n*** {title} ***\n{body}\n")

                    # optional: re-render current menu header/prompt
                    try:
                        if getattr(self, "ctx", None) and getattr(self.ctx, "active_menu_title", None):
                            print(f"--- {self.ctx.active_menu_title} ---")
                    except Exception:
                        pass

                else:
                    # generic event hook/print
                    if hasattr(self, "on_event") and callable(getattr(self, "on_event")):
                        self.on_event(resp)
                    else:
                        print(f"[event] {t}: {d}")

                continue  # keep waiting for our RPC reply

            # 3) Legacy event envelope (id:'evt' or event field)
            if resp.get("id") == "evt" or resp.get("event"):
                ev_type = resp.get("event") or "event"
                d = resp.get("data") or {}
                print(f"[event] {ev_type}: {d}")
                continue

            # 4) Async errors without reply_to (rare broadcast errors)
            if resp.get("status") in ("error", "refused") and resp.get("reply_to") is None:
                err = (resp.get("error") or {}).get("message") or "error"
                print(f"[async-{resp.get('status')}] {resp.get('type') or ''} {err}")
                continue

            # 5) Unknown frame: ignore but keep the RPC wait alive
            # print(f"[WARN] Ignoring frame id={resp.get('id')} reply_to={resp.get('reply_to')}")
            # loop continues


    
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

def compute_flags(ctx: Context) -> dict:
    """
    Compute state flags based on current sector info, player info, etc.
    These flags drive conditional menu item visibility.
    """
    d = ctx.last_sector_desc or {}
    ships = d.get("ships", [])
    my_ship_id = get_my_ship_id(ctx.conn)
    my_name = get_my_player_name(ctx.conn)
    # keep beacon count handy for label interpolation
    ctx.state["beacon_count"] = get_my_beacon_count(ctx.conn)

    flags = {
        "has_port": bool(d.get("port")),
        "is_stardock": ctx.state.get("is_stardock", False),
        "is_shipyard_port": ctx.state.get("is_shipyard_port", False),
        "has_planet": bool(d.get("planets")),
        "can_set_beacon": (d.get("beacon") in (None, "")),
        "has_boardable": has_boardable_ship(ships, my_ship_id=my_ship_id, my_name=my_name),
        "has_tow_target": has_tow_target(ships, my_ship_id=my_ship_id),
        "on_planet": bool(ctx.state.get("on_planet")),
        "planet_has_products": bool(ctx.state.get("planet_products_available")),
        "is_corp_member": bool(ctx.player_info.get("corp_id")),
        "can_autopilot": bool(ctx.state.get("ap_route_plotted")),
        "has_exchange_access": ctx.state.get("has_exchange_access", False),
        "has_insurance_access": ctx.state.get("has_insurance_access", False),
        "has_tavern_access": ctx.state.get("has_tavern_access", False),
        "has_hardware_access": ctx.state.get("has_hardware_access", False),
        "in_corporation": ctx.state.get("in_corporation", False),
        "is_ceo": ctx.state.get("is_ceo", False),
        "is_ceo_or_officer": ctx.state.get("is_ceo_or_officer", False),
        "not_in_corporation": not ctx.state.get("in_corporation", False),
        "corp_not_public": not ctx.state.get("corp_is_public", False),
        "corp_is_public": ctx.state.get("corp_is_public", False)
    }
    return flags

@register("sector_density_scan_flow")
def sector_density_scan_flow(ctx: Context):
    """
    Density-only scan that is resilient:
    - Sends sector_id (if known)
    - Tries sector.scan.density; falls back to move.scan.density
    - Prints table for [sid, density, ...] or single dict shape
    """
    sector_id = ctx.current_sector_id
    payload = {"sector_id": sector_id} if sector_id is not None else {}
    try:
        resp = ctx.conn.rpc("sector.scan.density", payload)
    except Exception:
        resp = ctx.conn.rpc("move.scan.density", payload)
    ctx.state["last_rpc"] = resp

    print("\n=== Density Scan ===")
    data = resp.get("data") if isinstance(resp, dict) else None

    # Case A: {"sector_id": X, "density": Y}
    if isinstance(data, dict) and "density" in data:
        sid = data.get("sector_id", sector_id)
        dens = data.get("density")
        print(f"Sector: {sid}")
        print(f"Density: {dens if dens is not None else '(unknown)'}")

    # Case B: [sid, density, sid, density, ...]
    elif isinstance(data, list) and all(isinstance(x, int) for x in data):
        pairs = list(zip(data[0::2], data[1::2]))
        print("Sector   Density")
        print("----------------")
        for sid, dens in pairs:
            print(f"{sid:>6}   {dens:>7}")
    else:
        print("(unexpected response shape)")


@register("pathfind_flow")
def pathfind_flow(ctx: Context):
    """Ask for a target, call move.pathfind, print the route, and optionally follow it."""
    # Where are we now?
    cur_desc = ctx.last_sector_desc or {}
    cur = cur_desc.get("sector_id") or cur_desc.get("id") or cur_desc.get("sector") or ctx.state.get("sector_id")
    try:
        target = int(input("Pathfind to sector id: ").strip())
    except (ValueError, TypeError):
        print("Invalid sector ID."); return

    if not cur:
        # Fallback: fetch current sector from server if we don't have it cached
        try:
            info = ctx.conn.rpc("move.describe_sector", {})
            data = (info or {}).get("data") or {}
            cur = data.get("sector_id") or data.get("id")
        except Exception:
            pass
    if not cur:
        print("Cannot determine current sector."); return

    # Ask server for a path
    req = {"from": cur, "to": target}
    print(f"[DEBUG] Pathfind request: {req}") # ADDED DEBUG PRINT
    resp = ctx.conn.rpc("move.pathfind", req)
    print(f"[DEBUG] Pathfind response: {resp}") # ADDED DEBUG PRINT
    status = (resp or {}).get("status")
    if status in ("error", "refused"):
        err = (resp.get("error") or {})
        print(f"Pathfind failed {err.get('code')}: {err.get('message') or 'error'}")
        return
    data = (resp or {}).get("data") or {}

    # Try to normalize a path from a few likely shapes
    path = data.get("path") or data.get("sectors") or data.get("steps") or []
    # Some servers return an array of {from,to} edges; flatten if needed
    if path and isinstance(path[0], dict):
        # e.g., [{"from":1,"to":2},{"from":2,"to":5}] -> [1,2,5]
        seq = [path[0].get("from")]
        for e in path:
            t = e.get("to")
            if t is not None:
                seq.append(t)
        path = seq

    hops = data.get("hops") or data.get("steps") or (len(path) - 1 if path else None)
    cost = data.get("total_cost") or data.get("cost")

    # Pretty print
    if not path or len(path) < 1:
        print("No route found.")
        return

    print("\n=== Route ===")
    print(" → ".join(str(s) for s in path))
    if hops is None:
        hops = max(0, len(path) - 1)
    extra = []
    extra.append(f"hops: {hops}")
    if cost is not None:
        extra.append(f"cost: {cost}")
    print("(" + ", ".join(extra) + ")\n")

    # Offer to follow route
    go = input("Follow this route now? [y/N] ").strip().lower()
    if go != "y":
        return

    # Walk the path hop-by-hop
    # We start from our *current* sector, so skip the first element if it matches.
    start_idx = 0
    if path and path[0] == cur:
        start_idx = 1

    for i in range(start_idx, len(path)):
        hop = path[i]
        print(f"→ warping to {hop} ...")
        r = ctx.conn.rpc("move.warp", {"to_sector_id": hop})
        st = (r or {}).get("status")
        if st == "ok":
            # Refresh sector view only if the hop succeeded
            new = ctx.conn.rpc("move.describe_sector", {"sector_id": hop})
            ctx.last_sector_desc = normalize_sector(get_data(new))
            call_handler("redisplay_sector", ctx)
        else:
            err = (r.get("error") or {}).get("message")
            reason = ((r.get("data") or {}).get("reason"))
            msg = f"Hop to {hop} "
            if st == "refused":
                msg += "refused"
            else:
                msg += "failed"
            if reason:
                msg += f" ({reason})"
            if err:
                msg += f": {err}"
            print(msg)
            print("Stopping route.")
            break


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

    resp = ctx.conn.rpc("move.warp", {"to_sector_id": target})
    status = (resp or {}).get("status")
    if status == "ok":
        # Only refresh if the server actually moved us
        new = ctx.conn.rpc("move.describe_sector", {"sector_id": target})
        ctx.last_sector_desc = normalize_sector(get_data(new))
        call_handler("redisplay_sector", ctx)
    else:
        err = (resp.get("error") or {}).get("message") if resp else "Unknown error"
        reason = (resp.get("data") or {}).get("reason") if resp else None
        if reason:
            print(f"Move refused: {reason} — {err or ''}".strip())
        else:
            print(f"Move failed: {err or 'no details'}")


@register("warp_flow")
def warp_flow(ctx: Context):
    raw = input("Warp to sector id: ").strip()
    try:
        target = int(raw)
    except ValueError:
        print("Invalid sector ID."); return

    resp = ctx.conn.rpc("move.warp", {"to_sector_id": target})
    status = (resp or {}).get("status")

    if status == "ok":
        # server accepted and moved us
        new = ctx.conn.rpc("move.describe_sector", {"sector_id": target})
        ctx.last_sector_desc = normalize_sector(get_data(new))
        call_handler("redisplay_sector", ctx)
    elif status in ("refused", "error"):
        # Show reason and stay where we are
        data = (resp or {}).get("data") or {}
        err = (resp.get("error") or {}).get("message") if resp else None
        from_id = data.get("from")
        to_id = data.get("to")
        reason = data.get("reason")
        msg = f"Warp {from_id}->{to_id} refused" if status == "refused" else "Warp failed"
        if reason:
            msg += f": {reason}"
        if err:
            msg += f" — {err}"
        print(msg)
    else:
        print("Warp failed: unexpected response")



# ---------- Settings pretty printers ----------
def _pp(obj):
    import json
    print(json.dumps(obj, ensure_ascii=False, indent=2))

def _pp_settings_blob(resp):
    d = (resp or {}).get("data") or {}
    print("\n=== SETTINGS ===")
    prefs = d.get("prefs") or {}
    subs  = d.get("subscriptions") or d.get("topics") or []
    bms   = d.get("bookmarks") or []
    avoid = d.get("avoid") or d.get("avoids") or []
    notes = d.get("notes") or []
    print("- prefs:")
    if isinstance(prefs, list):
        for p in prefs:
            if isinstance(p, dict):
                print(f"  {p.get('key')}: {p.get('value')}")
    elif isinstance(prefs, dict):
        for k, v in prefs.items():
            print(f"  {k}: {v}")
    print("- subscriptions:")
    for s in subs:
        if isinstance(s, dict):
            print(f"  {s.get('topic')}  (locked={bool(s.get('locked'))})")
        else:
            print(f"  {s}")
    print("- bookmarks:")
    for b in bms:
        print(f"  {b.get('name')} -> {b.get('sector_id')}")
    print("- avoid:")
    print("  " + (", ".join(map(str, avoid)) if avoid else "(none)"))
    if notes:
        print("- notes:")
        for n in notes:
            print(f"  [{n.get('scope')}:{n.get('key')}] {n.get('note')}")

@register("settings_view")
def settings_view(ctx: Context):
    # Try aggregate; fall back to granular if server doesn’t have the union yet.
    try:
        resp = ctx.conn.rpc("player.get_settings", {"include": ["prefs","subscriptions","bookmarks","avoid","notes"]})
        ctx.state["last_rpc"] = resp
        _pp_settings_blob(resp)
        return
    except Exception:
        pass

    # Granular fallbacks (all defined in your server/API)
    bundle = {"prefs":{}, "subscriptions":[], "bookmarks":[], "avoid":[], "notes":[]}
    try:
        r = ctx.conn.rpc("player.get_prefs", {});               bundle["prefs"] = (r.get("data") or {}).get("prefs") or {}
    except Exception: pass
    try:
        r = ctx.conn.rpc("subscribe.list", {});                 bundle["subscriptions"] = (r.get("data") or {}).get("active") or (r.get("data") or {}).get("topics") or []
    except Exception: pass
    try:
        r = ctx.conn.rpc("nav.bookmark.list", {});              bundle["bookmarks"] = (r.get("data") or {}).get("bookmarks") or []
    except Exception: pass
    try:
        r = ctx.conn.rpc("nav.avoid.list", {});                 bundle["avoid"] = (r.get("data") or {}).get("sectors") or []
    except Exception: pass
    try:
        r = ctx.conn.rpc("notes.list", {});                     bundle["notes"] = (r.get("data") or {}).get("notes") or []
    except Exception: pass
    ctx.state["last_rpc"] = {"status":"ok","type":"player.settings_v1","data":bundle}
    _pp_settings_blob(ctx.state["last_rpc"])


@register("prefs_toggle_24h")
def prefs_toggle_24h(ctx: Context):
    # Fetch current to compute toggle
    cur = ctx.conn.rpc("player.get_prefs", {})
    items = (cur.get("data") or {}).get("prefs") or []
    now = {i["key"]: i.get("value") for i in items if isinstance(i, dict) and "key" in i}
    new_val = not str(now.get("ui.clock_24h","true")).lower() in ("true","1","yes")
    r = ctx.conn.rpc("player.set_prefs", {"items":[{"key":"ui.clock_24h","type":"bool","value": new_val}]})
    _pp(r)

@register("prefs_set_locale")
def prefs_set_locale(ctx: Context):
    loc = input("Locale (e.g., en-GB): ").strip() or "en-GB"
    r = ctx.conn.rpc("player.set_prefs", {"items":[{"key":"ui.locale","type":"string","value": loc}]})
    _pp(r)

@register("bookmark_add_flow")
def bookmark_add_flow(ctx: Context):
    name = input("Bookmark name: ").strip()
    try:
        sid = int(input("Sector id: ").strip())
    except ValueError:
        print("Invalid sector id."); return
    r = ctx.conn.rpc("nav.bookmark.add", {"name": name, "sector_id": sid})
    _pp(r)

@register("bookmark_remove_flow")
def bookmark_remove_flow(ctx: Context):
    name = input("Bookmark name to remove: ").strip()
    r = ctx.conn.rpc("nav.bookmark.remove", {"name": name})
    _pp(r)

@register("avoid_add_flow")
def avoid_add_flow(ctx: Context):
    try:
        sid = int(input("Sector id to avoid: ").strip())
    except ValueError:
        print("Invalid sector id."); return
    r = ctx.conn.rpc("nav.avoid.add", {"sector_id": sid})
    if r.get("status") == "ok":
        print(f"Sector {sid} added to avoid list.")
    else:
        _pp(r) # Fallback to pretty print full response on error

@register("avoid_remove_flow")
def avoid_remove_flow(ctx: Context):
    try:
        sid = int(input("Sector id to remove from avoid: ").strip())
    except ValueError:
        print("Invalid sector id."); return
    r = ctx.conn.rpc("nav.avoid.remove", {"sector_id": sid})
    if r.get("status") == "ok":
        print(f"Sector {sid} removed from avoid list.")
    else:
        _pp(r) # Fallback to pretty print full response on error


@register("avoid_list_flow")
def avoid_list_flow(ctx: Context):
    r = ctx.conn.rpc("nav.avoid.list", {})
    ctx.state["last_rpc"] = r
    if r.get("status") == "ok":
        data = r.get("data") or {}
        items = data.get("items") or []
        if items:
            print("Sectors to avoid: " + ", ".join(map(str, items)))
        else:
            print("No sectors in avoid list.")
    else:
        _pp(r) # Fallback to pretty print full response on error


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
        # Extract 'id' if present
        if p0.get("id"):
            port_obj["id"] = p0["id"]
        pcls = p0.get("class") or p0.get("type")
        if isinstance(pcls, int) or (isinstance(pcls, str) and pcls.isdigit()):
            port_obj["class"] = int(pcls)
        if p0.get("name"):
            port_obj["name"] = p0["name"]
    elif isinstance(d.get("port"), dict):
        port_obj = dict(d.get("port"))
        # Ensure 'id' is copied if it exists in the raw port data
        if "id" not in port_obj and d["port"].get("id"):
            port_obj["id"] = d["port"]["id"]
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
        "beacon": beacon,
        "counts": d.get("counts") # Add this line
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

def _get_menu_flags(ctx: Context) -> dict:
    d = ctx.state.get("sector_info", {})
    ships = d.get("ships", [])
    my_ship_id = get_my_ship_id(ctx.conn)
    my_name = get_my_player_name(ctx.conn)
    # keep beacon count handy for label interpolation
    ctx.state["beacon_count"] = get_my_beacon_count(ctx.conn)

    flags = {
        "has_port": bool(d.get("port")),
        "is_stardock": ctx.state.get("is_stardock", False),
        "is_shipyard_port": ctx.state.get("is_shipyard_port", False),
        "has_planet": bool(d.get("planets")),
        "can_set_beacon": (d.get("beacon") in (None, "")),
        "has_boardable": has_boardable_ship(ships, my_ship_id=my_ship_id, my_name=my_name),
        "has_tow_target": has_tow_target(ships, my_ship_id=my_ship_id),
        "on_planet": bool(ctx.state.get("on_planet")),
        "planet_has_products": bool(ctx.state.get("planet_products_available")),
        "is_corp_member": bool(ctx.player_info.get("corp_id")),
        "can_autopilot": bool(ctx.state.get("ap_route_plotted")),
        "has_exchange_access": ctx.state.get("has_exchange_access", False),
        "has_insurance_access": ctx.state.get("has_insurance_access", False),
        "has_tavern_access": ctx.state.get("has_tavern_access", False),
        "has_hardware_access": ctx.state.get("has_hardware_access", False),
        "in_corporation": ctx.state.get("in_corporation", False),
        "is_ceo": ctx.state.get("is_ceo", False),
        "is_ceo_or_officer": ctx.state.get("is_ceo_or_officer", False),
        "not_in_corporation": not ctx.state.get("in_corporation", False),
        "corp_not_public": not ctx.state.get("corp_is_public", False),
        "corp_is_public": ctx.state.get("corp_is_public", False),
    }
    return flags

def _update_corp_context(ctx: Context):
    """
    Fetches and updates corporation-related context flags in ctx.state.
    Should be called after player_info changes or a corp-related action.
    """
    ctx.state["in_corporation"] = False
    ctx.state["is_ceo"] = False
    ctx.state["is_ceo_or_officer"] = False
    ctx.state["corp_is_public"] = False
    ctx.state["corp_not_public"] = True # Default until proven otherwise

    # Fetch player_info to get basic corp membership status
    my_info_resp = ctx.conn.rpc("player.my_info", {})
    if my_info_resp.get("status") == "ok":
        ctx.player_info = get_data(my_info_resp)
    else:
        # If player_info can't be fetched, can't determine corp status
        return

    corp_data = ctx.player_info.get("corporation")
    if corp_data and isinstance(corp_data, dict):
        ctx.state["in_corporation"] = True
        
        # Fetch corp.status to get role and public status
        corp_status_resp = ctx.conn.rpc("corp.status", {})
        if corp_status_resp.get("status") == "ok":
            corp_status_data = get_data(corp_status_resp)
            if corp_status_data.get("your_role") == "Leader":
                ctx.state["is_ceo"] = True
                ctx.state["is_ceo_or_officer"] = True
            elif corp_status_data.get("your_role") == "Officer":
                ctx.state["is_ceo_or_officer"] = True

            # Check if corporation has public stock
            corp_id = corp_status_data.get("corp_id")
            if corp_id:
                # Assuming stock.exchange.list_stocks can filter by corp_id
                # Or a direct corp.stock_info command
                list_stocks_resp = ctx.conn.rpc("stock.exchange.list_stocks", {"corp_id": corp_id})
                if list_stocks_resp.get("status") == "ok":
                    stocks = get_data(list_stocks_resp).get("stocks", [])
                    if any(s.get("corp_id") == corp_id for s in stocks):
                        ctx.state["corp_is_public"] = True
                        ctx.state["corp_not_public"] = False
        else:
            print("[Warning] Could not fetch corporation status.")
    
@register("print_last_rpc")
def print_last_rpc(ctx):
    resp = ctx.state.get("last_rpc")
    if resp is None:
        print("[no response captured]")
        return
    print(json.dumps(resp, ensure_ascii=False, indent=2))

@register("pretty_print_trade_quote")
def pretty_print_trade_quote(ctx):
    resp = ctx.state.get("last_rpc")
    if resp is None:
        print("[no response captured]")
        return

    data = resp.get("data")
    if not isinstance(data, dict):
        print("[Error] Invalid trade.quote response data.")
        _pp(resp) # Fallback to raw print if data is malformed
        return

    port_id = data.get("port_id")
    commodity = data.get("commodity")
    quantity = data.get("quantity")
    buy_price = data.get("buy_price")
    sell_price = data.get("sell_price")
    total_buy_price = data.get("total_buy_price")
    total_sell_price = data.get("total_sell_price")

    print(f"\n--- Trade Quote for Port {port_id} ---")
    print(f"  Commodity: {commodity.capitalize()}")
    print(f"  Quantity:  {quantity}")
    print(f"  Buy Price (per unit):  {buy_price:.2f} credits")
    print(f"  Sell Price (per unit): {sell_price:.2f} credits")
    print(f"  Total Buy Price:   {total_buy_price} credits")
    print(f"  Total Sell Price:  {total_sell_price} credits")
@register("pretty_print_trade_receipt")
def pretty_print_trade_receipt(ctx):
    resp = ctx.state.get("last_rpc")
    if resp is None:
        print("[no response captured]")
        return

    if resp.get("status") != "ok":
        print(f"[Error] Trade failed: {resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(resp) # Fallback to raw print on error
        return

    data = resp.get("data")
    if not isinstance(data, dict):
        print("[Error] Invalid trade receipt data.")
        _pp(resp) # Fallback to raw print if data is malformed
        return

    sector_id = data.get("sector_id")
    port_id = data.get("port_id")
    player_id = data.get("player_id")
    total_cost = data.get("total_cost")
    credits_remaining = data.get("credits_remaining")
    lines = data.get("lines", [])

    print(f"\n--- Trade Receipt (Buy) ---")
    print(f"  Sector ID: {sector_id}")
    print(f"  Port ID:   {port_id}")
    print(f"  Player ID: {player_id}")
    print(f"  Total Cost: {total_cost} credits")
    print(f"  Credits Remaining: {credits_remaining} credits")
    print(f"\n  Items Purchased:")
    if lines:
        for item in lines:
            commodity = item.get("commodity")
            quantity = item.get("quantity")
            unit_price = item.get("unit_price")
            value = item.get("value")
            print(f"    - {quantity} x {commodity.capitalize()} @ {unit_price} cr/unit = {value} credits")
    else:
        print("    (No items listed)")

@register("pretty_print_sell_receipt")
def pretty_print_sell_receipt(ctx):
    resp = ctx.state.get("last_rpc")
    if resp is None:
        print("[no response captured]")
        return

    if resp.get("status") != "ok":
        print(f"[Error] Sell failed: {resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(resp) # Fallback to raw print on error
        return

    data = resp.get("data")
    if not isinstance(data, dict):
        print("[Error] Invalid sell receipt data.")
        _pp(resp) # Fallback to raw print if data is malformed
        return

    sector_id = data.get("sector_id")
    port_id = data.get("port_id")
    player_id = data.get("player_id")
    total_credits = data.get("total_credits")
    credits_remaining = data.get("credits_remaining")
    lines = data.get("lines", [])

    print(f"\n--- Trade Receipt (Sell) ---")
    print(f"  Sector ID: {sector_id}")
    print(f"  Port ID:   {port_id}")
    print(f"  Player ID: {player_id}")
    print(f"  Total Credits Gained: {total_credits} credits")
    print(f"  Credits Remaining: {credits_remaining} credits")
    print(f"\n  Items Sold:")
    if lines:
        for item in lines:
            commodity = item.get("commodity")
            quantity = item.get("quantity")
            unit_price = item.get("unit_price")
            value = item.get("value")
            print(f"    - {quantity} x {commodity.capitalize()} @ {unit_price} cr/unit = {value} credits")
    else:
        print("    (No items listed)")
@register("pretty_print_shipyard_list")
def pretty_print_shipyard_list(ctx):
    resp = ctx.state.get("last_rpc")
    if resp is None:
        print("[no response captured]")
        return

    if resp.get("status") != "ok":
        print(f"[Error] Failed to get shipyard list: {resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(resp)
        return

    data = resp.get("data")
    if not isinstance(data, dict):
        print("[Error] Invalid shipyard list data.")
        _pp(resp)
        return

    current_ship = data.get("current_ship")
    available_ships = data.get("available", [])

    print("\n--- Shipyard List ---")
    if current_ship:
        print(f"Your current ship: {current_ship.get('type')} (Trade-in Value: {current_ship.get('trade_in_value')} cr)")
    else:
        print("You don't have a ship or it could not be identified.")

    print("\nAvailable for Purchase/Upgrade:")
    if available_ships:
        for ship in available_ships:
            eligible_status = "Eligible" if ship.get("eligible") else "Ineligible"
            reasons = f" ({', '.join(ship.get('reasons'))})" if ship.get("reasons") else ""
            print(f"  - {ship.get('name')} (Price: {ship.get('shipyard_price')} cr, Net Cost: {ship.get('net_cost')} cr) - {eligible_status}{reasons}")
    else:
        print("No ships available at this shipyard.")

@register("pretty_print_hardware_list")
def pretty_print_hardware_list(ctx):
    resp = ctx.state.get("last_rpc")
    if resp is None:
        print("[no response captured]")
        return

    if resp.get("status") != "ok":
        print(f"[Error] Failed to get hardware list: {resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(resp)
        return

    data = resp.get("data")
    if not isinstance(data, dict) or "hardware" not in data:
        print("[Error] Invalid hardware list data.")
        _pp(resp)
        return

    hardware_items = data.get("hardware", [])
    if hardware_items:
        print("\n--- Available Hardware ---")
        for item in hardware_items:
            print(f"  - {item.get('name')} ({item.get('item_code')}) - Price: {item.get('price')} cr - {item.get('description')}")
    else:
        print("No hardware available at this location.")

@register("hardware_buy_flow")
def hardware_buy_flow(ctx: Context):
    item_code = input("Enter item code to buy: ").strip().upper()
    if not item_code:
        print("Cancelled.")
        return

    try:
        quantity = int(input("Enter quantity: ").strip())
        if quantity <= 0:
            print("Quantity must be positive.")
            return
    except ValueError:
        print("Invalid quantity.")
        return

    payload = {
        "item_code": item_code,
        "quantity": quantity
    }

    resp = ctx.conn.rpc("hardware.buy", payload)
    ctx.state["last_rpc"] = resp

    if resp.get("status") == "ok":
        print(f"Successfully purchased {quantity} units of {item_code}.")
        _pp(resp)
        # Refresh player info to update cargo/equipment display
        my_info_resp = ctx.conn.rpc("player.my_info", {})
        if my_info_resp.get("status") == "ok":
            ctx.player_info = get_data(my_info_resp)
        else:
            print("[Warning] Could not refresh player info after hardware purchase.")
    else:
        print(f"[Error] Hardware purchase failed: {resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(resp)

@register("pretty_print_bank_history")
def pretty_print_bank_history(ctx):
    resp = ctx.state.get("last_rpc")
    if resp is None:
        print("[no response captured]")
        return

    if resp.get("status") != "ok":
        print(f"[Error] Failed to get bank history: {resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(resp)
        return

    data = resp.get("data")
    if not isinstance(data, dict) or "history" not in data:
        print("[Error] Invalid bank history data.")
        _pp(resp)
        return

    history_items = data.get("history", [])
    if history_items:
        print("\n--- Bank Transaction History ---")
        for item in history_items:
            ts = item.get("timestamp")
            if isinstance(ts, (int, float)):
                try:
                    import datetime
                    dt_object = datetime.datetime.fromtimestamp(ts, tz=datetime.timezone.utc)
                    formatted_ts = dt_object.isoformat(timespec='seconds')
                except Exception:
                    formatted_ts = str(ts)
            else:
                formatted_ts = str(ts)

            print(f"  ID: {item.get('id')} | Type: {item.get('type')} | Direction: {item.get('direction')}")
            print(f"    Amount: {item.get('amount') / 100.0:.2f} {item.get('currency')} | Balance After: {item.get('balance_after') / 100.0:.2f}")
            print(f"    Timestamp: {formatted_ts} | Desc: {item.get('description') or 'N/A'}")
            print("-" * 30)
        if data.get("has_next_page"):
            print(f"More history available. Next cursor: {data.get('next_cursor')}")
    else:
        print("No bank transaction history available.")

@register("pretty_print_bank_leaderboard")
def pretty_print_bank_leaderboard(ctx):
    resp = ctx.state.get("last_rpc")
    if resp is None:
        print("[no response captured]")
        return

    if resp.get("status") != "ok":
        print(f"[Error] Failed to get bank leaderboard: {resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(resp)
        return

    data = resp.get("data")
    if not isinstance(data, dict) or "leaderboard" not in data:
        print("[Error] Invalid bank leaderboard data.")
        _pp(resp)
        return

    leaderboard_items = data.get("leaderboard", [])
    if leaderboard_items:
        print("\n--- Bank Wealth Leaderboard ---")
        print(" Rank | Player Name       | Balance")
        print("------|-------------------|------------")
        for i, item in enumerate(leaderboard_items):
            # Assuming balance is in minor units and needs conversion
            balance_display = f"{item.get('balance') / 100.0:.2f}" if isinstance(item.get('balance'), (int, float)) else str(item.get('balance'))
            print(f" {i+1:<4} | {item.get('player_name', 'Unknown'):<17} | {balance_display}")
    else:
        print("No bank leaderboard data available.")

@register("pretty_print_corp_status")
def pretty_print_corp_status(ctx):
    resp = ctx.state.get("last_rpc")
    if resp is None:
        print("[no response captured]")
        return

    if resp.get("status") != "ok":
        print(f"[Error] Failed to get corporation status: {resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(resp)
        return

    data = resp.get("data")
    if not isinstance(data, dict):
        print("[Error] Invalid corporation status data.")
        _pp(resp)
        return

    print("\n--- Corporation Status ---")
    print(f"  Name: {data.get('name', 'N/A')} ({data.get('tag', 'N/A')})")
    print(f"  ID: {data.get('corp_id', 'N/A')}")
    print(f"  CEO Player ID: {data.get('ceo_id', 'N/A')}")
    print(f"  Member Count: {data.get('member_count', 'N/A')}")
    print(f"  Your Role: {data.get('your_role', 'N/A')}")
    print(f"  Created At: {data.get('created_at', 'N/A')}")
    # Add other relevant fields if they exist in the response
    print("--------------------------")

@register("pretty_print_corp_roster")
def pretty_print_corp_roster(ctx):
    resp = ctx.state.get("last_rpc")
    if resp is None:
        print("[no response captured]")
        return

    if resp.get("status") != "ok":
        print(f"[Error] Failed to get corporation roster: {resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(resp)
        return

    data = resp.get("data")
    if not isinstance(data, dict) or "roster" not in data:
        print("[Error] Invalid corporation roster data.")
        _pp(resp)
        return

    roster_items = data.get("roster", [])
    if roster_items:
        print(f"\n--- Corporation Roster for Corp ID {data.get('corp_id', 'N/A')} ---")
        print(" Player Name       | Role    | Player ID")
        print("-------------------|---------|-----------")
        for item in roster_items:
            print(f" {item.get('name', 'Unknown'):<17} | {item.get('role', 'N/A'):<7} | {item.get('player_id', 'N/A')}")
    else:
        print("No members in this corporation.")
    print("---------------------------------------")

@register("pretty_print_corp_list")
def pretty_print_corp_list(ctx):
    resp = ctx.state.get("last_rpc")
    if resp is None:
        print("[no response captured]")
        return

    if resp.get("status") != "ok":
        print(f"[Error] Failed to get corporation list: {resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(resp)
        return

    data = resp.get("data")
    if not isinstance(data, dict) or "corporations" not in data:
        print("[Error] Invalid corporation list data.")
        _pp(resp)
        return

    corporations = data.get("corporations", [])
    if corporations:
        print("\n--- All Corporations ---")
        print(" Name                  | Tag   | CEO                | Members | ID")
        print("-----------------------|-------|--------------------|---------|-----------")
        for corp in corporations:
            print(f" {corp.get('name', 'N/A'):<21} | {corp.get('tag', 'N/A'):<5} | {corp.get('ceo_name', 'Unknown'):<18} | {corp.get('member_count', 'N/A'):<7} | {corp.get('corp_id', 'N/A')}")
    else:
        print("No corporations found.")
    print("---------------------------------------------------------------------")

@register("pretty_print_corp_balance")
def pretty_print_corp_balance(ctx):
    resp = ctx.state.get("last_rpc")
    if resp is None:
        print("[no response captured]")
        return

    if resp.get("status") != "ok":
        print(f"[Error] Failed to get corporation balance: {resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(resp)
        return

    data = resp.get("data")
    if not isinstance(data, dict) or "balance" not in data:
        print("[Error] Invalid corporation balance data.")
        _pp(resp)
        return

    balance = data.get("balance")
    balance_display = f"{int(balance) / 100.0:.2f}" if isinstance(balance, (int, float)) else str(balance)

    print("\n--- Corporation Treasury Balance ---")
    print(f"  Current Balance: {balance_display} credits")
    print("----------------------------------")

@register("pretty_print_corp_statement")
def pretty_print_corp_statement(ctx):
    resp = ctx.state.get("last_rpc")
    if resp is None:
        print("[no response captured]")
        return

    if resp.get("status") != "ok":
        print(f"[Error] Failed to get corporation statement: {resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(resp)
        return

    data = resp.get("data")
    if not isinstance(data, dict) or "transactions" not in data:
        print("[Error] Invalid corporation statement data.")
        _pp(resp)
        return

    transactions = data.get("transactions", [])
    if transactions:
        print("\n--- Corporation Treasury Statement ---")
        print(" ID   | Timestamp          | Type       | Amount       | Balance After")
        print("------|--------------------|------------|--------------|---------------")
        for txn in transactions:
            ts = txn.get("ts")
            if isinstance(ts, (int, float)):
                try:
                    import datetime
                    dt_object = datetime.datetime.fromtimestamp(ts, tz=datetime.timezone.utc)
                    formatted_ts = dt_object.isoformat(timespec='seconds')
                except Exception:
                    formatted_ts = str(ts)
            else:
                formatted_ts = str(ts)
            
            amount_display = f"{int(txn.get('amount')) / 100.0:.2f}" if isinstance(txn.get('amount'), (int, float)) else str(txn.get('amount'))
            balance_display = f"{int(txn.get('balance')) / 100.0:.2f}" if isinstance(txn.get('balance'), (int, float)) else str(txn.get('balance'))

            print(f" {str(txn.get('id')):<4} | {formatted_ts:<18} | {txn.get('type', 'N/A'):<10} | {amount_display:<12} | {balance_display}")
        if data.get("has_next_page"):
            print(f"More transactions available. Next cursor: {data.get('next_cursor')}")
    else:
        print("No transactions in corporation statement.")
    print("---------------------------------------------------------------------")

@register("pretty_print_stock_ipo_register")
def pretty_print_stock_ipo_register(ctx):
    resp = ctx.state.get("last_rpc")
    if resp is None:
        print("[no response captured]")
        return
    
    if resp.get("status") != "ok":
        print(f"[Error] Failed to register IPO: {resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(resp)
        return

    data = resp.get("data")
    if not isinstance(data, dict):
        print("[Error] Invalid IPO registration data.")
        _pp(resp)
        return

    print("\n--- IPO Registration Successful ---")
    print(f"  Corporation ID: {data.get('corp_id')}")
    print(f"  Stock ID: {data.get('stock_id')}")
    print(f"  Ticker: {data.get('ticker')}")
    print(f"  Message: {data.get('message')}")
    print("---------------------------------")

@register("pretty_print_stock_buy")
def pretty_print_stock_buy(ctx):
    resp = ctx.state.get("last_rpc")
    if resp is None:
        print("[no response captured]")
        return
    
    if resp.get("status") != "ok":
        print(f"[Error] Failed to buy stock: {resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(resp)
        return

    data = resp.get("data")
    if not isinstance(data, dict):
        print("[Error] Invalid stock purchase data.")
        _pp(resp)
        return

    print("\n--- Stock Purchase Successful ---")
    print(f"  Stock ID: {data.get('stock_id')}")
    print(f"  Ticker: {data.get('ticker')}")
    print(f"  Quantity: {data.get('quantity')}")
    print(f"  Total Cost: {data.get('total_cost')}")
    print(f"  Message: {data.get('message')}")
    print("-------------------------------")

@register("pretty_print_stock_dividend_set")
def pretty_print_stock_dividend_set(ctx):
    resp = ctx.state.get("last_rpc")
    if resp is None:
        print("[no response captured]")
        return
    
    if resp.get("status") != "ok":
        print(f"[Error] Failed to declare dividend: {resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(resp)
        return

    data = resp.get("data")
    if not isinstance(data, dict):
        print("[Error] Invalid dividend declaration data.")
        _pp(resp)
        return

    print("\n--- Dividend Declared ---")
    print(f"  Stock ID: {data.get('stock_id')}")
    print(f"  Amount per share: {data.get('amount_per_share')}")
    print(f"  Total Payout: {data.get('total_payout')}")
    print(f"  Message: {data.get('message')}")
    print("-------------------------")

@register("pretty_print_stock_dividend_set")
def pretty_print_stock_dividend_set(ctx):
    resp = ctx.state.get("last_rpc")
    if resp is None:
        print("[no response captured]")
        return
    
    if resp.get("status") != "ok":
        print(f"[Error] Failed to declare dividend: {resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(resp)
        return

    data = resp.get("data")
    if not isinstance(data, dict):
        print("[Error] Invalid dividend declaration data.")
        _pp(resp)
        return

    print("\n--- Dividend Declared ---")
    print(f"  Stock ID: {data.get('stock_id')}")
    print(f"  Amount per share: {data.get('amount_per_share')}")
    print(f"  Total Payout: {data.get('total_payout')}")
    print(f"  Message: {data.get('message')}")
    print("-------------------------")

@register("corp_create_flow")
def corp_create_flow(ctx: Context):
    if ctx.state.get("in_corporation"):
        print("You are already in a corporation. You must leave or dissolve it first.")
        return

    corp_name = input("Enter new corporation name: ").strip()
    if not corp_name:
        print("Corporation name cannot be empty. Cancelled.")
        return
    
    corp_tag = input("Enter corporation tag (3-5 characters): ").strip().upper()
    if not (3 <= len(corp_tag) <= 5):
        print("Corporation tag must be 3-5 characters. Cancelled.")
        return

    resp = ctx.conn.rpc("corp.create", {"name": corp_name, "tag": corp_tag})
    ctx.state["last_rpc"] = resp

    if resp.get("status") == "ok":
        print("Corporation created successfully!")
        _pp(resp)
        # Refresh player info to update corporation status
        my_info_resp = ctx.conn.rpc("player.my_info", {})
        if my_info_resp.get("status") == "ok":
            ctx.player_info = get_data(my_info_resp)
            _update_corp_context(ctx) # Update context flags
    else:
        print(f"[Error] Failed to create corporation: {resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(resp)

@register("corp_join_flow")
def corp_join_flow(ctx: Context):
    if ctx.state.get("in_corporation"):
        print("You are already in a corporation.")
        return
    
    corp_id_str = input("Enter Corporation ID to join: ").strip()
    if not corp_id_str:
        print("Cancelled.")
        return
    try:
        corp_id = int(corp_id_str)
    except ValueError:
        print("Invalid Corporation ID.")
        return

    resp = ctx.conn.rpc("corp.join", {"corp_id": corp_id})
    ctx.state["last_rpc"] = resp

    if resp.get("status") == "ok":
        print(f"Successfully joined corporation ID {corp_id}.")
        _pp(resp)
        my_info_resp = ctx.conn.rpc("player.my_info", {})
        if my_info_resp.get("status") == "ok":
            ctx.player_info = get_data(my_info_resp)
            _update_corp_context(ctx)
    else:
        print(f"[Error] Failed to join corporation: {resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(resp)

@register("corp_leave_flow")
def corp_leave_flow(ctx: Context):
    if not ctx.state.get("in_corporation"):
        print("You are not currently in a corporation.")
        return
    
    if ctx.state.get("is_ceo"):
        print("As CEO, you cannot simply leave. You must first transfer CEO role or dissolve the corporation.")
        return

    confirm = input("Are you sure you want to leave your corporation? (y/N): ").strip().lower()
    if confirm != "y":
        print("Cancelled.")
        return

    resp = ctx.conn.rpc("corp.leave", {})
    ctx.state["last_rpc"] = resp

    if resp.get("status") == "ok":
        print("Successfully left the corporation.")
        _pp(resp)
        my_info_resp = ctx.conn.rpc("player.my_info", {})
        if my_info_resp.get("status") == "ok":
            ctx.player_info = get_data(my_info_resp)
            _update_corp_context(ctx)
    else:
        print(f"[Error] Failed to leave corporation: {resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(resp)

@register("corp_kick_flow")
def corp_kick_flow(ctx: Context):
    if not ctx.state.get("in_corporation"):
        print("You are not in a corporation.")
        return
    if not ctx.state.get("is_ceo_or_officer"):
        print("You must be a CEO or Officer to kick members.")
        return

    target_player_id_str = input("Enter Player ID to kick: ").strip()
    if not target_player_id_str:
        print("Cancelled.")
        return
    try:
        target_player_id = int(target_player_id_str)
    except ValueError:
        print("Invalid Player ID.")
        return
    
    # Optional: confirm target name
    resp_roster = ctx.conn.rpc("corp.roster", {})
    roster = resp_roster.get("data", {}).get("roster", [])
    target_name = "Unknown Player"
    for member in roster:
        if member.get("player_id") == target_player_id:
            target_name = member.get("name")
            break

    confirm = input(f"Are you sure you want to kick {target_name} (ID: {target_player_id})? (y/N): ").strip().lower()
    if confirm != "y":
        print("Cancelled.")
        return

    resp = ctx.conn.rpc("corp.kick", {"target_player_id": target_player_id})
    ctx.state["last_rpc"] = resp

    if resp.get("status") == "ok":
        print(f"Successfully kicked {target_name} from the corporation.")
        _pp(resp)
    else:
        print(f"[Error] Failed to kick member: {resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(resp)

@register("corp_invite_flow")
def corp_invite_flow(ctx: Context):
    if not ctx.state.get("in_corporation"):
        print("You are not in a corporation.")
        return
    if not ctx.state.get("is_ceo_or_officer"):
        print("You must be a CEO or Officer to invite players.")
        return

    target_player_id_str = input("Enter Player ID to invite: ").strip()
    if not target_player_id_str:
        print("Cancelled.")
        return
    try:
        target_player_id = int(target_player_id_str)
    except ValueError:
        print("Invalid Player ID.")
        return

    resp = ctx.conn.rpc("corp.invite", {"target_player_id": target_player_id})
    ctx.state["last_rpc"] = resp

    if resp.get("status") == "ok":
        print(f"Successfully invited player ID {target_player_id}.")
        _pp(resp)
    else:
        print(f"[Error] Failed to invite player: {resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(resp)

@register("corp_deposit_flow")
def corp_deposit_flow(ctx: Context):
    if not ctx.state.get("in_corporation"):
        print("You are not in a corporation.")
        return

    amount_str = input("Enter amount to deposit to corporation treasury: ").strip()
    if not amount_str:
        print("Cancelled.")
        return
    try:
        amount = int(float(amount_str) * 100) # Convert to minor units
    except ValueError:
        print("Invalid amount.")
        return

    resp = ctx.conn.rpc("corp.deposit", {"amount": amount})
    ctx.state["last_rpc"] = resp

    if resp.get("status") == "ok":
        print(f"Successfully deposited {amount_str} credits to corporation treasury.")
        _pp(resp)
    else:
        print(f"[Error] Failed to deposit to treasury: {resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(resp)

@register("corp_withdraw_flow")
def corp_withdraw_flow(ctx: Context):
    if not ctx.state.get("in_corporation"):
        print("You are not in a corporation.")
        return
    if not ctx.state.get("is_ceo_or_officer"):
        print("You must be a CEO or Officer to withdraw from treasury.")
        return

    amount_str = input("Enter amount to withdraw from corporation treasury: ").strip()
    if not amount_str:
        print("Cancelled.")
        return
    try:
        amount = int(float(amount_str) * 100) # Convert to minor units
    except ValueError:
        print("Invalid amount.")
        return

    resp = ctx.conn.rpc("corp.withdraw", {"amount": amount})
    ctx.state["last_rpc"] = resp

    if resp.get("status") == "ok":
        print(f"Successfully withdrew {amount_str} credits from corporation treasury.")
        _pp(resp)
    else:
        print(f"[Error] Failed to withdraw from treasury: {resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(resp)

@register("corp_dissolve_flow")
def corp_dissolve_flow(ctx: Context):
    if not ctx.state.get("is_ceo"):
        print("You must be the CEO to dissolve the corporation.")
        return

    corp_name = ctx.player_info.get("corporation", {}).get("name")
    confirm = input(f"Are you absolutely sure you want to dissolve '{corp_name}'? This action cannot be undone. (y/N): ").strip().lower()
    if confirm != "y":
        print("Cancelled.")
        return
    
    confirm2 = input(f"Type '{corp_name}' to confirm dissolution: ").strip()
    if confirm2 != corp_name:
        print("Confirmation failed. Cancelled.")
        return

    resp = ctx.conn.rpc("corp.dissolve", {})
    ctx.state["last_rpc"] = resp

    if resp.get("status") == "ok":
        print(f"Corporation '{corp_name}' dissolved successfully.")
        _pp(resp)
        my_info_resp = ctx.conn.rpc("player.my_info", {})
        if my_info_resp.get("status") == "ok":
            ctx.player_info = get_data(my_info_resp)
            _update_corp_context(ctx)
    else:
        print(f"[Error] Failed to dissolve corporation: {resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(resp)

@register("corp_transfer_ceo_flow")
def corp_transfer_ceo_flow(ctx: Context):
    if not ctx.state.get("is_ceo"):
        print("You must be the CEO to transfer leadership.")
        return
    
    target_player_id_str = input("Enter Player ID of new CEO: ").strip()
    if not target_player_id_str:
        print("Cancelled.")
        return
    try:
        target_player_id = int(target_player_id_str)
    except ValueError:
        print("Invalid Player ID.")
        return

    # Optional: confirm target name
    resp_roster = ctx.conn.rpc("corp.roster", {})
    roster = resp_roster.get("data", {}).get("roster", [])
    target_name = "Unknown Player"
    for member in roster:
        if member.get("player_id") == target_player_id:
            target_name = member.get("name")
            break
    
    confirm = input(f"Are you sure you want to transfer CEO role to {target_name} (ID: {target_player_id})? (y/N): ").strip().lower()
    if confirm != "y":
        print("Cancelled.")
        return

    resp = ctx.conn.rpc("corp.transfer_ceo", {"target_player_id": target_player_id})
    ctx.state["last_rpc"] = resp

    if resp.get("status") == "ok":
        print(f"CEO role transferred to {target_name} successfully.")
        _pp(resp)
        my_info_resp = ctx.conn.rpc("player.my_info", {})
        if my_info_resp.get("status") == "ok":
            ctx.player_info = get_data(my_info_resp)
            _update_corp_context(ctx)
    else:
        print(f"[Error] Failed to transfer CEO role: {resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(resp)

@register("stock_ipo_register_flow")
def stock_ipo_register_flow(ctx: Context):
    if not ctx.state.get("is_ceo"):
        print("Only the CEO can register for an IPO.")
        return
    if ctx.state.get("corp_is_public"):
        print("Your corporation is already publicly traded.")
        return
    
    ticker = input("Enter stock ticker (3-5 characters, e.g., 'IBM'): ").strip().upper()
    if not (3 <= len(ticker) <= 5):
        print("Ticker must be 3-5 characters. Cancelled.")
        return
    
    try:
        total_shares = int(input("Enter total number of shares to issue: ").strip())
        if total_shares <= 0:
            print("Total shares must be positive.")
            return
        par_value = int(float(input("Enter par value per share (e.g., 100 for 1.00): ").strip()) * 100)
        if par_value <= 0:
            print("Par value must be positive.")
            return
    except ValueError:
        print("Invalid number. Cancelled.")
        return

    resp = ctx.conn.rpc("stock.ipo.register", {
        "ticker": ticker,
        "total_shares": total_shares,
        "par_value": par_value
    })
    ctx.state["last_rpc"] = resp

    if resp.get("status") == "ok":
        print("IPO registered successfully!")
        _pp(resp)
        _update_corp_context(ctx) # Refresh corp context
    else:
        print(f"[Error] Failed to register IPO: {resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(resp)

@register("stock_buy_flow")
def stock_buy_flow(ctx: Context):
    if not ctx.state.get("corp_is_public"):
        print("Cannot buy stock: your corporation is not publicly traded.")
        return

    try:
        stock_id = int(input("Enter Stock ID to buy: ").strip())
        quantity = int(input("Enter quantity to buy: ").strip())
        if quantity <= 0:
            print("Quantity must be positive.")
            return
    except ValueError:
        print("Invalid input.")
        return

    resp = ctx.conn.rpc("stock.buy", {"stock_id": stock_id, "quantity": quantity})
    ctx.state["last_rpc"] = resp

    if resp.get("status") == "ok":
        print("Stock purchased successfully!")
        _pp(resp)
    else:
        print(f"[Error] Failed to buy stock: {resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(resp)

@register("stock_dividend_set_flow")
def stock_dividend_set_flow(ctx: Context):
    if not ctx.state.get("is_ceo"):
        print("Only the CEO can declare dividends.")
        return
    if not ctx.state.get("corp_is_public"):
        print("Cannot declare dividend: your corporation is not publicly traded.")
        return

    try:
        stock_id = int(input("Enter Stock ID to declare dividend for: ").strip())
        amount_per_share = int(float(input("Enter amount per share (e.g., 0.50 for 50 cents): ").strip()) * 100)
        if amount_per_share <= 0:
            print("Amount per share must be positive.")
            return
    except ValueError:
        print("Invalid input.")
        return

    resp = ctx.conn.rpc("stock.dividend.set", {"stock_id": stock_id, "amount_per_share": amount_per_share})
    ctx.state["last_rpc"] = resp

    if resp.get("status") == "ok":
        print("Dividend declared successfully!")
        _pp(resp)
    else:
        print(f"[Error] Failed to declare dividend: {resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(resp)

@register("insurance_buy_flow")
def insurance_buy_flow(ctx: Context):
    policy_id = input("Enter policy ID to buy: ").strip()
    if not policy_id:
        print("Cancelled.")
        return

    resp = ctx.conn.rpc("insurance.policies.buy", {"policy_id": policy_id})
    ctx.state["last_rpc"] = resp

    if resp.get("status") == "ok":
        print(f"Successfully purchased insurance policy {policy_id}.")
        _pp(resp)
    else:
        print(f"[Error] Failed to buy insurance policy: {resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(resp)

@register("insurance_claim_flow")
def insurance_claim_flow(ctx: Context):
    active_policy_id = input("Enter active policy ID to file a claim against: ").strip()
    if not active_policy_id:
        print("Cancelled.")
        return

    details = input("Enter claim details: ").strip()
    if not details:
        print("Claim details cannot be empty. Cancelled.")
        return

    resp = ctx.conn.rpc("insurance.claim.file", {"active_policy_id": active_policy_id, "details": details})
    ctx.state["last_rpc"] = resp

    if resp.get("status") == "ok":
        print(f"Successfully filed claim for policy {active_policy_id}.")
        _pp(resp)
    else:
        print(f"[Error] Failed to file insurance claim: {resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(resp)

@register("tavern_graffiti_post_flow")
def tavern_graffiti_post_flow(ctx: Context):
    message = input("Enter your graffiti message (max 255 chars): ").strip()
    if not message:
        print("Cancelled.")
        return
    if len(message) > 255:
        print("Message too long (max 255 chars). Cancelled.")
        return

    resp = ctx.conn.rpc("tavern.graffiti.post", {"message": message})
    ctx.state["last_rpc"] = resp

    if resp.get("status") == "ok":
        print("Graffiti posted successfully.")
        _pp(resp)
    else:
        print(f"[Error] Failed to post graffiti: {resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(resp)

@register("tavern_rumour_get_hint_flow")
def tavern_rumour_get_hint_flow(ctx: Context):
    resp = ctx.conn.rpc("tavern.rumour.get_hint", {})
    ctx.state["last_rpc"] = resp

    if resp.get("status") == "ok":
        hint = resp.get("data", {}).get("hint")
        if hint:
            print("\n--- Rumour Mill Hint ---")
            print(hint)
            print("------------------------")
        else:
            print("No hint received from the rumour mill.")
        _pp(resp)
    else:
        print(f"[Error] Failed to get rumour hint: {resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(resp)

@register("disconnect_and_quit")

def disconnect_and_quit(ctx):
    try:
        resp = ctx.conn.rpc("system.disconnect", {})
        ctx.state["last_rpc"] = resp
        print(json.dumps(resp, ensure_ascii=False, indent=2))
    except Exception as e:
        print(f"disconnect error: {e}")
    sys.exit(0)

# --- BEGIN AUTO-ADDED CAPS_HELPERS ---
def _ctx_capabilities_get(ctx, path, default=None):
    """Resolve dotted path from ctx.capabilities (dict)."""
    cur = getattr(ctx, 'capabilities', None) or {}
    for part in path.split("."):
        if not isinstance(cur, dict):
            return default
        cur = cur.get(part)
    return default if cur is None else cur
# --- END AUTO-ADDED CAPS_HELPERS ---

# add near the top of the file (or just before dispatch_action)
def _run_post(ctx, post):

    if not post:
        return
    if isinstance(post, str):
        # call a named handler like "print_last_rpc"
        call_handler(post, ctx)
    elif isinstance(post, dict):
        # allow a nested action, e.g. {"rpc": {...}, "post": "print_last_rpc"}
        dispatch_action(ctx, post)
    elif isinstance(post, list):
        for item in post:
            _run_post(ctx, item)
    # else: ignore unknown types


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
                val = int(raw) if typ == "int" else (float(raw) if typ == "float" else raw)
            except Exception:
                print(f"Invalid {typ}.")
                return None
            # Convenience: if prompting for commodity, accept short codes (ORE, ORG, EQU)
            if typ == 'str' and 'commodity' in (msg or '').lower():
                if isinstance(val, str):
                    v = val.strip().lower()
                    if v in ('ore', 'o'):
                        return 'ore'
                    if v in ('org', 'orgs', 'organ', 'organics', 'organi'):
                        return 'organics'
                    if v in ('equ', 'equip', 'equipment', 'e'):
                        return 'equipment'
            return val
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
    # run optional on-enter hook for this menu
    try:
        menu_on_enter(ctx, menu)
    except Exception:
        pass
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
                if option_visible(opt, flags) and _option_visible_with_ctx(ctx, opt):
                    print(" ", _interp_label(opt["label"], ctx))
    else:
        for opt in menu.get("options", []):
            if option_visible(opt, flags) and _option_visible_with_ctx(ctx, opt):
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
    # if "pycall" in action:
    #     call_handler(action["pycall"], ctx)
    #     if post := action.get("post"):
    #         call_handler(post, ctx)
    #     return
    if "pycall" in action:
        call_handler(action["pycall"], ctx)
        _run_post(ctx, action.get("post"))
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
        _run_post(ctx, action.get("post"))
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

    # Display deployed fighters if available
    counts = d.get("counts")
    if isinstance(counts, dict):
        fighters = counts.get("fighters")
        if isinstance(fighters, int) and fighters > 0:
            print(f"Deployed Fighters: {fighters}")

    # Display deployed mines if available
    if isinstance(counts, dict):
        mines = counts.get("mines")
        if isinstance(mines, int) and mines > 0:
            print(f"Deployed Mines: {mines}")

    if planets:
        print("Planets: " + ", ".join(pl.get("name") or "Unnamed" for pl in planets))
    else:
        print("Planets: none")

    print("Beacon: " + (beacon if beacon else "none"))

@register("refresh_sector_and_display")
def refresh_sector_and_display(ctx: Context):
    """Fetches the latest sector data, updates context, and then redisplays the sector."""
    if ctx.current_sector_id is None:
        print("Cannot refresh sector: current sector ID is unknown.")
        return

    resp = ctx.conn.rpc("move.describe_sector", {"sector_id": ctx.current_sector_id})
    if resp.get("status") == "ok":
        ctx.last_sector_desc = normalize_sector(get_data(resp))
        redisplay_sector(ctx)
    else:
        print(f"[Error] Failed to refresh sector: {resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(resp) # Fallback to pretty print full response on error

@register("recall_fighters_flow")
def recall_fighters_flow(ctx: Context):
    if ctx.current_sector_id is None:
        print("Cannot recall fighters: current sector ID is unknown.")
        return

    # 1. List deployed fighters
    list_resp = ctx.conn.rpc("deploy.fighters.list", {"sector_id": ctx.current_sector_id})
    if list_resp.get("status") != "ok":
        print(f"[Error] Failed to list deployed fighters: {list_resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(list_resp)
        return

    fighters_data = get_data(list_resp)
    
    fighters_list = fighters_data.get("entries", [])
    
    if not fighters_list:
        print("No fighters deployed in this sector to recall.")
        return

    print("\n--- Deployed Fighters ---")
    for f in fighters_list:
        print(f"  Asset ID: {f.get('asset_id')}, Quantity: {f.get('count')}, Offensive Setting: {f.get('offense_mode')}")
    print("-------------------------")

    # 2. Prompt user for asset_id
    asset_id_str = input("Enter Asset ID to recall (or 'q' to cancel): ").strip()
    if asset_id_str.lower() == 'q':
        print("Recall cancelled.")
        return

    try:
        asset_id = int(asset_id_str)
    except ValueError:
        print("Invalid Asset ID. Please enter a number.")
        return

    # 3. Execute recall RPC
    recall_resp = ctx.conn.rpc("fighters.recall", {
        "sector_id": ctx.current_sector_id,
        "asset_id": asset_id
    })

    if recall_resp.get("status") == "ok":
        print("Fighters recalled successfully.")
        refresh_sector_and_display(ctx)
    else:
        print(f"[Error] Failed to recall fighters: {recall_resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(recall_resp)

@register("deploy_mines_flow")
def deploy_mines_flow(ctx: Context):
    if ctx.current_sector_id is None:
        print("Cannot deploy mines: current sector ID is unknown.")
        return

    # Prompt user for amount and offensive setting
    amount_str = input("Amount to deploy: ").strip()
    if not amount_str.isdigit():
        print("Invalid amount. Please enter a number.")
        return
    amount = int(amount_str)

    offense_str = input("Offensive setting (1=TOLL, 2=DEFEND, 3=ATTACK): ").strip()
    if not offense_str.isdigit() or int(offense_str) not in [1, 2, 3]:
        print("Invalid offensive setting. Please choose 1, 2, or 3.")
        return
    offense = int(offense_str)

    deploy_resp = ctx.conn.rpc("combat.deploy_mines", {
        "amount": amount,
        "offense": offense
    })

    if deploy_resp.get("status") == "ok":
        print("Mines deployed successfully.")
        refresh_sector_and_display(ctx)
    else:
        print(f"[Error] Failed to deploy mines: {deploy_resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(deploy_resp)

@register("recall_mines_flow")
def recall_mines_flow(ctx: Context):
    if ctx.current_sector_id is None:
        print("Cannot recall mines: current sector ID is unknown.")
        return

    # 1. List deployed mines
    list_resp = ctx.conn.rpc("deploy.mines.list", {"sector_id": ctx.current_sector_id})
    if list_resp.get("status") != "ok":
        print(f"[Error] Failed to list deployed mines: {list_resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(list_resp)
        return

    mines_data = get_data(list_resp)
    mines_list = mines_data.get("entries", [])
    if not mines_list:
        print("No mines deployed in this sector to recall.")
        return

    print("\n--- Deployed Mines ---")
    for m in mines_list:
        print(f"  Asset ID: {m.get('asset_id')}, Quantity: {m.get('count')}, Offensive Setting: {m.get('offense_mode')}")
    print("----------------------")

    # 2. Prompt user for asset_id
    asset_id_str = input("Enter Asset ID to recall (or 'q' to cancel): ").strip()
    if asset_id_str.lower() == 'q':
        print("Recall cancelled.")
        return

    try:
        asset_id = int(asset_id_str)
    except ValueError:
        print("Invalid Asset ID. Please enter a number.")
        return

    # 3. Execute recall RPC
    recall_resp = ctx.conn.rpc("mines.recall", {
        "sector_id": ctx.current_sector_id,
        "asset_id": asset_id
    })

    if recall_resp.get("status") == "ok":
        print("Mines recalled successfully.")
        refresh_sector_and_display(ctx)
    else:
        print(f"[Error] Failed to recall mines: {recall_resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(recall_resp)

@register("list_deployed_fighters_flow")
def list_deployed_fighters_flow(ctx: Context):
    if ctx.current_sector_id is None:
        print("Cannot list fighters: current sector ID is unknown.")
        return

    list_resp = ctx.conn.rpc("deploy.fighters.list", {"sector_id": ctx.current_sector_id})
    if list_resp.get("status") != "ok":
        print(f"[Error] Failed to list deployed fighters: {list_resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(list_resp)
        return

    fighters_data = get_data(list_resp)
    fighters_list = fighters_data.get("entries", [])

    if not fighters_list:
        print("No fighters deployed in this sector.")
        return

    print("\n--- Deployed Fighters ---")
    for f in fighters_list:
        print(f"  Asset ID: {f.get('asset_id')}, Quantity: {f.get('count')}, Offensive Setting: {f.get('offense_mode')}")
    print("-------------------------")

@register("list_deployed_mines_flow")
def list_deployed_mines_flow(ctx: Context):
    if ctx.current_sector_id is None:
        print("Cannot list mines: current sector ID is unknown.")
        return

    list_resp = ctx.conn.rpc("deploy.mines.list", {"sector_id": ctx.current_sector_id})
    if list_resp.get("status") != "ok":
        print(f"[Error] Failed to list deployed mines: {list_resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(list_resp)
        return

    mines_data = get_data(list_resp)
    mines_list = mines_data.get("entries", [])

    if not mines_list:
        print("No mines deployed in this sector.")
        return

    print("\n--- Deployed Mines ---")
    for m in mines_list:
        print(f"  Asset ID: {m.get('asset_id')}, Quantity: {m.get('count')}, Offensive Setting: {m.get('offense_mode')}")
    print("----------------------")

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
    sector_id = (ship.get("location") or {}).get("sector_id") or "?"
    owner = (ship.get("owner") or {}).get("name") if isinstance(ship.get("owner"), dict) else ship.get("owner")
    owner = owner or "unknown"
    flags = _bool_flags(ship.get("flags") or {})
    shields = _fmt_int((ship.get("defence") or {}).get("shields") or 0)
    fighters = _fmt_int((ship.get("defence") or {}).get("fighters") or 0)
    holds_val = ship.get("holds")
    cargo_items = ship.get("cargo") or {}
    
    holds_used = sum(cargo_items.values()) # Sum of all cargo items
    
    if isinstance(holds_val, dict):
        holds_total = holds_val.get("total") or 0
        holds_free  = max(0, int(holds_total) - int(holds_used))
    elif isinstance(holds_val, int):
        holds_total = holds_val # This is max_holds
        holds_free  = max(0, int(holds_total) - int(holds_used))
    else:
        holds_total = 0
        holds_free = 0
    
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

@register("print_inspect_response_pretty")
def print_inspect_response_pretty(ctx):
    resp = ctx.state.get("last_rpc")
    if resp is None:
        print("[no response captured]")
        return
    
    # Directly access data to see where it fails
    data = resp.get("data")

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

@register("simple_buy_handler")
def simple_buy_handler(ctx: Context):

    port_id = (ctx.last_sector_desc.get("port") or {}).get("id")
    if port_id is None:
        print("Cannot determine current port ID. Please ensure you are docked.")
        return

    commodity_input = input("Product Code (ORE, ORGANICS, EQUIPMENT): ").strip().upper()
    
    code_map = {
        "ORE": "ore",
        "ORGANICS": "organics",
        "EQUIPMENT": "equipment"
    }
    commodity = code_map.get(commodity_input)

    if not commodity:
        print("Invalid commodity. Please use ORE, ORGANICS, or EQUIPMENT.")
        return

    try:
        quantity = int(input(f"Quantity of {commodity_input.capitalize()}: ").strip())
        if quantity <= 0:
            print("Quantity must be positive.")
            return
    except ValueError:
        print("Invalid quantity.")
        return

    # Generate a unique idempotency key
    idempotency_key = str(uuid.uuid4())

    print(f"Attempting to buy {quantity} units of {commodity.capitalize()} (Port ID: {port_id})...")

    payload = {
        "port_id": port_id,
        "items": [
            {
                "commodity": commodity,
                "quantity": quantity
            }
        ],
        "idempotency_key": idempotency_key
    }

    try:
        resp = ctx.conn.rpc("trade.buy", payload)
        ctx.state["last_rpc"] = resp
        _run_post(ctx, "pretty_print_trade_receipt")
    except Exception as e:
        print(f"[ERROR] An unexpected error occurred during RPC call: {e}")
        ctx.state["last_rpc"] = {"status": "error", "error": {"message": str(e)}}
        _run_post(ctx, "pretty_print_trade_receipt")

@register("simple_sell_handler")
def simple_sell_handler(ctx: Context):
    port_id = (ctx.last_sector_desc.get("port") or {}).get("id")
    if port_id is None:
        print("Cannot determine current port ID. Please ensure you are docked.")
        return

    commodity_input = input("Product Code (ORE, ORGANICS, EQUIPMENT): ").strip().upper()
    
    code_map = {
        "ORE": "ore",
        "ORGANICS": "organics",
        "EQUIPMENT": "equipment"
    }
    commodity = code_map.get(commodity_input)

    if not commodity:
        print("Invalid commodity. Please use ORE, ORGANICS, or EQUIPMENT.")
        return

    try:
        quantity = int(input(f"Quantity of {commodity_input.capitalize()}: ").strip())
        if quantity <= 0:
            print("Quantity must be positive.")
            return
    except ValueError:
        print("Invalid quantity.")
        return

    idempotency_key = str(uuid.uuid4())

    print(f"Attempting to sell {quantity} units of {commodity.capitalize()} (Port ID: {port_id})...")

    payload = {
        "port_id": port_id,
        "items": [
            {
                "commodity": commodity,
                "quantity": quantity
            }
        ],
        "idempotency_key": idempotency_key
    }

    try:
        resp = ctx.conn.rpc("trade.sell", payload)
        ctx.state["last_rpc"] = resp
        _run_post(ctx, "pretty_print_sell_receipt")
    except Exception as e:
        print(f"[ERROR] An unexpected error occurred during RPC call: {e}")
        ctx.state["last_rpc"] = {"status": "error", "error": {"message": str(e)}}
        _run_post(ctx, "pretty_print_sell_receipt")

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
    """Ask for a target, call move.pathfind, and add the route to autopilot."""
    # Where are we now?
    cur_desc = ctx.last_sector_desc or {}
    cur = cur_desc.get("sector_id") or cur_desc.get("id") or cur_desc.get("sector") or ctx.state.get("sector_id")
    try:
        target = int(input("Add sector to route (target sector ID): ").strip())
    except (ValueError, TypeError):
        print("Invalid sector ID."); return

    if not cur:
        # Fallback: fetch current sector from server if we don't have it cached
        try:
            info = ctx.conn.rpc("move.describe_sector", {})
            data = (info or {}).get("data") or {}
            cur = data.get("sector_id") or data.get("id")
        except Exception:
            pass
    if not cur:
        print("Cannot determine current sector. Please re-display sector (D) first."); return

    # Ask server for a path
    req = {"from": cur, "to": target}
    resp = ctx.conn.rpc("move.pathfind", req)
    status = (resp or {}).get("status")
    if status in ("error", "refused"):
        err = (resp.get("error") or {})
        print(f"Pathfind failed {err.get('code')}: {err.get('message') or 'error'}")
        return
    data = (resp or {}).get("data") or {}

    # Try to normalize a path from a few likely shapes
    path = data.get("path") or data.get("sectors") or data.get("steps") or []
    # Some servers return an array of {from,to} edges; flatten if needed
    if path and isinstance(path[0], dict):
        # e.g., [{"from":1,"to":2},{"from":2,"to":5}] -> [1,2,5]
        seq = [path[0].get("from")]
        for e in path:
            t = e.get("to")
            if t is not None:
                seq.append(t)
        path = seq

    if not path or len(path) < 1:
        print("No route found to target sector.")
        return

    # Append path to current autopilot route, skipping the current sector if it's the first element
    route = ctx.state.setdefault("ap_route", [])
    start_idx = 0
    if path and path[0] == cur:
        start_idx = 1

    for sector_id in path[start_idx:]:
        route.append(sector_id)

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

@register("planet_info_flow")
def planet_info_flow(ctx: Context):
    """
    Get info for a planet in the current sector.
    - If no planets, print a message.
    - If planets, prompt the user to select one.
    - Call planet.info with the selected planet's ID.
    """
    planets = ctx.last_sector_desc.get("planets") or []

    if not planets:
        print("No planet in sector")
        return

    if len(planets) == 1:
        selected_planet = planets[0]
    else:
        print("Planets in this sector:")
        for i, planet in enumerate(planets):
            print(f"  {i+1}: {planet.get('name', 'Unnamed Planet')} (ID: {planet.get('id')})")

        try:
            choice = int(input("Which planet? "))
            if 1 <= choice <= len(planets):
                selected_planet = planets[choice - 1]
            else:
                print("Invalid choice.")
                return
        except ValueError:
            print("Invalid input.")
            return

    planet_id = selected_planet.get("id")
    if planet_id is None:
        print("Could not determine planet ID.")
        return

    resp = ctx.conn.rpc("planet.info", {"planet_id": planet_id})
    ctx.state["last_rpc"] = resp
    # Pretty print the response
    print(json.dumps(resp, indent=2))

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
@register("shipyard_upgrade_flow")
def shipyard_upgrade_flow(ctx: Context):
    """
    Flow for upgrading/purchasing a new ship at the shipyard.
    - Lists available ships.
    - Prompts for new ship type ID and name.
    - Calls shipyard.upgrade.
    """
    list_resp = ctx.conn.rpc("shipyard.list", {})
    ctx.state["last_rpc"] = list_resp
    if list_resp.get("status") != "ok":
        print(f"[Error] Could not retrieve shipyard list: {list_resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(list_resp)
        return

    data = list_resp.get("data")
    if not isinstance(data, dict):
        print("[Error] Invalid shipyard list data.")
        _pp(list_resp)
        return

    available_ships = data.get("available", [])
    if not available_ships:
        print("No ships available for upgrade at this shipyard.")
        return

    print("\n--- Available Ships for Upgrade ---")
    for i, ship in enumerate(available_ships):
        eligible_status = "Eligible" if ship.get("eligible") else "Ineligible"
        reasons = f" ({', '.join(ship.get('reasons'))})" if ship.get("reasons") else ""
        print(f"  {i+1}: {ship.get('name')} (Type ID: {ship.get('type_id')}) - Net Cost: {ship.get('net_cost')} cr - {eligible_status}{reasons}")

    try:
        choice = int(input("Enter number of ship to upgrade to: ").strip())
        if not (1 <= choice <= len(available_ships)):
            print("Invalid choice.")
            return
        selected_ship = available_ships[choice - 1]
    except ValueError:
        print("Invalid input. Please enter a number.")
        return

    if not selected_ship.get("eligible"):
        print(f"Cannot upgrade to {selected_ship.get('name')}: {', '.join(selected_ship.get('reasons'))}")
        return

    new_ship_type_id = selected_ship.get("type_id")
    new_ship_name = input(f"Enter new name for your {selected_ship.get('name')} (current ship name suggested): ").strip()
    if not new_ship_name:
        print("New ship name cannot be empty. Cancelled.")
        return

    payload = {
        "new_type_id": new_ship_type_id,
        "new_ship_name": new_ship_name
    }

    upgrade_resp = ctx.conn.rpc("shipyard.upgrade", payload)
    ctx.state["last_rpc"] = upgrade_resp

    if upgrade_resp.get("status") == "ok":
        print(f"Successfully upgraded to {new_ship_name}!")
        _pp(upgrade_resp)
        # Refresh player info and sector description after a successful upgrade
        my_info_resp = ctx.conn.rpc("player.my_info", {})
        if my_info_resp.get("status") == "ok":
            ctx.player_info = get_data(my_info_resp)
            ctx.last_sector_desc = normalize_sector(get_data(ctx.conn.rpc("move.describe_sector", {"sector_id": ctx.current_sector_id})))
        else:
            print("[Warning] Could not refresh player info after ship upgrade.")
    else:
        print(f"[Error] Ship upgrade failed: {upgrade_resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(upgrade_resp)

@register("buy_ship_flow")
def buy_ship_flow(ctx: Context):
    """
    Lists available ships and prompts user to buy a new one.
    This assumes a server command like 'shipyard.buy' or similar if distinct from upgrade.
    For now, it redirects to the upgrade flow for simplicity, assuming 'upgrade' handles initial purchase.
    """
    print("Please use the 'Upgrade Ship' option to purchase a new ship at the shipyard, or implement a dedicated buy command.")
    shipyard_upgrade_flow(ctx)

@register("upgrade_ship_flow")
def upgrade_ship_flow(ctx: Context):
    """ Placeholder for ship component upgrades. """
    print("Ship component upgrades are not yet implemented. Use the Shipyard 'Upgrade Ship' option for hull upgrades.")

@register("downgrade_ship_flow")
def downgrade_ship_flow(ctx: Context):
    """ Placeholder for ship component downgrades. """
    print("Ship component downgrades are not yet implemented.")

@register("sell_ship_flow")
def sell_ship_flow(ctx: Context):
    """ Placeholder for selling ships. """
    print("Selling ships is not yet implemented.")





@register("jettison_cargo_flow")
def jettison_cargo_flow(ctx: Context):
    commodities = (ctx.player_info.get("ship") or {}).get("cargo") or []
    if not commodities:
        print("Your ship has no cargo to jettison.")
        return

    print("\n--- Current Cargo ---")
    for item in commodities:
        print(f"  {item.get('commodity').capitalize()}: {item.get('quantity')}")
    print("---------------------")

    commodity_to_jettison = input("Enter commodity to jettison: ").strip().lower()
    if not commodity_to_jettison:
        print("Cancelled.")
        return

    try:
        quantity_to_jettison = int(input(f"Quantity of {commodity_to_jettison.capitalize()} to jettison: ").strip())
        if quantity_to_jettison <= 0:
            print("Quantity must be positive.")
            return
    except ValueError:
        print("Invalid quantity.")
        return

    payload = {
        "commodity": commodity_to_jettison,
        "quantity": quantity_to_jettison
    }

    resp = ctx.conn.rpc("ship.jettison", payload)
    ctx.state["last_rpc"] = resp
    if resp.get("status") == "ok":
        print(f"Successfully jettisoned {quantity_to_jettison} units of {commodity_to_jettison}.")
        # Refresh player info to update cargo display
        my_info_resp = ctx.conn.rpc("player.my_info", {})
        if my_info_resp.get("status") == "ok":
            ctx.player_info = get_data(my_info_resp)
        else:
            print("[Warning] Could not refresh player info after jettison.")
    else:
        print(f"[Error] Failed to jettison cargo: {resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(resp)


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
        r = ctx.conn.rpc("system.capabilities", {})  # was system.hello
        print(f"[{i+1}] {r.get('status')} {r.get('type')}")



        
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

            login = conn.rpc("auth.login", {"username": user, "passwd": passwd})

            if login.get("status") in ("error","refused"):
                print("Login failed.");
                return 2

            current = extract_current_sector(login)
            if not isinstance(current, int):
                info = conn.rpc("player.my_info", {})
                current = extract_current_sector(info) or 1

            # describe sector and normalize
            desc = conn.rpc("move.describe_sector", {"sector_id": int(current)})
            norm = normalize_sector(get_data(desc))

            ctx = Context(conn=conn, menus=menus, last_sector_desc=norm)
            ctx.state["cli"] = {"host": args.host, "port": args.port, "user": user, "debug": bool(args.debug)}

            # Initial update of corporation context after login
            _update_corp_context(ctx)

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


# --- BEGIN AUTO-ADDED CAPS_HELPERS ---
def _ctx_capabilities_get(ctx, path, default=None):
    """Resolve dotted path from ctx.capabilities (dict)."""
    cur = getattr(ctx, 'capabilities', None) or {}
    for part in path.split("."):
        if not isinstance(cur, dict):
            return default
        cur = cur.get(part)
    return default if cur is None else cur
# --- END AUTO-ADDED CAPS_HELPERS ---























@register("sector_density_scan_flow")
def sector_density_scan_flow(ctx: Context):
    """
    Density-only scan: calls sector.scan.density (fallback to move.scan.density).
    Supports both dict payload and flat [sector_id, density, ...] arrays.
    """
    sector_id = ctx.current_sector_id
    payload = {"sector_id": sector_id} if sector_id is not None else {}
    try:
        resp = ctx.conn.rpc("sector.scan.density", payload)
    except Exception:
        resp = ctx.conn.rpc("move.scan.density", payload)
    ctx.state["last_rpc"] = resp

    print("\n=== Density Scan ===")
    data = resp.get("data") if isinstance(resp, dict) else None

    # Case A
    if isinstance(data, dict) and "density" in data:
        sid = data.get("sector_id", sector_id)
        dens = data.get("density")
        print(f"Sector: {sid}")
        print(f"Density: {dens if dens is not None else '(unknown)'}")
    # Case B
    elif isinstance(data, list) and all(isinstance(x, int) for x in data):
        pairs = list(zip(data[0::2], data[1::2]))
        print("Sector   Density")
        print("----------------")
        for sid, dens in pairs:
            print(f"{sid:>6}   {dens:>7}")
    else:
        print("(unexpected response shape)")
