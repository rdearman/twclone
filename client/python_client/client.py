from __future__ import annotations # <--- MUST be here


#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
test_clientv3.py — Data-driven menu client for TWClone (server-connected, v2 parity)

- Reads menus from menus.json (or uses built-in fallback)
- Generic menu engine (submenu/back/rpc/pycall/flow/post/help)
- Real socket Conn (v2-compatible), login, sector fetch & normalization
- Handlers ported/aligned with v2: redisplay header, beacon flow, enter-ship,
  testing (full), adjacency-aware move, towing, help, shipyard, warp,
  autopilot route (add/clear/start), intercept, land, computer tools.
"""
### from __future__ import annotations
import argparse
import math
import uuid
import getpass
import json
import os
import re
import socket
import sys
import shutil
import textwrap
from dataclasses import replace
from typing import Any, Dict, Callable, List, Optional

from protocol import Conn, get_data, extract_current_sector, normalize_sector
from state import Context
from presenters import (
    present_move_result, present_move_hazards, present_subscription_result,
    present_settings, present_bookmarks, present_avoid_list, present_money,
)
from settings_model import (
    NormalizationError, normalize_settings, normalize_prefs,
    normalize_bookmark_list, normalize_avoid_list, normalize_notes_list,
)
from money import MoneyError, parse_credits, format_credits
import hud as hud_module
from events import render_event_text, targets_player_ship_destruction


def _hud_rpc(ctx: "Context", command: str, data: dict) -> dict:
    """
    Call `command` via `ctx.conn.rpc` and, on success, feed the response
    through the single centralised HUD extractor (hud.apply_response) so
    field extraction is never scattered across menu handlers. Refused/error
    responses never update HUD state (apply_response checks status itself).
    Returns the raw response unchanged for the caller's own use.
    """
    resp = ctx.conn.rpc(command, data)
    ctx.hud = hud_module.apply_response(ctx.hud, command, resp)
    return resp


def _authoritative_hud_record(response: dict, key: str) -> Optional[dict]:
    """Return a typed HUD record only when the server supplied one."""
    if not isinstance(response, dict) or response.get("status") != "ok":
        return None
    data = response.get("data")
    if not isinstance(data, dict) or not isinstance(data.get(key), dict):
        return None
    return data[key]


def _refresh_response_type_matches(response: Any, expected: str) -> bool:
    """Correlate a refresh record with the command that requested it.

    The V3 server publishes one typed response schema for each refresh.  An
    absent or empty value is not a second success variant: without the type,
    a success-shaped record from a different command must not authorize a
    post-landing surface refresh.
    """
    if not isinstance(response, dict):
        return False
    return response.get("type") == expected


def _landing_player_refresh_is_current(
    ctx: "Context", response: dict, *, require_identity: bool = False
) -> bool:
    """Accept a landing refresh only when it still identifies this player.

    A player.my_info response without an ID is still useful for ordinary HUD
    refreshes, but it cannot prove that a post-landing state belongs to the
    logged-in player when an earlier authoritative player ID is known.
    """
    if not _refresh_response_type_matches(response, "player.info"):
        return False
    record = _authoritative_hud_record(response, "player")
    if record is None:
        return False
    previous = ctx.player_info.get("player") if isinstance(ctx.player_info, dict) else None
    expected_id = previous.get("id") if isinstance(previous, dict) else None
    if expected_id is None:
        # The initial session hydration may not have populated player_info yet.
        # Keep the login session bound to a server-provided player ID so a
        # partial refresh cannot authorize a planetary surface.  A response
        # without an identity is not sufficient even when no prior local ID
        # exists; otherwise a success-shaped HUD record could be accepted for
        # the wrong session.
        expected_id = ctx.state.get("player_id")
    actual_id = record.get("id")
    if type(actual_id) is not int or actual_id <= 0:
        return not require_identity and expected_id is None
    if expected_id is None:
        return True
    return type(expected_id) is int and expected_id > 0 and actual_id == expected_id


def _refresh_player_after_landing(
    ctx: "Context", *, require_identity: bool = False
) -> dict:
    """Refresh player HUD only when the response identifies this player."""
    response = ctx.conn.rpc("player.my_info", {})
    if not _landing_player_refresh_is_current(
        ctx, response, require_identity=require_identity
    ):
        print("Warning: player refresh did not identify the logged-in player; player HUD remains unchanged.")
        return response
    ctx.hud = hud_module.apply_response(ctx.hud, "player.my_info", response)
    return response


def _refresh_ship_after_move(
    ctx: "Context", *, expected_ship_id: Optional[int] = None
) -> dict:
    """Refresh the active ship using the confirmed active-ship identity.

    A terminal destruction can replace the destroyed ship with an escape pod.
    In that case the caller may pass the new ID from the authoritative player
    refresh. Ordinary movement leaves this unset and remains bound to the
    previously confirmed HUD ship.
    """
    response = ctx.conn.rpc("ship.status", {})
    expected_id = ctx.hud.ship_id if expected_ship_id is None else expected_ship_id
    record = (
        _authoritative_hud_record(response, "ship")
        if _refresh_response_type_matches(response, "ship.status")
        else None
    )
    actual_id = record.get("id", record.get("ship_id")) if record else None
    # A hazard/movement response can be well-formed while its follow-up ship
    # read is incomplete.  Do not treat hull, shields, or cargo from such a
    # record as current merely because no earlier local ship ID exists.
    if (type(actual_id) is not int or actual_id <= 0
            or (expected_id is not None and actual_id != expected_id)):
        print("Warning: ship status refresh did not identify the confirmed active ship; ship HUD remains unchanged.")
        return response
    ctx.hud = hud_module.apply_response(ctx.hud, "ship.status", response)
    return response


def _refresh_sector_after_shipyard(ctx: "Context") -> bool:
    """Apply only the typed, current-sector snapshot after a hull exchange."""
    response = ctx.conn.rpc("move.describe_sector", {"sector_id": ctx.current_sector_id})
    if response.get("status") != "ok" or not _refresh_response_type_matches(response, "sector.info"):
        return False
    payload = response.get("data")
    if not isinstance(payload, dict):
        return False
    refreshed = normalize_sector(get_data(response))
    refreshed_id = _authoritative_positive_integer_id(refreshed.get("id"))
    current_id = _authoritative_positive_integer_id((ctx.last_sector_desc or {}).get("id"))
    if refreshed_id is None or (current_id is not None and refreshed_id != current_id):
        return False
    ctx.last_sector_desc = refreshed
    return True


def _safe_landing_refresh(ctx: "Context", command: str, data: dict) -> Optional[dict]:
    """Refresh landing HUD state without turning a reconnect into a traceback."""
    try:
        if command == "ship.status":
            return _refresh_ship_after_move(ctx)
        if command == "player.my_info":
            # Landing reconciliation is an authorization boundary: a player
            # record without the session's authoritative ID may not update
            # turns/credits, even though it is otherwise a well-formed HUD
            # payload.  Keep the response available to the caller so it can
            # report an incomplete refresh, but do not merge it into HUD state.
            return _refresh_player_after_landing(ctx, require_identity=True)
        return _hud_rpc(ctx, command, data)
    except (ConnectionError, OSError) as exc:
        print(f"Landing state refresh could not be confirmed after connection loss: {exc}")
        print("Reconnect before relying on turns, ship state, or planetary access.")
        return None


def _safe_landing_sector_refresh(ctx: "Context") -> bool:
    """Reconcile the current sector without turning a lost reply into a traceback."""
    try:
        refreshed = refresh_sector_and_display(ctx)
    except (ConnectionError, OSError) as exc:
        print(f"Landing state refresh could not be confirmed after connection loss: {exc}")
        print("Reconnect before relying on location or planetary access.")
        return False
    if refreshed is False:
        print("Landing state refresh could not be confirmed; reconnect before relying on location or planetary access.")
        return False
    return True


def _safe_launch_refresh(ctx: "Context") -> None:
    """Reconcile launch aftermath without turning a lost refresh into a crash."""
    _safe_landing_refresh(ctx, "player.my_info", {})
    _safe_landing_refresh(ctx, "ship.status", {})
    _safe_landing_sector_refresh(ctx)


def _refresh_after_ship_destruction(ctx: "Context") -> bool:
    """Reconcile destruction aftermath from fresh server-owned records."""
    try:
        player_response = ctx.conn.rpc("player.my_info", {})
        if not _landing_player_refresh_is_current(ctx, player_response, require_identity=True):
            print("Ship destruction reported, but the player aftermath could not be confirmed.")
            return False
        player_record = _authoritative_hud_record(player_response, "player")
        ctx.player_info = get_data(player_response)
        ctx.state["player_id"] = player_record["id"]
        ctx.hud = hud_module.apply_response(ctx.hud, "player.my_info", player_response)
        replacement_ship_id = player_record.get("ship_id")
        if type(replacement_ship_id) is not int or replacement_ship_id <= 0:
            print("Ship destruction reported, but the server did not identify the active ship.")
            return False
        ship_response = _refresh_ship_after_move(ctx, expected_ship_id=replacement_ship_id)
        if not _authoritative_hud_record(ship_response, "ship"):
            print("Ship destruction reported, but the replacement ship could not be confirmed.")
            return False
        return _safe_landing_sector_refresh(ctx)
    except (ConnectionError, OSError):
        print("Ship destruction reported, but aftermath refresh was interrupted; reconnect to verify state.")
        return False


def _reconcile_destruction_events(ctx: Context) -> bool:
    """Refresh once when the drained batch contains this player's death."""
    player_id = _known_player_id(ctx)
    if player_id is None:
        return False
    if any(targets_player_ship_destruction(event, player_id) for event in ctx.last_drained_events):
        return _refresh_after_ship_destruction(ctx)
    return False


# ---------------------------
# CLI defaults (match v2)
# ---------------------------
HOST = "127.0.0.1"
PORT = 1234

# Process exit codes. Named constants so the meaning of a given exit status
# is documented at the call site rather than a bare integer.
EXIT_OK = 0
EXIT_CONNECTION_REFUSED = 1  # could not establish the initial connection
EXIT_LOGIN_FAILED = 2        # connected, but auth.login was refused/errored
EXIT_CONNECTION_LOST = 3     # mid-session disconnect (post-login)

def parse_args():
    p = argparse.ArgumentParser(description="Trade Wars 2002 interactive client (v3).")
    p.add_argument("--host", default=HOST, help=f"Server host (default: {HOST})")
    p.add_argument("--port", type=int, default=PORT, help=f"Server port (default: {PORT})")
    p.add_argument("--user", help="Player name for login.")
    p.add_argument("--passwd", help="Password for login.")
    p.add_argument("--register", action="store_true",
                   help="Create a new account, then enter the game.")
    p.add_argument("--debug", action="store_true",
                   help="Enable wire-protocol debug output and expose diagnostic/testing menus")
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
    data = refresh_autopilot_status(ctx)
    if data is None:
        response = ctx.state.get("autopilot_status_response") or {}
        error = response.get("error") if isinstance(response, dict) else {}
        error = error if isinstance(error, dict) else {}
        print(f"[Error] Failed to get autopilot status: {error.get('message', 'Unknown error')}")
        return
    print_autopilot_status(data)


def refresh_autopilot_status(ctx, announce=False):
    """Refresh the server-owned route snapshot after login or reconnect."""
    try:
        response = ctx.conn.rpc("move.autopilot.status", {})
    except ConnectionError:
        raise
    except Exception:
        return None
    ctx.state["autopilot_status_response"] = response
    if not isinstance(response, dict) or response.get("status") != "ok":
        return None
    data = response.get("data")
    if not isinstance(data, dict):
        return None
    ctx.state["autopilot_status"] = data
    if announce and data.get("state") not in (None, "idle", "complete"):
        next_sector = data.get("next_sector_id", data.get("next"))
        print("Saved autopilot route restored: %s; next sector %s. No movement will resume until you start it." % (
            data.get("state", "unknown"),
            next_sector if next_sector is not None else "unavailable"))
    return data


def print_autopilot_status(data):
    """Display only authoritative fields returned by move.autopilot.status."""
    next_sector = data.get("next_sector_id", data.get("next"))
    details = [f"Autopilot: {data.get('state', 'unknown')} ({data.get('mode', 'unknown')})"]
    if data.get("current_sector_id") is not None:
        details.append(f"current sector {data['current_sector_id']}")
    if data.get("target_sector_id") is not None:
        details.append(f"destination {data['target_sector_id']}")
    details.append(f"next {next_sector if next_sector is not None else '-'}")
    path = data.get("path")
    if isinstance(path, list) and path:
        details.append("path " + " → ".join(map(str, path)))
    print("; ".join(details))
    if data.get("last_error"):
        print(f"Last route error: {data['last_error']}")

@register("cli_autopilot_control")
def cli_autopilot_control(ctx):
    """Send a control action and display the server-confirmed result."""
    print("Controls: [Y] stop at next  |  [N] continue  |  [E] express  |  [S] stop now  |  [Q] back")
    while True:
        k = input("> ").strip().lower()
        action = None
        if k == "y":
            action = "stop_at_next"
        elif k == "n":
            action = "continue"
        elif k == "e":
            action = "express"
        elif k == "s":
            cli_autopilot_stop(ctx)
            continue
        elif k == "q" or k == "":
            return
        else:
            print("Y/N/E/S or Q")
            continue

        response = ctx.conn.rpc("move.autopilot.control", {"action": action})
        if (response or {}).get("status") in ("error", "refused"):
            error = (response.get("error") or {})
            print(f"[Error] Autopilot {action} refused: {error.get('message', 'Unknown error')}")
            continue

        data = (response or {}).get("data") or {}
        ctx.state["autopilot_status"] = data
        next_sector = data.get("next_sector_id")
        detail = f"; next sector {next_sector}" if next_sector is not None else ""
        print(f"Autopilot {action} confirmed: {data.get('state', 'updated')}{detail}.")


@register("cli_autopilot_stop")
def cli_autopilot_stop(ctx):
    """Stop the persisted server route immediately, without moving the ship."""
    response = ctx.conn.rpc("move.autopilot.stop", {})
    if not isinstance(response, dict) or response.get("status") != "ok":
        error = response.get("error") if isinstance(response, dict) else {}
        error = error if isinstance(error, dict) else {}
        print(f"[Error] Autopilot stop refused: {error.get('message', 'Unknown error')}")
        ctx.state["autopilot_status_response"] = response
        return False
    data = response.get("data")
    if not isinstance(data, dict):
        data = {}
    ctx.state["autopilot_status"] = data
    ctx.state["autopilot_status_response"] = response
    current = data.get("current_sector_id")
    suffix = f" at sector {current}" if current is not None else ""
    print(f"Autopilot stopped{suffix}; no movement was made by this command.")
    return True


@register("saved_autopilot_route_flow")
def saved_autopilot_route_flow(ctx: Context):
    """Create a server-persisted route, then offer to execute confirmed hops."""
    current_sector = ctx.current_sector_id
    if current_sector is None:
        print("Cannot save a route until the current sector is confirmed.")
        return
    try:
        destination = int(input("Server-saved route destination: ").strip())
    except ValueError:
        print("Invalid sector ID.")
        return
    if destination <= 0:
        print("Sector ID must be greater than zero.")
        return
    if destination == current_sector:
        print("You are already at that destination.")
        return
    if not _trade_confirmation("Create and save this route?", default_no=True):
        print("Route creation cancelled; no route was changed.")
        return
    response = ctx.conn.rpc("move.autopilot.start", {"to_sector_id": destination})
    ctx.state["autopilot_status_response"] = response
    if not isinstance(response, dict) or response.get("status") != "ok":
        error = (response.get("error") or {}) if isinstance(response, dict) else {}
        print(f"[Error] Could not save route: {error.get('message', 'The server refused the route.')}")
        return
    data = get_data(response)
    path = data.get("path") if isinstance(data, dict) else None
    if (not isinstance(path, list) or not path
            or any(not isinstance(item, int) or item <= 0 for item in path)
            or path[0] != current_sector or path[-1] != destination):
        print("The server saved the request but returned no usable route; stopping that saved route now.")
        cli_autopilot_stop(ctx)
        return
    route = list(path[1:]) if path[0] == current_sector else list(path)
    if not route:
        print("The server confirms you are already at the destination.")
        return
    ctx.state["ap_route"] = route
    ctx.state["ap_route_plotted"] = True
    print("Server-saved route: " + " → ".join(map(str, path)))
    choice = input("Execute the server-confirmed route? [Y] standard · [E] express · [N] cancel: ").strip().lower()
    while choice not in ("y", "e", "n", "q", ""):
        print("Choose Y, E or N.")
        choice = input("Execute the server-confirmed route? [Y] standard · [E] express · [N] cancel: ").strip().lower()
    if choice not in ("y", "e"):
        if cli_autopilot_stop(ctx):
            ctx.state["ap_route"] = []
            ctx.state["ap_route_plotted"] = False
        return
    if choice == "e" and not _autopilot_set_mode(ctx, "express"):
        return
    start_autopilot(ctx)
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
    r = ctx.conn.rpc("bulk.execute", {"commands": items})
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
    items = data.get("topics") or []
    if not items:
        print("(no subscriptions)")
    for it in items:
        if isinstance(it, str):
            t, locked = it, False
        elif isinstance(it, dict):
            t, locked = it.get("topic"), it.get("locked")
        else:
            continue
        print(f"- {t}{' [locked]' if locked else ''}")
    ctx.state["last_rpc"] = r

@register("cli_subscriptions_remove")
def cli_subscriptions_remove(ctx):
    topic = input("Topic to remove: ").strip()
    r = ctx.conn.rpc("subscribe.remove", {"topic": topic})
    print(present_subscription_result(r, "remove", topic))
    ctx.state["last_rpc"] = r

@register("subscriptions_list_flow")
def subscriptions_list_flow(ctx: Context):
    r = ctx.conn.rpc("subscribe.list", {})
    ctx.state["last_rpc"] = r
    # The protocol document describes the compact landing result as
    # ``status: landed`` with a top-level planet_id, while the gateway
    # envelope normally uses ``status: ok`` and nests the same ID in data.
    # Accept both documented success shapes, but keep the authoritative ID
    # and post-landing refresh requirements identical.
    landing_succeeded = (
        isinstance(r, dict)
        and r.get("status") in ("ok", "landed")
    )
    if landing_succeeded:
        data = r.get("data") or {}
        items = data.get("topics") or []
        if items:
            print("Current subscriptions:")
            for item in items:
                if isinstance(item, str):
                    topic, locked = item, False
                elif isinstance(item, dict):
                    topic, locked = item.get("topic"), item.get("locked")
                else:
                    continue
                if topic:
                    locked_str = " (locked)" if locked else ""
                    print(f"  - {topic}{locked_str}")
        else:
            print("No active subscriptions.")
    else:
        print(present_subscription_result(r, "list"))

@register("subscriptions_add_flow")
def subscriptions_add_flow(ctx: Context):
    # Get available topics from the server
    catalog_resp = ctx.conn.rpc("subscribe.catalog", {})
    if catalog_resp.get("status") != "ok":
        print("Could not retrieve subscription catalog from server.")
        print(present_subscription_result(catalog_resp, "list"))
        return

    catalog_data = catalog_resp.get("data") or {}
    available_topics = catalog_data.get("topics") or []

    if not available_topics:
        print("No available subscription topics found on the server.")
        return

    print("Available subscription topics:")
    for i, topic in enumerate(available_topics):
        print(f"  {i+1}: {topic.get('pattern')} - {topic.get('desc', '')}")

    try:
        choice = int(input("Choose a topic to subscribe to: "))
        if 1 <= choice <= len(available_topics):
            pattern = available_topics[choice - 1].get("pattern")
        else:
            print("Invalid choice.")
            return
    except ValueError:
        print("Invalid input.")
        return

    if not pattern:
        print("Invalid choice.")
        return

    selected_topic = pattern
    if pattern.endswith(".*"):
        prefix = pattern[:-2]
        concrete = input(f"Enter a specific topic (e.g. {prefix}.42): ").strip()
        if not concrete:
            print("Cancelled.")
            return
        selected_topic = concrete

    add_resp = ctx.conn.rpc("subscribe.add", {"topic": selected_topic})
    print(present_subscription_result(add_resp, "add", selected_topic))
    ctx.state["last_rpc"] = add_resp

@register("subscriptions_remove_flow")
def subscriptions_remove_flow(ctx: Context):
    topic = input("Topic to remove: ").strip()
    if not topic:
        print("Cancelled.")
        return
    r = ctx.conn.rpc("subscribe.remove", {"topic": topic})
    print(present_subscription_result(r, "remove", topic))
    ctx.state["last_rpc"] = r
# --- END AUTO-ADDED SUBSCRIPTIONS ---


# --- BEGIN AUTO-ADDED CHAT_MAIL ---
@register("cli_chat_history")
def cli_chat_history(ctx):
    r = ctx.conn.rpc("chat.history", {"limit": 20})
    ctx.state["last_rpc"] = r
    if r.get("status") == "ok":
        data = r.get("data") or {}
        msgs = data.get("messages") if isinstance(data, dict) else []
        if not isinstance(msgs, list):
            msgs = []
        msgs = [message for message in msgs if isinstance(message, dict)]
        if not msgs:
            print("No chat history yet.")
        for m in msgs:
            sender = m.get("sender_name") or f"ID:{m.get('sender_id')}"
            to = m.get("recipient_name")
            dest = f" -> {to}" if to else ""
            print(f"[{m.get('sent_at')}] {sender}{dest}: {m.get('message')}")
    else:
        err = (r.get("error") or {}).get("message") or r.get("status")
        print(f"Failed to fetch chat history: {err}")

@register("cli_chat_send")
def cli_chat_send(ctx):
    msg = input("Message: ").strip()
    if not msg:
        print("Empty message.")
        return
    scope = (input("Scope (global|player) [global]: ").strip() or "global").lower()
    if scope == "player":
        to_player = input("Player name: ").strip()
        if not to_player:
            print("Cancelled.")
            return
        r = ctx.conn.rpc("chat.send", {"to_player": to_player, "message": msg})
    elif scope == "global":
        r = ctx.conn.rpc("chat.broadcast", {"message": msg})
    else:
        print("Unknown scope; choose global or player.")
        return
    ctx.state["last_rpc"] = r
    if r.get("status") == "ok":
        print("Message sent.")
        history = ctx.conn.rpc("chat.history", {"limit": 20})
        if history.get("status") == "ok" and isinstance(history.get("data"), dict):
            messages = history["data"].get("messages", [])
            ctx.state["chat_messages"] = [m for m in messages if isinstance(m, dict)]
        else:
            print("Warning: message sent, but chat history could not be refreshed.")
    else:
        err = (r.get("error") or {}).get("message") or r.get("status")
        print(f"Failed to send message: {err}")

@register("cli_mail_inbox")
def cli_mail_inbox(ctx):
    mail_inbox_flow(ctx)

@register("cli_mail_read")
def cli_mail_read(ctx):
    mail_read_flow(ctx)

@register("cli_mail_send")
def cli_mail_send(ctx):
    mail_send_flow(ctx)

@register("cli_mail_delete")
def cli_mail_delete(ctx):
    mail_delete_flow(ctx)

@register("mail_inbox_flow")
def mail_inbox_flow(ctx: Context):
    # mail.inbox has an empty authoritative request schema.  The server owns
    # the page size/default cursor; do not send a client-side limit field that
    # schema validation will reject.
    r = _hud_rpc(ctx, "mail.inbox", {})
    ctx.state["last_rpc"] = r
    if r.get("status") == "ok":
        data = r.get("data") or {}
        items = data.get("items") if isinstance(data, dict) else []
        if not isinstance(items, list):
            items = []
        # Mail IDs are authoritative server-owned JSON integers.  In
        # particular, bool is an int subclass in Python; accepting it here
        # would make a malformed row actionable as message ID 1.
        items = [
            item for item in items
            if isinstance(item, dict) and _authoritative_mail_id(item.get("id")) is not None
        ]
        ctx.state["mail_items"] = items
        if items:
            print("\n--- Inbox ---")
            for index, item in enumerate(items, 1):
                unread = "[NEW] " if not item.get("read_at") else "      "
                mail_id = _authoritative_mail_id(item.get("id"))
                sender = item.get("sender_name") or f"ID: {item.get('sender_id')}"
                subject = item.get("subject") or "(no subject)"
                sent_at = item.get("sent_at") or "unknown time"
                print(f"  {index:>2}. {unread}From: {sender} | {subject} | {sent_at} (id {mail_id})")
        else:
            ctx.state["mail_items"] = []
            print("Inbox is empty.")
    else:
        print(f"[Error] Could not load inbox: {(r.get('error') or {}).get('message') or 'server refusal'}")

@register("mail_read_flow")
def mail_read_flow(ctx: Context):
    try:
        index = int(input("Inbox item number to read: ").strip())
    except ValueError:
        print("Invalid inbox item number."); return

    items = ctx.state.get("mail_items") or []
    if not isinstance(items, list) or not 1 <= index <= len(items):
        print("That inbox item is not available. Open Inbox first.")
        return
    item = items[index - 1]
    mid = item.get("id") if isinstance(item, dict) else None
    mid = _authoritative_mail_id(mid)
    if mid is None:
        print("That inbox item has no valid message ID.")
        return

    # The release schema and handler use mail_id. Do not send the legacy
    # `id` alias: mail.read rejects an unrecognised field.
    r = ctx.conn.rpc("mail.read", {"mail_id": mid})
    ctx.state["last_rpc"] = r
    if r.get("status") == "ok":
        data = r.get("data") or {}
        if isinstance(data, dict) and data:
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
        # mail.read may mark the item read server-side. Reconcile the inbox
        # instead of leaving the cached unread marker stale.
        mail_inbox_flow(ctx)
    else:
        print(f"[Error] Could not read mail: {(r.get('error') or {}).get('message') or 'server refusal'}")

@register("mail_send_flow")
def mail_send_flow(ctx: Context):
    to = input("To (player name or ID): ").strip()
    if not to:
        print("Recipient cannot be empty. Cancelled."); return
    subj = input("Subject: ").strip()
    body = input("Body: ").strip()
    if not body:
        print("Message body cannot be empty. Cancelled.")
        return
    r = ctx.conn.rpc("mail.send", {"to": to, "subject": subj, "body": body,
                                    "idempotency_key": str(uuid.uuid4())})
    ctx.state["last_rpc"] = r
    if r.get("status") == "ok":
        print(f"Mail sent to {to}.")
    else:
        print(f"Failed to send mail to {to}.")
        print(f"[Error] {(r.get('error') or {}).get('message') or 'server refusal'}")

@register("mail_delete_flow")
def mail_delete_flow(ctx: Context):
    try:
        mid = _authoritative_mail_id(int(input("Mail ID to delete: ").strip()))
    except ValueError:
        print("Invalid Mail ID."); return
    if mid is None:
        print("Invalid Mail ID."); return

    r = ctx.conn.rpc("mail.delete", {"ids": [mid]})
    ctx.state["last_rpc"] = r
    if r.get("status") == "ok":
        print(f"Mail ID {mid} deleted.")
        # Deletion changes the authoritative inbox, so the next display must
        # come from a fresh server response rather than the old cache.
        mail_inbox_flow(ctx)
    else:
        print(f"Failed to delete Mail ID {mid}.")
        print(f"[Error] {(r.get('error') or {}).get('message') or 'server refusal'}")
# --- END AUTO-ADDED CHAT_MAIL ---


def _authoritative_mail_id(value: Any) -> Optional[int]:
    """Return a usable mail ID without coercing schema-mismatched values."""
    return value if type(value) is int and value > 0 else None


@register("comms_events_view")
def comms_events_view(ctx: Context):
    """
    Event inbox/log: drain anything queued since the last render, then show
    a bounded recent history. Reading the log clears the unread *counter*
    only — the underlying recent history is preserved (see events.EventLog).
    """
    ctx.drain_events()
    entries = ctx.event_log.recent(limit=25)
    debug = bool(ctx.state.get("debug"))
    print("\n--- Events ---")
    if not entries:
        print("(no events yet)")
    else:
        for ev in entries:
            print("  " + render_event_text(ev, debug=debug))
    ctx.event_log.mark_read()


@register("comms_notices_view")
def comms_notices_view(ctx: Context):
    r = _hud_rpc(ctx, "notice.list", {})
    ctx.state["last_rpc"] = r
    if r.get("status") != "ok":
        _pp(r)
        return
    data = r.get("data") or {}
    items = data.get("items") if isinstance(data, dict) else []
    if not isinstance(items, list):
        items = []
    print("\n--- Notices ---")
    if not items:
        print("(no notices)")
    for it in items:
        if not isinstance(it, dict):
            print("  [info] Unreadable notice (skipped)")
            continue
        seen = "" if it.get("seen_at") is not None else " [new]"
        print(f"  [{it.get('severity','info')}] {it.get('title','')}{seen}\n    {it.get('body','')}")


@register("comms_notice_ack_flow")
def comms_notice_ack_flow(ctx: Context):
    """Acknowledge one unread notice using its server-issued ID."""
    response = _hud_rpc(ctx, "notice.list", {})
    ctx.state["last_rpc"] = response
    if response.get("status") != "ok":
        _pp(response)
        return
    data = response.get("data")
    items = data.get("items") if isinstance(data, dict) else None
    if not isinstance(items, list):
        print("The server returned an invalid notice list.")
        return
    unread = [row for row in items if isinstance(row, dict)
              and isinstance(row.get("id"), int) and not isinstance(row.get("id"), bool)
              and row.get("id") > 0 and row.get("seen_at") is None]
    print("\n--- Unread Notices ---")
    for row in unread:
        print(f"  ID {row['id']}: {row.get('title', '')}")
    if not unread:
        print("There are no unread notices to acknowledge.")
        return
    try:
        notice_id = int(input("Notice ID to acknowledge (Enter to cancel): ").strip())
    except (ValueError, EOFError):
        return
    if notice_id not in {row["id"] for row in unread}:
        print("That ID is not an unread notice in the current server listing.")
        return
    if input(f"Acknowledge notice {notice_id}? [y/N]: ").strip().lower() != "y":
        print("Acknowledgement cancelled.")
        return
    result = _hud_rpc(ctx, "notice.ack", {"notice_id": notice_id})
    ctx.state["last_rpc"] = result
    if result.get("status") != "ok":
        print(f"Notice acknowledgement refused: {(result.get('error') or {}).get('message') or 'server refusal'}")
        return
    confirmed = result.get("data") if isinstance(result.get("data"), dict) else {}
    if confirmed.get("id") != notice_id:
        print("The server response did not confirm the selected notice ID.")
        return
    print(f"Notice {notice_id} acknowledged (confirmed by server).")


@register("comms_bounty_board")
def comms_bounty_board(ctx: Context):
    """Display the public active bounty list without local interpretation."""
    response = _hud_rpc(ctx, "bounty.list", {})
    ctx.state["last_rpc"] = response
    if response.get("status") != "ok":
        _pp(response)
        return
    data = response.get("data")
    bounties = data.get("bounties") if isinstance(data, dict) else None
    if not isinstance(bounties, list):
        print("The server returned an invalid bounty list.")
        return
    print("\n--- Active Bounties ---")
    readable = 0
    for row in bounties:
        if not isinstance(row, dict):
            continue
        bounty_id = row.get("bounty_id")
        target_id = row.get("target_id")
        reward = row.get("reward")
        if not all(isinstance(value, int) and not isinstance(value, bool)
                   for value in (bounty_id, target_id, reward)):
            continue
        print(f"  #{bounty_id}: {row.get('target_name') or f'Player {target_id}'} — {reward:,} CR")
        readable += 1
    if not bounties:
        print("There are no active bounties.")
    elif not readable:
        print("No readable bounty entries were returned.")


@register("police_status_flow")
def police_status_flow(ctx: Context):
    """Show the current player's authoritative local law-enforcement state."""
    response = _hud_rpc(ctx, "police.status", {})
    ctx.state["last_rpc"] = response
    if response.get("status") != "ok":
        _pp(response)
        return
    data = response.get("data") if isinstance(response.get("data"), dict) else {}
    print("\n--- Police Status ---")
    print(f"Jurisdiction cluster: {data.get('cluster_id') or 'none'}")
    print(f"Suspicion: {data.get('suspicion', 'unreported')}")
    print(f"Wanted level: {data.get('wanted_level', 'unreported')}")
    print(f"Banned: {'yes' if data.get('banned') is True else 'no' if data.get('banned') is False else 'unreported'}")


@register("police_bribe_flow")
def police_bribe_flow(ctx: Context):
    """Submit a player-chosen bribe; all consequences remain server-owned."""
    try:
        amount = int(input("Bribe amount in credits (positive integer): ").strip())
    except (ValueError, EOFError):
        print("Invalid bribe amount; no command sent.")
        return
    if amount <= 0:
        print("Bribe amount must be positive; no command sent.")
        return
    if input(f"Offer {amount} CR to local police? [y/N]: ").strip().lower() != "y":
        print("Bribe cancelled.")
        return
    response = _hud_rpc(ctx, "police.bribe", {"amount": amount})
    ctx.state["last_rpc"] = response
    if response.get("status") != "ok":
        _pp(response)
        return
    data = response.get("data") if isinstance(response.get("data"), dict) else {}
    print(data.get("message") or "Bribe outcome confirmed by the server.")
    if "suspicion_after" in data:
        print(f"Suspicion: {data['suspicion_after']}; wanted level: {data.get('wanted_level_after', 'unreported')}")


@register("police_surrender_flow")
def police_surrender_flow(ctx: Context):
    """Ask before surrendering and display only the server-confirmed outcome."""
    if input("Surrender to the local authorities? [y/N]: ").strip().lower() != "y":
        print("Surrender cancelled.")
        return
    response = _hud_rpc(ctx, "police.surrender", {})
    ctx.state["last_rpc"] = response
    if response.get("status") != "ok":
        _pp(response)
        return
    data = response.get("data") if isinstance(response.get("data"), dict) else {}
    print(data.get("message") or "Surrender outcome confirmed by the server.")
    if "suspicion_after" in data:
        print(f"Suspicion: {data['suspicion_after']}; wanted level: {data.get('wanted_level_after', 'unreported')}")


# --- BEGIN AUTO-ADDED HUD_HOOK ---
def _hud_after_action(ctx):
    try:
        _print_rate_limit_hud(ctx)
    except Exception:
        pass
# --- END AUTO-ADDED HUD_HOOK ---


# --- BEGIN AUTO-ADDED DOCK_CONTEXT_REFRESH ---
def _authoritative_is_stardock(data: dict) -> bool:
    """Read Stardock access only from the server-owned port type."""
    if not isinstance(data, dict):
        return False
    port = data.get("port") if isinstance(data.get("port"), dict) else data
    # The canonical port contract uses numeric type 9 for Stardock.  Names,
    # class labels, and sector numbers are presentation/location context and
    # must never become a second service-availability authority.
    return type(port.get("type")) is int and port.get("type") == 9

def ctx_refresh_port_context(ctx):
    """Fetch and normalize the current port before entering the dock menu.

    ``port.info`` is the only read needed for the trading screen.  In
    particular, do not probe a collection of guessed command names here.
    """
    for key in ('is_stardock', 'is_shipyard_port', 'has_exchange_access',
                'has_insurance_access', 'has_tavern_access', 'has_hardware_access'):
        ctx.state[key] = False
    # A previous dock's inventory must never remain usable after a refusal.
    ctx.state.pop("port_inventory", None)
    ctx.state.pop("port_info_error", None)

    conn = getattr(ctx, 'conn', None)
    if conn is None:
        return

    sector_id = _authoritative_positive_integer_id(ctx.current_sector_id)
    if sector_id is None:
        print("Port details unavailable: the current sector has no authoritative ID; refresh before docking.")
        return
    port = (ctx.last_sector_desc or {}).get("port") or {}
    data = {"sector_id": sector_id} if sector_id is not None else {}
    port_id = _authoritative_positive_integer_id(port.get("id"))
    if port_id is not None:
        data["port_id"] = port_id
    r = _refresh_port_info(ctx, data)
    if r.get("status") == "ok":
        port_data = (r.get("data") or {}).get("port") or {}
        # _refresh_port_info only commits an inventory after matching the
        # requested authoritative port ID.  Do not derive service access from
        # a success-shaped response that was rejected as another port.
        if "port_inventory" in ctx.state:
            ctx.state["is_stardock"] = _authoritative_is_stardock({"port": port_data})
        else:
            error = r.get("error") if isinstance(r, dict) else {}
            message = error.get("message") if isinstance(error, dict) else None
            detail = message or "the server returned a different or malformed port."
            print("Port details unavailable · the server returned a different or malformed port: %s" % detail)
    else:
        error = r.get("error") if isinstance(r, dict) else {}
        message = error.get("message") if isinstance(error, dict) else None
        print(f"Port details unavailable · {message or 'the server did not disclose this port here.'}")


