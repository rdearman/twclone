"""Static regression checks for the V3 faction roster migration.

The database-backed population tests exercise the same migration on an isolated
PostgreSQL database.  These checks are intentionally dependency-free so the
catalogue cannot regress during a source-only validation.
"""

import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MIGRATION = ROOT / "sql/pg/167_v3_faction_rosters_and_ferengi_classes.sql"


def test_manifest_registers_faction_roster_migration_once():
    manifest = json.loads((ROOT / "sql/pg/migration_manifest.json").read_text())
    names = [entry["name"] for entry in manifest["scripts"]]
    assert names.count(MIGRATION.name) == 1
    assert MIGRATION.name in manifest["fresh_install"]["v3_runtime"]


def test_ferengi_classes_have_approved_values_and_are_npc_only():
    sql = MIGRATION.read_text()
    expected = {
        "Ferengi Trade Master": (90000, 1000, 60, 250, 3000, 4, 5, 2, 2000, 6, 8, 20),
        "Ferengi Cruiser": (115000, 3000, 30, 120, 8000, 3, 15, 3, 3000, 12, 12, 30),
        "Ferengi Interceptor": (75000, 2500, 10, 35, 4000, 1, 5, 0, 1500, 18, 7, 5),
        "Ferengi Dreadnought": (350000, 12000, 15, 75, 40000, 8, 50, 5, 10000, 20, 20, 100),
    }
    for name, values in expected.items():
        assert name in sql
        assert all(str(value) in sql for value in values)
    assert "('Ferengi Warship'" not in sql  # canonical row is retained by seed
    assert "can_purchase, has_escape_pod, enabled" in sql
    assert "FALSE, FALSE, TRUE)" in sql


def test_explicit_roster_sizes_and_distribution_boundaries():
    sql = MIGRATION.read_text()
    ferengi_values = sql.split("INSERT INTO ferengi_v3_names", 1)[1].split(
        "WITH existing", 1
    )[0]
    orion_values = sql.split("INSERT INTO orion_v3_names", 1)[1].split(
        "WITH generated", 1
    )[0]
    assert len(re.findall(r"\(\d+,'[^']+(?:''[^']+)*'\)", ferengi_values)) == 250
    assert len(re.findall(r"\(\d+,'[^']+(?:''[^']+)*'\)", orion_values)) == 100
    assert "ceil(r.total * 40 / 100.0)" in sql
    assert "ceil(r.total * 70 / 100.0)" in sql
    assert "ceil(r.total * 86 / 100.0)" in sql
    assert "ceil(r.total * 96 / 100.0)" in sql


def test_existing_ids_and_resources_are_not_deleted_by_migration():
    sql = MIGRATION.read_text()
    assert "DELETE FROM" not in sql
    assert "DROP TABLE" not in sql
    assert "UPDATE players p SET name" in sql
    assert "UPDATE ships s SET type_id" in sql
    assert "COALESCE(cg.quantity,0) <= COALESCE(st.maxholds,0)" in sql
