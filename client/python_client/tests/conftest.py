"""
Shared pytest fixtures for the Python client unit tests.

These tests exercise client-side logic (state transitions, menu gating,
presenters, CLI parsing) against a scripted `FakeConn` — no live server is
required or contacted.
"""
from __future__ import annotations

import json
import os
import sys
from typing import Any, Dict, List, Optional

import pytest

# client.py is a script, not a package; add its directory to sys.path so the
# tests can `import client`, `import protocol`, `import state`, `import
# presenters` the same way client.py itself does when run directly.
CLIENT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if CLIENT_DIR not in sys.path:
    sys.path.insert(0, CLIENT_DIR)

from state import Context  # noqa: E402
from protocol import EventQueue  # noqa: E402


class FakeConn:
    """
    A scripted stand-in for protocol.Conn. Responses are queued per-command;
    each call to rpc(command, data) pops the next queued response for that
    command (or a default if none was queued) and records the call.
    """

    def __init__(self):
        self.calls: List[Dict[str, Any]] = []
        self._queued: Dict[str, List[Dict[str, Any]]] = {}
        self.session_token = None
        self.debug = False
        self.connected = True
        self.events = EventQueue(maxlen=200)

    # Positive fixtures default to the schema the V3 server publishes. Tests
    # for malformed or deliberately untyped replies use queue_untyped().
    RESPONSE_TYPES = {
        "player.my_info": "player.info",
        "ship.status": "ship.status",
        "move.describe_sector": "sector.info",
        "sector.info": "sector.info",
        "sector.scan": "sector.scan",
        "sector.scan.density": "sector.density.scan",
        "move.scan": "sector.scan",
        "move.warp": "move.result",
        "move.transwarp": "move.transwarp",
        "planet.land": "planet.land.success",
        "planet.launch": "planet.launch.success",
        "planet.info": "planet.info",
        "planet.rename": "planet.rename.success",
        "planet.fighters.transfer": "planet.fighters.transferred_v1",
        "planet.deposit": "planet.deposit",
        "planet.withdraw": "planet.withdraw",
        "planet.colonists.allocate": "planet.colonists.allocate",
        "citadel.build": "citadel.upgrade_started",
        "ship.rename": "ship.renamed",
        "port.info": "trade.port_info",
        "trade.quote": "trade.quote",
        "trade.buy": "trade.buy_receipt_v1",
        "trade.sell": "trade.sell_receipt_v1",
    }

    def queue(self, command: str, response: Any):
        # A few malformed-response tests deliberately queue scalars or None;
        # preserve those values so the client, rather than the fixture, handles
        # the invalid envelope.
        if not isinstance(response, dict):
            self._queued.setdefault(command, []).append(response)
            return
        response = dict(response)
        if "type" not in response:
            if response.get("status") == "ok":
                response_type = self.RESPONSE_TYPES.get(command)
                if response_type:
                    response["type"] = response_type
            elif response.get("status") in ("refused", "error"):
                response["type"] = "error"
        self._queued.setdefault(command, []).append(response)

    def queue_untyped(self, command: str, response: Any):
        """Queue an intentionally malformed response with no ``type`` field."""
        if not isinstance(response, dict):
            self._queued.setdefault(command, []).append(response)
            return
        response = dict(response)
        response.pop("type", None)
        self._queued.setdefault(command, []).append(response)

    def rpc(self, command: str, data: dict) -> dict:
        self.calls.append({"command": command, "data": data})
        q = self._queued.get(command)
        if q:
            response = q.pop(0)
            return response
        # Sensible default: unknown command → ok with empty data, so tests
        # that don't care about a given call don't crash.
        return {"status": "ok", "data": {}}


@pytest.fixture
def fake_conn():
    return FakeConn()


@pytest.fixture
def menus():
    path = os.path.join(CLIENT_DIR, "menus.json")
    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)
    return data.get("menus", data)


@pytest.fixture
def ctx_factory(fake_conn, menus):
    def _make(sector: Optional[Dict[str, Any]] = None, debug: bool = False) -> Context:
        ctx = Context(conn=fake_conn, menus=menus)
        if sector is not None:
            ctx.last_sector_desc = sector
        ctx.state["debug"] = debug
        return ctx
    return _make
