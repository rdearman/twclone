"""Real PostgreSQL regression for trade replay turn accounting.

This test creates and drops only a new database named by
TRADE_TURN_QA_DATABASE. It starts an isolated BigBang-created server on
ephemeral ports. Required environment:

  TRADE_TURN_QA_DATABASE=twclone_trade_turn_qa_<unique>
  TRADE_TURN_QA_ADMIN_DSN=<PostgreSQL admin DSN>
  TRADE_TURN_QA_APP_TEMPLATE=<application DSN containing %DB%>

BIGBANG_BIN and SERVER_BIN can select isolated candidate executables.
"""

from __future__ import annotations

import json
import os
from pathlib import Path
import secrets
import signal
import socket
import subprocess
import sys
import tempfile
import time

import psycopg2
from psycopg2 import sql

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests.v2"))
from test_pg_bigbang_economy_profit import (  # noqa: E402
    free_port,
    login_client,
    market_rows,
    profitable_routes,
    status_ok,
)


def trade_snapshot(conn, account: str, port_id: int, commodity: str, action: str):
    with conn.cursor() as cur:
        cur.execute(
            "SELECT player_id,ship_id,credits FROM players WHERE name=%s", (account,)
        )
        player_id, ship_id, credits = cur.fetchone()
        cur.execute(
            "SELECT COALESCE((SELECT quantity FROM ship_cargo "
            "WHERE ship_id=%s AND commodity_code=%s),0)",
            (ship_id, commodity),
        )
        cargo = cur.fetchone()[0]
        cur.execute(
            "SELECT quantity FROM entity_stock WHERE entity_type='port' "
            "AND entity_id=%s AND commodity_code=%s",
            (port_id, commodity),
        )
        stock = cur.fetchone()[0]
        cur.execute(
            "SELECT count(*) FROM trade_log WHERE player_id=%s AND port_id=%s "
            "AND commodity=%s AND action=%s",
            (player_id, port_id, commodity, action),
        )
        trade_count = cur.fetchone()[0]
        cur.execute("SELECT turns_remaining FROM turns WHERE player_id=%s", (player_id,))
        turns = cur.fetchone()[0]
    return {
        "credits": credits,
        "cargo": cargo,
        "stock": stock,
        "trade_log_count": trade_count,
        "turns": turns,
    }


def all_trade_state(conn, account: str, ports: list[int], commodity: str):
    """Capture every economic field affected by either side of a trade."""
    with conn.cursor() as cur:
        cur.execute(
            "SELECT player_id,ship_id,credits FROM players WHERE name=%s", (account,)
        )
        player_id, ship_id, credits = cur.fetchone()
        cur.execute(
            "SELECT COALESCE((SELECT quantity FROM ship_cargo "
            "WHERE ship_id=%s AND commodity_code=%s),0)",
            (ship_id, commodity),
        )
        cargo = cur.fetchone()[0]
        cur.execute("SELECT turns_remaining FROM turns WHERE player_id=%s", (player_id,))
        turns = cur.fetchone()[0]
        port_state = {}
        for port_id in sorted(set(ports)):
            cur.execute(
                "SELECT quantity FROM entity_stock WHERE entity_type='port' "
                "AND entity_id=%s AND commodity_code=%s",
                (port_id, commodity),
            )
            row = cur.fetchone()
            stock = row[0] if row else None
            cur.execute(
                "SELECT action,count(*) FROM trade_log WHERE player_id=%s "
                "AND port_id=%s AND commodity=%s GROUP BY action ORDER BY action",
                (player_id, port_id, commodity),
            )
            port_state[port_id] = (stock, tuple(cur.fetchall()))
    return credits, cargo, turns, port_state


def assert_error(response: dict, label: str) -> None:
    if response.get("status") not in ("error", "refused"):
        raise AssertionError(f"{label} was not rejected: {response!r}")


def set_position(conn, account: str, sector_id: int) -> None:
    with conn.cursor() as cur:
        cur.execute("SELECT player_id,ship_id FROM players WHERE name=%s", (account,))
        player_id, ship_id = cur.fetchone()
        cur.execute("UPDATE players SET sector_id=%s WHERE player_id=%s", (sector_id, player_id))
        cur.execute("UPDATE ships SET sector_id=%s WHERE ship_id=%s", (sector_id, ship_id))
    conn.commit()


def run_trade(client, command: str, payload: dict) -> dict:
    client.send_json({"command": command, "data": payload})
    return client.recv_next_non_notice()


