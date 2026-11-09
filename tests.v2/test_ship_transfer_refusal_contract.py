"""Server-side SHIP-02 refusal mapping contract.

This is a static contract test only.  It does not claim socket or PostgreSQL
gameplay acceptance; the live two-player transfer scenario remains pending.
"""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class ShipTransferRefusalContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = (ROOT / "src" / "server_ships.c").read_text(encoding="utf-8")

    def _handler_body(self):
        start = self.source.index("\ncmd_ship_transfer (" ) + 1
        return self.source[start:self.source.index("\nh_update_ship_cargo", start)]

    def test_domain_refusal_is_not_reported_as_database_failure(self):
        body = self._handler_body()
        self.assertIn("repo_ships_transfer_primary", body)
        self.assertIn("rc == ERR_TARGET_INVALID || rc == ERR_INVALID_ARG || rows == 0", body)
        refusal = body.index("send_response_refused_steal")
        db_failure = body.index("send_response_error", refusal)
        self.assertLess(refusal, db_failure)

    def test_transfer_still_uses_transaction_and_repository_boundary(self):
        body = self._handler_body()
        self.assertIn("db_tx_begin (db, DB_TX_IMMEDIATE", body)
        self.assertIn("db_tx_rollback (db, &err)", body)
        self.assertIn("db_tx_commit (db, &err)", body)


if __name__ == "__main__":
    unittest.main()
