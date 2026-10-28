"""Run BigBang repeatedly against one explicitly disposable PostgreSQL DB."""

import json
import os
from pathlib import Path
import subprocess
import tempfile

import psycopg2

ROOT = Path(__file__).resolve().parents[1]
PORT_CLASSES = {
    1: {("ORE", "sell"), ("ORG", "buy"), ("EQU", "buy")},
    2: {("ORE", "sell"), ("ORG", "sell"), ("EQU", "buy")},
    3: {("ORE", "buy"), ("ORG", "sell"), ("EQU", "sell")},
    4: {("ORE", "buy"), ("ORG", "sell"), ("EQU", "buy")},
    5: {("ORE", "buy"), ("ORG", "buy"), ("EQU", "sell")},
    6: {("ORE", "sell"), ("ORG", "buy"), ("EQU", "sell")},
    7: {("ORE", "buy"), ("ORG", "buy"), ("EQU", "buy")},
    8: {("ORE", "sell"), ("ORG", "sell"), ("EQU", "sell")},
}


def check_universe(dsn):
    with psycopg2.connect(dsn) as conn, conn.cursor() as cur:
        cur.execute(
            "WITH RECURSIVE reachable(sector_id) AS ("
            "  SELECT sector_id::bigint FROM sectors WHERE sector_id=1 UNION "
            "  SELECT sw.to_sector FROM sector_warps sw "
            "  JOIN reachable r ON r.sector_id=sw.from_sector"
            ") SELECT (SELECT count(*) FROM sectors),"
            "(SELECT count(*) FROM reachable),"
            "(SELECT count(*) FROM sector_warps sw "
            " JOIN sector_warps rev ON rev.from_sector=sw.to_sector "
            " AND rev.to_sector=sw.from_sector WHERE sw.from_sector < sw.to_sector)"
        )
        sector_count, reachable_count, reciprocal_pairs = cur.fetchone()
        if reachable_count != sector_count:
            raise AssertionError(
                f"directed reachability from sector 1 is {reachable_count}/{sector_count}"
            )
        cur.execute(
            "SELECT count(*) FROM sector_warps sw WHERE NOT EXISTS ("
            "SELECT 1 FROM sector_warps reverse "
            "WHERE reverse.from_sector=sw.to_sector AND reverse.to_sector=sw.from_sector)"
        )
        one_way_edges = cur.fetchone()[0]
        if one_way_edges == 0:
            raise AssertionError("generated topology lost all intentional one-way warps")

        cur.execute(
            "WITH RECURSIVE reachable(sector_id) AS ("
            "  SELECT sector_id::bigint FROM sectors WHERE sector_id=1 UNION "
            "  SELECT sw.to_sector FROM sector_warps sw "
            "  JOIN reachable r ON r.sector_id=sw.from_sector"
            ") SELECT p.name,p.sector_id FROM planets p "
            "WHERE p.name IN ('Terra','Ferenginar','Orion Hideout') "
            "AND NOT EXISTS (SELECT 1 FROM reachable r "
            "WHERE r.sector_id=p.sector_id)"
        )
        if cur.fetchall():
            raise AssertionError("a generated homeworld is not reachable from sector 1")
        cur.execute(
            "SELECT count(*) FROM planets "
            "WHERE name IN ('Terra','Ferenginar','Orion Hideout')"
        )
        if cur.fetchone()[0] != 3:
            raise AssertionError("BigBang did not generate all three expected homeworlds")

        cur.execute(
            "SELECT p.is_npc, p.type, p.sector_id, p.ship_id, "
            "s.ship_id, s.sector_id, so.is_primary, p.passwd ~ '^\\$argon2id\\$' "
            "FROM players p LEFT JOIN ships s ON s.ship_id=p.ship_id "
            "LEFT JOIN ship_ownership so ON so.ship_id=s.ship_id "
            "AND so.player_id=p.player_id WHERE p.name='ai_qa_bot'"
        )
        qa_bot = cur.fetchone()
        if (qa_bot is None or qa_bot[0] is not False or qa_bot[1] != 2
                or qa_bot[2] != 1 or qa_bot[3] is None or qa_bot[4] != qa_bot[3]
                or qa_bot[5] != 1 or qa_bot[6] is not True or qa_bot[7] is not True):
            raise AssertionError(
                "BigBang ai_qa_bot is not an ordinary authenticated player with "
                "a type-2 primary starter ship in sector 1"
            )

        cur.execute(
            "SELECT p.planet_id,c.code,c.capacity,c.legacy,pg.max_capacity,"
            "pg.quantity,es.quantity "
            "FROM planets p "
            "JOIN planettypes pt ON pt.planettypes_id=p.type "
            "CROSS JOIN LATERAL (VALUES ('ORE', pt.maxore, p.ore_on_hand),"
            "('ORG', pt.maxorganics, p.organics_on_hand),"
            "('EQU', pt.maxequipment, p.equipment_on_hand)) AS c(code, capacity, legacy) "
            "LEFT JOIN planet_goods pg ON pg.planet_id=p.planet_id AND pg.commodity=c.code "
            "LEFT JOIN entity_stock es ON es.entity_type='planet' "
            "AND es.entity_id=p.planet_id AND es.commodity_code=c.code "
            "ORDER BY p.planet_id,c.code"
        )
        stock_rows = cur.fetchall()
        expected_stock = {}
        for planet_id, commodity, capacity, legacy, goods_capacity, goods_qty, qty in stock_rows:
            expected_qty = (capacity // 2) if planet_id in (1, 2, 3) else 0
            if (goods_capacity != capacity or goods_qty != 0 or legacy != 0
                    or qty != expected_qty):
                raise AssertionError(
                    f"planet {planet_id} {commodity} has missing/inconsistent authoritative stock"
                )
            expected_stock[(planet_id, commodity)] = qty
        cur.execute("SELECT count(*) FROM planets")
        if len(stock_rows) != cur.fetchone()[0] * 3:
            raise AssertionError("not every planet has all three authoritative commodity rows")

        # Re-running generation initialization must preserve balances exactly.
        cur.execute(
            "SELECT initialize_planet_commodity_stock(planet_id) FROM planets"
        )
        cur.execute(
            "SELECT entity_id,commodity_code,quantity FROM entity_stock "
            "WHERE entity_type='planet' AND commodity_code IN ('ORE','ORG','EQU')"
        )
        repeated_stock = {(pid, code): qty for pid, code, qty in cur.fetchall()}
        if repeated_stock != expected_stock:
            raise AssertionError("repeated planet stock initialization changed balances")

        cur.execute(
            "SELECT p.type, t.commodity, t.mode, count(*) "
            "FROM ports p JOIN port_trade t USING (port_id) "
            "GROUP BY p.type, t.commodity, t.mode ORDER BY p.type, t.commodity, t.mode"
        )
        grouped = cur.fetchall()
        type_counts = {}
        for port_type, commodity, mode, count in grouped:
            if port_type in PORT_CLASSES and (commodity, mode) not in PORT_CLASSES[port_type]:
                raise AssertionError(f"port type {port_type} has a noncanonical {commodity}/{mode} trade")
        cur.execute("SELECT type, count(*) FROM ports GROUP BY type ORDER BY type")
        type_counts = dict(cur.fetchall())

        counts = {}
        for commodity in ("ORE", "ORG", "EQU"):
            cur.execute(
                "SELECT t.mode, count(DISTINCT p.port_id) "
                "FROM port_trade t JOIN ports p USING (port_id) "
                "JOIN entity_stock es ON es.entity_type='port' AND es.entity_id=p.port_id "
                "AND es.commodity_code=t.commodity "
                "WHERE t.commodity=%s AND ((t.mode='buy' AND es.quantity < COALESCE(p.size,10)*1000) "
                "OR (t.mode='sell' AND es.quantity > 0)) GROUP BY t.mode",
                (commodity,),
            )
            row_counts = dict(cur.fetchall())
            counts[commodity] = {mode: row_counts.get(mode, 0) for mode in ("buy", "sell")}
            if not all(counts[commodity].values()):
                raise AssertionError(f"{commodity} missing at least one buy or sell port")

        cur.execute(
            "SELECT count(*) FROM (SELECT p.port_id FROM ports p "
            "JOIN port_trade t USING (port_id) WHERE p.type BETWEEN 1 AND 6 "
            "GROUP BY p.port_id HAVING bool_or(t.mode='buy') AND bool_or(t.mode='sell')) mixed"
        )
        mixed_ports = cur.fetchone()[0]
        if mixed_ports == 0:
            raise AssertionError("universe has no mixed buy/sell port class (types 1-6)")
        return counts, type_counts, mixed_ports, {
            "sectors": sector_count,
            "reachable": reachable_count,
            "one_way_edges": one_way_edges,
            "reciprocal_pairs": reciprocal_pairs,
        }


def main():
    db_name = os.environ.get("PORT_QA_DATABASE", "")
    admin_dsn = os.environ.get("PORT_QA_ADMIN_DSN")
    app_template = os.environ.get("PORT_QA_APP_TEMPLATE")
    bigbang = os.environ.get("BIGBANG_BIN", str(ROOT / "bin/bigbang"))
    if not db_name.startswith("twclone_port_qa_"):
        raise SystemExit("PORT_QA_DATABASE must be a dedicated twclone_port_qa_* disposable database")
    if not admin_dsn or not app_template or "%DB%" not in app_template:
        raise SystemExit("Set PORT_QA_ADMIN_DSN and PORT_QA_APP_TEMPLATE (which must contain %DB%).")
    if not Path(bigbang).is_file():
        raise SystemExit("BIGBANG_BIN does not point to the built BigBang executable")
    with psycopg2.connect(admin_dsn) as admin_conn, admin_conn.cursor() as cur:
        cur.execute("SELECT 1 FROM pg_database WHERE datname=%s", (db_name,))
        if cur.fetchone():
            raise SystemExit("PORT_QA_DATABASE already exists; choose a new disposable database name")

    # 500 is the documented standard; 1000 and 2500 cover larger supported setups.
    sizes = tuple(int(s) for s in os.environ.get(
        "PORT_QA_SIZES", "500,1000,2500"
    ).split(","))
    seeds = tuple(float(s) for s in os.environ.get(
        "PORT_QA_SEEDS", "-0.75,-0.25,0.25,0.75"
    ).split(","))
    for sectors in sizes:
        for seed in seeds:
            config = {
                "admin": admin_dsn,
                "db": db_name,
                "app": app_template,
                "sql_dir": str(ROOT / "sql/pg"),
                "sectors": sectors,
                "density": 4,
                "port_ratio": 40,
                "planet_ratio": 1,
                "port_seed": seed,
                "min_tunnels": 15,
                "min_tunnel_len": 4,
            }
            with tempfile.TemporaryDirectory(prefix="twclone-port-qa-") as run_dir:
                Path(run_dir, "bigbang.json").write_text(json.dumps(config))
                result = subprocess.run(
                    [bigbang], cwd=run_dir, input="y\n", text=True,
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=1800,
                )
                if result.returncode != 0:
                    raise RuntimeError(
                        f"BigBang failed for sectors={sectors}, port_seed={seed} "
                        f"(exit {result.returncode}); final output:\n"
                        f"{(result.stdout + result.stderr)[-5000:]}"
                    )
            dsn = app_template.replace("%DB%", db_name)
            counts, type_counts, mixed_ports, topology = check_universe(dsn)
            print(
                f"PASS sectors={sectors} port_ratio=40 port_seed={seed} "
                f"ports_by_type={type_counts} mixed_ports={mixed_ports} "
                f"directed_reachability={topology['reachable']}/{topology['sectors']} "
                f"one_way_edges={topology['one_way_edges']} "
                f"planet_stock=consistent "
                "eligible (port buys from player / sells to player): "
                f"ORE={counts['ORE']} ORG={counts['ORG']} EQU={counts['EQU']}"
            )


if __name__ == "__main__":
    main()
