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

    def queue(self, command: str, response: Dict[str, Any]):
        self._queued.setdefault(command, []).append(response)

    def rpc(self, command: str, data: dict) -> dict:
        self.calls.append({"command": command, "data": data})
        q = self._queued.get(command)
        if q:
            return q.pop(0)
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
