"""Disposable PostgreSQL integration for the V2-only BigBang SQL set.

The test derives connection strings from bin/bigbang.json, replaces the
database with unique twclone_bigbang_qa_* names, and drops only those names.
It never connects to the development database named twclone.
"""

from __future__ import annotations

import json
import os
from pathlib import Path
import re
import secrets
import subprocess
import sys
import tempfile

import psycopg2
from psycopg2 import sql

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
BIGBANG = ROOT / "bin/bigbang"
SQL_DIR = ROOT / "sql/pg"


def apply_lines(output: str) -> list[str]:
    applied = []
    for line in output.splitlines():
        if "Applying " not in line:
            continue
        match = re.search(r"\b\d{3}_[A-Za-z0-9_.-]+\.sql\b", line)
        if match:
            applied.append(match.group(0))
    return applied


def drop_if_present(admin_dsn: str, name: str) -> None:
    conn = psycopg2.connect(admin_dsn)
    conn.autocommit = True
    try:
        with conn.cursor() as cur:
            cur.execute("SELECT 1 FROM pg_database WHERE datname=%s", (name,))
            if cur.fetchone():
                cur.execute(sql.SQL("DROP DATABASE {} WITH (FORCE)").format(sql.Identifier(name)))
    finally:
        conn.close()


def run_bigbang(base: dict, name: str) -> tuple[str, str]:
    admin_dsn = base["admin"]
    app_dsn = base["app"].replace("%DB%", name)
    with psycopg2.connect(admin_dsn) as admin:
        with admin.cursor() as cur:
            cur.execute("SELECT 1 FROM pg_database WHERE datname=%s", (name,))
            if cur.fetchone():
                raise AssertionError(f"refusing to reuse existing QA database {name}")

    config = dict(base)
    config.update({
        "db": name,
        "app": app_dsn,
        "sql_dir": str(SQL_DIR),
        "sectors": 500,
        "server_port": 28654,
        "s2s_port": 48654,
    })
    with tempfile.TemporaryDirectory(prefix="twclone-bigbang-select-") as tmp:
        run_dir = Path(tmp)
        cfg = run_dir / "bigbang.json"
        cfg.write_text(json.dumps(config), encoding="utf-8")
        os.chmod(cfg, 0o600)
        generated = subprocess.run(
            [str(BIGBANG)], cwd=run_dir, input="y\n", text=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=900,
        )
    if generated.returncode:
        raise AssertionError("BigBang failed:\n" + generated.stdout[-6000:])

    applied = apply_lines(generated.stdout)
    common = sorted(
        path.name for path in SQL_DIR.glob("*.sql")
        if path.name not in {"107_seed_illegal_commodities.sql",
                             }
    )
    expected = common[:]
    expected.append("107_seed_illegal_commodities.sql")
    if applied != expected:
        raise AssertionError(f"unexpected script order: {applied!r}; expected {expected!r}")

    app_conn = psycopg2.connect(app_dsn)
    try:
        with app_conn.cursor() as cur:
            cur.execute("SELECT count(*) FROM sectors")
            sector_count = cur.fetchone()[0]
            if sector_count < 500:
                raise AssertionError(f"generated universe has only {sector_count} sectors")
            cur.execute("SELECT count(*) FROM sector_warps")
            if cur.fetchone()[0] <= 0:
                raise AssertionError("generated universe has no warp links")
            cur.execute(
                "SELECT convalidated FROM pg_constraint "
                "WHERE conname='players_passwd_argon2id_check'"
            )
            row = cur.fetchone()
            if not row or not row[0]:
                raise AssertionError("password enforcement migration 119 is absent")
            cur.execute("SELECT passwd FROM players")
            passwords = [row[0] for row in cur.fetchall()]
            from tools.argon2id_passwords import is_valid_encoded, load_argon2
            argon2 = load_argon2()
            if not passwords or not all(is_valid_encoded(argon2, value) for value in passwords):
                raise AssertionError("freshly generated account has a non-Argon2id password")
            cur.execute(
                "SELECT to_regclass('public.player_progress_adjustments'), "
                "to_regclass('public.player_event_streams'), "
                "to_regclass('public.player_events')"
            )
            v3_objects = cur.fetchone()
            if any(v3_objects):
                raise AssertionError(f"V2 selection created V3 objects: {v3_objects}")
        app_conn.commit()
    finally:
        app_conn.close()
    return name, generated.stdout


def main() -> None:
    if not BIGBANG.is_file():
        raise SystemExit("Build bin/bigbang first")
    base = json.loads((ROOT / "bin/bigbang.json").read_text(encoding="utf-8"))
    suffix = secrets.token_hex(5)
    names = [f"twclone_bigbang_qa_{suffix}_v2"]
    print(f"QA database: {names[0]}", flush=True)
    try:
        v2_name, _ = run_bigbang(base, names[0])
        print(f"PASS canonical SQL BigBang: V2 database={v2_name}")
    finally:
        for name in names:
            drop_if_present(base["admin"], name)


if __name__ == "__main__":
    main()
