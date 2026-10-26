from __future__ import annotations

from typing import Any

import httpx
import pytest

from app.config import Settings
from app.game_server import GameServerRefused, GameServerTimeout, GameServerUnavailable
from app.main import create_app


class FakeGameServer:
    def __init__(self) -> None:
        self.calls: list[tuple[str, dict[str, Any], str | None]] = []
        self.login_refused = False
        self.unavailable = False
        self.timeout = False

    async def command(self, command: str, data=None, session_token=None):
        self.calls.append((command, data or {}, session_token))
        if self.timeout:
            raise GameServerTimeout("timeout")
        if self.unavailable:
            raise GameServerUnavailable("offline")
        if command == "auth.login":
            if self.login_refused:
                raise GameServerRefused({"status": "error", "error": {"code": 1101}})
            assert data == {"username": "newguy", "passwd": "secret"}
            return {
                "status": "ok",
                "type": "auth.session",
                "data": {
                    "player_id": 17,
                    "current_sector": 2271,
                    "unread_news_count": 2,
                    "session_token": "game-secret-token",
                },
            }
        if command == "player.my_info":
            return {"status": "ok", "type": "player.my_info", "data": {"player": {"id": 17}}}
        if command == "auth.logout":
            return {"status": "ok", "type": "auth.logged_out"}
        raise AssertionError(f"Unexpected game command: {command}")


@pytest.fixture
def anyio_backend():
    return "asyncio"


def make_client() -> tuple[httpx.AsyncClient, FakeGameServer]:
    app = create_app(
        Settings(
            environment="test",
            trusted_hosts=("testserver",),
            session_cookie_secure=True,
        )
    )
    fake = FakeGameServer()
    app.state.game_server = fake
    client = httpx.AsyncClient(
        transport=httpx.ASGITransport(app=app), base_url="https://testserver"
    )
    return client, fake


async def login(client: httpx.AsyncClient) -> dict[str, Any]:
    response = await client.post(
        "/api/v1/auth/login",
        json={"username": "newguy", "password": "secret"},
    )
    assert response.status_code == 200
    return response.json()


@pytest.mark.anyio
async def test_login_keeps_game_token_server_side_and_sets_secure_cookie():
    client, _ = make_client()
    async with client:
        body = await login(client)

        assert body["player"] == {
            "id": 17,
            "current_sector": 2271,
            "unread_news_count": 2,
        }
        assert body["csrf_token"]
        assert "game-secret-token" not in str(body)
        session_cookie = next(cookie for cookie in client.cookies.jar if cookie.name == "twclone_session")
        assert session_cookie.has_nonstandard_attr("HttpOnly")
        assert session_cookie.secure
        assert session_cookie.get_nonstandard_attr("SameSite") == "lax"


@pytest.mark.anyio
async def test_player_me_uses_server_session_and_returns_authoritative_data():
    client, fake = make_client()
    async with client:
        await login(client)
        response = await client.get("/api/v1/player/me")

        assert response.status_code == 200
        assert response.json() == {"player": {"player": {"id": 17}}}
        assert fake.calls[-1] == ("player.my_info", {}, "game-secret-token")
        assert response.headers["cache-control"] == "no-store"


@pytest.mark.anyio
async def test_logout_requires_csrf_revokes_game_session_and_clears_cookie():
    client, fake = make_client()
    async with client:
        csrf_token = (await login(client))["csrf_token"]
        response = await client.post(
            "/api/v1/auth/logout", headers={"X-CSRF-Token": csrf_token}
        )

        assert response.status_code == 204
        assert fake.calls[-1] == ("auth.logout", {}, "game-secret-token")
        me = await client.get("/api/v1/player/me")
        assert me.status_code == 401
        assert client.cookies.get("twclone_session") is None


@pytest.mark.anyio
async def test_logout_without_csrf_is_rejected():
    client, fake = make_client()
    async with client:
        await login(client)
        response = await client.post("/api/v1/auth/logout")

        assert response.status_code == 403
        assert response.json()["error"]["code"] == "csrf_invalid"
        assert all(call[0] != "auth.logout" for call in fake.calls)


@pytest.mark.anyio
async def test_invalid_credentials_and_upstream_failures_are_safely_mapped():
    client, fake = make_client()
    async with client:
        fake.login_refused = True
        denied = await client.post(
            "/api/v1/auth/login",
            json={"username": "newguy", "password": "wrong"},
        )
        assert denied.status_code == 401
        assert denied.json()["error"]["message"] == "Invalid username or password"

        fake.login_refused = False
        fake.unavailable = True
        unavailable = await client.post(
            "/api/v1/auth/login",
            json={"username": "newguy", "password": "secret"},
        )
        assert unavailable.status_code == 503
        assert unavailable.json()["error"]["code"] == "game_server_unavailable"

        fake.unavailable = False
        fake.timeout = True
        timed_out = await client.post(
            "/api/v1/auth/login",
            json={"username": "newguy", "password": "secret"},
        )
        assert timed_out.status_code == 504
        assert timed_out.json()["error"]["code"] == "game_server_timeout"


@pytest.mark.anyio
async def test_login_rejects_unknown_fields_and_cross_origin_requests():
    client, _ = make_client()
    async with client:
        invalid_body = await client.post(
            "/api/v1/auth/login",
            json={"username": "newguy", "password": "secret", "host": "attacker"},
        )
        assert invalid_body.status_code == 422
        assert invalid_body.json()["error"]["code"] == "invalid_request"

        cross_origin = await client.post(
            "/api/v1/auth/login",
            json={"username": "newguy", "password": "secret"},
            headers={"Origin": "https://attacker.example"},
        )
        assert cross_origin.status_code == 403
