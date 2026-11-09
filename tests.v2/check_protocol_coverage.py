#!/usr/bin/env python3
"""Static PROTOCOL.v3 -> JSON acceptance coverage inventory.

This is deliberately a contract inventory, not a replacement test runner.  It
reads command headings from the authoritative protocol documents, registered
commands from the server registry, and command-bearing JSON cases from the
existing tests.v2 suites.
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DOCS = ROOT / "docs" / "PROTOCOL.v3"
SUITES = ROOT / "tests.v2"
DEFAULT_REPORT = SUITES / "protocol_coverage_report.json"

HEADING = re.compile(r"^###\s+(.+?)\s*$")
COMMAND = re.compile(r"`([^`]+)`")
REGISTERED = re.compile(r'^\s*\{"([^\"]+)",\s*[^,]+,', re.MULTILINE)


def expand_heading(raw: str) -> list[str]:
    values = []
    tokens = COMMAND.findall(raw)
    namespace = None
    for token in tokens:
        if "." in token and not token.endswith(".*"):
            namespace = token.rsplit(".", 1)[0]
        # A grouped heading such as `bank.deposit` / `bank.withdraw` has
        # separate backtick tokens in the protocol documents.  Keep wildcard
        # headings as contract groups; they are not executable commands.
        if token.endswith(".*") or token.startswith("http"):
            continue
        value = token
        if "." not in value and namespace:
            value = f"{namespace}.{value}"
        if value not in values:
            values.append(value)
    return values


def protocol_inventory() -> dict[str, dict]:
    inventory: dict[str, dict] = {}
    for path in sorted(DOCS.glob("*.md")):
        section = ""
        for number, line in enumerate(path.read_text().splitlines(), 1):
            if line.startswith("## "):
                section = line
            match = HEADING.match(line)
            if not match:
                continue
            heading = match.group(1)
            lowered = f"{heading} {section}".lower()
            if path.name in {"03_Message_Types_and_Semantics.md", "08_Engine_Event_Contract.md"}:
                kind = "engine_or_event"
            elif "server -> client" in lowered or "(event)" in lowered or "events" in section.lower():
                kind = "response_or_event"
            elif "(engine)" in lowered:
                kind = "engine_or_event"
            else:
                kind = "client_request"
            for command in expand_heading(match.group(1)):
                item = inventory.setdefault(command, {"references": []})
                item["references"].append({"file": str(path.relative_to(ROOT)), "line": number})
                item["kinds"] = sorted(set(item.get("kinds", [])) | {kind})
                item["nyi"] = "[NYI]" in match.group(1)
    return inventory


def registered_commands() -> set[str]:
    source = (ROOT / "src" / "server_loop.c").read_text()
    return set(REGISTERED.findall(source))


def command_cases(value, result: dict[str, list[str]], source: str):
    if isinstance(value, dict):
        command = value.get("command")
        if isinstance(command, str):
            result.setdefault(command, []).append(source)
        for child in value.values():
            command_cases(child, result, source)
    elif isinstance(value, list):
        for child in value:
            command_cases(child, result, source)


def json_coverage() -> dict[str, list[str]]:
    result: dict[str, list[str]] = {}
    for path in sorted(SUITES.glob("*.json")):
        if path.name in {
            "regression_report.json", "regression_previous.json",
            "regression_failures.json", DEFAULT_REPORT.name,
        }:
            continue
        try:
            payload = json.loads(path.read_text())
        except json.JSONDecodeError:
            continue
        command_cases(payload, result, path.name)
    return result


def build_report() -> dict:
    docs = protocol_inventory()
    registered = registered_commands()
    covered = json_coverage()
    commands = []
    for name in sorted(docs):
        item = docs[name]
        kinds = item.get("kinds", ["client_request"])
        executable = "client_request" in kinds and not any(k in kinds for k in ("response_or_event", "engine_or_event"))
        commands.append({
            "command": name,
            "specification": item["references"],
            "registered": name in registered,
            "kinds": kinds,
            "implementation_state": (
                "IMPLEMENTED_OR_REGISTERED" if name in registered else
                "DEFERRED_NYI" if item.get("nyi", False) else
                "DEFERRED" if executable else
                "PRODUCED_RESPONSE_OR_EVENT" if "response_or_event" in kinds else
                "ENGINE_OR_EVENT"
            ),
            "executable_json_target": executable,
            "nyi": item.get("nyi", False),
            "json_cases": sorted(set(covered.get(name, []))),
            "protocol_covered": bool(covered.get(name)) if executable else None,
            "classification": (
                "covered" if covered.get(name) else
                "specification_blocker" if not executable else
                "uncovered"
            ),
        })
    executable_items = [x for x in commands if x["executable_json_target"]]
    covered_items = [x for x in executable_items if x["protocol_covered"]]
    registered_items = [x for x in executable_items if x["registered"]]
    category_counts = {}
    for item in commands:
        category = "client_request" if item["executable_json_target"] else (
            "engine_or_event" if "engine_or_event" in item["kinds"] else "response_or_event"
        )
        category_counts[category] = category_counts.get(category, 0) + 1
    return {
        "source": "docs/PROTOCOL.v3",
        "registered_source": "src/server_loop.c",
        "commands": commands,
        "summary": {
            "protocol_commands": len(executable_items),
            "protocol_commands_with_json": len(covered_items),
            "protocol_coverage_percent": round(100 * len(covered_items) / len(executable_items), 2) if executable_items else 100.0,
            "uncovered_commands": [x["command"] for x in executable_items if not x["protocol_covered"]],
            "registered_executable_commands": len(registered_items),
            "registered_executable_commands_with_json": len([x for x in registered_items if x["protocol_covered"]]),
            "registered_executable_uncovered": [x["command"] for x in registered_items if not x["protocol_covered"]],
            "specification_blockers": [x["command"] for x in commands if not x["executable_json_target"]],
            "category_counts": category_counts,
            "inventory_entries": len(commands),
            "multi_classified_entries": [
                x["command"] for x in commands if len(x["kinds"]) > 1
            ],
            "documented_unregistered_requests": [
                x["command"] for x in executable_items if not x["registered"]
            ],
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--report", type=Path, default=DEFAULT_REPORT)
    parser.add_argument("--check", action="store_true", help="fail if an executable protocol command has no JSON case")
    args = parser.parse_args()
    report = build_report()
    args.report.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report["summary"], indent=2))
    if args.check and report["summary"]["registered_executable_uncovered"]:
        print("Uncovered executable protocol commands:", file=__import__("sys").stderr)
        for command in report["summary"]["registered_executable_uncovered"]:
            print(f"  {command}", file=__import__("sys").stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
