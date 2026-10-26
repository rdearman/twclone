"""Environment-backed settings for the HTTPS gateway."""

from __future__ import annotations

import os
from dataclasses import dataclass
from typing import Mapping
from urllib.parse import urlsplit


def _csv(value: str) -> tuple[str, ...]:
    return tuple(part.strip() for part in value.split(",") if part.strip())


def _port(name: str, value: str | None, default: int) -> int:
    if value is None or value.strip() == "":
        return default
    try:
        port = int(value)
    except ValueError as exc:
        raise ValueError(f"{name} must be an integer port") from exc
    if not 1 <= port <= 65535:
        raise ValueError(f"{name} must be between 1 and 65535")
    return port


def _boolean(name: str, value: str | None, default: bool) -> bool:
    if value is None:
        return default
    normalized = value.strip().lower()
    if normalized in {"1", "true", "yes", "on"}:
        return True
    if normalized in {"0", "false", "no", "off"}:
        return False
    raise ValueError(f"{name} must be true or false")


def _origin(value: str | None) -> str | None:
    origin = (value or "").strip().rstrip("/")
    if not origin:
        return None
    parsed = urlsplit(origin)
    try:
        parsed.port
    except ValueError as exc:
        raise ValueError("GATEWAY_PUBLIC_ORIGIN must be an HTTP(S) origin") from exc
    if (
        parsed.scheme not in {"http", "https"}
        or not parsed.netloc
        or not parsed.hostname
        or parsed.username is not None
        or parsed.password is not None
        or parsed.path
        or parsed.query
        or parsed.fragment
    ):
        raise ValueError("GATEWAY_PUBLIC_ORIGIN must be an HTTP(S) origin")
    return origin


@dataclass(frozen=True, slots=True)
class Settings:
    environment: str = "development"
    host: str = "127.0.0.1"
    port: int = 8080
    log_level: str = "info"
    game_server_host: str = "127.0.0.1"
    game_server_port: int = 1234
    game_server_timeout_seconds: float = 5.0
    trusted_hosts: tuple[str, ...] = ("localhost", "127.0.0.1", "testserver")
    cors_origins: tuple[str, ...] = ()
    public_origin: str | None = None
    session_cookie_name: str = "twclone_session"
    session_cookie_secure: bool = True
    session_cookie_samesite: str = "lax"
    session_lifetime_seconds: int = 86400

    @classmethod
    def from_env(cls, environ: Mapping[str, str] | None = None) -> "Settings":
        env = os.environ if environ is None else environ
        environment = env.get("GATEWAY_ENV", "development").strip().lower()
        if environment not in {"development", "test", "production"}:
            raise ValueError("GATEWAY_ENV must be development, test, or production")

        try:
            timeout = float(env.get("TWCLONE_SERVER_TIMEOUT_SECONDS", "5"))
        except ValueError as exc:
            raise ValueError("TWCLONE_SERVER_TIMEOUT_SECONDS must be numeric") from exc
        if timeout <= 0:
            raise ValueError("TWCLONE_SERVER_TIMEOUT_SECONDS must be positive")

        default_trusted_hosts = "localhost,127.0.0.1,testserver"
        cookie_samesite = env.get("GATEWAY_SESSION_COOKIE_SAMESITE", "lax").strip().lower()
        if cookie_samesite not in {"lax", "strict", "none"}:
            raise ValueError("GATEWAY_SESSION_COOKIE_SAMESITE must be lax, strict, or none")
        cookie_secure = _boolean(
            "GATEWAY_SESSION_COOKIE_SECURE",
            env.get("GATEWAY_SESSION_COOKIE_SECURE"),
            environment == "production",
        )
        if cookie_samesite == "none" and not cookie_secure:
            raise ValueError("SameSite=None requires GATEWAY_SESSION_COOKIE_SECURE=true")

        return cls(
            environment=environment,
            host=env.get("GATEWAY_HOST", "127.0.0.1").strip(),
            port=_port("GATEWAY_PORT", env.get("GATEWAY_PORT"), 8080),
            log_level=env.get("GATEWAY_LOG_LEVEL", "info").strip().lower(),
            game_server_host=env.get("TWCLONE_SERVER_HOST", "127.0.0.1").strip(),
            game_server_port=_port(
                "TWCLONE_SERVER_PORT", env.get("TWCLONE_SERVER_PORT"), 1234
            ),
            game_server_timeout_seconds=timeout,
            trusted_hosts=_csv(env.get("GATEWAY_TRUSTED_HOSTS", default_trusted_hosts)),
            cors_origins=_csv(env.get("GATEWAY_CORS_ORIGINS", "")),
            public_origin=_origin(env.get("GATEWAY_PUBLIC_ORIGIN")),
            session_cookie_name=env.get(
                "GATEWAY_SESSION_COOKIE_NAME", "twclone_session"
            ).strip(),
            session_cookie_secure=cookie_secure,
            session_cookie_samesite=cookie_samesite,
            session_lifetime_seconds=86400,
        )
