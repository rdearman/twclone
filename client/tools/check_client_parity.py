#!/usr/bin/env python3
"""Check that required player commands are exposed by both clients.

This is deliberately a static check. It does not connect to a server and it
does not decide whether a server command is currently implemented; the
manifest records unavailable server-backed features separately.
"""
from __future__ import annotations

import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "client_parity_manifest.json"


def python_commands() -> set[str]:
    values: set[str] = set()
    menus = json.loads((ROOT / "python_client" / "menus.json").read_text())

    def visit(value: object) -> None:
        if isinstance(value, dict):
            command = value.get("command")
            if isinstance(command, str):
                values.add(command)
            for child in value.values():
                visit(child)
        elif isinstance(value, list):
            for child in value:
                visit(child)

    visit(menus)
    source = (ROOT / "python_client" / "client.py").read_text()
    values.update(re.findall(r'rpc\(\s*["\']([^"\']+)', source))
    values.update(re.findall(r'_hud_rpc\(\s*[^,]+,\s*["\']([^"\']+)', source))
    values.update(re.findall(r'["\']([a-z][a-z0-9_]*(?:\.[a-z0-9_]+)+)["\']', source))
    return values


def godot_commands() -> set[str]:
    values: set[str] = set()
    for path in (ROOT / "godot_client" / "godot").glob("*.gd"):
        source = path.read_text()
        values.update(re.findall(r'"([a-z][a-z0-9_.]+)"', source))
    return {value for value in values if "." in value}


def main() -> int:
    manifest = json.loads(MANIFEST.read_text())
    required = set(manifest["required_player_commands"])
    unavailable = set(manifest["intentionally_unavailable"])
    py = python_commands()
    gd = godot_commands()
    failures: list[str] = []
    for command in sorted(required):
        if command not in py and command not in unavailable:
            failures.append(f"Python missing {command}")
        if command not in gd and command not in unavailable:
            failures.append(f"Godot missing {command}")
    if failures:
        print("Client parity check failed:")
        print("\n".join(f"- {failure}" for failure in failures))
        return 1
    print(f"Client parity check passed ({len(required)} required commands).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
