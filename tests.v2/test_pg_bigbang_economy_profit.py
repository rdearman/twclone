"""Disposable BigBang regression for quantity-feasible, profitable port routes.

Required environment:
  ECON_QA_DATABASE=twclone_econ_qa_<unique>
  ECON_QA_ADMIN_DSN=<PostgreSQL admin connection string>
  ECON_QA_APP_TEMPLATE=<app connection string containing %DB%>

The named database must not exist. This test creates it with BigBang, starts
the real server against it, performs one-unit buy/sell cycles for ORE, ORG and
EQU, and drops only that uniquely named database when done.
"""

from __future__ import annotations

import json
import math
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
COMMODITIES = ("ORE", "ORG", "EQU")


def free_port() -> int:
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def status_ok(response: dict) -> bool:
    return response.get("status") == "ok"


def nested_value(value, name):
    if isinstance(value, dict):
        if name in value:
            return value[name]
        for child in value.values():
            found = nested_value(child, name)
            if found is not None:
                return found
    elif isinstance(value, list):
        for child in value:
            found = nested_value(child, name)
            if found is not None:
                return found
    return None


def quote(row, side: str) -> int:
    """Mirror the public C quote formulas for fresh, unshocked markets."""
    base = row["base_price"]
    quantity = row["quantity"]
    capacity = row["capacity"]
    tech = row["techlevel"]
    elasticity = row["price_elasticity"]
    volatility = row["volatility_factor"]
    fill = quantity / capacity
    if side == "ask":  # price player pays at a port selling the commodity
        factor = (1 + (1 - fill) * elasticity * volatility) if fill < 0.5 else (
            1 - (fill - 0.5) * elasticity * volatility
        )
        factor *= 1 + (tech - 1) * 0.05
    else:  # bid: price a port buying from the player pays
        factor = (1 - (fill - 0.5) * elasticity * volatility) if fill > 0.5 else (
            1 + (0.5 - fill) * elasticity * volatility
        )
        factor *= 1 - (tech - 1) * 0.02
    return max(1, math.ceil(base * factor))


def market_rows(conn):
    with conn.cursor() as cur:
        cur.execute(
            "SELECT p.port_id,p.sector_id,pt.commodity,pt.mode,es.quantity,"
            "COALESCE(p.size,10)*1000 AS capacity,p.techlevel,c.base_price,"
            "ec.price_elasticity,ec.volatility_factor "
            "FROM ports p JOIN port_trade pt USING(port_id) "
            "JOIN entity_stock es ON es.entity_type='port' AND es.entity_id=p.port_id "
            "AND es.commodity_code=pt.commodity "
            "JOIN commodities c ON c.code=pt.commodity "
            "JOIN economy_curve ec USING(economy_curve_id) "
            "WHERE pt.commodity = ANY(%s) ORDER BY pt.commodity,p.port_id,pt.mode",
            (list(COMMODITIES),),
        )
        names = [col.name for col in cur.description]
        rows = [dict(zip(names, row)) for row in cur.fetchall()]
        cur.execute(
            "SELECT count(*) FROM market_shocks WHERE starts_at <= CURRENT_TIMESTAMP "
            "AND expires_at > CURRENT_TIMESTAMP"
        )
        active_shocks = cur.fetchone()[0]
    if active_shocks:
        raise AssertionError("fresh universe unexpectedly has active market shocks")
    return rows


