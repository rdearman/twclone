#!/usr/bin/env python3
"""Live combat.attack fail-closed regression for a locked PostgreSQL target.

Requires a running server (HOST/PORT) and a disposable local PostgreSQL
database named twclone_qa_*. The test locks the authoritative target row in a
second PostgreSQL session while issuing combat.attack through the server.
"""

import os
from pathlib import Path
import subprocess
import tempfile
import time
from urllib.parse import urlparse
import uuid

from twclient import TWClient


def require_qa_database():
    url = os.environ.get("QA_DATABASE_URL", "")
    parsed = urlparse(url)
    if parsed.scheme not in ("postgres", "postgresql"):
        raise SystemExit("QA_DATABASE_URL must be a PostgreSQL URL")
    if parsed.hostname not in ("localhost", "127.0.0.1", "::1"):
        raise SystemExit("refusing non-local database host")
    if not parsed.path.lstrip("/").startswith("twclone_qa_"):
        raise SystemExit("refusing database not named twclone_qa_*")
    if not os.environ.get("HOST") or not os.environ.get("PORT"):
        raise SystemExit("HOST and PORT must identify the running test server")
    return url


def psql(url, sql):
    result = subprocess.run(
        ["psql", url, "-X", "-qAt", "-v", "ON_ERROR_STOP=1", "-c", sql],
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    return result.stdout.strip()


def command(client, name, data):
    client.send_json({"command": name, "data": data})
    return client.recv_next_non_notice()


def require_ok(response, label):
    if response.get("status") != "ok":
        raise RuntimeError(f"{label} failed: {response}")


def main():
    database_url = require_qa_database()
    host, port = os.environ["HOST"], int(os.environ["PORT"])
    suffix = uuid.uuid4().hex[:12]
    attacker_name = "combat_lock_a_" + suffix
    target_name = "combat_lock_t_" + suffix
    password = "RC2-combat-lock-test"
    attacker = TWClient(host, port)
    target = TWClient(host, port)
    admin = TWClient(host, port)
    holder = None

    try:
        for client, username in ((attacker, attacker_name), (target, target_name)):
            client.connect()
            if not client.register(username, password, fail_if_exists=True):
                raise RuntimeError(f"could not register disposable test user {username}")
            if not client.login(username, password):
                raise RuntimeError(f"could not log in disposable test user {username}")

        admin.connect()
        if not admin.login("System", "BOT"):
            raise RuntimeError("could not authenticate sysop test session")

        require_ok(command(admin, "sys.raw_sql_exec", {
            "sql": "INSERT INTO sectors(sector_id,name) VALUES (11,'combat lock regression') "
                   "ON CONFLICT(sector_id) DO NOTHING"
        }), "create combat sector")
        require_ok(command(admin, "sys.raw_sql_exec", {
            "sql": "UPDATE players SET sector_id=11 WHERE player_id IN (%d,%d)"
                   % (attacker.player_id, target.player_id)
        }), "position test players")
        require_ok(command(admin, "sys.raw_sql_exec", {
            "sql": "UPDATE ships SET sector_id=11,hull=100,shields=0 "
                   "WHERE ship_id IN (SELECT ship_id FROM players WHERE player_id IN (%d,%d))"
                   % (attacker.player_id, target.player_id)
        }), "repair test ships")

        attacker_ship = int(psql(database_url,
            "SELECT ship_id FROM players WHERE player_id=%d" % attacker.player_id))
        target_ship = int(psql(database_url,
            "SELECT ship_id FROM players WHERE player_id=%d" % target.player_id))
        hull_before = int(psql(database_url,
            "SELECT hull FROM ships WHERE ship_id=%d" % target_ship))

        marker = Path(tempfile.mktemp(prefix="twclone-combat-target-locked-"))
        holder = subprocess.Popen(
            ["psql", database_url, "-X", "-v", "ON_ERROR_STOP=1"],
            stdin=subprocess.PIPE,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            text=True,
        )
        holder.stdin.write(
            "BEGIN;\n"
            f"SELECT ship_id FROM ships WHERE ship_id={target_ship} FOR UPDATE;\n"
            f"\\! touch {marker}\n"
            "SELECT pg_sleep(5);\n"
            "ROLLBACK;\n"
        )
        holder.stdin.close()
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline and not marker.exists():
            if holder.poll() is not None:
                raise RuntimeError("PostgreSQL lock holder exited before acquiring target row")
            time.sleep(0.05)
        if not marker.exists():
            raise TimeoutError("PostgreSQL target row lock was not acquired")

        response = command(attacker, "combat.attack", {"target_ship_id": target_ship})
        if response.get("status") not in ("error", "refused"):
            raise AssertionError(f"locked target attack was not rejected: {response}")
        response_data = response.get("data")
        if isinstance(response_data, dict) and response_data.get("success") is True:
            raise AssertionError(f"locked target attack reported success: {response}")

        holder.wait(timeout=10)
        hull_after = int(psql(database_url,
            "SELECT hull FROM ships WHERE ship_id=%d" % target_ship))
        if hull_after != hull_before:
            raise AssertionError(
                f"failed attack changed target hull: {hull_before} -> {hull_after}")
        print("PASS: locked combat target returned an error and persisted hull stayed "
              f"{hull_before} (attacker ship {attacker_ship}, target ship {target_ship}).")
    finally:
        if holder is not None and holder.poll() is None:
            holder.terminate()
            holder.wait(timeout=5)
        for client in (attacker, target, admin):
            client.close()


if __name__ == "__main__":
    main()
