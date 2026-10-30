"""PostgreSQL contract tests for the repair driver's all-or-nothing CTE."""
import os
import subprocess
import time
import uuid

import pytest

DSN = os.environ.get("TWCLONE_TEST_PG_DSN")
pytestmark = pytest.mark.skipif(not DSN, reason="TWCLONE_TEST_PG_DSN is not set")


def psql(sql, check=True):
    return subprocess.run(["psql", DSN, "-X", "-qAt", "-v", "ON_ERROR_STOP=1", "-c", sql],
                          check=check, text=True, capture_output=True)


REPAIR = """WITH target_ship AS (
  SELECT ship_id FROM ships WHERE ship_id = 3 FOR UPDATE
), player AS (
  UPDATE players SET credits = credits - 40
   WHERE player_id = 1 AND ship_id = 3 AND credits >= 40
     AND EXISTS (SELECT 1 FROM target_ship)
   RETURNING credits
), ship AS (
  UPDATE ships SET hull = 100 WHERE ship_id = 3
    AND EXISTS (SELECT 1 FROM player)
  RETURNING ship_id
)
SELECT (SELECT credits FROM player), (SELECT ship_id FROM ship);"""


def test_repair_success_refusal_and_rollback_are_atomic():
    schema = "repair_atomic_" + uuid.uuid4().hex
    psql(f'''CREATE SCHEMA {schema}; SET search_path TO {schema}, pg_catalog;
      CREATE TABLE players(player_id integer PRIMARY KEY, ship_id integer, credits bigint NOT NULL CHECK (credits >= 0));
      CREATE TABLE ships(ship_id integer PRIMARY KEY, hull integer NOT NULL CHECK (hull BETWEEN 0 AND 100));
      INSERT INTO players VALUES (1,3,100),(2,4,10);
      INSERT INTO ships VALUES (3,40),(4,40);''')
    try:
        success = psql(f"SET search_path TO {schema}, pg_catalog; {REPAIR}")
        assert success.stdout.strip() == "60|3"
        assert psql(f"SELECT credits FROM {schema}.players WHERE player_id=1").stdout.strip() == "60"
        assert psql(f"SELECT hull FROM {schema}.ships WHERE ship_id=3").stdout.strip() == "100"

        # Insufficient funds must leave both rows unchanged.
        refused = psql(f"SET search_path TO {schema}, pg_catalog; UPDATE players SET credits=10 WHERE player_id=1; {REPAIR}")
        assert refused.stdout.strip() == "|"
        assert psql(f"SELECT credits || ':' || hull FROM {schema}.players p JOIN {schema}.ships s ON s.ship_id=p.ship_id WHERE p.player_id=1").stdout.strip() == "10:100"

        # A player cannot pay to repair another player's ship.
        mismatch = psql(f"SET search_path TO {schema}, pg_catalog; UPDATE players SET credits=100, ship_id=4 WHERE player_id=1; {REPAIR}")
        assert mismatch.stdout.strip() == "|"
        assert psql(f"SELECT credits || ':' || hull FROM {schema}.players p JOIN {schema}.ships s ON s.ship_id=3 WHERE p.player_id=1").stdout.strip() == "100:100"
    finally:
        psql(f"DROP SCHEMA {schema} CASCADE")


def test_concurrent_repairs_charge_only_once():
    schema = "repair_race_" + uuid.uuid4().hex
    psql(f'''CREATE SCHEMA {schema}; SET search_path TO {schema}, pg_catalog;
      CREATE TABLE players(player_id integer PRIMARY KEY, ship_id integer, credits bigint NOT NULL CHECK (credits >= 0));
      CREATE TABLE ships(ship_id integer PRIMARY KEY, hull integer NOT NULL CHECK (hull BETWEEN 0 AND 100));
      INSERT INTO players VALUES (1,3,40); INSERT INTO ships VALUES (3,40);''')
    first = None
    try:
        first = subprocess.Popen(["psql", DSN, "-X", "-qAt", "-v", "ON_ERROR_STOP=1", "-c",
          f"SET search_path TO {schema}, pg_catalog; BEGIN; SELECT ship_id FROM ships WHERE ship_id=3 FOR UPDATE; SELECT pg_sleep(2); {REPAIR} COMMIT;"],
          stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        time.sleep(0.4)
        second = psql(f"SET search_path TO {schema}, pg_catalog; {REPAIR}")
        out, err = first.communicate(timeout=5)
        assert first.returncode == 0, err
        assert second.stdout.strip() == "|"
        assert psql(f"SELECT credits || ':' || hull FROM {schema}.players p JOIN {schema}.ships s ON s.ship_id=3 WHERE p.player_id=1").stdout.strip() == "0:100"
    finally:
        if first and first.poll() is None:
            first.kill(); first.communicate()
        psql(f"DROP SCHEMA {schema} CASCADE")