def verify_fresh_black_market_hardware(conn):
    """Check BigBang initialized every mapped item with the approved policy."""
    with conn.cursor() as cur:
        cur.execute(
            "SELECT count(*) FROM ports p JOIN porttypes pt USING(porttype_id) "
            "WHERE pt.code='BLACKMARKET'"
        )
        black_market_ports = cur.fetchone()[0]
        if black_market_ports <= 0:
            raise AssertionError("fresh BigBang generated no black-market ports")

        cur.execute(
            "SELECT array_agg(hi.code ORDER BY hi.code) FROM hardware_items hi "
            "JOIN porttype_items pti USING(hardware_items_id) "
            "JOIN porttypes pt USING(porttype_id) "
            "WHERE pt.code='BLACKMARKET' AND pti.can_buy AND hi.enabled"
        )
        eligible_codes = cur.fetchone()[0] or []
        expected_codes = [
            "CLOAK", "DETONATOR", "FIGHTERS", "GENESIS", "HOLDS",
            "LSCANNER", "PSCANNER", "SHIELDS", "TWARP",
        ]
        if eligible_codes != expected_codes:
            raise AssertionError(
                "fresh V2 black-market eligibility differs from the approved "
                f"nine-item catalogue: {eligible_codes}"
            )

        cur.execute(
            "SELECT count(*) FROM ports p "
            "JOIN porttypes pt USING(porttype_id) "
            "JOIN porttype_items pti USING(porttype_id) "
            "JOIN hardware_items hi USING(hardware_items_id) "
            "WHERE pt.code='BLACKMARKET' AND pti.can_buy AND hi.enabled"
        )
        expected_rows = cur.fetchone()[0]
        if expected_rows <= 0:
            raise AssertionError("fresh universe has no eligible black-market hardware")

        cur.execute(
            "SELECT count(*) FROM ports p "
            "JOIN porttypes pt USING(porttype_id) "
            "JOIN porttype_items pti USING(porttype_id) "
            "JOIN hardware_items hi USING(hardware_items_id) "
            "LEFT JOIN port_hardware_stock phs "
            "ON phs.port_id=p.port_id AND phs.hardware_items_id=hi.hardware_items_id "
            "WHERE pt.code='BLACKMARKET' AND pti.can_buy AND hi.enabled "
            "AND (phs.port_id IS NULL OR phs.stock_quantity <> 2 "
            "OR phs.max_stock <> 5 OR phs.restock_quantity <> 1 "
            "OR phs.restock_interval_seconds <> 86400 "
            "OR phs.next_restock_at < CURRENT_TIMESTAMP + INTERVAL '23 hours 59 minutes' "
            "OR phs.next_restock_at > CURRENT_TIMESTAMP + INTERVAL '24 hours 1 minute')"
        )
        invalid_rows = cur.fetchone()[0]
        if invalid_rows:
            raise AssertionError(
                f"{invalid_rows} eligible black-market stock rows are missing or misconfigured"
            )

        cur.execute(
            "SELECT count(*) FROM ports p "
            "JOIN porttypes pt USING(porttype_id) "
            "JOIN port_hardware_stock phs USING(port_id) "
            "JOIN hardware_items hi USING(hardware_items_id) "
            "LEFT JOIN porttype_items pti "
            "ON pti.porttype_id=pt.porttype_id "
            "AND pti.hardware_items_id=hi.hardware_items_id "
            "WHERE pt.code='BLACKMARKET' "
            "AND (pti.hardware_items_id IS NULL OR NOT pti.can_buy OR NOT hi.enabled)"
        )
        ineligible_rows = cur.fetchone()[0]
        if ineligible_rows:
            raise AssertionError(
                f"{ineligible_rows} generated black-market inventory rows are ineligible"
            )
    return black_market_ports, expected_rows


def profitable_routes(rows):
    result = {}
    for commodity in COMMODITIES:
        commodity_rows = [r for r in rows if r["commodity"] == commodity]
        sellers = [r for r in commodity_rows if r["mode"] == "sell" and r["quantity"] >= 1]
        buyers = [
            r for r in commodity_rows
            if r["mode"] == "buy" and r["quantity"] < r["capacity"]
        ]
        feasible = []
        possible_pairs = 0
        for source in sellers:
            for destination in buyers:
                if source["port_id"] == destination["port_id"]:
                    continue
                # One cargo hold and one unit are feasible at both endpoints.
                possible_pairs += 1
                ask = quote(source, "ask")
                bid = quote(destination, "bid")
                # calculate_fees() currently returns zero for port trades.
                margin = bid - ask
                if margin > 0:
                    feasible.append((margin, ask, bid, source, destination))
        if not sellers or not buyers:
            raise AssertionError(f"{commodity}: missing buy or sell market")
        if not feasible:
            raise AssertionError(f"{commodity}: no profitable one-unit route")
        result[commodity] = {
            "seller_ports": len({r["port_id"] for r in sellers}),
            "buyer_ports": len({r["port_id"] for r in buyers}),
            "possible_pairs": possible_pairs,
            "profitable_pairs": len(feasible),
            "unprofitable_pairs": possible_pairs - len(feasible),
            "best": max(feasible, key=lambda item: item[0]),
        }
    return result


