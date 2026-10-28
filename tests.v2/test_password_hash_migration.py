"""Isolated-schema integration checks for the legacy password converter."""

import os
import pathlib
import sys
import unittest
import uuid

import psycopg2

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from tools.migrate_passwords_argon2id import load_argon2, migrate_rows


class PasswordMigrationTest(unittest.TestCase):
    def test_conversion_preservation_replay_and_malformed_value(self):
        dsn = os.environ.get("TEST_DATABASE_URL")
        if not dsn:
            self.skipTest("TEST_DATABASE_URL is not set to a disposable PostgreSQL database")
        with psycopg2.connect(dsn) as conn:
            with conn.cursor() as cur:
                cur.execute("SELECT current_database()")
                if not cur.fetchone()[0].startswith("twclone_password_test_"):
                    self.fail("TEST_DATABASE_URL must name an isolated twclone_password_test_* database")
                schema = "pwtest_" + uuid.uuid4().hex
                cur.execute(f'CREATE SCHEMA "{schema}"')
                cur.execute(
                    f'CREATE TABLE "{schema}".players '
                    "(player_id integer PRIMARY KEY, passwd text NOT NULL)"
                )
                cur.executemany(
                    f'INSERT INTO "{schema}".players VALUES (%s, %s)',
                    [(1, "BOT"), (2, "legacy-player-password"), (3, "")],
                )
                api = load_argon2()
                self.assertEqual(migrate_rows(cur, api, schema), (3, 3, 0))
                cur.execute(f'SELECT passwd FROM "{schema}".players ORDER BY player_id')
                hashes = [row[0] for row in cur.fetchall()]
                self.assertTrue(all(value.startswith("$argon2id$") for value in hashes))
                self.assertEqual(len(set(hashes)), 3)
                self.assertEqual(migrate_rows(cur, api, schema), (3, 0, 3))

                cur.execute(
                    f'INSERT INTO "{schema}".players VALUES (%s, %s)',
                    (4, "$argon2id$broken"),
                )
                with self.assertRaises(ValueError):
                    migrate_rows(cur, api, schema)
                cur.execute(f'DROP SCHEMA "{schema}" CASCADE')


if __name__ == "__main__":
    unittest.main()
