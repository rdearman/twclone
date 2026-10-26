"""ASGI entry point for the TWClone HTTPS gateway."""

from __future__ import annotations

import asyncio
import json
import logging
import secrets
import time
from dataclasses import dataclass
from typing import Any
from urllib.parse import urlsplit

from fastapi import FastAPI, HTTPException, Request, Response, WebSocket, WebSocketDisconnect
from fastapi.exceptions import RequestValidationError
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse
from pydantic import BaseModel, ConfigDict, Field, SecretStr
from starlette.middleware.trustedhost import TrustedHostMiddleware

from app.config import Settings
from app.game_server import (
    GameServerClient,
    GameServerRefused,
    GameServerTimeout,
    GameServerUnavailable,
)
from app.event_stream import (
    EventStreamAuthenticationError,
    EventStreamCursorExpired,
    EventStreamError,
    PlayerEventStream,
)

logger = logging.getLogger(__name__)


@dataclass(slots=True)
class GatewaySession:
    player_id: int
    game_token: str
    csrf_token: str
    expires_at: float


class LoginRequest(BaseModel):
    model_config = ConfigDict(extra="forbid")

    username: str = Field(min_length=1, max_length=128)
    password: SecretStr = Field(min_length=1, max_length=1024)


class LoginResponse(BaseModel):
    player: dict[str, Any]
    csrf_token: str


def _error(status_code: int, code: str, message: str) -> HTTPException:
    return HTTPException(
        status_code=status_code,
        detail={"error": {"code": code, "message": message}},
        headers={"Cache-Control": "no-store"},
    )


def _check_origin(request: Request, configured_origin: str | None) -> None:
    origin = request.headers.get("origin")
    if not origin:
        return
    parsed = urlsplit(origin)
    expected = configured_origin or f"{request.url.scheme}://{request.headers.get('host', '')}"
    allowed = {expected.rstrip("/").lower()}
    allowed.update(value.rstrip("/").lower() for value in request.app.state.settings.cors_origins)
    if (
        not parsed.scheme
        or not parsed.netloc
        or parsed.path not in ("", "/")
        or parsed.query
        or parsed.fragment
        or origin.rstrip("/").lower() not in allowed
    ):
        raise _error(403, "origin_forbidden", "Request origin is not allowed")


