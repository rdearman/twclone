from __future__ import annotations

import json
import asyncio
from types import SimpleNamespace

import pytest

from app.config import Settings
from app.game_server import GameServerClient, GameServerRefused, GameServerUnavailable


class FakeWriter:
    def __init__(self) -> None:
        self.payload = b""
        self.closed = False

    def write(self, data: bytes) -> None:
        self.payload += data

    async def drain(self) -> None:
        return None

    def close(self) -> None:
        self.closed = True

    async def wait_closed(self) -> None:
        return None


class FakeReader:
    def __init__(self, *lines: bytes) -> None:
        self.lines = list(lines)

    async def readline(self) -> bytes:
        return self.lines.pop(0) if self.lines else b""


def test_client_sends_existing_protocol_envelope_and_ignores_unmatched_notice(monkeypatch):
    response = {
        "id": "server-response",
        "reply_to": "gateway-fixed",
        "status": "ok",
        "type": "auth.session",
        "data": {"player_id": 7, "session_token": "private"},
    }
    reader = FakeReader(
        b'{"type":"system.notice","data":{},"reply_to":"other"}\n',
        json.dumps(response).encode() + b"\n",
    )
    writer = FakeWriter()

    async def fake_open_connection(host, port, limit):
        assert host == "fixed-game-host"
        assert port == 4567
        return reader, writer

    monkeypatch.setattr(asyncio, "open_connection", fake_open_connection)
    monkeypatch.setattr("app.game_server.uuid.uuid4", lambda: SimpleNamespace(hex="fixed"))
    settings = Settings(game_server_host="fixed-game-host", game_server_port=4567)
    result = asyncio.run(
        GameServerClient(settings).command(
            "auth.login", {"username": "u", "passwd": "p"}
        )
    )

    request = json.loads(writer.payload)
    assert request["command"] == "auth.login"
    assert request["data"] == {"username": "u", "passwd": "p"}
    assert request["id"] == response["reply_to"]
    assert result["data"]["session_token"] == "private"
    assert writer.closed


def test_client_injects_auth_session_and_surfaces_game_refusal(monkeypatch):
    refusal = {
        "id": "server-response",
        "reply_to": "gateway-fixed",
        "status": "error",
        "error": {"code": 401, "message": "Not authenticated"},
    }
    reader = FakeReader(json.dumps(refusal).encode() + b"\n")
    writer = FakeWriter()

    async def fake_open_connection(host, port, limit):
        return reader, writer

    monkeypatch.setattr(asyncio, "open_connection", fake_open_connection)
    monkeypatch.setattr("app.game_server.uuid.uuid4", lambda: SimpleNamespace(hex="fixed"))
    with pytest.raises(GameServerRefused):
        asyncio.run(
            GameServerClient(Settings()).command(
                "player.my_info", session_token="held-token"
            )
        )
    request = json.loads(writer.payload)
    assert request["auth"] == {"session": "held-token"}


def test_client_reports_unavailable_server():
    with pytest.raises(GameServerUnavailable):
        asyncio.run(
            GameServerClient(
                Settings(game_server_host="127.0.0.1", game_server_port=1,
                         game_server_timeout_seconds=0.2)
            ).command("auth.login", {"username": "u", "passwd": "p"})
        )
