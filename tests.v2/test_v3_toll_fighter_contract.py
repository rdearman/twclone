import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class V3TollFighterContractTests(unittest.TestCase):
    def test_migration_records_offers_and_split_payees(self):
        sql = (ROOT / "sql/pg/184_v3_toll_passage.sql").read_text()
        for table in ("toll_passage_offers", "toll_passage_payees"):
            self.assertIn(f"CREATE TABLE IF NOT EXISTS {table}", sql)
        manifest = json.loads((ROOT / "sql/pg/migration_manifest.json").read_text())
        self.assertIn("184_v3_toll_passage.sql", manifest["fresh_install"]["v3_runtime"])

    def test_fighter_damage_requires_refused_toll(self):
        source = (ROOT / "src/server_combat.c").read_text()
        self.assertIn("modes[i] == OFFENSE_TOLL", source)
        self.assertIn("ctx->toll_paid_sector_id == sector_id", source)
        movement = (ROOT / "src/server_universe.c").read_text()
        self.assertIn("toll_passage_offers", movement)
        self.assertIn("h_deduct_player_petty_cash_unlocked", movement)
        self.assertIn("h_add_player_petty_cash_unlocked", movement)

    def test_python_warp_flow_prompts_and_resumes_offer(self):
        source = (ROOT / "client/python_client/client.py").read_text()
        self.assertIn("Accept the toll and continue? [y/N]", source)
        self.assertIn('"toll_offer_id": offer["offer_id"]', source)
        self.assertIn('"toll_accept": accept', source)


if __name__ == "__main__":
    unittest.main()