def normalize_port_info(resp: dict) -> dict:
    """Return a stable, player-client port/inventory shape."""
    if not isinstance(resp, dict) or resp.get("status") != "ok":
        return {}
    raw = (resp.get("data") or {}).get("port")
    if not isinstance(raw, dict):
        return {}
    # A port inventory is actionable only when it is tied to a server-owned
    # positive integer identity.  Do not cache a success-shaped response with
    # a missing, boolean, string, or non-positive ID; later trade requests
    # must never fall back to an inferred/current object.
    port_id = _authoritative_positive_integer_id(raw.get("id"))
    if port_id is None:
        return {}
    rows = []
    commodities = raw.get("commodities")
    # The published port contract uses an array.  A malformed success payload
    # must invalidate the inventory rather than raising while iterating it and
    # leaving a prior port snapshot actionable.
    if not isinstance(commodities, list):
        return {}
    for item in commodities:
        code = item.get("code") if isinstance(item, dict) else None
        # Commodity identity is an authoritative server string.  Do not
        # stringify booleans, numbers, or other malformed JSON values into a
        # commandable commodity code.
        if not isinstance(code, str) or not code.strip() or code != code.strip():
            continue
        # Fixed direction is authoritative metadata.  An omitted flag is an
        # older/partial disclosure and remains unknown, but an explicitly
        # malformed flag must never be treated as permission to send a trade.
        # Drop only that row so valid commodities in the same port remain
        # usable after the server publishes a bad record.
        if (("sells" in item and not isinstance(item.get("sells"), bool))
                or ("buys" in item and not isinstance(item.get("buys"), bool))):
            # An omitted mode is an older/partial disclosure and remains
            # unknown to the client.  An explicit JSON null is different: it
            # is malformed authoritative metadata, and must match Godot's
            # fail-closed behavior rather than becoming an executable action.
            continue
        # JSON booleans are Python ints, but they are not valid quantity
        # measurements.  Keep malformed authoritative stock out of the trade
        # UI rather than turning ``true`` into one available unit or capacity.
        qty = item.get("quantity")
        # The authoritative trade.port_info schema calls this field
        # ``capacity``.  Keep ``max_quantity`` as a read-only compatibility
        # fallback for already-cached legacy snapshots; never infer capacity
        # from stock or use it as a settlement rule.
        maximum = item.get("capacity", item.get("max_quantity"))
        sells = item.get("sells")
        buys = item.get("buys")
        price = item.get("price", item.get("buy_price", item.get("sell_price")))
        # Port-info prices are server-owned indicative integer credits.  Keep
        # malformed values out of the display model rather than allowing a
        # boolean/object/string to look actionable or reach money formatting.
        if isinstance(price, bool) or not isinstance(price, int) or price < 0:
            price = None
        rows.append({
            # Preserve the authoritative server code exactly.  Commodity
            # identity is not a presentation value: Godot forwards this code
            # unchanged and the quote/receipt contract owns any canonical
            # casing.  Rewriting it here would make the two clients send
            # different protocol values for the same port record.
            "commodity": code,
            "available": qty if type(qty) is int and qty >= 0 else None,
            "max_quantity": maximum if type(maximum) is int and maximum >= 0 else None,
            "base_price": price,
            # Fixed direction is server policy; never derive it from stock.
            "sells": sells if isinstance(sells, bool) else None,
            "buys": buys if isinstance(buys, bool) else None,
        })
    return {"id": port_id, "name": raw.get("name") or "Port",
            "commodities": rows, "raw": raw}


def _refresh_port_info(ctx, data=None):
    resp = ctx.conn.rpc("port.info", data or {})
    # The released handler answers the `port.info` request with its
    # `trade.port_info` schema type.  An explicit type from another read is
    # not interchangeable, even when it happens to contain a matching port
    # ID; otherwise a delayed/wrong response can repopulate the dock cache.
    normalized = (normalize_port_info(resp)
                  if _response_type_matches(resp, "port.info") else {})
    # A port ID is globally useful, but a response may also identify the
    # sector in which the port was read.  When that optional identity is
    # present, correlate it with the requested sector before making the
    # inventory actionable.  Do not invent a sector for older responses that
    # omit the field; only reject an explicit mismatch or malformed value.
    requested_sector_id = (data or {}).get("sector_id") if isinstance(data, dict) else None
    if normalized and requested_sector_id is not None:
        expected_sector_id = _authoritative_positive_integer_id(requested_sector_id)
        # A caller-provided location selector is part of the trade context. If
        # it is malformed, do not let a response that omits the optional
        # sector_id field turn a success-shaped port record into actionable
        # inventory. The server remains authoritative for proximity; the
        # client must not silently discard an invalid selector and continue.
        if expected_sector_id is None:
            normalized = {}
            if isinstance(resp, dict):
                error = resp.setdefault("error", {})
                if isinstance(error, dict):
                    error.setdefault("message", "The requested sector ID is not authoritative.")
        raw_port = ((resp.get("data") or {}).get("port")
                    if isinstance(resp, dict) and isinstance(resp.get("data"), dict)
                    else None)
        if normalized and isinstance(raw_port, dict):
            # The released port header uses ``sector`` while newer response
            # fixtures may expose the explicit ``sector_id`` alias.  If the
            # server supplies either identity, correlate it with the sector
            # requested by the client; never let an explicitly different
            # location repopulate an actionable dock snapshot.
            returned_sector_key = ("sector_id" if "sector_id" in raw_port else "sector")
            if returned_sector_key not in raw_port:
                returned_sector_id = None
            else:
                returned_sector_id = _authoritative_positive_integer_id(raw_port.get(returned_sector_key))
            if returned_sector_key in raw_port and (expected_sector_id is None or returned_sector_id != expected_sector_id):
                normalized = {}
                if isinstance(resp, dict):
                    error = resp.setdefault("error", {})
                    if isinstance(error, dict):
                        error.setdefault("message", "The server returned a port from a different sector.")
    if normalized == {} and isinstance(resp, dict) and resp.get("status") == "ok":
        response_type = resp.get("type")
        # A successful envelope without the published response schema is not
        # a usable port-info response.  Preserve that contract distinction so
        # the caller can refresh/retry the authoritative dock read instead of
        # treating a schema mismatch as an ordinary visibility refusal.
        if response_type != "trade.port_info":
            error = resp.setdefault("error", {})
            if isinstance(error, dict):
                error.setdefault("message", "The server returned an unexpected port response type.")
    requested_port_id = (data or {}).get("port_id") if isinstance(data, dict) else None
    if normalized and requested_port_id is not None:
        expected_id = _authoritative_positive_integer_id(requested_port_id)
        returned_id = _authoritative_positive_integer_id(normalized.get("id"))
        if expected_id is None or returned_id != expected_id:
            # A success envelope for a different port is not authoritative
            # context for the current dock.  Clear all derived inventory and
            # leave the server response available to the caller for logging.
            normalized = {}
            if isinstance(resp, dict):
                error = resp.setdefault("error", {})
                if isinstance(error, dict):
                    error.setdefault("message", "The server returned a different port than requested.")
    if normalized:
        ctx.state["port_inventory"] = normalized
        ctx.state.pop("port_info_error", None)
        ctx.last_sector_desc.setdefault("port", {})
        ctx.last_sector_desc["port"].update({k: normalized[k] for k in ("id", "name")
                                              if normalized.get(k) is not None})
        ctx.last_sector_desc["port"]["commodities"] = normalized["commodities"]
    else:
        # Do not leave a previous port's stock/prices available after a
        # proximity refusal or malformed response.
        ctx.state.pop("port_inventory", None)
        if isinstance(ctx.last_sector_desc, dict) and isinstance(ctx.last_sector_desc.get("port"), dict):
            ctx.last_sector_desc["port"].pop("commodities", None)
        error = resp.get("error") if isinstance(resp, dict) else {}
        message = error.get("message") if isinstance(error, dict) else None
        ctx.state["port_info_error"] = message or "The server did not disclose port details in this sector."
    return resp


def _safe_refresh_port_info(ctx, data=None) -> bool:
    """Reconcile port state without letting a lost refusal refresh crash trade.

    A quote refusal is not a settlement, but it invalidates the cached port
    snapshot.  The follow-up read can itself lose the connection; preserve the
    fail-closed trade state and let reconnect perform the next authoritative
    refresh instead of leaking the transport exception through the CLI flow.
    """
    if not isinstance(data, dict) or _authoritative_positive_integer_id(data.get("port_id")) is None:
        _invalidate_trade_port_context(
            ctx, "trade port refresh has no authoritative port ID"
        )
        print("Port refresh could not be sent because the port ID is not authoritative.")
        return False
    try:
        response = _refresh_port_info(ctx, data)
    except (ConnectionError, OSError) as exc:
        _invalidate_trade_port_context(
            ctx, str(exc) or "connection lost while refreshing port state"
        )
        print("Port refresh could not be confirmed after connection loss.")
        print("Reconnect before attempting another trade so port direction and stock can be refreshed.")
        return False
    return (isinstance(response, dict)
            and response.get("status") == "ok"
            and isinstance(ctx.state.get("port_inventory"), dict))


def _trade_port_refresh_request(ctx: Context, port_id: int) -> dict:
    """Build a port refresh request from confirmed IDs only.

    Trade reconciliation may run after a stale or malformed sector snapshot.
    The optional sector identity must never be coerced into the wire request;
    the server remains authoritative for proximity and disclosure.
    """
    authoritative_port_id = _authoritative_positive_integer_id(port_id)
    if authoritative_port_id is None:
        return {}
    request = {"port_id": authoritative_port_id}
    sector_id = _authoritative_positive_integer_id(ctx.current_sector_id)
    if sector_id is not None:
        request["sector_id"] = sector_id
    return request


def render_port_summary(port: dict) -> list:
    """Build readable port lines without making RPCs or printing JSON."""
    lines = [f"--- Port: {port.get('name') or 'Port'} ---"]
    rows = port.get("commodities") or []
    if not rows:
        return lines + ["(No commodities reported.)"]
    for row in rows:
        stock = "?" if row.get("available") is None else str(row["available"])
        cap = "?" if row.get("max_quantity") is None else str(row["max_quantity"])
        # The server names the directions from the port's perspective:
        # ``sells`` means the player may BUY, while ``buys`` means the player
        # may SELL. Keep the summary aligned with the buttons and trade
        # request direction; never derive either capability from stock.
        explicit_modes = [(x, row.get(key)) for x, key in (("buy", "sells"), ("sell", "buys"))]
        modes = "/".join(x for x, ok in explicit_modes if ok)
        if not modes and any(ok is None for _, ok in explicit_modes):
            modes = "server decides"
        if not modes:
            modes = "closed"
        price = row.get("base_price")
        indicative = f"~{present_money(price)}" if price is not None else "~?"
        lines.append(f"  {row['commodity']}: stock {stock}/{cap}; {modes}; base {indicative}")
    return lines


def _print_port_summary(ctx):
    inventory = ctx.state.get("port_inventory")
    if not inventory:
        print(f"Port details unavailable · {ctx.state.get('port_info_error', 'inspect a port in your current sector first.')}")
        return
    for line in render_port_summary(inventory):
        print(line)
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

    ConnectionError must always propagate (protocol.Conn is the single
    place that normalises socket failures into it; a lost connection must
    never be silently swallowed here). Other prefetch failures remain
    non-fatal for this slice — that broad `except Exception` is recorded
    technical debt in the handover, not a deliberate design choice.
    """
    if not isinstance(menu_def, dict):
        return
    on_enter = menu_def.get("on_enter")
    if isinstance(on_enter, dict) and on_enter.get("pycall"):
        fn = globals().get(on_enter["pycall"])
        if callable(fn):
            try:
                fn(ctx)
            except ConnectionError:
                raise
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
# Conn and Context now live in protocol.py / state.py (see imports above).
# ---------------------------

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
    my_ship_id = get_my_ship_id(ctx.conn, ctx)
    my_name = get_my_player_name(ctx.conn, ctx)
    # keep beacon count handy for label interpolation
    ctx.state["beacon_count"] = get_my_beacon_count(ctx.conn, ctx)

    pl = ctx.player_info.get("player") or {}
    flags = {
        # Transwarp is a proximity-sensitive mutation.  Keep it available in
        # the normal workflow only after a positive integer sector identity
        # has been established by an authoritative server refresh; the server
        # remains responsible for all equipment, fuel, lock and turn rules.
        "has_authoritative_sector": _authoritative_positive_integer_id(ctx.current_sector_id) is not None,
        "has_port": bool(d.get("port")),
        "is_stardock": ctx.state.get("is_stardock", False),
        "is_shipyard_port": ctx.state.get("is_shipyard_port", False),
        "has_planet": bool(d.get("planets")),
        "can_set_beacon": (d.get("beacon") in (None, "")),
        "has_boardable": has_boardable_ship(ships, my_ship_id=my_ship_id, my_name=my_name),
        "has_tow_target": (
            has_tow_target(ships, my_ship_id=my_ship_id)
            or bool(ctx.state.get("towing_ship_id"))
        ),
        "on_planet": bool(ctx.state.get("on_planet")),
        "planet_has_products": bool(ctx.state.get("planet_products_available")),
        "is_corp_member": bool(pl.get("corp_id")),
        "can_autopilot": bool(ctx.state.get("ap_route_plotted")),
        "has_exchange_access": ctx.state.get("has_exchange_access", False),
        "has_insurance_access": ctx.state.get("has_insurance_access", False),
        "has_tavern_access": ctx.state.get("has_tavern_access", False),
        "has_shipyard_access": bool(ctx.state.get("has_shipyard_access") or ctx.state.get("is_shipyard_port")),
        "has_local_services": bool(
            ctx.state.get("has_exchange_access") or
            ctx.state.get("has_insurance_access") or
            ctx.state.get("has_tavern_access") or
            ctx.state.get("is_shipyard_port") or
            ctx.state.get("has_shipyard_access")
        ),
        "in_corporation": ctx.state.get("in_corporation", False),
        "is_ceo": ctx.state.get("is_ceo", False),
        "is_ceo_or_officer": ctx.state.get("is_ceo_or_officer", False),
        "not_in_corporation": not ctx.state.get("in_corporation", False),
        "corp_not_public": not ctx.state.get("corp_is_public", False),
        "corp_is_public": ctx.state.get("corp_is_public", False)
    }
    ctx.state["has_local_services"] = flags["has_local_services"]
    ctx.state["has_shipyard_access"] = flags["has_shipyard_access"]
    ctx.state["not_in_corporation"] = flags["not_in_corporation"]
    return flags

def _authoritative_density_value(value: Any) -> bool:
    """Accept only the integer density value defined by the V3 response contract."""
    return type(value) is int and value >= 0


@register("sector_info_flow")
def sector_info_flow(ctx: Context):
    """Refresh the server-confirmed current sector without probing remote sectors."""
    try:
        response = ctx.conn.rpc("sector.info", {})
    except (ConnectionError, OSError) as exc:
        # A lost visibility read is not evidence that a sector was disclosed.
        # Preserve the last confirmed cache and let the normal reconnect path
        # establish a fresh location before another proximity-sensitive action.
        ctx.state["last_rpc"] = None
        print(f"[Error] Sector information outcome is uncertain after connection loss: {exc}")
        print("Reconnect before relying on sector information; the confirmed sector was not changed.")
        return
    ctx.state["last_rpc"] = response
    if not isinstance(response, dict) or response.get("status") != "ok":
        raw_error = response.get("error") if isinstance(response, dict) else None
        error = raw_error if isinstance(raw_error, dict) else {}
        print(f"[Error] Sector information refused: {error.get('message', 'The server did not return current-sector information.')}")
        # A refusal can mean the cached location is stale (for example after
        # reconnect).  Re-read only the server-confirmed current sector; do
        # not merge any payload attached to the refused response.
        _refresh_after_scan_refusal(ctx)
        return
    if not _response_type_matches(response, "sector.info"):
        print("[Error] Sector information response had an unexpected type; cached state was not changed.")
        _refresh_after_scan_refusal(ctx)
        return
    data = get_data(response)
    if isinstance(data, dict):
        try:
            refreshed = normalize_sector(data)
        except (TypeError, ValueError, AttributeError, KeyError):
            # A success envelope is not sufficient to replace the confirmed
            # visibility cache.  Keep malformed decoder input fail-closed and
            # use the same authoritative current-sector reconciliation as a
            # refused or mismatched response.
            print("[Error] The server returned malformed sector information; cached state was not changed.")
            _refresh_after_scan_refusal(ctx)
            return
        refreshed_id = refreshed.get("id")
        expected_id = ctx.current_sector_id
        if (type(refreshed_id) is not int or refreshed_id <= 0
                or type(expected_id) is not int or expected_id <= 0
                or refreshed_id != expected_id):
            if type(refreshed_id) is not int or refreshed_id <= 0:
                reason = "without an authoritative sector ID"
            elif type(expected_id) is not int or expected_id <= 0:
                reason = "while the current sector ID is unconfirmed"
            else:
                reason = f"for sector {refreshed_id} instead of confirmed sector {expected_id}"
            print(f"[Error] The server returned sector information {reason}.")
            _refresh_after_scan_refusal(ctx)
            return
        # Commit only normalized server data.  In particular, this keeps
        # undisclosed port class/type fields out of the cache and prevents a
        # display-only refresh from leaving later commands on stale state.
        ctx.last_sector_desc = refreshed
        _pp(refreshed)
    else:
        print("[Error] The server returned an unrecognized sector.info response.")
        # A success envelope with an unusable payload is not authoritative
        # location state. Reconcile the confirmed current sector before the
        # cached view is used for another proximity-sensitive command.
        _refresh_after_scan_refusal(ctx)


@register("detailed_sector_scan_flow")
def detailed_sector_scan_flow(ctx: Context):
    """Display the current sector's detailed scan as returned under fog rules."""
    if _authoritative_positive_integer_id(ctx.current_sector_id) is None:
        print("Detailed scan unavailable: the current sector has no authoritative ID; refresh before scanning.")
        return
    try:
        response = ctx.conn.rpc("sector.scan", {})
    except (ConnectionError, OSError) as exc:
        # A lost read reply is not evidence that the scan succeeded. Preserve
        # the last confirmed sector and make the reconnect/reconciliation
        # requirement explicit instead of leaking a traceback or stale scan UI.
        ctx.state["last_rpc"] = None
        print(f"[Error] Detailed scan outcome is uncertain after connection loss: {exc}")
        print("Reconnect before relying on scan information; the confirmed sector was not changed.")
        return
    ctx.state["last_rpc"] = response
    if not isinstance(response, dict) or response.get("status") != "ok":
        raw_error = response.get("error") if isinstance(response, dict) else None
        error = raw_error if isinstance(raw_error, dict) else {}
        print(f"[Error] Detailed scan refused: {error.get('message', 'The server did not return scan information.')}")
        _refresh_after_scan_refusal(ctx)
        return
    if not _response_type_matches(response, "sector.scan"):
        print("[Error] Detailed scan response had an unexpected type; cached state was not changed.")
        _refresh_after_scan_refusal(ctx)
        return
    data = get_data(response)
    if isinstance(data, dict):
        confirmed_sector_id = ctx.current_sector_id
        reported_sector_id = _authoritative_positive_integer_id(
            data.get("sector_id", data.get("id"))
        )
        confirmed_sector_id = _authoritative_positive_integer_id(confirmed_sector_id)
        if confirmed_sector_id is None:
            print("[Error] Detailed scan cannot be displayed without a confirmed current sector.")
            _refresh_after_scan_refusal(ctx)
            return
        if reported_sector_id is None or reported_sector_id != confirmed_sector_id:
            print("[Error] Detailed scan did not identify the confirmed current sector; cached state was not changed.")
            _refresh_after_scan_refusal(ctx)
            return
        # Keep detailed scans within the same disclosure boundary as fast
        # scans.  Do not dump the response: a schema-mismatched server can
        # attach port class/type or other remote-only fields even when the
        # envelope says "ok".  The renderer exposes only the fields this
        # client is allowed to present and keeps the cached sector untouched.
        rendered_scan = _render_scan_card({"status": "ok", "data": data})
        if rendered_scan.startswith("\n── Fast Scan: unavailable"):
            print("[Error] Detailed scan contained undisclosed data; cached sector state was not changed.")
            _refresh_after_scan_refusal(ctx)
            return
        print(rendered_scan.replace("── Fast Scan:", "── Detailed Scan:", 1))
    else:
        print("[Error] The server returned an unrecognized sector.scan response.")
        _refresh_after_scan_refusal(ctx)


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
        try:
            info = ctx.conn.rpc("player.my_info", {})
            cur = extract_current_sector(info)
        except (ConnectionError, OSError):
            raise
        except Exception:
            pass
    if not cur:
        print("Cannot determine current sector."); return

    # Use the exact fields accepted by the current move.pathfind API schema.
    req = {"from_sector_id": cur, "to_sector_id": target}
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

    hops_value = data.get("hops")
    hops = hops_value if isinstance(hops_value, int) else max(0, len(path) - 1)
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
        if not _perform_warp(ctx, hop):
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

    # Extract sector numbers from adjacent_sectors format
    adj_nums = []
    for a in adj:
        if isinstance(a, dict):
            adj_nums.append(a.get("to_sector"))
        else:
            adj_nums.append(a)

    if target not in adj_nums:
        print(f"{target} is not adjacent. Valid: {', '.join(map(str, adj_nums)) if adj_nums else '(none)'}")
        return

    _perform_warp(ctx, target)


@register("warp_flow")
def warp_flow(ctx: Context):
    raw = input("Warp to sector id: ").strip()
    try:
        target = int(raw)
    except ValueError:
        print("Invalid sector ID."); return

    _perform_warp(ctx, target)


@register("transwarp_flow")
def transwarp_flow(ctx: Context):
    """Request transwarp and return whether relocation was server-confirmed.

    A false result includes refusal, transport uncertainty, terminal hazard,
    and failed reconciliation.  The caller must not treat any of those paths
    as a confirmed destination; the server-owned refreshes below remain the
    only source of post-movement state.
    """
    raw = input("Transwarp to sector id: ").strip()
    try:
        target = int(raw)
    except ValueError:
        print("Invalid sector ID.")
        return False
    if target <= 0:
        print("Sector ID must be greater than zero.")
        return False
    # Transwarp is proximity/state sensitive.  The client may only send it
    # after an authoritative current-sector refresh has established the
    # session's origin; an empty or malformed cache is not a second protocol
    # variant and must not be converted into a request the server cannot bind
    # to the displayed state.
    current_sector_id = _authoritative_positive_integer_id(ctx.current_sector_id)
    if current_sector_id is None:
        print("Transwarp unavailable: refresh the confirmed current sector before moving.")
        return False
    try:
        response = ctx.conn.rpc("move.transwarp", {"to_sector_id": target})
    except (ConnectionError, OSError) as exc:
        # A lost reply cannot establish whether the server charged the turn,
        # relocated the ship, or applied entry hazards.  Keep the last
        # confirmed snapshot intact while attempting the same authoritative
        # reconciliation used for an uncertain movement response.  The refresh
        # may itself fail while the transport is down; that failure remains
        # uncertainty and must not be converted into a local destination or
        # damage claim.
        ctx.state["last_rpc"] = None
        print(f"Transwarp outcome is uncertain after connection loss: {exc}")
        _safe_move_error_refresh(ctx)
        print("Reconnect before attempting another move so location, turns, and ship state can be refreshed.")
        return False
    ok, message = present_move_result(response)
    if not _movement_response_type_matches(response, "move.transwarp"):
        print("Transwarp response had an unexpected type; refreshing confirmed player, ship and sector state.")
        _safe_move_error_refresh(ctx)
        return False
    for hazard in present_move_hazards(response):
        print(hazard)
    if _move_response_is_terminal(response):
        if not ok:
            print(message)
        _safe_move_error_refresh(ctx)
        return False
    if not ok:
        print(message)
        # A refusal is still an authoritative movement boundary: the server
        # owns whether validation, fuel/interdiction checks, rollback, or
        # entry-hazard handling changed player/ship/location state. Reconcile
        # all affected snapshots before another move instead of inferring
        # side effects (or their absence) from the refusal envelope.
        _safe_move_error_refresh(ctx)
        return False
    # The transwarp success schema names the authoritative destination
    # ``sector_id``. Do not let a generic movement alias authorize completion;
    # reconcile all affected state when the server omits that canonical field.
    # Terminal/error envelopes are handled above because their authoritative
    # hazard report and destination live in ``error.data`` rather than the
    # successful response's top-level ``data`` object.
    response_data = get_data(response)
    response_sector_id = response_data.get("sector_id") if isinstance(response_data, dict) else None
    response_status = response_data.get("status") if isinstance(response_data, dict) else None
    if (type(response_sector_id) is not int or response_sector_id <= 0
            or response_sector_id != target
            or response_status != "transwarp_complete"):
        print("Transwarp was not confirmed by the server's canonical destination field; refreshing confirmed player, ship and sector state.")
        _safe_move_error_refresh(ctx)
        return False
    actual_sector = _safe_move_refresh(ctx, expected_sector_id=target)
    if actual_sector != target:
        print("Transwarp was accepted, but the requested destination could not be confirmed; navigation stopped.")
        return False
    print(f"Transwarped to sector {target}.")
    call_handler("redisplay_sector", ctx)
    return True


def _refresh_current_sector_after_move(
    ctx: Context, expected_sector_id: Optional[int] = None
) -> Optional[int]:
    """Replace the old sector only with a matching current-sector read.

    Successful movement has a requested destination, so a response naming a
    different sector is not usable current-location state. Error recovery
    leaves ``expected_sector_id`` unset because the server may have committed
    movement before returning the error and must be allowed to identify the
    actual current sector.
    """
    # Keep the last confirmed snapshot intact until the replacement has been
    # fully validated. A disconnect or schema-mismatched response after a
    # committed move is uncertainty, not evidence that the previous snapshot
    # should be erased.
    response = ctx.conn.rpc("move.describe_sector", {})
    if not isinstance(response, dict) or response.get("status") != "ok":
        return None
    # Movement reconciliation is an authoritative refresh boundary.  A
    # success-shaped payload from another operation must not be accepted just
    # because it happens to contain a sector-looking object; after entry
    # hazards that could make the pre-move cache appear confirmed.
    if not _response_type_matches(response, "sector.info"):
        return None
    data = get_data(response)
    if not isinstance(data, dict):
        return None
    try:
        sector = normalize_sector(data)
    except (TypeError, ValueError, AttributeError):
        return None
    sector_id = sector.get("id")
    if type(sector_id) is not int or sector_id <= 0:
        return None
    if (expected_sector_id is not None
            and (type(expected_sector_id) is not int or expected_sector_id <= 0
                 or sector_id != expected_sector_id)):
        return None
    ctx.last_sector_desc = sector
    ctx.hud = hud_module.merge_hud(ctx.hud, hud_module.extract_sector_fields(sector))
    return sector_id


def _reconcile_after_move_error(ctx: Context) -> None:
    """A server error can follow a committed move or sector-entry damage."""
    sector_id = _refresh_current_sector_after_move(ctx)
    # Hazard processing may have committed before the movement command
    # returned its error/refusal. Only merge the follow-up player record when
    # it carries the authoritative identity for this session; an anonymous or
    # mismatched record must not change turns, credits, or corp gating.
    player = _refresh_player_after_landing(ctx, require_identity=True)
    refreshed_ship_id = None
    if _landing_player_refresh_is_current(ctx, player):
        data = get_data(player)
        if isinstance(data, dict) and isinstance(data.get("player"), dict):
            ctx.player_info = data
            candidate_ship_id = data["player"].get("ship_id")
            if type(candidate_ship_id) is int and candidate_ship_id > 0:
                refreshed_ship_id = candidate_ship_id
            # Keep capability/menu gating in step with the same authoritative
            # player refresh used after a successful hazard-bearing move.
            # An error/refusal envelope may still follow committed entry
            # effects, so retaining the pre-move corporation context is unsafe.
            _update_corp_context(ctx)
    _refresh_ship_after_move(ctx, expected_ship_id=refreshed_ship_id)
    if sector_id is None:
        print("Current sector could not be refreshed after the movement error; navigation state is unavailable.")
    else:
        print(f"Current sector refreshed after the movement error: {sector_id}.")


def _safe_move_refresh(ctx: Context, expected_sector_id: Optional[int]) -> Optional[int]:
    """Reconcile movement aftermath without treating a lost read as success."""
    try:
        actual_sector = _refresh_current_sector_after_move(
            ctx, expected_sector_id=expected_sector_id
        )
        # Entry hazards can change player-owned state. Require the refresh to
        # identify this session before merging those changes into the HUD or
        # capability context.
        player_response = _refresh_player_after_landing(ctx, require_identity=True)
        # Movement entry hazards can change turns, credits, alignment, or
        # other player-owned fields. Keep the session's authoritative player
        # record in step with the HUD refresh so a later landing/planet action
        # does not validate against a pre-hazard snapshot.
        if _landing_player_refresh_is_current(ctx, player_response):
            player_data = get_data(player_response)
            if isinstance(player_data, dict) and isinstance(player_data.get("player"), dict):
                ctx.player_info = player_data
                _update_corp_context(ctx)
        _refresh_ship_after_move(ctx)
        return actual_sector
    except (ConnectionError, OSError) as exc:
        # Movement or entry hazards may already have committed. A failed
        # follow-up read therefore cannot safely restore the pre-move state or
        # establish the destination; require the normal reconnect refresh.
        ctx.state["last_rpc"] = None
        print(f"Movement was accepted, but post-move state could not be confirmed after connection loss: {exc}")
        print("Reconnect before relying on location, turns, or ship state.")
        return None


def _safe_move_error_refresh(ctx: Context) -> None:
    """Reconcile a movement error without turning a lost read into a crash."""
    try:
        _reconcile_after_move_error(ctx)
    except (ConnectionError, OSError) as exc:
        ctx.state["last_rpc"] = None
        print(f"Movement error state could not be confirmed after connection loss: {exc}")
        print("Reconnect before relying on location, turns, or ship state.")


def _reconcile_after_combat_refusal(ctx: Context) -> None:
    """Refresh authoritative aftermath when combat returns a refusal.

    A refusal envelope does not prove that no state changed: server-side
    enforcement may destroy the active ship before returning the refusal.
    Refresh the session-owned player relationship first so a replacement
    escape pod can be accepted by the subsequent ship refresh.
    """
    try:
        player_response = _refresh_player_after_landing(ctx, require_identity=True)
        refreshed_ship_id = None
        if _landing_player_refresh_is_current(ctx, player_response):
            data = get_data(player_response)
            if isinstance(data, dict) and isinstance(data.get("player"), dict):
                ctx.player_info = data
                candidate_ship_id = data["player"].get("ship_id")
                if type(candidate_ship_id) is int and candidate_ship_id > 0:
                    refreshed_ship_id = candidate_ship_id
                _update_corp_context(ctx)
        _refresh_ship_after_move(ctx, expected_ship_id=refreshed_ship_id)
        _refresh_current_sector_after_move(ctx)
    except (ConnectionError, OSError) as exc:
        ctx.state["last_rpc"] = None
        print(f"Combat refusal aftermath could not be confirmed after connection loss: {exc}")
        print("Reconnect before relying on player, ship, or location state.")


def _move_response_is_terminal(response: Any) -> bool:
    """Return true only for an explicit server-reported terminal outcome."""
    if not isinstance(response, dict):
        return False
    sources = []
    data = response.get("data")
    if isinstance(data, dict):
        sources.append(data)
    error = response.get("error")
    if isinstance(error, dict) and isinstance(error.get("data"), dict):
        sources.append(error["data"])
    for source in sources:
        if (source.get("ship_destroyed") is True
                or source.get("ship_remains_in_sector") is False):
            return True
        hazards = source.get("hazards")
        if not isinstance(hazards, list):
            continue
        # Some authoritative hazard schemas attach terminal state to the
        # individual hazard row rather than repeating it at envelope level.
        # Consume those explicit booleans, but never infer terminal state from
        # damage, hull loss, or any other descriptive field.
        for hazard in hazards:
            if not isinstance(hazard, dict):
                continue
            if (hazard.get("destroyed") is True
                    or hazard.get("ship_remains_in_sector") is False):
                return True
    return False


def _perform_warp(ctx: Context, target: int) -> bool:
    """
    Single, authoritative move.warp caller used by every movement flow.

    The server response is treated as authoritative: the cached sector is
    refreshed from the current-sector read after a successful move. On a
    server error, refresh before assuming the move had no side effects.
    """
    try:
        resp = ctx.conn.rpc("move.warp", {"to_sector_id": target})
    except (ConnectionError, OSError) as exc:
        # A lost reply cannot establish whether the server charged the turn,
        # relocated the ship, or applied entry hazards. Keep the last
        # confirmed snapshot intact and require reconnect/reconciliation
        # instead of presenting a locally inferred outcome.
        ctx.state["last_rpc"] = None
        print(f"Warp outcome is uncertain after connection loss: {exc}")
        print("Reconnect before attempting another move so location, turns, and ship state can be refreshed.")
        return False
    ok, message = present_move_result(resp)
    if not _movement_response_type_matches(resp, "move.result"):
        print("Warp response had an unexpected type; destination could not be confirmed, refreshing confirmed player, ship and sector state.")
        _safe_move_error_refresh(ctx)
        return False
    for hazard in present_move_hazards(resp):
        print(hazard)
    if _move_response_is_terminal(resp):
        if not ok:
            print(message)
        _safe_move_error_refresh(ctx)
        return False
    if ok:
        actual_sector = _safe_move_refresh(ctx, expected_sector_id=target)
        if actual_sector != target:
            print("Move was accepted, but the requested destination could not be confirmed; navigation stopped.")
            return False
        print(f"Moved to sector {target}.")
        call_handler("redisplay_sector", ctx)
    else:
        print(message)
        # A refused response can still carry an authoritative hazard report
        # from entry processing.  Reconcile before the next action whenever
        # the server supplied that report; do not infer whether movement or
        # damage committed from the refusal status alone.
        has_refused_hazards = bool(present_move_hazards(resp))
        if (isinstance(resp, dict)
                and (resp.get("status") == "error"
                     or (resp.get("status") == "refused" and has_refused_hazards))):
            _safe_move_error_refresh(ctx)
    return ok


# ---------- Settings pretty printers ----------
def _pp(obj):
    import json
    print(json.dumps(obj, ensure_ascii=False, indent=2))


@register("settings_view")
def settings_view(ctx: Context):
    resp = ctx.conn.rpc("player.get_settings", {})
    ctx.state["last_rpc"] = resp
    if resp.get("status") != "ok":
        err = resp.get("error") or {}
        print(f"[Error] Failed to get settings: {err.get('message', 'Unknown error')}")
        return
    try:
        model = normalize_settings(resp)
    except NormalizationError as e:
        print(f"[Error] Malformed settings response: {e}")
        return

    # notes.list is a separate confirmed handler; player.settings_v1 does not
    # include notes, so merge it in rather than inventing an aggregate field.
    notes_resp = ctx.conn.rpc("notes.list", {})
    try:
        model["notes"] = normalize_notes_list(notes_resp) if notes_resp.get("status") == "ok" else []
    except NormalizationError:
        model["notes"] = []

    print(present_settings(model))


