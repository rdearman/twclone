#!/usr/bin/env python3
"""Static guards for V3 XP/tax persistence contracts and migration layout."""

import hashlib
import json
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class V3PersistenceContractTests(unittest.TestCase):
    def test_migration_adds_idempotent_progress_ledger(self):
        migration = (ROOT / "sql/pg/115_v3_xp_tax_persistence.sql").read_text()
        self.assertIn("CREATE TABLE IF NOT EXISTS player_progress_adjustments", migration)
        self.assertIn("source_key varchar(255) NOT NULL UNIQUE", migration)
        self.assertIn("REFERENCES players (player_id)", migration)
        self.assertIn("formula_version integer", migration)
        self.assertNotRegex(migration, r"(?i)(UPDATE|DELETE)\s+(commission|players)")

    def test_migration_has_safe_global_tax_default(self):
        migration = (ROOT / "sql/pg/115_v3_xp_tax_persistence.sql").read_text()
        self.assertIn("CREATE TABLE IF NOT EXISTS planet_tax_global_config", migration)
        self.assertIn("CHECK (config_id = 1)", migration)
        self.assertIn("VALUES (1, 0, FALSE)", migration)
        self.assertIn("ON CONFLICT (config_id) DO NOTHING", migration)

    def test_existing_commission_rows_are_unchanged(self):
        seed = (ROOT / "sql/pg/091_seed_essential.sql").read_text()
        rows = re.findall(
            r"\((\d+),\s*(TRUE|FALSE),\s*(\d+),\s*'([^']+)'\)", seed
        )
        normalized = sorted(
            (int(i), evil == "TRUE", int(xp), name)
            for i, evil, xp, name in rows
        )
        serialized = "\n".join(
            f"{i}|{int(evil)}|{xp}|{name}"
            for i, evil, xp, name in normalized
        ).encode()
        self.assertEqual(len(normalized), 44)
        self.assertEqual(
            hashlib.sha256(serialized).hexdigest(),
            "7e3bc4e1a35c939413474f1b4a81fbc6dc2932eb40173f877ad6b8c2bd502715",
        )

    def test_tax_cron_reads_global_configuration(self):
        repo = (ROOT / "src/db/repo/repo_cron.c").read_text()
        tax_tick = repo.split("db_cron_planet_tax_tick (", 1)[1]
        self.assertIn("planet_tax_global_config", tax_tick)
        self.assertNotIn("planet_tax_policies", tax_tick)
        self.assertIn("corp_tx", tax_tick)
        self.assertIn("UPDATE players p SET credits=p.credits+assessed.tax_amount", tax_tick)

    def test_regression_suites_are_valid_json_and_use_global_tax(self):
        tax_suite = json.loads((ROOT / "tests.v2/suite_planet_taxation_e2e.json").read_text())
        xp_suite = json.loads((ROOT / "tests.v2/suite_v3_xp_persistence.json").read_text())
        self.assertTrue(tax_suite["tests"])
        self.assertTrue(xp_suite["tests"])
        serialized = json.dumps(tax_suite)
        self.assertIn("planet_tax_global_config", serialized)
        self.assertIn("player_progress_adjustments", json.dumps(xp_suite))
        self.assertIn("engine_offset", json.dumps(xp_suite))

    def test_event_consumer_uses_persisted_event_identity(self):
        consumer = (ROOT / "src/engine_consumer.c").read_text()
        fetch_repo = (ROOT / "src/db/repo/repo_engine_consumer.c").read_text()
        handler = (ROOT / "src/server_engine.c").read_text()
        progress_repo = (ROOT / "src/db/repo/repo_cmd.c").read_text()
        self.assertIn('"source_event_id", json_integer (event_id)', consumer)
        self.assertIn("json_array_elements_text({3})", fetch_repo)
        self.assertIn('"engine_event:%" PRId64', handler)
        self.assertIn("db_player_apply_progress_event", handler)
        self.assertIn("ON CONFLICT (source_key) DO NOTHING", progress_repo)


if __name__ == "__main__":
    unittest.main()
