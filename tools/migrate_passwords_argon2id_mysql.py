#!/usr/bin/env python3
"""Convert MySQL players.passwd values to the shared Argon2id format.

Requires mysql-connector-python and libargon2. Connection settings are read
from MYSQL_HOST, MYSQL_PORT, MYSQL_USER, MYSQL_PASSWORD, and MYSQL_DATABASE.
Run while the application is stopped, after a verified backup.
"""

import argparse
import os
import re
import sys

try:
    import mysql.connector
except ImportError:  # Import lazily so static tests do not require the driver.
    mysql = None
else:
    mysql = mysql.connector

try:
    from .argon2id_passwords import encoded_hash, is_valid_encoded, load_argon2
except ImportError:  # Direct script invocation from tools/.
    from argon2id_passwords import encoded_hash, is_valid_encoded, load_argon2


def migrate_rows(cursor, api):
    cursor.execute("SELECT player_id, passwd FROM players ORDER BY player_id FOR UPDATE")
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
        cursor.executemany("UPDATE players SET passwd = %s WHERE player_id = %s", pending)
    return len(rows), len(pending), already_hashed


def _has_process_privilege(cursor):
    cursor.execute("SHOW GRANTS")
    grants = [str(row[0]).upper() for row in cursor.fetchall()]
    return any(
        re.search(r"\bPROCESS\b", grant)
        or re.search(r"\bALL PRIVILEGES\s+ON\s+\*\.\*", grant)
        for grant in grants
    )


def _connect_from_environment():
    required = ("MYSQL_USER", "MYSQL_DATABASE")
    if any(not os.environ.get(key) for key in required):
        raise RuntimeError("MYSQL_USER and MYSQL_DATABASE are required")
    options = {
        "user": os.environ["MYSQL_USER"],
        "database": os.environ["MYSQL_DATABASE"],
        "password": os.environ.get("MYSQL_PASSWORD", ""),
        "autocommit": False,
    }
    if os.environ.get("MYSQL_SOCKET"):
        options["unix_socket"] = os.environ["MYSQL_SOCKET"]
    elif os.environ.get("MYSQL_HOST"):
        options["host"] = os.environ["MYSQL_HOST"]
    else:
        raise RuntimeError("MYSQL_HOST or MYSQL_SOCKET is required")
    port = os.environ.get("MYSQL_PORT")
    if port and "unix_socket" not in options:
        options["port"] = int(port)
    return mysql.connect(**options)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dry-run", action="store_true", help="hash and validate, then roll back")
    args = parser.parse_args()
    if mysql is None:
        print("Password migration failed (mysql-connector-python is not installed).", file=sys.stderr)
        return 1

    conn = None
    try:
        api = load_argon2()
        conn = _connect_from_environment()
        cursor = conn.cursor()
        # Require PROCESS visibility so a restricted account cannot mistake
        # hidden application sessions for exclusive database access.
        if not _has_process_privilege(cursor):
            raise RuntimeError("PROCESS privilege is required to verify exclusive access")
        cursor.execute(
            "SELECT COUNT(*) FROM information_schema.processlist "
            "WHERE db = DATABASE() AND id <> CONNECTION_ID()"
        )
        if cursor.fetchone()[0]:
            raise RuntimeError("other database sessions are connected; stop the application")
        cursor.execute(
            "SELECT ENGINE FROM information_schema.tables "
            "WHERE table_schema = DATABASE() AND table_name = 'players'"
        )
        table = cursor.fetchone()
        if not table or table[0].upper() != "INNODB":
            raise RuntimeError("players must be an InnoDB table for transactional conversion")
        conn.start_transaction()
        total, converted, hashed = migrate_rows(cursor, api)
        if args.dry_run:
            conn.rollback()
        else:
            conn.commit()
        action = "would convert" if args.dry_run else "converted"
        print(f"MySQL password migration {action} {converted} account(s); {hashed} already hashed; {total} total.")
        return 0
    except Exception as exc:
        if conn is not None:
            try:
                conn.rollback()
            except Exception:
                pass
        print(f"MySQL password migration failed ({type(exc).__name__}); credentials and hashes were not logged.", file=sys.stderr)
        return 1
    finally:
        if conn is not None:
            conn.close()


if __name__ == "__main__":
    raise SystemExit(main())