@register("change_password_flow")
def change_password_flow(ctx: Context):
    """Change the authenticated player's password without echoing secrets."""
    old_password = getpass.getpass("Current password: ")
    new_password = getpass.getpass("New password: ")
    confirmation = getpass.getpass("Confirm new password: ")
    if not old_password or not new_password:
        print("Password change cancelled: password fields cannot be empty.")
        return
    if new_password != confirmation:
        print("Password change cancelled: the new passwords do not match.")
        return
    response = ctx.conn.rpc("auth.change_password", {
        "old_password": old_password,
        "new_password": new_password,
    })
    ctx.state["last_rpc"] = response
    if response.get("status") == "ok":
        print("Password changed. Log out and sign in with the new password next time.")
    elif (response.get("error") or {}).get("code") == 1101:
        print("Password change refused: the current password was not accepted.")
    else:
        print("Password change refused. Check the server response and try again.")


def authenticate(conn: Conn, username: str, password: str) -> dict:
    """Authenticate and retain a session token on this connection."""
    response = conn.rpc("auth.login", {"username": username, "passwd": password})
    if response.get("status") == "ok":
        token = (response.get("data") or {}).get("session_token")
        if token:
            conn.session_token = token
    return response


def registration_payload(username: str, password: str, ship_name: str = "",
                         ui_locale: str = "", ui_timezone: str = "") -> dict:
    """Build the public auth.register payload after minimal local validation."""
    username = str(username or "").strip()
    if not username:
        raise ValueError("Captain name is required.")
    if not password:
        raise ValueError("Password is required.")
    payload = {"username": username, "passwd": password}
    for key, value in (("ship_name", ship_name), ("ui_locale", ui_locale),
                       ("ui_timezone", ui_timezone)):
        value = str(value or "").strip()
        if value:
            payload[key] = value
    return payload


def validate_registration_passwords(password: str, confirmation: str) -> None:
    """Reject an empty or mistyped password before sending auth.register."""
    if not password:
        raise ValueError("Password is required.")
    if password != confirmation:
        raise ValueError("The passwords do not match.")


def register_account(conn: Conn, username: str, password: str,
                     ship_name: str = "", ui_locale: str = "",
                     ui_timezone: str = "") -> dict:
    """Create an account through the public protocol and retain its session."""
    response = conn.rpc("auth.register", registration_payload(
        username, password, ship_name, ui_locale, ui_timezone))
    if response.get("status") == "ok":
        token = (response.get("data") or {}).get("session_token")
        if isinstance(token, str) and token:
            conn.session_token = token
    return response


@register("prefs_toggle_24h")
def prefs_toggle_24h(ctx: Context):
    # Fetch current to compute toggle
    cur = ctx.conn.rpc("player.get_prefs", {})
    try:
        now = normalize_prefs(cur)
    except NormalizationError as e:
        print(f"[Error] Malformed preferences response: {e}")
        return
    new_val = not str(now.get("ui.clock_24h", "true")).lower() in ("true", "1", "yes")
    r = ctx.conn.rpc("player.set_prefs", {"items": [{"key": "ui.clock_24h", "type": "bool", "value": new_val}]})
    _pp(r)

@register("prefs_set_locale")
def prefs_set_locale(ctx: Context):
    loc = input("Locale (e.g., en-GB): ").strip() or "en-GB"
    r = ctx.conn.rpc("player.set_prefs", {"items":[{"key":"ui.locale","type":"string","value": loc}]})
    _pp(r)

@register("trade_account_preference_flow")
def trade_account_preference_flow(ctx: Context):
    """Persist the server-owned default account used by subsequent trades."""
    choice = input("Default trade account [P]etty cash / [B]ank (Enter cancels): ").strip().lower()
    if choice in ("p", "petty", "petty cash", "0"):
        preference, label = 0, "petty cash"
    elif choice in ("b", "bank", "1"):
        preference, label = 1, "bank"
    else:
        print("Trade-account preference unchanged.")
        return
    response = ctx.conn.rpc("player.set_trade_account_preference", {"preference": preference})
    ctx.state["last_rpc"] = response
    if isinstance(response, dict) and response.get("status") == "ok":
        print(f"Default trade account saved as {label}. Subsequent trades will use the server preference when no account is selected.")
    else:
        error = response.get("error") if isinstance(response, dict) else {}
        error = error if isinstance(error, dict) else {}
        print(f"Trade-account preference was not saved: {error.get('message', 'The server refused the request')}.")

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

@register("bookmark_list_flow")
def bookmark_list_flow(ctx: Context):
    r = ctx.conn.rpc("nav.bookmark.list", {})
    ctx.state["last_rpc"] = r
    if r.get("status") != "ok":
        err = r.get("error") or {}
        print(f"[Error] Failed to list bookmarks: {err.get('message', 'Unknown error')}")
        return
    try:
        bookmarks = normalize_bookmark_list(r)
    except NormalizationError as e:
        print(f"[Error] Malformed bookmark list response: {e}")
        return
    print(present_bookmarks(bookmarks))

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
    if r.get("status") != "ok":
        _pp(r) # Fallback to pretty print full response on error
        return
    try:
        avoid = normalize_avoid_list(r)
    except NormalizationError as e:
        print(f"[Error] Malformed avoid list response: {e}")
        return
    print(present_avoid_list(avoid))


def _fine_id_from_record(fine: dict) -> Optional[int]:
    value = fine.get("id", fine.get("fine_id"))
    if type(value) is int and value > 0:
        return value
    if isinstance(value, str) and value.isdigit() and int(value) > 0:
        return int(value)
    return None


@register("fine_list_flow")
def fine_list_flow(ctx: Context):
    """Show server-authoritative fines without deriving balances or status."""
    response = _hud_rpc(ctx, "fine.list", {})
    ctx.state["last_rpc"] = response
    if response.get("status") != "ok":
        print("Could not list outstanding fines.")
        _pp(response)
        return
    data = get_data(response)
    fines = data.get("fines") if isinstance(data, dict) else None
    if not isinstance(fines, list):
        print("The server returned an invalid fine list.")
        return
    if not fines:
        print("No outstanding fines.")
        return
    print("Outstanding fines:")
    for fine in fines:
        if not isinstance(fine, dict):
            continue
        fine_id = _fine_id_from_record(fine)
        if fine_id is None:
            continue
        amount = fine.get("amount", "unreported")
        reason = fine.get("reason", "Reason not reported")
        status = fine.get("status", "unreported")
        print(f"  ID {fine_id} · {amount} CR · {reason} · {status}")


@register("fine_pay_flow")
def fine_pay_flow(ctx: Context):
    """Pay one outstanding listed fine; the server validates ownership and funds."""
    response = _hud_rpc(ctx, "fine.list", {})
    ctx.state["last_rpc"] = response
    if response.get("status") != "ok":
        print("Could not load fines before payment.")
        _pp(response)
        return
    data = get_data(response)
    fines = data.get("fines") if isinstance(data, dict) else None
    if not isinstance(fines, list):
        print("The server returned an invalid fine list.")
        return
    unpaid: dict[int, dict] = {}
    for fine in fines:
        if not isinstance(fine, dict) or str(fine.get("status", "")).lower() == "paid":
            continue
        fine_id = _fine_id_from_record(fine)
        if fine_id is not None:
            unpaid[fine_id] = fine
    if not unpaid:
        print("No outstanding fines to pay.")
        return
    print("Outstanding fines:")
    for fine_id, fine in unpaid.items():
        print(f"  ID {fine_id} · {fine.get('amount', 'unreported')} CR · {fine.get('reason', 'Reason not reported')}")
    try:
        fine_id = int(input("Fine ID to pay (Enter to cancel): ").strip())
    except ValueError:
        print("Fine payment cancelled or invalid ID.")
        return
    if fine_id not in unpaid:
        print("That fine is not in the server's current outstanding fine list.")
        return
    if input(f"Pay the full listed fine {fine_id}? [y/N]: ").strip().lower() != "y":
        print("Fine payment cancelled.")
        return
    payment = ctx.conn.rpc("fine.pay", {"fine_id": fine_id})
    ctx.state["last_rpc"] = payment
    if payment.get("status") != "ok":
        print("Fine payment refused or failed:")
        _pp(payment)
        return
    result = get_data(payment)
    amount_paid = result.get("amount_paid", "unreported") if isinstance(result, dict) else "unreported"
    print(f"Fine {fine_id} paid: {amount_paid} CR (confirmed by server).")
    refreshed = _hud_rpc(ctx, "player.my_info", {})
    if refreshed.get("status") == "ok":
        ctx.player_info = get_data(refreshed)
    else:
        print("Warning: fine payment succeeded, but player credits could not be refreshed.")


@register("trade_history_flow")
def trade_history_flow(ctx: Context):
    """Read server trade records, following only its opaque pagination cursor."""
    cursor = None
    seen_cursors = set()
    while True:
        request = {"limit": 20}
        if cursor is not None:
            request["cursor"] = cursor
        response = ctx.conn.rpc("trade.history", request)
        ctx.state["last_rpc"] = response
        if not isinstance(response, dict) or response.get("status") != "ok":
            print("Could not retrieve trade history:")
            _pp(response)
            return
        data = get_data(response)
        rows = data.get("history") if isinstance(data, dict) else None
        if not isinstance(rows, list):
            print("The server returned an invalid trade history response.")
            return
        if not rows:
            print("No trade history on this page." if cursor else "No trade history is available.")
        for row in rows:
            if not isinstance(row, dict):
                continue
            print("  {action} {units} {commodity} · {price} CR/unit · port {port} · time {timestamp}".format(
                action=row.get("action", "unreported"),
                units=row.get("units", "unreported"),
                commodity=row.get("commodity", "unreported"),
                price=row.get("price_per_unit", "unreported"),
                port=row.get("port_id", "unreported"),
                timestamp=row.get("timestamp", "unreported"),
            ))
        next_cursor = data.get("next_cursor") if isinstance(data, dict) else None
        if not isinstance(next_cursor, str) or not next_cursor:
            return
        if next_cursor in seen_cursors or next_cursor == cursor:
            print("Trade history stopped because the server repeated its page cursor.")
            return
        seen_cursors.add(next_cursor)
        if input("More trade history? [y/N]: ").strip().lower() != "y":
            return
        cursor = next_cursor


def _trade_offer_action_result(ctx: Context, command: str, offer_id: int) -> None:
    response = _hud_rpc(ctx, command, {"offer_id": offer_id})
    ctx.state["last_rpc"] = response
    if response.get("status") != "ok":
        print("Trade offer action refused by the server:")
        _pp(response)
        return
    data = get_data(response)
    data = data if isinstance(data, dict) else {}
    print(f"Trade offer {data.get('offer_id', offer_id)}: {data.get('status', 'updated by server')}.")


@register("trade_offer_create_flow")
def trade_offer_create_flow(ctx: Context):
    """Create a public-protocol player offer; the server validates assets."""
    try:
        recipient_id = int(input("Recipient player ID: ").strip())
        commodity = input("Three-character commodity code: ").strip().upper()
        quantity = int(input("Quantity: ").strip())
        price = int(input("Price per unit in credits (0 or more): ").strip())
    except (ValueError, EOFError):
        print("Invalid trade offer input; no request was sent.")
        return
    mode = input("Offer mode [buy/sell]: ").strip().lower()
    if (recipient_id <= 0 or quantity <= 0 or price < 0
            or not re.fullmatch(r"[A-Z0-9]{3}", commodity)
            or mode not in {"buy", "sell"}):
        print("Invalid recipient, commodity, quantity, price or mode; no request was sent.")
        return
    total = quantity * price
    print("Trade offers do not reserve cargo or credits. The server rechecks both accounts when accepted.")
    if input(f"Create {mode} offer: {quantity} {commodity} at {price} CR each ({total} CR total) to player {recipient_id}? [y/N]: ").strip().lower() != "y":
        print("Trade offer cancelled.")
        return
    data = {
        "to_player_id": recipient_id,
        "commodity": commodity,
        "quantity": quantity,
        "price": price,
        "mode": mode,
        "idempotency_key": str(uuid.uuid4()),
    }
    response = _hud_rpc(ctx, "trade.offer", data)
    ctx.state["last_rpc"] = response
    if response.get("status") != "ok":
        print("Trade offer creation refused by the server:")
        _pp(response)
        return
    result = get_data(response)
    result = result if isinstance(result, dict) else {}
    print(f"Trade offer {result.get('offer_id', 'created')} created; status {result.get('status', 'unreported')}.")


def _trade_offer_id_prompt() -> Optional[int]:
    try:
        offer_id = int(input("Trade offer ID (Enter to cancel): ").strip())
    except (ValueError, EOFError):
        return None
    return offer_id if offer_id > 0 else None


@register("trade_offer_accept_flow")
def trade_offer_accept_flow(ctx: Context):
    """Accept an offer by its server-issued ID after explicit confirmation."""
    offer_id = _trade_offer_id_prompt()
    if offer_id is None:
        print("Invalid offer ID; no request was sent.")
        return
    if input(f"Accept trade offer {offer_id}? The server will recheck cargo, holds and credits. [y/N]: ").strip().lower() != "y":
        print("Trade acceptance cancelled.")
        return
    _trade_offer_action_result(ctx, "trade.accept", offer_id)


@register("trade_offer_cancel_flow")
def trade_offer_cancel_flow(ctx: Context):
    """Cancel an offer by its server-issued ID after explicit confirmation."""
    offer_id = _trade_offer_id_prompt()
    if offer_id is None:
        print("Invalid offer ID; no request was sent.")
        return
    if input(f"Cancel trade offer {offer_id}? [y/N]: ").strip().lower() != "y":
        print("Trade cancellation cancelled.")
        return
    _trade_offer_action_result(ctx, "trade.cancel", offer_id)


# get_data / extract_current_sector / normalize_sector now live in protocol.py.


def get_my_player_name(conn: Conn, ctx: Optional[Context] = None) -> Optional[str]:
    try:
        if ctx and ctx.player_info:
            d = ctx.player_info
        else:
            d = get_data(conn.rpc("player.my_info", {}))
    except Exception:
        return None
    
    # Try nested structure first
    pl = d.get("player") or {}
    for key in ("username", "name", "player_name", "display_name"):
        v = pl.get(key)
        if isinstance(v, str) and v.strip(): return v.strip()
        
    # Fallback to top level
    for key in ("username", "name", "player_name", "display_name"):
        v = d.get(key)
        if isinstance(v, str) and v.strip(): return v.strip()
    return None

def get_my_ship_id(conn: Conn, ctx: Optional[Context] = None) -> Optional[int]:
    try:
        if ctx and ctx.player_info:
            d = ctx.player_info
        else:
            d = get_data(conn.rpc("player.my_info", {}))
    except Exception:
        return None

    if not isinstance(d, dict):
        return None

    # A player ID is not a ship ID, even when both happen to be integers.
    pl = d.get("player") if isinstance(d.get("player"), dict) else {}
    for k in ("ship_id", "current_ship_id"):
        if type(pl.get(k)) is int and pl[k] > 0: return pl[k]

    # Try top-level
    for k in ("ship_id", "current_ship_id"):
        if type(d.get(k)) is int and d[k] > 0: return d[k]
        
    # Try ship object
    ship = d.get("ship") if isinstance(d.get("ship"), dict) else {}
    sid = ship.get("id") or ship.get("ship_id")
    if type(sid) is int and sid > 0: return sid
    
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

def get_my_beacon_count(conn: Conn, ctx: Optional[Context] = None) -> Optional[int]:
    try:
        if ctx and ctx.player_info:
            d = ctx.player_info
        else:
            d = get_data(conn.rpc("player.my_info", {}))
        return _extract_beacon_count(d)
    except Exception:
        return None

def has_boardable_ship(ships, my_ship_id: Optional[int] = None, my_name: Optional[str] = None) -> bool:
    my_name_lc = (my_name or "").strip().lower()
    for s in ships or []:
        if not isinstance(s, dict):
            continue
        sid = _authoritative_ship_row_id(s)
        if sid is None:
            continue
        owner = s.get("owner")
        if owner is not None and not isinstance(owner, str):
            continue
        owner_lc = (owner or "").strip().lower()
        is_derelict = (not owner_lc) or owner_lc == "derelict"
        is_owned = bool(my_name_lc) and owner_lc == my_name_lc
        if not is_derelict and not is_owned:
            continue
        if my_ship_id is not None and isinstance(sid, int) and sid == my_ship_id:
            continue
        return True
    return False


def _authoritative_ship_row_id(ship: Any) -> Optional[int]:
    """Return one non-contradictory server ship ID from a visible row.

    Sector payloads may expose either canonical ID spelling, but two different
    positive IDs are not a compatibility variant.  Such a row must not become
    an ownership or boarding mutation target.
    """
    if not isinstance(ship, dict):
        return None
    present = []
    for key in ("id", "ship_id"):
        if key in ship:
            value = ship.get(key)
            if type(value) is not int or value <= 0:
                return None
            present.append(value)
    if not present or len(set(present)) != 1:
        return None
    return present[0]

def has_tow_target(ships, my_ship_id=None) -> bool:
    return bool(_tow_targets(ships, my_ship_id))


def _tow_targets(ships, my_ship_id=None):
    """Return identifiable ships other than our active ship.

    The server remains authoritative for ownership, pilot, and tow eligibility;
    this only prevents offering malformed rows or our own ship as a target.
    """
    targets = []
    for ship in ships or []:
        if not isinstance(ship, dict):
            continue
        sid = _authoritative_positive_integer_id(ship.get("id"))
        if sid is None:
            sid = _authoritative_positive_integer_id(ship.get("ship_id"))
        if sid is None or sid == my_ship_id:
            continue
        targets.append(ship)
    return targets

def _get_menu_flags(ctx: Context) -> dict:
    d = ctx.state.get("sector_info", {})
    ships = d.get("ships", [])
    my_ship_id = get_my_ship_id(ctx.conn, ctx)
    my_name = get_my_player_name(ctx.conn, ctx)
    # keep beacon count handy for label interpolation
    ctx.state["beacon_count"] = get_my_beacon_count(ctx.conn, ctx)

    pl = ctx.player_info.get("player") or {}
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
        "is_corp_member": bool(pl.get("corp_id")),
        "can_autopilot": bool(ctx.state.get("ap_route_plotted")),
        "has_exchange_access": ctx.state.get("has_exchange_access", False),
        "has_insurance_access": ctx.state.get("has_insurance_access", False),
        "has_tavern_access": ctx.state.get("has_tavern_access", False),
        "has_shipyard_access": bool(ctx.state.get("has_shipyard_access") or ctx.state.get("is_shipyard_port")),
        "has_local_services": bool(
            ctx.state.get("has_exchange_access") or
            ctx.state.get("has_insurance_access") or
            ctx.state.get("has_tavern_access") or
            ctx.state.get("is_shipyard_port") or
            ctx.state.get("has_shipyard_access")
        ),
        "in_corporation": ctx.state.get("in_corporation", False),
        "is_ceo": ctx.state.get("is_ceo", False),
        "is_ceo_or_officer": ctx.state.get("is_ceo_or_officer", False),
        "not_in_corporation": not ctx.state.get("in_corporation", False),
        "corp_not_public": not ctx.state.get("corp_is_public", False),
        "corp_is_public": ctx.state.get("corp_is_public", False),
    }
    ctx.state["has_local_services"] = flags["has_local_services"]
    ctx.state["has_shipyard_access"] = flags["has_shipyard_access"]
    ctx.state["not_in_corporation"] = flags["not_in_corporation"]
    return flags

def _update_corp_context(ctx: Context, force: bool = False):
    """
    Fetches and updates corporation-related context flags in ctx.state.
    Should be called after player_info changes or a corp-related action.
    """
    if not force and ctx.player_info:
        # player.my_info publishes membership as player.corp_id. Older client
        # code looked for a nested corporation object the server does not send.
        corp_data = ctx.player_info.get("corporation")
        player_data = ctx.player_info.get("player", {})
        corp_id = player_data.get("corp_id") if isinstance(player_data, dict) else None
        if corp_id is None:
            corp_id = ctx.player_info.get("corp_id")
        ctx.state["corp_id"] = int(corp_id) if isinstance(corp_id, int) and corp_id > 0 else None
        ctx.state["in_corporation"] = bool(ctx.state["corp_id"] or (corp_data and isinstance(corp_data, dict)))
        ctx.state["is_corp_member"] = ctx.state["in_corporation"]
        return

    ctx.state["in_corporation"] = False
    ctx.state["is_ceo"] = False
    ctx.state["is_ceo_or_officer"] = False
    ctx.state["corp_is_public"] = False
    ctx.state["corp_not_public"] = True # Default until proven otherwise

    # Fetch player_info to get basic corp membership status
    my_info_resp = _hud_rpc(ctx, "player.my_info", {})
    if my_info_resp.get("status") == "ok":
        ctx.player_info = get_data(my_info_resp)
    else:
        # If player_info can't be fetched, can't determine corp status
        return

    corp_data = ctx.player_info.get("corporation")
    player_data = ctx.player_info.get("player", {})
    corp_id = player_data.get("corp_id") if isinstance(player_data, dict) else None
    if corp_id is None:
        corp_id = ctx.player_info.get("corp_id")
    ctx.state["corp_id"] = int(corp_id) if isinstance(corp_id, int) and corp_id > 0 else None
    if ctx.state["corp_id"] or (corp_data and isinstance(corp_data, dict)):
        ctx.state["in_corporation"] = True
        ctx.state["is_corp_member"] = True
        
        # Only do the heavy lifting if forced
        if not force:
            return

        # Fetch corp.status to get role and public status
        corp_status_resp = ctx.conn.rpc("corp.status", {})
        if corp_status_resp.get("status") == "ok":
            corp_status_data = get_data(corp_status_resp)
            ctx.state["corp_id"] = corp_status_data.get("corp_id", ctx.state.get("corp_id"))
            if corp_status_data.get("your_role") in ("Leader", "CEO"):
                ctx.state["is_ceo"] = True
                ctx.state["is_ceo_or_officer"] = True
            elif corp_status_data.get("your_role") == "Officer":
                ctx.state["is_ceo_or_officer"] = True
            # Note: No read endpoint exists for public corporation status.
            # corp_is_public stays False/unverified; validation is server-owned.
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

    # A response type alone does not establish a quote.  Refused responses
    # must remain non-actionable even if a server/proxy echoes stale data in
    # the envelope; only an authoritative successful quote may reach the
    # trade presentation surface.
    if not isinstance(resp, dict) or resp.get("status") != "ok":
        error = resp.get("error") if isinstance(resp, dict) else None
        message = error.get("message") if isinstance(error, dict) else None
        print(f"[Error] Trade quote refused; refresh the port before trading. {message or ''}".rstrip())
        return

    # The server publishes one typed quote response.  Do not render an
    # untyped success envelope (or a settlement receipt) as a quote: doing so
    # would present non-authoritative values as actionable trade information.
    if not _trade_quote_type_matches(resp):
        print("[Error] Unexpected trade.quote response type; refresh the port before trading.")
        return

    data = resp.get("data")
    if not _trade_quote_display_data_is_actionable(data):
        print("[Error] Invalid trade.quote response data.")
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
    print(f"  Buy Price (per unit):  {present_money(buy_price)}")
    print(f"  Sell Price (per unit): {present_money(sell_price)}")
    print(f"  Total Buy Price:   {present_money(total_buy_price)}")
    print(f"  Total Sell Price:  {present_money(total_sell_price)}")


def _trade_quote_display_data_is_actionable(data: Any) -> bool:
    """Require the quote identity fields before exposing a trade screen.

    The server owns the port, commodity, and quantity that a quote authorizes.
    A typed envelope with malformed identity is not a compact response variant;
    it must not reach presentation (or fail while calling ``capitalize``).
    Price fields remain optional display values and are handled by
    ``present_money`` without becoming client-side trade rules.
    """
    if not isinstance(data, dict):
        return False
    port_id = _authoritative_positive_integer_id(data.get("port_id"))
    quantity = data.get("quantity")
    commodity = data.get("commodity")
    if not (port_id is not None
            and type(quantity) is int and quantity > 0
            and isinstance(commodity, str)
            and bool(commodity)
            and commodity == commodity.strip()):
        return False
    # The server publishes prices as numeric JSON values (the C handler uses a
    # JSON real for per-unit prices and integers for totals).  A typed quote
    # with a string, boolean, NaN, or negative price is not actionable: do not
    # let malformed display data reach the confirmation/settlement boundary.
    for key in ("buy_price", "sell_price", "total_buy_price", "total_sell_price"):
        if key not in data:
            continue
        value = data[key]
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            return False
        if not math.isfinite(float(value)) or float(value) < 0:
            return False
    return True
@register("pretty_print_trade_receipt")
def pretty_print_trade_receipt(ctx):
    resp = ctx.state.get("last_rpc")
    if resp is None:
        print("[no response captured]")
        return

    if resp.get("status") != "ok":
        print(f"[Error] Trade failed: {resp.get('error', {}).get('message', 'Unknown error')}")
        return

    if not _trade_receipt_type_matches(resp, "buy"):
        print("[Error] Unexpected trade.buy receipt response type; refresh the port before relying on the settlement.")
        return
    if not _trade_receipt_player_matches(resp, _known_player_id(ctx)):
        print("[Error] Trade receipt did not identify the authenticated player; refresh before relying on the settlement.")
        return
    if not _trade_receipt_display_data_is_actionable(resp):
        print("[Error] Trade receipt omitted authoritative settlement details; refresh before relying on the settlement.")
        return

    data = resp.get("data")
    if not isinstance(data, dict):
        print("[Error] Invalid trade receipt data.")
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
    print(f"  Total Cost: {present_money(total_cost)}")
    print(f"  Credits Remaining: {present_money(credits_remaining)}")
    print(f"\n  Items Purchased:")
    if lines:
        for item in lines:
            commodity = item.get("commodity")
            quantity = item.get("quantity")
            unit_price = item.get("unit_price")
            value = item.get("value")
            print(f"    - {quantity} x {commodity} @ {present_money(unit_price)}/unit = {present_money(value)}")
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
        return

    if not _trade_receipt_type_matches(resp, "sell"):
        print("[Error] Unexpected trade.sell receipt response type; refresh the port before relying on the settlement.")
        return
    if not _trade_receipt_player_matches(resp, _known_player_id(ctx)):
        print("[Error] Trade receipt did not identify the authenticated player; refresh before relying on the settlement.")
        return
    if not _trade_receipt_display_data_is_actionable(resp):
        print("[Error] Trade receipt omitted authoritative settlement details; refresh before relying on the settlement.")
        return

    data = resp.get("data")
    if not isinstance(data, dict):
        print("[Error] Invalid sell receipt data.")
        return

    sector_id = data.get("sector_id")
    port_id = data.get("port_id")
    player_id = data.get("player_id")
    total_credits = data.get("total_cost")
    credits_remaining = data.get("credits_remaining")
    lines = data.get("lines", [])

    print(f"\n--- Trade Receipt (Sell) ---")
    print(f"  Sector ID: {sector_id}")
    print(f"  Port ID:   {port_id}")
    print(f"  Player ID: {player_id}")
    print(f"  Total Credits Gained: {present_money(total_credits)}")
    print(f"  Credits Remaining: {present_money(credits_remaining)}")
    print(f"\n  Items Sold:")
    if lines:
        for item in lines:
            commodity = item.get("commodity")
            quantity = item.get("quantity")
            unit_price = item.get("unit_price")
            value = item.get("value")
            print(f"    - {quantity} x {commodity} @ {present_money(unit_price)}/unit = {present_money(value)}")
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
    if not isinstance(data, dict) or not ("items" in data or "hardware" in data):
        print("[Error] Invalid hardware list data.")
        _pp(resp)
        return

    hardware_items = data.get("items", data.get("hardware", []))
    if hardware_items:
        print("\n--- Available Hardware ---")
        for item in hardware_items:
            code = item.get("code", item.get("item_code"))
            print(f"  - {item.get('name')} ({code}) - Price: {item.get('price')} cr - {item.get('description')}")
    else:
        print("No hardware available at this location.")