def move_test_player(conn, name: str, port: dict) -> None:
    with conn.cursor() as cur:
        cur.execute("SELECT player_id,ship_id FROM players WHERE name=%s", (name,))
        player_id, ship_id = cur.fetchone()
        cur.execute("UPDATE players SET sector_id=%s WHERE player_id=%s", (port["sector_id"], player_id))
        cur.execute("UPDATE ships SET sector_id=%s WHERE ship_id=%s", (port["sector_id"], ship_id))
    conn.commit()


def hardware_buy(client, code: str, quantity: int, expect_success: bool):
    client.send_json({
        "command": "hardware.buy",
        "data": {"code": code, "quantity": quantity},
    })
    response = client.recv_next_non_notice()
    if status_ok(response) != expect_success:
        raise AssertionError(
            f"hardware.buy {code} x{quantity}: expected success={expect_success}, "
            f"got status={response.get('status')}"
        )
    return response


def wait_for_hardware_restock(conn, port_id: int, hardware_id: int,
                              expected_stock: int, timeout_seconds: int = 75):
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        with conn.cursor() as cur:
            cur.execute(
                "SELECT stock_quantity, next_restock_at > CURRENT_TIMESTAMP "
                "FROM port_hardware_stock WHERE port_id=%s AND hardware_items_id=%s",
                (port_id, hardware_id),
            )
            quantity, rescheduled = cur.fetchone()
        conn.rollback()
        if quantity == expected_stock and rescheduled:
            return
        time.sleep(1)
    raise TimeoutError(
        f"hardware restock did not reach {expected_stock} within {timeout_seconds}s"
    )


def wait_for_cron_run(conn, previous_run, timeout_seconds: int = 75):
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        with conn.cursor() as cur:
            cur.execute(
                "SELECT last_run_at FROM cron_tasks WHERE name='port_hardware_restock'"
            )
            current_run = cur.fetchone()[0]
        conn.rollback()
        if current_run is not None and current_run != previous_run:
            return current_run
        time.sleep(1)
    raise TimeoutError("port_hardware_restock cron did not execute")


