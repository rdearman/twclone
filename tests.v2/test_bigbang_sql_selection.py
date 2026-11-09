"""Regression coverage for BigBang's canonical PostgreSQL SQL selection."""

from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest
import json
import shutil

ROOT = Path(__file__).resolve().parents[1]
BIGBANG = ROOT / "bin/bigbang"
SOURCE = ROOT / "src/bigbang_pg_main.c"
SAFE_ADMIN = "host=127.0.0.1 port=1 dbname=postgres connect_timeout=1"
SAFE_APP = "host=127.0.0.1 port=1 dbname=%DB% connect_timeout=1"
SAFE_DB = "twclone_bigbang_preflight_only"


def selection_inventory():
    manifest = json.loads((ROOT / "sql/pg/migration_manifest.json").read_text())
    return {
        entry["name"]: (entry["required"], entry["v3_only"], entry["diagnostic"])
        for entry in manifest["scripts"]
    }


class BigBangSqlSelectionTest(unittest.TestCase):
    def test_canonical_directory_is_classified_and_v2_is_default(self):
        inventory = selection_inventory()
        disk = {path.name for path in (ROOT / "sql/pg").glob("*.sql")}
        self.assertEqual(set(inventory), disk)
        self.assertTrue(all(required for required, _, _ in inventory.values()))
        v3_names = {name for name, (_, v3, _) in inventory.items() if v3}
        self.assertIn("115_v3_xp_tax_persistence.sql", v3_names)
        self.assertIn("116_player_event_journal.sql", v3_names)
        self.assertIn("159_ship_ownership_role_invariant.sql", v3_names)
        self.assertNotIn("119_password_hash_enforcement.sql", v3_names)
        self.assertTrue(inventory["119_password_hash_enforcement.sql"][0])
        self.assertEqual(sum(1 for _, (_, _, diagnostic) in inventory.items() if diagnostic), 15)
        ordered = sorted(inventory, key=lambda name: (int(name.split("_", 1)[0]), name))
        self.assertLess(ordered.index("114_planet_fighter_production.sql"),
                        ordered.index("119_password_hash_enforcement.sql"))
        self.assertIn("load_sql_manifest", SOURCE.read_text(encoding="utf-8"))
        self.assertIn('"include-v3-migrations"', SOURCE.read_text(encoding="utf-8"))

    def test_abandoned_hulls_are_player_owned_classes_and_idempotent(self):
        sql = (ROOT / "sql/pg/040_functions.sql").read_text(encoding="utf-8")
        function = sql[sql.index("CREATE OR REPLACE FUNCTION spawn_initial_fleet"):sql.index("CREATE OR REPLACE FUNCTION apply_game_defaults")]
        self.assertIn("WHERE enabled = TRUE AND can_purchase = TRUE", function)
        self.assertIn("NOT EXISTS (", function)
        self.assertIn("FROM ship_ownership", function)
        self.assertIn("FROM sectors", function)
        repair = (ROOT / "sql/pg/168_v3_abandoned_hull_repair.sql").read_text(encoding="utf-8")
        self.assertIn("enabled = TRUE AND can_purchase = TRUE", repair)
        self.assertIn("pg_advisory_xact_lock", repair)
        self.assertIn("UPDATE players SET ship_id", repair)
        self.assertIn("INSERT INTO ship_ownership", repair)

    def test_missing_required_script_fails_before_database_connection(self):
        if not BIGBANG.is_file():
            self.skipTest("build bin/bigbang before running selection CLI regressions")
        with tempfile.TemporaryDirectory(prefix="twclone-bigbang-missing-") as tmp:
            empty = Path(tmp) / "sql"
            empty.mkdir()
            shutil.copy(ROOT / "sql/pg/migration_manifest.json", empty / "migration_manifest.json")
            result = subprocess.run(
                [str(BIGBANG), "--admin", SAFE_ADMIN, "--app", SAFE_APP,
                 "--db", SAFE_DB, "--sql-dir", str(empty)],
                cwd=tmp, text=True, capture_output=True, timeout=10,
                env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"},
            )
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("required V2 SQL script is missing: 000_tables.sql", result.stderr)

    def test_unknown_migration_fails_closed(self):
        if not BIGBANG.is_file():
            self.skipTest("build bin/bigbang before running selection CLI regressions")
        with tempfile.TemporaryDirectory(prefix="twclone-bigbang-unknown-") as tmp:
            sql_dir = Path(tmp) / "sql"
            sql_dir.mkdir()
            shutil.copy(ROOT / "sql/pg/migration_manifest.json", sql_dir / "migration_manifest.json")
            (sql_dir / "117_future.sql").write_text("-- test only\n", encoding="utf-8")
            result = subprocess.run(
                [str(BIGBANG), "--admin", SAFE_ADMIN, "--app", SAFE_APP,
                 "--db", SAFE_DB, "--sql-dir", str(sql_dir)],
                cwd=tmp, text=True, capture_output=True, timeout=10,
                env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"},
            )
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("unrecognized SQL script '117_future.sql'", result.stderr)


if __name__ == "__main__":
    unittest.main()
