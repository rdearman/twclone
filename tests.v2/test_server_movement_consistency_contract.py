import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "src/server_universe.c").read_text()
SCHEMAS = (ROOT / "src/schemas.c").read_text()


class MovementConsistencyContractTest(unittest.TestCase):
    def test_warp_uses_authoritative_player_ship_move(self):
        start = SOURCE.index("cmd_move_warp")
        end = SOURCE.index("cmd_move_pathfind", start)
        handler = SOURCE[start:end]
        self.assertIn("repo_universe_move_player_and_ship", handler)
        self.assertIn("player_ship_id, to", handler)
        self.assertNotIn("db_player_set_sector", handler)

    def test_transwarp_uses_authoritative_player_ship_move(self):
        start = SOURCE.index("cmd_move_transwarp")
        end = SOURCE.index("cmd_ferengi_traders", start)
        handler = SOURCE[start:end]
        self.assertIn("repo_universe_move_player_and_ship", handler)
        self.assertIn("ship_id,\n                                          to_sector_id", handler)
        self.assertNotIn("db_player_set_sector", handler)

    def test_pathfind_loads_persisted_avoids_and_visible_exit_graph(self):
        start = SOURCE.index("cmd_move_pathfind")
        end = SOURCE.index("static void\nattach_sector_asset_counts", start)
        handler = SOURCE[start:end]
        self.assertIn("repo_players_get_avoids", handler)
        self.assertIn("avoid[avoided_sector] = 1", handler)
        self.assertIn("repo_universe_get_player_known_warps", handler)
        self.assertIn("known[v] = 1", handler)
        self.assertIn("avoid[v] || seen[v] || !known[v]", handler)

    def test_describe_sector_request_matches_optional_protocol_argument(self):
        start = SCHEMAS.index("schema_move_describe_sector (void)\n{")
        end = SCHEMAS.index("schema_move_scan (void)\n{", start)
        schema = SCHEMAS[start:end]
        self.assertIn('"required", json_array ()', schema)
        self.assertNotIn('json_string ("sector_id")', schema)


if __name__ == "__main__":
    unittest.main()
