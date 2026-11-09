"""Server contract checks for ship-transfer refusal/error classification.

These checks are source-level guardrails only; they do not claim live socket
or PostgreSQL gameplay evidence.
"""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class ShipTransferErrorContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = (ROOT / "src" / "server_ships.c").read_text(encoding="utf-8")
        start = source.index("cmd_ship_transfer")
        cls.handler = source[start:source.index("h_update_ship_cargo", start)]

    def test_zero_rows_is_refusal_only_after_successful_repository_call(self):
        self.assertIn("if (rc == 0 && rows == 0)", self.handler)
        self.assertNotIn("|| rows == 0)", self.handler)

    def test_repository_failure_is_reported_as_database_error(self):
        self.assertIn('else if (rc != 0)', self.handler)
        self.assertIn('ERR_DB_QUERY_FAILED', self.handler)
        self.assertIn('db_tx_rollback (db, &err)', self.handler)


if __name__ == "__main__":
    unittest.main()
