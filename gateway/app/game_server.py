"""Small fixed-target TCP/NDJSON adapter for existing game commands."""

from __future__ import annotations

import asyncio
import json
import uuid
from typing import Any

from app.config import Settings

MAX_FRAME_BYTES = 64 * 1024


class GameServerUnavailable(Exception):
    """The configured game server could not complete the request."""


class GameServerTimeout(Exception):
    """The configured game server did not complete the request in time."""


class GameServerRefused(Exception):
    """The game server returned a non-success game response."""

    def __init__(self, response: dict[str, Any]):
        self.response = response
        super().__init__(str(response.get("message", "Game command refused")))


class GameServerClient:
    """One request per TCP connection; the browser cannot select the target."""

    def __init__(self, settings: Settings):
        self._settings = settings

    async def command(
        self,
        command: str,
        data: dict[str, Any] | None = None,
        session_token: str | None = None,
    ) -> dict[str, Any]:
        request_id = f"gateway-{uuid.uuid4().hex}"
        request: dict[str, Any] = {
            "id": request_id,
            "command": command,
            "data": data or {},
        }
        if session_token:
            request["auth"] = {"session": session_token}

        try:
            reader, writer = await asyncio.wait_for(
                asyncio.open_connection(
                    self._settings.game_server_host,
                    self._settings.game_server_port,
                    limit=MAX_FRAME_BYTES + 1,
                ),
                timeout=self._settings.game_server_timeout_seconds,
            )
            try:
                encoded = json.dumps(request, separators=(",", ":")).encode("utf-8")
                if len(encoded) > MAX_FRAME_BYTES:
                    raise GameServerUnavailable("Request exceeds protocol frame limit")
                writer.write(encoded + b"\n")
                await asyncio.wait_for(
                    writer.drain(), timeout=self._settings.game_server_timeout_seconds
                )
                while True:
                    line = await asyncio.wait_for(
                        reader.readline(),
                        timeout=self._settings.game_server_timeout_seconds,
                    )
                    if not line:
                        raise GameServerUnavailable("Game server closed the connection")
                    if len(line) > MAX_FRAME_BYTES:
                        raise GameServerUnavailable("Game server frame exceeds limit")
                    response = json.loads(line)
                    if not isinstance(response, dict):
                        raise GameServerUnavailable("Invalid game server response")
                    # Login may push notices on this stream before its RPC reply.
                    reply_id = response.get("reply_to")
                    if reply_id is not None and reply_id != request_id:
                        continue
                    if response.get("type") == "system.notice":
                        continue
                    break
            finally:
                writer.close()
                await writer.wait_closed()
        except GameServerRefused:
            raise
        except asyncio.TimeoutError as exc:
            raise GameServerTimeout("Game server request timed out") from exc
        except (OSError, json.JSONDecodeError, ValueError) as exc:
            raise GameServerUnavailable("Game server request failed") from exc

        if response.get("status") != "ok":
            raise GameServerRefused(response)
        return response
