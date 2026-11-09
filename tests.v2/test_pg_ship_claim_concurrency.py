"""Run against a disposable PostgreSQL database using TWCLONE_TEST_PG_DSN."""
import os
import subprocess
import time
import uuid
from pathlib import Path

import pytest

DSN = os.environ.get("TWCLONE_TEST_PG_DSN")
pytestmark = pytest.mark.skipif(not DSN, reason="TWCLONE_TEST_PG_DSN is not set")
MIGRATION = Path(__file__).resolve().parents[1] / "sql/pg/168_v3_abandoned_hull_repair.sql"


def psql(sql):
    return subprocess.run(["psql", DSN, "-X", "-qAt", "-v", "ON_ERROR_STOP=1", "-c", sql],
                          check=True, text=True, capture_output=True).stdout.strip()


def test_competing_claims_cannot_assign_one_ship_to_two_players():
    schema = "claim_test_" + uuid.uuid4().hex
    setup = f'''CREATE SCHEMA {schema}; SET search_path TO {schema}, pg_catalog;
        CREATE TYPE db_result AS (code integer, message text, value bigint);
        CREATE TABLE shiptypes (
            shiptypes_id integer PRIMARY KEY, enabled boolean NOT NULL,
            can_purchase boolean NOT NULL, initialholds integer,
            maxholds integer, maxfighters integer, maxshields integer
        );
        CREATE TABLE ship_ownership (
            ship_id integer NOT NULL, player_id integer NOT NULL,
            role_id integer NOT NULL, is_primary boolean NOT NULL
        );
        CREATE TABLE ships (
            ship_id integer PRIMARY KEY, sector_id integer,
            destroyed boolean NOT NULL DEFAULT false, type_id integer,
            flags bigint
        );
        CREATE TABLE players (player_id integer PRIMARY KEY, sector_id integer, ship_id integer REFERENCES ships(ship_id));
        INSERT INTO shiptypes VALUES (1, true, true, 10, 30, 100, 100);
        {MIGRATION.read_text()}
        INSERT INTO ships VALUES (10,1,false,1,0),(11,1,false,1,0),(20,1,false,1,0);
        INSERT INTO ship_ownership VALUES (10,1,1,true),(11,2,1,true);
        INSERT INTO players VALUES (1,1,10),(2,1,11);'''
    psql(setup)
    first = None
    try:
        first = subprocess.Popen(["psql", DSN, "-X", "-qAt", "-v", "ON_ERROR_STOP=1", "-c",
            f"SET application_name='claim_test_{schema}'; SET search_path TO {schema}, pg_catalog; "
            "BEGIN; SELECT (ship_claim(1,20)).code; SELECT pg_sleep(3); COMMIT;"],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if psql(f"SELECT count(*) FROM pg_stat_activity WHERE application_name='claim_test_{schema}' AND wait_event='PgSleep'") == "1":
                break
            if first.poll() is not None:
                raise AssertionError(first.communicate())
            time.sleep(0.05)
        else:
            raise AssertionError("first transaction did not reach its lock-holding barrier")
        second = psql(f"SET search_path TO {schema}, pg_catalog; SELECT (ship_claim(2,20)).code")
        stdout, stderr = first.communicate(timeout=5)
        assert first.returncode == 0, stderr
        assert stdout.splitlines()[0] == "0"
        assert second == "409"
        assert psql(f"SELECT count(*) FROM {schema}.players WHERE ship_id=20") == "1"
        assert psql(f"SELECT count(*) FROM {schema}.ship_ownership WHERE ship_id=20 AND player_id=1 AND is_primary") == "1"
        assert psql(f"SELECT count(*) FROM {schema}.ship_ownership WHERE ship_id=10 AND player_id=1 AND is_primary") == "0"
    finally:
        if first and first.poll() is None:
            first.kill()
            first.communicate()
        psql(f"DROP SCHEMA {schema} CASCADE")