def exercise_fresh_hardware_stock(conn, host: str, client_port: int,
                                  account: str, password: str):
    with conn.cursor() as cur:
        cur.execute(
            "SELECT p.port_id,p.sector_id,hi.hardware_items_id,phs.stock_quantity "
            "FROM ports p JOIN porttypes pt USING(porttype_id) "
            "JOIN port_hardware_stock phs USING(port_id) "
            "JOIN hardware_items hi USING(hardware_items_id) "
            "WHERE pt.code='BLACKMARKET' AND hi.code='FIGHTERS' "
            "AND phs.stock_quantity=2 ORDER BY p.port_id LIMIT 1"
        )
        row = cur.fetchone()
        if not row:
            raise AssertionError("no generated black-market FIGHTERS stock row found")
        port_id, sector_id, hardware_id, initial_stock = row
    conn.rollback()

    port = {"port_id": port_id, "sector_id": sector_id}
    move_test_player(conn, account, port)
    client = login_client(host, client_port, account, password)
    try:
        client.send_json({"command": "hardware.list", "data": {}})
        listing = client.recv_next_non_notice()
        if not status_ok(listing):
            raise AssertionError("hardware.list failed at a generated black-market port")
        listed_items = nested_value(listing, "items") or []
        fighters_listing = next(
            (item for item in listed_items if item.get("code") == "FIGHTERS"), None
        )
        if not fighters_listing or fighters_listing.get("stock_quantity") != 2:
            raise AssertionError("hardware.list did not expose generated FIGHTERS stock")
        hardware_buy(client, "FIGHTERS", 1, expect_success=True)
    finally:
        client.close()
    with conn.cursor() as cur:
        cur.execute(
            "SELECT stock_quantity FROM port_hardware_stock "
            "WHERE port_id=%s AND hardware_items_id=%s",
            (port_id, hardware_id),
        )
        if cur.fetchone()[0] != initial_stock - 1:
            raise AssertionError("successful hardware purchase did not decrement stock once")
        cur.execute(
            "SELECT phs.hardware_items_id,phs.stock_quantity FROM port_hardware_stock phs "
            "JOIN hardware_items hi USING(hardware_items_id) "
            "WHERE phs.port_id=%s AND hi.code='CLOAK'",
            (port_id,),
        )
        cloak_stock = cur.fetchone()
        if not cloak_stock or cloak_stock[1] != 2:
            raise AssertionError("fresh black-market CLOAK stock is not initialized")
    conn.commit()

    # Empty inventory is authoritative: a failed purchase cannot create stock
    # or change the zero quantity.
    with conn.cursor() as cur:
        cur.execute(
            "UPDATE port_hardware_stock SET stock_quantity=0 "
            "WHERE port_id=%s AND hardware_items_id=%s",
            (port_id, hardware_id),
        )
    conn.commit()
    client = login_client(host, client_port, account, password)
    try:
        hardware_buy(client, "FIGHTERS", 1, expect_success=False)
    finally:
        client.close()
    with conn.cursor() as cur:
        cur.execute(
            "SELECT stock_quantity FROM port_hardware_stock "
            "WHERE port_id=%s AND hardware_items_id=%s",
            (port_id, hardware_id),
        )
        if cur.fetchone()[0] != 0:
            raise AssertionError("empty-stock purchase changed or invented inventory")

    # CLOAK has a one-per-ship limit. The rejected quantity must not consume
    # any inventory, even though two units are stocked.
    client = login_client(host, client_port, account, password)
    try:
        hardware_buy(client, "CLOAK", 2, expect_success=False)
    finally:
        client.close()
    with conn.cursor() as cur:
        cur.execute(
            "SELECT stock_quantity FROM port_hardware_stock "
            "WHERE port_id=%s AND hardware_items_id=%s",
            (port_id, cloak_stock[0]),
        )
        if cur.fetchone()[0] != 2:
            raise AssertionError("capacity-rejected purchase changed black-market stock")

    # Exercise the actual cron path. An early cron invocation must leave
    # inventory untouched while its per-row restock deadline is still ahead.
    with conn.cursor() as cur:
        cur.execute(
            "UPDATE port_hardware_stock SET stock_quantity=1, "
            "next_restock_at=CURRENT_TIMESTAMP + INTERVAL '24 hours' "
            "WHERE port_id=%s AND hardware_items_id=%s",
            (port_id, hardware_id),
        )
        cur.execute(
            "SELECT last_run_at FROM cron_tasks WHERE name='port_hardware_restock'"
        )
        prior_run = cur.fetchone()[0]
        cur.execute(
            "UPDATE cron_tasks SET next_due_at=CURRENT_TIMESTAMP "
            "WHERE name='port_hardware_restock'"
        )
    conn.commit()
    wait_for_cron_run(conn, prior_run)
    with conn.cursor() as cur:
        cur.execute(
            "SELECT stock_quantity, next_restock_at > CURRENT_TIMESTAMP "
            "FROM port_hardware_stock WHERE port_id=%s AND hardware_items_id=%s",
            (port_id, hardware_id),
        )
        early_quantity, still_scheduled = cur.fetchone()
        if early_quantity != 1 or not still_scheduled:
            raise AssertionError("hardware restocked before its scheduled time")

    # Then verify a due restock adds one unit and a full stock row stays capped.
    with conn.cursor() as cur:
        cur.execute(
            "UPDATE port_hardware_stock SET stock_quantity=1, "
            "next_restock_at=CURRENT_TIMESTAMP - INTERVAL '1 second' "
            "WHERE port_id=%s AND hardware_items_id=%s",
            (port_id, hardware_id),
        )
        cur.execute(
            "UPDATE cron_tasks SET next_due_at=CURRENT_TIMESTAMP "
            "WHERE name='port_hardware_restock'"
        )
    conn.commit()
    wait_for_hardware_restock(conn, port_id, hardware_id, 2)

    with conn.cursor() as cur:
        cur.execute(
            "UPDATE port_hardware_stock SET stock_quantity=max_stock, "
            "next_restock_at=CURRENT_TIMESTAMP - INTERVAL '1 second' "
            "WHERE port_id=%s AND hardware_items_id=%s",
            (port_id, hardware_id),
        )
        cur.execute(
            "UPDATE cron_tasks SET next_due_at=CURRENT_TIMESTAMP "
            "WHERE name='port_hardware_restock'"
        )
    conn.commit()
    wait_for_hardware_restock(conn, port_id, hardware_id, 5)
    return port_id


