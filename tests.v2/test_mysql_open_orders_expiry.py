#!/usr/bin/env python3
"""Static checks for the MySQL commodity order expiry schema parity fix."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


class MySqlOpenOrderExpirySchemaTests(unittest.TestCase):
    def test_fresh_schema_matches_postgres_nullable_expiry(self):
        mysql = (ROOT / "sql/my/000_tables.sql").read_text()
        postgres = (ROOT / "sql/pg/000_tables.sql").read_text()

        mysql_table = re.search(
            r"CREATE TABLE commodity_orders\s*\((.*?)\n\);", mysql, re.S
        )
        pg_table = re.search(
            r"CREATE TABLE commodity_orders\s*\((.*?)\n\);", postgres, re.S
        )
        self.assertIsNotNone(mysql_table)
        self.assertIsNotNone(pg_table)
        self.assertRegex(mysql_table.group(1), r"(?im)^\s*expires_at\s+TIMESTAMP\s+NULL,?\s*$")
        self.assertRegex(pg_table.group(1), r"(?im)^\s*expires_at\s+timestamptz\s*$")

    def test_upgrade_adds_column_only_when_missing(self):
        migration = (ROOT / "sql/my/118_commodity_order_expiry.sql").read_text()
        self.assertIn("TABLE_NAME = 'commodity_orders'", migration)
        self.assertIn("COLUMN_NAME = 'expires_at'", migration)
        self.assertRegex(
            migration,
            r"ALTER TABLE commodity_orders ADD COLUMN expires_at TIMESTAMP NULL",
        )
        self.assertIn("'SELECT 1'", migration)


if __name__ == "__main__":
    unittest.main()
