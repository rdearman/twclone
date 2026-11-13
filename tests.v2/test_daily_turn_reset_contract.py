import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class DailyTurnResetContractTests(unittest.TestCase):
    def test_handler_uses_the_documented_default_when_unconfigured(self):
        source = (ROOT / "src/server_cron.c").read_text()
        handler = source.split("h_daily_turn_reset (", 1)[1].split(
            "h_autouncloak_sweeper (", 1
        )[0]
        self.assertIn('h_get_config_int_unlocked (db, "turnsperday", 120)', handler)

    def test_repository_uses_the_resolved_turn_value(self):
        source = (ROOT / "src/db/repo/repo_cron.c").read_text()
        reset = source.split("db_cron_reset_daily_turns (", 1)[1].split(
            "db_cron_autouncloak_ships (", 1
        )[0]
        self.assertIn("turns <= 0", reset)
        self.assertIn('"UPDATE turns SET turns_remaining = {1}"', reset)
        self.assertIn("db_bind_i32 (turns)", reset)
        self.assertNotIn("SELECT value FROM config", reset)

    def test_disposable_docker_bigbang_seeds_turns_per_day(self):
        config = json.loads((ROOT / "docker/bigbang.beta.json").read_text())
        self.assertEqual(config["turnsperday"], 120)


if __name__ == "__main__":
    unittest.main()
