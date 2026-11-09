"""Static parity checks for the MySQL Argon2id storage contract."""

from pathlib import Path
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.argon2id_passwords import is_valid_encoded, load_argon2
from tools.migrate_passwords_argon2id_mysql import migrate_rows


class FakeCursor:
    def __init__(self, rows):
        self.rows = list(rows)
        self.updates = []

    def execute(self, sql):
        if "FOR UPDATE" not in sql:
            raise AssertionError("MySQL conversion must lock rows")

    def fetchall(self):
        return list(self.rows)

    def executemany(self, sql, parameters):
        self.updates.extend(parameters)
        replacements = {player_id: passwd for passwd, player_id in parameters}
        self.rows = [
            (player_id, replacements.get(player_id, passwd))
            for player_id, passwd in self.rows
        ]


class MySQLPasswordHashStaticTest(unittest.TestCase):
    def test_player_column_uses_ascii_encoded_hash_storage(self):
        schema = (ROOT / "sql/my/000_tables.sql").read_text()
        self.assertRegex(
            schema,
            r"passwd\s+VARCHAR\(255\)\s+CHARACTER SET ascii\s+COLLATE ascii_bin\s+NOT NULL",
        )

    def test_mysql_seed_passwords_are_argon2id_with_unique_salts(self):
        seed = (ROOT / "sql/my/095_player_registration.sql").read_text()
        rows = re.findall(
            r"\(1,\s*1,\s*now\(\),\s*'[^']*',\s*'([^']*)',\s*1,",
            seed,
        )
        self.assertEqual(len(rows), 6)
        self.assertTrue(all(value.startswith("$argon2id$v=19$") for value in rows))
        self.assertEqual(len(set(rows)), len(rows))
        api = load_argon2()
        self.assertTrue(all(is_valid_encoded(api, value) for value in rows))

    def test_mysql_bootstrap_accounts_match_postgresql_seed(self):
        mysql_seed = (ROOT / "sql/my/095_player_registration.sql").read_text()
        pg_seed = (ROOT / "sql/pg/095_player_registration.sql").read_text()
        accounts = ("System", "Federation Administrator")
        for account in accounts:
            with self.subTest(account=account):
                mysql_match = re.search(
                    rf"'{re.escape(account)}',\s*'([^']+)'", mysql_seed
                )
                pg_match = re.search(
                    rf"register_player\s*\('{re.escape(account)}',\s*'([^']+)'",
                    pg_seed,
                )
                self.assertIsNotNone(mysql_match)
                self.assertIsNotNone(pg_match)
                self.assertEqual(mysql_match.group(1), pg_match.group(1))

        self.assertIn("CREATE PROCEDURE seed_bootstrap_players()", mysql_seed)
        self.assertIn("CALL seed_bootstrap_players()", mysql_seed)
        self.assertIn("DROP PROCEDURE seed_bootstrap_players", mysql_seed)

    def test_migration_enforces_hash_only_and_is_replay_guarded(self):
        migration = (ROOT / "sql/my/119_password_hash_enforcement.sql").read_text()
        self.assertIn("MODIFY passwd VARCHAR(255) CHARACTER SET ascii COLLATE ascii_bin", migration)
        self.assertIn("information_schema.table_constraints", migration)
        self.assertIn("players_passwd_argon2id_check", migration)
        self.assertIn("CHECK (passwd REGEXP", migration)

    def test_mysql_converter_uses_shared_argon2id_and_transactional_row_locking(self):
        converter = (ROOT / "tools/migrate_passwords_argon2id_mysql.py").read_text()
        self.assertIn("argon2id_passwords", converter)
        self.assertIn("FOR UPDATE", converter)
        self.assertIn("conn.start_transaction()", converter)
        self.assertIn("conn.rollback()", converter)
        self.assertIn("information_schema.processlist", converter)

    def test_mysql_converter_hashes_replays_and_rejects_malformed_values(self):
        api = load_argon2()
        cursor = FakeCursor([(1, "legacy-system"), (2, "legacy-player"), (3, "")])
        self.assertEqual(migrate_rows(cursor, api), (3, 3, 0))
        values = [passwd for _, passwd in cursor.rows]
        self.assertTrue(all(is_valid_encoded(api, value) for value in values))
        self.assertEqual(len(set(values)), 3)
        self.assertEqual(migrate_rows(cursor, api), (3, 0, 3))
        self.assertEqual(len(cursor.updates), 3)

        malformed = FakeCursor([(1, "legacy-not-written"), (2, "$argon2id$broken")])
        with self.assertRaises(ValueError):
            migrate_rows(malformed, api)
        self.assertEqual(malformed.updates, [])


if __name__ == "__main__":
    unittest.main()
