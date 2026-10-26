"""Authenticated upstream player event stream over the game NDJSON protocol."""

from __future__ import annotations

import asyncio
import json
import uuid
from typing import Any

from app.config import Settings

MAX_FRAME_BYTES = 64 * 1024
PAGE_SIZE = 100
MAX_BOOTSTRAP_EVENTS = 10_000
MAX_BUFFERED_LIVE_EVENTS = 512


class EventStreamError(Exception):
    """The game server event stream could not be established or continued."""


class EventStreamAuthenticationError(EventStreamError):
    """The game server rejected the held player session."""


class EventStreamCursorExpired(EventStreamError):
    """The requested cursor is invalid or outside retained history."""


class EventStreamUnavailable(EventStreamError):
    """The game server transport or event response was invalid."""


class PlayerEventStream:
    """Persistent server connection with replay completed before live output."""

    def __init__(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter,
                 timeout: float) -> None:
        self._reader = reader
        self._writer = writer
        self._timeout = timeout
        self._live: list[dict[str, Any]] = []
        self._live_ids: set[str] = set()
        self._replayed_ids: set[str] = set()
        self._initial_events: list[dict[str, Any]] = []
        self._command_write_lock = asyncio.Lock()
        self._pending_commands: dict[str, tuple[str, asyncio.Future[dict[str, Any]]]] = {}
        self._closed = False

    @classmethod
    async def open(
        cls, settings: Settings, session_token: str, after_cursor: str | None
    ) -> "PlayerEventStream":
        try:
            reader, writer = await asyncio.wait_for(
                asyncio.open_connection(
                    settings.game_server_host,
                    settings.game_server_port,
                    limit=MAX_FRAME_BYTES + 1,
                ),
                timeout=settings.game_server_timeout_seconds,
            )
        except (OSError, asyncio.TimeoutError) as exc:
            raise EventStreamUnavailable("Game server connection failed") from exc

        stream = cls(reader, writer, settings.game_server_timeout_seconds)
        try:
            await stream._resume_all(session_token, after_cursor)
            return stream
        except Exception:
            await stream.close()
            raise

    @property
    def initial_events(self) -> list[dict[str, Any]]:
        """Replay pages followed by live events buffered during catch-up."""
        return self._initial_events

    async def _send_resume(
        self, session_token: str, data: dict[str, Any]
    ) -> dict[str, Any]:
        request_id = f"gateway-{uuid.uuid4().hex}"
        request = {
            "id": request_id,
            "command": "events.resume",
            "auth": {"session": session_token},
            "data": data,
        }
        encoded = json.dumps(request, separators=(",", ":")).encode("utf-8")
        if len(encoded) > MAX_FRAME_BYTES:
            raise EventStreamUnavailable("Resume request exceeds protocol frame limit")
        try:
            self._writer.write(encoded + b"\n")
            await asyncio.wait_for(self._writer.drain(), timeout=self._timeout)
            while True:
                frame = await self._read_frame(timeout=self._timeout)
                if frame.get("reply_to") == request_id:
                    if frame.get("status") != "ok":
                        self._raise_server_error(frame)
                    page = frame.get("data")
                    if not self._valid_page(page):
                        raise EventStreamUnavailable("Invalid event resume response")
                    return page
                if "reply_to" not in frame:
                    self._buffer_live(frame)
        except asyncio.TimeoutError as exc:
            raise EventStreamUnavailable("Game server event resume timed out") from exc
        except (OSError, json.JSONDecodeError, ValueError) as exc:
            raise EventStreamUnavailable("Game server event stream failed") from exc

    async def _read_frame(self, timeout: float | None) -> dict[str, Any]:
        read = self._reader.readline()
        line = await asyncio.wait_for(read, timeout=timeout) if timeout else await read
        if not line:
            raise EventStreamUnavailable("Game server closed the event stream")
        if len(line) > MAX_FRAME_BYTES or not line.endswith(b"\n"):
            raise EventStreamUnavailable("Game server frame exceeds protocol limit")
        value = json.loads(line)
        if not isinstance(value, dict):
            raise EventStreamUnavailable("Invalid game server frame")
        return value

    @staticmethod
    def _valid_event(value: Any) -> bool:
        return (
            isinstance(value, dict)
            and isinstance(value.get("id"), str)
            and isinstance(value.get("type"), str)
            and isinstance(value.get("data"), dict)
            and isinstance(value.get("meta"), dict)
        )

    @classmethod
    def _valid_page(cls, value: Any) -> bool:
        return (
            isinstance(value, dict)
            and isinstance(value.get("events"), list)
            and all(cls._valid_event(event) for event in value["events"])
            and isinstance(value.get("next_cursor"), str)
            and isinstance(value.get("high_watermark"), str)
            and isinstance(value.get("has_more"), bool)
        )

    @staticmethod
    def _raise_server_error(frame: dict[str, Any]) -> None:
        error = frame.get("error")
        error = error if isinstance(error, dict) else {}
        code = error.get("code")
        details = error.get("data")
        if code in (401, "401", 1300, "1300"):
            raise EventStreamAuthenticationError("Game session is invalid")
        if code in (1306, "1306") and isinstance(details, dict) and details.get("resync_required") is True:
            raise EventStreamCursorExpired("Event cursor requires resynchronization")
        raise EventStreamUnavailable("Game server rejected event resume")

    def _buffer_live(self, frame: dict[str, Any]) -> None:
        if len(self._live) >= MAX_BUFFERED_LIVE_EVENTS:
            raise EventStreamUnavailable("Live event buffer limit exceeded")
        event_id = frame.get("id")
        if isinstance(event_id, str):
            if event_id in self._live_ids:
                return
            self._live_ids.add(event_id)
        self._live.append(frame)

    async def _resume_all(self, session_token: str, after_cursor: str | None) -> None:
        request: dict[str, Any] = {"limit": PAGE_SIZE}
        if after_cursor is not None:
            request["after_cursor"] = after_cursor
        expected_after = after_cursor
        through_cursor: str | None = None

        while True:
            if through_cursor is not None:
                request["through_cursor"] = through_cursor
            page = await self._send_resume(session_token, request)
            if through_cursor is None:
                through_cursor = page["high_watermark"]
            if page["high_watermark"] != through_cursor:
                raise EventStreamUnavailable("Event resume watermark changed")
            for event in page["events"]:
                event_id = event["id"]
                if event_id not in self._replayed_ids:
                    self._replayed_ids.add(event_id)
                    self._initial_events.append(event)
                    if len(self._initial_events) > MAX_BOOTSTRAP_EVENTS:
                        raise EventStreamUnavailable("Event replay exceeds gateway limit")
            if not page["has_more"]:
                break
            next_cursor = page["next_cursor"]
            if not next_cursor or next_cursor == expected_after:
                raise EventStreamUnavailable("Event resume cursor did not advance")
            expected_after = next_cursor
            request = {"limit": PAGE_SIZE, "after_cursor": next_cursor}

        # The server captured the watermark when the first resume request was
        # handled. Events received during pagination are therefore newer than
        # replay and follow it; event IDs remove replay/live overlap.
        for event in self._live:
            event_id = event.get("id")
            if not isinstance(event_id, str) or event_id not in self._replayed_ids:
                self._initial_events.append(event)
        self._live.clear()
        self._live_ids.clear()

    async def next_live_event(self) -> dict[str, Any]:
        while True:
            frame = await self._read_frame(timeout=None)
            reply_to = frame.get("reply_to")
            pending = self._pending_commands.pop(reply_to, None) if isinstance(reply_to, str) else None
            if pending:
                browser_id, future = pending
                frame["reply_to"] = browser_id
                if not future.done():
                    future.set_result(frame)
                continue
            if "reply_to" in frame:
                continue
            return frame

    async def send_command(
        self,
        browser_id: str,
        command: str,
        data: dict[str, Any],
        meta: dict[str, Any],
        session_token: str,
    ) -> dict[str, Any]:
        """Forward one approved browser envelope through the held player session."""
        request_id = f"gateway-{uuid.uuid4().hex}"
        request: dict[str, Any] = {
            "id": request_id,
            "command": command,
            "auth": {"session": session_token},
            "data": data,
        }
        if meta:
            request["meta"] = meta
        encoded = json.dumps(request, separators=(",", ":")).encode("utf-8")
        if len(encoded) > MAX_FRAME_BYTES:
            raise EventStreamUnavailable("Command exceeds protocol frame limit")
        future: asyncio.Future[dict[str, Any]] = asyncio.get_running_loop().create_future()
        self._pending_commands[request_id] = (browser_id, future)
        try:
            async with self._command_write_lock:
                self._writer.write(encoded + b"\n")
                await asyncio.wait_for(self._writer.drain(), timeout=self._timeout)
            return await asyncio.wait_for(future, timeout=self._timeout)
        except asyncio.TimeoutError as exc:
            raise EventStreamUnavailable("Game command timed out") from exc
        except OSError as exc:
            raise EventStreamUnavailable("Game command transport failed") from exc
        finally:
            self._pending_commands.pop(request_id, None)

    async def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        for _, future in self._pending_commands.values():
            if not future.done():
                future.set_exception(EventStreamUnavailable("Game event stream closed"))
        self._pending_commands.clear()
        self._writer.close()
        try:
            await self._writer.wait_closed()
        except OSError:
            pass
