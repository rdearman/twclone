from __future__ import annotations

import asyncio
import json
from types import SimpleNamespace
from typing import Any
from urllib.parse import parse_qs, urlsplit

import pytest

from app.config import Settings
from app.event_stream import (
    EventStreamAuthenticationError,
    EventStreamCursorExpired,
    PlayerEventStream,
)
from app.main import GatewaySession, create_app


class FakeWriter:
    def __init__(self) -> None:
        self.payload = bytearray()
        self.closed = False

    def write(self, value: bytes) -> None:
        self.payload.extend(value)

    async def drain(self) -> None:
        return None

    def close(self) -> None:
        self.closed = True

    async def wait_closed(self) -> None:
        return None


class FakeReader:
    def __init__(self, *frames: dict[str, Any]) -> None:
        self.frames = [json.dumps(frame).encode() + b"\n" for frame in frames]

    async def readline(self) -> bytes:
        await asyncio.sleep(0)
        return self.frames.pop(0) if self.frames else b""


class FakeWebSocket:
    def __init__(self, path: str, cookies=None, headers=None, disconnect_after=None):
        parsed = urlsplit(path)
        self.url = SimpleNamespace(scheme="ws")
        self.query_params = {key: values[-1] for key, values in parse_qs(parsed.query).items()}
        self.cookies = cookies or {}
        self.headers = {"host": "testserver", **(headers or {})}
        self.disconnect_after = disconnect_after
        self.incoming: asyncio.Queue[dict[str, Any]] = asyncio.Queue()
        self.sent: list[dict[str, Any]] = []
        self.accepted = False
        self.close_code = None

    async def accept(self):
        self.accepted = True

    async def close(self, code=1000, reason=None):
        self.close_code = code

    async def send_json(self, value):
        self.sent.append(value)
        if self.disconnect_after and len(self.sent) >= self.disconnect_after:
            await self.incoming.put({"type": "websocket.disconnect"})

    async def receive(self):
        return await self.incoming.get()


def event(event_id: str, cursor: str) -> dict[str, Any]:
    return {
        "id": event_id,
        "ts": "2026-09-29T12:00:00Z",
        "status": "ok",
        "type": "sector.player_entered",
        "data": {"sector_id": 4},
        "error": None,
        "meta": {"topic": "sector.4", "cursor": cursor, "replayable": True},
    }


def reply(request_id: str, data: dict[str, Any]) -> dict[str, Any]:
    return {"id": "reply", "reply_to": request_id, "status": "ok", "data": data}


def test_upstream_resume_pages_before_buffered_live_events_and_deduplicates(monkeypatch):
    live_duplicate = event("event-1", "opaque-1")
    live_after_watermark = event("event-3", "opaque-3")
    reader = FakeReader(
        live_duplicate,
        reply("gateway-first", {
            "events": [event("event-1", "opaque-1")],
            "next_cursor": "opaque-1",
            "high_watermark": "opaque-2",
            "has_more": True,
        }),
        live_after_watermark,
        reply("gateway-second", {
            "events": [event("event-2", "opaque-2")],
            "next_cursor": "opaque-2",
            "high_watermark": "opaque-2",
            "has_more": False,
        }),
    )
    writer = FakeWriter()
    async def fake_open_connection(host, port, limit):
        assert (host, port) == ("game", 1234)
        return reader, writer

    ids = iter(("first", "second"))
    monkeypatch.setattr(asyncio, "open_connection", fake_open_connection)
    monkeypatch.setattr("app.event_stream.uuid.uuid4", lambda: SimpleNamespace(hex=next(ids)))

    stream = asyncio.run(
        PlayerEventStream.open(
            Settings(game_server_host="game", game_server_port=1234),
            "server-session-token",
            "opaque-before",
        )
    )

    assert [item["id"] for item in stream.initial_events] == [
        "event-1", "event-2", "event-3"
    ]
    requests = [json.loads(line) for line in writer.payload.splitlines()]
    assert requests[0]["command"] == "events.resume"
    assert requests[0]["auth"] == {"session": "server-session-token"}
    assert requests[0]["data"] == {"limit": 100, "after_cursor": "opaque-before"}
    assert requests[1]["data"] == {
        "limit": 100,
        "after_cursor": "opaque-1",
        "through_cursor": "opaque-2",
    }
    asyncio.run(stream.close())
    assert writer.closed