def assert_first_and_replay(conn, host: str, port: int, account: str,
                            password: str, command: str, sector: int,
                            port_id: int, commodity: str, key: str,
                            other_sector: int, other_port_id: int) -> None:
    action = "buy" if command == "trade.buy" else "sell"
    payload = {
        "sector_id": sector,
        "port_id": port_id,
        "account": 0,
        "idempotency_key": key,
        "items": [{"commodity": commodity, "quantity": 1}],
    }
    client = login_client(host, port, account, password)
    try:
        before = trade_snapshot(conn, account, port_id, commodity, action)
        first = run_trade(client, command, payload)
        if not status_ok(first):
            raise AssertionError(f"initial {command} failed: {first!r}")
        after_first = trade_snapshot(conn, account, port_id, commodity, action)
        if after_first["turns"] != before["turns"] - 1:
            raise AssertionError(
                f"first {command} did not consume exactly one turn: "
                f"before={before['turns']} after={after_first['turns']}"
            )
        expected = dict(after_first)
        replay = run_trade(client, command, dict(payload))
        if not status_ok(replay) or replay.get("type") != first.get("type") \
                or replay.get("data") != first.get("data"):
            raise AssertionError(f"{command} replay did not return the original receipt")
        after_replay = trade_snapshot(conn, account, port_id, commodity, action)
        if after_replay != expected:
            raise AssertionError(
                f"{command} replay changed authoritative state: "
                f"first={expected!r} replay={after_replay!r}"
            )
        print(
            f"{command} authoritative turns: {before['turns']} -> "
            f"{after_first['turns']} -> replay {after_replay['turns']}"
        )

        # Conflicting reuse must be rejected before any turn, balance, cargo,
        # port stock, or trade-log change, even when the request names another
        # sector. The DB key is unique independently of sector_id.
        expected_all = all_trade_state(conn, account, [port_id, other_port_id], commodity)
        conflicts = []
        changed = dict(payload)
        changed["items"] = [{"commodity": commodity, "quantity": 2}]
        conflicts.append((changed, "changed quantity"))
        changed_sector = dict(payload)
        changed_sector["sector_id"] = other_sector
        conflicts.append((changed_sector, "different sector"))
        changed_port = dict(payload)
        changed_port["port_id"] = other_port_id
        conflicts.append((changed_port, "different port"))
        opposite = dict(payload)
        conflicts.append((opposite, "opposite direction", "trade.sell" if command == "trade.buy" else "trade.buy"))
        for conflict in conflicts:
            conflict_payload, label = conflict[0], conflict[1]
            conflict_command = conflict[2] if len(conflict) > 2 else command
            assert_error(
                run_trade(client, conflict_command, conflict_payload),
                f"{command} key reused with {label}",
            )
            if all_trade_state(conn, account, [port_id, other_port_id], commodity) != expected_all:
                raise AssertionError(
                    f"{command} key reuse with {label} changed authoritative state"
                )

        for invalid_key in (None, "", "x" * 129, 17):
            invalid = dict(payload)
            if invalid_key is None:
                invalid.pop("idempotency_key")
            else:
                invalid["idempotency_key"] = invalid_key
            assert_error(run_trade(client, command, invalid), f"{command} invalid key")
            if all_trade_state(conn, account, [port_id, other_port_id], commodity) != expected_all:
                raise AssertionError(f"{command} invalid-key request changed state")
    finally:
        client.close()


