#!/usr/bin/env python3
"""Run the ordinary Python client's real MOVE menu toll workflow on loopback."""
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
    username = os.environ.get("TWCLONE_PYTHON_USERNAME", "python_toll_payer")
    password = os.environ.get("TWCLONE_PYTHON_PASSWORD", "qa-password")
    sock = socket.create_connection(("127.0.0.1", port), timeout=10)
    conn = Conn(sock)
    output = io.StringIO()
    try:
        conn.rpc("system.hello", {"client": "twclone-cli", "client_version": "3.x"})
        login = client.authenticate(conn, username, password)
        if not isinstance(login, dict) or login.get("status") != "ok":
            raise AssertionError("ordinary Python toll payer login failed")
        player_response = conn.rpc("player.my_info", {})
        player = get_data(player_response).get("player", {})
        sector_id = extract_current_sector(login)
        if sector_id is None:
            sector_id = extract_current_sector(player_response)
        if type(player.get("id")) is not int or sector_id != 1:
            raise AssertionError("Python client did not restore the fixture payer's authoritative identity and sector")
        sector_response = conn.rpc("move.describe_sector", {"sector_id": sector_id})
        if sector_response.get("status") != "ok" or sector_response.get("type") != "sector.info":
            raise AssertionError("Python client could not refresh its current sector")
        sector = normalize_sector(get_data(sector_response))
        ctx = Context(conn=conn, menus=client.load_menus(str(CLIENT_DIR / "menus.json")), last_sector_desc=sector)
        conn.ctx = ctx
        ctx.player_info = player_response.get("data", {})
        ctx.state.update({"player_id": player["id"], "cli": {"host": "127.0.0.1", "port": port, "user": username, "debug": False}})
        ctx.menu_stack[:] = ["MOVE"]
        prompts: list[str] = []
        answers = iter(["2", "y"])

        def answer(prompt: str = "") -> str:
            prompts.append(prompt)
            return next(answers)

        with contextlib.redirect_stdout(output), patch("builtins.input", side_effect=answer):
            client.render_menu(ctx)
            client.handle_choice(ctx, "w")
        rendered = output.getvalue()
        required = (
            "(W) Warp to Sector",
            "Toll fighters demand 3 credits (3 fighters).",
            "Moved to sector 2.",
        )
        missing = [item for item in required if item not in rendered]
        if missing:
            raise AssertionError("Python toll workflow did not present authoritative UI stages: %r" % missing)
        if not any("Accept the toll and continue?" in prompt for prompt in prompts):
            raise AssertionError("Python client did not present the explicit toll decision prompt")
        if ctx.last_sector_desc.get("id") != 2:
            raise AssertionError("Python client did not refresh the confirmed destination sector")
        print("LIVE_PYTHON_NORMAL_WARP_TOLL_PASS player_id=%d sector=2 offer=3 paid=3" % player["id"])
        print("Python MOVE menu toll workflow checks: 5 passed")
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
