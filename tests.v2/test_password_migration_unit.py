import pathlib
import re
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from tools.migrate_passwords_argon2id import load_argon2, migrate_rows


class MemoryCursor:
    def __init__(self, rows):
        self.rows = rows
        self.selected = None

    def execute(self, statement):
        if "SELECT player_id, passwd" not in statement:
            raise AssertionError("unexpected migration SQL")
        self.selected = list(self.rows)

    def fetchall(self):
        return self.selected

    def executemany(self, statement, updates):
        if "UPDATE" not in statement:
            raise AssertionError("unexpected migration SQL")
        by_id = {player_id: password_hash for password_hash, player_id in updates}
        self.rows = [(pid, by_id.get(pid, passwd)) for pid, passwd in self.rows]


class PasswordMigrationUnitTest(unittest.TestCase):
    def test_legacy_accounts_are_converted_and_replay_is_idempotent(self):
        api = load_argon2()
        cursor = MemoryCursor([(1, "BOT"), (2, "player-secret"), (3, "")])
        self.assertEqual(migrate_rows(cursor, api, "fixture"), (3, 3, 0))
        hashes = [passwd for _, passwd in cursor.rows]
        self.assertEqual(len(set(hashes)), 3)
        self.assertEqual(migrate_rows(cursor, api, "fixture"), (3, 0, 3))
        self.assertEqual(api.argon2id_verify(hashes[0].encode(), b"BOT", 3), 0)
        self.assertEqual(api.argon2id_verify(hashes[1].encode(), b"player-secret", len(b"player-secret")), 0)
        self.assertEqual(api.argon2id_verify(hashes[2].encode(), None, 0), 0)

    def test_malformed_hash_stops_conversion(self):
        with self.assertRaises(ValueError):
            migrate_rows(MemoryCursor([(1, "$argon2id$malformed")]), load_argon2(), "fixture")

    def test_postgresql_seed_accounts_are_prehashed_with_unique_salts(self):
        seed_path = pathlib.Path(__file__).resolve().parents[1] / "sql/pg/095_player_registration.sql"
        seed = seed_path.read_text()
        api = load_argon2()
        expected = {
            "System": b"BOT",
            "Federation Administrator": b"BOT",
            "newguy": b"pass123",
            "ai_qa_bot": b"quality",
            "Zydras, Heavy Fighter Captain": b"",
            "Krell, Scout Captain": b"",
            "Vex, Contraband Captain": b"",
            "Jaxx, Smuggler Captain": b"",
            "Sira, Market Guard Captain": b"",
            "Fer": b"",
        }
        records = dict(re.findall(r"register_player \('([^']+)', '(\$argon2id\$[^']+)'", seed))
        records.update(dict(re.findall(r"now\(\), '([^']+)', '(\$argon2id\$[^']+)'", seed)))
        self.assertEqual(set(records), set(expected))
        self.assertEqual(len(set(records.values())), len(expected))
        for name, password in expected.items():
            encoded = records[name].encode("ascii")
            self.assertEqual(api.argon2id_verify(encoded, password or None, len(password)), 0)


if __name__ == "__main__":
    unittest.main()
