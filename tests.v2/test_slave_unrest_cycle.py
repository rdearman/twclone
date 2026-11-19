import unittest
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


class SlaveUnrestCycleTests(unittest.TestCase):
    def test_increments_decrements_clamps_and_probability_boundaries(self):
        with tempfile.TemporaryDirectory(prefix="twclone-unrest-cycle-") as temp:
            binary = Path(temp) / "unrest-cycle-test"
            subprocess.run(
                [os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Werror",
                 "-I", str(ROOT / "src"),
                 str(ROOT / "tests.v2/test_slave_unrest_cycle.c"),
                 "-o", str(binary)], check=True, capture_output=True, text=True
            )
            subprocess.run([str(binary)], check=True)

    def test_schema_adds_bounded_state_and_per_tick_idempotency(self):
        pg = (ROOT / "sql/pg/190_v3_slave_unrest_production_cycles.sql").read_text()
        self.assertIn("slave_unrest_level BETWEEN 0 AND 100", pg)
        self.assertIn("PRIMARY KEY (planet_id, production_tick)", pg)
        self.assertIn("disruption_probability BETWEEN 0 AND 10", pg)
        self.assertIn("disruption_roll BETWEEN 0 AND 99", pg)
        mysql = (ROOT / "sql/my/132_v3_slave_unrest_production_cycles.sql").read_text()
        self.assertIn("slave_unrest_level BETWEEN 0 AND 100", mysql)
        self.assertIn("PRIMARY KEY (planet_id, production_tick)", mysql)

    def test_authoritative_production_tick_persists_unrest_atomically(self):
        source = (ROOT / "src/db/repo/repo_cron.c").read_text()
        production = source.split("db_cron_planet_update_production_stock (", 1)[1]
        production = production.split("db_cron_apply_slave_unrest (", 1)[0]
        self.assertIn("slave_unrest_cycle_evaluate", production)
        self.assertIn("planet_slave_production_events", production)
        self.assertIn("production_last_tick", production)
        self.assertIn("db_tx_rollback", production)
        self.assertIn("!unrest_cycle.disrupted", production)

    def test_pop05_daily_handler_remains_separate(self):
        source = (ROOT / "src/db/repo/repo_cron.c").read_text()
        pop05 = source.split("db_cron_apply_slave_unrest (", 1)[1]
        self.assertIn("planet_slave_unrest_events", pop05)
        self.assertIn("applied_population_loss", pop05)
        self.assertIn("slave_unrest_stoppage_until", pop05)

    def test_postgresql_migration_is_registered(self):
        import json
        manifest = json.loads((ROOT / "sql/pg/migration_manifest.json").read_text())
        self.assertIn("190_v3_slave_unrest_production_cycles.sql",
                      manifest["fresh_install"]["v3_runtime"])

    def test_approved_daily_policy_is_migrated_on_both_sql_backends(self):
        pg = (ROOT / "sql/pg/192_v3_approved_slave_unrest_rules.sql").read_text()
        mysql = (ROOT / "sql/my/134_v3_approved_slave_unrest_rules.sql").read_text()
        for migration in (pg, mysql):
            self.assertIn("configured_population_loss_percent", migration)
            self.assertIn("BETWEEN 0 AND 100", migration)
            self.assertIn("planet.slave_unrest_interval_days", migration)
            self.assertIn("planet.slave_unrest_stoppage_cycles", migration)
            self.assertIn("planet.slave_unrest_population_loss_percent", migration)
            self.assertIn("'1'", migration)
        self.assertIn("legacy_configured_population_loss", pg)
        self.assertIn("legacy_configured_population_loss", mysql)


if __name__ == "__main__":
    unittest.main()
