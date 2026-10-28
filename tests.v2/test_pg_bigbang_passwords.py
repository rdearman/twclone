"""Static guard for BigBang's canonical V2 password seed paths."""

from pathlib import Path
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.argon2id_passwords import is_valid_encoded, load_argon2
def _seed_records(path):
    """Return account-name/hash pairs from registration calls and NPC rows."""
    source = path.read_text(encoding="utf-8")
    records = dict(
        re.findall(
            r"register_player\s*\(\s*'([^']+)'\s*,\s*'([^']*)'",
            source,
            flags=re.IGNORECASE,
        )
    )
    records.update(
        dict(
            re.findall(
                r"now\(\)\s*,\s*'([^']+)'\s*,\s*'([^']*)'\s*,\s*1\s*,",
                source,
                flags=re.IGNORECASE,
            )
        )
    )
    return records


class BigBangPasswordSeedTest(unittest.TestCase):
    def test_canonical_sql_contains_hash_migration_after_player_seeds(self):
        self.assertTrue((ROOT / "sql/pg/119_password_hash_enforcement.sql").is_file())
        self.assertTrue((ROOT / "sql/pg/095_player_registration.sql").is_file())
        self.assertTrue((ROOT / "src/bigbang_pg_main.c").is_file())
        migration = (ROOT / "sql/pg/119_password_hash_enforcement.sql").read_text()
        self.assertIn("players_passwd_argon2id_check", migration)

    def test_every_seeded_password_is_a_valid_argon2id_hash(self):
        expected = {
            "pg": {
                "System",
                "Federation Administrator",
                "newguy",
                "ai_qa_bot",
                "Zydras, Heavy Fighter Captain",
                "Krell, Scout Captain",
                "Vex, Contraband Captain",
                "Jaxx, Smuggler Captain",
                "Sira, Market Guard Captain",
                "Fer",
            },
            "my": {
                "System",
                "Federation Administrator",
                "Zydras, Heavy Fighter Captain",
                "Krell, Scout Captain",
                "Vex, Contraband Captain",
                "Jaxx, Smuggler Captain",
                "Sira, Market Guard Captain",
                "Fer",
            },
        }
        api = load_argon2()
        for backend in ("pg", "my"):
            with self.subTest(backend=backend):
                seed_path = ROOT / f"sql/{backend}/095_player_registration.sql"
                records = _seed_records(seed_path)
                self.assertEqual(set(records), expected[backend])
                self.assertEqual(len(set(records.values())), len(records))
                for account, encoded in records.items():
                    if not is_valid_encoded(api, encoded):
                        self.fail(
                            f"{backend} seed account {account} has an invalid password hash"
                        )

    def test_bigbang_does_not_insert_player_passwords_itself(self):
        source = (ROOT / "src/bigbang_pg_main.c").read_text(encoding="utf-8")
        self.assertNotRegex(source, r"(?i)INSERT\s+INTO\s+players")
        self.assertIn("load_sql_files_in_order", source)

    def test_ai_qa_bot_is_seeded_as_an_ordinary_player(self):
        source = (ROOT / "sql/pg/095_player_registration.sql").read_text(
            encoding="utf-8"
        )
        self.assertRegex(
            source,
            r"register_player\s*\(\s*'ai_qa_bot'\s*,\s*'\$argon2id\$"
            r"[^']*'\s*,\s*'Used Scout Marauder'\s*,\s*FALSE\s*,\s*1\s*,\s*2\s*\)",
        )


if __name__ == "__main__":
    unittest.main()