def login_client(host: str, port: int, name: str, password: str):
    sys.path.insert(0, str(ROOT / "tests.v2"))
    from twclient import TWClient

    client = TWClient(host, port, timeout=10)
    client.connect()
    if not client.login(name, password):
        client.close()
        raise AssertionError("disposable trade-test player login failed")
    return client


def execute_trade(client, command: str, sector: int, port_id: int, commodity: str,
                  key: str):
    payload = {
            "sector_id": sector,
            "port_id": port_id,
            "account": 0,
            "idempotency_key": key,
            "items": [{"commodity": commodity, "quantity": 1}],
    }
    request = {"command": command, "data": payload}
    client.send_json(request)
    response = client.recv_next_non_notice()
    if not status_ok(response):
        error = response.get("error") if isinstance(response, dict) else None
        if not isinstance(error, dict):
            error = {}
        raise AssertionError(
            f"{command} failed for {commodity}: status={response.get('status')} "
            f"code={error.get('code')} message={error.get('message')}"
        )
    # Repeating a canonical V2 request must replay the original receipt.
    client.send_json({"command": command, "data": dict(payload)})
    replay = client.recv_next_non_notice()
    if not status_ok(replay) or replay.get("type") != response.get("type") \
            or replay.get("data") != response.get("data"):
        raise AssertionError(f"{command} retry did not replay its original receipt")
    return response


def trade_state(conn, account: str, port_id: int, commodity: str, action: str):
    with conn.cursor() as cur:
        cur.execute("SELECT player_id,ship_id,credits FROM players WHERE name=%s", (account,))
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
        trades = cur.fetchone()[0]
    return {"credits": credits, "cargo": cargo, "stock": stock, "trades": trades}


