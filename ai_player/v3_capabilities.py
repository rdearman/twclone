"""Protocol V3 capability and command-safety helpers."""

from __future__ import annotations

import uuid


RESPONSE_OR_EVENT_PREFIXES = ("event.", "engine.", "system.welcome")
PRIVILEGED_PREFIXES = ("sysop.", "s2s.", "engine.")
RESPONSE_ONLY_COMMANDS = {"move.result", "combat.hit", "system.welcome"}


def eligible_combat_targets(targets, opponent_model=None, threat_threshold=25.0):
    """Return visible targets with public evidence of hostility or NPC status.

    Sector presence alone is not evidence that another player is hostile. The
    server remains authoritative for final protections, but the bot should not
    initiate combat against an unidentified ordinary player.
    """
    opponent_model = opponent_model if isinstance(opponent_model, dict) else {}
    eligible = []
    for target in targets or []:
        if not isinstance(target, dict) or target.get("is_derelict"):
            continue
        if target.get("hostile") is True or target.get("is_hostile") is True or target.get("is_npc") is True:
            eligible.append(target)
            continue
        identities = (target.get("player_id"), target.get("ship_id"), target.get("id"))
        observed = next((opponent_model.get(str(value)) for value in identities
                         if value is not None and opponent_model.get(str(value)) is not None), None)
        try:
            threat = float((observed or {}).get("threat_score", 0) or 0)
        except (TypeError, ValueError):
            threat = 0.0
        if threat >= float(threat_threshold):
            eligible.append(target)
    return eligible


def is_safe_player_command(command: str) -> bool:
    """Return whether *command* is eligible for ordinary-player execution."""
    if not isinstance(command, str) or not command or command in RESPONSE_ONLY_COMMANDS:
        return False
    if command.startswith(PRIVILEGED_PREFIXES) or command.startswith(RESPONSE_OR_EVENT_PREFIXES):
        return False
    if command.startswith("system."):
        return False
    return "." in command


def normalize_command_catalogue(payload: dict) -> list[str]:
    """Normalize command-list variants emitted by V2/V3 servers."""
    if not isinstance(payload, dict):
        return []
    result = set()
    for item in payload.get("commands", []):
        if isinstance(item, str):
            name = item
        elif isinstance(item, dict):
            name = item.get("cmd") or item.get("command") or item.get("name")
        else:
            name = None
        if is_safe_player_command(name):
            result.add(name)
    return sorted(result)


def build_system_hello(config: dict) -> dict:
    """Build the unauthenticated V3 handshake request."""
    request_id = str(uuid.uuid4())
    version = config.get("client_version", "ai-player/3")
    return {
        "id": f"c-hello-{request_id[:8]}",
        "command": "system.hello",
        "data": {
            "client_version": version,
            "capabilities": ["ndjson", "schema-discovery", "idempotency", "state-readback"],
        },
        "meta": {"client_version": version},
    }


def capability_snapshot(catalogue: list[str], schemas: dict | None = None) -> dict:
    """Return serializable inventory data for logs, QA, and persisted state."""
    commands = sorted(set(catalogue or []))
    schemas = schemas or {}
    return {
        "commands": commands,
        "command_count": len(commands),
        "schema_count": len(set(commands) & set(schemas)),
        "missing_schemas": sorted(set(commands) - set(schemas)),
    }


def build_player_command(command: str, data: dict | None, session_token: str | None,
                         client_version: str | None, allowed_commands=None,
                         idempotency_key: str | None = None) -> dict:
    """Build a safe correlated request for an advertised player command."""
    if not is_safe_player_command(command):
        raise ValueError(f"not an ordinary player request: {command!r}")
    advertised = set(allowed_commands or [])
    if advertised and command not in advertised:
        raise ValueError(f"command is not advertised by server: {command}")
    if data is not None and not isinstance(data, dict):
        raise TypeError("command data must be an object")
    key = idempotency_key or str(uuid.uuid4())
    payload = dict(data or {})
    if command in {"trade.buy", "trade.sell"}:
        payload.setdefault("idempotency_key", key)
    return {
        "id": f"c-{key[:8]}",
        "command": command,
        "data": payload,
        "meta": {
            "client_version": client_version,
            "idempotency_key": key,
            "session_token": session_token,
        },
    }
