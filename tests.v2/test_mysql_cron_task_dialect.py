#!/usr/bin/env python3
"""Guard the native cron-task insert syntax for both SQL backends."""

from pathlib import Path
import unittest


SOURCE = (Path(__file__).resolve().parents[1] / "src/db/repo/repo_engine.c").read_text()


class MysqlCronTaskDialectTests(unittest.TestCase):
    def test_cron_registration_uses_portable_select_before_insert(self):
        self.assertIn("SELECT 1 FROM cron_tasks WHERE name = {1} LIMIT 1", SOURCE)
        self.assertIn("INSERT INTO cron_tasks (name, schedule, last_run_at, next_due_at, enabled, payload)", SOURCE)
        self.assertIn("if (err.code == ERR_DB_CONSTRAINT)", SOURCE)

    def test_repository_has_no_dialect_specific_upsert_syntax(self):
        self.assertNotIn("ON CONFLICT", SOURCE)
        self.assertNotIn("ON DUPLICATE KEY", SOURCE)


if __name__ == "__main__":
    unittest.main()
