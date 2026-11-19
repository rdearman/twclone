#!/usr/bin/env python3
"""Drive the ordinary Python client's deployment menu against a live QA server.

Required environment: TWCLONE_PYTHON_PORT, TWCLONE_PYTHON_USERNAME,
TWCLONE_PYTHON_PASSWORD, and optionally TWCLONE_PYTHON_PLANET_ID (default 977790).
This connects only to loopback and does not use the JSON test runner protocol
as a substitute for the client workflow: it dispatches the actual DEPLOYMENT_MAIN
menu action and calls the production combat_attack_planet_flow handler.
"""
from __future__ import annotations

import contextlib
import io
import os
import socket
import sys
from pathlib import Path
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
CLIENT_DIR = ROOT / "client" / "python_client"
sys.path.insert(0, str(CLIENT_DIR))

import client  # noqa: E402
from protocol import Conn, extract_current_sector, get_data, normalize_sector  # noqa: E402
from state import Context  # noqa: E402


def main() -> int:
    port = int(os.environ["TWCLONE_PYTHON_PORT"])
    username = os.environ["TWCLONE_PYTHON_USERNAME"]
    password = os.environ["TWCLONE_PYTHON_PASSWORD"]
    planet_id = int(os.environ.get("TWCLONE_PYTHON_PLANET_ID", "977790"))
    sock = socket.create_connection(("127.0.0.1", port), timeout=10)
    conn = Conn(sock)
    transcript = io.StringIO()
    try:
        conn.rpc("system.hello", {"client": "twclone-cli", "client_version": "3.x"})
        login = client.authenticate(conn, username, password)
        if not isinstance(login, dict) or login.get("status") != "ok":
            raise AssertionError("ordinary Python client login failed")
        player_response = conn.rpc("player.my_info", {})
        player = get_data(player_response).get("player", {})
        if not isinstance(player, dict) or type(player.get("id")) is not int:
            raise AssertionError("Python client did not receive an authoritative player ID")
        sector_id = extract_current_sector(login)
        if type(sector_id) is not int:
            sector_id = extract_current_sector(player_response)
        if type(sector_id) is not int or sector_id <= 0:
            raise AssertionError("Python client did not receive an authoritative current sector")
        sector_response = conn.rpc("move.describe_sector", {"sector_id": sector_id})
        if sector_response.get("status") != "ok" or sector_response.get("type") != "sector.info":
            raise AssertionError("Python client sector refresh did not return sector.info")
        sector = normalize_sector(get_data(sector_response))
        listed = sector.get("planets", [])
        if not any(isinstance(row, dict) and row.get("id", row.get("planet_id")) == planet_id for row in listed):
            raise AssertionError("capture target was not listed by the server in the Python client's current sector")
        ctx = Context(conn=conn, menus=client.load_menus(str(CLIENT_DIR / "menus.json")), last_sector_desc=sector)
        conn.ctx = ctx
        ctx.state["player_id"] = player["id"]
        ctx.state["cli"] = {"host": "127.0.0.1", "port": port, "user": username, "debug": False}
        ctx.menu_stack[:] = ["DEPLOYMENT_MAIN"]
        choices = iter([str(planet_id), "y", "y"])
        with contextlib.redirect_stdout(transcript), patch("builtins.input", lambda _prompt="": next(choices)):
            client.render_menu(ctx)
            client.handle_choice(ctx, "p")
        output = transcript.getvalue()
        if "(P) Attack Listed Planet" not in output:
            raise AssertionError("the Python deployment menu did not render its planet-attack action")
        if "Captured: True" not in output or "Attacker fighters remaining: 90" not in output:
            raise AssertionError("Python client did not present the server-confirmed capture result")
        if "Planet fighters remaining: 0" not in output or "Colonists enslaved: True" not in output:
            raise AssertionError("Python client omitted the authoritative defender/population result")
        print("LIVE_PYTHON_PLANET_CAPTURE_PASS client=Python player_id=%d planet_id=%d" % (player["id"], planet_id))
        print("Python deployment menu and combat workflow checks: 5 passed")
        return 0
    finally:
        try:
            if conn.session_token:
                conn.rpc("auth.logout", {})
        except (ConnectionError, OSError):
            pass
        sock.close()


if __name__ == "__main__":
    raise SystemExit(main())
