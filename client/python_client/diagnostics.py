"""Bounded, publication-safe protocol diagnostics for player feedback."""
from __future__ import annotations

import copy
import re
import sys
import time
from collections import deque
from typing import Any

_SENSITIVE = ("pass", "token", "secret", "cookie", "auth", "email", "ip", "path", "private", "chat", "message")
_SAFE_FIELDS = {
    "id", "request_id", "correlation_id", "command", "timestamp", "status", "type",
    "response_type", "error_code", "duration_ms", "timeout", "data", "meta", "context",
    "client", "client_version", "build_id", "protocol_version", "os", "sector_id", "ship_id",
    "planet_id", "player_id", "turns_remaining", "credits", "quantity", "amount", "count",
    "level", "class", "action", "destination", "to_sector_id", "from_sector_id", "cargo",
    "fighters", "shields", "hull", "commodity", "port_id", "port_class", "error",
    "last_failed_command", "menu", "workflow", "stack_trace", "request", "response",
}
_EMAIL = re.compile(r"(?i)\b[A-Z0-9._%+-]+@[A-Z0-9.-]+\.[A-Z]{2,}\b")
_IP = re.compile(r"\b(?:\d{1,3}\.){3}\d{1,3}(?::\d{1,5})?\b")
_IPV6 = re.compile(r"(?i)(?<![A-Za-z0-9])(?:[0-9a-f]{0,4}:){2,7}[0-9a-f:]{0,4}(?![A-Za-z0-9])")
_PATH = re.compile(r"(?:/(?:home|Users|tmp|var|opt|mnt|private|Volumes)/|[A-Za-z]:\\)[^\s\"']+")
_BEARER = re.compile(r"(?i)\bbearer\s+[A-Za-z0-9._~+/-]+=*")
_ASSIGNMENT = re.compile(r"(?i)\b(password|token|secret|api[_-]?key)\s*[:=]\s*[^\s,;]+")
_JWT = re.compile(r"\beyJ[A-Za-z0-9_-]{8,}\.[A-Za-z0-9_-]{8,}\.[A-Za-z0-9_-]{8,}\b")
_GH_TOKEN = re.compile(r"\b(?:gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,})\b")


def sanitize_text(value: str) -> str:
    # Structured key filtering is the primary control. These patterns remove
    # common personal data/credential forms in player-authored free text.
    value = _EMAIL.sub("[redacted-email]", value)
    value = _IP.sub("[redacted-ip]", value)
    value = _IPV6.sub("[redacted-ip]", value)
    value = _PATH.sub("[redacted-path]", value)
    value = _BEARER.sub("[redacted-credential]", value)
    value = _ASSIGNMENT.sub("[redacted-credential]", value)
    value = _JWT.sub("[redacted-credential]", value)
    value = _GH_TOKEN.sub("[redacted-credential]", value)
    return value[:12000]


def sanitize(value: Any, depth: int = 0) -> Any:
    if depth > 8:
        return "[depth-limited]"
    if isinstance(value, dict):
        result = {}
        for key, item in value.items():
            name = str(key).lower()
            if any(part in name for part in _SENSITIVE) or name not in _SAFE_FIELDS:
                continue
            result[str(key)] = sanitize(item, depth + 1)
        return result
    if isinstance(value, (list, tuple)):
        return [sanitize(item, depth + 1) for item in list(value)[:100]]
    if isinstance(value, str):
        return sanitize_text(value)
    if value is None or isinstance(value, (bool, int, float)):
        return value
    return None


class DiagnosticBuffer:
    def __init__(self, capacity: int = 100):
        if capacity < 1 or capacity > 100:
            raise ValueError("capacity must be in 1..100")
        self._capacity = capacity
        self._items: deque[dict[str, Any]] = deque(maxlen=capacity)

    def append(self, entry: dict[str, Any]) -> None:
        clean = sanitize(entry)
        if isinstance(clean, dict):
            self._items.append(clean)

    def recent(self, count: int = 25) -> list[dict[str, Any]]:
        if count not in (10, 25, 50, 100):
            raise ValueError("count must be one of 10, 25, 50, 100")
        return copy.deepcopy(list(self._items)[-count:])

    def __len__(self) -> int:
        return len(self._items)


def client_context(hud: dict[str, Any] | None = None) -> dict[str, Any]:
    hud = hud if isinstance(hud, dict) else {}
    return sanitize({"client": "python", "client_version": "3", "build_id": "", "protocol_version": "v3",
                     "os": sys.platform, "sector_id": hud.get("sector_id"), "ship_id": hud.get("ship_id"),
                     "planet_id": hud.get("planet_id"), "player_id": hud.get("player_id")})


def make_exchange(request: dict[str, Any], response: dict[str, Any] | None,
                  duration_ms: int, context: dict[str, Any] | None = None) -> dict[str, Any]:
    command = request.get("command", "")
    # Authentication payloads and private communication are not captured at all.
    if str(command).startswith("auth.") or str(command).startswith(("chat.", "mail.", "comm.")):
        return {"timestamp": time.time(), "request_id": request.get("id"), "command": command,
                "response_type": (response or {}).get("type"), "status": (response or {}).get("status"),
                "duration_ms": duration_ms, "timeout": response is None,
                "context": sanitize(context or {})}
    return {"timestamp": time.time(), "request_id": request.get("id"), "command": command,
            "request": sanitize(request.get("data", {})), "response": sanitize((response or {}).get("data", {})),
            "response_type": (response or {}).get("type"), "status": (response or {}).get("status"),
            "error_code": ((response or {}).get("error") or {}).get("code") if isinstance((response or {}).get("error"), dict) else None,
            "duration_ms": duration_ms, "timeout": response is None,
            "context": sanitize(context or {})}
