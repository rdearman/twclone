"""Static protocol guardrails for the ship lifecycle command contracts.

These assertions cover schema/dispatcher wiring only. They do not claim that
the current stub handlers provide sale or transfer gameplay semantics.
"""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class ShipLifecycleProtocolContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.schemas_h = (ROOT / "src/schemas.h").read_text(encoding="utf-8")
        cls.schemas_c = (ROOT / "src/schemas.c").read_text(encoding="utf-8")
        cls.loop = (ROOT / "src/server_loop.c").read_text(encoding="utf-8")
        cls.ships = (ROOT / "src/server_ships.c").read_text(encoding="utf-8")

    def test_lifecycle_schema_functions_are_declared_and_registered(self):
        for name in ("schema_ship_sell", "schema_ship_transfer",
                     "schema_shipyard_sell"):
            self.assertIn(f"json_t *{name} (void);", self.schemas_h)
            self.assertIn(f"{name} (void)", self.schemas_c)

    def test_lifecycle_dispatchers_do_not_use_placeholder_schema(self):
        expected = {
            '"ship.sell"': "schema_ship_sell",
            '"ship.transfer"': "schema_ship_transfer",
            '"shipyard.sell"': "schema_shipyard_sell",
        }
        for command, schema in expected.items():
            start = self.loop.index(command)
            entry = self.loop[start:start + 180]
            self.assertIn(schema, entry)
            self.assertNotIn("schema_placeholder", entry)

    def test_transfer_schema_requires_both_ship_and_recipient(self):
        start = self.schemas_c.index("json_t *\nschema_ship_transfer (void)")
        end = self.schemas_c.index("schema_player_get_prefs (void)", start)
        body = self.schemas_c[start:end]
        self.assertIn('"ship_id"', body)
        self.assertIn('"target_player_id"', body)
        self.assertIn('schema_nav_require (required, "ship_id")', body)
        self.assertIn('schema_nav_require (required, "target_player_id")', body)

    def test_ship_status_uses_auth_and_database_errors_not_unrelated_codes(self):
        start = self.ships.index("cmd_ship_status (client_ctx_t *ctx")
        end = self.ships.index("cmd_ship_info_compat", start)
        body = self.ships[start:end]
        self.assertIn("ERR_NOT_AUTHENTICATED", body)
        self.assertIn("ERR_SERVICE_UNAVAILABLE", body)
        self.assertNotIn("ERR_SECTOR_NOT_FOUND", body)


if __name__ == "__main__":
    unittest.main()