def test_upstream_uses_persistent_tcp_until_stream_closes():
    async def scenario():
        connection_closed = asyncio.Event()

        async def handle(reader, writer):
            request = json.loads(await reader.readline())
            assert request["command"] == "events.resume"
            assert request["auth"] == {"session": "real-tcp-token"}
            response = reply(request["id"], {
                "events": [],
                "next_cursor": "opaque-0",
                "high_watermark": "opaque-0",
                "has_more": False,
            })
            writer.write(json.dumps(response).encode() + b"\n")
            await writer.drain()
            writer.write(json.dumps(event("tcp-live", "opaque-1")).encode() + b"\n")
            await writer.drain()
            await reader.read()
            writer.close()
            await writer.wait_closed()
            connection_closed.set()

        server = await asyncio.start_server(handle, "127.0.0.1", 0)
        port = server.sockets[0].getsockname()[1]
        stream = await PlayerEventStream.open(
            Settings(game_server_host="127.0.0.1", game_server_port=port),
            "real-tcp-token",
            None,
        )
        live = await asyncio.wait_for(stream.next_live_event(), timeout=1)
        assert live["id"] == "tcp-live"
        await stream.close()
        await asyncio.wait_for(connection_closed.wait(), timeout=1)
        server.close()
        await server.wait_closed()

    asyncio.run(scenario())


def test_browser_command_uses_gateway_session_and_correlates_server_reply(monkeypatch):
    async def scenario():
        reader = FakeReader(
            {"reply_to": "gateway-command-1", "status": "refused", "data": {"reason": "test"}},
            event("after-command", "cursor-next"),
        )
        writer = FakeWriter()
        stream = PlayerEventStream(reader, writer, timeout=1)
        monkeypatch.setattr(
            "app.event_stream.uuid.uuid4", lambda: SimpleNamespace(hex="command-1")
        )
        command = asyncio.create_task(stream.send_command(
            "browser-request-7", "port.trade", {"commodity": "ore", "quantity": 1},
            {"idempotency_key": "operation-7"}, "held-token",
        ))
        await asyncio.sleep(0)
        live = asyncio.create_task(stream.next_live_event())
        response = await command
        next_event = await live
        request = json.loads(writer.payload.splitlines()[0])
        assert request == {
            "id": "gateway-command-1",
            "command": "port.trade",
            "auth": {"session": "held-token"},
            "data": {"commodity": "ore", "quantity": 1},
            "meta": {"idempotency_key": "operation-7"},
        }
        assert response == {
            "reply_to": "browser-request-7", "status": "refused", "data": {"reason": "test"}
        }
        assert next_event["id"] == "after-command"
        await stream.close()

    asyncio.run(scenario())


@pytest.mark.parametrize(
    ("error", "exception"),
    [
        ({"code": 1300, "message": "invalid session"}, EventStreamAuthenticationError),
        ({"code": 1306, "message": "cursor", "data": {"resync_required": True}},
         EventStreamCursorExpired),
    ],
)
def test_upstream_maps_authentication_and_expired_cursor_failures(
    monkeypatch, error, exception
):
    reader = FakeReader({
        "id": "reply",
        "reply_to": "gateway-test",
        "status": "error",
        "error": error,
    })
    writer = FakeWriter()
    async def fake_open_connection(host, port, limit):
        return reader, writer

    monkeypatch.setattr(asyncio, "open_connection", fake_open_connection)
    monkeypatch.setattr(
        "app.event_stream.uuid.uuid4", lambda: SimpleNamespace(hex="test")
    )
    with pytest.raises(exception):
        asyncio.run(PlayerEventStream.open(Settings(), "token", "cursor"))
    assert writer.closed


class FakeStream:
    def __init__(self, after_cursor: str | None) -> None:
        self.after_cursor = after_cursor
        self.initial_events = [event("replayed-event", "cursor-1")]
        self.closed = False
        self.pending = asyncio.Event()
        self.live_sent = False

    async def next_live_event(self):
        if not self.live_sent:
            await asyncio.sleep(0.02)
            self.live_sent = True
            return event("live-event", "cursor-2")
        await self.pending.wait()
        raise OSError("upstream closed")

    async def close(self):
        self.closed = True
        self.pending.set()


def make_test_app() -> Any:
    app = create_app(Settings(environment="test", session_cookie_secure=False))
    app.state.sessions["valid-cookie"] = GatewaySession(
        player_id=8,
        game_token="held-game-token",
        csrf_token="csrf",
        expires_at=9_999_999_999,
    )
    return app