def main() -> None:
    db_name = os.environ.get("TRADE_TURN_QA_DATABASE", "")
    admin_dsn = os.environ.get("TRADE_TURN_QA_ADMIN_DSN")
    app_template = os.environ.get("TRADE_TURN_QA_APP_TEMPLATE")
    if not db_name.startswith("twclone_trade_turn_qa_") or db_name == "twclone":
        raise SystemExit("TRADE_TURN_QA_DATABASE must be a unique twclone_trade_turn_qa_* name")
    if not admin_dsn or not app_template or "%DB%" not in app_template:
        raise SystemExit("Set TRADE_TURN_QA_ADMIN_DSN and TRADE_TURN_QA_APP_TEMPLATE (with %DB%).")

    bigbang = Path(os.environ.get("BIGBANG_BIN", ROOT / "bin/bigbang")).resolve()
    server_bin = Path(os.environ.get("SERVER_BIN", ROOT / "bin/server")).resolve()
    if not bigbang.is_file() or not server_bin.is_file():
        raise SystemExit("Built isolated bin/bigbang and bin/server are required")

    with psycopg2.connect(admin_dsn) as admin:
        with admin.cursor() as cur:
            cur.execute("SELECT 1 FROM pg_database WHERE datname=%s", (db_name,))
            if cur.fetchone():
                raise SystemExit("Refusing to use an existing database; choose a fresh disposable name")

    app_dsn = app_template.replace("%DB%", db_name)
    client_port, s2s_port = free_port(), free_port()
    account = "trade_turn_qa_" + secrets.token_hex(5)
    password = secrets.token_urlsafe(28)
    server = None
    server_log = None
    conn = None
    created = False
    try:
        with tempfile.TemporaryDirectory(prefix="twclone-trade-turn-qa-") as tmp:
            temp = Path(tmp)
            config = json.loads((ROOT / "bin/bigbang.json").read_text(encoding="utf-8"))
            config.update({
                "admin": admin_dsn,
                "db": db_name,
                "app": app_dsn,
                "sql_dir": str(ROOT / "sql/pg"),
                "sectors": int(os.environ.get("TRADE_TURN_QA_SECTORS", "2500")),
                "server_port": client_port,
                "s2s_port": s2s_port,
            })
            bb_dir = temp / "bigbang"
            bb_dir.mkdir()
            (bb_dir / "bigbang.json").write_text(json.dumps(config), encoding="utf-8")
            os.chmod(bb_dir / "bigbang.json", 0o600)
            created = True
            generated = subprocess.run(
                [str(bigbang)], cwd=bb_dir, input="y\n", text=True,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=900,
            )
            if generated.returncode:
                raise RuntimeError("BigBang failed on disposable database:\n" + generated.stdout[-5000:])

            conn = psycopg2.connect(app_dsn)
            server_dir = temp / "server"
            server_dir.mkdir()
            (server_dir / "bigbang.json").write_text(json.dumps(config), encoding="utf-8")
            os.chmod(server_dir / "bigbang.json", 0o600)
            server_log = (server_dir / "server.log").open("w", encoding="utf-8")
            server = subprocess.Popen(
                [str(server_bin)], cwd=server_dir, stdout=server_log,
                stderr=subprocess.STDOUT, start_new_session=True,
            )
            deadline = time.monotonic() + 45
            while time.monotonic() < deadline:
                if server.poll() is not None:
                    raise RuntimeError("isolated server exited during startup")
                try:
                    with socket.create_connection(("127.0.0.1", client_port), timeout=0.2):
                        break
                except OSError:
                    time.sleep(0.2)
            else:
                raise TimeoutError("isolated server did not start")

            from twclient import TWClient
            registration = TWClient("127.0.0.1", client_port, timeout=10)
            registration.connect()
            if not registration.register(account, password, fail_if_exists=True):
                raise AssertionError("disposable trade-turn player registration failed")
            registration.close()

            routes = profitable_routes(market_rows(conn))
            _, _, _, source, destination = routes["ORE"]["best"]
            set_position(conn, account, source["sector_id"])
            assert_first_and_replay(
                conn, "127.0.0.1", client_port, account, password, "trade.buy",
                source["sector_id"], source["port_id"], "ORE", "trade-turn-buy-" + secrets.token_hex(8),
                destination["sector_id"], destination["port_id"],
            )
            set_position(conn, account, destination["sector_id"])
            assert_first_and_replay(
                conn, "127.0.0.1", client_port, account, password, "trade.sell",
                destination["sector_id"], destination["port_id"], "ORE", "trade-turn-sell-" + secrets.token_hex(8),
                source["sector_id"], source["port_id"],
            )
            print(f"PASS trade.buy and trade.sell replay turn accounting on disposable database {db_name}")
    finally:
        if server is not None and server.poll() is None:
            os.killpg(server.pid, signal.SIGTERM)
            try:
                server.wait(timeout=12)
            except subprocess.TimeoutExpired:
                os.killpg(server.pid, signal.SIGKILL)
                server.wait(timeout=5)
        if server_log is not None:
            server_log.close()
        if conn is not None:
            conn.close()
        if created:
            admin = psycopg2.connect(admin_dsn)
            admin.autocommit = True
            try:
                with admin.cursor() as cur:
                    cur.execute("SELECT 1 FROM pg_database WHERE datname=%s", (db_name,))
                    if cur.fetchone():
                        cur.execute(sql.SQL("DROP DATABASE {}").format(sql.Identifier(db_name)))
            finally:
                admin.close()


if __name__ == "__main__":
    main()
