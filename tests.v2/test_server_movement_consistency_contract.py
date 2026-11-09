import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "src/server_universe.c").read_text()


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


if __name__ == "__main__":
    unittest.main()
