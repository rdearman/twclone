"""Guardrails for transwarp refusal ordering and its command regression suite."""
from pathlib import Path
import json
import unittest


ROOT = Path(__file__).resolve().parents[1]


class TranswarpServerContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = (ROOT / "src/server_universe.c").read_text(encoding="utf-8")
        cls.handler = source.split("cmd_move_transwarp (", 1)[1].split(
            "\nint\n", 1
        )[0]
        cls.suite = json.loads(
            (ROOT / "tests.v2/suite_movement_move_transwarp.json").read_text(
                encoding="utf-8"
            )
        )
        cls.nav_suite = json.loads(
            (ROOT / "tests.v2/suite_movement_nav.json").read_text(encoding="utf-8")
        )

    def test_destination_validation_precedes_drive_and_turn_mutation(self):
        max_check = self.handler.index("repo_universe_get_max_sector_id")
        drive_check = self.handler.index("repo_universe_check_transwarp")
        tx_begin = self.handler.index("db_tx_begin")
        homing_check = self.handler.index("repo_universe_lock_sector_fighter")
        turn_debit = self.handler.index("h_consume_player_turn")
        self.assertLess(max_check, drive_check)
        self.assertLess(drive_check, turn_debit)
        self.assertLess(tx_begin, homing_check)
        self.assertLess(homing_check, turn_debit)
        self.assertIn("Failed to validate transwarp destination", self.handler)
        self.assertIn("Failed to verify transwarp capability", self.handler)
        self.assertIn("ERR_SECTOR_NOT_FOUND", self.handler)
        self.assertIn("Transwarp requires a fighter in the destination sector.", self.handler)

    def test_integration_suites_assert_refusal_without_turn_debit(self):
        transwarp_names = [t.get("name") for t in self.suite["tests"]]
        self.assertIn(
            "Invalid destination preserves turns and location", transwarp_names
        )
        invalid = next(
            t for t in self.suite["tests"]
            if t.get("name") == "Negative: Out-of-universe transwarp destination is refused"
        )
        self.assertEqual(invalid["expect"], {"status": "refused", "error_code": 1700})

        nav_names = [t.get("name") for t in self.nav_suite["tests"]]
        self.assertIn("Missing transwarp equipment preserves turns", nav_names)

    def test_transwarp_is_synchronous_and_commits_movement_with_entry_hazards(self):
        tx_begin = self.handler.index("db_tx_begin")
        turn_debit = self.handler.index("h_consume_player_turn")
        relocation = self.handler.index("repo_universe_move_player_and_ship")
        hazards = self.handler.index("server_combat_apply_entry_hazards_report")
        commit = self.handler.index("db_tx_commit", hazards)
        self.assertLess(tx_begin, turn_debit)
        self.assertLess(turn_debit, relocation)
        self.assertLess(relocation, hazards)
        self.assertLess(hazards, commit)
        self.assertIn("transwarp_complete", self.handler)
        self.assertNotIn("interrupted", self.handler.lower())


if __name__ == "__main__":
    unittest.main()