@register("hardware_buy_flow")
def hardware_buy_flow(ctx: Context):
    catalogue = ctx.conn.rpc("hardware.list", {})
    ctx.state["last_rpc"] = catalogue
    if not isinstance(catalogue, dict) or catalogue.get("status") != "ok":
        print(f"[Error] Could not load the hardware catalogue: {_planet_error(catalogue)}")
        return
    data = get_data(catalogue)
    items = data.get("items", data.get("hardware", [])) if isinstance(data, dict) else []
    items = [row for row in items if isinstance(row, dict) and (row.get("code") or row.get("item_code"))]
    if not items:
        print("No hardware is available at this dock.")
        return
    print("\n--- Hardware available at this dock ---")
    for index, item in enumerate(items, 1):
        extra_stock = f" · stock {item['stock_quantity']}" if item.get("stock_quantity") is not None else ""
        code = item.get("code", item.get("item_code"))
        print(f"  {index}. {item.get('name', code)} [{code}] · {item.get('price', '?')} CR{extra_stock}")
    selection = input("Hardware number or displayed item code: ").strip().upper()
    try:
        selected_index = int(selection)
        item = items[selected_index - 1] if 1 <= selected_index <= len(items) else None
    except ValueError:
        item = next((row for row in items if str(row.get("code", row.get("item_code", ""))).upper() == selection), None)
    if item is None:
        print("Cancelled: choose a hardware item from the server catalogue.")
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
        "code": str(item.get("code", item.get("item_code"))).upper(),
        "quantity": quantity
    }

    resp = ctx.conn.rpc("hardware.buy", payload)
    ctx.state["last_rpc"] = resp

    if resp.get("status") == "ok":
        receipt = resp.get("data")
        receipt_valid = (
            resp.get("type") == "hardware.purchase_v1"
            and isinstance(receipt, dict)
            and receipt.get("code") == payload["code"]
            and type(receipt.get("quantity")) is int
            and receipt["quantity"] == quantity
            and type(receipt.get("credits_spent")) is int
            and receipt["credits_spent"] >= 0
            and isinstance(receipt.get("ship"), dict)
        )
        if not receipt_valid:
            print("[Warning] Hardware purchase was not confirmed: the server returned an invalid purchase receipt.")
            _hud_rpc(ctx, "player.my_info", {})
            _hud_rpc(ctx, "ship.status", {})
            return
        print(f"Successfully purchased {quantity} units of {payload['code']}.")
        _pp(resp)
        # Refresh credits and ship equipment through the authoritative views.
        my_info_resp = _hud_rpc(ctx, "player.my_info", {})
        if my_info_resp.get("status") == "ok":
            ctx.player_info = get_data(my_info_resp)
        else:
            print("[Warning] Could not refresh player info after hardware purchase.")
        ship_resp = _hud_rpc(ctx, "ship.status", {})
        if ship_resp.get("status") != "ok":
            print("[Warning] Could not refresh ship status after hardware purchase.")
    else:
        print(f"[Error] Hardware purchase failed: {resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(resp)

@register("sweep_mines_flow")
def sweep_mines_flow(ctx: Context):
    """Sweep mines in the confirmed current sector using the server contract."""
    sector_id = ctx.current_sector_id
    if not isinstance(sector_id, int) or sector_id <= 0:
        print("Cannot sweep mines: current sector ID is unknown.")
        return
    response = ctx.conn.rpc("combat.sweep_mines", {"target_sector_id": sector_id})
    if response.get("status") == "ok":
        data = get_data(response)
        print(f"Mine sweep complete: {data.get('mines_removed', 0)} mine asset(s) removed.")
        refresh_sector_and_display(ctx)
    else:
        print(f"[Error] Mine sweep failed: {response.get('error', {}).get('message', 'Unknown error')}")
        _pp(response)

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
            print(f"    Amount: {present_money(item.get('amount'))} | Balance After: {present_money(item.get('balance_after'))}")
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
            balance_display = present_money(item.get("balance"))
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
    balance_display = present_money(balance)

    print("\n--- Corporation Treasury Balance ---")
    print(f"  Current Balance: {balance_display}")
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
            
            amount_display = present_money(txn.get("amount"))
            balance_display = present_money(txn.get("balance"))

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
    print(f"  Total Cost: {present_money(data.get('total_cost'))}")
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
    print(f"  Amount per share: {present_money(data.get('amount_per_share'))}")
    print(f"  Total Payout: {present_money(data.get('total_payout'))}")
    print(f"  Message: {data.get('message')}")
    print("-------------------------")
    print("-------------------------")

@register("pretty_print_news_feed")
def pretty_print_news_feed(ctx):
    resp = ctx.state.get("last_rpc")
    if not resp or resp.get("status") != "ok":
        _pp(resp)
        return
    data = resp.get("data") or {}
    articles = data.get("articles") or []
    if not articles:
        print("(No news available)")
        return
    for art in articles:
        # Format timestamp if possible
        ts_val = art.get("timestamp")
        ts_str = str(ts_val)
        if isinstance(ts_val, (int, float)):
            try:
                import datetime
                dt = datetime.datetime.fromtimestamp(ts_val, tz=datetime.timezone.utc)
                ts_str = dt.strftime("%Y-%m-%d %H:%M:%S UTC")
            except Exception:
                pass
        
        print(f"\n--- {art.get('headline', 'News')} ---")
        print(f"Time: {ts_str}  Category: {art.get('category')}  ID: {art.get('id')}")
        print(f"Body: {art.get('body')}")
    print("-" * 20)

@register("pretty_print_news_marked_read")
def pretty_print_news_marked_read(ctx):
    resp = ctx.state.get("last_rpc")
    if not resp or resp.get("status") != "ok":
        _pp(resp)
        return
    print("All news marked as read.")

@register("pretty_print_trade_routes")
def pretty_print_trade_routes(ctx):
    resp = ctx.state.get("last_rpc")
    if not resp or resp.get("status") != "ok":
        _pp(resp)
        return
    
    data = resp.get("data") or {}
    routes = data.get("routes") or []

    if data.get("pathing_model") or isinstance(data.get("pairs_checked"), int):
        print("\nRoute search:")
        if data.get("pathing_model"):
            print(f"  Pathing model: {data['pathing_model']}")
        if isinstance(data.get("pairs_checked"), int):
            print(f"  Port pairs checked: {data['pairs_checked']}")
        if data.get("truncated") is True:
            print("  Results were limited; increase the result limit to see more.")
    
    if not routes:
        print("\n(No profitable trade routes found in your known database.)")
        return

    print("\n--- Recommended Trade Routes ---")
    print(f"{'Port A':<8} {'Port B':<8} {'Sect A':<8} {'Sect B':<8} {'Hops':<6} {'Dist':<6} {'Type'}")
    print("-" * 65)
    
    for r in routes:
        p_a = r.get("port_a_id")
        p_b = r.get("port_b_id")
        s_a = r.get("sector_a_id")
        s_b = r.get("sector_b_id")
        hops = r.get("hops_between")
        dist = r.get("hops_from_player")
        is_2way = "2-way" if r.get("is_two_way") else "1-way"
        
        print(f"{p_a:<8} {p_b:<8} {s_a:<8} {s_b:<8} {hops:<6} {dist:<6} {is_2way}")
    print("-" * 65)
    print("(Dist is hops from your current location to the nearest port in the route.)\n")

@register("corp_create_flow")
def corp_create_flow(ctx: Context):
    if ctx.state.get("in_corporation"):
        print("You are already in a corporation. You must leave or dissolve it first.")
        return

    corp_name = input("Enter new corporation name: ").strip()
    if not corp_name:
        print("Corporation name cannot be empty. Cancelled.")
        return
    
    resp = ctx.conn.rpc("corp.create", {"name": corp_name})
    ctx.state["last_rpc"] = resp

    if resp.get("status") == "ok":
        print("Corporation created successfully!")
        _pp(resp)
        # Refresh player info to update corporation status
        _update_corp_context(ctx, force=True)
    else:
        print(f"[Error] Failed to create corporation: {resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(resp)

@register("corp_roster_flow")
def corp_roster_flow(ctx: Context):
    """Use the authenticated corporation status to supply the required roster ID."""
    status = ctx.conn.rpc("corp.status", {})
    if status.get("status") != "ok":
        print(f"[Error] Failed to get corporation status: {status.get('error', {}).get('message', 'Unknown error')}")
        _pp(status)
        return
    data = get_data(status)
    corp_id = data.get("corp_id") if isinstance(data, dict) else None
    if not isinstance(corp_id, int) or corp_id <= 0:
        print("[Error] The server did not provide a valid corporation ID.")
        _pp(status)
        return
    resp = ctx.conn.rpc("corp.roster", {"corp_id": corp_id})
    ctx.state["last_rpc"] = resp
    call_handler("pretty_print_corp_roster", ctx)

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
        my_info_resp = _hud_rpc(ctx, "player.my_info", {})
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
        my_info_resp = _hud_rpc(ctx, "player.my_info", {})
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
    status = ctx.conn.rpc("corp.status", {})
    status_data = get_data(status) if status.get("status") == "ok" else {}
    corp_id = status_data.get("corp_id") if isinstance(status_data, dict) else None
    if not isinstance(corp_id, int) or corp_id <= 0:
        print("Unable to confirm your corporation ID; no member action was sent.")
        return
    resp_roster = ctx.conn.rpc("corp.roster", {"corp_id": corp_id})
    if resp_roster.get("status") != "ok":
        print(f"Unable to load the corporation roster: {resp_roster.get('error', {}).get('message', 'Unknown error')}")
        return
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

    amount_str = input("Enter amount to deposit to corporation treasury (whole credits): ").strip()
    if not amount_str:
        print("Cancelled.")
        return
    try:
        amount = parse_credits(amount_str)
    except MoneyError as e:
        print(f"Invalid amount: {e}")
        return
    if amount <= 0:
        print("Amount must be greater than zero.")
        return

    resp = ctx.conn.rpc("corp.deposit", {"amount": amount})
    ctx.state["last_rpc"] = resp

    if resp.get("status") == "ok":
        print(f"Successfully deposited {format_credits(amount)} to corporation treasury.")
        _pp(resp)
        refreshed = ctx.conn.rpc("corp.balance", {})
        if refreshed.get("status") == "ok":
            ctx.state["corp_balance"] = get_data(refreshed)
        else:
            print("Warning: deposit succeeded, but treasury balance could not be refreshed.")
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

    amount_str = input("Enter amount to withdraw from corporation treasury (whole credits): ").strip()
    if not amount_str:
        print("Cancelled.")
        return
    try:
        amount = parse_credits(amount_str)
    except MoneyError as e:
        print(f"Invalid amount: {e}")
        return
    if amount <= 0:
        print("Amount must be greater than zero.")
        return

    resp = ctx.conn.rpc("corp.withdraw", {"amount": amount})
    ctx.state["last_rpc"] = resp

    if resp.get("status") == "ok":
        print(f"Successfully withdrew {format_credits(amount)} from corporation treasury.")
        _pp(resp)
        refreshed = ctx.conn.rpc("corp.balance", {})
        if refreshed.get("status") == "ok":
            ctx.state["corp_balance"] = get_data(refreshed)
        else:
            print("Warning: withdrawal succeeded, but treasury balance could not be refreshed.")
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
        my_info_resp = _hud_rpc(ctx, "player.my_info", {})
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
    status = ctx.conn.rpc("corp.status", {})
    status_data = get_data(status) if status.get("status") == "ok" else {}
    corp_id = status_data.get("corp_id") if isinstance(status_data, dict) else None
    if not isinstance(corp_id, int) or corp_id <= 0:
        print("Unable to confirm your corporation ID; leadership was not changed.")
        return
    resp_roster = ctx.conn.rpc("corp.roster", {"corp_id": corp_id})
    if resp_roster.get("status") != "ok":
        print(f"Unable to load the corporation roster: {resp_roster.get('error', {}).get('message', 'Unknown error')}")
        return
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
        my_info_resp = _hud_rpc(ctx, "player.my_info", {})
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
        par_value = parse_credits(input("Enter par value per share (whole credits): ").strip())
        if par_value <= 0:
            print("Par value must be positive.")
            return
    except (ValueError, MoneyError):
        print("Invalid number. Cancelled.")
        return

    resp = ctx.conn.rpc("equity.ipo_register", {
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

    resp = ctx.conn.rpc("equity.buy", {"equity_id": stock_id, "quantity": quantity})
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

    try:
        amount_per_share = parse_credits(input("Enter amount per share (whole credits): ").strip())
        if amount_per_share <= 0:
            print("Amount per share must be positive.")
            return
    except (ValueError, MoneyError):
        print("Invalid input.")
        return

    resp = ctx.conn.rpc("equity.dividend_set", {"amount_per_share": amount_per_share})
    ctx.state["last_rpc"] = resp

    if resp.get("status") == "ok":
        print("Dividend declared successfully!")
        _pp(resp)
    else:
        err = resp.get("error", {})
        msg = err.get("message") if isinstance(err, dict) else str(err)
        print(f"[Error] Failed to declare dividend: {msg or 'Unknown error'}")
        _pp(resp)

@register("insurance_buy_flow")
def insurance_buy_flow(ctx: Context):
    insurance_unavailable_flow(ctx)

@register("insurance_claim_flow")
def insurance_claim_flow(ctx: Context):
    insurance_unavailable_flow(ctx)


@register("insurance_unavailable_flow")
def insurance_unavailable_flow(ctx: Context):
    print(
        "Insurance is unavailable: the current server handlers return "
        "placeholder responses and do not persist policies or claims. "
        "No request was sent."
    )

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
        # Support default value syntax: <prompt:type:msg[default]>
        m = re.match(r"^<prompt:(int|float|str|bool)(?::([^\]]+))?(?:\[([^\]]+)\])?>$", val)
        if m:
            typ, msg, default = m.group(1), (m.group(2) or "Enter value"), m.group(3)
            raw = input(f"{msg}: ").strip()
            
            if not raw and default is not None:
                raw = default
            
            if not raw and typ != "str":
                return None

            try:
                if typ == "int": return int(raw)
                if typ == "float": return float(raw)
                if typ == "bool":
                    return raw.lower() in ("y", "yes", "true", "1")
                return raw
            except Exception:
                print(f"Invalid {typ}.")
                return None
            
            # Convenience: if prompting for commodity, accept short codes (ORE, ORG, EQU)
            if typ == 'str' and 'commodity' in (msg or '').lower():
                if isinstance(val, str):
                    v = val.strip().lower()
                    if v in ('ore', 'o'):
                        return 'ORE'
                    if v in ('org', 'orgs', 'organ', 'organics', 'organi'):
                        return 'ORG'
                    if v in ('equ', 'equip', 'equipment', 'e'):
                        return 'EQU'
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

# Debug/protocol screens where the HUD would just add noise (per slice
# requirement: never on login/registration/help/debug screens).
_HUD_EXCLUDED_MENUS = {"TESTING", "BULK"}


def terminal_width(stream=None, fallback: int = 80) -> int:
    """Return a stable usable width; redirected output always uses fallback."""
    stream = stream or sys.stdout
    try:
        if not stream.isatty():
            return fallback
        columns = shutil.get_terminal_size(fallback=(fallback, 24)).columns
        return max(20, int(columns))
    except (AttributeError, OSError, ValueError):
        return fallback


def layout_menu_options(labels: list, width: int = 80) -> list:
    """Lay out complete menu labels in one or two columns without truncation."""
    width = max(20, int(width or 80))
    labels = [str(label) for label in labels]
    if not labels:
        return []
    gap = 4
    column_width = max((len(label) for label in labels), default=0)
    if len(labels) < 2 or column_width > (width - gap) // 2:
        return ["  " + label for label in labels]
    lines = []
    for index in range(0, len(labels), 2):
        left = labels[index]
        if index + 1 >= len(labels):
            lines.append("  " + left)
            continue
        right = labels[index + 1]
        lines.append("  " + left.ljust(column_width) + (" " * gap) + right)
    return lines


def layout_menu_lines(menu: dict, visible_labels: list, width: int = 80) -> list:
    """Pure menu line calculation used by render_menu and unit tests."""
    lines = []
    if "sections" not in menu:
        return layout_menu_options(visible_labels, width)
    offset = 0
    for section in menu.get("sections", []):
        if visible_labels and isinstance(visible_labels[0], list):
            labels = visible_labels[offset] if offset < len(visible_labels) else []
            offset += 1
        else:
            labels = visible_labels[offset:offset + len(section.get("options", []))]
            offset += len(section.get("options", []))
        if section.get("name"):
            lines.append(f"[{section['name']}]")
        lines.extend(layout_menu_options(labels, width))
    return lines


def _render_hud(ctx: Context, width: int) -> None:
    if ctx.current_menu in _HUD_EXCLUDED_MENUS:
        return
    for line in hud_module.render_hud_lines(
        ctx.hud, connected=ctx.conn.connected, activity=ctx.activity_count,
        width=width
    ):
        print(line)

def render_menu(ctx: Context):
    menu = ctx.menus.get(ctx.current_menu)
    if not menu:
        print(f"[Error] Menu '{ctx.current_menu}' not found.")
        return
    width = terminal_width()
    _render_hud(ctx, width)
    # run optional on-enter hook for this menu
    try:
        menu_on_enter(ctx, menu)
    except ConnectionError:
        raise
    except Exception:
        pass
    flags = compute_flags(ctx)
    title = menu.get("title", ctx.current_menu)
    # Simple template replacement
    title = title.replace("{{sector_id}}", str(ctx.current_sector_id or "?"))
    port_name = (ctx.last_sector_desc.get("port") or {}).get("name") or "Port"
    title = title.replace("{{port_name}}", str(port_name))
    print("\n" + "\n".join(textwrap.wrap(title, width=width) or [title]))
    if "sections" in menu:
        visible_labels = []
        for sec in menu["sections"]:
            visible_labels.append([_interp_label(opt["label"], ctx) for opt in sec.get("options", [])
                                   if option_visible(opt, flags) and _option_visible_with_ctx(ctx, opt)])
    else:
        visible_labels = [_interp_label(opt["label"], ctx) for opt in menu.get("options", [])
                          if option_visible(opt, flags) and _option_visible_with_ctx(ctx, opt)]
    for line in layout_menu_lines(menu, visible_labels, width):
        print(line)

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
    # Keep keyboard dispatch aligned with the rendered menu.  In particular,
    # NAV-02 transwarp must not be sent while the current sector lacks a
    # server-owned positive ID, even if a stale shortcut is pressed.
    flags = compute_flags(ctx)
    if not option_visible(match, flags) or not _option_visible_with_ctx(ctx, match):
        print("That action is unavailable until the authoritative state is refreshed.")
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
    knowledge = d.get("knowledge") if isinstance(d.get("knowledge"), dict) else {}
    beacon = d.get("beacon")

    print(f"\nYou are in sector {sid}{' — ' + name if name else ''}.")
    # Extract sector numbers from adjacent_sectors format
    adj_nums = []
    for a in adj:
        if isinstance(a, dict):
            adj_nums.append(str(a.get("to_sector", "?")))
        else:
            adj_nums.append(str(a))
    if knowledge.get("adjacent", d.get("adjacent") is not None):
        print(f"Adjacent sectors: {', '.join(adj_nums) if adj_nums else 'none'}")
    else:
        print("Adjacent sectors: not reported")

    if not knowledge.get("ports", port is not None):
        print("Ports here: not reported")
    elif port and type(port.get("present")) is bool and port["present"]:
        pnm = port.get("name") or "Port (details undisclosed)"
        print(f"Ports here: {pnm}")
    elif port and type(port.get("present")) is bool and not port["present"]:
        print("Ports here: none")
    else:
        print("Ports here: not reported")

    if not knowledge.get("ships", d.get("ships") is not None):
        print("Ships here: not reported")
    elif ships:
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

    if not knowledge.get("planets", d.get("planets") is not None):
        print("Planets: not reported")
    elif planets:
        print("Planets: " + ", ".join(pl.get("name") or "Unnamed" for pl in planets))
    else:
        print("Planets: none")

    print("Beacon: " + (beacon if beacon else "none"))

@register("refresh_sector_and_display")
def refresh_sector_and_display(ctx: Context):
    """Fetches the latest sector data, updates context, and then redisplays the sector."""
    sector_id = _authoritative_positive_integer_id(ctx.current_sector_id)
    if sector_id is None:
        print("Cannot refresh sector: current sector ID is not authoritative.")
        return False

    try:
        resp = ctx.conn.rpc("move.describe_sector", {"sector_id": sector_id})
    except (ConnectionError, OSError) as exc:
        # A reconciliation read that loses its reply is not evidence of a new
        # location or visibility state. Keep the last confirmed cache and let
        # the normal reconnect refresh establish authoritative state.
        print(f"[Error] Sector refresh outcome is uncertain after connection loss: {exc}")
        print("Reconnect before relying on sector information; the confirmed sector was not changed.")
        return False
    if isinstance(resp, dict) and resp.get("status") == "ok":
        if not _response_type_matches(resp, "sector.info"):
            print("[Error] Sector refresh returned an unexpected response type; keeping the last confirmed state.")
            return False
        try:
            refreshed = normalize_sector(get_data(resp))
        except (TypeError, ValueError, AttributeError):
            # A success envelope is not sufficient to replace the cache.  A
            # schema-mismatched reconciliation response must fail closed just
            # like a refused read, otherwise visibility workflows can crash or
            # expose a partially decoded sector after a stale/uncertain scan.
            print("[Error] Sector refresh returned malformed data; keeping the last confirmed state.")
            return False
        refreshed_id = refreshed.get("id")
        expected_id = sector_id
        # A refresh is the recovery path after a refusal or uncertain read, so
        # it must be stricter than ordinary rendering: only a positive JSON
        # integer matching the confirmed location can replace the cache.
        if (type(refreshed_id) is not int or refreshed_id <= 0
                or type(expected_id) is not int or expected_id <= 0
                or refreshed_id != expected_id):
            if type(refreshed_id) is not int or refreshed_id <= 0:
                reason = "no authoritative sector ID"
            else:
                reason = f"sector {refreshed_id} instead of confirmed sector {expected_id}"
            print(f"[Error] Sector refresh returned {reason}; keeping the last confirmed state.")
            return False
        ctx.last_sector_desc = refreshed
        redisplay_sector(ctx)
        return True
    else:
        error = resp.get("error") if isinstance(resp, dict) else None
        message = error.get("message", "Unknown error") if isinstance(error, dict) else (
            "Malformed server response" if not isinstance(resp, dict) else "Unknown error"
        )
        print(f"[Error] Failed to refresh sector: {message}")
        # A refresh is commonly requested after a refused or ambiguous read.
        # Do not dump the complete upstream envelope here: it is not an
        # authoritative sector record and may contain undisclosed future data.
        return False


def _refresh_after_scan_refusal(ctx: Context) -> None:
    """Re-read only the current sector after a scan refusal.

    A refused scan is never merged into the cache, but the local display may
    still be stale. The normal current-sector workflow is authoritative and
    leaves the last confirmed cache intact if that read also fails.
    """
    try:
        refresh_sector_and_display(ctx)
    except (ConnectionError, OSError) as exc:
        # A refused scan followed by a lost reconciliation reply still does
        # not establish any new location or visibility state. Keep the last
        # confirmed cache intact and leave a reconnect-safe explanation for
        # the normal interactive workflow.
        print(f"[Error] Sector refresh could not be confirmed after connection loss: {exc}")
        print("Reconnect before relying on visibility information; the confirmed sector was not changed.")

@register("recall_fighters_flow")
def recall_fighters_flow(ctx: Context):
    if ctx.current_sector_id is None:
        print("Cannot recall fighters: current sector ID is unknown.")
        return

    # 1. List deployed fighters
    # The authoritative list schema is an empty object. The server scopes the
    # result to the authenticated player; sector_id is display context only.
    list_resp = ctx.conn.rpc("deploy.fighters.list", {})
    if list_resp.get("status") != "ok":
        print(f"[Error] Failed to list deployed fighters: {list_resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(list_resp)
        return

    fighters_data = get_data(list_resp)
    
    fighters_list = _authoritative_deployment_entries(fighters_data)
    
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
    # The authoritative list schema is an empty object. The server scopes the
    # result to the authenticated player; sector_id is display context only.
    list_resp = ctx.conn.rpc("deploy.mines.list", {})
    if list_resp.get("status") != "ok":
        print(f"[Error] Failed to list deployed mines: {list_resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(list_resp)
        return

    mines_data = get_data(list_resp)
    mines_list = _authoritative_deployment_entries(mines_data)
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

    # The authoritative list schema is an empty object; the local sector is
    # only used for presentation and recall context.
    list_resp = ctx.conn.rpc("deploy.fighters.list", {})
    if list_resp.get("status") != "ok":
        print(f"[Error] Failed to list deployed fighters: {list_resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(list_resp)
        return

    fighters_data = get_data(list_resp)
    fighters_list = _authoritative_deployment_entries(fighters_data)

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

    # The authoritative list schema is an empty object; the local sector is
    # only used for presentation and recall context.
    list_resp = ctx.conn.rpc("deploy.mines.list", {})
    if list_resp.get("status") != "ok":
        print(f"[Error] Failed to list deployed mines: {list_resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(list_resp)
        return

    mines_data = get_data(list_resp)
    mines_list = _authoritative_deployment_entries(mines_data)

    if not mines_list:
        print("No mines deployed in this sector.")
        return

    print("\n--- Deployed Mines ---")
    for m in mines_list:
        print(f"  Asset ID: {m.get('asset_id')}, Quantity: {m.get('count')}, Offensive Setting: {m.get('offense_mode')}")
    print("----------------------")


def _authoritative_deployment_entries(data: Any) -> list[dict]:
    """Keep only server rows with a usable, non-coerced asset identity."""
    entries = data.get("entries") if isinstance(data, dict) else None
    if not isinstance(entries, list):
        return []
    return [
        entry for entry in entries
        if isinstance(entry, dict)
        and type(entry.get("asset_id")) is int
        and entry["asset_id"] > 0
    ]

def _render_scan_card(scan: dict) -> str:
    """
    Pretty-print the server's move.scan response.
    Robust against partial/misaligned payloads (missing sector_id, flat counts, etc).
    """
    try:
        # Basic shape checks
        if not isinstance(scan, dict):
            # A scan presenter must never stringify an unrecognized server
            # value.  Apart from leaking undisclosed fields, doing so would
            # make a transport/schema failure look like usable visibility
            # data to callers that invoke the presenter directly.
            return "\n── Fast Scan: unavailable (the response was not a scan envelope)"

        # Refusal/error envelopes are not visibility records.  Show only the
        # server's refusal message and never render a payload attached to the
        # envelope; generic result presenters can call this helper without
        # first routing through the command-specific refusal branch.
        if scan.get("status") in ("error", "refused"):
            err = scan.get("error") or {}
            code = err.get("code")
            msg = err.get("message") or "Unknown error"
            label = "ERROR" if scan.get("status") == "error" else "REFUSED"
            return f"\n── Fast Scan: {label} {code} — {msg}"

        if scan.get("status") != "ok":
            return "\n── Fast Scan: unavailable (the response was not successful)"

        d = scan.get("data") or {}
        if not isinstance(d, dict):
            # A successful envelope with a non-object payload is not an
            # authoritative empty scan.  Treat it as malformed so generic
            # callers cannot display or cache a schema-mismatched visibility
            # result as if the server had disclosed no entities.
            return "\n── Fast Scan: unavailable (the response data was not a scan object)"

        # Core fields (tolerant fallbacks).  The presenter is also called by
        # generic result paths, so enforce the same authoritative-ID boundary
        # here as the command flow does before it merges or displays a scan.
        # `sector_id` is authoritative when present. A truthiness fallback
        # would let malformed primary metadata (for example 0) redirect the
        # scan to a conflicting legacy `id` alias.
        sid = d.get("sector_id") if "sector_id" in d else d.get("id")
        if _authoritative_positive_integer_id(sid) is None:
            return "\n── Fast Scan: unavailable (the response did not include an authoritative sector ID)"
        raw_name = d.get("name")
        if raw_name is not None and not isinstance(raw_name, str):
            raise TypeError("sector name is not a JSON string")
        name = raw_name or "Unknown"

        sec = d.get("security") or {}
        # Do not coerce schema-mismatched values (notably "false" -> True).
        # Unknown visibility flags stay unknown instead of becoming a client
        # assertion about the server's disclosure state.
        fed = _authoritative_boolean(sec.get("fedspace"))
        safe = _authoritative_boolean(sec.get("safe_zone"))
        locked = _authoritative_boolean(sec.get("combat_locked"))

        adjacent_known = "adjacent" in d or "adjacent_sectors" in d
        raw_adj = d.get("adjacent", d.get("adjacent_sectors", []))
        # Scan metadata is displayable only when each destination is a real
        # server ID.  Do not let bool/float/string values become actionable
        # destinations through presentation or the cache merge below.
        adj = ([value for value in raw_adj
                if _authoritative_positive_integer_id(value) is not None]
               if isinstance(raw_adj, list) else [])

        # A non-empty array is visibility evidence only when every row has
        # the authoritative object shape.  Do not turn malformed rows into a
        # false "Port: present" disclosure; the server owns the visibility
        # decision and this presenter must fail closed.
        ports_are_objects = (
            isinstance(d.get("ports"), list)
            and all(isinstance(row, dict) for row in d["ports"])
        )
        if ports_are_objects:
            port_status = "present" if d["ports"] else "none"
        elif (isinstance(d.get("port"), dict)
              and _authoritative_boolean(d["port"].get("present")) is not None):
            port_status = "present" if d["port"]["present"] else "none"
        else:
            port_status = "not reported"

        # Counts: prefer nested object; else reconstruct from flat fields
        counts = d.get("counts") if isinstance(d.get("counts"), dict) else {}
        def count_value(key, aliases=()):
            for candidate in (key, *aliases):
                value = counts.get(candidate, d.get(candidate))
                # Entity counts are authoritative JSON cardinalities. Keep
                # Python aligned with the Godot presenter: negative integers
                # are schema-invalid and must not be shown as server facts.
                if (isinstance(value, int) and not isinstance(value, bool)
                        and value >= 0):
                    return str(value)
            return "unknown"

        beacon = d.get("beacon")
        # normalise beacon: allow string or explicit null
        beacon_str = beacon if isinstance(beacon, str) and beacon.strip() else None

        # Render
        lines = []
        lines.append(f"\n── Fast Scan: Sector {sid if sid is not None else '∅'} — {name}")
        lines.append(f"   Adjacent: {', '.join(map(str, adj)) if adj else ('none' if adjacent_known else 'not reported')}")
        lines.append(f"   Security: fedspace={_display_authoritative_value(fed)}, safe_zone={_display_authoritative_value(safe)}, combat_locked={_display_authoritative_value(locked)}")
        lines.append(f"   Port: {port_status} (class/details not disclosed by scan)")
        lines.append("   Counts: ships=%s, planets=%s, mines=%s, fighters=%s" % (
            count_value("ships", ("ship_count", "ships_count")),
            count_value("planets", ("planet_count", "planets_count")),
            count_value("mines"), count_value("fighters")))
        lines.append(f"   Beacon: {beacon_str if beacon_str else 'none'}")
        return "\n".join(lines)

    except Exception:
        # Never fall back to rendering the complete server envelope: a future
        # field or an unexpected value may be outside this client's disclosure
        # contract.  Keep the result truthful and let the caller reconcile the
        # authoritative sector before another command relies on cached state.
        return "\n── Fast Scan: unavailable (the response contained undisclosed data)"


def _authoritative_boolean(value: Any) -> Optional[bool]:
    """Return a boolean only when the server supplied a JSON boolean."""
    return value if type(value) is bool else None


def _display_authoritative_value(value: Any) -> str:
    """Render absent or schema-mismatched scalar fields as unavailable."""
    # Keep this helper fail-closed even when called outside _render_scan_card.
    # Truthiness is not the V3 boolean contract: the JSON string "false" and
    # integer 1 must never become an authoritative visibility assertion.
    if type(value) is not bool:
        return "unknown"
    return "true" if value else "false"


@register("scan_sector")
def scan_sector(ctx: "Context"):
    # A lost visibility reply is not evidence that the scan succeeded. Keep
    # the last confirmed cache intact and require the normal reconnect path to
    # establish fresh location/visibility state before another scan.
    if _authoritative_positive_integer_id(ctx.current_sector_id) is None:
        print("Scan unavailable: the current sector has no authoritative ID; refresh before scanning.")
        return
    try:
        resp = ctx.conn.rpc("move.scan", {})
    except (ConnectionError, OSError) as exc:
        ctx.state["last_rpc"] = None
        print(f"Scan outcome is uncertain after connection loss: {exc}")
        print("Reconnect before relying on scan information; the confirmed sector was not changed.")
        return
    ctx.state["last_rpc"] = resp

    # Pretty print the scan
    if not isinstance(resp, dict) or resp.get("status") != "ok":
        error = (resp.get("error") or {}) if isinstance(resp, dict) else {}
        print(f"Scan refused: {error.get('message', 'The server did not return scan information.')}")
        _refresh_after_scan_refusal(ctx)
        return
    if not _response_type_matches(resp, "move.scan"):
        print("Scan response had an unexpected type; cached state was not changed.")
        _refresh_after_scan_refusal(ctx)
        return

    scan_data = resp.get("data") if isinstance(resp.get("data"), dict) else None
    confirmed_sector_id = ctx.current_sector_id
    reported_sector_id = (
        scan_data.get("sector_id", scan_data.get("id"))
        if scan_data is not None else None
    )
    # A scan is usable only when it can be correlated to the authoritative
    # current location.  Do not render or merge a success-shaped payload that
    # omits its sector ID (or reports a different sector).
    if (type(confirmed_sector_id) is not int or confirmed_sector_id <= 0
            or type(reported_sector_id) is not int or reported_sector_id <= 0
            or reported_sector_id != confirmed_sector_id):
        if type(reported_sector_id) is not int or reported_sector_id <= 0:
            reason = "did not include an authoritative sector ID"
        elif type(confirmed_sector_id) is not int or confirmed_sector_id <= 0:
            reason = "cannot be correlated without a confirmed current sector"
        else:
            reason = "identified sector %s, but the confirmed current sector is %s" % (
                reported_sector_id, confirmed_sector_id
            )
        print(
            "Scan response %s; cached sector state was not changed." % reason
        )
        _refresh_after_scan_refusal(ctx)
        return

    try:
        rendered_scan = _render_scan_card(resp)
        print(rendered_scan)
        if rendered_scan.startswith("\n── Fast Scan: unavailable"):
            # The presenter deliberately refused an unexpected field/value.
            # Do not merge even the apparently safe subset of that envelope:
            # the response was not renderable under the client disclosure
            # contract. Reconcile the current sector before another command
            # relies on the cached view.
            _refresh_after_scan_refusal(ctx)
            return
    except Exception:
        # A presenter failure is not permission to expose the complete server
        # envelope.  Keep the result truthful and let the authoritative sector
        # refresh below reconcile the cached location before another command
        # relies on it.
        print("\n── Fast Scan: unavailable (the response contained undisclosed data)")
        _refresh_after_scan_refusal(ctx)
        return

    # Optional: lightly merge a few fields into our cached view WITHOUT
    # wiping richer objects from describe_sector (ships/planets lists, etc.)
    try:
        d = scan_data or {}
        if not isinstance(d, dict):
            return
        cur = ctx.last_sector_desc or {}
        merged = dict(cur)

        if isinstance(d.get("sector_id"), int):
            merged["id"] = d["sector_id"]
        if isinstance(d.get("name"), str):
            merged["name"] = d["name"]
        raw_adjacent = d.get("adjacent", d.get("adjacent_sectors"))
        if isinstance(raw_adjacent, list):
            merged["adjacent"] = [
                value for value in raw_adjacent
                if _authoritative_positive_integer_id(value) is not None
            ]

        # Represent scan’s port presence as a minimal port object
        if "ports" in d and isinstance(d["ports"], list):
            ports = d["ports"]
            # A non-empty array is not enough to establish port presence: a
            # schema-invalid row must not become visibility evidence.  Keep
            # the confirmed cache unchanged until every returned row is an
            # object, just as the renderer does for other malformed fields.
            if all(isinstance(row, dict) for row in ports):
                port = ports[0] if ports else {}
                merged["port"] = {"present": bool(ports)}
                # An explicit ``id`` is authoritative when present.  Do not
                # let a malformed explicit ID fall through to a conflicting
                # legacy alias and turn schema-mismatched visibility data into
                # an actionable port target.
                port_id_value = port.get("id") if "id" in port else port.get("port_id")
                port_id = _authoritative_positive_integer_id(port_id_value)
                if port_id is not None:
                    merged["port"]["id"] = port_id
                if isinstance(port.get("name"), str):
                    merged["port"]["name"] = port["name"]
                merged.setdefault("knowledge", {})["ports"] = True
        elif (isinstance(d.get("port"), dict)
              and type(d["port"].get("present")) is bool):
            # Keep schema-mismatched visibility metadata out of the confirmed
            # sector cache. In particular, bool("false") would incorrectly
            # turn a textual refusal/placeholder into an actionable port.
            merged["port"] = {"present": d["port"]["present"]}
            merged.setdefault("knowledge", {})["ports"] = True

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
    if ctx.current_menu == "COMPUTER":
        print("--- Ship's Computer Help ---")
        print("S: Ship information     P: Planet information")
        print("F: Scan adjacent sectors   D: Sector density scan")
        print("R: Rename ship          H: Hardware catalogue")
        print("J: Jettison cargo       T: Genesis torpedo")
        print("K: Player rankings        ?: This help")
        print("Q: Return to the main menu")
        return

    d = ctx.last_sector_desc or {}
    print("--- Help ---")
    print("M: Move   D: Re-display sector   P: Port and trade")
    print("L: Land on a planet   S: Sector services   F: Operations")
    print("C: Ship's computer   G: Comms and events   N: News")
    print("O: Corporation   U: User settings   H: Help")

@register("rankings_flow")
def rankings_flow(ctx: Context):
    """Display the server-authoritative player rankings."""
    response = ctx.conn.rpc("player.rankings", {"by": "net_worth", "limit": 10})
    ctx.state["last_rpc"] = response
    if not isinstance(response, dict) or response.get("status") != "ok":
        print("Player rankings refused or failed:")
        _pp(response)
        return
    data = get_data(response)
    rows = data.get("rankings", []) if isinstance(data, dict) else []
    if not isinstance(rows, list) or not rows:
        print("No player rankings are available yet.")
        return
    rendered = 0
    for row in rows:
        if not isinstance(row, dict):
            continue
        player_id = row.get("player_id")
        name = row.get("name")
        experience = row.get("experience")
        alignment = row.get("alignment")
        if (
            type(player_id) is not int or player_id <= 0
            or not isinstance(name, str) or not name.strip()
            or type(experience) is not int
            or type(alignment) is not int
        ):
            continue
        rendered += 1
        index = rendered
        print(f"{index}. {name} · XP {experience} · alignment {alignment}")
    if rendered == 0:
        print("No player rankings are available yet.")

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

    my_count = get_my_beacon_count(ctx.conn, ctx)
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
    # Server sometimes returns "" or {} or dict of commodities, or Protocol v3 list of objects
    if cargo in ("", None):
        return "none"
    if isinstance(cargo, list):
        # Protocol v3: [{"commodity": "ore", "quantity": 10}, ...]
        parts = []
        for item in cargo:
            if isinstance(item, dict):
                c = item.get("commodity")
                q = item.get("quantity")
                if c and q is not None:
                    parts.append(f"{c}: {_fmt_int(q)}")
        return ", ".join(parts) if parts else "none"
    if isinstance(cargo, dict) and cargo:
        parts = []
        for k, v in cargo.items():
            parts.append(f"{k}: {_fmt_int(v)}")
        return ", ".join(parts)
    return str(cargo)

def render_ship_card(ship: dict) -> str:
    """Return a pretty printable card for a ship dict from ship.inspect or ship.status."""
    sid   = ship.get("id") or ship.get("ship_id") or "?"
    name  = ship.get("name") or ship.get("ship_name") or "Unnamed"
    
    # Type handling
    stype = (ship.get("type") or {}).get("name") or ship.get("ship_type")
    if not stype:
        tid = ship.get("type_id")
        stype = f"Type {tid}" if tid is not None else "?"
        
    sector_id = (ship.get("location") or {}).get("sector_id") or ship.get("sector_id") or "?"
    
    owner = (ship.get("owner") or {}).get("name") if isinstance(ship.get("owner"), dict) else ship.get("owner")
    owner = owner or "unknown"
    
    flags = _bool_flags(ship.get("flags") or {})
    
    # Defence handling (nested or flat)
    defence = ship.get("defence") or {}
    shields = defence.get("shields") if isinstance(defence, dict) else None
    if shields is None:
        shields = ship.get("shields") or 0
        
    fighters = defence.get("fighters") if isinstance(defence, dict) else None
    if fighters is None:
        fighters = ship.get("fighters") or 0
    hull = ship.get("hull")
    hull_text = _fmt_int(hull) if type(hull) is int and 0 <= hull <= 100 else "—"

    # Holds and Cargo handling
    holds_val = ship.get("holds")
    cargo_data = ship.get("cargo")
    
    holds_used = 0
    if isinstance(cargo_data, list):
        for item in cargo_data:
            if isinstance(item, dict):
                holds_used += item.get("quantity") or 0
    elif isinstance(cargo_data, dict):
        holds_used = sum(cargo_data.values())
    
    if isinstance(holds_val, dict):
        holds_total = holds_val.get("total") or 0
    elif isinstance(holds_val, int):
        holds_total = holds_val # This is max_holds
    else:
        holds_total = 0
    
    holds_line  = f"{_fmt_int(holds_used)} used / {_fmt_int(holds_total)} total"
    cargo_str = _render_cargo(cargo_data)

    lines = []
    lines.append(f"\n┌─ Ship: {name}  (ID {sid})")
    lines.append(f"│   Type: {stype}")
    lines.append(f"│   Sector: {sector_id}    Owner: {owner}")
    lines.append(f"│   Flags: {flags}")
    lines.append(f"│   Defence: Shields {_fmt_int(shields)}, Fighters {_fmt_int(fighters)}")
    lines.append(f"│   Hull:   {hull_text}/100")
    lines.append(f"│   Holds:   {holds_line}")
    lines.append(f"│   Cargo:   {cargo_str}")
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
    if data is None:
        print("[no data in response]")
        return

    ships = data.get("ships") or []
    if not isinstance(ships, list) or not ships:
        # Some servers return a single 'ship' object (e.g. ship.status)
        ship = data.get("ship")
        if isinstance(ship, dict):
            # Inject context if missing
            if "location" not in ship and "location" in data:
                ship["location"] = data["location"]
            if "owner" not in ship and "name" in data:
                ship["owner"] = data["name"]
            print(render_ship_card(ship))
            return
        raise ValueError("No ships found in response.")
    for ship in ships:
        print(render_ship_card(ship))
    return



@register("transfer_cargo_flow")
def transfer_cargo_flow(ctx: Context):
    """Transfer cargo from the active ship to an owned ship in this sector."""
    ships = (ctx.last_sector_desc or {}).get("ships") or []
    active_id = get_my_ship_id(ctx.conn, ctx)
    if type(active_id) is not int or active_id <= 0:
        # The server derives the source ship from the authenticated active
        # ship.  Without a confirmed identity, sending a mutating request would
        # make a stale/ambiguous client snapshot actionable.  Refresh the
        # authoritative active-ship contract before allowing another attempt.
        print("Cargo transfer unavailable: the current ship identity is not confirmed by the server.")
        try:
            _refresh_ship_after_move(ctx)
        except (ConnectionError, OSError, TypeError, ValueError, AttributeError) as exc:
            print(f"Warning: current ship identity could not be refreshed: {exc}")
        return
    targets = []
    for ship in ships:
        if not isinstance(ship, dict):
            continue
        ship_id = ship.get("id", ship.get("ship_id"))
        if type(ship_id) is int and ship_id > 0 and ship_id != active_id:
            targets.append((ship_id, ship.get("name") or ship.get("ship_name") or "Unnamed"))
    if not targets:
        print("No eligible target ships are visible in this sector.")
        return
    for index, (ship_id, name) in enumerate(targets, 1):
        print(f" {index}) {name} (ship {ship_id})")
    try:
        selected = int(input("Target ship number (Enter to cancel): ").strip())
        commodity_id = int(input("Commodity ID: ").strip())
        quantity = int(input("Quantity: ").strip())
    except ValueError:
        print("Cargo transfer cancelled or invalid number.")
        return
    if not (1 <= selected <= len(targets)) or commodity_id <= 0 or quantity <= 0:
        print("Cargo transfer requires a listed target and positive commodity and quantity values.")
        return
    target_id = targets[selected - 1][0]
    if input(f"Transfer {quantity} of commodity {commodity_id} to ship {target_id}? [y/N]: ").strip().lower() != "y":
        print("Cargo transfer cancelled.")
        return
    try:
        response = ctx.conn.rpc("ship.transfer_cargo", {
            "commodity_id": commodity_id, "quantity": quantity, "target_ship_id": target_id,
        })
    except (ConnectionError, OSError) as exc:
        # The mutation may have committed before the transport failed.  Do not
        # claim refusal or success; reconcile the authoritative active ship and
        # current-sector roster before another cargo action is offered.
        ctx.state["last_rpc"] = None
        print(f"Cargo transfer outcome is uncertain after connection loss: {exc}")
        try:
            _refresh_ship_after_move(ctx)
            sector = ctx.conn.rpc("move.describe_sector", {})
            if sector.get("status") == "ok":
                ctx.last_sector_desc = normalize_sector(get_data(sector))
        except (ConnectionError, OSError, TypeError, ValueError, AttributeError) as refresh_exc:
            print(f"Warning: cargo transfer reconciliation was incomplete: {refresh_exc}")
        return
    ctx.state["last_rpc"] = response
    if response.get("status") != "ok":
        print("Cargo transfer refused or failed:")
        _pp(response)
        return
    if not _valid_ship_transfer_cargo_receipt(
        response, active_id, target_id, commodity_id, quantity
    ):
        # The server may have committed the transaction even if the response
        # envelope was malformed.  Do not claim success or merge receipt data;
        # reconcile the active ship and sector from authoritative reads.
        print("Cargo transfer was not displayed because the server receipt did not identify the requested ships, commodity, quantity, and resulting balances.")
        _refresh_ship_after_move(ctx)
        sector = ctx.conn.rpc("move.describe_sector", {})
        if sector.get("status") == "ok":
            try:
                ctx.last_sector_desc = normalize_sector(get_data(sector))
            except (TypeError, ValueError, AttributeError):
                print("Warning: transfer receipt was invalid, and the sector could not be refreshed.")
        else:
            print("Warning: transfer receipt was invalid, and the sector could not be refreshed.")
        return
    print("Cargo transfer confirmed by server.")
    refreshed = _refresh_ship_after_move(ctx)
    if refreshed.get("status") != "ok":
        print("Warning: transfer succeeded, but active ship status could not be refreshed.")
    sector = ctx.conn.rpc("move.describe_sector", {})
    if sector.get("status") == "ok":
        try:
            ctx.last_sector_desc = normalize_sector(get_data(sector))
        except (TypeError, ValueError, AttributeError):
            print("Warning: transfer succeeded, but the sector could not be refreshed.")
    else:
        print("Warning: transfer succeeded, but the sector could not be refreshed.")


def _valid_ship_transfer_cargo_receipt(
    response: Any,
    active_ship_id: Optional[int],
    target_ship_id: int,
    commodity_id: int,
    quantity: int,
) -> bool:
    """Accept only the documented authoritative cargo-transfer receipt."""
    if not isinstance(response, dict):
        return False
    response_type = response.get("type")
    if response_type != "ship.cargo_transferred_v1":
        return False
    data = get_data(response)
    if not isinstance(data, dict):
        return False

    def positive_int(value: Any) -> bool:
        return type(value) is int and value > 0

    if not positive_int(active_ship_id) or not positive_int(target_ship_id):
        return False
    if data.get("source_ship_id") != active_ship_id:
        return False
    if data.get("target_ship_id") != target_ship_id:
        return False
    if data.get("commodity_id") != commodity_id or data.get("quantity") != quantity:
        return False
    return (
        type(data.get("source_remaining")) is int
        and data["source_remaining"] >= 0
        and type(data.get("target_quantity")) is int
        and data["target_quantity"] >= 0
    )


@register("ship_upgrade_flow")
def ship_upgrade_flow(ctx: Context):
    """Request an equipment upgrade and reconcile the active ship."""
    upgrade_type = input("Upgrade type (Enter to cancel): ").strip()
    if not upgrade_type:
        print("Ship upgrade cancelled.")
        return
    if input(f"Apply ship upgrade '{upgrade_type}'? [y/N]: ").strip().lower() != "y":
        print("Ship upgrade cancelled.")
        return
    response = ctx.conn.rpc("ship.upgrade", {"upgrade_type": upgrade_type})
    ctx.state["last_rpc"] = response
    if response.get("status") != "ok":
        print("Ship upgrade refused or failed:")
        _pp(response)
        return
    print("Ship upgrade confirmed by server.")
    refreshed = _hud_rpc(ctx, "ship.status", {})
    if refreshed.get("status") != "ok":
        print("Warning: upgrade succeeded, but ship status could not be refreshed.")


@register("ship_list_flow")
def ship_list_flow(ctx: Context, response: Optional[Dict[str, Any]] = None):
    """Display the server-authoritative primary-owned ship roster."""
    if response is None:
        try:
            response = ctx.conn.rpc("ship.list", {})
        except (ConnectionError, OSError) as exc:
            ctx.state["last_rpc"] = None
            print(f"Ship roster could not be confirmed after connection loss: {exc}")
            _refresh_ship_roster_state(ctx)
            return
        ctx.state["last_rpc"] = response
    if not isinstance(response, dict):
        print("Ship roster unavailable: the server returned an invalid response.")
        _refresh_ship_roster_state(ctx, include_roster=True)
        return
    if response.get("status") != "ok":
        print(f"Ship roster unavailable: {response.get('error', {}).get('message', 'The server did not return a ship roster.')}")
        # A refusal is not evidence that the cached roster is still current.
        # Reconcile the authoritative active-ship relationship before the
        # player can make another lifecycle choice from stale UI state.
        _refresh_ship_roster_state(ctx)
        return
    # `ship.list.response` is the one authoritative roster contract.  Do not
    # accept an omitted/empty type as a legacy variant: a success envelope
    # without its schema identity could be data from another command and must
    # not become an ownership/lifecycle choice in the client.
    if response.get("type") != "ship.list.response":
        print("Ship roster unavailable: the server returned an unexpected response type.")
        _refresh_ship_roster_state(ctx)
        return
    data = response.get("data")
    ships = data.get("ships") if isinstance(data, dict) else None
    if not isinstance(ships, list):
        print("Ship roster unavailable: the server returned no authoritative ship list.")
        _refresh_ship_roster_state(ctx)
        return
    roster_ids = set()
    roster_valid = True
    rows = []
    for ship in ships:
        if not isinstance(ship, dict):
            roster_valid = False
            continue
        ship_id = ship.get("ship_id")
        if type(ship_id) is not int or ship_id <= 0:
            roster_valid = False
            continue
        if ship_id in roster_ids:
            roster_valid = False
        roster_ids.add(ship_id)
        active = ship.get("active")
        # The server schema defines active as a JSON boolean.  Do not turn a
        # malformed value (for example, the string "true") into a usable
        # lifecycle choice merely because another row is active.
        if type(active) is not bool:
            roster_valid = False
        active_text = "active" if active is True else "inactive" if active is False else "status unavailable"
        rows.append(
            f"{ship_id}: {ship.get('name', 'Unnamed')} · sector {ship.get('sector_id', 'unavailable')} · {active_text}"
        )
    if not roster_valid:
        print("No authoritative ships were returned: the server returned an invalid or ambiguous ownership or active-ship state.")
        _refresh_ship_roster_state(ctx)
        return
    print("\n--- Your ships (server-authoritative) ---")
    if rows:
        print("\n".join(rows))
    else:
        print("No ships are currently owned.")


@register("ship_roster_inspect_flow")
def ship_roster_inspect_flow(ctx: Context):
    """Inspect one owned ship selected from the authoritative roster."""
    try:
        response = ctx.conn.rpc("ship.list", {})
    except (ConnectionError, OSError) as exc:
        print(f"Ship roster could not be confirmed after connection loss: {exc}")
        return
    ctx.state["last_rpc"] = response
    if not isinstance(response, dict) or response.get("status") != "ok":
        print("Ship roster unavailable: the server did not return an authoritative roster.")
        return
    if response.get("type") != "ship.list.response":
        print("Ship roster unavailable: the server returned an unexpected response type.")
        return
    data = response.get("data")
    ships = data.get("ships") if isinstance(data, dict) else None
    if not isinstance(ships, list):
        print("Ship roster unavailable: the server returned no authoritative ship list.")
        return

    valid_ships = []
    seen_ids = set()
    for ship in ships:
        if not isinstance(ship, dict):
            print("Ship roster unavailable: the server returned an invalid ship row.")
            return
        ship_id = ship.get("ship_id")
        if type(ship_id) is not int or ship_id <= 0 or ship_id in seen_ids:
            print("Ship roster unavailable: the server returned ambiguous ship identity.")
            return
        if type(ship.get("active")) is not bool:
            print("Ship roster unavailable: the server returned an invalid active-ship flag.")
            return
        seen_ids.add(ship_id)
        valid_ships.append(ship)

    if not valid_ships:
        print("No ships are currently owned.")
        return
    print("\n--- Inspect an owned ship ---")
    for index, ship in enumerate(valid_ships, 1):
        state = "active" if ship["active"] else "inactive"
        print(f"{index}: {ship.get('name', 'Unnamed')} (ship {ship['ship_id']}, {state})")
    try:
        choice = input("Ship number to inspect (Enter to cancel): ").strip()
        if not choice:
            print("Ship inspection cancelled.")
            return
        index = int(choice)
    except ValueError:
        print("Ship inspection cancelled: enter a roster number.")
        return
    if not 1 <= index <= len(valid_ships):
        print("Ship inspection cancelled: that roster number is unavailable.")
        return

    ship_id = valid_ships[index - 1]["ship_id"]
    try:
        response = ctx.conn.rpc("ship.inspect", {"ship_id": ship_id})
    except (ConnectionError, OSError) as exc:
        # The roster selection is only a snapshot.  If the connection drops
        # before inspection, do not leave the selected ship actionable from
        # stale UI state or let the transport exception abort the client;
        # reconcile the authoritative active-ship record first.
        print(f"Ship inspection outcome is uncertain after connection loss: {exc}")
        _refresh_ship_roster_state(ctx)
        return
    ctx.state["last_rpc"] = response
    if not isinstance(response, dict) or response.get("status") != "ok":
        print("Ship inspection unavailable: the server refused the selected ship.")
        # A refusal may mean the roster became stale between the list and the
        # inspection.  Reconcile the active ship before another lifecycle
        # action can rely on the old selection.
        _refresh_ship_roster_state(ctx)
        return
    if response.get("type") != "ship.inspect":
        print("Ship inspection unavailable: the server returned an unexpected response type.")
        _refresh_ship_roster_state(ctx)
        return
    inspected = get_data(response)
    if isinstance(inspected, dict) and isinstance(inspected.get("ship"), dict):
        inspected = inspected["ship"]
    if not isinstance(inspected, dict):
        print("Ship inspection unavailable: the server returned no ship record.")
        _refresh_ship_roster_state(ctx)
        return
    inspected_id = _authoritative_positive_integer_id(inspected.get("id", inspected.get("ship_id")))
    if inspected_id != ship_id:
        print("Ship inspection unavailable: the server returned a different ship ID.")
        _refresh_ship_roster_state(ctx)
        return
    print("Server-authoritative ship inspection:")
    print(render_ship_card(inspected))


@register("ship_transfer_flow")
def ship_transfer_flow(ctx: Context):
    """Transfer one owned ship using the canonical typed server receipt."""
    try:
        roster = ctx.conn.rpc("ship.list", {})
    except (ConnectionError, OSError) as exc:
        print(f"Ship transfer unavailable after connection loss: {exc}")
        return
    ctx.state["last_rpc"] = roster
    if not _ship_list_has_authoritative_ids(roster):
        print("Ship transfer unavailable: the server did not return an authoritative owned-ship roster.")
        _refresh_ship_roster_state(ctx)
        return
    ships = roster["data"]["ships"]
    if not ships:
        print("Ship transfer unavailable: no ships are currently owned.")
        return
    print("\n--- Transfer an owned ship ---")
    for index, ship in enumerate(ships, 1):
        active = "active" if ship["active"] else "inactive"
        print(f"{index}: {ship.get('name', 'Unnamed')} (ship {ship['ship_id']}, {active})")
    try:
        choice = input("Ship number or ID to transfer (Enter to cancel): ").strip()
        if not choice:
            print("Ship transfer cancelled.")
            return
        selection = int(choice)
        target_player_id = int(input("Recipient player ID: ").strip())
    except ValueError:
        print("Ship transfer cancelled or invalid player/ship ID.")
        return
    if target_player_id <= 0:
        print("Ship transfer requires a valid owned-ship selection and positive recipient ID.")
        return
    # The roster's ship_id is the authoritative lifecycle identity.  Accept an
    # exact ID when the player supplies one (useful when a roster is refreshed
    # between display and input), while retaining the visible 1-based row
    # number for normal menu use.  Both paths resolve to the same server-owned
    # ID; no client-generated or cached identity is sent.
    selected_ship = next((ship for ship in ships if ship["ship_id"] == selection), None)
    if selected_ship is None and 1 <= selection <= len(ships):
        selected_ship = ships[selection - 1]
    if selected_ship is None:
        print("Ship transfer requires a valid owned-ship selection and positive recipient ID.")
        return
    ship_id = selected_ship["ship_id"]
    if input(f"Transfer ship {ship_id} to player {target_player_id}? [y/N]: ").strip().lower() != "y":
        print("Ship transfer cancelled.")
        return
    try:
        response = ctx.conn.rpc("ship.transfer", {"ship_id": ship_id, "target_player_id": target_player_id})
    except (ConnectionError, OSError) as exc:
        # A transport failure after a mutation request leaves the outcome
        # ambiguous. Never present a local success or let the exception abort
        # the client; reconcile the ownership roster before another lifecycle
        # action can use stale state.
        ctx.state["last_rpc"] = None
        print(f"Ship transfer outcome is uncertain after connection loss: {exc}")
        _refresh_ship_roster_state(ctx, include_roster=True)
        return
    ctx.state["last_rpc"] = response
    data = response.get("data") if isinstance(response, dict) else None
    if not isinstance(response, dict) or response.get("status") != "ok":
        print("Ship transfer refused or failed:")
        _pp(response)
        _refresh_ship_roster_state(ctx, include_roster=True)
        return
    if not _ship_transfer_receipt_matches(
        response,
        ship_id=ship_id,
        target_player_id=target_player_id,
        owner_player_id=_known_player_id(ctx),
    ):
        print("Ship transfer was not confirmed: the server returned different ownership IDs.")
        _refresh_ship_roster_state(ctx, include_roster=True)
        return
    print(f"Ship transfer confirmed by server: ship {ship_id} -> player {target_player_id}.")
    _refresh_after_ship_transfer(ctx, outcome_label="Confirmed ship transfer")
    ship_list_flow(ctx)


def _refresh_after_ship_transfer(
    ctx: Context, *, outcome_label: str = "Ship transfer outcome"
) -> None:
    """Refresh all client views affected by an ownership change or ambiguity."""
    try:
        player = ctx.conn.rpc("player.my_info", {})
        player_record = _authoritative_hud_record(player, "player")
        if _landing_player_refresh_is_current(ctx, player, require_identity=True):
            ctx.player_info = get_data(player)
            ctx.hud = hud_module.apply_response(ctx.hud, "player.my_info", player)
        else:
            player_record = None
            print(f"Warning: {outcome_label} could not confirm the logged-in player during refresh.")

        ship = ctx.conn.rpc("ship.status", {})
        expected_ship_id = None
        if isinstance(player_record, dict):
            expected_ship_id = _authoritative_positive_integer_id(player_record.get("ship_id"))
        if expected_ship_id is None:
            expected_ship_id = ctx.hud.ship_id
        ship_record = _authoritative_hud_record(ship, "ship")
        returned_ship_id = (
            _authoritative_positive_integer_id(ship_record.get("id", ship_record.get("ship_id")))
            if isinstance(ship_record, dict) else None
        )
        if (_refresh_response_type_matches(ship, "ship.status")
                and returned_ship_id is not None
                and (expected_ship_id is None or returned_ship_id == expected_ship_id)):
            ctx.hud = hud_module.apply_response(ctx.hud, "ship.status", ship)
        else:
            print(f"Warning: {outcome_label} could not refresh the confirmed active ship.")

        sector = ctx.conn.rpc("move.describe_sector", {})
        if sector.get("status") == "ok" and _refresh_response_type_matches(sector, "sector.info"):
            refreshed_sector = normalize_sector(get_data(sector))
            confirmed_sector_id = _authoritative_positive_integer_id(
                (ctx.last_sector_desc or {}).get("id")
            )
            refreshed_sector_id = _authoritative_positive_integer_id(
                refreshed_sector.get("id")
            )
            if (refreshed_sector_id is not None
                    and (confirmed_sector_id is None
                         or refreshed_sector_id == confirmed_sector_id)):
                ctx.last_sector_desc = refreshed_sector
            else:
                print(f"Warning: {outcome_label} returned a different sector during refresh.")
        else:
            print(f"Warning: {outcome_label} could not refresh sector state.")
    except (ConnectionError, OSError, TypeError, ValueError, AttributeError) as exc:
        print(f"Warning: {outcome_label} authoritative refresh was incomplete: {exc}")


def _refresh_ship_roster_state(ctx: Context, *, include_roster: bool = False) -> None:
    """Reconcile the active ship after an unusable roster response.

    A malformed or empty roster is not evidence that the player has no ship;
    it may be a stale/schema-mismatched response.  Refresh only the typed
    active-ship record and leave the existing HUD unchanged if that read cannot
    be confirmed.
    """
    try:
        if include_roster:
            roster = ctx.conn.rpc("ship.list", {})
            if (isinstance(roster, dict)
                    and roster.get("status") == "ok"
                    and _ship_list_has_authoritative_ids(roster)):
                ship_list_flow(ctx, response=roster)
            else:
                print("Warning: ship ownership roster could not be confirmed after the ambiguous transfer response.")
            # A transfer request is a mutation of ownership, not merely a
            # roster read.  Even when the follow-up roster is usable, the
            # outcome may have changed the session's active ship or player
            # relationship.  Reconcile those authoritative domains before
            # allowing another lifecycle action; never leave a stale HUD
            # snapshot after an ambiguous/refused transfer.
            _refresh_after_ship_transfer(ctx)
            return
        # Reconcile through the typed ship.status contract.  A delayed or
        # mismatched success envelope must not hydrate the HUD merely because
        # it contains a ship-shaped payload; the roster was already rejected
        # and this read is the only authoritative recovery path.
        refreshed = ctx.conn.rpc("ship.status", {})
        if not _refresh_response_type_matches(refreshed, "ship.status"):
            print("Warning: ship roster was unusable and the active ship refresh had an unexpected response type.")
            return
        ctx.hud = hud_module.apply_response(ctx.hud, "ship.status", refreshed)
    except (ConnectionError, OSError) as exc:
        print(f"Warning: ship roster could not be confirmed after connection loss: {exc}")
        return
    if not isinstance(refreshed, dict) or refreshed.get("status") != "ok":
        print("Warning: ship roster was unusable and the active ship could not be refreshed.")


def _ship_list_has_authoritative_ids(response: Any) -> bool:
    """Validate the owned-roster identity fields before using a refresh."""
    # A typed payload is not authoritative when the envelope refused the
    # request.  Do not let a refusal carrying diagnostic/stale rows become a
    # lifecycle selection for transfer or another ownership mutation.
    if (not isinstance(response, dict)
            or response.get("status") != "ok"
            or response.get("type") != "ship.list.response"):
        return False
    data = response.get("data")
    ships = data.get("ships") if isinstance(data, dict) else None
    if not isinstance(ships, list):
        return False
    seen = set()
    for ship in ships:
        if not isinstance(ship, dict):
            return False
        ship_id = ship.get("ship_id")
        if type(ship_id) is not int or ship_id <= 0 or ship_id in seen:
            return False
        if type(ship.get("active")) is not bool:
            return False
        seen.add(ship_id)
    return True


def _transfer_selected_ship_flow(ctx: Context, ship_id: int) -> None:
    """Transfer the selected visible ship through the canonical receipt flow."""
    if _authoritative_positive_integer_id(ship_id) is None:
        print("Ship transfer unavailable: the selected ship has no authoritative server ID.")
        return
    try:
        target_player_id = int(input("Recipient player ID: ").strip())
    except ValueError:
        print("Ship transfer cancelled or invalid recipient player ID.")
        return
    if target_player_id <= 0:
        print("Ship transfer requires a positive recipient player ID.")
        return
    try:
        response = ctx.conn.rpc("ship.transfer", {
            "ship_id": int(ship_id),
            "target_player_id": target_player_id,
        })
    except (ConnectionError, OSError) as exc:
        ctx.state["last_rpc"] = None
        print(f"Ship transfer outcome is uncertain after connection loss: {exc}")
        _refresh_ship_roster_state(ctx, include_roster=True)
        return
    ctx.state["last_rpc"] = response
    data = response.get("data") if isinstance(response, dict) else None
    if not isinstance(response, dict) or response.get("status") != "ok":
        print("Ship transfer refused or failed:")
        _pp(response)
        _refresh_ship_roster_state(ctx, include_roster=True)
        return
    if not _ship_transfer_receipt_matches(
        response,
        ship_id=int(ship_id),
        target_player_id=target_player_id,
        owner_player_id=_known_player_id(ctx),
    ):
        print("Ship transfer was not confirmed: the server returned different ownership IDs.")
        _refresh_ship_roster_state(ctx, include_roster=True)
        return
    print(f"Ship transfer confirmed by server: ship {ship_id} -> player {target_player_id}.")
    _refresh_after_ship_transfer(ctx, outcome_label="Confirmed ship transfer")
    ship_list_flow(ctx)


@register("enter_ship_menu")
def enter_ship_menu(ctx: Context):
    d = ctx.last_sector_desc or {}
    ships = d.get("ships") or []
    my_ship_id = get_my_ship_id(ctx.conn, ctx)
    my_name = get_my_player_name(ctx.conn, ctx)

    # Build list of boardable ships
    boardable = []
    for s in ships:
        if has_boardable_ship([s], my_ship_id=my_ship_id, my_name=my_name):
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
        sid = _authoritative_ship_row_id(s)
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
            else:
                print("Invalid selection; no ship selected.")
                return
        except ValueError:
            print("Invalid selection; no ship selected.")
            return

    while True:
        target, nm, tp, ship_id = current_target(target_idx)
        authoritative_ship_id = _authoritative_positive_integer_id(ship_id)
        print("\n--- Enter Ship Menu ---")
        print(f" Target: #{target_idx+1} \"{nm}\" [{tp}] (ship_id={ship_id})")
        print(" (I) Show visible sector data")
        print(" (C) Claim")
        print(" (V) Sell this ship")
        print(" (T) Transfer this ship")
        #print(" (R) Rename / Re-register")
        if ctx.state.get("debug"):
            print(" (P) Set Primary [debug: ship.set_primary not implemented]")
        print(" (S) Select a different ship")
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
                else:
                    print("Invalid selection.")
            except ValueError:
                print("Invalid selection.")
            continue

        if cmd == "i":
            inspect_id = authoritative_ship_id
            if inspect_id <= 0:
                print("Ship inspection unavailable: the selected ship has no authoritative server ID.")
                continue

            response = ctx.conn.rpc("ship.inspect", {"ship_id": inspect_id})
            ctx.state["last_rpc"] = response
            if not isinstance(response, dict) or response.get("status") != "ok":
                print(f"Ship inspection unavailable: {_planet_error(response)}")
                continue
            if response.get("type") != "ship.inspect":
                # A success-shaped envelope from another command is not a
                # compatible response variant.  Do not render it as selected
                # ship details or merge it into the cached sector state.
                print("Ship inspection unavailable: the server returned an unexpected response type.")
                continue

            inspected = get_data(response)
            if isinstance(inspected, dict) and isinstance(inspected.get("ship"), dict):
                inspected = inspected["ship"]
            if not isinstance(inspected, dict):
                print("Ship inspection unavailable: the server returned no ship record.")
                continue
            inspected_id = _authoritative_positive_integer_id(
                inspected.get("id", inspected.get("ship_id"))
            )
            if inspected_id != inspect_id:
                print("Ship inspection unavailable: the server returned a different ship ID.")
                continue
            print("Server-authoritative ship inspection:")
            print(render_ship_card(inspected))
            continue

        if cmd not in ("c", "v", "t") and not (cmd == "p" and ctx.state.get("debug")):
            print("Invalid command. Please try again.")
            continue

        # Confirm current target before any mutating action.
        confirm = input(f'Operate on #{target_idx+1} "{nm}" [{tp}] (ship_id={ship_id})? (y/N): ').strip().lower()
        if confirm != "y":
            print("Cancelled.")
            continue

        elif cmd == "c":
            if authoritative_ship_id <= 0:
                print("Ship claim unavailable: the selected ship has no authoritative server ID.")
                continue
            # The server decides whether the selected ship can be claimed.
            payload = {"ship_id": authoritative_ship_id}
            resp = ctx.conn.rpc("ship.claim", payload)
            print(json.dumps(resp, ensure_ascii=False, indent=2))

            if resp.get("status") == "ok":
                # Tow state belongs to the previous active ship, not the session.
                ctx.state.pop("towing_ship_id", None)
                player = _hud_rpc(ctx, "player.my_info", {})
                if player.get("status") == "ok" and isinstance(player.get("data"), dict):
                    ctx.player_info = get_data(player)
                else:
                    print("[Warning] Could not refresh player info after claim; displayed player data may be stale.")
                ship = _hud_rpc(ctx, "ship.status", {})
                if ship.get("status") != "ok":
                    print("[Warning] Could not refresh ship status after claim; displayed ship data may be stale.")
                new = ctx.conn.rpc("move.describe_sector", {"sector_id": d.get("id")})
                if new.get("status") == "ok" and isinstance(new.get("data"), dict):
                    ctx.last_sector_desc = normalize_sector(get_data(new))
                else:
                    print("[Warning] Could not refresh sector after claim; displayed sector may be stale.")
                return
            else:
                # Nicer errors for common cases
                err = (resp.get("error") or {}).get("message") or "Claim failed"
                print(f"⚠ {err}")

        elif cmd == "v":
            if authoritative_ship_id <= 0:
                print("Ship sale unavailable: the selected ship has no authoritative server ID.")
                continue
            shipyard_sell_flow(ctx, selected_ship_id=authoritative_ship_id)

        elif cmd == "t":
            if authoritative_ship_id <= 0:
                print("Ship transfer unavailable: the selected ship has no authoritative server ID.")
                continue
            _transfer_selected_ship_flow(ctx, authoritative_ship_id)


                
        elif cmd == "p" and ctx.state.get("debug"):
            resp = ctx.conn.rpc("ship.set_primary", {"ship_id": ship_id})
            print(json.dumps(resp, ensure_ascii=False, indent=2))

        else:
            print("Invalid command. Please try again.")



# ---------------------------
# Tow
# ---------------------------
@register("tow_flow")
def tow_flow(ctx: Context):
    d = ctx.last_sector_desc or {}
    my_ship_id = get_my_ship_id(ctx.conn, ctx)
    targets = _tow_targets(d.get("ships") or [], my_ship_id=my_ship_id)

    # Reconcile towing through the documented read endpoint. Sector contents
    # identify possible targets, but do not prove the current tow relationship.
    towing_id = ctx.state.get("towing_ship_id")
    towing_status_reported = False
    try:
        status_resp = ctx.conn.rpc("ship.tow.status", {})
        if isinstance(status_resp, dict) and status_resp.get("status") == "ok":
            status_data = get_data(status_resp)
            if isinstance(status_data, dict) and "towing_ship_id" in status_data:
                value = status_data.get("towing_ship_id")
                # Null is the server's explicit "not towing" value.  Other
                # malformed IDs (including JSON booleans) are not evidence
                # that the beam is free, so keep mutation gated until a
                # later authoritative status read is usable.
                if value is None:
                    towing_id = None
                    towing_status_reported = True
                    ctx.state.pop("towing_ship_id", None)
                else:
                    towing_id = _authoritative_positive_integer_id(value)
                    towing_status_reported = towing_id is not None
                    if towing_status_reported:
                        ctx.state["towing_ship_id"] = towing_id
                    else:
                        ctx.state.pop("towing_ship_id", None)
    except (ConnectionError, OSError):
        raise
    except Exception:
        pass

    if towing_id and towing_status_reported:
        print(f"Currently towing ship {towing_id} (reported by ship.tow.status).")
    elif towing_id:
        print(f"Last confirmed tow response: towing ship {towing_id} (this session).")
    else:
        print("No active tow is reported." if towing_status_reported else "Tow status could not be confirmed; successful tow actions are tracked for this session.")

    if targets:
        print("Ships in this sector:")
        for ship in targets:
            sid = _authoritative_positive_integer_id(ship.get("id"))
            if sid is None:
                sid = _authoritative_positive_integer_id(ship.get("ship_id"))
            name = ship.get("name") or ship.get("ship_name") or "Unnamed ship"
            ship_type = ship.get("ship_type") or ship.get("type") or "type unknown"
            owner = ship.get("owner")
            if isinstance(owner, dict):
                owner = owner.get("name") or owner.get("username") or "owner unknown"
            print(f"  {sid}: {name} [{ship_type}], owner={owner or 'unknown'}")
    else:
        print("No other ships with usable IDs are listed in this sector.")

    print("Enter a listed ship ID to engage the tow beam, R to release, or Enter to cancel.")
    choice = input("Tow action: ").strip()
    if not choice:
        print("Tow action cancelled.")
        return

    if choice.lower() == "r":
        payload = {}
        action = "release the tow beam"
    else:
        if not towing_status_reported:
            print("Cannot engage a tow until the server confirms the current tow status. Try again.")
            return
        try:
            target_id = int(choice)
        except ValueError:
            print("Enter a listed ship ID, R, or Enter to cancel.")
            return
        selected = next((ship for ship in targets
                         if (_authoritative_positive_integer_id(ship.get("id"))
                             or _authoritative_positive_integer_id(ship.get("ship_id"))) == target_id), None)
        if selected is None:
            print("That ship is not listed in the current sector.")
            return
        if towing_id == target_id:
            print("That ship is already being towed. Enter R to release the tow beam.")
            return
        payload = {"target_ship_id": target_id}
        action = f"tow ship {target_id}"

    response = ctx.conn.rpc("ship.tow", payload)
    ctx.state["last_rpc"] = response
    # Tow entry can carry the same authoritative sector-entry hazard report as
    # movement. Display only fields supplied by this command and reconcile
    # before retaining any local tow relationship after a terminal outcome.
    tow_response_type = response.get("type") if isinstance(response, dict) else None
    tow_status = response.get("status") if isinstance(response, dict) else None
    # A successful tow has exactly one authoritative response schema per
    # action.  An omitted/empty type is not a compact success variant: using
    # it here could display hazards or update the local tow relationship from
    # an unrelated success envelope.  Error/refusal envelopes may carry the
    # shared movement hazard report without a success response type.
    tow_response_is_typed = (
        isinstance(response, dict)
        and (
            (tow_status == "ok" and tow_response_type in ("ship.tow.engaged", "ship.tow.disengaged"))
            or tow_status in ("error", "refused")
        )
    )
    tow_hazards = present_move_hazards(response) if tow_response_is_typed else []
    for hazard in tow_hazards:
        print(hazard)
    if tow_response_is_typed and _move_response_is_terminal(response):
        _safe_move_error_refresh(ctx)
        return
    if not isinstance(response, dict) or response.get("status") != "ok":
        error = (response.get("error") or {}) if isinstance(response, dict) else {}
        print(f"[Error] Could not {action}: {error.get('message') or 'The server refused the request.'}")
        if tow_response_is_typed and tow_hazards:
            _safe_move_error_refresh(ctx)
        return

    if tow_response_type not in ("ship.tow.engaged", "ship.tow.disengaged"):
        # A success-shaped envelope without the canonical tow result cannot
        # establish whether the relationship changed. Reconcile before the
        # next action instead of presenting a generic completion message.
        print("Tow response had an unexpected type; refreshing confirmed tow, ship, and sector state.")
        _safe_move_error_refresh(ctx)
        return

    data = get_data(response)
    response_type = str(response.get("type") or "")
    result_text = data.get("status") if isinstance(data, dict) else None
    if response_type.endswith(".engaged"):
        engaged_id = _authoritative_positive_integer_id(data.get("towee_ship_id"))
        if engaged_id is None:
            # The request target is not proof of the committed relationship.
            # Keep the local state unchanged until the server returns its
            # authoritative towed-ship ID.
            print("Tow engagement was not confirmed with an authoritative towed ship ID; refresh before acting on the tow.")
        else:
            ctx.state["towing_ship_id"] = engaged_id
            print(f"Tow beam engaged with ship {engaged_id}.")
    elif response_type.endswith(".disengaged"):
        released_id = data.get("towee_ship_id") or towing_id
        ctx.state.pop("towing_ship_id", None)
        print(f"Tow beam released from ship {released_id or '(ID not returned)'}.")
    elif result_text:
        print(str(result_text))
    else:
        print("Tow command completed.")
    if tow_response_is_typed and tow_hazards:
        _safe_move_refresh(ctx, expected_sector_id=None)

# ---------------------------
# MOVE flows: adjacency-aware move, warp, intercept
# ---------------------------
# NOTE: move_to_adjacent/warp_flow were previously defined twice in this file;
# the second definitions here silently overrode the safer ones above (Python
# dict registration = last-wins) and always called move.describe_sector
# regardless of the response status. Removed; see the single implementations
# above, which route through _perform_warp().

def _trade_rows(ctx, direction):
    port = ctx.state.get("port_inventory") or {}
    key = "buys" if direction == "sell" else "sells"
    rows = []
    for row in port.get("commodities", []):
        if not isinstance(row, dict):
            continue
        # The cache can outlive a reconnect or be populated by an older
        # response shape.  Commodity identity is an authoritative protocol
        # value, not display text: do not let a missing, non-string, empty, or
        # whitespace-padded value cross the trade command boundary.
        commodity = row.get("commodity")
        if (not isinstance(commodity, str)
                or not commodity
                or commodity != commodity.strip()):
            continue
        # An omitted direction is an older/partial disclosure; leave that
        # case to the authoritative quote. An explicit non-boolean value is
        # malformed server metadata and must never become an executable trade
        # action (Godot applies the same boundary in PortWorkflow.gd).
        if key in row and not isinstance(row[key], bool):
            continue
        # An explicit direction flag is authoritative permission.  Only a
        # boolean true enables the action; an explicit false or malformed
        # value must stay out of the command boundary.  An omitted flag is
        # still unknown and is left for the server quote to decide.
        direction_allowed = row.get(key)
        if direction_allowed is None or direction_allowed is True:
            rows.append(row)
    return rows


def _trade_confirmation(prompt, default_no=True):
    answer = input(prompt + " [y/N]: ").strip().lower()
    return answer in ("y", "yes")


def _trade_error(resp, action):
    error = (resp.get("error") or {}) if isinstance(resp, dict) else {}
    print(f"[Error] {action} refused: {error.get('message') or 'The server refused the trade.'}")


def _present_trade_receipt(resp, direction):
    if not isinstance(resp, dict) or resp.get("status") != "ok":
        _trade_error(resp or {}, direction.capitalize())
        return
    data = resp.get("data") or {}
    if not isinstance(data, dict):
        print("[Error] The server returned an invalid trade receipt.")
        return
    verb = "Bought" if direction == "buy" else "Sold"
    print(f"--- Trade Receipt ({verb}) ---")
    total = data.get("total_cost")
    fees = data.get("fees")
    if direction == "buy":
        print(f"Total charged (including fees): {present_money(total)}")
    else:
        print(f"Net credits received (after fees): {present_money(total)}")
    if fees is not None:
        print(f"Fees: {present_money(fees)}")
    if data.get("credits_remaining") is not None:
        print(f"Credits remaining: {present_money(data['credits_remaining'])}")
    for line in data.get("lines") or []:
        if isinstance(line, dict):
            print(f"  {line.get('quantity', '?')} x {line.get('commodity', '?')}"
                  f" @ {present_money(line.get('unit_price'))}/unit")


def _trade_receipt_type_matches(response: dict, direction: str) -> bool:
    """Require the canonical receipt schema for the requested trade direction."""
    if not isinstance(response, dict):
        return False
    return response.get("type") == f"trade.{direction}_receipt_v1"


def _trade_quote_type_matches(response: dict) -> bool:
    """Require the canonical quote schema before a trade can be committed."""
    if not isinstance(response, dict):
        return False
    return response.get("type") == "trade.quote"


def _trade_receipt_lines_match(response: dict, request: dict) -> bool:
    """Reject a receipt whose returned line items contradict the request.

    The V3 receipt contract includes settlement lines.  A missing or empty
    collection is therefore not a compact success variant: it cannot prove
    that the authoritative settlement matches the requested commodity and
    quantity.
    """
    data = response.get("data") if isinstance(response, dict) else None
    lines = data.get("lines") if isinstance(data, dict) else None
    if not isinstance(lines, list) or not lines or len(request.get("items", [])) != 1:
        return False
    requested = request["items"][0]
    total = 0
    for line in lines:
        if not isinstance(line, dict) or line.get("commodity") != requested.get("commodity"):
            return False
        quantity = line.get("quantity")
        if type(quantity) is not int or quantity <= 0:
            return False
        total += quantity
    return total == requested.get("quantity")


def _trade_receipt_player_matches(response: dict, expected_player_id) -> bool:
    """Require the canonical receipt to identify this authenticated player."""
    if not isinstance(response, dict):
        return False
    data = response.get("data")
    if not isinstance(data, dict) or type(data.get("player_id")) is not int:
        return False
    if data["player_id"] <= 0:
        return False
    return expected_player_id is not None and data["player_id"] == expected_player_id


def _trade_receipt_display_data_is_actionable(response: dict) -> bool:
    """Require authoritative settlement identity and lines before display."""
    data = response.get("data") if isinstance(response, dict) else None
    if not isinstance(data, dict):
        return False
    if (_authoritative_positive_integer_id(data.get("port_id")) is None
            or _authoritative_positive_integer_id(data.get("sector_id")) is None):
        return False
    lines = data.get("lines")
    if not isinstance(lines, list) or not lines:
        return False
    for line in lines:
        if not isinstance(line, dict):
            return False
        commodity = line.get("commodity")
        quantity = line.get("quantity")
        if (not isinstance(commodity, str) or not commodity
                or commodity != commodity.strip()
                or type(quantity) is not int or quantity <= 0):
            return False
    return True


def _invalidate_trade_port_context(ctx: Context, message: str) -> None:
    """Make an uncertain trade unable to reuse a stale port snapshot.

    A lost settlement reply does not establish whether the server committed the
    trade.  The cached stock and fixed-direction metadata are therefore no
    longer safe to present as an actionable trade surface; the next action must
    begin with an authoritative port refresh.
    """
    ctx.state.pop("port_inventory", None)
    ctx.state["trade_refresh_error"] = message
    if isinstance(getattr(ctx, "last_sector_desc", None), dict):
        port = ctx.last_sector_desc.get("port")
        if isinstance(port, dict):
            port.pop("commodities", None)


def _refresh_trade_views(ctx: Context, port_id: int) -> bool:
    """Refresh all trade-affected views without turning a lost refresh into a crash.

    A trade response is authoritative for whether the server accepted the
    command, but the follow-up player/ship/port reads are still required to
    reconcile balances, cargo, and stock.  If the connection drops between
    those reads, retain the response for diagnostics and explicitly leave the
    client in an unconfirmed-refresh state; never synthesize local state.
    """
    refresh_identity = {"player": False, "ship": False}

    def refresh_hud_view(command: str, expected_type: str) -> dict:
        """Apply a trade refresh only when its response is for that view.

        Trade settlement is followed by three independent reads.  A delayed
        success envelope from another command must not overwrite the credits
        or cargo HUD merely because it also says ``status: ok``.
        """
        response = ctx.conn.rpc(command, {})
        response_is_current = (
            isinstance(response, dict)
            and response.get("status") == "ok"
            and _response_type_matches(response, expected_type)
        )
        if response_is_current and command == "player.my_info":
            # A typed success is not enough to reconcile a settlement.  The
            # player record must still identify this authenticated session;
            # otherwise a delayed or cross-account response could replace the
            # HUD credits after a real trade.
            response_is_current = _landing_player_refresh_is_current(
                ctx, response, require_identity=True
            )
        elif response_is_current and command == "ship.status":
            record = _authoritative_hud_record(response, "ship")
            actual_id = record.get("id", record.get("ship_id")) if record else None
            expected_id = ctx.hud.ship_id
            response_is_current = (
                type(actual_id) is int and actual_id > 0
                and (expected_id is None or actual_id == expected_id)
            )
        identity_key = "player" if command == "player.my_info" else "ship" if command == "ship.status" else ""
        if identity_key:
            refresh_identity[identity_key] = response_is_current
        if response_is_current:
            ctx.hud = hud_module.apply_response(ctx.hud, command, response)
        return response

    try:
        player_response = refresh_hud_view("player.my_info", "player.info")
        ship_response = refresh_hud_view("ship.status", "ship.status")
        port_response = _refresh_port_info(ctx, _trade_port_refresh_request(ctx, port_id))
    except (ConnectionError, OSError) as exc:
        _invalidate_trade_port_context(
            ctx, str(exc) or "connection lost during trade refresh"
        )
        print("Trade response received, but authoritative balance, cargo, or port stock could not be refreshed.")
        print("Reconnect before attempting another trade or relying on the displayed state.")
        return False
    # A successful trade receipt does not make a refused, cross-location, or
    # malformed port read authoritative.  The refresh helper deliberately
    # clears the cached inventory in that case; propagate that incomplete
    # reconciliation instead of presenting the trade as fully refreshed.
    # Balance/cargo reads are equally authoritative.  Previously a refused
    # player or ship refresh could be hidden by a successful port read, leaving
    # the client to claim that the trade views were reconciled while credits or
    # cargo were still stale.
    player_refresh_ok = (
        isinstance(player_response, dict)
        and player_response.get("status") == "ok"
        and _response_type_matches(player_response, "player.info")
        and refresh_identity["player"]
    )
    ship_refresh_ok = (
        isinstance(ship_response, dict)
        and ship_response.get("status") == "ok"
        and _response_type_matches(ship_response, "ship.status")
        and refresh_identity["ship"]
    )
    port_refresh_ok = (
        isinstance(port_response, dict)
        and port_response.get("status") == "ok"
        and isinstance(ctx.state.get("port_inventory"), dict)
        and ctx.state["port_inventory"].get("id") == port_id
    )
    if not player_refresh_ok or not ship_refresh_ok or not port_refresh_ok:
        failed_domains = []
        if not player_refresh_ok:
            failed_domains.append("player balance")
        if not ship_refresh_ok:
            failed_domains.append("ship cargo")
        if not port_refresh_ok:
            failed_domains.append("port stock")
        failure = ", ".join(failed_domains) or "trade state"
        # A successful port read cannot make the dock actionable when the
        # player or ship refresh failed.  Credits and cargo are still stale,
        # so discard the entire trade context before the next mutation rather
        # than leaving a server-valid-looking stock snapshot available.
        _invalidate_trade_port_context(
            ctx, f"authoritative {failure} refresh failed"
        )
        ctx.state["trade_refresh_error"] = f"authoritative {failure} refresh failed"
        print(f"Trade response received, but authoritative {failure} could not be refreshed.")
        print("Refresh the current port before relying on the displayed inventory.")
        return False
    ctx.state.pop("trade_refresh_error", None)
    return True


def _known_player_id(ctx: Context):
    """Return the session's server-issued player ID when it is known."""
    value = ctx.state.get("player_id")
    if type(value) is int and value > 0:
        return value
    player = ctx.player_info.get("player") if isinstance(ctx.player_info, dict) else None
    value = player.get("id") if isinstance(player, dict) else None
    return value if type(value) is int and value > 0 else None


def _ship_transfer_receipt_matches(
    response: Any, *, ship_id: int, target_player_id: int, owner_player_id: Optional[int]
) -> bool:
    """Accept only the canonical, typed receipt for this ownership change.

    Ship ownership is server state.  A generic success envelope or a receipt
    with coercible IDs must never make the client claim that a transfer took
    effect; the caller will refresh the authoritative roster instead.
    """
    if not isinstance(response, dict) or response.get("status") != "ok":
        return False
    if response.get("type") != "ship.transfer.response":
        return False
    data = response.get("data")
    if not isinstance(data, dict):
        return False
    returned_ship_id = _authoritative_positive_integer_id(data.get("ship_id"))
    returned_from_player_id = _authoritative_positive_integer_id(data.get("from_player_id"))
    returned_target_player_id = _authoritative_positive_integer_id(data.get("target_player_id"))
    requested_ship_id = _authoritative_positive_integer_id(ship_id)
    requested_target_player_id = _authoritative_positive_integer_id(target_player_id)
    return (
        requested_ship_id is not None
        and requested_target_player_id is not None
        and owner_player_id is not None
        and returned_ship_id == requested_ship_id
        and returned_from_player_id == owner_player_id
        and returned_target_player_id == requested_target_player_id
    )


def dock_trade_flow(ctx: Context, direction: str):
    """Shared, quoted buy/sell flow for the current normalized port."""
    if direction not in ("buy", "sell"):
        return
    port = ctx.state.get("port_inventory") or {}
    if not port.get("commodities"):
        reason = ctx.state.get("port_info_error")
        print(f"Port inventory unavailable · {reason or 'inspect a port in your current sector first.'}")
        return
    rows = _trade_rows(ctx, direction)
    if not rows:
        print(f"This port has nothing available to {direction}.")
        return
    print(f"\n--- {direction.capitalize()} ---")
    for index, row in enumerate(rows, 1):
        print(f"  {index}. {row['commodity']} (stock {row.get('available', '?')}/"
              f"{row.get('max_quantity', '?')}, base ~{present_money(row.get('base_price'))})")
    try:
        selected = int(input("Commodity number: ").strip())
        row = rows[selected - 1]
    except (ValueError, IndexError):
        print("Cancelled: choose a displayed commodity number.")
        return
    try:
        quantity = int(input(f"Quantity of {row['commodity']}: ").strip())
    except ValueError:
        print("Invalid quantity.")
        return
    if quantity <= 0:
        print("Quantity must be positive.")
        return
    account_choice = input(
        "Trade account [S]aved preference / [P]etty cash / [B]ank (default S): "
    ).strip().lower()
    account = None
    account_name = "saved server preference"
    if account_choice in ("", "s", "saved", "preference", "server"):
        # Omitting account is the published way to let the server apply the
        # saved player.set_trade_account_preference value. The empty response
        # must follow the same authoritative path as an explicit preference
        # choice; otherwise a saved bank preference is silently ignored.
        pass
    elif account_choice in ("p", "petty", "petty cash", "0"):
        account = 0
        account_name = "petty cash"
    elif account_choice in ("b", "bank", "1"):
        account = 1
        account_name = "bank"
    else:
        print("Cancelled: choose petty cash, bank, or saved preference.")
        return
    capacity = ctx.hud.cargo_total
    used = ctx.hud.cargo_used
    if direction == "buy" and isinstance(capacity, int) and isinstance(used, int):
        print(f"Cargo guidance: {max(0, capacity - used)} hold(s) available; credits: "
              f"{present_money(ctx.hud.credits) if ctx.hud.credits is not None else '?'}.")
        # Guidance only; the server owns hold and quantity rules.
    port_id = _authoritative_positive_integer_id(port.get("id"))
    if port_id is None:
        print("Cannot determine a valid authoritative port ID; refresh the port before trading.")
        return
    sector_id = _authoritative_positive_integer_id(ctx.current_sector_id)
    if sector_id is None:
        print("Cannot trade without a valid authoritative current sector; refresh your location first.")
        return
    try:
        quote = ctx.conn.rpc("trade.quote", {"port_id": port_id, "commodity": row["commodity"], "quantity": quantity})
    except (ConnectionError, OSError) as exc:
        # A quote is read-only, but a lost reply still leaves the cached port
        # snapshot untrusted.  Keep the workflow playable after a disconnect:
        # do not open confirmation or attempt settlement.  Invalidate the
        # cached inventory now so a reconnect or later command cannot reuse
        # stale stock/direction metadata before the next authoritative read.
        ctx.state["last_rpc"] = None
        _invalidate_trade_port_context(
            ctx, "trade quote outcome is uncertain after connection loss"
        )
        print(f"Trade quote could not be confirmed after connection loss: {exc}")
        print("Reconnect before attempting another trade so port direction and stock can be refreshed.")
        return
    if not isinstance(quote, dict) or quote.get("status") != "ok":
        _trade_error(quote, "Quote")
        # A quote refusal can mean that the port stock, trade direction, or
        # player location changed after the cached port read. Reconcile the
        # authoritative port record before offering another trade, without
        # treating the refused quote as a mutation.
        _safe_refresh_port_info(ctx, _trade_port_refresh_request(ctx, port_id))
        return
    if not _trade_quote_type_matches(quote):
        print("Trade quote had an unexpected response type; refresh the port before trading.")
        _safe_refresh_port_info(ctx, _trade_port_refresh_request(ctx, port_id))
        return
    q = quote.get("data")
    if (not isinstance(q, dict)
            or not _trade_quote_display_data_is_actionable(q)):
        # A typed success envelope does not make malformed data actionable.
        # Keep the quote read-only: do not open confirmation or send a trade
        # mutation, and replace the cached inventory with a fresh server read.
        print("Trade quote returned invalid data; refresh the port before trading.")
        _safe_refresh_port_info(ctx, _trade_port_refresh_request(ctx, port_id))
        return
    quote_port_id = _authoritative_positive_integer_id(
        q.get("port_id") if isinstance(q, dict) else None
    )
    quote_commodity = q.get("commodity") if isinstance(q, dict) else None
    quote_quantity = q.get("quantity") if isinstance(q, dict) else None
    if (quote_port_id != port_id
            or not isinstance(quote_commodity, str)
            or not quote_commodity.strip()
            or quote_commodity.casefold() != row["commodity"].casefold()
            or type(quote_quantity) is not int
            or quote_quantity != quantity):
        # A successful-looking quote is not actionable unless it identifies
        # the exact server-owned port and requested line.  Do not allow a
        # stale or cross-location response to reach the mutating command.
        print("Trade quote did not identify the requested port, commodity, and quantity; refresh the port before trading.")
        _safe_refresh_port_info(ctx, _trade_port_refresh_request(ctx, port_id))
        return
    total_key = "total_buy_price" if direction == "buy" else "total_sell_price"
    total = q.get(total_key)
    # The quote is the authoritative commodity representation.  Port info is
    # display context and may use a different case from the canonical trade
    # code returned by the server; settlement and receipt correlation must use
    # the quoted value rather than the stale presentation value.
    commodity = quote_commodity
    print(f"Authoritative quote: {quantity} x {commodity} = {present_money(total)}"
          f" (unit {present_money(q.get('buy_price' if direction == 'buy' else 'sell_price'))}).")
    print(f"Trade account: {account_name}.")
    if ctx.hud.credits is not None and direction == "buy" and total is not None:
        try:
            projected = ctx.hud.credits - parse_credits(total)
            if projected < 0:
                print(f"Warning: locally projected credits would be {format_credits(projected)}.")
        except MoneyError:
            pass
    if not _trade_confirmation("Commit this trade?" ):
        print("Trade cancelled; no changes were made.")
        return
    payload = {"port_id": port_id, "sector_id": sector_id,
               "items": [{"commodity": commodity, "quantity": quantity}],
               "idempotency_key": str(uuid.uuid4())}
    if account is not None:
        payload["account"] = account
    try:
        resp = ctx.conn.rpc(f"trade.{direction}", payload)
    except (ConnectionError, OSError) as exc:
        # A dropped reply cannot distinguish a rejected trade from a committed
        # one.  Do not present a receipt, retry locally, or retain a synthetic
        # response; the reconnect/refresh path must reconcile credits, cargo,
        # turns and port stock from the server's authoritative state.
        ctx.state["last_rpc"] = None
        _invalidate_trade_port_context(
            ctx, "trade outcome is uncertain after connection loss"
        )
        print(f"Trade outcome is uncertain after connection loss: {exc}")
        print("Reconnect before attempting another trade so balances, cargo and port stock can be refreshed.")
        return
    ctx.state["last_rpc"] = resp
    if not isinstance(resp, dict) or resp.get("status") != "ok":
        _trade_error(resp, direction.capitalize())
        # A refusal can reflect a changed stock, balance or cargo snapshot.
        # Re-read all affected authoritative views before allowing another
        # trade, while preserving the fact that no trade was confirmed.
        _refresh_trade_views(ctx, port_id)
        return
    receipt = resp.get("data")
    if (not _trade_receipt_type_matches(resp, direction)
            or not _trade_receipt_lines_match(resp, payload)
            or not _trade_receipt_player_matches(resp, _known_player_id(ctx))):
        print("Trade response had an unexpected receipt type; settlement was not displayed.")
        _refresh_trade_views(ctx, port_id)
        return
    receipt_port_id = _authoritative_positive_integer_id(
        receipt.get("port_id") if isinstance(receipt, dict) else None
    )
    receipt_sector_id = _authoritative_positive_integer_id(
        receipt.get("sector_id") if isinstance(receipt, dict) else None
    )
    expected_sector_id = _authoritative_positive_integer_id(ctx.current_sector_id)
    if (receipt_port_id != port_id
            or expected_sector_id is None
            or receipt_sector_id != expected_sector_id):
        # A successful-looking envelope for another location is not a usable
        # settlement receipt. Keep the response for diagnostics, but do not
        # present it as a completed trade; refresh all affected views before
        # allowing another transaction.
        print("Trade response did not identify the confirmed port and sector; settlement was not displayed.")
        _refresh_trade_views(ctx, port_id)
        return
    expected_player_id = _known_player_id(ctx)
    receipt_player_id = (receipt.get("player_id")
                         if isinstance(receipt, dict) else None)
    # The canonical receipt schema always identifies the settling player. A
    # missing identity is not a compact response variant: it cannot prove that
    # this session's credits/cargo were settled, even when the local session
    # snapshot is incomplete.
    if (type(receipt_player_id) is not int or receipt_player_id <= 0
            or (expected_player_id is not None
                and receipt_player_id != expected_player_id)):
        # A receipt for another player is not evidence of this session's
        # settlement. A known session identity also makes an omitted player ID
        # non-authoritative under the published receipt contract. Reconcile the
        # authoritative views without presenting it or retrying the mutation.
        print("Trade response did not identify the confirmed player; settlement was not displayed.")
        _refresh_trade_views(ctx, port_id)
        return
    _present_trade_receipt(resp, direction)
    # Refresh authoritative player/ship state, then the port inventory shown here.
    if _refresh_trade_views(ctx, port_id):
        _print_port_summary(ctx)


@register("simple_buy_handler")
def simple_buy_handler(ctx: Context):
    dock_trade_flow(ctx, "buy")

@register("simple_sell_handler")
def simple_sell_handler(ctx: Context):
    dock_trade_flow(ctx, "sell")

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
    if target <= 0:
        print("Invalid sector ID."); return

    if not cur:
        # Fallback: fetch current sector from server if we don't have it cached
        try:
            info = ctx.conn.rpc("move.describe_sector", {})
            if isinstance(info, dict) and info.get("status") == "ok":
                data = get_data(info)
                if isinstance(data, dict):
                    cur = data.get("sector_id") or data.get("id")
        except (ConnectionError, OSError):
            raise
        except Exception:
            pass
    if type(cur) is not int or cur <= 0:
        print("Cannot determine current sector. Please re-display sector (D) first."); return

    route = ctx.state.get("ap_route") or []
    if not isinstance(route, list) or any(type(hop) is not int or hop <= 0 for hop in route):
        print("The existing route is invalid. Clear it before plotting another sector."); return
    origin = route[-1] if route else cur

    # Ask server for a path
    req = {"from_sector_id": origin, "to_sector_id": target}
    resp = ctx.conn.rpc("move.pathfind", req)
    status = resp.get("status") if isinstance(resp, dict) else None
    if status != "ok":
        err = (resp.get("error") or {}) if isinstance(resp, dict) else {}
        if not isinstance(err, dict):
            err = {}
        print(f"Pathfind failed {err.get('code')}: {err.get('message') or 'error'}")
        return
    data = resp.get("data")
    if not isinstance(data, dict):
        print("The server returned an invalid route; the existing route was not changed.")
        return

    # Try to normalize a path from a few likely shapes
    path = data.get("path") or data.get("sectors") or data.get("steps") or []
    if not isinstance(path, list):
        print("The server returned an invalid route; the existing route was not changed.")
        return
    # Some servers return an array of {from,to} edges; flatten if needed
    if path and isinstance(path[0], dict):
        # e.g., [{"from":1,"to":2},{"from":2,"to":5}] -> [1,2,5]
        seq = [path[0].get("from")]
        for e in path:
            if not isinstance(e, dict) or e.get("from") != seq[-1] or "to" not in e:
                print("The server returned an invalid route; the existing route was not changed.")
                return
            seq.append(e["to"])
        path = seq

    if not path or len(path) < 1:
        print("No route found to target sector.")
        return
    if (any(type(sector_id) is not int or sector_id <= 0 for sector_id in path)
            or path[0] != origin or path[-1] != target):
        print("The server returned an invalid route; the existing route was not changed.")
        return

    # Append path to current autopilot route, skipping the current sector if it's the first element
    route = list(route)
    route.extend(path[1:])
    ctx.state["ap_route"] = route

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
    for index, target in enumerate(route):
        if not _perform_warp(ctx, target):
            # Keep the uncompleted part visible so the player can inspect or
            # retry it after resolving the server refusal.
            ctx.state["ap_route"] = route[index:]
            ctx.state["ap_route_plotted"] = True
            print("Autopilot stopped; remaining route: " + " -> ".join(map(str, route[index:])))
            return
        remaining = route[index + 1:]
        ctx.state["ap_route"] = list(remaining)
        ctx.state["ap_route_plotted"] = bool(remaining)
        if not remaining:
            continue
        action = _autopilot_boundary_prompt(ctx, remaining)
        if action == "stop":
            print("Autopilot stopped at the last confirmed sector; remaining route: " + " -> ".join(map(str, remaining)))
            return
        if action == "stop_after_next":
            next_target = remaining[0]
            if not _perform_warp(ctx, next_target):
                ctx.state["ap_route"] = list(remaining)
                ctx.state["ap_route_plotted"] = True
                return
            remaining = remaining[1:]
            ctx.state["ap_route"] = list(remaining)
            ctx.state["ap_route_plotted"] = bool(remaining)
            print("Autopilot stopped after the next confirmed sector; remaining route: " + " -> ".join(map(str, remaining)))
            return
    ctx.state["ap_route"] = []
    ctx.state["ap_route_plotted"] = False


def _autopilot_set_mode(ctx: Context, action: str) -> bool:
    response = ctx.conn.rpc("move.autopilot.control", {"action": action})
    if not isinstance(response, dict) or response.get("status") != "ok":
        error = response.get("error") if isinstance(response, dict) else {}
        error = error if isinstance(error, dict) else {}
        print(f"[Error] Autopilot {action} refused: {error.get('message', 'Unknown error')}")
        return False
    data = get_data(response)
    if isinstance(data, dict):
        ctx.state["autopilot_status"] = data
    print(f"Autopilot {action} confirmed.")
    return True


def _autopilot_boundary_prompt(ctx: Context, remaining: List[int]) -> str:
    """Offer route controls only between server-confirmed warps."""
    while True:
        print("\nRoute controls: [Y] continue · [N] stop · [E] express · [I] sector info · [R] route · [S] status · [D] display · [P] stop after next warp")
        choice = input("Autopilot > ").strip().lower()
        if choice in ("y", ""):
            return "continue"
        if choice in ("n", "q"):
            cli_autopilot_stop(ctx)
            return "stop"
        if choice == "e":
            if _autopilot_set_mode(ctx, "express"):
                return "continue"
            return "stop"
        if choice == "i":
            response = _hud_rpc(ctx, "sector.info", {})
            print(json.dumps(response, ensure_ascii=False, indent=2))
        elif choice == "r":
            print("Remaining route: " + " → ".join(map(str, remaining)))
        elif choice == "s":
            cli_autopilot_status(ctx)
        elif choice == "d":
            call_handler("redisplay_sector", ctx)
        elif choice == "p":
            if _autopilot_set_mode(ctx, "stop_at_next"):
                return "stop_after_next"
            return "stop"
        else:
            print("Choose Y, N, E, I, R, S, D or P.")

# ---------------------------
# Planet & Computer flows
# ---------------------------
def _planet_id(ctx: Context) -> Optional[int]:
    # A confirmed landing identity may be retained while a follow-up refresh
    # is incomplete, so reconnect recovery can reconcile the same planet. It
    # is not sufficient authorization for a surface mutation: only the
    # authoritative on_planet flag makes the retained ID actionable.
    if ctx.state.get("on_planet") is not True:
        return None
    value = ctx.state.get("current_planet_id")
    # Planet IDs are server-authoritative JSON integers. Do not coerce a
    # boolean, float, or numeric string from stale/malformed state.
    if not isinstance(value, int) or isinstance(value, bool):
        return None
    return value if value > 0 else None


def _planet_record_id(planet: dict) -> Optional[int]:
    """Return the server-listed planet ID without coercing schema values."""
    if not isinstance(planet, dict):
        return None
    # `planet_id` is the explicit protocol field. When it is present, never
    # let a conflicting legacy `id` alias redirect the request to another
    # object. Older sector payloads may still expose only `id`, so retain
    # that compatibility path when the authoritative field is absent.
    if "planet_id" in planet:
        return _authoritative_positive_integer_id(planet.get("planet_id"))
    return _authoritative_positive_integer_id(planet.get("id"))


def _planet_payload(response: dict) -> dict:
    """Unwrap the supported planet.info envelopes without guessing fields."""
    data = get_data(response) if isinstance(response, dict) else {}
    if isinstance(data, list):
        data = data[0] if data and isinstance(data[0], dict) else {}
    if isinstance(data, dict) and isinstance(data.get("planet"), dict):
        return data["planet"]
    return data if isinstance(data, dict) else {}


def _planet_error(response: Any) -> str:
    error = response.get("error") if isinstance(response, dict) else None
    if isinstance(error, dict):
        return str(error.get("message") or error.get("code") or "The server refused the request.")
    if error:
        return str(error)
    return "The server refused the request."


def _planet_mutation_matches_request(response: Any, planet_id: int) -> bool:
    """Reject an explicitly contradictory planet mutation response.

    The mutation response contract does not require a planet object, so an
    omitted ID remains compatible with the authoritative follow-up
    ``planet.info`` refresh.  If the server does return an identity field,
    however, it must identify the requested planet; otherwise the client must
    not present the mutation as a successful transfer.
    """
    if not isinstance(response, dict):
        return False
    data = get_data(response)
    if not isinstance(data, dict):
        return True
    if "planet_id" not in data and "id" not in data and not isinstance(data.get("planet"), dict):
        return True
    returned_id = _planet_record_id(data)
    return returned_id == planet_id


def _planet_info(ctx: Context, planet_id: int) -> Optional[dict]:
    response = ctx.conn.rpc("planet.info", {"planet_id": planet_id})
    ctx.state["last_rpc"] = response
    if not isinstance(response, dict) or response.get("status") != "ok":
        print(f"[Error] Could not load planet {planet_id}: {_planet_error(response)}")
        return None
    # The planet surface is an authoritative follow-up read, not a generic
    # success envelope.  Correlating only the returned ID would allow a
    # delayed response from another command to populate the current surface
    # when its payload happened to contain the same planet ID.
    if response.get("type") != "planet.info":
        print(f"[Error] Planet {planet_id} was not displayed: planet.info returned an unexpected response type.")
        return None
    payload = _planet_payload(response)
    returned_id = _planet_record_id(payload)
    if returned_id != planet_id:
        # A successful envelope is not enough to make a surface record
        # authoritative.  Delayed or cross-object replies must not replace
        # the selected planet's confirmed state.
        detail = "omitted its authoritative planet ID" if returned_id is None else f"identified planet {returned_id}"
        print(f"[Error] Planet {planet_id} was not displayed: planet.info {detail}.")
        return None
    return payload


def _print_planet_overview(planet: dict) -> None:
    """Present only server-confirmed values; do not estimate stock/population."""
    print("\n--- Planet Overview ---")
    fields = (
        ("name", "Name"), ("class", "Class"), ("sector_id", "Sector"),
        ("owner_type", "Owner type"), ("owner_id", "Owner ID"),
        ("population", "Population"), ("population_type", "Population type"),
        ("morale_status", "Morale status"), ("colonists_unassigned", "Unassigned colonists"),
        ("colonists_ore", "Ore workers"), ("colonists_org", "Organics workers"),
        ("colonists_eq", "Equipment workers"), ("colonists_weapons", "Weapons workers"),
        ("fighters", "Planet fighters"), ("citadel_level", "Citadel level"),
    )
    for key, label in fields:
        if key in planet and planet[key] is not None:
            print(f"{label}: {planet[key]}")
    goods = planet.get("goods")
    print("Commodities:")
    if isinstance(goods, list) and goods:
        for row in goods:
            if isinstance(row, dict):
                print("  {commodity}: {quantity} / {max_capacity}".format(
                    commodity=row.get("commodity", "?"),
                    quantity=row.get("quantity", "?"),
                    max_capacity=row.get("max_capacity", "?"),
                ))
    else:
        print("  No configured commodity storage was returned.")


def _refresh_planet_overview(ctx: Context, planet_id: int) -> None:
    planet = _planet_info(ctx, planet_id)
    if planet is not None:
        _print_planet_overview(planet)


def _current_or_selected_planet_id(ctx: Context) -> Optional[int]:
    current = _planet_id(ctx)
    if current:
        return current
    planets = (ctx.last_sector_desc or {}).get("planets") or []
    if not planets:
        print("No planet is available in the current sector.")
        return None
    if len(planets) == 1:
        candidate = planets[0]
    else:
        print("Planets in this sector:")
        for index, planet in enumerate(planets, 1):
            print(f"  {index}. {planet.get('name', 'Unnamed Planet')} (ID: {planet.get('id') or planet.get('planet_id')})")
        try:
            choice = int(input("Planet number: ").strip())
        except ValueError:
            print("Invalid planet selection.")
            return None
        if not 1 <= choice <= len(planets):
            print("Invalid planet selection.")
            return None
        candidate = planets[choice - 1]
    # Keep follow-up surface actions aligned with land_flow: a malformed
    # legacy alias must not shadow a valid authoritative planet_id.
    selected = _planet_record_id(candidate)
    if selected is None:
        print("The selected planet has no usable ID.")
        return None
    return selected


@register("planet_overview_flow")
def planet_overview_flow(ctx: Context):
    planet_id = _current_or_selected_planet_id(ctx)
    if planet_id is None:
        return
    try:
        planet = _planet_info(ctx, planet_id)
    except (ConnectionError, OSError) as exc:
        # A lost planet.info reply is not evidence that the surface record was
        # read.  Keep the confirmed landing identity for reconnect recovery,
        # but do not leave the interactive surface in a traceback path.
        print(f"Planet information outcome is uncertain after connection loss: {exc}")
        print("Reconnect before relying on planetary records or issuing surface actions.")
        if ctx.menu_stack and ctx.menu_stack[-1] == "SURFACE":
            ctx.menu_stack.pop()
        return
    if planet is None:
        # A confirmed landing does not make a refused planet read safe to
        # display. Remove the surface menu before reconciling the location so
        # stale planet actions cannot be issued against an unknown record.
        if ctx.menu_stack and ctx.menu_stack[-1] == "SURFACE":
            ctx.menu_stack.pop()
        refresh_sector_and_display(ctx)
        return
    _print_planet_overview(planet)


def _read_nonnegative_target(label: str, current: int) -> Optional[int]:
    raw = input(f"{label} workers [{current}]: ").strip()
    if not raw:
        return current
    try:
        value = int(raw)
    except ValueError:
        print(f"{label} worker target must be a whole number.")
        return None
    if value < 0:
        print(f"{label} worker target cannot be negative.")
        return None
    return value


@register("planet_workforce_flow")
def planet_workforce_flow(ctx: Context):
    planet_id = _planet_id(ctx)
    if planet_id is None:
        print("Land on a planet before assigning its workforce.")
        return
    planet = _planet_info(ctx, planet_id)
    if planet is None:
        return
    jobs = (("ore", "Ore"), ("organics", "Organics"),
            ("equipment", "Equipment"), ("weapons", "Weapons / fighter production"))
    current_keys = {"ore": "colonists_ore", "organics": "colonists_org",
                    "equipment": "colonists_eq", "weapons": "colonists_weapons"}
    targets = {}
    for job, label in jobs:
        value = _read_nonnegative_target(label, int(planet.get(current_keys[job], 0) or 0))
        if value is None:
            return
        targets[job] = value
    population_value = planet.get("population")
    population = int(population_value) if isinstance(population_value, (int, float)) else sum(
        int(planet.get(key, 0) or 0)
        for key in ("colonists_unassigned", "colonists_ore", "colonists_org", "colonists_eq", "colonists_weapons")
    )
    if sum(targets.values()) > population:
        print(f"Assignments total {sum(targets.values())}, above the confirmed planet population of {population}.")
        return
    payload = {"planet_id": planet_id, **targets}
    response = ctx.conn.rpc("planet.colonists.allocate", payload)
    ctx.state["last_rpc"] = response
    if not isinstance(response, dict) or response.get("status") != "ok":
        print(f"[Error] Workforce allocation refused: {_planet_error(response)}")
        return
    data = get_data(response)
    print("Workforce allocation accepted by the server.")
    print("  Ore: {colonists_ore}; Organics: {colonists_org}; Equipment: {colonists_eq}; Weapons: {colonists_weapons}; Unassigned: {colonists_unassigned}".format(
        **{key: data.get(key, targets.get(key.removeprefix("colonists_"), "not returned"))
           for key in ("colonists_ore", "colonists_org", "colonists_eq", "colonists_weapons", "colonists_unassigned")}
    ))
    _refresh_planet_overview(ctx, planet_id)


@register("planet_harvest_unavailable_flow")
def planet_harvest_unavailable_flow(ctx: Context):
    """Explain why harvest cannot be requested from the client yet.

    The protocol exposes a harvest-shaped command, but the authoritative
    server handler is still unavailable and V3 has no pinned harvest policy.
    Keep the capability visible while refusing to fabricate a formula or RPC.
    """
    print(
        "Planet harvest unavailable: the authoritative server handler and "
        "V3 production policy are not released, so no request was sent."
    )


def _select_planet_commodity(planet: dict, include_credits: bool = False) -> Optional[str]:
    goods = planet.get("goods")
    options = [row for row in goods if isinstance(row, dict) and row.get("commodity")] if isinstance(goods, list) else []
    if include_credits and int(planet.get("citadel_level", 0) or 0) > 0:
        options.append({"commodity": "CREDITS", "quantity": "treasury"})
    if not options:
        print("The server reports no transferable planet commodities.")
        return None
    print("Planet stock:")
    for index, row in enumerate(options, 1):
        print(f"  {index}. {row['commodity']} · {row.get('quantity', '?')} available")
    raw = input("Commodity number or code: ").strip().upper()
    try:
        index = int(raw)
        if 1 <= index <= len(options):
            return str(options[index - 1]["commodity"]).upper()
    except ValueError:
        pass
    return raw if any(str(row["commodity"]).upper() == raw for row in options) else None


def _positive_quantity(prompt: str) -> Optional[int]:
    try:
        quantity = int(input(prompt).strip())
    except ValueError:
        print("Quantity must be a whole number.")
        return None
    if quantity <= 0:
        print("Quantity must be greater than zero.")
        return None
    return quantity


@register("combat_attack_flow")
def combat_attack_flow(ctx: Context):
    """Attack only a server-listed ship, with a FedSpace client-side guard."""
    sector_id = ctx.current_sector_id
    if sector_id is None:
        print("Refresh current-sector information before selecting an attack target.")
        return
    if 1 <= sector_id <= 10:
        print("Attack cancelled: Captain Z protects FedSpace sectors 1–10.")
        return
    my_ship_id = get_my_ship_id(ctx.conn, ctx)
    targets = []
    for row in (ctx.last_sector_desc or {}).get("ships") or []:
        if not isinstance(row, dict):
            continue
        target_id = row.get("id", row.get("ship_id"))
        if not isinstance(target_id, int) or target_id <= 0 or target_id == my_ship_id:
            continue
        targets.append(row)
    if not targets:
        print("No other server-listed ship is available as a target in this sector.")
        return
    print("Ships listed in the current sector:")
    for row in targets:
        target_id = row.get("id", row.get("ship_id"))
        name = row.get("name") or row.get("ship_name") or "Unnamed ship"
        print(f"  {target_id}: {name}")
    try:
        target_id = int(input("Target ship ID: ").strip())
    except ValueError:
        print("Attack cancelled: enter one of the listed ship IDs.")
        return
    if not any(row.get("id", row.get("ship_id")) == target_id for row in targets):
        print("Attack cancelled: that ship is not listed in the current sector.")
        return
    if not _trade_confirmation(f"Attack ship {target_id}?", default_no=True):
        print("Attack cancelled; no command was sent.")
        return
    response = ctx.conn.rpc("combat.attack", {"target_ship_id": target_id})
    ctx.state["last_rpc"] = response
    if not isinstance(response, dict) or response.get("status") != "ok":
        error = (response.get("error") or {}) if isinstance(response, dict) else {}
        print(f"[Error] Attack refused: {error.get('message', 'The server did not accept the attack.')}")
        # Some authoritative refusal paths have already applied a punishment
        # (for example Captain Z destroying the attacking ship). Reconcile
        # from server-owned reads; never infer death or a replacement vessel
        # from the refusal envelope.
        _reconcile_after_combat_refusal(ctx)
        return
    print("--- Server Combat Report ---")
    _pp(get_data(response))
    _hud_rpc(ctx, "ship.status", {})
    sector_response = ctx.conn.rpc("sector.info", {})
    if isinstance(sector_response, dict) and sector_response.get("status") == "ok":
        ctx.last_sector_desc = normalize_sector(get_data(sector_response))


def _planet_transfer_flow(ctx: Context, direction: str, treasury_only: bool = False):
    planet_id = _planet_id(ctx)
    if planet_id is None:
        print("Land on the planet before transferring stock.")
        return
    planet = _planet_info(ctx, planet_id)
    if planet is None:
        return
    if treasury_only:
        if int(planet.get("citadel_level", 0) or 0) < 1:
            print("A level 1 citadel is required to use the planet treasury.")
            return
        commodity = "CREDITS"
    else:
        commodity = _select_planet_commodity(planet, include_credits=True)
        if commodity is None:
            print("Choose one of the commodities returned by planet.info.")
            return
        if commodity == "CREDITS" and int(planet.get("citadel_level", 0) or 0) < 1:
            print("A level 1 citadel is required to use the planet treasury.")
            return
    quantity = _positive_quantity(f"Credits to {direction}: " if commodity == "CREDITS" else f"{commodity} quantity to {direction}: ")
    if quantity is None:
        return
    command = "planet.deposit" if direction == "deposit" else "planet.withdraw"
    response = ctx.conn.rpc(command, {"planet_id": planet_id, "commodity": commodity, "quantity": quantity})
    ctx.state["last_rpc"] = response
    if not isinstance(response, dict) or response.get("status") != "ok":
        print(f"[Error] Planet {commodity} {direction} refused: {_planet_error(response)}")
        return
    if not _planet_mutation_matches_request(response, planet_id):
        print(
            f"[Error] Planet {commodity} {direction} was not confirmed: "
            "the server response identified a different planet."
        )
        _refresh_planet_overview(ctx, planet_id)
        return
    data = get_data(response)
    print(f"{quantity} {commodity} {direction} accepted by the server.")
    for key, label in (("planet_quantity", "Planet stock"), ("ship_quantity", "Ship cargo"),
                       ("planet_treasury_balance", "Treasury balance"), ("player_credits", "Player credits")):
        if key in data:
            print(f"{label}: {data[key]}")
    _refresh_planet_overview(ctx, planet_id)


@register("planet_stock_transfer_flow")
def planet_stock_transfer_flow(ctx: Context):
    direction = input("(D)eposit to planet or (W)ithdraw to ship? ").strip().lower()
    if direction in ("d", "deposit"):
        _planet_transfer_flow(ctx, "deposit")
    elif direction in ("w", "withdraw"):
        _planet_transfer_flow(ctx, "withdraw")
    else:
        print("Choose deposit or withdraw.")


@register("planet_fighter_transfer_flow")
def planet_fighter_transfer_flow(ctx: Context):
    """Transfer fighters between the landed active ship and its planet."""
    planet_id = _planet_id(ctx)
    if planet_id is None:
        print("Land your active ship on the planet before transferring fighters.")
        return
    planet = _planet_info(ctx, planet_id)
    if planet is None:
        return
    print(f"Confirmed planet fighters: {planet.get('fighters', 'not returned')}")
    direction = input("Transfer (S)hip fighters to planet or (P)lanet fighters to ship? ").strip().lower()
    if direction in ("s", "ship", "ship_to_planet"):
        direction = "ship_to_planet"
    elif direction in ("p", "planet", "planet_to_ship"):
        direction = "planet_to_ship"
    else:
        print("Choose ship-to-planet or planet-to-ship.")
        return
    quantity = _positive_quantity("Fighter quantity: ")
    if quantity is None:
        return
    if not _trade_confirmation(
        f"Transfer {quantity} fighters ({direction.replace('_', ' ')})?", default_no=True
    ):
        print("Fighter transfer cancelled; no command was sent.")
        return
    response = ctx.conn.rpc("planet.fighters.transfer", {
        "planet_id": planet_id, "direction": direction, "quantity": quantity,
    })
    ctx.state["last_rpc"] = response
    if not isinstance(response, dict) or response.get("status") != "ok":
        print(f"[Error] Planet fighter transfer refused: {_planet_error(response)}")
        return
    data = get_data(response)
    print("Fighter transfer accepted by the server.")
    for key, label in (("planet_fighters", "Planet fighters"), ("ship_fighters", "Ship fighters")):
        if key in data:
            print(f"{label}: {data[key]}")
    _refresh_planet_overview(ctx, planet_id)


@register("planet_treasury_deposit_flow")
def planet_treasury_deposit_flow(ctx: Context):
    _planet_transfer_flow(ctx, "deposit", treasury_only=True)


@register("planet_treasury_withdraw_flow")
def planet_treasury_withdraw_flow(ctx: Context):
    _planet_transfer_flow(ctx, "withdraw", treasury_only=True)


@register("citadel_build_flow")
def citadel_build_flow(ctx: Context):
    planet_id = _planet_id(ctx)
    if planet_id is None:
        print("Land on a planet before starting citadel construction.")
        return
    planet = _planet_info(ctx, planet_id)
    if planet is None:
        return
    raw_level = planet.get("citadel_level", planet.get("level"))
    level = _authoritative_nonnegative_integer(raw_level)
    if level is None:
        print("Citadel construction unavailable: planet.info did not return a valid authoritative citadel level.")
        return
    if level >= 6:
        print("Citadel is already at the maximum level (6).")
        return
    target = level + 1
    print(f"Citadel level {level}; next target level {target}.")
    print("Current confirmed stock and colonists:")
    _print_planet_overview(planet)
    print("Construction requirements are checked by the server for this planet class.")
    print("The current planet.info response does not include the exact requirement table.")
    answer = input("Start construction? [y/N]: ").strip().lower()
    if answer not in ("y", "yes"):
        print("Construction cancelled; no resources were spent.")
        return
    command = "citadel.build" if level == 0 else "citadel.upgrade"
    response = ctx.conn.rpc(command, {"planet_id": planet_id})
    ctx.state["last_rpc"] = response
    if not isinstance(response, dict) or response.get("status") != "ok":
        print(f"[Error] Citadel construction refused: {_planet_error(response)}")
        return
    data = get_data(response)
    print(f"Citadel construction started toward level {data.get('target_level', target)}.")
    if data.get("days_to_complete") is not None:
        print(f"Server-reported duration: {data['days_to_complete']} day(s).")
    if data.get("completion_time") is not None:
        print(f"Server-reported completion time: {data['completion_time']}.")
    _refresh_planet_overview(ctx, planet_id)


@register("planet_rename_flow")
def planet_rename_flow(ctx: Context):
    planet_id = _planet_id(ctx)
    if planet_id is None:
        print("Land on the planet before renaming it.")
        return
    new_name = input("New planet name (3–32 characters): ").strip()
    if not 3 <= len(new_name) <= 32:
        print("Planet names must be between 3 and 32 characters.")
        return
    response = ctx.conn.rpc("planet.rename", {"planet_id": planet_id, "new_name": new_name})
    ctx.state["last_rpc"] = response
    if not isinstance(response, dict) or response.get("status") != "ok":
        print(f"[Error] Planet rename refused: {_planet_error(response)}")
        return
    print(f"Planet renamed to {new_name}.")
    _refresh_planet_overview(ctx, planet_id)


@register("land_flow")
def land_flow(ctx: Context):
    # Check current sector data for planets
    if not isinstance(ctx.last_sector_desc, dict):
        print("Planet landing unavailable: current-sector information is malformed; refresh the sector before landing.")
        return
    planets = ctx.last_sector_desc.get("planets") or []
    
    if not planets:
        print("No planets in this sector.")
        return

    if not all(isinstance(planet, dict) for planet in planets):
        print("Planet landing unavailable: the sector response contains an invalid planet record; refresh the sector before landing.")
        return

    selected_pid = None
    listed_ids = {}
    valid_planet_ids = []
    for planet in planets:
        if not isinstance(planet, dict):
            continue
        # Accept the release schema's supported aliases, but only after
        # validating each candidate.  A malformed legacy `id` field must not
        # shadow a valid authoritative `planet_id` supplied alongside it.
        planet_id = _planet_record_id(planet)
        if planet_id is not None:
            valid_planet_ids.append(planet_id)
            listed_ids[planet_id] = planet

    if len(valid_planet_ids) != len(set(valid_planet_ids)):
        print("Planet landing unavailable: the sector response contains duplicate planet IDs; refresh the sector before landing.")
        return

    if not listed_ids:
        print("No listed planet has a usable server ID.")
        return

    if len(planets) == 1:
        p = planets[0]
        # Auto-select if only one
        selected_pid = next(iter(listed_ids))
        print(f"Landing on {p.get('name')} (ID: {selected_pid})...")
    else:
        print("\nPlanets in this sector:")
        for i, p in enumerate(planets):
            pid = _planet_record_id(p)
            name = p.get("name") or "Unnamed"
            displayed_id = str(pid) if pid is not None else "unavailable"
            print(f"  {i+1}) {name} [ID: {displayed_id}]")
        
        raw = input("Land on (number or ID): ").strip()
        if not raw: return
        
        # Try as index first
        try:
            idx = int(raw)
            if 1 <= idx <= len(planets):
                selected_pid = _planet_record_id(planets[idx - 1])
                if selected_pid not in listed_ids:
                    selected_pid = None
            else:
                # Typed IDs must come from the authoritative sector listing.
                selected_pid = idx if idx in listed_ids else None
        except ValueError:
            print("Invalid input.")
            return

    if not isinstance(selected_pid, int) or selected_pid <= 0:
        print("Could not determine a listed planet ID; refresh the sector before landing.")
        return

    try:
        r = ctx.conn.rpc("planet.land", {"planet_id": selected_pid})
    except (ConnectionError, OSError) as exc:
        # A lost reply cannot establish whether the server consumed the turn
        # or completed the landing.  Preserve the last confirmed client state
        # and leave reconciliation to the reconnect/refresh path; never open
        # a local surface from an unconfirmed request.
        ctx.state["last_rpc"] = None
        print(f"Landing outcome is uncertain after connection loss: {exc}")
        print("Reconnect before attempting another landing so turns and location can be refreshed.")
        return
    ctx.state["last_rpc"] = r
    # The authoritative C handler emits one response schema: an ``ok``
    # envelope typed ``planet.land.success`` with ``data.planet_id``.  Do not
    # treat a compact/root-level or untyped success-shaped envelope as a
    # second protocol variant that can authorize a planet surface.
    if isinstance(r, dict) and r.get("status") == "ok":
        response_type = r.get("type")
        if response_type != "planet.land.success":
            print(f"Landing response had unexpected type {response_type}; refreshing confirmed state.")
            _invalidate_unconfirmed_landing_surface(ctx)
            _safe_landing_refresh(ctx, "player.my_info", {})
            _safe_landing_refresh(ctx, "ship.status", {})
            _safe_landing_sector_refresh(ctx)
            return
        response_data = get_data(r)
        response_data = response_data if isinstance(response_data, dict) else {}
        raw_landed_pid = response_data.get("planet_id")
        landed_pid = _authoritative_positive_integer_id(raw_landed_pid)
        if landed_pid is None or landed_pid != selected_pid:
            print("Landing succeeded, but the server did not return the requested planet ID.")
            # Do not infer a landing from an incomplete success payload, but
            # do reconcile every state domain that a landing may have changed.
            # The server can consume a turn or apply landing defenses before
            # returning a malformed/partial response.
            _invalidate_unconfirmed_landing_surface(ctx)
            _safe_landing_refresh(ctx, "player.my_info", {})
            _safe_landing_refresh(ctx, "ship.status", {})
            _safe_landing_sector_refresh(ctx)
            return
        print(f"Landed on planet {landed_pid}.")
        ctx.state["last_rpc"] = r
        # Landing consumes a turn and changes ship/planet state. Refresh the
        # authoritative HUD domains before opening the surface menu so the
        # next render cannot show pre-landing turns or ship state.
        player_resp = _safe_landing_refresh(ctx, "player.my_info", {})
        player_data = _authoritative_hud_record(player_resp, "player")
        player_refreshed = _landing_player_refresh_is_current(
            ctx, player_resp, require_identity=True
        )
        if player_refreshed:
            ctx.player_info = get_data(player_resp)
            _update_corp_context(ctx)
        # Landing changes the active ship's location/state.  Reconcile through
        # the same identity-checked refresh used after movement so a valid
        # looking response for another ship cannot make stale surface actions
        # actionable.
        ship_resp = _safe_landing_refresh(ctx, "ship.status", {})
        ship_record = (
            _authoritative_hud_record(ship_resp, "ship")
            if _refresh_response_type_matches(ship_resp, "ship.status")
            else None
        )
        expected_ship_id = ctx.hud.ship_id
        # When the pre-landing HUD had no active-ship identity, the refreshed
        # player record is the authoritative relationship for this session.
        # Do not let an otherwise valid ship.status for another ship make the
        # newly landed surface actionable.
        if expected_ship_id is None and isinstance(player_data, dict):
            player_ship_id = _authoritative_positive_integer_id(player_data.get("ship_id"))
            if player_ship_id is not None:
                expected_ship_id = player_ship_id
        actual_ship_id = (ship_record.get("id", ship_record.get("ship_id"))
                          if ship_record else None)
        ship_refreshed = (
            ship_record is not None
            # A landing surface is actionable only after the refresh identifies
            # the server-owned active ship.  Do not accept a partial ship
            # object merely because it contains display fields such as hull.
            and type(actual_ship_id) is int and actual_ship_id > 0
            and (expected_ship_id is None or actual_ship_id == expected_ship_id)
        )
        if ship_refreshed:
            # ship.status exposes the server-owned landed planet ID. It is the
            # final identity check before exposing planet actions: omission is
            # also a partial refresh, because a ship-only record cannot prove
            # that this active ship is on the planet returned by planet.land.
            landed_planet_id = _authoritative_positive_integer_id(ship_record.get("onplanet"))
            ship_refreshed = landed_planet_id == landed_pid
        sector_refreshed = _safe_landing_sector_refresh(ctx)
        if not player_refreshed or not ship_refreshed or not sector_refreshed:
            # The landing envelope is not enough to make planet actions
            # actionable.  Do not leave a stale/local surface context behind
            # when the authoritative refresh cannot prove the active ship is
            # on this exact planet.
            ctx.state["on_planet"] = False
            ctx.state["current_planet_id"] = landed_pid
            # A landing can be retried from a previously confirmed surface
            # after the local location has gone stale.  Do not leave that
            # old menu frame reachable while the replacement landing is
            # awaiting authoritative player/ship reconciliation.
            while len(ctx.menu_stack) > 1 and ctx.menu_stack[-1] == "SURFACE":
                ctx.menu_stack.pop()
            print("Landing confirmed, but the post-landing authoritative refresh was incomplete; displayed state may be stale.")
            # Keep the server-confirmed landing identity, but do not enter a
            # surface workflow that could present pre-landing credits, turns,
            # cargo or hull as actionable state.  The next normal refresh can
            # safely retry the surface read without inventing a landing result.
            return
        ctx.state["on_planet"] = True
        ctx.state["current_planet_id"] = landed_pid  # Store for colonist transfer
        # A valid replacement landing supersedes any stale surface frame. Keep
        # one actionable surface transition, matching the Godot workflow, so
        # back-navigation cannot return to a pre-landing planet context.
        while len(ctx.menu_stack) > 1 and ctx.menu_stack[-1] == "SURFACE":
            ctx.menu_stack.pop()
        ctx.menu_stack.append("SURFACE")
        planet_overview_flow(ctx)
    else:
        if not _landing_response_type_matches(r):
            response_type = r.get("type") if isinstance(r, dict) else None
            print(f"Landing response had unexpected type {response_type}; refreshing confirmed state.")
            _invalidate_unconfirmed_landing_surface(ctx)
            _safe_landing_refresh(ctx, "player.my_info", {})
            _safe_landing_refresh(ctx, "ship.status", {})
            _safe_landing_sector_refresh(ctx)
            return
        error = r.get("error", {}) if isinstance(r, dict) else None
        if not isinstance(r, dict):
            err = "The server returned a malformed landing response."
        elif isinstance(error, dict):
            err = error.get("message", "Unknown error")
        elif error:
            err = str(error)
        else:
            err = "The server returned a malformed landing response."
        print(f"Landing failed: {err}")
        outcome = _planet_landing_outcome_lines(r)
        for line in outcome:
            print(line)
        # Only an explicit server terminal outcome may invalidate an existing
        # surface. A refusal by itself is not enough to infer destruction.
        if _move_response_is_terminal(r):
            _invalidate_unconfirmed_landing_surface(ctx)
            print("Landing outcome was terminal; planetary actions are unavailable until state is refreshed.")
        # The server is authoritative even for a refusal. Refresh state that
        # may be stale, and never infer a landing or retry it locally.
        player_resp = _safe_landing_refresh(ctx, "player.my_info", {})
        # A refusal can still follow server-side defense/turn processing, so
        # reconcile the HUD, but do not apply an unbound player record.  The
        # success path already requires identity before exposing the surface;
        # refusal reconciliation must use the same authoritative-ID rule.
        if _landing_player_refresh_is_current(ctx, player_resp, require_identity=True):
            ctx.player_info = get_data(player_resp)
            _update_corp_context(ctx)
        _safe_landing_refresh(ctx, "ship.status", {})
        _safe_landing_sector_refresh(ctx)


def _invalidate_unconfirmed_landing_surface(ctx: Context) -> None:
    """Remove stale local surface actions after an ambiguous landing reply.

    An explicit server refusal is handled separately and may leave an already
    confirmed surface intact.  A success-shaped response with a missing or
    mismatched identity is different: the client cannot know whether the
    attempted transition replaced that surface, so it must not keep local
    planet actions actionable while the authoritative refresh is in flight.
    """
    ctx.state["on_planet"] = False
    ctx.state.pop("current_planet_id", None)
    while len(ctx.menu_stack) > 1 and ctx.menu_stack[-1] == "SURFACE":
        ctx.menu_stack.pop()


def _authoritative_positive_integer_id(value: Any) -> Optional[int]:
    """Accept only JSON values that represent a positive integer ID.

    In particular, bool is an int subclass in Python and coercing a JSON
    number such as ``77.0`` or ``77.5`` can turn schema-mismatched metadata
    into a request target. Neither coercion is safe for a server-authoritative
    object identifier.
    """
    if isinstance(value, bool):
        return None
    if isinstance(value, int):
        return value if value > 0 else None
    return None


def _authoritative_nonnegative_integer(value: Any) -> Optional[int]:
    """Accept only a JSON integer measurement at or above zero.

    Planet development actions depend on the returned level.  Do not coerce
    strings, floats, or booleans from a mismatched/stale response into a
    level that could authorize a build or upgrade request.
    """
    if isinstance(value, bool):
        return None
    if isinstance(value, int) and value >= 0:
        return value
    return None


def _response_type_matches(response: Any, expected: str) -> bool:
    """Accept only the published response schema for an actionable read."""
    if not isinstance(response, dict):
        return False
    response_type = response.get("type")
    if expected == "port.info":
        # `cmd_trade_port_info` is shared by the port-info command aliases,
        # but it publishes one response schema for all of them.  An omitted
        # type is not a compatibility variant: accepting it could let an
        # unrelated success-shaped envelope repopulate actionable trade
        # state. `port.info` itself is a command name, not a response schema.
        return response_type == "trade.port_info"
    if expected == "move.scan":
        # The command is move.scan, but the C handler publishes the shared
        # sector.scan response schema for both summary and detailed scans.
        return response_type == "sector.scan"
    if response_type == expected:
        return True
    # The authoritative server response schema is sector.density.scan.  Do
    # not accept client-invented aliases: a success-shaped response from a
    # different or unavailable protocol variant must not become visibility
    # evidence.
    if expected == "sector.scan.density":
        return response_type == "sector.density.scan"
    if expected == "planet.launch":
        return response_type == "planet.launch.success" or (
            response_type in ("error", "refused")
            and response.get("status") in ("error", "refused")
        )
    return False


def _movement_response_type_matches(response: Any, expected: str) -> bool:
    """Accept only the documented movement result envelope.

    Successful movement replies must identify their published response
    schema. The server's canonical refused/error envelope uses ``type:
    error`` while carrying the movement status and any authoritative hazard
    data in ``error.data``; accept that pairing only for a non-success
    movement response. An omitted/empty type or ``type: refused`` is not a
    second protocol variant, and an explicit type from a different command
    must not authorize movement or hazard state.
    """
    if not isinstance(response, dict):
        return False
    response_type = response.get("type")
    if response_type == expected:
        # A command-specific movement type is a success contract only. A
        # negative envelope must use the shared error type so malformed
        # responses cannot expose hazard data or authorize relocation.
        return response.get("status") == "ok"
    # The C server uses the shared error response schema for both refused and
    # error statuses. Keep the client aligned with that one wire contract.
    return (response_type == "error"
            and response.get("status") in ("refused", "error"))


def _landing_response_type_matches(response: Any) -> bool:
    """Accept only the published error envelope for ``planet.land``.

    The C server uses the shared ``error`` response type for both refused and
    error statuses.  ``type: refused`` is not a second protocol variant and
    must not be treated as an authoritative landing refusal.
    """
    if not isinstance(response, dict):
        return False
    response_type = response.get("type")
    return (response_type == "error"
            and response.get("status") in ("error", "refused"))


def _planet_attack_response_type_matches(response: Any) -> bool:
    """Accept only the canonical planetary-attack response envelope."""
    if not isinstance(response, dict):
        return False
    # A successful attack must carry the one published response type. Do not
    # turn an envelope with no command identity into a capture/loss fact.
    return response.get("type") == "combat.attack_planet"


def _planet_attack_result_is_authoritative(data: Any, planet_id: int) -> bool:
    """Accept only the published combat.attack_planet result fields.

    The client may display the server's loss/capture result, but it must not
    turn a malformed success envelope into a gameplay fact.  Ownership and
    destruction remain server-owned and are not inferred here.
    """
    if not isinstance(data, dict) or type(data.get("planet_id")) is not int:
        return False
    if data.get("planet_id") != planet_id or type(data.get("captured")) is not bool:
        return False
    return all(
        type(data.get(field)) is int and data[field] >= 0
        for field in ("attacker_remaining_fighters", "defender_remaining_fighters")
    )


def _refresh_after_planet_attack(ctx: Context) -> None:
    """Reconcile every client-owned snapshot affected by planetary combat.

    An attack may change fighters, ship survival, credits/ownership, and the
    planet record before its response reaches the client.  A lost refresh must
    not prevent the remaining domains from being queried, and no local result
    is inferred when the server response is unavailable.
    """
    try:
        player_resp = _hud_rpc(ctx, "player.my_info", {})
        if player_resp.get("status") == "ok":
            ctx.player_info = get_data(player_resp)
            _update_corp_context(ctx)
    except (ConnectionError, OSError):
        pass
    try:
        _hud_rpc(ctx, "ship.status", {})
    except (ConnectionError, OSError):
        pass
    try:
        _refresh_current_sector_after_move(ctx)
    except (ConnectionError, OSError):
        pass


def _planet_landing_outcome_lines(
    response: dict, closing_message: str = "Landing was refused; no planetary access was granted."
) -> list[str]:
    data = response.get("data", {}) if isinstance(response, dict) else {}
    error = response.get("error", {}) if isinstance(response, dict) else {}
    error_data = error.get("data", {}) if isinstance(error, dict) else {}
    # The gateway may carry a refusal's authoritative combat report in either
    # envelope.  Preserve both locations, with error.data taking precedence
    # for fields repeated by the server; never derive a result locally.
    details = dict(data) if isinstance(data, dict) else {}
    if isinstance(error_data, dict):
        details.update(error_data)
    if not isinstance(details, dict):
        return []
    combat = details.get("combat", [])
    if not isinstance(combat, list):
        combat = []
    terminal_location = details.get("ship_remains_in_sector")
    if not combat and terminal_location is not False:
        return []
    lines = ["Authoritative planetary defense report:"]
    for item in combat:
        if not isinstance(item, dict):
            continue
        if item.get("defense") == "atmosphere_quasar":
            lines.append(
                "  Atmospheric Quasar: %s damage; shields lost %s, hull lost %s; "
                "remaining shields %s, hull %s%s."
                % (item.get("damage", "unknown"), item.get("shields_lost", "unknown"),
                   item.get("hull_lost", "unknown"), item.get("shields_remaining", "unknown"),
                   item.get("hull_remaining", "unknown"),
                   "; ship destroyed" if item.get("destroyed") is True else "")
            )
        else:
            lines.append(
                "  Planetary fighters %s: ship lost %s fighters (%s remain); "
                "planet lost %s fighters (%s remain); citadel shields absorbed %s (%s remain)%s."
                % (item.get("result", "engaged"), item.get("attacker_fighters_lost", "unknown"),
                   item.get("attacker_fighters_remaining", "unknown"),
                   item.get("planet_fighters_lost", "unknown"),
                   item.get("planet_fighters_remaining", "unknown"),
                   item.get("citadel_shields_absorbed", "unknown"),
                   item.get("citadel_shields_remaining", "unknown"),
                   "; ship destroyed" if item.get("destroyed") is True else "")
            )
    if details.get("ship_remains_in_sector") is False:
        lines.append("  The server reports that your ship did not remain in the sector.")
    lines.append(f"  {closing_message}")
    return lines


@register("combat_attack_planet_flow")
def combat_attack_planet_flow(ctx: Context):
    """Attack only an explicitly listed current-sector planet, with server outcome authoritative."""
    sector = ctx.last_sector_desc if isinstance(ctx.last_sector_desc, dict) else {}
    planets = sector.get("planets", [])
    listed = [
        p for p in planets
        if isinstance(p, dict)
        and _authoritative_positive_integer_id(p.get("id", p.get("planet_id"))) is not None
    ]
    if not listed:
        print("No server-listed planets are available in the current sector.")
        return
    print("Current-sector planets:")
    for index, planet in enumerate(listed, 1):
        print(f"  {index}. {planet.get('name', 'Unnamed')} [ID: {_authoritative_positive_integer_id(planet.get('id', planet.get('planet_id')))}]")
    raw = input("Attack planet number or listed ID: ").strip()
    if not raw:
        print("Cancelled.")
        return
    try:
        selected = int(raw)
    except ValueError:
        print("Invalid selection.")
        return
    planet = listed[selected - 1] if 1 <= selected <= len(listed) else next(
        (p for p in listed
         if _authoritative_positive_integer_id(p.get("id", p.get("planet_id"))) == selected), None
    )
    if planet is None:
        print("Choose a planet listed in the current sector; no request was sent.")
        return
    planet_id = _authoritative_positive_integer_id(planet.get("id", planet.get("planet_id")))
    if planet_id is None:
        print("The selected planet did not contain a valid authoritative server ID; no request was sent.")
        return
    if input(f"Attack {planet.get('name', 'this planet')} (ID {planet_id})? This can destroy fighters and change ownership. (y/N): ").strip().lower() != "y":
        print("Cancelled.")
        return
    try:
        response = ctx.conn.rpc("combat.attack_planet", {"planet_id": planet_id})
    except (ConnectionError, OSError) as exc:
        ctx.state["last_rpc"] = None
        print(f"Planet attack outcome is uncertain after connection loss: {exc}")
        print("Reconnect before relying on planet ownership or fighter state.")
        _refresh_after_planet_attack(ctx)
        return
    ctx.state["last_rpc"] = response
    if not isinstance(response, dict):
        print("Planet attack result was not displayed because the server returned an invalid response; refreshing confirmed state.")
        _refresh_after_planet_attack(ctx)
        return
    if response.get("status") != "ok":
        error = response.get("error", {}) if isinstance(response, dict) else {}
        print(f"Planet attack refused: {error.get('message', 'The server did not accept the attack.')}")
        for line in _planet_landing_outcome_lines(
            response, "Planet attack was refused; no planetary attack was confirmed."
        ):
            print(line)
        # A refusal can still contain an authoritative planetary-defense
        # outcome: the server may have depleted fighters/shields or destroyed
        # the ship before denying the attack. Reconcile every affected domain
        # before allowing another action; never leave the pre-combat snapshot
        # as the apparent state.
        _refresh_after_planet_attack(ctx)
        return
    if not _planet_attack_response_type_matches(response):
        print("Planet attack result was not displayed because the server returned an unexpected response type; refreshing confirmed state.")
        _refresh_after_planet_attack(ctx)
        return
    data = get_data(response)
    reported_planet_id = data.get("planet_id") if isinstance(data, dict) else None
    if not _planet_attack_result_is_authoritative(data, planet_id):
        print("Planet attack result was not displayed because the server returned no matching planet ID; refreshing confirmed state.")
        _refresh_after_planet_attack(ctx)
        return
    print("Authoritative planet attack result:")
    print(f"  Planet ID: {data.get('planet_id', planet_id)}")
    print(f"  Captured: {data.get('captured', 'not reported')}")
    print(f"  Attacker fighters remaining: {data.get('attacker_remaining_fighters', 'not reported')}")
    print(f"  Planet fighters remaining: {data.get('defender_remaining_fighters', 'not reported')}")
    _refresh_after_planet_attack(ctx)


def _ferengi_list_data(response: dict) -> Optional[dict]:
    if not isinstance(response, dict) or response.get("status") != "ok":
        return None
    data = get_data(response)
    if not isinstance(data, dict):
        return None
    traders = data.get("traders", [])
    offers = data.get("offers", [])
    if not isinstance(traders, list) or not isinstance(offers, list):
        return None
    if any(not isinstance(trader, dict) for trader in traders):
        return None
    if any(not isinstance(offer, dict) or type(offer.get("deal_id")) is not int
           or offer["deal_id"] <= 0 for offer in offers):
        return None
    return data


def _print_ferengi_traders(data: dict) -> None:
    sector_id = data.get("sector_id", "unavailable")
    traders = data.get("traders", [])
    offers = data.get("offers", [])
    print(f"Ferengi traders in Sector {sector_id}:")
    if traders:
        for trader in traders:
            if not isinstance(trader, dict):
                continue
            print("  {name} · {code} · trader {trader_id} · reputation {reputation}".format(
                name=trader.get("display_name", "Unnamed trader"),
                code=trader.get("trader_code", "unknown"),
                trader_id=trader.get("trader_id", "unknown"),
                reputation=trader.get("reputation", "unknown"),
            ))
    else:
        print("  No active Ferengi traders are present in this sector.")
    print("Open offers:")
    if not offers:
        print("  No open offers.")
        return
    for index, offer in enumerate(offers, 1):
        if not isinstance(offer, dict):
            continue
        side = {"trader_sells": "you buy", "trader_buys": "you sell"}.get(
            offer.get("side"), "direction unavailable")
        print("  {index}. Deal {deal_id}: {name} · {side} {quantity} {commodity} at {price} CR each · trader sector {sector} · expires {expires}".format(
            index=index,
            deal_id=offer.get("deal_id", "unknown"),
            name=offer.get("display_name", "Ferengi trader"),
            side=side,
            quantity=offer.get("quantity", "?"),
            commodity=offer.get("commodity", "unknown"),
            price=offer.get("unit_price", "?"),
            sector=offer.get("trader_sector_id", "unknown"),
            expires=offer.get("expires_at", "unknown"),
        ))


@register("ferengi_traders_flow")
def ferengi_traders_flow(ctx: Context):
    """List current-sector Ferengi and durable open offers, with explicit deal actions."""
    response = ctx.conn.rpc("ferengi.traders", {})
    ctx.state["last_rpc"] = response
    data = _ferengi_list_data(response)
    if data is None:
        error = response.get("error", {}) if isinstance(response, dict) else {}
        message = error.get("message", "The server returned an invalid trader listing.") if isinstance(error, dict) else "The server returned an invalid trader listing."
        print(f"[Error] Ferengi trader list unavailable: {message}")
        return
    _print_ferengi_traders(data)
    offers = data.get("offers", [])
    if not offers:
        return
    choice = input("Deal number to act on, or Enter to return: ").strip()
    if not choice:
        return
    try:
        index = int(choice) - 1
    except ValueError:
        print("Invalid offer selection; no deal action was sent.")
        return
    if index < 0 or index >= len(offers):
        print("Choose an offer number from the list; no deal action was sent.")
        return
    action = input("Accept or reject this offer? [a/r, Enter cancels]: ").strip().lower()
    if action not in ("a", "r"):
        print("Deal action cancelled.")
        return
    command = "ferengi.deal.accept" if action == "a" else "ferengi.deal.reject"
    deal_id = int(offers[index]["deal_id"])
    result = ctx.conn.rpc(command, {"deal_id": deal_id})
    ctx.state["last_rpc"] = result
    if not isinstance(result, dict) or result.get("status") != "ok":
        error = result.get("error", {}) if isinstance(result, dict) else {}
        message = error.get("message", "The server refused the deal.") if isinstance(error, dict) else "The server refused the deal."
        print(f"[Error] Deal {deal_id} {('acceptance' if action == 'a' else 'rejection')} refused: {message}")
        return
    receipt = get_data(result)
    if isinstance(receipt, dict):
        print("Deal {status}: {quantity} {commodity} at {price} CR each · reputation {reputation}.".format(
            status=receipt.get("status", "updated"),
            quantity=receipt.get("quantity", "?"),
            commodity=receipt.get("commodity", "unknown"),
            price=receipt.get("unit_price", "?"),
            reputation=receipt.get("reputation", "unknown"),
        ))
    if action == "a":
        player_response = _hud_rpc(ctx, "player.my_info", {})
        if isinstance(player_response, dict) and player_response.get("status") == "ok":
            ctx.player_info = get_data(player_response)
            _update_corp_context(ctx)
        _hud_rpc(ctx, "ship.status", {})
    refreshed = ctx.conn.rpc("ferengi.traders", {})
    refreshed_data = _ferengi_list_data(refreshed)
    if refreshed_data is None:
        print("Deal response was confirmed; trader list refresh failed and may be stale.")
    else:
        _print_ferengi_traders(refreshed_data)

@register("launch_flow")
def launch_flow(ctx: Context):
    try:
        r = ctx.conn.rpc("planet.launch", {})
    except (ConnectionError, OSError) as exc:
        # A lost reply cannot establish whether launch committed or whether
        # entry hazards were applied.  Preserve the last confirmed surface
        # state and require the normal reconnect refresh to reconcile it;
        # never close the surface or claim that the ship left locally.
        ctx.state["last_rpc"] = None
        print(f"Launch outcome is uncertain after connection loss: {exc}")
        print("Reconnect before attempting another launch so location and ship state can be refreshed.")
        return
    ctx.state["last_rpc"] = r
    # Hazard details are authoritative only when the envelope belongs to the
    # launch operation.  Do not render a plausible hazard report attached to
    # a success-shaped response from another command; the response-type
    # check below will reconcile the affected state instead.
    launch_response_is_typed = _response_type_matches(r, "planet.launch")
    if launch_response_is_typed:
        for hazard in present_move_hazards(r if isinstance(r, dict) else {}):
            print(hazard)
    launch_data = get_data(r) if isinstance(r, dict) else {}
    launch_data = launch_data if isinstance(launch_data, dict) else {}
    launch_confirmed = (
        isinstance(r, dict)
        and r.get("status") == "ok"
        and _response_type_matches(r, "planet.launch")
        # The authoritative C response identifies the destination sector;
        # there is no client-owned `in_space` protocol field.
        and type(launch_data.get("sector_id")) is int
        and launch_data.get("sector_id") > 0
    )
    if launch_confirmed:
        print("Launched from planet.")
        # Launch entry can apply authoritative hazards and changes the player's
        # location/ship state. Refresh every affected domain before rendering.
        # A post-launch refresh must identify the authenticated player before
        # its turns/credits are allowed to reconcile the hazard outcome.  A
        # well-formed player record for another account is not authoritative
        # for this session and must not make departure actionable.
        player_resp = _safe_landing_refresh(ctx, "player.my_info", {})
        player_refreshed = _landing_player_refresh_is_current(
            ctx, player_resp, require_identity=True
        )
        if player_refreshed:
            ctx.player_info = get_data(player_resp)
            _update_corp_context(ctx)
        ship_resp = _safe_landing_refresh(ctx, "ship.status", {})
        ship_record = _authoritative_hud_record(ship_resp, "ship")
        expected_ship_id = ctx.hud.ship_id
        actual_ship_id = (ship_record.get("id", ship_record.get("ship_id"))
                          if ship_record else None)
        ship_refreshed = (
            ship_record is not None
            # A post-hazard launch refresh must identify the server-owned ship
            # even when the pre-launch HUD had no ship ID.  Without this check,
            # a partial ship record could make the client close the confirmed
            # planet surface and present space controls for an unknown asset.
            and type(actual_ship_id) is int and actual_ship_id > 0
            and (expected_ship_id is None or actual_ship_id == expected_ship_id)
        )
        if not player_refreshed or not ship_refreshed:
            print("Launch confirmed, but the post-launch HUD refresh was incomplete; displayed state may be stale.")
            return
        # Do not close the surface or mark departure until the authoritative
        # sector read succeeds.  Entry hazards can commit before a follow-up
        # read is lost, so an otherwise successful launch envelope is not
        # enough to make the space-view controls actionable.
        if not _safe_landing_sector_refresh(ctx):
            print("Launch confirmed, but the current sector could not be refreshed; the surface remains available until reconnect.")
            return
        ctx.state["on_planet"] = False
        # Pop SURFACE menu only after the affected authoritative state is usable.
        if ctx.menu_stack[-1] == "SURFACE":
            ctx.menu_stack.pop()
    else:
        # An explicit terminal entry outcome means the confirmed planet surface
        # is no longer an actionable local context.  Do not keep presenting
        # surface controls while reconciling the authoritative replacement
        # state; only the server-supplied terminal fields authorize this
        # transition.
        # Terminal hazard fields are authoritative only when they came from
        # the launch response schema.  A success/refusal envelope carrying a
        # different command type must not destroy the confirmed surface or
        # otherwise authorize a local lifecycle transition.
        if launch_response_is_typed and _move_response_is_terminal(r):
            _invalidate_unconfirmed_landing_surface(ctx)
            print("Launch outcome is terminal; refreshing confirmed player, ship and sector state.")
            _safe_launch_refresh(ctx)
            return
        error = r.get("error", {}) if isinstance(r, dict) else {}
        if isinstance(r, dict) and r.get("status") == "ok":
            # The authoritative successful launch payload must identify the
            # destination sector. Do not close the surface menu or claim a
            # departure from an incomplete/ambiguous response.
            err = "The server did not confirm the launch destination sector."
        else:
            err = error.get("message", "Unknown error") if isinstance(error, dict) else "Unknown error"
        print(f"Launch failed: {err}")
        # A failed launch may follow entry-hazard processing or a lost server
        # transaction. Reconcile confirmed state without changing the local
        # landed context unless the server confirms success.
        # A refusal may still include authoritative entry-hazard data, but a
        # connection loss during reconciliation must not turn that refusal
        # into a traceback or locally close the confirmed surface.  Keep the
        # surface context until the normal reconnect refresh can complete.
        _safe_launch_refresh(ctx)

@register("planet_info_flow")
def planet_info_flow(ctx: Context):
    planet_overview_flow(ctx)

@register("colonist_transfer_flow")
def colonist_transfer_flow(ctx: Context):
    """
    Transfer colonists to/from a planet (when landed).
    """
    # When called from planet surface, we should have the planet ID from landing
    # It gets set in the response from planet.land or planet.info
    
    # Prefer the locally confirmed landing identity. A prior response may be
    # stale or malformed, so only accept server-shaped positive integer IDs.
    planet_id = _planet_id(ctx)
    if planet_id is None:
        print("Could not determine a confirmed planet ID. Refresh or land again before transporting colonists.")
        return

    # Get current state of colonists on planet and ship
    resp_get = ctx.conn.rpc("planet.colonists.get", {"planet_id": planet_id})
    ctx.state["last_rpc"] = resp_get
    
    if resp_get.get("status") != "ok":
        error = resp_get.get('error')
        if isinstance(error, dict):
            error_msg = error.get('message', str(error))
        else:
            error_msg = str(error) if error else 'Unknown error'
        print(f"Error: {error_msg}")
        return

    data = resp_get.get("data", {})
    planet_colonists = data.get("planet_colonists", 0)
    ship_colonists = data.get("ship_colonists", 0)
    ship_holds_available = data.get("ship_holds_available", 0)

    print(f"\nColonists on planet {planet_id}:")
    print(f"  Planet: {planet_colonists} unassigned")
    print(f"  Ship: {ship_colonists} onboard")
    print(f"  Ship holds available: {ship_holds_available}\n")

    # Prompt for action
    print("(P)ickup from planet / (D)ropoff to planet / (Q)uit")
    action_input = input("Action? ").strip().upper()

    if action_input == "Q":
        return
    elif action_input == "P":
        action = "pickup"
        max_qty = min(planet_colonists, ship_holds_available)
        print(f"Maximum available: {max_qty} colonists")
    elif action_input == "D":
        action = "dropoff"
        max_qty = ship_colonists
        print(f"Maximum available: {max_qty} colonists")
    else:
        print("Invalid action.")
        return

    if max_qty <= 0:
        print("Cannot transfer: no colonists available.")
        return

    try:
        qty = int(input(f"How many? (1-{max_qty}): "))
        if qty < 1 or qty > max_qty:
            print("Invalid quantity.")
            return
    except ValueError:
        print("Invalid input.")
        return

    # Perform transfer
    resp_set = ctx.conn.rpc("planet.colonists.set", {
        "planet_id": planet_id,
        "action": action,
        "quantity": qty
    })
    ctx.state["last_rpc"] = resp_set

    if resp_set.get("status") == "ok":
        data = resp_set.get("data", {})
        print(f"\n✓ {action.capitalize()} successful!")
        print(f"  Transferred: {data.get('quantity')} colonists")
        print(f"  Ship colonists now: {data.get('ship_colonists')}")
        _refresh_planet_overview(ctx, int(planet_id))
    else:
        print(f"\n✗ Transfer failed: {resp_set.get('error', 'Unknown error')}")

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
@register("shipyard_sell_flow")
def shipyard_sell_flow(ctx: Context, selected_ship_id: Optional[int] = None):
    """Sell an owned hull after explicitly selecting its replacement."""
    active_ship_id = get_my_ship_id(ctx.conn, ctx)
    ship_id = selected_ship_id if selected_ship_id is not None else active_ship_id
    if type(ship_id) is not int or ship_id <= 0 or active_ship_id is None:
        print("Shipyard sale requires an authoritative active-ship ID; no request was sent.")
        return
    try:
        roster = ctx.conn.rpc("ship.list", {})
    except (ConnectionError, OSError) as exc:
        print(f"Shipyard sale cannot choose a replacement without the current ship roster: {exc}")
        return
    roster_data = roster.get("data") if isinstance(roster, dict) else None
    if (not isinstance(roster, dict) or roster.get("status") != "ok"
            or roster.get("type") != "ship.list.response"
            or not isinstance(roster_data, dict)
            or not isinstance(roster_data.get("ships"), list)):
        print("Shipyard sale requires a valid server-authoritative ship roster; no sale request was sent.")
        return
    replacements = [item for item in roster_data["ships"]
                    if isinstance(item, dict) and type(item.get("ship_id")) is int
                    and item["ship_id"] > 0 and item["ship_id"] != ship_id]
    if not replacements:
        print("Your last ship cannot be sold; no eligible replacement ship is owned.")
        return
    print("Eligible owned replacement ships:")
    for item in replacements:
        print(f"  {item['ship_id']}: {item.get('name', 'Unnamed ship')}")
    replacement_text = input("Replacement ship ID: ").strip()
    try:
        replacement_ship_id = int(replacement_text)
    except ValueError:
        print("A numeric replacement ship ID is required; no sale request was sent.")
        return
    if not any(item["ship_id"] == replacement_ship_id for item in replacements):
        print("Select an owned replacement ship from the list; no sale request was sent.")
        return
    if input(f"Sell ship {ship_id} at the shipyard? [y/N]: ").strip().lower() != "y":
        print("Shipyard sale cancelled.")
        return
    try:
        response = ctx.conn.rpc("shipyard.sell", {
            "ship_id": ship_id,
            "replacement_ship_id": replacement_ship_id,
        })
    except (ConnectionError, OSError) as exc:
        ctx.state["last_rpc"] = None
        print(f"Shipyard sale outcome is uncertain after connection loss: {exc}")
        return
    ctx.state["last_rpc"] = response
    if not isinstance(response, dict) or response.get("status") != "ok":
        # The server remains authoritative for unavailable/eligibility reasons;
        # do not replace its error with a client-side capability claim.
        message = response.get("error", {}).get("message", "server refusal") if isinstance(response, dict) else "invalid response"
        print(f"Shipyard sale refused or failed: {message}")
        _hud_rpc(ctx, "player.my_info", {})
        _refresh_ship_after_move(ctx)
        _refresh_sector_after_shipyard(ctx)
        return
    receipt = response.get("data")
    if (response.get("type") != "ship.sold_v1" or not isinstance(receipt, dict)
            or receipt.get("ship_id") != ship_id
            or receipt.get("replacement_ship_id") != replacement_ship_id
            or type(receipt.get("sale_proceeds")) is not int):
        print("Sale response did not match the published receipt; reconciling authoritative state.")
    else:
        print(f"Ship sale confirmed: {present_money(receipt['sale_proceeds'])} credits received.")
        print(f"  Cargo forfeited: {bool(receipt.get('cargo_forfeited'))}; "
              f"insurance terminated: {bool(receipt.get('insurance_terminated'))}.")
    _hud_rpc(ctx, "player.my_info", {})
    _refresh_ship_after_move(ctx)
    _refresh_sector_after_shipyard(ctx)


@register("ship_repair_flow")
def ship_repair_flow(ctx: Context):
    """Repair the active ship using the typed server receipt and refresh state."""
    response = ctx.conn.rpc("ship.repair", {})
    ctx.state["last_rpc"] = response
    if not isinstance(response, dict) or response.get("status") != "ok":
        message = response.get("error", {}).get("message", "server refusal") if isinstance(response, dict) else "invalid response"
        print(f"Ship repair refused or failed: {message}")
        # A refusal can race a stale hull/credit snapshot (for example after
        # another shipyard action).  Reconcile the authoritative domains
        # before the next lifecycle action; never leave the refused request's
        # preflight state actionable as if it were current.
        _hud_rpc(ctx, "player.my_info", {})
        _refresh_ship_after_move(ctx)
        return
    if response.get("type") != "ship.repair":
        print("Ship repair was not confirmed: the server returned an unexpected response type.")
        _refresh_ship_roster_state(ctx)
        return
    data = response.get("data")
    if not isinstance(data, dict) or type(data.get("repaired")) is not bool \
            or type(data.get("cost")) is not int or data.get("cost") < 0 \
            or type(data.get("hull")) is not int or not 0 <= data.get("hull") <= 100:
        print("Ship repair was not confirmed: the server returned an invalid repair receipt.")
        _refresh_ship_roster_state(ctx)
        return
    print(f"Ship repair confirmed by server: hull {data['hull']} (cost {data['cost']} credits).")
    player = _hud_rpc(ctx, "player.my_info", {})
    ship = _refresh_ship_after_move(ctx)
    if player.get("status") != "ok" or not _refresh_response_type_matches(ship, "ship.status"):
        print("Warning: repair succeeded, but authoritative credits or ship state could not be refreshed.")


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

    # `shipyard.list_v1` is the authoritative catalogue contract.  A generic
    # successful envelope must not become an upgrade choice: it may be a
    # response from another command or an older incompatible server.
    if list_resp.get("type") != "shipyard.list_v1":
        print("[Error] Could not retrieve shipyard list: the server returned an unexpected response type.")
        return

    data = list_resp.get("data")
    if not isinstance(data, dict):
        print("[Error] Invalid shipyard list data.")
        _pp(list_resp)
        return

    available_ships = data.get("available", [])
    if not isinstance(available_ships, list):
        print("[Error] Invalid shipyard list data: the server returned no authoritative hull catalogue.")
        return
    for ship in available_ships:
        if not isinstance(ship, dict) or type(ship.get("type_id")) is not int or ship["type_id"] <= 0:
            print("[Error] Invalid shipyard list data: a hull omitted its authoritative type ID.")
            return
    if not available_ships:
        print("No ships available for upgrade at this shipyard.")
        return

    print("\n--- Available Ships for Upgrade ---")
    for i, ship in enumerate(available_ships):
        eligible_status = "Eligible" if ship.get("eligible") else "Ineligible"
        reasons = f" ({', '.join(ship.get('reasons'))})" if ship.get("reasons") else ""
        print(f"  {i+1}: {ship.get('name')} (Type ID: {ship.get('type_id')}) - Net before tax: {ship.get('net_cost')} cr - {eligible_status}{reasons}")

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
    new_ship_name = input(f"Enter new name for your {selected_ship.get('name')}: ").strip()
    if not new_ship_name:
        print("New ship name cannot be empty. Cancelled.")
        return

    print("The listed net cost excludes tax. The server recalculates the final charge when you exchange hulls.")
    if input(f"Exchange your current hull for {selected_ship.get('name')} named {new_ship_name}? [y/N]: ").strip().lower() != "y":
        print("Ship upgrade cancelled; no exchange was sent.")
        return

    payload = {
        "new_type_id": new_ship_type_id,
        "new_ship_name": new_ship_name
    }

    upgrade_resp = ctx.conn.rpc("shipyard.upgrade", payload)
    ctx.state["last_rpc"] = upgrade_resp

    if upgrade_resp.get("status") == "ok":
        receipt = upgrade_resp.get("data")
        requested_type_id = new_ship_type_id
        receipt_valid = (
            upgrade_resp.get("type") == "shipyard.upgraded_v1"
            and isinstance(receipt, dict)
            and type(receipt.get("ship_id")) is int
            and receipt["ship_id"] > 0
            and receipt.get("new_type_id") == requested_type_id
            and receipt.get("new_ship_name") == new_ship_name
            and type(receipt.get("credits_spent")) is int
            and receipt["credits_spent"] >= 0
        )
        if not receipt_valid:
            print("[Warning] Ship upgrade was not confirmed: the server returned an invalid shipyard receipt.")
            # A success-shaped but untyped/mismatched response cannot update
            # client state. Reconcile the domains affected by the authoritative
            # exchange before allowing another lifecycle action.
            _hud_rpc(ctx, "player.my_info", {})
            _hud_rpc(ctx, "ship.status", {})
            if not _refresh_sector_after_shipyard(ctx):
                print("[Warning] Could not refresh sector after the ambiguous shipyard response; displayed sector may be stale.")
            return
        ctx.state.pop("towing_ship_id", None)
        print(f"Successfully upgraded to {new_ship_name}!")
        _pp(upgrade_resp)
        # The receipt contains the amount spent, not the resulting balances or ship.
        my_info_resp = _hud_rpc(ctx, "player.my_info", {})
        if my_info_resp.get("status") == "ok":
            ctx.player_info = get_data(my_info_resp)
        else:
            print("[Warning] Could not refresh player info after ship upgrade; displayed credits may be stale.")
        ship_resp = _hud_rpc(ctx, "ship.status", {})
        if ship_resp.get("status") != "ok":
            print("[Warning] Could not refresh ship status after ship upgrade; displayed ship data may be stale.")
        if not _refresh_sector_after_shipyard(ctx):
            print("[Warning] Could not refresh sector after ship upgrade; displayed sector may be stale.")
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
    """Compatibility menu alias for the authoritative shipyard sale flow."""
    shipyard_sell_flow(ctx)





@register("jettison_cargo_flow")
def jettison_cargo_flow(ctx: Context):
    # Fetch fresh ship info to ensure cargo is up-to-date
    r = _hud_rpc(ctx, "ship.info", {})
    if r.get("status") != "ok":
        print("Could not fetch ship info.")
        _pp(r)
        return
    
    ship_data = (r.get("data") or {}).get("ship") or {}
    commodities = ship_data.get("cargo") or []

    if not commodities:
        print("Your ship has no cargo to jettison.")
        return

    print("\n--- Current Cargo ---")
    for item in commodities:
        print(f"  {item.get('commodity').capitalize()}: {item.get('quantity')}")
    print("---------------------")

    commodity_input = input("Enter commodity code to jettison (ORE, ORG, EQU, COL): ").strip().upper()
    if not commodity_input:
        print("Cancelled.")
        return

    code_map = {
        "ORE": "ORE",
        "ORG": "ORG",
        "EQU": "EQU",
        "ORGANICS": "ORG",
        "EQUIPMENT": "EQU",
        "COLONISTS": "COLONISTS",
        "COL": "COLONISTS"
    }
    commodity = code_map.get(commodity_input)

    if not commodity:
        print("Invalid commodity. Please use ORE, ORG, EQU, or COL.")
        return

    try:
        quantity_to_jettison = int(input(f"Quantity of {commodity} to jettison: ").strip())
        if quantity_to_jettison <= 0:
            print("Quantity must be positive.")
            return
    except ValueError:
        print("Invalid quantity.")
        return

    available = None
    for item in commodities:
        if not isinstance(item, dict):
            continue
        cargo_name = str(item.get("commodity", "")).strip().upper()
        cargo_code = code_map.get(cargo_name, cargo_name)
        cargo_quantity = item.get("quantity")
        if cargo_code == commodity and isinstance(cargo_quantity, int) and not isinstance(cargo_quantity, bool):
            available = cargo_quantity
            break
    if available is None:
        print(f"The latest ship status does not report {commodity} cargo; no jettison was sent.")
        return
    if quantity_to_jettison > available:
        print(f"You have only {available} units of {commodity}; no jettison was sent.")
        return
    if not _trade_confirmation(
        f"Permanently discard {quantity_to_jettison} units of {commodity} and spend 1 turn?"
    ):
        print("Jettison cancelled; no cargo or turn was spent.")
        return

    payload = {
        "commodity": commodity,
        "quantity": quantity_to_jettison
    }

    resp = ctx.conn.rpc("ship.jettison", payload)
    ctx.state["last_rpc"] = resp
    if resp.get("status") == "ok":
        print(f"Successfully jettisoned {quantity_to_jettison} units of {commodity}.")
        # Refresh both player and ship views so displayed credits and cargo
        # come from the authoritative post-mutation state.
        my_info_resp = _hud_rpc(ctx, "player.my_info", {})
        if my_info_resp.get("status") == "ok":
            ctx.player_info = get_data(my_info_resp)
        else:
            print("[Warning] Could not refresh player info after jettison.")
        ship_resp = _hud_rpc(ctx, "ship.status", {})
        if ship_resp.get("status") != "ok":
            print("[Warning] Could not refresh ship cargo after jettison.")
    else:
        print(f"[Error] Failed to jettison cargo: {resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(resp)

@register("genesis_torpedo_flow")
def genesis_torpedo_flow(ctx: Context):
    # Check if we have a genesis torpedo? 
    # Protocol doesn't explicitly check item ownership in client usually, server handles it.
    # But we can check ctx.player_info or ship info if we want to be nice.
    # For now, let's just try to fire it.

    sector_id = ctx.current_sector_id
    if not sector_id:
        print("Cannot determine current sector.")
        return

    print(f"\n--- Genesis Torpedo Sequence ---")
    print(f"Target Sector: {sector_id}")
    
    planet_name = input("Enter name for the new planet: ").strip()
    if not planet_name:
        print("Aborted.")
        return

    # Check for confirmation
    confirm = input(f"Create planet '{planet_name}' in sector {sector_id}? [y/N] ").strip().lower()
    if confirm != 'y':
        print("Aborted.")
        return

    payload = {
        "sector_id": sector_id,
        "name": planet_name,
        "owner_entity_type": "player",
        "idempotency_key": str(uuid.uuid4()),
    }

    print("Firing Genesis Torpedo...")
    resp = ctx.conn.rpc("planet.genesis_create", payload)
    ctx.state["last_rpc"] = resp

    if resp.get("status") == "ok":
        print(f"Genesis Torpedo successful! Planet '{planet_name}' created.")
        _pp(resp)
        # Genesis consumes ship equipment and changes the sector.
        ship_resp = _hud_rpc(ctx, "ship.status", {})
        if ship_resp.get("status") != "ok":
            print("[Warning] Could not refresh ship status; displayed equipment may be stale.")
        new_sector = ctx.conn.rpc("move.describe_sector", {"sector_id": sector_id})
        if new_sector.get("status") == "ok" and isinstance(new_sector.get("data"), dict):
            ctx.last_sector_desc = normalize_sector(get_data(new_sector))
        else:
            print("[Warning] Could not refresh the sector; displayed sector may be stale.")
    else:
        print(f"[Error] Genesis failed: {resp.get('error', {}).get('message', 'Unknown error')}")
        _pp(resp)


@register("rename_ship_flow")
def rename_ship_flow(ctx: Context):
    """Rename the active ship using the authoritative V3 contract."""
    new_name = input("New ship name (1-100 characters, Enter to cancel): ").strip()
    if not new_name:
        print("Ship rename cancelled.")
        return
    if len(new_name) > 100:
        print("Ship name must be 1 to 100 characters.")
        return

    response = ctx.conn.rpc("ship.rename", {"name": new_name})
    ctx.state["last_rpc"] = response
    if not isinstance(response, dict) or response.get("status") != "ok":
        print("Ship rename refused or failed:")
        _pp(response)
        return

    # `ship.renamed` is the sole success contract for this mutation.  An
    # untyped success envelope can belong to another command and must not be
    # allowed to authorize a local rename confirmation.
    if response.get("type") != "ship.renamed":
        print("Ship rename returned an unexpected response type; refreshing ship state.")
        refreshed = _hud_rpc(ctx, "ship.status", {})
        if refreshed.get("status") != "ok":
            print("Warning: ship status could not be refreshed after the ambiguous rename response.")
        return

    data = get_data(response)
    confirmed_ship_id = data.get("ship_id") if isinstance(data, dict) else None
    expected_ship_id = ctx.hud.ship_id
    if (type(confirmed_ship_id) is not int or confirmed_ship_id <= 0
            or (expected_ship_id is not None and confirmed_ship_id != expected_ship_id)):
        # The rename handler returns the affected ship ID.  Do not present a
        # successful name change when that identity is absent or belongs to a
        # different active ship; reconcile the HUD before allowing another
        # ship action.
        print("Ship rename returned no authoritative active-ship identity; refreshing ship state.")
        refreshed = _hud_rpc(ctx, "ship.status", {})
        if refreshed.get("status") != "ok":
            print("Warning: ship status could not be refreshed after the ambiguous rename response.")
        return
    confirmed_name = data.get("name") if isinstance(data, dict) else None
    if not isinstance(confirmed_name, str) or not confirmed_name.strip():
        # A successful envelope without the server's authoritative name is
        # not enough to claim that the rename took effect.  Reconcile the
        # active ship, but never echo the locally requested value as a
        # fabricated confirmation.
        print("Ship rename returned no authoritative ship name; refreshing ship state.")
        refreshed = _hud_rpc(ctx, "ship.status", {})
        if refreshed.get("status") != "ok":
            print("Warning: ship status could not be refreshed after the ambiguous rename response.")
        return
    print(f"Ship renamed to '{confirmed_name}' (confirmed by server).")
    refreshed = _hud_rpc(ctx, "ship.status", {})
    if refreshed.get("status") != "ok":
        print("Warning: the rename succeeded, but ship status could not be refreshed.")


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
    """Keep the legacy menu entry on the canonical quoted buy workflow.

    This entry used to send ``trade.buy`` directly, bypassing the server
    quote, fixed port direction, receipt correlation, and authoritative
    post-settlement refresh. It remains registered for menu compatibility,
    but no longer creates a second trade protocol path.
    """
    dock_trade_flow(ctx, "buy")

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
def run_session(ctx: Context) -> int:
    """
    Run the interactive menu loop until the player quits or the connection
    is lost.

    Contract:
      * Returns EXIT_OK for a normal quit. Menu quit handlers call
        `sys.exit(0)` directly (unchanged, existing behaviour); this
        function catches that `SystemExit` and translates it into a plain
        return so callers/tests have one predictable, non-exception return
        value instead of having to handle SystemExit themselves.
      * Returns EXIT_CONNECTION_LOST if a `ConnectionError` propagates from
        the transport layer once the session is under way (after login).
        `protocol.Conn` is solely responsible for normalising socket-level
        failures (OSError variants, EOF, etc.) into `ConnectionError`; this
        function does not — and must not — interpret those variants itself.
      * Prints exactly one concise, player-facing message on disconnect and
        never lets a traceback reach the terminal. It does not drain/print
        the queued "connection.lost" event first — that event remains
        available, unread, for later inspection (e.g. Comms > Events, if a
        future session reconnects) rather than being duplicated here as a
        second message.
    """
    try:
        while True:
            render_menu(ctx)
            choice = read_choice()
            new_events = ctx.drain_events()
            if new_events:
                print(f"({new_events} new event{'s' if new_events != 1 else ''}. See Comms > Events.)")
                # A destruction event is only a server-owned aftermath
                # trigger. Reconcile before accepting another gameplay action.
                _reconcile_destruction_events(ctx)
            handle_choice(ctx, choice)
    except SystemExit as exc:
        code = exc.code
        return EXIT_OK if code in (None, 0) else code
    except ConnectionError:
        print("Connection to the server was lost.")
        return EXIT_CONNECTION_LOST


# ---------------------------
def main():
    args = parse_args()

    menus = load_menus(args.menus)

    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.connect((args.host, args.port))
            conn = Conn(s, debug=args.debug)

            user = args.user if args.user else input("Player Name: ")

            # optional hello
            capabilities = {}
            try:
                hello_resp = conn.rpc("system.hello", {"client": "twclone-cli", "client_version": "3.x"})
                if hello_resp.get("status") == "ok":
                    capabilities = hello_resp.get("data", {}).get("capabilities") or {}
            except Exception:
                pass

            if args.register:
                if args.passwd:
                    print("Registration passwords must be entered at the hidden prompt; do not pass --passwd.")
                    return EXIT_LOGIN_FAILED
                passwd = getpass.getpass("New password: ")
                confirmation = getpass.getpass("Confirm new password: ")
                try:
                    validate_registration_passwords(passwd, confirmation)
                except ValueError as exc:
                    print(f"Registration cancelled: {exc}")
                    return EXIT_LOGIN_FAILED
                ship_name = input("Ship name (optional): ").strip()
                try:
                    login = register_account(conn, user, passwd, ship_name)
                except ValueError as exc:
                    print(f"Registration cancelled: {exc}")
                    return EXIT_LOGIN_FAILED
                finally:
                    passwd = ""
                    confirmation = ""
                if login.get("status") != "ok":
                    code = (login.get("error") or {}).get("code")
                    if code == 1105:
                        print("Registration refused: that captain name is already in use.")
                    else:
                        print("Registration refused. Check the captain name and try again.")
                    return EXIT_LOGIN_FAILED
                if not conn.session_token:
                    print("Registration completed, but the server did not provide a login session.")
                    return EXIT_LOGIN_FAILED
                print("Account created. Connecting to your new captain.")
            else:
                passwd = args.passwd if args.passwd else getpass.getpass("Password: ")
                login = authenticate(conn, user, passwd)
                passwd = ""

            if login.get("status") in ("error","refused"):
                print("Login failed.");
                return EXIT_LOGIN_FAILED

            # If hello failed or didn't have caps, try explicit capabilities command
            if not capabilities:
                try:
                    cap_resp = conn.rpc("system.capabilities", {})
                    if cap_resp.get("status") == "ok":
                        capabilities = cap_resp.get("data") or {}
                except Exception:
                    pass

            current = extract_current_sector(login)
            my_info_resp = None
            if not isinstance(current, int):
                my_info_resp = conn.rpc("player.my_info", {})
                current = extract_current_sector(my_info_resp) or 1

            # describe sector and normalize
            desc = conn.rpc("move.describe_sector", {"sector_id": int(current)})
            norm = normalize_sector(get_data(desc))

            ctx = Context(conn=conn, menus=menus, last_sector_desc=norm, capabilities=capabilities)
            ctx.state["cli"] = {"host": args.host, "port": args.port, "user": user, "debug": bool(args.debug)}
            ctx.state["debug"] = bool(args.debug)
            initial_player = _authoritative_hud_record(my_info_resp, "player")
            if initial_player is not None and type(initial_player.get("id")) is int and initial_player["id"] > 0:
                ctx.state["player_id"] = initial_player["id"]

            # Authoritative HUD hydration: only confirmed, non-consuming
            # handlers are used (see hud.py module docstring for provenance
            # of every field, including the login-only unread-news snapshot).
            if my_info_resp is None:
                my_info_resp = conn.rpc("player.my_info", {})
            ship_resp = conn.rpc("ship.status", {})
            mail_resp = None
            notice_resp = None
            try:
                mail_resp = conn.rpc("mail.inbox", {"limit": 20})
            except Exception:
                pass
            try:
                notice_resp = conn.rpc("notice.list", {})
            except Exception:
                pass
            ctx.hud = hud_module.hydrate_login(
                ctx.hud, login,
                my_info_resp=my_info_resp, ship_resp=ship_resp, sector_resp=norm,
                mail_resp=mail_resp, notice_resp=notice_resp,
            )

            # Initial update of corporation context after login
            _update_corp_context(ctx)

            # Re-read the server-persisted route after every login/reconnect.
            # A saved route is displayed for review; movement remains an
            # explicit player action in the normal warp-by-warp workflow.
            route_status = refresh_autopilot_status(ctx, announce=True)
            if route_status is None:
                print("Saved autopilot status is unavailable; navigation remains manual.")

            # First render sector header once
            call_handler("redisplay_sector", ctx)

            return run_session(ctx)

    except ConnectionRefusedError:
        print(f"Couldn’t connect to {args.host}:{args.port}. Is the server running?")
        return EXIT_CONNECTION_REFUSED

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
    Density-only scan: calls the published sector.scan.density endpoint.
    Supports both dict payload and flat [sector_id, density, ...] arrays.
    """
    sector_id = _authoritative_positive_integer_id(ctx.current_sector_id)
    if sector_id is None:
        # The published request schema requires a positive server-owned sector
        # ID. Do not send an empty, string, boolean, or float-derived request
        # and let the server interpret it as a visibility probe.
        print("Density scan unavailable: the current sector has no authoritative ID; reconnect and refresh first.")
        return
    payload = {"sector_id": sector_id}
    try:
        resp = ctx.conn.rpc("sector.scan.density", payload)
    except (ConnectionError, OSError) as exc:
        # A lost read reply is not evidence that density was revealed. Keep
        # the last confirmed location intact and require reconciliation after
        # reconnect instead of presenting a traceback or guessed result.
        ctx.state["last_rpc"] = None
        print(f"Density scan outcome is uncertain after connection loss: {exc}")
        print("Reconnect before relying on density information; the confirmed sector was not changed.")
        return
    ctx.state["last_rpc"] = resp

    print("\n=== Density Scan ===")
    if not isinstance(resp, dict) or resp.get("status") != "ok":
        raw_error = resp.get("error") if isinstance(resp, dict) else None
        error = raw_error if isinstance(raw_error, dict) else {}
        message = error.get("message", "The server did not return density information.")
        print(f"Density scan refused: {message}")
        _refresh_after_scan_refusal(ctx)
        return

    data = resp.get("data") if isinstance(resp, dict) else None

    if not _response_type_matches(resp, "sector.scan.density"):
        print("Density scan response had an unexpected type; cached state was not changed.")
        _refresh_after_scan_refusal(ctx)
        return

    # The server may wrap rows in a sectors object. Keep the response shape
    # authoritative instead of guessing a missing or remote sector identity.
    if isinstance(data, dict) and "sectors" in data:
        data = data["sectors"]

    # The release response may carry one or more authoritative row objects.
    # Validate every row before displaying any of them so a partially malformed
    # list cannot leak an untrusted density alongside otherwise valid rows.
    if (isinstance(data, list) and len(data) == 1
            and isinstance(data[0], dict)):
        data = data[0]
    elif isinstance(data, list) and data and all(isinstance(row, dict) for row in data):
        rows = []
        for row in data:
            row_sector_id = _authoritative_positive_integer_id(
                row.get("sector_id", row.get("id"))
            )
            row_density = row.get("density")
            if row_sector_id is None or not _authoritative_density_value(row_density):
                print("Density response contained an invalid authoritative row; cached state was not changed.")
                _refresh_after_scan_refusal(ctx)
                return
            rows.append((row_sector_id, row_density))
        print("Sector   Density")
        print("----------------")
        for row_sector_id, row_density in rows:
            print(f"{row_sector_id:>6}   {row_density:>7}")
        return

    # Case A: a location-bearing single-sector response. It may only be
    # displayed when it identifies the confirmed current sector; otherwise a
    # successful-looking payload could disclose or cache remote information.
    if isinstance(data, dict) and "density" in data:
        reported_sector_id = _authoritative_positive_integer_id(
            data.get("sector_id", data.get("id"))
        )
        confirmed_sector_id = _authoritative_positive_integer_id(sector_id)
        if confirmed_sector_id is None or reported_sector_id is None or reported_sector_id != confirmed_sector_id:
            print("Density response did not identify the confirmed current sector; cached state was not changed.")
            _refresh_after_scan_refusal(ctx)
            return
        dens = data.get("density")
        if not _authoritative_density_value(dens):
            print("Density response contained an invalid authoritative value; cached state was not changed.")
            _refresh_after_scan_refusal(ctx)
            return
        print(f"Sector: {reported_sector_id}")
        print(f"Density: {dens}")
    # Case B
    elif isinstance(data, list) and not data:
        # An empty authoritative row set is still a successful density result;
        # there is no location-bearing payload to merge or disclose.  Do not
        # turn a valid no-row response into a false visibility refusal or an
        # unnecessary current-sector refresh.
        print("No density values were returned by the server.")
    elif (isinstance(data, list) and len(data) > 0 and len(data) % 2 == 0
          and all(_authoritative_positive_integer_id(x) is not None
                  and _authoritative_density_value(y)
                  for x, y in zip(data[0::2], data[1::2]))):
        pairs = list(zip(data[0::2], data[1::2]))
        print("Sector   Density")
        print("----------------")
        for sid, dens in pairs:
            print(f"{sid:>6}   {dens:>7}")
    else:
        print("(unexpected response shape)")
        _refresh_after_scan_refusal(ctx)
