#!/usr/bin/env python3
"""Static dialect guard for the combat locking query shared by PG/MySQL."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class CombatLockDialectTests(unittest.TestCase):
    def test_combat_query_keeps_nullable_lookup_outside_locking_query_block(self):
        repo = (ROOT / "src/db/repo/repo_combat.c").read_text()
        start = repo.index("int db_combat_load_ship_full_locked")
        end = repo.index("int db_combat_load_ship_full_unlocked", start)
        query = repo[start:end]

        self.assertIn("sql_for_update_of (db, \"s, st, op\"", query)
        self.assertIn("SKIP LOCKED", query)
        self.assertIn("LEFT JOIN corp_members cm", query)

    def test_native_dialect_lock_clauses_are_preserved(self):
        driver = (ROOT / "src/db/sql_driver.c").read_text()
        mysql_case = driver[driver.index("case DB_BACKEND_MYSQL:", driver.index("sql_for_update_of")):]
        self.assertIn('snprintf (out_buf, out_sz, " FOR UPDATE OF %s", tables)', mysql_case)
        self.assertGreaterEqual(driver.count('return " FOR UPDATE SKIP LOCKED";'), 2)


if __name__ == "__main__":
    unittest.main()
