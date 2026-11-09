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
        cls.cargo_repo = (ROOT / "src/db/repo/repo_cargo.c").read_text(encoding="utf-8")
        cls.ships_repo = (ROOT / "src/db/repo/repo_ships.c").read_text(encoding="utf-8")
        cls.ports = (ROOT / "src/server_ports.c").read_text(encoding="utf-8")
        cls.universe = (ROOT / "src/server_universe.c").read_text(encoding="utf-8")

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

    def test_cargo_mutations_lock_ship_and_serialize_capacity_checks(self):
        wrapper = self.cargo_repo.split("repo_cargo_add (", 1)[1].split(
            "repo_cargo_sync_legacy_columns", 1
        )[0]
        self.assertIn("db_tx_begin", wrapper)
        self.assertIn("repo_cargo_lock_ship_for_update", wrapper)
        self.assertIn("repo_cargo_add_unlocked", wrapper)
        self.assertIn("SELECT ship_id FROM ships WHERE ship_id = {1} FOR UPDATE",
                      self.cargo_repo)
        lock_pair = self.cargo_repo.split("repo_cargo_lock_ships_for_update (", 1)[1].split(
            "repo_cargo_add (", 1
        )[0]
        self.assertIn("first_ship_id < second_ship_id", lock_pair)

    def test_cargo_transfer_revalidates_active_ship_and_syncs_both_ship_views(self):
        transfer = self.ships.split("cmd_ship_transfer_cargo (", 1)[1].split(
            "\nint\n", 1
        )[0]
        self.assertIn("repo_ships_get_active_id_for_update", transfer)
        self.assertIn("repo_cargo_lock_ships_for_update", transfer)
        self.assertIn("repo_cargo_sync_legacy_columns (db, source_ship_id)", transfer)
        self.assertIn("repo_cargo_sync_legacy_columns (db, target_ship_id)", transfer)
        self.assertIn("SELECT ship_id FROM players WHERE player_id = {1} FOR UPDATE",
                      self.ships_repo)

    def test_two_ship_settlements_take_pair_locks_in_repository_order(self):
        for source, function, end in (
            (self.ports, "trade_offer_settle (", "cmd_trade_accept ("),
            (self.universe, "ferengi_settle_deal (", "ferengi_deal_action ("),
        ):
            body = source.split(function, 1)[1].split(end, 1)[0]
            self.assertIn("repo_cargo_lock_ships_for_update", body)


if __name__ == "__main__":
    unittest.main()
