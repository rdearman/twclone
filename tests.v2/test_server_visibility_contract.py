"""Static server guardrails for visibility refusal side effects.

These checks validate source ordering only. They are not live server or
PostgreSQL gameplay evidence.
"""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class ServerVisibilityContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = (ROOT / "src/server_universe.c").read_text(encoding="utf-8")
        cls.server_source = source
        start = source.index("int\ncmd_move_scan")
        end = source.index("int\ncmd_sector_mine_disrupt", start)
        cls.scan_body = source[start:end]

    def test_move_scan_builds_detailed_payload_before_charging_turn(self):
        payload = self.scan_body.index("build_sector_scan_json")
        turn_check = self.scan_body.index("TurnConsumeResult tc")
        self.assertLess(payload, turn_check)
        self.assertIn("json_decref (payload)", self.scan_body)

    def test_density_handler_owns_visibility_and_response_validation(self):
        density_start = self.server_source.index("void\ncmd_sector_scan_density")
        density_end = self.server_source.index("int\nh_warp_exists", density_start)
        density_body = self.server_source[density_start:density_end]
        self.assertIn("sector_is_current_or_adjacent", density_body)
        self.assertIn('"sector.density.scan"', density_body)

    def test_sector_scan_redacts_port_class(self):
        """Port class is trading intelligence, not generic scan output."""
        start = self.server_source.index("json_t *\nbuild_sector_scan_json")
        end = self.server_source.index("static int\nsector_is_current_or_adjacent", start)
        scan_builder = self.server_source[start:end]
        self.assertIn('json_object_del (port, "type")', scan_builder)
        self.assertIn("through sector scans", scan_builder)

    def test_scan_fails_closed_on_visibility_query_errors(self):
        """A DB visibility error must not produce a partial scan payload."""
        start = self.server_source.index("json_t *\nbuild_sector_scan_json")
        end = self.server_source.index("static int\nsector_is_current_or_adjacent", start)
        scan_builder = self.server_source[start:end]

        for query in (
            "db_adjacent_sectors_json",
            "db_ships_at_sector_json",
            "db_ports_at_sector_json",
            "db_planets_at_sector_json",
        ):
            query_pos = scan_builder.index(query)
            refusal = scan_builder.index("json_decref (root);", query_pos)
            self.assertLess(query_pos, refusal, query)

    def test_sector_scan_checks_database_before_active_ship_lookup(self):
        start = self.server_source.index("void\ncmd_sector_scan")
        end = self.server_source.index("void\ncmd_sector_scan_density", start)
        body = self.server_source[start:end]
        self.assertLess(body.index("if (!db)"),
                        body.index("h_get_active_ship_id"))

    def test_sector_description_validates_context_before_database_access(self):
        start = self.server_source.index("int\ncmd_move_describe_sector")
        end = self.server_source.index("/* Validate that a warp link exists", start)
        body = self.server_source[start:end]
        self.assertLess(body.index("if (!ctx || !root)"),
                        body.index("game_db_get_handle"))


if __name__ == "__main__":
    unittest.main()
