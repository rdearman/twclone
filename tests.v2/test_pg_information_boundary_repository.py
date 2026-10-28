"""Static regression checks for database information-boundary queries.

Runtime visibility is enforced by the server command layer. These checks keep
the shared PostgreSQL repository helpers from reintroducing unscoped topology
or knowledge writes.
"""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def function_body(source: str, signature: str, next_signature: str) -> str:
    start = source.index(signature)
    end = source.index(next_signature, start)
    return source[start:end]


class InformationBoundaryRepositoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.universe = (ROOT / "src/db/repo/repo_universe.c").read_text()
        cls.players = (ROOT / "src/db/repo/repo_players.c").read_text()

    def test_warp_helper_returns_only_exits_from_current_or_visited_sectors(self):
        body = function_body(
            self.universe,
            "repo_universe_get_player_known_warps(",
            "repo_universe_get_asset_counts(",
        )
        self.assertIn("FROM sector_warps sw", body)
        self.assertIn("sw.from_sector = {2}", body)
        self.assertIn("p.player_id = {1} AND p.sector_id = {2}", body)
        self.assertIn("sh.sector_id = {2}", body)
        self.assertIn("pv.player_id = {1} AND pv.sector_id = sw.from_sector", body)
        self.assertIn("SELECT sw.from_sector, sw.to_sector", body)
        # Outgoing exits of a known sector are visible, but the next sector's
        # exits are withheld until that sector has a visit row.
        self.assertNotRegex(body, r"pv\.sector_id\s*=\s*sw\.to_sector")

    def test_recommended_routes_do_not_load_the_full_universe_graph(self):
        body = function_body(
            self.players,
            "int repo_players_get_recommended_routes(",
            "void repo_players_free_routes(",
        )
        self.assertIn("repo_universe_get_player_known_warps", body)
        self.assertNotIn("repo_universe_get_all_warps", body)

    def test_port_knowledge_write_requires_player_and_ship_in_port_sector(self):
        body = function_body(
            self.players,
            "int repo_players_record_port_knowledge(",
            "int repo_players_record_visit(",
        )
        self.assertIn("JOIN ports po ON po.port_id = {2}", body)
        self.assertIn("p.player_id = {1} AND p.sector_id = po.sector_id", body)
        self.assertIn("sh.sector_id = po.sector_id", body)
        self.assertRegex(body, r"(?s)INSERT INTO player_known_ports.*?SELECT \{1\}, \{2\} WHERE EXISTS")

    def test_sector_visit_write_requires_player_and_ship_in_sector(self):
        body = function_body(
            self.players,
            "int repo_players_record_visit(",
            "db_res_t *repo_players_get_visited_sectors(",
        )
        self.assertIn("p.player_id = {1} AND p.sector_id = {2}", body)
        self.assertIn("sh.sector_id = {2}", body)
        self.assertRegex(body, r"(?s)INSERT INTO player_visited_sectors.*?SELECT \{1\}, \{2\} WHERE EXISTS")


if __name__ == "__main__":
    unittest.main()
