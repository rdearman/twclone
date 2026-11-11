import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class StarPortSectorAndPtaContract(unittest.TestCase):
    def test_both_base_schemas_use_unique_sector_index(self):
        for backend in ("pg", "my"):
            sql = (ROOT / "sql" / backend / "010_indexes.sql").read_text()
            self.assertRegex(
                sql,
                r"CREATE\s+UNIQUE\s+INDEX\s+idx_ports_loc\s+ON\s+ports\s*\(\s*sector_id\s*\)",
            )

    def test_upgrade_migrations_refuse_to_discard_duplicate_ports(self):
        pg = (ROOT / "sql/pg/180_one_port_per_sector.sql").read_text()
        mysql = (ROOT / "sql/my/180_one_port_per_sector.sql").read_text()
        for sql in (pg, mysql):
            self.assertIn("GROUP BY sector_id", sql)
            self.assertIn("HAVING COUNT(*) > 1", sql)
            self.assertIn("Cannot enforce one StarPort per sector", sql)
            self.assertIn("idx_ports_loc", sql)

        manifest = json.loads((ROOT / "sql/pg/migration_manifest.json").read_text())
        names = {entry["name"] for entry in manifest["scripts"] if entry["required"]}
        self.assertIn("180_one_port_per_sector.sql", names)

    def test_bigbang_places_stardock_and_orion_only_in_empty_sectors(self):
        pg = (ROOT / "sql/pg/040_functions.sql").read_text()
        mysql = (ROOT / "sql/my/040_functions.sql").read_text()
        for sql in (pg, mysql):
            self.assertIn("NOT EXISTS (SELECT 1 FROM ports p WHERE p.sector_id = s.sector_id)", sql)
            self.assertIn("NOT EXISTS (SELECT 1 FROM ports p WHERE p.sector_id = lt.exit_sector)", sql)
        self.assertIn("No eligible empty sector available for Stardock", pg)
        self.assertIn("No eligible empty sector available for Stardock", mysql)
        self.assertIn("Orion Black Market Dock", pg)
        self.assertIn("Orion Black Market", mysql)

    def test_pta_suite_exercises_settlement_retry_and_refusals(self):
        suite = json.loads((ROOT / "tests.v2/suite_planet_trade_port_pta.json").read_text())
        commands = [case.get("command") for case in suite["tests"]]
        self.assertIn("planet.trade.port", commands)
        self.assertGreaterEqual(commands.count("planet.trade.port"), 4)
        self.assertTrue(any("conservation" in case.get("name", "").lower() for case in suite["tests"]))

        source = (ROOT / "src/server_planets.c").read_text()
        handler = source.split("cmd_planet_trade_port (", 1)[1].split("\nint\ncmd_planet_market_buy_order", 1)[0]
        for required in (
            "db_tx_begin", "db_ports_lock_for_update", "h_planet_market_lock_control",
            "db_ports_decrement_stock", "h_update_port_stock", "db_ports_insert_idemp_sell",
            "db_tx_commit", "db_tx_rollback", "turns_consumed",
        ):
            self.assertIn(required, handler)


if __name__ == "__main__":
    unittest.main()
