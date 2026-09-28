#!/usr/bin/env python3
"""Exercise planetary fighter production through server -> engine -> PostgreSQL.

Requires the disposable tests.v2 rig, PostgreSQL migrations through 114, and
the engine's planet_growth cron running on the server. All fixture rows use a
unique planet type and are removed in a finally block.
"""

import os
import time
import uuid

from twclient import TWClient


HOST = os.getenv("HOST", "localhost")
PORT = int(os.getenv("PORT", "1234"))
TIMEOUT_SECONDS = 75


def quote(value):
    return "'" + value.replace("'", "''") + "'"


def main():
    suffix = uuid.uuid4().hex[:12]
    type_code = "QF" + suffix.upper()
    type_name = "V2 fighter regression " + suffix
    names = {
        "no_citadel": "v2_fighter_none_" + suffix,
        "cap": "v2_fighter_cap_" + suffix,
        "production": "v2_fighter_prod_" + suffix,
        "shortage": "v2_fighter_short_" + suffix,
    }
    client = TWClient(HOST, PORT, timeout=20)
    type_created = False
    connected = False
    authenticated = False

    def execute(sql):
        client.send_json({
            "command": "sys.raw_sql_exec",
            "data": {"sql": sql},
        })
        response = client.recv_next_non_notice(timeout=20)
        if response.get("status") != "ok":
            raise RuntimeError("sys.raw_sql_exec failed: " + repr(response))
        return response.get("data", {}).get("rows", [])

    def wait_for_tick(tick_id):
        deadline = time.monotonic() + TIMEOUT_SECONDS
        while time.monotonic() < deadline:
            rows = execute(
                "SELECT COUNT(*) FROM planets p JOIN citadels c USING (planet_id) "
                "WHERE p.name IN (" + ",".join(quote(names[k]) for k in
                    ("cap", "production", "shortage")) + ") "
                "AND p.fighter_production_last_tick = " + str(tick_id)
            )
            if rows and int(rows[0][0]) == 3:
                return
            time.sleep(1)
        raise TimeoutError("planet_growth did not persist fighter production tick")

    def wait_for_cron_after(previous):
        deadline = time.monotonic() + TIMEOUT_SECONDS
        while time.monotonic() < deadline:
            rows = execute(
                "SELECT last_run_at FROM cron_tasks WHERE name = 'planet_growth'"
            )
            if rows and rows[0][0] and str(rows[0][0]) != str(previous):
                return rows[0][0]
            time.sleep(1)
        raise TimeoutError("planet_growth cron did not record the forced retry")

    try:
        client.connect()
        connected = True
        if not client.login("System", "BOT"):
            raise RuntimeError("System/BOT login failed; run the disposable v2 rig first")
        authenticated = True

        owner_rows = execute(
            "SELECT player_id FROM players WHERE name = 'citadel_owner'"
        )
        if not owner_rows:
            raise RuntimeError("citadel_owner rig fixture is missing")
        owner_id = int(owner_rows[0][0])

        execute(
            "INSERT INTO planettypes (code, typeName, typeDescription, "
            "fighterProduction, maxfighters) VALUES (" + quote(type_code) + ", "
            + quote(type_name) + ", 'tests.v2 isolated fixture', 10, 3)"
        )
        type_created = True
        type_rows = execute(
            "SELECT planettypes_id FROM planettypes WHERE code = " + quote(type_code)
        )
        type_id = int(type_rows[0][0])
        for commodity in ("ORE", "ORG", "EQU"):
            execute(
                "INSERT INTO planet_production (planet_type_id, commodity_code, "
                "base_prod_rate, base_cons_rate) VALUES (" + str(type_id) + ", "
                + quote(commodity) + ", 0, 0)"
            )

        planets = [
            ("no_citadel", 100, 0, 100),
            ("cap", 100, 2, 100),
            ("production", 20, 0, 100),
            ("shortage", 30, 0, 0),
        ]
        for key, weapons, fighters, equipment in planets:
            execute(
                "INSERT INTO planets (sector_id, name, owner_id, owner_type, class, "
                "population, type, created_by, colonists_weapons, fighters, "
                "citadel_level) VALUES (1, " + quote(names[key]) + ", "
                + str(owner_id) + ", 'player', 'M', 0, " + str(type_id) + ", "
                + str(owner_id) + ", " + str(weapons) + ", " + str(fighters)
                + ", 0)"
            )
            planet_id_rows = execute(
                "SELECT planet_id FROM planets WHERE name = " + quote(names[key])
            )
            planet_id = int(planet_id_rows[0][0])
            if key != "no_citadel":
                execute(
                    "INSERT INTO citadels (planet_id, level, owner_id) VALUES ("
                    + str(planet_id) + ", 1, " + str(owner_id) + ")"
                )
            for commodity in ("ORE", "ORG", "EQU"):
                quantity = equipment if commodity == "EQU" else 0
                stock_capacity = (
                    100 if key == "no_citadel" and commodity == "EQU" else 5000
                )
                execute(
                    "INSERT INTO planet_goods (planet_id, commodity, quantity, "
                    "max_capacity, production_rate) VALUES (" + str(planet_id)
                    + ", " + quote(commodity) + ", " + str(quantity)
                    + ", " + str(stock_capacity) + ", 0)"
                )
                execute(
                    "INSERT INTO entity_stock (entity_type, entity_id, commodity_code, "
                    "quantity, price) VALUES ('planet', " + str(planet_id) + ", "
                    + quote(commodity) + ", " + str(quantity) + ", 0)"
                )

        tick_id = int(time.time()) // 600
        if int(time.time()) // 600 != tick_id:
            raise RuntimeError("tick interval changed during fixture setup; retry suite")
        execute(
            "UPDATE cron_tasks SET next_due_at = CURRENT_TIMESTAMP, enabled = TRUE "
            "WHERE name = 'planet_growth'"
        )
        wait_for_tick(tick_id)

        result_sql = (
            "SELECT p.name, p.fighters, es.quantity "
            "FROM planets p JOIN entity_stock es ON es.entity_type = 'planet' "
            "AND es.entity_id = p.planet_id AND es.commodity_code = 'EQU' "
            "WHERE p.name IN (" + ",".join(quote(n) for n in names.values()) + ") "
            "ORDER BY p.name"
        )

        def states():
            return {row[0]: (int(row[1]), int(row[2])) for row in execute(result_sql)}

        expected = {
            names["no_citadel"]: (0, 100),
            names["cap"]: (3, 99),
            names["production"]: (2, 98),
            names["shortage"]: (0, 0),
        }
        first = states()
        if first != expected:
            raise AssertionError("unexpected production state: " + repr(first))

        cron_rows = execute(
            "SELECT last_run_at FROM cron_tasks WHERE name = 'planet_growth'"
        )
        previous_run = cron_rows[0][0] if cron_rows else None
        execute(
            "UPDATE cron_tasks SET next_due_at = CURRENT_TIMESTAMP, enabled = TRUE "
            "WHERE name = 'planet_growth'"
        )
        wait_for_cron_after(previous_run)
        second = states()
        if second != expected:
            raise AssertionError("same-tick cron replay changed persisted state: " + repr(second))

        print("planet_growth fighter production integration checks passed")
    finally:
        if connected and authenticated:
            try:
                cleanup_names = ",".join(quote(n) for n in names.values())
                execute(
                    "DELETE FROM entity_stock WHERE entity_type = 'planet' AND "
                    "entity_id IN (SELECT planet_id FROM planets WHERE name IN ("
                    + cleanup_names + "))"
                )
                execute(
                    "DELETE FROM planet_goods WHERE planet_id IN "
                    "(SELECT planet_id FROM planets WHERE name IN (" + cleanup_names + "))"
                )
                execute("DELETE FROM planets WHERE name IN (" + cleanup_names + ")")
                if type_created:
                    execute(
                        "DELETE FROM planet_production WHERE planet_type_id IN "
                        "(SELECT planettypes_id FROM planettypes WHERE code = "
                        + quote(type_code) + ")"
                    )
                    execute("DELETE FROM planettypes WHERE code = " + quote(type_code))
            finally:
                client.close()


if __name__ == "__main__":
    main()