def create_app(settings: Settings | None = None) -> FastAPI:
    config = settings or Settings.from_env()
    app = FastAPI(
        title="TWClone HTTPS Gateway",
        version="0.1.0",
        docs_url="/docs" if config.environment != "production" else None,
        redoc_url=None,
    )
    app.state.settings = config
    app.state.sessions = {}
    app.state.game_server = GameServerClient(config)

    if config.trusted_hosts and "*" not in config.trusted_hosts:
        app.add_middleware(TrustedHostMiddleware, allowed_hosts=list(config.trusted_hosts))
    if config.cors_origins:
        app.add_middleware(
            CORSMiddleware,
            allow_origins=list(config.cors_origins),
            allow_credentials=True,
            allow_methods=["GET", "POST", "PUT", "PATCH", "DELETE"],
            allow_headers=["Content-Type", "X-CSRF-Token"],
        )

    @app.get("/healthz", tags=["operations"])
    async def healthz(response: Response) -> dict[str, str]:
        """Return process liveness only; this does not probe the game server."""
        response.headers["Cache-Control"] = "no-store"
        return {"status": "ok", "service": "twclone-https-gateway"}

    @app.exception_handler(HTTPException)
    async def gateway_http_error(request: Request, exc: HTTPException) -> Response:
        detail = exc.detail
        if not isinstance(detail, dict) or "error" not in detail:
            detail = {"error": {"code": "request_error", "message": str(detail)}}
        return JSONResponse(
            status_code=exc.status_code,
            content=detail,
            headers={**(exc.headers or {}), "Cache-Control": "no-store"},
        )

    @app.exception_handler(RequestValidationError)
    async def request_validation_error(
        request: Request, exc: RequestValidationError
    ) -> Response:
        return JSONResponse(
            status_code=422,
            content={"error": {"code": "invalid_request", "message": "Invalid request"}},
            headers={"Cache-Control": "no-store"},
        )

    async def current_session(request: Request) -> tuple[str, GatewaySession]:
        session_id = request.cookies.get(config.session_cookie_name)
        session = app.state.sessions.get(session_id) if session_id else None
        if not session or session.expires_at <= time.time():
            if session_id:
                app.state.sessions.pop(session_id, None)
            raise _error(401, "session_invalid", "Login required")
        return session_id, session

    @app.post("/api/v1/auth/login", response_model=LoginResponse, tags=["auth"])
    async def login(payload: LoginRequest, request: Request, response: Response):
        _check_origin(request, config.public_origin)
        try:
            upstream = await app.state.game_server.command(
                "auth.login",
                {
                    "username": payload.username,
                    # Current server schema is passwd; browser contract uses password.
                    "passwd": payload.password.get_secret_value(),
                },
            )
        except GameServerRefused as exc:
            logger.info("Game server rejected gateway login")
            raise _error(401, "invalid_credentials", "Invalid username or password") from exc
        except GameServerTimeout as exc:
            raise _error(504, "game_server_timeout", "Game server request timed out") from exc
        except GameServerUnavailable as exc:
            raise _error(503, "game_server_unavailable", "Game server unavailable") from exc

        data = upstream.get("data")
        if not isinstance(data, dict):
            raise _error(502, "invalid_upstream_response", "Game server response invalid")
        token = data.get("session_token")
        player_id = data.get("player_id")
        if not isinstance(token, str) or not token or not isinstance(player_id, int):
            raise _error(502, "invalid_upstream_response", "Game server response invalid")

        session_id = secrets.token_urlsafe(32)
        csrf_token = secrets.token_urlsafe(32)
        expires_at = time.time() + config.session_lifetime_seconds
        app.state.sessions[session_id] = GatewaySession(
            player_id=player_id,
            game_token=token,
            csrf_token=csrf_token,
            expires_at=expires_at,
        )
        response.set_cookie(
            key=config.session_cookie_name,
            value=session_id,
            max_age=config.session_lifetime_seconds,
            httponly=True,
            secure=config.session_cookie_secure,
            samesite=config.session_cookie_samesite,
            path="/",
        )
        response.headers["Cache-Control"] = "no-store"
        return LoginResponse(
            player={
                "id": player_id,
                "current_sector": data.get("current_sector"),
                "unread_news_count": data.get("unread_news_count"),
            },
            csrf_token=csrf_token,
        )

    @app.get("/api/v1/auth/csrf", tags=["auth"])
    async def csrf(request: Request, response: Response):
        _, session = await current_session(request)
        response.headers["Cache-Control"] = "no-store"
        return {"csrf_token": session.csrf_token}

    @app.post("/api/v1/auth/logout", status_code=204, tags=["auth"])
    async def logout(request: Request, response: Response):
        _check_origin(request, config.public_origin)
        session_id, session = await current_session(request)
        supplied = request.headers.get("x-csrf-token", "")
        if not secrets.compare_digest(supplied, session.csrf_token):
            raise _error(403, "csrf_invalid", "CSRF token is missing or invalid")

        # Revoke upstream, while ensuring local logout succeeds if it is offline.
        app.state.sessions.pop(session_id, None)
        try:
            await app.state.game_server.command(
                "auth.logout", {}, session_token=session.game_token
            )
        except (GameServerRefused, GameServerTimeout, GameServerUnavailable):
            logger.warning("Game server logout revocation failed")
        response.delete_cookie(
            key=config.session_cookie_name,
            path="/",
            httponly=True,
            secure=config.session_cookie_secure,
            samesite=config.session_cookie_samesite,
        )
        response.headers["Cache-Control"] = "no-store"
        response.status_code = 204
        return response

    @app.get("/api/v1/player/me", tags=["player"])
    async def player_me(request: Request, response: Response):
        session_id, session = await current_session(request)
        try:
            upstream = await app.state.game_server.command(
                "player.my_info", {}, session_token=session.game_token
            )
        except GameServerRefused as exc:
            code = exc.response.get("error", {}).get("code")
            if code in (401, "401", 1300):
                app.state.sessions.pop(session_id, None)
                raise _error(401, "session_invalid", "Login required") from exc
            raise _error(502, "game_command_refused", "Player information unavailable") from exc
        except GameServerTimeout as exc:
            raise _error(504, "game_server_timeout", "Game server request timed out") from exc
        except GameServerUnavailable as exc:
            raise _error(503, "game_server_unavailable", "Game server unavailable") from exc
        response.headers["Cache-Control"] = "no-store"
        return {"player": upstream.get("data", {})}

    @app.websocket("/api/v1/events")
    async def player_events(websocket: WebSocket):
        """Keep an authenticated game-server connection for event delivery."""
        origin = websocket.headers.get("origin")
        if origin:
            parsed = urlsplit(origin)
            websocket_scheme = websocket.url.scheme
            browser_scheme = "https" if websocket_scheme == "wss" else "http"
            expected = config.public_origin or (
                f"{browser_scheme}://{websocket.headers.get('host', '')}"
            )
            allowed = {expected.rstrip("/").lower()}
            allowed.update(value.rstrip("/").lower() for value in config.cors_origins)
            if (
                not parsed.scheme
                or not parsed.netloc
                or origin.rstrip("/").lower() not in allowed
                or parsed.path not in ("", "/")
                or parsed.query
                or parsed.fragment
            ):
                await websocket.accept()
                await websocket.close(code=4403, reason="Origin is not allowed")
                return

        session_id = websocket.cookies.get(config.session_cookie_name)
        session = app.state.sessions.get(session_id) if session_id else None
        await websocket.accept()
        if not session or session.expires_at <= time.time():
            if session_id:
                app.state.sessions.pop(session_id, None)
            await websocket.close(code=4401, reason="Login required")
            return

        after_cursor = websocket.query_params.get("after_cursor")
        if after_cursor is not None and (not after_cursor or len(after_cursor) > 256):
            await websocket.close(code=4400, reason="Invalid event cursor")
            return

        try:
            upstream = await PlayerEventStream.open(
                config, session.game_token, after_cursor
            )
        except EventStreamAuthenticationError:
            app.state.sessions.pop(session_id, None)
            await websocket.close(code=4401, reason="Game session is invalid")
            return
        except EventStreamCursorExpired:
            await websocket.send_json(
                {
                    "type": "gateway.events.resync_required",
                    "data": {"resync_required": True},
                }
            )
            await websocket.close(code=4409, reason="Event cursor requires resynchronization")
            return
        except EventStreamError:
            await websocket.close(code=1013, reason="Game event stream unavailable")
            return

        async def send_events() -> None:
            for event in upstream.initial_events:
                async with send_lock:
                    await asyncio.wait_for(websocket.send_json(event), timeout=10)
            while True:
                event = await upstream.next_live_event()
                async with send_lock:
                    await asyncio.wait_for(websocket.send_json(event), timeout=10)

        send_lock = asyncio.Lock()
        command_tasks: set[asyncio.Task[None]] = set()

        async def handle_command(text: str) -> None:
            browser_id = ""
            try:
                value = json.loads(text)
                if not isinstance(value, dict):
                    raise ValueError("Command envelope must be an object")
                candidate_id = value.get("id")
                command = value.get("command")
                data = value.get("data", {})
                supplied_meta = value.get("meta", {})
                if isinstance(candidate_id, str) and 1 <= len(candidate_id) <= 128:
                    browser_id = candidate_id
                if not browser_id or not isinstance(command, str) or not command.strip() or len(command) > 128:
                    raise ValueError("Command envelope has invalid id or command")
                if not isinstance(data, dict) or not isinstance(supplied_meta, dict):
                    raise ValueError("Command data and metadata must be objects")
                # Only the documented idempotency field is forwarded. Browser
                # auth, target and S2S values are never copied upstream.
                meta: dict[str, Any] = {}
                idempotency_key = supplied_meta.get("idempotency_key")
                if idempotency_key is not None:
                    if not isinstance(idempotency_key, str) or not (1 <= len(idempotency_key) <= 128):
                        raise ValueError("Invalid idempotency key")
                    meta["idempotency_key"] = idempotency_key
                result = await upstream.send_command(
                    browser_id, command.strip(), data, meta, session.game_token
                )
            except (ValueError, json.JSONDecodeError):
                if not browser_id:
                    return
                result = {
                    "reply_to": browser_id,
                    "status": "error",
                    "error": {"code": "invalid_command_envelope", "message": "Invalid command envelope"},
                }
            except EventStreamAuthenticationError:
                result = {
                    "reply_to": browser_id,
                    "status": "error",
                    "error": {"code": "session_invalid", "message": "Login required"},
                }
            except EventStreamError:
                result = {
                    "reply_to": browser_id,
                    "status": "error",
                    "error": {"code": "game_server_unavailable", "message": "Game command unavailable"},
                }
            async with send_lock:
                await websocket.send_json(result)

        async def watch_browser() -> None:
            while True:
                if (
                    app.state.sessions.get(session_id) is not session
                    or session.expires_at <= time.time()
                ):
                    await websocket.close(code=4401, reason="Login required")
                    return
                try:
                    message = await asyncio.wait_for(
                        websocket.receive(), timeout=1
                    )
                except asyncio.TimeoutError:
                    continue
                if message.get("type") == "websocket.disconnect":
                    return
                text = message.get("text")
                if not isinstance(text, str):
                    await websocket.close(code=1003, reason="Text command envelope required")
                    return
                task = asyncio.create_task(handle_command(text))
                command_tasks.add(task)
                task.add_done_callback(command_tasks.discard)

        tasks = {
            asyncio.create_task(send_events()),
            asyncio.create_task(watch_browser()),
        }
        try:
            done, pending = await asyncio.wait(
                tasks, return_when=asyncio.FIRST_COMPLETED
            )
            for task in done:
                if task.cancelled():
                    continue
                error = task.exception()
                if error:
                    if isinstance(error, WebSocketDisconnect):
                        continue
                    logger.debug("Gateway event stream ended: %s", type(error).__name__)
                    try:
                        await websocket.close(code=1013, reason="Event stream ended")
                    except RuntimeError:
                        pass
        finally:
            for task in tasks:
                if not task.done():
                    task.cancel()
            await asyncio.gather(*tasks, return_exceptions=True)
            for task in command_tasks:
                if not task.done():
                    task.cancel()
            await asyncio.gather(*command_tasks, return_exceptions=True)
            await upstream.close()

    return app


app = create_app()
