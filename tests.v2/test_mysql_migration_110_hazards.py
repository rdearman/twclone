#!/usr/bin/env python3
"""Static parity guard for the MySQL sector-hazards base schema/migration."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
BASE = (ROOT / "sql/my/000_tables.sql").read_text()
MIGRATION = (ROOT / "sql/my/110_sector_environmental_hazards.sql").read_text()


class MysqlHazardMigrationTests(unittest.TestCase):
    def test_fresh_install_and_upgrade_define_same_table(self):
        def table_body(sql):
            match = re.search(
                r"CREATE TABLE(?: IF NOT EXISTS)? sector_hazards\s*\((.*?)\);",
                sql,
                re.IGNORECASE | re.DOTALL,
            )
            self.assertIsNotNone(match)
            return re.sub(r"\s+", " ", match.group(1)).lower()

        self.assertEqual(table_body(BASE), table_body(MIGRATION))

    def test_table_keeps_constraints_index_and_mysql_native_types(self):
        definition = re.sub(r"\s+", " ", MIGRATION).lower()
        self.assertIn("sector_id bigint not null", definition)
        self.assertIn("hazard_type varchar(16) character set ascii collate ascii_bin not null", definition)
        self.assertIn("severity int not null", definition)
        self.assertIn("primary key (sector_id, hazard_type)", definition)
        self.assertIn("key idx_sector_hazards_type (hazard_type, sector_id)", definition)
        self.assertIn("references sectors (sector_id) on delete cascade", definition)
        self.assertIn("hazard_type in ('volcanic', 'nebula', 'radiation')", definition)
        self.assertIn("severity between 1 and 1000", definition)
        self.assertIn("create table if not exists sector_hazards", definition)


if __name__ == "__main__":
    unittest.main()