def main() -> None:
    db_name = os.environ.get("ECON_QA_DATABASE", "")
    admin_dsn = os.environ.get("ECON_QA_ADMIN_DSN")
    app_template = os.environ.get("ECON_QA_APP_TEMPLATE")
    if not db_name.startswith("twclone_econ_qa_") or db_name == "twclone":
        raise SystemExit("ECON_QA_DATABASE must be a unique twclone_econ_qa_* database")
    if not admin_dsn or not app_template or "%DB%" not in app_template:
        raise SystemExit("Set ECON_QA_ADMIN_DSN and ECON_QA_APP_TEMPLATE (with %DB%).")

    bigbang = Path(os.environ.get("BIGBANG_BIN", ROOT / "bin/bigbang")).resolve()
    server_bin = Path(os.environ.get("SERVER_BIN", ROOT / "bin/server")).resolve()
    if not bigbang.is_file() or not server_bin.is_file():
        raise SystemExit("Built bin/bigbang and bin/server are required")

    with psycopg2.connect(admin_dsn) as admin:
        with admin.cursor() as cur:
            cur.execute("SELECT 1 FROM pg_database WHERE datname=%s", (db_name,))
            if cur.fetchone():
                raise SystemExit("Refusing to use an existing database; choose a fresh disposable name")

    created = False
    server = None
    server_log = None
    conn = None
    client_port, s2s_port = free_port(), free_port()
    app_dsn = app_template.replace("%DB%", db_name)
    account = "econ_qa_" + secrets.token_hex(5)
    password = secrets.token_urlsafe(28)
    reports = {}
    try:
        with tempfile.TemporaryDirectory(prefix="twclone-econ-qa-") as tmp:
            temp = Path(tmp)
            base_config = json.loads((ROOT / "bin/bigbang.json").read_text(encoding="utf-8"))
            config = dict(base_config)
            config.update({
                "admin": admin_dsn,
                "db": db_name,
                "app": app_dsn,
                "sql_dir": str(ROOT / "sql/pg"),
                "sectors": int(os.environ.get("ECON_QA_SECTORS", "2500")),
                "server_port": client_port,
                "s2s_port": s2s_port,
                "tech_level": 3,
                "port_size": 5,
            })
            bb_dir = temp / "bigbang-run"
            bb_dir.mkdir()
            (bb_dir / "bigbang.json").write_text(json.dumps(config), encoding="utf-8")
            os.chmod(bb_dir / "bigbang.json", 0o600)
            # The name was verified absent above; ensure a partial BigBang
            # failure is still cleaned up in the finally block.
            created = True
            generated = subprocess.run(
                [str(bigbang)], cwd=bb_dir, input="y\n", text=True,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=900,
            )
            if generated.returncode:
                raise RuntimeError("BigBang failed on the new disposable database:\n" + generated.stdout[-5000:])
            applied = [line.split("Applying ", 1)[1].split("...", 1)[0]
                       for line in generated.stdout.splitlines()
                       if "Applying " in line]
            if not applied or "119_password_hash_enforcement.sql" not in applied:
                raise AssertionError("canonical V2 BigBang did not apply migration 119")
            if any(name.startswith(("115_", "116_")) for name in applied):
                raise AssertionError("default V2 BigBang applied V3 migrations")
            if applied.index("114_planet_fighter_production.sql") > applied.index("119_password_hash_enforcement.sql"):
                raise AssertionError("migration 119 ran before the V2 migration set completed")

            conn = psycopg2.connect(app_dsn)
            with conn.cursor() as cur:
                cur.execute("SELECT count(*) FROM pg_constraint WHERE conname='players_passwd_argon2id_check' AND convalidated")
                if cur.fetchone()[0] != 1:
                    raise AssertionError("V2 BigBang did not apply migration 119")
                cur.execute("SELECT value FROM config WHERE key='server_port'")
                if int(cur.fetchone()[0]) != client_port:
                    raise AssertionError("BigBang did not persist the isolated server port")
            hardware_ports, hardware_rows = verify_fresh_black_market_hardware(conn)
            conn.commit()

            server_dir = temp / "server-run"
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
                    raise RuntimeError("isolated game server exited before opening its client port")
                try:
                    with socket.create_connection(("127.0.0.1", client_port), timeout=0.2):
                        break
                except OSError:
                    time.sleep(0.2)
            else:
                raise TimeoutError("isolated game server did not start")

            with conn.cursor() as cur:
                cur.execute(
                    "SELECT count(*) FROM players WHERE name=%s", (account,)
                )
                if cur.fetchone()[0]:
                    raise AssertionError("unexpected collision for random QA account")
            conn.commit()
            sys.path.insert(0, str(ROOT / "tests.v2"))
            from twclient import TWClient
            registration = TWClient("127.0.0.1", client_port, timeout=10)
            registration.connect()
            if not registration.register(account, password, fail_if_exists=True):
                raise AssertionError("trade-test player registration failed")
            registration.close()

            # Optional credential is supplied by the isolated QA runner, never
            # embedded in the repository or emitted in test output.
            qa_bot_password = os.environ.get("AI_QA_BOT_PASSWORD")
            if qa_bot_password:
                qa_bot = login_client(
                    "127.0.0.1", client_port, "ai_qa_bot", qa_bot_password
                )
                try:
                    qa_bot.send_json({"command": "player.my_info", "data": {}})
                    bot_info = qa_bot.recv_next_non_notice()
                    if not status_ok(bot_info):
                        raise AssertionError(
                            "ordinary-player ai_qa_bot login could not read player state"
                        )
                    with conn.cursor() as cur:
                        cur.execute(
                            "SELECT to_sector FROM sector_warps "
                            "WHERE from_sector=1 ORDER BY to_sector LIMIT 1"
                        )
                        adjacent_sector = cur.fetchone()[0]
                    qa_bot.send_json({
                        "command": "move.warp",
                        "data": {"to_sector_id": adjacent_sector},
                    })
                    if not status_ok(qa_bot.recv_next_non_notice()):
                        raise AssertionError(
                            "ordinary-player ai_qa_bot could not use move.warp"
                        )
                finally:
                    qa_bot.close()
                print(
                    "PASS ai_qa_bot public authentication, player.my_info, and move.warp"
                )

            hardware_port_id = exercise_fresh_hardware_stock(
                conn, "127.0.0.1", client_port, account, password
            )

            rows = market_rows(conn)
            routes = profitable_routes(rows)
            for commodity, item in routes.items():
                margin, ask, bid, source, destination = item["best"]
                if margin <= 0 or source["quantity"] < 1 or destination["quantity"] >= destination["capacity"]:
                    raise AssertionError(f"{commodity}: route is not quantity-feasible and profitable")
                reports[commodity] = {
                    "sellers": item["seller_ports"],
                    "buyers": item["buyer_ports"],
                    "possible_pairs": item["possible_pairs"],
                    "profitable_pairs": item["profitable_pairs"],
                    "unprofitable_pairs": item["unprofitable_pairs"],
                    "best_margin": margin,
                    "best_ask": ask,
                    "best_bid": bid,
                }

            qa_bot_hardware_port = None
            if qa_bot_password:
                # Prove the seeded identity uses ordinary market and hardware
                # permissions as well as ordinary-player authentication.
                margin, ask, bid, source, destination = routes["ORE"]["best"]
                move_test_player(conn, "ai_qa_bot", source)
                buyer = login_client(
                    "127.0.0.1", client_port, "ai_qa_bot", qa_bot_password
                )
                bot_before = trade_state(
                    conn, "ai_qa_bot", source["port_id"], "ORE", "buy"
                )
                buy_response = execute_trade(
                    buyer, "trade.buy", source["sector_id"], source["port_id"],
                    "ORE", "qa-bot-buy-" + secrets.token_hex(8),
                )
                buyer.close()
                bot_after_buy = trade_state(
                    conn, "ai_qa_bot", source["port_id"], "ORE", "buy"
                )
                if (bot_after_buy["cargo"] != bot_before["cargo"] + 1
                        or bot_after_buy["stock"] != bot_before["stock"] - 1
                        or int(nested_value(buy_response, "unit_price")) != ask):
                    raise AssertionError("ai_qa_bot trade.buy did not persist correctly")

                move_test_player(conn, "ai_qa_bot", destination)
                seller = login_client(
                    "127.0.0.1", client_port, "ai_qa_bot", qa_bot_password
                )
                bot_sell_before = trade_state(
                    conn, "ai_qa_bot", destination["port_id"], "ORE", "sell"
                )
                sell_response = execute_trade(
                    seller, "trade.sell", destination["sector_id"],
                    destination["port_id"], "ORE",
                    "qa-bot-sell-" + secrets.token_hex(8),
                )
                seller.close()
                bot_after_sell = trade_state(
                    conn, "ai_qa_bot", destination["port_id"], "ORE", "sell"
                )
                if (bot_after_sell["cargo"] != bot_after_buy["cargo"] - 1
                        or bot_after_sell["stock"] != bot_sell_before["stock"] + 1
                        or int(nested_value(sell_response, "unit_price")) != bid
                        or bot_after_sell["credits"] - bot_before["credits"] != margin):
                    raise AssertionError("ai_qa_bot trade.sell did not persist correctly")

                qa_bot_hardware_port = exercise_fresh_hardware_stock(
                    conn, "127.0.0.1", client_port,
                    "ai_qa_bot", qa_bot_password,
                )
                print(
                    "PASS ai_qa_bot public ORE trading and black-market hardware purchase"
                )

            # Execute the same public buy/sell commands for one unit of each
            # commodity. Position changes are isolated test fixture SQL.
            for commodity, item in routes.items():
                margin, ask, bid, source, destination = item["best"]
                with conn.cursor() as cur:
                    cur.execute("SELECT credits FROM players WHERE name=%s", (account,))
                    credits_before = cur.fetchone()[0]
                move_test_player(conn, account, source)
                buyer = login_client("127.0.0.1", client_port, account, password)
                buy_state_before = trade_state(
                    conn, account, source["port_id"], commodity, "buy"
                )
                buyer.send_json({
                    "command": "trade.buy",
                    "data": {
                        "sector_id": source["sector_id"],
                        "port_id": source["port_id"],
                        "account": 0,
                        "items": [{"commodity": commodity, "quantity": 1}],
                    },
                })
                missing_key = buyer.recv_next_non_notice()
                if missing_key.get("status") != "error":
                    raise AssertionError("trade.buy without data.idempotency_key was not rejected")
                if trade_state(conn, account, source["port_id"], commodity, "buy") != buy_state_before:
                    raise AssertionError("missing-key trade.buy changed authoritative state")
                buy_response = execute_trade(
                    buyer, "trade.buy", source["sector_id"], source["port_id"],
                    commodity, "econ-qa-buy-" + secrets.token_hex(8),
                )
                buyer.close()
                buy_state_after = trade_state(
                    conn, account, source["port_id"], commodity, "buy"
                )
                if (buy_state_after["cargo"] != buy_state_before["cargo"] + 1
                        or buy_state_after["stock"] != buy_state_before["stock"] - 1
                        or buy_state_after["trades"] != buy_state_before["trades"] + 1
                        or buy_state_after["credits"] >= buy_state_before["credits"]):
                    raise AssertionError("duplicate trade.buy executed more or less than once")
                actual_ask = nested_value(buy_response, "unit_price")
                if actual_ask is None or int(actual_ask) != ask:
                    raise AssertionError(f"{commodity}: actual buy quote differs from the calculated public ask")

                move_test_player(conn, account, destination)
                seller = login_client("127.0.0.1", client_port, account, password)
                sell_state_before = trade_state(
                    conn, account, destination["port_id"], commodity, "sell"
                )
                seller.send_json({
                    "command": "trade.sell",
                    "data": {
                        "sector_id": destination["sector_id"],
                        "port_id": destination["port_id"],
                        "account": 0,
                        "items": [{"commodity": commodity, "quantity": 1}],
                    },
                })
                missing_key = seller.recv_next_non_notice()
                if missing_key.get("status") != "error":
                    raise AssertionError("trade.sell without data.idempotency_key was not rejected")
                if trade_state(conn, account, destination["port_id"], commodity, "sell") != sell_state_before:
                    raise AssertionError("missing-key trade.sell changed authoritative state")
                sell_response = execute_trade(
                    seller, "trade.sell", destination["sector_id"], destination["port_id"],
                    commodity, "econ-qa-sell-" + secrets.token_hex(8),
                )
                seller.close()
                sell_state_after = trade_state(
                    conn, account, destination["port_id"], commodity, "sell"
                )
                if (sell_state_after["cargo"] != sell_state_before["cargo"] - 1
                        or sell_state_after["stock"] != sell_state_before["stock"] + 1
                        or sell_state_after["trades"] != sell_state_before["trades"] + 1
                        or sell_state_after["credits"] <= sell_state_before["credits"]):
                    raise AssertionError("duplicate trade.sell executed more or less than once")
                actual_bid = nested_value(sell_response, "unit_price")
                if actual_bid is None or int(actual_bid) != bid:
                    raise AssertionError(f"{commodity}: actual sell quote differs from the calculated public bid")
                with conn.cursor() as cur:
                    cur.execute("SELECT credits FROM players WHERE name=%s", (account,))
                    credits_after = cur.fetchone()[0]
                    actual_profit = credits_after - credits_before
                    if actual_profit <= 0 or actual_profit != margin:
                        raise AssertionError(
                            f"{commodity}: actual credits did not preserve the public quote margin "
                            "after transaction costs"
                        )
                    reports[commodity]["actual_profit"] = actual_profit
                    cur.execute("SELECT COALESCE(quantity,0) FROM entity_stock WHERE entity_type='port' AND entity_id=%s AND commodity_code=%s", (source["port_id"], commodity))
                    reports[commodity]["source_stock_after_trade"] = cur.fetchone()[0]
                conn.commit()

            print(
                f"PASS database={db_name} sectors={config['sectors']} "
                f"tech_level=3 port_size=5 black_market_ports={hardware_ports} "
                f"initialized_hardware_rows={hardware_rows} "
                f"hardware_purchase_restock_port={hardware_port_id}"
                + (f" ai_qa_bot_hardware_port={qa_bot_hardware_port}"
                   if qa_bot_hardware_port is not None else "")
            )
            for commodity in COMMODITIES:
                r = reports[commodity]
                print(
                    f"{commodity}: buy_ports={r['buyers']} sell_ports={r['sellers']} "
                    f"profitable_quantity_feasible_routes={r['profitable_pairs']} "
                    f"unprofitable_quantity_feasible_pairs={r['unprofitable_pairs']} "
                    f"best_buy_price={r['best_ask']} best_sell_price={r['best_bid']} "
                    f"best_unit_margin={r['best_margin']} "
                    f"actual_trade_profit_after_fees={r['actual_profit']}"
                )
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
                    cur.execute("SELECT datname FROM pg_database WHERE datname=%s", (db_name,))
                    if cur.fetchone():
                        cur.execute(sql.SQL("DROP DATABASE {}").format(sql.Identifier(db_name)))
            finally:
                admin.close()


if __name__ == "__main__":
    main()
