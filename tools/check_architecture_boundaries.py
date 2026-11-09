#!/usr/bin/env python3
"""Cheap, deterministic architecture drift checks for V3.

This is deliberately a small source-tree check, not a second build system.
It validates boundaries that are otherwise easy to regress accidentally.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
MAKEFILE = ROOT / "bin/Makefile.am"
SERVER_LOOP = ROOT / "src/server_loop.c"
SQL_DIR = ROOT / "sql/pg"
MANIFEST = SQL_DIR / "migration_manifest.json"


def command_names() -> list[str]:
    text = SERVER_LOOP.read_text(encoding="utf-8")
    return re.findall(r'\{\s*"([^"\n]+)"\s*,\s*cmd_[A-Za-z0-9_]+\s*,', text)


def registered_c_units() -> set[str]:
    text = MAKEFILE.read_text(encoding="utf-8")
    return {Path(match).name for match in re.findall(r'\.\./(src/[^\\\s]+\.c)', text)}


def production_c_units() -> set[str]:
    units = set()
    for pattern in ("server_*.c", "game_db.c", "engine_consumer.c"):
        units.update(path.name for path in (ROOT / "src").glob(pattern))
    units.update(path.name for path in (ROOT / "src/db/repo").glob("repo_*.c"))
    return units


def manifest_sql() -> set[str]:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    entries = manifest.get("scripts", manifest.get("migrations", manifest))
    if isinstance(entries, dict):
        entries = entries.values()
    return {str(item.get("name", item.get("file"))) for item in entries}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--report-baseline", action="store_true",
                        help="report transitional omissions without failing")
    args = parser.parse_args()
    failures: list[str] = []

    names = command_names()
    duplicates = sorted({name for name in names if names.count(name) > 1})
    if duplicates:
        failures.append("duplicate command registrations: " + ", ".join(duplicates))

    missing_units = sorted(production_c_units() - registered_c_units())
    transitional_units = {"repo_trade.c", "repo_s2s.c"}
    new_missing = [name for name in missing_units if name not in transitional_units]
    if new_missing:
        failures.append("unregistered production C units: " + ", ".join(new_missing))
    if missing_units:
        print("TRANSITIONAL_UNREGISTERED_C_UNITS=" + ",".join(missing_units))

    manifest_names = manifest_sql()
    sql_names = {path.name for path in SQL_DIR.glob("*.sql")}
    missing_manifest = sorted(sql_names - manifest_names)
    missing_files = sorted(manifest_names - sql_names)
    if missing_manifest:
        failures.append("SQL files missing from migration manifest: " + ", ".join(missing_manifest))
    if missing_files:
        failures.append("manifest entries missing on disk: " + ", ".join(missing_files))

    if failures and not args.report_baseline:
        for failure in failures:
            print("ARCHITECTURE_FAIL: " + failure, file=sys.stderr)
        return 1
    for failure in failures:
        print("ARCHITECTURE_BASELINE: " + failure)
    if not failures:
        print("ARCHITECTURE_PASS: command, C-source and migration boundaries are consistent")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
