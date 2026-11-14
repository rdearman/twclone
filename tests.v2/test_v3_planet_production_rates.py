import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class V3PlanetProductionRateContractTests(unittest.TestCase):
    def test_versioned_default_rates_cover_each_builtin_class(self):
        migration = (ROOT / "sql/pg/182_v3_planet_production_rates.sql").read_text()
        expected = {
            "M": (100, 100, 100),
            "L": (50, 100, 100),
            "O": (100, 50, 100),
            "K": (50, 100, 100),
            "H": (50, 100, 50),
            "C": (100, 100, 100),
            "U": (0, 0, 0),
        }
        for planet_class, rates in expected.items():
            for commodity, rate in zip(("ORE", "ORG", "EQU"), rates):
                self.assertIn(
                    f"('{commodity}', '{planet_class}', {rate})", migration
                )

    def test_migration_is_manifested_as_required_v3_runtime(self):
        manifest = json.loads(
            (ROOT / "sql/pg/migration_manifest.json").read_text()
        )
        migration = "182_v3_planet_production_rates.sql"
        self.assertIn(migration, manifest["fresh_install"]["v3_runtime"])
        entries = [entry for entry in manifest["scripts"]
                   if entry["name"] == migration]
        self.assertEqual(len(entries), 1)
        self.assertTrue(entries[0]["required"])

    def test_production_tick_uses_persisted_integer_labor_remainders(self):
        source = (ROOT / "src/db/repo/repo_cron.c").read_text()
        self.assertIn("pp.workers_per_unit", source)
        self.assertIn("p.ore_production_remainder", source)
        self.assertIn("p.organics_production_remainder", source)
        self.assertIn("p.equipment_production_remainder", source)
        self.assertIn("next_worker_remainder", source)


if __name__ == "__main__":
    unittest.main()
