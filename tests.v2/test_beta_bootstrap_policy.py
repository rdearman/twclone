"""Release bootstrap must not publish development logins or usable SysOps."""

from pathlib import Path
import re
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from argon2id_passwords import load_argon2


class BetaBootstrapPolicyTest(unittest.TestCase):
    def test_beta_turns_and_universe_size_are_explicit(self):
        import json

        config = json.loads((ROOT / "docker/bigbang.beta.json").read_text())
        self.assertEqual(config["sectors"], 2500)
        self.assertEqual(config["turnsperday"], 200)
        self.assertTrue(config["exact_sector_count"])
        self.assertEqual(config["startingcredits"], 5000)
        self.assertEqual(config["startingfighters"], 10)
        self.assertEqual(config["startingholds"], 20)
        self.assertEqual(config["max_planets_per_sector"], 6)
        self.assertEqual(config["max_total_planets"], 3000)
        self.assertEqual(config["max_citadel_level"], 6)
        self.assertEqual(config["genesis_enabled"], 1)
        self.assertEqual(config["genesis_block_at_cap"], 1)

    def test_beta_has_no_inline_database_passwords(self):
        import json

        config = json.loads((ROOT / "docker/bigbang.beta.json").read_text())
        for key in ("admin", "app"):
            self.assertNotRegex(config[key], r"(?i)(password|passwd)\s*=")
        self.assertIn("passfile=", config[key])

    def test_beta_listener_is_tls_only_loopback_published_and_runtime_is_separate(self):
        compose = (ROOT / "compose.beta.yaml").read_text()
        self.assertIn("TWCLONE_TLS_ENABLED: 1", compose)
        self.assertIn("TWCLONE_TLS_REQUIRED: 1", compose)
        self.assertIn("user=twclone_runtime", compose)
        self.assertIn('${TWCLONE_BETA_HOST_ADDRESS:-127.0.0.1}:${TWCLONE_BETA_HOST_PORT:-12483}:12483', compose)
        self.assertIn('group_add: ["10002"]', compose)
        self.assertEqual((ROOT / "docker/postgres/10-runtime-role.sh").stat().st_mode & 0o111, 0o111)
        self.assertIn("max-size: \"20m\"", compose)

    def test_development_login_seeds_are_opted_out_by_beta_migration(self):
        seed = (ROOT / "sql/pg/095_player_registration.sql").read_text()
        compose = (ROOT / "compose.beta.yaml").read_text()
        self.assertIn("twclone.provision_dev_accounts", seed)
        self.assertIn("twclone.provision_dev_accounts=off", compose)
        self.assertIn("name = 'newguy'", seed)
        self.assertIn("name = 'ai_qa_bot'", seed)

    def test_sysop_lock_hash_is_argon2id_and_not_seed_password(self):
        result = subprocess.run(
            [sys.executable, str(ROOT / "tools/lock_seed_sysop_password.py")],
            check=True,
            capture_output=True,
            text=True,
        )
        encoded = result.stdout.strip()
        self.assertRegex(encoded, r"^\$argon2id\$v=19\$m=65536,t=3,p=1\$")
        api = load_argon2()
        self.assertNotEqual(
            api.argon2id_verify(encoded.encode("ascii"), b"BOT", 3), 0
        )
        self.assertNotIn("BOT", encoded)
        self.assertNotIn("Federation Administrator", result.stdout)

    def test_turn_grant_tool_is_not_part_of_public_server_protocol(self):
        server = (ROOT / "src/server_loop.c").read_text()
        self.assertNotIn('"playtest.turns.grant"', server)
        self.assertNotIn('"debug.turns.grant"', server)


if __name__ == "__main__":
    unittest.main()
