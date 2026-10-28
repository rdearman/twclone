#!/usr/bin/env python3
"""Convert PostgreSQL players.passwd values before enabling hash-only auth.

Requires psycopg2 and the maintained libargon2 runtime library. Connection
details are read only from DATABASE_URL and are never printed.
"""

import argparse
import os
import re
import sys

import psycopg2
try:
    from .argon2id_passwords import encoded_hash, is_valid_encoded, load_argon2
except ImportError:  # Direct script invocation from tools/.
    from argon2id_passwords import encoded_hash, is_valid_encoded, load_argon2


def migrate_rows(cursor, api, schema="public"):
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", schema):
        raise ValueError("invalid schema identifier")
    table = '"' + schema + '"."players"'
    cursor.execute(f"SELECT player_id, passwd FROM {table} ORDER BY player_id FOR UPDATE")
    rows = cursor.fetchall()
    pending = []
    already_hashed = 0
    for player_id, value in rows:
        if not isinstance(value, str):
            raise ValueError("password column contains a non-text value")
        if value.startswith("$argon2id$"):
            if not is_valid_encoded(api, value):
                raise ValueError("password column contains a malformed Argon2id value")
            already_hashed += 1
            continue
        pending.append((encoded_hash(api, value.encode("utf-8")), player_id))
    if pending:
        cursor.executemany(f"UPDATE {table} SET passwd = %s WHERE player_id = %s", pending)
    return len(rows), len(pending), already_hashed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dry-run", action="store_true", help="hash and validate without committing")
    args = parser.parse_args()
    dsn = os.environ.get("DATABASE_URL")
    if not dsn:
        parser.error("DATABASE_URL must be set")

    try:
        api = load_argon2()
        with psycopg2.connect(dsn, application_name="twclone-password-migration") as conn:
            with conn.cursor() as cursor:
                cursor.execute(
                    "SELECT count(*) FROM pg_stat_activity "
                    "WHERE datname = current_database() AND pid <> pg_backend_pid()"
                )
                if cursor.fetchone()[0]:
                    raise RuntimeError("other database sessions are connected; stop the application and retry")
                cursor.execute("SELECT pg_advisory_xact_lock(741209, 119)")
                total, converted, hashed = migrate_rows(cursor, api)
                if args.dry_run:
                    conn.rollback()
                else:
                    conn.commit()
        action = "would convert" if args.dry_run else "converted"
        print(f"Password migration {action} {converted} account(s); {hashed} already hashed; {total} total.")
        return 0
    except Exception as exc:
        # Never include exception details from database/library calls: they can
        # contain SQL parameter data or connection details.
        print(f"Password migration failed ({type(exc).__name__}); no credentials or hashes were logged.", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
