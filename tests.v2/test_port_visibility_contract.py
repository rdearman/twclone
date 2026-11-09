"""Static guardrails for port-information visibility side effects.

These checks validate source ordering only. They are not live server or
PostgreSQL gameplay evidence.
"""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class PortVisibilityContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = (ROOT / "src/server_ports.c").read_text(encoding="utf-8")
        start = source.index("int\ncmd_trade_port_info")
        end = source.index("int\ncmd_trade_jettison", start)
        cls.body = source[start:end]

    def test_port_knowledge_is_written_after_all_response_building(self):
        knowledge = self.body.index("repo_players_record_port_knowledge")
        commodity_query = self.body.index("db_ports_get_commodities")
        payload = self.body.index("payload = json_object ()")
        self.assertGreater(knowledge, commodity_query)
        self.assertGreater(knowledge, payload)

    def test_visit_is_written_only_with_port_knowledge(self):
        knowledge = self.body.index("repo_players_record_port_knowledge")
        visit = self.body.index("repo_players_record_visit")
        self.assertGreater(visit, knowledge)


if __name__ == "__main__":
    unittest.main()
