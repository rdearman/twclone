"""Regression coverage for BigBang's canonical PostgreSQL SQL selection."""

from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BIGBANG = ROOT / "bin/bigbang"
SOURCE = ROOT / "src/bigbang_pg_main.c"
SAFE_ADMIN = "host=127.0.0.1 port=1 dbname=postgres connect_timeout=1"
SAFE_APP = "host=127.0.0.1 port=1 dbname=%DB% connect_timeout=1"
SAFE_DB = "twclone_bigbang_preflight_only"


def selection_inventory():
    source = SOURCE.read_text(encoding="utf-8")
    block = source.split("static const bigbang_sql_script_t k_bigbang_sql_scripts[] = {", 1)[1]
    block = block.split("};", 1)[0]
    return {
        name: required == "1"
        for name, required in re.findall(
            r'\{"([^\"]+\.sql)",\s*([01])\}', block
        )
    }


class BigBangSqlSelectionTest(unittest.TestCase):
    def test_canonical_directory_is_v2_only_and_ordered(self):
        inventory = selection_inventory()
        disk = {path.name for path in (ROOT / "sql/pg").glob("*.sql")}
        self.assertEqual(set(inventory), disk)
        self.assertTrue(all(inventory.values()))
        self.assertIn("119_password_hash_enforcement.sql", inventory)
        ordered = sorted(inventory, key=lambda name: (int(name.split("_", 1)[0]), name))
        self.assertLess(ordered.index("114_planet_fighter_production.sql"),
                        ordered.index("119_password_hash_enforcement.sql"))
        source = SOURCE.read_text(encoding="utf-8")
        self.assertNotIn("include-v3-migrations", source)
        self.assertNotIn("115_v3_xp_tax_persistence.sql", source)
        self.assertNotIn("116_player_event_journal.sql", source)


    def test_missing_required_script_fails_before_database_connection(self):
        if not BIGBANG.is_file():
            self.skipTest("build bin/bigbang before running selection CLI regressions")
        with tempfile.TemporaryDirectory(prefix="twclone-bigbang-missing-") as tmp:
            empty = Path(tmp) / "sql"
            empty.mkdir()
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
            (sql_dir / "115_v3_xp_tax_persistence.sql").write_text("-- test only\n", encoding="utf-8")
            result = subprocess.run(
                [str(BIGBANG), "--admin", SAFE_ADMIN, "--app", SAFE_APP,
                 "--db", SAFE_DB, "--sql-dir", str(sql_dir)],
                cwd=tmp, text=True, capture_output=True, timeout=10,
                env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"},
            )
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("unrecognized SQL script '115_v3_xp_tax_persistence.sql'", result.stderr)


if __name__ == "__main__":
    unittest.main()
