"""Static guardrails for the real command-driven #471 regression suite."""
from pathlib import Path
import json
import unittest

ROOT = Path(__file__).resolve().parents[1]


class SectorEntryServerContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.combat = (ROOT / "src/server_combat.c").read_text(encoding="utf-8")
        cls.ship_repo = (ROOT / "src/db/repo/repo_ships.c").read_text(encoding="utf-8")
        cls.engine_consumer = (ROOT / "src/engine_consumer.c").read_text(encoding="utf-8")
        cls.universe = (ROOT / "src/server_universe.c").read_text(encoding="utf-8")
        cls.suite = json.loads(
            (ROOT / "tests.v2/suite_combat_sector_hazards.json").read_text(
                encoding="utf-8"
            )
        )

    def test_warp_and_transwarp_invoke_entry_resolution(self):
        self.assertIn("server_combat_apply_entry_hazards_report", self.universe)
        self.assertEqual(self.universe.count("server_combat_apply_entry_hazards_report"), 2)
        self.assertIn('"current_sector_id"', self.universe)

    def test_entry_resolution_propagates_errors_and_stops_on_destruction(self):
        body = self.combat.split("server_combat_apply_entry_hazards_single", 1)[1]
        self.assertIn("quasar_rc < 0", body)
        self.assertIn("mine_rc < 0", body)
        self.assertIn("fighters_rc < 0", body)
        self.assertIn("apply_limpet_mines_on_entry", body)
        self.assertIn("environment_rc < 0", body)
        destroy = self.combat.split(
            "static int\ndestroy_ship_in_transaction", 1
        )[1].split("int\nh_decloak_ship", 1)[0]
        self.assertIn("handle_ship_destruction (db, &kill_ctx)", destroy)
        self.assertIn("KILL_CAUSE_OTHER", destroy)
        self.assertNotIn("db_destroy_ship", destroy)
        self.assertIn("ctx->sector_id = 1", self.combat)

    def test_all_destruction_uses_shared_pod_settlement(self):
        self.assertIn("handle_ship_destruction (db, &kill_ctx)", self.combat)
        self.assertIn("handle_ship_destruction (db, &kill_ctx)", self.engine_consumer)
        self.assertIn("WHERE ship_id={1} AND destroyed=FALSE", self.ship_repo)
        self.assertIn("INSERT INTO ships (name,type_id,sector_id,holds,fighters,shields,", self.ship_repo)
        self.assertIn('"ship-destruction-%lld"', self.ship_repo)
        self.assertIn("DELETE FROM ship_ownership WHERE ship_id={1}", self.ship_repo)
        self.assertIn("settle_destruction_insurance", self.ship_repo)
        self.assertIn("insurance_funds SET balance=balance-{1}", self.ship_repo)
        self.assertIn("insurance-auto-%lld-%lld", self.ship_repo)
        self.assertIn("insurance_claims SET status='paid'", self.ship_repo)

    def test_hazard_suite_uses_movement_and_suite_owned_assets(self):
        names = [test.get("name", "") for test in self.suite["tests"]]
        commands = [test.get("command") for test in self.suite["tests"]]
        self.assertIn("move.warp", commands)
        self.assertIn("move.transwarp", commands)
        self.assertIn("Rig: Place hostile mine, fighter, and limpet", names)
        self.assertIn("Verify attached limpet persists on victim ship", names)
        self.assertIn("Verify mine damage event persistence", names)
        self.assertIn("Verify fighter damage event persistence", names)
        self.assertIn("Verify limpet attachment event persistence", names)
        self.assertIn("Verify all hostile sector assets were consumed", names)
        self.assertIn("Refused warp to unlinked sector changes no state", names)
        self.assertIn("Refused warp consumes no turns", names)
        self.assertIn("Successful warp charges its normal single turn", names)
        self.assertIn("Successful transwarp charges its normal single turn", names)
        inventory = next(
            test for test in self.suite["tests"]
            if test.get("name") == "Verify all hostile sector assets were consumed"
        )
        self.assertIn("NOT EXISTS (SELECT 1 FROM sector_assets", inventory["data"]["sql"])
        encoded = json.dumps(self.suite)
        self.assertNotIn("555", encoded)
        self.assertIn("combat.hit.fighters", encoded)
        self.assertIn("combat.limpet.attached", encoded)


if __name__ == "__main__":
    unittest.main()