def endpoint(app):
    return next(route.endpoint for route in app.routes
                if getattr(route, "path", None) == "/api/v1/events")


def test_websocket_rejects_missing_authentication():
    app = create_app(Settings(environment="test", session_cookie_secure=False))
    socket = FakeWebSocket("/api/v1/events", headers={"origin": "http://testserver"})
    asyncio.run(endpoint(app)(socket))
    assert socket.close_code == 4401


def test_websocket_replays_then_delivers_live_and_closes_upstream(monkeypatch):
    app = make_test_app()
    opened: list[FakeStream] = []

    async def fake_open(settings, token, after_cursor):
        assert token == "held-game-token"
        stream = FakeStream(after_cursor)
        opened.append(stream)
        return stream

    monkeypatch.setattr("app.main.PlayerEventStream.open", fake_open)
    socket = FakeWebSocket(
        "/api/v1/events?after_cursor=opaque-cursor",
        cookies={"twclone_session": "valid-cookie"},
        headers={"origin": "http://testserver"},
        disconnect_after=2,
    )
    asyncio.run(endpoint(app)(socket))
    assert [item["id"] for item in socket.sent] == [
        "replayed-event", "live-event"
    ]
    assert opened[0].after_cursor == "opaque-cursor"
    assert opened[0].closed


def test_websocket_forwards_only_documented_command_fields(monkeypatch):
    app = make_test_app()
    captured: list[tuple[Any, ...]] = []

    class CommandStream(FakeStream):
        def __init__(self, after_cursor):
            super().__init__(after_cursor)
            self.initial_events = []

        async def send_command(self, browser_id, command, data, meta, session_token):
            captured.append((browser_id, command, data, meta, session_token))
            return {"id": "server-response", "reply_to": browser_id, "status": "refused", "data": {"reason": "no"}}

        async def next_live_event(self):
            await self.pending.wait()
            raise OSError("closed")

    async def fake_open(settings, token, after_cursor):
        assert token == "held-game-token"
        return CommandStream(after_cursor)

    monkeypatch.setattr("app.main.PlayerEventStream.open", fake_open)
    socket = FakeWebSocket(
        "/api/v1/events",
        cookies={"twclone_session": "valid-cookie"},
        headers={"origin": "http://testserver"},
        disconnect_after=1,
    )
    socket.incoming.put_nowait({
        "type": "websocket.receive",
        "text": json.dumps({
            "id": "browser-id", "command": "port.trade", "data": {"quantity": 1},
            "meta": {"idempotency_key": "operation-id", "other": "drop"},
            "auth": {"session": "attacker-token"}, "target": "s2s", "s2s": {"x": 1},
        }),
    })
    asyncio.run(endpoint(app)(socket))
    assert captured == [(
        "browser-id", "port.trade", {"quantity": 1},
        {"idempotency_key": "operation-id"}, "held-game-token",
    )]
    assert socket.sent == [{
        "id": "server-response", "reply_to": "browser-id", "status": "refused", "data": {"reason": "no"}
    }]


def test_websocket_rejects_invalid_game_session(monkeypatch):
    app = make_test_app()

    async def rejected(settings, token, after_cursor):
        raise EventStreamAuthenticationError("invalid server session")

    monkeypatch.setattr("app.main.PlayerEventStream.open", rejected)
    socket = FakeWebSocket(
        "/api/v1/events",
        cookies={"twclone_session": "valid-cookie"},
        headers={"origin": "http://testserver"},
    )
    asyncio.run(endpoint(app)(socket))
    assert socket.close_code == 4401
    assert "valid-cookie" not in app.state.sessions


def test_websocket_reports_expired_cursor_for_resynchronization(monkeypatch):
    app = make_test_app()

    async def expired(settings, token, after_cursor):
        raise EventStreamCursorExpired("expired")

    monkeypatch.setattr("app.main.PlayerEventStream.open", expired)
    socket = FakeWebSocket(
        "/api/v1/events?after_cursor=expired-cursor",
        cookies={"twclone_session": "valid-cookie"},
        headers={"origin": "http://testserver"},
    )
    asyncio.run(endpoint(app)(socket))
    assert socket.sent == [{
        "type": "gateway.events.resync_required",
        "data": {"resync_required": True},
    }]
    assert socket.close_code == 4409
